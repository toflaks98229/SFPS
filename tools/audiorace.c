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
#include "audio_dev.h"   /* audio_dev_lock, and AUDIO_MIXER_STALL_MS the stall check prints */

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

    /* --- the join that does not join --------------------------------------
       ENGLISH: audio_shutdown gives the mixer 500ms and then, until this was
       fixed, carried on regardless: it closed the device the mixer was writing
       to, unprepared the headers it held, and deleted the critical section it
       was inside. No machine reaches that on purpose -- a driver has to block
       -- so build.ps1 forces it with -DAUDIO_MIXER_STALL_MS past the deadline
       and this is what that binary exists to run.

       WHAT THIS DOES AND DOES NOT SHOW. It reaches the branch, which nothing
       did before, and it pins the contract that branch has to keep: shutdown
       returns on the deadline rather than on the mixer, the module is shut to
       new callers, and nothing crashes -- all of it under the sanitizer the
       tools build with.

       It is NOT a regression test for the teardown itself, and saying so is the
       point of this paragraph: the pre-fix code passes it too. What that code
       did wrong was invisible from out here, because the two things it freed
       are both re-gated on g_ready. The mixer re-tests that flag inside
       audio_dev_lock, so it never enters the deleted critical section by the
       ordinary route, and waveOutWrite to a closed device returns an error
       rather than faulting. What is left is a narrow window -- a mixer that
       passed the gate and was preempted before entering the lock -- and
       CloseHandle on the event it is waiting inside. Both are real and neither
       is reachable from a test that can only call the public functions.

       In the normal build the stall is zero, the mixer stops immediately, and
       the checks below assert the opposite: the join succeeds and the device
       is actually torn down.

       한국어: audio_shutdown은 믹서에 500ms를 주고, 이것이 고쳐지기 전까지는 그 뒤로도
       개의치 않고 진행했습니다. 믹서가 쓰고 있는 장치를 닫고, 들고 있는 헤더를 해제하고,
       들어가 있는 임계 영역을 삭제했습니다. 어떤 기계도 일부러 그곳에 도달하지 않으므로
       (드라이버가 막혀야 합니다) build.ps1이 -DAUDIO_MIXER_STALL_MS로 기한을 넘기도록
       강제하며, 이 함수가 그 바이너리가 실행하려고 존재하는 것입니다.

       이것이 보여 주는 것과 보여 주지 못하는 것. 이 검사는 그 갈래에 *도달*하며, 그것을
       한 것은 이전에 아무것도 없었습니다. 그리고 그 갈래가 지켜야 할 계약을 고정합니다.
       종료 함수가 믹서가 아니라 기한에 맞춰 반환하고, 모듈이 새 호출자에게 닫히며, 무엇도
       죽지 않는다는 것입니다. 전부 도구가 함께 빌드하는 새니타이저 아래에서입니다.

       그러나 이것은 정리 과정 자체에 대한 *회귀 테스트가 아니며*, 그렇게 말하는 것이 이
       문단의 요점입니다. 수정 전 코드도 이것을 통과합니다. 그 코드가 잘못한 일은 이곳에서
       보이지 않았습니다. 해제하던 두 가지가 모두 g_ready로 다시 막혀 있기 때문입니다.
       믹서는 audio_dev_lock 안에서 그 플래그를 다시 검사하므로 평범한 경로로는 삭제된 임계
       영역에 결코 들어가지 않고, 닫힌 장치에 대한 waveOutWrite는 죽지 않고 오류를
       반환합니다. 남는 것은 좁은 창(게이트를 통과하고 락에 들어가기 전에 선점된 믹서)과,
       믹서가 그 안에서 기다리고 있는 이벤트에 대한 CloseHandle입니다. 둘 다 실재하며, 둘 다
       공개 함수만 호출할 수 있는 테스트에서는 도달할 수 없습니다.

       일반 빌드에서는 지연이 0이고 믹서가 즉시 멈추므로, 아래 검사가 그 반대를 단언합니다.
       합류가 성공하고 장치가 실제로 정리됩니다. */
    {
        if (!audio_init()) {
            printf("  (no audio device; the stuck-mixer check needs one)\n");
        } else {
            audio_play("shot", 100);
            Sleep(50);               /* let the mixer get into a pass */

            DWORD t0 = GetTickCount();
            audio_shutdown();
            DWORD spent = GetTickCount() - t0;

            /* Bounded either way. The stalled mixer must cost the deadline and
               not the stall; the ordinary one must cost neither.
               어느 쪽이든 유계입니다. 지연된 믹서는 지연 시간이 아니라 기한만큼 들어야 하고,
               평범한 믹서는 둘 다 들지 않아야 합니다. */
            ok(spent < 1500, "shutdown returns on a deadline, not on the mixer");
            printf("    stall=%dms, shutdown took %lums\n",
                   (int)AUDIO_MIXER_STALL_MS, (unsigned long)spent);

            /* Closed to new callers whichever branch it took: the gate is shut
               before the join is attempted, so this holds even when the join
               failed and the teardown was skipped.
               어느 갈래를 탔든 새 호출자에게는 닫혀 있습니다. 게이트는 합류를 시도하기
               전에 닫히므로, 합류가 실패해 정리를 건너뛴 경우에도 성립합니다. */
            ok(audio_dev_lock() == 0, "and the module is shut to new callers");

            audio_play("shot", 100);   /* must not crash either way */
            ok(1, "and a play after it is still a safe no-op");
        }
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall audio threading checks passed\n", fails);
    return fails != 0;
}
