/**
 * @file diag.c
 * @brief Implements the capacity-overflow counters. Dev builds only.
 *
 * ENGLISH
 * -------
 * The whole translation unit is empty in release: `#ifdef DIAG_ENABLED` wraps
 * everything, so the shipped binary contains no counters, no name strings and
 * no code from this file. The build script compiles it unconditionally and
 * the linker simply finds nothing to keep -- verified by checking diag.o's
 * contribution in build/game.map, which is 0 bytes of .text, .data and .bss.
 *
 * No formatting library is used. wsprintfA is the project's only string
 * formatter (see main.c's HUD) and pulling in snprintf for a dev aid would
 * drag stdio into every tool that links this. Integers are rendered by hand.
 *
 * 한국어
 * ------
 * 릴리스에서는 이 번역 단위 전체가 비어 있습니다. `#ifdef DIAG_ENABLED`가 전부를
 * 감싸므로, 배포되는 바이너리에는 이 파일의 카운터도 이름 문자열도 코드도 남지
 * 않습니다. 빌드 스크립트는 이 파일을 조건 없이 컴파일하며, 링커는 유지할 것을 아무것도
 * 찾지 못합니다. build/game.map에서 diag.o의 기여분이 .text, .data, .bss 모두
 * 0바이트임을 확인했습니다.
 *
 * 어떤 포매팅 라이브러리도 사용하지 않습니다. wsprintfA가 이 프로젝트의 유일한 문자열
 * 포매터이며(main.c의 HUD 참조), 개발 보조 도구를 위해 snprintf를 끌어들이면 이 파일을
 * 링크하는 모든 도구에 stdio가 딸려 들어옵니다. 정수는 직접 변환합니다.
 */

#include "diag.h"

#ifdef DIAG_ENABLED

/* Included INSIDE the guard so the release translation unit still pulls in
   nothing at all -- the property build/game.map is checked against. txt.h is a
   leaf of static inline routines and would emit no code either way, but the
   file's contract is "release contains nothing from here" and that is easier
   to keep true than to re-verify.
   릴리스 번역 단위가 여전히 아무것도 끌어오지 않도록 가드 *안쪽*에 포함합니다.
   build/game.map으로 확인하는 그 성질입니다. txt.h는 static inline 루틴만 있는 리프이므로
   어느 쪽이든 코드를 생성하지 않지만, 이 파일의 계약은 "릴리스에는 이곳의 것이 아무것도
   남지 않는다"이며 그것은 매번 다시 확인하는 것보다 참으로 유지하는 편이 쉽습니다. */
#include "txt.h"

/* --- Static variable definitions / 정적 변수 정의 --- */

/**
 * @brief Accumulated overflow count per ::DiagKind.
 *
 * ENGLISH
 * -------
 * Never reset. A count that appeared during level load stays visible
 * afterward, which is the point -- the overflow is usually long over by the
 * time anyone looks at the HUD.
 *
 * 한국어
 * ------
 * 절대 초기화되지 않습니다. 레벨 로드 중에 발생한 횟수가 이후에도 계속 표시되며, 그것이
 * 핵심입니다. 누군가 HUD를 볼 시점에는 대개 초과 상황이 이미 끝난 뒤이기 때문입니다.
 */
static int g_counts[DIAG_COUNT];

/**
 * @brief The frame each counter first and last fired on.
 *
 * ENGLISH
 * -------
 * Meaningful only where the matching `g_counts` entry is non-zero, which is
 * why neither needs a sentinel value or a run-time initialiser: zero-filled
 * .bss plus "the count says whether to look" is enough. ::diag_first returns
 * -1 for a counter that never fired by asking the count, not by storing -1.
 *
 * 한국어
 * ------
 * 대응하는 `g_counts` 항목이 0이 아닐 때만 의미가 있으며, 그래서 둘 다 감시 값이나
 * 런타임 초기화가 필요 없습니다. 0으로 채워진 .bss와 "횟수가 볼지 말지를 알려 준다"는
 * 규칙이면 충분합니다. ::diag_first는 한 번도 발생하지 않은 카운터에 대해 -1을 저장해
 * 두는 것이 아니라 횟수에 물어서 -1을 반환합니다.
 */
static int g_first[DIAG_COUNT];
static int g_last[DIAG_COUNT];

/**
 * @brief The frame clock, advanced by ::diag_tick.
 *
 * ENGLISH
 * -------
 * Starts at 0 and stays there until something steps a world, so a report
 * made during a level load carries frame 0 -- which is exactly right, since
 * no frame has run.
 *
 * 한국어
 * ------
 * 0에서 시작하여 무언가 월드를 단계시킬 때까지 그대로이므로, 레벨 로드 중에 이루어진
 * 보고는 프레임 0을 갖습니다. 아직 어떤 프레임도 실행되지 않았으니 정확히 맞는 값입니다.
 */
static int g_frame;

/**
 * @brief Short display labels, indexed by ::DiagKind.
 *
 * ENGLISH
 * -------
 * @warning Must stay in step with the ::DiagKind enum -- ::diag_name indexes
 *          straight into this table. The static assert below enforces the
 *          length; the ORDER is still on whoever edits the enum.
 *
 * 한국어
 * ------
 * @warning ::DiagKind 열거형과 반드시 동기화되어야 합니다. ::diag_name이 이 테이블을
 *          인덱스로 직접 참조하기 때문입니다. 아래의 정적 검사가 길이를 강제하지만,
 *          *순서*는 여전히 열거형을 수정하는 사람의 책임입니다.
 */
static const char *const DIAG_NAMES[DIAG_COUNT] = {
    "vtx",      /* DIAG_VERTEX_BUF   */
    "meshpt",   /* DIAG_MESH_POINTS  */
    "mdlpt",    /* DIAG_MODEL_POINTS */
    "range",    /* DIAG_MAT_RANGES   */
    "texop",    /* DIAG_TEX_OPS      */
    "texcache", /* DIAG_TEX_CACHE    */
    "fx",       /* DIAG_FX_CAP       */
    "light",    /* DIAG_LIGHT_CAP    */
    "enemy",    /* DIAG_ENEMY_CAP    */
    "pickup",   /* DIAG_PICKUP_CAP   */
    "shot",     /* DIAG_SHOT_CAP     */
    "sound",    /* DIAG_SOUND_CAP    */
    "door",     /* DIAG_DOOR_CAP     */
    "inflate",  /* DIAG_ASSET_INFLATE */
    "png",      /* DIAG_PNG           */
    "doorstale",/* DIAG_DOOR_STALE   */
    "montable", /* DIAG_MON_TABLE    */
    "pass",     /* DIAG_PASS_ORDER   */
    "lcache",   /* DIAG_LIGHT_CACHE  */
    "brush",    /* DIAG_BRUSH_CAP    */
    "mapent",   /* DIAG_MAPENT_CAP   */
    "unclosed", /* DIAG_BRUSH_OPEN   */
    "lvlslot",  /* DIAG_LEVEL_SLOTS  */
    "demofull", /* DIAG_DEMO_FULL    */
    "traceev",  /* DIAG_TRACE_EVENTS */
    "ent",      /* DIAG_ENT_CAP      */
    "wardcand", /* DIAG_WARD_CAND    */
    "sector",   /* DIAG_SECTOR_CAP   */
    "secpt",    /* DIAG_POINT_CAP    */
    "saveio",   /* DIAG_SAVE_IO      */
    "story"     /* DIAG_STORY_CAP    */
};

/* A name table shorter than the enum would read past its end the first time
   the missing counter fired -- exactly the class of silent fault this module
   exists to expose, so it is caught at compile time instead.
   이름 테이블이 열거형보다 짧으면, 누락된 카운터가 처음 발생하는 순간 배열 끝을 넘어
   읽게 됩니다. 이는 바로 이 모듈이 드러내고자 하는 종류의 조용한 결함이므로, 대신
   컴파일 시점에 잡습니다. */
_Static_assert(sizeof(DIAG_NAMES) / sizeof(DIAG_NAMES[0]) == DIAG_COUNT,
               "DIAG_NAMES must have exactly one entry per DiagKind");

/* The two append helpers this file used to define now live in txt.h, where
   ::txt_copy already was. They were lifted rather than copied when a second
   caller wanted them; the behaviour is unchanged for every value this file
   produces, since counters are non-negative and ::diag_summary skips zeros.
   이 파일이 정의하던 두 덧붙이기 헬퍼는 이제 ::txt_copy가 이미 있던 txt.h에 있습니다.
   두 번째 사용처가 생겼을 때 복사하지 않고 옮겼습니다. 이 파일이 만들어 내는 모든 값에
   대해 동작은 동일합니다. 카운터는 음수가 아니며 ::diag_summary는 0을 건너뜁니다. */

/* --- Public function definitions / 공개 함수 정의 --- */

void diag_report(DiagKind kind) {
    /* Bounds-checked rather than trusted: a caller passing DIAG_COUNT itself
       is a plausible mistake, and this module must never be the thing that
       corrupts memory.
       신뢰하지 않고 범위를 검사합니다. 호출자가 DIAG_COUNT 자체를 전달하는 것은 충분히
       있을 법한 실수이며, 이 모듈이 메모리를 손상시키는 원인이 되어서는 안 됩니다. */
    if ((unsigned)kind >= (unsigned)DIAG_COUNT) return;

    /* Saturate instead of wrapping. A counter that overflowed back to zero
       would report "no problem" during the worst possible case -- a limit
       being hit millions of times.
       순환하지 않고 포화시킵니다. 카운터가 0으로 되돌아가면 최악의 상황, 즉 한계가 수백만
       번 초과되는 동안 "문제 없음"으로 보고하게 됩니다. */
    /* Before the increment, because "is this the first" is a question about
       the count as it stands. After it, every report looks like a repeat.
       증가 이전입니다. "이것이 처음인가"는 현재 상태의 횟수에 대한 질문이기 때문입니다.
       증가 이후라면 모든 보고가 반복처럼 보입니다. */
    if (!g_counts[kind]) g_first[kind] = g_frame;

    /* Outside the saturation check on purpose. A counter pinned at INT_MAX
       has stopped being able to say how many, but it can still say whether it
       is still going, and that is the more useful of the two by then.
       포화 검사 바깥에 둔 것은 의도적입니다. INT_MAX에 고정된 카운터는 몇 번인지 말할 수
       없게 되었지만 여전히 진행 중인지는 말할 수 있으며, 그 시점에는 그쪽이 더 유용합니다. */
    g_last[kind] = g_frame;

    if (g_counts[kind] < 0x7fffffff) g_counts[kind]++;
}

/* Which half of the frame is being drawn, moved here from post.c so that a
   module can check the boundary without depending on the post-processing
   header to ask. Written only by ::post_begin and ::post_end, through the
   DIAG_PASS_* macros, so it does not exist at all in a release build.
   프레임의 어느 절반을 그리는 중인지이며, post.c에서 이곳으로 옮겼습니다. 모듈이 경계를
   검사하기 위해 후처리 헤더에 의존하지 않아도 되게 하기 위함입니다. DIAG_PASS_* 매크로를
   통해 ::post_begin과 ::post_end만 기록하므로, 릴리스 빌드에는 아예 존재하지 않습니다. */
static int g_in_world;

void diag_pass_set(int in_world) { g_in_world = in_world; }
int  diag_pass_in_world(void)    { return g_in_world; }

void diag_tick(void) {
    if (g_frame < 0x7fffffff) g_frame++;
}

int diag_frame(void) {
    return g_frame;
}

int diag_count(DiagKind kind) {
    if ((unsigned)kind >= (unsigned)DIAG_COUNT) return 0;
    return g_counts[kind];
}

int diag_first(DiagKind kind) {
    if ((unsigned)kind >= (unsigned)DIAG_COUNT) return -1;
    return g_counts[kind] ? g_first[kind] : -1;
}

int diag_last(DiagKind kind) {
    if ((unsigned)kind >= (unsigned)DIAG_COUNT) return -1;
    return g_counts[kind] ? g_last[kind] : -1;
}

const char *diag_name(DiagKind kind) {
    /* "?" rather than NULL: a display path should print something odd, not
       dereference nothing.
       NULL이 아닌 "?"를 반환합니다. 표시 경로는 아무것도 역참조하지 않는 대신 이상한
       값이라도 출력해야 합니다. */
    if ((unsigned)kind >= (unsigned)DIAG_COUNT) return "?";
    return DIAG_NAMES[kind];
}

int diag_summary(char *out, int cap) {
    if (!out || cap < 1) return 0;

    int pos = 0, any = 0;
    for (int i = 0; i < DIAG_COUNT; i++) {
        if (!g_counts[i]) continue;

        /* Leading separator only between entries, so the caller can append
           this straight onto an existing string.
           항목 사이에만 구분자를 넣으므로, 호출자가 이 결과를 기존 문자열에 그대로
           덧붙일 수 있습니다. */
        if (any) pos = txt_append_str(out, cap, pos, " ");
        pos = txt_append_str(out, cap, pos, DIAG_NAMES[i]);
        pos = txt_append_str(out, cap, pos, "=");
        pos = txt_append_int(out, cap, pos, g_counts[i]);

        /* When it happened, not just how often. A burst gets one frame and a
           span gets two, so the shape of the entry answers "is this still
           going" before the digits are read.
           얼마나 자주인지만이 아니라 언제인지를 함께 씁니다. 한 번의 폭발은 프레임 하나를,
           지속되는 것은 둘을 얻으므로, 숫자를 읽기 전에 항목의 모양이 "지금도 진행
           중인가"에 답합니다. */
        pos = txt_append_str(out, cap, pos, "@");
        pos = txt_append_int(out, cap, pos, g_first[i]);
        if (g_last[i] != g_first[i]) {
            pos = txt_append_str(out, cap, pos, "..");
            pos = txt_append_int(out, cap, pos, g_last[i]);
        }
        any = 1;
    }

    /* Always terminated, including the nothing-to-report case.
       보고할 내용이 없는 경우를 포함하여 항상 널로 종료합니다. */
    out[pos < cap ? pos : cap - 1] = 0;
    return any;
}

#else

/* Release: nothing at all. A translation unit must not be empty in C, so a
   typedef stands in -- it emits no code and no data.
   릴리스: 아무것도 없습니다. C에서 번역 단위가 완전히 비어 있을 수는 없으므로 typedef를
   두었으며, 이는 코드도 데이터도 생성하지 않습니다. */
typedef int diag_translation_unit_not_empty;

#endif /* DIAG_ENABLED */
