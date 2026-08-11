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

/* model.h, tex.h and sprite.h are deliberately absent. The level's geometry,
   its materials and the sprite atlases all moved into scene.c, so this file no
   longer names any of those types -- and an include kept "just in case" is how
   a header graph stops describing what actually depends on what.
   model.h, tex.h, sprite.h는 의도적으로 제외했습니다. 레벨의 지오메트리, 재질,
   스프라이트 아틀라스가 모두 scene.c로 옮겨 갔으므로 이 파일은 더 이상 그 타입들을
   언급하지 않습니다. "혹시 몰라서" 남겨 둔 include는 헤더 그래프가 실제 의존 관계를
   설명하지 못하게 만드는 원인입니다. */
#include "render.h"
#include "weapon.h"
#include "data.h"
#include "audio.h"
#include "player.h"
#include "level.h"
#include "font.h"
#include "enemy.h"
#include "pickup.h"
#include "scene.h"    /* the per-frame draw passes and everything they own */
#include "fx.h"       /* authored particle effects */
#include "post.h"
#include "menu.h"     /* the ESC menu: pause, settings, restart, quit */
#include "run.h"      /* RunState: what a restart puts back */
#include "diag.h"
#include "txt.h"      /* txt_copy -- the level-name buffers */

/* GET_X_LPARAM / GET_Y_LPARAM, for the menu's mouse coordinates. These unpack
   a WM_MOUSEMOVE lParam as SIGNED shorts; the obvious LOWORD/HIWORD do it
   unsigned, so a cursor dragged off the left or top edge arrives as ~65000
   instead of a small negative -- which reads as a click on the far side of the
   screen rather than as one outside the window.
   메뉴의 마우스 좌표를 위한 것입니다. WM_MOUSEMOVE의 lParam을 *부호 있는* short로
   분해합니다. 흔히 쓰는 LOWORD/HIWORD는 부호 없이 처리하므로, 커서를 창의 왼쪽이나 위쪽
   경계 밖으로 끌면 작은 음수가 아니라 65000 부근의 값으로 도착합니다. 그러면 창 바깥이
   아니라 화면 반대편을 클릭한 것으로 읽힙니다. */
#include <windowsx.h>

/* ------------------------------------------------------------------ config */

#define WIN_W 1280           ///< @brief Initial window width, pixels. / 초기 창 너비 (픽셀).
#define WIN_H 720            ///< @brief Initial window height, pixels. / 초기 창 높이 (픽셀).
#define WORLD_FOV 1.5708f    ///< @brief 90 degrees, the boomer-shooter default. / 90도. 클래식 FPS의 기본값입니다.

#define MOUSE_SENS   0.0022f ///< @brief Radians of look per pixel of mouse movement. / 마우스 1픽셀 이동당 시점 회전량 (라디안).

/**
 * @brief How much coarser than the render buffer the vertex snap grid is.
 *
 * ENGLISH
 * -------
 * 1.0 snaps to the offscreen buffer's own pixels, which is what the hardware
 * did and is almost invisible here: the PlayStation ran at 320x240 and this
 * runs at 640x360, so a whole-pixel jump is half the angular size it was.
 * Dividing the grid down makes the jumps bigger without touching the render
 * resolution, which is the dial for how much of the artefact to actually show.
 *
 * @note This is the ONE number to change when tuning the wobble. Raising it
 *       past about 4 starts tearing thin geometry apart, because two vertices
 *       of the same triangle can land on the same grid line and the triangle
 *       collapses.
 *
 * 한국어
 * ------
 * @brief 정점 스냅 격자가 렌더 버퍼보다 얼마나 성긴지를 나타냅니다.
 *
 * 1.0이면 오프스크린 버퍼의 픽셀에 그대로 맞추며, 이것이 실제 하드웨어의 동작이지만
 * 여기서는 거의 보이지 않습니다. 플레이스테이션은 320x240이었고 이 프로젝트는
 * 640x360이므로, 한 픽셀 도약의 각크기가 절반입니다. 격자를 나누면 렌더 해상도를
 * 건드리지 않고 도약을 키울 수 있으며, 이것이 아티팩트를 얼마나 드러낼지 조정하는
 * 값입니다.
 *
 * @note 흔들림을 조정할 때 바꿔야 할 *유일한* 값입니다. 약 4를 넘기면 얇은 지오메트리가
 *       찢어지기 시작합니다. 같은 삼각형의 두 정점이 같은 격자선에 놓여 삼각형이 붕괴하기
 *       때문입니다.
 */
#define PSX_SNAP_COARSE 2.0f

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

/**
 * @brief The current run. Reset through ::run_reset, never field by field.
 * / 현재 플레이. 필드를 하나씩이 아니라 ::run_reset을 통해 초기화합니다.
 */
static RunState g_run;


/**
 * @brief Seconds the death screen ignores input for.
 *
 * ENGLISH
 * -------
 * The trigger that killed you is usually still held. Without this the death
 * screen appears and vanishes inside one frame, which reads as the game not
 * having one.
 *
 * 한국어
 * ------
 * @brief 사망 화면이 입력을 무시하는 시간(초)입니다.
 *
 * 당신을 죽인 방아쇠는 대개 아직 눌린 상태입니다. 이것이 없으면 사망 화면이 한 프레임
 * 안에 나타났다 사라지며, 게임에 사망 화면이 없는 것처럼 보입니다.
 */
#define DEATH_INPUT_DELAY 0.8f

/* --- lava smoke ------------------------------------------------------------
 * How often a batch of puffs is emitted, and how many are attempted per batch.
 * Together these set the density; separating them lets the rate be raised
 * without making the plume arrive in visible clumps.
 *
 * The range keeps far-off smoke out of the shared particle pool entirely. At
 * FX_MAX_PARTICLES a lava room can starve every other effect in the game, and
 * a puff behind the player is worth nothing.
 *
 * 연기 묶음을 얼마나 자주 방출하는지와 묶음당 몇 개를 시도하는지입니다. 둘이 함께 밀도를
 * 결정하며, 분리해 두면 방출이 눈에 띄는 덩어리로 도착하지 않게 하면서 비율을 올릴 수
 * 있습니다.
 *
 * 사거리는 먼 곳의 연기를 공유 파티클 풀에서 아예 배제합니다. FX_MAX_PARTICLES에서
 * 용암 방 하나가 게임의 다른 모든 효과를 고갈시킬 수 있으며, 플레이어 뒤의 연기는 아무런
 * 가치가 없습니다. */
#define LAVA_SMOKE_INTERVAL  0.16f  ///< @brief Seconds between batches. / 묶음 사이의 간격 (초).
#define LAVA_SMOKE_PER_TICK  2      ///< @brief Puffs attempted per batch. / 묶음당 시도하는 연기 수.
#define LAVA_SMOKE_RANGE     22.0f  ///< @brief Metres beyond which no smoke spawns. / 이 거리를 넘으면 연기를 생성하지 않습니다 (미터).


/* ------------------------------------------------------------------ cursor */

/**
 * @brief Shows or hides the mouse cursor, idempotently.
 *
 * ENGLISH
 * -------
 * @param[in] show Non-zero to make the cursor visible.
 *
 * @note ShowCursor is a COUNTER, not a flag. Every ShowCursor(FALSE) drops it
 *       by one and it is only visible at >= 0, so two hides need two shows to
 *       undo. Calling it directly from several places is therefore not
 *       idempotent, and the counter drifts: WM_SETFOCUS hid the cursor on
 *       every focus gain, so alt-tabbing away and back while paused hid it
 *       once more each time, and the menu's single ShowCursor(TRUE) could no
 *       longer bring it back. The player was left operating a menu with no
 *       pointer.
 *
 *       This drives the counter to the requested state instead of nudging it,
 *       and remembers what it last asked for so a repeat call does nothing.
 *
 * 한국어
 * ------
 * @brief 마우스 커서를 표시하거나 숨기며, 몇 번을 호출해도 결과가 같습니다.
 * @param[in] show 0이 아니면 커서를 표시합니다.
 *
 * @note ShowCursor는 플래그가 아니라 *카운터*입니다. ShowCursor(FALSE)를 호출할 때마다
 *       1씩 줄고 0 이상일 때만 보이므로, 두 번 숨기면 되돌리는 데 두 번 표시해야 합니다.
 *       따라서 여러 곳에서 직접 호출하면 멱등하지 않으며 카운터가 어긋납니다.
 *       WM_SETFOCUS가 포커스를 얻을 때마다 커서를 숨겼으므로, 일시정지 중에 alt-tab으로
 *       나갔다 돌아올 때마다 한 번씩 더 숨겨졌고, 메뉴의 ShowCursor(TRUE) 한 번으로는
 *       더 이상 되돌릴 수 없었습니다. 플레이어는 포인터 없이 메뉴를 조작하게 됩니다.
 *
 *       이 함수는 카운터를 조금씩 밀지 않고 요청된 상태로 *몰아갑니다*. 또한 마지막으로
 *       요청한 상태를 기억하므로 같은 호출을 반복해도 아무 일도 하지 않습니다.
 */
static void cursor_show(int show) {
    static int g_cursor_shown = 1;   /* Windows starts with it visible */
    if (!show == !g_cursor_shown) return;

    /* Drive the counter to the target rather than assuming it is where we left
       it. The loop is bounded because ShowCursor returns the new count, so
       each step is verifiable rather than hopeful.
       카운터가 우리가 둔 자리에 있다고 가정하지 않고 목표까지 몰아갑니다. ShowCursor가
       새 카운트를 반환하므로 각 단계를 확인할 수 있으며, 그래서 루프에 상한이
       있습니다. */
    for (int i = 0; i < 16; i++) {
        int c = ShowCursor(show ? TRUE : FALSE);
        if (show ? (c >= 0) : (c < 0)) break;
    }
    g_cursor_shown = show ? 1 : 0;
}

/**
 * @brief Puts the cursor in the state the current mode calls for.
 *
 * ENGLISH
 * -------
 * Visible whenever the player needs to point at something -- a menu, or
 * another window -- and hidden while they are looking around the world.
 *
 * @note One place decides this, and every site that changes focus or opens the
 *       menu calls it rather than reasoning about the cursor itself. That is
 *       what stops the two from disagreeing: the old code hid the cursor in
 *       WM_SETFOCUS without asking whether the menu was up, so returning to a
 *       paused game left it operating a menu it could not see.
 *
 * 한국어
 * ------
 * @brief 현재 모드가 요구하는 상태로 커서를 맞춥니다.
 *
 * 플레이어가 무언가를 가리켜야 할 때(메뉴, 또는 다른 창) 보이고, 월드를 둘러보는 동안
 * 숨겨집니다.
 *
 * @note 이를 결정하는 곳은 한 군데이며, 포커스를 바꾸거나 메뉴를 여는 모든 지점이 커서에
 *       대해 스스로 판단하지 않고 이 함수를 호출합니다. 그것이 둘이 어긋나지 않게 하는
 *       방법입니다. 이전 코드는 메뉴가 열려 있는지 묻지 않고 WM_SETFOCUS에서 커서를
 *       숨겼으므로, 일시정지된 게임으로 돌아오면 보이지 않는 메뉴를 조작하게
 *       되었습니다. */
static void cursor_update(void) {
    /* The title and death screens do not steer a camera either, so the pointer
       belongs to the player on those too. Same question, same answer, one
       place.
       타이틀 화면과 사망 화면도 카메라를 조작하지 않으므로 그곳에서도 포인터는
       플레이어의 것입니다. 같은 질문, 같은 답, 한 곳입니다. */
    cursor_show(menu_is_open() || g_run.title || g_run.dead || !g_focused);
}

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
        /* The title screen takes any key but ESC, which still opens the menu
           so the player can change settings or quit before starting.
           타이틀 화면은 ESC를 제외한 모든 키를 받습니다. ESC는 여전히 메뉴를 열어,
           시작 전에 설정을 바꾸거나 종료할 수 있게 합니다. */
        if (g_run.title && !menu_is_open() && wp != VK_ESCAPE) {
            g_run.title = 0;
            g_warp_mouse = 1;
            cursor_update();
            return 0;
        }

        /* ESC opens the menu; it no longer quits. One mistaken keypress used
           to end a run outright with no confirmation, which is the behaviour
           this menu exists to replace. Leaving is now a row the player picks.
           ESC는 메뉴를 열며 더 이상 종료하지 않습니다. 예전에는 잘못 누른 키 하나가
           확인 없이 진행 중인 플레이를 그대로 끝냈으며, 그것이 이 메뉴가 대체하려는
           동작입니다. 이제 나가는 것은 플레이어가 직접 고르는 행입니다.

           On the message edge, like F1 below and for the same reason: one
           press must be one step, and a held key polled in update() would
           walk the whole menu in a frame.
           아래의 F1과 같은 이유로 메시지 시점에 처리합니다. 한 번 누름은 한 단계여야
           하며, update()에서 폴링하면 키를 누르고 있는 동안 한 프레임에 메뉴 전체를
           지나가게 됩니다. */
        if (wp == VK_ESCAPE) {
            menu_escape();
            if (menu_is_open()) {
                /* Let go of everything the player was holding. Returning to a
                   game still walking forward, still firing, or still attached
                   to a hook is the same class of surprise WM_KILLFOCUS below
                   guards against.
                   플레이어가 누르고 있던 것을 전부 놓습니다. 계속 전진하거나 사격
                   중이거나 훅에 걸린 채로 게임에 돌아오는 것은, 아래 WM_KILLFOCUS가
                   방지하는 것과 같은 종류의 놀라움입니다. */
                g_mouse_down = 0;
                g_hook_down  = 0;
                wp_hook_release(&g_weapon);
                wp_hook_arm(&g_weapon);
                for (int i = 0; i < 256; i++) g_keys[i] = 0;
            } else {
                /* Closing: the cursor was parked wherever the player left it,
                   so discard the first delta the way taking focus does.
                   닫는 경우: 커서가 플레이어가 둔 자리에 그대로 있으므로, 포커스를
                   얻을 때와 마찬가지로 첫 변화량을 버립니다. */
                g_warp_mouse = 1;
            }
            cursor_update();
            return 0;
        }

        /* Dead: any key restarts, once the grace period has passed. The delay
           is why this is not simply "any key" -- the shot that killed you is
           very often still held down, and restarting on it reads as the game
           skipping the death screen entirely.
           사망 상태에서는 유예 시간이 지난 뒤 아무 키나 누르면 재시작합니다. 이 지연이
           단순한 "아무 키"가 아닌 이유입니다. 당신을 죽인 그 사격은 대개 아직 눌린
           상태이며, 그것으로 재시작되면 게임이 사망 화면을 통째로 건너뛴 것처럼
           보입니다. */
        if (g_run.dead && !menu_is_open() && g_run.death_time > DEATH_INPUT_DELAY) {
            g_run.restart_wanted = 1;
            return 0;
        }

        /* Menu navigation, also on the edge. While the menu is up these keys
           drive it and nothing else -- the world is frozen, so there is no
           second meaning for them to have.
           메뉴 탐색이며 역시 엣지에서 처리합니다. 메뉴가 열려 있는 동안 이 키들은
           메뉴만 조작합니다. 월드가 정지해 있으므로 다른 의미를 가질 여지가 없습니다. */
        if (menu_is_open()) {
            switch (wp) {
            case 'W': case VK_UP:    menu_move(-1);   return 0;
            case 'S': case VK_DOWN:  menu_move(+1);   return 0;
            case 'A': case VK_LEFT:  menu_adjust(-1); return 0;
            case 'D': case VK_RIGHT: menu_adjust(+1); return 0;
            case VK_RETURN: case VK_SPACE: menu_activate(); return 0;
            }
            return 0;   /* swallow everything else while paused */
        }

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
    /* The buttons are ignored while the menu is up. Not merely unused: the
       press that closes a menu row must not also be the press that fires the
       gun on the frame the game resumes.
       메뉴가 열려 있는 동안 버튼은 무시됩니다. 단순히 쓰이지 않는 것이 아닙니다. 메뉴
       행을 닫는 그 누름이, 게임이 재개되는 프레임에 총을 쏘는 누름이 되어서는 안
       됩니다. */
    /* Cursor motion drives the highlight while the menu is up, so the mouse
       and the keyboard are one menu rather than two: whichever the player last
       touched, the highlight shows where a press would land.
       메뉴가 열려 있는 동안 커서 이동이 강조 표시를 조작하므로, 마우스와 키보드가 두
       개의 메뉴가 아닌 하나가 됩니다. 플레이어가 마지막으로 무엇을 만졌든 강조 표시가
       지금 누르면 어디에 닿을지를 보여 줍니다. */
    case WM_MOUSEMOVE:
        if (menu_is_open()) {
            RECT mc; GetClientRect(w, &mc);
            menu_hover((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp),
                       mc.right - mc.left, mc.bottom - mc.top);
        }
        return 0;

    /* While the menu is up the buttons operate it and never reach the gun. The
       press that picks a row must not also be the press that fires on the
       frame the game resumes.
       메뉴가 열려 있는 동안 버튼은 메뉴를 조작하며 총에 도달하지 않습니다. 행을 고르는
       그 누름이, 게임이 재개되는 프레임에 사격하는 누름이 되어서는 안 됩니다. */
    case WM_LBUTTONDOWN:
        /* The title and death screens take a click as readily as a key: the
           cursor is visible on both, so clicking is what a player reaches for.
           타이틀 화면과 사망 화면은 키만큼이나 클릭도 받아들입니다. 양쪽 모두 커서가
           보이므로 플레이어가 손을 뻗는 것은 클릭입니다. */
        if (g_run.title && !menu_is_open()) {
            g_run.title = 0;
            g_warp_mouse = 1;
            cursor_update();
            return 0;
        }
        if (g_run.dead && !menu_is_open() && g_run.death_time > DEATH_INPUT_DELAY) {
            g_run.restart_wanted = 1;
            return 0;
        }
        if (menu_is_open()) {
            RECT mc; GetClientRect(w, &mc);
            menu_click((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp),
                       mc.right - mc.left, mc.bottom - mc.top, 0);
            /* Activating RESUME closes the menu, so the cursor has to follow
               it back out on this very message -- waiting for the next focus
               change would leave a pointer floating over the game.
               RESUME를 실행하면 메뉴가 닫히므로 커서도 바로 이 메시지에서 따라
               나가야 합니다. 다음 포커스 변경을 기다리면 게임 위에 포인터가 떠 있게
               됩니다. */
            if (!menu_is_open()) g_warp_mouse = 1;
            cursor_update();
        } else {
            g_mouse_down = 1;
        }
        return 0;
    case WM_LBUTTONUP:
        g_mouse_down = 0;
        return 0;
    case WM_RBUTTONDOWN:
        if (menu_is_open()) {
            RECT mc; GetClientRect(w, &mc);
            menu_click((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp),
                       mc.right - mc.left, mc.bottom - mc.top, 1);
            if (!menu_is_open()) g_warp_mouse = 1;
            cursor_update();
        } else {
            g_hook_down = 1;
        }
        return 0;
    case WM_RBUTTONUP:
        g_hook_down = 0;
        return 0;
    case WM_SETFOCUS:
        g_focused = 1;
        g_warp_mouse = 1;
        /* Not an unconditional hide: returning to a PAUSED game has to keep
           the pointer, or the player is left operating a menu they cannot see.
           무조건 숨기지 않습니다. *일시정지된* 게임으로 돌아올 때는 포인터를 유지해야
           하며, 그렇지 않으면 플레이어가 보이지 않는 메뉴를 조작하게 됩니다. */
        cursor_update();
        return 0;
    case WM_KILLFOCUS:
        g_focused = 0;
        g_mouse_down = 0;
        g_hook_down = 0;
        wp_hook_release(&g_weapon);   /* do not leave it attached off-screen */
        wp_hook_arm(&g_weapon);       /* the button is up now, so rearm with it */
        cursor_update();
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

    /* Paused: the cursor belongs to the player again, so it is neither read
       nor recentred. Recentring it while the menu is up would fight whatever
       they are doing with it and pin the pointer to the middle of the screen.
       정지 상태: 커서가 다시 플레이어의 것이므로 읽지도 중앙으로 되돌리지도 않습니다.
       메뉴가 열린 채로 커서를 중앙에 붙들면 플레이어의 조작과 충돌하며 포인터가 화면
       한가운데에 고정됩니다. */
    if (g_focused && !menu_is_open()) {
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
    if (g_hook_down && !g_run.won) {
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

/* ------------------------------------------------------ graphics settings */

/**
 * @brief Art-resolution target for each ::GfxPixelPreset, in pixels of height.
 *
 * ENGLISH
 * -------
 * Indexed by the preset, so a new preset is a row here and an enum value in
 * menu.h -- nothing else. ::POST_HEIGHT remains the compiled-in default and is
 * what ::GFX_PIXEL_NORMAL reproduces exactly, so shipping with the menu
 * untouched renders precisely the frame the game rendered before it existed.
 *
 * 한국어
 * ------
 * @brief 각 ::GfxPixelPreset의 아트 해상도 목표 높이(픽셀)입니다.
 *
 * 프리셋으로 인덱싱되므로 새 프리셋은 이곳의 행 하나와 menu.h의 열거형 값 하나가
 * 전부입니다. ::POST_HEIGHT는 컴파일 시점 기본값으로 남으며 ::GFX_PIXEL_NORMAL이 그것을
 * 정확히 재현하므로, 메뉴를 건드리지 않고 실행하면 이 기능이 없던 때와 정확히 같은
 * 프레임이 그려집니다.
 */
static const int PIXEL_HEIGHTS[GFX_PIXEL_COUNT] = {
    240,           /* GFX_PIXEL_CHUNKY -- roughly the PlayStation's own */
    POST_HEIGHT,   /* GFX_PIXEL_NORMAL -- the shipped default, 360 */
    540            /* GFX_PIXEL_FINE   -- close to unpixelised at 1080p */
};

/**
 * @brief (Re)creates the offscreen target to match the window and the preset.
 *
 * ENGLISH
 * -------
 * @param[in] preset A ::GfxPixelPreset.
 * @note ::post_init allocates unconditionally, so an existing target has to be
 *       torn down first or its GL objects leak. ::post_shutdown fully resets
 *       the module, which is what makes shutdown-then-init a safe resize.
 * @note The integer magnification is picked FIRST and the buffer derived from
 *       it, never the other way round -- see the note at the original call
 *       site in WinMain for what the rounded-width version did to the image.
 *
 * 한국어
 * ------
 * @brief 창과 프리셋에 맞추어 오프스크린 타깃을 (재)생성합니다.
 * @param[in] preset ::GfxPixelPreset 값.
 * @note ::post_init은 무조건 할당하므로 기존 타깃을 먼저 해제하지 않으면 GL 객체가
 *       누수됩니다. ::post_shutdown이 모듈을 완전히 초기화하므로 해제 후 재생성이
 *       안전한 크기 변경 방법입니다.
 * @note 정수 확대 배율을 *먼저* 정하고 버퍼를 그로부터 유도하며, 결코 그 반대가 아닙니다.
 *       너비를 반올림하던 방식이 화면에 무엇을 했는지는 WinMain의 원래 호출 지점 주석을
 *       참조하십시오.
 */
static void apply_pixel_preset(int preset) {
    if (preset < 0 || preset >= GFX_PIXEL_COUNT) preset = GFX_PIXEL_NORMAL;

    RECT ir; GetClientRect(g_wnd, &ir);
    int iw = ir.right - ir.left, ih = ir.bottom - ir.top;
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;

    int target = PIXEL_HEIGHTS[preset];
    int scale  = ih / target;
    if (scale < 1) scale = 1;

    /* Preserve whether the effect was switched off, so changing the pixel size
       does not quietly switch the whole pass back on.
       효과가 꺼져 있었는지를 보존합니다. 픽셀 크기를 바꾸는 것이 패스 전체를 조용히 다시
       켜서는 안 됩니다. */
    int was_on = post_enabled();

    post_shutdown();
    post_init(iw / scale, ih / scale);
    post_set_enabled(was_on);
}

/**
 * @brief Switches the window between windowed and borderless.
 *
 * ENGLISH
 * -------
 * @param[in] mode A ::DisplayMode.
 * @note Restyles the EXISTING window rather than recreating it. Recreating it
 *       would destroy the GL context bound to its device context, which would
 *       take every texture, buffer and shader in the process with it -- the
 *       whole world would have to be rebuilt to change a window border.
 * @note Borderless rather than exclusive fullscreen, so the desktop resolution
 *       is never changed and never has to be restored. See ::DisplayMode.
 *
 * 한국어
 * ------
 * @brief 창 모드와 테두리 없는 전체 화면을 전환합니다.
 * @param[in] mode ::DisplayMode 값.
 * @note 창을 다시 만들지 않고 *기존* 창의 스타일만 바꿉니다. 다시 만들면 그 디바이스
 *       컨텍스트에 묶인 GL 컨텍스트가 파괴되어 프로세스의 모든 텍스처, 버퍼, 셰이더가
 *       함께 사라집니다. 창 테두리를 바꾸자고 월드 전체를 다시 만들어야 하는 셈입니다.
 * @note 독점 전체 화면이 아니라 테두리 없는 창이므로 바탕화면 해상도를 바꾸지 않으며
 *       되돌릴 필요도 없습니다. ::DisplayMode를 참조하십시오.
 */
static void apply_display_mode(int mode) {
    if (mode == DISPLAY_BORDERLESS) {
        MONITORINFO mi = {0};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrA(g_wnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_wnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right  - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        DWORD style = (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX))
                    | WS_VISIBLE;
        RECT r = {0, 0, WIN_W, WIN_H};
        AdjustWindowRect(&r, style, FALSE);
        int w = r.right - r.left, h = r.bottom - r.top;

        /* Centred on the monitor, rather than left wherever borderless put it.
           Borderless parks the window at the monitor's top-left corner, so
           keeping that position would drop a small window into the corner of
           the screen with its title bar half off the top on some layouts.
           테두리 없는 모드가 두었던 자리에 남기지 않고 모니터 중앙에 놓습니다. 테두리
           없는 모드는 창을 모니터 좌상단에 두므로, 그 위치를 유지하면 작은 창이 화면
           구석에 놓이고 배치에 따라서는 제목 표시줄이 위로 잘려 나갑니다. */
        MONITORINFO mi = {0};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(g_wnd, MONITOR_DEFAULTTONEAREST), &mi);
        int mx = mi.rcMonitor.left + (mi.rcMonitor.right  - mi.rcMonitor.left - w) / 2;
        int my = mi.rcMonitor.top  + (mi.rcMonitor.bottom - mi.rcMonitor.top  - h) / 2;

        SetWindowLongPtrA(g_wnd, GWL_STYLE, style);
        SetWindowPos(g_wnd, HWND_TOP, mx, my, w, h,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

/* ------------------------------------------------------- level lifecycle */

/**
 * @brief Loads a level and brings the whole world into step with it.
 *
 * ENGLISH
 * -------
 * @param[in]     name        Level to load, looked up in the level text.
 * @param[in,out] sc          Scene receiving the rebuilt geometry.
 * @param[in]     dynamic     Non-zero when geometry has already been uploaded
 *                            once, so the existing allocation is reused. Zero
 *                            only on the very first build.
 * @param[in]     carry_state Non-zero to preserve health and ammo across the
 *                            load. ::player_spawn resets health, so a
 *                            transition has to put it back; a fresh start and
 *                            a hot reload want the reset.
 * @return 1 when the level loaded, 0 when no level of that name exists -- in
 *         which case NOTHING has been touched and the caller stays put.
 *
 * @note The five steps below have to happen together and in this order, and
 *       they were previously written out at three call sites: startup, the
 *       exit transition, and hot reload. They had already drifted -- hot
 *       reload did not respawn the player, and whether that was deliberate or
 *       an omission could not be answered from the code. This is the same
 *       argument ::scene_build_level was extracted for, one layer up.
 * @note The caller's `cur_level` buffer is NOT written here. `name` may alias
 *       it, and it may also alias `g_level.next`, which ::level_load blanks
 *       before parsing -- so the copy has to happen in the caller, out of a
 *       buffer this function never touches.
 *
 * 한국어
 * ------
 * @brief 레벨을 로드하고 월드 전체를 그에 맞게 갱신합니다.
 * @param[in]     name        로드할 레벨 이름.
 * @param[in,out] sc          재생성된 지오메트리를 받을 장면.
 * @param[in]     dynamic     지오메트리가 이미 한 번 업로드된 경우 0이 아닌 값을
 *                            전달하여 기존 할당을 재사용합니다. 최초 생성 시에만 0입니다.
 * @param[in]     carry_state 0이 아니면 체력과 탄약을 보존합니다. ::player_spawn이
 *                            체력을 초기화하므로 전환 시에는 되돌려야 하며, 새 시작과
 *                            핫 리로드는 초기화를 원합니다.
 * @return 로드에 성공하면 1, 해당 이름의 레벨이 없으면 0. 후자의 경우 아무것도 변경되지
 *         않으므로 호출자는 그대로 머무릅니다.
 *
 * @note 아래 다섯 단계는 반드시 함께, 이 순서로 수행되어야 합니다. 이전에는 시작,
 *       출구 전환, 핫 리로드 세 곳에 각각 작성되어 있었고 이미 어긋나 있었습니다.
 *       핫 리로드는 플레이어를 다시 스폰하지 않았는데, 그것이 의도인지 누락인지 코드만
 *       보고는 답할 수 없었습니다. ::scene_build_level을 추출한 것과 동일한 논리를
 *       한 단계 위에 적용한 것입니다.
 * @note 호출자의 `cur_level` 버퍼는 이곳에서 기록하지 않습니다. `name`이 그것과
 *       별칭일 수 있고 ::level_load가 파싱 전에 비우는 `g_level.next`와도 별칭일 수
 *       있으므로, 복사는 이 함수가 건드리지 않는 버퍼를 써서 호출자 쪽에서
 *       이루어져야 합니다.
 */
static int load_level(const char *name, Scene *sc, int dynamic, int carry_state) {
    /* Parsed first, and nothing else is touched until it succeeds. An unknown
       target -- a typo, or a half-authored map -- must leave the player where
       they are rather than dropping them into a void.
       먼저 파싱하며, 성공하기 전까지는 아무것도 건드리지 않습니다. 알 수 없는 대상(오타
       또는 절반만 제작된 맵)은 플레이어를 빈 공간에 떨어뜨리는 대신 있던 자리에 두어야
       합니다. */
    if (!level_load(name, &g_level)) return 0;

    scene_build_level(sc, &g_level, dynamic);

    int hp = g_player.health, ammo = g_weapon.ammo;
    g_yaw   = player_spawn(&g_player, &g_level);   /* resets health */
    g_pitch = 0.0f;
    if (carry_state) {
        g_player.health = hp;                      /* ...so restore it */
        g_weapon.ammo   = ammo;
    }

    /* Monsters and pickups are placed from the level's own entities. */
    enemy_spawn_level(&g_level);
    pickup_spawn_level(&g_level);

    /* A hook mid-flight belongs to the level that is being left. Carrying it
       across would reel the player toward an anchor in geometry that no longer
       exists, and toward a monster index that now means somebody else.
       비행 중인 훅은 떠나는 레벨에 속합니다. 이를 이어 가면 더 이상 존재하지 않는
       지오메트리의 앵커로, 그리고 이제 다른 대상을 가리키는 몬스터 인덱스로 플레이어를
       끌어당기게 됩니다. */
    wp_hook_release(&g_weapon);
    wp_hook_arm(&g_weapon);

    return 1;
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
       an exact integer scale in both axes.

       The sizing rule itself lives in apply_pixel_preset, because the settings
       menu resizes the target at runtime and a second copy of that reasoning
       would drift from the first. menu_init runs before it so the preset it
       reads is a real one.

       Failure is not fatal -- post_begin/post_end become no-ops and the game
       renders straight to the window. Refusing to start over a cosmetic pass
       would be the wrong trade.

       오프스크린 타깃입니다. 창 크기로 다시 확대할 때 양쪽 축 모두 정확한 정수 배율이
       되도록 크기를 정합니다.

       크기 결정 규칙 자체는 apply_pixel_preset에 있습니다. 설정 메뉴가 런타임에 타깃
       크기를 바꾸는데, 그 논리의 사본이 두 개면 서로 어긋나게 되기 때문입니다. 읽어 오는
       프리셋이 실제 값이 되도록 menu_init을 먼저 실행합니다.

       실패해도 치명적이지 않습니다. post_begin/post_end가 아무 동작도 하지 않게 되고
       게임은 창에 직접 렌더링합니다. 순전히 미관을 위한 패스 때문에 실행을 거부하는
       것은 잘못된 선택입니다. */
    menu_init(0);
    apply_pixel_preset(menu_settings()->pixel);

    audio_init();   /* silent, not fatal, if there is no output device */

    /* --- everything below is generated, nothing is loaded --- */
    font_init();

    /* title=1: the game comes up on the title screen, and the same call seeds
       the smoke rng and zeroes every clock. Startup and a restart therefore
       begin from a state produced by one function rather than by a declaration
       here and an assignment there -- which is how the two drifted apart in the
       first place. See RunState.
       title=1입니다. 게임은 타이틀 화면으로 시작하며, 같은 호출이 연기 난수의 시드를
       설정하고 모든 시계를 0으로 만듭니다. 따라서 시작과 재시작은 이곳의 선언과 저곳의
       대입이 아니라 하나의 함수가 만들어 낸 상태에서 출발합니다. 애초에 그 둘이 어긋난
       이유가 바로 그것이었습니다. RunState를 참조하십시오. */
    run_reset(&g_run, 1);

    /* The level the game is currently in. Kept in its own buffer, not read
       back out of g_level.name: level_load clears the destination's name field
       before parsing, so passing g_level.name back into it would blank the
       search string. Also what hot reload reloads and what an exit updates. */
    char cur_level[32] = "arena";

    /* The draw passes and everything they own: the level's geometry, the
       per-frame billboard buffers, and the sprite atlases. One struct so that
       every mb_init has an mb_free -- see scene.h. */
    Scene scene;
    scene_init(&scene);

    /* Before load_level, which calls wp_hook_release on the weapon: wp_init is
       what zeroes it and records the level shots trace against.
       load_level보다 먼저 호출합니다. load_level이 무기에 wp_hook_release를 호출하는데,
       무기를 0으로 초기화하고 사격 판정 대상 레벨을 기록하는 것이 wp_init이기
       때문입니다. */
    wp_init(&g_weapon, &g_level);

    /* dynamic=0: the first build, so the geometry is uploaded rather than
       replacing an existing allocation. carry_state=0: a fresh start begins at
       full health with a starting belt, which is what wp_init just set. */
    load_level(cur_level, &scene, 0, 0);

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

        /* --- menu actions, before anything else this frame ------------------
           Taken once per frame and acted on immediately, so a restart happens
           before the world it restarts is stepped.
           프레임마다 한 번 가져와 즉시 처리하므로, 재시작이 그것이 재시작할 월드를
           진행시키기 전에 일어납니다. */
        switch (menu_take_action()) {
        case MENU_ACT_QUIT:
            g_running = 0;
            break;
        case MENU_ACT_RESTART:
            g_run.restart_wanted = 1;
            break;
        case MENU_ACT_DISPLAY:
            apply_display_mode(menu_settings()->display);
            /* After the window has been restyled, not before: the offscreen
               buffer is sized from the CLIENT area, and that is only correct
               once the new style has been applied.
               창 스타일을 바꾼 *뒤*여야 합니다. 오프스크린 버퍼는 클라이언트 영역을
               기준으로 크기가 정해지는데, 그 값은 새 스타일이 적용된 뒤에야
               올바릅니다. */
            apply_pixel_preset(menu_settings()->pixel);
            break;
        case MENU_ACT_NONE:
            break;
        }
        if (!g_running) break;

        /* --- restart -------------------------------------------------------
           One implementation, three ways in: the menu's RESTART row, a key on
           the death screen, and a click on it. They must clear exactly the
           same state -- a death screen that restarted without clearing `won`,
           or a menu restart that left the player dead, would each be a
           separate half-working path.

           ::run_reset is what makes that structural rather than a matter of
           keeping a list current: this used to assign three fields by name, and
           there were more than three. See ::RunState.

           구현은 하나이고 진입로는 셋입니다. 메뉴의 RESTART 행, 사망 화면에서의 키,
           그리고 그곳에서의 클릭입니다. 셋은 정확히 같은 상태를 정리해야 합니다. `won`을
           지우지 않고 재시작하는 사망 화면이나 플레이어를 죽은 채로 두는 메뉴 재시작은
           각각 절반만 동작하는 별개의 경로가 됩니다.

           ::run_reset이 그것을 목록을 최신으로 유지하는 문제가 아니라 구조적인 것으로
           만듭니다. 이전에는 세 개의 필드를 이름으로 대입했지만 실제로는 세 개보다
           많았습니다. ::RunState를 참조하십시오. */
        if (g_run.restart_wanted) {
            /* carry_state=0: a restart is a fresh run, so health and ammo
               reset. dynamic=1 because geometry has already been uploaded once.
               재시작은 새 플레이이므로 체력과 탄약이 초기화됩니다. 지오메트리는 이미 한
               번 업로드되었으므로 dynamic=1입니다. */
            load_level(cur_level, &scene, 1, 0);

            /* title=0: the player asked to play, so a restart goes straight
               back into the run rather than showing the title again. This
               clears restart_wanted along with everything else, which is why
               nothing does so by hand.
               title=0입니다. 플레이어가 플레이를 요청했으므로 재시작은 타이틀을 다시
               보여 주지 않고 곧바로 플레이로 되돌아갑니다. 이 호출이 나머지 전부와 함께
               restart_wanted도 지우므로 손으로 지우는 곳이 없습니다. */
            run_reset(&g_run, 0);

            menu_close();
            g_warp_mouse = 1;
            /* menu_close and `dead` have both just changed what the cursor
               should be, so this has to follow them rather than assume.
               menu_close와 `dead`가 방금 커서의 올바른 상태를 바꾸었으므로, 가정하지
               않고 그 뒤에 이 호출이 와야 합니다. */
            cursor_update();
        }

        /* Settings that need no signal: read straight from the menu every
           frame, so toggling them is visible on the very next one.
           신호가 필요 없는 설정입니다. 매 프레임 메뉴에서 직접 읽으므로 전환하면 바로
           다음 프레임에 반영됩니다. */
        post_set_enabled(menu_settings()->post_on);
        post_set_scanline(menu_settings()->scanlines ? POST_SCANLINE_DEFAULT : 0.0f);

        /* The world does not advance while the menu is up or the game is won.
           One flag for both, because they freeze exactly the same things and
           two conditions repeated at five call sites is how one of them ends up
           missing a site.
           메뉴가 열려 있거나 게임에서 이겼으면 월드가 진행하지 않습니다. 둘이 정확히
           같은 것들을 정지시키므로 플래그 하나로 둡니다. 다섯 곳에서 반복되는 두 개의
           조건은 그중 하나가 어느 한 곳을 빠뜨리게 되는 지름길입니다. */
        int frozen = g_run.won || g_run.dead || g_run.title || menu_is_open();

        /* Look, move, fire and the AI all stop, but the last frame keeps
           drawing under the overlay. */
        if (!frozen) update(dt);

        /* Monsters advance after the player has moved, so a swing is measured
           against where the player actually ends up this frame. The module
           owns the AI; the player's health is owned here. */
        if (!frozen) {
            int dmg = enemy_update(&g_level, g_player.pos, dt);
            if (dmg > 0 && g_player.health > 0) {
                g_player.health -= dmg;
                if (g_player.health < 0) g_player.health = 0;
                g_player.hurt = 1.0f;
                audio_play("phurt", 90);
            }

            /* --- hazard floors: lava, acid ---------------------------------
               Charged only while GROUNDED. A hazard is the floor, so jumping
               over a lava channel or being pulled across it by the hook has to
               be a way through -- otherwise the room is a wall rather than an
               obstacle, and the momentum systems this game is built on have
               nothing to do there.

               The rate is per second and multiplied by dt, so the damage is
               the same on any machine and crossing a corner costs less than
               standing in the middle. Accumulated in a float because a rate
               below one point per frame would otherwise truncate to zero and
               a slow hazard would be entirely harmless.

               지면에 있을 때만 적용됩니다. 위험 지형은 바닥이므로, 용암 수로를 뛰어넘거나
               훅에 끌려 가로지르는 것이 통과 수단이 되어야 합니다. 그렇지 않으면 그 방은
               장애물이 아니라 벽이 되고, 이 게임이 기반한 운동량 시스템이 그곳에서 할 일이
               없어집니다.

               비율은 초당이며 dt를 곱하므로 어떤 기기에서도 피해량이 같고, 모서리를 스쳐
               가는 것이 한가운데 서 있는 것보다 덜 듭니다. float로 누적하는 이유는, 프레임당
               1점 미만인 비율이 그렇지 않으면 0으로 잘려 느린 지형이 완전히 무해해지기
               때문입니다. */
            int dps = g_player.grounded
                    ? level_hazard_at(&g_level, g_player.pos.x, g_player.pos.z)
                    : 0;
            if (dps > 0 && g_player.health > 0) {
                g_run.hazard_accum += dps * dt;
                int whole = (int)g_run.hazard_accum;
                if (whole > 0) {
                    g_run.hazard_accum -= (float)whole;
                    g_player.health -= whole;
                    if (g_player.health < 0) g_player.health = 0;
                    /* Held at a low value rather than set to 1: standing in
                       lava should read as a constant burn, not as a series of
                       separate hits, and a full flash every tick is a strobe.
                       1로 설정하지 않고 낮은 값으로 유지합니다. 용암 위에 서 있는 것은
                       개별적인 피격의 연속이 아니라 지속적인 화상으로 읽혀야 하며, 틱마다
                       완전한 섬광은 스트로브가 됩니다. */
                    if (g_player.hurt < 0.45f) g_player.hurt = 0.45f;
                }
                /* Retriggered on a timer for the same reason the hook's reel
                   loop is: the mixer has no concept of a sustained sound.
                   훅의 감기 루프와 같은 이유로 타이머로 재시작합니다. 믹서에는 지속음
                   개념이 없습니다. */
                g_run.burn_timer -= dt;
                if (g_run.burn_timer <= 0.0f) {
                    g_run.burn_timer = 0.28f;
                    audio_play("phurt", 55);
                }
            } else {
                g_run.hazard_accum = 0.0f;
            }

            /* --- death ------------------------------------------------------
               One place notices it, whatever emptied the bar -- a swing, a
               bolt or a lava floor. Checking at each damage site instead would
               mean a new damage source could forget to kill the player, and
               "the health bar sits at zero and nothing happens" is a bug that
               looks like a design decision.
               무엇이 체력을 비웠든(근접 공격, 볼트, 용암 바닥) 한 곳에서 이를 감지합니다.
               피해 발생 지점마다 검사하면 새로운 피해원이 플레이어를 죽이는 것을 잊을 수
               있으며, "체력이 0인 채로 아무 일도 일어나지 않는다"는 설계 판단처럼 보이는
               버그입니다. */
            if (!g_run.dead && g_player.health <= 0) {
                g_run.dead = 1;
                g_run.death_time = 0.0f;
                /* The hook would otherwise keep reeling a corpse across the
                   room, and the claw is still out there.
                   그렇지 않으면 훅이 시체를 방 건너로 계속 끌어당기며, 클로도 아직
                   밖에 나가 있습니다. */
                wp_hook_release(&g_weapon);
                wp_hook_arm(&g_weapon);
                audio_play("pdie", 100);
            }
        }
        if (g_run.dead)  g_run.death_time += dt;
        if (g_run.title) g_run.title_time += dt;
        if (g_player.hurt > 0.0f) g_player.hurt -= dt * 2.0f;

        /* Particles advance even on the win screen: freezing the world mid-air
           would strand whatever was in flight when the exit was reached.
           승리 화면에서도 입자는 진행합니다. 월드를 공중에서 정지시키면 출구에 도달한
           순간 날아가던 것들이 그대로 멈춰 버립니다. */
        /* --- smoke boiling off the lava -------------------------------------
         *
         * ENGLISH
         * -------
         * Spawned at points sampled inside the hazard sectors, and ONLY where
         * the lava is actually exposed.
         *
         * That last part is the whole difficulty. A hazard sector may be
         * covered by a later sector -- the safe dais in vault sits in the
         * middle of the lava moat -- and smoke must not rise through a floor
         * the player is standing on. The test for that already exists and is
         * the same one the damage uses: level_hazard_at() resolves the
         * last-declared-wins rule, so a point under the dais reports 0 even
         * though the lava sector's own polygon contains it. Sampling a random
         * point inside the sector's bounds and asking that question is
         * therefore correct by construction -- there is no second rule to keep
         * in step, and a platform added later is automatically smoke-free.
         *
         * Rejection sampling rather than walking the polygon: a sector is an
         * arbitrary concave outline, and picking a uniformly distributed point
         * inside one properly means triangulating it. Throwing darts at its
         * bounding box and keeping the hits costs a few wasted samples on an
         * L-shaped room and no code at all.
         *
         * The height comes from the sector's floor rather than from the hit
         * point, so a puff starts ON the lava rather than at eye level above
         * it.
         *
         * Paced by a timer rather than spawned per frame. Per-frame emission
         * makes the density depend on the frame rate and lets one lava room
         * fill the shared 256-particle pool by itself -- the same rule the
         * bolt trail follows, and for the same reason.
         *
         * 한국어
         * ------
         * 위험 지형 섹터 안에서 표본 추출한 지점에 생성하되, 용암이 실제로 *노출된*
         * 곳에서만 생성합니다.
         *
         * 마지막 조건이 어려움의 전부입니다. 위험 지형 섹터는 나중에 선언된 섹터에 덮일
         * 수 있고(vault의 안전한 단상이 용암 해자 한가운데 있습니다), 연기가 플레이어가
         * 서 있는 바닥을 뚫고 올라와서는 안 됩니다. 그 판정은 이미 존재하며 피해가 쓰는
         * 것과 동일합니다. level_hazard_at()이 마지막 선언 우선 규칙을 해석하므로, 단상
         * 아래의 지점은 용암 섹터의 다각형이 그것을 포함하더라도 0을 보고합니다. 따라서
         * 섹터 경계 안의 임의의 점을 뽑아 그 질문을 던지는 것은 구조적으로 올바릅니다.
         * 동기화를 유지할 두 번째 규칙이 없으며, 나중에 추가되는 발판은 자동으로 연기가
         * 나지 않습니다.
         *
         * 다각형을 순회하지 않고 기각 표본 추출을 씁니다. 섹터는 임의의 오목한
         * 외곽선이며, 그 내부의 균일 분포 점을 제대로 뽑으려면 삼각분할이 필요합니다.
         * 바운딩 박스에 다트를 던져 맞은 것만 취하면 L자 방에서 표본 몇 개를 낭비할 뿐
         * 코드는 전혀 들지 않습니다.
         *
         * 높이는 충돌 지점이 아니라 섹터의 바닥에서 가져오므로, 연기가 용암 위쪽 눈높이가
         * 아니라 용암 *위에서* 시작합니다.
         *
         * 프레임마다가 아니라 타이머로 조절합니다. 프레임 단위 방출은 밀도를 프레임률에
         * 의존하게 만들고 용암 방 하나가 공유 256개 파티클 풀을 혼자 채우게 합니다.
         * 볼트 궤적이 따르는 것과 같은 규칙이며 이유도 같습니다. */
        if (!frozen) {
            g_run.smoke_timer -= dt;
            if (g_run.smoke_timer <= 0.0f) {
                g_run.smoke_timer = LAVA_SMOKE_INTERVAL;

                for (int s = 0; s < LAVA_SMOKE_PER_TICK; s++) {
                    /* One dart per attempt, a few attempts per puff. Giving up
                       is correct: a level with no lava should cost nothing
                       here, and a sector that is almost entirely covered
                       should emit almost nothing.
                       시도마다 다트 하나, 연기 하나당 몇 번의 시도입니다. 포기하는 것이
                       옳습니다. 용암이 없는 레벨은 이곳에서 비용이 들지 않아야 하고,
                       거의 전부 덮인 섹터는 거의 아무것도 내뿜지 않아야 합니다. */
                    for (int tries = 0; tries < 4; tries++) {
                        g_run.smoke_rng = g_run.smoke_rng * 1664525u + 1013904223u;
                        int si = (int)((g_run.smoke_rng >> 16) % (unsigned)
                                       (g_level.n_sectors > 0 ? g_level.n_sectors : 1));
                        const Sector *sec = &g_level.sectors[si];
                        if (sec->hurt <= 0 || !sec->has_bounds) continue;

                        g_run.smoke_rng = g_run.smoke_rng * 1664525u + 1013904223u;
                        float fx_ = (g_run.smoke_rng >> 8) * (1.0f / 16777216.0f);
                        g_run.smoke_rng = g_run.smoke_rng * 1664525u + 1013904223u;
                        float fz_ = (g_run.smoke_rng >> 8) * (1.0f / 16777216.0f);

                        float x = (sec->min_x + (sec->max_x - sec->min_x) * fx_) * 0.01f;
                        float z = (sec->min_z + (sec->max_z - sec->min_z) * fz_) * 0.01f;

                        /* The one question that matters, and it is the same
                           one the damage asks. A covered point answers 0.
                           중요한 유일한 질문이며 피해가 묻는 것과 같습니다. 덮인
                           지점은 0을 답합니다. */
                        if (level_hazard_at(&g_level, x, z) <= 0) continue;

                        /* Far-off smoke is invisible and still costs a slot in
                           a shared pool, so it is not spawned at all.
                           먼 곳의 연기는 보이지 않으면서 공유 풀의 슬롯을 차지하므로
                           아예 생성하지 않습니다. */
                        float dx = x - g_player.pos.x, dz = z - g_player.pos.z;
                        if (dx*dx + dz*dz > LAVA_SMOKE_RANGE * LAVA_SMOKE_RANGE)
                            continue;

                        v3 at = v3f(x, sec->floor * 0.01f + 0.05f, z);
                        fx_spawn("lavasmoke", at, v3f(0.0f, 1.0f, 0.0f));
                        break;
                    }
                }
            }
        }

        fx_update(dt);

        /* Pickups top up health and ammo when walked over. */
        if (!frozen)
            pickup_update(g_player.pos, &g_player.health, PLAYER_MAX_HP,
                          &g_weapon.ammo, WEAPON_MAX_AMMO, dt);

        /* Reaching the exit either loads the next level or, on a level with no
           `next`, ends the game. Health and ammo carry over on a transition,
           the way a Doom episode runs -- the exit is a reward you arrive at,
           not a reset. */
        if (!frozen && level_exit_at(&g_level, g_player.pos.x, g_player.pos.z)) {
            if (!g_level.next[0]) {
                /* Terminal level: the exit goes nowhere, so this is the end. */
                g_run.won = 1;
                audio_play("win", 100);
            } else {
                /* Copied out before the load: level_load blanks the
                   destination's `next` field before parsing, so handing it
                   g_level.next directly would blank the search string
                   mid-call. dest is a buffer load_level never touches.
                   로드 전에 복사합니다. level_load는 파싱 전에 대상의 `next` 필드를
                   비우므로, g_level.next를 그대로 넘기면 호출 도중에 검색 문자열이
                   지워집니다. dest는 load_level이 건드리지 않는 버퍼입니다. */
                char dest[32];
                txt_copy(dest, sizeof(dest), g_level.next, -1);

                /* carry_state=1: health and ammo cross the exit, the way a Doom
                   episode runs -- the exit is a reward you arrive at, not a
                   reset. An unknown target changes nothing and leaves the
                   player where they are; a typo does not win the game. */
                if (load_level(dest, &scene, 1, 1)) {
                    txt_copy(cur_level, sizeof(cur_level), dest, -1);
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
            wp_set_model("shotgun");
            wp_reload_texture();          /* also flushes the texture cache */
            audio_reload();
            fx_reload();                  /* re-read effects.txt, drop live particles */

            /* After the flush, not before: tex_flush deletes every cached
               material, so resolving the level's materials first would leave
               level_tex holding deleted texture names. load_level resolves
               them, so it has to run after wp_reload_texture.
               플러시 이후여야 합니다. tex_flush는 캐시된 모든 재질을 삭제하므로, 레벨의
               재질을 먼저 해석하면 level_tex가 삭제된 텍스처 이름을 보유하게 됩니다.
               load_level이 재질을 해석하므로 wp_reload_texture 다음에 실행되어야 합니다. */

            /* carry_state=0: an edit to the map is an authoring action, so the
               player is respawned at the (possibly moved) start with a full
               belt rather than left standing wherever the old geometry put
               them -- which could now be inside a wall. This previously did
               not respawn at all, and whether that was deliberate could not be
               answered from the code.
               맵 편집은 제작 행위이므로 플레이어를 (옮겨졌을 수 있는) 시작 지점에 탄약을
               채워 다시 스폰합니다. 예전 지오메트리 기준의 위치에 그대로 두면 이제 벽
               속일 수도 있습니다. 이전에는 아예 다시 스폰하지 않았는데, 그것이 의도인지
               코드만으로는 판단할 수 없었습니다. */
            load_level(cur_level, &scene, 1, 0);
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

        /* Vertex snapping, on for the world and off again before the UI.
           The grid is the offscreen buffer, so the quantisation lands on the
           pixels the image is actually rasterised into. post_size reports 0,0
           when the pass is off, which disables the snap -- the right answer,
           since with no pixelisation there is no grid to snap to.
           정점 스냅입니다. 월드에 대해 켜고 UI 이전에 다시 끕니다. 격자는 오프스크린
           버퍼이므로 양자화가 이미지가 실제로 래스터화되는 픽셀에 놓입니다. 패스가 꺼져
           있으면 post_size가 0,0을 보고하여 스냅이 비활성화되는데, 픽셀화가 없으면 맞출
           격자도 없으므로 올바른 동작입니다. */
        /* The clock the lava flows against. Advances with dt rather than being
           read from a system timer, so it stops when the world does -- a lava
           floor that keeps churning behind a pause menu says the game is still
           running when it is not.
         *
         * Wrapped at a whole number of the shader's own pulse periods, so the
         * pulse is continuous across the reset. The DRIFT term is an unbounded
         * scroll and cannot be made seamless this way: it jumps once every
         * WRAP seconds. At four and a half minutes between jumps, on a surface
         * whose whole appearance is churning noise, that is a trade worth
         * making against the alternative of the flow visibly stepping as float
         * precision runs out.
         *
         * 용암이 흐르는 기준 시계입니다. 시스템 타이머가 아니라 dt로 진행하므로 월드가
         * 멈추면 함께 멈춥니다. 일시정지 메뉴 뒤에서 계속 끓는 용암 바닥은 게임이 멈춰
         * 있는데도 돌아가고 있다고 말하는 셈입니다.
         *
         * 셰이더 자체 맥동 주기의 정수배에서 순환시키므로 맥동은 초기화를 넘어 연속입니다.
         * DRIFT 항은 경계 없는 스크롤이라 이 방식으로 매끄럽게 만들 수 없으며, WRAP초마다
         * 한 번 튑니다. 4분 30초에 한 번, 그것도 외형 전체가 끓는 노이즈인 표면에서라면,
         * float 정밀도가 바닥나며 흐름이 눈에 띄게 끊기는 대안보다 나은 거래입니다. */
        {
            /* 20 x (2*pi / PULSE), with PULSE = 0.45 in the shader. */
            const float WRAP = 279.253f;
            if (!frozen) g_run.world_time += dt;
            if (g_run.world_time > WRAP) g_run.world_time -= WRAP;
            rd_time(g_run.world_time);
        }

        {
            int sw, sh;
            post_size(&sw, &sh);
            rd_snap((float)sw / PSX_SNAP_COARSE, (float)sh / PSX_SNAP_COARSE);
        }

        /* Recoil rides on top of the player's own pitch and springs back. */
        float aim_pitch = g_pitch + g_weapon.recoil;

        /* --- the death collapse ----------------------------------------------
           Applied to the CAMERA rather than to g_player.pos, so the body the
           simulation knows about never moves. Sinking the real position would
           put the eye inside the floor, where level_trace reports an immediate
           hit at zero range -- the failure mode hooktest's fixture note
           describes -- and would leave the player somewhere they could not
           legally stand if the run were ever resumed.
           g_player.pos가 아니라 *카메라*에 적용하므로 시뮬레이션이 아는 몸은 움직이지
           않습니다. 실제 위치를 내리면 눈이 바닥 안으로 들어가고, 그곳에서 level_trace는
           거리 0에서 즉시 충돌을 보고합니다. hooktest의 픽스처 주석이 설명하는 실패
           양상이며, 플레이가 재개된다면 플레이어가 합법적으로 설 수 없는 곳에 남게
           됩니다. */
        v3    eye_pos   = g_player.pos;
        float cam_pitch = aim_pitch;
        float cam_roll  = 0.0f;
        if (g_run.dead) {
            float k = g_run.death_time / DEATH_ANIM_TIME;
            if (k > 1.0f) k = 1.0f;
            /* Ease out: 1-(1-k)^2. Fast at the start, settling at the end --
               a body falls, it does not descend.
               감속 이징입니다. 처음에 빠르고 끝에서 안착합니다. 몸은 떨어지는 것이지
               내려가는 것이 아닙니다. */
            float e = 1.0f - (1.0f - k) * (1.0f - k);
            eye_pos.y  -= DEATH_DROP * e;
            cam_pitch  -= DEATH_PITCH * e;
            cam_roll    = DEATH_ROLL * e;
        }

        mat4 proj = mat4_perspective(WORLD_FOV, aspect, 0.05f, 200.0f);
        mat4 view = mat4_fps_view_roll(eye_pos, g_yaw, cam_pitch, cam_roll);
        mat4 vp   = mat4_mul(proj, view);

        /* The basis the billboards face along. Derived from the SAME pitch and
           roll the view matrix uses, or the sprites would keep facing where the
           camera used to be and turn edge-on as it rolls -- monsters vanishing
           as the player dies is exactly the wrong thing to lose sight of.
           빌보드가 향하는 기저입니다. 뷰 행렬이 사용하는 것과 *동일한* 피치와 롤에서
           유도합니다. 그렇지 않으면 스프라이트가 카메라가 있던 곳을 계속 향하다가 롤링에
           따라 옆으로 서 버립니다. 플레이어가 죽는 순간 몬스터가 사라지는 것은 시야에서
           놓쳐서는 안 될 바로 그것입니다. */
        float cy = cosf(g_yaw), sy = sinf(g_yaw);
        float cp = cosf(cam_pitch), sp = sinf(cam_pitch);
        v3 cam_fwd   = v3f(-sy * cp, sp, -cy * cp);
        v3 cam_right = v3f(cy, 0, -sy);
        v3 cam_up    = v3cross(cam_right, cam_fwd);
        if (cam_roll != 0.0f) {
            float cr = cosf(cam_roll), sr = sinf(cam_roll);
            v3 r2 = v3add(v3scale(cam_right, cr), v3scale(cam_up, sr));
            v3 u2 = v3add(v3scale(cam_right, -sr), v3scale(cam_up, cr));
            cam_right = r2;
            cam_up    = u2;
        }

        /* --- world ---
           Lit and fogged from the camera's real position, which during the
           collapse is the fallen eye rather than the standing one.
           카메라의 실제 위치를 기준으로 조명과 안개를 적용합니다. 쓰러지는 동안 그것은
           서 있던 눈이 아니라 넘어진 눈입니다. */
        scene_draw_level(&scene, vp, eye_pos, &g_level);

        /* --- monsters, pickups and projectiles ---
           Three sprite passes, each building its billboards on the CPU and
           uploading once. They stay on the world side of the pass boundary so
           they are pixelised and dithered along with everything else in the
           scene -- see scene.h. */
        scene_draw_enemies(&scene, vp, eye_pos, cam_right);
        scene_draw_pickups(&scene, vp, eye_pos, cam_right);
        scene_draw_shots  (&scene, vp, cam_right, cam_up);
        fx_draw(vp, cam_right, cam_up);

        /* --- bullet holes and tracers, still in world space --- */
        glDisable(GL_CULL_FACE);
        wp_draw_world(&g_weapon, vp, eye_pos, cam_right, cam_up);

        /* --- the gun, over a cleared depth buffer ---
           Dropped the moment the player dies. The view model is drawn in its
           own space with its own projection, so it does not roll or fall with
           the camera -- it would hang perfectly level in the middle of the
           screen while the world tipped over behind it, which is a far louder
           tell than simply not being there. A dead hand lets go.
           플레이어가 죽는 순간 사라집니다. 뷰 모델은 자체 공간에서 자체 투영으로 그려지므로
           카메라와 함께 기울거나 떨어지지 않습니다. 뒤에서 월드가 넘어가는 동안 화면
           한가운데에 완벽히 수평으로 떠 있게 되는데, 그것은 그냥 없는 것보다 훨씬 요란한
           표시입니다. 죽은 손은 놓습니다. */
        glEnable(GL_CULL_FACE);
        if (!g_run.dead) wp_draw_view(&g_weapon, aspect);

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

        /* --- crosshair ---
           Hidden while frozen, for the same reason it is hidden on the win
           screen: a crosshair implies you can still act.
           정지 중에는 숨깁니다. 승리 화면에서 숨기는 것과 같은 이유이며, 조준점은 아직
           행동할 수 있음을 암시하기 때문입니다. */
        if (!frozen) {
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

        /* --- HUD and the win screen, at native resolution ---
           Both are UI, so both run after post_end: magnified and dithered,
           5x7 glyphs are unreadable.

           The HUD draws unconditionally and the win screen goes OVER it, dim
           included. Making them exclusive would be the obvious simplification
           and it is wrong: the readouts belong to the frozen frame underneath,
           and the dimming is what pushes them back rather than removing them. */
        /* The UI is drawn at native resolution and must not wobble: text
           snapped to a coarse grid loses whole glyph rows.
           UI는 원해상도로 그려지며 흔들려서는 안 됩니다. 성긴 격자에 스냅된 텍스트는
           글리프의 행 전체를 잃습니다. */
        rd_snap(0.0f, 0.0f);

        /* The HUD is skipped on the title screen: health and ammo belong to a
           run, and showing them before one has started says the game is
           already in progress.
           타이틀 화면에서는 HUD를 건너뜁니다. 체력과 탄약은 진행 중인 플레이에 속하며,
           시작하기도 전에 표시하면 게임이 이미 진행 중이라고 말하는 셈입니다. */
        if (!g_run.title) scene_draw_hud(&scene, vw, vh, &g_player, &g_weapon);

        /* The three end/start screens are mutually exclusive by construction:
           g_run.title is cleared before a run can begin, and a run that has been
           won cannot also have been lost.
           세 개의 시작·종료 화면은 구조적으로 상호 배타적입니다. `title`은 플레이가
           시작되기 전에 해제되며, 승리한 플레이가 동시에 패배할 수는 없습니다. */
        if (g_run.title)
            scene_draw_title(&scene, vw, vh, g_run.title_time);
        else if (g_run.won)
            scene_draw_win(&scene, vw, vh, &g_player, &g_weapon);
        else if (g_run.dead)
            /* The overlay's clock starts when the COLLAPSE ends, not when the
               player dies. Fading a red wash in over the fall would hide the
               animation behind it, and the fall is the part that says what
               happened.
               오버레이의 시계는 플레이어가 죽을 때가 아니라 *쓰러짐이 끝날 때* 시작합니다.
               넘어지는 동안 붉은 막을 덮으면 그 뒤로 애니메이션이 가려지는데, 무슨 일이
               있었는지 말해 주는 것이 바로 그 넘어짐입니다. */
            scene_draw_death(&scene, vw, vh, g_run.death_time - DEATH_ANIM_TIME,
                             g_run.death_time > DEATH_INPUT_DELAY);

        /* Last, so it sits over the HUD and the win screen both. A menu opened
           from the win screen has to be readable too, and it is the thing the
           player is currently operating.
           마지막에 그려 HUD와 승리 화면 양쪽 위에 놓입니다. 승리 화면에서 연 메뉴도 읽을
           수 있어야 하며, 그것이 플레이어가 지금 조작하고 있는 대상입니다. */
        scene_draw_menu(&scene, vw, vh);

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

    /* Pairs every mb_init in scene_init, and level_buf's below. The process is
       about to exit and the OS would reclaim all of it anyway, but render.h
       states the contract and this file is the one people copy from when they
       add a buffer -- leaving it unpaired here is how it stops being kept
       anywhere.
       scene_init의 모든 mb_init과 짝을 맞춥니다. 프로세스가 곧
       종료되므로 OS가 어차피 전부 회수하지만, render.h가 계약을 명시하고 있으며 새
       버퍼를 추가하는 사람이 참고하는 파일이 바로 이 파일입니다. 이곳에서 짝을 맞추지
       않으면 그 계약은 어디에서도 지켜지지 않게 됩니다. */
    scene_free(&scene);

    post_shutdown();
    audio_shutdown();
    cursor_show(1);   /* leave the desktop's pointer as we found it */
    return 0;
}
