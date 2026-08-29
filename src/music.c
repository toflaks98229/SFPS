/**
 * @file music.c
 * @brief Sequences the baked note stream against a clock and sounds each note as it falls due.
 *
 * ENGLISH
 * -------
 * A SEQUENCER, NOT A PLAYER. Nothing here decodes anything: there is no
 * sample, no stream and no decoder in this translation unit. The score arrives
 * as text that the bake produced from MIDI, and the whole runtime cost is a
 * forward scan through a sorted array handing notes to ::audio_note. See
 * music.h for why the engine takes a score rather than a recording.
 *
 * THE SCORE IS PARSED ONCE, LAZILY. ::g_parsed guards it and every public
 * entry point that needs notes checks it, so no start-up order has to be
 * arranged: whichever call arrives first pays for the parse and the rest find
 * it done. The tables below are written only by ::parse and read by everything
 * else.
 *
 * PLAYBACK IS THREE NUMBERS. What is playing (::g_cur), how far into it
 * (::g_clock_ms), and which note comes next (::g_cursor). A loop is a wrap of
 * the clock and a reset of the cursor, which is why a track needs no state
 * beyond the slice it occupies.
 *
 * @note Runs entirely on the game thread. ::audio_note is the only thing this
 *       file calls that reaches the mixer, and it takes the device lock
 *       itself, so nothing here locks anything.
 * @note Allocates nothing and owns no handle. The note table is a file-scope
 *       array sized by ::MUSIC_MAX_NOTES, so the module has no teardown and
 *       there is nothing to free.
 * @warning The score tables are file-scope, so ONE score is in play per
 *          process. That is deliberate -- there is one soundtrack -- but it
 *          means a second ::Level does not bring a second sequencer.
 *
 * 한국어
 * ------
 * *플레이어가 아니라 시퀀서입니다.* 이곳에서는 아무것도 디코딩하지 않습니다. 이 번역 단위에는
 * 샘플도, 스트림도, 디코더도 없습니다. 악보는 베이크가 MIDI로부터 만들어 낸 텍스트로 도착하며,
 * 실행 시간 비용의 전부는 정렬된 배열을 앞으로 훑으며 음표를 ::audio_note에 건네는 것입니다.
 * 엔진이 녹음이 아니라 악보를 받는 이유는 music.h를 참조하십시오.
 *
 * *악보는 지연 방식으로 한 번만 파싱됩니다.* ::g_parsed가 그것을 지키며 음표가 필요한 모든
 * 공개 진입점이 이를 확인하므로, 시작 순서를 따로 맞출 필요가 없습니다. 먼저 도착한 호출이
 * 파싱 비용을 치르고 나머지는 이미 끝난 것을 발견합니다. 아래의 표들은 오직 ::parse만이
 * 기록하고 그 외에는 모두 읽기만 합니다.
 *
 * *재생은 숫자 세 개입니다.* 무엇이 재생 중인가(::g_cur), 그 안으로 얼마나 들어왔는가
 * (::g_clock_ms), 다음 음표가 무엇인가(::g_cursor)입니다. 순환은 시계를 되감고 커서를
 * 되돌리는 것이며, 그래서 트랙은 자신이 차지한 구간 외에 어떤 상태도 필요로 하지 않습니다.
 *
 * @note 전적으로 게임 스레드에서 동작합니다. 이 파일이 부르는 것 중 믹서에 닿는 것은
 *       ::audio_note뿐이며 그것이 스스로 장치 락을 잡으므로, 이곳에서는 아무것도 잠그지
 *       않습니다.
 * @note 아무것도 할당하지 않고 어떤 핸들도 소유하지 않습니다. 음표 표는 ::MUSIC_MAX_NOTES로
 *       크기가 정해진 파일 범위 배열이므로 이 모듈에는 정리 절차가 없고 해제할 것도 없습니다.
 * @warning 악보 표가 파일 범위이므로 프로세스당 악보는 *하나*입니다. 사운드트랙이 하나이니
 *          의도한 바이지만, 두 번째 ::Level이 두 번째 시퀀서를 데려오지는 않는다는 뜻입니다.
 */

#include "music.h"

#include <math.h>

#include "audio.h"
#include "data.h"
#include "txt.h"
#include "diag.h"

/* --- File-local constants / 파일 지역 상수 --- */

/**
 * @brief Ceiling on notes across every track.
 *
 * ENGLISH
 * -------
 * The three tracks the importer emits come to just under four thousand after
 * its polyphony reduction, so this is that with room to retune the reduction
 * without editing C. Past it the parse stops and reports; the music simply
 * ends early, which is audible and therefore findable.
 *
 * 한국어
 * ------
 * 임포터가 내보내는 세 트랙은 폴리포니 축소 후 4천에 조금 못 미치므로, 이 값은 C를 고치지
 * 않고 축소를 다시 조정할 여유를 더한 것입니다. 이를 넘으면 파싱이 멈추고 보고합니다. 음악이
 * 일찍 끝날 뿐이며, 그것은 들리므로 찾을 수 있습니다.
 */
#define MUSIC_MAX_NOTES 4608

/**
 * @brief How loud a note is at full MIDI velocity, against ::audio_note's 0-100.
 *
 * ENGLISH
 * -------
 * Well under full. Music sits behind the game rather than in front of it, and
 * every one of these notes is a bare oscillator with no recipe shaping it -- a
 * square wave at 100 against a shotgun sample at 100 is not a balance, it is a
 * klaxon.
 *
 * 한국어
 * ------
 * 최대에 한참 못 미칩니다. 음악은 게임 앞이 아니라 뒤에 놓이며, 이 음표들은 전부 레시피가
 * 다듬어 주지 않는 맨 오실레이터입니다. 게인 100의 사각파와 게인 100의 샷건 샘플은 균형이
 * 아니라 경적입니다.
 */
#define MUSIC_NOTE_GAIN 34

/* --- File-local types / 파일 지역 타입 --- */

/**
 * @brief One note: when it sounds, how long for, and with what.
 *
 * ENGLISH
 * -------
 * Packed deliberately. There are thousands of these and they are read in
 * order, so the narrow fields keep a whole track inside a handful of cache
 * lines. The widths mirror what MIDI can express, which is what the importer
 * has to hand: pitch and velocity are seven-bit, so a byte each is exact
 * rather than merely sufficient.
 *
 * 한국어
 * ------
 * 의도적으로 조밀하게 담았습니다. 이것이 수천 개이고 순서대로 읽히므로, 좁은 필드가 트랙
 * 하나를 캐시 라인 몇 개 안에 유지시킵니다. 각 폭은 MIDI가 표현할 수 있는 것을 그대로
 * 반영하며 그것이 임포터가 건네주는 전부입니다. 음높이와 세기는 7비트이므로 1바이트씩이면
 * 충분한 정도가 아니라 정확합니다.
 */
typedef struct {
    int           at_ms;   /**< @brief Milliseconds from the track's start. / 트랙 시작으로부터의 밀리초. */
    short         dur;     /**< @brief How long it sounds, in milliseconds. / 울리는 시간 (밀리초). */
    unsigned char note;    /**< @brief MIDI pitch, 0-127. / MIDI 음높이. 0-127. */
    unsigned char vel;     /**< @brief MIDI velocity, 0-127. / MIDI 세기. 0-127. */
    unsigned char wave;    /**< @brief Which oscillator. See audio.c. / 어느 오실레이터인지. audio.c 참조. */
} Note;

/**
 * @brief A slice of ::g_notes, plus the point it wraps at.
 *
 * ENGLISH
 * -------
 * A track owns no notes of its own; it names a run of them inside the one
 * shared array. That is what makes a track switch free -- moving the cursor to
 * ::Track::first is the whole of it -- and what makes ::Track::len_ms rather
 * than the last note's time the loop point, because a track's silence at the
 * end is part of its length.
 *
 * 한국어
 * ------
 * 트랙은 자기 음표를 소유하지 않고, 공유되는 하나의 배열 안에서 연속된 구간을 지목합니다.
 * 그래서 트랙 전환이 공짜입니다. 커서를 ::Track::first로 옮기는 것이 전부입니다. 또한 마지막
 * 음표의 시각이 아니라 ::Track::len_ms가 순환 지점인 이유이기도 합니다. 트랙 끝의 침묵도 그
 * 길이의 일부이기 때문입니다.
 */
typedef struct {
    int first, n;   /**< @brief Index and count into ::g_notes. / ::g_notes에 대한 인덱스와 개수. */
    int len_ms;     /**< @brief Loop length in milliseconds. / 순환 길이 (밀리초). */
} Track;

/* --- Module state: the parsed score / 모듈 상태: 파싱된 악보 --- */

/* Written once by ::parse and read-only afterwards. The four move together:
   the notes, how many of them are real, the slices that name them, and whether
   any of it has happened yet.
   ::parse가 한 번 기록하고 그 뒤로는 읽기 전용입니다. 넷은 함께 움직입니다. 음표들, 그중
   실제인 것의 수, 그것을 지목하는 구간들, 그리고 그 일이 이미 일어났는지 여부입니다. */

/** @brief Every track's notes, back to back. Sliced by ::g_track. / 모든 트랙의 음표를 연이어 담습니다. ::g_track이 구간을 나눕니다. */
static Note  g_notes[MUSIC_MAX_NOTES];
/** @brief How many entries of ::g_notes are real. / ::g_notes 중 실제인 항목의 수. */
static int   g_n_notes;
/** @brief Where each track sits inside ::g_notes. Indexed by ::MusicTrack. / 각 트랙이 ::g_notes 안에서 차지하는 위치. ::MusicTrack으로 색인합니다. */
static Track g_track[MUSIC_TRACKS];
/** @brief Non-zero once ::parse has run. Guards the lazy parse. / ::parse가 실행되었으면 0이 아닙니다. 지연 파싱을 지킵니다. */
static int   g_parsed;

/* --- Module state: playback / 모듈 상태: 재생 --- */

/* The playhead, and nothing more. Reset together by ::music_play, advanced
   together by ::music_update.
   재생 헤드이며 그 이상은 아닙니다. ::music_play가 함께 초기화하고 ::music_update가 함께
   진행시킵니다. */

/** @brief The track now playing, or ::MUSIC_NONE for silence. / 지금 재생 중인 트랙. 무음이면 ::MUSIC_NONE입니다. */
static MusicTrack g_cur;
/** @brief How far into ::g_cur the playhead has reached, in milliseconds. / 재생 헤드가 ::g_cur 안으로 들어온 거리 (밀리초). */
static float      g_clock_ms;
/** @brief Index into ::g_notes of the next note not yet sounded. / 아직 울리지 않은 다음 음표의 ::g_notes 인덱스. */
static int        g_cursor;

/* --- static function prototypes / 정적 함수 원형 --- */

static MusicTrack track_named(const char *t, int len);
static void       parse(void);
static float      note_hz(int midi);

/* --- Public function definitions / 공개 함수 정의 --- */

void music_play(MusicTrack track) {
    /* Anything outside the enum becomes silence rather than an index into the
       track table. The argument reaches here from game logic that may have
       computed it, and a bad one would otherwise read a Track past the array.
       enum 밖의 값은 트랙 표에 대한 인덱스가 아니라 무음이 됩니다. 인자는 그것을 계산했을
       수도 있는 게임 로직에서 이곳에 도달하며, 잘못된 값은 그러지 않으면 배열 너머의 Track을
       읽게 됩니다. */
    if ((unsigned)track >= (unsigned)MUSIC_TRACKS) track = MUSIC_NONE;
    if (track == g_cur) return;          /* see music.h: this is a condition, not an event */

    if (!g_parsed) parse();

    g_cur      = track;
    g_clock_ms = 0.0f;

    /* MUSIC_NONE owns no slice, so its cursor is a placeholder rather than a
       position -- ::music_update returns before ever reading it.
       MUSIC_NONE은 구간을 소유하지 않으므로 그 커서는 위치가 아니라 자리를 채우는 값입니다.
       ::music_update는 그것을 읽기 전에 반환합니다. */
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
       notes after it in the same frame rather than a frame late. A `while`
       rather than an `if` because ::Track::len_ms may be shorter than the
       clamped step above, and one subtraction would leave the clock still past
       the end.
       먼저 순환시킵니다. 한 프레임 안에서 순환 지점을 넘어도 그 뒤의 음표가 한 프레임 늦지
       않고 같은 프레임에 울리도록 하기 위함입니다. `if`가 아니라 `while`인 이유는
       ::Track::len_ms가 위에서 제한된 간격보다 짧을 수 있고, 한 번만 빼면 시계가 여전히 끝을
       지나 있기 때문입니다. */
    while (g_clock_ms >= (float)T->len_ms) {
        g_clock_ms -= (float)T->len_ms;
        g_cursor    = T->first;
    }

    /* Every note whose time has come, not merely the next one: a long frame
       may cover several, and stopping at one would spread a chord across
       frames and leave the track permanently behind its own clock.
       다음 하나가 아니라 시각이 된 *모든* 음표입니다. 긴 프레임은 여러 개를 포함할 수 있고,
       하나에서 멈추면 화음이 여러 프레임에 흩어지며 트랙이 자신의 시계보다 영구히
       뒤처집니다. */
    int end = T->first + T->n;
    while (g_cursor < end && (float)g_notes[g_cursor].at_ms <= g_clock_ms) {
        const Note *N = &g_notes[g_cursor++];
        /* Velocity scaled into ::MUSIC_NOTE_GAIN's range. Integer arithmetic
           in this order -- multiply, then divide -- keeps a full-velocity note
           at exactly the gain the constant names.
           세기를 ::MUSIC_NOTE_GAIN의 범위로 환산합니다. 곱한 뒤 나누는 이 순서의 정수 연산은
           최대 세기의 음표를 상수가 지정한 게인 그대로 유지합니다. */
        audio_note((int)N->wave, (int)note_hz(N->note), (int)N->dur,
                   MUSIC_NOTE_GAIN * N->vel / 127);
    }
}

MusicTrack music_now(void) { return g_cur; }

int music_note_count(MusicTrack track) {
    if ((unsigned)track >= (unsigned)MUSIC_TRACKS) return 0;
    /* Parsed on demand here too: a test may ask what a track holds without
       ever having played it.
       이곳에서도 요청 시 파싱합니다. 테스트는 트랙을 재생한 적 없이 그것이 무엇을 담는지
       물을 수 있습니다. */
    if (!g_parsed) parse();
    return g_track[track].n;
}

/* --- static helper definitions / 정적 보조 함수 정의 --- */

/**
 * @brief Maps a track name in the score file to its slot here.
 *
 * ENGLISH
 * -------
 * @param[in] t   Token from the score text. NOT NUL-terminated.
 * @param[in] len Length of `t` in bytes.
 * @return The matching ::MusicTrack, or ::MUSIC_NONE when the name is unknown.
 *
 * @note The file is generated, so a name that does not match is a change to
 *       the importer that did not reach this table. ::parse reports the miss
 *       rather than ignoring it, because the symptom otherwise is one silent
 *       track.
 *
 * 한국어
 * ------
 * @brief 악보 파일의 트랙 이름을 이곳의 슬롯에 대응시킵니다.
 * @param[in] t   악보 텍스트의 토큰. NUL로 끝나지 *않습니다*.
 * @param[in] len `t`의 길이 (바이트).
 * @return 대응하는 ::MusicTrack. 이름을 알 수 없으면 ::MUSIC_NONE입니다.
 *
 * @note 파일은 생성물이므로, 맞지 않는 이름은 이 표에 도달하지 못한 임포터 변경입니다.
 *       ::parse는 무시하지 않고 그 불일치를 보고합니다. 그러지 않으면 증상이 조용한 트랙
 *       하나뿐이기 때문입니다.
 */
static MusicTrack track_named(const char *t, int len) {
    if (txt_is(t, len, "title")) return MUSIC_TITLE;
    if (txt_is(t, len, "level")) return MUSIC_LEVEL;
    if (txt_is(t, len, "boss"))  return MUSIC_BOSS;
    return MUSIC_NONE;
}

/**
 * @brief Reads the whole score out of ::DATA_MUSIC into ::g_notes and ::g_track.
 *
 * ENGLISH
 * -------
 * The format is two statements. `t <name> <len_ms>` opens a track, and every
 * `n <at> <dur> <pitch> <vel> <wave>` after it appends one note to whichever
 * track is open. The importer emits notes in time order and this preserves
 * that order, which is what lets ::music_update advance with a cursor instead
 * of a search.
 *
 * @note Sets ::g_parsed unconditionally, INCLUDING after a truncated read. A
 *       score that hit a cap or a malformed line is not retried -- retrying
 *       would re-report the same diagnostic every frame for a file that cannot
 *       improve between calls.
 * @warning Rewrites ::g_notes, ::g_n_notes and ::g_track in place. No caller
 *          holds a pointer into them across this call, and ::g_cursor is valid
 *          only once ::music_play has set it from the freshly parsed slice.
 *
 * 한국어
 * ------
 * @brief ::DATA_MUSIC의 악보 전체를 ::g_notes와 ::g_track으로 읽어들입니다.
 *
 * 형식은 두 종류의 문입니다. `t <이름> <길이_ms>`는 트랙을 열고, 그 뒤의 모든
 * `n <시각> <길이> <음높이> <세기> <파형>`은 열려 있는 트랙에 음표 하나를 덧붙입니다. 임포터가
 * 음표를 시간 순으로 내보내며 이 함수가 그 순서를 보존합니다. 그 덕분에 ::music_update가
 * 탐색이 아니라 커서로 진행할 수 있습니다.
 *
 * @note 잘린 읽기 뒤에도 *포함하여* 무조건 ::g_parsed를 설정합니다. 상한에 닿았거나 형식이
 *       잘못된 줄을 만난 악보를 다시 시도하지 않습니다. 다시 시도하면 호출 사이에 나아질 수
 *       없는 파일에 대해 같은 진단을 매 프레임 되풀이해 보고하게 됩니다.
 * @warning ::g_notes, ::g_n_notes, ::g_track을 제자리에서 다시 씁니다. 어떤 호출자도 이 호출을
 *          가로질러 그것들에 대한 포인터를 쥐지 않으며, ::g_cursor는 ::music_play가 갓 파싱된
 *          구간으로부터 설정한 뒤에만 유효합니다.
 */
static void parse(void) {
    const char *p = data_text(DATA_MUSIC);
    Track *cur = 0;

    /* Cleared before the walk, not after: a re-parse must not leave a track
       this score no longer defines pointing into the previous score's notes.
       순회 이후가 아니라 이전에 비웁니다. 다시 파싱할 때, 이 악보가 더 이상 정의하지 않는
       트랙이 이전 악보의 음표를 가리킨 채 남아서는 안 됩니다. */
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

            /* An unknown name CLOSES the current track rather than leaving it
               open, so the notes that follow are dropped instead of landing in
               whichever track happened to precede it.
               알 수 없는 이름은 현재 트랙을 열어 둔 채 두지 않고 *닫습니다*. 그래야 뒤따르는
               음표가 우연히 앞서 있던 트랙에 들어가지 않고 버려집니다. */
            if (which == MUSIC_NONE) { cur = 0; DIAG(DIAG_SOUND_CAP); continue; }
            cur = &g_track[which];
            cur->first  = g_n_notes;
            cur->n      = 0;
            /* Zero would make ::music_update's wrap loop spin forever, so a
               track that declares no length gets the shortest real one.
               0이면 ::music_update의 순환 루프가 영원히 돌므로, 길이를 선언하지 않은 트랙은
               가능한 가장 짧은 실제 길이를 받습니다. */
            cur->len_ms = ms > 0 ? ms : 1;
            continue;
        }

        if (txt_is(t, len, "n")) {
            int v[5] = {0}, ok = 1;
            for (int i = 0; i < 5 && ok; i++) p = txt_read_int(p, &v[i], &ok);
            if (!ok) break;
            if (!cur) continue;                 /* a note outside any track */

            /* `continue`, not `break`: the cap is a ceiling on what fits, and
               the lines after it are still well-formed. Stopping would also
               abandon the tracks that come later, turning one over-long track
               into total silence.
               `break`가 아니라 `continue`입니다. 상한은 담을 수 있는 양의 천장이며 그 뒤의
               줄들도 여전히 올바른 형식입니다. 멈추면 뒤에 오는 트랙들까지 버리게 되어,
               지나치게 긴 트랙 하나가 전체 무음으로 바뀝니다. */
            if (g_n_notes >= MUSIC_MAX_NOTES) { DIAG(DIAG_SOUND_CAP); continue; }
            Note *N = &g_notes[g_n_notes++];
            N->at_ms = v[0];
            /* Each field narrowed to what ::Note holds. Duration SATURATES,
               because a clipped long note is still a note. Pitch, velocity and
               wave MASK, because MIDI cannot exceed the mask and a value that
               does is a corrupt file rather than a loud one.
               각 필드를 ::Note가 담는 폭으로 좁힙니다. 지속 시간은 *포화*시킵니다. 잘린 긴
               음표도 여전히 음표이기 때문입니다. 음높이·세기·파형은 *마스크*합니다. MIDI는
               마스크를 넘을 수 없으므로 넘는 값은 큰 소리가 아니라 손상된 파일입니다. */
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

/**
 * @brief Converts a MIDI note number to its frequency in hertz.
 *
 * ENGLISH
 * -------
 * @param[in] midi MIDI pitch. 69 is A440, and every 12 is one octave.
 * @return The pitch in hertz.
 *
 * @note Equal temperament off A440, which is what the MIDI numbers mean. The
 *       caller truncates the result to an integer, which is inaudible at these
 *       pitches and is the form ::audio_note takes.
 *
 * 한국어
 * ------
 * @brief MIDI 음 번호를 헤르츠 단위 주파수로 변환합니다.
 * @param[in] midi MIDI 음높이. 69가 A440이고 12마다 한 옥타브입니다.
 * @return 헤르츠 단위의 음높이.
 *
 * @note MIDI 번호가 뜻하는 그대로의 A440 기준 평균율입니다. 호출자가 결과를 정수로
 *       버림하는데, 이 음역에서는 들리지 않는 차이이며 ::audio_note가 받는 형태입니다.
 */
static float note_hz(int midi) {
    return 440.0f * powf(2.0f, (midi - 69) / 12.0f);
}
