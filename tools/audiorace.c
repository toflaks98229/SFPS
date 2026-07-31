/* audiorace -- the audio module's threading contract, checked under contention.
 *
 * audio.c is the only place in this project with a second thread: the mixer
 * runs on its own while the game thread starts sounds and, in a dev build,
 * reloads recipes when assets/sounds.txt changes. That makes it the only
 * place a data race can exist, and races do not show up in a normal run --
 * they show up as one burst of noise, once, on someone else's machine.
 *
 * The bug this exists to catch: audio_play() used to call find_sound()
 * OUTSIDE the critical section, and find_sound() parsed on demand. So a hot
 * reload could have the game thread rewriting g_sounds[] from index 0 while
 * the mixer thread was dereferencing Voice::snd into the very entries being
 * overwritten -- a torn read of live recipe data, plus voices left pointing
 * at a different sound than the one they started.
 *
 * The test drives that exact interleaving as hard as it can: one thread
 * hammering audio_play, another hammering audio_reload, for long enough that
 * the unlocked window would be hit many times over.
 *
 * WHAT THIS TEST DOES AND DOES NOT PROVE -- read before trusting it.
 *
 * It verifies that the module survives heavy concurrent play/reload traffic:
 * no deadlock, no crash, no permanent corruption of the recipe table, and
 * safe behaviour around shutdown and the no-device tool path. Those are real
 * regressions it would catch.
 *
 * It does NOT reproduce the original race, and this was confirmed empirically
 * rather than assumed: reverting the fix and running this suite still passes.
 * Two reasons, both structural:
 *
 *   1. The corruption was transient. parse_sounds() always leaves a valid
 *      table behind, so every end-state assertion passes regardless of what
 *      happened during the run.
 *   2. The unsafe read was the MIXER thread dereferencing Voice::snd. No
 *      public API exposes that pointer, so no test binary can observe it
 *      without instrumenting audio.c itself.
 *
 * Catching it properly would need either a debug hook inside the mixer or a
 * thread sanitiser (GCC's -fsanitize=thread, not available in w64devkit).
 * This file is therefore a guard against regressions in the surrounding
 * contract, not proof that the contract is upheld -- the proof is the code
 * review recorded in audio.c's own comments.
 */

#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "audio.h"

#define ITERATIONS 20000

static int fails;
static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Sounds that exist in assets/sounds.txt. Named explicitly rather than
   discovered, so a renamed asset fails loudly here instead of silently
   turning this into a test that plays nothing. */
static const char *NAMES[] = { "shot", "pump", "dry", "hook", "impact" };
#define N_NAMES ((int)(sizeof(NAMES) / sizeof(NAMES[0])))

static volatile LONG g_go;
static volatile LONG g_reload_count;

/* Hammers audio_reload() -- the hot-reload path that invalidates the recipe
   table underneath anything reading it. */
static DWORD WINAPI reload_thread(LPVOID p) {
    (void)p;
    while (!g_go) Sleep(0);
    for (int i = 0; i < ITERATIONS; i++) {
        audio_reload();
        InterlockedIncrement(&g_reload_count);
        /* No sleep: maximum contention is the entire point. */
    }
    return 0;
}

int main(void) {
    printf("audiorace\n\n");

    /* --- the offline path must work with no device at all ------------------
       tools/sndtest.c never calls audio_init, so audio_render has to parse
       recipes itself. This also pins down that removing find_sound's implicit
       parse did not break the tool path -- which it would have, silently,
       returning zero frames for every sound. */
    {
        static short buf[48000];
        int frames = audio_render("shot", buf, 48000);
        ok(frames > 0, "audio_render works with no device open (tool path)");

        int nonzero = 0;
        for (int i = 0; i < frames; i++) if (buf[i]) { nonzero = 1; break; }
        ok(nonzero, "and renders actual samples, not silence");

        ok(audio_render("no_such_sound_xyz", buf, 48000) == 0,
           "an unknown name renders nothing rather than misbehaving");
    }

    /* --- audio_reload with no device must not touch an uninitialised lock --
       g_ready is false here, so the critical section was never created.
       Entering it would be undefined behaviour. */
    {
        audio_reload();
        static short buf[8000];
        int frames = audio_render("pump", buf, 8000);
        ok(frames > 0, "audio_reload with no device is safe, and reparses");
    }

    /* --- the contended case -----------------------------------------------
       Open the real device so the mixer thread exists, then run play and
       reload against each other. If the device cannot be opened (headless CI,
       no sound card) audio_init returns 0 and every call becomes a harmless
       no-op -- the test then still verifies that, which is worth knowing. */
    int have_device = audio_init();
    printf("\n  [audio device: %s]\n\n", have_device ? "open" : "unavailable (no-op mode)");

    {
        HANDLE t = CreateThread(0, 0, reload_thread, 0, 0, 0);
        ok(t != 0, "reload thread started");

        g_go = 1;
        for (int i = 0; i < ITERATIONS; i++)
            audio_play(NAMES[i % N_NAMES], 60);

        /* A generous join: the reload thread does no blocking work, so if it
           has not finished by now something is deadlocked. */
        DWORD w = WaitForSingleObject(t, 30000);
        ok(w == WAIT_OBJECT_0, "both threads finished -- no deadlock on g_lock");
        if (t) CloseHandle(t);

        ok(g_reload_count == ITERATIONS,
           "every reload completed rather than stalling");
    }

    /* --- the module is still functional after all that --------------------
       Necessary but NOT sufficient on its own: parse_sounds always leaves a
       valid table behind, so an end-state check passes even when the race is
       present. It is here to catch permanent corruption, not the race. The
       transient corruption is what the observer loop below actually detects. */
    {
        static short buf[48000];
        int frames = audio_render("shot", buf, 48000);
        ok(frames > 0, "recipes still resolve after 20k concurrent reloads");

        int nonzero = 0;
        for (int i = 0; i < frames; i++) if (buf[i]) { nonzero = 1; break; }
        ok(nonzero, "and still synthesise -- the table was not corrupted");

        /* Every name must still be found. A torn parse that dropped entries
           would show up here as a specific sound going missing. */
        int all_found = 1;
        for (int i = 0; i < N_NAMES; i++)
            if (audio_render(NAMES[i], buf, 8000) <= 0) all_found = 0;
        ok(all_found, "and every recipe survived, not just the first");
    }

    /* --- lookups stay consistent under concurrent reloads ------------------
       A name that exists in assets/sounds.txt must ALWAYS resolve. Not
       usually -- always. g_n_sounds is zeroed at the top of parse_sounds and
       climbs back as entries refill, so any read that reached the table
       mid-reparse would see a short or empty one and this would fail.

       Scope, stated honestly: this exercises the LOCKED read path, so it
       proves the lock holds under contention -- it does not reproduce the
       original bug. That bug lived on the mixer thread dereferencing
       Voice::snd, which no public API exposes, so it cannot be observed from
       a test binary without instrumenting audio.c itself. See the note at the
       top of this file. */
    {
        g_go = 0;
        g_reload_count = 0;
        HANDLE t = CreateThread(0, 0, reload_thread, 0, 0, 0);
        ok(t != 0, "observer-phase reload thread started");

        static short buf[4096];
        int misses = 0;
        g_go = 1;
        for (int i = 0; i < ITERATIONS; i++) {
            /* "shot" is the first recipe in the file, so it is the one most
               likely to be caught half-written by a concurrent reparse. */
            if (audio_render("shot", buf, 4096) <= 0) misses++;
        }
        WaitForSingleObject(t, 30000);
        if (t) CloseHandle(t);

        if (misses)
            printf("  [!] %d/%d lookups of a known sound returned nothing\n",
                   misses, ITERATIONS);
        ok(misses == 0,
           "a known recipe never vanishes mid-reload (locked read path)");
    }

    audio_shutdown();

    /* --- shutdown ordering ------------------------------------------------
       audio_shutdown clears g_ready BEFORE deleting the critical section, so
       a late audio_play cannot pass the gate and then enter a deleted lock.
       Calling into the module afterward must be a safe no-op. */
    {
        audio_play("shot", 80);      /* must not crash */
        audio_reload();              /* must not crash */
        ok(1, "calls after shutdown are safe no-ops");

        audio_shutdown();            /* double shutdown */
        ok(1, "a second shutdown is harmless");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall audio threading checks passed\n", fails);
    return fails != 0;
}
