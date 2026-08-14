/**
 * @file txt.h
 * @brief The tokenizer every asset language in this project shares.
 *
 * ENGLISH
 * -------
 * Textures, models and sounds are all written in the same shape: bare words
 * followed by a known count of integers, '#' to end of line for comments,
 * newlines carrying no meaning. There is no float parsing anywhere -- every
 * number is an integer with a fixed implied scale, which is what keeps these
 * parsers to a few dozen lines each.
 *
 * Every routine is `static inline`, so unused helpers cost nothing after
 * `--gc-sections`.
 *
 * @note All of these operate on null-terminated text and never allocate,
 *       never copy, and never modify their input. A token is reported as a
 *       borrowed (pointer, length) pair into the original buffer rather than
 *       as a string, so it is NOT null-terminated and must not be passed to
 *       the C string functions. The caller owns the text and must keep it
 *       alive for as long as any token pointer is in use.
 *
 * 한국어
 * ------
 * 텍스처, 모델, 사운드는 모두 동일한 형태로 작성됩니다. 단순한 단어 뒤에 정해진
 * 개수의 정수가 오고, '#'부터 줄 끝까지는 주석이며, 줄바꿈은 아무런 의미를 갖지
 * 않습니다. 부동소수점 파싱은 어디에도 없습니다. 모든 숫자는 고정된 암묵적
 * 배율을 가진 정수이며, 이것이 각 파서를 수십 줄 수준으로 유지하는 비결입니다.
 *
 * 모든 루틴은 `static inline`이므로 사용되지 않는 헬퍼는 `--gc-sections` 이후
 * 아무런 비용도 발생시키지 않습니다.
 *
 * @note 이 함수들은 모두 널로 끝나는 텍스트를 대상으로 동작하며, 메모리를
 *       할당하거나 복사하지 않고 입력을 수정하지도 않습니다. 토큰은 문자열이
 *       아니라 원본 버퍼를 가리키는 (포인터, 길이) 쌍으로 반환되므로 널로
 *       끝나지 않으며, C 문자열 함수에 전달해서는 안 됩니다. 텍스트는 호출자의
 *       소유이며, 토큰 포인터가 사용되는 동안 유효하게 유지해야 합니다.
 */
#ifndef TXT_H
#define TXT_H

/* --- Scanning / 스캔 --- */

/**
 * @brief Advances past whitespace and '#' comments.
 *
 * ENGLISH
 * -------
 * @brief Advances past whitespace and '#' comments.
 * @param[in] p Position in the text to scan from.
 * @return The first character that is neither whitespace nor part of a
 *         comment; the null terminator when the text ends.
 * @note Comments matter because these files are hand edited: without this, a
 *       word like "p" inside a comment would be read as an opcode and quietly
 *       corrupt whatever came before it.
 *
 * 한국어
 * ------
 * @brief 공백과 '#' 주석을 건너뜁니다.
 * @param[in] p 스캔을 시작할 텍스트 내 위치.
 * @return 공백도 주석의 일부도 아닌 첫 번째 문자. 텍스트가 끝나면 널 종료 문자.
 * @note 이 파일들은 사람이 직접 편집하므로 주석 처리가 중요합니다. 이 처리가
 *       없으면 주석 안의 "p" 같은 단어가 명령어로 읽혀 그 앞의 내용을 조용히
 *       손상시킬 수 있습니다.
 */
static inline const char *txt_skip(const char *p) {
    /* Alternating loop: a comment can be followed by more whitespace and
       another comment, so neither pass alone is sufficient.
       교대 반복입니다. 주석 뒤에 다시 공백과 또 다른 주석이 이어질 수 있으므로,
       어느 한쪽 처리만으로는 충분하지 않습니다. */
    for (;;) {
        while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
        if (*p != '#') return p;
        while (*p && *p != '\n') p++;
    }
}

/**
 * @brief Finds the next token, skipping whitespace and comments.
 *
 * ENGLISH
 * -------
 * @brief Finds the next token, skipping whitespace and comments.
 * @param[in]  p   Position in the text to scan from.
 * @param[out] len Receives the token's length in characters.
 * @return A pointer to the token's first character, or NULL at end of text.
 * @warning The returned pointer is NOT null-terminated: it borrows into the
 *          caller's text and is only valid together with `*len`. `*len` is
 *          left untouched when NULL is returned.
 * @note Advance past a token with `p = token + len`.
 *
 * 한국어
 * ------
 * @brief 공백과 주석을 건너뛰고 다음 토큰을 찾습니다.
 * @param[in]  p   스캔을 시작할 텍스트 내 위치.
 * @param[out] len 토큰의 길이(문자 수)를 받습니다.
 * @return 토큰의 첫 문자를 가리키는 포인터. 텍스트가 끝나면 NULL.
 * @warning 반환된 포인터는 널로 끝나지 않습니다. 호출자의 텍스트를 참조하며
 *          `*len`과 함께여야만 유효합니다. NULL이 반환되는 경우 `*len`은
 *          변경되지 않습니다.
 * @note 토큰을 지나 진행하려면 `p = token + len`을 사용하십시오.
 */
static inline const char *txt_token(const char *p, int *len) {
    p = txt_skip(p);
    if (!*p) return 0;
    const char *s = p;
    while (*p && *p != ' ' && *p != '\n' && *p != '\t' && *p != '\r') p++;
    *len = (int)(p - s);
    return s;
}

/* --- Token inspection / 토큰 검사 --- */

/**
 * @brief Tests whether a token exactly equals a literal.
 *
 * ENGLISH
 * -------
 * @brief Tests whether a token exactly equals a literal.
 * @param[in] tok Token start, as returned by ::txt_token.
 * @param[in] len Token length.
 * @param[in] lit Null-terminated literal to compare against.
 * @return 1 on an exact match, 0 otherwise.
 * @note The match is exact in both directions: a token that merely starts
 *       with the literal fails, because the literal's own terminator must
 *       fall precisely at `len`. This is what stops "pos" from matching "p".
 *
 * 한국어
 * ------
 * @brief 토큰이 리터럴과 정확히 일치하는지 검사합니다.
 * @param[in] tok ::txt_token이 반환한 토큰 시작 위치.
 * @param[in] len 토큰 길이.
 * @param[in] lit 비교 대상이 되는 널로 끝나는 리터럴.
 * @return 정확히 일치하면 1, 그렇지 않으면 0.
 * @note 양방향 모두 정확히 일치해야 합니다. 리터럴로 시작하기만 하는 토큰은
 *       실패하는데, 리터럴의 종료 문자가 정확히 `len` 위치에 와야 하기
 *       때문입니다. 이것이 "pos"가 "p"와 일치하지 않게 하는 장치입니다.
 */
static inline int txt_is(const char *tok, int len, const char *lit) {
    /* The `!lit[i]` test catches a literal shorter than the token, which
       would otherwise read past its terminator.
       `!lit[i]` 검사는 토큰보다 짧은 리터럴을 걸러 냅니다. 그렇지 않으면 리터럴의
       종료 문자를 넘어서 읽게 됩니다. */
    for (int i = 0; i < len; i++)
        if (tok[i] != lit[i] || !lit[i]) return 0;
    /* And this rejects a literal longer than the token.
       그리고 이 검사는 토큰보다 긴 리터럴을 거부합니다. */
    return lit[len] == 0;
}

/**
 * @brief Tests whether a token is a decimal integer.
 *
 * ENGLISH
 * -------
 * @brief Tests whether a token is a decimal integer.
 * @param[in] tok Token start.
 * @param[in] len Token length.
 * @return 1 when the token is an optionally negative run of digits, 0 otherwise.
 * @note Used to find the end of a variable-length number list, which is why a
 *       lone "-" is rejected rather than treated as zero.
 *
 * 한국어
 * ------
 * @brief 토큰이 10진 정수인지 검사합니다.
 * @param[in] tok 토큰 시작 위치.
 * @param[in] len 토큰 길이.
 * @return 선택적인 음수 부호가 붙은 숫자 나열이면 1, 그렇지 않으면 0.
 * @note 가변 길이 숫자 목록의 끝을 찾는 데 사용되므로, "-" 하나만 있는 경우는
 *       0으로 취급하지 않고 거부합니다.
 */
static inline int txt_is_number(const char *tok, int len) {
    int i = (tok[0] == '-') ? 1 : 0;
    /* A bare "-" has no digits and is not a number.
       "-"만 있는 경우 숫자가 없으므로 정수가 아닙니다. */
    if (i >= len) return 0;
    for (; i < len; i++)
        if (tok[i] < '0' || tok[i] > '9') return 0;
    return 1;
}

/* --- Copying / 복사 --- */

/**
 * @brief Copies a token or string into a fixed-size buffer, always terminated.
 *
 * ENGLISH
 * -------
 * @param[out] dst Destination buffer.
 * @param[in]  cap Capacity of `dst` in bytes, INCLUDING the terminator. Must
 *                 be at least 1.
 * @param[in]  src Source characters. Need not be null-terminated when `len` is
 *                 given, which is what makes this usable directly on a
 *                 ::txt_token result.
 * @param[in]  len How many characters to copy, or -1 to copy `src` up to its
 *                 own null terminator.
 * @return How many characters were written, not counting the terminator.
 *
 * @note Truncates rather than overflowing, and null-terminates in every case
 *       including truncation and `len == 0`.
 * @note The bound is tested BEFORE the source character, so the capacity is
 *       what stops the loop and a malformed source cannot run past the buffer.
 *       Five hand-written copy loops were replaced by this, and one of them
 *       (main.c's level-name copy) tested them the other way round -- correct
 *       only because the source happened to be terminated in range.
 * @warning `dst` and `src` must not overlap.
 *
 * 한국어
 * ------
 * @brief 토큰 또는 문자열을 고정 크기 버퍼에 복사하며, 항상 널로 종료합니다.
 * @param[out] dst 대상 버퍼.
 * @param[in]  cap `dst`의 용량 (바이트). 종료 문자를 *포함*합니다. 최소 1 이상이어야
 *                 합니다.
 * @param[in]  src 원본 문자열. `len`이 주어지면 널로 끝나지 않아도 되며, 덕분에
 *                 ::txt_token의 결과에 바로 사용할 수 있습니다.
 * @param[in]  len 복사할 문자 수. -1이면 `src` 자신의 널 종료 문자까지 복사합니다.
 * @return 종료 문자를 제외하고 기록된 문자 수.
 *
 * @note 넘치지 않고 잘라 내며, 잘린 경우와 `len == 0`인 경우를 포함해 모든 경우에
 *       널로 종료합니다.
 * @note 원본 문자보다 경계를 *먼저* 검사하므로 루프를 멈추는 것은 용량이며, 잘못된
 *       원본이 버퍼를 넘어설 수 없습니다. 손으로 작성한 복사 루프 다섯 개가 이 함수로
 *       대체되었는데, 그중 하나(main.c의 레벨 이름 복사)는 검사 순서가 반대였고 원본이
 *       우연히 범위 안에서 종료되었기 때문에만 올바르게 동작했습니다.
 * @warning `dst`와 `src`는 겹쳐서는 안 됩니다.
 */
static inline int txt_copy(char *dst, int cap, const char *src, int len) {
    int i = 0;
    /* `i < cap - 1` first: the capacity is what stops this loop, so a source
       that is longer than expected -- or not terminated at all -- truncates
       instead of running off the end of dst.
       `i < cap - 1`을 먼저 검사합니다. 루프를 멈추는 것은 용량이므로, 예상보다 긴
       원본이나 아예 종료되지 않은 원본도 dst 밖으로 나가지 않고 잘립니다. */
    for (; i < cap - 1 && (len < 0 || i < len) && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
    return i;
}

/* --- Building a line / 한 줄 만들기 --------------------------------------
 *
 * ENGLISH
 * -------
 * ::txt_copy answers "put this string in that buffer". These two answer "build
 * a line out of several pieces", which is the other half of the same job and
 * was being done by wsprintfA -- the one formatter in this project that takes
 * no destination capacity. Its every current call site is safe, because each
 * one formats integers and fixed string constants into a buffer sized for
 * them; the hazard is the next one, where a level name or a material name
 * read from an asset file reaches a format string and nothing in the
 * signature makes anyone check.
 *
 * Deliberately NOT a printf-style formatter. A varargs formatter needs a
 * format string parser, and its safety would then depend on the format string
 * agreeing with its arguments -- which is the property that fails silently and
 * is exactly what is being escaped. Appending is checked by the compiler
 * instead: the wrong type is a diagnostic, not a corrupted line.
 *
 * These are the two helpers diag.c wrote for itself, lifted here now that a
 * second caller wants them. See diag.c's note on why no formatting library is
 * used: pulling in snprintf for this would drag stdio into every tool that
 * links these parsers, and pulling in wsprintfA would drag windows.h into the
 * eight files that include txt.h and manage without it today.
 *
 * 한국어
 * ------
 * ::txt_copy는 "이 문자열을 저 버퍼에 넣어라"에 답합니다. 아래 둘은 "여러 조각으로 한 줄을
 * 만들어라"에 답하며, 이는 같은 일의 나머지 절반으로서 그동안 wsprintfA가 맡고 있었습니다.
 * 이 프로젝트에서 대상 용량을 받지 않는 유일한 포매터입니다. 현재의 모든 호출 지점은
 * 안전합니다. 각각 정수와 고정 문자열 상수를 그에 맞게 크기를 정한 버퍼에 넣기 때문입니다.
 * 위험한 것은 그다음입니다. 에셋 파일에서 읽은 레벨 이름이나 재질 이름이 형식 문자열에
 * 닿는 순간이며, 그때 서명에는 누구에게도 검사를 요구하는 것이 없습니다.
 *
 * printf 형식의 포매터가 아닌 것은 의도적입니다. 가변 인자 포매터는 형식 문자열 파서를
 * 필요로 하고, 그러면 안전성이 "형식 문자열이 인자와 일치하는가"에 의존하게 됩니다. 그것이
 * 바로 조용히 실패하는 성질이며 여기서 벗어나려는 대상입니다. 덧붙이기 방식은 대신
 * 컴파일러가 검사합니다. 타입이 틀리면 손상된 줄이 아니라 진단 메시지가 나옵니다.
 *
 * 이 둘은 diag.c가 스스로 작성했던 헬퍼이며, 두 번째 사용처가 생긴 지금 이곳으로
 * 옮겼습니다. 어떤 포매팅 라이브러리도 쓰지 않는 이유는 diag.c의 설명을 참조하십시오.
 * 이를 위해 snprintf를 끌어들이면 이 파서들을 링크하는 모든 도구에 stdio가 딸려 오고,
 * wsprintfA를 끌어들이면 txt.h를 포함하면서 오늘날 windows.h 없이 지내는 여덟 개 파일에
 * windows.h가 딸려 옵니다.
 */

/**
 * @brief Appends a string, truncating at the buffer's capacity.
 *
 * ENGLISH
 * -------
 * @param[out] dst Destination buffer.
 * @param[in]  cap Capacity of `dst` in bytes, INCLUDING the terminator.
 * @param[in]  pos Current write offset, as returned by the previous append.
 * @param[in]  s   Null-terminated string to append.
 * @return The new write offset, never more than `cap - 1`.
 *
 * @note Null-terminates after every call, so the buffer is a valid string at
 *       each step of the build rather than only at the end. That costs one
 *       store and removes the failure where an early return leaves a line
 *       unterminated.
 * @note Clamps `pos` into range rather than trusting it, so a misused offset
 *       truncates instead of writing outside the buffer.
 *
 * 한국어
 * ------
 * @brief 버퍼 용량에서 잘라 내며 문자열을 덧붙입니다.
 * @param[out] dst 대상 버퍼.
 * @param[in]  cap `dst`의 용량 (바이트). 종료 문자를 *포함*합니다.
 * @param[in]  pos 현재 쓰기 위치. 직전 덧붙이기의 반환값입니다.
 * @param[in]  s   덧붙일 널 종료 문자열.
 * @return 새로운 쓰기 위치. 절대 `cap - 1`을 넘지 않습니다.
 *
 * @note 호출할 때마다 널로 종료하므로, 버퍼는 마지막에만이 아니라 생성 과정의 매 단계에서
 *       유효한 문자열입니다. 저장 한 번의 비용으로, 중간에 반환되어 줄이 종료되지 않는
 *       실패를 없앱니다.
 * @note `pos`를 신뢰하지 않고 범위 안으로 제한하므로, 잘못 쓰인 위치는 버퍼 바깥에
 *       기록하지 않고 잘립니다.
 */
static inline int txt_append_str(char *dst, int cap, int pos, const char *s) {
    if (cap < 1) return pos;
    if (pos < 0) pos = 0;
    if (pos > cap - 1) pos = cap - 1;
    while (*s && pos < cap - 1) dst[pos++] = *s++;
    dst[pos] = 0;
    return pos;
}

/**
 * @brief Appends a signed integer in decimal.
 *
 * ENGLISH
 * -------
 * @param[out] dst   Destination buffer.
 * @param[in]  cap   Capacity of `dst` in bytes, INCLUDING the terminator.
 * @param[in]  pos   Current write offset.
 * @param[in]  value Value to render. Negatives get a leading '-'.
 * @return The new write offset, never more than `cap - 1`.
 *
 * @note Handles the whole `int` range. The magnitude is taken in UNSIGNED
 *       arithmetic because negating `INT_MIN` as a signed int is undefined --
 *       the one input a hand-rolled integer renderer reliably gets wrong.
 * @note Renders "0" for zero, which a digit loop written as `while (value)`
 *       would print as nothing at all.
 *
 * 한국어
 * ------
 * @brief 부호 있는 정수를 10진수로 덧붙입니다.
 * @param[out] dst   대상 버퍼.
 * @param[in]  cap   `dst`의 용량 (바이트). 종료 문자를 *포함*합니다.
 * @param[in]  pos   현재 쓰기 위치.
 * @param[in]  value 변환할 값. 음수에는 앞에 '-'가 붙습니다.
 * @return 새로운 쓰기 위치. 절대 `cap - 1`을 넘지 않습니다.
 *
 * @note `int` 전 범위를 처리합니다. 절댓값을 *부호 없는* 연산으로 구하는 이유는 `INT_MIN`을
 *       부호 있는 정수로 부호 반전하는 것이 정의되지 않은 동작이기 때문입니다. 손으로 만든
 *       정수 변환기가 어김없이 틀리는 바로 그 입력입니다.
 * @note 0에 대해 "0"을 출력합니다. `while (value)`로 쓴 자릿수 루프라면 아무것도 출력하지
 *       않았을 값입니다.
 */
static inline int txt_append_int(char *dst, int cap, int pos, int value) {
    if (cap < 1) return pos;
    if (pos < 0) pos = 0;
    if (pos > cap - 1) pos = cap - 1;

    /* Magnitude first, in unsigned. `0u - (unsigned)INT_MIN` is INT_MIN's
       magnitude exactly, where `-value` would be undefined.
       절댓값을 부호 없는 연산으로 먼저 구합니다. `0u - (unsigned)INT_MIN`은 INT_MIN의
       절댓값을 정확히 내며, `-value`는 정의되지 않은 동작입니다. */
    unsigned u = value < 0 ? 0u - (unsigned)value : (unsigned)value;

    /* Reverse into scratch, then unwind. 10 digits covers every unsigned 32-bit
       value; 12 leaves room without thinking about it.
       임시 버퍼에 역순으로 만든 뒤 되감습니다. 자릿수 10개면 32비트 부호 없는 모든 값을
       담으며, 12는 따져 볼 필요 없이 여유를 둡니다. */
    char tmp[12];
    int  n = 0;
    do { tmp[n++] = (char)('0' + (int)(u % 10u)); u /= 10u; } while (u);

    if (value < 0 && pos < cap - 1) dst[pos++] = '-';
    while (n > 0 && pos < cap - 1) dst[pos++] = tmp[--n];
    dst[pos] = 0;
    return pos;
}

/* --- Conversion / 변환 --- */

/**
 * @brief Converts a token to an integer.
 *
 * ENGLISH
 * -------
 * @brief Converts a token to an integer.
 * @param[in] tok Token start.
 * @param[in] len Token length.
 * @return The parsed value.
 * @warning Assumes the token has already passed ::txt_is_number. Non-digit
 *          characters produce a garbage value rather than an error, and a
 *          token too large for `int` overflows silently.
 *
 * 한국어
 * ------
 * @brief 토큰을 정수로 변환합니다.
 * @param[in] tok 토큰 시작 위치.
 * @param[in] len 토큰 길이.
 * @return 변환된 값.
 * @warning 토큰이 이미 ::txt_is_number를 통과했다고 가정합니다. 숫자가 아닌
 *          문자는 오류가 아닌 잘못된 값을 만들어 내며, `int` 범위를 넘는 토큰은
 *          조용히 오버플로됩니다.
 */
static inline int txt_to_int(const char *tok, int len) {
    int sign = 1, i = 0, v = 0;
    if (tok[0] == '-') { sign = -1; i = 1; }
    /* Horner's method: shift the running value up a decimal place per digit.
       호너의 방법입니다. 숫자마다 누적값을 10진수 한 자리씩 올립니다. */
    for (; i < len; i++) v = v * 10 + (tok[i] - '0');
    return v * sign;
}

/**
 * @brief Reads one integer and advances the read position.
 *
 * ENGLISH
 * -------
 * @brief Reads one integer and advances the read position.
 * @param[in]  p   Position in the text to read from.
 * @param[out] out Receives the parsed value; untouched on failure.
 * @param[out] ok  Receives 1 on success, 0 when the next token is not a
 *                 number. May be NULL if the caller does not care.
 * @return The position just past the number on success, or `p` unchanged on
 *         failure, so a failed read can be retried by a different rule.
 * @note This non-consuming failure is what lets a caller read a run of
 *       numbers until the next keyword without needing to look ahead.
 *
 * 한국어
 * ------
 * @brief 정수 하나를 읽고 읽기 위치를 진행시킵니다.
 * @param[in]  p   읽기를 시작할 텍스트 내 위치.
 * @param[out] out 변환된 값을 받습니다. 실패 시에는 변경되지 않습니다.
 * @param[out] ok  성공 시 1, 다음 토큰이 숫자가 아니면 0을 받습니다. 호출자가
 *                 필요로 하지 않으면 NULL이어도 됩니다.
 * @return 성공 시 숫자 바로 다음 위치. 실패 시에는 `p`가 그대로 반환되므로,
 *         실패한 읽기를 다른 규칙으로 재시도할 수 있습니다.
 * @note 이처럼 실패 시 소비하지 않는 특성 덕분에, 호출자는 미리 살펴보는 과정
 *       없이 다음 키워드가 나올 때까지 숫자 나열을 읽을 수 있습니다.
 */
static inline const char *txt_read_int(const char *p, int *out, int *ok) {
    int len;
    const char *t = txt_token(p, &len);
    /* Return the ORIGINAL p, not the skipped position: a failed read must
       leave the stream exactly as it found it.
       건너뛴 위치가 아닌 원래의 p를 반환합니다. 실패한 읽기는 스트림을 발견한
       그대로 남겨 두어야 합니다. */
    if (!t || !txt_is_number(t, len)) { if (ok) *ok = 0; return p; }
    *out = txt_to_int(t, len);
    if (ok) *ok = 1;
    return t + len;
}

#endif
