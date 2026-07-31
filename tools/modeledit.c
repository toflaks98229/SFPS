/* modeledit -- draw weapon outlines with the mouse instead of typing numbers.
 *
 * Left half:  the 2D profile you edit. Drag points, insert, delete.
 * Right half: the extruded result, live, using the game's own geometry code.
 *
 * A full 3D modeller would be the wrong tool here: the format IS a 2D profile
 * plus a thickness, so anything you could only express in 3D could not be
 * saved. Editing the profile directly is both simpler and complete.
 *
 * Saving rewrites only this model's block inside assets/models.txt, leaving
 * the file's documentation header and any other models untouched. The game,
 * if running from a -Debug build, hot-reloads the change immediately.
 *
 *   drag             move a point, or the muzzle (snapped to whole units)
 *   ctrl + click     insert a point on the nearest edge
 *   click empty      deselect
 *   DEL / X          delete the selected point
 *   [  ]             previous / next part
 *   N                new part (a square at the origin)
 *   M / SHIFT+M      cycle this part's material forwards / back
 *   SHIFT+DEL        delete the current part
 *   -  =             part thickness down / up
 *   ,  .             uv scale down / up
 *   L                toggle this part between extrude and lathe
 *   T                toggle per-point thickness on this part
 *   PgUp / PgDn      selected point's own thickness (per-point mode)
 *   arrows           pan            wheel   zoom
 *   right drag       orbit the 3D preview
 *   CTRL+S           save to assets/models.txt
 *   ESC              quit
 *
 * `modeledit <name> -print` loads the model and writes what a save would
 * produce to stdout, then exits without opening a window. That makes the
 * parse/serialise round trip testable without a mouse anywhere near it.
 */

#include "../src/model.h"
#include "../src/tex.h"
#include "../src/data.h"
#include "../src/txt.h"

#include <stdio.h>

#define VIEW_SPLIT 0.55f          /* fraction of the window given to 2D */

static int   g_running = 1;
static HWND  g_wnd;
static Model g_model;
static char  g_model_name[32] = "shotgun";

/* ------------------------------------------------------------ 2D viewport */

static float g_cam_x = -20.0f, g_cam_y = -10.0f;  /* centre, in model units */
static float g_upp   = 0.30f;                     /* units per pixel */

static int   g_part  = 0;

/* g_point indexes into the current part, except for this sentinel, which
   means the muzzle handle is selected instead. */
#define SEL_MUZZLE (-2)
static int   g_point = -1;
static int   g_dragging;

/* ------------------------------------------------------------ 3D viewport */

static float g_orbit_yaw = 0.9f, g_orbit_pitch = 0.35f, g_orbit_dist = 3.0f;
static v3    g_orbit_target;
static int   g_orbiting;
static POINT g_orbit_prev;

static Mesh   g_mesh3d, g_lines, g_quads;
static MeshBuf g_line_buf, g_quad_buf, g_mesh_buf;

/* Same per-material split the game draws with, so the preview shows the real
   materials rather than one stand-in texture. */
static MdlRange g_ranges[MDL_MAX_RANGES];
static Mat      g_range_tex[MDL_MAX_RANGES];
static int      g_range_count;

static int    g_dirty = 1;        /* geometry needs rebuilding */
static char   g_status[128] = "ready";

/* ------------------------------------------------------------------ utils */

static int vp2d_w(int w) { return (int)(w * VIEW_SPLIT); }

static void screen_to_world(int sx, int sy, int w, int h, float *wx, float *wy) {
    int vw = vp2d_w(w);
    *wx = g_cam_x + (sx - vw * 0.5f) * g_upp;
    *wy = g_cam_y - (sy - h  * 0.5f) * g_upp;
}

static void world_to_screen(float wx, float wy, int w, int h, float *sx, float *sy) {
    int vw = vp2d_w(w);
    *sx = (wx - g_cam_x) / g_upp + vw * 0.5f;
    *sy = h * 0.5f - (wy - g_cam_y) / g_upp;
}

static void status(const char *fmt, int a, int b) {
    wsprintfA(g_status, fmt, a, b);
}

/* --------------------------------------------------------------- editing */

static MdlPart *cur(void) {
    if (g_model.n_parts == 0) return 0;
    if (g_part >= g_model.n_parts) g_part = g_model.n_parts - 1;
    if (g_part < 0) g_part = 0;
    return &g_model.parts[g_part];
}

/* Nearest handle across all parts, within a small screen radius. The muzzle
   competes with the outline points, so it is dragged the same way they are. */
static int pick_point(int sx, int sy, int w, int h, int *out_part) {
    float best = 12.0f * 12.0f;
    int found = -1;

    {
        float px, py;
        world_to_screen(g_model.muzzle[2], g_model.muzzle[1], w, h, &px, &py);
        float dx = px - sx, dy = py - sy;
        float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; found = SEL_MUZZLE; *out_part = g_part; }
    }

    for (int p = 0; p < g_model.n_parts; p++) {
        MdlPart *part = &g_model.parts[p];
        for (int i = 0; i < part->n; i++) {
            float px, py;
            world_to_screen(part->pts[i*2], part->pts[i*2+1], w, h, &px, &py);
            float dx = px - sx, dy = py - sy;
            float d2 = dx * dx + dy * dy;
            if (d2 < best) { best = d2; found = i; *out_part = p; }
        }
    }
    return found;
}

/* Splits the edge of the current part nearest the cursor. Insertion has to
   happen on an edge rather than at the end of the list, or the outline
   self-intersects the moment you add a point anywhere but the tail. */
static void insert_point(float wx, float wy) {
    MdlPart *part = cur();
    if (!part || part->n >= MB_MAX_SILHOUETTE) return;

    int best_edge = 0;
    float best_d = 1e30f;

    for (int i = 0; i < part->n; i++) {
        int j = (i + 1) % part->n;
        float ax = part->pts[i*2], ay = part->pts[i*2+1];
        float bx = part->pts[j*2], by = part->pts[j*2+1];
        float ex = bx - ax, ey = by - ay;
        float len2 = ex * ex + ey * ey;
        float t = len2 > 0 ? ((wx - ax) * ex + (wy - ay) * ey) / len2 : 0;
        t = clampf(t, 0.0f, 1.0f);
        float dx = ax + ex * t - wx, dy = ay + ey * t - wy;
        float d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best_edge = i; }
    }

    int at = best_edge + 1;
    for (int i = part->n; i > at; i--) {
        part->pts[i*2]   = part->pts[(i-1)*2];
        part->pts[i*2+1] = part->pts[(i-1)*2+1];
        part->thick[i]   = part->thick[i-1];
    }
    part->pts[at*2]   = (short)(wx + (wx < 0 ? -0.5f : 0.5f));
    part->pts[at*2+1] = (short)(wy + (wy < 0 ? -0.5f : 0.5f));
    part->thick[at]   = (short)part->th;
    part->n++;
    g_point = at;
    g_dirty = 1;
    status("inserted point %d of %d", at, part->n);
}

static void delete_point(void) {
    MdlPart *part = cur();
    if (!part || g_point < 0 || part->n <= 3) return;
    for (int i = g_point; i < part->n - 1; i++) {
        part->pts[i*2]   = part->pts[(i+1)*2];
        part->pts[i*2+1] = part->pts[(i+1)*2+1];
        part->thick[i]   = part->thick[i+1];
    }
    part->n--;
    if (g_point >= part->n) g_point = part->n - 1;
    g_dirty = 1;
    status("deleted point, %d left", part->n, 0);
}

static void new_part(void) {
    if (g_model.n_parts >= MDL_MAX_PARTS) return;
    MdlPart *p = &g_model.parts[g_model.n_parts];
    static const short square[8] = { -10, 10,  10, 10,  10, -10,  -10, -10 };
    for (int i = 0; i < 8; i++) p->pts[i] = square[i];
    for (int i = 0; i < 4; i++) p->thick[i] = 5;
    p->n = 4; p->th = 5; p->tapered = 0; p->kind = MDL_EXTRUDE; p->segments = 12;
    p->rot = 0; p->at[0] = p->at[1] = p->at[2] = 0;
    g_part = g_model.n_parts++;
    g_point = -1;
    g_dirty = 1;
    status("new part %d", g_part, 0);
}

static void delete_part(void) {
    if (g_model.n_parts <= 1) return;
    for (int i = g_part; i < g_model.n_parts - 1; i++)
        g_model.parts[i] = g_model.parts[i + 1];
    g_model.n_parts--;
    if (g_part >= g_model.n_parts) g_part = g_model.n_parts - 1;
    g_point = -1;
    g_dirty = 1;
    status("deleted part, %d left", g_model.n_parts, 0);
}

static void toggle_taper(void) {
    MdlPart *p = cur();
    if (!p) return;
    p->tapered = !p->tapered;
    /* Entering taper mode starts flat at the part's thickness, so the shape
       does not jump; leaving it drops back to that single number. */
    for (int i = 0; i < p->n; i++) p->thick[i] = (short)p->th;
    g_dirty = 1;
    status(p->tapered ? "part %d: per-point thickness ON (PgUp/PgDn)"
                      : "part %d: uniform thickness", g_part, 0);
}

/* The only place thickness changes. A uniform part edits `th`; a tapered one
   shifts every point by the same delta so the taper's shape is preserved. */
static void adjust_thickness(int delta) {
    MdlPart *p = cur();
    if (!p) return;

    if (p->tapered) {
        for (int i = 0; i < p->n; i++) {
            int v = p->thick[i] + delta;
            p->thick[i] = (short)(v < 1 ? 1 : v);
        }
        status("part %d: all points %+d", g_part, delta);
    } else {
        int v = p->th + delta;
        p->th = v < 1 ? 1 : v;
        for (int i = 0; i < p->n; i++) p->thick[i] = (short)p->th;
        status("part %d thickness %d", g_part, p->th);
    }
    g_dirty = 1;
}

static void adjust_point_thickness(int delta) {
    MdlPart *p = cur();
    if (!p || g_point < 0 || g_point >= p->n) return;
    if (!p->tapered) {
        status("part %d is uniform -- press T first", g_part, 0);
        return;
    }
    int v = p->thick[g_point] + delta;
    p->thick[g_point] = (short)(v < 1 ? 1 : v);
    g_dirty = 1;
    status("point %d thickness %d", g_point, p->thick[g_point]);
}

static int same_str(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return !*a && !*b;
}

/* Material names come from the recipe text rather than a list in here, so a
   new material in assets/textures.txt is immediately selectable. */
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

static void cycle_material(int dir) {
    MdlPart *part = cur();
    if (!part) return;

    char names[16][16];
    int n = material_names(names, 16);
    if (!n) return;

    int at_i = 0;
    for (int i = 0; i < n; i++) if (same_str(names[i], part->mat)) { at_i = i; break; }
    at_i = (at_i + dir + n) % n;

    int k = 0;
    for (; names[at_i][k] && k < (int)sizeof(part->mat) - 1; k++)
        part->mat[k] = names[at_i][k];
    part->mat[k] = 0;
    g_dirty = 1;
    status("part %d material -> %d", g_part, at_i);
}

/* ---------------------------------------------------------------- saving */

/* assets/ sits beside the source tree, but the exe lives in build/. */
static void asset_path(char *out, int cap) {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(0, exe, MAX_PATH);
    while (n > 0 && exe[n-1] != '\\') n--;
    if (n > 1) n--;
    while (n > 0 && exe[n-1] != '\\') n--;

    int i = 0;
    for (; i < (int)n && i < cap - 1; i++) out[i] = exe[i];
    const char *rel = "assets\\models.txt";
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

/* Emits the model in the same grammar the parser reads. */
static int serialise(char *buf, int cap) {
    int at = 0;
    at = append(buf, at, cap, "m ");
    at = append(buf, at, cap, g_model_name);
    at = append(buf, at, cap, "\nuv ");
    at = append_int(buf, at, cap, g_model.uv);
    at = append(buf, at, cap, "\nmuzzle ");
    for (int i = 0; i < 3; i++) {
        at = append_int(buf, at, cap, g_model.muzzle[i]);
        at = append(buf, at, cap, i < 2 ? " " : "\n");
    }

    int last_th = -99999, last_seg = -1;
    short last_at[3] = {0, 0, 0};
    short last_rot = 0;
    char last_mat[16] = "metal";       /* the parser's own default */

    for (int p = 0; p < g_model.n_parts; p++) {
        MdlPart *part = &g_model.parts[p];

        /* Anything the editor cannot edit still has to survive a save. A
           serialiser that only writes what it understands deletes the rest,
           which is how the materials vanished the first time. */
        if (!same_str(part->mat, last_mat)) {
            at = append(buf, at, cap, "mat ");
            at = append(buf, at, cap, part->mat);
            at = append(buf, at, cap, "\n");
            int k = 0;
            for (; part->mat[k] && k < (int)sizeof(last_mat) - 1; k++)
                last_mat[k] = part->mat[k];
            last_mat[k] = 0;
        }

        /* Mesh parts come from Blender and have no points to edit. Writing
           them straight back keeps a save from silently deleting them. */
        if (part->kind == MDL_MESH) {
            at = append(buf, at, cap, "mesh ");
            at = append(buf, at, cap, part->mesh);
            at = append(buf, at, cap, "\n");
            continue;
        }

        int tapered = part->tapered;

        if (part->at[0] != last_at[0] || part->at[1] != last_at[1] ||
            part->at[2] != last_at[2]) {
            at = append(buf, at, cap, "at ");
            for (int i = 0; i < 3; i++) {
                at = append_int(buf, at, cap, part->at[i]);
                at = append(buf, at, cap, i < 2 ? " " : "\n");
            }
            for (int i = 0; i < 3; i++) last_at[i] = part->at[i];
        }
        if (part->rot != last_rot) {
            at = append(buf, at, cap, "rot ");
            at = append_int(buf, at, cap, part->rot);
            at = append(buf, at, cap, "\n");
            last_rot = part->rot;
        }
        if (!tapered && part->th != last_th) {
            at = append(buf, at, cap, "th ");
            at = append_int(buf, at, cap, part->th);
            at = append(buf, at, cap, "\n");
            last_th = part->th;
        }
        if (part->kind == MDL_LATHE && part->segments != last_seg) {
            at = append(buf, at, cap, "seg ");
            at = append_int(buf, at, cap, part->segments);
            at = append(buf, at, cap, "\n");
            last_seg = part->segments;
        }

        at = append(buf, at, cap,
                    part->kind == MDL_LATHE ? "rev" : (tapered ? "pt" : "p"));

        for (int i = 0; i < part->n; i++) {
            at = append(buf, at, cap, i % 4 == 0 ? "\n   " : "   ");
            at = append_int(buf, at, cap, part->pts[i*2]);
            at = append(buf, at, cap, " ");
            at = append_int(buf, at, cap, part->pts[i*2+1]);
            if (tapered) {
                at = append(buf, at, cap, " ");
                at = append_int(buf, at, cap, part->thick[i]);
            }
        }
        at = append(buf, at, cap, "\n");
    }
    return at;
}

/* Replaces just this model's block in the file, so the documentation header
   and every other model survive a save. */
static int save(void) {
    /* If the model text came from the baked copy rather than the file, this
       editor is showing a stale snapshot and writing it out would destroy
       whatever is actually in assets/. Refuse rather than corrupt. */
    if (!data_from_file(DATA_MODELS)) {
        status("REFUSING TO SAVE: not reading assets/models.txt", 0, 0);
        printf("refusing to save: models came from the baked copy, not the\n"
               "file. Build tools with -DHOT_RELOAD and run from build/.\n");
        fflush(stdout);
        return 0;
    }

    char path[MAX_PATH];
    asset_path(path, MAX_PATH);

    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE) { status("save: cannot open file", 0, 0); return 0; }

    DWORD size = GetFileSize(f, 0), got = 0;
    char *old = HeapAlloc(GetProcessHeap(), 0, size + 1);
    ReadFile(f, old, size, &got, 0);
    old[got] = 0;
    CloseHandle(f);

    /* Find "m <name>" at the start of a line, and the next "m " after it. */
    int start = -1, end = (int)got;
    for (int i = 0; i < (int)got; i++) {
        int bol = (i == 0) || old[i-1] == '\n';
        if (!bol || old[i] != 'm' || old[i+1] != ' ') continue;

        int k = i + 2;
        while (old[k] == ' ') k++;
        int j = 0;
        while (g_model_name[j] && old[k + j] == g_model_name[j]) j++;
        int name_ends = old[k+j] == '\n' || old[k+j] == '\r' || old[k+j] == ' ';

        if (start < 0 && !g_model_name[j] && name_ends) { start = i; continue; }
        if (start >= 0) { end = i; break; }
    }
    if (start < 0) { start = (int)got; end = (int)got; }

    int cap = size + 8192;
    char *out = HeapAlloc(GetProcessHeap(), 0, cap);
    int at = 0;
    for (int i = 0; i < start && at < cap - 1; i++) out[at++] = old[i];
    out[at] = 0;

    at += serialise(out + at, cap - at);
    at = append(out, at, cap, "\n");
    for (int i = end; i < (int)got && at < cap - 1; i++) out[at++] = old[i];

    f = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, 0);
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

static void line2d(MeshBuf *b, float x0, float y0, float x1, float y1) {
    mb_line(b, v3f(x0, y0, 0), v3f(x1, y1, 0));
}

static void point_quad(MeshBuf *b, float x, float y, float r) {
    mb_billboard(b, v3f(x, y, 0), v3f(1, 0, 0), v3f(0, 1, 0), r * 2, r * 2);
}

static void draw_2d(int w, int h) {
    int vw = vp2d_w(w);
    glViewport(0, 0, vw, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    float halfw = vw * 0.5f * g_upp, halfh = h * 0.5f * g_upp;
    mat4 proj = mat4_ortho(g_cam_x - halfw, g_cam_x + halfw,
                           g_cam_y - halfh, g_cam_y + halfh, -1, 1);
    rd_mvp(proj);
    rd_mode(RD_FLAT);

    /* --- grid --- */
    mb_reset(&g_line_buf);
    float l = g_cam_x - halfw, r = g_cam_x + halfw;
    float bo = g_cam_y - halfh, t = g_cam_y + halfh;
    for (int x = (int)(l / 10) * 10; x <= (int)r; x += 10)
        line2d(&g_line_buf, (float)x, bo, (float)x, t);
    for (int y = (int)(bo / 10) * 10; y <= (int)t; y += 10)
        line2d(&g_line_buf, l, (float)y, r, (float)y);
    mesh_upload(&g_lines, &g_line_buf, 1);
    rd_color(1, 1, 1, 0.06f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mesh_draw_lines(&g_lines);

    /* --- axes --- */
    mb_reset(&g_line_buf);
    line2d(&g_line_buf, l, 0, r, 0);
    line2d(&g_line_buf, 0, bo, 0, t);
    mesh_upload(&g_lines, &g_line_buf, 1);
    rd_color(0.4f, 0.6f, 1.0f, 0.35f);
    mesh_draw_lines(&g_lines);

    /* --- outlines: other parts dim, current part bright --- */
    for (int pass = 0; pass < 2; pass++) {
        mb_reset(&g_line_buf);
        for (int p = 0; p < g_model.n_parts; p++) {
            if ((p == g_part) != (pass == 1)) continue;
            MdlPart *part = &g_model.parts[p];
            int closed = part->kind != MDL_LATHE;
            for (int i = 0; i < part->n - (closed ? 0 : 1); i++) {
                int j = (i + 1) % part->n;
                line2d(&g_line_buf, part->pts[i*2], part->pts[i*2+1],
                                    part->pts[j*2], part->pts[j*2+1]);
            }
        }
        if (!g_line_buf.count) continue;
        mesh_upload(&g_lines, &g_line_buf, 1);
        if (pass == 0) rd_color(0.45f, 0.50f, 0.58f, 0.75f);
        else           rd_color(1.00f, 0.78f, 0.30f, 1.00f);
        glLineWidth(pass == 1 ? 2.5f : 1.5f);
        mesh_draw_lines(&g_lines);
    }

    /* --- points of the current part --- */
    MdlPart *part = cur();
    float r_units = 4.0f * g_upp;
    if (part) {
        mb_reset(&g_quad_buf);
        for (int i = 0; i < part->n; i++)
            if (i != g_point)
                point_quad(&g_quad_buf, part->pts[i*2], part->pts[i*2+1], r_units);
        if (g_quad_buf.count) {
            mesh_upload(&g_quads, &g_quad_buf, 1);
            rd_color(1.0f, 0.85f, 0.45f, 1.0f);
            mesh_draw(&g_quads);
        }
        if (g_point >= 0 && g_point < part->n) {
            mb_reset(&g_quad_buf);
            point_quad(&g_quad_buf, part->pts[g_point*2], part->pts[g_point*2+1],
                       r_units * 1.7f);
            mesh_upload(&g_quads, &g_quad_buf, 1);
            rd_color(0.3f, 1.0f, 0.5f, 1.0f);
            mesh_draw(&g_quads);
        }
    }

    /* --- the muzzle: a crosshair, so it never reads as an outline point --- */
    {
        float mz = g_model.muzzle[2], my_ = g_model.muzzle[1];
        float k = r_units * 2.2f;
        mb_reset(&g_line_buf);
        line2d(&g_line_buf, mz - k, my_, mz + k, my_);
        line2d(&g_line_buf, mz, my_ - k, mz, my_ + k);
        /* A stub pointing down -z shows which way the shot leaves. */
        line2d(&g_line_buf, mz, my_, mz - k * 2.5f, my_);
        mesh_upload(&g_lines, &g_line_buf, 1);
        rd_color(0.35f, 0.95f, 1.0f, g_point == SEL_MUZZLE ? 1.0f : 0.7f);
        glLineWidth(g_point == SEL_MUZZLE ? 3.0f : 2.0f);
        mesh_draw_lines(&g_lines);
    }
    glDisable(GL_BLEND);
}

/* Frames the whole model in the 2D viewport. Without this the editor opens
   on whatever the default pan happened to be, with half the outline off the
   left edge -- which is exactly the "where is my model" problem the tool is
   supposed to remove. */
static void fit_2d(int vw, int h) {
    if (!g_model.n_parts) return;

    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (int p = 0; p < g_model.n_parts; p++) {
        MdlPart *part = &g_model.parts[p];
        for (int i = 0; i < part->n; i++) {
            float x = part->pts[i*2], y = part->pts[i*2+1];
            if (x < lo_x) lo_x = x;
            if (x > hi_x) hi_x = x;
            if (y < lo_y) lo_y = y;
            if (y > hi_y) hi_y = y;
        }
    }

    g_cam_x = (lo_x + hi_x) * 0.5f;
    g_cam_y = (lo_y + hi_y) * 0.5f;

    float need_x = (hi_x - lo_x) * 1.25f, need_y = (hi_y - lo_y) * 1.25f;
    float upp_x = need_x / (vw > 1 ? vw : 1);
    float upp_y = need_y / (h  > 1 ? h  : 1);
    g_upp = clampf(upp_x > upp_y ? upp_x : upp_y, 0.02f, 3.0f);
}

static void rebuild(void) {
    mb_reset(&g_mesh_buf);
    g_range_count = mdl_geometry(&g_mesh_buf, &g_model, g_ranges, MDL_MAX_RANGES);
    mesh_upload(&g_mesh3d, &g_mesh_buf, 1);
    for (int r = 0; r < g_range_count; r++)
        g_range_tex[r] = tex_mat(g_ranges[r].mat);

    v3 lo = v3f(1e9f, 1e9f, 1e9f), hi = v3f(-1e9f, -1e9f, -1e9f);
    for (int i = 0; i < g_mesh_buf.count; i++) {
        v3 p = v3f(g_mesh_buf.v[i].px, g_mesh_buf.v[i].py, g_mesh_buf.v[i].pz);
        lo = v3f(p.x < lo.x ? p.x : lo.x, p.y < lo.y ? p.y : lo.y, p.z < lo.z ? p.z : lo.z);
        hi = v3f(p.x > hi.x ? p.x : hi.x, p.y > hi.y ? p.y : hi.y, p.z > hi.z ? p.z : hi.z);
    }
    if (g_mesh_buf.count) {
        g_orbit_target = v3scale(v3add(lo, hi), 0.5f);
        float span = v3len(v3sub(hi, lo));
        if (g_orbit_dist < span * 0.5f || g_orbit_dist > span * 4.0f)
            g_orbit_dist = span * 1.3f;
    }
    g_dirty = 0;
}

static void draw_3d(int w, int h) {
    int vw = vp2d_w(w), pw = w - vw;
    if (pw < 1) return;
    glViewport(vw, 0, pw, h);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClear(GL_DEPTH_BUFFER_BIT);

    float aspect = (float)pw / (float)h;
    mat4 proj = mat4_perspective(0.9f, aspect, 0.01f, 60.0f);
    float cp = cosf(g_orbit_pitch), sp = sinf(g_orbit_pitch);
    v3 eye = v3add(g_orbit_target,
                   v3f(sinf(g_orbit_yaw) * cp * g_orbit_dist,
                       sp * g_orbit_dist,
                       cosf(g_orbit_yaw) * cp * g_orbit_dist));
    mat4 view = mat4_fps_view(eye, g_orbit_yaw, -g_orbit_pitch);

    rd_mvp(mat4_mul(proj, view));
    rd_mode(RD_VIEWMODEL);
    glActiveTexture(GL_TEXTURE0);
    for (int r = 0; r < g_range_count; r++) {
        tex_use(&g_range_tex[r]);
        mesh_draw_range(&g_mesh3d, g_ranges[r].first, g_ranges[r].count);
    }
}

/* ------------------------------------------------------------------ input */

static int ctrl_down(void)  { return GetKeyState(VK_CONTROL) < 0; }
static int shift_down(void) { return GetKeyState(VK_SHIFT)   < 0; }

static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    RECT rc; GetClientRect(wnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (h < 1) h = 1;

    int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);

    switch (msg) {
    case WM_CLOSE: case WM_DESTROY:
        g_running = 0; return 0;

    case WM_LBUTTONDOWN: {
        if (mx >= vp2d_w(w)) return 0;              /* 3D half is view-only */
        float wx, wy; screen_to_world(mx, my, w, h, &wx, &wy);
        if (ctrl_down()) { insert_point(wx, wy); g_dragging = 1; return 0; }
        int part_hit;
        int i = pick_point(mx, my, w, h, &part_hit);
        if (i >= 0) { g_part = part_hit; g_point = i; g_dragging = 1; }
        else        { g_point = -1; }
        SetCapture(wnd);
        return 0;
    }
    case WM_LBUTTONUP:
        g_dragging = 0; ReleaseCapture(); return 0;

    case WM_RBUTTONDOWN:
        g_orbiting = 1; GetCursorPos(&g_orbit_prev); SetCapture(wnd); return 0;
    case WM_RBUTTONUP:
        g_orbiting = 0; ReleaseCapture(); return 0;

    case WM_MOUSEMOVE: {
        if (g_dragging && g_point != -1) {
            float wx, wy; screen_to_world(mx, my, w, h, &wx, &wy);
            /* Snap to whole units: the format is integers, so a point that
               looks placed must actually be placed. */
            short sx_ = (short)(wx < 0 ? wx - 0.5f : wx + 0.5f);
            short sy_ = (short)(wy < 0 ? wy - 0.5f : wy + 0.5f);

            if (g_point == SEL_MUZZLE) {
                g_model.muzzle[2] = sx_;      /* horizontal axis is z */
                g_model.muzzle[1] = sy_;
                g_model.has_muzzle = 1;
                status("muzzle z %d y %d", sx_, sy_);
            } else {
                MdlPart *part = cur();
                if (part && g_point < part->n) {
                    part->pts[g_point*2]   = sx_;
                    part->pts[g_point*2+1] = sy_;
                    g_dirty = 1;
                }
            }
        }
        if (g_orbiting) {
            POINT c; GetCursorPos(&c);
            g_orbit_yaw   += (c.x - g_orbit_prev.x) * 0.01f;
            g_orbit_pitch = clampf(g_orbit_pitch + (c.y - g_orbit_prev.y) * 0.01f,
                                   -1.5f, 1.5f);
            g_orbit_prev = c;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int up = GET_WHEEL_DELTA_WPARAM(wp) > 0;
        POINT c = {mx, my};
        ScreenToClient(wnd, &c);
        if (c.x < vp2d_w(w)) g_upp = clampf(g_upp * (up ? 0.88f : 1.14f), 0.02f, 3.0f);
        else g_orbit_dist = clampf(g_orbit_dist * (up ? 0.88f : 1.14f), 0.05f, 40.0f);
        return 0;
    }

    case WM_KEYDOWN: {
        MdlPart *part = cur();
        switch (wp) {
        case VK_ESCAPE: g_running = 0; break;
        case VK_LEFT:   g_cam_x -= 20 * g_upp; break;
        case VK_RIGHT:  g_cam_x += 20 * g_upp; break;
        case VK_UP:     g_cam_y += 20 * g_upp; break;
        case VK_DOWN:   g_cam_y -= 20 * g_upp; break;
        case VK_DELETE: case 'X':
            if (shift_down()) delete_part(); else delete_point();
            break;
        case VK_OEM_4:  g_part = (g_part + g_model.n_parts - 1) % g_model.n_parts;
                        g_point = -1; status("part %d of %d", g_part, g_model.n_parts); break;
        case VK_OEM_6:  g_part = (g_part + 1) % g_model.n_parts;
                        g_point = -1; status("part %d of %d", g_part, g_model.n_parts); break;
        case 'N':       new_part(); break;
        case 'M':       cycle_material(shift_down() ? -1 : 1); break;
        case 'L':       if (part) { part->kind = part->kind == MDL_LATHE ? MDL_EXTRUDE : MDL_LATHE;
                            g_dirty = 1;
                            status(part->kind == MDL_LATHE ? "part %d: lathe" : "part %d: extrude",
                                   g_part, 0); }
                        break;
        case 'T':       toggle_taper(); break;
        /* Both the main row and the numeric keypad, so it does not matter
           which minus key gets pressed. */
        case VK_OEM_MINUS: case VK_SUBTRACT: adjust_thickness(-1); break;
        case VK_OEM_PLUS:  case VK_ADD:      adjust_thickness(+1); break;
        case VK_PRIOR:  adjust_point_thickness(+1); break;
        case VK_NEXT:   adjust_point_thickness(-1); break;
        case VK_OEM_COMMA:  if (g_model.uv > 10) { g_model.uv -= 10; g_dirty = 1;
                                status("uv %d", g_model.uv, 0); } break;
        case VK_OEM_PERIOD: g_model.uv += 10; g_dirty = 1;
                            status("uv %d", g_model.uv, 0); break;
        case 'S':       if (ctrl_down()) save(); break;
        }
        return 0;
    }
    }
    return DefWindowProcA(wnd, msg, wp, lp);
}

/* ------------------------------------------------------------------- main */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;

    int print_only = 0;
    if (cmd && *cmd) {
        int i = 0;
        while (cmd[i] && cmd[i] != ' ' && i < (int)sizeof(g_model_name) - 1) {
            g_model_name[i] = cmd[i]; i++;
        }
        g_model_name[i] = 0;
        for (int k = 0; cmd[k]; k++)
            if (cmd[k] == '-' && cmd[k+1] == 'p') print_only = 1;
    }

    /* Headless round-trip check: parse, serialise, print. No GL, no input. */
    if (print_only) {
        if (!mdl_load(g_model_name, &g_model)) {
            printf("no model '%s'\n", g_model_name);
            return 1;
        }
        static char buf[16384];
        serialise(buf, sizeof(buf));
        fputs(buf, stdout);
        return 0;
    }

    if (!gl_bootstrap(inst)) {
        MessageBoxA(0, "OpenGL 3.3 unavailable.", "modeledit", MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc = {0};
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorA(0, IDC_CROSS);
    wc.lpszClassName = "medit";
    RegisterClassA(&wc);

    RECT r = {0, 0, 1400, 760};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_wnd = CreateWindowExA(0, "medit", "modeledit", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
    HDC dc = GetDC(g_wnd);
    if (!gl_make_context(dc)) {
        MessageBoxA(0, "No 3.3 core context.", "modeledit", MB_ICONERROR);
        return 1;
    }
    gl_set_vsync(1);
    ShowWindow(g_wnd, show);

    rd_init();

    if (!mdl_load(g_model_name, &g_model)) {
        printf("no model '%s' in assets/models.txt -- starting a new one\n",
               g_model_name);
        g_model.n_parts = 0;
        g_model.uv = 300;
        new_part();
    }

    mb_init(&g_line_buf, 8192);
    mb_init(&g_quad_buf, MB_MAX_SILHOUETTE * 6 + 64);
    mb_init(&g_mesh_buf, 8192);

    printf("modeledit -- editing '%s' (%d parts)\n"
           "  drag point | ctrl+click insert | DEL delete | [ ] part | N new\n"
           "  - = thickness | , . uv | L lathe | T taper | PgUp/PgDn point thickness\n"
           "  right-drag orbit | wheel zoom | CTRL+S save | ESC quit\n",
           g_model_name, g_model.n_parts);
    fflush(stdout);

    glClearColor(0.09f, 0.10f, 0.13f, 1.0f);

    {
        RECT cr; GetClientRect(g_wnd, &cr);
        fit_2d(vp2d_w(cr.right - cr.left), cr.bottom - cr.top);
    }

    while (g_running) {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        if (!g_running) break;

        if (g_dirty) rebuild();

        RECT cr; GetClientRect(g_wnd, &cr);
        int w = cr.right - cr.left, h = cr.bottom - cr.top;
        if (h < 1) h = 1;

        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_2d(w, h);
        draw_3d(w, h);
        SwapBuffers(dc);

        MdlPart *part = cur();
        char title[256];
        wsprintfA(title, "modeledit [%s]  part %d/%d  %d pts  th %d  uv %d  "
                         "%s  |  %d verts  |  %s",
                  g_model_name, g_part + 1, g_model.n_parts,
                  part ? part->n : 0, part ? part->th : 0, g_model.uv,
                  part && part->kind == MDL_LATHE ? "lathe" : "extrude",
                  g_mesh_buf.count, g_status);
        SetWindowTextA(g_wnd, title);
    }
    return 0;
}
