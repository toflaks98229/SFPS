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
    "ent"       /* DIAG_ENT_CAP      */
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
    if (g_counts[kind] < 0x7fffffff) g_counts[kind]++;
}

int diag_count(DiagKind kind) {
    if ((unsigned)kind >= (unsigned)DIAG_COUNT) return 0;
    return g_counts[kind];
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
