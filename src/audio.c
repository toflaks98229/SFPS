/**
 * @file audio.c
 * @brief Parses sound recipes, decodes sampled sounds, and mixes the voices.
 *
 * ENGLISH
 * -------
 * The synthesis half of the audio system; audio_win32.c owns the device and
 * calls ::audio_mix from its own thread. Nothing here opens anything, which is
 * what lets tools/sndtest.c render a sound to a buffer with no device at all.
 *
 * A sound is EITHER a recipe or a sample, never both at once. A recipe is a
 * handful of integers naming an oscillator and an envelope, and it costs about
 * thirty bytes where the PCM would cost sixty kilobytes. A sample is 4-bit
 * ADPCM the bake produced from a WAV, for the sounds that were recorded rather
 * than described. ::Sound carries the fields for both and `pcm_n` decides,
 * so deleting a WAV brings its recipe straight back.
 *
 * TWO THREADS TOUCH THIS FILE. The game thread parses and starts sounds; the
 * mixer thread renders them. The recipe table and the voice table are shared
 * between them and every access is under the device lock -- the contract is
 * written out above the statics below, and the two races it describes are
 * bugs this file has actually had.
 *
 * @note The volume settings and the listener position are game-thread state
 *       that the mixer never reads. A voice stores the gain it was handed and
 *       never looks at the world again.
 * @warning Every `const Sound *` a voice holds points into ::g_sounds, which a
 *          reload rewrites in place. Anything that rewrites that table must
 *          silence the voices first.
 *
 * 한국어
 * ------
 * 오디오 시스템의 합성 절반입니다. 장치는 audio_win32.c가 소유하며 자기 스레드에서
 * ::audio_mix를 호출합니다. 이곳에서는 아무것도 열지 않으며, 그 덕분에 tools/sndtest.c가
 * 장치 없이도 소리를 버퍼에 렌더링할 수 있습니다.
 *
 * 사운드는 레시피이거나 샘플이며, 동시에 둘일 수는 없습니다. 레시피는 오실레이터와 엔벨로프를
 * 지정하는 정수 몇 개이고, PCM이 60킬로바이트를 쓸 자리에 약 30바이트를 씁니다. 샘플은
 * 서술된 것이 아니라 녹음된 소리를 위해 베이크가 WAV에서 만든 4비트 ADPCM입니다. ::Sound는
 * 양쪽 필드를 모두 지니고 `pcm_n`이 판정하므로, WAV를 지우면 레시피가 곧바로 돌아옵니다.
 *
 * *두 스레드가 이 파일을 건드립니다.* 게임 스레드는 파싱하고 소리를 시작하며, 믹서 스레드는
 * 그것을 렌더링합니다. 레시피 표와 보이스 표는 둘 사이에 공유되고 모든 접근은 장치 락
 * 아래에서 이루어집니다. 계약은 아래 정적 변수들 위에 적어 두었으며, 그것이 서술하는 두
 * 레이스는 이 파일이 실제로 겪은 결함입니다.
 *
 * @note 음량 설정과 청취자 위치는 믹서가 결코 읽지 않는 게임 스레드 상태입니다. 보이스는
 *       건네받은 게인을 저장할 뿐 다시는 월드를 보지 않습니다.
 * @warning 보이스가 쥔 모든 `const Sound *`는 ::g_sounds를 가리키며, 재적재는 그 표를
 *          제자리에서 다시 씁니다. 그 표를 다시 쓰는 쪽은 먼저 보이스를 정지시켜야 합니다.
 */

#include "audio.h"

/* WIN32_LEAN_AND_MEAN predates this file dropping windows.h; it is defined
   before any system header so it would still apply if one came back.
   WIN32_LEAN_AND_MEAN은 이 파일이 windows.h를 걷어내기 전부터 있던 것입니다. 시스템 헤더가
   다시 들어오더라도 적용되도록 그보다 앞서 정의합니다. */
#define WIN32_LEAN_AND_MEAN
#include <math.h>

#include "audio_dev.h"   /* RATE/FRAMES/NBUF, audio_mix, and the lock */
#include "data.h"
#include "txt.h"
#include "diag.h"

/* --- Recipe shape / 레시피의 형태 --- */

/**
 * @brief Oscillator layers one recipe may stack.
 *
 * ENGLISH: Six is what the loudest sound in the library needs. A recipe past
 * it is parsed, clamped and reported through ::DIAG_SOUND_CAP rather than
 * silently truncated, because a layer that vanished changes the character of a
 * sound without making it absent -- the hardest kind of change to notice.
 *
 * 한국어: 라이브러리에서 가장 복잡한 소리가 필요로 하는 수가 6입니다. 이를 넘는 레시피는
 * 조용히 잘리지 않고 파싱·제한된 뒤 ::DIAG_SOUND_CAP으로 보고됩니다. 사라진 레이어는 소리를
 * 없애지 않으면서 성격만 바꾸며, 그것이 가장 알아채기 어려운 변화이기 때문입니다.
 */
#define MAX_LAYERS  6

/**
 * @brief Longest sound name the table stores, terminator included.
 *
 * ENGLISH: Names longer than this are truncated on the way in, so two sounds
 * whose names agree for the first fifteen characters would collide. Every name
 * in the library is far shorter.
 *
 * 한국어: 이보다 긴 이름은 들어오는 길에 잘리므로, 앞의 15자가 같은 두 사운드는 충돌합니다.
 * 라이브러리의 모든 이름은 그보다 훨씬 짧습니다.
 */
#define NAME_LEN    16

/**
 * @brief Ceiling on cached sound recipes.
 *
 * ENGLISH
 * -------
 * Raised twice, and both times from a cap that EXACTLY equalled the number of
 * sounds that existed. A cap with no headroom is indistinguishable from a
 * correct limit right up until somebody adds a line to a text file: the first
 * sound past it is dropped, and the one that goes missing is not the new one
 * but whichever happened to land last. The last time, that was `switch`, and
 * the symptom was a door that stopped clicking -- which reads as a bug in
 * doors.
 *
 * @note Reported through ::DIAG_SOUND_CAP, and reported is not the same as
 *       survived. ::AUDIO_MAX_SOUNDS is public so a test can check the recipe
 *       text against the cap BY NAME instead of waiting for that symptom.
 * @note One ::Sound is roughly a hundred bytes and the array lives in .bss, so
 *       headroom here costs nothing on disk.
 *
 * 한국어
 * ------
 * @brief 캐시할 수 있는 사운드 레시피의 상한.
 *
 * 두 번 올렸고, 두 번 모두 존재하던 사운드 수와 *정확히* 같은 상한에서 올렸습니다. 여유가
 * 없는 상한은 누군가 텍스트 파일에 한 줄을 더할 때까지는 올바른 제한과 구별되지 않습니다.
 * 그것을 넘는 첫 사운드가 버려지고, 사라지는 것은 새 사운드가 아니라 마지막에 놓인 것입니다.
 * 지난번에는 그것이 `switch`였고, 증상은 더 이상 딸깍이지 않는 문이었습니다. 그것은 문의
 * 결함처럼 읽힙니다.
 *
 * @note ::DIAG_SOUND_CAP으로 보고되지만 보고와 생존은 다릅니다. ::AUDIO_MAX_SOUNDS를
 *       공개해 두었으므로, 테스트가 그 증상을 기다리는 대신 레시피 텍스트를 상한과
 *       *이름으로* 대조할 수 있습니다.
 * @note ::Sound 하나는 약 100바이트이고 배열은 .bss에 있으므로, 이곳의 여유는 디스크
 *       용량을 소모하지 않습니다.
 */
#define MAX_SOUNDS  AUDIO_MAX_SOUNDS

/* --- Voice budget / 보이스 예산 --- */

/**
 * @brief Sounds that may be audible at once, music and effects together.
 *
 * ENGLISH: Twelve, split below rather than shared. Each voice is a few hundred
 * bytes of .bss and a loop iteration per buffer, so the number is bounded by
 * what is worth hearing rather than by what is affordable.
 *
 * 한국어: 열둘이며, 공유하지 않고 아래에서 분할합니다. 보이스 하나는 .bss 수백 바이트와
 * 버퍼당 루프 한 번이므로, 이 수는 감당할 수 있는 양이 아니라 들을 가치가 있는 양으로
 * 정해집니다.
 */
#define MAX_VOICES  12

/**
 * @brief Voices effects may use, the rest being reserved for music.
 *
 * ENGLISH
 * -------
 * MUSIC GETS ITS OWN, and the reason is the allocator: when every voice is
 * busy it evicts the OLDEST, and a music track is a stream of notes that are
 * always older than the shotgun that just fired. Sharing one pool would have
 * the music cut the gunfire, or -- with the eviction the other way -- a
 * firefight cut the music. Neither is a balance anybody can tune.
 *
 * Splitting means each is guaranteed its own and neither can starve the other.
 * The cost is that a very busy soundscape runs out of effect voices sooner,
 * eight instead of twelve, which is the right thing to spend: the twelfth
 * simultaneous sound effect is inaudible under the eleven others, and a
 * dropped bar of music is not.
 *
 * @note Effects take the FRONT of ::g_voices and music the tail. Every
 *       allocator in this file bounds its search by that split, which is what
 *       keeps an effect from evicting a note.
 *
 * 한국어
 * ------
 * @brief 효과음이 쓸 수 있는 보이스 수이며, 나머지는 음악에 예약됩니다.
 *
 * *음악은 자기 보이스를 가지며*, 이유는 할당기입니다. 모든 보이스가 사용 중이면 가장
 * *오래된* 것을 밀어내는데, 음악 트랙은 방금 발사된 샷건보다 언제나 오래된 음표의 흐름입니다.
 * 풀을 공유하면 음악이 총성을 끊거나, 축출 방향이 반대라면 총격전이 음악을 끊습니다. 어느
 * 쪽도 누군가 조율할 수 있는 균형이 아닙니다.
 *
 * 분할하면 각자 자기 몫을 보장받고 어느 쪽도 상대를 굶길 수 없습니다. 대가는 아주 분주한
 * 음향에서 효과음 보이스가 더 일찍 바닥난다는 것입니다. 열둘이 아니라 여덟이며, 그것이 쓸 만한
 * 대가입니다. 열두 번째 동시 효과음은 나머지 열하나 밑에서 들리지 않지만, 빠진 음악 한 마디는
 * 들립니다.
 *
 * @note 효과음은 ::g_voices의 *앞쪽*을, 음악은 뒤쪽을 씁니다. 이 파일의 모든 할당기가 그
 *       분할로 탐색 범위를 제한하며, 그것이 효과음이 음표를 밀어내지 못하게 합니다.
 */
#define SFX_VOICES (MAX_VOICES - MUSIC_VOICES)
_Static_assert(SFX_VOICES > 0, "MUSIC_VOICES must leave room for sound effects");

/* --- Sampled sounds / 샘플 사운드 --------------------------------------------
 *
 * ENGLISH
 * -------
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
 * 한국어
 * ------
 * 4비트 IMA ADPCM(11025Hz)이며, 로드 시 한 번 디코딩해 공유 버퍼에 넣습니다. 보이스마다
 * 디코딩하면 발사할 때마다 같은 일을 되풀이하게 되고, ADPCM은 본질적으로 순차적이라
 * 중간에서 시작할 수 없습니다. 각 샘플이 앞 샘플로부터의 차이이기 때문입니다. 그래서 보이스는
 * 탐색할 수 없는데, 이들이 일회성 효과음이므로 문제되지 않습니다.
 *
 * 11025는 믹서의 44100의 정확히 4분의 1이므로 재생은 원본 샘플 하나를 출력 네 샘플 동안
 * 유지합니다. 리샘플러도, 누적되는 위상 오차도 없으며, 이 비율은 이곳에서 고른 숫자가 아니라
 * Doom 자신의 것입니다.
 */

#define PCM_MAX     220000          /**< Decoded sample ceiling. / 디코딩된 샘플의 상한. */
#define PCM_STEP    (RATE / 11025)  /**< Source samples per output sample, i.e. 4. / 출력 샘플당 원본 샘플. 즉 4입니다. */

/* --- File-local types / 파일 지역 타입 --- */

/**
 * @struct Layer
 * @brief One oscillator's worth of a recipe: what it plays and how it fades.
 *
 * ENGLISH: Every field is a `short` because the whole library is a few hundred
 * of these and they live in .bss. The sweep is linear from `f0` to `f1` across
 * `ms`, which is enough shape for a shot, a step or a pickup and is the reason
 * no recipe here needs a curve.
 *
 * 한국어: 라이브러리 전체가 이것 수백 개이고 .bss에 놓이므로 모든 필드가 `short`입니다.
 * 스윕은 `ms` 동안 `f0`에서 `f1`까지 선형이며, 발사음·발소리·획득음에는 그 정도의 형태로
 * 충분합니다. 이곳의 어떤 레시피도 곡선을 필요로 하지 않는 이유입니다.
 */
typedef struct {
    short wave;        /**< 0 square, 1 saw, 2 sine, 3 noise. / 0 사각파, 1 톱니파, 2 사인파, 3 노이즈. */
    short ms;          /**< How long this layer sounds, in ms. / 이 레이어의 지속 시간(밀리초). */
    short f0, f1;      /**< Sweep start and end, in Hz. / 주파수 스윕의 시작과 끝(Hz). */
    short atk, dec;    /**< Attack and decay, in ms. / 어택과 디케이(밀리초). */
    short vol;         /**< This layer's level, 0-100. / 이 레이어의 레벨(0-100). */
} Layer;

/**
 * @struct Sound
 * @brief One named entry in the library: a recipe, a sample, or both fields
 *        present with `pcm_n` deciding.
 *
 * ENGLISH: Held in ::g_sounds, which a reload rewrites in place. A ::Voice
 * points straight at one of these, so the pointer is only as stable as the
 * table -- see the threading contract below.
 *
 * 한국어: ::g_sounds에 담기며, 재적재는 그 표를 제자리에서 다시 씁니다. ::Voice가 이것을
 * 곧바로 가리키므로 포인터의 안정성은 표의 안정성만큼입니다. 아래의 스레딩 계약을
 * 참조하십시오.
 */
typedef struct {
    char  name[NAME_LEN];        /**< The name callers ask for. / 호출자가 지목하는 이름. */
    Layer layers[MAX_LAYERS];    /**< The recipe, unused when this is a sample. / 레시피. 샘플일 때는 쓰이지 않습니다. */
    int   n;                     /**< How many layers the recipe has. / 레시피의 레이어 수. */
    /* A RECIPE OR A SAMPLE, whichever the library gave this name.
       pcm_n > 0 means Freedoom recorded this one and the layers are ignored;
       the layers stay because deleting the WAV brings the recipe straight
       back, which is the same bargain the drawn sprites strike with the
       generated creatures. `pump`, `hook` and `hreel` have no Doom equivalent
       and are recipes for good.
       레시피이거나 샘플입니다. pcm_n > 0이면 Freedoom이 녹음한 것이고 레이어는
       무시됩니다. 레이어를 남겨 두는 이유는 WAV를 지우면 레시피가 곧바로 돌아오기
       때문이며, 그려진 스프라이트가 생성된 생물과 맺는 것과 같은 거래입니다. */
    int   pcm_at;                /**< Offset into ::g_pcm. / g_pcm 내의 시작 위치. */
    int   pcm_n;                 /**< Samples; 0 means this is a recipe. / 샘플 수. 0이면 레시피입니다. */
} Sound;

/**
 * @struct Voice
 * @brief One sound currently being heard.
 *
 * ENGLISH: Carries everything the mixer needs, so rendering never reads game
 * state. `gain` is already scaled by the volume settings and the distance
 * curve at the moment the voice started; nothing recomputes it afterwards,
 * which is why a sound cannot change volume because its emitter moved or died.
 *
 * 한국어: 믹서가 필요로 하는 모든 것을 담고 있으므로 렌더링은 게임 상태를 결코 읽지
 * 않습니다. `gain`은 보이스가 시작되는 순간 이미 음량 설정과 거리 곡선으로 조정된 값이며
 * 이후 다시 계산되지 않습니다. 소리를 낸 대상이 움직이거나 죽었다고 해서 음량이 변할 수 없는
 * 이유입니다.
 */
typedef struct {
    const Sound *snd;       /**< What is playing; null means the slot is free. / 재생 중인 것. 널이면 빈 슬롯입니다. */
    int      pos;           /**< Samples elapsed. Also its age, for eviction. / 경과 샘플 수. 축출을 위한 나이이기도 합니다. */
    int      gain;          /**< Final level, 0-100, fixed at start. / 최종 레벨(0-100). 시작 시 고정됩니다. */
    unsigned rng;           /**< Noise state, seeded per slot. / 노이즈 난수 상태. 슬롯마다 씨앗이 다릅니다. */
    float    phase[MAX_LAYERS]; /**< Each layer's oscillator phase. / 각 레이어의 오실레이터 위상. */
    float    hold[MAX_LAYERS];  /**< Each layer's sample-and-hold noise value. / 각 레이어의 샘플 앤 홀드 값. */
} Voice;

/* --- Static variable definitions / 정적 변수 정의 ----------------------------
 *
 * Threading contract
 * ------------------
 * ENGLISH: ::g_sounds, ::g_n_sounds, ::g_parsed and ::g_voices are all shared
 * with the mixer thread, which dereferences ::Voice::snd straight into
 * ::g_sounds. Every one of them is touched only under the device lock. A
 * lookup and a parse must both sit INSIDE the critical section -- an earlier
 * version had ::find_sound outside it, which let a hot reload rewrite the
 * recipe table from index 0 while the mixer was reading the entries being
 * overwritten.
 *
 * Holding the lock across a parse is acceptable because it happens at most
 * once per hot reload, never in steady state: ::g_parsed gates it, and the
 * mixer's worst case is one late buffer, which the 4-buffer / ~46ms queue
 * absorbs without a dropout.
 *
 * ::g_listener and the three volume rows are the exception: they are game
 * thread only and the mixer never reads them, so they need no lock at all.
 *
 * 한국어
 * ------
 * ::g_sounds, ::g_n_sounds, ::g_parsed, ::g_voices는 모두 믹서 스레드와 공유되며, 믹서는
 * ::Voice::snd를 통해 ::g_sounds를 직접 역참조합니다. 이들 전부는 장치 락 아래에서만
 * 접근됩니다. 조회와 파싱은 모두 임계 영역 *안에* 있어야 합니다. 이전 버전은 ::find_sound가
 * 바깥에 있었고, 그 탓에 핫 리로드가 레시피 표를 0번 인덱스부터 재작성하는 동안 믹서가
 * 덮어써지는 항목을 읽을 수 있었습니다.
 *
 * 파싱 동안 락을 보유하는 것은 허용됩니다. 이는 핫 리로드당 최대 한 번만 발생하고 정상
 * 상태에서는 결코 일어나지 않기 때문입니다. ::g_parsed가 이를 통제하며, 믹서의 최악의
 * 경우는 버퍼 하나가 늦어지는 것인데 4버퍼 약 46ms 큐가 끊김 없이 이를 흡수합니다.
 *
 * ::g_listener와 세 개의 음량 값은 예외입니다. 게임 스레드 전용이고 믹서가 결코 읽지
 * 않으므로 락이 전혀 필요하지 않습니다.
 */

/** @brief The parsed library, its length, and whether it has been read yet. / 파싱된 라이브러리와 그 길이, 그리고 아직 읽지 않았는지 여부. */
static Sound g_sounds[MAX_SOUNDS];
static int   g_n_sounds;
static int   g_parsed;

/** @brief Every voice, effects in front and music in the tail. / 모든 보이스. 앞쪽은 효과음, 뒤쪽은 음악입니다. */
static Voice            g_voices[MAX_VOICES];

/* One recipe per music voice, rewritten when that voice takes a note.
   Voice::snd is a POINTER, so a note cannot be a local -- it has to outlive the
   call that started it. One per voice rather than a shared ring because a voice
   only ever reads its own, so nothing can rewrite a recipe out from under a
   note that is still sounding.
   음악 보이스마다 레시피 하나이며, 그 보이스가 음표를 받을 때 덮어씁니다. Voice::snd는
   *포인터*이므로 음표는 지역 변수일 수 없습니다. 그것을 시작한 호출보다 오래 살아야 합니다.
   공유 링이 아니라 보이스당 하나인 이유는, 보이스가 오직 자기 것만 읽으므로 아직 울리고 있는
   음표 밑에서 레시피가 다시 쓰이는 일이 없기 때문입니다. */
static Sound g_note[MUSIC_VOICES];

/** @brief Every decoded sample, packed end to end, and how much is used. / 디코딩된 모든 샘플을 끝과 끝을 맞대어 담은 버퍼와 사용량. */
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

/* Where the player's ears are. Game thread only -- see audio.h.
   플레이어의 귀 위치입니다. 게임 스레드 전용입니다. audio.h를 참조하십시오. */
static v3 g_listener;

/* --- volume ----------------------------------------------------------------
 * ENGLISH
 * -------
 * Game thread only, exactly like g_listener above, and for the same reason: the
 * mixer never reads either. A voice stores the gain it was handed and never
 * looks outward again, so scaling at the moment of play keeps the per-sample
 * path untouched and needs no lock of its own.
 *
 * WHAT THAT COSTS, stated rather than discovered: a sound already playing keeps
 * the loudness it started with. Every effect here is a fraction of a second, so
 * the longest a change can take to be heard in full is one sound -- and paying
 * for the alternative would mean the mixer reading two globals per sample.
 *
 * 한국어
 * ------
 * 게임 스레드 전용이며, 위의 g_listener와 정확히 같은 이유입니다. 믹서는 둘 중 어느 것도 읽지
 * 않습니다. 보이스는 건네받은 게인을 저장하고 다시는 바깥을 보지 않으므로, 재생 시점에
 * 조정하면 샘플별 경로를 건드리지 않고 자체 락도 필요 없습니다.
 *
 * *그 대가*를 발견이 아니라 명시로 적습니다. 이미 재생 중인 소리는 시작할 때의 음량을
 * 유지합니다. 이곳의 모든 효과음이 1초 미만이므로 변경이 온전히 들리기까지 걸리는 최대 시간은
 * 소리 하나이며, 대안을 택하는 대가는 믹서가 샘플마다 전역 둘을 읽는 것입니다. */
static int g_vol_master = 100;
static int g_vol_sfx    = 100;
static int g_vol_music  = 100;

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static void         parse_sounds(void);
static void         parse_text(const char *text, int want_layers);
static const Sound *find_sound(const char *name);
static void         play_gain(const char *name, int gain);
static int          gain_at(int gain, v3 pos);
static int          pcm_b64(char c);
static int          pcm_decode(const char *data, int len, int n, int at);
static int          render_voice(Voice *V, short *out, int frames);
static float        osc(int wave, float phase, float *hold, unsigned *rng);
static float        envelope(const Layer *L, float t_ms);
static float        frand(unsigned *s);

/* --- Public function definitions / 공개 함수 정의 --- */
/* Ordered as audio.h declares them. ::audio_init and ::audio_shutdown are the
   device's half and live in audio_win32.c.
   audio.h가 선언한 순서를 따릅니다. ::audio_init과 ::audio_shutdown은 장치 쪽 절반이며
   audio_win32.c에 있습니다. */
void audio_set_volume(int master, int sfx, int music) {
    g_vol_master = master < 0 ? 0 : (master > 100 ? 100 : master);
    g_vol_sfx    = sfx    < 0 ? 0 : (sfx    > 100 ? 100 : sfx);
    g_vol_music  = music  < 0 ? 0 : (music  > 100 ? 100 : music);
}

void audio_listener(v3 pos) { g_listener = pos; }

void audio_play(const char *name, int gain) { play_gain(name, gain); }

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

void audio_note(int wave, int freq, int dur_ms, int gain) {
    if (dur_ms <= 0 || freq <= 0) return;
    if (!audio_dev_lock()) return;

    /* Music voices only, and the oldest of those loses. Unlike an effect, a
       note that gets evicted is one the piece needed -- but four voices is
       four voices, and the importer already reduced the arrangement to fit.
       What arrives here past that is a chord the reduction let through.
       음악 보이스만이며, 그중 가장 오래된 것이 밀립니다. 효과음과 달리 밀려난 음표는 곡에
       필요했던 음표입니다. 그러나 네 보이스는 네 보이스이고, 임포터가 이미 편곡을 그에 맞게
       줄였습니다. 그것을 지나 이곳에 도착하는 것은 축소가 통과시킨 화음입니다. */
    int slot = -1, oldest = SFX_VOICES, oldest_pos = -1;
    for (int i = SFX_VOICES; i < MAX_VOICES; i++) {
        if (!g_voices[i].snd) { slot = i; break; }
        if (g_voices[i].pos > oldest_pos) { oldest_pos = g_voices[i].pos; oldest = i; }
    }
    if (slot < 0) slot = oldest;

    /* A note is a ONE-LAYER RECIPE with no sweep: f0 == f1, so the pitch holds.
       Everything render_voice already does -- the oscillator, the envelope, the
       gain -- applies unchanged, which is why music needed no mixer changes.
       음표는 스윕이 없는 *1레이어 레시피*입니다. f0 == f1이므로 음높이가 유지됩니다.
       render_voice가 이미 하는 모든 것(오실레이터, 엔벨로프, 게인)이 그대로 적용되며, 그래서
       음악에 믹서 변경이 필요 없었습니다. */
    Sound *S = &g_note[slot - SFX_VOICES];
    S->name[0] = 0;
    S->pcm_at  = 0;
    S->pcm_n   = 0;
    S->n       = 1;

    Layer *L = &S->layers[0];
    L->wave = (short)(wave & 3);
    L->ms   = (short)(dur_ms > 32767 ? 32767 : dur_ms);
    L->f0   = L->f1 = (short)(freq > 32767 ? 32767 : freq);
    /* A short attack keeps a square wave from clicking on every note; the decay
       runs the whole length so a note fades rather than stopping dead.
       짧은 어택이 사각파가 음표마다 딸깍거리지 않게 하고, 디케이가 전체 길이에 걸쳐 진행되어
       음표가 뚝 끊기지 않고 사그라듭니다. */
    L->atk  = 6;
    L->dec  = L->ms;
    L->vol  = 100;

    gain = gain * g_vol_master / 100;
    gain = gain * g_vol_music  / 100;

    Voice *V = &g_voices[slot];
    V->snd  = S;
    V->pos  = 0;
    V->gain = gain < 0 ? 0 : (gain > 100 ? 100 : gain);
    V->rng  = 0x2545f491u + (unsigned)slot * 2654435761u;
    for (int k = 0; k < MAX_LAYERS; k++) { V->phase[k] = 0.0f; V->hold[k] = 0.0f; }

    audio_dev_unlock();
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
    if (!audio_dev_lock()) { g_parsed = 0; return; }   /* no device; no mixer to race */
    g_parsed = 0;
    audio_dev_unlock();
}

int audio_sound_count(void) {
    /* Parses on demand, because a tool may ask before anything has played.
       Everything else that reads the table goes through audio_play, which
       parses first; this is the one caller that does not.
       아무것도 재생되기 전에 도구가 물어볼 수 있으므로 필요 시 파싱합니다. 표를 읽는
       다른 모든 경로는 먼저 파싱하는 audio_play를 거치며, 이것이 그러지 않는 유일한
       호출자입니다.

       UNDER THE LOCK, like every other reader of the recipe table. It was not,
       and the threading contract above this function says plainly that
       g_sounds[], g_n_sounds and g_parsed are touched only under g_lock -- so
       this was the one place that read the gate and could rewrite the whole
       table from index 0 while the mixer was dereferencing Voice::snd into it.
       That is the same race the contract describes find_sound() having had.

       It could not fire in practice: the only caller is tools/sndtest.c, which
       never calls audio_init, so there is no mixer thread and audio_dev_lock
       returns 0. But "no current caller reaches it" is not the same as "it
       cannot happen", and this is a public entry point in audio.h -- the next
       caller would be a game-thread one, with a device open.

       레시피 표를 읽는 다른 모든 경로와 마찬가지로 락 안에서 수행합니다. 이전에는 그렇지
       않았고, 이 함수 위의 스레딩 계약은 g_sounds[]·g_n_sounds·g_parsed가 오직 g_lock
       하에서만 접근된다고 분명히 적고 있습니다. 그런데 이곳이 게이트를 읽고, 믹서가
       Voice::snd로 역참조하는 동안 표 전체를 0번 인덱스부터 다시 쓸 수 있는 유일한
       지점이었습니다. 계약이 find_sound()에 대해 서술한 바로 그 레이스입니다.

       실제로 발생할 수는 없었습니다. 유일한 호출자인 tools/sndtest.c가 audio_init을 결코
       호출하지 않으므로 믹서 스레드가 없고 audio_dev_lock이 0을 반환합니다. 그러나 "현재
       도달하는 호출자가 없다"는 "일어날 수 없다"와 같지 않으며, 이것은 audio.h의 공개
       진입점입니다. 다음 호출자는 장치가 열린 채의 게임 스레드 호출자일 것입니다. */
    int held = audio_dev_lock();
    if (!g_parsed) parse_sounds();
    int n = g_n_sounds;
    if (held) audio_dev_unlock();
    return n;
}

int audio_rate(void) { return RATE; }

int audio_render(const char *name, short *out, int max_frames) {
    /* Offline path, and the only public entry point that may run with no
       device open at all -- tools/sndtest.c never calls audio_init. Take the
       audio_dev_lock answers "was there anything to take" and returns 0 when
       no device was ever opened, which is exactly this case -- see audio_dev.h
       for why the gate belongs inside the lock rather than at each of the four
       call sites that used to test it. The voice below is a local, so nothing
       here publishes state to the mixer either way.

       오프라인 경로이며, 장치가 전혀 열리지 않은 상태에서 실행될 수 있는 유일한
       공개 진입점입니다. tools/sndtest.c는 audio_init을 호출하지 않습니다. 경쟁할
       audio_dev_lock이 "획득할 것이 있었는가"에 답하며, 장치가 한 번도 열리지 않았으면
       0을 반환합니다. 바로 이 경우입니다. 게이트가 그것을 검사하던 네 지점이 아니라 락
       안에 속하는 이유는 audio_dev.h를 참조하십시오. 아래의 보이스는 지역 변수이므로,
       어느 쪽이든 믹서에 상태를 공개하지 않습니다. */
    int lock = audio_dev_lock();

    if (!g_parsed) parse_sounds();
    const Sound *s = find_sound(name);

    if (lock) audio_dev_unlock();
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

/* --- Mixer thread entry point / 믹서 스레드 진입점 --- */

/**
 * @brief Renders every live voice into one output buffer.
 *
 * ENGLISH
 * -------
 * Declared in audio_dev.h rather than audio.h, because the device calls it and
 * the game never does. This is the only function in this file that runs on the
 * mixer thread.
 *
 * @param[out] out    Buffer to fill, `frames` samples of 16-bit mono.
 * @param[in]  frames How many samples to produce.
 *
 * @note Voices that finish are freed here, by clearing ::Voice::snd. That is
 *       the only place a voice ends on its own.
 * @warning Runs on the mixer thread and takes the device lock itself. Do not
 *          call it with the lock already held.
 *
 * 한국어
 * ------
 * @brief 살아 있는 모든 보이스를 하나의 출력 버퍼로 렌더링합니다.
 *
 * audio.h가 아니라 audio_dev.h가 선언합니다. 장치가 호출하고 게임은 결코 호출하지 않기
 * 때문입니다. 이 파일에서 믹서 스레드 위에서 실행되는 유일한 함수입니다.
 *
 * @param[out] out    채울 버퍼. 16비트 모노 `frames` 샘플입니다.
 * @param[in]  frames 만들어 낼 샘플 수.
 *
 * @note 끝난 보이스는 ::Voice::snd를 지워 이곳에서 해제합니다. 보이스가 스스로 끝나는 곳은
 *       이곳뿐입니다.
 * @warning 믹서 스레드에서 실행되며 장치 락을 직접 획득합니다. 이미 락을 보유한 채로
 *          호출하지 마십시오.
 */
void audio_mix(short *out, int frames) {
    /* Cleared first and unconditionally. Every sample is written even when no
       voice is playing, because the caller hands this buffer straight to
       hardware and one left untouched replays whatever was in it last.
       가장 먼저 무조건 비웁니다. 재생 중인 보이스가 없어도 모든 샘플을 기록하는 것은,
       호출자가 이 버퍼를 하드웨어에 그대로 넘기므로 손대지 않은 버퍼는 직전에 들어 있던
       것을 다시 재생하기 때문입니다. */
    for (int i = 0; i < frames; i++) out[i] = 0;

    int held = audio_dev_lock();
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *V = &g_voices[v];
        if (!V->snd) continue;
        if (!render_voice(V, out, frames)) V->snd = 0;
    }
    if (held) audio_dev_unlock();
}

/* --- Test hooks, authoring builds only / 테스트 훅. 제작 빌드 전용 ---
 * Thin wrappers that expose a static to a test, so the test drives the SAME
 * code the game does. A test that reimplemented either would pass while the
 * game used a different rule.
 * 정적 함수를 테스트에 노출하는 얇은 껍데기입니다. 테스트가 게임과 *같은* 코드를 구동하게
 * 합니다. 어느 쪽이든 다시 구현한 테스트는 게임이 다른 규칙을 쓰는 동안에도 통과합니다. */
#ifdef HOT_RELOAD
int audio_gain_at(int gain, v3 pos) { return gain_at(gain, pos); }
#endif
#ifdef HOT_RELOAD
int audio_b64val(char c) { return pcm_b64(c); }
#endif

/* --- Static helper definitions / 정적 헬퍼 정의 --- */

/**
 * @brief Starts `name` on an effect voice at an already-scaled gain.
 *
 * ENGLISH
 * -------
 * The body of both ::audio_play and ::audio_play_at, so the lock discipline,
 * the recipe-or-sample test and the volume scaling exist once. The two public
 * entry points differ only in whether a distance curve ran first.
 *
 * @param[in] name Sound to start. An unknown name is ignored.
 * @param[in] gain 0-100, the recipe's own level as the caller asked for it.
 *
 * @note Takes and releases the device lock. Returns doing nothing when no
 *       device is open.
 * @note Evicts the oldest EFFECT voice when all of them are busy, never a
 *       music voice.
 *
 * 한국어
 * ------
 * @brief 이미 조정된 게인으로 효과음 보이스에서 `name`을 시작합니다.
 *
 * ::audio_play와 ::audio_play_at 양쪽의 본체이므로 락 규율, 레시피·샘플 판정, 음량 조정이
 * 한 번만 존재합니다. 두 공개 진입점은 거리 곡선이 먼저 실행되었는지에서만 다릅니다.
 *
 * @param[in] name 시작할 사운드. 알 수 없는 이름은 무시합니다.
 * @param[in] gain 0-100. 호출자가 요청한 그대로의 레시피 자체 레벨입니다.
 *
 * @note 장치 락을 획득하고 해제합니다. 열린 장치가 없으면 아무것도 하지 않고 반환합니다.
 * @note 모든 보이스가 사용 중이면 가장 오래된 *효과음* 보이스를 밀어내며, 음악 보이스는
 *       결코 밀어내지 않습니다.
 */
static void play_gain(const char *name, int gain) {
    if (!audio_dev_lock()) return;

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
    if (!s || (!s->n && s->pcm_n <= 0)) { audio_dev_unlock(); return; }

    /* SFX_VOICES, not MAX_VOICES: the tail of the array belongs to the music
       and an effect must never evict a note out of it. See the split above.
       MAX_VOICES가 아니라 SFX_VOICES입니다. 배열의 뒤쪽은 음악의 것이며 효과음이 그곳에서
       음표를 밀어내서는 안 됩니다. 위의 분할을 참조하십시오. */
    int slot = -1, oldest = -1, oldest_pos = -1;
    for (int i = 0; i < SFX_VOICES; i++) {
        if (!g_voices[i].snd) { slot = i; break; }
        if (g_voices[i].pos > oldest_pos) { oldest_pos = g_voices[i].pos; oldest = i; }
    }
    if (slot < 0) slot = oldest;

    Voice *V = &g_voices[slot];
    V->snd  = s;
    V->pos  = 0;
    /* The volume rows land HERE and nowhere else. `gain` is the recipe's own
       level as the caller asked for it; the two settings scale it, and the
       clamp that was already here still has the last word.
       음량 행이 도달하는 곳은 *이곳*이며 그 밖에는 없습니다. `gain`은 호출자가 요청한 그대로의
       레시피 자체 레벨이고, 두 설정이 그것을 조정하며, 이미 있던 제한이 여전히 마지막 말을
       합니다. */
    gain = gain * g_vol_master / 100;
    gain = gain * g_vol_sfx    / 100;
    V->gain = gain < 0 ? 0 : (gain > 100 ? 100 : gain);
    V->rng  = 0x2545f491u + (unsigned)slot * 2654435761u;
    for (int k = 0; k < MAX_LAYERS; k++) { V->phase[k] = 0.0f; V->hold[k] = 0.0f; }
    audio_dev_unlock();
}

/**
 * @brief What `gain` is worth heard from `pos`, given the current listener.
 *
 * ENGLISH
 * -------
 * The curve, in one place, so ::audio_play_at and the test that checks it
 * cannot drift apart.
 *
 * @param[in] gain 0-100 at the listener's feet.
 * @param[in] pos  Where in the world the sound happens.
 * @return The attenuated gain: `gain` within ::AUDIO_NEAR, 0 past
 *         ::AUDIO_FAR, linear between.
 *
 * @note Reads ::g_listener, so it is game-thread only.
 *
 * 한국어
 * ------
 * @brief 현재 청취자를 기준으로 `pos`에서 들리는 `gain`의 값.
 *
 * 곡선을 한 곳에 둡니다. ::audio_play_at과 그것을 검사하는 테스트가 어긋날 수 없도록 하기
 * 위함입니다.
 *
 * @param[in] gain 청취자의 발밑에서의 0-100 값.
 * @param[in] pos  월드에서 소리가 나는 지점.
 * @return 감쇠된 게인. ::AUDIO_NEAR 안에서는 `gain`, ::AUDIO_FAR를 넘으면 0, 그 사이는
 *         선형입니다.
 *
 * @note ::g_listener를 읽으므로 게임 스레드 전용입니다.
 */
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

/**
 * @brief Reads the sound library, recipes first and samples second.
 *
 * ENGLISH
 * -------
 * `l` supplies seven integers; the shared tokenizer handles the rest.
 *
 * @warning Caller MUST hold the device lock. This rewrites ::g_sounds in
 *          place, and the mixer thread dereferences ::Voice::snd straight into
 *          that array.
 * @note Silences every live voice before touching the recipes. A voice holds
 *       a raw `const Sound *`, so once the array is rewritten that pointer
 *       either addresses a half-written recipe or a completely different
 *       sound than the one that was playing. Killing them first is what makes
 *       the rewrite safe -- a hot reload costs one frame of audio, which is
 *       inaudible, where the alternative was a torn read.
 *
 * 한국어
 * ------
 * @brief 사운드 라이브러리를 읽습니다. 레시피가 먼저, 샘플이 나중입니다.
 *
 * `l`은 7개의 정수를 전달하며, 공유 토크나이저가 나머지를 처리합니다.
 *
 * @warning 호출자는 반드시 장치 락을 보유해야 합니다. 이 함수는 ::g_sounds를 제자리에서
 *          재작성하며, 믹서 스레드는 ::Voice::snd를 통해 그 배열을 직접 역참조하기
 *          때문입니다.
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

/**
 * @brief Parses one pass of sound text into the library.
 *
 * ENGLISH
 * -------
 * @param[in] text        Sound text to read.
 * @param[in] want_layers Non-zero on the first pass, which clears the library
 *                        and accepts `l` lines. Zero on the second pass over
 *                        the baked text.
 *
 * @note `want_layers` is 0 for the second pass because the recipes there are
 *       the same ones already parsed, and appending their layers again would
 *       overflow ::MAX_LAYERS and report a cap that was never really reached.
 * @warning Caller MUST hold the device lock; see ::parse_sounds.
 *
 * 한국어
 * ------
 * @brief 사운드 텍스트를 한 번 훑어 라이브러리에 넣습니다.
 *
 * @param[in] text        읽을 사운드 텍스트.
 * @param[in] want_layers 첫 번째 패스에서 0이 아니며, 라이브러리를 비우고 `l` 줄을
 *                        받아들입니다. 구워 넣은 텍스트에 대한 두 번째 패스에서는 0입니다.
 *
 * @note 두 번째 패스에서 `want_layers`가 0인 이유는, 그곳의 레시피가 이미 파싱한 것과
 *       같으며 레이어를 다시 덧붙이면 ::MAX_LAYERS를 넘겨 실제로는 닿은 적 없는 한계를
 *       보고하기 때문입니다.
 * @warning 호출자는 반드시 장치 락을 보유해야 합니다. ::parse_sounds를 참조하십시오.
 */
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
 * @brief Looks a recipe up by name. A pure lookup that never parses.
 *
 * ENGLISH
 * -------
 * @param[in] name Name to find.
 * @return The entry, or 0 if no sound has that name.
 *
 * @warning Caller MUST hold the device lock: this walks ::g_sounds, which
 *          ::parse_sounds rewrites in place.
 * @note Deliberately does NOT parse on demand. It used to, and that was a
 *       data race: ::audio_play called it OUTSIDE the lock, so a hot reload
 *       could have the game thread rewriting ::g_sounds from index 0 while
 *       the mixer thread was dereferencing ::Voice::snd into the very entries
 *       being overwritten. Parsing is now the caller's explicit job.
 *
 * 한국어
 * ------
 * @brief 이름으로 레시피를 찾습니다. 파싱하지 않는 순수 조회입니다.
 *
 * @param[in] name 찾을 이름.
 * @return 해당 항목. 그 이름의 사운드가 없으면 0입니다.
 *
 * @warning 호출자는 반드시 장치 락을 보유해야 합니다. ::parse_sounds가 제자리에서
 *          재작성하는 ::g_sounds를 순회하기 때문입니다.
 * @note 의도적으로 요청 시 파싱을 수행하지 않습니다. 이전에는 파싱했으며 그것이
 *       데이터 레이스였습니다. ::audio_play가 이 함수를 락 *바깥에서* 호출했으므로,
 *       핫 리로드 시 게임 스레드가 ::g_sounds를 0번 인덱스부터 재작성하는 동안
 *       믹서 스레드가 바로 그 덮어써지는 항목들을 ::Voice::snd로 역참조할 수
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
 * @brief Decodes one character of the sampled-sound alphabet.
 *
 * ENGLISH
 * -------
 * The same 64-character alphabet the sprite codec uses.
 *
 * @param[in] c A character from a `w` data line.
 * @return Its 0-63 value, or -1 if the character is not in the alphabet.
 *
 * @note bake.ps1 holds the only other copy of this alphabet, and sndtest
 *       asserts that the two agree, because nothing else can -- the contract
 *       spans PowerShell and C and no compiler sees it. If they ever disagree
 *       every sampled sound decodes to noise, which sounds like a bad
 *       recording rather than like a bug.
 *
 * 한국어
 * ------
 * @brief 샘플 사운드 알파벳의 한 문자를 해석합니다.
 *
 * 스프라이트 코덱이 쓰는 것과 같은 64문자 알파벳입니다.
 *
 * @param[in] c `w` 데이터 줄의 문자.
 * @return 0-63 값. 알파벳에 없는 문자면 -1입니다.
 *
 * @note 이 알파벳의 다른 사본은 bake.ps1에만 있으며, 둘이 일치하는지는 sndtest가
 *       단언합니다. 다른 무엇도 그럴 수 없기 때문입니다. 이 계약은 PowerShell과 C에 걸쳐
 *       있어 어떤 컴파일러도 보지 못합니다. 둘이 어긋나면 모든 샘플 사운드가 잡음으로
 *       디코딩되며, 그것은 버그가 아니라 녹음이 나쁜 것처럼 들립니다.
 */
static int pcm_b64(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '-') return 63;
    return -1;
}

/**
 * @brief Expands packed ADPCM into the shared sample buffer.
 *
 * ENGLISH
 * -------
 * @param[in] data Packed text, two alphabet characters per 12-bit group.
 * @param[in] len  Length of `data`.
 * @param[in] n    Samples the caller expects.
 * @param[in] at   Where in ::g_pcm to write them.
 * @return How many samples landed, short of `n` only when the buffer filled or
 *         the text ran out.
 *
 * @note Neither shortfall is fatal and both are reported by the caller: a
 *       truncated sound is better than a silent game.
 * @note Serial by nature -- each sample is a delta from the one before it --
 *       so this cannot start in the middle and a voice cannot seek.
 *
 * 한국어
 * ------
 * @brief 압축된 ADPCM을 공유 샘플 버퍼로 펼칩니다.
 *
 * @param[in] data 포장된 텍스트. 12비트 묶음마다 알파벳 두 문자입니다.
 * @param[in] len  `data`의 길이.
 * @param[in] n    호출자가 기대하는 샘플 수.
 * @param[in] at   ::g_pcm에서 기록을 시작할 위치.
 * @return 실제로 채워진 샘플 수. 버퍼가 찼거나 텍스트가 떨어졌을 때만 `n`에 못 미칩니다.
 *
 * @note 어느 쪽 부족도 치명적이지 않으며 둘 다 호출자가 보고합니다. 잘린 소리가 조용한
 *       게임보다 낫습니다.
 * @note 본질적으로 순차적이므로(각 샘플이 앞 샘플로부터의 차이입니다) 중간에서 시작할 수
 *       없고 보이스도 탐색할 수 없습니다.
 */
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
 * @brief Renders one voice on top of what is already in the buffer.
 *
 * ENGLISH
 * -------
 * @param[in,out] V      Voice to advance. ::Voice::pos moves by `frames`.
 * @param[in,out] out    Buffer to add into; existing content is preserved.
 * @param[in]     frames Samples to produce.
 * @return 1 while the voice still has something to play, 0 once it is done.
 *
 * @note Adds and clamps rather than overwriting, which is what lets
 *       ::audio_mix layer twelve voices into one buffer with no mix-down pass.
 * @note Takes the sample path or the oscillator path on ::Sound::pcm_n, never
 *       both.
 * @warning Runs on the mixer thread under the device lock, and dereferences
 *          ::Voice::snd into ::g_sounds.
 *
 * 한국어
 * ------
 * @brief 버퍼에 이미 들어 있는 것 위에 보이스 하나를 렌더링합니다.
 *
 * @param[in,out] V      진행시킬 보이스. ::Voice::pos가 `frames`만큼 이동합니다.
 * @param[in,out] out    더해 넣을 버퍼. 기존 내용은 보존됩니다.
 * @param[in]     frames 만들어 낼 샘플 수.
 * @return 보이스가 아직 재생할 것이 남아 있으면 1, 끝났으면 0입니다.
 *
 * @note 덮어쓰지 않고 더한 뒤 제한하므로, ::audio_mix가 별도의 믹스다운 패스 없이 열두
 *       보이스를 한 버퍼에 겹칠 수 있습니다.
 * @note ::Sound::pcm_n에 따라 샘플 경로나 오실레이터 경로 중 하나만 타며, 둘 다 타지는
 *       않습니다.
 * @warning 믹서 스레드에서 장치 락 아래 실행되며 ::Voice::snd를 ::g_sounds로 역참조합니다.
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
 * @brief Produces one oscillator sample.
 *
 * ENGLISH
 * -------
 * @param[in]     wave  0 square, 1 saw, 2 sine, 3 noise.
 * @param[in]     phase Current phase; only its fractional part shapes the wave.
 * @param[in,out] hold  Sample-and-hold value, rewritten when noise re-triggers.
 * @param[in,out] rng   Noise state, advanced on every re-trigger.
 * @return The sample, in -1.0 to 1.0.
 *
 * @note Noise re-triggers on `phase >= 1.0f` rather than per sample, so its
 *       pitch is the layer's frequency instead of the sample rate. White noise
 *       at 44100 sounds the same whatever the recipe asked for.
 *
 * 한국어
 * ------
 * @brief 오실레이터 샘플 하나를 만듭니다.
 *
 * @param[in]     wave  0 사각파, 1 톱니파, 2 사인파, 3 노이즈.
 * @param[in]     phase 현재 위상. 소수부만이 파형을 결정합니다.
 * @param[in,out] hold  샘플 앤 홀드 값. 노이즈가 다시 트리거될 때 갱신됩니다.
 * @param[in,out] rng   노이즈 난수 상태. 다시 트리거될 때마다 진행합니다.
 * @return -1.0에서 1.0 사이의 샘플.
 *
 * @note 노이즈는 샘플마다가 아니라 `phase >= 1.0f`에서 다시 트리거되므로, 그 음높이가
 *       샘플 레이트가 아니라 레이어의 주파수가 됩니다. 44100의 백색 잡음은 레시피가 무엇을
 *       요청했든 똑같이 들립니다.
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
 * @brief Computes a layer's attack-decay envelope at one instant.
 *
 * ENGLISH
 * -------
 * @param[in] L    Layer whose `atk` and `dec` shape the curve.
 * @param[in] t_ms Milliseconds since the voice started.
 * @return A multiplier in 0.0 to 1.0.
 *
 * @note Both stages are linear and both degrade to 1.0 when their time is
 *       zero, so a layer with no envelope is a flat tone rather than silence.
 * @note Decay is measured from the END of the attack, so lengthening the
 *       attack does not shorten the tail.
 *
 * 한국어
 * ------
 * @brief 한 시점에서 레이어의 어택·디케이 엔벨로프를 계산합니다.
 *
 * @param[in] L    `atk`와 `dec`가 곡선을 결정하는 레이어.
 * @param[in] t_ms 보이스가 시작된 뒤 경과한 밀리초.
 * @return 0.0에서 1.0 사이의 배수.
 *
 * @note 두 단계 모두 선형이며, 시간이 0이면 둘 다 1.0으로 퇴화합니다. 따라서 엔벨로프가
 *       없는 레이어는 무음이 아니라 평평한 음입니다.
 * @note 디케이는 어택이 *끝난* 지점부터 잽니다. 어택을 늘려도 꼬리가 짧아지지 않습니다.
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
 * @brief Advances a linear congruential generator and returns -1.0 to 1.0.
 *
 * ENGLISH
 * -------
 * @param[in,out] s Generator state, advanced in place.
 * @return The next value, in -1.0 to 1.0.
 *
 * @note The state is per voice, seeded from the slot index, so two noise
 *       sounds started in the same frame do not produce identical noise.
 *
 * 한국어
 * ------
 * @brief 선형 합동 생성기를 진행시키고 -1.0에서 1.0 사이의 값을 반환합니다.
 *
 * @param[in,out] s 생성기 상태. 제자리에서 진행합니다.
 * @return -1.0에서 1.0 사이의 다음 값.
 *
 * @note 상태는 보이스마다 따로이며 슬롯 인덱스로 씨앗을 받으므로, 같은 프레임에 시작된 두
 *       노이즈 사운드가 똑같은 잡음을 내지 않습니다.
 */
static float frand(unsigned *s) {
    *s = *s * 1664525u + 1013904223u;
    return ((*s >> 8) & 0xffff) / 32768.0f - 1.0f;
}
