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
 *   W A S D          fly ALONG THE VIEW -- look up and go up
 *   SPACE / C        straight up / down, whatever you are facing
 *   SHIFT            faster
 *   right drag       look (3D) or pan (plan)
 *   wheel            zoom (plan)
 *   F                snap the camera to the player start
 *   arrows, PGUP/PGDN also fly, for the old layout
 *
 * Forward follows the camera's pitch, the way every 3D editor flies: you get
 * above a room by looking up and going there, not by holding a separate key.
 * SPACE/C stay for the case that genuinely wants a world axis -- rising up a
 * wall without looking away from it. Strafing is always level.
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
#include "ui.h"             /* the inspector's widgets */

#include <stdio.h>
#include <string.h>

/* --- the layout, dragged rather than compiled in ------------------------
 *
 * ENGLISH
 * -------
 * These were `#define`s, which made the split between the plan, the 3D view and
 * the inspector a property of the build. It is not: it is a property of what
 * you are doing right now. Laying out a floor plan wants the plan tall;
 * dressing surfaces with materials wants the 3D view tall; neither wants the
 * other's proportions, and rebuilding to change one is not an option a person
 * takes -- they just work in the wrong-sized view.
 *
 * Kept as macros over variables rather than renamed, so the eighteen call sites
 * that read PANEL still read PANEL. Renaming them would have been a diff across
 * the whole file to say nothing new.
 *
 * 한국어
 * ------
 * 이 값들은 `#define`이었고, 그래서 평면도·3D 뷰·인스펙터 사이의 분할이 *빌드*의 속성이
 * 되었습니다. 그것은 빌드의 속성이 아니라 지금 무엇을 하고 있는지의 속성입니다. 평면도를
 * 배치할 때는 평면도가 높아야 하고, 재질로 표면을 꾸밀 때는 3D 뷰가 높아야 합니다. 어느
 * 쪽도 상대의 비율을 원하지 않으며, 그것을 바꾸려고 다시 빌드하는 사람은 없습니다. 그냥
 * 잘못된 크기의 뷰에서 작업할 뿐입니다.
 *
 * 이름을 바꾸지 않고 변수 위에 매크로를 씌워 두었으므로, PANEL을 읽던 열여덟 곳이 그대로
 * PANEL을 읽습니다. 이름을 바꾸는 것은 새로운 내용 없이 파일 전체를 건드리는 diff가
 * 되었을 것입니다. */

/** @brief Inspector width in pixels. Dragged by the splitter. / 인스펙터 너비 (픽셀). 분할선으로 조절합니다. */
static int   g_panel_w = 256;
/** @brief Fraction of the window given to the plan view. / 창에서 평면도가 차지하는 비율. */
static float g_split   = 0.62f;

#define PANEL  g_panel_w
#define SPLIT  g_split

#define PANEL_MIN   180   ///< @brief Narrowest the inspector may be dragged. / 인스펙터를 줄일 수 있는 최소 너비.
#define SPLIT_GRAB    5   ///< @brief Half-width of a splitter's grab zone, pixels. / 분할선을 잡을 수 있는 영역의 절반 폭 (픽셀).
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

/* ------------------------------------------------------------- inspector */

/** @brief The widget layer's state. One per tool. / 위젯 레이어의 상태. 도구당 하나입니다. */
static Ui      g_ui;

/**
 * @brief Input accumulated by ::wnd_proc, consumed once per frame.
 *
 * ENGLISH
 * -------
 * Edges (`click`, `release`, `ch`, ...) are set here as the messages arrive and
 * cleared after the frame has drawn, so a click that lands between two frames
 * is still delivered rather than lost. Positions are overwritten freely; only
 * the edges need the buffering.
 *
 * 한국어
 * ------
 * 엣지(`click`, `release`, `ch` 등)는 메시지가 도착할 때 이곳에 설정되고 프레임이 그려진
 * 뒤에 지워집니다. 그래야 두 프레임 사이에 발생한 클릭이 사라지지 않고 전달됩니다. 위치는
 * 자유롭게 덮어써도 되며, 버퍼링이 필요한 것은 엣지뿐입니다.
 */
static UiInput g_uin;

/** @brief Which light the inspector is editing, or -1. / 인스펙터가 편집 중인 조명. 없으면 -1. */
static int   g_light_sel = -1;

/** @brief Which entity the inspector is editing, or -1. / 인스펙터가 편집 중인 엔티티. 없으면 -1. */
static int   g_ent_sel = -1;

/** @brief Collapsed/expanded state per section, remembered across frames. / 섹션별 접힘 상태. 프레임을 넘어 유지됩니다. */
static int   g_sec_level = 1, g_sec_sector = 1, g_sec_light = 1, g_sec_help = 0;
static int   g_sec_list  = 0, g_sec_ent = 0;

/** @brief Scroll offset of the panel's contents. / 패널 내용의 스크롤 오프셋. */
static float g_panel_scroll;

/** @brief A splitter is being dragged: the panel's edge, or the plan/3D divide. / 분할선을 끄는 중입니다. */
static int   g_drag_edge, g_drag_split;

/** @brief The selected light is being dragged in the plan view. / 선택된 조명을 평면도에서 끄는 중입니다. */
static int   g_drag_light;

/**
 * @brief Which splitter, if any, the cursor is close enough to grab.
 *
 * @return 1 for the panel's right edge, 2 for the plan/3D divide, 0 for neither.
 * @note A grab zone wider than the line it drags, because a one-pixel target is
 *       one nobody finds. The zone is the whole reason the splitter is usable
 *       without a visible handle.
 *
 * @brief 커서가 잡을 수 있을 만큼 가까이 있는 분할선을 반환합니다.
 * @note 잡는 영역이 실제 선보다 넓습니다. 1픽셀 목표물은 아무도 찾지 못하기 때문입니다.
 */
static int plan_h(int h);   /* defined with the other layout helpers below */

static int splitter_at(int mx, int my, int h) {
    int d = mx - PANEL;
    if (d < 0) d = -d;
    if (d <= SPLIT_GRAB) return 1;

    if (mx > PANEL) {
        int dy = my - plan_h(h);
        if (dy < 0) dy = -dy;
        if (dy <= SPLIT_GRAB) return 2;
    }
    return 0;
}

/**
 * @brief Whether the pointer is over the inspector rather than the map.
 *
 * ENGLISH
 * -------
 * Asked before a click is treated as a map edit. The panel is a fixed column on
 * the left, so its area answers this exactly -- and it answers on the SAME
 * message that delivered the click, where ::ui_wants_mouse can only report what
 * was hot as of the previous frame. Clicking a button the cursor arrived at in
 * the same frame would otherwise also drag whatever sector sits behind it.
 *
 * 한국어
 * ------
 * @brief 포인터가 맵이 아니라 인스펙터 위에 있는지 여부입니다.
 *
 * 클릭을 맵 편집으로 처리하기 전에 묻습니다. 패널은 왼쪽의 고정된 열이므로 그 영역이 이
 * 질문에 정확히 답하며, 클릭을 전달한 *바로 그* 메시지에서 답합니다. ::ui_wants_mouse는
 * 이전 프레임 기준의 hot만 보고할 수 있습니다. 그렇지 않으면 커서가 같은 프레임에 도착한
 * 버튼을 클릭하는 것이 그 뒤의 섹터까지 끌게 됩니다.
 */
static int over_panel(int mx) { return mx < PANEL; }

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

        /* Hazard floors. Written only when the sector actually has one, because
           `hurt` defaults to 0 and level.c's parser resets it on every `s` --
           emitting "hurt 0" everywhere would double the size of the sector
           block to say nothing.

           Placed between the heights and the materials to match how vault is
           authored. It is deliberately NOT derived from the floor material:
           level.h states the two are independent on purpose, so a level may
           use lava as wall decoration or make an innocuous floor lethal.

           위험 지형 바닥입니다. 섹터가 실제로 그것을 가질 때만 기록합니다. `hurt`의
           기본값은 0이고 level.c의 파서가 모든 `s`에서 이를 초기화하므로, 어디에나
           "hurt 0"을 쓰면 아무 내용도 없이 섹터 블록의 크기만 두 배가 됩니다.

           vault가 제작된 방식에 맞추어 높이와 재질 사이에 둡니다. 바닥 재질에서 유도하지
           *않는다*는 점이 중요합니다. level.h가 둘이 의도적으로 독립적이라고 명시하므로,
           레벨이 용암을 벽 장식으로 쓰거나 멀쩡해 보이는 바닥을 치명적으로 만들 수
           있습니다. */
        if (s->hurt) {
            at = append(buf, at, cap, "\nhurt ");
            at = append_int(buf, at, cap, s->hurt);
        }

        /* Doors, written inside the sector they move. The same class of loss
           the lights were: level.c parses `door` into the Level, and a
           serialiser that did not write it back would delete every door in the
           map on the first save. `mapedit -verify` counts them for that reason.
           문은 자신이 움직이는 섹터 안에 기록합니다. 조명과 같은 종류의 손실입니다.
           level.c가 `door`를 Level로 파싱하는데 직렬화기가 되돌려 쓰지 않으면, 첫 저장에서
           맵의 모든 문이 삭제됩니다. `mapedit -verify`가 그래서 문을 셉니다. */
        for (int di = 0; di < g_level.n_doors; di++) {
            const DoorDef *d = &g_level.doors[di];
            if (d->sector != i) continue;

            static const char *AXIS[DOOR_AXES] = { "up", "down", "x", "z" };
            at = append(buf, at, cap, "\ndoor ");
            at = append(buf, at, cap,
                        (d->axis >= 0 && d->axis < DOOR_AXES) ? AXIS[d->axis] : "up");
            at = append(buf, at, cap, " ");
            at = append_int(buf, at, cap, d->amount);

            at = append(buf, at, cap, " speed ");
            at = append_int(buf, at, cap, d->speed);

            if (d->tag) {
                at = append(buf, at, cap, " tag ");
                at = append_int(buf, at, cap, d->tag);
            }
            if (d->key != KEY_NONE) {
                at = append(buf, at, cap, " key ");
                at = append(buf, at, cap,
                            d->key == KEY_RED  ? "red" :
                            d->key == KEY_BLUE ? "blue" : "yellow");
            }
        }

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

    /* Point lights, last, the way arena is authored.
     *
     * ENGLISH
     * -------
     * These and `hurt` above used to be dropped entirely: level_load parses
     * both into the Level this editor holds, and the serialiser wrote neither
     * back. Opening arena and pressing CTRL+S deleted all four of its lights,
     * and opening vault deleted its lava -- silently, with the level still
     * loading and playing, just dark and harmless.
     *
     * That is the worst shape a bug can have here. The editor is the tool you
     * reach for to make a small change, and the cost of the small change was
     * everything in the level the editor did not know about.
     *
     * @note There is still no UI for editing a light; this only guarantees the
     *       round trip. That is the same promise `next` above has kept since it
     *       was added, and it is the floor a UI can then be built on -- an
     *       inspector that edits a field the serialiser drops would be worse
     *       than no inspector at all.
     *
     * 한국어
     * ------
     * 이것과 위의 `hurt`는 이전에는 아예 누락되었습니다. level_load가 둘 다 이 에디터가
     * 보유한 Level로 파싱하는데, 직렬화기는 어느 쪽도 되돌려 쓰지 않았습니다. arena를
     * 열고 CTRL+S를 누르면 조명 네 개가 전부 삭제되었고, vault를 열면 용암이
     * 삭제되었습니다. 조용히, 레벨은 여전히 로드되고 플레이되며, 다만 어둡고 무해해진
     * 채로 말입니다.
     *
     * 이곳에서 버그가 가질 수 있는 최악의 형태입니다. 에디터는 작은 수정을 하려고 손을
     * 뻗는 도구인데, 그 작은 수정의 대가가 에디터가 몰랐던 레벨의 모든 것이었습니다.
     *
     * @note 조명을 *편집하는* UI는 아직 없습니다. 이 코드는 왕복만을 보장합니다. 위의
     *       `next`가 추가된 이래 지켜 온 것과 같은 약속이며, UI는 그 위에 세울 수
     *       있습니다. 직렬화기가 버리는 필드를 편집하는 인스펙터는 인스펙터가 아예 없는
     *       것보다 나쁩니다. */
    if (g_level.n_lights) at = append(buf, at, cap, "\n");
    for (int i = 0; i < g_level.n_lights; i++) {
        const Light *L = &g_level.lights[i];
        const short v[8] = { L->x, L->y, L->z, L->radius,
                             L->r, L->g, L->b, L->power };
        at = append(buf, at, cap, "light");
        for (int k = 0; k < 8; k++) {
            at = append(buf, at, cap, " ");
            at = append_int(buf, at, cap, v[k]);
        }
        at = append(buf, at, cap, "\n");
    }
    return at;
}

/* ------------------------------------------------------- round-trip check */

/**
 * @brief Reads a serialised level back and reports what the writer dropped.
 *
 * ENGLISH
 * -------
 * @param[in] buf Text ::serialise just produced.
 * @return How many discrepancies were found; 0 when the round trip is lossless.
 *
 * The bug this exists to catch has already shipped once. `hurt` and `light`
 * were parsed into the Level by level.c, held correctly in memory, edited
 * around happily -- and simply not written back. Saving arena deleted its four
 * lights; saving vault deleted its lava. Nothing failed, nothing warned, and
 * the level still loaded, so the only symptom was a room that had gone dark.
 *
 * A writer is exactly the kind of code where an omission is invisible from the
 * inside: every line that IS there works. So this reads the output back and
 * counts, rather than trusting a review of the writer.
 *
 * @note Deliberately NOT a second copy of level.c's parser. It counts
 *       directives and compares the values it finds against the Level in
 *       memory, which is the question being asked -- "did everything survive"
 *       -- and nothing more. A full reparser here would be a rule with two
 *       copies, which is the thing this project keeps refusing to have.
 * @note Checks structure and the two directives that were lost. A field added
 *       to the format needs a line here, and the count assertions are what make
 *       forgetting one visible: a new directive that is never written produces
 *       no token to count.
 *
 * 한국어
 * ------
 * @brief 직렬화된 레벨을 다시 읽어 writer가 무엇을 누락했는지 보고합니다.
 * @param[in] buf ::serialise가 방금 생성한 텍스트.
 * @return 발견된 불일치의 수. 왕복이 무손실이면 0입니다.
 *
 * 이 함수가 잡으려는 버그는 이미 한 번 출시되었습니다. `hurt`와 `light`는 level.c가
 * Level로 파싱했고, 메모리에 올바르게 보관되었고, 편집도 문제없이 되었으며, 다만 되돌려
 * 기록되지 않았을 뿐입니다. arena를 저장하면 조명 네 개가 삭제되었고 vault를 저장하면
 * 용암이 삭제되었습니다. 아무것도 실패하지 않았고 아무 경고도 없었으며 레벨은 여전히
 * 로드되었으므로, 유일한 증상은 어두워진 방이었습니다.
 *
 * writer는 누락이 내부에서 보이지 않는 대표적인 코드입니다. *있는* 줄은 전부 동작하기
 * 때문입니다. 그래서 writer를 검토하는 대신 출력을 다시 읽어 세어 봅니다.
 *
 * @note 의도적으로 level.c 파서의 두 번째 사본이 *아닙니다*. 지시자를 세고 찾은 값을
 *       메모리의 Level과 비교할 뿐이며, 그것이 여기서 묻는 질문("전부 살아남았는가")의
 *       전부입니다. 완전한 재파서는 사본이 둘인 규칙이 되며, 이 프로젝트가 계속 거부해 온
 *       것이 바로 그것입니다.
 */
static int verify_roundtrip(const char *buf) {
    int n_s = 0, n_e = 0, n_light = 0, n_door = 0, bad = 0;
    int hurt_seen[LVL_MAX_SECTORS], n_hurt = 0;
    short light_seen[LVL_MAX_LIGHTS][8];

    const char *p = buf;
    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        if (txt_is(t, len, "s")) { n_s++; continue; }
        if (txt_is(t, len, "e")) { n_e++; continue; }

        if (txt_is(t, len, "hurt")) {
            int v, ok;
            p = txt_read_int(p, &v, &ok);
            if (ok && n_hurt < LVL_MAX_SECTORS) hurt_seen[n_hurt++] = v;
            continue;
        }
        if (txt_is(t, len, "door")) { n_door++; continue; }
        if (txt_is(t, len, "light")) {
            int v[8], ok = 1;
            for (int i = 0; i < 8 && ok; i++) p = txt_read_int(p, &v[i], &ok);
            if (ok && n_light < LVL_MAX_LIGHTS) {
                for (int i = 0; i < 8; i++) light_seen[n_light][i] = (short)v[i];
            }
            if (ok) n_light++;
            continue;
        }
    }

    if (n_s != g_level.n_sectors) {
        printf("  FAIL sectors: wrote %d, level has %d\n", n_s, g_level.n_sectors);
        bad++;
    }
    if (n_e != g_level.n_ents) {
        printf("  FAIL entities: wrote %d, level has %d\n", n_e, g_level.n_ents);
        bad++;
    }
    if (n_door != g_level.n_doors) {
        printf("  FAIL doors: wrote %d, level has %d\n", n_door, g_level.n_doors);
        bad++;
    }
    if (n_light != g_level.n_lights) {
        printf("  FAIL lights: wrote %d, level has %d\n", n_light, g_level.n_lights);
        bad++;
    }

    /* Hazards are written only where they are non-zero, so the expected count
       is the non-zero ones rather than the sector count.
       위험 지형은 0이 아닌 곳에만 기록되므로, 기대되는 개수는 섹터 수가 아니라 0이 아닌
       것들의 수입니다. */
    int want_hurt = 0;
    for (int i = 0; i < g_level.n_sectors; i++)
        if (g_level.sectors[i].hurt) want_hurt++;
    if (n_hurt != want_hurt) {
        printf("  FAIL hazards: wrote %d, level has %d\n", n_hurt, want_hurt);
        bad++;
    } else {
        int k = 0;
        for (int i = 0; i < g_level.n_sectors; i++) {
            if (!g_level.sectors[i].hurt) continue;
            if (hurt_seen[k] != g_level.sectors[i].hurt) {
                printf("  FAIL sector %d hurt: wrote %d, level has %d\n",
                       i, hurt_seen[k], g_level.sectors[i].hurt);
                bad++;
            }
            k++;
        }
    }

    if (n_light == g_level.n_lights) {
        for (int i = 0; i < g_level.n_lights; i++) {
            const Light *L = &g_level.lights[i];
            const short want[8] = { L->x, L->y, L->z, L->radius,
                                    L->r, L->g, L->b, L->power };
            for (int k = 0; k < 8; k++) {
                if (light_seen[i][k] != want[k]) {
                    printf("  FAIL light %d field %d: wrote %d, level has %d\n",
                           i, k, light_seen[i][k], want[k]);
                    bad++;
                }
            }
        }
    }
    return bad;
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
    /* Refresh the cached bounding boxes FIRST. Every editing operation here
       moves points -- dragging a corner, inserting a vertex, duplicating a
       sector -- and point_in_sector rejects against those bounds, so a stale
       box makes collision and picking disagree with the geometry that is
       drawn: a corner dragged outward becomes a wall you can see and walk
       through.
       Done here rather than at each of the seven mutation sites because this
       is the one funnel they all pass through on their way to being queried,
       and a site that forgot would fail silently.
       캐시된 바운딩 박스를 *먼저* 갱신합니다. 이 에디터의 모든 편집 동작은 점을
       움직이며(모서리 드래그, 정점 삽입, 섹터 복제), point_in_sector가 그 경계값으로
       기각하므로 갱신되지 않은 박스는 충돌 및 선택 판정이 화면에 그려진 지오메트리와
       어긋나게 만듭니다. 바깥으로 끌어낸 모서리는 보이지만 통과할 수 있는 벽이 됩니다.
       일곱 곳의 수정 지점마다가 아니라 이곳에서 수행하는 이유는, 그 지점들이 질의에
       도달하기 전에 반드시 거치는 유일한 길목이기 때문입니다. 빠뜨린 지점이 있으면
       조용히 실패하게 됩니다. */
    for (int i = 0; i < g_level.n_sectors; i++)
        level_bounds(&g_level.sectors[i]);

    /* Then the lookup grid, which is built from those boxes and so must follow
       them. Same argument as the bounds themselves, one layer along: a stale
       grid omits a sector from the cell that now needs it, and an omitted
       sector is the same visible-but-walkable wall. This funnel is where both
       stay honest.
       그다음 조회 격자입니다. 그 박스들로부터 생성되므로 반드시 뒤에 와야 합니다.
       경계값 자체와 동일한 논거가 한 단계 더 적용됩니다. 갱신되지 않은 격자는 이제 그
       섹터를 필요로 하는 셀에서 그것을 누락시키며, 누락된 섹터는 똑같이 보이지만 통과할
       수 있는 벽이 됩니다. 이 길목이 둘 모두를 정직하게 유지하는 지점입니다. */
    level_grid_build(&g_level);

    /* And the baked light, which the game deliberately does NOT drop when a
       sector moves -- that cache is what keeps a door's swing off the frame
       time. An editor is the other case: the author moves a lamp, or moves a
       wall that was shadowing one, and expects to see what they just did. The
       cost of re-tracing every vertex is a fraction of a frame in a tool that
       is already waiting for a human, so this funnel pays it every time rather
       than trying to work out which edits changed the lighting.
       그리고 구워진 빛입니다. 게임은 섹터가 움직여도 이것을 의도적으로 버리지 *않습니다*.
       그 캐시가 문이 열리는 동안의 프레임 시간을 지켜 주기 때문입니다. 에디터는 반대의
       경우입니다. 제작자가 등을 옮기거나 등을 가리던 벽을 옮기고서 방금 한 일을 보기를
       기대합니다. 모든 정점을 다시 판정하는 비용은 어차피 사람을 기다리고 있는 도구에서
       프레임의 일부에 불과하므로, 이 길목은 어떤 편집이 조명을 바꿨는지 알아내려 하는 대신
       매번 그 비용을 치릅니다. */
    level_light_cache_reset();

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

    /* --- lights ----------------------------------------------------------
     *
     * ENGLISH
     * -------
     * Drawn in their own colour, with a ring at their actual radius.
     *
     * A light had no representation anywhere in the editor: not in the plan,
     * not in 3D, not in the panel. You authored one by typing eight numbers
     * into the level text and then ran the game to find out where it landed.
     * The radius ring is the part that matters -- reach is the field that is
     * hardest to guess and the one that decides whether a room is lit or has a
     * bright spot in it.
     *
     * Each is drawn in the colour it emits, so the plan reads as a lighting
     * plan rather than as a list of markers.
     *
     * 한국어
     * ------
     * 자신의 색으로, 실제 반경의 원과 함께 그립니다.
     *
     * 조명은 에디터 어디에도 표현이 없었습니다. 평면도에도, 3D에도, 패널에도 없었습니다.
     * 레벨 텍스트에 숫자 여덟 개를 입력해 제작한 뒤 게임을 실행해야 어디에 놓였는지 알 수
     * 있었습니다. 반경 원이 중요한 부분입니다. 도달 거리는 가장 짐작하기 어려운 값이며,
     * 방이 밝은지 아니면 방 안에 밝은 점이 하나 있는지를 결정하는 값입니다.
     *
     * 각 조명을 자신이 내는 색으로 그리므로, 평면도가 표식의 목록이 아니라 조명 계획으로
     * 읽힙니다. */
    for (int i = 0; i < g_level.n_lights; i++) {
        const Light *L = &g_level.lights[i];
        world_to_screen(L->x * 0.01f, L->z * 0.01f, w, h, &sx, &sy);

        int on = (i == g_light_sel);
        mb_reset(&g_line_buf);

        /* The reach, as a ring. Twenty segments is round enough at any zoom
           the plan reaches and costs nothing to build per frame.
           도달 거리를 원으로 표현합니다. 20분할이면 평면도가 도달하는 어떤 배율에서도
           충분히 둥글며, 프레임마다 생성해도 비용이 없습니다. */
        float rpx = (L->radius * 0.01f) / g_upp;
        const int SEG = 20;
        for (int k = 0; k < SEG; k++) {
            float a0 = (float)k       * 6.2831853f / SEG;
            float a1 = (float)(k + 1) * 6.2831853f / SEG;
            line2(&g_line_buf, sx + cosf(a0) * rpx, sy + sinf(a0) * rpx,
                               sx + cosf(a1) * rpx, sy + sinf(a1) * rpx);
        }

        /* A cross at the lamp itself, bigger when selected so the one the
           inspector is editing is obvious among several. */
        float k2 = on ? 9.0f : 6.0f;
        line2(&g_line_buf, sx - k2, sy, sx + k2, sy);
        line2(&g_line_buf, sx, sy - k2, sx, sy + k2);

        mesh_upload(&g_lines, &g_line_buf, 1);
        rd_color(L->r / 255.0f, L->g / 255.0f, L->b / 255.0f, on ? 1.0f : 0.5f);
        glLineWidth(on ? 2.5f : 1.0f);
        mesh_draw_lines(&g_lines);
    }
    glLineWidth(1.0f);

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

/* Draws the swatch column at `y`, which the inspector reserved space for.
 *
 * The header and the layout moved into draw_inspector when the panel became a
 * widget stack -- this now paints only the part that a widget cannot: each
 * swatch is the material drawn by its own shader, so a procedural surface shows
 * its real pattern rather than a colour chip.
 *
 * 헤더와 배치는 패널이 위젯 스택이 되면서 draw_inspector로 옮겨 갔습니다. 이 함수는 이제
 * 위젯이 할 수 없는 부분만 칠합니다. 각 견본은 자신의 셰이더가 그린 재질이므로, 절차적
 * 표면은 색상 칩이 아니라 실제 패턴을 보여 줍니다. */
static float draw_palette(int w, int h, float y) {
    if (!g_pal_n) return y;

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

/* Adds a light at the camera, and selects it.
 *
 * Placed where you are standing rather than at the origin, because a light that
 * appears somewhere off-screen has to be hunted down before it can be judged --
 * and judging it is the entire job. The defaults are a middling white lamp: a
 * starting point that is visible everywhere, which a zero radius or a black
 * colour would not be.
 *
 * 원점이 아니라 지금 서 있는 곳에 배치합니다. 화면 밖 어딘가에 나타난 조명은 판단하기 전에
 * 먼저 찾아다녀야 하는데, 판단하는 것이 일의 전부이기 때문입니다. 기본값은 중간 밝기의 흰
 * 램프이며, 반경 0이나 검은색과 달리 어디서든 보이는 출발점입니다. */
static void add_light(void) {
    if (g_level.n_lights >= LVL_MAX_LIGHTS) {
        status("lights are full (%d)", LVL_MAX_LIGHTS, 0);
        return;
    }
    snapshot();
    Light *L = &g_level.lights[g_level.n_lights++];
    L->x = (short)(g_eye.x * 100.0f);
    L->y = (short)(g_eye.y * 100.0f);
    L->z = (short)(g_eye.z * 100.0f);
    L->radius = 800;
    L->r = 255; L->g = 230; L->b = 190;
    L->power = 100;
    g_light_sel = g_level.n_lights - 1;
    g_dirty = 1;
    status("light %d added", g_light_sel, 0);
}

static void delete_light(int i) {
    if (i < 0 || i >= g_level.n_lights) return;
    snapshot();
    for (int k = i; k < g_level.n_lights - 1; k++)
        g_level.lights[k] = g_level.lights[k + 1];
    g_level.n_lights--;
    if (g_light_sel >= g_level.n_lights) g_light_sel = g_level.n_lights - 1;
    g_dirty = 1;
    status("light %d deleted", i, 0);
}

/**
 * @brief The inspector: every field of the level the format can hold.
 *
 * ENGLISH
 * -------
 * This replaces a column of read-only text. The difference is not cosmetic --
 * `hurt` and the point lights had no editor representation at all, so a lava
 * pit or a lamp could only be authored by hand-editing assets/levels.txt, and
 * until the serialiser was fixed alongside this, opening the level in the
 * editor and saving deleted them.
 *
 * @note Every widget writes straight into ::g_level. There is no apply step and
 *       no copy of the values held beside them, so the panel cannot show
 *       something the level does not contain. See ui.h on why immediate mode
 *       was chosen for exactly this.
 * @note ::snapshot is taken on the frame a field first changes, not per frame
 *       of a drag: dragging a ceiling from 300 to 600 is one edit to undo, not
 *       three hundred.
 *
 * 한국어
 * ------
 * @brief 인스펙터입니다. 포맷이 담을 수 있는 레벨의 모든 필드를 다룹니다.
 *
 * 읽기 전용 텍스트 열을 대체합니다. 차이는 외형이 아닙니다. `hurt`와 점광원은 에디터에
 * 표현 자체가 없었으므로 용암 구덩이나 램프는 assets/levels.txt를 직접 편집해야만 제작할
 * 수 있었고, 직렬화기를 함께 고치기 전까지는 에디터에서 레벨을 열고 저장하면 그것들이
 * 삭제되었습니다.
 *
 * @note 모든 위젯이 ::g_level에 직접 씁니다. 적용 단계도, 값의 사본도 없으므로 패널이
 *       레벨에 없는 것을 보여 줄 수 없습니다. 정확히 이 때문에 즉시 모드를 택했다는 설명은
 *       ui.h를 참조하십시오.
 * @note ::snapshot은 드래그의 매 프레임이 아니라 필드가 *처음* 바뀌는 프레임에
 *       가져옵니다. 천장을 300에서 600으로 끄는 것은 실행 취소 300번이 아니라 한 번입니다.
 */
static void draw_inspector(int w, int h) {
    Sector *s = sel();
    char line[96];

    ui_begin(&g_ui, w, h, &g_uin);
    ui_panel(&g_ui, 0.0f, 0.0f, (float)PANEL, (float)h, 0.94f);

    /* --- the toolbar, above the scroll ---------------------------------
       Outside the scroll region deliberately: these are the operations you
       reach for most and they must not be somewhere you have to scroll back up
       to. Every one of them already had a key, and every one of them was
       therefore invisible to anyone who had not read the key list.
       의도적으로 스크롤 영역 바깥에 둡니다. 가장 자주 손이 가는 동작들이며, 다시 위로
       스크롤해야 닿는 곳에 있어서는 안 됩니다. 이들 각각은 이미 단축키가 있었고, 따라서
       키 목록을 읽지 않은 사람에게는 전부 보이지 않는 기능이었습니다. */
    {
        float wx = 0.0f, wz = 0.0f;
        POINT c; GetCursorPos(&c); ScreenToClient(g_wnd, &c);
        screen_to_world(c.x, c.y, w, h, &wx, &wz);

        if (ui_button_strip(&g_ui, UI_ID, "new", 0, 3, 0)) new_sector(wx, wz);
        if (ui_button_strip(&g_ui, UI_ID, "dup", 1, 3, 0)) duplicate_sector();
        if (ui_button_strip(&g_ui, UI_ID, "del", 2, 3, 0)) delete_sector();

        if (ui_button_strip(&g_ui, UI_ID, "undo", 0, 3, 0)) undo();
        if (ui_button_strip(&g_ui, UI_ID, "redo", 1, 3, 0)) redo();
        if (ui_button_strip(&g_ui, UI_ID, "SAVE", 2, 3, 0)) save();
    }
    ui_separator(&g_ui);

    /* Everything below scrolls except the status line, which is pinned. */
    ui_scroll_begin(&g_ui, (float)h - ui_cursor_y(&g_ui) - 26.0f, &g_panel_scroll);

    /* --- the level ----------------------------------------------------- */
    if (ui_section(&g_ui, UI_ID, "LEVEL", &g_sec_level)) {
        if (ui_text_field(&g_ui, UI_ID, "name", g_level_name, sizeof(g_level_name)))
            status_s("level renamed to %s", g_level_name);

        /* `next` had no UI at all and was preserved blind through a save. The
           exit's destination is what wires one level to the next, so being
           unable to see it meant progression could only be read out of the
           text file.
           `next`는 UI가 전혀 없었고 저장 과정에서 보이지 않게 보존되기만 했습니다. 출구의
           목적지가 레벨을 다음 레벨로 잇는 것이므로, 그것을 볼 수 없다는 것은 진행 구조를
           텍스트 파일에서만 읽을 수 있다는 뜻이었습니다. */
        if (ui_text_field(&g_ui, UI_ID, "next", g_level.next, sizeof(g_level.next)))
            status_s("exit leads to %s", g_level.next[0] ? g_level.next : "(ends the game)");
        if (!g_level.next[0]) ui_label_dim(&g_ui, "  (no next: ends the game)");

        ui_drag_short(&g_ui, UI_ID, "start x", &g_level.start[0], -30000, 30000, 4.0f);
        ui_drag_short(&g_ui, UI_ID, "start z", &g_level.start[1], -30000, 30000, 4.0f);
        ui_drag_short(&g_ui, UI_ID, "yaw mdeg", &g_level.start[2], -180000, 180000, 300.0f);

        wsprintfA(line, "%d sectors  %d ents  %d lights",
                  g_level.n_sectors, g_level.n_ents, g_level.n_lights);
        ui_label_dim(&g_ui, line);
    }
    ui_separator(&g_ui);

    /* --- every sector, as a list ----------------------------------------
       TAB cycles the selection and always did, which works for six sectors and
       stops working somewhere before sixty-four: you cannot see where you are
       in the order, and a sector you want is however many presses away. A list
       answers "what is in this map" and "take me to that one" at once.

       The floor and ceiling are in each row because they are what tells two
       otherwise identical sectors apart in a plan view.

       TAB이 선택을 순환하며 예전부터 그랬습니다. 섹터 여섯 개에는 통하지만 예순네 개
       이전 어딘가에서 통하지 않게 됩니다. 순서상 어디에 있는지 볼 수 없고, 원하는 섹터는
       몇 번을 눌러야 나올지 모르는 거리에 있습니다. 목록은 "이 맵에 무엇이 있는가"와
       "그곳으로 데려가 달라"에 동시에 답합니다. */
    wsprintfA(line, "SECTORS  %d/%d", g_level.n_sectors, LVL_MAX_SECTORS);
    if (ui_section(&g_ui, UI_ID, line, &g_sec_list)) {
        for (int i = 0; i < g_level.n_sectors; i++) {
            const Sector *si = &g_level.sectors[i];
            wsprintfA(line, "%d:  f%d c%d  %dv%s",
                      i, si->floor, si->ceil, si->n, si->hurt ? "  HAZARD" : "");
            if (ui_list_item(&g_ui, UI_IDX(i), line, i == g_sel)) {
                g_sel = i;
                g_sel_vert = -1;
                status("sector %d of %d", g_sel, g_level.n_sectors);
            }
        }
    }
    ui_separator(&g_ui);

    /* --- entities --------------------------------------------------------
       Placement already existed (E) and editing did not: an entity dropped in
       the wrong place could only be deleted and placed again, and its kind
       could only be changed before it existed by cycling the tool. Both are
       now properties of a thing you can select.
       배치는 이미 있었고(E) 편집은 없었습니다. 잘못된 위치에 놓인 엔티티는 지우고 다시
       놓는 수밖에 없었고, 종류는 생성 *전에* 도구를 순환시켜야만 바꿀 수 있었습니다. 이제
       둘 다 선택할 수 있는 대상의 속성입니다. */
    wsprintfA(line, "ENTITIES  %d/%d", g_level.n_ents, LVL_MAX_ENTS);
    if (ui_section(&g_ui, UI_ID, line, &g_sec_ent)) {
        for (int i = 0; i < g_level.n_ents; i++) {
            const Entity *e = &g_level.ents[i];
            wsprintfA(line, "%d:  %s  %d %d", i, e->kind, e->x, e->z);
            if (ui_list_item(&g_ui, UI_IDX(i), line, i == g_ent_sel))
                g_ent_sel = (g_ent_sel == i) ? -1 : i;
        }

        if (g_ent_sel >= 0 && g_ent_sel < g_level.n_ents) {
            Entity *e = &g_level.ents[g_ent_sel];
            ui_separator(&g_ui);
            if (ui_drag_short(&g_ui, UI_ID, "x", &e->x, -30000, 30000, 4.0f)) g_dirty = 1;
            if (ui_drag_short(&g_ui, UI_ID, "z", &e->z, -30000, 30000, 4.0f)) g_dirty = 1;

            /* The kind is one of a fixed set, so it is a row of choices rather
               than a text field: a typo in an entity kind spawns nothing at
               all, and nothing is exactly what an empty room looks like.
               종류는 고정된 집합의 하나이므로 텍스트 필드가 아니라 선택 행입니다. 엔티티
               종류의 오타는 아무것도 생성하지 않으며, 아무것도 없는 것은 빈 방과 정확히
               같아 보입니다. */
            ui_label_dim(&g_ui, "kind");
            for (int k = 0; k < N_ENT_KINDS; k++) {
                int on = same_str(e->kind, ENT_KINDS[k]);
                /* Four per row: seven kinds at full width would be a very tall
                   strip, and the names are short. */
                int col = k % 4, ncol = 4;
                if (ui_button_strip(&g_ui, UI_IDX(k), ENT_KINDS[k], col, ncol, on)) {
                    snapshot();
                    set_str(e->kind, LVL_MAT, ENT_KINDS[k]);
                    g_dirty = 1;
                    status_s("entity -> %s", e->kind);
                }
                /* The strip advances the cursor on its last column only, so a
                   partial final row has to be closed by hand. */
                if (k == N_ENT_KINDS - 1 && col != ncol - 1)
                    ui_space(&g_ui, UI_ROW_H + 2.0f);
            }

            if (ui_button(&g_ui, UI_ID, "- delete this entity")) {
                snapshot();
                for (int k = g_ent_sel; k < g_level.n_ents - 1; k++)
                    g_level.ents[k] = g_level.ents[k + 1];
                g_level.n_ents--;
                if (g_ent_sel >= g_level.n_ents) g_ent_sel = g_level.n_ents - 1;
                g_dirty = 1;
                status("entity deleted, %d left", g_level.n_ents, 0);
            }
        }
    }
    ui_separator(&g_ui);

    /* --- the selected sector -------------------------------------------- */
    if (s) {
        wsprintfA(line, "SECTOR %d", g_sel);
        if (ui_section(&g_ui, UI_ID, line, &g_sec_sector)) {
            int before_f = s->floor, before_c = s->ceil, before_h = s->hurt;

            ui_drag_short(&g_ui, UI_ID, "floor", &s->floor, -30000, 30000, 2.0f);
            ui_drag_short(&g_ui, UI_ID, "ceil",  &s->ceil,  -30000, 30000, 2.0f);

            /* Hazard damage per second. 0 is a safe floor; the level format
               keeps this independent of the material on purpose, so a lava
               texture is decoration until this says otherwise.
               초당 위험 피해량입니다. 0이면 안전한 바닥이며, 레벨 포맷은 의도적으로 이
               값을 재질과 독립적으로 유지합니다. 따라서 용암 텍스처는 이 값이 달리 말하기
               전까지는 장식입니다. */
            ui_drag_short(&g_ui, UI_ID, "hurt dps", &s->hurt, 0, 999, 0.4f);
            if (s->hurt > 0) ui_label_dim(&g_ui, "  hazard floor");

            if (s->floor != before_f || s->ceil != before_c || s->hurt != before_h) {
                g_dirty = 1;
                if (s->ceil < s->floor) s->ceil = s->floor;
            }

            wsprintfA(line, "%d verts", s->n);
            ui_label_dim(&g_ui, line);
            wsprintfA(line, "floor %s", s->mat_floor);
            ui_label_dim(&g_ui, line);
            wsprintfA(line, "wall  %s", s->mat_wall);
            ui_label_dim(&g_ui, line);
            wsprintfA(line, "ceil  %s", s->mat_ceil);
            ui_label_dim(&g_ui, line);
        }
    } else {
        ui_label_dim(&g_ui, "no sector selected");
    }
    ui_separator(&g_ui);

    /* --- the material palette -------------------------------------------
       Drawn outside the widget batch, because a swatch is the material itself
       rendered by the shader that draws it -- which is the whole point of the
       palette and something a flat rect cannot do. Space is reserved here and
       the swatches are painted over it after ui_end.
       위젯 배치 바깥에서 그립니다. 견본은 그것을 그리는 셰이더가 렌더링한 재질 자체이며,
       그것이 팔레트의 요점이자 단색 사각형으로는 할 수 없는 일이기 때문입니다. 이곳에서는
       공간만 확보하고, ui_end 이후에 그 위에 견본을 칠합니다. */
    ui_label_dim(&g_ui, "MATERIALS  (click / drag onto a surface)");
    g_pal_top = ui_cursor_y(&g_ui);
    ui_space(&g_ui, g_pal_n * SW_STEP + 4.0f);
    ui_separator(&g_ui);

    /* --- lights ---------------------------------------------------------- */
    wsprintfA(line, "LIGHTS  %d/%d", g_level.n_lights, LVL_MAX_LIGHTS);
    if (ui_section(&g_ui, UI_ID, line, &g_sec_light)) {
        for (int i = 0; i < g_level.n_lights; i++) {
            const Light *L = &g_level.lights[i];
            wsprintfA(line, "%d:  %d %d %d   r%d", i, L->x, L->y, L->z, L->radius);
            if (ui_list_item(&g_ui, UI_IDX(i), line, i == g_light_sel))
                g_light_sel = (g_light_sel == i) ? -1 : i;
        }

        if (ui_button(&g_ui, UI_ID, "+ add light here")) add_light();

        if (g_light_sel >= 0 && g_light_sel < g_level.n_lights) {
            Light *L = &g_level.lights[g_light_sel];
            int changed = 0;
            ui_separator(&g_ui);
            changed |= ui_drag_short(&g_ui, UI_ID, "x", &L->x, -30000, 30000, 4.0f);
            changed |= ui_drag_short(&g_ui, UI_ID, "y", &L->y, -30000, 30000, 2.0f);
            changed |= ui_drag_short(&g_ui, UI_ID, "z", &L->z, -30000, 30000, 4.0f);
            changed |= ui_drag_short(&g_ui, UI_ID, "radius", &L->radius, 0, 30000, 6.0f);
            changed |= ui_color_rgb(&g_ui, UI_ID, "colour", &L->r, &L->g, &L->b);
            changed |= ui_drag_short(&g_ui, UI_ID, "power %", &L->power, 0, 400, 1.0f);
            if (changed) g_dirty = 1;
            if (ui_button(&g_ui, UI_ID, "- delete this light"))
                delete_light(g_light_sel);
        }
    }
    ui_separator(&g_ui);

    /* --- tool state ------------------------------------------------------ */
    wsprintfA(line, "grid %d  %s", g_grid, g_snap ? "SNAP" : "free");
    ui_label_dim(&g_ui, line);
    wsprintfA(line, "entity: %s   (R cycles)", ENT_KINDS[g_ent_kind]);
    ui_label_dim(&g_ui, line);
    wsprintfA(line, "undo %d/%d", g_undo_at, g_undo_n);
    ui_label_dim(&g_ui, line);
    {
        static const char *KIND[] = { "-", "floor", "ceil", "wall" };
        const Pick *hp = g_drag3d ? &g_grab : &g_hover;
        if (hp->kind == SURF_NONE) wsprintfA(line, "3D: -");
        else wsprintfA(line, "3D: s%d %s", hp->sector, KIND[hp->kind]);
        ui_label_dim(&g_ui, line);
    }
    ui_separator(&g_ui);

    /* --- the key list, collapsed by default -----------------------------
       It was always on screen and it is reference material: useful on the
       first day and noise afterwards, which is what a collapsed section is for.
       예전에는 항상 화면에 있었고 이것은 참고 자료입니다. 첫날에는 유용하고 그 뒤로는
       소음이며, 접힌 섹션이 존재하는 이유가 그것입니다. */
    if (ui_section(&g_ui, UI_ID, "KEYS", &g_sec_help)) {
        static const char *HELP[] = {
            "WASD  fly where you look",
            "SPACE/C  straight up/down",
            "SHIFT faster   RMB look",
            "F     go to start",
            "drag  move vert/sector",
            "^drag insert vertex",
            "wheel raise surface (3D)",
            "      zoom (plan)",
            "- =   floor (SHIFT ceil)",
            "N new  ^D dup   DEL del",
            "V     delete vertex",
            "TAB   next sector",
            "E ent  R kind  P start",
            "[ ]   grid     G snap",
            "M     next material",
            "^Z ^Y undo    ^S save",
        };
        for (int i = 0; i < (int)(sizeof(HELP)/sizeof(HELP[0])); i++)
            ui_label_dim(&g_ui, HELP[i]);
    }

    ui_scroll_end(&g_ui);
    ui_end(&g_ui);

    /* Status pinned to the bottom, where it does not move as the panel grows. */
    float fade = g_status_age > 4.0f ? 0.35f : 1.0f;
    draw_text(w, h, 10, h - 18.0f, 1.0f, fade, fade * 0.9f, fade * 0.5f, g_status);
}

static void draw_panel(int w, int h) {
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    draw_inspector(w, h);

    /* --- the splitters --------------------------------------------------
       Drawn so they can be found. An invisible drag zone is a feature only the
       person who wrote it knows about, and this one has no key and no menu
       entry to discover it by. Brighter while held, so the drag is confirmed.
       찾을 수 있도록 그립니다. 보이지 않는 드래그 영역은 작성한 사람만 아는 기능이며,
       이것은 단축키도 메뉴 항목도 없어 달리 발견할 방법이 없습니다. 끄는 동안에는 더 밝게
       하여 드래그가 진행 중임을 알립니다. */
    {
        int ph = plan_h(h);
        mb_reset(&g_quad_buf);
        mb_billboard(&g_quad_buf, v3f(PANEL + 1.0f, h * 0.5f, 0),
                     v3f(1,0,0), v3f(0,1,0), 2.0f, (float)h);
        mb_billboard(&g_quad_buf, v3f((PANEL + w) * 0.5f, (float)ph, 0),
                     v3f(1,0,0), v3f(0,1,0), (float)(w - PANEL), 2.0f);
        mesh_upload(&g_quads, &g_quad_buf, 1);
        rd_mvp(screen_mvp(w, h));
        rd_mode(RD_FLAT);
        float b = (g_drag_edge || g_drag_split) ? 0.75f : 0.28f;
        rd_color(b, b * 0.95f, b * 0.85f, 1.0f);
        mesh_draw(&g_quads);
    }

    /* The swatches, over the space the inspector reserved for them. Clipped to
       the scroll region by hand: the UI's scissor was released by ui_end, and a
       palette that ignored the scroll would float over the sections above it.
       인스펙터가 확보해 둔 공간 위에 견본을 그립니다. 스크롤 영역에 대한 클리핑은 직접
       수행합니다. UI의 시저는 ui_end가 해제했으며, 스크롤을 무시하는 팔레트는 위쪽 섹션들
       위에 떠 있게 됩니다. */
    if (g_pal_n > 0) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 18, PANEL, h - 34);
        draw_palette(w, h, g_pal_top);
        glDisable(GL_SCISSOR_TEST);
    }

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
        /* Splitters first, ahead of both the panel and the views: their grab
           zones straddle the boundary, so whichever side the cursor is on it
           has to mean "resize" rather than "edit whatever is under here".
           패널과 뷰 양쪽보다 분할선을 먼저 처리합니다. 잡는 영역이 경계를 걸치고 있으므로,
           커서가 어느 쪽에 있든 "여기 있는 것을 편집"이 아니라 "크기 조절"을 뜻해야
           합니다. */
        {
            int sp = splitter_at(mx, my, h);
            if (sp == 1) { g_drag_edge = 1;  SetCapture(wnd); return 0; }
            if (sp == 2) { g_drag_split = 1; SetCapture(wnd); return 0; }
        }

        /* The panel gets first refusal: without this a click on it falls
           through to the 3D pick, which reads a ray from off-viewport coords.
           Nothing is applied yet -- a press on a swatch may turn into a drag
           onto a surface, and that is only known at release. */
        if (over_panel(mx)) {
            g_uin.mx = (float)mx; g_uin.my = (float)my;
            g_uin.click = 1; g_uin.down = 1;
            SetCapture(wnd);

            /* A widget under the cursor takes the click; the palette only sees
               what is left. The two share the column, and the inspector's
               reserved swatch area carries no widget, so the question sorts
               itself out without either side knowing about the other.
               커서 아래에 위젯이 있으면 그것이 클릭을 가져가고, 팔레트는 남은 것만
               봅니다. 둘은 같은 열을 공유하지만 인스펙터가 확보한 견본 영역에는 위젯이
               없으므로, 양쪽이 서로를 알지 못해도 이 문제는 저절로 정리됩니다. */
            if (!ui_wants_mouse(&g_ui)) {
                g_pal_drag = palette_hit(mx, my);
                g_pal_mx = mx; g_pal_my = my;
            }
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

        /* --- a light, if the cursor is on one -----------------------------
           Ahead of vertices and sectors: a light usually sits inside a sector,
           so testing the sector first would mean a lamp could be selected only
           where it happened to hang over empty space. Picking is in SCREEN
           pixels rather than world units so the target stays the same size to
           the hand at every zoom.
           정점과 섹터보다 먼저 처리합니다. 조명은 대개 섹터 *안에* 있으므로 섹터를 먼저
           판정하면 램프가 빈 공간 위에 걸쳐 있을 때만 선택될 수 있습니다. 선택 판정은
           월드 단위가 아니라 *화면* 픽셀로 하므로, 어떤 배율에서도 손에 닿는 목표물의
           크기가 같습니다. */
        for (int i = 0; i < g_level.n_lights; i++) {
            float lx, ly;
            world_to_screen(g_level.lights[i].x * 0.01f,
                            g_level.lights[i].z * 0.01f, w, h, &lx, &ly);
            float dx = lx - mx, dy = ly - my;
            if (dx*dx + dy*dy <= PICK_PX * PICK_PX) {
                g_light_sel = i;
                g_sec_light = 1;          /* open the section that edits it */
                g_drag_light = 1;
                snapshot();
                SetCapture(wnd);
                status("light %d selected", i, 0);
                return 0;
            }
        }

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
        /* Always delivered to the UI, wherever the cursor ended up: a widget
           fires on release, and a drag that wandered out of the panel still has
           to be let go of. Suppressing this outside the panel is what would
           leave a field stuck to the mouse.
           커서가 어디에서 끝났든 항상 UI에 전달합니다. 위젯은 뗄 때 발동하며, 패널 밖으로
           벗어난 드래그도 놓아 주어야 합니다. 패널 바깥에서 이를 억제하면 필드가 마우스에
           붙어 버립니다. */
        g_uin.mx = (float)mx; g_uin.my = (float)my;
        g_uin.release = 1; g_uin.down = 0;

        if (g_drag_edge || g_drag_split) {
            g_drag_edge = g_drag_split = 0;
            ReleaseCapture();
            return 0;
        }

        if (g_pal_drag >= 0) {
            /* Dropped on a surface: that surface. Released back over the
               panel: it was a click, so use whatever was last picked in 3D. */
            Pick target = g_hover;
            if (over_panel(mx)) target = g_last_click;
            palette_apply(g_pal_drag, &target);
            g_pal_drag = -1;
            ReleaseCapture();
            return 0;
        }
        g_drag_vert = g_drag_sector = g_drag3d = g_drag_light = 0;
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

        /* The widget layer needs the position every frame regardless of where
           it is -- a drag is tracked by total travel, so a cursor that leaves
           the panel mid-drag still has to be followed.
           위젯 레이어는 위치가 어디든 매 프레임 그것을 필요로 합니다. 드래그는 총 이동량으로
           추적되므로, 드래그 도중 패널을 벗어난 커서도 계속 따라가야 합니다. */
        g_uin.mx = (float)mx; g_uin.my = (float)my;

        /* Resizing takes the whole message: nothing else should hover, pick or
           drag while a boundary is being moved.
           크기 조절이 메시지 전체를 가져갑니다. 경계를 옮기는 동안에는 다른 어떤 것도
           호버·선택·드래그를 해서는 안 됩니다. */
        if (g_drag_edge) {
            int nw = mx;
            if (nw < PANEL_MIN) nw = PANEL_MIN;
            if (nw > w - 200)   nw = w - 200;   /* always leave a usable view */
            g_panel_w = nw;
            return 0;
        }
        if (g_drag_split) {
            g_split = clampf((float)my / (float)h, 0.15f, 0.9f);
            return 0;
        }

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

        if (g_drag_light && g_light_sel >= 0 && g_light_sel < g_level.n_lights) {
            /* Snapped like every other placement, so a lamp lands on the same
               grid the geometry does.
               다른 모든 배치와 마찬가지로 스냅됩니다. 램프가 지오메트리와 같은 격자에
               놓이도록 하기 위함입니다. */
            float wx2, wz2; screen_to_world(mx, my, w, h, &wx2, &wz2);
            g_level.lights[g_light_sel].x = snap(wx2);
            g_level.lights[g_light_sel].z = snap(wz2);
            g_dirty = 1;
        } else if (g_drag_vert) {
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

        /* Over the panel the wheel scrolls the inspector. It is a tall column
           of sections and the alternative is only ever seeing the top of it.
           패널 위에서는 휠이 인스펙터를 스크롤합니다. 섹션이 세로로 긴 열을 이루므로,
           그렇지 않으면 언제나 그 위쪽만 보게 됩니다. */
        if (over_panel((int)c.x)) {
            g_uin.wheel = up ? 1.0f : -1.0f;
            return 0;
        }

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

    /* Printable characters, for the inspector's text and number fields.
     *
     * WM_CHAR rather than WM_KEYDOWN because only this message has been through
     * the keyboard layout: WM_KEYDOWN carries a virtual key, so building text
     * from it would type the wrong letters on every non-US layout and would get
     * shift and the number row wrong even on a US one.
     *
     * 인스펙터의 텍스트·숫자 필드를 위한 출력 가능 문자입니다.
     *
     * WM_KEYDOWN이 아니라 WM_CHAR인 이유는 이 메시지만이 키보드 레이아웃을 거쳤기
     * 때문입니다. WM_KEYDOWN은 가상 키를 전달하므로 그것으로 텍스트를 만들면 미국식이 아닌
     * 모든 레이아웃에서 엉뚱한 글자가 입력되고, 미국식에서도 shift와 숫자열을 틀립니다. */
    case WM_CHAR:
        if (wp >= 32 && wp < 127) g_uin.ch = (int)wp;
        return 0;

    case WM_KEYDOWN: {
        float wx = 0, wz = 0;
        POINT c; GetCursorPos(&c); ScreenToClient(wnd, &c);
        screen_to_world(c.x, c.y, w, h, &wx, &wz);

        /* --- keys the inspector owns while it is being typed into ----------
           Every shortcut here is a bare letter, so typing a level name would
           otherwise delete a vertex on the V, duplicate a sector on the D and
           quit the editor on the first Escape. The field takes the keystroke
           and nothing else sees it.
           이곳의 모든 단축키가 단일 문자이므로, 그렇지 않으면 레벨 이름을 입력하는 것이
           V에서 정점을 삭제하고, D에서 섹터를 복제하고, 첫 ESC에서 에디터를 종료하게
           됩니다. 필드가 키 입력을 가져가고 그 외에는 아무도 보지 못합니다. */
        if (ui_wants_keys(&g_ui)) {
            switch (wp) {
            case VK_BACK:   g_uin.backspace = 1; break;
            case VK_RETURN: g_uin.enter     = 1; break;
            case VK_ESCAPE: g_uin.escape    = 1; break;
            default: break;   /* the text itself arrives as WM_CHAR */
            }
            return 0;
        }

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

    int print_only = 0, new_at = 0, verify = 0;
    if (cmd && *cmd) {
        int i = 0;
        while (cmd[i] && cmd[i] != ' ' && i < (int)sizeof(g_level_name) - 1) {
            g_level_name[i] = cmd[i]; i++;
        }
        g_level_name[i] = 0;
        for (int k = 0; cmd[k]; k++) {
            if (cmd[k] == '-' && cmd[k+1] == 'p') print_only = 1;
            /* -verify checks every level the file defines rather than printing
               one, and returns non-zero on loss so a test suite can run it.
               -verify는 하나를 출력하는 대신 파일이 정의하는 모든 레벨을 검사하고, 손실이
               있으면 0이 아닌 값을 반환하여 테스트 스위트가 실행할 수 있게 합니다. */
            if (cmd[k] == '-' && cmd[k+1] == 'v') verify = 1;
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

    /* Every level the text defines, not just the one named: a round trip that
       holds for arena and loses vault's lava is still a broken save, and the
       level somebody forgets to check is the one that breaks.
       이름을 지정한 하나가 아니라 텍스트가 정의하는 모든 레벨을 검사합니다. arena에서는
       성립하고 vault의 용암을 잃는 왕복도 여전히 망가진 저장이며, 검사를 잊는 레벨이 곧
       망가지는 레벨입니다. */
    if (verify) {
        static char buf[65536];
        int levels = 0, bad = 0;

        printf("mapedit -verify: save/reload round trip\n\n");

        const char *p = data_text(DATA_LEVELS);
        for (;;) {
            int len;
            const char *t = txt_token(p, &len);
            if (!t) break;
            p = t + len;
            if (!txt_is(t, len, "l")) continue;

            const char *nm = txt_token(p, &len);
            if (!nm) break;
            p = nm + len;

            char name[32];
            txt_copy(name, sizeof(name), nm, len);
            if (!level_load(name, &g_level)) continue;

            levels++;
            serialise(buf, sizeof(buf));
            int lost = verify_roundtrip(buf);
            printf("  %-12s %2d sectors  %2d ents  %2d lights  %2d doors   %s\n",
                   name, g_level.n_sectors, g_level.n_ents, g_level.n_lights,
                   g_level.n_doors, lost ? "LOSSY" : "ok");
            bad += lost;
        }

        if (!levels) { printf("  no levels found in the level text\n"); return 1; }
        printf(bad ? "\n%d field(s) lost on save\n"
                   : "\nevery level survives a save unchanged\n", bad);
        return bad != 0;
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

        /* --- fly the 3D camera ------------------------------------------
         *
         * ENGLISH
         * -------
         * Held keys are polled rather than handled as messages, so movement is
         * smooth and framerate independent.
         *
         * WASD, because every 3D editor and every game uses it and the other
         * hand is on the mouse. The arrows still work for anyone who learnt the
         * old layout. Ctrl suppresses it so CTRL+S and CTRL+D do not fly the
         * camera while they save and duplicate.
         *
         * **Forward follows where the camera is LOOKING, pitch included.** This
         * used to build its own basis from yaw alone -- `v3f(-sy, 0, -cy)` --
         * so W walked a flat plane and the only way to change height was
         * SPACE/C. That is a first-person walker's control scheme, and this is
         * a modelling view: to get above a room you had to hold a separate key
         * and watch the ceiling come at you, rather than simply looking up and
         * going there. Unity, Godot, Blender and TrenchBroom all fly along the
         * view vector for the same reason.
         *
         * ::view_basis already computes exactly this and the picking code has
         * been using it all along, so the fix is to stop keeping a second,
         * flatter copy of the camera's own orientation. That is also why the
         * two could disagree: the ray you picked with was pitched and the
         * direction you flew was not.
         *
         * `right` stays horizontal (::view_basis gives it a zero y), which is
         * what keeps strafing level while looking down -- a strafe that tilted
         * with the camera would roll the view sideways out of the room.
         *
         * SPACE/C are kept as a WORLD-vertical override rather than removed.
         * Rising straight up while still facing a wall is a real editor
         * operation -- checking a wall's full height, or getting out of a pit
         * without looking away from what you were working on -- and the two
         * mechanisms answer different questions. Every editor named above ships
         * both for that reason.
         *
         * 한국어
         * ------
         * 눌린 키를 메시지가 아니라 폴링으로 처리하므로 이동이 부드럽고 프레임률에
         * 무관합니다.
         *
         * **전진은 카메라가 *바라보는* 방향을 따르며 피치가 포함됩니다.** 이전에는 yaw
         * 만으로 자체 기저를 만들었기 때문에(`v3f(-sy, 0, -cy)`) W가 평평한 면 위를
         * 걸었고, 높이를 바꾸는 유일한 방법이 SPACE/C였습니다. 그것은 1인칭 보행자의 조작
         * 체계이고 이곳은 모델링 뷰입니다. 방 위로 올라가려면 별도의 키를 누른 채 천장이
         * 다가오는 것을 지켜봐야 했는데, 그냥 위를 보고 그쪽으로 가면 될 일이었습니다.
         * Unity, Godot, Blender, TrenchBroom이 모두 같은 이유로 시선 벡터를 따라
         * 비행합니다.
         *
         * ::view_basis가 이미 정확히 이것을 계산하며 피킹 코드는 줄곧 그것을 써 왔으므로,
         * 수정은 카메라 자신의 방향에 대한 더 납작한 두 번째 사본을 그만 두는 것입니다.
         * 둘이 어긋날 수 있었던 이유이기도 합니다. 선택에 쓰는 광선은 피치가 적용되어
         * 있었고 실제로 비행하는 방향은 그렇지 않았습니다.
         *
         * `right`는 수평으로 유지됩니다(::view_basis가 y를 0으로 줍니다). 아래를 내려다보는
         * 동안에도 좌우 이동이 수평을 유지하게 하는 요소이며, 카메라를 따라 기우는 좌우
         * 이동은 시야를 옆으로 굴려 방 밖으로 내보냅니다.
         *
         * SPACE/C는 제거하지 않고 *월드* 수직 이동으로 남깁니다. 벽을 마주 본 채 수직으로
         * 상승하는 것은 실제 편집 동작입니다. 벽의 전체 높이를 확인하거나, 작업하던 대상에서
         * 시선을 떼지 않고 구덩이에서 빠져나오는 경우입니다. 두 방식은 서로 다른 질문에
         * 답하며, 위에 언급한 모든 에디터가 그래서 둘 다 제공합니다. */
        if (GetKeyState(VK_CONTROL) >= 0) {
            float sp = (GetKeyState(VK_SHIFT) < 0 ? 18.0f : 7.0f) * dt;
            v3 fwd, right, up;
            view_basis(&fwd, &right, &up);
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

        /* Consume the frame's input edges once the UI has seen them.
         *
         * Cleared here rather than in ui_begin because the window procedure
         * runs between frames: a click that arrives after ui_begin and before
         * the next one must survive until a frame actually reads it. Clearing
         * at the point of consumption is what makes a fast click impossible to
         * lose, and dropping one on a button is the sort of thing that gets
         * blamed on the mouse.
         *
         * `down` is deliberately NOT cleared -- it is a level, not an edge, and
         * it says whether the button is held right now.
         *
         * UI가 확인한 뒤 이번 프레임의 입력 엣지를 소비합니다.
         *
         * ui_begin이 아니라 이곳에서 지우는 이유는 창 프로시저가 프레임 사이에 실행되기
         * 때문입니다. ui_begin 이후, 다음 ui_begin 이전에 도착한 클릭은 어떤 프레임이
         * 실제로 그것을 읽을 때까지 살아남아야 합니다. 소비 지점에서 지우는 것이 빠른
         * 클릭을 잃지 않게 하는 방법이며, 버튼에서 클릭 하나를 놓치는 것은 대개 마우스
         * 탓으로 돌려지는 종류의 일입니다.
         *
         * `down`은 의도적으로 지우지 않습니다. 엣지가 아니라 레벨이며, 지금 버튼이 눌려
         * 있는지를 말합니다. */
        g_uin.click = g_uin.release = 0;
        g_uin.ch = g_uin.backspace = g_uin.enter = g_uin.escape = 0;
        g_uin.wheel = 0.0f;

        SwapBuffers(dc);

        char title[192];
        wsprintfA(title, "mapedit [%s]  %d sectors  %d ents  grid %d  %d verts",
                  g_level_name, g_level.n_sectors, g_level.n_ents, g_grid,
                  g_mesh_buf.count);
        SetWindowTextA(g_wnd, title);
    }
    return 0;
}
