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
/* Raised from 24, which was EXACTLY the number of sounds that existed -- so
   the first sound added past it was dropped, and the one that went missing was
   not the new one but `switch`, whichever happened to land last. A cap that
   equals the current count is a cap with no headroom and no warning: it is
   indistinguishable from a correct limit right up until someone adds a line to
   a text file.
   Reported through DIAG_SOUND_CAP, and reported is not the same as survived:
   the audible result is a door that stops clicking, which reads as a bug in
   doors.
   24에서 올렸습니다. 24는 당시 존재하던 사운드의 수와 *정확히* 같았으므로, 그것을 넘어
   추가된 첫 사운드가 버려졌고 사라진 것은 새 사운드가 아니라 마지막에 놓인 `switch`
   였습니다. 현재 개수와 같은 상한은 여유도 경고도 없는 상한입니다. 누군가 텍스트 파일에
   한 줄을 더할 때까지는 올바른 제한과 구별되지 않습니다. DIAG_SOUND_CAP으로 보고되지만
   보고와 생존은 다릅니다. 귀에 들리는 결과는 더 이상 딸깍이지 않는 문이며, 그것은 문의
   결함처럼 읽힙니다. */
#define MAX_SOUNDS  AUDIO_MAX_SOUNDS  ///< @brief 캐시할 수 있는 최대 사운드 레시피 수.
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
    /* A RECIPE OR A SAMPLE, whichever the library gave this name.
       pcm_n > 0 means Freedoom recorded this one and the layers are ignored;
       the layers stay because deleting the WAV brings the recipe straight
       back, which is the same bargain the drawn sprites strike with the
       generated creatures. `pump`, `hook` and `hreel` have no Doom equivalent
       and are recipes for good.
       레시피이거나 샘플입니다. pcm_n > 0이면 Freedoom이 녹음한 것이고 레이어는
       무시됩니다. 레이어를 남겨 두는 이유는 WAV를 지우면 레시피가 곧바로 돌아오기
       때문이며, 그려진 스프라이트가 생성된 생물과 맺는 것과 같은 거래입니다. */
    int   pcm_at;                /**< g_pcm 내의 시작 위치. / offset into g_pcm */
    int   pcm_n;                 /**< 샘플 수. 0이면 레시피입니다. / samples; 0 = recipe */
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
 * THE GATE ON g_lock's OWN VALIDITY. audio_shutdown clears it from the game
 * thread while the mixer may still be running, and once it is clear the
 * critical section has been deleted and must not be entered.
 *
 * READ AND WRITTEN THROUGH ::flag_get AND ::flag_set, not as a plain variable,
 * and it used to be `volatile` instead. That was borrowed from MSVC, where
 * volatile carries acquire/release semantics by default. It does not in GCC,
 * which is what builds this: there volatile means only "do not cache it in a
 * register", and orders nothing with respect to the surrounding
 * non-volatile writes. The claim this flag makes is an ORDERING claim -- a
 * thread that sees it set must also see an initialised g_lock and an open
 * device, and a thread that sees it clear must not be about to enter either.
 * That needs a release on the way out and an acquire on the way in, which is
 * what the two helpers below are.
 *
 * It worked, and would have gone on working: x86 does not reorder stores with
 * stores or loads with loads, so the hardware supplied for free what the source
 * never asked for. Nothing here is a bug report. It is a claim that was true
 * about the machine rather than about the program.
 *
 * 한국어
 * ------
 * g_lock 자체의 유효성을 결정하는 게이트입니다. audio_shutdown이 믹서가 아직 실행 중일 수
 * 있는 상태에서 게임 스레드로부터 이 값을 해제하며, 해제된 이후에는 임계 영역이 삭제된
 * 상태이므로 진입해서는 안 됩니다.
 *
 * 평범한 변수가 아니라 ::flag_get과 ::flag_set을 통해 읽고 씁니다. 이전에는 대신
 * `volatile`이었습니다. 그것은 MSVC에서 빌려 온 것으로, 그곳에서는 volatile이 기본적으로
 * 획득·해제 의미론을 가집니다. 이것을 빌드하는 GCC에서는 그렇지 않습니다. 그곳에서 volatile은
 * "레지스터에 캐시하지 말라"는 뜻일 뿐이며 주변의 비휘발성 쓰기에 대해 아무 순서도 정하지
 * 않습니다. 이 플래그가 하는 주장은 *순서*에 대한 주장입니다. 이것이 설정된 것을 본 스레드는
 * 초기화된 g_lock과 열린 장치도 보아야 하고, 해제된 것을 본 스레드는 그 어느 쪽에도 진입하려
 * 해서는 안 됩니다. 그러려면 나갈 때 해제, 들어올 때 획득이 필요하며, 아래 두 헬퍼가 그것입니다.
 *
 * 동작했고 앞으로도 동작했을 것입니다. x86은 저장을 저장과, 적재를 적재와 재배열하지 않으므로
 * 하드웨어가 소스 코드는 요청한 적 없는 것을 공짜로 공급했습니다. 이것은 버그 보고가 아닙니다.
 * 프로그램이 아니라 기계에 대해 참이던 주장입니다.
 */
static LONG g_ready;

// 오디오 백엔드
static HWAVEOUT g_dev;
static HANDLE   g_event, g_thread;
static WAVEHDR  g_hdr[NBUF];
static short    g_buf[NBUF][FRAMES];
/* Read by the mixer thread's loop and cleared by the game thread. Same
   treatment as g_ready and for the same reason; see the note above it.
   믹서 스레드의 루프가 읽고 게임 스레드가 해제합니다. g_ready와 같은 처리이며 이유도
   같습니다. 그 위의 설명을 참조하십시오. */
static LONG g_running;

/**
 * @brief Reads a cross-thread flag, and everything the writer wrote before it.
 *
 * ENGLISH
 * -------
 * Acquire, so a thread that sees the flag set also sees the critical section,
 * the device and the buffers that were prepared before it was set. On x86 this
 * compiles to the same plain load `volatile` did -- the barrier is against the
 * COMPILER reordering, which is the part that was never guaranteed.
 *
 * 한국어
 * ------
 * @brief 스레드 간 플래그를, 기록자가 그 이전에 쓴 모든 것과 함께 읽습니다.
 *
 * 획득 의미론입니다. 플래그가 설정된 것을 본 스레드는 그것이 설정되기 전에 준비된 임계 영역과
 * 장치와 버퍼도 봅니다. x86에서는 `volatile`이 만들던 것과 같은 평범한 적재로 컴파일됩니다.
 * 장벽이 막는 것은 *컴파일러*의 재배열이며, 그 부분이 보장된 적 없던 것입니다.
 */
static int flag_get(LONG *p) {
    return (int)__atomic_load_n(p, __ATOMIC_ACQUIRE);
}

/**
 * @brief Publishes a cross-thread flag, and everything written before it.
 *
 * ENGLISH: Release, so nothing the compiler could have moved past this write
 * becomes visible to a reader before the write itself.
 *
 * 한국어: 해제 의미론입니다. 컴파일러가 이 쓰기 너머로 옮길 수 있었을 무엇도, 그 쓰기 자체보다
 * 먼저 판독자에게 보이지 않게 됩니다.
 */
static void flag_set(LONG *p, int v) {
    __atomic_store_n(p, (LONG)v, __ATOMIC_RELEASE);
}

/* --- 정적 함수 선언 --- */
static void parse_sounds(void);
static void parse_text(const char *text, int want_layers);
static const Sound *find_sound(const char *name);
static float frand(unsigned *s);
static float osc(int wave, float phase, float *hold, unsigned *rng);
static float envelope(const Layer *L, float t_ms);
static int render_voice(Voice *V, short *out, int frames);
static void mix(short *out, int frames);
static DWORD WINAPI mixer_thread(LPVOID param);

/* --- 정적 함수 구현 --- */

/* --- Sampled sounds / 샘플 사운드 -----------------------------------------
 *
 * 4-bit IMA ADPCM at 11025Hz, decoded once at load into one shared buffer.
 * Decoding per voice instead would redo the same work for every shot fired,
 * and ADPCM is inherently serial -- you cannot start in the middle, because
 * each sample is a delta from the one before it. That also means a voice
 * cannot seek, which is fine: these are one-shot effects.
 *
 * 11025 is a quarter of the mixer's 44100, so playback holds each source
 * sample for four output samples. No resampler, no accumulating phase error,
 * and the ratio is Doom's own rather than a number chosen here.
 *
 * 4비트 IMA ADPCM(11025Hz)이며, 로드 시 한 번 디코딩해 공유 버퍼에 넣습니다. 보이스마다
 * 디코딩하면 발사할 때마다 같은 일을 되풀이하게 되고, ADPCM은 본질적으로 순차적이라
 * 중간에서 시작할 수 없습니다. 각 샘플이 앞 샘플로부터의 차이이기 때문입니다.
 * 11025는 믹서의 44100의 정확히 4분의 1이므로 재생은 원본 샘플 하나를 출력 네 샘플 동안
 * 유지합니다. 리샘플러도, 누적되는 위상 오차도 없습니다. */

#define PCM_MAX     220000   /**< 디코딩된 샘플의 상한. / decoded sample ceiling */
#define PCM_STEP    (RATE / 11025)  /**< 출력 샘플 당 원본 샘플. / 4 */

static short g_pcm[PCM_MAX];
static int   g_pcm_used;

/* The IMA tables, shared by encoder and decoder. bake.ps1 holds the only other
   copy, and sndtest compares the two by decoding text the script produced.
   IMA 테이블입니다. 다른 사본은 bake.ps1에만 있으며, sndtest가 그 스크립트가 만든
   텍스트를 디코딩해 둘을 비교합니다. */
static const short PCM_STEP_TAB[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,
    80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
    494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,
    2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,
    8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767
};
static const signed char PCM_NEXT_TAB[16] = {
    -1,-1,-1,-1, 2, 4, 6, 8, -1,-1,-1,-1, 2, 4, 6, 8
};

/* The same 64-character alphabet the sprite codec uses; sndtest asserts that
   this and bake.ps1's agree, because nothing else can. */
static int pcm_b64(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '-') return 63;
    return -1;
}

#ifdef HOT_RELOAD
int audio_b64val(char c) { return pcm_b64(c); }
#endif

/* Decode `n` samples of packed ADPCM into g_pcm. Returns how many landed,
   which is short of `n` only when the buffer is full or the text ran out --
   both reported, neither fatal, because a truncated sound is better than a
   silent game. */
static int pcm_decode(const char *data, int len, int n, int at) {
    int pred = 0, ix = 0, got = 0;

    for (int i = 0; i + 1 < len && got < n; i += 2) {
        int hi = pcm_b64(data[i]), lo = pcm_b64(data[i + 1]);
        if (hi < 0 || lo < 0) break;
        int v = (hi << 6) | lo;                    /* three nibbles, 12 bits */

        for (int k = 8; k >= 0 && got < n; k -= 4) {
            int code = (v >> k) & 15;
            int st   = PCM_STEP_TAB[ix];

            int d = st >> 3;
            if (code & 4) d += st;
            if (code & 2) d += st >> 1;
            if (code & 1) d += st >> 2;
            pred += (code & 8) ? -d : d;
            if (pred >  32767) pred =  32767;
            if (pred < -32768) pred = -32768;

            ix += PCM_NEXT_TAB[code];
            if (ix < 0)  ix = 0;
            if (ix > 88) ix = 88;

            if (at + got < PCM_MAX) g_pcm[at + got] = (short)pred;
            got++;
        }
    }
    return got;
}

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
    /* Recipes come from data_text, which a dev build points at the file so
       they can be tuned live. Samples come from the BAKED text either way:
       they are ADPCM the bake produced from WAVs, so the file has none, and
       reading only the file made a hot-reload build hear recipes where the
       shipped build played samples.
       Parsed second so its `s <name>` lines reopen the recipes rather than
       replacing them -- select-or-create is what lets a sample attach to the
       sound the recipe already made.
       레시피는 data_text에서 옵니다. 개발 빌드는 이를 파일로 향하게 해 실시간 조정을
       가능하게 합니다. 샘플은 어느 쪽이든 *베이크된* 텍스트에서 옵니다. 베이크가 WAV로
       만든 ADPCM이라 파일에는 없으며, 파일만 읽으면 핫 리로드 빌드는 레시피를, 배포
       빌드는 샘플을 재생하게 됩니다. */
    parse_text(data_text(DATA_SOUNDS), 1);
    if (data_text(DATA_SOUNDS) != data_baked(DATA_SOUNDS))
        parse_text(data_baked(DATA_SOUNDS), 0);
    g_parsed = 1;
}

/* want_layers is 0 for the second pass over the baked text: the recipes there
   are the same ones already parsed, and appending their layers again would
   overflow MAX_LAYERS and report a cap that was never really reached.
   두 번째 패스에서는 want_layers가 0입니다. 그곳의 레시피는 이미 파싱한 것과 같으며,
   레이어를 다시 덧붙이면 MAX_LAYERS를 넘겨 실제로는 닿은 적 없는 한계를 보고합니다. */
static void parse_text(const char *text, int want_layers) {
    const char *p = text;
    Sound *cur = 0;

    /* Only the first pass clears. The second exists to ATTACH samples to the
       sounds the first made, so clearing again would throw them away and then
       recreate them from the baked recipes -- which works, but silently makes
       the file's edits irrelevant in the build that exists to honour them.
       첫 번째 패스만 초기화합니다. 두 번째 패스는 첫 번째가 만든 사운드에 샘플을
       *붙이기* 위해 존재하므로, 다시 초기화하면 그것을 버리고 베이크된 레시피로부터
       다시 만들게 됩니다. 동작은 하지만, 파일의 수정을 존중하려고 존재하는 빌드에서
       그 수정을 조용히 무의미하게 만듭니다. */
    if (want_layers) {
        /* Drop every voice before the recipes they point into are rewritten.
           보이스가 가리키는 레시피가 재작성되기 전에 모든 보이스를 정지시킵니다. */
        for (int i = 0; i < MAX_VOICES; i++) g_voices[i].snd = 0;
        g_n_sounds = 0;
        g_pcm_used = 0;
    }

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
            /* SELECT OR CREATE. A name that already has a recipe is
               reopened rather than duplicated, which is how a sampled sound
               attaches itself to the entry the recipe made: bake emits the
               samples after the recipes, so `s shot` here finds the shotgun
               recipe and the `w` line below gives it audio.
               선택하거나 생성합니다. 이미 레시피를 가진 이름은 복제되지 않고 다시
               열리며, 그것이 샘플 사운드가 레시피가 만든 항목에 붙는 방법입니다. */
            cur = 0;
            for (int i = 0; i < g_n_sounds; i++) {
                int j = 0;
                while (j < len && g_sounds[i].name[j] &&
                       g_sounds[i].name[j] == nm[j]) j++;
                if (j == len && !g_sounds[i].name[j]) { cur = &g_sounds[i]; break; }
            }
            if (!cur) {
                cur = (g_n_sounds < MAX_SOUNDS) ? &g_sounds[g_n_sounds++] : 0;
                if (!cur) DIAG(DIAG_SOUND_CAP);
                if (cur) {
                    int i = 0;
                    for (; i < len && i < NAME_LEN - 1; i++) cur->name[i] = nm[i];
                    cur->name[i] = 0;
                    cur->n = 0;
                    cur->pcm_at = cur->pcm_n = 0;
                }
            }
            continue;
        }

        /* w <samples> <packed ADPCM> -- see pcm_decode. */
        if (txt_is(t, len, "w")) {
            int n = 0, ok = 1;
            p = txt_read_int(p, &n, &ok);
            const char *d = txt_token(p, &len);
            if (!ok || !d) break;
            p = d + len;
            if (cur && n > 0) {
                int got = pcm_decode(d, len, n, g_pcm_used);
                if (got < n) DIAG(DIAG_SOUND_CAP);
                cur->pcm_at = g_pcm_used;
                cur->pcm_n  = got;
                g_pcm_used += got;
            }
            continue;
        }

        if (txt_is(t, len, "l")) {
            int v[7] = {0}, ok = 1;
            if (!want_layers) {
                /* Consume it so the stream stays in sync, then discard. */
                for (int i = 0; i < 7 && ok; i++) p = txt_read_int(p, &v[i], &ok);
                continue;
            }
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

    /* A SAMPLE PLAYS ITSELF; the oscillators below are for recipes only.
       One source sample every PCM_STEP output samples, held rather than
       interpolated: at 11025 into 44100 the alternative is a lowpass nobody
       asked for, and Doom's own sounds were authored to be heard this way.
       샘플은 스스로 재생됩니다. 아래의 오실레이터는 레시피 전용입니다. 출력
       PCM_STEP 샘플마다 원본 한 샘플을 내보내며, 보간하지 않고 유지합니다. */
    if (V->snd->pcm_n > 0) {
        for (int i = 0; i < frames; i++) {
            int idx = (V->pos + i) / PCM_STEP;
            if (idx >= V->snd->pcm_n) { V->pos += i; return 0; }
            int s = out[i] + (int)(g_pcm[V->snd->pcm_at + idx] *
                                   (V->gain / 100.0f) * 0.55f * AUDIO_MASTER);
            out[i] = (short)(s >  32767 ?  32767 :
                             s < -32768 ? -32768 : s);
        }
        V->pos += frames;
        return 1;
    }

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

        int s = out[i] + (int)(acc * (V->gain / 100.0f) * 8000.0f * AUDIO_MASTER);
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
    while (flag_get(&g_running)) {
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
    int lock = flag_get(&g_ready);
    if (lock) EnterCriticalSection(&g_lock);

    if (!g_parsed) parse_sounds();
    const Sound *s = find_sound(name);

    if (lock) LeaveCriticalSection(&g_lock);
    /* The same test audio_play makes, and it was wrong here too: a sampled
       sound has no layers, so this returned 0 frames for every sound that came
       from a WAV. Two copies of one rule is how one of them stays wrong -- the
       playback path was fixed and the render path, which is what the tests
       drive, went on reporting silence.
       audio_play와 같은 판정이며 이곳에서도 틀려 있었습니다. 하나의 규칙이 두 벌 있으면
       그중 하나는 계속 틀린 채로 남습니다. 재생 경로는 고쳤는데 테스트가 구동하는
       렌더 경로는 계속 무음을 보고하고 있었습니다. */
    if (!s || (!s->n && s->pcm_n <= 0)) return 0;

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

    flag_set(&g_running, 1);
    g_thread = CreateThread(0, 0, mixer_thread, 0, 0, 0);
    if (!g_thread) { audio_shutdown(); return 0; }

    /* LAST, and that is the ordering this flag exists to express: everything
       it gates -- the lock, the device, the prepared headers, the running mixer
       -- is in place above, and the release makes all of it visible to whoever
       sees this set.
       마지막이며, 그것이 이 플래그가 표현하려는 순서입니다. 이것이 여는 모든 것(락, 장치,
       준비된 헤더, 실행 중인 믹서)이 위에서 갖춰졌고, 해제 의미론이 그 전부를 이 값이 설정된
       것을 본 쪽에게 보이게 합니다. */
    flag_set(&g_ready, 1);
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
    int was_ready = flag_get(&g_ready);
    flag_set(&g_ready, 0);

    flag_set(&g_running, 0);
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
    if (!flag_get(&g_ready)) { g_parsed = 0; return; }   /* device never opened; no mixer to race */
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
/* Where the player's ears are. Game thread only -- see audio.h.
   플레이어의 귀 위치입니다. 게임 스레드 전용입니다. */
static v3 g_listener;

void audio_listener(v3 pos) { g_listener = pos; }

static void play_gain(const char *name, int gain) {
    if (!flag_get(&g_ready)) return;

    EnterCriticalSection(&g_lock);

    /* Parse on demand, but on THIS thread and under the lock. find_sound no
       longer does this for us, precisely so it cannot happen unlocked.
       요청 시 파싱하되, *이* 스레드에서 락을 보유한 채 수행합니다. find_sound는 더
       이상 이 작업을 대신하지 않으며, 이는 락 없이 파싱이 일어나지 않도록 하기 위한
       것입니다. */
    if (!g_parsed) parse_sounds();

    /* A sample has no layers, so `!s->n` alone rejected every sound that
       came from a WAV rather than a recipe -- which is why the door, the
       switch and the keycard made no noise at all after they were imported.
       샘플에는 레이어가 없으므로 `!s->n`만으로는 레시피가 아니라 WAV에서 온 모든
       사운드를 거부했고, 그래서 문과 스위치와 열쇠는 이식된 뒤에도 아무 소리를 내지
       않았습니다. */
    const Sound *s = find_sound(name);
    if (!s || (!s->n && s->pcm_n <= 0)) { LeaveCriticalSection(&g_lock); return; }

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

void audio_play(const char *name, int gain) { play_gain(name, gain); }

int audio_sound_count(void) {
    /* Parses on demand, because a tool may ask before anything has played.
       Everything else that reads the table goes through audio_play, which
       parses first; this is the one caller that does not.
       아무것도 재생되기 전에 도구가 물어볼 수 있으므로 필요 시 파싱합니다. 표를 읽는
       다른 모든 경로는 먼저 파싱하는 audio_play를 거치며, 이것이 그러지 않는 유일한
       호출자입니다. */
    if (!g_parsed) parse_sounds();
    return g_n_sounds;
}

/* The curve, in one place, so audio_play_at and the test that checks it
   cannot drift apart. Returns what `gain` is worth from `pos`.
   곡선을 한 곳에 둡니다. audio_play_at과 그것을 검사하는 테스트가 어긋날 수 없도록
   하기 위함입니다. */
static int gain_at(int gain, v3 pos) {
    float dx = pos.x - g_listener.x;
    float dy = pos.y - g_listener.y;
    float dz = pos.z - g_listener.z;
    float d2 = dx * dx + dy * dy + dz * dz;

    /* Compared squared, so nothing beyond earshot pays for a square root.
       제곱으로 비교하므로 가청 범위 밖의 것은 제곱근 비용을 치르지 않습니다. */
    if (d2 >= AUDIO_FAR * AUDIO_FAR) return 0;
    if (d2 <= AUDIO_NEAR * AUDIO_NEAR) return gain;

    float d = sqrtf(d2);
    float k = (AUDIO_FAR - d) / (AUDIO_FAR - AUDIO_NEAR);
    return (int)(gain * k + 0.5f);
}

#ifdef HOT_RELOAD
int audio_gain_at(int gain, v3 pos) { return gain_at(gain, pos); }
#endif

void audio_play_at(const char *name, int gain, v3 pos) {
    int g = gain_at(gain, pos);

    /* A sound that rounded to nothing still costs a voice, and voices are the
       scarce thing in a firefight: twelve of them, and the allocator evicts
       the OLDEST, so a distant inaudible shot can silence a near one.
       0으로 반올림된 소리도 보이스를 차지하며, 총격전에서 희소한 것이 바로 보이스입니다.
       열두 개뿐이고 할당기는 가장 *오래된* 것을 밀어내므로, 들리지도 않는 먼 총성이
       가까운 소리를 끊을 수 있습니다. */
    if (g > 0) play_gain(name, g);
}
