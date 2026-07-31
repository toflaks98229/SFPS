/* sndtest -- render every sound recipe and report what came out.
 *
 * There is no way to eyeball a waveform the way you can eyeball a model, and
 * "I can't hear anything" is a terrible bug report. This runs the game's own
 * synth offline and prints numbers that say whether a recipe actually made
 * sound, how long it lasted, and whether its frequency sweep is really
 * sweeping -- plus a WAV per sound to listen to.
 *
 *   sndtest              report on every sound
 *   sndtest shot         one sound
 *   sndtest -wav         also write build/snd_<name>.wav
 */

#include "../src/audio.h"
#include "../src/data.h"
#include "../src/txt.h"

#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MAX_FRAMES (44100 * 3)
static short g_buf[MAX_FRAMES];

static void write_wav(const char *name, const short *pcm, int frames, int rate) {
    char path[MAX_PATH];
    wsprintfA(path, "build\\snd_%s.wav", name);

    FILE *f = fopen(path, "wb");
    if (!f) { printf("    (could not write %s)\n", path); return; }

    int data_bytes = frames * 2;
    int riff = 36 + data_bytes;
    int fmt_len = 16, byte_rate = rate * 2;
    short one = 1, chans = 1, align = 2, bits = 16;

    fwrite("RIFF", 1, 4, f);        fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);    fwrite(&fmt_len, 4, 1, f);
    fwrite(&one, 2, 1, f);          fwrite(&chans, 2, 1, f);
    fwrite(&rate, 4, 1, f);         fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f);        fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);        fwrite(&data_bytes, 4, 1, f);
    fwrite(pcm, 2, frames, f);
    fclose(f);
    printf("    wrote %s\n", path);
}

/* Zero crossings per second, windowed. A falling rate across the windows is
   the audible pitch sweep; a flat rate means the sweep never happened. */
static void report_sweep(const short *pcm, int frames, int rate) {
    const int W = 4;
    printf("    pitch (zero-crossings/s, %d windows): ", W);
    for (int w = 0; w < W; w++) {
        int a = frames * w / W, b = frames * (w + 1) / W;
        int cross = 0;
        for (int i = a + 1; i < b; i++)
            if ((pcm[i-1] < 0) != (pcm[i] < 0)) cross++;
        int span = b - a;
        printf("%6d", span > 0 ? (int)((float)cross * rate / span) : 0);
    }
    printf("\n");
}

static int report(const char *name, int want_wav) {
    int rate = audio_rate();
    int frames = audio_render(name, g_buf, MAX_FRAMES);

    if (frames <= 0) { printf("  %-10s NO OUTPUT (no such sound?)\n", name); return 1; }

    int peak = 0;
    double sq = 0;
    for (int i = 0; i < frames; i++) {
        int v = g_buf[i] < 0 ? -g_buf[i] : g_buf[i];
        if (v > peak) peak = v;
        sq += (double)g_buf[i] * g_buf[i];
    }
    int rms = (int)(sq > 0 ? __builtin_sqrt(sq / frames) : 0);

    int fails = 0;
    if (peak < 500)   { printf("  %-10s SILENT (peak %d)\n", name, peak); fails++; }
    if (peak > 32000) { printf("  %-10s CLIPPING (peak %d)\n", name, peak); fails++; }

    printf("  %-10s %5d ms  peak %5d  rms %5d  %s\n",
           name, frames * 1000 / rate, peak, rms, fails ? "FAIL" : "ok");
    report_sweep(g_buf, frames, rate);
    if (want_wav) write_wav(name, g_buf, frames, rate);
    return fails;
}

/* The sound names are in the recipe text; read them back rather than
   hardcoding a list here that would go stale. */
static int each_sound(int want_wav) {
    const char *p = data_text(DATA_SOUNDS);
    int fails = 0, found = 0;

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;
        if (!txt_is(t, len, "s")) continue;

        const char *nm = txt_token(p, &len);
        if (!nm) break;
        p = nm + len;

        char name[32];
        int i = 0;
        for (; i < len && i < 31; i++) name[i] = nm[i];
        name[i] = 0;

        fails += report(name, want_wav);
        found++;
    }
    if (!found) { printf("  no sounds found in the recipe text\n"); return 1; }
    return fails;
}

int main(int argc, char **argv) {
    int want_wav = 0;
    const char *one = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-wav") == 0) want_wav = 1;
        else one = argv[i];
    }

    printf("sndtest -- %d Hz, reading %s\n", audio_rate(),
           data_from_file(DATA_SOUNDS) ? "assets/sounds.txt" : "the baked copy");

    int fails = one ? report(one, want_wav) : each_sound(want_wav);
    printf(fails ? "\n%d problem(s)\n" : "\nall sounds produced audio\n", fails);
    return fails != 0;
}
