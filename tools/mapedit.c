/* mapedit -- draw levels.
 *
 * Top half is the plan view you edit in; bottom half flies through the same
 * level in 3D using the game's own geometry and materials. Doom's editor and
 * TrenchBroom both settled on this split for the same reason: a floor plan is
 * where you actually lay out a map, and a 3D view is where you find out it
 * feels wrong.
 *
 * The editor is not shipped, so it is free to be as large as it needs to be.
 *
 * ---- camera ----------------------------------------------------------
 *   W A S D          fly, because that is where the hand already is
 *   SPACE / C        rise / fall           SHIFT  faster
 *   right drag       look (3D) or pan (plan)
 *   wheel            zoom (plan)
 *   F                snap the camera to the player start
 *   arrows, PGUP/PGDN also fly, for the old layout
 *
 * ---- shaping ---------------------------------------------------------
 *   hover            highlights the surface under the cursor (3D)
 *   wheel            raise / lower that surface  (SHIFT: four grid steps)
 *   left drag        floor / ceiling height, or slide a wall along its normal
 *   ctrl+drag wall   move the nearer corner instead of the whole edge
 *   - / =            selected sector's floor down / up (SHIFT: ceiling)
 *
 * Pointing at a surface and scrolling is the whole height workflow: no key,
 * no selection first, and no wondering which of three slots is about to move.
 *
 * ---- plan view -------------------------------------------------------
 *   left drag        move the vertex or sector under the cursor
 *   left drag empty  rubber-band select sectors
 *   ctrl+click edge  insert a vertex
 *   N                new sector at the cursor
 *   CTRL+D           duplicate            DEL  delete
 *   V                delete the vertex under the cursor
 *   TAB / SHIFT+TAB  cycle selected sector
 *   [ ]              grid size             G  snap on/off
 *   E                place the current entity kind at the cursor
 *   R                cycle entity kind     P  move the player start here
 *
 * ---- materials -------------------------------------------------------
 *   palette          click a swatch, or drag one onto a surface in 3D
 *   M                cycle the material of the last surface clicked
 *
 * Picking uses level_edge_spans(), the same function the geometry builder
 * uses, so what you click is exactly what you can see. Two copies of that
 * rule would drift and you would find yourself grabbing nothing.
 *
 * ---- file ------------------------------------------------------------
 *   CTRL+Z / CTRL+Y  undo / redo
 *   CTRL+S           save to assets/levels.txt
 *   ESC              quit
 *
 * `mapedit <level> -print` writes what a save would produce and exits, so the
 * parse/serialise round trip is testable with no window and no mouse.
 *
 * `mapedit <level> -new <x> <z>` drops a sector at those centimetre
 * coordinates and prints the result. What a new sector inherits depends on
 * where it lands, and aiming a cursor at a map coordinate from a test script
 * is fiddly enough that aiming it wrong looks exactly like the code being
 * wrong -- which is how an hour went into a bug that was not there.
 */

#include "../src/level.h"
#include "../src/model.h"   /* MdlRange by value -- level.h only forward-declares it */
#include "../src/font.h"
#include "../src/tex.h"
#include "../src/data.h"
#include "../src/txt.h"

#include <stdio.h>
#include <string.h>

#define SPLIT      0.62f     /* fraction of the window given to the plan */
#define PANEL      210       /* left panel width, pixels */
#define MAX_UNDO   64
#define MAX_MATS   32
#define PICK_PX    10.0f

static int   g_running = 1;
static HWND  g_wnd;
static Level g_level;
static char  g_level_name[32] = "arena";

/* ------------------------------------------------------------------ state */

static float g_cam_x, g_cam_z;        /* plan centre, world units */
static float g_upp = 0.06f;           /* world units per pixel */
static int   g_grid = 50;             /* snap, in file units (cm) */
static int   g_snap = 1;

static int   g_sel = 0;               /* selected sector */
static int   g_sel_vert = -1;         /* selected vertex within it */
static int   g_drag_vert, g_drag_sector, g_panning;
static POINT g_drag_prev;
static float g_drag_ox, g_drag_oz;    /* cursor-to-sector offset while dragging */

static int   g_ent_kind = 0;
static const char *ENT_KINDS[] = { "imp", "brute", "hound", "caster",
                                   "ammo", "health", "exit" };
#define N_ENT_KINDS ((int)(sizeof(ENT_KINDS) / sizeof(ENT_KINDS[0])))

static char  g_status[160] = "ready";
static float g_status_age;
static float g_time;              /* seconds since start, for undo coalescing */

/* 3D fly camera */
static v3    g_eye = {0, 1.7f, 12.0f};
static float g_fly_yaw, g_fly_pitch;
static int   g_looking;
static POINT g_look_prev;

/* ---- 3D picking ---------------------------------------------------------
   What the cursor is over in the 3D view, and what a drag started on. */
enum { SURF_NONE, SURF_FLOOR, SURF_CEIL, SURF_WALL };

typedef struct {
    int   kind;        /* SURF_* */
    int   sector;
    int   edge;        /* SURF_WALL only */
    float t;           /* distance along the ray */
    v3    point;       /* where it was hit */
    v3    normal;
} Pick;

static Pick g_hover;                  /* refreshed every frame */
static Pick g_grab;                   /* what the current drag started on */
static int  g_drag3d;                 /* a 3D drag is in progress */
static int  g_drag_corner;            /* wall drag moves one corner, not both */
static v3   g_drag_anchor;            /* where the drag began, world space */
static short g_drag_start_pts[LVL_MAX_PTS * 2];
static short g_drag_start_floor, g_drag_start_ceil;

static Pick g_last_click;             /* which surface `M` retextures */

/* geometry, rebuilt whenever the level changes */
static Mesh     g_mesh3d, g_lines, g_quads, g_text;
static MeshBuf  g_line_buf, g_quad_buf, g_mesh_buf, g_text_buf;
static MdlRange g_ranges[LVL_MAX_RANGES];
static Mat      g_range_tex[LVL_MAX_RANGES];
static int      g_range_count;
static int      g_dirty = 1;

/* ------------------------------------------------------------------ undo */

/* Whole-level snapshots. A Level is a few kilobytes and edits are made by
   hand at human speed, so there is nothing to gain from a command log and a
   great deal to lose in bugs. */
static Level g_undo[MAX_UNDO];
static int   g_undo_n, g_undo_at;

static void snapshot(void) {
    if (g_undo_at < MAX_UNDO) {
        g_undo[g_undo_at++] = g_level;
        g_undo_n = g_undo_at;
    } else {
        for (int i = 1; i < MAX_UNDO; i++) g_undo[i - 1] = g_undo[i];
        g_undo[MAX_UNDO - 1] = g_level;
        g_undo_n = g_undo_at = MAX_UNDO;
    }
}

static void status(const char *fmt, int a, int b);
static void status_s(const char *fmt, const char *s);

static void undo(void) {
    if (g_undo_at <= 1) { status("nothing to undo", 0, 0); return; }
    g_undo_at--;
    g_level = g_undo[g_undo_at - 1];
    g_dirty = 1;
    status("undo (%d left)", g_undo_at - 1, 0);
}

static void redo(void) {
    if (g_undo_at >= g_undo_n) { status("nothing to redo", 0, 0); return; }
    g_level = g_undo[g_undo_at];
    g_undo_at++;
    g_dirty = 1;
    status("redo", 0, 0);
}

/* ----------------------------------------------------------------- utils */

static void status(const char *fmt, int a, int b) {
    wsprintfA(g_status, fmt, a, b);
    g_status_age = 0.0f;
}

/* Separate from status() because a pointer cannot ride in an int parameter --
   %s and %d need different signatures on 64-bit. */
static void status_s(const char *fmt, const char *s) {
    wsprintfA(g_status, fmt, s);
    g_status_age = 0.0f;
}

static int same_str(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return !*a && !*b;
}

static void set_str(char *dst, int cap, const char *src) {
    int i = 0;
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int plan_h(int h) { return (int)(h * SPLIT); }

/* Plan view maps world x to screen x and world z to screen y (down). */
static void screen_to_world(int sx, int sy, int w, int h, float *wx, float *wz) {
    int ph = plan_h(h);
    float vw = (float)(w - PANEL);
    *wx = g_cam_x + (sx - PANEL - vw * 0.5f) * g_upp;
    *wz = g_cam_z + (sy - ph * 0.5f) * g_upp;
}

static void world_to_screen(float wx, float wz, int w, int h, float *sx, float *sy) {
    int ph = plan_h(h);
    float vw = (float)(w - PANEL);
    *sx = PANEL + vw * 0.5f + (wx - g_cam_x) / g_upp;
    *sy = ph * 0.5f + (wz - g_cam_z) / g_upp;
}

static short snap(float world) {
    int v = (int)(world * 100.0f + (world < 0 ? -0.5f : 0.5f));
    if (!g_snap || g_grid <= 0) return (short)v;
    int g = g_grid;
    int r = (v >= 0) ? (v + g / 2) / g : (v - g / 2) / g;
    return (short)(r * g);
}

static Sector *sel(void) {
    if (g_level.n_sectors == 0) return 0;
    if (g_sel >= g_level.n_sectors) g_sel = g_level.n_sectors - 1;
    if (g_sel < 0) g_sel = 0;
    return &g_level.sectors[g_sel];
}

/* ------------------------------------------------------------- materials */

/* Read from the recipe text so a new material becomes selectable without the
   editor being rebuilt. */
static int material_names(char out[][16], int max) {
    const char *p = data_text(DATA_RECIPES);
    int n = 0;
    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;
        if (!txt_is(t, len, "t")) continue;
        const char *nm = txt_token(p, &len);
        if (!nm) break;
        p = nm + len;
        if (n >= max) continue;
        int i = 0;
        for (; i < len && i < 15; i++) out[n][i] = nm[i];
        out[n][i] = 0;
        n++;
    }
    return n;
}

/* The palette: every material in the library, with the pixels or the shader it
   actually draws with. Built once per load rather than per frame because a
   pixel material costs a 256x256 generate. */
static char g_pal_name[MAX_MATS][16];
static Mat  g_pal_mat [MAX_MATS];
static int  g_pal_n;

static void palette_build(void) {
    g_pal_n = material_names(g_pal_name, MAX_MATS);
    for (int i = 0; i < g_pal_n; i++) g_pal_mat[i] = tex_mat(g_pal_name[i]);
}

static void set_material(int which, const char *name) {
    Sector *s = sel();
    if (!s) return;
    char *field = which == 0 ? s->mat_floor
                : which == 1 ? s->mat_wall : s->mat_ceil;
    snapshot();
    set_str(field, LVL_MAT, name);
    g_dirty = 1;
}

static void cycle_material(int which, int dir) {
    Sector *s = sel();
    if (!s || !g_pal_n) return;

    const char *field = which == 0 ? s->mat_floor
                      : which == 1 ? s->mat_wall : s->mat_ceil;

    int at = 0;
    for (int i = 0; i < g_pal_n; i++) if (same_str(g_pal_name[i], field)) { at = i; break; }
    at = (at + dir + g_pal_n) % g_pal_n;

    set_material(which, g_pal_name[at]);
    status_s("-> %s", g_pal_name[at]);
}

/* ------------------------------------------------------------- selection */

/* Nearest vertex within PICK_PX, across all sectors. */
static int pick_vertex(int mx, int my, int w, int h, int *out_sector) {
    float best = PICK_PX * PICK_PX;
    int found = -1;
    for (int si = 0; si < g_level.n_sectors; si++) {
        Sector *s = &g_level.sectors[si];
        for (int i = 0; i < s->n; i++) {
            float px, py;
            world_to_screen(s->pts[i*2] * 0.01f, s->pts[i*2+1] * 0.01f, w, h, &px, &py);
            float dx = px - mx, dy = py - my;
            float d = dx*dx + dy*dy;
            if (d < best) { best = d; found = i; *out_sector = si; }
        }
    }
    return found;
}

static int point_in(const Sector *s, float x, float z) {
    int inside = 0;
    for (int i = 0, j = s->n - 1; i < s->n; j = i++) {
        float xi = s->pts[i*2] * 0.01f, zi = s->pts[i*2+1] * 0.01f;
        float xj = s->pts[j*2] * 0.01f, zj = s->pts[j*2+1] * 0.01f;
        if ((zi > z) == (zj > z)) continue;
        if (x < (xj - xi) * (z - zi) / (zj - zi) + xi) inside = !inside;
    }
    return inside;
}

/* Last sector containing the point, matching the engine's last-wins rule so
   clicking selects whatever you are actually looking at. */
static int pick_sector(float x, float z) {
    int found = -1;
    for (int i = 0; i < g_level.n_sectors; i++)
        if (point_in(&g_level.sectors[i], x, z)) found = i;
    return found;
}

static int ctrl_down(void);
static int shift_down(void);

/* ------------------------------------------------------------ 3D picking */

static void view_basis(v3 *fwd, v3 *right, v3 *up) {
    float cy = cosf(g_fly_yaw), sy = sinf(g_fly_yaw);
    float cp = cosf(g_fly_pitch), sp = sinf(g_fly_pitch);
    *fwd   = v3f(-sy * cp, sp, -cy * cp);
    *right = v3f(cy, 0, -sy);
    *up    = v3cross(*right, *fwd);
}

/* Ray through the cursor. Built from the camera basis and the fov rather than
   by inverting the view-projection: fewer moving parts, and it cannot fall
   out of step with what draw_3d() set up. */
static int cursor_ray(int mx, int my, int w, int h, v3 *o, v3 *d) {
    int ph = plan_h(h), vh = h - ph;
    int y = my - ph;
    if (y < 0 || y >= vh || vh < 2) return 0;

    float ndc_x = 2.0f * mx / (float)w - 1.0f;
    float ndc_y = 1.0f - 2.0f * y / (float)vh;
    float t = tanf(1.5708f * 0.5f);
    float aspect = (float)w / (float)vh;

    v3 fwd, right, up;
    view_basis(&fwd, &right, &up);
    *o = g_eye;
    *d = v3norm(v3add(fwd, v3add(v3scale(right, ndc_x * t * aspect),
                                 v3scale(up,    ndc_y * t))));
    return 1;
}

static void try_hit(Pick *best, int kind, int sector, int edge,
                    float t, v3 point, v3 normal) {
    if (t <= 0.01f) return;
    if (best->kind != SURF_NONE && t >= best->t) return;
    best->kind = kind; best->sector = sector; best->edge = edge;
    best->t = t; best->point = point; best->normal = normal;
}

static Pick pick3d(v3 o, v3 d) {
    Pick best = {0};
    best.kind = SURF_NONE;

    for (int si = 0; si < g_level.n_sectors; si++) {
        Sector *s = &g_level.sectors[si];

        /* Floor and ceiling: a horizontal plane, then a containment test. */
        for (int c = 0; c < 2; c++) {
            float y = (c ? s->ceil : s->floor) * 0.01f;
            if (fabsf(d.y) < 1e-6f) continue;
            float t = (y - o.y) / d.y;
            if (t <= 0.01f) continue;
            v3 p = v3add(o, v3scale(d, t));
            if (!point_in(s, p.x, p.z)) continue;
            try_hit(&best, c ? SURF_CEIL : SURF_FLOOR, si, -1, t, p,
                    v3f(0, c ? -1.0f : 1.0f, 0));
        }

        /* Walls: the same spans the geometry builder emits. */
        for (int e = 0; e < s->n; e++) {
            EdgeSpan sp[LVL_MAX_SPANS];
            int n = level_edge_spans(&g_level, si, e, sp, LVL_MAX_SPANS);
            if (!n) continue;

            int j = (e + 1) % s->n;
            float ax = s->pts[e*2] * 0.01f, az = s->pts[e*2+1] * 0.01f;
            float bx = s->pts[j*2] * 0.01f, bz = s->pts[j*2+1] * 0.01f;
            v3 nrm = level_edge_normal(&g_level, si, e);

            /* Intersect the vertical plane the edge lies in. */
            float denom = d.x * nrm.x + d.z * nrm.z;
            if (fabsf(denom) < 1e-6f) continue;
            float t = ((ax - o.x) * nrm.x + (az - o.z) * nrm.z) / denom;
            if (t <= 0.01f) continue;

            v3 p = v3add(o, v3scale(d, t));

            /* Inside the edge's extent? */
            float ex = bx - ax, ez = bz - az;
            float len2 = ex*ex + ez*ez;
            if (len2 < 1e-9f) continue;
            float u = ((p.x - ax) * ex + (p.z - az) * ez) / len2;
            if (u < 0.0f || u > 1.0f) continue;

            /* u locates the hit along the edge, so a span covering only
               part of it is only clickable over that part -- otherwise you
               grab a wall by pointing at the gap beside it. */
            for (int k = 0; k < n; k++) {
                if (u < sp[k].t0 || u > sp[k].t1) continue;
                if (p.y < sp[k].y0 || p.y > sp[k].y1) continue;
                try_hit(&best, SURF_WALL, si, e, t, p,
                        sp[k].face_out ? nrm : v3scale(nrm, -1.0f));
            }
        }
    }
    return best;
}

/* Height the cursor is pointing at, for a vertical drag.
 *
 * The ray is intersected with an upright plane through the anchor that faces
 * the camera, and the height of that hit is taken. That gives a floor drag
 * the feel of holding the surface, rather than an arbitrary pixels-to-metres
 * constant -- and unlike a ray/line closest-approach it is three lines and
 * hard to get wrong. */
static float ray_height_at(v3 o, v3 d, v3 anchor) {
    v3 fwd, right, up;
    view_basis(&fwd, &right, &up);

    v3 n = v3norm(v3f(fwd.x, 0.0f, fwd.z));      /* upright, facing the camera */
    float den = v3dot(d, n);
    if (fabsf(den) < 1e-4f) return anchor.y;     /* looking straight down it */

    float t = v3dot(v3sub(anchor, o), n) / den;
    if (t <= 0.01f) return anchor.y;
    return o.y + d.y * t;
}

/* Where the cursor ray meets the horizontal plane at height y. */
static int ray_on_plane(v3 o, v3 d, float y, v3 *out) {
    if (fabsf(d.y) < 1e-6f) return 0;
    float t = (y - o.y) / d.y;
    if (t <= 0.01f) return 0;
    *out = v3add(o, v3scale(d, t));
    return 1;
}

/* --------------------------------------------------------------- editing */

static void new_sector(float x, float z) {
    if (g_level.n_sectors >= LVL_MAX_SECTORS) { status("sector limit", 0, 0); return; }
    snapshot();

    /* Inherit from whatever is under the cursor. A hardcoded ceiling was a
       reported bug: dropping a box into a room 6m tall gave the box a 3m
       ceiling, so the 3m of solid above it was built as walls and the box
       came with a pillar standing on top of it.

       Falling back to the selected sector rather than to fixed numbers means
       a sector started in empty space still matches the level it belongs to. */
    int under = level_sector_at(&g_level, x, z);
    const Sector *from = under >= 0            ? &g_level.sectors[under]
                       : g_level.n_sectors > 0 ? &g_level.sectors[g_sel] : 0;

    short floor = 0, ceil = 300;
    char mf[LVL_MAT] = "brick", mw[LVL_MAT] = "brick", mc[LVL_MAT] = "brick";
    if (from) {
        ceil = from->ceil;
        set_str(mf, LVL_MAT, from->mat_floor);
        set_str(mw, LVL_MAT, from->mat_wall);
        set_str(mc, LVL_MAT, from->mat_ceil);
        /* One grid step proud of the floor it sits on, so a box dropped into
           a room is visibly a box instead of being flush with the ground. In
           open space it is a new room, so it keeps the floor height. */
        floor = (short)(from->floor + (under >= 0 ? g_grid : 0));
        if (floor > ceil - 10) floor = (short)(ceil - 10);
    }

    Sector *s = &g_level.sectors[g_level.n_sectors++];
    short cx = snap(x), cz = snap(z), r = (short)(g_grid * 4);
    short pts[8] = { (short)(cx-r), (short)(cz-r), (short)(cx+r), (short)(cz-r),
                     (short)(cx+r), (short)(cz+r), (short)(cx-r), (short)(cz+r) };
    for (int i = 0; i < 8; i++) s->pts[i] = pts[i];
    s->n = 4;
    s->floor = floor;
    s->ceil  = ceil;
    set_str(s->mat_floor, LVL_MAT, mf);
    set_str(s->mat_wall,  LVL_MAT, mw);
    set_str(s->mat_ceil,  LVL_MAT, mc);

    g_sel = g_level.n_sectors - 1;
    g_sel_vert = -1;
    g_dirty = 1;
    status("new sector %d  floor %d", g_sel, s->floor);
}

static void delete_sector(void) {
    if (g_level.n_sectors <= 1) { status("cannot delete the last sector", 0, 0); return; }
    snapshot();
    for (int i = g_sel; i < g_level.n_sectors - 1; i++)
        g_level.sectors[i] = g_level.sectors[i + 1];
    g_level.n_sectors--;
    if (g_sel >= g_level.n_sectors) g_sel = g_level.n_sectors - 1;
    g_sel_vert = -1;
    g_dirty = 1;
    status("deleted sector, %d left", g_level.n_sectors, 0);
}

static void duplicate_sector(void) {
    Sector *s = sel();
    if (!s || g_level.n_sectors >= LVL_MAX_SECTORS) return;
    snapshot();
    Sector copy = *s;
    for (int i = 0; i < copy.n; i++) copy.pts[i*2] += (short)(g_grid * 2);
    g_level.sectors[g_level.n_sectors++] = copy;
    g_sel = g_level.n_sectors - 1;
    g_dirty = 1;
    status("duplicated to sector %d", g_sel, 0);
}

/* Splits the edge nearest the cursor. Adding to the end of the list instead
   would fold the polygon over itself the moment you insert anywhere but the
   tail. */
static void insert_vertex(float x, float z) {
    Sector *s = sel();
    if (!s || s->n >= LVL_MAX_PTS) return;

    int best = 0;
    float best_d = 1e30f;
    for (int i = 0; i < s->n; i++) {
        int j = (i + 1) % s->n;
        float ax = s->pts[i*2] * 0.01f, az = s->pts[i*2+1] * 0.01f;
        float bx = s->pts[j*2] * 0.01f, bz = s->pts[j*2+1] * 0.01f;
        float ex = bx - ax, ez = bz - az;
        float len2 = ex*ex + ez*ez;
        float t = len2 > 0 ? ((x-ax)*ex + (z-az)*ez) / len2 : 0.0f;
        t = clampf(t, 0.0f, 1.0f);
        float dx = ax + ex*t - x, dz = az + ez*t - z;
        float d = dx*dx + dz*dz;
        if (d < best_d) { best_d = d; best = i; }
    }

    snapshot();
    int at = best + 1;
    for (int i = s->n; i > at; i--) {
        s->pts[i*2]   = s->pts[(i-1)*2];
        s->pts[i*2+1] = s->pts[(i-1)*2+1];
    }
    s->pts[at*2]   = snap(x);
    s->pts[at*2+1] = snap(z);
    s->n++;
    g_sel_vert = at;
    g_dirty = 1;
    status("inserted vertex %d of %d", at, s->n);
}

static void delete_vertex(void) {
    Sector *s = sel();
    if (!s || g_sel_vert < 0 || s->n <= 3) { status("need at least 3 vertices", 0, 0); return; }
    snapshot();
    for (int i = g_sel_vert; i < s->n - 1; i++) {
        s->pts[i*2]   = s->pts[(i+1)*2];
        s->pts[i*2+1] = s->pts[(i+1)*2+1];
    }
    s->n--;
    if (g_sel_vert >= s->n) g_sel_vert = s->n - 1;
    g_dirty = 1;
    status("deleted vertex, %d left", s->n, 0);
}

/* A run of wheel notches or a held key is one edit, not twenty. Without this,
   raising a floor by a metre costs twenty presses of CTRL+Z to take back and
   fills the undo stack with steps nobody wants. */
static int nudge_is_new(int key) {
    static float at = -100.0f;
    static int   last = -1;
    int fresh = key != last || g_time - at > 0.7f;
    last = key;
    at = g_time;
    return fresh;
}

static void nudge_height(int ceiling, int delta) {
    Sector *s = sel();
    if (!s) return;
    if (nudge_is_new(g_sel * 2 + ceiling)) snapshot();
    if (ceiling) {
        s->ceil = (short)(s->ceil + delta);
        if (s->ceil < s->floor + 10) s->ceil = (short)(s->floor + 10);
        status("sector %d ceiling %d", g_sel, s->ceil);
    } else {
        s->floor = (short)(s->floor + delta);
        if (s->floor > s->ceil - 10) s->floor = (short)(s->ceil - 10);
        status("sector %d floor %d", g_sel, s->floor);
    }
    g_dirty = 1;
}

static void place_entity(float x, float z) {
    if (g_level.n_ents >= LVL_MAX_ENTS) { status("entity limit", 0, 0); return; }
    snapshot();
    Entity *e = &g_level.ents[g_level.n_ents++];
    set_str(e->kind, LVL_MAT, ENT_KINDS[g_ent_kind]);
    e->x = snap(x);
    e->z = snap(z);
    status("placed %s (%d total)", 0, g_level.n_ents);
    wsprintfA(g_status, "placed %s  (%d total)", ENT_KINDS[g_ent_kind], g_level.n_ents);
}

/* ------------------------------------------------------------- 3D dragging */

static void begin_drag3d(v3 o, v3 d) {
    g_grab = g_hover;
    if (g_grab.kind == SURF_NONE) return;

    snapshot();
    g_sel = g_grab.sector;
    g_sel_vert = -1;
    g_last_click = g_grab;
    g_drag3d = 1;
    g_drag_anchor = g_grab.point;

    Sector *s = &g_level.sectors[g_grab.sector];
    g_drag_start_floor = s->floor;
    g_drag_start_ceil  = s->ceil;
    for (int i = 0; i < s->n * 2; i++) g_drag_start_pts[i] = s->pts[i];

    /* Grabbing a wall near one of its ends moves that corner, if asked. */
    g_drag_corner = -1;
    if (g_grab.kind == SURF_WALL && ctrl_down()) {
        int e = g_grab.edge, j = (e + 1) % s->n;
        float ax = s->pts[e*2] * 0.01f, az = s->pts[e*2+1] * 0.01f;
        float bx = s->pts[j*2] * 0.01f, bz = s->pts[j*2+1] * 0.01f;
        float da = (g_grab.point.x-ax)*(g_grab.point.x-ax) + (g_grab.point.z-az)*(g_grab.point.z-az);
        float db = (g_grab.point.x-bx)*(g_grab.point.x-bx) + (g_grab.point.z-bz)*(g_grab.point.z-bz);
        g_drag_corner = (da < db) ? e : j;
    }

    (void)o; (void)d;
}

static void update_drag3d(v3 o, v3 d) {
    if (!g_drag3d || g_grab.kind == SURF_NONE) return;
    Sector *s = &g_level.sectors[g_grab.sector];

    if (g_grab.kind == SURF_FLOOR || g_grab.kind == SURF_CEIL) {
        float y = ray_height_at(o, d, g_drag_anchor);
        short v = snap(y);
        if (g_grab.kind == SURF_FLOOR) {
            if (v > s->ceil - 10) v = (short)(s->ceil - 10);
            s->floor = v;
        } else {
            if (v < s->floor + 10) v = (short)(s->floor + 10);
            s->ceil = v;
        }
        status(g_grab.kind == SURF_FLOOR ? "floor %d" : "ceiling %d",
               g_grab.kind == SURF_FLOOR ? s->floor : s->ceil, 0);
        g_dirty = 1;
        return;
    }

    /* Wall: follow the cursor across the horizontal plane it was grabbed on. */
    v3 p;
    if (!ray_on_plane(o, d, g_drag_anchor.y, &p)) return;

    if (g_drag_corner >= 0 && g_drag_corner < s->n) {
        s->pts[g_drag_corner*2]     = snap(p.x);
        s->pts[g_drag_corner*2 + 1] = snap(p.z);
        status("corner %d moved", g_drag_corner, 0);
    } else {
        /* Slide along the edge's own normal only, so a wall stays straight
           and keeps its length. */
        v3 n = level_edge_normal(&g_level, g_grab.sector, g_grab.edge);
        float along = (p.x - g_drag_anchor.x) * n.x + (p.z - g_drag_anchor.z) * n.z;

        int e = g_grab.edge, j = (e + 1) % s->n;
        float ax = g_drag_start_pts[e*2] * 0.01f + n.x * along;
        float az = g_drag_start_pts[e*2+1] * 0.01f + n.z * along;
        float bx = g_drag_start_pts[j*2] * 0.01f + n.x * along;
        float bz = g_drag_start_pts[j*2+1] * 0.01f + n.z * along;

        s->pts[e*2]   = snap(ax); s->pts[e*2+1] = snap(az);
        s->pts[j*2]   = snap(bx); s->pts[j*2+1] = snap(bz);
        status("wall moved %d", (int)(along * 100.0f), 0);
    }
    g_dirty = 1;
}

/* Cycles the material of whichever surface was last clicked in 3D, so you
   retexture by pointing at a thing rather than by remembering which of the
   three slots it lives in. */
static void cycle_picked_material(int dir) {
    if (g_last_click.kind == SURF_NONE) { status("click a surface first", 0, 0); return; }
    g_sel = g_last_click.sector;
    int which = g_last_click.kind == SURF_FLOOR ? 0
              : g_last_click.kind == SURF_CEIL  ? 2 : 1;
    cycle_material(which, dir);
}

/* ---------------------------------------------------------------- saving */

static void asset_path(char *out, int cap) {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(0, exe, MAX_PATH);
    while (n > 0 && exe[n-1] != '\\') n--;
    if (n > 1) n--;
    while (n > 0 && exe[n-1] != '\\') n--;
    int i = 0;
    for (; i < (int)n && i < cap - 1; i++) out[i] = exe[i];
    const char *rel = "assets\\levels.txt";
    for (int k = 0; rel[k] && i < cap - 1; k++) out[i++] = rel[k];
    out[i] = 0;
}

static int append(char *buf, int at, int cap, const char *s) {
    for (int i = 0; s[i] && at < cap - 1; i++) buf[at++] = s[i];
    buf[at] = 0;
    return at;
}

static int append_int(char *buf, int at, int cap, int v) {
    char tmp[16];
    wsprintfA(tmp, "%d", v);
    return append(buf, at, cap, tmp);
}

static int serialise(char *buf, int cap) {
    int at = 0;
    at = append(buf, at, cap, "l ");
    at = append(buf, at, cap, g_level_name);
    at = append(buf, at, cap, "\nstart ");
    for (int i = 0; i < 3; i++) {
        at = append_int(buf, at, cap, g_level.start[i]);
        at = append(buf, at, cap, i < 2 ? " " : "\n");
    }

    /* Preserve the exit's destination through a round trip -- there is no UI
       for it yet, but dropping it on save would silently break progression. */
    if (g_level.next[0]) {
        at = append(buf, at, cap, "next ");
        at = append(buf, at, cap, g_level.next);
        at = append(buf, at, cap, "\n");
    }

    for (int i = 0; i < g_level.n_sectors; i++) {
        const Sector *s = &g_level.sectors[i];
        at = append(buf, at, cap, "\ns\nfloor ");
        at = append_int(buf, at, cap, s->floor);
        at = append(buf, at, cap, "  ceil ");
        at = append_int(buf, at, cap, s->ceil);
        at = append(buf, at, cap, "\nmat floor ");
        at = append(buf, at, cap, s->mat_floor);
        at = append(buf, at, cap, "  wall ");
        at = append(buf, at, cap, s->mat_wall);
        at = append(buf, at, cap, "  ceil ");
        at = append(buf, at, cap, s->mat_ceil);
        at = append(buf, at, cap, "\np");
        for (int k = 0; k < s->n; k++) {
            at = append(buf, at, cap, k % 4 == 0 ? "\n   " : "   ");
            at = append_int(buf, at, cap, s->pts[k*2]);
            at = append(buf, at, cap, " ");
            at = append_int(buf, at, cap, s->pts[k*2+1]);
        }
        at = append(buf, at, cap, "\n");
    }

    if (g_level.n_ents) at = append(buf, at, cap, "\n");
    for (int i = 0; i < g_level.n_ents; i++) {
        at = append(buf, at, cap, "e ");
        at = append(buf, at, cap, g_level.ents[i].kind);
        at = append(buf, at, cap, " ");
        at = append_int(buf, at, cap, g_level.ents[i].x);
        at = append(buf, at, cap, " ");
        at = append_int(buf, at, cap, g_level.ents[i].z);
        at = append(buf, at, cap, "\n");
    }
    return at;
}

/* Replaces only this level's block, so the file's documentation header and
   any other levels survive. */
static int save(void) {
    if (!data_from_file(DATA_LEVELS)) {
        status("REFUSING TO SAVE: not reading assets/levels.txt", 0, 0);
        printf("refusing to save: the level came from the baked copy.\n");
        fflush(stdout);
        return 0;
    }

    char path[MAX_PATH];
    asset_path(path, MAX_PATH);

    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE) { status("save: cannot open the file", 0, 0); return 0; }

    DWORD size = GetFileSize(f, 0), got = 0;
    char *old = HeapAlloc(GetProcessHeap(), 0, size + 1);
    ReadFile(f, old, size, &got, 0);
    old[got] = 0;
    CloseHandle(f);

    int start = -1, end = (int)got;
    for (int i = 0; i < (int)got; i++) {
        int bol = (i == 0) || old[i-1] == '\n';
        if (!bol || old[i] != 'l' || old[i+1] != ' ') continue;
        int k = i + 2;
        while (old[k] == ' ') k++;
        int j = 0;
        while (g_level_name[j] && old[k+j] == g_level_name[j]) j++;
        int ends = old[k+j] == '\n' || old[k+j] == '\r' || old[k+j] == ' ';
        if (start < 0 && !g_level_name[j] && ends) { start = i; continue; }
        if (start >= 0) { end = i; break; }
    }
    if (start < 0) { start = (int)got; end = (int)got; }

    int cap = size + 65536;
    char *out = HeapAlloc(GetProcessHeap(), 0, cap);
    int at = 0;
    for (int i = 0; i < start && at < cap - 1; i++) out[at++] = old[i];
    out[at] = 0;
    at += serialise(out + at, cap - at);
    at = append(out, at, cap, "\n");
    for (int i = end; i < (int)got && at < cap - 1; i++) out[at++] = old[i];

    f = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    int ok = 0;
    if (f != INVALID_HANDLE_VALUE) {
        DWORD wrote;
        ok = WriteFile(f, out, at, &wrote, 0) && wrote == (DWORD)at;
        CloseHandle(f);
    }
    HeapFree(GetProcessHeap(), 0, old);
    HeapFree(GetProcessHeap(), 0, out);

    status(ok ? "SAVED %d bytes" : "SAVE FAILED", at, 0);
    printf("%s -> %s\n", ok ? "saved" : "FAILED", path);
    fflush(stdout);
    return ok;
}

/* --------------------------------------------------------------- drawing */

static void line2(MeshBuf *b, float x0, float y0, float x1, float y1) {
    mb_line(b, v3f(x0, y0, 0), v3f(x1, y1, 0));
}

static void rebuild(void) {
    mb_reset(&g_mesh_buf);
    g_range_count = level_geometry(&g_mesh_buf, &g_level, g_ranges, LVL_MAX_RANGES);
    mesh_upload(&g_mesh3d, &g_mesh_buf, 1);
    for (int i = 0; i < g_range_count; i++)
        g_range_tex[i] = tex_mat(g_ranges[i].mat);
    /* Cheap after the first call -- tex_mat is cached -- so it can ride along
       with the geometry rebuild rather than needing its own trigger. */
    palette_build();
    g_dirty = 0;
}

/* Pixel-space projection shared by the plan view and every text overlay. */
static mat4 screen_mvp(int w, int h) {
    return mat4_ortho(0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f);
}

static void draw_text(int w, int h, float x, float y, float size,
                      float r, float g, float bl, const char *s) {
    mb_reset(&g_text_buf);
    font_text(&g_text_buf, x, y, size, s);
    mesh_upload(&g_text, &g_text_buf, 1);

    rd_mvp(screen_mvp(w, h));
    rd_mode(RD_TEXT);
    rd_color(r, g, bl, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture());
    mesh_draw(&g_text);
}

static void draw_plan(int w, int h) {
    int ph = plan_h(h);
    glViewport(0, h - ph, w, ph);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Work in pixels: the grid, the picking radius and the text all want the
       same units, and converting once here is cheaper than everywhere else. */
    mat4 proj = mat4_ortho(0.0f, (float)w, (float)ph, 0.0f, -1.0f, 1.0f);
    rd_mvp(proj);
    rd_mode(RD_FLAT);

    float sx, sy;

    /* --- grid --- */
    mb_reset(&g_line_buf);
    float step = g_grid * 0.01f;
    if (step / g_upp >= 4.0f) {              /* skip when it would be a smear */
        float wx0, wz0, wx1, wz1;
        screen_to_world(PANEL, 0, w, h, &wx0, &wz0);
        screen_to_world(w, ph, w, h, &wx1, &wz1);
        for (float x = floorf(wx0 / step) * step; x <= wx1; x += step) {
            world_to_screen(x, 0, w, h, &sx, &sy);
            line2(&g_line_buf, sx, 0, sx, (float)ph);
        }
        for (float z = floorf(wz0 / step) * step; z <= wz1; z += step) {
            world_to_screen(0, z, w, h, &sx, &sy);
            line2(&g_line_buf, (float)PANEL, sy, (float)w, sy);
        }
        mesh_upload(&g_lines, &g_line_buf, 1);
        rd_color(1, 1, 1, 0.05f);
        glLineWidth(1.0f);
        mesh_draw_lines(&g_lines);
    }

    /* --- axes --- */
    mb_reset(&g_line_buf);
    world_to_screen(0, 0, w, h, &sx, &sy);
    line2(&g_line_buf, (float)PANEL, sy, (float)w, sy);
    line2(&g_line_buf, sx, 0, sx, (float)ph);
    mesh_upload(&g_lines, &g_line_buf, 1);
    rd_color(0.35f, 0.55f, 1.0f, 0.30f);
    mesh_draw_lines(&g_lines);

    /* --- sector outlines: unselected first, selected on top --- */
    for (int pass = 0; pass < 2; pass++) {
        mb_reset(&g_line_buf);
        for (int si = 0; si < g_level.n_sectors; si++) {
            if ((si == g_sel) != (pass == 1)) continue;
            Sector *s = &g_level.sectors[si];
            for (int i = 0; i < s->n; i++) {
                int j = (i + 1) % s->n;
                float ax, ay, bx, by;
                world_to_screen(s->pts[i*2]*0.01f, s->pts[i*2+1]*0.01f, w, h, &ax, &ay);
                world_to_screen(s->pts[j*2]*0.01f, s->pts[j*2+1]*0.01f, w, h, &bx, &by);
                line2(&g_line_buf, ax, ay, bx, by);
            }
        }
        if (!g_line_buf.count) continue;
        mesh_upload(&g_lines, &g_line_buf, 1);
        if (pass == 0) { rd_color(0.45f, 0.52f, 0.60f, 0.85f); glLineWidth(1.5f); }
        else           { rd_color(1.00f, 0.78f, 0.30f, 1.00f); glLineWidth(2.5f); }
        mesh_draw_lines(&g_lines);
    }

    /* --- vertices of the selected sector --- */
    Sector *s = sel();
    if (s) {
        mb_reset(&g_quad_buf);
        for (int i = 0; i < s->n; i++) {
            if (i == g_sel_vert) continue;
            world_to_screen(s->pts[i*2]*0.01f, s->pts[i*2+1]*0.01f, w, h, &sx, &sy);
            mb_billboard(&g_quad_buf, v3f(sx, sy, 0), v3f(1,0,0), v3f(0,1,0), 7, 7);
        }
        if (g_quad_buf.count) {
            mesh_upload(&g_quads, &g_quad_buf, 1);
            rd_color(1.0f, 0.85f, 0.45f, 1.0f);
            mesh_draw(&g_quads);
        }
        if (g_sel_vert >= 0 && g_sel_vert < s->n) {
            mb_reset(&g_quad_buf);
            world_to_screen(s->pts[g_sel_vert*2]*0.01f, s->pts[g_sel_vert*2+1]*0.01f,
                            w, h, &sx, &sy);
            mb_billboard(&g_quad_buf, v3f(sx, sy, 0), v3f(1,0,0), v3f(0,1,0), 11, 11);
            mesh_upload(&g_quads, &g_quad_buf, 1);
            rd_color(0.3f, 1.0f, 0.5f, 1.0f);
            mesh_draw(&g_quads);
        }
    }

    /* --- entities and the player start --- */
    mb_reset(&g_line_buf);
    for (int i = 0; i < g_level.n_ents; i++) {
        world_to_screen(g_level.ents[i].x*0.01f, g_level.ents[i].z*0.01f, w, h, &sx, &sy);
        line2(&g_line_buf, sx-5, sy, sx+5, sy);
        line2(&g_line_buf, sx, sy-5, sx, sy+5);
    }
    if (g_line_buf.count) {
        mesh_upload(&g_lines, &g_line_buf, 1);
        rd_color(0.4f, 1.0f, 0.9f, 0.9f);
        glLineWidth(2.0f);
        mesh_draw_lines(&g_lines);
    }

    mb_reset(&g_line_buf);
    world_to_screen(g_level.start[0]*0.01f, g_level.start[1]*0.01f, w, h, &sx, &sy);
    line2(&g_line_buf, sx-9, sy-9, sx+9, sy+9);
    line2(&g_line_buf, sx-9, sy+9, sx+9, sy-9);
    mesh_upload(&g_lines, &g_line_buf, 1);
    rd_color(1.0f, 0.4f, 0.4f, 1.0f);
    glLineWidth(2.5f);
    mesh_draw_lines(&g_lines);

    /* --- per-sector height labels --- */
    for (int si = 0; si < g_level.n_sectors; si++) {
        Sector *sc = &g_level.sectors[si];
        float cx = 0, cz = 0;
        for (int i = 0; i < sc->n; i++) { cx += sc->pts[i*2]; cz += sc->pts[i*2+1]; }
        cx = cx / sc->n * 0.01f; cz = cz / sc->n * 0.01f;
        world_to_screen(cx, cz, w, h, &sx, &sy);
        if (sx < PANEL || sx > w || sy < 0 || sy > ph) continue;
        char lbl[32];
        wsprintfA(lbl, "%d..%d", sc->floor, sc->ceil);
        float bright = (si == g_sel) ? 1.0f : 0.55f;
        draw_text(w, h, sx - font_width(1.0f, lbl) * 0.5f, sy - 4, 1.0f,
                  bright, bright * 0.95f, bright * 0.8f, lbl);
        /* draw_text leaves its own viewport/projection; restore the plan's. */
        glViewport(0, h - ph, w, ph);
        rd_mvp(proj);
        rd_mode(RD_FLAT);
    }

    glDisable(GL_BLEND);
}

static void draw_3d(int w, int h) {
    int ph = plan_h(h), vh = h - ph;
    if (vh < 2) return;
    glViewport(0, 0, w, vh);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glScissor(0, 0, w, vh);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    mat4 proj = mat4_perspective(1.5708f, (float)w / (float)vh, 0.05f, 200.0f);
    mat4 view = mat4_fps_view(g_eye, g_fly_yaw, g_fly_pitch);
    rd_mvp(mat4_mul(proj, view));
    rd_mode(RD_WORLD);
    rd_eye(g_eye);
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < g_range_count; i++) {
        tex_use(&g_range_tex[i]);
        mesh_draw_range(&g_mesh3d, g_ranges[i].first, g_ranges[i].count);
    }

    /* --- outline whatever the cursor is over, so it is obvious what a drag
           would grab before you commit to it --- */
    const Pick *hp = g_drag3d ? &g_grab : &g_hover;
    if (hp->kind == SURF_NONE) return;

    const Sector *s = &g_level.sectors[hp->sector];
    mb_reset(&g_line_buf);

    if (hp->kind == SURF_FLOOR || hp->kind == SURF_CEIL) {
        float y = (hp->kind == SURF_CEIL ? s->ceil : s->floor) * 0.01f;
        for (int i = 0; i < s->n; i++) {
            int j = (i + 1) % s->n;
            mb_line(&g_line_buf,
                    v3f(s->pts[i*2]*0.01f, y, s->pts[i*2+1]*0.01f),
                    v3f(s->pts[j*2]*0.01f, y, s->pts[j*2+1]*0.01f));
        }
    } else {
        EdgeSpan sp[LVL_MAX_SPANS];
        int n = level_edge_spans(&g_level, hp->sector, hp->edge, sp, LVL_MAX_SPANS);
        int e = hp->edge, j = (e + 1) % s->n;
        float ox = s->pts[e*2]*0.01f, oz = s->pts[e*2+1]*0.01f;
        float dx = s->pts[j*2]*0.01f - ox, dz = s->pts[j*2+1]*0.01f - oz;
        /* Outline each piece separately: an edge that a platform covers part
           of is several rectangles, and drawing one box round the lot would
           claim geometry that is not there. */
        for (int k = 0; k < n; k++) {
            float ax = ox + dx * sp[k].t0, az = oz + dz * sp[k].t0;
            float bx = ox + dx * sp[k].t1, bz = oz + dz * sp[k].t1;
            float y0 = sp[k].y0, y1 = sp[k].y1;
            mb_line(&g_line_buf, v3f(ax,y0,az), v3f(bx,y0,bz));
            mb_line(&g_line_buf, v3f(ax,y1,az), v3f(bx,y1,bz));
            mb_line(&g_line_buf, v3f(ax,y0,az), v3f(ax,y1,az));
            mb_line(&g_line_buf, v3f(bx,y0,bz), v3f(bx,y1,bz));
        }
    }

    if (g_line_buf.count) {
        mesh_upload(&g_lines, &g_line_buf, 1);
        rd_mode(RD_FLAT);
        /* Drawn through the geometry: an outline you cannot see because the
           wall you are pointing at is in the way defeats the purpose. */
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(2.5f);
        if (g_drag3d) rd_color(0.30f, 1.00f, 0.50f, 1.0f);
        else          rd_color(1.00f, 0.80f, 0.25f, 0.9f);
        mesh_draw_lines(&g_lines);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }
}

/* ---------------------------------------------------------- the palette */

/* Laid out here and hit-tested from the same numbers, so a swatch cannot drift
   away from the region that responds to a click on it. */
#define SW_X    12.0f
#define SW_SZ   17.0f
#define SW_STEP 20.0f

static float g_pal_top;       /* panel y of the first swatch, set while drawing */
static int   g_pal_drag = -1; /* swatch being dragged onto a surface, or -1 */
static int   g_pal_mx, g_pal_my;  /* cursor, for the swatch that follows it */

/* An axis-aligned quad with explicit UVs. mb_quad derives UVs from the
   position, which in pixel space would slide the pattern around with the
   layout; a swatch has to show the same corner of the material every time. */
static void quad_uv(MeshBuf *b, float x, float y, float sz, float uv) {
    v3 n = v3f(0, 0, 1);
    mb_vtx(b, v3f(x,      y,      0), n, 0.0f, 0.0f);
    mb_vtx(b, v3f(x + sz, y,      0), n, uv,   0.0f);
    mb_vtx(b, v3f(x + sz, y + sz, 0), n, uv,   uv);
    mb_vtx(b, v3f(x,      y,      0), n, 0.0f, 0.0f);
    mb_vtx(b, v3f(x + sz, y + sz, 0), n, uv,   uv);
    mb_vtx(b, v3f(x,      y + sz, 0), n, 0.0f, uv);
}

/* Returns the y to carry on drawing at. */
static float draw_palette(int w, int h, float y) {
    if (!g_pal_n) return y;

    draw_text(w, h, 10, y, 1.0f, 0.70f, 0.72f, 0.78f, "MATERIALS  (click / drag)");
    y += 15.0f;
    g_pal_top = y;

    /* One buffer, one upload, one draw call per material: the swatches differ
       only by which material is bound. */
    mb_reset(&g_quad_buf);
    for (int i = 0; i < g_pal_n; i++)
        quad_uv(&g_quad_buf, SW_X, y + i * SW_STEP, SW_SZ, 3.0f);
    mesh_upload(&g_quads, &g_quad_buf, 1);

    rd_mvp(screen_mvp(w, h));
    rd_mode(RD_SWATCH);
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < g_pal_n; i++) {
        tex_use(&g_pal_mat[i]);
        mesh_draw_range(&g_quads, i * 6, 6);
    }
    /* Leave the shader off so nothing drawn later inherits a procedural
       surface -- RD_FLAT and RD_TEXT ignore it, but the 3D pass does not. */
    rd_proc(PROC_TEXTURE, 0, 0.0f, 0);

    Sector *s = sel();
    for (int i = 0; i < g_pal_n; i++) {
        /* A dot marks the material already on the surface a click would
           retexture, which is the one question the list has to answer. */
        int on = 0;
        if (s) {
            const char *cur = g_last_click.kind == SURF_FLOOR ? s->mat_floor
                            : g_last_click.kind == SURF_CEIL  ? s->mat_ceil
                            : s->mat_wall;
            on = same_str(cur, g_pal_name[i]);
        }
        draw_text(w, h, SW_X + SW_SZ + 7.0f, y + i * SW_STEP + 5.0f, 1.0f,
                  on ? 1.00f : 0.62f, on ? 0.84f : 0.65f, on ? 0.35f : 0.70f,
                  g_pal_name[i]);
    }
    return y + g_pal_n * SW_STEP + 6.0f;
}

/* Which swatch the cursor is over, or -1. */
static int palette_hit(int mx, int my) {
    if (mx < (int)SW_X || mx > PANEL - 6) return -1;
    float rel = my - g_pal_top;
    if (rel < 0.0f) return -1;
    int i = (int)(rel / SW_STEP);
    if (i >= g_pal_n || rel - i * SW_STEP > SW_SZ) return -1;
    return i;
}

/* Puts palette entry `i` onto surface `t`. A SURF_NONE target falls back to
   the walls of the selected sector, so the palette still does something before
   anything has been picked -- that is how a brand new sector gets dressed. */
static void palette_apply(int i, const Pick *t) {
    if (i < 0 || i >= g_pal_n) return;
    int which = 1;
    if (t->kind != SURF_NONE) {
        g_sel = t->sector;
        which = t->kind == SURF_FLOOR ? 0 : t->kind == SURF_CEIL ? 2 : 1;
    }
    set_material(which, g_pal_name[i]);
    status_s("-> %s", g_pal_name[i]);
}

static void draw_panel(int w, int h) {
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* Panel background. */
    mb_reset(&g_quad_buf);
    mb_billboard(&g_quad_buf, v3f(PANEL * 0.5f, h * 0.5f, 0),
                 v3f(1,0,0), v3f(0,1,0), (float)PANEL, (float)h);
    mesh_upload(&g_quads, &g_quad_buf, 1);
    rd_mvp(screen_mvp(w, h));
    rd_mode(RD_FLAT);
    rd_color(0.07f, 0.08f, 0.10f, 0.94f);
    mesh_draw(&g_quads);

    Sector *s = sel();
    char line[96];
    float y = 10.0f;
    const float LH = 13.0f;

    draw_text(w, h, 10, y, 1.5f, 1.0f, 0.85f, 0.35f, g_level_name); y += 20;

    wsprintfA(line, "sectors %d/%d", g_level.n_sectors, LVL_MAX_SECTORS);
    draw_text(w, h, 10, y, 1.0f, 0.75f, 0.78f, 0.82f, line); y += LH;
    wsprintfA(line, "entities %d", g_level.n_ents);
    draw_text(w, h, 10, y, 1.0f, 0.75f, 0.78f, 0.82f, line); y += LH + 6;

    if (s) {
        wsprintfA(line, "SECTOR %d", g_sel);
        draw_text(w, h, 10, y, 1.2f, 1.0f, 0.78f, 0.30f, line); y += 16;
        wsprintfA(line, "verts  %d", s->n);
        draw_text(w, h, 10, y, 1.0f, 0.80f, 0.84f, 0.88f, line); y += LH;
        wsprintfA(line, "floor  %d   (wheel)", s->floor);
        draw_text(w, h, 10, y, 1.0f, 0.80f, 0.84f, 0.88f, line); y += LH;
        wsprintfA(line, "ceil   %d   (wheel)", s->ceil);
        draw_text(w, h, 10, y, 1.0f, 0.80f, 0.84f, 0.88f, line); y += LH + 4;

        wsprintfA(line, "floor %s", s->mat_floor);
        draw_text(w, h, 10, y, 1.0f, 0.70f, 0.90f, 0.70f, line); y += LH;
        wsprintfA(line, "wall  %s", s->mat_wall);
        draw_text(w, h, 10, y, 1.0f, 0.70f, 0.90f, 0.70f, line); y += LH;
        wsprintfA(line, "ceil  %s", s->mat_ceil);
        draw_text(w, h, 10, y, 1.0f, 0.70f, 0.90f, 0.70f, line); y += LH + 6;
    }

    y = draw_palette(w, h, y);

    wsprintfA(line, "grid %d %s", g_grid, g_snap ? "SNAP" : "free");
    draw_text(w, h, 10, y, 1.0f, 0.75f, 0.78f, 0.82f, line); y += LH;
    wsprintfA(line, "entity: %s  (R)", ENT_KINDS[g_ent_kind]);
    draw_text(w, h, 10, y, 1.0f, 0.40f, 0.95f, 0.90f, line); y += LH;
    wsprintfA(line, "undo %d/%d", g_undo_at, g_undo_n);
    draw_text(w, h, 10, y, 1.0f, 0.75f, 0.78f, 0.82f, line); y += LH;

    {
        static const char *KIND[] = { "-", "floor", "ceil", "wall" };
        const Pick *hp = g_drag3d ? &g_grab : &g_hover;
        if (hp->kind == SURF_NONE) wsprintfA(line, "3D: -");
        else wsprintfA(line, "3D: s%d %s", hp->sector, KIND[hp->kind]);
        draw_text(w, h, 10, y, 1.0f, 1.0f, 0.80f, 0.30f, line);
    }
    y += LH + 8;

    static const char *HELP[] = {
        "WASD   fly   SPACE/C up/dn",
        "SHIFT  faster  RMB look",
        "F      go to start",
        "",
        "drag   move vert/sector",
        "^drag  insert vertex",
        "wheel  raise surface (3D)",
        "       zoom (plan)",
        "- =    floor  (SHIFT ceil)",
        "",
        "N new  ^D dup   DEL del",
        "V      delete vertex",
        "TAB    next sector",
        "E ent  R kind  P start",
        "[ ]    grid    G snap",
        "M      next material",
        "",
        "^Z ^Y undo   ^S save",
    };
    for (int i = 0; i < (int)(sizeof(HELP)/sizeof(HELP[0])); i++) {
        draw_text(w, h, 10, y, 1.0f, 0.45f, 0.48f, 0.54f, HELP[i]);
        y += 11;
    }

    /* Status pinned to the bottom, where it does not move as the panel grows. */
    float fade = g_status_age > 4.0f ? 0.35f : 1.0f;
    draw_text(w, h, 10, h - 18.0f, 1.0f, fade, fade * 0.9f, fade * 0.5f, g_status);

    /* The swatch in flight. Drawn last, and over the whole window rather than
       just the panel, because the point of dragging it is to hold it against
       the surface you are about to drop it on. */
    if (g_pal_drag >= 0) {
        mb_reset(&g_quad_buf);
        quad_uv(&g_quad_buf, g_pal_mx - 15.0f, g_pal_my - 15.0f, 30.0f, 3.0f);
        mesh_upload(&g_quads, &g_quad_buf, 1);
        rd_mvp(screen_mvp(w, h));
        rd_mode(RD_SWATCH);
        glActiveTexture(GL_TEXTURE0);
        tex_use(&g_pal_mat[g_pal_drag]);
        mesh_draw(&g_quads);
        rd_proc(PROC_TEXTURE, 0, 0.0f, 0);
        draw_text(w, h, g_pal_mx + 20.0f, g_pal_my - 4.0f, 1.0f,
                  1.0f, 0.85f, 0.35f, g_pal_name[g_pal_drag]);
    }

    glDisable(GL_BLEND);
}

/* ------------------------------------------------------------------ input */

static int ctrl_down(void)  { return GetKeyState(VK_CONTROL) < 0; }
static int shift_down(void) { return GetKeyState(VK_SHIFT)   < 0; }


static void frame_camera_on_start(void) {
    g_eye = v3f(g_level.start[0] * 0.01f, 1.7f, g_level.start[1] * 0.01f);
    g_fly_yaw = g_level.start[2] * 0.0000174533f;
    g_fly_pitch = 0.0f;
}

static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    RECT rc; GetClientRect(wnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (h < 2) h = 2;
    int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
    int in_plan = my < plan_h(h) && mx >= PANEL;

    switch (msg) {
    case WM_CLOSE: case WM_DESTROY:
        g_running = 0; return 0;

    case WM_LBUTTONDOWN: {
        /* The panel gets first refusal: without this a click on it falls
           through to the 3D pick, which reads a ray from off-viewport coords.
           Nothing is applied yet -- a press on a swatch may turn into a drag
           onto a surface, and that is only known at release. */
        if (mx < PANEL) {
            g_pal_drag = palette_hit(mx, my);
            g_pal_mx = mx; g_pal_my = my;
            if (g_pal_drag >= 0) SetCapture(wnd);
            return 0;
        }
        if (!in_plan) {
            v3 o, d;
            if (cursor_ray(mx, my, w, h, &o, &d)) {
                g_hover = pick3d(o, d);
                begin_drag3d(o, d);
                SetCapture(wnd);
            }
            return 0;
        }
        float wx, wz; screen_to_world(mx, my, w, h, &wx, &wz);

        if (ctrl_down()) { insert_vertex(wx, wz); g_drag_vert = 1; SetCapture(wnd); return 0; }

        int si;
        int vi = pick_vertex(mx, my, w, h, &si);
        if (vi >= 0) {
            g_sel = si; g_sel_vert = vi; g_drag_vert = 1;
            snapshot();
        } else {
            int hit = pick_sector(wx, wz);
            if (hit >= 0) {
                g_sel = hit; g_sel_vert = -1; g_drag_sector = 1;
                g_drag_ox = wx; g_drag_oz = wz;
                snapshot();
            } else {
                g_sel_vert = -1;
            }
        }
        SetCapture(wnd);
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_pal_drag >= 0) {
            /* Dropped on a surface: that surface. Released back over the
               panel: it was a click, so use whatever was last picked in 3D. */
            Pick target = g_hover;
            if (mx < PANEL) target = g_last_click;
            palette_apply(g_pal_drag, &target);
            g_pal_drag = -1;
            ReleaseCapture();
            return 0;
        }
        g_drag_vert = g_drag_sector = g_drag3d = 0;
        ReleaseCapture(); return 0;

    case WM_RBUTTONDOWN:
        GetCursorPos(&g_drag_prev);
        g_look_prev = g_drag_prev;
        if (in_plan) g_panning = 1; else g_looking = 1;
        SetCapture(wnd);
        return 0;
    case WM_RBUTTONUP:
        g_panning = g_looking = 0; ReleaseCapture(); return 0;

    case WM_MOUSEMOVE: {
        /* Track what the cursor is over in 3D whenever it is down there and
           nothing else has the mouse. The panel is neither view: without the
           mx test a ray gets cast from off-viewport coordinates and the hover
           readout reports a surface the cursor is nowhere near. */
        g_pal_mx = mx; g_pal_my = my;
        /* While a swatch is in flight the only thing that matters is what is
           under it, so keep hovering even though the mouse is captured. */
        if (g_pal_drag >= 0) {
            v3 o, d;
            if (mx >= PANEL && !in_plan && cursor_ray(mx, my, w, h, &o, &d))
                g_hover = pick3d(o, d);
            else
                g_hover.kind = SURF_NONE;
            return 0;
        }
        if (mx >= PANEL && !in_plan && !g_panning && !g_drag_vert && !g_drag_sector) {
            v3 o, d;
            if (cursor_ray(mx, my, w, h, &o, &d)) {
                if (g_drag3d) update_drag3d(o, d);
                else          g_hover = pick3d(o, d);
            }
        } else if (in_plan && !g_drag3d) {
            g_hover.kind = SURF_NONE;
        }

        if (g_drag_vert) {
            Sector *s = sel();
            if (s && g_sel_vert >= 0) {
                float wx, wz; screen_to_world(mx, my, w, h, &wx, &wz);
                s->pts[g_sel_vert*2]   = snap(wx);
                s->pts[g_sel_vert*2+1] = snap(wz);
                g_dirty = 1;
            }
        } else if (g_drag_sector) {
            Sector *s = sel();
            if (s) {
                float wx, wz; screen_to_world(mx, my, w, h, &wx, &wz);
                short dx = (short)(snap(wx) - snap(g_drag_ox));
                short dz = (short)(snap(wz) - snap(g_drag_oz));
                if (dx || dz) {
                    for (int i = 0; i < s->n; i++) {
                        s->pts[i*2]   = (short)(s->pts[i*2] + dx);
                        s->pts[i*2+1] = (short)(s->pts[i*2+1] + dz);
                    }
                    g_drag_ox = wx; g_drag_oz = wz;
                    g_dirty = 1;
                }
            }
        }
        if (g_panning) {
            POINT c; GetCursorPos(&c);
            g_cam_x -= (c.x - g_drag_prev.x) * g_upp;
            g_cam_z -= (c.y - g_drag_prev.y) * g_upp;
            g_drag_prev = c;
        }
        if (g_looking) {
            POINT c; GetCursorPos(&c);
            g_fly_yaw   -= (c.x - g_look_prev.x) * 0.005f;
            g_fly_pitch -= (c.y - g_look_prev.y) * 0.005f;
            g_fly_pitch = clampf(g_fly_pitch, -1.5f, 1.5f);
            g_look_prev = c;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        /* The wheel arrives in screen coordinates, unlike every other mouse
           message here. */
        POINT c = {mx, my};
        ScreenToClient(wnd, &c);
        int up = GET_WHEEL_DELTA_WPARAM(wp) > 0;

        if (c.y < plan_h(h) && c.x >= PANEL) {
            g_upp = clampf(g_upp * (up ? 0.85f : 1.18f), 0.004f, 1.2f);
            return 0;
        }
        /* In 3D the wheel raises whatever is under the cursor. Point at a
           floor and scroll: no key, no selection, no wondering which of the
           three height slots you are about to move. */
        if (c.x >= PANEL && g_hover.kind != SURF_NONE) {
            int step = g_grid * (shift_down() ? 4 : 1);
            g_sel = g_hover.sector;
            g_sel_vert = -1;
            if (g_hover.kind == SURF_CEIL) nudge_height(1, up ? step : -step);
            else                           nudge_height(0, up ? step : -step);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        float wx = 0, wz = 0;
        POINT c; GetCursorPos(&c); ScreenToClient(wnd, &c);
        screen_to_world(c.x, c.y, w, h, &wx, &wz);

        switch (wp) {
        case VK_ESCAPE: g_running = 0; break;
        case 'Z': if (ctrl_down()) undo(); break;
        case 'Y': if (ctrl_down()) redo(); break;
        case 'S': if (ctrl_down()) save(); break;
        case 'N': new_sector(wx, wz); break;
        case 'D': if (ctrl_down()) duplicate_sector(); break;
        case 'V': delete_vertex(); break;

        /* Heights sit next to the grid keys rather than on WASD, which the
           camera now needs. Shift moves the ceiling instead of the floor. */
        case VK_OEM_MINUS: nudge_height(shift_down(), -g_grid); break;
        case VK_OEM_PLUS:  nudge_height(shift_down(),  g_grid); break;
        case VK_DELETE: delete_sector(); break;
        case VK_TAB:
            if (g_level.n_sectors)
                g_sel = (g_sel + (shift_down() ? -1 : 1) + g_level.n_sectors)
                        % g_level.n_sectors;
            g_sel_vert = -1;
            status("sector %d of %d", g_sel, g_level.n_sectors);
            break;
        case 'E': place_entity(wx, wz); g_dirty = 1; break;
        case 'R': g_ent_kind = (g_ent_kind + 1) % N_ENT_KINDS;
                  wsprintfA(g_status, "entity kind: %s", ENT_KINDS[g_ent_kind]);
                  break;
        case 'P': snapshot();
                  g_level.start[0] = snap(wx);
                  g_level.start[1] = snap(wz);
                  status("player start moved", 0, 0);
                  break;
        case 'G': g_snap = !g_snap; status(g_snap ? "snap on" : "snap off", 0, 0); break;
        case VK_OEM_4: if (g_grid > 5)   g_grid /= 2; status("grid %d", g_grid, 0); break;
        case VK_OEM_6: if (g_grid < 800) g_grid *= 2; status("grid %d", g_grid, 0); break;
        case 'F': frame_camera_on_start(); break;
        case 'M': cycle_picked_material(shift_down() ? -1 : 1); break;
        }
        return 0;
    }
    }
    return DefWindowProcA(wnd, msg, wp, lp);
}

/* ------------------------------------------------------------------- main */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;

    int print_only = 0, new_at = 0;
    if (cmd && *cmd) {
        int i = 0;
        while (cmd[i] && cmd[i] != ' ' && i < (int)sizeof(g_level_name) - 1) {
            g_level_name[i] = cmd[i]; i++;
        }
        g_level_name[i] = 0;
        for (int k = 0; cmd[k]; k++) {
            if (cmd[k] == '-' && cmd[k+1] == 'p') print_only = 1;
            /* -new X Z drops a sector at those centimetre coordinates and
               prints the result. The GUI path to new_sector runs through a
               cursor position that is a nuisance to aim from a test script,
               and aiming it wrong looks exactly like the code being wrong. */
            if (cmd[k] == '-' && cmd[k+1] == 'n') {
                int e = k;
                while (cmd[e] && cmd[e] != ' ') e++;   /* past the flag word */
                new_at = e;
                print_only = 1;
            }
        }
    }

    if (print_only) {
        if (!level_load(g_level_name, &g_level)) {
            printf("no level '%s'\n", g_level_name);
            return 1;
        }
        if (new_at) {
            int nx, nz, ok;
            const char *p = txt_read_int(cmd + new_at, &nx, &ok);
            if (ok) p = txt_read_int(p, &nz, &ok);
            if (!ok) { printf("usage: -new <x> <z>  (centimetres)\n"); return 1; }
            new_sector(nx * 0.01f, nz * 0.01f);
            printf("# new sector %d: floor %d ceil %d  %s/%s/%s\n",
                   g_sel, g_level.sectors[g_sel].floor, g_level.sectors[g_sel].ceil,
                   g_level.sectors[g_sel].mat_floor, g_level.sectors[g_sel].mat_wall,
                   g_level.sectors[g_sel].mat_ceil);
        }
        static char buf[65536];
        serialise(buf, sizeof(buf));
        fputs(buf, stdout);
        return 0;
    }

    if (!gl_bootstrap(inst)) {
        MessageBoxA(0, "OpenGL 3.3 unavailable.", "mapedit", MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc = {0};
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorA(0, IDC_CROSS);
    wc.lpszClassName = "mapedit";
    RegisterClassA(&wc);

    RECT r = {0, 0, 1500, 900};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_wnd = CreateWindowExA(0, "mapedit", "mapedit", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
    HDC dc = GetDC(g_wnd);
    if (!gl_make_context(dc)) {
        MessageBoxA(0, "No 3.3 core context.", "mapedit", MB_ICONERROR);
        return 1;
    }
    gl_set_vsync(1);
    ShowWindow(g_wnd, show);

    rd_init();
    font_init();

    if (!level_load(g_level_name, &g_level)) {
        printf("no level '%s' -- starting an empty one\n", g_level_name);
        g_level.n_sectors = 0;
        g_level.n_ents = 0;
        g_level.start[0] = g_level.start[1] = g_level.start[2] = 0;
        new_sector(0, 0);
    }
    snapshot();                       /* base state, so the first undo works */
    frame_camera_on_start();

    mb_init(&g_line_buf, 65536);
    mb_init(&g_quad_buf, 8192);
    mb_init(&g_mesh_buf, 65536);
    mb_init(&g_text_buf, 8192);

    printf("mapedit -- '%s': %d sectors, %d entities\n"
           "  reading %s\n",
           g_level_name, g_level.n_sectors, g_level.n_ents,
           data_from_file(DATA_LEVELS) ? "assets/levels.txt" : "THE BAKED COPY (saves disabled)");
    fflush(stdout);

    glClearColor(0.10f, 0.11f, 0.14f, 1.0f);

    LARGE_INTEGER freq, prev_t;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev_t);

    while (g_running) {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        if (!g_running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev_t.QuadPart)
                         / (double)freq.QuadPart);
        prev_t = now;
        if (dt > 0.1f) dt = 0.1f;
        g_status_age += dt;
        g_time += dt;

        /* Fly the 3D camera. Held keys are polled rather than handled as
           messages so movement is smooth and framerate independent. */
        /* WASD, because every 3D editor and every game uses it and the other
           hand is on the mouse. The arrows still work for anyone who learnt
           the old layout. Ctrl suppresses it so CTRL+S and CTRL+D do not fly
           the camera while they save and duplicate. */
        if (GetKeyState(VK_CONTROL) >= 0) {
            float sp = (GetKeyState(VK_SHIFT) < 0 ? 18.0f : 7.0f) * dt;
            float cy = cosf(g_fly_yaw), sy = sinf(g_fly_yaw);
            v3 fwd = v3f(-sy, 0, -cy), right = v3f(cy, 0, -sy);
            int f = GetKeyState('W') < 0 || GetKeyState(VK_UP)    < 0;
            int b = GetKeyState('S') < 0 || GetKeyState(VK_DOWN)  < 0;
            int r = GetKeyState('D') < 0 || GetKeyState(VK_RIGHT) < 0;
            int l = GetKeyState('A') < 0 || GetKeyState(VK_LEFT)  < 0;
            int u = GetKeyState(VK_SPACE) < 0 || GetKeyState(VK_PRIOR) < 0;
            int d = GetKeyState('C') < 0 || GetKeyState(VK_NEXT)  < 0;
            if (f) g_eye = v3add(g_eye, v3scale(fwd,   sp));
            if (b) g_eye = v3add(g_eye, v3scale(fwd,  -sp));
            if (r) g_eye = v3add(g_eye, v3scale(right, sp));
            if (l) g_eye = v3add(g_eye, v3scale(right,-sp));
            if (u) g_eye.y += sp;
            if (d) g_eye.y -= sp;
        }

        if (g_dirty) rebuild();

        RECT cr; GetClientRect(g_wnd, &cr);
        int w = cr.right - cr.left, h = cr.bottom - cr.top;
        if (w < 2) w = 2;
        if (h < 2) h = 2;

        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        draw_3d(w, h);
        draw_plan(w, h);
        draw_panel(w, h);
        SwapBuffers(dc);

        char title[192];
        wsprintfA(title, "mapedit [%s]  %d sectors  %d ents  grid %d  %d verts",
                  g_level_name, g_level.n_sectors, g_level.n_ents, g_grid,
                  g_mesh_buf.count);
        SetWindowTextA(g_wnd, title);
    }
    return 0;
}
