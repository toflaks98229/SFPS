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
    OP_WOOD, OP_CHECK, OP_RIBS, OP_BLUE, OP_GLOSS, OP_PROC, OP_COUNT
};

static const struct { const char *name; unsigned char arity; } OPS[OP_COUNT] = {
    {"base",    3}, {"brick",   3}, {"tint",    3}, {"grain",   1},
    {"bevel",   3}, {"mortar",  4}, {"brush",   3}, {"blotch",  2},
    {"scratch", 2}, {"seam",    3}, {"wood",    4}, {"check",   2},
    {"ribs",    2}, {"blue",    1}, {"gloss",   1}, {"proc",    2}
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
       instead, and is read by tex_mat rather than by the interpreter. */
    case OP_PROC:
        break;
    }
}

/* --------------------------------------------------------------- assembly */

static unsigned char clamp8(float v) {
    return (unsigned char)(v < 0.0f ? 0 : v > 1.0f ? 255 : v * 255.0f);
}

GLuint tex_make(const char *name) {
    Op ops[MAX_OPS];
    int n_ops = parse_recipe(name, ops);
    if (!n_ops) return 0;

    unsigned char *buf = HeapAlloc(GetProcessHeap(), 0, SIZE * SIZE * 4);

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            Px px = {0};
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

#define MAX_CACHED 24

static struct { char name[24]; Mat mat; } g_cache[MAX_CACHED];
static int g_cached;

Mat tex_mat(const char *name) {
    for (int i = 0; i < g_cached; i++) {
        const char *a = g_cache[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return g_cache[i].mat;
    }

    Mat m = mat_make(name);
    if ((m.tex || m.proc) && g_cached < MAX_CACHED) {
        int i = 0;
        for (; name[i] && i < (int)sizeof(g_cache[0].name) - 1; i++)
            g_cache[g_cached].name[i] = name[i];
        g_cache[g_cached].name[i] = 0;
        g_cache[g_cached].mat = m;
        g_cached++;
    }
    return m;
}


void tex_flush(void) {
    for (int i = 0; i < g_cached; i++)
        if (g_cache[i].mat.tex) glDeleteTextures(1, &g_cache[i].mat.tex);
    g_cached = 0;
}
