/**
 * @file music.c
 * @brief Walks a note stream against a clock. See music.h for why that is all it is.
 */

#include "music.h"
#include "audio.h"
#include "data.h"
#include "txt.h"
#include "diag.h"
#include <math.h>

/**
 * @brief Ceiling on notes across every track.
 *
 * ENGLISH: The three tracks the importer emits come to just under four
 * thousand after its polyphony reduction, so this is that with room to retune
 * the reduction without editing C. Past it the parse stops and reports; the
 * music simply ends early, which is audible and therefore findable.
 *
 * 한국어: 임포터가 내보내는 세 트랙은 폴리포니 축소 후 4천에 조금 못 미치므로, 이 값은 C를
 * 고치지 않고 축소를 다시 조정할 여유를 더한 것입니다. 이를 넘으면 파싱이 멈추고 보고합니다.
 * 음악이 일찍 끝날 뿐이며, 그것은 들리므로 찾을 수 있습니다.
 */
#define MUSIC_MAX_NOTES 4608

/**
 * @brief How loud a note is at full MIDI velocity, against ::audio_note's 0-100.
 *
 * ENGLISH: Well under full. Music sits behind the game rather than in front of
 * it, and every one of these notes is a bare oscillator with no recipe shaping
 * it -- a square wave at 100 against a shotgun sample at 100 is not a balance,
 * it is a klaxon.
 *
 * 한국어: 최대에 한참 못 미칩니다. 음악은 게임 앞이 아니라 뒤에 놓이며, 이 음표들은 전부
 * 레시피가 다듬어 주지 않는 맨 오실레이터입니다. 게인 100의 사각파와 게인 100의 샷건 샘플은
 * 균형이 아니라 경적입니다.
 */
#define MUSIC_NOTE_GAIN 34

/** @brief One note. Packed, because there are thousands. / 음표 하나. 수천 개이므로 조밀하게 담습니다. */
typedef struct {
    int           at_ms;   /**< Milliseconds from the track's start. / 트랙 시작으로부터의 밀리초. */
    short         dur;     /**< How long it sounds. / 지속 시간. */
    unsigned char note;    /**< MIDI pitch, 0-127. / MIDI 음높이. */
    unsigned char vel;     /**< MIDI velocity, 0-127. / MIDI 세기. */
    unsigned char wave;    /**< Which oscillator. See audio.c. / 어느 오실레이터인지. */
} Note;

/** @brief A slice of ::g_notes, plus what to wrap at. / ::g_notes의 한 구간과 순환 지점. */
typedef struct {
    int first, n;   /**< Index and count into ::g_notes. / ::g_notes에 대한 인덱스와 개수. */
    int len_ms;     /**< Loop length. / 순환 길이. */
} Track;

static Note  g_notes[MUSIC_MAX_NOTES];
static int   g_n_notes;
static Track g_track[MUSIC_TRACKS];
static int   g_parsed;

static MusicTrack g_cur;        /* what is playing */
static float      g_clock_ms;   /* how far into it */
static int        g_cursor;     /* next note not yet sounded */

/* Name in the file -> slot here. The file is generated, so a name that does not
   match is a change to the importer that did not reach this table -- reported
   rather than ignored, because the symptom otherwise is one silent track.
   파일의 이름을 이곳의 슬롯에 대응시킵니다. 파일은 생성물이므로, 맞지 않는 이름은 이 표에
   도달하지 못한 임포터 변경입니다. 무시하지 않고 보고하는 이유는, 그러지 않으면 증상이 조용한
   트랙 하나뿐이기 때문입니다. */
static MusicTrack track_named(const char *t, int len) {
    if (txt_is(t, len, "title")) return MUSIC_TITLE;
    if (txt_is(t, len, "level")) return MUSIC_LEVEL;
    if (txt_is(t, len, "boss"))  return MUSIC_BOSS;
    return MUSIC_NONE;
}

static void parse(void) {
    const char *p = data_text(DATA_MUSIC);
    Track *cur = 0;

    g_n_notes = 0;
    for (int i = 0; i < MUSIC_TRACKS; i++) { g_track[i].first = g_track[i].n = 0; g_track[i].len_ms = 0; }

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        if (txt_is(t, len, "t")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            MusicTrack which = track_named(nm, len);

            int ms = 0, ok = 1;
            p = txt_read_int(p, &ms, &ok);
            if (!ok) break;

            if (which == MUSIC_NONE) { cur = 0; DIAG(DIAG_SOUND_CAP); continue; }
            cur = &g_track[which];
            cur->first  = g_n_notes;
            cur->n      = 0;
            cur->len_ms = ms > 0 ? ms : 1;
            continue;
        }

        if (txt_is(t, len, "n")) {
            int v[5] = {0}, ok = 1;
            for (int i = 0; i < 5 && ok; i++) p = txt_read_int(p, &v[i], &ok);
            if (!ok) break;
            if (!cur) continue;

            if (g_n_notes >= MUSIC_MAX_NOTES) { DIAG(DIAG_SOUND_CAP); continue; }
            Note *N = &g_notes[g_n_notes++];
            N->at_ms = v[0];
            N->dur   = (short)(v[1] > 32767 ? 32767 : v[1]);
            N->note  = (unsigned char)(v[2] & 127);
            N->vel   = (unsigned char)(v[3] & 127);
            N->wave  = (unsigned char)(v[4] & 3);
            cur->n++;
            continue;
        }
    }
    g_parsed = 1;
}

/* Equal temperament off A440, which is what the MIDI numbers mean.
   MIDI 번호가 뜻하는 그대로의 A440 기준 평균율입니다. */
static float note_hz(int midi) {
    return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}

void music_play(MusicTrack track) {
    if ((unsigned)track >= (unsigned)MUSIC_TRACKS) track = MUSIC_NONE;
    if (track == g_cur) return;          /* see music.h: this is a condition, not an event */

    if (!g_parsed) parse();

    g_cur      = track;
    g_clock_ms = 0.0f;
    g_cursor   = (track == MUSIC_NONE) ? 0 : g_track[track].first;
}

void music_update(float dt) {
    if (g_cur == MUSIC_NONE) return;
    if (!g_parsed) parse();

    const Track *T = &g_track[g_cur];
    if (T->n <= 0) return;

    /* Clamped, so a stall -- a debugger pause, a long level load -- does not
       come back and fire half the track into four voices at once. The music
       simply resumes; nothing tries to catch up.
       멈춤(디버거 정지, 긴 레벨 로드)이 돌아와서 트랙의 절반을 네 보이스에 한꺼번에 쏟지
       않도록 제한합니다. 음악은 그냥 이어질 뿐 따라잡으려 하지 않습니다. */
    if (dt > 0.25f) dt = 0.25f;
    g_clock_ms += dt * 1000.0f;

    /* Wrap first, so a loop point crossed inside one frame still sounds the
       notes after it in the same frame rather than a frame late.
       먼저 순환시킵니다. 한 프레임 안에서 순환 지점을 넘어도 그 뒤의 음표가 한 프레임 늦지
       않고 같은 프레임에 울리도록 하기 위함입니다. */
    while (g_clock_ms >= (float)T->len_ms) {
        g_clock_ms -= (float)T->len_ms;
        g_cursor    = T->first;
    }

    int end = T->first + T->n;
    while (g_cursor < end && (float)g_notes[g_cursor].at_ms <= g_clock_ms) {
        const Note *N = &g_notes[g_cursor++];
        audio_note((int)N->wave, (int)note_hz(N->note), (int)N->dur,
                   MUSIC_NOTE_GAIN * N->vel / 127);
    }
}

MusicTrack music_now(void) { return g_cur; }

int music_note_count(MusicTrack track) {
    if ((unsigned)track >= (unsigned)MUSIC_TRACKS) return 0;
    if (!g_parsed) parse();
    return g_track[track].n;
}
