/* modelview -- look at a model, and tune where the view model sits.
 *
 * This tool exists because placing the shotgun was three rounds of nudging
 * five constants, rebuilding the game, taking a screenshot, and squinting.
 * That loop is the thing an editor is supposed to delete.
 *
 * It links the game's own render.c, tex.c, model.c and weapon.c, and calls
 * wp_gun_matrix() for view-model mode, so what you see here is what the game
 * draws -- not a reimplementation that drifts.
 *
 * Tool size does not count against the 1.44MB budget; only game.exe ships.
 *
 *   TAB      orbit view  <->  view-model view
 *   drag     orbit (orbit mode)
 *   wheel    zoom
 *   W A S D  offset x / y      R F  offset z
 *   Q E      base yaw          T G  base pitch
 *   Z X      scale             C V  fov
 *   SPACE    toggle idle sway and bob
 *   ENTER    print the current pose to the console, ready to paste
 *   ESC      quit
 *
 * The live pose is in the title bar. ENTER writes it as a C initialiser.
 *
 * A starting pose can also be given on the command line, which makes a pose
 * exactly reproducible instead of depending on how long a key was held:
 *
 *   modelview shotgun offx=175 offy=-90 scale=376 yaw=450
 *
 * Values are integers in thousandths (matching the title-bar readout), except
 * yaw and pitch which are millidegrees.
 */

#include "../src/weapon.h"
#include "../src/weaponview.h"
#include "../src/model.h"
#include "../src/tex.h"
#include "../src/level.h"

#include <stdio.h>

static int   g_running = 1;
static int   g_keys[256];
static int   g_viewmodel_mode = 1;
static int   g_animate = 0;
static HWND  g_wnd;

/* orbit camera */
static float g_orbit_yaw = 0.9f, g_orbit_pitch = 0.35f, g_orbit_dist = 2.2f;
static int   g_dragging;
static POINT g_drag_prev;
/* Models are authored around the muzzle/grip, not the origin, so the orbit
   target is the mesh centre or the subject drifts out of frame. */
static v3    g_orbit_target;

static Weapon g_weapon;

static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: case WM_DESTROY:
        g_running = 0; return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { g_running = 0; return 0; }
        if (wp == VK_TAB)    g_viewmodel_mode = !g_viewmodel_mode;
        if (wp == VK_SPACE)  g_animate = !g_animate;
        g_keys[wp & 0xff] = 1;
        return 0;
    case WM_KEYUP:
        g_keys[wp & 0xff] = 0; return 0;
    case WM_LBUTTONDOWN:
        g_dragging = 1; GetCursorPos(&g_drag_prev); SetCapture(w); return 0;
    case WM_LBUTTONUP:
        g_dragging = 0; ReleaseCapture(); return 0;
    case WM_MOUSEWHEEL:
        g_orbit_dist *= (GET_WHEEL_DELTA_WPARAM(wp) > 0) ? 0.88f : 1.14f;
        g_orbit_dist = clampf(g_orbit_dist, 0.15f, 12.0f);
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* Holding a key nudges continuously; dt keeps the rate framerate independent. */
static void nudge(int down_key, int up_key, float *value, float rate, float dt) {
    if (g_keys[down_key]) *value -= rate * dt;
    if (g_keys[up_key])   *value += rate * dt;
}

/* --- command line: "<model> key=int key=int ..." ------------------------- */

static int str_eq_n(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int parse_int_at(const char *s, int *out) {
    int sign = 1, v = 0, any = 0;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; }
    *out = v * sign;
    return any;
}

/* Consumes key=value pairs from `cmd`, writing into g_gun_pose. Returns a
   pointer to the model name, which is whatever leading word is not a pair. */
static const char *parse_cmdline(char *cmd, char *name_buf, int name_cap) {
    static const struct { const char *key; int len; float *field; float scale; }
    FIELDS[] = {
        {"offx=",  5, &g_gun_pose.off_x,   0.001f},
        {"offy=",  5, &g_gun_pose.off_y,   0.001f},
        {"offz=",  5, &g_gun_pose.off_z,   0.001f},
        {"scale=", 6, &g_gun_pose.scale,   0.001f},
        {"fov=",   4, &g_gun_pose.fov,     0.001f},
        {"yaw=",   4, &g_gun_pose.yaw,     0.0000174533f}, /* millidegrees */
        {"pitch=", 6, &g_gun_pose.pitch,   0.0000174533f},
        {"pivy=",  5, &g_gun_pose.pivot_y, 0.001f},
        {"pivz=",  5, &g_gun_pose.pivot_z, 0.001f},
    };
    const int N_FIELDS = (int)(sizeof(FIELDS) / sizeof(FIELDS[0]));

    name_buf[0] = 0;
    char *p = cmd;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        char *word = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        char saved = *p; *p = 0;

        int matched = 0;
        for (int i = 0; i < N_FIELDS; i++) {
            int len = FIELDS[i].len;
            if (!str_eq_n(word, FIELDS[i].key, len)) continue;
            int v;
            if (parse_int_at(word + len, &v))
                *FIELDS[i].field = v * FIELDS[i].scale;
            matched = 1;
            break;
        }
        if (!matched && !name_buf[0]) {
            int i = 0;
            while (word[i] && i < name_cap - 1) { name_buf[i] = word[i]; i++; }
            name_buf[i] = 0;
        }

        *p = saved;
        if (saved) p++;
    }

    return name_buf[0] ? name_buf : "shotgun";
}

static void print_pose(void) {
    const GunPose *g = &g_gun_pose;
    printf("\nGunPose g_gun_pose = {\n"
           "    .scale = %.3ff, .fov = %.3ff,\n"
           "    .off_x = %.3ff, .off_y = %.3ff, .off_z = %.3ff,\n"
           "    .yaw   = %.3ff,  .pitch = %.3ff,\n"
           "    .pivot_y = %.3ff, .pivot_z = %.3ff\n};\n",
           g->scale, g->fov, g->off_x, g->off_y, g->off_z,
           g->yaw, g->pitch, g->pivot_y, g->pivot_z);
    fflush(stdout);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;

    static char name_buf[64];
    const char *model = parse_cmdline(cmd ? cmd : "", name_buf, sizeof(name_buf));

    if (!gl_bootstrap(inst)) {
        MessageBoxA(0, "OpenGL 3.3 unavailable.", "modelview", MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc = {0};
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorA(0, IDC_ARROW);
    wc.lpszClassName = "mvwnd";
    RegisterClassA(&wc);

    RECT r = {0, 0, 1100, 700};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_wnd = CreateWindowExA(0, "mvwnd", "modelview", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
    HDC dc = GetDC(g_wnd);
    if (!gl_make_context(dc)) {
        MessageBoxA(0, "No 3.3 core context.", "modelview", MB_ICONERROR);
        return 1;
    }
    gl_set_vsync(1);
    ShowWindow(g_wnd, show);

    rd_init();

    /* Per-material ranges, exactly as the game draws them. */
    Model mdl;
    MdlRange ranges[MDL_MAX_RANGES];
    Mat      range_tex[MDL_MAX_RANGES];
    int      range_count = 0;

    /* wp_init records the level its shots trace against. The viewer never
       fires, but handing it the real level keeps the tool on exactly the
       same code path as the game.

       The view is a local rather than a Scene: this tool draws one model, not
       a world, and wpview_set_model is the whole of what it wants from it.
       Scene이 아니라 지역 변수인 이유는, 이 도구가 월드가 아니라 모델 하나를 그리며
       wpview_set_model이 그로부터 원하는 전부이기 때문입니다. */
    static Level level;
    level_load("arena", &level);
    wp_init(&g_weapon);

    static WeaponView view;
    wpview_init(&view, &g_weapon);
    wpview_set_model(&view, &g_weapon, model);

    MeshBuf mb;
    mb_init(&mb, MDL_MAX_VERTS);
    int ok = mdl_load(model, &mdl);
    if (ok) {
        range_count = mdl_geometry(&mb, &mdl, ranges, MDL_MAX_RANGES);
        for (int r = 0; r < range_count; r++) range_tex[r] = tex_mat(ranges[r].mat);
    }
    /* Frame the model: orbit its centre, and back off far enough to see all
       of it whatever its size. */
    v3 lo = v3f(1e9f, 1e9f, 1e9f), hi = v3f(-1e9f, -1e9f, -1e9f);
    for (int i = 0; i < mb.count; i++) {
        v3 p = v3f(mb.v[i].px, mb.v[i].py, mb.v[i].pz);
        lo = v3f(p.x < lo.x ? p.x : lo.x, p.y < lo.y ? p.y : lo.y,
                 p.z < lo.z ? p.z : lo.z);
        hi = v3f(p.x > hi.x ? p.x : hi.x, p.y > hi.y ? p.y : hi.y,
                 p.z > hi.z ? p.z : hi.z);
    }
    g_orbit_target = v3scale(v3add(lo, hi), 0.5f);
    g_orbit_dist   = v3len(v3sub(hi, lo)) * 1.1f;

    Mesh mesh = {0};
    mesh_upload(&mesh, &mb, 0);
    int verts = mb.count;
    mb_free(&mb);

    if (!ok) {
        char msg[128];
        wsprintfA(msg, "No model named '%s' in MODELS.", model);
        MessageBoxA(0, msg, "modelview", MB_ICONERROR);
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
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

        if (g_dragging) {
            POINT c; GetCursorPos(&c);
            g_orbit_yaw   += (c.x - g_drag_prev.x) * 0.01f;
            g_orbit_pitch += (c.y - g_drag_prev.y) * 0.01f;
            g_orbit_pitch = clampf(g_orbit_pitch, -1.5f, 1.5f);
            g_drag_prev = c;
        }

        GunPose *g = &g_gun_pose;
        nudge('A', 'D', &g->off_x, 0.25f, dt);
        nudge('S', 'W', &g->off_y, 0.25f, dt);
        nudge('F', 'R', &g->off_z, 0.40f, dt);
        nudge('Q', 'E', &g->yaw,   0.80f, dt);
        nudge('G', 'T', &g->pitch, 0.80f, dt);
        nudge('Z', 'X', &g->scale, 0.40f, dt);
        nudge('C', 'V', &g->fov,   0.60f, dt);
        g->scale = clampf(g->scale, 0.05f, 3.0f);
        g->fov   = clampf(g->fov,   0.30f, 2.5f);

        if (g_keys[VK_RETURN]) { print_pose(); g_keys[VK_RETURN] = 0; }

        /* Idle sway/bob, so the pose can be judged in motion rather than
           frozen -- a placement that looks fine still can read badly moving. */
        if (g_animate) {
            g_weapon.bob_phase += dt * 6.0f;
            g_weapon.sway_x = sinf(g_weapon.bob_phase * 0.37f) * 0.03f;
            g_weapon.sway_y = cosf(g_weapon.bob_phase * 0.29f) * 0.02f;
        } else {
            g_weapon.bob_phase = 0;
            g_weapon.sway_x = g_weapon.sway_y = 0;
        }

        RECT cr; GetClientRect(g_wnd, &cr);
        int vw = cr.right - cr.left, vh = cr.bottom - cr.top;
        if (vh < 1) vh = 1;
        float aspect = (float)vw / (float)vh;

        glViewport(0, 0, vw, vh);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);
        rd_mode(RD_VIEWMODEL);

        if (g_viewmodel_mode) {
            /* Exactly what the game does, via the shared matrix. */
            mat4 proj = mat4_perspective(g->fov, aspect, 0.005f, 4.0f);
            rd_mvp(mat4_mul(proj, wp_gun_matrix(&g_weapon)));
        } else {
            mat4 proj = mat4_perspective(0.9f, aspect, 0.01f, 60.0f);
            float cp = cosf(g_orbit_pitch), sp = sinf(g_orbit_pitch);

            /* mat4_fps_view's forward is (-sin y*cos p, sin p, -cos y*cos p).
               Placing the eye at +(sin y*cp, sp, cos y*cp) from the target
               makes that forward point straight back at the target -- adding
               pi here, as a turntable normally would, aims it the other way
               and renders an empty screen. */
            v3 eye = v3add(g_orbit_target,
                           v3f(sinf(g_orbit_yaw) * cp * g_orbit_dist,
                               sp * g_orbit_dist,
                               cosf(g_orbit_yaw) * cp * g_orbit_dist));
            mat4 view = mat4_fps_view(eye, g_orbit_yaw, -g_orbit_pitch);
            rd_mvp(mat4_mul(proj, view));
        }

        for (int r = 0; r < range_count; r++) {
            tex_use(&range_tex[r]);
            mesh_draw_range(&mesh, ranges[r].first, ranges[r].count);
        }
        SwapBuffers(dc);

        static float title_t; title_t += dt;
        if (title_t > 0.1f) {
            title_t = 0;
            char t[256];
            wsprintfA(t, "modelview [%s] %d verts | %s | off %d %d %d  "
                         "yaw %d pitch %d mdeg  scale %d  fov %d  (ENTER=print)",
                      model, verts,
                      g_viewmodel_mode ? "VIEWMODEL" : "ORBIT",
                      (int)(g->off_x * 1000), (int)(g->off_y * 1000),
                      (int)(g->off_z * 1000),
                      (int)(g->yaw * 57295.8f), (int)(g->pitch * 57295.8f),
                      (int)(g->scale * 1000), (int)(g->fov * 1000));
            SetWindowTextA(g_wnd, t);
        }
    }

    print_pose();
    return 0;
}
