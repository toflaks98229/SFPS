/**
 * @file brush.c
 * @brief Reads .map text and turns brush planes into face polygons.
 *
 * ENGLISH
 * -------
 * Four stages, in the order they depend on each other, exactly as
 * ::level_load's four named steps are: TOKENISE the text, READ a face into a
 * plane, ASSEMBLE faces into brushes and brushes into entities, then DERIVE
 * each brush's box from the polygons its planes cut out of one another. See
 * brush.h for what a brush is and why there is no BSP tree.
 *
 * 한국어
 * ------
 * 서로 의존하는 순서대로 네 단계이며, ::level_load의 이름 붙은 네 단계와 같은 방식입니다.
 * 텍스트를 토큰으로 나누고, 면 하나를 평면으로 읽고, 면을 브러시로 브러시를 엔티티로
 * 조립하고, 마지막으로 각 브러시의 박스를 그 평면들이 서로를 잘라 내어 만든 폴리곤에서
 * 유도합니다. 브러시가 무엇이고 왜 BSP 트리가 없는지는 brush.h를 참조하십시오.
 */

#include "brush.h"

/* The render types brush.h only forward-declares. Included HERE and not there,
   which is the same split level.h and level.c keep: the header stays usable by
   headless simulation code, and only the one file that actually emits vertices
   pays for gl.h and windows.h.
   brush.h가 전방 선언만 해 둔 렌더 타입입니다. 그곳이 아니라 *이곳에서* 포함하며, 이는
   level.h와 level.c가 유지하는 것과 같은 분리입니다. 헤더는 헤드리스 시뮬레이션 코드가 계속
   쓸 수 있고, 실제로 정점을 내보내는 파일 하나만 gl.h와 windows.h의 비용을 치릅니다. */
#include "render.h"
#include "model.h"

#include "txt.h"
#include "diag.h"

#include <math.h>

/* The starting quad's half-extent, in world units. Twice the declared world
   so a face plane anywhere inside it is still fully covered before clipping.
   See BRUSH_MAX_COORD for why this is not larger.
   절단 전 시작 사각형의 절반 크기이며 월드 단위입니다. 선언된 세계의 두 배이므로, 그 안
   어디에 놓인 면 평면이든 절단 전에는 완전히 덮입니다. 왜 더 크지 않은지는
   BRUSH_MAX_COORD를 참조하십시오. */
#define QUAD_R (2.0f * BRUSH_MAX_COORD * BRUSH_UNIT)

/* Any vertex past this is outside the world the map may describe, which means
   the brush's other planes never bounded this face.
   이 값을 넘는 정점은 맵이 기술할 수 있는 세계 바깥에 있으며, 이는 브러시의 다른 평면들이
   이 면을 한정한 적이 없다는 뜻입니다. */
#define WORLD_R (BRUSH_MAX_COORD * BRUSH_UNIT)

/* ------------------------------------------------------------------ axes */

/* Map space is z-up; the engine is y-up. See ::brush_parse for why this is a
   rotation and why that matters.
   맵 공간은 z가 위이고 엔진은 y가 위입니다. 왜 이것이 회전이며 그 점이 왜 중요한지는
   ::brush_parse를 참조하십시오. */

/* A direction: rotated, not scaled. Normals and texture axes stay unit length,
   which is what lets `dot(n, p)` keep meaning a distance.
   방향입니다. 회전하되 크기는 바꾸지 않습니다. 법선과 텍스처 축이 단위 길이를 유지하며,
   그래야 `dot(n, p)`가 계속 거리를 뜻합니다. */
static v3 map_dir(float x, float y, float z) { return v3f(x, z, -y); }

/* A position: rotated and scaled into metres. / 위치입니다. 회전 후 미터로 환산합니다. */
static v3 map_pos(float x, float y, float z) {
    return v3scale(map_dir(x, y, z), BRUSH_UNIT);
}

/* ---------------------------------------------------------------- lexing */

/**
 * The .map tokeniser.
 *
 * ENGLISH
 * -------
 * NOT txt.h's, and the reasons are specific rather than stylistic:
 *
 *   - ::txt_skip treats `#` as a comment and knows nothing of `//`. A .map has
 *     no `#` and every TrenchBroom file is full of `// entity 0` headers, so
 *     the shared tokeniser would read the comments as data and skip nothing.
 *   - Brackets are SELF-DELIMITING here. `(-64 -64 -16)` and `( -64 -64 -16 )`
 *     are both written in the wild -- the first by hand, the second by
 *     TrenchBroom -- and a whitespace-only split turns `(-64` into one token.
 *   - `"key" "value"` needs quoted strings, which txt.h has no notion of, and
 *     a value may legitimately contain spaces.
 *   - .map numbers are floats. ::txt_read_int is integers only, and a UV axis
 *     is `-0.707107`.
 *
 * So this is a second tokeniser on purpose. What is NOT duplicated is string
 * copying: ::txt_copy still does that, per the project rule against strcpy.
 *
 * 한국어
 * ------
 * txt.h의 것이 아니며, 그 이유는 취향이 아니라 구체적입니다.
 *
 *   - ::txt_skip은 `#`을 주석으로 취급하고 `//`를 모릅니다. .map에는 `#`이 없고 모든
 *     TrenchBroom 파일은 `// entity 0` 머리말로 가득하므로, 공용 토크나이저는 주석을
 *     데이터로 읽고 아무것도 건너뛰지 않게 됩니다.
 *   - 이곳에서 괄호는 스스로 경계가 됩니다. `(-64 -64 -16)`과 `( -64 -64 -16 )`이 둘 다
 *     실제로 쓰이며(앞은 손으로, 뒤는 TrenchBroom이) 공백만으로 나누면 `(-64`가 한 토큰이
 *     됩니다.
 *   - `"key" "value"`에는 따옴표 문자열이 필요한데 txt.h에는 그 개념이 없고, 값에는 공백이
 *     정당하게 들어갈 수 있습니다.
 *   - .map의 수는 실수입니다. ::txt_read_int는 정수뿐이고 UV 축은 `-0.707107`입니다.
 *
 * 따라서 이것은 의도적으로 두 번째 토크나이저입니다. 중복되지 *않는* 것은 문자열 복사이며,
 * strcpy 계열을 금지하는 프로젝트 규칙에 따라 여전히 ::txt_copy가 담당합니다.
 */
typedef struct {
    const char *p;      /* Read cursor. / 읽기 위치. */
    const char *end;    /* One past the last byte. / 마지막 바이트의 다음. */
    const char *tok;    /* Current token; borrows into the text. / 현재 토큰. 원본 텍스트를 참조합니다. */
    int         len;    /* Its length. / 그 길이. */
    int         quoted; /* It came from "..." and is data, never syntax. / "..."에서 왔으며 구문이 아니라 데이터입니다. */
} Lex;

/* An END POINTER rather than a terminator check, resolved once here.
   ::data_map returns a slice of the packed blob where the next map starts
   immediately after this one, so there is no null byte to stop at. Working out
   the end at init means every scan below is one bound test instead of two, and
   there is no path where a missing terminator and a supplied length disagree.
   종료 문자 검사가 아니라 끝 포인터이며, 이곳에서 한 번 결정됩니다. ::data_map은 포장된
   블롭의 일부를 돌려주고 그곳에서는 다음 맵이 이 맵 바로 뒤에서 시작하므로, 멈출 널 바이트가
   없습니다. 초기화 시점에 끝을 정해 두면 아래의 모든 훑기가 두 번이 아닌 한 번의 경계 검사가
   되며, 없는 종료 문자와 주어진 길이가 어긋날 경로도 없습니다. */
static void lex_init(Lex *L, const char *text, int len) {
    L->p = text;
    if (len >= 0) {
        L->end = text + len;
    } else {
        const char *e = text;
        while (*e) e++;
        L->end = e;
    }
    L->tok = 0; L->len = 0; L->quoted = 0;
}

static int lex_delim(char c) {
    return c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']';
}

static int lex_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int lex_next(Lex *L) {
    const char *p = L->p, *e = L->end;

    /* Alternating, like ::txt_skip: a comment may be followed by more space and
       another comment, so neither pass alone reaches the token.
       ::txt_skip과 같이 교대로 반복합니다. 주석 뒤에 다시 공백과 또 다른 주석이 올 수
       있으므로 어느 한쪽만으로는 토큰에 닿지 못합니다. */
    for (;;) {
        while (p < e && lex_space(*p)) p++;
        if (e - p >= 2 && p[0] == '/' && p[1] == '/') {
            while (p < e && *p != '\n') p++;
            continue;
        }
        break;
    }

    if (p >= e) { L->p = p; L->tok = 0; L->len = 0; L->quoted = 0; return 0; }

    if (*p == '"') {
        const char *s = ++p;
        /* Stops at a newline as well as at the closing quote. An unterminated
           string would otherwise swallow the rest of the file and report the
           error hundreds of lines from where it is.
           닫는 따옴표뿐 아니라 줄바꿈에서도 멈춥니다. 그러지 않으면 종료되지 않은 문자열이
           파일의 나머지를 통째로 삼키고, 실제 위치에서 수백 줄 떨어진 곳에서 오류를
           보고하게 됩니다. */
        while (p < e && *p != '"' && *p != '\n') p++;
        L->tok = s; L->len = (int)(p - s); L->quoted = 1;
        L->p = (p < e && *p == '"') ? p + 1 : p;
        return 1;
    }

    L->quoted = 0;
    if (lex_delim(*p)) { L->tok = p; L->len = 1; L->p = p + 1; return 1; }

    const char *s = p;
    while (p < e && !lex_space(*p) && !lex_delim(*p) && *p != '"') p++;
    L->tok = s; L->len = (int)(p - s); L->p = p;
    return 1;
}

/* Structural words are never quoted: a texture literally named `}` must not
   end the brush that uses it.
   구문 단어는 결코 따옴표 안에 있지 않습니다. 이름이 정말로 `}`인 텍스처가 그것을 쓰는
   브러시를 끝내서는 안 됩니다. */
static int lex_want(Lex *L, const char *lit) {
    return lex_next(L) && !L->quoted && txt_is(L->tok, L->len, lit);
}

/**
 * One float, written out rather than taken from the C library.
 *
 * ENGLISH
 * -------
 * `strtof` would drag stdlib into every tool that links this, which is the same
 * argument diag.c makes about `snprintf` and txt.h makes about `wsprintfA`.
 * The grammar accepted is the one TrenchBroom writes: an optional sign, digits,
 * an optional fraction, and an optional exponent -- the last because some
 * versions format with `%g` and emit `1e-06` for a UV axis that rounded small.
 *
 * TRAILING CHARACTERS ARE A FAILURE, not ignored. The token is already
 * delimited by the lexer, so anything left over means the file said something
 * that is not a number, and reading `64abc` as 64 would accept a corrupt map
 * as a valid one.
 *
 * 한국어
 * ------
 * `strtof`를 쓰면 이것을 링크하는 모든 도구에 stdlib이 딸려 들어옵니다. diag.c가
 * `snprintf`에 대해, txt.h가 `wsprintfA`에 대해 펴는 것과 같은 논거입니다. 받아들이는 문법은
 * TrenchBroom이 쓰는 것입니다. 선택적 부호, 숫자, 선택적 소수부, 선택적 지수부이며, 마지막
 * 항목은 일부 판본이 `%g`로 서식화하여 작게 반올림된 UV 축에 `1e-06`을 내보내기 때문입니다.
 *
 * 남은 문자는 무시가 아니라 실패입니다. 토큰은 이미 렉서가 경계를 지었으므로 남는 것이
 * 있다면 파일이 숫자가 아닌 무언가를 말했다는 뜻이고, `64abc`를 64로 읽는 것은 손상된 맵을
 * 유효한 맵으로 받아들이는 일입니다.
 */
static int parse_float(const char *s, int len, float *out) {
    int i = 0, digits = 0, sign = 1;
    double v = 0.0;

    if (i < len && (s[i] == '+' || s[i] == '-')) { sign = (s[i] == '-') ? -1 : 1; i++; }
    while (i < len && s[i] >= '0' && s[i] <= '9') { v = v * 10.0 + (s[i] - '0'); i++; digits++; }
    if (i < len && s[i] == '.') {
        double f = 0.1;
        i++;
        while (i < len && s[i] >= '0' && s[i] <= '9') { v += (s[i] - '0') * f; f *= 0.1; i++; digits++; }
    }
    if (!digits) return 0;

    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        int esign = 1, e = 0, edig = 0;
        i++;
        if (i < len && (s[i] == '+' || s[i] == '-')) { esign = (s[i] == '-') ? -1 : 1; i++; }
        while (i < len && s[i] >= '0' && s[i] <= '9') { e = e * 10 + (s[i] - '0'); i++; edig++; }
        if (!edig) return 0;
        /* Clamped rather than looped: past this the result is infinity or zero
           whatever the exact exponent was, and an unclamped loop on a hostile
           `1e999999` would spin.
           반복하지 않고 제한합니다. 이 너머에서는 정확한 지수가 무엇이든 결과가 무한대이거나
           0이며, 제한 없는 반복은 악의적인 `1e999999`에서 멈추지 않습니다. */
        if (e > 60) e = 60;
        for (int k = 0; k < e; k++) v = (esign > 0) ? v * 10.0 : v / 10.0;
    }

    if (i != len) return 0;
    *out = (float)(sign * v);
    return 1;
}

static int lex_num(Lex *L, float *out) {
    return lex_next(L) && parse_float(L->tok, L->len, out);
}

/* ------------------------------------------------ standard-format UV axes */

/**
 * Quake's base texture axes, in MAP space (z up), six groups of
 * {normal, u, v}.
 *
 * ENGLISH
 * -------
 * A Standard-format face names no axes, so they are derived from whichever of
 * these six the face most nearly faces. Reproduced from qbsp rather than
 * re-derived for a y-up world, and that is deliberate: this table IS the
 * definition of what a Standard face means, and a table rewritten into other
 * axes is a second definition that has to be proved equal to the first.
 * Deriving here and rotating afterwards keeps one.
 *
 * 한국어
 * ------
 * Quake의 기저 텍스처 축이며, *맵* 공간(z가 위)에서 {법선, u, v} 여섯 묶음입니다.
 *
 * Standard 형식 면은 축을 지목하지 않으므로, 이 여섯 중 그 면이 가장 가깝게 향한 것에서
 * 유도합니다. y가 위인 세계를 위해 다시 유도하지 않고 qbsp에서 그대로 옮겨 왔으며 이는
 * 의도적입니다. 이 표가 곧 "Standard 면이 무엇을 뜻하는가"의 정의이고, 다른 축으로 다시 쓴
 * 표는 첫 번째와 같음을 증명해야 하는 두 번째 정의입니다. 이곳에서 유도하고 나중에
 * 회전시키면 정의는 하나로 남습니다.
 */
static const float BASE_AXIS[18][3] = {
    { 0, 0, 1}, { 1, 0, 0}, { 0,-1, 0},   /* floor      */
    { 0, 0,-1}, { 1, 0, 0}, { 0,-1, 0},   /* ceiling    */
    { 1, 0, 0}, { 0, 1, 0}, { 0, 0,-1},   /* west wall  */
    {-1, 0, 0}, { 0, 1, 0}, { 0, 0,-1},   /* east wall  */
    { 0, 1, 0}, { 1, 0, 0}, { 0, 0,-1},   /* south wall */
    { 0,-1, 0}, { 1, 0, 0}, { 0, 0,-1}    /* north wall */
};

/* Derives a Standard face's u and v axes in MAP space. `n` is the face's
   map-space normal; `rot` is the authored rotation in degrees. */
static void std_axes(v3 n, float rot, float u[3], float v[3]) {
    int best = 0;
    float bestd = -1.0f;
    for (int i = 0; i < 6; i++) {
        float d = n.x * BASE_AXIS[i*3][0] + n.y * BASE_AXIS[i*3][1] + n.z * BASE_AXIS[i*3][2];
        if (d > bestd) { bestd = d; best = i; }
    }
    for (int k = 0; k < 3; k++) {
        u[k] = BASE_AXIS[best*3 + 1][k];
        v[k] = BASE_AXIS[best*3 + 2][k];
    }

    /* The quarter turns are spelled out rather than left to sinf and cosf.
       cosf(pi/2) is -4.4e-8, not 0, and a u axis carrying that is no longer
       axis-aligned -- which shows up as a texture that creeps by a texel across
       a long wall and matches nothing on the wall beside it. Quake's own qbsp
       special-cases these four for the same reason.
       4분의 1 회전은 sinf와 cosf에 맡기지 않고 직접 적습니다. cosf(pi/2)는 0이 아니라
       -4.4e-8이며, 그 값을 지닌 u축은 더 이상 축에 정렬되어 있지 않습니다. 그 결과는 긴 벽을
       가로지르며 텍셀 단위로 밀려나고 옆 벽과는 아무것도 맞지 않는 텍스처입니다. Quake의
       qbsp도 같은 이유로 이 네 경우를 특수 처리합니다. */
    float sv, cv;
    if      (rot ==   0.0f) { sv = 0.0f; cv =  1.0f; }
    else if (rot ==  90.0f) { sv = 1.0f; cv =  0.0f; }
    else if (rot == 180.0f) { sv = 0.0f; cv = -1.0f; }
    else if (rot == 270.0f) { sv = -1.0f; cv = 0.0f; }
    else { float a = rot * (M_PI_F / 180.0f); sv = sinf(a); cv = cosf(a); }

    /* Both base axes are axis-aligned, so rotating within the face plane is a
       rotation of the two components that are not zero.
       두 기저 축 모두 축에 정렬되어 있으므로, 면 평면 안에서의 회전은 0이 아닌 두 성분에
       대한 회전입니다. */
    int si = u[0] ? 0 : (u[1] ? 1 : 2);
    int ti = v[0] ? 0 : (v[1] ? 1 : 2);
    float *axis[2] = { u, v };
    for (int i = 0; i < 2; i++) {
        float ns = cv * axis[i][si] - sv * axis[i][ti];
        float nt = sv * axis[i][si] + cv * axis[i][ti];
        axis[i][si] = ns;
        axis[i][ti] = nt;
    }
}

/* ----------------------------------------------------------- face parsing */

/* Copies a name and says whether all of it fitted. The caller reports; this
   only measures, so the two decisions stay apart.
   이름을 복사하고 전부 들어갔는지 알려 줍니다. 보고는 호출자가 하며 이 함수는 재기만
   하므로 두 판단이 분리되어 있습니다. */
static int copy_fits(char *dst, int cap, const char *src, int len) {
    return txt_copy(dst, cap, src, len) == len;
}

/* Reads `x y z )`, the opening bracket having been consumed already. */
static int read_point(Lex *L, v3 *out) {
    float x, y, z;
    if (!lex_num(L, &x) || !lex_num(L, &y) || !lex_num(L, &z)) return 0;
    if (!lex_want(L, ")")) return 0;
    *out = v3f(x, y, z);
    return 1;
}

/**
 * One face: three points, a texture, and a UV description in either format.
 *
 * ENGLISH
 * -------
 * WHICH FORMAT IS DECIDED PER FACE, by looking at the token after the texture
 * name: `[` begins a Valve 220 axis and anything else is a Standard offset.
 * Per face rather than per file because a file may hold both -- TrenchBroom
 * writes whichever the map was saved as, and a brush pasted between two maps
 * arrives in the format it left.
 *
 * @param store 0 when the caller has no room left. The face is still PARSED,
 *              because the tokens have to be consumed either way or everything
 *              after this brush is read as garbage. Only the storing is
 *              skipped.
 * @return 1 on a well-formed face, 0 on a syntax error.
 *
 * 한국어
 * ------
 * 면 하나입니다. 세 점, 텍스처, 그리고 두 형식 중 하나로 된 UV 기술입니다.
 *
 * 어느 형식인지는 텍스처 이름 다음 토큰을 보고 *면마다* 결정합니다. `[`이면 Valve 220 축이
 * 시작되고 그 밖의 것이면 Standard 오프셋입니다. 파일마다가 아니라 면마다인 이유는 한 파일이
 * 둘 다 담을 수 있기 때문입니다. TrenchBroom은 그 맵이 저장된 형식으로 쓰고, 두 맵 사이를
 * 오간 브러시는 떠날 때의 형식 그대로 도착합니다.
 *
 * @param store 호출자에게 자리가 없으면 0입니다. 그래도 면은 *파싱합니다*. 어느 쪽이든
 *              토큰은 소비되어야 하며, 그러지 않으면 이 브러시 이후의 모든 것이 쓰레기로
 *              읽히기 때문입니다. 건너뛰는 것은 저장뿐입니다.
 */
static int parse_face(Lex *L, BrushMap *m, int store) {
    v3 mp[3];

    /* The first '(' was consumed by the caller, which is how it knew this was a
       face and not the brush's closing brace.
       첫 '('는 호출자가 소비했습니다. 그것이 이곳이 브러시의 닫는 중괄호가 아니라 면임을
       호출자가 알아낸 방법입니다. */
    if (!read_point(L, &mp[0])) return 0;
    for (int i = 1; i < 3; i++) {
        if (!lex_want(L, "(")) return 0;
        if (!read_point(L, &mp[i])) return 0;
    }

    if (!lex_next(L)) return 0;
    const char *tex = L->tok;
    int texlen = L->len;

    /* Quake's plane: the three points are clockwise seen from outside, so this
       cross product points out of the solid.
       Quake의 평면입니다. 세 점은 바깥에서 볼 때 시계 방향이므로 이 외적은 고체의 바깥을
       향합니다. */
    v3 nm = v3cross(v3sub(mp[0], mp[1]), v3sub(mp[2], mp[1]));
    float nlen = v3len(nm);

    float ua[3], va[3], uoff = 0, voff = 0, rot = 0, us = 1, vs = 1;
    int valve;
    {
        Lex save = *L;
        if (!lex_next(L)) return 0;
        valve = (!L->quoted && txt_is(L->tok, L->len, "["));
        if (!valve) *L = save;   /* put it back: Standard starts with a number */
    }

    if (valve) {
        /* `[ ux uy uz uoff ] [ vx vy vz voff ] rot xs ys` */
        if (!lex_num(L, &ua[0]) || !lex_num(L, &ua[1]) || !lex_num(L, &ua[2]) ||
            !lex_num(L, &uoff) || !lex_want(L, "]")) return 0;
        if (!lex_want(L, "[")) return 0;
        if (!lex_num(L, &va[0]) || !lex_num(L, &va[1]) || !lex_num(L, &va[2]) ||
            !lex_num(L, &voff) || !lex_want(L, "]")) return 0;
        if (!lex_num(L, &rot) || !lex_num(L, &us) || !lex_num(L, &vs)) return 0;
        /* `rot` is read and discarded. In Valve 220 the axes already carry the
           rotation, and keeping the angle beside them invites applying it a
           second time -- which is a texture at twice the angle the mapper set,
           on some faces and not others.
           `rot`은 읽고 버립니다. Valve 220에서는 축이 이미 회전을 담고 있으며, 각도를 그
           곁에 보관하면 두 번째로 적용하도록 부추기게 됩니다. 그 결과는 제작자가 설정한 각의
           두 배로 돌아간 텍스처이며, 그것도 일부 면에서만 그렇습니다. */
    } else {
        /* `uoff voff rot xs ys` -- axes derived from the plane. */
        if (!lex_num(L, &uoff) || !lex_num(L, &voff) || !lex_num(L, &rot) ||
            !lex_num(L, &us) || !lex_num(L, &vs)) return 0;
        if (nlen <= 0.0f) return 0;
        std_axes(v3scale(nm, 1.0f / nlen), rot, ua, va);
    }

    /* Quake 2 and Half-Life append contents, flags and value. Skipped rather
       than rejected: they describe surface properties this engine has no
       concept of, and refusing the file over three integers nothing reads would
       turn "a format we do not use" into "a map that will not load".
       Quake 2와 Half-Life는 contents, flags, value를 덧붙입니다. 거부하지 않고 건너뜁니다.
       이 엔진에 개념이 없는 표면 속성을 기술하는 값들이며, 아무도 읽지 않는 정수 셋 때문에
       파일을 거부하는 것은 "쓰지 않는 형식"을 "열리지 않는 맵"으로 바꾸는 일입니다. */
    for (;;) {
        Lex save = *L;
        float junk;
        if (!lex_next(L)) { *L = save; break; }
        if (!parse_float(L->tok, L->len, &junk)) { *L = save; break; }
    }

    /* A degenerate plane -- three collinear points -- bounds nothing, so
       dropping it loses nothing. If the brush needed it to close, the closure
       check in ::brush_face_poly reports that; if it did not, the face was
       redundant and its absence is correct.
       축퇴된 평면은(세 점이 한 직선 위) 아무것도 한정하지 않으므로 버려도 잃는 것이
       없습니다. 브러시가 닫히기 위해 그것을 필요로 했다면 ::brush_face_poly의 닫힘 검사가
       보고하고, 필요로 하지 않았다면 그 면은 불필요했으므로 없는 것이 옳습니다. */
    if (nlen <= 0.0f) return 1;
    if (!store) return 1;

    if (m->n_faces >= BR_MAX_TOTAL_FACES) { DIAG(DIAG_BRUSH_CAP); return 1; }
    BrushFace *f = &m->faces[m->n_faces++];

    f->normal = map_dir(nm.x / nlen, nm.y / nlen, nm.z / nlen);
    f->dist   = v3dot(f->normal, map_pos(mp[0].x, mp[0].y, mp[0].z));

    f->uaxis = map_dir(ua[0], ua[1], ua[2]);
    f->vaxis = map_dir(va[0], va[1], va[2]);
    f->uoff  = uoff;
    f->voff  = voff;

    /* A scale of 0 would divide by zero on every vertex. 1 is what every other
       reading of "no scale" means, so a broken face draws at the default
       instead of producing infinities that propagate into the bounding box.
       배율 0은 모든 정점에서 0으로 나누게 됩니다. "배율 없음"에 대한 다른 모든 해석이 1이므로,
       망가진 면은 무한대를 만들어 바운딩 박스까지 오염시키는 대신 기본값으로 그려집니다. */
    f->uscale = (us != 0.0f) ? us : 1.0f;
    f->vscale = (vs != 0.0f) ? vs : 1.0f;

    if (!copy_fits(f->tex, BR_TEX, tex, texlen)) DIAG(DIAG_BRUSH_CAP);
    return 1;
}

/* --------------------------------------------------------- brush assembly */

static int parse_brush(Lex *L, BrushMap *m) {
    int store = (m->n_brushes < BR_MAX_BRUSHES);
    if (!store) DIAG(DIAG_BRUSH_CAP);

    int first = m->n_faces;
    int count = 0;

    for (;;) {
        if (!lex_next(L)) return 0;                       /* text ran out */
        if (!L->quoted && txt_is(L->tok, L->len, "}")) break;

        /* The only two things a brush body may hold. The opening bracket is
           consumed here rather than pushed back, which is why ::parse_face
           starts at the first point rather than at the bracket.
           브러시 본문이 담을 수 있는 것은 이 둘뿐입니다. 여는 괄호는 되돌려 놓지 않고
           이곳에서 소비하며, 그것이 ::parse_face가 괄호가 아니라 첫 점에서 시작하는
           이유입니다. */
        if (L->quoted || !txt_is(L->tok, L->len, "(")) return 0;

        int before = m->n_faces;
        if (!parse_face(L, m, store)) return 0;
        count += m->n_faces - before;

        /* The per-brush cap is separate from the pool: a brush of more than
           BR_MAX_FACES planes would overrun the scratch buffers
           ::brush_face_poly clips in, which BR_MAX_POLY is sized against.
           브러시당 상한은 풀과 별개입니다. BR_MAX_FACES를 넘는 평면을 가진 브러시는
           ::brush_face_poly가 절단에 쓰는 임시 버퍼를 넘어서게 되며, BR_MAX_POLY는 그
           크기에 맞춰져 있습니다. */
        if (count > BR_MAX_FACES) {
            DIAG(DIAG_BRUSH_CAP);
            m->n_faces = before;
            count = m->n_faces - first;
        }
    }

    if (!store || count <= 0) return 1;

    Brush *b = &m->brushes[m->n_brushes++];
    b->first_face = (short)first;
    b->n_faces    = (short)count;
    /* Invalid until ::finish_bounds computes it, which is the state brush.h
       documents as "no faces bounded this".
       ::finish_bounds가 계산하기 전까지는 유효하지 않으며, 이는 brush.h가 "이것을 둘러싼
       면이 없다"로 기록한 상태입니다. */
    b->min = v3f( 1.0f,  1.0f,  1.0f);
    b->max = v3f(-1.0f, -1.0f, -1.0f);
    return 1;
}

static int parse_entity(Lex *L, BrushMap *m) {
    int store = (m->n_ents < BR_MAX_ENTS);
    if (!store) DIAG(DIAG_MAPENT_CAP);

    BrushEnt *e = store ? &m->ents[m->n_ents] : 0;
    if (e) {
        e->n_keys = 0;
        e->first_brush = (short)m->n_brushes;
        e->n_brushes = 0;
    }

    for (;;) {
        if (!lex_next(L)) return 0;
        if (!L->quoted && txt_is(L->tok, L->len, "}")) break;

        if (!L->quoted && txt_is(L->tok, L->len, "{")) {
            if (!parse_brush(L, m)) return 0;
            continue;
        }

        /* Anything else must be a quoted key followed by a quoted value.
           그 밖의 것은 따옴표로 감싼 키와 그 뒤의 따옴표로 감싼 값이어야 합니다. */
        if (!L->quoted) return 0;
        const char *k = L->tok;
        int klen = L->len;
        if (!lex_next(L) || !L->quoted) return 0;

        if (!e) continue;
        if (e->n_keys >= BR_MAX_KEYS) { DIAG(DIAG_MAPENT_CAP); continue; }

        int i = e->n_keys++;
        if (!copy_fits(e->keys[i], BR_KEY, k, klen))          DIAG(DIAG_MAPENT_CAP);
        if (!copy_fits(e->vals[i], BR_VAL, L->tok, L->len))   DIAG(DIAG_MAPENT_CAP);
    }

    if (e) {
        e->n_brushes = (short)(m->n_brushes - e->first_brush);
        m->n_ents++;
    }
    return 1;
}

/* ------------------------------------------------------ polygons from planes */

/* Clips a polygon to the half-space `dot(n,p) <= d`, the side a brush's solid
   is on. Sutherland-Hodgman: walk the edges, keep the inside vertices and add
   one where an edge crosses. */
static int clip(const v3 *in, int n, v3 nrm, float d, v3 *out, int cap) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        v3 a = in[i], b = in[(i + 1) % n];
        float da = v3dot(nrm, a) - d, db = v3dot(nrm, b) - d;
        int ina = (da <= BRUSH_EPSILON), inb = (db <= BRUSH_EPSILON);

        if (ina && m < cap) out[m++] = a;

        if (ina != inb) {
            float den = da - db;
            /* Guarded and clamped. The two ends straddle the epsilon, so `den`
               is normally well away from zero -- but two vertices that both sit
               within float noise of the plane can straddle it by nothing at
               all, and an unclamped t would place the new vertex somewhere off
               the edge entirely.
               보호하고 제한합니다. 양 끝이 엡실론을 사이에 두므로 `den`은 보통 0에서 충분히
               떨어져 있지만, 둘 다 평면으로부터 float 잡음 이내에 있는 정점 쌍은 아무 간격도
               없이 그것을 사이에 둘 수 있습니다. 제한하지 않은 t는 새 정점을 모서리에서
               완전히 벗어난 곳에 놓게 됩니다. */
            if (den > 1e-12f || den < -1e-12f) {
                float t = clampf(da / den, 0.0f, 1.0f);
                if (m < cap) out[m++] = v3add(a, v3scale(v3sub(b, a), t));
            }
        }
    }
    return m;
}

/* The quad covering a face's whole plane, wound counter-clockwise about the
   normal so the polygon that survives clipping already faces the right way. */
static int start_quad(const BrushFace *f, v3 *out) {
    v3 n = f->normal;
    float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);

    /* The axis LEAST aligned with the normal, so the cross product below is as
       far from degenerate as the geometry allows. Picking a fixed axis would
       give a zero-length u on every face parallel to it.
       법선과 가장 덜 정렬된 축입니다. 아래의 외적이 기하학이 허용하는 한 축퇴에서 멀어지게
       합니다. 고정된 축을 고르면 그 축과 평행한 모든 면에서 u의 길이가 0이 됩니다. */
    v3 seed = (ax <= ay && ax <= az) ? v3f(1, 0, 0)
            : (ay <= az)             ? v3f(0, 1, 0)
                                     : v3f(0, 0, 1);

    v3 u = v3norm(v3cross(seed, n));
    v3 v = v3cross(n, u);          /* unit already: n and u are unit and perpendicular */
    v3 c = v3scale(n, f->dist);    /* the plane's closest point to the origin */

    /* u cross v is n, so this order is counter-clockwise seen from the outside
       -- the winding GL's front-face test wants, arrived at by construction
       rather than by flipping until it looked right.
       u와 v의 외적이 n이므로 이 순서는 바깥에서 볼 때 반시계 방향입니다. GL의 전면 판정이
       원하는 감김 방향이며, 맞아 보일 때까지 뒤집어서가 아니라 구성으로 얻었습니다. */
    out[0] = v3add(c, v3add(v3scale(u, -QUAD_R), v3scale(v, -QUAD_R)));
    out[1] = v3add(c, v3add(v3scale(u,  QUAD_R), v3scale(v, -QUAD_R)));
    out[2] = v3add(c, v3add(v3scale(u,  QUAD_R), v3scale(v,  QUAD_R)));
    out[3] = v3add(c, v3add(v3scale(u, -QUAD_R), v3scale(v,  QUAD_R)));
    return 4;
}

int brush_face_poly(const BrushMap *m, int brush, int face, v3 *out, int max) {
    if (!m || !out || max < 3) return 0;
    if (brush < 0 || brush >= m->n_brushes) return 0;

    const Brush *b = &m->brushes[brush];
    if (face < 0 || face >= b->n_faces) return 0;

    v3 buf[2][BR_MAX_POLY];
    int cur = 0;
    int n = start_quad(&m->faces[b->first_face + face], buf[cur]);

    for (int j = 0; j < b->n_faces && n >= 3; j++) {
        if (j == face) continue;
        const BrushFace *o = &m->faces[b->first_face + j];
        n = clip(buf[cur], n, o->normal, o->dist, buf[cur ^ 1], BR_MAX_POLY);
        cur ^= 1;
    }
    if (n < 3) return 0;

    /* Anything still out here was never bounded: the other planes did not close
       around this one. Reported and dropped, because drawing a polygon a
       kilometre across fills the screen with one surface and gives no hint that
       a brush was left open.
       여기까지 남아 바깥에 있는 것은 한정된 적이 없습니다. 다른 평면들이 이 평면을 감싸 닫지
       못한 것입니다. 보고하고 버립니다. 1킬로미터짜리 폴리곤을 그리면 화면이 하나의 면으로
       가득 차며, 브러시가 열려 있었다는 단서를 전혀 주지 않기 때문입니다. */
    for (int i = 0; i < n; i++) {
        v3 p = buf[cur][i];
        if (fabsf(p.x) > WORLD_R || fabsf(p.y) > WORLD_R || fabsf(p.z) > WORLD_R) {
            DIAG(DIAG_BRUSH_OPEN);
            return 0;
        }
    }

    /* Clipping through a corner produces a vertex twice. Dropped here rather
       than left to the renderer, because a repeated vertex is a zero-area
       triangle -- harmless to draw and not harmless to a trace, and this is the
       one place both get their geometry from.
       모서리를 지나는 절단은 같은 정점을 두 번 만듭니다. 렌더러에 맡기지 않고 이곳에서
       제거합니다. 반복된 정점은 넓이가 0인 삼각형이며, 그리기에는 무해하지만 판정에는
       무해하지 않고, 둘 모두가 지오메트리를 얻는 곳이 이곳 하나이기 때문입니다. */
    int w = 0;
    for (int i = 0; i < n && w < max; i++) {
        v3 p = buf[cur][i];
        if (w > 0 && v3len(v3sub(p, out[w - 1])) < BRUSH_EPSILON) continue;
        out[w++] = p;
    }
    if (w > 2 && v3len(v3sub(out[w - 1], out[0])) < BRUSH_EPSILON) w--;
    return (w >= 3) ? w : 0;
}

/* ------------------------------------------------------------------ bounds */

static void finish_bounds(BrushMap *m) {
    for (int i = 0; i < m->n_brushes; i++) {
        Brush *b = &m->brushes[i];
        v3 lo = v3f( 1e30f,  1e30f,  1e30f);
        v3 hi = v3f(-1e30f, -1e30f, -1e30f);
        int any = 0;

        for (int f = 0; f < b->n_faces; f++) {
            v3 poly[BR_MAX_POLY];
            int n = brush_face_poly(m, i, f, poly, BR_MAX_POLY);
            for (int k = 0; k < n; k++) {
                if (poly[k].x < lo.x) lo.x = poly[k].x;
                if (poly[k].y < lo.y) lo.y = poly[k].y;
                if (poly[k].z < lo.z) lo.z = poly[k].z;
                if (poly[k].x > hi.x) hi.x = poly[k].x;
                if (poly[k].y > hi.y) hi.y = poly[k].y;
                if (poly[k].z > hi.z) hi.z = poly[k].z;
                any = 1;
            }
        }

        /* Left invalid when nothing bounded the brush, which is the state
           brush.h says zeroed memory and an open brush share.
           브러시를 한정한 것이 아무것도 없으면 유효하지 않은 상태로 둡니다. brush.h가 0으로
           초기화된 메모리와 열린 브러시가 공유한다고 적은 그 상태입니다. */
        if (any) { b->min = lo; b->max = hi; }
    }
}

/* -------------------------------------------------------------- public API */

int brush_parse(const char *text, int len, BrushMap *out) {
    if (!out) return 0;
    out->n_faces = out->n_brushes = out->n_ents = 0;
    if (!text) return 0;

    Lex L;
    lex_init(&L, text, len);

    for (;;) {
        if (!lex_next(&L)) break;                 /* clean end of file */
        if (!L.quoted && txt_is(L.tok, L.len, "{")) {
            if (parse_entity(&L, out)) continue;
        }
        /* ALL OR NOTHING. A syntax error clears everything rather than keeping
           what parsed before it, because half a level is worse than no level:
           no level is a load failure somebody sees immediately, and half a
           level opens, looks almost right, and is missing a wall somewhere the
           player will eventually walk through. Nothing writes malformed .map
           files -- TrenchBroom certainly does not -- so the case this handles
           is a hand edit, and a hand edit is exactly when the author is there
           to be told.
           전부 아니면 전무입니다. 구문 오류는 그 앞까지 파싱된 것을 남기지 않고 전부
           비웁니다. 절반짜리 레벨이 레벨 없음보다 나쁘기 때문입니다. 레벨이 없으면 누군가
           즉시 보게 되는 로드 실패이지만, 절반짜리 레벨은 열리고 거의 맞아 보이며 플레이어가
           언젠가 통과하게 될 어딘가의 벽 하나가 없습니다. 잘못된 형식의 .map을 쓰는 것은
           아무것도 없으며 TrenchBroom은 분명히 아니므로, 이 경우가 다루는 것은 손으로 한
           수정입니다. 그리고 손으로 수정하는 때야말로 제작자가 그 자리에 있어 들을 수 있는
           때입니다. */
        out->n_faces = out->n_brushes = out->n_ents = 0;
        return 0;
    }

    finish_bounds(out);
    return out->n_ents;
}

void brush_face_uv(const BrushFace *f, v3 p, float tex_w, float tex_h,
                   float *u, float *v) {
    if (!f || !u || !v) return;

    /* Back into map units: the axes and scales are what the file authored, and
       the file authored them against map coordinates.
       맵 단위로 되돌립니다. 축과 배율은 파일이 제작한 값이고, 파일은 그것을 맵 좌표를
       기준으로 제작했습니다. */
    float mu = v3dot(p, f->uaxis) / BRUSH_UNIT;
    float mv = v3dot(p, f->vaxis) / BRUSH_UNIT;

    float w = (tex_w > 0.0f) ? tex_w : 1.0f;
    float h = (tex_h > 0.0f) ? tex_h : 1.0f;

    *u = (mu / f->uscale + f->uoff) / w;
    *v = (mv / f->vscale + f->voff) / h;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

const char *brush_ent_value(const BrushEnt *e, const char *key) {
    if (!e || !key) return 0;
    for (int i = 0; i < e->n_keys; i++)
        if (str_eq(e->keys[i], key)) return e->vals[i];
    return 0;
}

int brush_ent_point(const BrushEnt *e, const char *key, v3 *out) {
    const char *s = brush_ent_value(e, key);
    if (!s || !out) return 0;

    /* The same lexer, over the value text. A second number reader would be a
       second set of rules about what `1e-06` means.
       같은 렉서를 값 텍스트에 대해 돌립니다. 두 번째 숫자 판독기는 `1e-06`이 무엇을
       뜻하는가에 대한 두 번째 규칙 집합이 됩니다. */
    Lex L;
    float x, y, z;
    lex_init(&L, s, -1);
    if (!lex_num(&L, &x) || !lex_num(&L, &y) || !lex_num(&L, &z)) return 0;

    *out = map_pos(x, y, z);
    return 1;
}

int brush_ent_triple(const BrushEnt *e, const char *key, float out[3]) {
    const char *s = brush_ent_value(e, key);
    if (!s || !out) return 0;

    Lex L;
    lex_init(&L, s, -1);
    for (int i = 0; i < 3; i++)
        if (!lex_num(&L, &out[i])) return 0;
    return 1;
}

float brush_ent_num(const BrushEnt *e, const char *key, float def) {
    const char *s = brush_ent_value(e, key);
    if (!s) return def;

    Lex L;
    float v;
    lex_init(&L, s, -1);
    return lex_num(&L, &v) ? v : def;
}

/* ------------------------------------------------------------ collision */

/**
 * How far a box reaches past its reference point, along one plane's inward
 * direction.
 *
 * ENGLISH
 * -------
 * THE WHOLE OF THE BOX HANDLING IS THIS FUNCTION. Sweeping a box against a
 * plane is the same problem as sweeping a point against that plane moved
 * outward by however far the box sticks out toward it -- and the part that
 * sticks out furthest is one corner, the one whose components are chosen by the
 * sign of the normal.
 *
 * `mins` and `maxs` are kept separate rather than reduced to a half-extent,
 * because the box this project sweeps is NOT centred on its reference point: the
 * point is the player's feet, so mins.y is 0 and maxs.y is their height. A
 * symmetric half-extent would put the reference in the navel and every caller
 * would be adjusting for it.
 *
 * 한국어
 * ------
 * 상자 처리의 전부가 이 함수입니다. 상자를 평면에 스윕하는 것은, 그 평면을 상자가 그쪽으로
 * 튀어나온 만큼 바깥으로 옮긴 뒤 점을 스윕하는 것과 같은 문제입니다. 그리고 가장 멀리
 * 튀어나온 부분은 모서리 하나이며, 그 성분은 법선의 부호가 고릅니다.
 *
 * `mins`와 `maxs`를 절반 크기 하나로 줄이지 않고 따로 두는 이유는, 이 프로젝트가 스윕하는
 * 상자가 기준점을 *중심으로 하지 않기* 때문입니다. 기준점은 플레이어의 발이므로 mins.y는
 * 0이고 maxs.y는 키입니다. 대칭적인 절반 크기는 기준점을 배꼽에 두게 되고, 모든 호출자가
 * 그것을 보정하게 됩니다.
 */
static float box_reach(v3 n, v3 mins, v3 maxs) {
    /* The corner furthest along -n: take mins where the normal is positive and
       maxs where it is negative, then measure it against the normal. The result
       is negative or zero, and subtracting it from the plane distance moves the
       plane outward.
       -n 방향으로 가장 먼 모서리입니다. 법선이 양수인 축에서는 mins를, 음수인 축에서는
       maxs를 취한 뒤 법선에 대고 잽니다. 결과는 0 이하이며, 평면 거리에서 그것을 빼면
       평면이 바깥으로 밀려납니다. */
    v3 corner = v3f(n.x > 0.0f ? mins.x : maxs.x,
                    n.y > 0.0f ? mins.y : maxs.y,
                    n.z > 0.0f ? mins.z : maxs.z);
    return v3dot(corner, n);
}

/**
 * Sweeps the box against one brush, narrowing the result if it hits sooner.
 *
 * ENGLISH
 * -------
 * A brush is the intersection of its half-spaces, so the sweep is inside it
 * over whatever span of t is inside ALL of them. Walking the planes and keeping
 * the latest entry and the earliest exit finds that span in one pass; if the
 * entry comes before the exit, the box is inside the brush for that stretch and
 * the entry is the moment of contact.
 *
 * This is Quake 3's CM_TraceThroughBrush and it is worth saying why it is not
 * something else. The obvious alternative -- step along the ray and test each
 * point -- is what ::level_trace does, and it costs more the further you go and
 * misses anything thinner than its step. Solving per plane costs the same at
 * any distance and cannot miss, because contact is found analytically rather
 * than sampled.
 *
 * 한국어
 * ------
 * 브러시는 자기 반공간들의 교집합이므로, 스윕은 *모든* 반공간 안에 있는 t 구간 동안 브러시
 * 안에 있습니다. 평면들을 훑으며 가장 늦은 진입과 가장 이른 이탈을 유지하면 한 번의 순회로 그
 * 구간을 찾습니다. 진입이 이탈보다 앞서면 상자는 그 구간 동안 브러시 안에 있고, 진입이 곧
 * 접촉의 순간입니다.
 *
 * Quake 3의 CM_TraceThroughBrush이며, 왜 다른 것이 아닌지 적어 둘 값어치가 있습니다. 뻔한
 * 대안(광선을 따라 걸으며 각 점을 검사하는 것)은 ::level_trace가 하는 일이고, 멀리 갈수록
 * 비싸지며 자기 간격보다 얇은 것을 놓칩니다. 평면마다 푸는 방식은 거리와 무관하게 같은 비용이며
 * 놓칠 수 없습니다. 접촉을 표본으로 얻는 대신 해석적으로 구하기 때문입니다.
 */
static void trace_brush(const BrushMap *m, const Brush *b, int index,
                        v3 start, v3 end, v3 mins, v3 maxs, BrushTrace *out) {
    if (b->n_faces < 4) return;

    float enter = -1.0f, leave = 1.0f;
    v3    enter_n = v3f(0, 0, 0);
    int   started_out = 0;

    for (int i = 0; i < b->n_faces; i++) {
        const BrushFace *f = &m->faces[b->first_face + i];

        /* The plane, pushed out by the box's reach toward it. From here on this
           is a point sweep.
           상자가 그쪽으로 뻗은 만큼 밀어낸 평면입니다. 이 지점부터는 점 스윕입니다. */
        float dist = f->dist - box_reach(f->normal, mins, maxs);
        float d0 = v3dot(f->normal, start) - dist;
        float d1 = v3dot(f->normal, end)   - dist;

        if (d0 > 0.0f) started_out = 1;

        /* Outside this plane at both ends: the sweep is outside the brush for
           its whole length, and no other plane can bring it back in.
           양 끝에서 이 평면 바깥입니다. 스윕은 전체 길이에 걸쳐 브러시 바깥이며, 다른 어떤
           평면도 그것을 안으로 되돌릴 수 없습니다. */
        if (d0 > 0.0f && d1 > 0.0f) return;

        /* Inside this plane at both ends: it never constrains the span. */
        if (d0 <= 0.0f && d1 <= 0.0f) continue;

        if (d0 > d1) {
            /* Crossing inward. The skin is taken off the entry so the box stops
               short of the surface rather than on it -- see BRUSH_SKIN.
               안쪽으로 넘어갑니다. 상자가 표면 위가 아니라 그 앞에서 멈추도록 진입에서
               스킨을 뺍니다. BRUSH_SKIN을 참조하십시오. */
            float f0 = (d0 - BRUSH_SKIN) / (d0 - d1);
            if (f0 > enter) { enter = f0; enter_n = f->normal; }
        } else {
            /* Crossing outward. */
            float f1 = (d0 + BRUSH_SKIN) / (d0 - d1);
            if (f1 < leave) leave = f1;
        }
    }

    /* Never outside any plane: the box began embedded in this brush. Reported
       and not resolved -- brush.h says why.
       어떤 평면 바깥에도 있지 않았습니다. 상자가 이 브러시에 박힌 채 시작했습니다. 보고할 뿐
       해소하지 않으며, 이유는 brush.h에 있습니다. */
    if (!started_out) {
        out->start_solid = 1;
        out->t = 0.0f;
        out->end = start;
        out->hit = 1;
        out->brush = index;
        return;
    }

    if (enter >= leave) return;          /* in and out again: never overlapped */

    /* -1 means no plane was ever crossed inward, so there is no contact. A
       NEGATIVE entry is a different thing and must not be treated as the same
       one: it is a box already within ::BRUSH_SKIN of the surface, where
       `(d0 - BRUSH_SKIN)` comes out below zero. Rejecting those was a box
       resting on a floor falling through it -- the trace that put it there left
       it a skin above, and any drift below that made the next downward trace
       report open air.
       Clamped to 0 instead, so "already touching" is contact at once. Quake 3
       draws the same distinction for the same reason.
       -1은 어떤 평면도 안쪽으로 넘은 적이 없다는 뜻이므로 접촉이 없습니다. *음수* 진입은
       다른 것이며 같은 것으로 취급해서는 안 됩니다. 상자가 이미 표면으로부터
       ::BRUSH_SKIN 이내에 있어 `(d0 - BRUSH_SKIN)`이 0 아래로 나오는 경우입니다. 그것을
       기각하는 것은 바닥에 얹힌 상자가 바닥을 통과해 떨어지는 일이었습니다. 그것을 그곳에
       놓은 트레이스가 스킨만큼 위에 남겨 두었고, 그 아래로 조금이라도 밀리면 다음 하강
       트레이스가 빈 공간이라고 보고했습니다. 대신 0으로 제한하여 "이미 닿아 있음"이 즉시
       접촉이 되게 합니다. Quake 3도 같은 이유로 같은 구분을 합니다. */
    if (enter <= -1.0f) return;
    float t = (enter < 0.0f) ? 0.0f : enter;

    if (t >= out->t) return;             /* something nearer was already found */

    out->t      = t;
    out->normal = enter_n;
    out->hit    = 1;
    out->brush  = index;
}

void brush_trace(const BrushMap *m, int first, int count,
                 v3 start, v3 end, v3 mins, v3 maxs, BrushTrace *out) {
    if (!out) return;

    out->t = 1.0f;
    out->end = end;
    out->normal = v3f(0, 0, 0);
    out->hit = 0;
    out->start_solid = 0;
    out->brush = -1;
    if (!m) return;

    if (first < 0) first = 0;
    if (first + count > m->n_brushes) count = m->n_brushes - first;

    /* The box that the whole sweep fits inside, so a brush nowhere near it is
       rejected in six compares instead of a plane walk. The same argument
       ::Sector's cached bounding box makes, and the same place in the loop.
       스윕 전체가 들어가는 상자입니다. 근처에도 없는 브러시를 평면 순회 대신 비교 6번으로
       기각합니다. ::Sector의 캐시된 바운딩 박스와 같은 논거이며 루프에서의 위치도 같습니다.

       No grid yet. level.h's LVL_GRID_MIN_SECTORS records that a grid cost more
       than it saved below about sixteen sectors, and the shipped map has eleven
       brushes; building one now would be adding the structure whose own comment
       says not to.
       아직 격자가 없습니다. level.h의 LVL_GRID_MIN_SECTORS는 섹터 열여섯 개 아래에서는 격자가
       아끼는 것보다 더 들었다고 기록하고 있고, 출하되는 맵은 브러시 열한 개입니다. 지금 만드는
       것은 스스로 그러지 말라고 적어 둔 구조를 더하는 일입니다. */
    v3 lo = v3f(start.x < end.x ? start.x : end.x,
                start.y < end.y ? start.y : end.y,
                start.z < end.z ? start.z : end.z);
    v3 hi = v3f(start.x > end.x ? start.x : end.x,
                start.y > end.y ? start.y : end.y,
                start.z > end.z ? start.z : end.z);
    lo = v3add(lo, mins); hi = v3add(hi, maxs);

    for (int i = first; i < first + count; i++) {
        const Brush *b = &m->brushes[i];

        /* An invalid box means no face bounded the brush -- brush.h's "no faces
           bounded this". Skipped rather than trusted: its planes are whatever
           an unclosed brush left behind, and colliding against them would put
           an invisible wall wherever the author's mistake happened to point.
           유효하지 않은 박스는 어떤 면도 그 브러시를 한정하지 못했다는 뜻입니다. brush.h의
           "이것을 둘러싼 면이 없다"입니다. 신뢰하지 않고 건너뜁니다. 그 평면들은 닫히지 않은
           브러시가 남긴 무엇이며, 그것과 충돌하면 제작자의 실수가 가리키는 아무 곳에나 보이지
           않는 벽이 생깁니다. */
        if (b->min.x > b->max.x) continue;

        if (b->max.x < lo.x || b->min.x > hi.x) continue;
        if (b->max.y < lo.y || b->min.y > hi.y) continue;
        if (b->max.z < lo.z || b->min.z > hi.z) continue;

        trace_brush(m, b, i, start, end, mins, maxs, out);
        if (out->start_solid) break;
    }

    /* The position the caller should use. Derived from t once, here, rather
       than left to each caller to recompute -- brush.h's note on `end` is about
       exactly the recomputation this removes.
       호출자가 써야 할 위치입니다. 각 호출자가 다시 계산하도록 남기지 않고 이곳에서 t로부터 한
       번 유도합니다. brush.h의 `end`에 대한 설명이 이곳에서 제거하는 바로 그 재계산에 관한
       것입니다. */
    if (!out->start_solid)
        out->end = v3add(start, v3scale(v3sub(end, start), out->t));
}

/* --------------------------------------------------------------- moving */

/**
 * How many planes one move remembers while resolving a corner.
 *
 * One more than ::BRUSH_MAX_BUMPS, because the floor is usually the first thing
 * recorded and the bumps that matter are the walls after it.
 * 한 번의 이동이 모서리를 해소하는 동안 기억하는 평면의 수입니다. ::BRUSH_MAX_BUMPS보다
 * 하나 많은데, 보통 바닥이 가장 먼저 기록되고 문제가 되는 충돌은 그 뒤의 벽들이기 때문입니다.
 */
#define MAX_CLIP_PLANES (BRUSH_MAX_BUMPS + 1)

/**
 * Pushes a velocity out of a plane it is heading into.
 *
 * ENGLISH
 * -------
 * The component along the normal is removed, which leaves exactly the part of
 * the motion the surface permits. The OVERCLIP factor removes slightly more
 * than that, so the result leans a hair away from the surface rather than
 * exactly along it -- a velocity left perfectly parallel re-contacts the same
 * plane on the next bump and spends the move's whole budget resolving one wall.
 *
 * 한국어
 * ------
 * 속도가 향하는 평면에서 그것을 밀어냅니다.
 *
 * 법선 방향 성분을 제거하며, 남는 것은 정확히 그 표면이 허용하는 운동 성분입니다. OVERCLIP
 * 계수는 그보다 아주 조금 더 제거하여, 결과가 표면과 정확히 평행하지 않고 표면에서 미세하게
 * 벗어나는 쪽으로 기울게 합니다. 완벽히 평행하게 남은 속도는 다음 충돌에서 같은 평면에 다시
 * 닿고, 벽 하나를 해소하는 데 이동의 예산 전부를 씁니다.
 */
#define OVERCLIP 1.001f

static v3 clip_velocity(v3 in, v3 normal) {
    float backoff = v3dot(in, normal);
    backoff = (backoff < 0.0f) ? backoff * OVERCLIP : backoff / OVERCLIP;
    return v3sub(in, v3scale(normal, backoff));
}

/* What is underfoot, and whether it can be stood on. A short probe rather than
   a remembered flag: brush.h's warning about a persistent `grounded` is the
   whole reason this is a function.
   발밑에 무엇이 있고 그것을 딛고 설 수 있는지입니다. 기억된 플래그가 아니라 짧은 탐침입니다.
   유지되는 `grounded`에 대한 brush.h의 경고가 이것이 함수인 이유 전부입니다. */
#define GROUND_PROBE (BRUSH_SKIN * 4.0f)

static void probe_ground(const BrushMap *m, int first, int count,
                         BrushMove *mv) {
    BrushTrace t;
    v3 down = v3f(mv->pos.x, mv->pos.y - GROUND_PROBE, mv->pos.z);
    brush_trace(m, first, count, mv->pos, down, mv->mins, mv->maxs, &t);

    mv->ground_normal = t.hit ? t.normal : v3f(0, 0, 0);
    mv->grounded = t.hit && t.normal.y >= BRUSH_GROUND_NORMAL;
}

/**
 * The slide itself: move, clip, move again with the time that is left.
 *
 * ENGLISH
 * -------
 * Quake's SV_FlyMove. The velocity is clipped against every plane hit so far
 * rather than only the last, because a box in a corner that is pushed out of
 * one wall is pushed straight into the other; clipping against both, and then
 * along their crease when both still object, is what lets it slide out rather
 * than stick.
 *
 * @return non-zero when the move ended still pushing into something.
 *
 * 한국어
 * ------
 * 미끄러짐 자체입니다. 이동하고, 자르고, 남은 시간으로 다시 이동합니다.
 *
 * Quake의 SV_FlyMove입니다. 속도는 마지막 평면뿐 아니라 지금까지 부딪힌 *모든* 평면에 대해
 * 잘립니다. 모서리에 있는 상자를 한쪽 벽에서 밀어내면 곧장 다른 쪽 벽으로 밀리기 때문입니다.
 * 양쪽 모두에 대해 자르고, 그러고도 양쪽이 거부하면 두 평면이 이루는 능선을 따라 자르는 것이
 * 상자가 끼지 않고 빠져나오게 하는 방법입니다.
 */
static int slide(const BrushMap *m, int first, int count,
                 BrushMove *mv, float dt) {
    v3  planes[MAX_CLIP_PLANES];
    int n_planes = 0;
    float time_left = dt;
    int blocked = 0;

    for (int bump = 0; bump < BRUSH_MAX_BUMPS; bump++) {
        if (time_left <= 0.0f) break;
        if (v3dot(mv->vel, mv->vel) == 0.0f) break;

        v3 end = v3add(mv->pos, v3scale(mv->vel, time_left));

        BrushTrace t;
        brush_trace(m, first, count, mv->pos, end, mv->mins, mv->maxs, &t);

        /* Embedded. brush.h says a trace does not push out, and neither does
           this: the move is abandoned where it stands so that whatever put the
           box inside a wall is still visible when somebody looks.
           박혀 있습니다. brush.h는 트레이스가 밀어내지 않는다고 말하며 이곳도 그렇습니다.
           이동은 그 자리에서 중단되고, 그래야 상자를 벽 안에 넣은 원인이 누군가 들여다볼 때
           여전히 보입니다. */
        if (t.start_solid) return 1;

        mv->pos = t.end;
        if (t.t >= 1.0f) break;              /* the whole move happened */

        time_left -= time_left * t.t;
        blocked = 1;

        if (n_planes >= MAX_CLIP_PLANES) { mv->vel = v3f(0, 0, 0); break; }
        planes[n_planes++] = t.normal;

        /* Find a velocity that no plane objects to. */
        v3 want = mv->vel;
        int settled = 0;
        for (int i = 0; i < n_planes && !settled; i++) {
            if (v3dot(mv->vel, planes[i]) >= 0.0f) continue;   /* already leaving */

            v3 clipped = clip_velocity(mv->vel, planes[i]);

            /* Does any OTHER plane still object to the result? */
            for (int j = 0; j < n_planes; j++) {
                if (j == i) continue;
                if (v3dot(clipped, planes[j]) >= 0.0f) continue;

                clipped = clip_velocity(clipped, planes[j]);
                if (v3dot(clipped, planes[i]) >= 0.0f) continue;

                /* Both planes still object, so the only direction left is
                   along the line where they meet. Two walls in a corner leave
                   exactly one way out, and this is it.
                   두 평면이 모두 거부하므로 남은 방향은 둘이 만나는 선을 따르는 것뿐입니다.
                   모서리의 두 벽이 남기는 출구는 정확히 하나이며 그것이 이것입니다. */
                v3 crease = v3norm(v3cross(planes[i], planes[j]));
                clipped = v3scale(crease, v3dot(crease, mv->vel));

                /* A third plane against the crease means wedged. Stopping is
                   the honest answer; anything else picks a direction the
                   geometry does not offer.
                   능선에 대해 세 번째 평면이 거부하면 끼인 것입니다. 멈추는 것이 정직한
                   답입니다. 그 밖의 무엇이든 기하가 제공하지 않는 방향을 고르는 일입니다. */
                for (int k = 0; k < n_planes; k++) {
                    if (k == i || k == j) continue;
                    if (v3dot(clipped, planes[k]) < 0.0f) {
                        clipped = v3f(0, 0, 0);
                        break;
                    }
                }
                break;
            }

            want = clipped;
            settled = 1;
        }
        mv->vel = want;
        if (v3dot(mv->vel, mv->vel) == 0.0f) break;
    }
    return blocked;
}

void brush_slide_move(const BrushMap *m, int first, int count,
                      BrushMove *mv, float dt) {
    if (!m || !mv) return;

    mv->blocked = 0;
    mv->grounded = 0;
    mv->ground_normal = v3f(0, 0, 0);

    v3 start_pos = mv->pos;
    v3 start_vel = mv->vel;

    /* --- what is underfoot, decided BEFORE anything moves ------------------
       The step is only allowed from ground, and "ground" means where the move
       began. Asking afterwards would let a box that has just left the floor
       step onto the wall it is falling past.
       발밑에 무엇이 있는지를 무엇이 움직이기 전에 판정합니다. 계단 오르기는 지면에서만
       허용되며, "지면"이란 이동이 시작된 곳입니다. 나중에 물으면 방금 바닥을 떠난 상자가
       스쳐 지나가며 떨어지는 벽 위로 올라설 수 있게 됩니다. */
    BrushTrace ground;
    v3 probe = v3f(start_pos.x, start_pos.y - GROUND_PROBE, start_pos.z);
    brush_trace(m, first, count, start_pos, probe, mv->mins, mv->maxs, &ground);
    int may_step = mv->step_height > 0.0f &&
                   ground.hit && ground.normal.y >= BRUSH_GROUND_NORMAL;

    int blocked = slide(m, first, count, mv, dt);

    if (!blocked || !may_step) {
        mv->blocked = blocked;
        probe_ground(m, first, count, mv);
        return;
    }

    /* --- the same move again, from one step up ---------------------------- */
    v3 plain_pos = mv->pos;
    v3 plain_vel = mv->vel;

    BrushTrace up;
    v3 raised = v3f(start_pos.x, start_pos.y + mv->step_height, start_pos.z);
    brush_trace(m, first, count, start_pos, raised, mv->mins, mv->maxs, &up);

    /* No headroom to rise into: there is nothing to try. */
    if (up.start_solid || up.t <= 0.0f) {
        mv->pos = plain_pos;
        mv->vel = plain_vel;
        mv->blocked = 1;
        probe_ground(m, first, count, mv);
        return;
    }

    BrushMove stepped = *mv;
    stepped.pos = up.end;
    stepped.vel = start_vel;
    int step_blocked = slide(m, first, count, &stepped, dt);

    /* Back down onto whatever is under the new position. Without this the box
       finishes the frame hovering by the step height, and a player walking
       along a flat floor would rise a step every time they brushed anything.
       새 위치 아래에 있는 것 위로 다시 내려놓습니다. 이것이 없으면 상자는 계단 높이만큼 떠
       있는 채로 프레임을 마치고, 평평한 바닥을 걷는 플레이어는 무언가를 스칠 때마다 한 칸씩
       올라가게 됩니다. */
    BrushTrace down;
    float drop = stepped.pos.y - start_pos.y + mv->step_height;
    v3 landed = v3f(stepped.pos.x, stepped.pos.y - drop, stepped.pos.z);
    brush_trace(m, first, count, stepped.pos, landed, mv->mins, mv->maxs, &down);
    if (!down.start_solid) {
        stepped.pos = down.end;
        if (down.hit) stepped.vel = clip_velocity(stepped.vel, down.normal);
    }

    /* KEEP WHICHEVER GOT FURTHER, measured horizontally. Height is not the
       question -- a step that rose and went nowhere is worse than staying put,
       and the point of stepping is to keep going.
       수평으로 더 멀리 간 쪽을 채택합니다. 높이는 질문이 아닙니다. 올라가기만 하고 아무 데도
       가지 못한 계단은 제자리에 있는 것보다 나쁘며, 계단을 오르는 목적은 계속 나아가는
       것입니다. */
    float dx = plain_pos.x - start_pos.x, dz = plain_pos.z - start_pos.z;
    float sx = stepped.pos.x - start_pos.x, sz = stepped.pos.z - start_pos.z;

    if (sx * sx + sz * sz > dx * dx + dz * dz) {
        mv->pos = stepped.pos;
        mv->vel = stepped.vel;
        mv->blocked = step_blocked;
    } else {
        mv->pos = plain_pos;
        mv->vel = plain_vel;
        mv->blocked = 1;
    }

    probe_ground(m, first, count, mv);
}

void brush_translate(BrushMap *m, int first, int count, v3 delta) {
    if (!m) return;
    if (first < 0) first = 0;
    if (first + count > m->n_brushes) count = m->n_brushes - first;

    for (int i = first; i < first + count; i++) {
        Brush *b = &m->brushes[i];

        for (int f = 0; f < b->n_faces; f++) {
            BrushFace *fc = &m->faces[b->first_face + f];
            /* The plane, moved. A point p on the old plane satisfies
               dot(n,p) == dist; the same point moved by delta satisfies
               dot(n, p+delta) == dist + dot(n,delta).
               평면을 옮깁니다. 옛 평면 위의 점 p는 dot(n,p) == dist를 만족하고, delta만큼
               옮겨진 같은 점은 dot(n, p+delta) == dist + dot(n,delta)를 만족합니다. */
            fc->dist += v3dot(fc->normal, delta);
        }

        /* Invalid boxes are left alone: an unclosed brush has no box to move
           and giving it one here would invent a volume nothing bounds.
           유효하지 않은 박스는 그대로 둡니다. 닫히지 않은 브러시에는 옮길 박스가 없으며,
           이곳에서 하나를 주는 것은 아무것도 한정하지 않는 부피를 만들어 내는 일입니다. */
        if (b->min.x > b->max.x) continue;
        b->min = v3add(b->min, delta);
        b->max = v3add(b->max, delta);
    }
}

/* ------------------------------------------------------------- geometry */

/**
 * Textures that mark a face as solid but not drawn.
 *
 * ENGLISH
 * -------
 * Quake's set, plus TrenchBroom's placeholder. Kept as a table rather than a
 * chain of comparisons so that adding one is a line of data, and so that
 * ::brush_tex_nodraw and anything that later asks the same question are reading
 * the same list.
 *
 *   __TB_empty  what TrenchBroom gives a face nobody has textured yet. A new
 *               brush is entirely this until the author picks something, and
 *               drawing it would fill the editor's own default state with grey.
 *   clip        solid to the player, invisible. The standard way to smooth a
 *               staircase or fence off a ledge without changing what is drawn.
 *   skip        the face is not drawn but the brush still bounds space.
 *   trigger     the volume of a trigger, which is a shape rather than a wall.
 *
 * 한국어
 * ------
 * 면을 고체이되 그리지 않는 것으로 표시하는 텍스처입니다.
 *
 * Quake의 집합에 TrenchBroom의 자리 표시자를 더한 것입니다. 비교의 연쇄가 아니라 표로 두어,
 * 하나를 더하는 일이 데이터 한 줄이 되게 하고, ::brush_tex_nodraw와 나중에 같은 질문을 하는
 * 무엇이든 같은 목록을 읽게 합니다.
 *
 *   __TB_empty  아직 아무도 텍스처를 입히지 않은 면에 TrenchBroom이 주는 이름입니다. 새
 *               브러시는 제작자가 무언가를 고르기 전까지 전부 이것이며, 그리면 에디터의 기본
 *               상태 자체가 회색으로 가득 찹니다.
 *   clip        플레이어에게는 고체이고 보이지 않습니다. 그려지는 것을 바꾸지 않고 계단을
 *               매끄럽게 하거나 난간을 막는 표준적인 방법입니다.
 *   skip        면은 그려지지 않지만 브러시는 여전히 공간을 한정합니다.
 *   trigger     트리거의 부피이며, 벽이 아니라 형태입니다.
 */
static const char *const NODRAW[] = { "__TB_empty", "clip", "skip", "trigger" };

int brush_tex_nodraw(const char *tex) {
    if (!tex) return 1;
    for (int i = 0; i < (int)(sizeof(NODRAW) / sizeof(NODRAW[0])); i++)
        if (str_eq(tex, NODRAW[i])) return 1;
    return 0;
}

/* Appends a range, merging with the previous one when the texture matches.
   The same shape level.c's push_range has, and for the same reason: a level
   walks its surfaces in author order and neighbours usually share a material,
   so merging as they arrive costs one compare and saves most of the table.
   level.c의 push_range와 같은 형태이며 이유도 같습니다. 레벨은 표면을 제작 순서로 훑고 이웃은
   보통 같은 재질을 공유하므로, 도착하는 대로 병합하면 비교 한 번의 비용으로 표의 대부분을
   아낍니다. */
static void push_range(MdlRange *r, int *n, int max, const char *tex,
                       int first, int count) {
    if (count <= 0) return;

    if (*n > 0 && str_eq(r[*n - 1].mat, tex)) {
        r[*n - 1].count += count;
        return;
    }
    if (*n >= max) {
        /* Merged into the previous run rather than dropped. The surplus draws
           with the wrong texture, which is visible; dropping it would delete
           the wall, which is not.
           버리지 않고 직전 구간에 병합합니다. 초과분은 잘못된 텍스처로 그려지며 그것은 눈에
           보입니다. 버리면 벽이 사라지고 그것은 보이지 않습니다. */
        DIAG(DIAG_MAT_RANGES);
        if (*n > 0) r[*n - 1].count += count;
        return;
    }

    txt_copy(r[*n].mat, (int)sizeof(r[*n].mat), tex, -1);
    r[*n].first = first;
    r[*n].count = count;
    (*n)++;
}

int brush_geometry(MeshBuf *b, const BrushMap *m, int first, int count,
                   MdlRange *ranges, int max_ranges) {
    if (!b || !m) return 0;

    int n_ranges = 0;
    if (first < 0) first = 0;
    if (first + count > m->n_brushes) count = m->n_brushes - first;

    for (int bi = first; bi < first + count; bi++) {
        const Brush *br = &m->brushes[bi];

        for (int fi = 0; fi < br->n_faces; fi++) {
            const BrushFace *f = &m->faces[br->first_face + fi];
            if (brush_tex_nodraw(f->tex)) continue;

            v3 poly[BR_MAX_POLY];
            int n = brush_face_poly(m, bi, fi, poly, BR_MAX_POLY);
            if (n < 3) continue;

            int start = b->count;

            /* Fanned from vertex 0. A brush face is convex by construction --
               it is the intersection of half-spaces -- so a fan is always
               valid here, which is not true of the sector model's floor
               polygons and is why level.c needs an ear-clip and this does not.
               0번 정점에서 부채꼴로 펼칩니다. 브러시 면은 구성상 볼록합니다. 반공간들의
               교집합이기 때문입니다. 따라서 이곳에서는 부채꼴이 언제나 유효하며, 섹터 모델의
               바닥 다각형은 그렇지 않습니다. level.c가 ear-clip을 필요로 하고 이곳이 그렇지
               않은 이유입니다. */
            for (int i = 1; i + 1 < n; i++) {
                v3 tri[3] = { poly[0], poly[i], poly[i + 1] };
                for (int k = 0; k < 3; k++) {
                    float u, v;
                    brush_face_uv(f, tri[k], BRUSH_TEXELS, BRUSH_TEXELS, &u, &v);
                    mb_vtx(b, tri[k], f->normal, u, v);
                }
            }

            if (ranges)
                push_range(ranges, &n_ranges, max_ranges, f->tex,
                           start, b->count - start);
        }
    }
    return n_ranges;
}
