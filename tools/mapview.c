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
#include "wgl.h"
#include "level.h"
#include "player.h"
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
 * Two levels and a pointer, so a reload that fails leaves the last good one on
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
static Level     g_levels[2];
static Level    *g_lv = &g_levels[0];
static MeshBuf   g_buf;
static Mesh      g_mesh;
static MdlRange  g_ranges[LVL_MAX_RANGES];
static Mat       g_range_tex[LVL_MAX_RANGES];
static int       g_range_count;
static int       g_verts;

/* Parses and builds. Everything derived is rebuilt together, because a map
   reloaded halfway is a mesh drawn against another map's ranges. */
/* THROUGH level_load AND level_geometry, not brush_parse and brush_geometry.
   Those two would draw the brushes and nothing else -- no lamps, because the
   static bake reads Level::lights and runs from level_geometry. Going the long
   way round is what makes this show the picture the game shows rather than a
   subset of it, which is the claim in this file's own header.
   brush_parse와 brush_geometry가 아니라 level_load와 level_geometry를 통과합니다. 그 둘은
   브러시만 그리고 그 이상은 그리지 않습니다. 등이 없습니다. 정적 베이크는 Level::lights를
   읽고 level_geometry에서 실행되기 때문입니다. 먼 길로 도는 것이 이것을 부분집합이 아니라
   게임이 보여 주는 그림을 보여 주게 만들며, 그것이 이 파일 머리말 자신의 주장입니다. */
static int load_map(const char *name) {
    Level *next = (g_lv == &g_levels[0]) ? &g_levels[1] : &g_levels[0];
    if (!level_load(name, next)) return 0;   /* the live level is untouched */
    if (!next->brushes) return 0;            /* a sector level is not ours to show */

    g_lv = next;

    mb_reset(&g_buf);
    g_range_count = level_geometry(&g_buf, g_lv, g_ranges, LVL_MAX_RANGES);
    g_verts = g_buf.count;
    mesh_upload(&g_mesh, &g_buf, 1);

    /* Materials last, exactly as scene_build_level does it: the ranges decide
       how many there are and each names what it wants. */
    for (int i = 0; i < g_range_count; i++)
        g_range_tex[i] = tex_mat(g_ranges[i].mat);

    return 1;
}

/* Puts the camera where the level says the player begins, at eye height, so the
   first thing seen is what the author framed.
   ::Level::start and ::Level::start_h rather than the entity, because level.c
   has already done that reading and done the angle conversion with it. Doing it
   again here would be a second place for "where does the player start" to be
   decided, and the two would drift the moment either changed.
   엔티티가 아니라 ::Level::start와 ::Level::start_h를 씁니다. level.c가 이미 그것을 읽었고
   각도 변환도 함께 마쳤기 때문입니다. 이곳에서 다시 하면 "플레이어는 어디서 시작하는가"를
   결정하는 두 번째 자리가 생기고, 어느 한쪽이 바뀌는 순간 둘은 어긋납니다. */
static void place_camera(void) {
    g_eye = v3f(g_lv->start[0] * 0.01f,
                g_lv->start_h  * 0.01f + PLAYER_EYE,
                g_lv->start[1] * 0.01f);
    g_yaw = g_lv->start[2] * 0.0000174533f;   /* millidegrees -> radians */
    g_pitch = 0.0f;
}

static const char *map_name_of(const char *cmd, char *buf, int cap) {
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    int i = 0;
    while (cmd[i] && cmd[i] != ' ' && cmd[i] != '\t' && i < cap - 1) {
        buf[i] = cmd[i]; i++;
    }
    buf[i] = 0;
    return buf[0] ? buf : "lqdm4";
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
           name, g_lv->brushes->n_ents, g_lv->brushes->n_brushes,
           g_lv->brushes->n_faces, g_verts, g_range_count);
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
           passing no dynamic lights -- which since the lamps became dynamic
           means no lamps either, so what is on screen is AMBIENT, the shader's
           key, and whatever sun the bake put in the vertices. Deliberate, and
           worth knowing while judging a texture: it is flatter here than the
           lit game will be. dithershot is the tool that shows the lighting;
           this one shows the surface. */
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
                      name, g_lv->brushes->n_brushes, g_lv->brushes->n_faces, g_verts,
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
