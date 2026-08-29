/* scenetest -- check that one frame is drawn in the order scene.h describes.
 *
 * The counterpart of tools\steptest.c, and it exists for the same reason. That
 * file checks the order ::world_step advances the world in; this one checks the
 * order ::scene_frame draws it in. Until scene_frame existed there was no such
 * order to reach -- it was inline in main.c's WinMain, and the only way to run
 * it was to open a window and play.
 *
 * WHAT AN ORDER TEST CAN ACTUALLY SEE. Not much, from the inside: scene_frame
 * returns nothing and the passes it calls report nothing. So this works from
 * the outside, on the two things a frame leaves behind.
 *
 *   1. The pass-boundary counter. diag.h's DIAG_PASS_ORDER fires when a draw
 *      lands on the wrong side of post_end -- a world pass after the resolve,
 *      or a UI pass before it. Every pass in scene.c already reports into it;
 *      nothing until now ever asked what it said.
 *   2. The pixels. A frame is rendered into a real context and read back, and
 *      the states are compared against each other rather than against a
 *      stored image. "The death screen and the win screen differ" survives an
 *      art change; "the death screen is this exact PNG" does not, and a test
 *      that has to be re-blessed every time somebody moves a label is a test
 *      people learn to re-bless without reading.
 *
 * The comparisons are chosen to pin the decisions scene.h argues for, because
 * those are the ones a later refactor would quietly undo: the crosshair
 * belongs to an unfrozen frame, a dead hand lets go of the gun, the end
 * screens are exclusive and in one specific precedence, and the menu goes over
 * all of it.
 *
 * 한국어
 * ------
 * tools\steptest.c의 짝이며 같은 이유로 존재합니다. 그 파일은 ::world_step이 월드를
 * 진행시키는 순서를 검사하고, 이 파일은 ::scene_frame이 그것을 그리는 순서를 검사합니다.
 * scene_frame이 있기 전에는 도달할 그런 순서 자체가 없었습니다. main.c의 WinMain 안에
 * 인라인으로 있었고, 실행하는 유일한 방법은 창을 열고 플레이하는 것이었습니다.
 *
 * 순서 테스트가 실제로 볼 수 있는 것. 안쪽에서는 별로 없습니다. scene_frame은 아무것도
 * 반환하지 않고 그것이 호출하는 패스들도 아무것도 보고하지 않습니다. 그래서 이 파일은
 * 바깥에서, 한 프레임이 남기는 두 가지를 봅니다.
 *
 *   1. 패스 경계 카운터. diag.h의 DIAG_PASS_ORDER는 그리기가 post_end의 잘못된 쪽에
 *      놓일 때 발생합니다. 해상 이후의 월드 패스, 또는 그 이전의 UI 패스입니다. scene.c의
 *      모든 패스가 이미 이곳에 보고하고 있었지만, 지금까지 무엇도 그 값을 묻지 않았습니다.
 *   2. 픽셀. 실제 컨텍스트에 한 프레임을 렌더링해 읽어 오며, 저장된 이미지가 아니라 상태
 *      끼리 비교합니다. "사망 화면과 승리 화면은 다르다"는 아트 변경을 견디지만 "사망
 *      화면은 정확히 이 PNG다"는 견디지 못합니다. 누군가 라벨을 옮길 때마다 다시 승인해야
 *      하는 테스트는 사람들이 읽지 않고 승인하는 법을 배우게 되는 테스트입니다.
 *
 * 비교 대상은 scene.h가 논거를 대는 결정들을 고정하도록 골랐습니다. 나중의 리팩토링이
 * 조용히 되돌릴 만한 것들이기 때문입니다. 조준점은 정지되지 않은 프레임의 것이고, 죽은
 * 손은 총을 놓으며, 종료 화면들은 배타적이고 하나의 정해진 우선순위를 가지며, 메뉴는 그
 * 모든 것 위에 놓입니다.
 */

#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gl.h"
#include "wgl.h"
#include "render.h"
#include "post.h"
#include "scene.h"    /* scene_frame -- the order under test */
#include "world.h"
#include "proj.h"   /* proj_fire: putting a light in the air for the check below */
#include "weapon.h"   /* the weapon's rules; scene_init takes one, so WinMain
                         does it separately from world_init and so does this */
#include "font.h"
#include "decal.h"
#include "menu.h"
#include "diag.h"

/* Small on purpose. Every check reads the whole framebuffer back, and the
   comparisons are between frames rather than against a reference, so
   resolution buys nothing but readback time.
   의도적으로 작습니다. 모든 검사가 프레임버퍼 전체를 읽어 오고, 비교는 기준 이미지가
   아니라 프레임끼리이므로, 해상도는 읽기 시간 외에 아무것도 사지 않습니다. */
#define VW 320
#define VH 180

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* ------------------------------------------------------ reading a frame */

static unsigned char g_px[VW * VH * 3];

/* Draws one frame of `w` and returns a fingerprint of what came out.
 *
 * glFinish before the read because glReadPixels is allowed to run ahead of a
 * pipeline that has not finished drawing, and a frame compared half-drawn is a
 * test that fails on a fast machine and passes on a slow one.
 * 읽기 전에 glFinish를 호출하는 이유는, glReadPixels가 아직 그리기를 마치지 않은 파이프라인을
 * 앞질러 실행될 수 있기 때문입니다. 절반만 그려진 채 비교되는 프레임은 빠른 기계에서 실패하고
 * 느린 기계에서 통과하는 테스트입니다. */
static unsigned frame_hash(const World *w, Scene *sc, int frozen) {
    scene_frame(w, sc, VW, VH, frozen);

    glFinish();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, VW, VH, GL_RGB, GL_UNSIGNED_BYTE, g_px);

    unsigned h = 2166136261u;
    for (int i = 0; i < VW * VH * 3; i++) h = (h ^ g_px[i]) * 16777619u;
    return h;
}

/* The same, but over one rectangle of the frame rather than all of it, so a
   check can say WHERE it expected a difference. Coordinates are in pixels from
   the bottom left, which is the order glReadPixels hands them back in.
   같은 것이되 프레임 전체가 아니라 한 사각형에 대해서입니다. 덕분에 검사가 어디에서 차이를
   기대했는지 말할 수 있습니다. 좌표는 좌하단 기준 픽셀이며, glReadPixels가 돌려주는
   순서입니다. */
static unsigned region_hash(int x0, int y0, int x1, int y1) {
    unsigned h = 2166136261u;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            for (int c = 0; c < 3; c++)
                h = (h ^ g_px[(y * VW + x) * 3 + c]) * 16777619u;
    return h;
}

/* ------------------------------------------------------------- fixtures */

/* Every run state is set on the World directly rather than reached by playing.
   world_step is steptest's subject; what this file needs is the state, not the
   route to it, and a test that had to kill the player to draw a death screen
   would fail for reasons that belong to another file.
   모든 실행 상태를 플레이로 도달하지 않고 World에 직접 설정합니다. world_step은
   steptest의 대상이며, 이 파일에 필요한 것은 상태이지 그곳에 이르는 경로가 아닙니다.
   사망 화면을 그리기 위해 플레이어를 죽여야 하는 테스트는 다른 파일에 속한 이유로
   실패하게 됩니다. */
static void clear_state(World *w) {
    w->run.title   = 0;
    w->run.dead    = 0;
    w->run.won     = 0;
    w->run.between = 0;
    w->run.death_time   = 0.0f;
    w->run.title_time   = 0.0f;
    w->run.between_time = 0.0f;
}

/* One frame of an end screen, set up so that the else-if chain is the only
 * thing that varies between two of them.
 *
 * `dead` is held set in every one, and that is not incidental. Two things in
 * scene_frame read it OUTSIDE the chain -- the camera collapse and the dropped
 * view model -- so a comparison that toggled it would be comparing a fallen
 * camera against a standing one, and would report the chain as broken when it
 * is not.
 *
 * The timers are past their fade-in for a reason the first run of this file
 * found: at t = 0 the death screen and the intermission have both drawn
 * nothing yet, and two screens that have drawn nothing are identical. That is
 * a true statement about fades and says nothing at all about the chain.
 *
 * 하나의 종료 화면 프레임이며, 두 프레임 사이에서 else-if 사슬만이 유일하게 변하도록
 * 설정합니다.
 *
 * 모든 프레임에서 `dead`를 세워 두며 이는 우연이 아닙니다. scene_frame에서 그것을 읽는 것이
 * 사슬 *바깥*에 둘 있습니다. 카메라 쓰러짐과 사라지는 뷰 모델입니다. 그것을 토글하는 비교는
 * 넘어진 카메라를 서 있는 카메라와 비교하는 셈이 되고, 멀쩡한 사슬을 깨졌다고 보고하게
 * 됩니다.
 *
 * 타이머를 페이드인 이후로 두는 이유는 이 파일의 첫 실행이 찾아냈습니다. t = 0에서는 사망
 * 화면도 인터미션도 아직 아무것도 그리지 않았고, 아무것도 그리지 않은 두 화면은 동일합니다.
 * 그것은 페이드에 관한 참인 진술이며 사슬에 대해서는 아무것도 말해 주지 않습니다. */
static unsigned end_frame(World *w, Scene *sc, int title, int won, int between) {
    clear_state(w);
    w->run.dead         = 1;
    w->run.death_time   = DEATH_ANIM_TIME + 1.0f;
    w->run.title        = title;
    w->run.won          = won;
    w->run.between      = between;
    w->run.title_time   = 1.0f;
    w->run.between_time = 1.0f;
    return frame_hash(w, sc, 1);
}

int main(void) {
    printf("scenetest\n\n");

    HINSTANCE inst = GetModuleHandleA(0);
    if (!gl_bootstrap(inst)) { printf("  gl_bootstrap FAILED\n"); return 1; }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = "scenetest";
    RegisterClassA(&wc);

    /* WS_POPUP, so the window IS its client area. A bordered style would make
       the drawable smaller than the size asked for, and then every readback
       here would run off the end of it -- glReadPixels leaves the bytes past
       the drawable untouched, so the comparison would be against whatever was
       in the buffer from last time and would look like a frame that failed to
       change. The first run of this file did exactly that.
       WS_POPUP이므로 창이 곧 클라이언트 영역입니다. 테두리가 있는 스타일은 드로어블을
       요청한 크기보다 작게 만들고, 그러면 이곳의 모든 읽기가 그 끝을 넘어갑니다.
       glReadPixels는 드로어블 바깥의 바이트를 건드리지 않으므로, 비교 대상이 지난번 버퍼에
       남아 있던 값이 되고 마치 바뀌지 않은 프레임처럼 보이게 됩니다. 이 파일의 첫 실행이
       정확히 그랬습니다. */
    HWND  wnd = CreateWindowExA(0, "scenetest", "", WS_POPUP,
                                0, 0, VW, VH, 0, 0, inst, 0);
    HDC   dc  = GetDC(wnd);
    HGLRC rc  = gl_make_context(dc);
    if (!rc) { printf("  gl_make_context FAILED\n"); return 1; }

    RECT cr;
    GetClientRect(wnd, &cr);
    if (cr.right - cr.left != VW || cr.bottom - cr.top != VH) {
        printf("  the drawable is %dx%d, not %dx%d -- every readback below\n"
               "  would be reading past it\n",
               (int)(cr.right - cr.left), (int)(cr.bottom - cr.top), VW, VH);
        return 1;
    }

    /* The same order WinMain brings things up in, for the same reasons. What
       used to matter here -- calling wp_init after the context and before the
       level load, because it uploaded the view model's texture -- no longer
       does: the drawn gun is scene_init's business now. See weaponview.h.
       WinMain이 같은 이유로 세우는 것과 같은 순서입니다. 이곳에서 중요했던 것, 즉 뷰
       모델의 텍스처를 업로드하므로 컨텍스트 이후이자 레벨 로드 이전에 wp_init을 호출해야
       한다는 점은 더 이상 해당되지 않습니다. 그려지는 총은 이제 scene_init의 소관입니다.
       weaponview.h를 참조하십시오. */
    rd_init();
    decal_init();
    menu_init(0);
    post_init(VW, VH);
    font_init();

    /* Grain to zero, which is the only thing in the frame that is SUPPOSED to
       be different every time. post.c calls it a temporal noise and drives it
       from a frame counter, on purpose: a still dither pattern reads as a
       texture stuck to the screen, and the grain is what stops it. It is also
       the one thing that makes two identical frames different, and every check
       in this file compares one frame against another.
       Turned off rather than worked around -- averaging it out or comparing
       loosely would blunt every comparison below to hide one known variable.
       The levels and the pattern stay at a real setting, so what is being
       compared is still a frame that went through the pass.

       그레인을 0으로 둡니다. 프레임에서 매번 달라지도록 *의도된* 유일한 것입니다. post.c는
       이것을 시간적 잡음이라 부르며 프레임 카운터로 구동하는데, 정지된 디더 패턴은 화면에
       달라붙은 텍스처처럼 보이고 그레인이 그것을 막기 때문입니다. 동시에 동일한 두 프레임을
       다르게 만드는 유일한 것이며, 이 파일의 모든 검사가 한 프레임을 다른 프레임과
       비교합니다.
       우회하지 않고 끕니다. 평균을 내거나 느슨하게 비교하면 알려진 변수 하나를 숨기려고 아래의
       모든 비교를 무디게 만드는 셈입니다. 단계 수와 패턴은 실제 설정값으로 두므로, 비교되는
       것은 여전히 패스를 통과한 프레임입니다. */
    post_set_dither(12.0f, 0.0f, 0.0f);   /* NORMAL levels, Bayer, no grain */

    static World w;
    world_init(&w);

    Scene scene;
    scene_init(&scene, &w.weapon);
    if (!world_load_level(&w, w.cur_level, WORLD_ENTER_NEW)) {
        printf("  world_load_level FAILED\n");
        return 1;
    }
    scene_build_level(&scene, &w.level, 0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);

    /* --- 1. the pass boundary ------------------------------------------
       Read before anything is drawn and again at the end, so what is asserted
       is what THIS file caused rather than whatever the level load may have
       reported. See diag.h: the counter is only compiled in a build that
       defines HOT_RELOAD, which build.ps1 gives every tool.
       무엇이든 그려지기 전에 한 번, 마지막에 다시 읽습니다. 그래서 단언되는 것은 레벨
       로드가 보고했을 수 있는 것이 아니라 *이 파일*이 유발한 것입니다. */
    int pass_before = diag_count(DIAG_PASS_ORDER);

    printf("  --- the frame draws at all ---\n");

    clear_state(&w);
    unsigned playing = frame_hash(&w, &scene, 0);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFinish();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, VW, VH, GL_RGB, GL_UNSIGNED_BYTE, g_px);
    unsigned cleared = 2166136261u;
    for (int i = 0; i < VW * VH * 3; i++)
        cleared = (cleared ^ g_px[i]) * 16777619u;

    ok(playing != cleared, "a drawn frame is not a cleared one");

    /* Everything below compares one frame against another, so a frame that
       does not reproduce itself would make every one of those comparisons
       meaningless -- and would fail them in a way that looks like whatever
       state the check happened to be varying. Established first, so a failure
       here is read as "frames are not deterministic" rather than as a false
       report about the thing under test.

       It is also a real property worth holding: scene_frame is handed a World
       and a viewport and nothing else, and drawing must not depend on what the
       previous frame did to the GL state.

       아래의 모든 것이 한 프레임을 다른 프레임과 비교하므로, 자기 자신을 재현하지 못하는
       프레임은 그 모든 비교를 무의미하게 만들고, 마침 그 검사가 변화시키던 것에 대한 거짓
       보고처럼 보이는 방식으로 실패하게 됩니다. 먼저 확립해 두어, 이곳의 실패가 검사 대상에
       대한 거짓 보고가 아니라 "프레임이 결정적이지 않다"로 읽히게 합니다.

       또한 붙잡을 가치가 있는 실제 성질입니다. scene_frame은 World와 뷰포트만 건네받으며,
       그리기는 이전 프레임이 GL 상태에 한 일에 의존해서는 안 됩니다. */
    clear_state(&w);
    static unsigned char first_px[VW * VH * 3];
    frame_hash(&w, &scene, 0);
    for (int i = 0; i < VW * VH * 3; i++) first_px[i] = g_px[i];

    unsigned playing_again = frame_hash(&w, &scene, 0);
    ok(playing == playing_again, "the same world drawn twice is the same frame");

    if (playing != playing_again) {
        int n = 0, x0 = VW, y0 = VH, x1 = -1, y1 = -1;
        for (int y = 0; y < VH; y++)
            for (int x = 0; x < VW; x++) {
                int i = (y * VW + x) * 3;
                if (first_px[i] != g_px[i] || first_px[i+1] != g_px[i+1]
                 || first_px[i+2] != g_px[i+2]) {
                    n++;
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
            }
        printf("      %d of %d pixels differ, within x %d..%d  y %d..%d\n",
               n, VW * VH, x0, x1, y0, y1);
    }

    /* --- 2. the crosshair belongs to an unfrozen frame -------------------
       scene_frame draws the crosshair and the hook readout only when the frame
       is not frozen, because a crosshair implies you can still act. Compared
       over the middle of the screen, which is the only place it can be.
       정지되지 않은 프레임에서만 조준점과 훅 표시를 그립니다. 조준점은 아직 행동할 수
       있음을 암시하기 때문입니다. 화면 한가운데에서 비교하며, 그곳이 조준점이 있을 수 있는
       유일한 자리입니다. */
    printf("\n  --- what a frozen frame stops showing ---\n");

    clear_state(&w);
    frame_hash(&w, &scene, 0);
    unsigned mid_live = region_hash(VW/2 - 12, VH/2 - 12, VW/2 + 12, VH/2 + 12);

    frame_hash(&w, &scene, 1);
    unsigned mid_frozen = region_hash(VW/2 - 12, VH/2 - 12, VW/2 + 12, VH/2 + 12);

    ok(mid_live != mid_frozen,
       "the crosshair is gone from the middle of a frozen frame");

    /* --- 3. a dead hand lets go ------------------------------------------
       The view model is dropped the moment the player dies, and it is the only
       thing that occupies the bottom of the screen, so that is where the two
       frames have to differ. death_time is past the collapse so the camera has
       settled and the difference is the gun rather than the fall.
       플레이어가 죽는 순간 뷰 모델이 사라지며, 화면 아래쪽을 차지하는 것은 그것뿐이므로
       두 프레임은 그곳에서 달라야 합니다. death_time을 쓰러짐 이후로 두어 카메라가 안착한
       뒤이므로, 차이는 넘어짐이 아니라 총입니다. */
    printf("\n  --- the gun, and who is holding it ---\n");

    clear_state(&w);
    frame_hash(&w, &scene, 0);
    unsigned gun_alive = region_hash(VW/2 - 40, 0, VW/2 + 40, 40);

    clear_state(&w);
    w.run.dead = 1;
    w.run.death_time = DEATH_ANIM_TIME + 1.0f;
    frame_hash(&w, &scene, 1);
    unsigned gun_dead = region_hash(VW/2 - 40, 0, VW/2 + 40, 40);

    ok(gun_alive != gun_dead, "the view model is not drawn once the player dies");

    /* --- 4. the end screens, and which one wins --------------------------
       scene_frame chains title / won / between / dead with else-if, and the
       ORDER of that chain is a decision with a reason: the world is frozen
       during the intermission so nothing can kill the player, and a terminal
       level sets `won` rather than `between`. Setting two at once and
       requiring the frame to match the winner alone is what pins the chain --
       reorder it and exactly one of these stops holding.
       scene_frame은 title / won / between / dead를 else-if로 잇고, 그 사슬의 *순서*는
       근거가 있는 결정입니다. 인터미션 동안 월드가 정지하므로 플레이어가 죽을 수 없고,
       종착 레벨은 `between`이 아니라 `won`을 세웁니다. 둘을 동시에 세우고 프레임이 이긴
       쪽 단독과 일치하기를 요구하는 것이 사슬을 고정합니다. 순서를 바꾸면 이 중 정확히
       하나가 성립하지 않게 됩니다. */
    printf("\n  --- the end screens are exclusive, in one order ---\n");

    unsigned dead_only    = end_frame(&w, &scene, 0, 0, 0);
    unsigned between_wins = end_frame(&w, &scene, 0, 0, 1);
    unsigned won_wins     = end_frame(&w, &scene, 0, 1, 0);
    unsigned title_wins   = end_frame(&w, &scene, 1, 0, 0);

    printf("      dead %08x  between %08x  won %08x  title %08x\n",
           dead_only, between_wins, won_wins, title_wins);

    ok(dead_only != between_wins && dead_only != won_wins
       && dead_only != title_wins && between_wins != won_wins
       && between_wins != title_wins && won_wins != title_wins,
       "the four screens draw four different frames");

    ok(end_frame(&w, &scene, 0, 1, 1) == won_wins,
       "a won run is not shown the intermission");

    ok(between_wins != dead_only,
       "the intermission is drawn over the death screen, not under it");

    ok(end_frame(&w, &scene, 1, 1, 1) == title_wins,
       "and the title screen beats all three");

    /* --- 5. the menu goes over everything --------------------------------
       Drawn last so it sits over the HUD and the end screens both: a menu
       opened from the win screen has to be readable, and it is the thing the
       player is operating. menu_escape opens it, which is the same door the
       ESC key uses.
       HUD와 종료 화면 양쪽 위에 놓이도록 마지막에 그립니다. 승리 화면에서 연 메뉴도 읽을
       수 있어야 하며, 그것이 플레이어가 조작하고 있는 대상입니다. */
    printf("\n  --- the menu is drawn last ---\n");

    clear_state(&w);
    w.run.dead = 1;
    unsigned death_plain = frame_hash(&w, &scene, 1);

    menu_escape();
    ok(menu_is_open(), "the menu opened for the check below");
    unsigned death_menu = frame_hash(&w, &scene, 1);
    menu_close();

    ok(death_plain != death_menu, "an open menu changes the death screen");

    unsigned death_again = frame_hash(&w, &scene, 1);
    ok(death_again == death_plain,
       "and closing it puts the frame back exactly as it was");

    /* --- the title screen is a menu with the game's name over it ---------
     *
     * THREE PASSES IN ONE FRAME and the order between two of them is the
     * point: scene_draw_menu washes the world for the screen it draws, and
     * scene_draw_title goes OVER that wash because the name is the screen's own
     * art rather than something the dim should push back. Drawn the other way
     * round the title would be darkened by the menu it heads, and nothing but
     * looking at it would say so.
     *
     * What is checkable from here is that all three combinations differ: a
     * title with no menu, a title with the menu, and the pause root over a run.
     * A title screen that had quietly stopped drawing its rows -- or its name --
     * would collapse two of those together.
     *
     * *한 프레임에 세 개의 패스*이며 그중 둘 사이의 순서가 요점입니다. scene_draw_menu는 자신이
     * 그리는 화면을 위해 월드를 씻어 내고, scene_draw_title은 그 워시 *위로* 갑니다. 이름은 그
     * 어둡게 하기가 뒤로 밀어내야 할 것이 아니라 화면 자신의 아트이기 때문입니다. 반대로 그리면
     * 제목이 자기가 머리글로 있는 메뉴에 의해 어두워지고, 그것을 말해 주는 것은 눈으로 보는 것뿐
     * 입니다.
     *
     * 이곳에서 검사할 수 있는 것은 세 조합이 모두 다르다는 것입니다. 메뉴 없는 타이틀, 메뉴가 있는
     * 타이틀, 그리고 플레이 위의 일시정지 최상위입니다. 조용히 행을 그리지 않게 된 타이틀 화면은
     * (또는 이름을 그리지 않게 된 화면은) 그중 둘을 하나로 무너뜨립니다. */
    printf("\n  --- the title screen ---\n");

    clear_state(&w);
    w.run.title      = 1;
    w.run.title_time = 2.0f;       /* past the header's fade-in */
    menu_close();
    unsigned title_bare = frame_hash(&w, &scene, 1);

    menu_open_title();
    ok(menu_screen() == MENU_TITLE, "the title menu is the screen showing");
    unsigned title_menu = frame_hash(&w, &scene, 1);
    ok(title_bare != title_menu, "the title menu's rows are drawn over the title");

    /* The locked row is DRAWN, not skipped -- a row nobody can see is a mode
       nobody knows to want. Faint, so unlocking it changes the frame.
       잠긴 행은 건너뛰어지지 않고 *그려집니다*. 아무도 볼 수 없는 행은 아무도 원할 줄 모르는
       모드입니다. 흐리게 그려지므로 해금하면 프레임이 바뀝니다. */
    menu_set_unlocked(MENU_UNLOCK_ENDLESS);
    unsigned title_unlocked = frame_hash(&w, &scene, 1);
    ok(title_menu != title_unlocked,
       "and a locked row is drawn faintly rather than left out");
    menu_set_unlocked(0);

    menu_close();
    ok(frame_hash(&w, &scene, 1) == title_bare,
       "closing the title menu puts the frame back, so nothing leaked out of it");

    /* --- 6. a level's lamps are baked, not uploaded ----------------------
       The shader's light slots are for light that MOVES. A level's own lamps
       are compiled into its vertices when it loads, so putting them in the
       slots as well applies each lamp twice -- once smoothly and shadowed from
       the bake, once per-pixel and unshadowed from the loop. That is what the
       code did until this check existed, and it is invisible: a room lit twice
       does not look broken, it looks bright.

       arena is loaded above and has four lamps, so a world pass that uploads
       any dynamic light at all is uploading those.

       셰이더의 광원 슬롯은 *움직이는* 빛을 위한 것입니다. 레벨 자신의 등은 로드될 때 정점에
       구워지므로, 그것을 슬롯에도 넣으면 각 등이 두 번 적용됩니다. 한 번은 베이크에서
       부드럽고 그림자가 진 채로, 한 번은 반복문에서 픽셀 단위로 그림자 없이. 이 검사가 생기기
       전까지 코드가 하던 일이며, 보이지 않습니다. 두 번 밝혀진 방은 고장 나 보이지 않고
       *밝아* 보입니다.

       위에서 로드한 arena에는 등이 넷 있으므로, 동적 광원을 하나라도 업로드하는 월드
       패스는 곧 그 등들을 업로드하고 있는 것입니다. */
    printf("\n  --- the level's lamps are baked, not uploaded ---\n");

    clear_state(&w);
    frame_hash(&w, &scene, 0);
    printf("      the level declares %d lamps; the shader was given %d\n",
           w.level.n_lights, rd_light_count());
    ok(w.level.n_lights > 0, "the level under test actually has lamps");
    ok(rd_light_count() == 0,
       "and none of them occupy a dynamic light slot");

    /* --- and the slots are not merely unused -----------------------------
       The check above passes just as well when nothing can ever fill a slot,
       which is exactly the state this code was in: rd_lights(0,0,0) sat at the
       end of the world pass and emptied the array every frame, so "no lamp
       occupies a slot" was true because NOTHING did. That is a weaker fact
       than it reads as, and it held for as long as the feature was missing.

       Putting a grenade in the air separates the two. If the count is still
       zero with one live projectile, the moving-light feed is gone again and
       the assertion above has quietly gone back to proving nothing.

       그리고 슬롯이 단지 *쓰이지 않는* 것이 아님을 확인합니다.
       위의 검사는 슬롯을 채울 수 있는 것이 아예 없을 때에도 똑같이 통과하며, 이 코드가 바로 그
       상태에 있었습니다. rd_lights(0,0,0)이 월드 패스 끝에 앉아 매 프레임 배열을 비웠으므로,
       "어떤 등도 슬롯을 차지하지 않는다"는 *아무것도* 차지하지 않았기 때문에 참이었습니다.
       읽히는 것보다 약한 사실이며, 기능이 없는 동안 내내 유지되었습니다.

       유탄을 공중에 띄우면 둘이 갈라집니다. 살아 있는 발사체가 하나 있는데도 개수가 여전히
       0이라면 움직이는 광원 공급이 다시 사라진 것이고, 위의 단언은 조용히 아무것도 증명하지
       않는 상태로 되돌아간 것입니다. */
    clear_state(&w);
    proj_fire(&w.pools, w.player.pos, v3f(0.0f, 0.0f, -1.0f),
              12.0f,   /* speed   */
              0.0f,    /* gravity: straight, so it stays where it was put */
              20,      /* damage  */
              2.0f,    /* blast   */
              3.0f);   /* fuse    */
    frame_hash(&w, &scene, 0);
    printf("      with one projectile in the air the shader was given %d\n",
           rd_light_count());
    ok(rd_light_count() > 0,
       "a projectile in flight does occupy one");

    /* Back to rest, so the passes after this one are not lit by a leftover.
       proj_reset AS WELL AS clear_state, because they clear different things:
       clear_state puts the RUN back and knows nothing about the pools, and this
       file never calls world_step, so nothing would ever retire the grenade on
       its own. The first version of this check left it in the air and failed
       here, which is the pools half of ::World saying so out loud.
       휴지 상태로 되돌립니다. 이후의 패스들이 남은 광원으로 조명되지 않도록 합니다.
       clear_state와 *함께* proj_reset을 호출하는 이유는 둘이 서로 다른 것을 비우기
       때문입니다. clear_state는 *플레이*를 되돌릴 뿐 풀에 대해 아무것도 모르며, 이 파일은
       world_step을 결코 호출하지 않으므로 유탄을 스스로 퇴역시킬 것이 없습니다. 이 검사의 첫
       판은 그것을 공중에 남겨 둔 채 이곳에서 실패했고, 그것이 ::World의 풀 절반이 직접 말해 준
       것입니다. */
    clear_state(&w);
    proj_reset(&w.pools);
    frame_hash(&w, &scene, 0);
    ok(rd_light_count() == 0, "and the set empties again once it is gone");

    /* --- 7. what the boundary counted -----------------------------------
       Last, so it covers every frame above. A non-zero delta means one of
       those frames drew a world pass after post_end or a UI pass before it,
       which is the failure scene.h's @note describes and which nothing could
       observe until this file existed.
       마지막에 두어 위의 모든 프레임을 포괄합니다. 0이 아닌 증가분은 그 프레임들 중 하나가
       post_end 이후에 월드 패스를 그렸거나 그 이전에 UI 패스를 그렸다는 뜻이며, scene.h의
       @note가 설명하는 실패이자 이 파일이 존재하기 전에는 무엇도 관측할 수 없던 것입니다. */
    printf("\n  --- the world/UI pass boundary ---\n");

    int pass_after = diag_count(DIAG_PASS_ORDER);
    printf("  DIAG_PASS_ORDER reports: %d\n", pass_after - pass_before);
    ok(pass_after == pass_before,
       "no pass landed on the wrong side of post_end");

    /* --- and the guard is not merely silent -------------------------------
       ENGLISH: the check above is a NEGATIVE. It passes when nothing reported,
       which is also exactly what a guard wired to nothing does -- and the flag
       behind these guards has just moved from post.c to diag.c, so "nothing
       reported" was one broken link away from meaning "nothing is watching".

       This drives the boundary by hand and claims the wrong half on each side.
       Both must report. It exercises the whole chain the move rearranged:
       post_begin and post_end announce, diag holds the answer, the guard reads
       it. Nothing is drawn -- the guards are the thing under test here, not the
       draw routines that call them.

       한국어: 위의 검사는 *부정형*입니다. 아무것도 보고하지 않으면 통과하는데, 그것은
       아무것에도 연결되지 않은 가드가 하는 일과 정확히 같습니다. 그리고 이 가드들 뒤의
       플래그는 방금 post.c에서 diag.c로 옮겨 갔으므로, "아무것도 보고하지 않았다"는 연결
       하나만 끊어지면 "아무도 지켜보지 않는다"를 뜻하게 될 참이었습니다.

       이 검사는 경계를 손으로 움직이며 양쪽에서 반대쪽 절반을 주장합니다. 둘 다 보고해야
       합니다. 이 이동이 재배치한 사슬 전체를 실행합니다. post_begin과 post_end가 알리고,
       diag가 답을 보유하고, 가드가 그것을 읽습니다. 아무것도 그리지 않습니다. 이곳에서
       시험 대상은 가드이지 그것을 호출하는 그리기 루틴이 아닙니다. */
    {
        int before = diag_count(DIAG_PASS_ORDER);
        post_begin();               /* the frame is in the world half now */
        DIAG_WANT_UI_PASS();        /* so claiming the UI half is wrong */
        int ui_claimed_in_world = diag_count(DIAG_PASS_ORDER) - before;

        before = diag_count(DIAG_PASS_ORDER);
        post_end(VW, VH);           /* and now it is in the UI half */
        DIAG_WANT_WORLD_PASS();     /* so claiming the world half is wrong */
        int world_claimed_in_ui = diag_count(DIAG_PASS_ORDER) - before;

        ok(ui_claimed_in_world == 1,
           "a UI-pass claim inside the world pass is reported");
        ok(world_claimed_in_ui == 1,
           "and a world-pass claim after post_end is reported too");
    }

    scene_free(&scene);
    decal_free();
    post_shutdown();

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall scene order checks passed\n",
           fails);
    return fails != 0;
}
