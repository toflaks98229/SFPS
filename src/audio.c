#include "audio.h"
#include "data.h"
#include "txt.h"
#include "diag.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>

/* --- 매크로 및 상수 --- */
#define RATE        44100   ///< @brief 오디오 샘플 레이트 (Hz).
#define FRAMES      512     ///< @brief 버퍼 당 프레임 수. 약 11.6ms에 해당하며, 약 46ms의 지연 시간을 가집니다.
#define NBUF        4       ///< @brief 오디오 버퍼의 수.
#define MAX_LAYERS  6       ///< @brief 사운드 레시피의 최대 레이어 수.
/* 캐시할 수 있는 최대 사운드 레시피 수.
 *
 * 16은 그 시점의 라이브러리 크기와 정확히 같았고, 그것이 문제였습니다. 한계에 딱 맞는
 * 용량은 다음에 사운드를 추가하는 사람이 조용히 잘리는 지점이며, 증상은 "그 소리가 안
 * 난다"뿐이라 원인을 알려 주지 않습니다(DIAG_SOUND_CAP이 잡아 주기는 합니다).
 *
 * 그래플 훅 사운드를 추가하며 정확히 그 경계에 도달했으므로, 여유를 두고 24로 올립니다.
 * Sound 하나는 약 100바이트이고 배열은 .bss에 위치하므로 디스크 용량을 소모하지
 * 않습니다. */
#define MAX_SOUNDS  24      ///< @brief 캐시할 수 있는 최대 사운드 레시피 수.
#define MAX_VOICES  12      ///< @brief 동시에 재생할 수 있는 최대 사운드 인스턴스 (보이스) 수.
#define NAME_LEN    16      ///< @brief 사운드 이름의 최대 길이.

/* --- 타입 정의 --- */

/**
 * @struct Layer
 * @brief 사운드를 구성하는 단일 오실레이터 레이어의 레시피입니다.
 */
typedef struct {
    short wave;        /**< 0: 사각파, 1: 톱니파, 2: 사인파, 3: 노이즈 */
    short ms;          /**< 레이어의 총 지속 시간 (밀리초) */
    short f0, f1;      /**< 주파수 스윕 시작 및 끝 값 (Hz) */
    short atk, dec;    /**< 어택 및 디케이 시간 (밀리초) */
    short vol;         /**< 볼륨 (0-100) */
} Layer;

/**
 * @struct Sound
 * @brief 여러 레이어로 구성된 완전한 사운드 레시피입니다.
 */
typedef struct {
    char  name[NAME_LEN];        /**< 사운드의 이름 */
    Layer layers[MAX_LAYERS];    /**< 이 사운드를 구성하는 레이어들 */
    int   n;                     /**< 레이어의 수 */
} Sound;

/**
 * @struct Voice
 * @brief 재생 중인 사운드의 활성 인스턴스입니다.
 */
typedef struct {
    const Sound *snd;       /**< 재생할 사운드 레시피에 대한 포인터 */
    int      pos;           /**< 경과된 샘플 수 */
    int      gain;          /**< 볼륨 게인 (0-100) */
    unsigned rng;           /**< 노이즈 생성을 위한 난수 상태 */
    float    phase[MAX_LAYERS]; /**< 각 레이어의 오실레이터 위상 */
    float    hold[MAX_LAYERS];  /**< 노이즈를 위한 샘플 앤 홀드 값 */
} Voice;

/* --- 정적 변수 --- */

/* 사운드 레시피.
 *
 * ENGLISH
 * -------
 * Shared between the game thread (which parses) and the mixer thread (which
 * dereferences Voice::snd into this array). Every access to these three is
 * therefore under g_lock -- see the threading contract above audio_play.
 *
 * 한국어
 * ------
 * 게임 스레드(파싱)와 믹서 스레드(Voice::snd를 통해 이 배열을 역참조)가 공유합니다.
 * 따라서 이 세 변수에 대한 모든 접근은 g_lock 하에서 이루어집니다. audio_play 위의
 * 스레딩 계약을 참조하십시오.
 */
static Sound g_sounds[MAX_SOUNDS];
static int   g_n_sounds;
static int   g_parsed;

// 활성 보이스
static Voice            g_voices[MAX_VOICES];
static CRITICAL_SECTION g_lock;

/* 장치가 열리고 스레드가 실행 중인지 여부.
 *
 * ENGLISH
 * -------
 * volatile because audio_shutdown clears it from the game thread while the
 * mixer may still be running, and because it gates g_lock's own validity:
 * once clear, the critical section has been deleted and must not be entered.
 *
 * 한국어
 * ------
 * audio_shutdown이 믹서가 아직 실행 중일 수 있는 상태에서 게임 스레드로부터 이 값을
 * 해제하며, 또한 이 값이 g_lock 자체의 유효성을 결정하기 때문에 volatile입니다.
 * 해제된 이후에는 임계 영역이 삭제된 상태이므로 진입해서는 안 됩니다.
 */
static volatile LONG    g_ready;

// 오디오 백엔드
static HWAVEOUT g_dev;
static HANDLE   g_event, g_thread;
static WAVEHDR  g_hdr[NBUF];
static short    g_buf[NBUF][FRAMES];
static volatile LONG g_running;

/* --- 정적 함수 선언 --- */
static void parse_sounds(void);
static const Sound *find_sound(const char *name);
static float frand(unsigned *s);
static float osc(int wave, float phase, float *hold, unsigned *rng);
static float envelope(const Layer *L, float t_ms);
static int render_voice(Voice *V, short *out, int frames);
static void mix(short *out, int frames);
static DWORD WINAPI mixer_thread(LPVOID param);

/* --- 정적 함수 구현 --- */

/**
 * @brief assets/sounds.txt에서 사운드 레시피를 파싱합니다.
 *
 * ENGLISH
 * -------
 * `l` supplies seven integers; the shared tokenizer handles the rest.
 *
 * @warning Caller MUST hold g_lock. This rewrites g_sounds[] in place, and
 *          the mixer thread dereferences Voice::snd straight into that array.
 * @note Silences every live voice before touching the recipes. A voice holds
 *       a raw `const Sound *`, so once the array is rewritten that pointer
 *       either addresses a half-written recipe or a completely different
 *       sound than the one that was playing. Killing them first is what makes
 *       the rewrite safe -- a hot reload costs one frame of audio, which is
 *       inaudible, where the alternative was a torn read.
 *
 * 한국어
 * ------
 * `l`은 7개의 정수를 전달하며, 공유 토크나이저가 나머지를 처리합니다.
 *
 * @warning 호출자는 반드시 g_lock을 보유해야 합니다. 이 함수는 g_sounds[]를
 *          제자리에서 재작성하며, 믹서 스레드는 Voice::snd를 통해 그 배열을 직접
 *          역참조하기 때문입니다.
 * @note 레시피를 건드리기 전에 살아 있는 모든 보이스를 정지시킵니다. 보이스는 가공되지
 *       않은 `const Sound *`를 보유하므로, 배열이 재작성되고 나면 그 포인터는 절반만
 *       기록된 레시피를 가리키거나 재생 중이던 것과 완전히 다른 사운드를 가리키게
 *       됩니다. 먼저 정지시키는 것이 재작성을 안전하게 만드는 방법입니다. 핫 리로드는
 *       오디오 한 프레임을 잃지만 이는 귀에 들리지 않으며, 그 대안은 찢어진
 *       읽기였습니다.
 */
static void parse_sounds(void) {
    const char *p = data_text(DATA_SOUNDS);
    Sound *cur = 0;

    /* Drop every voice before the recipes they point into are rewritten.
       보이스가 가리키는 레시피가 재작성되기 전에 모든 보이스를 정지시킵니다. */
    for (int i = 0; i < MAX_VOICES; i++) g_voices[i].snd = 0;

    g_n_sounds = 0;

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        if (txt_is(t, len, "s")) {
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;
            /* Past MAX_SOUNDS `cur` stays null and the recipe is parsed but
               discarded, so the sound simply never plays. Reported because
               "that effect is missing" gives no hint that a cap was the
               cause. Safe to report here: parse_sounds only ever runs on the
               game thread, never on the mixer (see the threading contract).
               MAX_SOUNDS를 넘으면 `cur`이 널로 유지되어 레시피가 파싱되되 폐기되므로,
               해당 사운드는 아예 재생되지 않습니다. "그 효과음이 안 난다"는 증상만으로는
               용량 한계가 원인이라는 단서를 얻을 수 없어 보고합니다. 이 위치에서의
               보고는 안전합니다. parse_sounds는 오직 게임 스레드에서만 실행되며 믹서
               스레드에서는 결코 실행되지 않습니다(스레딩 계약 참조). */
            cur = (g_n_sounds < MAX_SOUNDS) ? &g_sounds[g_n_sounds++] : 0;
            if (!cur) DIAG(DIAG_SOUND_CAP);
            if (cur) {
                int i = 0;
                for (; i < len && i < NAME_LEN - 1; i++) cur->name[i] = nm[i];
                cur->name[i] = 0;
                cur->n = 0;
            }
            continue;
        }

        if (txt_is(t, len, "l")) {
            int v[7] = {0}, ok = 1;
            for (int i = 0; i < 7 && ok; i++) p = txt_read_int(p, &v[i], &ok);
            if (ok && cur && cur->n >= MAX_LAYERS) DIAG(DIAG_SOUND_CAP);
            if (ok && cur && cur->n < MAX_LAYERS) {
                Layer *L = &cur->layers[cur->n++];
                L->wave = (short)v[0]; L->ms  = (short)v[1];
                L->f0   = (short)v[2]; L->f1  = (short)v[3];
                L->atk  = (short)v[4]; L->dec = (short)v[5];
                L->vol  = (short)v[6];
            }
            continue;
        }
    }
    g_parsed = 1;
}

/**
 * @brief 이름으로 사운드 레시피를 찾습니다. 순수 조회이며 파싱하지 않습니다.
 *
 * ENGLISH
 * -------
 * @param[in] name 찾을 사운드의 이름.
 * @return 찾은 Sound 구조체에 대한 포인터, 없으면 NULL.
 * @warning Caller MUST hold g_lock: this walks g_sounds[], which parse_sounds
 *          rewrites in place.
 * @note Deliberately does NOT parse on demand. It used to, and that was a
 *       data race: audio_play called it OUTSIDE the lock, so a hot reload
 *       could have the game thread rewriting g_sounds[] from index 0 while
 *       the mixer thread was dereferencing Voice::snd into the very entries
 *       being overwritten. Parsing is now the caller's explicit job.
 *
 * 한국어
 * ------
 * @param[in] name 찾을 사운드의 이름.
 * @return 찾은 Sound 구조체에 대한 포인터, 없으면 NULL.
 * @warning 호출자는 반드시 g_lock을 보유해야 합니다. parse_sounds가 제자리에서
 *          재작성하는 g_sounds[]를 순회하기 때문입니다.
 * @note 의도적으로 요청 시 파싱을 수행하지 않습니다. 이전에는 파싱했으며 그것이
 *       데이터 레이스였습니다. audio_play가 이 함수를 락 *바깥에서* 호출했으므로,
 *       핫 리로드 시 게임 스레드가 g_sounds[]를 0번 인덱스부터 재작성하는 동안
 *       믹서 스레드가 바로 그 덮어써지는 항목들을 Voice::snd로 역참조할 수
 *       있었습니다. 이제 파싱은 호출자의 명시적 책임입니다.
 */
static const Sound *find_sound(const char *name) {
    for (int i = 0; i < g_n_sounds; i++) {
        const char *a = g_sounds[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return &g_sounds[i];
    }
    return 0;
}

/**
 * @brief 시드를 사용하여 -1.0에서 1.0 사이의 의사 난수를 생성합니다.
 * @param s 난수 시드에 대한 포인터. 업데이트됩니다.
 * @return float 타입의 난수.
 */
static float frand(unsigned *s) {
    *s = *s * 1664525u + 1013904223u;
    return ((*s >> 8) & 0xffff) / 32768.0f - 1.0f;
}

/**
 * @brief 지정된 파형에 대한 오실레이터 샘플을 생성합니다.
 * @param wave 파형 유형 (0: 사각파, 1: 톱니파, 2: 사인파, 3: 노이즈).
 * @param phase 현재 위상 (0.0 ~ 1.0).
 * @param hold 노이즈 파형을 위한 샘플 앤 홀드 값에 대한 포인터.
 * @param rng 난수 시드에 대한 포인터.
 * @return 오실레이터의 현재 샘플 값.
 */
static float osc(int wave, float phase, float *hold, unsigned *rng) {
    float frac = phase - (float)(int)phase;
    switch (wave) {
    case 0: return frac < 0.5f ? 1.0f : -1.0f;
    case 1: return frac * 2.0f - 1.0f;
    case 2: return sinf(frac * 6.2831853f);
    default:
        if (phase >= 1.0f) *hold = frand(rng);
        return *hold;
    }
}

/**
 * @brief 주어진 시간에 대한 AD(어택-디케이) 엔벨로프 값을 계산합니다.
 * @param L 엔벨로프 파라미터를 포함하는 레이어.
 * @param t_ms 경과 시간 (밀리초).
 * @return 계산된 엔벨로프 값 (0.0 ~ 1.0).
 */
static float envelope(const Layer *L, float t_ms) {
    float a = (L->atk > 0) ? t_ms / L->atk : 1.0f;
    if (a > 1.0f) a = 1.0f;
    float d = 1.0f;
    if (L->dec > 0) {
        float since = t_ms - L->atk;
        if (since > 0.0f) d = 1.0f - since / L->dec;
    }
    if (d < 0.0f) d = 0.0f;
    return a * d;
}

/**
 * @brief 단일 보이스를 지정된 프레임 수만큼 렌더링하여 `out` 버퍼에 추가합니다.
 * @param V 렌더링할 보이스.
 * @param out 출력 오디오 버퍼.
 * @param frames 렌더링할 프레임 수.
 * @return 레이어가 여전히 활성 상태이면 1, 그렇지 않으면 0.
 */
static int render_voice(Voice *V, short *out, int frames) {
    int still_alive = 0;

    for (int i = 0; i < frames; i++) {
        float t_ms = (V->pos + i) * 1000.0f / RATE;
        float acc = 0.0f;
        int alive = 0;

        for (int k = 0; k < V->snd->n; k++) {
            const Layer *L = &V->snd->layers[k];
            if (t_ms >= L->ms) continue;
            alive = 1;

            float u    = (L->ms > 0) ? t_ms / L->ms : 1.0f;
            float freq = L->f0 + (L->f1 - L->f0) * u;
            if (freq < 1.0f) freq = 1.0f;

            V->phase[k] += freq / RATE;
            float s = osc(L->wave, V->phase[k], &V->hold[k], &V->rng);
            if (V->phase[k] >= 1.0f) V->phase[k] -= (float)(int)V->phase[k];

            acc += s * envelope(L, t_ms) * (L->vol / 100.0f);
        }

        if (!alive) return 0;
        still_alive = 1;

        int s = out[i] + (int)(acc * (V->gain / 100.0f) * 8000.0f);
        out[i] = (short)(s >  32767 ?  32767 :
                         s < -32768 ? -32768 : s);
    }

    V->pos += frames;
    return still_alive;
}

/**
 * @brief 모든 활성 보이스를 믹싱하여 출력 버퍼에 씁니다.
 * @param out 출력 오디오 버퍼.
 * @param frames 믹싱할 프레임 수.
 */
static void mix(short *out, int frames) {
    for (int i = 0; i < frames; i++) out[i] = 0;

    EnterCriticalSection(&g_lock);
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *V = &g_voices[v];
        if (!V->snd) continue;
        if (!render_voice(V, out, frames)) V->snd = 0;
    }
    LeaveCriticalSection(&g_lock);
}

/**
 * @brief 오디오 믹싱을 처리하는 백그라운드 스레드 함수입니다.
 * @param param 스레드 파라미터 (사용되지 않음).
 * @return 스레드 종료 코드.
 */
static DWORD WINAPI mixer_thread(LPVOID param) {
    (void)param;
    while (g_running) {
        WaitForSingleObject(g_event, 100);
        for (int i = 0; i < NBUF; i++) {
            if (!(g_hdr[i].dwFlags & WHDR_DONE)) continue;
            mix(g_buf[i], FRAMES);
            waveOutWrite(g_dev, &g_hdr[i], sizeof(WAVEHDR));
        }
    }
    return 0;
}

/* --- 공개 API 함수 --- */

int audio_rate(void) { return RATE; }

int audio_render(const char *name, short *out, int max_frames) {
    /* Offline path, and the only public entry point that may run with no
       device open at all -- tools/sndtest.c never calls audio_init. Take the
       lock only when there is a mixer to race with; g_ready false means the
       critical section was never initialised and entering it is itself the
       bug. The voice below is a local, so nothing here publishes state to
       the mixer either way.

       오프라인 경로이며, 장치가 전혀 열리지 않은 상태에서 실행될 수 있는 유일한
       공개 진입점입니다. tools/sndtest.c는 audio_init을 호출하지 않습니다. 경쟁할
       믹서가 존재할 때만 락을 획득합니다. g_ready가 거짓이라는 것은 임계 영역이
       초기화된 적이 없다는 뜻이며, 그 상태에서 진입하는 것 자체가 버그입니다. 아래의
       보이스는 지역 변수이므로, 어느 쪽이든 믹서에 상태를 공개하지 않습니다. */
    int lock = g_ready;
    if (lock) EnterCriticalSection(&g_lock);

    if (!g_parsed) parse_sounds();
    const Sound *s = find_sound(name);

    if (lock) LeaveCriticalSection(&g_lock);
    if (!s || !s->n) return 0;

    Voice V = {0};
    V.snd  = s;
    V.gain = 100;
    V.rng  = 0x2545f491u;

    for (int i = 0; i < max_frames; i++) out[i] = 0;

    int done = 0;
    const int block = 256;
    while (done + block <= max_frames) {
        if (!render_voice(&V, out + done, block)) break;
        done += block;
    }
    return done;
}

int audio_init(void) {
    WAVEFORMATEX fmt = {0};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = 1;
    fmt.nSamplesPerSec  = RATE;
    fmt.wBitsPerSample  = 16;
    fmt.nBlockAlign     = 2;
    fmt.nAvgBytesPerSec = RATE * 2;

    g_event = CreateEventA(0, FALSE, FALSE, 0);
    if (!g_event) return 0;

    if (waveOutOpen(&g_dev, WAVE_MAPPER, &fmt, (DWORD_PTR)g_event, 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(g_event);
        g_event = 0;
        return 0;
    }

    InitializeCriticalSection(&g_lock);

    for (int i = 0; i < NBUF; i++) {
        g_hdr[i].lpData         = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = FRAMES * 2;
        waveOutPrepareHeader(g_dev, &g_hdr[i], sizeof(WAVEHDR));
        waveOutWrite(g_dev, &g_hdr[i], sizeof(WAVEHDR));
    }

    g_running = 1;
    g_thread = CreateThread(0, 0, mixer_thread, 0, 0, 0);
    if (!g_thread) { audio_shutdown(); return 0; }

    g_ready = 1;
    return 1;
}

void audio_shutdown(void) {
    if (!g_event) return;

    /* Close the gate FIRST. g_ready is what audio_play tests before entering
       g_lock, so it has to be false before the critical section is deleted --
       otherwise a call already past the test enters a deleted lock. The
       mixer is stopped and joined below, so once this is clear no thread can
       reach the lock again.

       가장 먼저 게이트를 닫습니다. g_ready는 audio_play가 g_lock에 진입하기 전에
       검사하는 값이므로, 임계 영역이 삭제되기 전에 반드시 거짓이어야 합니다. 그렇지
       않으면 이미 검사를 통과한 호출이 삭제된 락에 진입하게 됩니다. 아래에서 믹서를
       정지시키고 합류시키므로, 이 값이 해제된 뒤에는 어떤 스레드도 락에 다시 도달할
       수 없습니다. */
    int was_ready = g_ready;
    g_ready = 0;

    g_running = 0;
    if (g_thread) {
        SetEvent(g_event);
        WaitForSingleObject(g_thread, 500);
        CloseHandle(g_thread);
        g_thread = 0;
    }
    if (g_dev) {
        waveOutReset(g_dev);
        for (int i = 0; i < NBUF; i++)
            waveOutUnprepareHeader(g_dev, &g_hdr[i], sizeof(WAVEHDR));
        waveOutClose(g_dev);
        g_dev = 0;
    }
    /* Safe now: the mixer has been joined and the gate is shut. */
    if (was_ready) DeleteCriticalSection(&g_lock);
    CloseHandle(g_event);
    g_event = 0;
}

void audio_reload(void) {
    /* g_parsed is shared with the mixer's view of g_sounds[], so clearing it
       takes the lock like every other access to the recipe table. The actual
       reparse happens on the next audio_play, on the game thread, with the
       lock held -- never on the mixer thread.

       g_parsed는 믹서가 바라보는 g_sounds[]와 공유되는 상태이므로, 이 값을 해제할
       때도 레시피 테이블에 대한 다른 모든 접근과 마찬가지로 락을 획득합니다. 실제
       재파싱은 다음 audio_play 시점에 락을 보유한 게임 스레드에서 수행되며, 믹서
       스레드에서는 결코 일어나지 않습니다. */
    if (!g_ready) { g_parsed = 0; return; }   /* device never opened; no mixer to race */
    EnterCriticalSection(&g_lock);
    g_parsed = 0;
    LeaveCriticalSection(&g_lock);
}

/* Threading contract
 * ------------------
 * ENGLISH: g_sounds[], g_n_sounds, g_parsed and g_voices[] are all shared
 * with the mixer thread, which dereferences Voice::snd straight into
 * g_sounds[]. Every one of them is touched only under g_lock. The lookup and
 * the parse both sit INSIDE the critical section here -- an earlier version
 * had find_sound() outside it, which let a hot reload rewrite the recipe
 * table from index 0 while the mixer was reading the entries being
 * overwritten.
 *
 * Holding the lock across a parse is acceptable because it happens at most
 * once per hot reload, never in steady state: g_parsed gates it, and the
 * mixer's worst case is one late buffer, which the 4-buffer / ~46ms queue
 * absorbs without a dropout.
 *
 * 한국어: g_sounds[], g_n_sounds, g_parsed, g_voices[]는 모두 믹서 스레드와
 * 공유되며, 믹서는 Voice::snd를 통해 g_sounds[]를 직접 역참조합니다. 이들 전부는
 * g_lock 하에서만 접근됩니다. 조회와 파싱이 모두 임계 영역 *안에* 위치합니다. 이전
 * 버전은 find_sound()가 바깥에 있었고, 그 탓에 핫 리로드가 레시피 테이블을 0번
 * 인덱스부터 재작성하는 동안 믹서가 덮어써지는 항목을 읽을 수 있었습니다.
 *
 * 파싱 동안 락을 보유하는 것은 허용됩니다. 이는 핫 리로드당 최대 한 번만 발생하고
 * 정상 상태에서는 결코 일어나지 않기 때문입니다. g_parsed가 이를 통제하며, 믹서의
 * 최악의 경우는 버퍼 하나가 늦어지는 것인데 4버퍼 약 46ms 큐가 끊김 없이 이를
 * 흡수합니다.
 */
void audio_play(const char *name, int gain) {
    if (!g_ready) return;

    EnterCriticalSection(&g_lock);

    /* Parse on demand, but on THIS thread and under the lock. find_sound no
       longer does this for us, precisely so it cannot happen unlocked.
       요청 시 파싱하되, *이* 스레드에서 락을 보유한 채 수행합니다. find_sound는 더
       이상 이 작업을 대신하지 않으며, 이는 락 없이 파싱이 일어나지 않도록 하기 위한
       것입니다. */
    if (!g_parsed) parse_sounds();

    const Sound *s = find_sound(name);
    if (!s || !s->n) { LeaveCriticalSection(&g_lock); return; }

    int slot = -1, oldest = -1, oldest_pos = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!g_voices[i].snd) { slot = i; break; }
        if (g_voices[i].pos > oldest_pos) { oldest_pos = g_voices[i].pos; oldest = i; }
    }
    if (slot < 0) slot = oldest;

    Voice *V = &g_voices[slot];
    V->snd  = s;
    V->pos  = 0;
    V->gain = gain < 0 ? 0 : (gain > 100 ? 100 : gain);
    V->rng  = 0x2545f491u + (unsigned)slot * 2654435761u;
    for (int k = 0; k < MAX_LAYERS; k++) { V->phase[k] = 0.0f; V->hold[k] = 0.0f; }
    LeaveCriticalSection(&g_lock);
}
