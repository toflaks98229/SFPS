/**
 * @file main.c
 * @brief SFPS -- a 3D FPS that fits on a 1.44MB floppy. Window, input and drawing.
 *
 * ENGLISH
 * -------
 * This file owns only what cannot be tested headlessly: the Win32 window, the
 * GL context, raw input, and the one graphics setting that is genuinely a
 * window operation -- switching between windowed and borderless.
 *
 * It used to say "the graphics settings that resize a render target", and that
 * had stopped being a description of this file and become a claim on work it
 * merely happened to hold. Sizing an offscreen buffer from a client area is
 * arithmetic; reading the settings block is a menu question; neither needs a
 * window and both are gfx.c's. What is left here is the HWND itself.
 *
 * The per-frame UPDATE used to be here too, in the body of `WinMain`, and so
 * did the per-frame DRAW. Neither is any more -- see world.c and scene.c. Both
 * orders were declared load-bearing by this very comment and nothing could
 * check either, because the only way to run them was to open a window and play.
 * tools\steptest.c now runs the update with no window at all, and the draw
 * order is reachable by anything holding a GL context.
 *
 * The line between what left and what stayed is drawn by what needs hardware.
 * The simulation hands this file a ::World and a `frozen` flag; this file hands
 * both straight back to ::scene_frame, and turns Win32 messages into an
 * ::Input. Nothing here decides what a frame MEANS, and nothing here decides
 * what one LOOKS like either -- what is left is a window, a clock and a
 * message pump.
 *
 * 한국어
 * ------
 * 이 파일은 헤드리스로 테스트할 수 없는 요소만을 담당합니다. Win32 창, GL 컨텍스트, 원시
 * 입력, 그리고 진짜로 창에 대한 작업인 그래픽 설정 하나입니다. 창 모드와 테두리 없는 전체
 * 화면 사이의 전환입니다.
 *
 * 이전에는 "렌더 타깃 크기를 바꾸는 그래픽 설정"이라고 적혀 있었고, 그것은 이 파일에 대한
 * 서술이기를 그만두고 단지 이 파일이 쥐고 있게 된 작업에 대한 주장이 되어 있었습니다.
 * 클라이언트 영역으로 오프스크린 버퍼 크기를 정하는 것은 산술이고, 설정 블록을 읽는 것은
 * 메뉴에 대한 질문이며, 어느 쪽도 창을 필요로 하지 않고 둘 다 gfx.c의 것입니다. 이곳에 남은
 * 것은 HWND 자체입니다.
 *
 * 프레임별 *갱신*도 이전에는 `WinMain`의 본문 안에 있었고, 프레임별 *그리기*도 그러했습니다.
 * 이제 둘 다 없습니다. world.c와 scene.c를 참조하십시오. 두 순서 모두 바로 이 주석이
 * 구조적으로 중요하다고 선언했지만 무엇도 그중 어느 것도 검사할 수 없었습니다. 실행하는
 * 유일한 방법이 창을 열고 플레이하는 것이었기 때문입니다. 이제 tools\steptest.c가 창 없이
 * 갱신을 실행하며, 그리기 순서에는 GL 컨텍스트를 가진 무엇이든 도달할 수 있습니다.
 *
 * 떠난 것과 남은 것 사이의 경계는 "하드웨어가 필요한가"로 그어집니다. 시뮬레이션이 이 파일에
 * ::World와 `frozen` 플래그를 건네면, 이 파일은 그 둘을 그대로 ::scene_frame에 다시 건네고,
 * Win32 메시지를 ::Input으로 바꿉니다. 이곳의 어떤 것도 한 프레임이 무엇을 *뜻하는지*
 * 결정하지 않으며, 한 프레임이 어떻게 *보이는지*도 결정하지 않습니다. 남은 것은 창과 시계와
 * 메시지 펌프입니다.
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
/* stdlib.h and txt.h were here, and both left with the demo plumbing. malloc
   and free served exactly two callers -- the recording read at startup and the
   one written at exit -- and txt_is parsed exactly two command-line flags. All
   four are demo_file.c's now, and an include kept past the last call site is
   how a header graph stops describing what actually depends on what.
   stdlib.h와 txt.h가 이곳에 있었고 둘 다 데모 배관을 따라 나갔습니다. malloc과 free는 정확히
   두 호출자를 위한 것이었고(시작 시 읽는 기록과 종료 시 쓰는 기록) txt_is는 정확히 두 개의
   명령줄 플래그를 파싱했습니다. 넷 모두 이제 demo_file.c의 것이며, 마지막 호출 지점보다 오래
   남은 include는 헤더 그래프가 실제 의존 관계를 설명하지 못하게 만드는 원인입니다. */
#include "world.h"    /* World and Input: the simulation this file drives */
#include "wgl.h"     /* gl_bootstrap/gl_make_context: this file owns the context */
#include "render.h"   /* rd_init -- the one call that needs a context and no frame */
#include "scene.h"    /* scene_frame: the draw order, and everything it owns */
/* WP_TYPES, and nothing else. This used to bring in wp_stats and the hook's
   release/rearm, because ::wnd_proc switched weapons and dropped the rope
   itself; both are ::world_step's now and what is left is the range check that
   says which number keys are weapon keys. hook.h went with them -- it was
   included for HOOK_* states this file no longer names.
   WP_TYPES 하나뿐입니다. 이전에는 wp_stats와 훅의 해제·재장전을 가져왔습니다. ::wnd_proc이
   직접 무기를 바꾸고 로프를 놓았기 때문입니다. 둘 다 이제 ::world_step의 것이고, 남은 것은
   어느 숫자 키가 무기 키인지 말하는 범위 검사입니다. hook.h도 함께 나갔습니다. 이 파일이 더
   이상 언급하지 않는 HOOK_* 상태 때문에 포함되어 있었습니다. */
#include "weapon.h"
#include "weaponview.h"   /* wpview_set_model / wpview_reload_texture: hot reload */
#include "post.h"
#include "menu.h"     /* the ESC menu: pause, settings, restart, quit */
#include "font.h"
#include "audio.h"
#include "music.h"   /* the background music, and which piece plays */
#include "data.h"
#include "decal.h"    /* decal_init/decal_free -- the pool's lifetime, not its draw */
#include "fx.h"       /* fx_reload -- what a hot reload does to live particles */
#include "demo.h"    /* demo_take / demo_put: the two calls that are in a frame */
#include "demo_file.h" /* DemoFile, and the three that are not: the flag and the bytes */
#include "gfx.h"     /* the render target a pixel preset asks for, and the live settings */
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

/**
 * @brief How many ShowCursor steps to take before giving up.
 *
 * ENGLISH: ShowCursor is a counter rather than a flag, so driving it to a state
 * is a loop. The bound is not a guess at how far it can drift -- the loop stops
 * as soon as ShowCursor reports the state it was asked for, and this only stops
 * a loop that can never get there from being infinite.
 *
 * 한국어: ShowCursor는 플래그가 아니라 카운터이므로 원하는 상태로 몰아가려면 루프가 필요합니다.
 * 이 상한은 카운터가 얼마나 어긋날 수 있는지에 대한 추측이 아닙니다. ShowCursor가 요청받은
 * 상태를 보고하는 즉시 루프가 끝나며, 이 값은 결코 도달할 수 없는 루프가 무한해지는 것만
 * 막습니다.
 */
#define CURSOR_DRIVE_MAX 16

/* WORLD_FOV and MOUSE_SENS used to be here. The first moved to world.h because
   the simulation and the renderer both need the same value; the second moved
   into world.c entirely, because pixels-to-radians is what the world does with
   a delta and nothing here has an opinion about it.
   WORLD_FOV와 MOUSE_SENS가 이곳에 있었습니다. 앞의 것은 시뮬레이션과 렌더러가 같은 값을
   필요로 하므로 world.h로 옮겼습니다. 뒤의 것은 world.c로 완전히 옮겼습니다. 픽셀을
   라디안으로 바꾸는 것은 월드가 변화량에 대해 하는 일이고, 이곳은 그에 대해 아무 견해도 갖지
   않기 때문입니다. */

/* PSX_SNAP_COARSE and DEATH_INPUT_DELAY used to be here as well, and followed
   the drawing out. The first is read only by ::scene_frame now, and a window
   has no opinion about how coarse a snap grid is. The second went to player.h
   beside the collapse it delays, and this file no longer reads it at all:
   ::wnd_proc used to apply the grace period before setting the restart flag,
   which is a rule about the run and is ::step_confirm's now. A window has no
   opinion about how long a death screen has to be looked at either.
   PSX_SNAP_COARSE와 DEATH_INPUT_DELAY도 이곳에 있었고, 그리기를 따라 나갔습니다. 앞의
   것은 이제 ::scene_frame만 읽으며, 창은 스냅 격자가 얼마나 성긴지에 대해 아무 견해도 갖지
   않습니다. 뒤의 것은 그것이 지연시키는 쓰러짐 연출 곁인 player.h로 갔으며, 이 파일은 이제
   그것을 전혀 읽지 않습니다. ::wnd_proc이 재시작 플래그를 세우기 전에 유예 시간을
   적용했었는데, 그것은 플레이에 대한 규칙이며 이제 ::step_confirm의 것입니다. 창은 사망
   화면을 얼마나 오래 봐야 하는지에 대해서도 아무 견해를 갖지 않습니다. */

/* ------------------------------------------------------------------- state */

/* --- Input state / 입력 상태 --- */

/**
 * @brief How many virtual key codes there are.
 *
 * ENGLISH: A Win32 virtual key code is one byte, so 256 is the whole space and
 * not a guess at how many keys anyone uses. It appeared as a bare 256 in the
 * array and in two clearing loops, and as a bare 0xff in the two places a
 * WPARAM was masked down to an index -- four literals for one fact, and the
 * mask silently has to be one less than the size.
 *
 * 한국어: Win32 가상 키 코드는 1바이트이므로 256은 공간 전체이지 누군가 몇 개의 키를 쓸지에
 * 대한 추측이 아닙니다. 배열과 두 개의 초기화 루프에 맨 256으로, WPARAM을 인덱스로 좁히는 두
 * 곳에 맨 0xff로 나타났습니다. 하나의 사실에 네 개의 리터럴이며, 마스크는 크기보다 정확히 1
 * 작아야 한다는 조건이 말없이 걸려 있습니다.
 */
#define VK_TABLE_SIZE 256

/** @brief Masks a WPARAM into ::VK_TABLE_SIZE. Derived, never written out. / WPARAM을 ::VK_TABLE_SIZE 안으로 좁힙니다. 직접 쓰지 않고 유도합니다. */
#define VK_TABLE_MASK (VK_TABLE_SIZE - 1)

_Static_assert((VK_TABLE_SIZE & VK_TABLE_MASK) == 0,
               "VK_TABLE_MASK is only the right mask for a power of two");

/** @brief Key-down flags indexed by virtual key code. / 가상 키 코드로 인덱싱되는 키 눌림 플래그. */
static int  g_keys[VK_TABLE_SIZE];

/**
 * @struct EdgeLatch
 * @brief Edges the window has seen and the frame has not consumed yet.
 *
 * ENGLISH
 * -------
 * These used to be written straight into ::g_world from ::wnd_proc, which is
 * how weapon selection, the hook's release and the death screen's restart rule
 * came to live inside a Win32 message handler -- unreachable from
 * tools\steptest.c, and contradicting this file's own claim that nothing here
 * decides what a frame means.
 *
 * A latch keeps what was true about the edge: the message sets it once and
 * ::input_gather clears it once, so one press is still one step. What it stops
 * being is a decision. ::wnd_proc still decides which KEY means which edge --
 * that is a keyboard question and it belongs here -- and ::world_step decides
 * what the edge DOES.
 *
 * @note One struct rather than three globals, for the reason ::RunState is one:
 *       draining it is one assignment, and a fourth edge is a field here rather
 *       than a line that has to be added to two places and will be added to
 *       one.
 *
 * 한국어
 * ------
 * @brief 창이 관측했지만 프레임이 아직 소비하지 않은 엣지들.
 *
 * 이들은 이전에 ::wnd_proc에서 ::g_world로 곧장 기록되었고, 그래서 무기 선택과 훅 해제와
 * 사망 화면의 재시작 규칙이 Win32 메시지 핸들러 안에 살게 되었습니다. tools\steptest.c에서
 * 도달할 수 없었고, 이곳의 무엇도 한 프레임이 무엇을 뜻하는지 결정하지 않는다는 이 파일
 * 자신의 주장과 모순되었습니다.
 *
 * 래치는 엣지에 대해 참이던 것을 지킵니다. 메시지가 한 번 세우고 ::input_gather가 한 번
 * 지우므로, 한 번 누름은 여전히 한 단계입니다. 더 이상 아닌 것은 *판단*입니다. ::wnd_proc은
 * 여전히 어느 *키*가 어느 엣지인지 결정하며(그것은 키보드에 관한 질문이고 이곳에 속합니다),
 * ::world_step이 그 엣지가 무엇을 *하는지* 결정합니다.
 *
 * @note 전역 셋이 아니라 구조체 하나인 이유는 ::RunState가 하나인 이유와 같습니다. 비우는 것이
 *       대입 한 번이고, 네 번째 엣지는 두 곳에 추가해야 하며 그중 한 곳에만 추가될 줄이
 *       아니라 이곳의 필드 하나가 됩니다.
 */
typedef struct {
    int confirm;   /**< A key or click answered the screen that is up. / 키나 클릭이 떠 있는 화면에 응답했습니다. */
    int weapon;    /**< One-based weapon request; 0 is none. See ::Input::want_weapon. / 1부터 시작하는 무기 요청. 0이면 없음. */
    int let_go;    /**< Focus was lost, or the menu opened. / 포커스를 잃었거나 메뉴가 열렸습니다. */
} EdgeLatch;

static EdgeLatch g_edge;
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

/* --- recording and replaying / 기록과 재생 --- */

/**
 * @brief The recording, where playback has got to, and the file it came from.
 *
 * ENGLISH
 * -------
 * A file-scope global for the reason ::g_world is one: it is 144KB, and the
 * frame loop below drives it every frame.
 *
 * What USED to be here as well was the path buffer beside it, the text
 * capacity those two calls needed, and the four functions that turned a command
 * line into one and a recording into bytes. All of that is demo_file.c now --
 * the rules were always demo.c's and the file was always the platform's, and
 * this file kept the second half only because there was nowhere else to put it.
 * There is now, and it turned out not to need a window at all.
 *
 * 한국어
 * ------
 * @brief 기록, 재생 진행 위치, 그리고 그것이 온 파일.
 *
 * 파일 스코프 전역인 이유는 ::g_world가 그런 이유와 같습니다. 144KB이며 아래의 프레임 루프가
 * 매 프레임 그것을 구동합니다.
 *
 * 이전에 이곳에 *함께* 있던 것은 그 곁의 경로 버퍼, 두 호출이 필요로 하던 텍스트 용량, 그리고
 * 명령줄을 기록으로, 기록을 바이트로 바꾸던 함수 넷입니다. 그 전부가 이제 demo_file.c입니다.
 * 규칙은 언제나 demo.c의 것이었고 파일은 언제나 플랫폼의 것이었으며, 이 파일이 두 번째 절반을
 * 쥐고 있던 이유는 달리 둘 곳이 없었기 때문입니다. 이제 있으며, 정작 창은 전혀 필요하지
 * 않았던 것으로 드러났습니다.
 */
static DemoFile g_demo;

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
 * @note Not copied or moved, but no longer because it CANNOT be: the weapon
 *       used to hold the address of `g_world.level` and does not any more. It
 *       simply has no reason to be anywhere but here. See ::World.
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
 * @note 복사하거나 옮기지 않지만, 더 이상 그럴 *수 없어서*가 아닙니다. 무기가
 *       `g_world.level`의 주소를 쥐고 있었고 이제 그러지 않습니다. 그저 이곳 외의 어디에
 *       있을 이유가 없을 뿐입니다. ::World를 참조하십시오.
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
    for (int i = 0; i < CURSOR_DRIVE_MAX; i++) {
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

/* ------------------------------------------------------- message handlers */

/**
 * @brief Lets go of every key the player was holding.
 *
 * ENGLISH: Two things need this and they are not near each other -- losing
 * focus, and opening the menu. Both are "the player stopped touching the
 * keyboard and does not know it", and a second copy of the loop is how one of
 * them ends up clearing a different range.
 *
 * 한국어: 두 곳이 이것을 필요로 하며 서로 가깝지 않습니다. 포커스를 잃는 것과 메뉴를 여는
 * 것입니다. 둘 다 "플레이어가 키보드에서 손을 뗐는데 본인은 모른다"이며, 루프의 사본이 두 개면
 * 그중 하나가 다른 범위를 지우게 됩니다.
 */
static void keys_release_all(void) {
    for (int i = 0; i < VK_TABLE_SIZE; i++) g_keys[i] = 0;
}

/**
 * @brief ESC: opens the pause menu, or closes it.
 *
 * ENGLISH
 * -------
 * ESC no longer quits. One mistaken keypress used to end a run outright with no
 * confirmation, which is the behaviour this menu exists to replace; leaving is
 * now a row the player picks.
 *
 * @note Opening lets go of everything the player was holding. Returning to a
 *       game still walking forward, still firing, or still attached to a hook is
 *       the same class of surprise losing focus guards against.
 * @note Closing discards the next mouse delta the way taking focus does: the
 *       cursor was parked wherever the player left it.
 *
 * 한국어
 * ------
 * @brief ESC. 일시정지 메뉴를 열거나 닫습니다.
 *
 * ESC는 더 이상 종료하지 않습니다. 예전에는 잘못 누른 키 하나가 확인 없이 진행 중인 플레이를
 * 그대로 끝냈으며, 그것이 이 메뉴가 대체하려는 동작입니다. 이제 나가는 것은 플레이어가 직접
 * 고르는 행입니다.
 *
 * @note 열 때는 플레이어가 누르고 있던 것을 전부 놓습니다. 계속 전진하거나 사격 중이거나 훅에
 *       걸린 채로 게임에 돌아오는 것은, 포커스를 잃는 경우가 방지하는 것과 같은 종류의
 *       놀라움입니다.
 * @note 닫을 때는 포커스를 얻을 때와 마찬가지로 다음 마우스 변화량을 버립니다. 커서가
 *       플레이어가 둔 자리에 그대로 있기 때문입니다.
 */
static void menu_toggle_from_escape(void) {
    menu_escape();

    if (menu_is_open()) {
        g_mouse_down  = 0;
        g_hook_down   = 0;
        g_edge.let_go = 1;   /* the rope, which does not clear itself */
        keys_release_all();
    } else {
        g_warp_mouse = 1;
    }
    cursor_update();
}

/**
 * @brief Is a press right now an answer to the screen in front of the player?
 *
 * ENGLISH: The title and death screens both take a press as an acknowledgement,
 * and from here they are one event -- what it MEANS is ::step_confirm's, which
 * is why this only asks whether one of them is up. A menu over either takes the
 * press instead.
 *
 * 한국어: 타이틀 화면과 사망 화면 모두 누름을 응답으로 받아들이며, 이곳에서 보면 둘은 하나의
 * 사건입니다. 그것이 무엇을 *뜻하는지*는 ::step_confirm의 것이고, 그래서 이 함수는 둘 중
 * 하나가 떠 있는지만 묻습니다. 그 위에 메뉴가 있으면 누름은 메뉴가 가져갑니다.
 */
static int screen_takes_press(void) {
    return (g_world.run.title || g_world.run.dead) && !menu_is_open();
}

/**
 * @brief Drives the open menu with one key.
 *
 * @note Swallows what it does not recognise. While the menu is up the world is
 *       frozen, so there is no second meaning for any key to have.
 *
 * @brief 열려 있는 메뉴를 키 하나로 조작합니다.
 * @note 인식하지 못하는 키는 삼킵니다. 메뉴가 떠 있는 동안 월드는 정지해 있으므로 어떤 키도
 *       다른 의미를 가질 여지가 없습니다.
 */
static void menu_take_key(WPARAM wp) {
    switch (wp) {
    case 'W': case VK_UP:          menu_move(-1);   break;
    case 'S': case VK_DOWN:        menu_move(+1);   break;
    case 'A': case VK_LEFT:        menu_adjust(-1); break;
    case 'D': case VK_RIGHT:       menu_adjust(+1); break;
    case VK_RETURN: case VK_SPACE: menu_activate(); break;
    default: break;
    }
}

/**
 * @brief Latches a weapon request if this key is a weapon key.
 *
 * ENGLISH
 * -------
 * @return Non-zero when the key was a weapon key, whatever came of it.
 *
 * WHICH KEYS ARE WEAPON KEYS, and nothing else. This block used to reach into
 * `g_world.weapon`, cancel a swing and play a sound, which put three rules
 * inside a Win32 message handler where no headless test could see them.
 *
 * @note One-based, so a frame with no request is a zeroed field rather than a
 *       sentinel every fixture has to know about. See ::Input::want_weapon, and
 *       ::step_weapon_pick for what now happens.
 *
 * 한국어
 * ------
 * @brief 이 키가 무기 키라면 무기 요청을 래치합니다.
 * @return 결과가 무엇이든, 그 키가 무기 키였으면 0이 아닌 값.
 *
 * 어느 키가 무기 키인지이며 그 외에는 아무것도 아닙니다. 이 블록은 이전에 `g_world.weapon`에
 * 손을 뻗어 진행 중인 공격을 취소하고 소리를 재생했습니다. 세 개의 규칙을 어떤 헤드리스
 * 테스트도 볼 수 없는 Win32 메시지 핸들러 안에 둔 것입니다.
 *
 * @note 1부터 시작하므로 요청이 없는 프레임은 모든 픽스처가 알아야 하는 특별한 값이 아니라
 *       0으로 초기화된 필드가 됩니다. ::Input::want_weapon을 참조하고, 이제 무슨 일이
 *       일어나는지는 ::step_weapon_pick을 보십시오.
 */
static int key_picks_weapon(WPARAM wp) {
    if (wp < '1' || wp >= '1' + WP_TYPES) return 0;
    g_edge.weapon = (int)(wp - '1') + 1;
    return 1;
}

/**
 * @brief What one key press means, in the order the meanings take precedence.
 *
 * ENGLISH
 * -------
 * @return Non-zero when the press was claimed; zero to record it as held state.
 *
 * THE ORDER IS THE CONTENT, and it is the only thing this function holds. Each
 * line is a claim on the key by one owner, and the first owner to want it gets
 * it: the menu key opens the menu even on the title screen, a screen in front
 * of the player takes anything else, an open menu takes everything, and only a
 * key none of them wanted reaches the world as held state.
 *
 * @note The title and death screens used to be two separate rungs with ESC
 *       excluded from the first. Asking about ESC first is the same chain with
 *       the exception removed, which is what let the two merge -- and it is
 *       what the mouse path had already done.
 *
 * 한국어
 * ------
 * @brief 키 한 번의 의미를, 그 의미들이 우선하는 순서대로.
 * @return 그 누름을 가져갔으면 0이 아닌 값. 유지 상태로 기록해야 하면 0.
 *
 * 순서가 곧 내용이며, 이 함수가 담은 것은 그것뿐입니다. 각 줄은 한 주인이 그 키에 대해 거는
 * 주장이고, 원하는 첫 번째 주인이 가져갑니다. 메뉴 키는 타이틀 화면에서도 메뉴를 열고, 눈앞의
 * 화면이 그 외의 모든 것을 가져가고, 열린 메뉴는 전부를 가져가며, 그중 누구도 원하지 않은
 * 키만이 유지 상태로 월드에 도달합니다.
 *
 * @note 타이틀 화면과 사망 화면은 이전에 두 개의 별도 단이었고 첫 번째에서 ESC가 제외되어
 *       있었습니다. ESC를 먼저 묻는 것은 그 예외를 없앤 같은 사슬이며, 그것이 둘을 합칠 수 있게
 *       한 것입니다. 마우스 경로는 이미 그렇게 하고 있었습니다.
 */
static int on_key_down(WPARAM wp) {
    if (wp == VK_ESCAPE)      { menu_toggle_from_escape(); return 1; }
    if (screen_takes_press()) { g_edge.confirm = 1;        return 1; }
    if (menu_is_open())       { menu_take_key(wp);         return 1; }

    /* F1 toggles the pixelise/dither pass. On the message edge rather than
       polled, so one press is one toggle -- a held key would otherwise flip it
       every frame. Comparing the two looks side by side is most of how the
       effect gets tuned.
       F1은 픽셀화/디더 패스를 전환합니다. 폴링하지 않고 메시지 시점에 처리하므로 한 번 누르면
       한 번 전환됩니다. 그렇지 않으면 키를 누르고 있는 동안 매 프레임 전환됩니다. 두 화면을
       나란히 비교하는 것이 이 효과를 조정하는 주된 방법입니다. */
    if (wp == VK_F1)          { post_set_enabled(!post_enabled()); return 1; }

    return key_picks_weapon(wp);
}

/**
 * @brief Hands a click to the open menu, if one is open.
 *
 * ENGLISH
 * -------
 * @param[in] w     Window, for its client rectangle.
 * @param[in] lp    The message's packed cursor position.
 * @param[in] right Non-zero for the right button.
 * @return Non-zero when the menu took the click.
 *
 * @note Activating RESUME closes the menu, so the cursor has to follow it back
 *       out on this very message -- waiting for the next focus change would
 *       leave a pointer floating over the game.
 *
 * 한국어
 * ------
 * @brief 메뉴가 열려 있으면 클릭을 그것에 건넵니다.
 * @return 메뉴가 클릭을 가져갔으면 0이 아닌 값.
 * @note RESUME를 실행하면 메뉴가 닫히므로 커서도 바로 이 메시지에서 따라 나가야 합니다. 다음
 *       포커스 변경을 기다리면 게임 위에 포인터가 떠 있게 됩니다.
 */
static int menu_takes_click(HWND w, LPARAM lp, int right) {
    if (!menu_is_open()) return 0;

    RECT rc; GetClientRect(w, &rc);
    menu_click((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp),
               rc.right - rc.left, rc.bottom - rc.top, right);

    if (!menu_is_open()) g_warp_mouse = 1;
    cursor_update();
    return 1;
}

/**
 * @brief Gaining or losing the window's keyboard focus.
 *
 * ENGLISH
 * -------
 * @param[in] gained Non-zero when focus arrived, zero when it left.
 *
 * @note Not an unconditional hide on the way in: returning to a PAUSED game has
 *       to keep the pointer, or the player is left operating a menu they cannot
 *       see. ::cursor_update is the one place that decides.
 * @note On the way out the keys are cleared here, because they are this file's,
 *       and the grapple is asked for by the step, because it is not.
 *
 * 한국어
 * ------
 * @brief 창의 키보드 포커스를 얻거나 잃는 것.
 * @param[in] gained 포커스가 도착했으면 0이 아니고, 떠났으면 0.
 *
 * @note 들어올 때 무조건 숨기지 않습니다. *일시정지된* 게임으로 돌아올 때는 포인터를 유지해야
 *       하며, 그렇지 않으면 플레이어가 보이지 않는 메뉴를 조작하게 됩니다. 결정하는 곳은
 *       ::cursor_update 한 군데입니다.
 * @note 나갈 때 키는 이 파일의 것이므로 이곳에서 지우고, 그래플은 이 파일의 것이 아니므로
 *       스텝에게 요청합니다.
 */
static void on_focus_change(int gained) {
    g_focused = gained ? 1 : 0;

    if (gained) {
        g_warp_mouse = 1;
    } else {
        g_mouse_down  = 0;
        g_hook_down   = 0;
        g_edge.let_go = 1;   /* do not come back still roped to a wall */
        keys_release_all();
    }
    cursor_update();
}

/**
 * @brief Window procedure: turns messages into held state and latched edges.
 *
 * ENGLISH
 * -------
 * @param[in] w   Window handle.
 * @param[in] msg Message identifier.
 * @param[in] wp  Message-specific parameter.
 * @param[in] lp  Message-specific parameter.
 * @return 0 for handled messages, otherwise the default window procedure's result.
 *
 * @note Only records state; all gameplay happens in ::world_step. Acting on
 *       input here would run at message rate rather than frame rate.
 * @note What IS handled here is every EDGE -- one press must be one step. A
 *       held key polled once a frame would walk a whole menu in a frame, toggle
 *       the pixelise pass every frame, and skip the death screen on the shot
 *       that caused it. Held state goes into ::g_keys; edges the SIMULATION has
 *       to answer go into ::g_edge, and ::input_gather delivers both.
 * @note ONE CASE, ONE CALL. Each case used to carry its own body, and
 *       WM_KEYDOWN's was a six-rung precedence chain sixty lines long. The
 *       chain still exists -- it is ::on_key_down -- but a `switch` over
 *       messages now reads as a list of which message means what, which is the
 *       only thing a window procedure is.
 *
 * 한국어
 * ------
 * @brief 창 프로시저입니다. 메시지를 유지 상태와 래치된 엣지로 바꿉니다.
 * @return 처리한 메시지에 대해서는 0, 그 외에는 기본 창 프로시저의 반환값.
 *
 * @note 상태를 기록하기만 합니다. 모든 게임 로직은 ::world_step에서 처리됩니다. 여기서 입력에
 *       반응하면 프레임 단위가 아닌 메시지 단위로 실행되기 때문입니다.
 * @note 이곳에서 처리하는 것은 모든 *엣지*입니다. 한 번 누름은 한 단계여야 합니다. 프레임마다
 *       폴링되는 유지 키는 한 프레임에 메뉴 전체를 지나가고, 매 프레임 픽셀화 패스를 전환하며,
 *       사망을 유발한 그 사격으로 사망 화면을 건너뜁니다. 유지 상태는 ::g_keys로, *시뮬레이션*이
 *       답해야 하는 엣지는 ::g_edge로 들어가며 ::input_gather가 둘 다 전달합니다.
 * @note 케이스 하나에 호출 하나. 이전에는 각 케이스가 자기 본문을 지니고 있었고 WM_KEYDOWN의
 *       것은 예순 줄짜리 여섯 단 우선순위 사슬이었습니다. 그 사슬은 여전히 존재하며
 *       ::on_key_down입니다. 다만 메시지에 대한 `switch`가 이제 어느 메시지가 무엇을 뜻하는지의
 *       목록으로 읽히며, 창 프로시저란 그것뿐입니다.
 */
static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        g_running = 0;
        return 0;

    case WM_KEYDOWN:
        if (!on_key_down(wp)) g_keys[wp & VK_TABLE_MASK] = 1;
        return 0;

    case WM_KEYUP:
        g_keys[wp & VK_TABLE_MASK] = 0;
        return 0;

    /* Cursor motion drives the highlight while the menu is up, so the mouse and
       the keyboard are one menu rather than two: whichever the player last
       touched, the highlight shows where a press would land.
       메뉴가 열려 있는 동안 커서 이동이 강조 표시를 조작하므로, 마우스와 키보드가 두 개의
       메뉴가 아닌 하나가 됩니다. 플레이어가 마지막으로 무엇을 만졌든 강조 표시가 지금 누르면
       어디에 닿을지를 보여 줍니다. */
    case WM_MOUSEMOVE:
        if (menu_is_open()) {
            RECT rc; GetClientRect(w, &rc);
            menu_hover((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp),
                       rc.right - rc.left, rc.bottom - rc.top);
        }
        return 0;

    /* The title and death screens take a click as readily as a key: the cursor
       is visible on both, so clicking is what a player reaches for. The RIGHT
       button does not, and that asymmetry is deliberate -- left is the answer
       to a screen, right is the grapple.
       타이틀 화면과 사망 화면은 키만큼이나 클릭도 받아들입니다. 양쪽 모두 커서가 보이므로
       플레이어가 손을 뻗는 것은 클릭입니다. *오른쪽* 버튼은 그렇지 않으며 그 비대칭은
       의도적입니다. 왼쪽은 화면에 대한 답이고 오른쪽은 그래플입니다. */
    case WM_LBUTTONDOWN:
        if (screen_takes_press()) { g_edge.confirm = 1; return 0; }
        if (!menu_takes_click(w, lp, 0)) g_mouse_down = 1;
        return 0;

    case WM_LBUTTONUP:
        /* Handed over unconditionally, without asking whether the menu is open
           or whether it was dragging anything. It is safe either way, and the
           alternative -- deciding here -- is a second copy of a question the
           menu already answers, in the one place that would go stale first.
           메뉴가 열려 있는지, 무언가를 끌고 있었는지 묻지 않고 무조건 넘깁니다. 어느 쪽이든
           안전하며, 대안(이곳에서 판단하는 것)은 메뉴가 이미 답하는 질문의 두 번째 사본을
           가장 먼저 낡을 자리에 두는 일입니다. */
        menu_mouse_up();
        g_mouse_down = 0;
        return 0;

    case WM_RBUTTONDOWN:
        if (!menu_takes_click(w, lp, 1)) g_hook_down = 1;
        return 0;

    case WM_RBUTTONUP:
        g_hook_down = 0;
        return 0;

    case WM_SETFOCUS:
        on_focus_change(1);
        return 0;

    case WM_KILLFOCUS:
        on_focus_change(0);
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

    /* --- the edges, drained ------------------------------------------------
       Taken and cleared in one place, so an edge the window saw is delivered to
       exactly one frame. Clearing by assigning a zeroed struct rather than by
       naming each field is the same reason ::run_reset does: a fourth edge is
       reset by construction instead of by somebody remembering to extend a
       list.

       NOT gated on focus, unlike `fire` above. A window that has just lost
       focus is precisely when ::EdgeLatch::let_go is set, and dropping it
       because the window is no longer focused would drop the one edge that
       exists to be handled at that moment.

       엣지를 한 곳에서 가져오고 지우므로, 창이 관측한 엣지는 정확히 한 프레임에 전달됩니다.
       필드를 하나씩 지우지 않고 0으로 초기화된 구조체를 대입하는 것은 ::run_reset과 같은
       이유입니다. 네 번째 엣지는 누군가 목록을 늘려 주기를 기다리지 않고 구조적으로
       초기화됩니다.

       위의 `fire`와 달리 포커스로 거르지 *않습니다*. 창이 방금 포커스를 잃은 순간이 바로
       ::EdgeLatch::let_go가 설정되는 때이며, 창이 더 이상 포커스를 갖지 않는다는 이유로
       그것을 버리면 정확히 그 순간에 처리되려고 존재하는 엣지를 버리게 됩니다. */
    in->confirm     = g_edge.confirm;
    in->want_weapon = g_edge.weapon;
    in->let_go      = g_edge.let_go;

    EdgeLatch none = {0};
    g_edge = none;
}

/* ------------------------------------------------------------ display mode */

/**
 * @brief The client area, which is the one fact ::gfx_apply_pixel_preset wants.
 *
 * ENGLISH: The sizing rule itself is gfx.c's; what a window is currently
 * measuring is this file's, and it is the whole of what crosses that seam. Two
 * ints rather than an HWND is what keeps gfx.c on the portable side of
 * build.ps1 -Portable -- see gfx.h.
 *
 * @note Not clamped here. ::gfx_apply_pixel_preset clamps what it divides by,
 *       so a minimised window is handled once at the place that would divide by
 *       zero rather than at each call site that might.
 *
 * 한국어: 크기 결정 규칙 자체는 gfx.c의 것이고, 창이 지금 무엇을 재고 있는지는 이 파일의
 * 것이며, 그 이음매를 넘는 것은 그것이 전부입니다. HWND가 아니라 int 둘인 것이 gfx.c를
 * build.ps1 -Portable의 이식 가능한 쪽에 남게 하는 것입니다. gfx.h를 참조하십시오.
 *
 * @note 이곳에서 보정하지 않습니다. ::gfx_apply_pixel_preset이 자신이 나누는 값을 보정하므로,
 *       최소화된 창은 그럴 수도 있는 각 호출 지점이 아니라 실제로 0으로 나누게 될 한 곳에서
 *       한 번 처리됩니다.
 */
static void client_size(int *w, int *h) {
    RECT rc; GetClientRect(g_wnd, &rc);
    *w = rc.right - rc.left;
    *h = rc.bottom - rc.top;
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

/* -------------------------------------------------------------------- draw */

/* frame_draw lived here, and is ::scene_frame in scene.c now. What it did was
   never this file's to decide: it bound a render target, derived a camera,
   ordered nine passes around the post-process boundary and chose which end
   screen goes over which. None of that is a window, and all of it was
   unreachable by anything that was not one -- the note at the top of world.h
   describes the same trap, one layer down, and the same way out.

   What stayed is the part that really is a window: turning Win32 messages into
   an ::Input, sizing the client area, and handing both to the two calls below.
   This file now names a pass order without describing one.

   frame_draw가 이곳에 있었고, 이제 scene.c의 ::scene_frame입니다. 그것이 하던 일은
   애초에 이 파일이 결정할 몫이 아니었습니다. 렌더 타깃을 바인딩하고, 카메라를 유도하고,
   포스트 프로세스 경계를 중심으로 아홉 개의 패스를 배열하고, 어느 종료 화면이 어느 것
   위에 놓이는지 골랐습니다. 그중 어느 것도 창이 아니며, 그 전부가 창이 아닌 무엇에게도
   도달 불가능했습니다. world.h 상단의 주석이 한 계층 아래에서 같은 함정과 같은 탈출구를
   설명합니다.

   남은 것은 정말로 창인 부분입니다. Win32 메시지를 ::Input으로 바꾸고, 클라이언트 영역
   크기를 재고, 그 둘을 아래의 두 호출에 건네는 일입니다. 이 파일은 이제 패스 순서를
   서술하지 않은 채 그것의 이름을 부릅니다. */

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
       먼저 생략하는 부분이 뒤쪽이기 때문입니다.

       The buffer is sized for the frame stamps each entry now carries. A
       truncated diagnostic is a diagnostic that lies by omission, and this is a
       debug-build stack local -- there is no reason to be thrifty with it.
       버퍼 크기는 각 항목이 이제 지니는 프레임 각인에 맞춰 잡았습니다. 잘린 진단은 누락으로
       거짓말하는 진단이며, 이것은 디버그 빌드의 스택 지역 변수이므로 아낄 이유가
       없습니다. */
    char over[256];
    int  any_over = diag_summary(over, sizeof(over));

    /* wsprintfA has no %f, so angles are printed in millidegrees and
       positions in centimetres -- integers all the way down. */
    char title[512];
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

/**
 * @brief Brings up the window, the context and everything drawn with them.
 *
 * ENGLISH
 * -------
 * @param[in]  inst The instance `WinMain` was handed.
 * @param[in]  show The show command `WinMain` was handed.
 * @param[out] dc   The device context the frame loop swaps.
 * @return Non-zero when the game can run; zero when it cannot, having already
 *         said why.
 *
 * THE ORDER IS THE CONTENT. Each step needs the one before it: there is no
 * context without a window, no shader without a context, no offscreen target
 * without a preset to size it from, and no scene without a weapon to hang the
 * gun on. Pulled out of `WinMain` because a startup sequence and a frame loop
 * are two different things that were sharing a function, and the one that grew
 * was neither of them -- it was the demo plumbing between them.
 *
 * 한국어
 * ------
 * @brief 창과 컨텍스트, 그리고 그것으로 그려지는 모든 것을 준비합니다.
 * @return 게임을 실행할 수 있으면 0이 아닌 값. 없으면 이미 이유를 말한 뒤 0.
 *
 * 순서가 곧 내용입니다. 각 단계는 그 앞의 것을 필요로 합니다. 창 없이 컨텍스트가 없고,
 * 컨텍스트 없이 셰이더가 없고, 크기를 정할 프리셋 없이 오프스크린 타깃이 없으며, 총을 걸
 * 무기 없이 장면이 없습니다. `WinMain`에서 꺼낸 이유는 시작 절차와 프레임 루프가 한 함수를
 * 나눠 쓰던 서로 다른 두 가지이고, 정작 커진 것은 그 둘 중 어느 쪽도 아닌 사이에 낀 데모
 * 배관이었기 때문입니다.
 */
static int app_start(HINSTANCE inst, int show, HDC *dc, Scene *scene) {
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
    *dc = GetDC(g_wnd);

    if (!gl_make_context(*dc)) {
        MessageBoxA(0, "Failed to create a 3.3 core context.",
                    "SFPS", MB_ICONERROR);
        return 1;
    }
    gl_set_vsync(1);
    ShowWindow(g_wnd, show);

    rd_init();
    decal_init();   /* pairs with decal_free below */

    /* The offscreen target, sized so that magnifying it back to the window is
       an exact integer scale in both axes. The sizing rule itself lives in
       gfx.c, because the settings menu resizes the target at
       runtime and a second copy of that reasoning would drift from the first.
       menu_init runs before it so the preset it reads is a real one.

       Failure is not fatal -- post_begin/post_end become no-ops and the game
       renders straight to the window. Refusing to start over a cosmetic pass
       would be the wrong trade.

       오프스크린 타깃입니다. 창 크기로 다시 확대할 때 양쪽 축 모두 정확한 정수 배율이
       되도록 크기를 정합니다. 크기 결정 규칙 자체는 gfx.c에 있습니다. 설정
       메뉴가 런타임에 타깃 크기를 바꾸는데, 그 논리의 사본이 두 개면 서로 어긋나게 되기
       때문입니다. 읽어 오는 프리셋이 실제 값이 되도록 menu_init을 먼저 실행합니다.

       실패해도 치명적이지 않습니다. post_begin/post_end가 아무 동작도 하지 않게 되고
       게임은 창에 직접 렌더링합니다. 순전히 미관을 위한 패스 때문에 실행을 거부하는
       것은 잘못된 선택입니다. */
    menu_init(0);
    int cw, ch;
    client_size(&cw, &ch);
    gfx_apply_pixel_preset(menu_settings()->pixel, cw, ch);

    audio_init();   /* silent, not fatal, if there is no output device */

    /* --- everything below is generated, nothing is loaded --- */
    font_init();

    /* The world, on the title screen, in WORLD_START_LEVEL but not yet loaded.
       Brings up its own weapon: world_init calls wp_init now that doing so no
       longer needs a GL context. The paragraph that used to sit below this one
       -- explaining why wp_init had to be called here, after the context and
       before the first load -- described a constraint that no longer exists.
       타이틀 화면 상태의 월드이며, WORLD_START_LEVEL을 대상으로 하되 아직 로드되지
       않았습니다. 자기 무기도 함께 준비합니다. 그렇게 하는 데 더 이상 GL 컨텍스트가 필요하지
       않으므로 world_init이 wp_init을 호출합니다. 이 아래에 있던 문단, 즉 컨텍스트 이후이자
       첫 로드 이전인 이 자리에서 wp_init을 호출해야 하는 이유를 설명하던 문단은 이제
       존재하지 않는 제약을 설명하고 있었습니다. */
    world_init(&g_world);

    /* The draw passes and everything they own: the level's geometry, the
       per-frame billboard buffers, the sprite atlases, and the gun. One struct
       so that every mb_init has an mb_free -- see scene.h.

       Takes the weapon because loading the gun model is how the weapon learns
       where its barrel ends; that is the one fact that crosses the
       weapon/weaponview seam. See weaponview.h.
       무기를 받는 이유는 총기 모델을 로드하는 것이 곧 무기가 자기 총열이 어디서 끝나는지
       알게 되는 경로이기 때문입니다. weapon/weaponview 이음매를 넘는 단 하나의 사실입니다.
       weaponview.h를 참조하십시오. */
    scene_init(scene, &g_world.weapon);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    return 1;
}

/* ------------------------------------------------------------------- main */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev;

    /* Before anything is built, because a playback overrides which level is
       loaded and that load is the last thing app_start's successor does.
       무엇이 만들어지기도 전입니다. 재생이 어느 레벨을 로드할지 덮어쓰며, 그 로드는 app_start
       다음에 오는 것이 하는 마지막 일이기 때문입니다. */
    demo_file_parse_cmdline(&g_demo, cmd);

    /* Initialised because ::app_start writes it only once the window exists,
       and the compiler cannot see that a non-zero return implies it did. A
       zeroed handle is also the honest value for "no window was made".
       ::app_start가 창이 생긴 뒤에야 이 값을 쓰며, 0이 아닌 반환이 그것을 뜻한다는 것을
       컴파일러는 알 수 없으므로 초기화합니다. 0인 핸들은 "창이 만들어지지 않았다"에 대한
       정직한 값이기도 합니다. */
    HDC   dc = 0;
    Scene scene;
    if (!app_start(inst, show, &dc, &scene)) return 1;

    /* The message box is here rather than inside, because saying something to
       the user is a window's job and demo_file.c has no window -- see
       demo_file.h. A recording that will not load is reported and then ignored,
       and the game starts normally: a demo is not a reason to refuse to run.
       메시지 박스가 안이 아니라 이곳에 있는 이유는, 사용자에게 무언가를 말하는 것이 창의
       일이고 demo_file.c에는 창이 없기 때문입니다. demo_file.h를 참조하십시오. 불러오지 못하는
       기록은 알린 뒤 무시하며 게임은 평소대로 시작합니다. 데모는 실행을 거부할 이유가
       아닙니다. */
    if (!demo_file_open(&g_demo, &g_world))
        MessageBoxA(0, "That demo could not be read.", "SFPS", MB_ICONERROR);

    /* WORLD_ENTER_NEW whichever this is: a fresh start begins at full health
       with the boot belt, which is what wp_init just set, and a playback enters
       its recorded level the same way -- which is the one entry a recording can
       reproduce without carrying world state. See demo.h. It also leaves behind
       the stage checkpoint a restart replays, so restarting the first stage
       lands exactly here. The geometry this makes stale is uploaded by the
       world_take_geometry_scope in the loop below, on the first frame.
       어느 쪽이든 WORLD_ENTER_NEW입니다. 새 시작은 wp_init이 방금 설정한 부팅 구성으로 체력이
       가득한 채 시작하고, 재생도 기록된 레벨에 같은 방식으로 진입합니다. 월드 상태를 나르지
       않고 기록이 재현할 수 있는 유일한 진입입니다. demo.h를 참조하십시오. 또한 재시작이
       재생할 스테이지 체크포인트를 남기므로, 첫 스테이지를 재시작하면 정확히 이 자리에
       도달합니다. 이것이 낡게 만드는 지오메트리는 아래 루프의 world_take_geometry_scope가 첫
       프레임에 업로드합니다. */
    world_load_level(&g_world, g_world.cur_level, WORLD_ENTER_NEW);

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
        case MENU_ACT_DISPLAY: {
            apply_display_mode(menu_settings()->display);
            /* After the window has been restyled, not before: the offscreen
               buffer is sized from the CLIENT area, and that is only correct
               once the new style has been applied.
               창 스타일을 바꾼 *뒤*여야 합니다. 오프스크린 버퍼는 클라이언트 영역을
               기준으로 크기가 정해지는데, 그 값은 새 스타일이 적용된 뒤에야
               올바릅니다. */
            int cw, ch;
            client_size(&cw, &ch);
            gfx_apply_pixel_preset(menu_settings()->pixel, cw, ch);
            break;
        }
        case MENU_ACT_NONE:
            break;
        }
        if (!g_running) break;

        gfx_apply_live_settings();

        /* The volume rows, read the same way and for the same reason: a setting
           that needs no signal to change is applied by reading it every frame,
           so moving the slider is audible on the next sound rather than on the
           next event that happens to notify something.
           음량 행이며, 같은 방식으로 같은 이유에서 읽습니다. 변경에 신호가 필요 없는 설정은
           매 프레임 읽어서 적용하므로, 슬라이더를 움직이면 무언가에 알리는 다음 사건이
           아니라 다음 소리에서 들립니다. */
        audio_set_volume(menu_settings()->master * MENU_VOL_PER_STEP,
                         menu_settings()->sfx    * MENU_VOL_PER_STEP,
                         menu_settings()->music  * MENU_VOL_PER_STEP);

        /* WHICH track, and it is stated as a CONDITION rather than fired as an
           event -- music_play is idempotent precisely so this can be. The title
           screen has its own piece; a brute alive is the closest thing this
           bestiary has to a boss (120hp against the next-toughest 40, and the
           only monster that is slower than the player); everything else is
           ordinary play.
           *어느* 트랙인지이며, 사건으로 발화하지 않고 *조건*으로 진술합니다. music_play가
           멱등인 것이 바로 이것을 가능하게 하기 위함입니다. 타이틀 화면은 자기 곡을 가지고,
           살아 있는 브루트는 이 도감이 가진 보스에 가장 가까운 것이며(다음으로 강한 것이
           40일 때 120hp이고, 플레이어보다 느린 유일한 몬스터입니다), 그 밖은 평상시
           플레이입니다. */
        music_play(g_world.run.title ? MUSIC_TITLE :
                   world_boss_present(&g_world) ? MUSIC_BOSS : MUSIC_LEVEL);

        /* REAL seconds, not world time: the music keeps playing behind a pause
           menu and across the between-levels screen. See music.h.
           월드 시간이 아니라 *실제* 초입니다. 음악은 일시정지 메뉴 뒤에서도 레벨 사이
           화면에서도 계속 재생됩니다. music.h를 참조하십시오. */
        music_update(dt);

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
        /* --- where this frame's intent comes from ---------------------------
           A replay supplies the intent AND the clock AND the viewport shape,
           because the same inputs over different intervals is not the same run
           and the muzzle solve reads the aspect. The window is still drawn at
           whatever size it happens to be; only the SIMULATION is handed the
           recorded one. ::demo_take writes nothing when it has nothing, so the
           two lines below are the whole of the fork.
           재생은 의도와 시계와 뷰포트 형태를 함께 공급합니다. 같은 입력을 다른 간격에
           적용하는 것은 같은 플레이가 아니고, 총구 계산이 종횡비를 읽기 때문입니다. 창은
           여전히 그때그때의 크기로 그려집니다. 기록된 값을 건네받는 것은 *시뮬레이션*뿐입니다.
           ::demo_take는 줄 것이 없으면 아무것도 쓰지 않으므로, 아래 두 줄이 분기의 전부입니다. */
        Input in;
        float sim_aspect = (float)vw / (float)vh;
        if (!demo_take(&g_demo.drive, &in, &sim_aspect, &dt))
            input_gather(&in, &g_world);

        /* Read before the step, because the step is what clears it and the
           window has to notice the transition to hand the cursor back and throw
           away one mouse delta. Dismissing the title used to happen inside
           ::wnd_proc, which could do both on the spot; the rule moved to
           ::step_confirm and only this half stayed behind.
           스텝 이전에 읽습니다. 그것을 지우는 것이 스텝이고, 창은 커서를 돌려주고 마우스
           변화량 하나를 버리기 위해 그 전이를 알아채야 하기 때문입니다. 타이틀 해제는 이전에
           ::wnd_proc 안에서 일어났고 그곳에서 둘 다 즉시 할 수 있었습니다. 규칙은
           ::step_confirm으로 옮겨 갔고 이 절반만 남았습니다. */
        int was_title = g_world.run.title;

        demo_put(&g_demo.drive, &in, vw, vh, dt);

        int frozen = world_step(&g_world, &in, sim_aspect, dt);

        if (was_title && !g_world.run.title) {
            g_warp_mouse = 1;
            cursor_update();
        }

        /* --- restart -------------------------------------------------------
           Three ways in -- the menu's RESTART row, a key on the death screen,
           and a click on it -- and all three arrive as the same flag, so there
           is one implementation. What is left here is the part that belongs to
           the window: the menu has to close, the next mouse delta has to be
           thrown away, and the cursor has to be re-decided AFTER `dead` has
           changed.

           AFTER the step rather than before it, which is where this used to be.
           The flag is now raised BY the step -- ::step_confirm is what applies
           the death screen's grace period -- so reading it first would act on
           the request a frame late. The menu's own RESTART row sets it above
           and is picked up here in the same frame it was chosen.

           진입로는 셋(메뉴의 RESTART 행, 사망 화면에서의 키, 그곳에서의 클릭)이며 셋 모두 같은
           플래그로 도착하므로 구현은 하나입니다. 이곳에 남은 것은 창에 속한 부분입니다. 메뉴를
           닫고, 다음 마우스 변화량을 버리고, `dead`가 바뀐 *뒤에* 커서를 다시 결정해야 합니다.

           이전에 있던 자리인 스텝 *이전*이 아니라 스텝 *이후*입니다. 이제 그 플래그를 세우는
           것이 스텝이므로(사망 화면의 유예 시간을 적용하는 것이 ::step_confirm입니다) 먼저
           읽으면 요청을 한 프레임 늦게 처리하게 됩니다. 메뉴의 RESTART 행은 위에서 플래그를
           세우며 고른 바로 그 프레임에 이곳에서 처리됩니다. */
        if (g_world.run.restart_wanted) {
            world_restart(&g_world);
            menu_close();
            g_warp_mouse = 1;
            cursor_update();

            /* Re-derived, because the frame about to be DRAWN is the restarted
               one and `frozen` describes the world the step ran against -- a
               world that was dead or paused, which is how the restart was asked
               for in the first place. Drawing it frozen would put the death
               screen's overlay over a run that has already begun again.
               다시 유도합니다. 이제 *그려질* 프레임은 재시작된 것이고 `frozen`은 스텝이
               대상으로 삼았던 월드, 즉 죽었거나 일시정지된 월드를 서술하기 때문입니다.
               애초에 재시작이 요청된 방식이 그것입니다. 정지 상태로 그리면 이미 다시 시작된
               플레이 위에 사망 화면의 오버레이가 놓입니다. */
            frozen = world_frozen(&g_world, menu_is_open());
        }

        /* Hot reload: in a HOT_RELOAD build this notices an edit under the
           assets directory and rebuilds whatever came out of it, so a
           silhouette change appears in the running game -- in the real level,
           with the real lighting and fog -- without a rebuild. Compiled to
           nothing in release, where data_poll() is a constant 0. */
        if (data_poll()) {
            wpview_set_model(&scene.wpview, &g_world.weapon, "shotgun");
            wpview_reload_texture(&scene.wpview);   /* also flushes the texture cache */
            audio_reload();
            fx_reload(&g_world.pools);                  /* re-read effects.txt, drop live particles */

            /* After the flush, not before: tex_flush deletes every cached
               material, so resolving the level's materials first would leave
               level_tex holding deleted texture names. The rebuild below
               resolves them, so it has to follow wp_reload_texture.

               WORLD_ENTER_NEW: an edit to the map is an authoring action, so
               the player is respawned at the (possibly moved) start with the
               boot belt rather than left standing wherever the old geometry put
               them -- which could now be inside a wall. It resets the stage
               checkpoint too, which is what an author wants: the map they just
               changed is the one being started, not the one they walked in from.

               플러시 이후여야 합니다. tex_flush는 캐시된 모든 재질을 삭제하므로, 레벨의
               재질을 먼저 해석하면 level_tex가 삭제된 텍스처 이름을 보유하게 됩니다. 아래의
               재생성이 재질을 해석하므로 wp_reload_texture 다음에 와야 합니다.

               맵 편집은 제작 행위이므로 플레이어를 (옮겨졌을 수 있는) 시작 지점에 탄약을
               채워 다시 스폰합니다. 예전 지오메트리 기준의 위치에 그대로 두면 이제 벽 속일
               수도 있습니다. */
            world_load_level(&g_world, g_world.cur_level, WORLD_ENTER_NEW);
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
        /* Still one site, and now it asks HOW MUCH went stale rather than only
           whether anything did. A door in motion reaches the second branch,
           which rebuilds and re-sends the geometry the door moves and leaves
           the rest of the level -- the overwhelming majority of it, and the
           part that costs a light bake -- exactly as it was.
           여전히 한 곳이며, 이제 무언가 낡았는지만이 아니라 *얼마나* 낡았는지를 묻습니다.
           움직이는 문은 두 번째 갈래에 도달하여, 문이 움직이는 지오메트리만 다시 만들고 다시
           보내며, 레벨의 나머지(압도적 다수이자 라이트 베이크 비용을 치르는 부분)는 있던 그대로
           둡니다. */
        int dynamic;
        switch (world_take_geometry_scope(&g_world, &dynamic)) {
        case WORLD_GEOM_ALL:
            scene_build_level(&scene, &g_world.level, dynamic);
            break;
        case WORLD_GEOM_MOVING:
            scene_rebuild_moving(&scene, &g_world.level);
            break;
        case WORLD_GEOM_NONE:
            break;
        }

        /* --- one frame of drawing ------------------------------------------
           The other half of the pair above: everything the game shows, in one
           call, in an order a test can reach. See scene.c. `frozen` is passed
           rather than re-derived, because the step may have set `dead` this
           very frame.
           위 호출의 나머지 절반입니다. 게임이 보여 주는 모든 것이 한 번의 호출 안에,
           테스트가 도달할 수 있는 순서로 있습니다. scene.c를 참조하십시오. `frozen`을 다시
           유도하지 않고 전달하는 이유는, 갱신이 바로 이번 프레임에 `dead`를 설정했을 수
           있기 때문입니다. */
        scene_frame(&g_world, &scene, vw, vh, frozen);

        SwapBuffers(dc);

        fps_accum += dt; fps_frames++;
        if (fps_accum >= 0.5) {
            set_title(&g_world, (int)(fps_frames / fps_accum));
            fps_accum = 0; fps_frames = 0;
        }
    }

    if (!demo_file_close(&g_demo))
        MessageBoxA(0, "The demo could not be written.", "SFPS", MB_ICONERROR);

    /* Pairs every mb_init the program made: ::scene_init's, and the two
       modules that build their own buffer on first draw rather than at
       start-up. fx's was the one missing -- it allocated inside ::fx_draw and
       nothing here answered it, which is exactly the drift this paragraph
       warns about happening to the paragraph itself. The process is
       about to exit and the OS would reclaim all of it anyway, but render.h
       states the contract and this file is the one people copy from when they
       add a buffer -- leaving it unpaired here is how it stops being kept
       anywhere.
       This paragraph sat above demo_close() until the demo plumbing moved out,
       which is how a comment ends up explaining the call that happens to follow
       it rather than the one it was written for.
       프로그램이 만든 모든 mb_init과 짝을 맞춥니다. ::scene_init의 것들, 그리고 시작 시점이
       아니라 첫 그리기에서 자기 버퍼를 만드는 두 모듈의 것입니다. 빠져 있던 것은 fx였습니다.
       ::fx_draw 안에서 할당하는데 이곳에서 답하는 것이 없었으며, 이 문단이 경고하는 바로 그
       어긋남이 이 문단 자신에게 일어난 것입니다. 프로세스가 곧
       종료되므로 OS가 어차피 전부 회수하지만, render.h가 계약을 명시하고 있으며 새
       버퍼를 추가하는 사람이 참고하는 파일이 바로 이 파일입니다. 이곳에서 짝을 맞추지
       않으면 그 계약은 어디에서도 지켜지지 않게 됩니다.
       이 문단은 데모 배관이 빠져나가기 전까지 demo_close() 위에 있었습니다. 주석이 자신이
       설명하려던 호출이 아니라 우연히 뒤에 오는 호출을 설명하게 되는 방식이 그것입니다. */
    scene_free(&scene);
    decal_free();
    fx_free();

    post_shutdown();
    audio_shutdown();
    cursor_show(1);   /* leave the desktop's pointer as we found it */
    return 0;
}
