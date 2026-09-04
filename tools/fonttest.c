/* fonttest -- what a string turns into, with no window.
 *
 * WHAT IS ACTUALLY AT RISK HERE, now that font.c draws Hangul. Not "does the
 * font look right" -- that is a question for eyes, and the glyphs are 김중태's
 * and were right in 1990. What is at risk is the arithmetic that stands
 * between a code point and a cell, because it is a table lookup feeding an
 * array index, and there are 11,172 code points that reach it.
 *
 * So this walks every one of them. A syllable that picked a 벌 out of range
 * would read past HANGUL and bake whatever followed it into the atlas -- a
 * corruption that shows up as one wrong glyph in one word, months later, in
 * the one sentence nobody tested. The check that it cannot happen is cheaper
 * than the bug.
 *
 * THE SECOND THING is that a byte stopped being a character. Every caller of
 * font_width was written when the font was fixed-pitch ASCII and the width was
 * `strlen * FONT_CW`, and the header promised exactly that. It no longer does,
 * so what is asserted below is the new promise: characters, not bytes, and two
 * pitches rather than one.
 *
 * 이제 font.c가 한글을 그리게 되었을 때 실제로 위험한 것. "글꼴이 제대로 보이는가"가
 * 아닙니다. 그것은 눈으로 볼 문제이고, 글리프는 김중태의 것이며 1990년에 이미 옳았습니다.
 * 위험한 것은 코드 포인트와 칸 사이에 놓인 산술입니다. 표 조회가 배열 인덱스로 흘러들고,
 * 그곳에 닿는 코드 포인트가 11,172개이기 때문입니다.
 *
 * 그래서 이 파일은 그 전부를 훑습니다. 범위를 벗어난 벌을 고른 음절은 HANGUL 너머를 읽어
 * 그 뒤에 있던 것을 아틀라스에 구워 넣습니다. 몇 달 뒤, 아무도 시험하지 않은 그 한 문장의,
 * 한 단어의, 한 글리프만 잘못 나오는 오염입니다. 그런 일이 없다는 검사가 그 버그보다
 * 쌉니다.
 *
 * 둘째는 바이트가 더는 문자가 아니게 되었다는 것입니다. font_width의 모든 호출자는 이
 * 폰트가 고정폭 ASCII이고 너비가 `strlen * FONT_CW`이던 시절에 쓰였으며, 헤더도 정확히
 * 그렇게 약속했습니다. 이제 아닙니다. 아래에서 단언하는 것은 새 약속입니다. 바이트가 아니라
 * 문자이고, 폭은 하나가 아니라 둘입니다.
 */

#include <stdio.h>
#include <string.h>
#include "font.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okf(int cond, const char *what, double got, double want) {
    printf("  %-58s %s", what, cond ? "ok\n" : "FAIL");
    if (!cond) { printf("  (got %g, want %g)\n", got, want); fails++; }
}

static void oki(int cond, const char *what, int got, int want) {
    printf("  %-58s %s", what, cond ? "ok\n" : "FAIL");
    if (!cond) { printf("  (got %d, want %d)\n", got, want); fails++; }
}

/* --- reading the quads back out ------------------------------------------
   font_text appends six vertices per quad and nothing else, so the buffer is
   the whole record of what it decided. The atlas is 256 wide and the Hangul
   band starts 64 rows down; recovering a cell index from a u,v is that in
   reverse, and it is how a rule table gets checked without a GL context.
   font_text는 쿼드마다 정점 여섯 개를 덧붙일 뿐 그 밖의 일은 하지 않으므로, 버퍼가 그
   함수가 내린 결정의 전부입니다. 아틀라스는 너비 256이고 한글 띠는 64행 아래에서
   시작합니다. u,v로부터 칸 번호를 되찾는 것은 그 역산이며, GL 컨텍스트 없이 규칙 표를
   검사하는 방법입니다. */

#define ATLAS_W 256
#define ATLAS_H 512
#define KCELL   16
#define KY      64

static Vtx  vbuf[4096];
static MeshBuf buf = { vbuf, 0, 4096 };

static int quads(const char *s) {
    buf.count = 0;
    font_text(&buf, 0, 0, 1.0f, s);
    return buf.count / 6;
}

/* Cell index of quad `q`, or -1 if its UV is not inside the Hangul band.
   쿼드 `q`의 칸 번호. UV가 한글 띠 안에 있지 않으면 -1입니다. */
static int cell_of(int q) {
    float u = vbuf[q * 6].u, v = vbuf[q * 6].v;
    int cx = (int)(u * ATLAS_W + 0.5f), cy = (int)(v * ATLAS_H + 0.5f);
    if (cy < KY || cx % KCELL || (cy - KY) % KCELL) return -1;
    return (cy - KY) / KCELL * (ATLAS_W / KCELL) + cx / KCELL;
}

int main(void) {
    printf("fonttest\n");

    /* --- the width contract ----------------------------------------------
       The header used to promise one pitch and now promises two. These are
       that promise, written so a caller that laid out a line by counting
       bytes would fail here rather than off the right edge of the screen.
       헤더는 폭이 하나라고 약속했었고 이제 둘이라고 약속합니다. 아래는 그 약속이며,
       바이트를 세어 줄을 배치한 호출자가 화면 오른쪽 바깥이 아니라 이곳에서 실패하도록
       썼습니다. */
    printf("\n  -- the width contract --\n");

    okf(font_width(2.0f, "ABC") == 3 * FONT_CW * 2.0f,
        "ASCII still advances FONT_CW per character",
        font_width(2.0f, "ABC"), 3 * FONT_CW * 2.0f);

    okf(font_width(2.0f, "가나다") == 3 * FONT_KW * 2.0f,
        "a syllable advances FONT_KW, not FONT_CW",
        font_width(2.0f, "가나다"), 3 * FONT_KW * 2.0f);

    okf(font_width(1.0f, "가A") == (float)(FONT_KW + FONT_CW),
        "a mixed run adds the two pitches",
        font_width(1.0f, "가A"), (double)(FONT_KW + FONT_CW));

    oki((int)strlen("가") == 3, "the encoding really is three bytes wide",
        (int)strlen("가"), 3);

    okf(font_width(1.0f, "가") == (float)FONT_KW,
        "but it costs one advance, not three",
        font_width(1.0f, "가"), (double)FONT_KW);

    okf(font_width(1.0f, "") == 0.0f, "an empty string is zero wide",
        font_width(1.0f, ""), 0.0);

    /* --- malformed input --------------------------------------------------
       A truncated sequence is not a hypothetical: a string clipped to a fixed
       buffer ends mid-character about two times in three. What must not happen
       is a hang, and what should happen is a visible '?'.
       잘린 시퀀스는 가정이 아닙니다. 고정 버퍼에 맞춰 잘린 문자열은 셋 중 둘 꼴로 문자
       가운데에서 끝납니다. 일어나서는 안 되는 것은 멈춤이고, 일어나야 하는 것은 눈에 보이는
       '?'입니다. */
    printf("\n  -- malformed input --\n");

    /* One '?' per stray byte rather than one for the sequence, because the
       decoder gives up a byte at a time. That is the choice that keeps a bad
       lead from eating the good character after it, and the count below is
       what it costs: a clipped 가 is two marks, which is honestly what is
       left of it.
       시퀀스 하나당 '?' 하나가 아니라 표류 바이트마다 하나입니다. 디코더가 한 번에 한
       바이트씩만 포기하기 때문입니다. 잘못된 선두 바이트가 그 뒤의 멀쩡한 문자를 먹지 않게
       하는 선택이며, 아래 개수가 그 대가입니다. 잘린 가는 두 개의 표시이고, 그것이 정직하게
       남은 전부입니다. */
    okf(font_width(1.0f, "\xEA\xB0") == 2 * (float)FONT_CW,
        "a truncated sequence is one '?' per stray byte",
        font_width(1.0f, "\xEA\xB0"), 2 * (double)FONT_CW);

    okf(font_width(1.0f, "\x80\x80") == 2 * (float)FONT_CW,
        "stray continuation bytes are one '?' each",
        font_width(1.0f, "\x80\x80"), 2 * (double)FONT_CW);

    okf(font_width(1.0f, "\xEA\xB0\x80" "A") == (float)(FONT_KW + FONT_CW),
        "a well-formed syllable does not swallow what follows",
        font_width(1.0f, "\xEA\xB0\x80" "A"), (double)(FONT_KW + FONT_CW));

    okf(font_width(1.0f, "\xF0\x9F\x98\x80") == 4 * (float)FONT_CW,
        "a four-byte lead is four '?' rather than one lost character",
        font_width(1.0f, "\xF0\x9F\x98\x80"), 4 * (double)FONT_CW);

    ok(font_width(1.0f, "ABC") == font_text(&buf, 0, 0, 1.0f, "ABC"),
       "font_width and font_text agree, as the header says");

    /* --- what a syllable emits -------------------------------------------- */
    printf("\n  -- what a syllable emits --\n");

    oki(quads("가") == 2, "a syllable with no 받침 is two quads", quads("가"), 2);
    oki(quads("각") == 3, "one with a 받침 is three", quads("각"), 3);
    oki(quads("A")  == 1, "a letter is still one", quads("A"), 1);

    /* 'A' is cell 33 of the ASCII grid and must stay there: the Hangul band was
       added below the old grid rather than through it.
       'A'는 ASCII 격자의 33번 칸이며 그 자리에 있어야 합니다. 한글 띠는 옛 격자를 관통하지
       않고 그 아래에 덧붙였기 때문입니다. */
    quads("A");
    oki(cell_of(0) == -1, "a letter does not land in the Hangul band", cell_of(0), -1);

    /* --- the rules, spot-checked -------------------------------------------
       Two rules are worth naming because both were measured rather than cited.
       The first: a 받침 changes which 초성 is drawn, so 가 and 각 must differ
       in their first quad. The second, and the strange one: ㄱ and ㅋ are the
       only initials that reach down into the vowel, so 고 gets a ㅗ with a
       shortened stem and 노 does not -- the same vowel, two different cells.
       측정으로 얻은 규칙 둘은 이름을 불러 둘 값이 있습니다. 첫째, 받침은 어떤 초성을
       그릴지를 바꾸므로 가와 각은 첫 쿼드가 달라야 합니다. 둘째이자 기묘한 쪽은, ㄱ과 ㅋ만이
       모음의 자리로 내려오는 초성이라는 것입니다. 그래서 고는 줄기를 줄인 ㅗ를 받고 노는
       받지 않습니다. 같은 모음, 다른 칸입니다. */
    printf("\n  -- the rules --\n");

    quads("가"); int cho_ga = cell_of(0), jung_ga = cell_of(1);
    quads("각"); int cho_gak = cell_of(0);
    quads("고"); int jung_go = cell_of(1);
    quads("노"); int jung_no = cell_of(1);

    ok(cho_ga != cho_gak, "a 받침 changes which 초성 is drawn");
    ok(jung_go != jung_no, "ㄱ shortens the stem of ㅗ where ㄴ does not");
    ok(cho_ga >= 1 && cho_ga < 160, "초성 comes from the first 160 cells");
    ok(jung_ga >= 160 && jung_ga < 248, "중성 from the next 88");

    quads("각"); ok(cell_of(2) >= 248 && cell_of(2) < 360, "종성 from the last 112");

    /* --- every syllable ----------------------------------------------------
       The whole point of the file. 11,172 code points, each one an index into
       a 360-cell table, and nothing between them and a read past the end but
       three lookup tables that were derived from bitmaps.
       이 파일의 요점입니다. 코드 포인트 11,172개가 저마다 360칸짜리 표의 인덱스가 되며,
       그것과 범위 밖 읽기 사이에 있는 것이라고는 비트맵에서 유도한 조회 표 셋뿐입니다. */
    printf("\n  -- every syllable --\n");

    int bad_count = 0, bad_cell = 0, bad_band = 0, checked = 0;
    int seen_cho[160] = {0}, seen_jung[88] = {0}, seen_jong[112] = {0};

    for (unsigned cp = 0xAC00u; cp <= 0xD7A3u; cp++) {
        char s[4];
        s[0] = (char)(0xE0u | (cp >> 12));
        s[1] = (char)(0x80u | ((cp >> 6) & 0x3F));
        s[2] = (char)(0x80u | (cp & 0x3F));
        s[3] = 0;

        int n = quads(s);
        int jong = (int)((cp - 0xAC00u) % 28u);
        if (n != (jong ? 3 : 2)) { bad_count++; continue; }

        for (int q = 0; q < n; q++) {
            int c = cell_of(q);
            if (c < 0)   { bad_band++; continue; }
            if (c >= 360){ bad_cell++; continue; }

            if (q == 0 && c >= 1   && c < 160) seen_cho [c]       = 1;
            if (q == 1 && c >= 160 && c < 248) seen_jung[c - 160] = 1;
            if (q == 2 && c >= 248 && c < 360) seen_jong[c - 248] = 1;

            /* The 초성/중성/종성 bands are contiguous and in that order, so a
               quad that strayed out of its own band is a table that indexed
               into a neighbour's.
               초성·중성·종성 띠는 그 순서로 잇닿아 있으므로, 자기 띠를 벗어난 쿼드는 표가
               이웃의 자리를 인덱싱했다는 뜻입니다. */
            int lo = q == 0 ? 1 : q == 1 ? 160 : 248;
            int hi = q == 0 ? 160 : q == 1 ? 248 : 360;
            if (c < lo || c >= hi) bad_cell++;
        }
        checked++;
    }

    oki(checked == 11172, "every precomposed syllable composed", checked, 11172);
    oki(bad_count == 0, "none emitted the wrong number of quads", bad_count, 0);
    oki(bad_band  == 0, "none sampled outside the Hangul band", bad_band, 0);
    oki(bad_cell  == 0, "none reached a cell outside its own third", bad_cell, 0);

    /* Coverage the other way round: a rule table that collapsed to one 벌
       would pass every check above and still be wrong, because the font would
       compose but stop fitting. Counting the cells actually reached is what
       notices that.
       반대 방향의 커버리지입니다. 한 벌로 주저앉은 규칙 표는 위의 모든 검사를 통과하면서도
       여전히 틀렸습니다. 조합은 되지만 아귀가 맞지 않게 되기 때문입니다. 실제로 닿은 칸을
       세는 것이 그것을 알아챕니다. */
    int n_cho = 0, n_jung = 0, n_jong = 0;
    for (int i = 0; i < 160; i++) n_cho  += seen_cho[i];
    for (int i = 0; i < 88;  i++) n_jung += seen_jung[i];
    for (int i = 0; i < 112; i++) n_jong += seen_jong[i];

    oki(n_cho == 8 * 19, "all eight 초성 sets are reached, all 19 each", n_cho, 152);
    oki(n_jung == 4 * 21, "all four 중성 sets, all 21 each", n_jung, 84);
    oki(n_jong == 4 * 27, "all four 종성 sets, all 27 each", n_jong, 108);

    printf("\n%s\n", fails ? "FAILURES" : "all font checks passed");
    return fails ? 1 : 0;
}
