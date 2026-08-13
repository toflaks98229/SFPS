/**
 * @file tex.c
 * @brief Interprets the texture recipe language and rasterises materials at startup.
 *
 * ENGLISH
 * -------
 * The interpreter is a flat op table: each recipe line names an operation and
 * supplies integer arguments, and the ops composite onto a single RGBA
 * staging buffer in order. RGB carries colour and ALPHA carries gloss, which
 * is why nothing here ever writes transparency.
 *
 * A material may resolve to no texture at all. The `proc` op selects a
 * procedural shader (PROC_* in render.h) that computes the surface per pixel
 * from the UV instead, costing four uniforms rather than 256KB and staying
 * sharp at any distance.
 *
 * 한국어
 * ------
 * 인터프리터는 평면적인 명령 테이블입니다. 레시피의 각 줄은 하나의 연산을 지정하고
 * 정수 인자를 제공하며, 이 연산들이 순서대로 하나의 RGBA 준비 버퍼에 합성됩니다.
 * RGB는 색상을, ALPHA는 광택을 담당하므로 이곳에서 투명도를 기록하는 코드는
 * 존재하지 않습니다.
 *
 * 재질이 텍스처를 전혀 갖지 않을 수도 있습니다. `proc` 연산은 절차적 셰이더
 * (render.h의 PROC_*)를 선택하며, 이 셰이더는 UV로부터 픽셀 단위로 표면을 계산하여
 * 256KB 대신 4개의 유니폼만 소모하면서도 어떤 거리에서든 선명하게 유지됩니다.
 */

#include "tex.h"
#include "data.h"
#include "txt.h"
#include "render.h"   /* rd_proc -- a material selects the shader it draws with */
#include "sprite.h"   /* sprite_wall -- imported surfaces, fetched by name */
#include "diag.h"
#include <math.h>

#define SIZE     256
#define MAX_OPS  32

/* ---------------------------------------------------------------- recipes */

/* The material library itself lives in assets/textures.txt -- see data.h for
   how that text reaches this parser in release versus hot-reload builds.

   Every number in the language is an integer with a fixed implied scale (see
   OPS below), so the parser never touches a float: no strtof, no decimal
   point handling, no exponent. */

/* Opcode table. Order defines the opcode values; arity is how many integers
   follow the name. Adding a material costs a line of text; adding a new
   *kind* of material costs one entry here and one case in run_op. */
enum {
    OP_BASE, OP_BRICK, OP_TINT, OP_GRAIN, OP_BEVEL, OP_MORTAR,
    OP_BRUSH, OP_BLOTCH, OP_SCRATCH, OP_SEAM,
    OP_WOOD, OP_CHECK, OP_RIBS, OP_BLUE, OP_GLOSS, OP_PROC, OP_BUMP,
    OP_IMAGE, OP_COUNT
};

static const struct { const char *name; unsigned char arity; } OPS[OP_COUNT] = {
    {"base",    3}, {"brick",   3}, {"tint",    3}, {"grain",   1},
    {"bevel",   3}, {"mortar",  4}, {"brush",   3}, {"blotch",  2},
    {"scratch", 2}, {"seam",    3}, {"wood",    4}, {"check",   2},
    {"ribs",    2}, {"blue",    1}, {"gloss",   1}, {"proc",    2},
    {"bump",    1}, {"image",   1}
};

typedef struct { unsigned char op; short a[4]; } Op;

/* ------------------------------------------------------------------ noise */

unsigned tex_hash(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16; return x;
}

float tex_hashf(unsigned x) { return (tex_hash(x) >> 8) * (1.0f / 16777216.0f); }

/* Value noise: hash the four lattice corners and smoothstep between them. */
static float noise2(float x, float y, unsigned seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);

    unsigned b = seed * 0x9e3779b9u;
    float n00 = tex_hashf(b + ix * 374761393u + iy * 668265263u);
    float n10 = tex_hashf(b + (ix+1) * 374761393u + iy * 668265263u);
    float n01 = tex_hashf(b + ix * 374761393u + (iy+1) * 668265263u);
    float n11 = tex_hashf(b + (ix+1) * 374761393u + (iy+1) * 668265263u);

    float a = n00 + (n10 - n00) * fx;
    float c = n01 + (n11 - n01) * fx;
    return a + (c - a) * fy;
}

/* ----------------------------------------------------------------- parser */

/* Collects the ops of one named recipe. Returns the op count, 0 if absent. */
static int parse_recipe(const char *name, Op *out) {
    const char *p = data_text(DATA_RECIPES);
    int found = 0, n = 0, len;

    for (;;) {
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = (t + len);

        if (txt_is(t, len, "t")) {
            if (found) break;                 /* next recipe: we are done */
            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = (nm + len);
            found = txt_is(nm, len, name);
            continue;
        }

        /* Identify the opcode so its operands can be consumed either way --
           skipping a foreign recipe still has to step over its numbers. */
        int op = -1;
        for (int i = 0; i < OP_COUNT; i++)
            if (txt_is(t, len, OPS[i].name)) { op = i; break; }
        if (op < 0) continue;                 /* unknown word: ignore */

        int a[4] = {0, 0, 0, 0};
        for (int i = 0; i < OPS[op].arity; i++) { int ok; p = txt_read_int(p, &a[i], &ok); }

        if (found && n < MAX_OPS) {
            out[n].op = (unsigned char)op;
            for (int i = 0; i < 4; i++) out[n].a[i] = (short)a[i];
            n++;
        } else if (found) {
            /* Recipe longer than MAX_OPS: the trailing ops never run, so the
               material comes out looking half-finished with no other clue.
               레시피가 MAX_OPS보다 긴 경우, 뒤쪽 연산이 실행되지 않아 재질이 다른
               단서 없이 절반만 완성된 것처럼 보입니다. */
            DIAG(DIAG_TEX_OPS);
        }
    }
    return found ? n : 0;
}

/* ------------------------------------------------------------ interpreter */

/* Per-pixel state threaded through the op list. `edge` and the cell fields
   are set by OP_BRICK and read by the ops that follow it, which is how a
   flat op list expresses "only inside a brick". */
typedef struct {
    float r, g, b;
    /* Gloss rides in the alpha channel. The textures were already RGBA with
       alpha pinned at 255 and unused, so this buys per-pixel specular without
       a second texture, a second sampler or a per-draw uniform -- and lets
       one material vary: checkering is matte where the metal around it
       shines. */
    float gloss;
    int   edge;
    int   lx, ly, bw, bh, m;
    unsigned cell;
} Px;

static void run_op(const Op *o, Px *px, int x, int y, unsigned seed) {
    const short *a = o->a;

    switch (o->op) {
    case OP_BASE:
        px->r = a[0] / 255.0f; px->g = a[1] / 255.0f; px->b = a[2] / 255.0f;
        break;

    case OP_BRICK: {
        int bw = a[0], bh = a[1], m = a[2];
        int row = y / bh;
        int xo = (x + (row & 1) * (bw / 2)) & (SIZE - 1);
        px->bw = bw; px->bh = bh; px->m = m;
        px->lx = xo % bw; px->ly = y % bh;
        px->cell = tex_hash(row * 73856093u + (xo / bw) * 19349663u);
        px->edge = px->lx < m || px->ly < m ||
                   px->lx >= bw - m || px->ly >= bh - m;
        break;
    }

    case OP_TINT: {
        if (px->edge) break;
        float t = (px->cell & 0xffu) / 255.0f;
        px->r += t * a[0] / 255.0f;
        px->g += t * a[1] / 255.0f;
        px->b += t * a[2] / 255.0f;
        break;
    }

    case OP_GRAIN: {
        if (px->edge) break;
        float d = (tex_hashf(x * 374761393u + y * 668265263u + seed) - 0.5f)
                * (a[0] / 100.0f);
        px->r += d; px->g += d; px->b += d;
        break;
    }

    case OP_BEVEL: {
        if (px->edge) break;
        int w = a[0];
        int dl = px->lx - px->m,               dt = px->ly - px->m;
        int dr = px->bw - px->m - 1 - px->lx,  db = px->bh - px->m - 1 - px->ly;
        float k = (dl < w || dt < w) ? a[1] / 100.0f
                : ((dr < w || db < w) ? a[2] / 100.0f : 1.0f);
        px->r *= k; px->g *= k; px->b *= k;
        break;
    }

    case OP_MORTAR: {
        if (!px->edge) break;
        float n = tex_hashf(x * 7919u + y * 104729u + seed) * (a[3] / 255.0f);
        px->r = a[0] / 255.0f + n;
        px->g = a[1] / 255.0f + n;
        px->b = a[2] / 255.0f + n;
        break;
    }

    case OP_BRUSH: {
        float d = (noise2(x * (a[0] / 100.0f), y * (a[1] / 100.0f), seed) - 0.5f)
                * (a[2] / 100.0f);
        px->r += d; px->g += d; px->b += d;
        break;
    }

    case OP_BLOTCH: {
        float f = a[0] / 100.0f;
        float d = (noise2(x * f, y * f, seed) - 0.5f) * (a[1] / 100.0f);
        px->r += d; px->g += d; px->b += d;
        break;
    }

    case OP_SCRATCH: {
        float thr = a[0] / 100.0f;
        float s = noise2(x * 0.9f, y * 0.22f, seed);
        if (s > thr) {
            float d = (s - thr) * (a[1] / 100.0f);
            px->r += d; px->g += d; px->b += d;
        }
        break;
    }

    case OP_SEAM: {
        int p = a[0], sx = x % p, sy = y % p;
        float k = (sx < 2 || sy < 2) ? a[1] / 100.0f
                : ((sx < 4 || sy < 4) ? a[2] / 100.0f : 1.0f);
        px->r *= k; px->g *= k; px->b *= k;
        break;
    }

    /* Sawn timber: bands running along x, warped by a noise field stretched
       the same way. Straight sine bands read as corduroy; the warp is what
       makes them read as grain. */
    case OP_WOOD: {
        float warp  = noise2(x * 0.014f, y * 0.30f, seed) * 16.0f;
        float rings = sinf((y + warp) * 0.55f) * 0.5f + 0.5f;
        rings = rings * rings;                       /* tighter dark lines */
        float fibre = noise2(x * 0.9f, y * 0.05f, seed + 7) * 0.16f - 0.08f;
        float k = 1.0f - rings * (a[3] / 100.0f) + fibre;
        px->r = a[0] / 255.0f * k;
        px->g = a[1] / 255.0f * k;
        px->b = a[2] / 255.0f * k;
        break;
    }

    /* Diamond checkering, as cut into a grip. The raised diamonds catch the
       light and the cuts between them go matte, which is most of why real
       checkering reads at a glance. */
    case OP_CHECK: {
        int s = a[0] > 1 ? a[0] : 2, half = s / 2;
        int dx = (x % s) - half, dy = (y % s) - half;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        int d = dx + dy;
        float k = (d < half) ? 1.0f + a[1] / 200.0f : 1.0f - a[1] / 100.0f;
        px->r *= k; px->g *= k; px->b *= k;
        px->gloss *= (d < half) ? 1.0f : 0.25f;
        break;
    }

    /* Cooling ribs along a barrel: a hard shadow line with a lit crest. */
    case OP_RIBS: {
        int p = a[0] > 1 ? a[0] : 2;
        int r = x % p;
        float k = (r < 2) ? 1.0f - a[1] / 100.0f
                : ((r < 4) ? 1.0f + a[1] / 200.0f : 1.0f);
        px->r *= k; px->g *= k; px->b *= k;
        break;
    }

    /* Bluing: pull toward a very dark blue-black without going flat grey.
       Real blued steel is much darker than people expect, and getting that
       darkness is most of what separates a gun from a prop. */
    case OP_BLUE: {
        float t = a[0] / 100.0f;
        float lum = (px->r + px->g + px->b) * (1.0f / 3.0f);
        px->r += (lum * 0.42f - px->r) * t;
        px->g += (lum * 0.46f - px->g) * t;
        px->b += (lum * 0.62f - px->b) * t;
        break;
    }

    case OP_GLOSS:
        px->gloss = a[0] / 100.0f;
        break;

    /* No per-pixel work: OP_PROC hands the surface to the fragment shader
       instead, and is read by tex_mat rather than by the interpreter. OP_BUMP
       is the same -- a strength the shader uses, not pixels to draw.
       OP_BUMP도 마찬가지입니다. 그릴 픽셀이 아니라 셰이더가 사용하는 강도입니다. */
    case OP_PROC:
    case OP_BUMP:
        break;
    }
}

/* --------------------------------------------------------------- assembly */

static unsigned char clamp8(float v) {
    return (unsigned char)(v < 0.0f ? 0 : v > 1.0f ? 255 : v * 255.0f);
}

/* Fills the whole buffer from an imported bitmap, repeated `tiles` times.
 *
 * ENGLISH
 * -------
 * THE MATERIAL'S OWN NAME IS THE LOOKUP KEY, which is why `image` takes a
 * repeat count and not a filename. Every other op in this file takes integers,
 * and adding string arguments to the recipe grammar for one op would be a
 * second kind of operand for every op that follows it to be parsed around. A
 * material called `wall_brick` fetches the drawing called `wall_brick`: one
 * name, one thing, and no way to write the two so they disagree.
 *
 * The repeat is why SPR_WALL divides SIZE. 128 into 256 is exactly two tiles,
 * so the wrap lands on the buffer edge where GL_REPEAT already joins it. A
 * size that did not divide would put a visible seam down every wall.
 *
 * Returns 0 when no drawing of that name was baked, and the caller then leaves
 * the recipe's other ops to paint the surface -- so a material whose art has
 * not been imported yet degrades to its written recipe rather than to black.
 *
 * 한국어
 * ------
 * 재질 자신의 이름이 조회 키이며, 그래서 `image`는 파일명이 아니라 반복 횟수를 받습니다.
 * 이 파일의 다른 모든 연산은 정수를 받으며, 연산 하나를 위해 레시피 문법에 문자열 인자를
 * 더하면 그 뒤의 모든 연산이 우회해서 파싱해야 할 두 번째 종류의 피연산자가 생깁니다.
 * `wall_brick`이라는 재질은 `wall_brick`이라는 그림을 가져옵니다. 이름 하나에 사물 하나,
 * 그리고 둘이 어긋나게 쓸 방법이 없습니다.
 *
 * SPR_WALL이 SIZE를 나누어떨어져야 하는 이유가 이 반복입니다. 256 안의 128은 정확히 두
 * 타일이므로 되감김이 버퍼 가장자리에 떨어지고, 그곳은 GL_REPEAT가 이미 잇는 자리입니다.
 *
 * 그 이름의 그림이 구워지지 않았으면 0을 반환하고, 호출자는 레시피의 나머지 연산이 표면을
 * 칠하도록 둡니다. 아직 아트를 가져오지 않은 재질이 검은색이 아니라 적힌 레시피로
 * 물러납니다.
 */
static int fill_from_image(const char *name, unsigned char *buf, int tiles) {
    if (tiles < 1) tiles = 1;

    unsigned char *src = HeapAlloc(GetProcessHeap(), 0, SPR_WALL * SPR_WALL * 4);
    if (!src) return 0;

    if (!sprite_wall(name, src)) {
        HeapFree(GetProcessHeap(), 0, src);
        return 0;
    }

    for (int y = 0; y < SIZE; y++) {
        int sy = (y * tiles * SPR_WALL / SIZE) % SPR_WALL;
        for (int x = 0; x < SIZE; x++) {
            int sx = (x * tiles * SPR_WALL / SIZE) % SPR_WALL;
            const unsigned char *s = &src[(sy * SPR_WALL + sx) * 4];
            unsigned char *d = &buf[(y * SIZE + x) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
            /* Alpha is gloss here, not transparency -- see the note on GLOSS in
               textures.txt. An imported wall is matte unless its recipe says
               otherwise, so the drawing's own alpha is deliberately dropped.
               여기서 알파는 투명도가 아니라 광택입니다. 가져온 벽은 레시피가 달리 말하지
               않는 한 무광이므로, 그림 자신의 알파는 일부러 버립니다. */
            d[3] = 0;
        }
    }

    HeapFree(GetProcessHeap(), 0, src);
    return 1;
}

GLuint tex_make(const char *name) {
    Op ops[MAX_OPS];
    int n_ops = parse_recipe(name, ops);
    if (!n_ops) return 0;

    unsigned char *buf = HeapAlloc(GetProcessHeap(), 0, SIZE * SIZE * 4);

    /* An imported surface paints the whole buffer before any per-pixel op
       runs, so the ops that follow it act ON the image: `tint` colours a door
       by its key, `grain` ages a wall. That ordering is what lets one bitmap
       become three locked doors instead of three bitmaps.
       가져온 표면은 픽셀 단위 연산이 돌기 전에 버퍼 전체를 칠하므로, 뒤따르는 연산들이
       그 이미지에 *작용*합니다. `tint`는 문을 열쇠 색으로 물들이고 `grain`은 벽을
       낡힙니다. 비트맵 하나가 비트맵 셋이 아니라 잠긴 문 셋이 되게 하는 것이 이 순서입니다. */
    int imaged = 0;
    for (int i = 0; i < n_ops; i++)
        if (ops[i].op == OP_IMAGE)
            imaged = fill_from_image(name, buf, ops[i].a[0]);

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            Px px = {0};
            if (imaged) {
                const unsigned char *q = &buf[(y * SIZE + x) * 4];
                px.r = q[0]; px.g = q[1]; px.b = q[2];
            }
            /* Seeding from the op index keeps two `brush` lines in the same
               recipe from producing identical noise. */
            for (int i = 0; i < n_ops; i++)
                run_op(&ops[i], &px, x, y, (unsigned)(i + 1));

            unsigned char *p = &buf[(y * SIZE + x) * 4];
            p[0] = clamp8(px.r); p[1] = clamp8(px.g);
            p[2] = clamp8(px.b); p[3] = clamp8(px.gloss);
        }
    }

    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* Anisotropic filtering, and it matters far more here than it would in an
       ordinary renderer.

       Plain mipmapping picks one level of detail per pixel, which is only
       correct when the texture is compressed equally in both axes. A floor
       seen at a grazing angle is the opposite case: hugely compressed along
       the view direction, barely compressed across it. Isotropic filtering
       has to choose, so it blurs one axis and aliases the other, and the
       aliasing lands exactly where the floor meets the horizon.

       Feeding that to the dither makes it worse rather than hiding it. The
       pattern is driven by luminance, so pixel-to-pixel luminance noise
       becomes pixel-to-pixel PATTERN noise -- the chaotic band across the
       mid-distance floor that this fixes. The dither can only be as clean as
       the image it quantises.

       Queried rather than assumed: the extension is near-universal on desktop
       GL but not core, so an absent value leaves the parameter untouched and
       filtering falls back to plain mipmapping.

       비등방성 필터링이며, 일반적인 렌더러에서보다 이곳에서 훨씬 더 중요합니다.

       단순 밉매핑은 픽셀당 하나의 상세 수준을 고르는데, 이는 텍스처가 양쪽 축으로
       동일하게 압축될 때만 올바릅니다. 비스듬히 보이는 바닥은 정반대의 경우입니다.
       시선 방향으로는 극도로 압축되고 그에 수직인 방향으로는 거의 압축되지 않습니다.
       등방성 필터링은 둘 중 하나를 선택해야 하므로 한 축은 흐려지고 다른 축은
       계단현상이 생기며, 그 계단현상은 정확히 바닥과 지평선이 만나는 지점에 나타납니다.

       이를 디더에 입력하면 감춰지는 것이 아니라 악화됩니다. 패턴이 휘도로 구동되므로
       픽셀 단위 휘도 잡음이 픽셀 단위 *패턴* 잡음이 됩니다. 중거리 바닥을 가로지르던
       혼란스러운 띠가 바로 이것이며 이 수정으로 해결됩니다. 디더는 자신이 양자화하는
       이미지만큼만 깨끗할 수 있습니다.

       가정하지 않고 조회합니다. 이 확장은 데스크톱 GL에서 거의 보편적이지만 코어는
       아니므로, 값을 얻지 못하면 해당 파라미터를 건드리지 않고 단순 밉매핑으로
       되돌아갑니다. */
    {
        GLfloat aniso = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &aniso);
        /* Clear the error an unsupported enum raises, so it cannot be blamed
           on whatever draws next.
           지원되지 않는 열거값이 발생시키는 오류를 지웁니다. 그래야 다음에 그리는
           것에 책임이 전가되지 않습니다. */
        while (glGetError() != GL_NO_ERROR) { }
        if (aniso > 1.0f) {
            if (aniso > 8.0f) aniso = 8.0f;   /* past 8 the gain is invisible */
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
        }
    }

    HeapFree(GetProcessHeap(), 0, buf);
    return t;
}

/* ------------------------------------------------------- procedural lookup */

/* Reads the shader selection out of a recipe. `base` and `gloss` do double
   duty here -- they already mean "colour" and "specular strength", so a
   procedural material needs no new vocabulary beyond the one `proc` line. */
static Mat mat_make(const char *name) {
    Op  ops[MAX_OPS];
    Mat m = {0};
    int n = parse_recipe(name, ops);
    if (!n) return m;

    m.scale = 1.0f;
    for (int i = 0; i < n; i++) {
        const short *a = ops[i].a;
        switch (ops[i].op) {
        case OP_BASE:
            m.rgb[0] = a[0] / 255.0f;
            m.rgb[1] = a[1] / 255.0f;
            m.rgb[2] = a[2] / 255.0f;
            break;
        case OP_GLOSS: m.params[0] = a[0] / 100.0f; break;
        /* Normal-map strength. Only the procedural path reads it -- a pixel
           material's relief is painted into the image already.
           노멀 맵 강도입니다. 절차적 경로만 이 값을 읽습니다. 픽셀 재질의 요철은
           이미 이미지에 칠해져 있습니다. */
        case OP_BUMP:  m.params[1] = a[0] / 100.0f; break;
        case OP_PROC:
            m.proc  = a[0];
            m.scale = a[1] / 100.0f;
            break;
        }
    }
    /* Only build pixels for the materials that actually have any. */
    if (!m.proc) m.tex = tex_make(name);
    return m;
}

void tex_use(const Mat *m) {
    glBindTexture(GL_TEXTURE_2D, m->tex);
    rd_proc(m->proc, m->rgb, m->scale, m->params);
}

/* ------------------------------------------------------------------ cache */

/* Sized against what actually asks for materials, not against a guess.
   tools/mapedit.c offers a palette of MAX_MATS (32) recipes and calls tex_mat
   on every one, so 24 was already short of the editor's own ceiling. 48 leaves
   room for the palette plus whatever a level names beyond it. This lives in
   .bss and is zero-filled, so it costs nothing on disk -- the same reasoning
   LVL_MAX_RANGES is sized by.

   무엇이 실제로 재질을 요청하는지를 기준으로 정했으며, 추측으로 정하지 않았습니다.
   tools/mapedit.c는 MAX_MATS(32)개의 레시피로 팔레트를 구성하고 그 전부에 대해
   tex_mat을 호출하므로, 24는 이미 에디터 자체의 상한에도 미치지 못했습니다. 48은
   팔레트와 그 너머로 레벨이 명명하는 것까지 감당할 여유를 남깁니다. 이 배열은 0으로
   채워진 .bss에 위치하므로 디스크 용량을 소모하지 않습니다. LVL_MAX_RANGES의 크기를
   정한 것과 동일한 근거입니다. */
/* Overridable so a test can force the overflow path. With the real value the
   cache comfortably holds every material the project defines, which means the
   reclaim below would never execute and would rot untested -- the branch only
   runs when something has gone wrong, which is precisely when it must work.
   tools/textest.c builds a second binary with a tiny value to exercise it.

   테스트가 초과 경로를 강제할 수 있도록 재정의 가능하게 두었습니다. 실제 값에서는 캐시가
   프로젝트가 정의하는 모든 재질을 여유롭게 담으므로, 아래의 회수 처리는 결코 실행되지
   않아 검증되지 않은 채 썩게 됩니다. 이 분기는 무언가 잘못되었을 때만 실행되며, 바로 그
   때 반드시 동작해야 합니다. tools/textest.c가 작은 값으로 두 번째 바이너리를 빌드하여
   이를 실행시킵니다. */
#ifndef MAX_CACHED
#define MAX_CACHED 48
#endif

/* The name field must hold any material name in full. A name longer than this
   would be TRUNCATED on store but compared in FULL on lookup, so it could
   never match its own entry: every call would miss, rebuild the texture, and
   -- before the reclaim below existed -- leak it. LVL_MAT is the authoring
   limit for a material name, so sizing from it makes that unreachable by
   construction rather than by luck.

   이름 필드는 어떤 재질 이름이든 온전히 담아야 합니다. 이보다 긴 이름은 저장 시에는
   *잘리고* 조회 시에는 *전체가* 비교되므로, 자기 자신의 항목과 결코 일치할 수 없습니다.
   매 호출이 캐시 미스가 되어 텍스처를 재생성하고, 아래의 회수 처리가 있기 전에는 그것을
   누수시켰습니다. LVL_MAT이 재질 이름의 제작 상한이므로, 이를 기준으로 크기를 정하면
   그 상황이 우연이 아니라 구조적으로 불가능해집니다.

   The bound is TEX_NAME_MAX in tex.h rather than a local constant, because it
   constrains callers and not just this file. It is tied to level.h's LVL_MAT
   by a static assert in weapon.c -- a file that already includes both headers
   and passes level-authored names to tex_mat, so the check costs no new
   dependency here.

   이 상한은 지역 상수가 아니라 tex.h의 TEX_NAME_MAX입니다. 이 파일만이 아니라 호출자를
   제약하는 값이기 때문입니다. level.c의 정적 검사가 이를 level.h의 LVL_MAT과 묶어
   줍니다. 두 헤더를 모두 정당하게 참조하는 유일한 파일이므로, 이곳에 새로운 의존성을
   추가하지 않고도 검사가 가능합니다. */
#define CACHE_NAME_LEN TEX_NAME_MAX

static struct { char name[CACHE_NAME_LEN]; Mat mat; } g_cache[MAX_CACHED];
static int g_cached;

Mat tex_mat(const char *name) {
    for (int i = 0; i < g_cached; i++) {
        const char *a = g_cache[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return g_cache[i].mat;
    }

    Mat m = mat_make(name);
    if (!m.tex && !m.proc) return m;        /* no such recipe; nothing to own */

    /* Measure before storing: a name that will not fit cannot be cached at
       all, because the truncated copy would never match the full name on
       lookup. Treated as a cache failure rather than stored half-written.
       저장 전에 길이를 확인합니다. 들어가지 않는 이름은 아예 캐시할 수 없습니다.
       잘린 사본은 조회 시 전체 이름과 결코 일치하지 않기 때문입니다. 절반만 기록된 채로
       저장하는 대신 캐시 실패로 처리합니다. */
    int len = 0;
    while (name[len]) len++;

    if (g_cached >= MAX_CACHED || len >= CACHE_NAME_LEN) {
        /* Cannot take ownership, so do not leave a texture behind. tex_flush
           only walks the cache, so a handle that never reached it would be
           unreachable for the rest of the process -- and this path runs on
           every level load and every hot reload, so it accumulates.

           Reported because the visible symptom is nothing at all: the material
           still draws correctly, it is just rebuilt from its recipe on every
           call. Silent, gradual, and exactly what diag.h exists for.

           소유권을 가질 수 없으므로 텍스처를 남겨 두지 않습니다. tex_flush는 캐시만
           순회하므로, 캐시에 도달하지 못한 핸들은 프로세스가 끝날 때까지 회수할 수 없게
           됩니다. 그리고 이 경로는 레벨 로드와 핫 리로드마다 실행되므로 누적됩니다.

           보고하는 이유는 눈에 보이는 증상이 전혀 없기 때문입니다. 해당 재질은 여전히
           올바르게 그려지며, 다만 호출할 때마다 레시피로부터 재생성될 뿐입니다. 조용하고
           점진적이며, 정확히 diag.h가 존재하는 이유입니다. */
        DIAG(DIAG_TEX_CACHE);
        if (m.tex) {
            glDeleteTextures(1, &m.tex);
            m.tex = 0;
        }
        return m;
    }

    int i = 0;
    for (; i < len; i++) g_cache[g_cached].name[i] = name[i];
    g_cache[g_cached].name[i] = 0;
    g_cache[g_cached].mat = m;
    g_cached++;
    return m;
}


void tex_flush(void) {
    for (int i = 0; i < g_cached; i++)
        if (g_cache[i].mat.tex) glDeleteTextures(1, &g_cache[i].mat.tex);
    g_cached = 0;
}
