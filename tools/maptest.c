/* maptest -- parse .map text and check the geometry that comes out.
 *
 * The fixtures are written HERE, not loaded from a file, and every one of them
 * is small enough to read in full beside the assertion that names it. A test
 * whose input lives elsewhere is a test that starts failing when somebody edits
 * a level, which teaches you to ignore the suite -- the same reasoning
 * movetest.c gives for building its own room instead of asserting against
 * `arena`.
 *
 * The cube fixture appears twice, once in Valve 220 and once in Standard, from
 * the same three-point text. That is deliberate: the two formats must produce
 * identical planes, and the only way to be sure is to parse both and compare.
 *
 * 픽스처는 파일에서 읽지 않고 *이곳에* 적습니다. 모두 그것을 지목하는 단언 옆에서 전문을
 * 읽을 수 있을 만큼 작습니다. 입력이 다른 곳에 사는 테스트는 누군가 레벨을 편집하면 실패하기
 * 시작하는 테스트이며, 그것은 검사 묶음을 무시하도록 가르칩니다. movetest.c가 `arena`에 대해
 * 단언하는 대신 자기 방을 짓는 이유와 같습니다.
 *
 * 큐브 픽스처는 같은 세 점 텍스트로부터 Valve 220과 Standard 두 번 등장합니다. 의도적입니다.
 * 두 형식은 동일한 평면을 만들어 내야 하며, 확인하는 유일한 방법은 둘 다 파싱해 비교하는
 * 것입니다.
 */

#include <stdio.h>
#include <math.h>
#include "brush.h"
#include "data.h"
#include "diag.h"
#include "render.h"
#include "model.h"
#include "txt.h"

static int fails;

/* Two null-terminated strings, compared. txt.h has txt_is, which wants a length
   for its first argument, and everything compared here is already terminated. */
static int str_same(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void check(int ok, const char *what) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static void checkf(float got, float want, float tol, const char *what) {
    int ok = fabsf(got - want) <= tol;
    printf("  %-58s got %9.4f  want %9.4f  %s\n",
           what, got, want, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static void checki(int got, int want, const char *what) {
    int ok = (got == want);
    printf("  %-58s got %9d  want %9d  %s\n",
           what, got, want, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static void checkv(v3 got, v3 want, float tol, const char *what) {
    int ok = fabsf(got.x - want.x) <= tol &&
             fabsf(got.y - want.y) <= tol &&
             fabsf(got.z - want.z) <= tol;
    printf("  %-58s got %6.3f %6.3f %6.3f  want %6.3f %6.3f %6.3f  %s\n",
           what, got.x, got.y, got.z, want.x, want.y, want.z, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

/* Big, so it is a file static rather than a stack local -- brush.h says so and
   a BrushMap on the stack is a few hundred kilobytes of it. */
static BrushMap M, M2;

/* --- the cube ------------------------------------------------------------
   x,y in [-64,64] and z in [0,128], map units. At BRUSH_UNIT that is 4m x 4m
   in plan and 4m tall, sitting on the floor.

   Each face's three points are clockwise seen from outside, which is what makes
   Quake's cross product point out of the solid. */
#define P_TOP    "( -64 -64 128 ) ( -64 64 128 ) ( 64 64 128 )"
#define P_BOTTOM "( -64 -64 0 ) ( 64 -64 0 ) ( 64 64 0 )"
#define P_WEST   "( -64 64 128 ) ( -64 -64 128 ) ( -64 -64 0 )"
#define P_EAST   "( 64 -64 128 ) ( 64 64 128 ) ( 64 64 0 )"
#define P_SOUTH  "( 64 -64 0 ) ( -64 -64 0 ) ( -64 -64 128 )"
#define P_NORTH  "( -64 64 0 ) ( 64 64 0 ) ( 64 64 128 )"

/* Valve 220: the face names its own u and v directions. */
#define V_FLAT "[ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1"      /* floors and ceilings */
#define V_XW   "[ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1"      /* walls facing +/-x   */
#define V_YW   "[ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1"      /* walls facing +/-y   */

static const char CUBE_VALVE[] =
    "// Game: SFPS\n"
    "// Format: Valve\n"
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "{\n"
    P_TOP    " wall_brick " V_FLAT "\n"
    P_BOTTOM " wall_brick " V_FLAT "\n"
    P_WEST   " wall_brick " V_XW   "\n"
    P_EAST   " wall_brick " V_XW   "\n"
    P_SOUTH  " wall_brick " V_YW   "\n"
    P_NORTH  " wall_brick " V_YW   "\n"
    "}\n"
    "}\n";

/* Standard: no axes, so they are derived from the plane and the rotation. */
static const char CUBE_STANDARD[] =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "{\n"
    P_TOP    " wall_brick 0 0 0 1 1\n"
    P_BOTTOM " wall_brick 0 0 0 1 1\n"
    P_WEST   " wall_brick 0 0 0 1 1\n"
    P_EAST   " wall_brick 0 0 0 1 1\n"
    P_SOUTH  " wall_brick 0 0 0 1 1\n"
    P_NORTH  " wall_brick 0 0 0 1 1\n"
    "}\n"
    "}\n";

/* Both formats inside ONE brush, which a file that has been edited in two
   editors -- or had a brush pasted into it -- genuinely contains. */
static const char CUBE_MIXED[] =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "{\n"
    P_TOP    " wall_brick " V_FLAT "\n"
    P_BOTTOM " wall_brick 0 0 0 1 1\n"
    P_WEST   " wall_brick " V_XW   "\n"
    P_EAST   " wall_brick 0 0 0 1 1\n"
    P_SOUTH  " wall_brick " V_YW   "\n"
    P_NORTH  " wall_brick 0 0 0 1 1\n"
    "}\n"
    "}\n";

/* Written the way a hand edit looks: no space inside the brackets, tabs, CRLF,
   and a comment mid-brush. TrenchBroom writes none of these and a person writes
   all of them. */
static const char CUBE_TIGHT[] =
    "{\r\n"
    "\t\"classname\" \"worldspawn\"\r\n"
    "\t{\r\n"
    "\t(-64 -64 128) (-64 64 128) (64 64 128) wall_brick [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\r\n"
    "\t// the floor\r\n"
    "\t(-64 -64 0) (64 -64 0) (64 64 0) wall_brick [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\r\n"
    "\t(-64 64 128) (-64 -64 128) (-64 -64 0) wall_brick [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1\r\n"
    "\t(64 -64 128) (64 64 128) (64 64 0) wall_brick [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1\r\n"
    "\t(64 -64 0) (-64 -64 0) (-64 -64 128) wall_brick [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1\r\n"
    "\t(-64 64 0) (64 64 0) (64 64 128) wall_brick [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1\r\n"
    "\t}\r\n"
    "}\r\n";

/* Finds a face by the direction it points, so an assertion names the face the
   way a person does ("the ceiling") rather than by the order it was written. */
static int face_facing(const BrushMap *m, int bi, v3 dir) {
    const Brush *b = &m->brushes[bi];
    for (int i = 0; i < b->n_faces; i++)
        if (v3dot(m->faces[b->first_face + i].normal, dir) > 0.999f) return i;
    return -1;
}

static void test_cube(void) {
    printf("\nthe cube, Valve 220\n");

    checki(brush_parse(CUBE_VALVE, -1, &M),1, "one entity");
    checki(M.n_brushes, 1, "one brush");
    checki(M.brushes[0].n_faces, 6, "six faces");

    /* The axis change, stated as the thing it is: map z is up and becomes
       engine y; map y is north and becomes engine -z. */
    checkv(M.brushes[0].min, v3f(-2.0f, 0.0f, -2.0f), 0.001f, "box min (metres)");
    checkv(M.brushes[0].max, v3f( 2.0f, 4.0f,  2.0f), 0.001f, "box max (metres)");

    struct { const char *name; v3 dir; } want[] = {
        { "ceiling faces up",       v3f( 0,  1,  0) },
        { "floor faces down",       v3f( 0, -1,  0) },
        { "west wall faces -x",     v3f(-1,  0,  0) },
        { "east wall faces +x",     v3f( 1,  0,  0) },
        { "map-south wall faces +z", v3f( 0,  0,  1) },
        { "map-north wall faces -z", v3f( 0,  0, -1) },
    };
    for (int i = 0; i < 6; i++)
        check(face_facing(&M, 0, want[i].dir) >= 0, want[i].name);

    /* Every face of a closed box is a quad, and every quad is wound so the
       right-hand rule about its own normal agrees with that normal. Getting
       this wrong draws the level inside out and is invisible until you are
       standing in it. */
    int quads = 0, wound = 0;
    for (int f = 0; f < M.brushes[0].n_faces; f++) {
        v3 poly[BR_MAX_POLY];
        int n = brush_face_poly(&M, 0, f, poly, BR_MAX_POLY);
        if (n == 4) quads++;
        if (n >= 3) {
            v3 geo = v3norm(v3cross(v3sub(poly[1], poly[0]), v3sub(poly[2], poly[0])));
            if (v3dot(geo, M.faces[M.brushes[0].first_face + f].normal) > 0.999f) wound++;
        }
    }
    checki(quads, 6, "all six faces are quads");
    checki(wound, 6, "all six wound counter-clockwise from outside");

    /* The ceiling sits at 4m and spans the full 4m square. */
    {
        int f = face_facing(&M, 0, v3f(0, 1, 0));
        v3 poly[BR_MAX_POLY];
        int n = brush_face_poly(&M, 0, f, poly, BR_MAX_POLY);
        float ylo = 1e9f, yhi = -1e9f, xlo = 1e9f, xhi = -1e9f;
        for (int i = 0; i < n; i++) {
            if (poly[i].y < ylo) ylo = poly[i].y;
            if (poly[i].y > yhi) yhi = poly[i].y;
            if (poly[i].x < xlo) xlo = poly[i].x;
            if (poly[i].x > xhi) xhi = poly[i].x;
        }
        checkf(ylo, 4.0f, 0.001f, "ceiling polygon is flat at 4m (low)");
        checkf(yhi, 4.0f, 0.001f, "ceiling polygon is flat at 4m (high)");
        checkf(xhi - xlo, 4.0f, 0.001f, "ceiling polygon is 4m across");
    }
}

static void test_formats_agree(void) {
    printf("\nStandard and Valve 220 describe the same solid\n");

    checki(brush_parse(CUBE_STANDARD, -1, &M2),1, "standard-format cube parses");
    checki(M2.brushes[0].n_faces, 6, "six faces");
    checkv(M2.brushes[0].min, M.brushes[0].min, 0.001f, "same box min as Valve");
    checkv(M2.brushes[0].max, M.brushes[0].max, 0.001f, "same box max as Valve");

    /* Plane for plane. The formats differ only in how the texture is placed, so
       a difference here would mean the point-to-plane derivation is reading one
       of the two files differently -- which is exactly what a mixed file would
       then get wrong in a way no single-format test could see. */
    int same = 0;
    for (int i = 0; i < 6; i++) {
        const BrushFace *a = &M.faces[M.brushes[0].first_face + i];
        const BrushFace *b = &M2.faces[M2.brushes[0].first_face + i];
        if (v3len(v3sub(a->normal, b->normal)) < 0.001f &&
            fabsf(a->dist - b->dist) < 0.001f) same++;
    }
    checki(same, 6, "all six planes identical");

    checki(brush_parse(CUBE_MIXED, -1, &M2),1, "one brush, both formats, parses");
    checki(M2.brushes[0].n_faces, 6, "six faces");
    checkv(M2.brushes[0].min, M.brushes[0].min, 0.001f, "mixed brush: same box min");
    checkv(M2.brushes[0].max, M.brushes[0].max, 0.001f, "mixed brush: same box max");

    checki(brush_parse(CUBE_TIGHT, -1, &M2),1, "CRLF, tabs, tight brackets, comments");
    checkv(M2.brushes[0].max, M.brushes[0].max, 0.001f, "hand-written form: same box");
}

/* --- the UV fix ----------------------------------------------------------
   The reason this format was adopted. The wall at map y=64 is 128 units wide
   and 128 tall, and these axes fit one 128px texture to it exactly: u runs
   along x with a 64 offset, v runs down z with a 128 offset.

   Under the sector model there was no way to say this. render.c's planar_uv
   projects along a world axis, so the texture was laid out in world space and
   the door got however much of it happened to fall across the door. */
static const char DOOR_FACE[] =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "{\n"
    P_TOP    " wall_door " V_FLAT "\n"
    P_BOTTOM " wall_door " V_FLAT "\n"
    P_WEST   " wall_door " V_XW   "\n"
    P_EAST   " wall_door " V_XW   "\n"
    P_SOUTH  " wall_door " V_YW   "\n"
    P_NORTH  " wall_door [ 1 0 0 64 ] [ 0 0 -1 128 ] 0 1 1\n"
    "}\n"
    "}\n";

static void test_uv(void) {
    printf("\none texture fitted to one face (the door problem)\n");

    checki(brush_parse(DOOR_FACE, -1, &M2),1, "parses");

    int f = face_facing(&M2, 0, v3f(0, 0, -1));
    check(f >= 0, "found the wall facing -z");
    if (f < 0) return;

    const BrushFace *face = &M2.faces[M2.brushes[0].first_face + f];
    checki(txt_is(face->tex, 9, "wall_door"), 1, "texture name survived the parse");

    /* The four corners of that wall, in world metres, and what a 128x128
       texture must give at each. 0,0 is the top-left of the texture; v grows
       downward, which is the same direction level.c's walls run. */
    struct { v3 p; float u, v; const char *what; } c[] = {
        { v3f(-2.0f, 4.0f, -2.0f), 0.0f, 0.0f, "west top    -> u 0 v 0" },
        { v3f( 2.0f, 4.0f, -2.0f), 1.0f, 0.0f, "east top    -> u 1 v 0" },
        { v3f( 2.0f, 0.0f, -2.0f), 1.0f, 1.0f, "east bottom -> u 1 v 1" },
        { v3f(-2.0f, 0.0f, -2.0f), 0.0f, 1.0f, "west bottom -> u 0 v 1" },
    };
    for (int i = 0; i < 4; i++) {
        float u, v;
        brush_face_uv(face, c[i].p, 128.0f, 128.0f, &u, &v);
        int ok = fabsf(u - c[i].u) < 0.001f && fabsf(v - c[i].v) < 0.001f;
        printf("  %-58s got %6.3f %6.3f  %s\n", c[i].what, u, v, ok ? "ok" : "FAIL");
        if (!ok) fails++;
    }

    /* And the point of it: the whole face is inside one tile. Under planar_uv
       the same 4m wall covered two tiles and started wherever it stood. */
    v3 poly[BR_MAX_POLY];
    int n = brush_face_poly(&M2, 0, f, poly, BR_MAX_POLY);
    int inside = 1;
    for (int i = 0; i < n; i++) {
        float u, v;
        brush_face_uv(face, poly[i], 128.0f, 128.0f, &u, &v);
        if (u < -0.001f || u > 1.001f || v < -0.001f || v > 1.001f) inside = 0;
    }
    check(n == 4 && inside, "every vertex of the face lands within one tile");
}

/* --- the slope -----------------------------------------------------------
   A ramp climbing from x=-64 at floor level to x=64 at 128 units. This is the
   shape the sector model could not hold at all: one x,z has one floor there,
   and a ramp needs a different floor at every point along it.

   Five planes, not six. The west wall would have zero height, and a brush does
   not need a plane where it has no face. */
static const char WEDGE[] =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "{\n"
    "( -64 -64 0 ) ( -64 64 0 ) ( 64 64 128 ) wall_stone [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n"
    P_BOTTOM " wall_stone " V_FLAT "\n"
    P_EAST   " wall_stone " V_XW   "\n"
    P_SOUTH  " wall_stone " V_YW   "\n"
    P_NORTH  " wall_stone " V_YW   "\n"
    "}\n"
    "}\n";

static void test_slope(void) {
    printf("\na slope, which no sector could express\n");

    checki(brush_parse(WEDGE, -1, &M2),1, "parses");
    checki(M2.brushes[0].n_faces, 5, "five planes close it");

    /* The ramp climbs toward +x, so its outward normal leans up and back. */
    v3 want = v3norm(v3f(-1.0f, 1.0f, 0.0f));
    int f = face_facing(&M2, 0, want);
    check(f >= 0, "the sloped face points up and toward -x");
    if (f < 0) return;

    v3 poly[BR_MAX_POLY];
    int n = brush_face_poly(&M2, 0, f, poly, BR_MAX_POLY);
    checki(n, 4, "the slope is a quad");

    /* Its low edge is on the floor at x=-2m and its high edge is at 4m. */
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < n; i++) {
        if (poly[i].y < lo) lo = poly[i].y;
        if (poly[i].y > hi) hi = poly[i].y;
    }
    checkf(lo, 0.0f, 0.001f, "slope starts on the floor");
    checkf(hi, 4.0f, 0.001f, "slope reaches 4m");
    checkv(M2.brushes[0].min, v3f(-2.0f, 0.0f, -2.0f), 0.001f, "wedge box min");
    checkv(M2.brushes[0].max, v3f( 2.0f, 4.0f,  2.0f), 0.001f, "wedge box max");
}

/* --- entities ------------------------------------------------------------ */

static const char ENTS[] =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "\"next\" \"vault\"\n"
    "{\n"
    P_TOP    " wall_brick " V_FLAT "\n"
    P_BOTTOM " wall_brick " V_FLAT "\n"
    P_WEST   " wall_brick " V_XW   "\n"
    P_EAST   " wall_brick " V_XW   "\n"
    P_SOUTH  " wall_brick " V_YW   "\n"
    P_NORTH  " wall_brick " V_YW   "\n"
    "}\n"
    "}\n"
    "{\n"
    "\"classname\" \"info_player_start\"\n"
    "\"origin\" \"32 -96 64\"\n"
    "\"angle\" \"90\"\n"
    "}\n"
    "{\n"
    "\"classname\" \"func_door\"\n"
    "\"speed\" \"3.5\"\n"
    "{\n"
    P_TOP    " wall_door " V_FLAT "\n"
    P_BOTTOM " wall_door " V_FLAT "\n"
    P_WEST   " wall_door " V_XW   "\n"
    P_EAST   " wall_door " V_XW   "\n"
    P_SOUTH  " wall_door " V_YW   "\n"
    P_NORTH  " wall_door " V_YW   "\n"
    "}\n"
    "}\n";

static void test_entities(void) {
    printf("\nentities, and which brushes belong to which\n");

    checki(brush_parse(ENTS, -1, &M2),3, "three entities");
    checki(M2.n_brushes, 2, "two brushes in the file");

    const char *cn = brush_ent_value(&M2.ents[0], "classname");
    check(cn && txt_is(cn, 10, "worldspawn"), "entity 0 is worldspawn");
    checki(M2.ents[0].n_brushes, 1, "worldspawn owns the room");

    const char *nx = brush_ent_value(&M2.ents[0], "next");
    check(nx && txt_is(nx, 5, "vault"), "an unknown key is carried through");
    check(brush_ent_value(&M2.ents[0], "nope") == 0, "an absent key reads as NULL");

    /* A point entity owns nothing, and its origin converts the same way a
       brush vertex does -- map (32,-96,64) is engine (1, 2, 3) metres. */
    checki(M2.ents[1].n_brushes, 0, "the player start owns no brushes");
    v3 o;
    check(brush_ent_point(&M2.ents[1], "origin", &o), "origin parses");
    checkv(o, v3f(1.0f, 2.0f, 3.0f), 0.001f, "origin converts to engine axes");
    checkf(brush_ent_num(&M2.ents[1], "angle", -1.0f), 90.0f, 0.001f, "angle reads");
    checkf(brush_ent_num(&M2.ents[1], "wait", 7.0f), 7.0f, 0.001f, "absent number falls back");
    checkf(brush_ent_num(&M2.ents[2], "speed", 0.0f), 3.5f, 0.001f, "a fractional value reads");

    /* The door owns its own brush, which is the whole of "a door is a group of
       brushes that moves". */
    checki(M2.ents[2].n_brushes, 1, "func_door owns one brush");
    checki(M2.ents[2].first_brush, 1, "and it is the second brush in the file");
}

/* --- faces that bound nothing, and brushes that close nothing ------------ */

/* A seventh plane, out past the far side of the cube. Dragging a face through a
   brush in the editor leaves exactly this, and the file keeps it. */
static const char REDUNDANT[] =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "{\n"
    P_TOP    " wall_brick " V_FLAT "\n"
    P_BOTTOM " wall_brick " V_FLAT "\n"
    P_WEST   " wall_brick " V_XW   "\n"
    P_EAST   " wall_brick " V_XW   "\n"
    P_SOUTH  " wall_brick " V_YW   "\n"
    P_NORTH  " wall_brick " V_YW   "\n"
    "( -64 -64 1000 ) ( -64 64 1000 ) ( 64 64 1000 ) wall_brick " V_FLAT "\n"
    "}\n"
    "}\n";

/* The cube with no ceiling. The four walls now reach the sky. */
static const char OPEN[] =
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "{\n"
    P_BOTTOM " wall_brick " V_FLAT "\n"
    P_WEST   " wall_brick " V_XW   "\n"
    P_EAST   " wall_brick " V_XW   "\n"
    P_SOUTH  " wall_brick " V_YW   "\n"
    P_NORTH  " wall_brick " V_YW   "\n"
    "}\n"
    "}\n";

static void test_unbounded(void) {
    printf("\nfaces that bound nothing, and a brush that closes nothing\n");

    int before = diag_count(DIAG_BRUSH_OPEN);
    checki(brush_parse(REDUNDANT, -1, &M2),1, "a redundant plane parses");
    checki(M2.brushes[0].n_faces, 7, "and is kept: it still bounds the solid");

    v3 poly[BR_MAX_POLY];
    checki(brush_face_poly(&M2, 0, 6, poly, BR_MAX_POLY), 0,
           "but has no polygon to draw");
    checkv(M2.brushes[0].max, v3f(2.0f, 4.0f, 2.0f), 0.001f,
           "and does not stretch the box");
    checki(diag_count(DIAG_BRUSH_OPEN) - before, 0, "no open-brush report");

    before = diag_count(DIAG_BRUSH_OPEN);
    checki(brush_parse(OPEN, -1, &M2),1, "a cube with no ceiling parses");
    check(diag_count(DIAG_BRUSH_OPEN) - before >= 4,
          "each unbounded wall is reported");
    checki(brush_face_poly(&M2, 0, 0, poly, BR_MAX_POLY), 4,
           "the floor is still bounded and still a quad");
}

/* --- a file that is not a map -------------------------------------------- */

static void test_malformed(void) {
    printf("\nall or nothing\n");

    /* A brush cut off mid-face. Everything before it is well formed, and none
       of it is kept: half a level opens and looks almost right. */
    static const char TRUNCATED[] =
        "{\n"
        "\"classname\" \"worldspawn\"\n"
        "{\n"
        P_TOP " wall_brick " V_FLAT "\n"
        "( -64 -64 0 ) ( 64 -64 0 ) ( 64 64\n";

    checki(brush_parse(TRUNCATED, -1, &M2),0, "a truncated file parses to nothing");
    checki(M2.n_brushes, 0, "and leaves no brushes behind");
    checki(M2.n_ents, 0, "and no entities");

    checki(brush_parse("", -1, &M2),0, "empty text is not a map");
    checki(brush_parse(0, -1, &M2),0, "and neither is nothing at all");
}

/* --- capacity ------------------------------------------------------------
   The overflow branch only runs when something overflows, so the fixture is
   generated rather than written: BR_MAX_BRUSHES cubes plus a few. */

static char BIG[420000];

static int emit_pt(char *o, int cap, int pos, int x, int y, int z) {
    pos = txt_append_str(o, cap, pos, "( ");
    pos = txt_append_int(o, cap, pos, x);
    pos = txt_append_str(o, cap, pos, " ");
    pos = txt_append_int(o, cap, pos, y);
    pos = txt_append_str(o, cap, pos, " ");
    pos = txt_append_int(o, cap, pos, z);
    return txt_append_str(o, cap, pos, " ) ");
}

static int emit_face(char *o, int cap, int pos,
                     int ax, int ay, int az, int bx, int by, int bz,
                     int cx, int cy, int cz) {
    pos = emit_pt(o, cap, pos, ax, ay, az);
    pos = emit_pt(o, cap, pos, bx, by, bz);
    pos = emit_pt(o, cap, pos, cx, cy, cz);
    return txt_append_str(o, cap, pos, " t [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n");
}

static int emit_cube(char *o, int cap, int pos,
                     int x0, int y0, int z0, int x1, int y1, int z1) {
    pos = txt_append_str(o, cap, pos, "{\n");
    pos = emit_face(o, cap, pos, x0,y0,z1, x0,y1,z1, x1,y1,z1);
    pos = emit_face(o, cap, pos, x0,y0,z0, x1,y0,z0, x1,y1,z0);
    pos = emit_face(o, cap, pos, x0,y1,z1, x0,y0,z1, x0,y0,z0);
    pos = emit_face(o, cap, pos, x1,y0,z1, x1,y1,z1, x1,y1,z0);
    pos = emit_face(o, cap, pos, x1,y0,z0, x0,y0,z0, x0,y0,z1);
    pos = emit_face(o, cap, pos, x0,y1,z0, x1,y1,z0, x1,y1,z1);
    return txt_append_str(o, cap, pos, "}\n");
}

static void test_capacity(void) {
    printf("\ncapacity: what does not fit is dropped and said so\n");

    const int cap = (int)sizeof(BIG);
    const int extra = 8;
    int pos = txt_append_str(BIG, cap, 0, "{\n\"classname\" \"worldspawn\"\n");
    for (int i = 0; i < BR_MAX_BRUSHES + extra; i++)
        pos = emit_cube(BIG, cap, pos, i * 16, 0, 0, i * 16 + 8, 8, 8);
    pos = txt_append_str(BIG, cap, pos, "}\n");
    check(pos < cap - 1, "the generated fixture fitted in its buffer");

    int before = diag_count(DIAG_BRUSH_CAP);
    checki(brush_parse(BIG, -1, &M2),1, "an oversized map still parses");
    checki(M2.n_brushes, BR_MAX_BRUSHES, "and stops at the cap");
    checki(diag_count(DIAG_BRUSH_CAP) - before, extra, "reporting each brush it dropped");

    /* The dropped brushes cost no faces either: parse_face is still run so the
       tokens are consumed, but nothing is stored. */
    checki(M2.n_faces, BR_MAX_BRUSHES * 6, "and no faces from the dropped ones");

    /* A texture name longer than BR_TEX holds. Truncating it silently would
       draw the wrong material with nothing to say why. */
    static const char LONGNAME[] =
        "{\n\"classname\" \"worldspawn\"\n{\n"
        P_TOP    " a_very_long_texture_name " V_FLAT "\n"
        P_BOTTOM " wall_brick " V_FLAT "\n"
        P_WEST   " wall_brick " V_XW   "\n"
        P_EAST   " wall_brick " V_XW   "\n"
        P_SOUTH  " wall_brick " V_YW   "\n"
        P_NORTH  " wall_brick " V_YW   "\n"
        "}\n}\n";
    before = diag_count(DIAG_BRUSH_CAP);
    checki(brush_parse(LONGNAME, -1, &M2),1, "a long texture name parses");
    checki(diag_count(DIAG_BRUSH_CAP) - before, 1, "and is reported, not silently cut");

    /* More keys than an entity holds. */
    static const char MANYKEYS[] =
        "{\n"
        "\"classname\" \"light\"\n\"k1\" \"1\"\n\"k2\" \"2\"\n\"k3\" \"3\"\n"
        "\"k4\" \"4\"\n\"k5\" \"5\"\n\"k6\" \"6\"\n\"k7\" \"7\"\n\"k8\" \"8\"\n"
        "\"k9\" \"9\"\n\"k10\" \"10\"\n\"k11\" \"11\"\n\"k12\" \"12\"\n"
        "}\n";
    before = diag_count(DIAG_MAPENT_CAP);
    checki(brush_parse(MANYKEYS, -1, &M2),1, "an entity with 13 keys parses");
    checki(M2.ents[0].n_keys, BR_MAX_KEYS, "and keeps what fits");
    check(diag_count(DIAG_MAPENT_CAP) - before >= 1, "reporting the ones it dropped");
}

/* --- the form TrenchBroom actually writes -------------------------------
   The editor does not emit the corners of a face. It emits a tiny right-angled
   triangle on the face's plane -- three points one unit apart -- because a
   plane is all the format stores and three near points name it exactly as well
   as three far ones. Every fixture above uses corners because they are legible;
   this one is here so that legibility is not the only thing being tested. */
static const char TB_CUBE[] =
    "// Game: SFPS\n"
    "// Format: Valve\n"
    "// entity 0\n"
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "// brush 0\n"
    "{\n"
    "( -64 -64 0 ) ( -64 -63 0 ) ( -64 -64 1 ) __TB_empty [ 0 -1 0 0 ] [ 0 0 -1 0 ] 0 1 1\n"
    "( -64 -64 0 ) ( -64 -64 1 ) ( -63 -64 0 ) __TB_empty [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1\n"
    "( -64 -64 0 ) ( -63 -64 0 ) ( -64 -63 0 ) __TB_empty [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n"
    "( 64 64 128 ) ( 64 65 128 ) ( 65 64 128 ) __TB_empty [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n"
    "( 64 64 128 ) ( 65 64 128 ) ( 64 64 129 ) __TB_empty [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1\n"
    "( 64 64 128 ) ( 64 64 129 ) ( 64 65 128 ) __TB_empty [ 0 -1 0 0 ] [ 0 0 -1 0 ] 0 1 1\n"
    "}\n"
    "}\n";

static void test_trenchbroom_form(void) {
    printf("\nplanes named by a unit triangle, which is what the editor writes\n");

    checki(brush_parse(TB_CUBE, -1, &M2), 1, "parses");
    checki(M2.brushes[0].n_faces, 6, "six faces");
    checkv(M2.brushes[0].min, v3f(-2.0f, 0.0f, -2.0f), 0.001f, "same box as the corner form");
    checkv(M2.brushes[0].max, v3f( 2.0f, 4.0f,  2.0f), 0.001f, "same box as the corner form");

    int quads = 0;
    for (int f = 0; f < M2.brushes[0].n_faces; f++) {
        v3 poly[BR_MAX_POLY];
        if (brush_face_poly(&M2, 0, f, poly, BR_MAX_POLY) == 4) quads++;
    }
    checki(quads, 6, "all six are quads");
}

/* --- one map inside a blob of them --------------------------------------- */

static void test_bounded_parse(void) {
    printf("\nreading one map out of many packed end to end\n");

    /* Two maps with nothing between them, which is how the baked blob stores
       them. The length is the only thing that says where the first stops. */
    static const char TWO[] =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"info_player_start\"\n\"origin\" \"1 2 3\"\n}\n";
    const int FIRST = (int)sizeof("{\n\"classname\" \"worldspawn\"\n}\n") - 1;

    checki(brush_parse(TWO, FIRST, &M2), 1, "a length stops at the first map");
    const char *cn = brush_ent_value(&M2.ents[0], "classname");
    check(cn && txt_is(cn, 10, "worldspawn"), "and it is the right one");

    checki(brush_parse(TWO, -1, &M2), 2, "without one, both are read as one map");
}

/* --- the shipped level ---------------------------------------------------- */

static void test_atrium(void) {
    printf("\nassets\\maps\\atrium.map, through the asset pipeline\n");

    int len = 0;
    const char *text = data_map("atrium", &len);
    check(text != 0 && len > 0, "data_map finds it");
    check(data_map("no_such_level", &len) == 0, "and reports an unknown name");
    if (!text) return;

    /* By CLASSNAME, not by index. The first version asserted "entity 2 is the
       door" and broke the moment atrium gained lights, which is a level doing
       the ordinary thing rather than a fault -- a test that fails when the
       fixture grows teaches you to stop reading it.
       인덱스가 아니라 classname으로 찾습니다. 첫 판본은 "엔티티 2가 문"이라고 단언했고
       atrium이 광원을 얻는 순간 깨졌습니다. 그것은 결함이 아니라 레벨이 평범한 일을 한
       것입니다. 픽스처가 자랄 때 실패하는 시험은 그것을 읽지 않도록 가르칩니다. */
    check(brush_parse(text, len, &M2) >= 3, "the level's entities parse");
    /* Every brush belongs to exactly one entity, which is a thing that must be
       true of any .map and stays true when the level grows. The first version
       named the total, and a trigger volume added later made it wrong without
       anything being wrong -- the same lesson the door lookup above already
       carries.
       모든 브러시는 정확히 하나의 엔티티에 속하며, 이는 어떤 .map에서나 참이어야 하고 레벨이
       자라도 계속 참입니다. 첫 판본은 총계를 명시했고, 나중에 더해진 트리거 부피가 아무것도
       잘못되지 않은 채로 그것을 틀리게 만들었습니다. 위의 문 조회가 이미 담고 있는 것과 같은
       교훈입니다. */
    int owned = 0;
    for (int i = 0; i < M2.n_ents; i++) owned += M2.ents[i].n_brushes;
    checki(owned, M2.n_brushes, "every brush belongs to exactly one entity");
    check(M2.n_brushes >= 11, "and the room is all there");

    const char *cn = brush_ent_value(&M2.ents[0], "classname");
    check(cn && txt_is(cn, 10, "worldspawn"), "entity 0 is worldspawn");

    /* THE SAME LESSON A THIRD TIME. This named ten, and then the floor was cut
       into four pieces to make a hole for the lava -- the level doing an
       ordinary thing, and the assertion breaking for it. What is actually
       being claimed is that the world holds the world: everything nobody else
       claimed. So count that instead, and let the room grow.
       같은 교훈이 세 번째입니다. 이곳은 10을 명시했고, 그다음 용암을 놓을 구멍을 내려고
       바닥이 네 조각으로 잘렸습니다. 레벨이 평범한 일을 했고 단언이 그것 때문에 깨졌습니다.
       실제로 주장하는 바는 세계가 세계를 담는다는 것, 즉 아무도 가져가지 않은 전부입니다.
       그러니 그것을 세고 방은 자라게 둡니다. */
    int claimed = 0;
    for (int i = 1; i < M2.n_ents; i++) claimed += M2.ents[i].n_brushes;
    checki(M2.ents[0].n_brushes, M2.n_brushes - claimed,
           "worldspawn owns every brush no other entity claimed");
    check(M2.ents[0].n_brushes >= 10, "and that is the room, not a corner of it");

    int door = -1, lights = 0;
    for (int i = 0; i < M2.n_ents; i++) {
        const char *k = brush_ent_value(&M2.ents[i], "classname");
        if (!k) continue;
        if (txt_is(k, 9, "func_door")) door = i;
        if (txt_is(k, 5, "light")) lights++;
    }
    check(door >= 0, "there is a func_door");
    checki(door >= 0 ? M2.ents[door].n_brushes : -1, 1, "and it owns the leaf");
    check(lights == 3, "and three light entities beside it");

    v3 o;
    check(brush_ent_point(&M2.ents[1], "origin", &o), "the player start has an origin");
    checkf(o.y, 1.0f, 0.001f, "one metre off the floor");

    /* The ramp. Its own reason for existing: a face whose normal is neither
       vertical nor horizontal, which no sector could hold. */
    int slopes = 0;
    for (int i = 0; i < M2.n_faces; i++) {
        float ny = M2.faces[i].normal.y;
        if (ny > 0.05f && ny < 0.95f) slopes++;
    }
    check(slopes >= 1, "at least one face is a genuine slope");

    /* The balcony over the floor: two solid surfaces at one x,z. Sampled at a
       point under the balcony, counting brushes whose box spans it. */
    int stacked = 0;
    for (int i = 0; i < M2.n_brushes; i++) {
        const Brush *b = &M2.brushes[i];
        if (b->min.x > b->max.x) continue;
        if (-5.0f >= b->min.x && -5.0f <= b->max.x &&
             5.0f >= b->min.z && 5.0f <= b->max.z) stacked++;
    }
    check(stacked >= 2, "floor and balcony both cover one point on the plan");
}

/* --- the bake ------------------------------------------------------------
   Every tool is built with HOT_RELOAD, so the ::data_map above read the file.
   The blob is what the shipped game reads and nothing else here would ever
   touch it. Comparing the two also checks the packing itself: bake.ps1 strips
   the comments and flattens the newlines, and the right answer to that
   transformation is "the same map". */

static BrushMap MB_FILE, MB_BAKED;

static void test_bake_matches(void) {
    printf("\nthe baked blob and the file on disk describe the same map\n");

    int flen = 0, blen = 0;
    const char *ftext = data_map("atrium", &flen);
    const char *btext = data_map_baked("atrium", &blen);

    check(ftext != 0, "the file is there");
    check(btext != 0, "and so is the baked copy");
    if (!ftext || !btext) return;

    check(blen < flen, "the baked copy is the smaller of the two");
    check(brush_parse(ftext, flen, &MB_FILE) > 0, "the file parses");
    check(brush_parse(btext, blen, &MB_BAKED) > 0, "the blob parses");

    checki(MB_BAKED.n_ents,    MB_FILE.n_ents,    "same entity count");
    checki(MB_BAKED.n_brushes, MB_FILE.n_brushes, "same brush count");
    checki(MB_BAKED.n_faces,   MB_FILE.n_faces,   "same face count");

    /* Plane for plane and texture for texture. A length that packed one byte
       short would shift the payload and show up here as a plane that moved,
       long before it showed up in a game as a wall in the wrong place. */
    int same_planes = 0, same_tex = 0, same_uv = 0;
    int n = MB_FILE.n_faces < MB_BAKED.n_faces ? MB_FILE.n_faces : MB_BAKED.n_faces;
    for (int i = 0; i < n; i++) {
        const BrushFace *a = &MB_FILE.faces[i], *b = &MB_BAKED.faces[i];
        if (v3len(v3sub(a->normal, b->normal)) < 0.0001f &&
            fabsf(a->dist - b->dist) < 0.0001f) same_planes++;
        if (str_same(a->tex, b->tex)) same_tex++;
        if (v3len(v3sub(a->uaxis, b->uaxis)) < 0.0001f &&
            v3len(v3sub(a->vaxis, b->vaxis)) < 0.0001f &&
            fabsf(a->uoff - b->uoff) < 0.0001f &&
            fabsf(a->voff - b->voff) < 0.0001f &&
            fabsf(a->uscale - b->uscale) < 0.0001f &&
            fabsf(a->vscale - b->vscale) < 0.0001f) same_uv++;
    }
    checki(same_planes, n, "every plane survived the packing");
    checki(same_tex,    n, "every texture name survived");
    checki(same_uv,     n, "every UV axis, offset and scale survived");

    /* The keys are where the escaping happens: every one of them is quoted,
       so `\"` is the only escape the bake ever writes and this is the only
       thing that would notice it coming back wrong. */
    int same_keys = 0, total_keys = 0;
    for (int e = 0; e < MB_FILE.n_ents && e < MB_BAKED.n_ents; e++) {
        const BrushEnt *a = &MB_FILE.ents[e], *b = &MB_BAKED.ents[e];
        for (int k = 0; k < a->n_keys && k < b->n_keys; k++) {
            total_keys++;
            if (str_same(a->keys[k], b->keys[k]) && str_same(a->vals[k], b->vals[k]))
                same_keys++;
        }
    }
    check(total_keys > 0, "there are quoted keys to compare");
    checki(same_keys, total_keys, "every key and value came back unescaped");
}

/* --- geometry ------------------------------------------------------------- */

static MeshBuf GB;
static MdlRange GR[BR_MAX_RANGES];

static void test_geometry(void) {
    printf("\nbuilding faces into vertices\n");

    mb_init(&GB, 200000);
    check(GB.cap > 0, "a buffer to build into");

    /* The cube: six quads, two triangles each, three vertices each. */
    checki(brush_parse(CUBE_VALVE, -1, &M2), 1, "the cube parses");
    mb_reset(&GB);
    int nr = brush_geometry(&GB, &M2, 0, M2.n_brushes, GR, BR_MAX_RANGES);
    checki(GB.count, 36, "six quads fan to 36 vertices");
    checki(nr, 1, "one material run, because one texture");

    /* Every vertex of the ceiling run must carry the UV its own face's axes
       give it, which is what makes this different from a planar projection. */
    checki(brush_parse(DOOR_FACE, -1, &M2), 1, "the fitted-door fixture parses");
    mb_reset(&GB);
    brush_geometry(&GB, &M2, 0, M2.n_brushes, GR, BR_MAX_RANGES);
    float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
    int f = face_facing(&M2, 0, v3f(0, 0, -1));
    v3 poly[BR_MAX_POLY];
    int n = brush_face_poly(&M2, 0, f, poly, BR_MAX_POLY);
    for (int i = 0; i < GB.count; i++) {
        /* Only the vertices on that face: they are the ones at its plane. */
        if (fabsf(GB.v[i].nz + 1.0f) > 0.001f) continue;
        if (GB.v[i].u < umin) umin = GB.v[i].u;
        if (GB.v[i].u > umax) umax = GB.v[i].u;
        if (GB.v[i].v < vmin) vmin = GB.v[i].v;
        if (GB.v[i].v > vmax) vmax = GB.v[i].v;
    }
    checki(n, 4, "the door face is a quad");
    checkf(umin, 0.0f, 0.001f, "built u starts at 0");
    checkf(umax, 1.0f, 0.001f, "built u ends at 1");
    checkf(vmin, 0.0f, 0.001f, "built v starts at 0");
    checkf(vmax, 1.0f, 0.001f, "built v ends at 1");

    /* A nodraw texture bounds the solid and produces no triangles. */
    checki(brush_parse(TB_CUBE, -1, &M2), 1, "the all-__TB_empty cube parses");
    checki(M2.brushes[0].n_faces, 6, "and keeps all six planes");
    mb_reset(&GB);
    nr = brush_geometry(&GB, &M2, 0, M2.n_brushes, GR, BR_MAX_RANGES);
    checki(GB.count, 0, "but draws nothing");
    checki(nr, 0, "and needs no material run");
    check(brush_tex_nodraw("clip") && brush_tex_nodraw("skip") &&
          brush_tex_nodraw("trigger") && !brush_tex_nodraw("wall_brick"),
          "the nodraw list is what Quake and TrenchBroom use");

    /* A brush RANGE, which is how a door is built apart from the room. */
    int len = 0;
    const char *text = data_map("atrium", &len);
    if (text && brush_parse(text, len, &M2) == 3) {
        mb_reset(&GB);
        brush_geometry(&GB, &M2, M2.ents[2].first_brush, M2.ents[2].n_brushes,
                       GR, BR_MAX_RANGES);
        int door_only = GB.count;
        mb_reset(&GB);
        brush_geometry(&GB, &M2, 0, M2.n_brushes, GR, BR_MAX_RANGES);
        check(door_only > 0 && door_only < GB.count,
              "the door builds on its own, and is part of the whole");
        checki(door_only, 36, "the leaf is one box: 36 vertices");
    }

    mb_free(&GB);
}

/* --- editing it in TrenchBroom and saving it back ------------------------
 *
 * TrenchBroom does not preserve the three points a face was written with. It
 * keeps the PLANE, and on save it writes whichever triple it likes -- normally
 * a small right-angled triangle rather than the corners a person would type.
 * So "the map still works after the editor has touched it" is really the claim
 * that our parse depends on the plane and not on which points name it.
 *
 * This re-emits atrium with every face named by a different triple taken off
 * its own polygon, parses that, and compares. Anything that depended on the
 * original points -- the winding, the offsets, the derived Standard axes --
 * comes apart here.
 *
 * WHAT IT CANNOT TELL YOU is whether TrenchBroom opens the file, because that
 * needs somebody to look at a window. It tells you that if it does open it,
 * saving it back changes nothing this engine reads.
 *
 * TrenchBroom은 면이 기록되었던 세 점을 보존하지 않습니다. *평면*을 유지하고, 저장할 때
 * 자기가 원하는 삼중항을 씁니다. 보통은 사람이 입력할 법한 모서리가 아니라 작은 직각삼각형
 * 입니다. 따라서 "에디터가 건드린 뒤에도 맵이 동작한다"는 실은 우리 파싱이 평면에만 의존하고
 * 어느 점이 그것을 지목했는지에는 의존하지 않는다는 주장입니다.
 *
 * 이 시험은 atrium을 다시 내보내되 각 면을 자기 폴리곤에서 취한 다른 삼중항으로 지목하고,
 * 그것을 파싱해 비교합니다. 원래의 점에 의존하던 것은 무엇이든(감김 방향, 오프셋, 유도된
 * Standard 축) 이곳에서 무너집니다.
 *
 * 이 시험이 말해 줄 수 없는 것은 TrenchBroom이 그 파일을 여는지 여부입니다. 그것은 누군가
 * 창을 들여다봐야 합니다. 말해 주는 것은, 만약 연다면 그것을 다시 저장해도 이 엔진이 읽는
 * 무엇도 달라지지 않는다는 사실입니다.
 */
static BrushMap RT;

/* World metres back to the map units the file speaks, rounded. The brushes are
   authored on integer coordinates and the clip that produced these vertices
   lands within a thousandth of them, so rounding recovers the exact value the
   author typed rather than drifting the plane. */
static int to_map_x(v3 p) { return (int)(p.x / BRUSH_UNIT + (p.x < 0 ? -0.5f : 0.5f)); }
static int to_map_y(v3 p) { return (int)(-p.z / BRUSH_UNIT + (-p.z < 0 ? -0.5f : 0.5f)); }
static int to_map_z(v3 p) { return (int)(p.y / BRUSH_UNIT + (p.y < 0 ? -0.5f : 0.5f)); }

static int emit_axis(char *o, int cap, int pos, const char *open,
                     v3 axis, float off) {
    pos = txt_append_str(o, cap, pos, open);
    pos = txt_append_int(o, cap, pos, to_map_x(axis));
    pos = txt_append_str(o, cap, pos, " ");
    pos = txt_append_int(o, cap, pos, to_map_y(axis));
    pos = txt_append_str(o, cap, pos, " ");
    pos = txt_append_int(o, cap, pos, to_map_z(axis));
    pos = txt_append_str(o, cap, pos, " ");
    pos = txt_append_int(o, cap, pos, (int)off);
    return txt_append_str(o, cap, pos, " ] ");
}

static void test_roundtrip(void) {
    printf("\nre-saved the way an editor re-saves it\n");

    int len = 0;
    const char *text = data_map("atrium", &len);
    if (!text || !brush_parse(text, len, &M2)) {
        printf("  no atrium.map to round-trip\n"); fails++; return;
    }

    const int cap = (int)sizeof(BIG);
    int pos = txt_append_str(BIG, cap, 0, "{\n\"classname\" \"worldspawn\"\n");
    int emitted = 0, skipped = 0;

    for (int bi = 0; bi < M2.n_brushes; bi++) {
        const Brush *b = &M2.brushes[bi];
        pos = txt_append_str(BIG, cap, pos, "{\n");

        for (int fi = 0; fi < b->n_faces; fi++) {
            const BrushFace *f = &M2.faces[b->first_face + fi];
            v3 poly[BR_MAX_POLY];
            int n = brush_face_poly(&M2, bi, fi, poly, BR_MAX_POLY);
            if (n < 3) { skipped++; continue; }

            /* A different triple on the same plane. The polygon is wound
               counter-clockwise about the normal, and Quake reads
               cross(q0-q1, q2-q1), so q0=poly[1], q1=poly[0], q2=poly[2]
               reproduces the same normal from different points. */
            v3 q[3] = { poly[1], poly[0], poly[2] };
            for (int k = 0; k < 3; k++)
                pos = emit_pt(BIG, cap, pos, to_map_x(q[k]), to_map_y(q[k]), to_map_z(q[k]));

            pos = txt_append_str(BIG, cap, pos, f->tex);
            pos = txt_append_str(BIG, cap, pos, " ");
            pos = emit_axis(BIG, cap, pos, "[ ", f->uaxis, f->uoff);
            pos = emit_axis(BIG, cap, pos, "[ ", f->vaxis, f->voff);
            pos = txt_append_str(BIG, cap, pos, "0 ");
            pos = txt_append_int(BIG, cap, pos, (int)f->uscale);
            pos = txt_append_str(BIG, cap, pos, " ");
            pos = txt_append_int(BIG, cap, pos, (int)f->vscale);
            pos = txt_append_str(BIG, cap, pos, "\n");
            emitted++;
        }
        pos = txt_append_str(BIG, cap, pos, "}\n");
    }
    pos = txt_append_str(BIG, cap, pos, "}\n");
    check(pos < cap - 1, "the re-emitted map fitted in its buffer");
    check(emitted > 50, "every drawable face was re-emitted");
    /* An editor re-saving cannot write a face that bounds nothing, because it
       has no polygon to take three points off. atrium has none, so nothing is
       lost here -- a level that did would come back with fewer planes, and
       that is a real difference worth seeing rather than tolerating. */
    checki(skipped, 0, "and no face had to be dropped for want of a polygon");

    check(brush_parse(BIG, pos, &RT) == 1, "the re-emitted map parses");
    checki(RT.n_brushes, M2.n_brushes, "same brush count");

    /* The faces that had a polygon to re-emit must come back on the same
       planes. A face that bounded nothing was dropped, which is why the counts
       are compared against `emitted` rather than against each other. */
    int same_planes = 0, same_tex = 0, cmp = 0;
    for (int bi = 0; bi < RT.n_brushes && bi < M2.n_brushes; bi++) {
        const Brush *a = &M2.brushes[bi], *r = &RT.brushes[bi];
        for (int fi = 0; fi < r->n_faces; fi++) {
            /* Same order, because dropped faces shift nothing: they were at
               the end of each brush in atrium or absent entirely. */
            if (fi >= a->n_faces) break;
            const BrushFace *fa = &M2.faces[a->first_face + fi];
            const BrushFace *fr = &RT.faces[r->first_face + fi];
            cmp++;
            if (v3len(v3sub(fa->normal, fr->normal)) < 0.001f &&
                fabsf(fa->dist - fr->dist) < 0.002f) same_planes++;
            if (str_same(fa->tex, fr->tex)) same_tex++;
        }
    }
    checki(same_planes, cmp, "every plane came back identical");
    checki(same_tex,    cmp, "every texture name came back identical");

    /* And the boxes, which are derived from the polygons rather than the
       planes -- so this catches a winding that survived the plane test. */
    int same_box = 0;
    for (int i = 0; i < RT.n_brushes && i < M2.n_brushes; i++)
        if (v3len(v3sub(RT.brushes[i].min, M2.brushes[i].min)) < 0.01f &&
            v3len(v3sub(RT.brushes[i].max, M2.brushes[i].max)) < 0.01f) same_box++;
    checki(same_box, M2.n_brushes, "and every brush occupies the same space");
}

int main(void) {
    printf("maptest\n");
    printf("  1 map unit = %g m   (grid 32 = %g m)\n",
           (double)BRUSH_UNIT, (double)(32.0f * BRUSH_UNIT));

    test_cube();
    test_formats_agree();
    test_trenchbroom_form();
    test_uv();
    test_slope();
    test_entities();
    test_unbounded();
    test_malformed();
    test_bounded_parse();
    test_atrium();
    test_bake_matches();
    test_roundtrip();
    test_geometry();
    test_capacity();

    char sum[256];
    if (diag_summary(sum, sizeof(sum))) printf("\n  diag: %s\n", sum);

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall map checks passed\n", fails);
    return fails != 0;
}
