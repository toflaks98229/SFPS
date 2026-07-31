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
