/**
 * @file main.c
 * @brief SFPS -- a 3D FPS that fits on a 1.44MB floppy. Window, input and the frame loop.
 *
 * ENGLISH
 * -------
 * This file owns only what cannot be tested headlessly: the Win32 window, the
 * GL context, raw input, and the order the per-frame update runs in. Geometry
 * and shaders live in render.c, texture recipes in tex.c, the gun and grapple
 * in weapon.c, movement in player.c.
 *
 * The update order in ::update is load-bearing rather than incidental. The
 * grapple's rope constraint resolves BEFORE ::player_move, so this frame's
 * correction is part of this frame's position rather than lagging one behind
 * -- the same reason gravity is applied at the top of player_move and not the
 * bottom.
 *
 * 한국어
 * ------
 * 이 파일은 헤드리스로 테스트할 수 없는 요소만을 담당합니다. Win32 창, GL 컨텍스트,
 * 원시 입력, 그리고 프레임별 갱신이 실행되는 순서입니다. 지오메트리와 셰이더는
 * render.c에, 텍스처 레시피는 tex.c에, 총기와 그래플은 weapon.c에, 이동은 player.c에
 * 있습니다.
 *
 * ::update의 갱신 순서는 우연이 아니라 구조적으로 중요합니다. 그래플의 로프 구속이
 * ::player_move보다 *먼저* 처리되므로, 이번 프레임의 보정이 한 프레임 뒤처지지 않고
 * 이번 프레임의 위치에 반영됩니다. 중력이 player_move의 끝이 아닌 앞부분에서 적용되는
 * 것과 같은 이유입니다.
 */

#include "render.h"
#include "model.h"    /* MdlRange by value -- level.h only forward-declares it */
#include "tex.h"
#include "weapon.h"
#include "data.h"
#include "audio.h"
#include "player.h"
#include "level.h"
#include "font.h"
#include "enemy.h"
#include "pickup.h"
#include "sprite.h"
#include "post.h"
#include "diag.h"

/* ------------------------------------------------------------------ config */

#define WIN_W 1280           ///< @brief Initial window width, pixels. / 초기 창 너비 (픽셀).
#define WIN_H 720            ///< @brief Initial window height, pixels. / 초기 창 높이 (픽셀).
#define WORLD_FOV 1.5708f    ///< @brief 90 degrees, the boomer-shooter default. / 90도. 클래식 FPS의 기본값입니다.

#define MOUSE_SENS   0.0022f ///< @brief Radians of look per pixel of mouse movement. / 마우스 1픽셀 이동당 시점 회전량 (라디안).

/* ------------------------------------------------------------------- state */

/* --- Input state / 입력 상태 --- */

/** @brief Key-down flags indexed by virtual key code. / 가상 키 코드로 인덱싱되는 키 눌림 플래그. */
static int  g_keys[256];
/** @brief Left mouse button held: the fire trigger. / 좌클릭 유지 상태. 발사 트리거입니다. */
static int  g_mouse_down;
/** @brief Right mouse button held: the grapple is a hold, not a toggle. / 우클릭 유지 상태. 그래플은 토글이 아닌 홀드 방식입니다. */
static int  g_hook_down;

/* --- Window and focus state / 창 및 포커스 상태 --- */

/** @brief Cleared to leave the message loop. / 메시지 루프를 벗어나려면 0으로 설정합니다. */
static int  g_running = 1;
/** @brief Non-zero while the window has keyboard focus. / 창이 키보드 포커스를 가지고 있으면 0이 아닙니다. */
static int  g_focused;
/**
 * @brief Set whenever the window (re)takes focus, to discard one mouse delta.
 *
 * ENGLISH
 * -------
 * Mouse look is a delta from screen centre, so the first frame after focus
 * must recentre the cursor and throw its delta away -- otherwise wherever the
 * pointer happened to be becomes one enormous look impulse, and alt-tabbing
 * back snaps the view somewhere random.
 *
 * 한국어
 * ------
 * 마우스 시점 조작은 화면 중앙으로부터의 변화량을 기준으로 하므로, 포커스를 얻은
 * 직후 첫 프레임에서는 커서를 다시 중앙에 놓고 그 변화량을 버려야 합니다. 그렇지
 * 않으면 포인터가 우연히 있던 위치가 거대한 시점 이동 입력이 되어, alt-tab으로
 * 돌아올 때마다 시야가 엉뚱한 방향으로 튑니다.
 */
static int  g_warp_mouse = 1;
/** @brief The game window. / 게임 창. */
static HWND g_wnd;

/* --- Camera and player state / 카메라 및 플레이어 상태 --- */

/** @brief The player: position, momentum, health. / 플레이어. 위치, 운동량, 체력을 포함합니다. */
static Player g_player = { {0.0f, PLAYER_EYE, 12.0f}, {0.0f, 0.0f, 0.0f}, 0, PLAYER_MAX_HP, 0.0f };
/** @brief Look angles in radians; pitch is clamped short of vertical. / 라디안 단위의 시점 각도. 피치는 수직 직전에서 제한됩니다. */
static float  g_yaw, g_pitch;
/** @brief This frame's mouse movement, also driving view model sway. / 이번 프레임의 마우스 이동량. 뷰 모델 스웨이도 함께 유발합니다. */
static float g_mouse_dx, g_mouse_dy;
/** @brief This frame's movement speed, driving the walk bob. / 이번 프레임의 이동 속도. 걷기 흔들림을 유발합니다. */
static float g_move_speed;

/* --- World state / 월드 상태 --- */

/** @brief The shotgun and grapple. / 샷건과 그래플. */
static Weapon g_weapon;
/** @brief The level currently loaded. / 현재 로드된 레벨. */
static Level  g_level;
/** @brief Reached a terminal exit -- the win screen is up. / 최종 출구에 도달함. 승리 화면이 표시된 상태입니다. */
static int    g_won;

/* ------------------------------------------------------------------ window */

/**
 * @brief Window procedure: records input edges and focus changes into globals.
 *
 * ENGLISH
 * -------
 * @param[in] w   Window handle.
 * @param[in] msg Message identifier.
 * @param[in] wp  Message-specific parameter.
 * @param[in] lp  Message-specific parameter.
 * @return 0 for handled messages, otherwise the default window procedure's result.
 * @note Only records state; all gameplay happens in ::update. Acting on input
 *       here would run at message rate rather than frame rate.
 * @note Losing focus clears every key and releases the grapple, so the player
 *       does not return from alt-tab still holding a rope or walking forward.
 *
 * 한국어
 * ------
 * @brief 창 프로시저입니다. 입력의 변화 시점과 포커스 변경을 전역 변수에 기록합니다.
 * @param[in] w   창 핸들.
 * @param[in] msg 메시지 식별자.
 * @param[in] wp  메시지별 매개변수.
 * @param[in] lp  메시지별 매개변수.
 * @return 처리한 메시지에 대해서는 0, 그 외에는 기본 창 프로시저의 반환값.
 * @note 상태를 기록하기만 합니다. 모든 게임 로직은 ::update에서 처리됩니다. 여기서
 *       입력에 반응하면 프레임 단위가 아닌 메시지 단위로 실행되기 때문입니다.
 * @note 포커스를 잃으면 모든 키를 해제하고 그래플을 놓습니다. 그래야 alt-tab에서
 *       돌아왔을 때 로프를 잡은 채이거나 계속 전진하는 상태가 되지 않습니다.
 */
static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        g_running = 0;
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { g_running = 0; return 0; }
        /* F1 toggles the pixelise/dither pass. Handled on the message edge
           rather than polled in update(), so one press is one toggle -- a
           held key would otherwise flip it every frame. Comparing the two
           looks side by side is most of how the effect gets tuned.
           F1은 픽셀화/디더 패스를 전환합니다. update()에서 폴링하지 않고 메시지
           시점에 처리하므로 한 번 누르면 한 번 전환됩니다. 그렇지 않으면 키를 누르고
           있는 동안 매 프레임 전환됩니다. 두 화면을 나란히 비교하는 것이 이 효과를
           조정하는 주된 방법입니다. */
        if (wp == VK_F1) { post_set_enabled(!post_enabled()); return 0; }
        g_keys[wp & 0xff] = 1;
        return 0;
    case WM_KEYUP:
        g_keys[wp & 0xff] = 0;
        return 0;
    case WM_LBUTTONDOWN:
        g_mouse_down = 1;
        return 0;
    case WM_LBUTTONUP:
        g_mouse_down = 0;
        return 0;
    case WM_RBUTTONDOWN:
        g_hook_down = 1;
        return 0;
    case WM_RBUTTONUP:
        g_hook_down = 0;
        return 0;
    case WM_SETFOCUS:
        g_focused = 1;
        g_warp_mouse = 1;
        ShowCursor(FALSE);
        return 0;
    case WM_KILLFOCUS:
        g_focused = 0;
        g_mouse_down = 0;
        g_hook_down = 0;
        wp_hook_release(&g_weapon);   /* do not leave it attached off-screen */
        wp_hook_arm(&g_weapon);       /* the button is up now, so rearm with it */
        ShowCursor(TRUE);
        for (int i = 0; i < 256; i++) g_keys[i] = 0;
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ------------------------------------------------------------------ update */

/**
 * @brief Advances one frame: mouse look, the grapple, movement and the weapon.
 *
 * ENGLISH
 * -------
 * @param[in] dt Timestep in seconds.
 * @warning The order of the steps below is load-bearing. The rope constraint
 *          resolves BEFORE ::player_move so this frame's correction reaches
 *          this frame's position, and ::wp_update runs last so its timers see
 *          the movement that actually happened.
 * @note Mouse look is skipped entirely while unfocused, which is what stops
 *       the view drifting when the player is in another window.
 *
 * 한국어
 * ------
 * @brief 한 프레임을 진행시킵니다. 마우스 시점, 그래플, 이동, 무기를 처리합니다.
 * @param[in] dt 시간 간격 (초).
 * @warning 아래 단계들의 순서는 구조적으로 중요합니다. 로프 구속은 ::player_move보다
 *          *먼저* 처리되어야 이번 프레임의 보정이 이번 프레임의 위치에 반영되며,
 *          ::wp_update는 마지막에 실행되어야 타이머가 실제로 발생한 이동을 반영합니다.
 * @note 포커스가 없으면 마우스 시점 처리를 완전히 건너뜁니다. 플레이어가 다른 창에
 *       있을 때 시야가 흘러가는 것을 막아 줍니다.
 */
static void update(float dt) {
    g_mouse_dx = g_mouse_dy = 0.0f;

    if (g_focused) {
        RECT rc; GetClientRect(g_wnd, &rc);
        POINT centre = {(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
        ClientToScreen(g_wnd, &centre);

        if (g_warp_mouse) {
            /* Just took focus: park the cursor and consume this frame's delta. */
            g_warp_mouse = 0;
        } else {
            POINT cur; GetCursorPos(&cur);
            g_mouse_dx = (float)(cur.x - centre.x);
            g_mouse_dy = (float)(cur.y - centre.y);

            /* The hook holds the aim still for its whole cycle -- see
               wp_hook_locks_aim for why each half needs it.

               The delta is still READ and the cursor is still recentred
               below; only the application to yaw/pitch is skipped. Skipping
               the read instead would let the pointer wander off the window
               and come back as one enormous jump the moment the hook ends,
               which is the same class of bug g_warp_mouse exists to prevent
               on focus changes. The view model's sway still gets the delta
               too, so the gun keeps reacting to the hand that is holding it.

               훅은 주기 전체에 걸쳐 조준을 고정합니다. 각 구간에 왜 필요한지는
               wp_hook_locks_aim을 참조하십시오.

               변화량은 여전히 *읽고* 커서도 아래에서 다시 중앙에 놓습니다. 건너뛰는
               것은 yaw/pitch에 적용하는 부분뿐입니다. 읽기 자체를 건너뛰면 포인터가
               창 밖으로 벗어났다가 훅이 끝나는 순간 거대한 도약으로 돌아오게 되는데,
               이는 포커스 변경 시 g_warp_mouse가 방지하는 것과 동일한 종류의
               버그입니다. 뷰 모델의 스웨이도 변화량을 그대로 받으므로, 총기는 그것을
               쥔 손에 계속 반응합니다. */
            if (!wp_hook_locks_aim(&g_weapon)) {
                g_yaw   -= g_mouse_dx * MOUSE_SENS;
                g_pitch -= g_mouse_dy * MOUSE_SENS;

                const float limit = M_PI_F * 0.49f;
                g_pitch = clampf(g_pitch, -limit, limit);
            }
        }

        SetCursorPos(centre.x, centre.y);
    }

    float cy = cosf(g_yaw), sy = sinf(g_yaw);
    v3 fwd   = v3f(-sy, 0, -cy);
    v3 right = v3f( cy, 0, -sy);

    v3 wish = v3f(0, 0, 0);
    if (g_keys['W']) wish = v3add(wish, fwd);
    if (g_keys['S']) wish = v3sub(wish, fwd);
    if (g_keys['D']) wish = v3add(wish, right);
    if (g_keys['A']) wish = v3sub(wish, right);
    wish = v3norm(wish);

    /* --- the grapple: fire, pull, damage, launch ---------------------------
       Pressing RMB throws the claw; the rest runs itself. Unlike the rope
       this replaced, there is nothing to hold and nothing to let go of -- a
       Meat Hook completes on its own and ends in an automatic launch, so the
       button's only job is to start one.

       wp_hook_fire refuses while a hook is already in the air, on cooldown,
       or until the button has been released, so handing it the held state
       every frame is safe and one press stays one throw. */
    if (g_hook_down && !g_won) {
        wp_hook_fire(&g_weapon, g_player.pos, g_yaw, g_pitch);
    } else {
        /* Button up: rearm. Unconditional, because a throw that missed also
           has to be cleared, and rearming is what the release edge means.
           wp_hook_arm deliberately does not touch the cooldown, so tapping
           fast still cannot beat the rate limit. */
        wp_hook_arm(&g_weapon);
    }

    int hooked = (g_weapon.hook_state == HOOK_PULLING);
    float speed = PLAYER_WALK * (g_keys[VK_SHIFT] ? 1.8f : 1.0f);
    /* Being reeled in, WASD stops driving position entirely. Normal walking
       SETS position every frame (see move_axis), so leaving any of it live
       would let the player simply walk out of a pull the velocity system
       cannot counteract. The claw's flight does not restrict movement at all
       -- only the pull does, and only while it lasts. */
    if (hooked) speed = 0.0f;

    g_move_speed = v3len(wish) * speed * (g_player.grounded ? 1.0f : 0.35f);

    /* The hook resolves BEFORE the player moves. Two reasons, both load
       bearing: the pull's gravity cancellation has to land on the same
       frame's gravity rather than the previous one, and the launch impulse
       has to be in `vel` before player_move integrates it -- otherwise the
       bounce lags a frame behind the arrival that caused it.

       Runs every frame regardless of the button, because flight and pull
       continue on their own once thrown. */
    wp_hook_update(&g_weapon, &g_level, &g_player.pos, &g_player.vel, dt);

    /* Jump is suppressed only while actually being pulled. It would not fire
       mid-pull anyway (jumping needs `grounded`), but suppressing it keeps
       the intent explicit rather than relying on that coincidence. */
    player_move(&g_player, &g_level, wish, speed, g_keys[VK_SPACE] && !hooked, dt);

    RECT cr; GetClientRect(g_wnd, &cr);
    int vh = cr.bottom - cr.top;
    float aspect = (float)(cr.right - cr.left) / (float)(vh < 1 ? 1 : vh);

    wp_update(&g_weapon, dt, g_focused && g_mouse_down,
              g_player.pos, g_yaw, g_pitch, g_move_speed, g_mouse_dx, g_mouse_dy,
              WORLD_FOV, aspect, &g_player.vel, g_player.grounded);
}

/* ------------------------------------------------------------------- main */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;

    if (!gl_bootstrap(inst)) {
        MessageBoxA(0, "OpenGL 3.3 is not available on this machine.",
                    "SFPS", MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc = {0};
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorA(0, IDC_ARROW);
    wc.lpszClassName = "qwnd";
    RegisterClassA(&wc);

    RECT r = {0, 0, WIN_W, WIN_H};
    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    AdjustWindowRect(&r, style, FALSE);

    g_wnd = CreateWindowExA(0, "qwnd", "SFPS", style,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top,
                            0, 0, inst, 0);
    HDC dc = GetDC(g_wnd);

    if (!gl_make_context(dc)) {
        MessageBoxA(0, "Failed to create a 3.3 core context.",
                    "SFPS", MB_ICONERROR);
        return 1;
    }
    gl_set_vsync(1);
    ShowWindow(g_wnd, show);

    rd_init();

    /* The offscreen target, sized so that magnifying it back to the window is
       an exact integer scale in both axes. See the note inside.

       Failure is not fatal -- post_begin/post_end become no-ops and the game
       renders straight to the window. Refusing to start over a cosmetic pass
       would be the wrong trade.

       오프스크린 타깃입니다. 창 크기로 다시 확대할 때 양쪽 축 모두 정확한 정수 배율이
       되도록 크기를 정합니다. 자세한 내용은 아래 주석을 참조하십시오.

       실패해도 치명적이지 않습니다. post_begin/post_end가 아무 동작도 하지 않게 되고
       게임은 창에 직접 렌더링합니다. 순전히 미관을 위한 패스 때문에 실행을 거부하는
       것은 잘못된 선택입니다. */
    {
        RECT ir; GetClientRect(g_wnd, &ir);
        int iw = ir.right - ir.left, ih = ir.bottom - ir.top;
        if (iw < 1) iw = 1;
        if (ih < 1) ih = 1;

        /* Pick an INTEGER magnification and derive the buffer from it, rather
           than fixing the height and rounding the width to suit.

           The rounded-width version shimmered. A buffer whose dimensions do
           not divide the window evenly means GL_NEAREST magnifies most source
           pixels to N screen pixels and some to N+1, and *which* ones get the
           extra row changes as the camera moves. Edges crawl and stipple
           creeps -- the artefact pixel-art renderers call pixel creep, and at
           this resolution it is the most visible flaw in the whole effect.

           Choosing the scale first makes every source pixel exactly `scale`
           screen pixels wide and tall, so the grid is stable no matter what
           the camera does. The buffer is then whatever that leaves, which is
           why the height is no longer exactly POST_HEIGHT.

           높이를 고정하고 너비를 반올림하는 대신, *정수* 확대 배율을 먼저 정하고
           버퍼 크기를 그로부터 유도합니다.

           너비를 반올림하던 방식은 깜빡임을 일으켰습니다. 버퍼 크기가 창을 정확히
           나누지 못하면 GL_NEAREST가 대부분의 원본 픽셀을 N개의 화면 픽셀로, 일부는
           N+1개로 확대하게 되며, *어느* 픽셀이 한 줄을 더 받는지가 카메라 이동에 따라
           바뀝니다. 가장자리가 기어 다니고 스티플이 흐르는데, 픽셀 아트 렌더러에서
           픽셀 크리프라 부르는 현상이며 이 해상도에서는 효과 전체를 통틀어 가장 눈에
           띄는 결함입니다.

           배율을 먼저 정하면 모든 원본 픽셀이 정확히 `scale`만큼의 화면 픽셀 폭과
           높이를 가지므로, 카메라가 무엇을 하든 격자가 안정적으로 유지됩니다. 버퍼는
           그 결과로 남는 크기가 되며, 그래서 높이가 더 이상 정확히 POST_HEIGHT는
           아닙니다. */
        int scale = ih / POST_HEIGHT;
        if (scale < 1) scale = 1;
        post_init(iw / scale, ih / scale);
    }

    audio_init();   /* silent, not fatal, if there is no output device */

    /* --- everything below is generated, nothing is loaded --- */
    font_init();

    MdlRange level_ranges[LVL_MAX_RANGES];
    Mat      level_tex[LVL_MAX_RANGES];
    int      level_range_count = 0;
    Mesh     level_mesh = {0};
    MeshBuf  level_buf;
    mb_init(&level_buf, 16384);

    /* The level the game is currently in. Kept in its own buffer, not read
       back out of g_level.name: level_load clears the destination's name field
       before parsing, so passing g_level.name back into it would blank the
       search string. Also what hot reload reloads and what an exit updates. */
    char cur_level[32] = "arena";

    level_load(cur_level, &g_level);
    level_range_count = level_geometry(&level_buf, &g_level,
                                       level_ranges, LVL_MAX_RANGES);
    mesh_upload(&level_mesh, &level_buf, 0);
    for (int i = 0; i < level_range_count; i++)
        level_tex[i] = tex_mat(level_ranges[i].mat);

    g_yaw = player_spawn(&g_player, &g_level);

    wp_init(&g_weapon, &g_level);

    /* Monsters: one sprite atlas, and one billboard buffer reused every frame.
       enemy_spawn_level reads the level's "spawn" entities. */
    GLuint  sprite_tex = sprite_atlas();
    Mesh    enemy_mesh = {0};
    MeshBuf enemy_buf;
    mb_init(&enemy_buf, ENEMY_MAX * 6);
    enemy_spawn_level(&g_level);

    /* Pickups: their own little atlas, spawned from ammo/health entities. */
    GLuint  pickup_tex = pickup_atlas();
    Mesh    pickup_mesh = {0};
    MeshBuf pickup_buf;
    mb_init(&pickup_buf, PICKUP_MAX * 6);
    pickup_spawn_level(&g_level);

    /* Monster projectiles: five quads each (halo petals + a starred core). */
    Mesh    shot_mesh = {0};
    MeshBuf shot_buf;
    mb_init(&shot_buf, ENEMY_MAX_SHOTS * 30);

    /* Text for the health readout. */
    Mesh    hud_mesh = {0};
    MeshBuf hud_buf;
    mb_init(&hud_buf, 256);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);

    LARGE_INTEGER freq, prev_t;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev_t);

    double fps_accum = 0; int fps_frames = 0;

    while (g_running) {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev_t.QuadPart)
                         / (double)freq.QuadPart);
        prev_t = now;
        if (dt > 0.1f) dt = 0.1f;

        /* The world is frozen on the win screen: look, move, fire and the AI
           all stop, but the last frame keeps drawing under the overlay. */
        if (!g_won) update(dt);

        /* Monsters advance after the player has moved, so a swing is measured
           against where the player actually ends up this frame. The module
           owns the AI; the player's health is owned here. */
        if (!g_won) {
            int dmg = enemy_update(&g_level, g_player.pos, dt);
            if (dmg > 0 && g_player.health > 0) {
                g_player.health -= dmg;
                if (g_player.health < 0) g_player.health = 0;
                g_player.hurt = 1.0f;
                audio_play("phurt", 90);
            }
        }
        if (g_player.hurt > 0.0f) g_player.hurt -= dt * 2.0f;

        /* Pickups top up health and ammo when walked over. */
        if (!g_won)
            pickup_update(g_player.pos, &g_player.health, PLAYER_MAX_HP,
                          &g_weapon.ammo, WEAPON_MAX_AMMO, dt);

        /* Reaching the exit either loads the next level or, on a level with no
           `next`, ends the game. Health and ammo carry over on a transition,
           the way a Doom episode runs -- the exit is a reward you arrive at,
           not a reset. */
        if (!g_won && level_exit_at(&g_level, g_player.pos.x, g_player.pos.z)) {
            if (!g_level.next[0]) {
                /* Terminal level: the exit goes nowhere, so this is the end. */
                g_won = 1;
                audio_play("win", 100);
            } else {
                char dest[32];
                int j = 0;
                for (; g_level.next[j] && j < 31; j++) dest[j] = g_level.next[j];
                dest[j] = 0;

                /* An unknown target leaves the player where they are rather
                   than dropping them into a void -- a typo does not win the
                   game, and a half-authored map is still walkable. */
                if (level_load(dest, &g_level)) {
                    mb_reset(&level_buf);
                    level_range_count = level_geometry(&level_buf, &g_level,
                                                       level_ranges, LVL_MAX_RANGES);
                    mesh_upload(&level_mesh, &level_buf, 1);
                    for (int i = 0; i < level_range_count; i++)
                        level_tex[i] = tex_mat(level_ranges[i].mat);

                    int hp = g_player.health, ammo = g_weapon.ammo;
                    g_yaw = player_spawn(&g_player, &g_level);  /* resets health */
                    g_pitch = 0.0f;
                    g_player.health = hp;                       /* ...so restore it */
                    g_weapon.ammo = ammo;

                    enemy_spawn_level(&g_level);
                    pickup_spawn_level(&g_level);

                    for (j = 0; dest[j]; j++) cur_level[j] = dest[j];
                    cur_level[j] = 0;
                    audio_play("exit", 90);
                }
            }
        }

        /* Hot reload: in a HOT_RELOAD build this notices an edit under the
           assets directory and rebuilds whatever came out of it, so a
           silhouette change appears in the running game -- in the real level,
           with the real lighting and fog -- without a rebuild. Compiled to
           nothing in release, where data_poll() is a constant 0. */
        if (data_poll()) {
            level_load(cur_level, &g_level);
            mb_reset(&level_buf);
            level_range_count = level_geometry(&level_buf, &g_level,
                                               level_ranges, LVL_MAX_RANGES);
            mesh_upload(&level_mesh, &level_buf, 1);
            wp_set_model("shotgun");
            wp_reload_texture();          /* also flushes the texture cache */
            for (int i = 0; i < level_range_count; i++)
                level_tex[i] = tex_mat(level_ranges[i].mat);
            audio_reload();
            enemy_spawn_level(&g_level);  /* re-place monsters from the new map */
            pickup_spawn_level(&g_level);
        }

        RECT cr; GetClientRect(g_wnd, &cr);
        int vw = cr.right - cr.left, vh = cr.bottom - cr.top;
        if (vh < 1) vh = 1;
        float aspect = (float)vw / (float)vh;

        /* Bind the offscreen target, if there is one. It returns the aspect
           of the small buffer rather than the window's, and the world must be
           rendered with THAT -- using the window's here stretches everything,
           because the offscreen buffer's proportions need not match exactly
           once its width has been rounded to whole pixels.
           오프스크린 타깃이 있으면 바인딩합니다. 창이 아닌 작은 버퍼의 종횡비를
           반환하며, 월드는 *그 값으로* 렌더링해야 합니다. 여기서 창의 값을 쓰면 전체가
           늘어나는데, 오프스크린 버퍼의 너비가 정수 픽셀로 반올림되고 나면 그 비율이
           창과 정확히 일치하지 않을 수 있기 때문입니다. */
        float post_aspect = post_begin();
        if (post_aspect > 0.0f) aspect = post_aspect;
        else                    glViewport(0, 0, vw, vh);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* Recoil rides on top of the player's own pitch and springs back. */
        float aim_pitch = g_pitch + g_weapon.recoil;

        mat4 proj = mat4_perspective(WORLD_FOV, aspect, 0.05f, 200.0f);
        mat4 view = mat4_fps_view(g_player.pos, g_yaw, aim_pitch);
        mat4 vp   = mat4_mul(proj, view);

        float cy = cosf(g_yaw), sy = sinf(g_yaw);
        float cp = cosf(aim_pitch), sp = sinf(aim_pitch);
        v3 cam_fwd   = v3f(-sy * cp, sp, -cy * cp);
        v3 cam_right = v3f(cy, 0, -sy);
        v3 cam_up    = v3cross(cam_right, cam_fwd);

        /* --- world --- */
        rd_mode(RD_WORLD);
        rd_mvp(vp);
        rd_eye(g_player.pos);
        glActiveTexture(GL_TEXTURE0);
        for (int i = 0; i < level_range_count; i++) {
            tex_use(&level_tex[i]);
            mesh_draw_range(&level_mesh, level_ranges[i].first,
                            level_ranges[i].count);
        }

        /* --- monsters: alpha-tested billboards, upright and camera-facing ---
           One buffer for them all; the depth test sorts them, and the alpha
           discard means no back-to-front ordering is needed. Cull is off
           because a billboard is single-sided and may face either way. */
        {
            int n = enemy_count();
            mb_reset(&enemy_buf);
            for (int i = 0; i < n; i++) {
                const Enemy *m = enemy_at(i);
                if (!m->active) continue;

                const MonType *S = mon_stats(m->type);

                /* Frame from state, walk cycle from the animation clock. */
                int fr = SPR_WALK0;
                if (m->state == E_DEAD)        fr = SPR_DEAD;
                else if (m->state == E_HURT)   fr = SPR_HURT;
                else if (m->state == E_ATTACK) fr = (m->timer < S->windup)
                                                    ? SPR_ATTACK : SPR_WALK0;
                else if (m->state == E_CHASE)
                    fr = (sinf(m->anim * 8.0f) > 0.0f) ? SPR_WALK0 : SPR_WALK1;

                float u0, v0, u1, v1;
                sprite_uv(m->type, fr, &u0, &v0, &u1, &v1);

                float h = S->height;
                float w = h * S->aspect;
                v3 centre = v3f(m->pos.x, m->pos.y + h * 0.5f, m->pos.z);
                mb_billboard_uv(&enemy_buf, centre, cam_right, v3f(0,1,0),
                                w, h, u0, v0, u1, v1);
            }
            if (enemy_buf.count) {
                mesh_upload(&enemy_mesh, &enemy_buf, 1);
                rd_mode(RD_SPRITE);
                rd_mvp(vp);
                rd_eye(g_player.pos);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sprite_tex);
                glDisable(GL_CULL_FACE);

                /* Per-monster tint: a hit flashes white, a corpse fades to
                   dark and sinks over its half-second. One draw call each, so
                   each gets its own uColor -- there are at most a few dozen. */
                glBindVertexArray(enemy_mesh.vao);
                int q = 0;
                for (int i = 0; i < n; i++) {
                    const Enemy *m = enemy_at(i);
                    if (!m->active) continue;
                    float flash = m->flash > 0.0f ? m->flash : 0.0f;
                    float shade = 1.0f;
                    if (m->state == E_DEAD) shade = 0.35f + 0.65f * (m->timer / 0.6f);
                    rd_color(shade, shade, shade, flash);
                    glDrawArrays(GL_TRIANGLES, q * 6, 6);
                    q++;
                }
                glEnable(GL_CULL_FACE);
            }
        }

        /* --- pickups: bobbing billboards, same alpha-tested sprite path --- */
        {
            int pn = pickup_count();
            mb_reset(&pickup_buf);
            for (int i = 0; i < pn; i++) {
                const Pickup *p = pickup_at(i);
                if (!p->active) continue;
                float u0, v0, u1, v1;
                pickup_uv(p->kind, &u0, &v0, &u1, &v1);
                float s = 0.5f;
                float bob = 0.06f * sinf(p->anim * 2.2f);
                v3 centre = v3f(p->pos.x, p->pos.y + 0.42f + bob, p->pos.z);
                mb_billboard_uv(&pickup_buf, centre, cam_right, v3f(0,1,0),
                                s, s, u0, v0, u1, v1);
            }
            if (pickup_buf.count) {
                mesh_upload(&pickup_mesh, &pickup_buf, 1);
                rd_mode(RD_SPRITE);
                rd_mvp(vp);
                rd_eye(g_player.pos);
                rd_color(1.0f, 1.0f, 1.0f, 0.0f);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, pickup_tex);
                glDisable(GL_CULL_FACE);
                mesh_draw(&pickup_mesh);
                glEnable(GL_CULL_FACE);
            }
        }

        /* --- monster projectiles: additive billboards, no texture at all ---
           A bolt is a light source, so it is drawn the way the muzzle flash is:
           several camera-facing quads rotated against each other and blended
           additively. A single quad reads as a glowing *square*, which is
           exactly what it looked like before -- overlapping three at 60 degrees
           gives a rosette that brightens toward the middle and reads round,
           for no texture and no new geometry helper. */
#define SHOT_HALOS 3      /* wide dim petals -- these carry the round shape */
#define SHOT_CORES 2      /* small bright ones -- a star, not a white square */
        {
            const int quads  = SHOT_HALOS + SHOT_CORES;
            const int stride = quads * 6;
            int sn = enemy_shot_count(), live = 0;

            mb_reset(&shot_buf);
            for (int i = 0; i < sn; i++) {
                const Shot *s = enemy_shot_at(i);
                if (!s->active) continue;

                /* Spin each bolt by its own remaining life, so a volley does
                   not look like one sprite stamped several times. */
                for (int q = 0; q < quads; q++) {
                    int   core = q >= SHOT_HALOS;
                    int   n    = core ? SHOT_CORES : SHOT_HALOS;
                    int   k    = core ? q - SHOT_HALOS : q;
                    float size = core ? 0.22f : 0.62f;
                    /* Spread the quads over a QUARTER turn, not a half: a
                       square maps onto itself every 90 degrees, so two quads
                       180/2 apart are the same square drawn twice -- which is
                       why the core first came out as a plain diamond. */
                    float a    = s->life * 2.3f + k * (M_PI_F * 0.5f / n);
                    v3 r = v3add(v3scale(cam_right,  cosf(a)),
                                 v3scale(cam_up,     sinf(a)));
                    v3 u = v3add(v3scale(cam_right, -sinf(a)),
                                 v3scale(cam_up,     cosf(a)));
                    mb_billboard(&shot_buf, s->pos, r, u, size, size);
                }
                live++;
            }

            if (live) {
                mesh_upload(&shot_mesh, &shot_buf, 1);
                rd_mode(RD_FLAT);
                rd_mvp(vp);
                glDisable(GL_CULL_FACE);
                glDepthMask(GL_FALSE);          /* glows do not occlude */
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                glBindVertexArray(shot_mesh.vao);
                for (int k = 0; k < live; k++) {
                    rd_color(0.10f, 0.42f, 0.85f, 0.30f);   /* halo petals */
                    glDrawArrays(GL_TRIANGLES, k * stride, SHOT_HALOS * 6);
                    rd_color(0.85f, 0.98f, 1.00f, 0.85f);   /* hot core */
                    glDrawArrays(GL_TRIANGLES, k * stride + SHOT_HALOS * 6,
                                 SHOT_CORES * 6);
                }
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                glEnable(GL_CULL_FACE);
            }
        }

        /* --- bullet holes and tracers, still in world space --- */
        glDisable(GL_CULL_FACE);
        wp_draw_world(&g_weapon, vp, g_player.pos, cam_right, cam_up);

        /* --- the gun, over a cleared depth buffer --- */
        glEnable(GL_CULL_FACE);
        wp_draw_view(&g_weapon, aspect);

        /* --- resolve the offscreen buffer to the window ---------------------
           Everything above this line is the WORLD, and it goes through the
           pixelise + dither pass. Everything below is UI, and does not: 5x7
           glyphs magnified four times are unreadable, and dithered text is
           worse still. The gun is deliberately on the world side of the line
           -- it is part of the scene's lighting, and a crisp weapon floating
           over a pixelated world reads as a bug.

           A no-op when the effect is off or unavailable, in which case the
           frame was drawn straight to the window and this changes nothing.

           이 줄 위쪽은 전부 *월드*이며 픽셀화 + 디더 패스를 거칩니다. 아래쪽은 UI이며
           거치지 않습니다. 5x7 글리프를 4배 확대하면 읽을 수 없고, 디더링된 텍스트는
           더 나쁩니다. 총기는 의도적으로 월드 쪽에 둡니다. 총기는 장면 조명의
           일부이며, 픽셀화된 월드 위에 선명한 무기가 떠 있으면 버그처럼 보입니다.

           효과가 꺼져 있거나 사용할 수 없으면 아무 동작도 하지 않으며, 그 경우 프레임은
           창에 직접 그려졌으므로 달라지는 것이 없습니다. */
        post_end(vw, vh);

        /* --- crosshair --- */
        if (!g_won) {
            glDisable(GL_CULL_FACE);
            /* post_end leaves depth testing off and does not restore culling,
               so the UI passes below set up their own state -- which they
               already did, because the HUD never wanted depth anyway.
               post_end는 깊이 테스트를 끈 상태로 두고 컬링도 복원하지 않으므로, 아래
               UI 패스들이 자체적으로 상태를 설정합니다. HUD는 원래 깊이 테스트를
               필요로 하지 않았으므로 이미 그렇게 하고 있었습니다. */
            /* The range test traces the level, so it happens here rather than
               inside the draw call -- see the note on wp_draw_hud.
               사거리 판정은 레벨을 탐색하므로 그리기 호출 내부가 아니라 이곳에서
               수행합니다. wp_draw_hud의 참고 사항을 확인하십시오. */
            int hook_ready = wp_hook_in_range(&g_weapon, &g_level,
                                              g_player.pos, g_yaw, g_pitch);
            wp_draw_hud(&g_weapon, aspect, hook_ready);
            glEnable(GL_CULL_FACE);
        }

        /* --- HUD: a red flash on taking a hit, and the health readout --- */
        {
            mat4 hud = mat4_ortho(0.0f, (float)vw, (float)vh, 0.0f, -1.0f, 1.0f);
            glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            if (g_player.hurt > 0.0f) {
                /* A full-screen wash, strongest right after the hit. */
                mb_reset(&hud_buf);
                mb_billboard(&hud_buf, v3f(vw * 0.5f, vh * 0.5f, 0),
                             v3f(1,0,0), v3f(0,1,0), (float)vw, (float)vh);
                mesh_upload(&hud_mesh, &hud_buf, 1);
                rd_mvp(hud);
                rd_mode(RD_FLAT);
                float a = g_player.hurt; if (a > 1.0f) a = 1.0f;
                rd_color(0.7f, 0.0f, 0.0f, a * 0.4f);
                mesh_draw(&hud_mesh);
            }

            /* Health, bottom-left. Green when healthy, red when low, so a
               glance at the colour says as much as the number. */
            char hp[16];
            wsprintfA(hp, "%d", g_player.health);
            mb_reset(&hud_buf);
            font_text(&hud_buf, 18.0f, vh - 40.0f, 3.5f, hp);
            mesh_upload(&hud_mesh, &hud_buf, 1);
            rd_mvp(hud);
            rd_mode(RD_TEXT);
            float lo = g_player.health / (float)PLAYER_MAX_HP;
            rd_color(1.0f - lo * 0.6f, 0.25f + lo * 0.7f, 0.25f, 1.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, font_texture());
            mesh_draw(&hud_mesh);

            /* Ammo, bottom-right, and red when the gun is empty. */
            char am[16];
            wsprintfA(am, "%d", g_weapon.ammo);
            mb_reset(&hud_buf);
            float aw = font_width(3.5f, am);
            font_text(&hud_buf, vw - 18.0f - aw, vh - 40.0f, 3.5f, am);
            mesh_upload(&hud_mesh, &hud_buf, 1);
            if (g_weapon.ammo == 0) rd_color(0.9f, 0.2f, 0.2f, 1.0f);
            else                    rd_color(0.9f, 0.85f, 0.4f, 1.0f);
            mesh_draw(&hud_mesh);

            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        /* --- win screen: the world stays frozen underneath, dimmed --- */
        if (g_won) {
            mat4 hud = mat4_ortho(0.0f, (float)vw, (float)vh, 0.0f, -1.0f, 1.0f);
            glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            mb_reset(&hud_buf);
            mb_billboard(&hud_buf, v3f(vw * 0.5f, vh * 0.5f, 0),
                         v3f(1,0,0), v3f(0,1,0), (float)vw, (float)vh);
            mesh_upload(&hud_mesh, &hud_buf, 1);
            rd_mvp(hud);
            rd_mode(RD_FLAT);
            rd_color(0.0f, 0.0f, 0.0f, 0.55f);
            mesh_draw(&hud_mesh);

            const char *title = "YOU WIN";
            float ts = 7.0f;
            float tw = font_width(ts, title);
            mb_reset(&hud_buf);
            font_text(&hud_buf, (vw - tw) * 0.5f, vh * 0.5f - 60.0f, ts, title);
            mesh_upload(&hud_mesh, &hud_buf, 1);
            rd_mode(RD_TEXT);
            rd_color(1.0f, 0.85f, 0.30f, 1.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, font_texture());
            mesh_draw(&hud_mesh);

            /* Final stats, so the ending says something rather than just
               stopping. */
            char line[64];
            wsprintfA(line, "health %d   ammo %d", g_player.health, g_weapon.ammo);
            float ls = 2.2f;
            float lw = font_width(ls, line);
            mb_reset(&hud_buf);
            font_text(&hud_buf, (vw - lw) * 0.5f, vh * 0.5f + 4.0f, ls, line);
            mesh_upload(&hud_mesh, &hud_buf, 1);
            rd_color(0.85f, 0.85f, 0.85f, 1.0f);
            mesh_draw(&hud_mesh);

            const char *hint = "ESC to quit";
            float hs = 1.4f;
            float hw = font_width(hs, hint);
            mb_reset(&hud_buf);
            font_text(&hud_buf, (vw - hw) * 0.5f, vh * 0.5f + 40.0f, hs, hint);
            mesh_upload(&hud_mesh, &hud_buf, 1);
            rd_color(0.55f, 0.55f, 0.58f, 1.0f);
            mesh_draw(&hud_mesh);

            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
        }

        SwapBuffers(dc);

        fps_accum += dt; fps_frames++;
        if (fps_accum >= 0.5) {
#ifdef DEBUG_HUD
            /* Capacity overflows, if any. Prefixed with "!" and placed at the
               FRONT of the title: a truncation is the one thing here that
               means something is actually wrong, and the tail of a long title
               bar is the first thing Windows elides.
               용량 초과가 있으면 표시합니다. "!"를 붙여 제목 맨 앞에 배치합니다. 이곳의
               정보 중 실제로 무언가 잘못되었음을 뜻하는 것은 절단뿐이며, 긴 제목
               표시줄에서 Windows가 가장 먼저 생략하는 부분이 뒤쪽이기 때문입니다. */
            char over[96];
            int  any_over = diag_summary(over, sizeof(over));

            /* wsprintfA has no %f, so angles are printed in millidegrees and
               positions in centimetres -- integers all the way down. */
            char title[320];
            wsprintfA(title,
                "%s%s%sSFPS %dfps | assets: %s | pos %d,%d,%d cm | "
                "yaw %d pitch %d recoil %d mdeg",
                any_over ? "! DROPPED " : "", any_over ? over : "",
                any_over ? " | " : "",
                (int)(fps_frames / fps_accum),
                /* Says out loud where the models came from. Editing a file
                   the running build cannot see is otherwise invisible: the
                   game just keeps drawing the old shape. */
                data_from_file(DATA_MODELS) ? "LIVE assets\\" : "baked (rebuild to update)",
                (int)(g_player.pos.x * 100), (int)(g_player.pos.y * 100), (int)(g_player.pos.z * 100),
                (int)(g_yaw * 57295.8f), (int)(g_pitch * 57295.8f),
                (int)(g_weapon.recoil * 57295.8f));
#else
            char title[64];
            wsprintfA(title, "SFPS   %d fps", (int)(fps_frames / fps_accum));
#endif
            SetWindowTextA(g_wnd, title);
            fps_accum = 0; fps_frames = 0;
        }
    }

    post_shutdown();
    audio_shutdown();
    ShowCursor(TRUE);
    return 0;
}
