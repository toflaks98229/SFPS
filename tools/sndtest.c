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
#include "../src/music.h"   /* the note streams, checked upstream of the mixer */
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

/* Every name the game passes to audio_play, gathered by hand from the source.
 *
 * ENGLISH
 * -------
 * The list above (each_sound) walks the FILE and asks whether each recipe makes
 * a noise. This asks the opposite and more important question: does every name
 * the CODE calls for still exist in the file?
 *
 * The two failures are not symmetrical. A recipe in the file that nothing plays
 * is dead weight and costs a few bytes. A name in the code that the file no
 * longer defines is silence at the moment the game most wanted a sound, and it
 * looks exactly like a sound that is simply quiet -- audio_play finds nothing
 * and returns, with no error anywhere. That is the failure mode this catches,
 * and it is the one a rename produces.
 *
 * fxtest makes this same check for effects and it is the assertion there that
 * has earned its keep; sounds had no equivalent. Kept as a hand-written list
 * because the alternative -- scanning the .c files -- would make this test a
 * parser of C rather than a test of the game.
 *
 * @note Anything added here must be a name some audio_play call site passes.
 *       Grep for `audio_play(` to refresh it.
 *
 * 한국어
 * ------
 * 위의 each_sound는 *파일*을 순회하며 각 레시피가 소리를 내는지 묻습니다. 이 검사는 그
 * 반대이자 더 중요한 질문을 던집니다. *코드*가 부르는 모든 이름이 파일에 아직 존재하는가?
 *
 * 두 실패는 대칭이 아닙니다. 아무도 재생하지 않는 레시피는 몇 바이트를 차지하는 잉여일
 * 뿐입니다. 그러나 파일이 더 이상 정의하지 않는 이름을 코드가 부르면, 게임이 가장 소리를
 * 원한 순간의 침묵이 되며 그것은 그냥 조용한 소리와 정확히 똑같아 보입니다. audio_play는
 * 아무것도 찾지 못하고 반환하며 어디에도 오류가 남지 않습니다. 이것이 이 검사가 잡아내는
 * 실패 양상이고, 이름 변경이 만들어 내는 실패입니다.
 *
 * fxtest가 이펙트에 대해 같은 검사를 하며 그곳에서 값어치를 증명한 단언이 바로 그것입니다.
 * 사운드에는 대응하는 것이 없었습니다. 손으로 작성한 목록으로 두는 이유는, 대안인 .c 파일
 * 스캔이 이 테스트를 게임의 테스트가 아니라 C 파서로 만들기 때문입니다.
 */
static const char *PLAYED[] = {
    "shot", "dry", "pump", "impact",          /* weapon.c */
    /* One voice per weapon. Every one of these was "shot" until the roster
       grew past the gun the table was written for.
       무기마다 하나씩입니다. 표가 쓰인 그 총을 넘어 구성이 늘어날 때까지 이 전부가
       "shot"이었습니다. */
    "launch", "plasma", "saw", "sawup", "sawhit",
    "blast",                                  /* proj.c -- was "impact" */
    "hook", "hreel", "hland", "hbite", "hbiteb",  /* hook.c */
    "phurt", "pdie", "win", "exit",           /* main.c */
    "pammo", "pmed"                           /* pickup.c */
};

/* Names the game plays that the recipe file must still define.
   게임이 재생하는 이름 중 레시피 파일이 여전히 정의해야 하는 것들입니다. */
/* --- the text fits, rather than being trimmed to fit ---------------------
   MAX_SOUNDS was 24 and there were exactly 24 sounds, so the first one added
   past it was dropped -- and the one that went missing was not the new sound
   but whichever landed last, which was `switch`. The symptom was a door that
   stopped clicking, which reads as a bug in doors.

   The checks below would eventually have caught it, but only by the sound of
   something else failing. This names the cause: it asks whether the file fits
   the table, which is a question with a number in it, and it fails while there
   is still headroom rather than at the moment the last slot is taken.

   MAX_SOUNDS가 24였고 사운드도 정확히 24개였으므로, 그것을 넘어 추가된 첫 사운드가
   버려졌습니다. 사라진 것은 새 사운드가 아니라 마지막에 놓인 것, 즉 `switch`였습니다.
   증상은 더 이상 딸깍이지 않는 문이었고 문의 결함처럼 읽힙니다. 아래 검사들도 결국은
   잡았겠지만 *다른 것이* 실패하는 소리로만 잡았을 것입니다. 이것은 원인을 지목합니다.
   파일이 표에 들어가는지를 묻고, 그것은 수치를 담은 질문이며, 마지막 칸이 차는 순간이
   아니라 아직 여유가 있을 때 실패합니다. */
static int check_capacity(void) {
    int bad = 0;
    int n = audio_sound_count();

    printf("\n  --- the recipe text against the table ---\n");
    printf("  %-30s %4d / %4d  %s\n", "sounds defined", n, AUDIO_MAX_SOUNDS,
           n < AUDIO_MAX_SOUNDS ? "ok" : "FAIL");
    if (n >= AUDIO_MAX_SOUNDS) {
        printf("      the text fills the table -- raise AUDIO_MAX_SOUNDS\n");
        bad++;
    }

#ifdef DIAG_ENABLED
    if (diag_count(DIAG_SOUND_CAP) > 0) {
        printf("      and sounds were DROPPED reaching it\n");
        bad++;
    }
#endif
    return bad;
}

/* --- distance attenuation ------------------------------------------------
   Two things nothing else can see.

   The first is that a SAMPLE-ONLY sound plays at all. audio_play used to
   reject a sound with no layers, which was right while every sound was a
   recipe and became wrong the moment some came from WAVs -- the door, the
   switch and the keycard were silent after being imported, and no test
   noticed, because the checks that render playback ask for names out of
   sounds.txt and those three are not in it.

   The second is the curve. Full inside AUDIO_NEAR, nothing at AUDIO_FAR, and
   never rising in between. A wrong curve does not crash and is not visible;
   it makes the world feel flat, or makes a distant monster deafening, and
   either reads as a design choice rather than as a bug.

   거리 감쇠입니다. 다른 무엇도 볼 수 없는 두 가지를 검사합니다. 첫째는 *샘플만 있는*
   사운드가 재생되기는 하는가입니다. audio_play는 레이어가 없는 사운드를 거부했는데,
   모든 사운드가 레시피이던 동안에는 옳았고 일부가 WAV에서 오는 순간 틀리게 되었습니다.
   둘째는 곡선입니다. 틀린 곡선은 크래시도 나지 않고 눈에 보이지도 않으며, 세계를
   납작하게 만들거나 먼 몬스터를 귀청 떨어지게 만들 뿐인데, 둘 다 버그가 아니라 설계
   선택처럼 읽힙니다. */
static int check_distance(void) {
    int bad = 0;

    /* Sample-only names: none of these has a recipe in sounds.txt. */
    static const char *SAMPLED[] = { "door", "switch", "key" };
    for (int i = 0; i < 3; i++) {
        if (audio_render(SAMPLED[i], g_buf, MAX_FRAMES) <= 0) {
            printf("  %-10s a sample-only sound produced NO audio\n", SAMPLED[i]);
            bad++;
        }
    }
    if (!bad) printf("  distance          sample-only sounds play\n");

    /* The curve, asked of the same function audio_play_at uses -- so this
       cannot pass while the game applies a different one. */
    audio_listener(v3f(0, 0, 0));
    int prev = -1, fell = 1;
    for (int m = 0; m <= 40; m += 2) {
        int g = audio_gain_at(100, v3f((float)m, 0.0f, 0.0f));
        if (m == 0 && g != 100) {
            printf("  distance          not full volume at the listener (%d)\n", g);
            bad++;
        }
        if ((float)m >= AUDIO_FAR && g != 0) {
            printf("  distance          %dm is past AUDIO_FAR and still %d\n", m, g);
            bad++;
        }
        if ((float)m < AUDIO_NEAR && g != 100) {
            printf("  distance          %dm is inside AUDIO_NEAR and only %d\n", m, g);
            bad++;
        }
        if (prev >= 0 && g > prev) fell = 0;
        prev = g;
    }
    if (fell) printf("  distance          never gets louder as it gets further\n");
    else { printf("  distance          not monotonic with range\n"); bad++; }

    /* And the band between is a ramp rather than a cliff: something halfway
       has to be audible AND quieter than something near.
       그 사이 구간이 절벽이 아니라 경사인지 봅니다. */
    int mid = audio_gain_at(100, v3f((AUDIO_NEAR + AUDIO_FAR) * 0.5f, 0, 0));
    if (mid <= 0 || mid >= 100) {
        printf("  distance          midway is %d, so the ramp is a cliff\n", mid);
        bad++;
    } else {
        printf("  distance          midway is %d of 100\n", mid);
    }

    return bad;
}

/* --- the alphabet contract, read out of bake.ps1 ------------------------
   bake.ps1 encodes sampled sounds with a 64-character string and audio.c
   decodes by COMPUTING the value from the character. Nothing checks that the
   two describe the same alphabet, and nothing can: one is PowerShell and the
   other is C. So the test reads the script, exactly as sprtest does for the
   sprite codec -- the two share the alphabet, and a divergence in either
   would be silent until something sounded or looked wrong.

   The failure is not subtle in its effect and is very subtle in its cause:
   every sampled sound decodes to the wrong deltas, which sounds like a bad
   recording rather than like a bug.

   bake.ps1은 64자 문자열로 샘플 사운드를 인코딩하고 audio.c는 문자로부터 값을
   *계산*합니다. 둘이 같은 알파벳을 기술하는지 확인할 방법은 없습니다. 하나는
   PowerShell이고 다른 하나는 C이기 때문입니다. 그래서 테스트가 스크립트를 읽습니다. */
static int check_alphabet(void) {
    FILE *f = fopen("bake.ps1", "rb");
    if (!f) f = fopen("../bake.ps1", "rb");
    if (!f) { printf("  bake.ps1 unreadable, cannot compare alphabets\n"); return 1; }

    char alpha[128] = {0};
    int  found = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        const char *m = strstr(line, "$alphabet = '");
        if (!m) continue;
        m += 13;
        int n = 0;
        while (*m && *m != '\'' && n < 127) alpha[n++] = *m++;
        alpha[n] = 0;
        found = (n == 64);
        break;
    }
    fclose(f);

    if (!found) {
        printf("  bake.ps1's ADPCM alphabet is not 64 characters (%d)\n",
               (int)strlen(alpha));
        return 1;
    }

    int bad = 0, first = -1;
    for (int i = 0; i < 64; i++)
        if (audio_b64val(alpha[i]) != i) { if (first < 0) first = i; bad++; }

    if (bad) {
        printf("  %d of 64 characters decode to the wrong value "
               "(first at %d, '%c')\n", bad, first, alpha[first]);
        return 1;
    }
    printf("  alphabet          bake.ps1 and audio.c agree on all 64\n");

    /* And nothing outside it may decode to something. A stray character in a
       data line has to stop the decoder, not be read as index 0. */
    if (audio_b64val('!') >= 0 || audio_b64val(' ') >= 0 ||
        audio_b64val('\n') >= 0) {
        printf("  a character outside the alphabet decodes to a value\n");
        return 1;
    }
    printf("  alphabet          a character outside it is rejected\n");
    return 0;
}



static int check_played(void) {
    int missing = 0;
    int n = (int)(sizeof(PLAYED) / sizeof(PLAYED[0]));

    printf("\n  --- names the code plays ---\n");
    for (int i = 0; i < n; i++) {
/* audio_render is the offline half of the same lookup audio_play does,
           and it needs no device -- it returns 0 frames for a name the recipe
           text does not define, which is exactly the question being asked.
           audio_render는 audio_play가 수행하는 것과 동일한 조회의 오프라인 버전이며
           장치가 필요 없습니다. 레시피 텍스트가 정의하지 않은 이름에 대해 0 프레임을
           반환하는데, 그것이 바로 여기서 묻는 질문입니다. */
        if (audio_render(PLAYED[i], g_buf, MAX_FRAMES) <= 0) {
            printf("  %-10s MISSING -- audio_play(\"%s\") will be silent\n",
                   PLAYED[i], PLAYED[i]);
            missing++;
        }
    }
    printf("  %-10s %d/%d names resolve  %s\n", "", n - missing, n,
           missing ? "FAIL" : "ok");
    return missing;
}

/* --- the music ------------------------------------------------------------
 *
 * Parsing only, and that is the honest limit of what this file can reach:
 * sndtest never calls audio_init, so audio_dev_lock finds no device and
 * audio_note returns before it touches a voice. What CAN be checked here is
 * everything upstream of the mixer -- that the baked stream parsed, that each
 * track got its own notes, and that switching tracks is a decision rather than
 * a restart. Those are the parts that fail silently; a mixer that is not
 * running is obvious the moment anybody listens.
 *
 * 파싱만이며, 그것이 이 파일이 닿을 수 있는 정직한 한계입니다. sndtest는 audio_init을 결코
 * 호출하지 않으므로 audio_dev_lock이 장치를 찾지 못하고 audio_note는 보이스를 건드리기 전에
 * 반환합니다. 이곳에서 검사할 수 있는 것은 믹서보다 위쪽 전부입니다. 구워진 스트림이
 * 파싱되었는지, 각 트랙이 자기 음표를 받았는지, 트랙 전환이 재시작이 아니라 결정인지입니다.
 * 조용히 실패하는 부분이 그것들입니다. 믹서가 돌지 않는 것은 누구든 들어 보면 즉시 드러납니다. */
static int music_ok(int cond, const char *what, int *bad) {
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) (*bad)++;
    return cond;
}

static int check_music(void) {
    int bad = 0;
    printf("\nmusic\n");

    int t = music_note_count(MUSIC_TITLE);
    int l = music_note_count(MUSIC_LEVEL);
    int b = music_note_count(MUSIC_BOSS);
    printf("      notes: title %d, level %d, boss %d\n", t, l, b);

    music_ok(t > 0 && l > 0 && b > 0, "every track parsed some notes", &bad);

    /* Each track is its own slice. Two tracks reporting the same count would be
       the symptom of a `t` line that did not open a new one.
       각 트랙은 자기 구간입니다. 두 트랙이 같은 개수를 보고하는 것은 새 구간을 열지 못한
       `t` 줄의 증상입니다. */
    music_ok(t != l && l != b, "and they are separate slices, not one shared", &bad);

    music_play(MUSIC_LEVEL);
    music_ok(music_now() == MUSIC_LEVEL, "asking for a track selects it", &bad);

    music_play(MUSIC_BOSS);
    music_ok(music_now() == MUSIC_BOSS, "and switching switches", &bad);

    /* Idempotent: the frame loop states "boss music" every frame, so a call that
       restarted the track would hold its first bar forever.
       멱등입니다. 프레임 루프가 매 프레임 "보스 음악"이라고 진술하므로, 트랙을 다시 시작하는
       호출이라면 첫 마디를 영원히 붙잡고 있게 됩니다. */
    music_play(MUSIC_BOSS);
    music_update(0.5f);
    music_play(MUSIC_BOSS);
    music_update(0.5f);
    music_ok(music_now() == MUSIC_BOSS, "and asking again is not a restart", &bad);

    /* A whole loop, in steps, with no device under it. Nothing here may run off
       the end of the note array when the clock wraps -- the title track is seven
       seconds, so fifteen seconds of stepping crosses the loop point twice.
       장치 없이 한 바퀴 전체를 단계적으로 돌립니다. 시계가 순환할 때 음표 배열 끝을 넘어서는
       일이 없어야 합니다. 타이틀 트랙이 7초이므로 15초를 진행하면 순환 지점을 두 번 넘습니다. */
    music_play(MUSIC_TITLE);
    for (int i = 0; i < 900; i++) music_update(0.016f);
    music_ok(music_now() == MUSIC_TITLE, "and a full loop survives with no device open", &bad);

    music_play(MUSIC_NONE);
    music_ok(music_now() == MUSIC_NONE, "and it can be stopped", &bad);
    return bad;
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

    /* Skipped when a single sound was named: the caller asked about that one,
       and failing on the other fourteen would be answering a different question.
       사운드 하나를 지정한 경우에는 건너뜁니다. 호출자가 그것에 대해 물었으므로, 나머지
       열네 개로 실패하는 것은 다른 질문에 답하는 셈입니다. */
    if (!one) fails += check_played();

    if (!one) fails += check_capacity();
    if (!one) fails += check_alphabet();
    if (!one) fails += check_distance();

    fails += check_music();

    printf(fails ? "\n%d problem(s)\n" : "\nall sounds produced audio\n", fails);
    return fails != 0;
}
