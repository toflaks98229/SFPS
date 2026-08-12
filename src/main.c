/**
 * @file main.c
 * @brief SFPS -- a 3D FPS that fits on a 1.44MB floppy. Window, input and drawing.
 *
 * ENGLISH
 * -------
 * This file owns only what cannot be tested headlessly: the Win32 window, the
 * GL context, raw input, the graphics settings that resize a render target, and
 * the order the draw passes run in.
 *
 * The per-frame UPDATE used to be here too, in the body of `WinMain`, and is
 * not any more -- see world.c. Its order was declared load-bearing by this very
 * comment and nothing could check it, because the only way to run it was to
 * open a window and play. tools\steptest.c now runs it with no window at all.
 *
 * The line between the two is drawn by what needs hardware. The simulation
 * hands this file a ::World and a `frozen` flag; this file turns them into
 * pixels, and turns Win32 messages back into an ::Input. Nothing here decides
 * what a frame MEANS.
 *
 * 한국어
 * ------
 * 이 파일은 헤드리스로 테스트할 수 없는 요소만을 담당합니다. Win32 창, GL 컨텍스트, 원시
 * 입력, 렌더 타깃 크기를 바꾸는 그래픽 설정, 그리고 드로우 패스가 실행되는 순서입니다.
 *
 * 프레임별 *갱신*도 이전에는 `WinMain`의 본문 안에 있었으나 이제는 없습니다. world.c를
 * 참조하십시오. 그 순서는 바로 이 주석이 구조적으로 중요하다고 선언했지만 무엇도 그것을
 * 검사할 수 없었습니다. 실행하는 유일한 방법이 창을 열고 플레이하는 것이었기 때문입니다.
 * 이제 tools\steptest.c가 창 없이 그것을 실행합니다.
 *
 * 둘 사이의 경계는 "하드웨어가 필요한가"로 그어집니다. 시뮬레이션이 이 파일에 ::World와
 * `frozen` 플래그를 건네고, 이 파일은 그것을 픽셀로 바꾸며, Win32 메시지를 다시 ::Input으로
 * 바꿉니다. 이곳의 어떤 것도 한 프레임이 무엇을 *뜻하는지* 결정하지 않습니다.
 */

/* model.h, tex.h and sprite.h are deliberately absent, and so now are enemy.h,
   pickup.h, proj.h, door.h, level.h and txt.h. The level's geometry, its
   materials and the sprite atlases moved into scene.c; the monsters, the items,
   the projectiles, the doors and the level lifecycle moved into world.c. This
   file no longer names any of those types -- and an include kept "just in case"
   is how a header graph stops describing what actually depends on what.
   model.h, tex.h, sprite.h는 의도적으로 제외했으며, 이제 enemy.h, pickup.h, proj.h,
   door.h, level.h, txt.h도 그렇습니다. 레벨의 지오메트리·재질·스프라이트 아틀라스는
   scene.c로, 몬스터·아이템·발사체·문·레벨 수명 주기는 world.c로 옮겨 갔습니다. 이 파일은
   더 이상 그 타입들을 언급하지 않습니다. "혹시 몰라서" 남겨 둔 include는 헤더 그래프가
   실제 의존 관계를 설명하지 못하게 만드는 원인입니다. */
#include "world.h"    /* World and Input: the simulation this file drives */
#include "render.h"
#include "scene.h"    /* the per-frame draw passes and everything they own */
#include "weapon.h"   /* the view model, the world decals and the HUD */
#include "post.h"
#include "menu.h"     /* the ESC menu: pause, settings, restart, quit */
#include "font.h"
#include "audio.h"
#include "data.h"
#include "fx.h"       /* fx_draw -- the particles, on the world side of the pass */
#include "diag.h"

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

/* WORLD_FOV and MOUSE_SENS used to be here. The first moved to world.h because
   the simulation and the renderer both need the same value; the second moved
   into world.c entirely, because pixels-to-radians is what the world does with
   a delta and nothing here has an opinion about it.
   WORLD_FOV와 MOUSE_SENS가 이곳에 있었습니다. 앞의 것은 시뮬레이션과 렌더러가 같은 값을
   필요로 하므로 world.h로 옮겼습니다. 뒤의 것은 world.c로 완전히 옮겼습니다. 픽셀을
   라디안으로 바꾸는 것은 월드가 변화량에 대해 하는 일이고, 이곳은 그에 대해 아무 견해도 갖지
   않기 때문입니다. */

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

/**
 * @brief Seconds the death screen ignores input for.
 *
 * ENGLISH
 * -------
 * The trigger that killed you is usually still held. Without this the death
 * screen appears and vanishes inside one frame, which reads as the game not
 * having one.
 *
 * @note Here rather than in world.h because both of its readers are in this
 *       file: ::wnd_proc, which turns a keypress into a restart, and the death
 *       overlay, which tells the player the key will work now.
 *
 * 한국어
 * ------
 * @brief 사망 화면이 입력을 무시하는 시간(초)입니다.
 *
 * 당신을 죽인 방아쇠는 대개 아직 눌린 상태입니다. 이것이 없으면 사망 화면이 한 프레임
 * 안에 나타났다 사라지며, 게임에 사망 화면이 없는 것처럼 보입니다.
 *
 * @note world.h가 아니라 이곳에 있는 이유는 이 값을 읽는 두 곳이 모두 이 파일에 있기
 *       때문입니다. 키 입력을 재시작으로 바꾸는 ::wnd_proc, 그리고 이제 키가 동작한다고
 *       플레이어에게 알리는 사망 화면 오버레이입니다.
 */
#define DEATH_INPUT_DELAY 0.8f

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

/* --- The simulation / 시뮬레이션 --- */

/**
 * @brief The level, the player, the weapon and the run.
 *
 * ENGLISH
 * -------
 * A file-scope global because ::wnd_proc needs it: a message arrives outside
 * any frame, and dismissing the title screen or asking for a restart writes to
 * the run. It is otherwise passed by pointer everywhere, which is what lets
 * tools\steptest.c own one of its own.
 *
 * @warning Never copied or moved. ::wp_init records the address of
 *          `g_world.level` inside the weapon module, and that is what hitscans
 *          trace against. See ::World.
 *
 * 한국어
 * ------
 * @brief 레벨, 플레이어, 무기, 플레이.
 *
 * 파일 스코프 전역인 이유는 ::wnd_proc이 이것을 필요로 하기 때문입니다. 메시지는 어떤
 * 프레임 바깥에서 도착하며, 타이틀 화면을 해제하거나 재시작을 요청하는 것은 플레이 상태에
 * 기록하는 일입니다. 그 밖의 모든 곳에서는 포인터로 전달되며, 그것이 tools\steptest.c가
 * 자신만의 World를 가질 수 있게 하는 이유입니다.
 *
 * @warning 결코 복사하거나 옮기지 않습니다. ::wp_init이 `g_world.level`의 주소를 무기 모듈
 *          안에 기록하며, 히트스캔이 그것을 대상으로 탐색합니다. ::World를 참조하십시오.
 */
static World g_world;

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
    cursor_show(menu_is_open() || g_world.run.title || g_world.run.dead || !g_focused);
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
 * @note Only records state; all gameplay happens in ::world_step. Acting on
 *       input here would run at message rate rather than frame rate.
 * @note What IS handled here is every EDGE -- one press must be one step. A
 *       held key polled once a frame would walk a whole menu in a frame, toggle
 *       the pixelise pass every frame, and skip the death screen on the shot
 *       that caused it. Held state goes into ::g_keys and is read by
 *       ::input_gather instead.
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
 * @note 상태를 기록하기만 합니다. 모든 게임 로직은 ::world_step에서 처리됩니다. 여기서
 *       입력에 반응하면 프레임 단위가 아닌 메시지 단위로 실행되기 때문입니다.
 * @note 이곳에서 처리하는 것은 모든 *엣지*입니다. 한 번 누름은 한 단계여야 합니다. 프레임마다
 *       폴링되는 유지 키는 한 프레임에 메뉴 전체를 지나가고, 매 프레임 픽셀화 패스를
 *       전환하며, 사망을 유발한 그 사격으로 사망 화면을 건너뜁니다. 유지 상태는 ::g_keys에
 *       들어가고 ::input_gather가 대신 읽습니다.
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
        if (g_world.run.title && !menu_is_open() && wp != VK_ESCAPE) {
            g_world.run.title = 0;
            g_warp_mouse = 1;
            cursor_update();
            return 0;
        }

        /* ESC opens the menu; it no longer quits. One mistaken keypress used
           to end a run outright with no confirmation, which is the behaviour
           this menu exists to replace. Leaving is now a row the player picks.
           ESC는 메뉴를 열며 더 이상 종료하지 않습니다. 예전에는 잘못 누른 키 하나가
           확인 없이 진행 중인 플레이를 그대로 끝냈으며, 그것이 이 메뉴가 대체하려는
           동작입니다. 이제 나가는 것은 플레이어가 직접 고르는 행입니다. */
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
                wp_hook_release(&g_world.weapon);
                wp_hook_arm(&g_world.weapon);
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
        if (g_world.run.dead && !menu_is_open()
            && g_world.run.death_time > DEATH_INPUT_DELAY) {
            g_world.run.restart_wanted = 1;
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
           rather than polled, so one press is one toggle -- a held key would
           otherwise flip it every frame. Comparing the two looks side by side
           is most of how the effect gets tuned.
           F1은 픽셀화/디더 패스를 전환합니다. 폴링하지 않고 메시지 시점에 처리하므로 한
           번 누르면 한 번 전환됩니다. 그렇지 않으면 키를 누르고 있는 동안 매 프레임
           전환됩니다. 두 화면을 나란히 비교하는 것이 이 효과를 조정하는 주된
           방법입니다. */
        if (wp == VK_F1) { post_set_enabled(!post_enabled()); return 0; }

        /* --- weapon select ---------------------------------------------
           On the message edge like the toggles above, so one press is one
           switch. A weapon that is not owned is not selected: silently
           refusing is better than an empty hand, and the number keys stay
           harmless before the roster fills out.
           위의 토글들과 마찬가지로 메시지 시점에 처리하므로 한 번 누르면 한 번
           전환됩니다. 보유하지 않은 무기는 선택되지 않습니다. 빈손보다는 조용히
           거절하는 편이 낫고, 구성이 갖춰지기 전까지 숫자 키가 무해하게 유지됩니다. */
        if (wp >= '1' && wp < '1' + WP_TYPES) {
            int want = (int)(wp - '1');
            if (g_world.weapon.owned[want] && g_world.weapon.cur != want) {
                g_world.weapon.cur = want;
                /* A switch cancels a swing in progress rather than carrying
                   its timer across: the axe's dash must not be inherited by
                   the shotgun.
                   진행 중인 공격을 이월하지 않고 취소합니다. 도끼의 대쉬를 샷건이
                   물려받아서는 안 됩니다. */
                g_world.weapon.cooldown = 0.0f;

                /* A weapon that announces itself does so here, on the switch
                   rather than on the first swing: the saw revving up is what
                   tells the player the change took, and hearing it only once
                   they attack makes the switch itself feel unacknowledged.
                   자기를 알리는 무기는 첫 공격이 아니라 *전환 시점*인 이곳에서 그렇게
                   합니다. 톱이 돌기 시작하는 소리가 전환이 먹혔다는 것을 알려 주며, 공격할
                   때에야 들린다면 전환 자체가 응답 없는 것처럼 느껴집니다. */
                const char *dsnd = wp_stats(want)->draw_snd;
                if (dsnd) audio_play(dsnd, 85);
            }
            return 0;
        }
        g_keys[wp & 0xff] = 1;
        return 0;
    case WM_KEYUP:
        g_keys[wp & 0xff] = 0;
        return 0;
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
        if (g_world.run.title && !menu_is_open()) {
            g_world.run.title = 0;
            g_warp_mouse = 1;
            cursor_update();
            return 0;
        }
        if (g_world.run.dead && !menu_is_open()
            && g_world.run.death_time > DEATH_INPUT_DELAY) {
            g_world.run.restart_wanted = 1;
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
        wp_hook_release(&g_world.weapon);   /* do not leave it attached off-screen */
        wp_hook_arm(&g_world.weapon);       /* the button is up now, so rearm with it */
        cursor_update();
        for (int i = 0; i < 256; i++) g_keys[i] = 0;
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

/* ------------------------------------------------------------------- input */

/**
 * @brief Turns this frame's hardware state into an ::Input.
 *
 * ENGLISH
 * -------
 * @param[out] in Filled in completely; every field is written.
 * @param[in]  w  The world, asked only whether it is frozen.
 *
 * @note Mouse look is read and the cursor recentred only while the window has
 *       focus and the world is running. Frozen, the cursor belongs to the
 *       player again, so it is neither read nor recentred -- recentring it
 *       would fight whatever they are doing with it and pin the pointer to the
 *       middle of the screen.
 * @note The gate is ::world_frozen and not menu_is_open(). It has to be: the
 *       title and death screens show a visible cursor and are operated with it,
 *       exactly as the menu is. When the read lived inside the update it was
 *       gated on the freeze for free, because the whole update was; pulling it
 *       out here is what made the gate something that had to be stated.
 * @note The delta is read even on frames the grapple has the aim locked. The
 *       lock lives in ::world_step and skips only the application to yaw and
 *       pitch; skipping the READ instead would let the pointer wander off the
 *       window and come back as one enormous jump the moment the hook ended,
 *       which is the same class of bug ::g_warp_mouse exists to prevent.
 *
 * 한국어
 * ------
 * @brief 이번 프레임의 하드웨어 상태를 ::Input으로 바꿉니다.
 * @param[out] in 모든 필드가 완전히 채워집니다.
 *
 * @note 마우스 시점은 창이 포커스를 갖고 월드가 진행 중일 때만 읽고 커서를 중앙으로
 *       되돌립니다. 정지 상태에서는 커서가 다시 플레이어의 것이므로 읽지도 되돌리지도
 *       않습니다. 커서를 중앙에 붙들면 플레이어의 조작과 충돌하며 포인터가 화면 한가운데에
 *       고정됩니다.
 * @note 판정 기준은 menu_is_open()이 아니라 ::world_frozen입니다. 그래야만 합니다. 타이틀
 *       화면과 사망 화면도 커서를 보여 주고 커서로 조작하며, 이는 메뉴와 정확히 같습니다.
 *       읽기가 갱신 안에 있을 때는 갱신 전체가 정지에 걸려 있었으므로 이 조건이 공짜로
 *       따라왔습니다. 이곳으로 끄집어낸 것이 그 조건을 명시해야 하는 것으로 만들었습니다.
 * @note 그래플이 조준을 고정한 프레임에도 변화량은 읽습니다. 고정은 ::world_step에 있으며
 *       yaw와 pitch에 적용하는 것만 건너뜁니다. *읽기*를 건너뛰면 포인터가 창 밖으로
 *       벗어났다가 훅이 끝나는 순간 거대한 도약으로 돌아오는데, 이는 ::g_warp_mouse가
 *       방지하기 위해 존재하는 것과 동일한 종류의 버그입니다.
 */
static void input_gather(Input *in, const World *w) {
    Input z = {0};
    *in = z;

    in->paused = menu_is_open();

    if (g_focused && !world_frozen(w, in->paused)) {
        RECT rc; GetClientRect(g_wnd, &rc);
        POINT centre = {(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
        ClientToScreen(g_wnd, &centre);

        if (g_warp_mouse) {
            /* Just took focus: park the cursor and consume this frame's delta. */
            g_warp_mouse = 0;
        } else {
            POINT cur; GetCursorPos(&cur);
            in->look_dx = (float)(cur.x - centre.x);
            in->look_dy = (float)(cur.y - centre.y);
        }

        SetCursorPos(centre.x, centre.y);
    }

    in->forward = g_keys['W'];
    in->back    = g_keys['S'];
    in->left    = g_keys['A'];
    in->right   = g_keys['D'];
    in->jump    = g_keys[VK_SPACE];

    /* Gated on focus so a click that landed in another window is not a shot
       fired in this one. */
    in->fire = g_focused && g_mouse_down;
    in->hook = g_hook_down;
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
 *       it, never the other way round -- a buffer sized by rounding the width
 *       lets the magnification differ per axis, which stretches the image.
 *
 * 한국어
 * ------
 * @brief 창과 프리셋에 맞추어 오프스크린 타깃을 (재)생성합니다.
 * @param[in] preset ::GfxPixelPreset 값.
 * @note ::post_init은 무조건 할당하므로 기존 타깃을 먼저 해제하지 않으면 GL 객체가
 *       누수됩니다. ::post_shutdown이 모듈을 완전히 초기화하므로 해제 후 재생성이
 *       안전한 크기 변경 방법입니다.
 * @note 정수 확대 배율을 *먼저* 정하고 버퍼를 그로부터 유도하며, 결코 그 반대가 아닙니다.
 *       너비를 반올림해 크기를 정한 버퍼는 축마다 배율이 달라질 수 있고, 그러면 이미지가
 *       늘어납니다.
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

/**
 * @brief Applies every menu setting that needs no signal to change.
 *
 * ENGLISH
 * -------
 * Read straight from the menu every frame, so toggling one is visible on the
 * very next frame and nothing has to notify anything. The two settings that
 * cannot work this way -- the display mode and the pixel size -- reallocate a
 * render target, so they arrive as menu ACTIONS instead.
 *
 * 한국어
 * ------
 * @brief 변경에 신호가 필요 없는 모든 메뉴 설정을 적용합니다.
 *
 * 매 프레임 메뉴에서 직접 읽으므로 전환하면 바로 다음 프레임에 반영되며 무엇도 무엇에게
 * 알릴 필요가 없습니다. 이 방식이 통하지 않는 두 설정(디스플레이 모드와 픽셀 크기)은 렌더
 * 타깃을 재할당하므로 메뉴 *액션*으로 도착합니다.
 */
static void apply_live_settings(void) {
    post_set_enabled(menu_settings()->post_on);
    post_set_scanline(menu_settings()->scanlines ? POST_SCANLINE_DEFAULT : 0.0f);

    /* Steps per channel, and the grain that rides on them. A table rather
       than arithmetic on the enum, because these are four points chosen by
       eye and the spacing between them is not regular -- HEAVY to NORMAL
       is the jump that matters and the rest is fine tuning.
       OFF is not zero dither: it is thirty-two steps, the PlayStation's own
       five bits a channel, where the pattern stops being visible on its own
       rather than being switched off.
       채널당 단계 수와 그 위에 얹히는 그레인입니다. 열거형에 대한 산술이 아니라
       표인 이유는, 이 넷이 눈으로 고른 지점이고 간격이 규칙적이지 않기 때문입니다.
       HEAVY에서 NORMAL로 가는 것이 중요한 도약이고 나머지는 미세 조정입니다.
       OFF는 디더 0이 아니라 32단계, 즉 플레이스테이션 자신의 채널당 5비트이며,
       패턴이 꺼지는 것이 아니라 그 자체로는 보이지 않게 되는 지점입니다. */
    static const struct { float levels, grain; } DITHER[GFX_DITHER_COUNT] = {
        {  4.0f, 0.050f },   /* HEAVY  -- what this shipped with */
        { 12.0f, 0.015f },   /* NORMAL */
        { 20.0f, 0.008f },   /* LIGHT  */
        { 32.0f, 0.000f },   /* OFF    -- the PlayStation's 15-bit colour */
    };
    int di = menu_settings()->dither;
    if (di < 0 || di >= GFX_DITHER_COUNT) di = GFX_DITHER_NORMAL;
    int pat = menu_settings()->pattern;
    if (pat < 0 || pat >= GFX_PATTERN_COUNT) pat = GFX_PATTERN_BAYER;
    post_set_dither(DITHER[di].levels, DITHER[di].grain,
                    pat == GFX_PATTERN_NOISE ? 1.0f : 0.0f);
}

/* -------------------------------------------------------------------- draw */

/**
 * @brief Draws one frame of a ::World, world first and UI second.
 *
 * ENGLISH
 * -------
 * @param[in]     w      The world to draw. Read only: drawing decides nothing.
 * @param[in,out] sc     The draw passes and the buffers they build into.
 * @param[in]     vw     Client width, pixels.
 * @param[in]     vh     Client height, pixels. At least 1.
 * @param[in]     frozen What ::world_step returned. NOT re-derived here -- the
 *                       step may have set `dead` this very frame, and asking
 *                       again would hide the crosshair one frame before the
 *                       death screen it belongs to appears.
 *
 * @note ::post_end is the world/UI boundary and everything about the order here
 *       is arranged around it. Above it the frame goes through the pixelise and
 *       dither pass; below it, nothing does -- 5x7 glyphs magnified four times
 *       are unreadable and dithered text is worse. The gun is deliberately
 *       ABOVE the line: it is part of the scene's lighting, and a crisp weapon
 *       over a pixelated world reads as a bug. diag.h's DIAG_PASS_ORDER watches
 *       this at runtime.
 *
 * 한국어
 * ------
 * @brief ::World의 한 프레임을 그립니다. 월드가 먼저, UI가 나중입니다.
 * @param[in]     w      그릴 월드. 읽기 전용입니다. 그리기는 아무것도 결정하지 않습니다.
 * @param[in,out] sc     드로우 패스와 그것들이 사용하는 버퍼.
 * @param[in]     vw     클라이언트 영역 너비 (픽셀).
 * @param[in]     vh     클라이언트 영역 높이 (픽셀). 최소 1입니다.
 * @param[in]     frozen ::world_step이 반환한 값입니다. 이곳에서 다시 유도하지 *않습니다*.
 *                       갱신이 바로 이번 프레임에 `dead`를 설정했을 수 있으며, 다시 물으면
 *                       그에 해당하는 사망 화면이 나타나기 한 프레임 전에 조준점이
 *                       사라집니다.
 *
 * @note ::post_end가 월드와 UI의 경계이며 이곳 순서의 모든 것이 그것을 중심으로 배치되어
 *       있습니다. 그 위쪽은 픽셀화와 디더 패스를 거치고, 아래쪽은 거치지 않습니다. 5x7
 *       글리프를 4배 확대하면 읽을 수 없고 디더링된 텍스트는 더 나쁩니다. 총기는 의도적으로
 *       경계 *위쪽*에 있습니다. 총기는 장면 조명의 일부이며, 픽셀화된 월드 위의 선명한
 *       무기는 버그처럼 보입니다. diag.h의 DIAG_PASS_ORDER가 이를 런타임에 감시합니다.
 */
static void frame_draw(const World *w, Scene *sc, int vw, int vh, int frozen) {
    float aspect = (float)vw / (float)vh;

    /* Bind the offscreen target, if there is one. It returns the aspect of the
       small buffer rather than the window's, and the world must be rendered
       with THAT -- using the window's here stretches everything, because the
       offscreen buffer's proportions need not match exactly once its width has
       been rounded to whole pixels.
       오프스크린 타깃이 있으면 바인딩합니다. 창이 아닌 작은 버퍼의 종횡비를 반환하며,
       월드는 *그 값으로* 렌더링해야 합니다. 여기서 창의 값을 쓰면 전체가 늘어나는데,
       오프스크린 버퍼의 너비가 정수 픽셀로 반올림되고 나면 그 비율이 창과 정확히 일치하지
       않을 수 있기 때문입니다. */
    float post_aspect = post_begin();
    if (post_aspect > 0.0f) aspect = post_aspect;
    else                    glViewport(0, 0, vw, vh);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* The clock animated materials run against. Advanced by ::world_step and
       only read here, so a lava floor stops churning when the world stops. */
    rd_time(w->run.world_time);

    /* Vertex snapping, on for the world and off again before the UI. The grid
       is the offscreen buffer, so the quantisation lands on the pixels the
       image is actually rasterised into. post_size reports 0,0 when the pass is
       off, which disables the snap -- the right answer, since with no
       pixelisation there is no grid to snap to.
       정점 스냅입니다. 월드에 대해 켜고 UI 이전에 다시 끕니다. 격자는 오프스크린 버퍼이므로
       양자화가 이미지가 실제로 래스터화되는 픽셀에 놓입니다. 패스가 꺼져 있으면 post_size가
       0,0을 보고하여 스냅이 비활성화되는데, 픽셀화가 없으면 맞출 격자도 없으므로 올바른
       동작입니다. */
    {
        int sw, sh;
        post_size(&sw, &sh);
        rd_snap((float)sw / PSX_SNAP_COARSE, (float)sh / PSX_SNAP_COARSE);
    }

    /* Recoil rides on top of the player's own pitch and springs back. */
    float aim_pitch = w->pitch + w->weapon.recoil;

    /* --- the death collapse ----------------------------------------------
       Applied to the CAMERA rather than to the player's position, so the body
       the simulation knows about never moves. Sinking the real position would
       put the eye inside the floor, where level_trace reports an immediate hit
       at zero range -- the failure mode hooktest's fixture note describes --
       and would leave the player somewhere they could not legally stand if the
       run were ever resumed.
       플레이어 위치가 아니라 *카메라*에 적용하므로 시뮬레이션이 아는 몸은 움직이지
       않습니다. 실제 위치를 내리면 눈이 바닥 안으로 들어가고, 그곳에서 level_trace는
       거리 0에서 즉시 충돌을 보고합니다. hooktest의 픽스처 주석이 설명하는 실패 양상이며,
       플레이가 재개된다면 플레이어가 합법적으로 설 수 없는 곳에 남게 됩니다. */
    v3    eye_pos   = w->player.pos;
    float cam_pitch = aim_pitch;
    float cam_roll  = 0.0f;
    if (w->run.dead) {
        float k = w->run.death_time / DEATH_ANIM_TIME;
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
    mat4 view = mat4_fps_view_roll(eye_pos, w->yaw, cam_pitch, cam_roll);
    mat4 vp   = mat4_mul(proj, view);

    /* The basis the billboards face along. Derived from the SAME pitch and
       roll the view matrix uses, or the sprites would keep facing where the
       camera used to be and turn edge-on as it rolls -- monsters vanishing
       as the player dies is exactly the wrong thing to lose sight of.
       빌보드가 향하는 기저입니다. 뷰 행렬이 사용하는 것과 *동일한* 피치와 롤에서
       유도합니다. 그렇지 않으면 스프라이트가 카메라가 있던 곳을 계속 향하다가 롤링에
       따라 옆으로 서 버립니다. 플레이어가 죽는 순간 몬스터가 사라지는 것은 시야에서
       놓쳐서는 안 될 바로 그것입니다. */
    float cy = cosf(w->yaw), sy = sinf(w->yaw);
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
    scene_draw_level(sc, vp, eye_pos, &w->level);

    /* --- monsters, pickups and projectiles ---
       Sprite passes, each building its billboards on the CPU and uploading
       once. They stay on the world side of the pass boundary so they are
       pixelised and dithered along with everything else -- see scene.h. */
    scene_draw_enemies(sc, vp, eye_pos, cam_right);
    scene_draw_pickups(sc, vp, eye_pos, cam_right);
    scene_draw_shots  (sc, vp, cam_right, cam_up);
    scene_draw_proj   (sc, vp, cam_right, cam_up);
    fx_draw(vp, cam_right, cam_up);

    /* --- bullet holes and tracers, still in world space --- */
    glDisable(GL_CULL_FACE);
    wp_draw_world(&w->weapon, vp, eye_pos, cam_right, cam_up);

    /* --- the gun, over a cleared depth buffer ---
       Dropped the moment the player dies. The view model is drawn in its own
       space with its own projection, so it does not roll or fall with the
       camera -- it would hang perfectly level in the middle of the screen while
       the world tipped over behind it, which is a far louder tell than simply
       not being there. A dead hand lets go.
       플레이어가 죽는 순간 사라집니다. 뷰 모델은 자체 공간에서 자체 투영으로 그려지므로
       카메라와 함께 기울거나 떨어지지 않습니다. 뒤에서 월드가 넘어가는 동안 화면 한가운데에
       완벽히 수평으로 떠 있게 되는데, 그것은 그냥 없는 것보다 훨씬 요란한 표시입니다.
       죽은 손은 놓습니다. */
    glEnable(GL_CULL_FACE);
    if (!w->run.dead) wp_draw_view(&w->weapon, aspect);

    /* --- resolve the offscreen buffer to the window ---------------------
       A no-op when the effect is off or unavailable, in which case the frame
       was drawn straight to the window and this changes nothing. See the note
       on this function for what the boundary means.
       효과가 꺼져 있거나 사용할 수 없으면 아무 동작도 하지 않으며, 그 경우 프레임은 창에
       직접 그려졌으므로 달라지는 것이 없습니다. 이 경계의 의미는 이 함수의 참고 사항을
       확인하십시오. */
    post_end(vw, vh);

    /* --- crosshair ---
       Hidden while frozen, for the same reason it is hidden on the win screen:
       a crosshair implies you can still act.
       정지 중에는 숨깁니다. 승리 화면에서 숨기는 것과 같은 이유이며, 조준점은 아직
       행동할 수 있음을 암시하기 때문입니다. */
    if (!frozen) {
        glDisable(GL_CULL_FACE);
        /* post_end leaves depth testing off and does not restore culling, so
           the UI passes below set up their own state -- which they already did,
           because the HUD never wanted depth anyway.
           post_end는 깊이 테스트를 끈 상태로 두고 컬링도 복원하지 않으므로, 아래 UI
           패스들이 자체적으로 상태를 설정합니다. HUD는 원래 깊이 테스트를 필요로 하지
           않았으므로 이미 그렇게 하고 있었습니다. */
        /* The range test traces the level, so it happens here rather than
           inside the draw call -- see the note on wp_draw_hud.
           사거리 판정은 레벨을 탐색하므로 그리기 호출 내부가 아니라 이곳에서
           수행합니다. wp_draw_hud의 참고 사항을 확인하십시오. */
        int hook_ready = wp_hook_in_range(&w->weapon, &w->level,
                                          w->player.pos, w->yaw, w->pitch);
        wp_draw_hud(&w->weapon, aspect, hook_ready);
        glEnable(GL_CULL_FACE);
    }

    /* The UI is drawn at native resolution and must not wobble: text snapped to
       a coarse grid loses whole glyph rows.
       UI는 원해상도로 그려지며 흔들려서는 안 됩니다. 성긴 격자에 스냅된 텍스트는
       글리프의 행 전체를 잃습니다. */
    rd_snap(0.0f, 0.0f);

    /* The HUD is skipped on the title screen: health and ammo belong to a run,
       and showing them before one has started says the game is already in
       progress.

       Otherwise it draws unconditionally and the end screens go OVER it, dim
       included. Making them exclusive would be the obvious simplification and
       it is wrong: the readouts belong to the frozen frame underneath, and the
       dimming is what pushes them back rather than removing them.

       타이틀 화면에서는 HUD를 건너뜁니다. 체력과 탄약은 진행 중인 플레이에 속하며, 시작하기도
       전에 표시하면 게임이 이미 진행 중이라고 말하는 셈입니다.

       그 외에는 조건 없이 그리고, 종료 화면들이 흐리게 처리된 부분까지 포함해 그 *위에*
       놓입니다. 둘을 배타적으로 만드는 것이 자명한 단순화처럼 보이지만 틀렸습니다. 그 수치들은
       아래에 정지된 프레임에 속하며, 흐리게 처리하는 것은 그것들을 제거하는 것이 아니라 뒤로
       밀어내는 일입니다. */
    if (!w->run.title) scene_draw_hud(sc, vw, vh, &w->player, &w->weapon);

    /* The three end/start screens are mutually exclusive by construction:
       `title` is cleared before a run can begin, and a run that has been won
       cannot also have been lost.
       세 개의 시작·종료 화면은 구조적으로 상호 배타적입니다. `title`은 플레이가
       시작되기 전에 해제되며, 승리한 플레이가 동시에 패배할 수는 없습니다. */
    if (w->run.title)
        scene_draw_title(sc, vw, vh, w->run.title_time);
    else if (w->run.won)
        scene_draw_win(sc, vw, vh, &w->player, &w->weapon);
    else if (w->run.dead)
        /* The overlay's clock starts when the COLLAPSE ends, not when the
           player dies. Fading a red wash in over the fall would hide the
           animation behind it, and the fall is the part that says what
           happened.
           오버레이의 시계는 플레이어가 죽을 때가 아니라 *쓰러짐이 끝날 때* 시작합니다.
           넘어지는 동안 붉은 막을 덮으면 그 뒤로 애니메이션이 가려지는데, 무슨 일이
           있었는지 말해 주는 것이 바로 그 넘어짐입니다. */
        scene_draw_death(sc, vw, vh, w->run.death_time - DEATH_ANIM_TIME,
                         w->run.death_time > DEATH_INPUT_DELAY);

    /* Last, so it sits over the HUD and the end screens both. A menu opened
       from the win screen has to be readable too, and it is the thing the
       player is currently operating.
       마지막에 그려 HUD와 종료 화면 양쪽 위에 놓입니다. 승리 화면에서 연 메뉴도 읽을
       수 있어야 하며, 그것이 플레이어가 지금 조작하고 있는 대상입니다. */
    scene_draw_menu(sc, vw, vh);
}

/**
 * @brief Writes the window title: the frame rate, and what has gone wrong.
 *
 * ENGLISH
 * -------
 * @param[in] w   The world, for the debug readout.
 * @param[in] fps Frames per second over the last measuring window.
 *
 * 한국어
 * ------
 * @brief 창 제목을 씁니다. 프레임률, 그리고 무엇이 잘못되었는지입니다.
 * @param[in] w   디버그 표시용 월드.
 * @param[in] fps 마지막 측정 구간의 초당 프레임 수.
 */
static void set_title(const World *w, int fps) {
#ifdef DEBUG_HUD
    /* Capacity overflows, if any. Prefixed with "!" and placed at the FRONT of
       the title: a truncation is the one thing here that means something is
       actually wrong, and the tail of a long title bar is the first thing
       Windows elides.
       용량 초과가 있으면 표시합니다. "!"를 붙여 제목 맨 앞에 배치합니다. 이곳의 정보 중
       실제로 무언가 잘못되었음을 뜻하는 것은 절단뿐이며, 긴 제목 표시줄에서 Windows가 가장
       먼저 생략하는 부분이 뒤쪽이기 때문입니다. */
    char over[96];
    int  any_over = diag_summary(over, sizeof(over));

    /* wsprintfA has no %f, so angles are printed in millidegrees and
       positions in centimetres -- integers all the way down. */
    char title[320];
    wsprintfA(title,
        "%s%s%sSFPS %dfps | %s | assets: %s | pos %d,%d,%d cm | "
        "yaw %d pitch %d recoil %d mdeg",
        any_over ? "! DROPPED " : "", any_over ? over : "",
        any_over ? " | " : "",
        fps,
        /* Which level the run is actually in. A transition that silently failed
           -- a `next` naming a map that does not exist -- otherwise looks
           exactly like a level with no exit.
           플레이가 실제로 있는 레벨입니다. 조용히 실패한 전환(존재하지 않는 맵을 가리키는
           `next`)은 그렇지 않으면 출구가 없는 레벨과 똑같이 보입니다. */
        w->cur_level,
        /* Says out loud where the models came from. Editing a file the running
           build cannot see is otherwise invisible: the game just keeps drawing
           the old shape. */
        data_from_file(DATA_MODELS) ? "LIVE assets\\" : "baked (rebuild to update)",
        (int)(w->player.pos.x * 100), (int)(w->player.pos.y * 100),
        (int)(w->player.pos.z * 100),
        (int)(w->yaw * 57295.8f), (int)(w->pitch * 57295.8f),
        (int)(w->weapon.recoil * 57295.8f));
#else
    (void)w;
    char title[64];
    wsprintfA(title, "SFPS   %d fps", fps);
#endif
    SetWindowTextA(g_wnd, title);
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
       an exact integer scale in both axes. The sizing rule itself lives in
       apply_pixel_preset, because the settings menu resizes the target at
       runtime and a second copy of that reasoning would drift from the first.
       menu_init runs before it so the preset it reads is a real one.

       Failure is not fatal -- post_begin/post_end become no-ops and the game
       renders straight to the window. Refusing to start over a cosmetic pass
       would be the wrong trade.

       오프스크린 타깃입니다. 창 크기로 다시 확대할 때 양쪽 축 모두 정확한 정수 배율이
       되도록 크기를 정합니다. 크기 결정 규칙 자체는 apply_pixel_preset에 있습니다. 설정
       메뉴가 런타임에 타깃 크기를 바꾸는데, 그 논리의 사본이 두 개면 서로 어긋나게 되기
       때문입니다. 읽어 오는 프리셋이 실제 값이 되도록 menu_init을 먼저 실행합니다.

       실패해도 치명적이지 않습니다. post_begin/post_end가 아무 동작도 하지 않게 되고
       게임은 창에 직접 렌더링합니다. 순전히 미관을 위한 패스 때문에 실행을 거부하는
       것은 잘못된 선택입니다. */
    menu_init(0);
    apply_pixel_preset(menu_settings()->pixel);

    audio_init();   /* silent, not fatal, if there is no output device */

    /* --- everything below is generated, nothing is loaded --- */
    font_init();

    /* The draw passes and everything they own: the level's geometry, the
       per-frame billboard buffers, and the sprite atlases. One struct so that
       every mb_init has an mb_free -- see scene.h. */
    Scene scene;
    scene_init(&scene);

    /* The world, on the title screen, in WORLD_START_LEVEL but not yet loaded. */
    world_init(&g_world);

    /* Before the first world_load_level, which calls wp_hook_release on the
       weapon: wp_init is what zeroes it and records the level shots trace
       against. It is not part of world_init because it uploads the view model's
       texture and so needs the GL context that exists here and nowhere else --
       which is also what lets tools\steptest.c drive a World with no context at
       all.
       첫 world_load_level보다 먼저 호출합니다. 그것이 무기에 wp_hook_release를 호출하는데,
       무기를 0으로 초기화하고 사격 판정 대상 레벨을 기록하는 것이 wp_init이기 때문입니다.
       world_init의 일부가 아닌 이유는 뷰 모델의 텍스처를 업로드하므로 이곳에만 존재하는 GL
       컨텍스트를 필요로 하기 때문입니다. 그것이 또한 tools\steptest.c가 컨텍스트 없이
       World를 구동할 수 있게 하는 이유입니다. */
    wp_init(&g_world.weapon, &g_world.level);

    /* carry_state=0: a fresh start begins at full health with a starting belt,
       which is what wp_init just set. The geometry this makes stale is uploaded
       by the world_take_geometry in the loop below, on the first frame. */
    world_load_level(&g_world, g_world.cur_level, 0);

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
            g_world.run.restart_wanted = 1;
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
           Three ways in -- the menu's RESTART row, a key on the death screen,
           and a click on it -- and all three set the same flag, so there is one
           implementation. What is left here is the part that belongs to the
           window: the menu has to close, the next mouse delta has to be thrown
           away, and the cursor has to be re-decided AFTER `dead` has changed.
           진입로는 셋(메뉴의 RESTART 행, 사망 화면에서의 키, 그곳에서의 클릭)이며 셋 모두
           같은 플래그를 설정하므로 구현은 하나입니다. 이곳에 남은 것은 창에 속한 부분입니다.
           메뉴를 닫고, 다음 마우스 변화량을 버리고, `dead`가 바뀐 *뒤에* 커서를 다시
           결정해야 합니다. */
        if (g_world.run.restart_wanted) {
            world_restart(&g_world);
            menu_close();
            g_warp_mouse = 1;
            cursor_update();
        }

        apply_live_settings();

        RECT cr; GetClientRect(g_wnd, &cr);
        int vw = cr.right - cr.left, vh = cr.bottom - cr.top;
        if (vh < 1) vh = 1;

        /* --- one frame of world --------------------------------------------
           Everything the game does to itself, in one call, in an order a test
           can reach. See world.c. The window aspect is what the muzzle solve
           has always used; see the note on ::world_step.
           게임이 자기 자신에게 하는 모든 일이 한 번의 호출 안에, 테스트가 도달할 수 있는
           순서로 있습니다. world.c를 참조하십시오. 총구 계산이 언제나 사용해 온 값은 창의
           종횡비입니다. ::world_step의 참고 사항을 확인하십시오. */
        Input in;
        input_gather(&in, &g_world);
        int frozen = world_step(&g_world, &in, (float)vw / (float)vh, dt);

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
               level_tex holding deleted texture names. The rebuild below
               resolves them, so it has to follow wp_reload_texture.

               carry_state=0: an edit to the map is an authoring action, so the
               player is respawned at the (possibly moved) start with a full
               belt rather than left standing wherever the old geometry put them
               -- which could now be inside a wall.

               플러시 이후여야 합니다. tex_flush는 캐시된 모든 재질을 삭제하므로, 레벨의
               재질을 먼저 해석하면 level_tex가 삭제된 텍스처 이름을 보유하게 됩니다. 아래의
               재생성이 재질을 해석하므로 wp_reload_texture 다음에 와야 합니다.

               맵 편집은 제작 행위이므로 플레이어를 (옮겨졌을 수 있는) 시작 지점에 탄약을
               채워 다시 스폰합니다. 예전 지오메트리 기준의 위치에 그대로 두면 이제 벽 속일
               수도 있습니다. */
            world_load_level(&g_world, g_world.cur_level, 0);
        }

        /* --- the drawn geometry catches up with the sectors -----------------
           One site, whatever moved them: a level that was just loaded, a door
           mid-swing, or the hot reload above. Three call sites each remembering
           to rebuild for their own reason is how one of them forgets -- and a
           frame that both opened a door and crossed an exit used to rebuild
           twice. See ::World::geometry_dirty.
           무엇이 섹터를 움직였든 한 곳입니다. 방금 로드된 레벨, 열리는 중인 문, 또는 위의 핫
           리로드입니다. 세 개의 호출 지점이 각자의 이유로 재생성을 기억하는 것은 그중 하나가
           잊게 되는 방식이며, 문을 열면서 출구를 넘은 프레임은 이전에 두 번 재생성했습니다.
           ::World::geometry_dirty를 참조하십시오. */
        int dynamic;
        if (world_take_geometry(&g_world, &dynamic))
            scene_build_level(&scene, &g_world.level, dynamic);

        frame_draw(&g_world, &scene, vw, vh, frozen);

        SwapBuffers(dc);

        fps_accum += dt; fps_frames++;
        if (fps_accum >= 0.5) {
            set_title(&g_world, (int)(fps_frames / fps_accum));
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
