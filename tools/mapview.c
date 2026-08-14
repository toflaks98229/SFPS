/* mapview -- walk through a .map, drawn by the game's own renderer.
 *
 * The numbers in maptest say the geometry is right. They cannot say it LOOKS
 * right, and the thing this whole format was adopted for -- a texture fitted to
 * a face instead of projected through it -- is a claim about what you see. So
 * this exists to see it.
 *
 * It links src\brush.c, src\render.c and src\tex.c and draws through rd_mode
 * (RD_WORLD) with tex_mat materials, which is the same path scene.c takes. A
 * viewer that reimplemented any of that would agree with the game right up
 * until it stopped.
 *
 * NO COLLISION. The camera flies. Brush collision is the next step and does not
 * exist yet, and a viewer that pretended otherwise would be testing a stub.
 *
 *   right drag   look
 *   W A S D      move, E / Q up and down
 *   SHIFT        move faster
 *   TAB          wireframe
 *   R            reload the map from disk
 *   ESC          quit
 *
 * Tool size does not count against the 1.44MB budget; only game.exe ships.
 *
 * The map is named on the command line and defaults to atrium:
 *
 *   mapview atrium
 *
 * A HOT_RELOAD build -- which build.ps1 makes every tool -- watches the file.
 * Save in TrenchBroom and the room changes here without a rebuild, which is the
 * loop this whole direction exists to get back.
 *
 * maptest의 숫자들은 지오메트리가 옳다고 말합니다. 그것이 *옳아 보이는지*는 말하지 못하며,
 * 이 형식을 채택한 이유 자체(면을 통과해 투영하는 대신 면에 맞춰지는 텍스처)는 눈에 보이는
 * 것에 관한 주장입니다. 그래서 그것을 보기 위해 존재합니다.
 *
 * 충돌 판정이 없습니다. 카메라는 날아다닙니다. 브러시 충돌은 다음 단계이며 아직 존재하지
 * 않습니다. 그렇지 않은 척하는 뷰어는 껍데기를 시험하는 것입니다.
 */

#include "brush.h"
#include "data.h"
#include "diag.h"
#include "gl.h"
#include "render.h"
#include "model.h"
#include "tex.h"
#include "txt.h"

#include <stdio.h>

static int   g_running = 1;
static int   g_keys[256];
static int   g_wire;
static int   g_reload;
static HWND  g_wnd;

static v3    g_eye = {0, 1.6f, 0};
static float g_yaw, g_pitch;
static int   g_looking;
static POINT g_look_prev;

static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: case WM_DESTROY:
        g_running = 0; return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { g_running = 0; return 0; }
        if (wp == VK_TAB)    g_wire = !g_wire;
        if (wp == 'R')       g_reload = 1;
        g_keys[wp & 0xff] = 1;
        return 0;
    case WM_KEYUP:
        g_keys[wp & 0xff] = 0; return 0;
    case WM_RBUTTONDOWN:
        g_looking = 1; GetCursorPos(&g_look_prev); SetCapture(w); return 0;
    case WM_RBUTTONUP:
        g_looking = 0; ReleaseCapture(); return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* --- the map ------------------------------------------------------------- */

/**
 * Two maps and a pointer, so a reload that fails leaves the last good one on
 * screen.
 *
 * A save in progress is a file that is briefly empty or half written -- data.c's
 * reload() already guards the same moment for the .txt assets, and here it
 * matters more, because parsing into the live map would blank the room and the
 * next keystroke in the editor would not bring it back until another save. The
 * staging copy costs a few hundred kilobytes of .bss in a tool, which is
 * nothing, and buys a viewer that never goes dark while you type.
 *
 * 맵 둘과 포인터 하나입니다. 실패한 다시 읽기가 마지막으로 성공한 맵을 화면에 남기게 합니다.
 *
 * 저장 중인 파일은 잠시 비어 있거나 절반만 기록된 파일입니다. data.c의 reload()가 .txt
 * 에셋에 대해 이미 같은 순간을 방어하며, 이곳에서는 그것이 더 중요합니다. 살아 있는 맵에
 * 파싱해 넣으면 방이 비어 버리고, 에디터에서의 다음 타건은 또 한 번 저장하기 전까지 그것을
 * 되돌려 주지 않기 때문입니다. 예비 사본은 도구에서 .bss 수백 킬로바이트이며 그것은 아무것도
 * 아니고, 대신 타이핑하는 동안 결코 어두워지지 않는 뷰어를 얻습니다.
 */
static BrushMap  g_maps[2];
static BrushMap *g_map = &g_maps[0];
static MeshBuf   g_buf;
static Mesh      g_mesh;
static MdlRange  g_ranges[BR_MAX_RANGES];
static Mat       g_range_tex[BR_MAX_RANGES];
static int       g_range_count;
static int       g_ents, g_verts;

/* Parses and builds. Everything derived is rebuilt together, because a map
   reloaded halfway is a mesh drawn against another map's ranges. */
static int load_map(const char *name) {
    int len = 0;
    const char *text = data_map(name, &len);
    if (!text) return 0;

    BrushMap *next = (g_map == &g_maps[0]) ? &g_maps[1] : &g_maps[0];
    int ents = brush_parse(text, len, next);
    if (!ents) return 0;          /* the live map and its mesh are untouched */

    g_map  = next;
    g_ents = ents;

    mb_reset(&g_buf);
    g_range_count = brush_geometry(&g_buf, g_map, 0, g_map->n_brushes,
                                   g_ranges, BR_MAX_RANGES);
    g_verts = g_buf.count;
    mesh_upload(&g_mesh, &g_buf, 1);

    /* Materials last, exactly as scene_build_level does it: the ranges decide
       how many there are and each names what it wants. */
    for (int i = 0; i < g_range_count; i++)
        g_range_tex[i] = tex_mat(g_ranges[i].mat);

    return 1;
}

/* Puts the camera at the map's info_player_start, so the first thing seen is
   what the author framed. Falls back to the middle of the world's bounding box,
   because a map being built may not have a start yet and refusing to show it
   would be the least useful moment to refuse. */
static void place_camera(void) {
    for (int i = 0; i < g_map->n_ents; i++) {
        const char *cn = brush_ent_value(&g_map->ents[i], "classname");
        if (!cn || !txt_is(cn, 17, "info_player_start")) continue;

        v3 o;
        if (!brush_ent_point(&g_map->ents[i], "origin", &o)) continue;
        g_eye = o;

        /* A CONVERSION, not a copy, and the 90 is the whole of it.
           Quake's `angle` is degrees counted from +x toward +y, so 0 is east
           and 90 is north. The engine's yaw runs about a different axis: at
           yaw 0, cam_basis's forward is (0,0,-1), and engine -z is map +y --
           which is north. So the two agree only after
               engine_yaw = map_angle - 90
           Worked from the definitions rather than by trying values, because
           being 90 degrees out puts the camera against a wall on the first
           frame and looks exactly like a map authored facing the wrong way.
           변환이며 복사가 아닙니다. 90이 그 전부입니다. Quake의 `angle`은 +x에서 +y 쪽으로
           센 각도이므로 0이 동쪽이고 90이 북쪽입니다. 엔진의 yaw는 다른 축을 중심으로
           돕니다. yaw 0에서 cam_basis의 전방은 (0,0,-1)이고 엔진의 -z는 맵의 +y, 즉
           북쪽입니다. 따라서 둘은 위 식을 거쳐야만 일치합니다. 값을 넣어 보는 대신 정의에서
           유도했습니다. 90도가 어긋나면 첫 프레임부터 카메라가 벽을 향하는데, 그것은 맵이
           잘못된 방향으로 제작된 것과 똑같아 보이기 때문입니다. */
        float ang = brush_ent_num(&g_map->ents[i], "angle", 90.0f);
        /* Quake reserves -1 for "up" and -2 for "down". Neither is a heading,
           and a player start should not carry one -- but a mapper who pasted
           one from a door would otherwise get a camera pointed at the floor
           with no hint why. North is the same fallback an absent key gets.
           Quake는 -1을 "위", -2를 "아래"로 예약합니다. 어느 쪽도 방위가 아니며 플레이어
           시작 지점이 그것을 지녀서는 안 됩니다. 그러나 문에서 복사해 붙인 제작자는
           그러지 않으면 이유를 알 수 없이 바닥을 향한 카메라를 얻게 됩니다. 북쪽은 키가
           없을 때의 대체값과 같습니다. */
        if (ang < 0.0f) ang = 90.0f;
        g_yaw = (ang - 90.0f) * (M_PI_F / 180.0f);
        g_pitch = 0.0f;
        return;
    }

    v3 lo = v3f(1e9f, 1e9f, 1e9f), hi = v3f(-1e9f, -1e9f, -1e9f);
    for (int i = 0; i < g_map->n_brushes; i++) {
        const Brush *b = &g_map->brushes[i];
        if (b->min.x > b->max.x) continue;
        if (b->min.x < lo.x) lo.x = b->min.x;
        if (b->min.y < lo.y) lo.y = b->min.y;
        if (b->min.z < lo.z) lo.z = b->min.z;
        if (b->max.x > hi.x) hi.x = b->max.x;
        if (b->max.y > hi.y) hi.y = b->max.y;
        if (b->max.z > hi.z) hi.z = b->max.z;
    }
    if (lo.x <= hi.x) g_eye = v3scale(v3add(lo, hi), 0.5f);
}

static const char *map_name_of(const char *cmd, char *buf, int cap) {
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    int i = 0;
    while (cmd[i] && cmd[i] != ' ' && cmd[i] != '\t' && i < cap - 1) {
        buf[i] = cmd[i]; i++;
    }
    buf[i] = 0;
    return buf[0] ? buf : "atrium";
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;

    static char name_buf[64];
    const char *name = map_name_of(cmd ? cmd : "", name_buf, sizeof(name_buf));

    if (!gl_bootstrap(inst)) {
        MessageBoxA(0, "OpenGL 3.3 unavailable.", "mapview", MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc = {0};
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorA(0, IDC_ARROW);
    wc.lpszClassName = "mapviewwnd";
    RegisterClassA(&wc);

    RECT r = {0, 0, 1100, 700};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_wnd = CreateWindowExA(0, "mapviewwnd", "mapview", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, 0, 0, inst, 0);
    HDC dc = GetDC(g_wnd);
    if (!gl_make_context(dc)) {
        MessageBoxA(0, "No 3.3 core context.", "mapview", MB_ICONERROR);
        return 1;
    }
    gl_set_vsync(1);
    ShowWindow(g_wnd, show);

    rd_init();
    mb_init(&g_buf, 200000);

    if (!load_map(name)) {
        char msg[192];
        wsprintfA(msg, "No map named '%s'.\n\n"
                       "Looked for assets\\maps\\%s.map and in the baked blob.",
                  name, name);
        MessageBoxA(0, msg, "mapview", MB_ICONERROR);
        return 1;
    }
    place_camera();

    printf("mapview [%s]  %d entities, %d brushes, %d faces, %d verts, %d runs\n",
           name, g_ents, g_map->n_brushes, g_map->n_faces, g_verts, g_range_count);
    for (int i = 0; i < g_range_count; i++)
        printf("  %-16s %6d verts\n", g_ranges[i].mat, g_ranges[i].count);
    fflush(stdout);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);

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

        /* data_poll reports that the watched .map changed on disk; it does not
           re-read it, so the reload happens here where nothing is mid-parse.
           R does the same thing by hand, for the case where the file was
           replaced rather than saved over. */
        if (data_poll() || g_reload) {
            g_reload = 0;
            load_map(name);
        }

        if (g_looking) {
            POINT c; GetCursorPos(&c);
            g_yaw   -= (c.x - g_look_prev.x) * 0.005f;
            g_pitch -= (c.y - g_look_prev.y) * 0.005f;
            g_pitch  = clampf(g_pitch, -1.5f, 1.5f);
            g_look_prev = c;
        }

        CamBasis cb = cam_basis(g_yaw, g_pitch, 0.0f);
        float speed = g_keys[VK_SHIFT] ? 18.0f : 6.0f;
        v3 flat = v3norm(v3f(cb.fwd.x, 0, cb.fwd.z));
        if (g_keys['W']) g_eye = v3add(g_eye, v3scale(flat,     speed * dt));
        if (g_keys['S']) g_eye = v3add(g_eye, v3scale(flat,    -speed * dt));
        if (g_keys['D']) g_eye = v3add(g_eye, v3scale(cb.right, speed * dt));
        if (g_keys['A']) g_eye = v3add(g_eye, v3scale(cb.right,-speed * dt));
        if (g_keys['E']) g_eye.y += speed * dt;
        if (g_keys['Q']) g_eye.y -= speed * dt;

        RECT cr; GetClientRect(g_wnd, &cr);
        int vw = cr.right - cr.left, vh = cr.bottom - cr.top;
        if (vh < 1) vh = 1;

        glViewport(0, 0, vw, vh);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, g_wire ? GL_LINE : GL_FILL);

        mat4 proj = mat4_perspective(1.2f, (float)vw / (float)vh, 0.05f, 200.0f);
        mat4 view = mat4_fps_view(g_eye, g_yaw, g_pitch);

        /* The world pass, set up the way scene_draw_level sets it up, down to
           passing no dynamic lights: brush geometry carries no baked light yet,
           so what is on screen is AMBIENT plus the shader's key. That is worth
           knowing while judging a texture -- it is flatter here than the lit
           game will be. */
        rd_mode(RD_WORLD);
        rd_mvp(mat4_mul(proj, view));
        rd_eye(g_eye);
        rd_lights(0, 0, 0);
        glActiveTexture(GL_TEXTURE0);

        for (int i = 0; i < g_range_count; i++) {
            tex_use(&g_range_tex[i]);
            mesh_draw_range(&g_mesh, g_ranges[i].first, g_ranges[i].count);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        SwapBuffers(dc);

        static float title_t; title_t += dt;
        if (title_t > 0.15f) {
            title_t = 0;
            char d[128]; d[0] = 0;
            diag_summary(d, sizeof(d));
            char t[320];
            wsprintfA(t, "mapview [%s] %d brushes %d faces %d verts %d runs | "
                         "pos %d %d %d | %s%s%s",
                      name, g_map->n_brushes, g_map->n_faces, g_verts,
                      g_range_count,
                      (int)(g_eye.x * 100), (int)(g_eye.y * 100),
                      (int)(g_eye.z * 100),
                      g_wire ? "WIRE " : "",
                      d[0] ? "diag: " : "", d);
            SetWindowTextA(g_wnd, t);
        }
    }

    mb_free(&g_buf);
    return 0;
}
