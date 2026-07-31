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
    "enemy",    /* DIAG_ENEMY_CAP    */
    "pickup",   /* DIAG_PICKUP_CAP   */
    "sound"     /* DIAG_SOUND_CAP    */
};

/* A name table shorter than the enum would read past its end the first time
   the missing counter fired -- exactly the class of silent fault this module
   exists to expose, so it is caught at compile time instead.
   이름 테이블이 열거형보다 짧으면, 누락된 카운터가 처음 발생하는 순간 배열 끝을 넘어
   읽게 됩니다. 이는 바로 이 모듈이 드러내고자 하는 종류의 조용한 결함이므로, 대신
   컴파일 시점에 잡습니다. */
_Static_assert(sizeof(DIAG_NAMES) / sizeof(DIAG_NAMES[0]) == DIAG_COUNT,
               "DIAG_NAMES must have exactly one entry per DiagKind");

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static int append_str(char *out, int cap, int pos, const char *s);
static int append_int(char *out, int cap, int pos, int value);

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
        if (any) pos = append_str(out, cap, pos, " ");
        pos = append_str(out, cap, pos, DIAG_NAMES[i]);
        pos = append_str(out, cap, pos, "=");
        pos = append_int(out, cap, pos, g_counts[i]);
        any = 1;
    }

    /* Always terminated, including the nothing-to-report case.
       보고할 내용이 없는 경우를 포함하여 항상 널로 종료합니다. */
    out[pos < cap ? pos : cap - 1] = 0;
    return any;
}

/* --- Static helper function definitions / 정적 헬퍼 함수 정의 --- */

/**
 * @brief Appends a string, stopping at the buffer's capacity.
 *
 * ENGLISH
 * -------
 * @param[out] out Destination buffer.
 * @param[in]  cap Capacity in bytes.
 * @param[in]  pos Current write offset.
 * @param[in]  s   Null-terminated string to append.
 * @return The new write offset, never exceeding `cap - 1`.
 * @note Reserves the final byte for the terminator the caller writes, so the
 *       result is always terminable without a further bounds check.
 *
 * 한국어
 * ------
 * @brief 버퍼 용량에 도달하면 멈추면서 문자열을 덧붙입니다.
 * @param[out] out 대상 버퍼.
 * @param[in]  cap 용량 (바이트).
 * @param[in]  pos 현재 쓰기 위치.
 * @param[in]  s   덧붙일 널 종료 문자열.
 * @return 새로운 쓰기 위치. 절대 `cap - 1`을 넘지 않습니다.
 * @note 호출자가 기록할 종료 문자를 위해 마지막 바이트를 남겨 두므로, 추가적인 범위
 *       검사 없이 항상 널 종료가 가능합니다.
 */
static int append_str(char *out, int cap, int pos, const char *s) {
    while (*s && pos < cap - 1) out[pos++] = *s++;
    return pos;
}

/**
 * @brief Appends a non-negative integer in decimal.
 *
 * ENGLISH
 * -------
 * @param[out] out   Destination buffer.
 * @param[in]  cap   Capacity in bytes.
 * @param[in]  pos   Current write offset.
 * @param[in]  value Value to render; negatives are clamped to 0.
 * @return The new write offset.
 * @note Hand-rolled rather than using a formatter: this file must not drag
 *       stdio into the tools that link it.
 *
 * 한국어
 * ------
 * @brief 음이 아닌 정수를 10진수로 덧붙입니다.
 * @param[out] out   대상 버퍼.
 * @param[in]  cap   용량 (바이트).
 * @param[in]  pos   현재 쓰기 위치.
 * @param[in]  value 변환할 값. 음수는 0으로 제한됩니다.
 * @return 새로운 쓰기 위치.
 * @note 포매터를 쓰지 않고 직접 구현했습니다. 이 파일이 자신을 링크하는 도구들에
 *       stdio를 끌어들여서는 안 되기 때문입니다.
 */
static int append_int(char *out, int cap, int pos, int value) {
    if (value <= 0) {
        if (pos < cap - 1) out[pos++] = '0';
        return pos;
    }

    /* Reverse into a scratch buffer, then copy back. 10 digits covers INT_MAX.
       임시 버퍼에 역순으로 만든 뒤 되돌려 복사합니다. 자릿수 10개면 INT_MAX를
       충분히 담습니다. */
    char tmp[12];
    int n = 0;
    while (value > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (n > 0 && pos < cap - 1) out[pos++] = tmp[--n];
    return pos;
}

#else

/* Release: nothing at all. A translation unit must not be empty in C, so a
   typedef stands in -- it emits no code and no data.
   릴리스: 아무것도 없습니다. C에서 번역 단위가 완전히 비어 있을 수는 없으므로 typedef를
   두었으며, 이는 코드도 데이터도 생성하지 않습니다. */
typedef int diag_translation_unit_not_empty;

#endif /* DIAG_ENABLED */
