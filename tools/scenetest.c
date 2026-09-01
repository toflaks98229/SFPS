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
#include "enemy.h"  /* Shot and MonTypeID: the bolt colour check places one by hand */
#include "weapon.h"   /* the weapon's rules; scene_init takes one, so WinMain
                         does it separately from world_init and so does this */
#include "font.h"
#include "decal.h"
#include "menu.h"
#include "diag.h"

/* Small on purpose, but NOT AS SMALL AS IT WAS. Every check reads the whole
   framebuffer back, and the comparisons are between frames rather than against
   a reference, so resolution buys nothing but readback time -- for every check
   whose subject is somewhere near the middle of the frame.
   THE MENU HEADERS ARE NOT. menu.c centres the row block and then measures the
   header UPWARDS from it: menu_title_y is block_top(vh) - TITLE_GAP_FRONT, and
   at 320x180 that lands at y = -155, above the top edge. The title screen drew
   its name into nothing, so the check below -- the one that says settings must
   not have that name over it -- passed at that size no matter what scene.c
   did. 540 is the smallest 16:9 height that puts BOTH headers on screen: the
   title's at y = 25, the settings page's (ten rows, so a taller block) at
   y = 10.
   의도적으로 작지만 *예전만큼 작지는 않습니다.* 모든 검사가 프레임버퍼 전체를 읽어 오고,
   비교는 기준 이미지가 아니라 프레임끼리이므로 해상도는 읽기 시간 외에 아무것도 사지
   않습니다. 대상이 프레임 한가운데 근처에 있는 검사에 대해서는 그렇습니다.
   *메뉴 머리글은 그렇지 않습니다.* menu.c는 행 블록을 가운데에 두고 머리글을 거기서 *위로*
   잽니다. menu_title_y는 block_top(vh) - TITLE_GAP_FRONT이며, 320x180에서 그것은 위쪽
   가장자리 바깥인 y = -155에 놓입니다. 타이틀 화면은 자기 이름을 허공에 그렸고, 그래서 아래의
   검사(설정 화면에 그 이름이 없어야 한다는 검사)는 scene.c가 무엇을 하든 그 크기에서
   통과했습니다. 540은 머리글 *둘 다*를 화면 안에 넣는 가장 작은 16:9 높이입니다. 타이틀의
   것은 y = 25, 설정 화면의 것은(행이 열이라 블록이 더 큽니다) y = 10입니다. */
#define VW 960
#define VH 540

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

/* How far the whole frame leans toward one channel, in 0-255. Signed against
   the mean of the other two, so a scene that simply got BRIGHTER scores zero
   and only a scene that got more RED, or more GREEN, moves it.
   That distinction is the whole point of the check it serves: this renderer
   quantises luminance into ::LIGHT_BANDS levels and saturates a sunlit
   surface, so "the light made things brighter" is a claim it often cannot
   make. Hue is the channel that survives, and this measures that channel.
   프레임 전체가 한 채널로 얼마나 기우는지를 0-255로 잽니다. 나머지 두 채널의 평균에 대해
   부호가 있으므로, 그저 *밝아진* 장면은 0점이고 더 *붉어지거나* 더 *푸르러진* 장면만
   움직입니다. 그 구분이 이것이 봉사하는 검사의 요점 전부입니다. 이 렌더러는 휘도를
   ::LIGHT_BANDS 단계로 양자화하고 햇빛 표면을 포화시키므로 "광원이 밝게 만들었다"는 주장은
   할 수 없을 때가 많습니다. 색조가 살아남는 채널이며, 이것이 그 채널을 잽니다. */
/* A copy of the last frame, and how many pixels two frames differ by. The
   hash says WHETHER anything changed; this says HOW MUCH, which is the only
   question that can tell a sprite being drawn from a sprite being drawn and
   then cut in half by something in front of it.
   마지막 프레임의 복사본과, 두 프레임이 몇 픽셀 다른지입니다. 해시는 무언가 *바뀌었는지*를
   말하고, 이것은 *얼마나*를 말합니다. 그것이 그려진 스프라이트와, 그려졌다가 앞의 무언가에
   반쯤 잘린 스프라이트를 가를 수 있는 유일한 질문입니다. */
static unsigned char g_prev[VW * VH * 3];

static void keep_frame(void) {
    for (int i = 0; i < VW * VH * 3; i++) g_prev[i] = g_px[i];
}

static int px_changed(void) {
    int n = 0;
    for (int i = 0; i < VW * VH; i++)
        if (g_px[i*3] != g_prev[i*3] || g_px[i*3+1] != g_prev[i*3+1] ||
            g_px[i*3+2] != g_prev[i*3+2]) n++;
    return n;
}

static float lean(int ch) {
    double acc = 0.0;
    for (int i = 0; i < VW * VH; i++) {
        int r = g_px[i * 3], g = g_px[i * 3 + 1], b = g_px[i * 3 + 2];
        int v[3] = { r, g, b };
        acc += v[ch] - (v[(ch + 1) % 3] + v[(ch + 2) % 3]) * 0.5;
    }
    return (float)(acc / (VW * VH));
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

    /* --- a screen reached FROM the title has no title over it ------------
     *
     * `title` STAYS SET while settings and credits are up. They are opened
     * from the title screen and the run has still not begun, so the name was
     * drawn on them too -- over the header scene_draw_menu puts at the very
     * same menu_title_y. Two headers, one printed on top of the other, and
     * the only thing that said so was looking at it.
     *
     * ASKED THROUGH THE FADE CLOCK rather than by comparing two screens. The
     * name arrives on title_time, so a frame that draws it CANNOT look the
     * same at t = 0 and t = 2, and a frame that does not draw it cannot look
     * different. That is the name's presence asked directly, and it needs
     * nothing else about the two screens to be equal -- which is what a
     * comparison against a settings page reached from a run would have needed,
     * and that page has a HUD behind it.
     *
     * *타이틀에서 들어간 화면 위에는 타이틀이 없습니다.*
     *
     * 설정과 크레딧이 떠 있는 동안에도 `title`은 그대로 서 있습니다. 타이틀 화면에서 열리며
     * 플레이는 여전히 시작되지 않았으므로, 이름이 그 위에도 그려졌습니다. scene_draw_menu가
     * 정확히 같은 menu_title_y에 두는 머리글 위에 말입니다. 머리글 둘이 서로 겹쳐 찍혔고, 그것을
     * 말해 주는 것은 눈으로 보는 것뿐이었습니다.
     *
     * *두 화면을 비교하지 않고 페이드 시계로 묻습니다.* 이름은 title_time에 따라 도착하므로,
     * 그것을 그리는 프레임은 t = 0과 t = 2에서 같아 *보일 수 없고*, 그리지 않는 프레임은 달라
     * 보일 수 없습니다. 이름의 존재를 직접 묻는 것이며, 두 화면에 관한 다른 무엇도 같을 것을
     * 요구하지 않습니다. 플레이에서 들어간 설정 화면과 비교했다면 그것이 필요했을 텐데, 그
     * 화면 뒤에는 HUD가 있습니다. */
    printf("\n  --- settings and credits opened from the title ---\n");

    menu_open_title();
    w.run.title_time = 0.0f;
    unsigned title_t0 = frame_hash(&w, &scene, 1);
    w.run.title_time = 2.0f;
    ok(frame_hash(&w, &scene, 1) != title_t0,
       "the name fades in on the title screen, so its clock shows there");

    /* STORY -> ENDLESS -> SETTINGS. The locked row is landed on rather than
       skipped, which is why this is two steps and not one.
       STORY -> ENDLESS -> SETTINGS입니다. 잠긴 행은 건너뛰어지지 않고 그 위에 놓이므로, 한
       걸음이 아니라 두 걸음입니다. */
    menu_move(2);
    menu_activate();
    ok(menu_screen() == MENU_SETTINGS, "SETTINGS opened from the title");

    unsigned settings_t2 = frame_hash(&w, &scene, 1);
    w.run.title_time = 0.0f;
    ok(frame_hash(&w, &scene, 1) == settings_t2,
       "and no name is drawn over it, whatever the name's clock says");

    w.run.title_time = 2.0f;
    menu_escape();
    ok(menu_screen() == MENU_TITLE, "ESC steps back to the title");
    ok(frame_hash(&w, &scene, 1) != settings_t2,
       "and the name comes back with the screen it belongs to");

    menu_move(3);
    menu_activate();
    ok(menu_screen() == MENU_CREDITS, "CREDITS opened from the title");

    unsigned credits_t2 = frame_hash(&w, &scene, 1);
    w.run.title_time = 0.0f;
    ok(frame_hash(&w, &scene, 1) == credits_t2,
       "and the licence screen carries no title over its own header either");

    w.run.title_time = 2.0f;
    menu_close();

    /* --- 6. a lamp put into a level lights nothing -----------------------
       THIS CHECK HAS POINTED BOTH WAYS AND THEN LOST ITS SUBJECT, and the
       history is why the shape it ends up in is what it is. It began as "the
       lamps occupy no dynamic slot", because they were baked into the vertices
       and being in both places applies each one twice. It became "the lamps
       occupy their slots", because the bake sampled them at the corners of
       faces metres across and they had moved to the shader's loop. Then they
       were switched off in the loop as well -- and then deleted from the level
       files, so no shipped map declares one at all.

       A CHECK THAT ONLY READS THE SHIPPED LEVELS WOULD NOW PASS FOR THE WRONG
       REASON. Zero lamps in, zero slots out, and it would go on passing if
       somebody wired ::Level::lights straight back into ::scene_lights. So
       this puts the lamps in itself. Eight of them, on top of the camera,
       reaching twenty metres -- placed where they would take every slot and
       light every wall in sight if anything read them at all -- and requires
       the count to stay put and the frame not to change by a single pixel.

       The other half of the same fact -- that they do not reach a VERTEX
       either -- is leveltest's and tracetest's, because this file has no
       vertices to look at. Neither half announces itself: a room lit twice
       looks bright, a room lit once looks fine, and only the pair says which
       arrangement is in force.

       *이 검사는 양쪽을 모두 가리켜 보았고 그다음 대상을 잃었으며*, 그 이력이 최종적인 형태의
       이유입니다. 처음에는 "등은 동적 슬롯을 차지하지 않는다"였습니다. 등이 정점에 구워져
       있었고 양쪽에 있으면 각각 두 번 적용되기 때문입니다. 다음에는 "등은 자기 슬롯을
       차지한다"가 되었습니다. 베이크가 몇 미터짜리 면의 모서리에서 표본추출했고 등은 셰이더의
       반복문으로 옮겨 갔기 때문입니다. 그 뒤 반복문에서도 꺼졌고, 이어서 레벨 파일에서
       삭제되어 이제 어떤 출하 맵도 등을 선언하지 않습니다.

       *출하 레벨만 읽는 검사는 이제 엉뚱한 이유로 통과합니다.* 등 0개가 들어가고 슬롯 0개가
       나오며, 누군가 ::Level::lights를 ::scene_lights에 그대로 다시 연결해도 계속 통과할
       것입니다. 그래서 이 검사는 등을 스스로 넣습니다. 카메라 위에 여덟 개, 20미터를 미치도록.
       무엇이든 그것을 읽기만 한다면 모든 슬롯을 차지하고 보이는 모든 벽을 밝힐 자리에
       놓고서, 개수가 그대로일 것과 프레임이 단 한 픽셀도 바뀌지 않을 것을 요구합니다.

       같은 사실의 나머지 절반, 곧 등이 *정점*에도 도달하지 않는다는 것은 leveltest와
       tracetest의 몫입니다. 이 파일에는 볼 정점이 없기 때문입니다. 어느 절반도 스스로를
       드러내지 않습니다. 두 번 밝혀진 방은 밝아 보이고 한 번 밝혀진 방은 멀쩡해 보이며, 어느
       배치가 적용되고 있는지는 그 쌍만이 말합니다. */
    printf("\n  --- a lamp put into a level lights nothing ---\n");

    clear_state(&w);
    unsigned bare = frame_hash(&w, &scene, 0);
    printf("      the level declares %d lamps; the shader was given %d\n",
           w.level.n_lights, rd_light_count());
    ok(w.level.n_lights == 0, "no shipped level declares a lamp any more");
    ok(rd_light_count() == 0, "and nothing occupies a light slot");

    int saved = w.level.n_lights;
    for (int i = 0; i < RD_MAX_LIGHTS && w.level.n_lights < LVL_MAX_LIGHTS; i++) {
        Light *L = &w.level.lights[w.level.n_lights++];
        L->x = (short)(w.player.pos.x * 100.0f);
        L->y = (short)(w.player.pos.y * 100.0f);
        L->z = (short)(w.player.pos.z * 100.0f);
        L->radius = 2000;              /* twenty metres: would light plenty */
        L->r = L->g = L->b = 255;
        L->power = 100;
    }
    unsigned lamped = frame_hash(&w, &scene, 0);
    printf("      %d lamps placed on the camera; the shader was given %d\n",
           w.level.n_lights, rd_light_count());
    ok(rd_light_count() == 0, "lamps on top of the camera still take no slot");
    ok(lamped == bare, "and change no pixel of the frame");

    w.level.n_lights = saved;

    /* --- and the slots are not merely unusable ---------------------------
       Every check above passes just as well when nothing can ever fill a slot,
       which is a state this code has been in before: rd_lights(0,0,0) once sat
       at the end of the world pass and emptied the array every frame, so "no
       lamp occupies a slot" was true because NOTHING did. That is a weaker
       fact than it reads as, and it held for as long as the feature was
       missing.

       Putting a grenade in the air separates the two.

       그리고 슬롯이 단지 *채울 수 없는* 것이 아님을 확인합니다. 위의 모든 검사는 슬롯을 채울
       수 있는 것이 아예 없을 때에도 똑같이 통과하며, 이 코드는 전에 그 상태에 있었습니다.
       rd_lights(0,0,0)이 월드 패스 끝에 앉아 매 프레임 배열을 비웠으므로, "어떤 등도 슬롯을
       차지하지 않는다"는 *아무것도* 차지하지 않았기 때문에 참이었습니다. 읽히는 것보다 약한
       사실이며, 기능이 없는 동안 내내 유지되었습니다.

       유탄을 공중에 띄우면 둘이 갈라집니다. */
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
    ok(rd_light_count() == 1, "a projectile in flight does occupy one");

    /* --- and the light it occupies the slot with is ITS OWN COLOUR --------
     *
     * ENGLISH
     * -------
     * THE CHECK ABOVE PASSES FOR A WHITE LIGHT, and for a light that lands on
     * nothing. It asks whether a slot was filled. proj.h's colour table is a
     * LEGEND -- "A LEGEND, NOT DECORATION" is its own phrase -- and a legend
     * that reaches the screen as the same pale wash for every entry has
     * stopped being one: a grenade is orange, a monster's bolt is green, and
     * the player is meant to read what is coming at them off the light.
     *
     * It stopped being one. `tint` mixed the dynamic lights first and the
     * baked sun over the top of them at a weight that reaches 1 on a sunlit
     * surface, so in daylight the last word on hue was always the sky, and
     * every projectile in the game lit the room the colour of the sky.
     *
     * MEASURED AS A LEAN, not as brightness. Luminance is quantised into five
     * levels and a sunlit surface saturates, so "the light made things
     * brighter" is a claim the renderer often cannot make -- the light is
     * there, and the band it lands in was already the top one. The channel
     * that survives is hue, so that is the channel this asks about.
     *
     * 한국어
     * ------
     * *위의 검사는 흰 광원에 대해서도 통과하고*, 아무 데도 닿지 않는 광원에 대해서도
     * 통과합니다. 슬롯이 채워졌는지를 물을 뿐입니다. proj.h의 색표는 *범례*이며 "장식이
     * 아니라 범례"가 그 파일 자신의 표현입니다. 모든 항목이 같은 희끄무레한 얼룩으로 화면에
     * 닿는 범례는 범례이기를 그만둔 것입니다. 유탄은 주황이고 몬스터의 탄환은 녹색이며,
     * 플레이어는 자기에게 오는 것을 그 빛으로 읽게 되어 있습니다.
     *
     * 그것은 범례이기를 그만두었습니다. `tint`가 동적 광원을 먼저 섞고 구워진 태양을 그 위에,
     * 햇빛 표면에서 1에 닿는 가중치로 덮었습니다. 그래서 낮에 색조에 대한 마지막 말은 언제나
     * 하늘이었고, 게임의 모든 투사체가 방을 하늘색으로 밝혔습니다.
     *
     * *밝기가 아니라 기울기로 잽니다.* 휘도는 다섯 단계로 양자화되고 햇빛 표면은 포화하므로
     * "광원이 밝게 만들었다"는 것은 렌더러가 자주 할 수 없는 주장입니다. 광원은 거기 있고,
     * 그것이 닿은 밴드는 이미 맨 위였습니다. 살아남는 채널은 색조이며, 그래서 이 검사는 그
     * 채널에 대해 묻습니다. */
    clear_state(&w);
    proj_reset(&w.pools);
    frame_hash(&w, &scene, 0);
    float dark_r = lean(0), dark_b = lean(2);

    proj_fire(&w.pools, w.player.pos, v3f(0.0f, 0.0f, -1.0f),
              12.0f, 0.0f, 20, 2.0f, 3.0f);
    /* FLOWN OUT IN FRONT before the frame is taken. A grenade fired from
       the eye is AT the eye, and its own sprite then fills the near plane
       -- the first cut of this measured that sprite rather than the light
       it throws, and read the frame as five parts cooler. Half a second
       puts it six metres down the room, lighting a wall the camera can
       see.
       *프레임을 찍기 전에 앞으로 날려 보냅니다.* 눈에서 발사된 유탄은 눈에 있고, 그
       스프라이트가 근평면을 채웁니다. 이 검사의 첫 판은 그것이 던지는 빛이 아니라 그
       스프라이트를 쟀고, 프레임을 다섯 만큼 차가워진 것으로 읽었습니다. 0.5초면
       방 안쪽 6미터에 놓여 카메라가 보는 벽을 밝힙니다. */
    for (int i = 0; i < 30; i++) proj_update(&w.pools, &w.level, 1.0f / 60.0f);
    frame_hash(&w, &scene, 0);
    float lit_r = lean(0), lit_b = lean(2);

    printf("      red lean %.2f -> %.2f, blue lean %.2f -> %.2f\n",
           dark_r, lit_r, dark_b, lit_b);
    float hue_shift = (lit_r - dark_r) - (lit_b - dark_b);
    printf("      hue shift %.2f (a colourless light scores 0)\n", hue_shift);
    ok(lit_r > dark_r, "a grenade's light leaves the room warmer");

    /* HALF A UNIT, and the number is calibrated rather than picked: on the
       shader this replaced the same grenade in the same fixture scored
       0.30, and it scores 0.67 now. A colourless light of any brightness
       scores 0 by construction, because `lean` is signed against the other
       two channels.
       WHAT THIS FIXTURE DOES NOT TEST, and it is worth saying: it has no
       sun to speak of, so almost all of the difference here is HUE_GAIN.
       The other half of the fix -- laying the sun's hue down BEFORE the
       lights instead of over them -- only shows where `vLit` is large, and
       that is a lit map rather than a box. Measured there instead: on
       lqdm4 a green light and an orange one at the same point differed by
       4.6 parts in 255 before and 12.4 after.
       *0.5이며, 그 수는 고른 것이 아니라 보정된 것입니다.* 이것이 대체한 셰이더에서 같은
       픽스처의 같은 유탄이 0.30을 기록했고 지금은 0.67입니다. 색 없는 광원은 아무리
       밝아도 구조적으로 0점입니다. `lean`이 나머지 두 채널에 대해 부호를 갖기
       때문입니다.
       *이 픽스처가 검사하지 *못하는* 것*도 적어 둘 값어치가 있습니다. 이곳에는 이렇다 할
       태양이 없으므로 여기 차이의 거의 전부는 HUE_GAIN입니다. 수정의 나머지 절반(태양의
       색조를 광원들 위가 아니라 *앞에* 까는 것)은 `vLit`이 큰 곳에서만 드러나며, 그것은
       상자가 아니라 밝혀진 맵입니다. 그곳에서 따로 쟀습니다. lqdm4에서 같은 지점의 녹색
       광원과 주황 광원이 이전에는 255분의 4.6, 이후에는 12.4만큼 달랐습니다. */
    ok(hue_shift > 0.5f,
       "and warmer the way its own colour says, not merely brighter");

    /* --- an artifact says which one it is, on the whole screen -------------
     *
     * ENGLISH
     * -------
     * THE READOUT IS THE POINT, not the effect. `steptest` already proves all
     * three powerups DO their three things; what nothing asked is whether the
     * player can tell WHICH is running without looking away from the fight.
     * There was a countdown for that, three digits above the health -- except
     * that at ::HUD_TEXT_SIZE a row of 1.15 is on top of the health, and a
     * number is a thing you have to look at anyway.
     *
     * Quake's answer is a colour over the whole frame, and a colour is read
     * without being looked at. So the check is the one the countdown could
     * never have passed: does the FRAME change, and does it change the way this
     * particular artifact says?
     *
     * Measured as a lean toward one channel against the mean of the other two,
     * so a wash that merely brightened or dimmed would score zero. Blue for the
     * quad and green for the aegis are Quake's own `V_CalcPowerupCshift`
     * colours; the shadow is grey and correctly scores nothing on any single
     * channel, which is why it is checked by how much it moves the frame at all
     * rather than by which way.
     *
     * 한국어
     * ------
     * *요점은 효과가 아니라 표시입니다.* `steptest`는 이미 파워업 셋이 각자의 일을 *한다*는
     * 것을 증명합니다. 아무도 묻지 않은 것은 플레이어가 전투에서 눈을 떼지 않고 *어느 것이*
     * 돌고 있는지 알 수 있는가입니다. 그것을 위한 카운트다운이 있었습니다. 체력 위의 세 자리
     * 숫자였는데, ::HUD_TEXT_SIZE 단위에서 1.15줄은 체력 *위*이고, 숫자는 어차피 들여다봐야
     * 하는 것입니다.
     *
     * 퀘이크의 답은 프레임 전체에 깔리는 색이며, 색은 들여다보지 않고 읽힙니다. 그래서 이 검사는
     * 카운트다운이 결코 통과할 수 없었을 그것입니다. *프레임이 바뀌는가, 그리고 이 아티팩트가
     * 말하는 방향으로 바뀌는가.*
     *
     * 나머지 두 채널의 평균에 대한 한 채널의 기울기로 재므로, 그저 밝아지거나 어두워지는 물듦은
     * 0점입니다. 쿼드의 파랑과 아이기스의 초록은 퀘이크 자신의 `V_CalcPowerupCshift` 색입니다.
     * 그림자는 회색이라 어느 단일 채널로도 옳게 0점이며, 그래서 어느 쪽인지가 아니라 프레임을
     * 얼마나 움직이는지로 검사합니다.
     */
    clear_state(&w);
    frame_hash(&w, &scene, 0);
    float base_b = lean(2), base_g = lean(1);
    unsigned bare_hud = frame_hash(&w, &scene, 0);

    w.player.power[PW_QUAD] = PLAYER_POWER_TIME;
    frame_hash(&w, &scene, 0);
    float quad_b = lean(2);
    w.player.power[PW_QUAD] = 0.0f;

    w.player.power[PW_AEGIS] = PLAYER_POWER_TIME;
    frame_hash(&w, &scene, 0);
    float aegis_g = lean(1);
    w.player.power[PW_AEGIS] = 0.0f;

    w.player.power[PW_SHADOW] = PLAYER_POWER_TIME;
    unsigned shadow_hud = frame_hash(&w, &scene, 0);
    w.player.power[PW_SHADOW] = 0.0f;

    printf("      blue lean %.2f -> %.2f (quad), green %.2f -> %.2f (aegis)\n",
           base_b, quad_b, base_g, aegis_g);
    ok(quad_b - base_b > 1.0f,  "the quad turns the whole frame blue");
    ok(aegis_g - base_g > 0.5f, "the aegis turns it green, and less hard");
    ok(shadow_hud != bare_hud,  "and the ring of shadows greys it");
    /* --- the floaters float, and the walkers do not ----------------------
     *
     * ENGLISH
     * -------
     * A BILLBOARD NAILED TO A FIXED HEIGHT reads as a cardboard cutout, which
     * is what a water spirit and a caster were: one is water and the other is
     * off the floor entirely, and both hung at exactly the height a brute
     * stands at.
     *
     * TWO FRAMES OF THE SAME SCENE, differing only in ::Enemy::anim. That is
     * the phase the bob is driven from, so the picture must move -- and for a
     * creature that does NOT float it must not, which is the half that makes
     * this a test rather than a proof that something somewhere changed.
     *
     * The state is ::E_IDLE on purpose. ::E_CHASE picks its frame off the same
     * clock, so a chasing monster's picture would differ between the two
     * hashes because the WALK CYCLE moved, and the check would pass with the
     * bob deleted.
     *
     * 한국어
     * ------
     * *고정된 높이에 못 박힌 빌보드는 판지 조각으로 읽히며*, 물 정령과 캐스터가 바로 그랬습니다.
     * 하나는 물이고 다른 하나는 바닥에서 아예 떨어져 있는데, 둘 다 브루트가 서 있는 바로 그
     * 높이에 걸려 있었습니다.
     *
     * *같은 장면의 두 프레임*이며 ::Enemy::anim만 다릅니다. 그것이 떠 있음을 구동하는 위상이므로
     * 그림이 움직여야 합니다. 그리고 *떠 있지 않은* 생물에 대해서는 움직이지 않아야 하며, 그
     * 절반이 이것을 "어딘가에서 무언가 바뀌었다"의 증명이 아니라 테스트로 만듭니다.
     *
     * 상태가 ::E_IDLE인 것은 의도적입니다. ::E_CHASE는 같은 시계에서 프레임을 고르므로, 쫓고
     * 있는 몬스터의 그림은 *걷기 주기*가 움직였기 때문에 두 해시 사이에서 달라지고, 떠 있음을
     * 지워도 검사가 통과합니다. */
    {
        /* Asked of the table rather than spelled here, so the check follows
           the flag if a second creature ever earns it.
           이곳에 적는 대신 표에 물으므로, 두 번째 생물이 이 플래그를 얻으면 검사가 따라갑니다. */
        #define S_UNFLINCHING(k) (mon_stats(mon_type_for(k))->flags & MON_UNFLINCHING)
        static const struct { const char *kind; int floats; } WHO[] = {
            { "water_spirit", 1 },
            { "caster",       1 },
            { "brute",        0 },
        };
        for (int k = 0; k < 3; k++) {
            clear_state(&w);
            enemy_reset(&w.pools);

            Level *lv = &w.level;
            int saved = lv->n_ents;
            Entity *e = &lv->ents[lv->n_ents++];
            for (int i = 0; i < (int)sizeof e->kind; i++) e->kind[i] = 0;
            for (int i = 0; WHO[k].kind[i] && i < (int)sizeof e->kind - 1; i++)
                e->kind[i] = WHO[k].kind[i];
            /* PLACED WHERE THE PLAYER IS STANDING, offset by a few metres and
               retried outward. ::make_monster refuses ground it cannot stand
               on -- a wall, a drop, the arena's lava -- so a fixed offset from
               a spawn point is a coin toss on whichever map this test loads.
               The player's own footing is the one square this scene guarantees.
               *플레이어가 서 있는 자리에 놓고* 몇 미터씩 밀어내며 다시 시도합니다.
               ::make_monster는 설 수 없는 지면을 거절합니다. 벽, 낭떠러지, 투기장의 용암이
               그렇습니다. 그러므로 스폰 지점에서 고정된 거리는 이 테스트가 어느 맵을 여느냐에
               걸린 동전 던지기입니다. 플레이어 자신의 발밑이 이 장면이 보장하는 한 칸입니다. */
            static const float OFF[4][2] = {
                { 0.0f, -4.0f }, { 4.0f, 0.0f }, { 0.0f, 4.0f }, { -4.0f, 0.0f }
            };
            for (int t = 0; t < 4 && enemy_count(&w.pools) < 1; t++) {
                e->x = (short)((w.player.pos.x + OFF[t][0]) * 100.0f);
                e->z = (short)((w.player.pos.z + OFF[t][1]) * 100.0f);
                enemy_spawn_level(&w.pools, lv);
            }
            lv->n_ents = saved;

            if (enemy_count(&w.pools) < 1) { ok(0, "the monster spawned"); continue; }
            Enemy *m = (Enemy *)enemy_at(&w.pools, 0);
            m->state = E_IDLE;

            m->anim = 0.0f;
            unsigned a = frame_hash(&w, &scene, 0);
            m->anim = 1.0f;                    /* a quarter of a bob cycle */
            unsigned b = frame_hash(&w, &scene, 0);

            printf("      %-13s %s between two phases\n", WHO[k].kind,
                   a == b ? "identical" : "moves");
            if (WHO[k].floats)
                ok(a != b, "a floating creature rises and falls");
            else
                ok(a == b, "and one that walks stays where it is put");
            /* --- and the flinch, which is a flash and a shudder ---------
               ::E_HURT used to pick ::SPR_HURT and no creature has a drawing
               for it, so being shot turned a monster back into the generated
               fallback for a quarter of a second. What replaces it has to be
               visible -- and for the brute, visible WITHOUT the shudder: its
               own stats say it cannot be stun-locked, and a wall that rocks
               when you shoot it is not a wall.
               ::Enemy::anim is held still across the two hashes, so the only
               thing that can move the picture is the flinch.
               *그리고 경직은 점멸과 떨림입니다.* ::E_HURT은 ::SPR_HURT를 골랐고 어느 생물에도
               그 그림이 없으므로, 맞는 것은 몬스터를 0.25초 동안 생성된 폴백으로 되돌리는
               일이었습니다. 그것을 대신하는 것은 보여야 하고, 브루트에 대해서는 *떨림 없이*
               보여야 합니다. 자기 수치가 스턴 락에 걸리지 않는다고 말하며, 쏘면 흔들리는 벽은
               벽이 아닙니다.
               두 해시 사이에서 ::Enemy::anim을 고정하므로 그림을 움직일 수 있는 것은
               경직뿐입니다. */
            m->anim  = 0.0f;
            m->flash = 0.0f;
            unsigned calm = frame_hash(&w, &scene, 0);
            m->flash = 1.0f;
            unsigned hit  = frame_hash(&w, &scene, 0);
            printf("      %-13s %s when hit\n", WHO[k].kind,
                   calm == hit ? "does not change" : "changes");
            ok(calm != hit, "a hit changes the picture");

            /* AND WHETHER IT SHUDDERS, which the check above cannot tell: the
               flash alone changes the frame, so every creature passes it.
               Isolated by holding ::Enemy::flash ON and moving the phase. For
               the brute that is the shake and nothing else -- it does not
               float -- so an identical frame is the exemption, proven.
               For a floater the bob moves with the same clock, so a difference
               here would not be evidence of the shudder. That half is left
               unclaimed rather than asserted badly.
               *그리고 떨리는지*이며, 위의 검사는 그것을 가를 수 없습니다. 점멸만으로 프레임이
               바뀌므로 모든 생물이 통과합니다. ::Enemy::flash를 *켠 채로* 위상만 움직여
               떼어 냅니다. 브루트에게 그것은 흔들림뿐입니다. 떠 있지 않으니까요. 그러므로
               같은 프레임이 곧 증명된 면제입니다.
               떠 있는 것에 대해서는 떠 있음이 같은 시계로 움직이므로, 이곳의 차이는 떨림의
               증거가 되지 못합니다. 그 절반은 엉성하게 단언하는 대신 주장하지 않고 둡니다. */
            if (S_UNFLINCHING(WHO[k].kind)) {
                m->flash = 1.0f; m->anim = 0.0f;
                unsigned s0 = frame_hash(&w, &scene, 0);
                m->anim = 0.7f;
                unsigned s1 = frame_hash(&w, &scene, 0);
                printf("      %-13s %s while the flash is lit\n", WHO[k].kind,
                       s0 == s1 ? "holds still" : "moves");
                ok(s0 == s1, "and the brute takes it without rocking");
            }
            m->flash = 0.0f;

        }
        clear_state(&w);
        enemy_reset(&w.pools);
    }
    /* --- an item on a corpse is still on the screen ----------------------
     *
     * ENGLISH
     * -------
     * A MONSTER'S DROP LANDS AT THE MONSTER. ::step_drops tosses it straight up
     * from where the body fell, on purpose -- "a corpse's drop is one item and
     * belongs where the body fell" -- so the item and the corpse share an x,z.
     * Both are camera-facing billboards, which makes their quads parallel
     * planes at the same distance: identical depth, to the bit.
     *
     * PICKUPS ARE DRAWN AFTER ENEMIES and the depth function is the GL default,
     * GL_LESS. A fragment at exactly the depth already written is not less than
     * it, so every pixel of the item that overlapped the corpse was discarded.
     * The reward vanished into the thing that dropped it.
     *
     * THE CHECK IS THAT THE FRAME CHANGES. Draw the corpse alone, then the same
     * corpse with an item at its feet: if the item is being cut away by the
     * depth test the two frames are identical, and that is exactly what the
     * bug looked like from the player's side.
     *
     * 한국어
     * ------
     * *몬스터의 드롭은 몬스터 자리에 내려앉습니다.* ::step_drops가 몸이 쓰러진 자리에서 곧장
     * 위로 던집니다. 의도적이며 "시체의 드롭은 아이템 하나이고 몸이 쓰러진 자리에 속한다"는
     * 이유입니다. 그래서 아이템과 시체는 x,z를 공유합니다. 둘 다 카메라를 향한 빌보드이므로 두
     * 사각형은 평행한 면이고 거리가 같습니다. 비트 단위로 같은 깊이입니다.
     *
     * *픽업은 몬스터보다 나중에 그려지고* 깊이 함수는 GL 기본값 GL_LESS입니다. 이미 쓰인 깊이와
     * 정확히 같은 조각은 그보다 작지 않으므로, 시체와 겹친 아이템의 모든 픽셀이 버려졌습니다.
     * 보상이 그것을 떨어뜨린 것 안으로 사라졌습니다.
     *
     * *검사는 프레임이 바뀌는가입니다.* 시체만 그린 다음, 같은 시체에 아이템을 발치에 두고
     * 그립니다. 아이템이 깊이 검사에 잘리고 있다면 두 프레임은 동일하며, 그것이 플레이어
     * 쪽에서 이 버그가 보이던 모습 그대로입니다.
     */
    {
        clear_state(&w);
        enemy_reset(&w.pools);
        pickup_reset(&w.pools);

        Level *lv = &w.level;
        int saved = lv->n_ents;
        Entity *e = &lv->ents[lv->n_ents++];
        for (int i = 0; i < (int)sizeof e->kind; i++) e->kind[i] = 0;
        const char *K = "brute";
        for (int i = 0; K[i] && i < (int)sizeof e->kind - 1; i++) e->kind[i] = K[i];
        static const float OFF[4][2] = {
            { 0.0f, -4.0f }, { 4.0f, 0.0f }, { 0.0f, 4.0f }, { -4.0f, 0.0f }
        };
        for (int t = 0; t < 4 && enemy_count(&w.pools) < 1; t++) {
            e->x = (short)((w.player.pos.x + OFF[t][0]) * 100.0f);
            e->z = (short)((w.player.pos.z + OFF[t][1]) * 100.0f);
            enemy_spawn_level(&w.pools, lv);
        }
        lv->n_ents = saved;

        if (enemy_count(&w.pools) < 1) { ok(0, "a body to drop on"); }
        else {
            Enemy *m = (Enemy *)enemy_at(&w.pools, 0);
            m->state = E_DEAD;
            m->timer = 0.0f;
            m->anim  = 0.0f;
            /* HOW MANY PIXELS THE ITEM IS WORTH, twice: once with the body
               behind it and once with the body gone. A hash comparison is not
               enough and the first cut of this check was exactly that -- it
               passed with the fix removed, because a brute's corpse does not
               cover the whole of a medkit and the leftover edge changed the
               frame either way. What the bug did was cut the OVERLAP, so the
               overlap is what has to be counted.
               *아이템이 몇 픽셀 값어치인지를 두 번* 잽니다. 뒤에 몸이 있을 때와 몸이 없을
               때입니다. 해시 비교로는 부족하고 이 검사의 첫 판이 정확히 그것이었습니다.
               수정을 빼도 통과했습니다. 브루트의 시체가 메드킷 전체를 덮지 않으므로 남은
               가장자리가 어느 쪽이든 프레임을 바꾸었기 때문입니다. 버그가 한 일은 *겹친
               부분*을 잘라 내는 것이었으므로, 세어야 하는 것도 겹친 부분입니다. */
            frame_hash(&w, &scene, 0);
            keep_frame();
            pickup_toss(&w.pools, PK_HEALTH, m->pos, v3f(0, 0, 0));
            frame_hash(&w, &scene, 0);
            int over_body = px_changed();

            /* The same item with nothing behind it. */
            m->active = 0;
            frame_hash(&w, &scene, 0);
            keep_frame();
            m->active = 1;
            pickup_reset(&w.pools);
            m->active = 0;
            frame_hash(&w, &scene, 0);
            keep_frame();
            pickup_toss(&w.pools, PK_HEALTH, m->pos, v3f(0, 0, 0));
            frame_hash(&w, &scene, 0);
            int in_clear = px_changed();
            m->active = 1;

            printf("      the item is %d px over the body, %d px in the clear\n",
                   over_body, in_clear);
            ok(in_clear > 0, "the item is worth pixels at all");
            ok(over_body * 4 >= in_clear * 3,
               "and a corpse behind it does not cut most of it away");
        }
        clear_state(&w);
        enemy_reset(&w.pools);
        pickup_reset(&w.pools);
    }

    clear_state(&w);
    proj_reset(&w.pools);

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

    /* --- 6a2. and the explosion lights what the projectile stopped lighting
       THE CHECK ABOVE IS EXACTLY THE FAULT THIS ONE IS ABOUT. It puts a
       grenade in the air, sees one slot filled, takes the grenade away and
       sees the slot empty -- and for one release that WAS the whole story:
       ::detonate cleared `active`, the light went with it, and the frame in
       which the room should have been at its brightest was the frame in which
       it went back to ambient. The pair of checks above passed the entire time,
       because "the set empties once the round is gone" is true either way.

       Placed by hand rather than by detonating something, and the reason is
       what this file is: ::scene_frame is under test here, not ::proj_update.
       tools\weapontest.c is where a grenade is made to go off and the record it
       leaves is inspected; this asks the one question that needs a context --
       whether ::scene_lights hands that record to the shader.

       위의 검사가 바로 이 검사가 다루는 결함 자체입니다. 유탄을 공중에 띄우고 슬롯 하나가
       차는 것을 보고, 유탄을 치우고 슬롯이 비는 것을 봅니다. 그리고 한 판 동안 그것이 이야기의
       전부였습니다. ::detonate가 `active`를 지웠고 빛도 함께 사라졌으며, 방이 가장 밝아야 할
       프레임이 방이 주변광으로 돌아가는 프레임이었습니다. 위의 두 검사는 그동안 내내
       통과했습니다. "탄이 사라지면 집합도 빈다"는 어느 쪽이든 참이기 때문입니다.

       무언가를 터뜨리는 대신 손으로 놓는 이유는 이 파일이 무엇인지에 있습니다. 이곳에서
       시험되는 것은 ::proj_update가 아니라 ::scene_frame입니다. 유탄을 실제로 터뜨리고 그것이
       남긴 기록을 살피는 곳은 tools\weapontest.c이며, 이곳은 컨텍스트가 필요한 하나의 질문만
       묻습니다. ::scene_lights가 그 기록을 셰이더에 건네는가. */
    clear_state(&w);
    proj_reset(&w.pools);
    proj_flash(&w.pools, w.player.pos, PROJ_BLAST_RADIUS, 1.0f, FLASH_BLAST, -1, 0);
    frame_hash(&w, &scene, 0);
    printf("      with one detonation and no projectile the shader was given %d\n",
           rd_light_count());
    ok(rd_light_count() == 1,
       "a blast lights the frame after the round that made it is gone");

    /* Twice its own life, so nothing is left to argue about. ::proj_reset
       would clear the pool too, but ageing it out is what proves the slot is
       released by the flash ENDING rather than by the pool being emptied.
       자기 수명의 두 배만큼 진행시키므로 논쟁의 여지가 없습니다. ::proj_reset으로도 풀을
       비울 수 있지만, 나이를 먹여 없애는 것이야말로 슬롯이 풀이 비워져서가 아니라 섬광이
       *끝나서* 반납된다는 것을 증명합니다. */
    proj_flash_update(&w.pools, PROJ_FLASH_TIME * 2.0f);
    frame_hash(&w, &scene, 0);
    ok(rd_light_count() == 0, "and the room is dark again once it is over");

    /* --- 6b. a bolt is coloured by whoever cast it ------------------------
       ONE FIELD, TWO READERS, AND NEITHER OF THEM IS OBLIGED TO AGREE.
       ::Shot::type picks a row of LIGHT_COL_SHOT twice over: ::scene_lights
       uses it for the light the bolt throws on the wall, ::scene_draw_shots
       for the glow the bolt itself is drawn as. They are separate functions
       reading the same table, which is exactly the arrangement that drifts --
       and the failure it drifts into is the one that table's own note names,
       a wall lit violet with a blue bolt in front of it.

       So this changes the type and NOTHING else. Same position, same life,
       same damage, same frame -- so a difference in the picture can only have
       come through the colour lookup. Caster against maw because those two
       rows are the furthest apart in the table, cold blue against warm
       crimson: a comparison between neighbouring hues could come out equal
       after the resolve pass quantises, and prove nothing.

       *필드 하나, 읽는 곳 둘, 그리고 그 둘은 일치할 의무가 없습니다.* ::Shot::type은
       LIGHT_COL_SHOT의 행을 두 번 고릅니다. ::scene_lights는 볼트가 벽에 던지는 빛을 위해,
       ::scene_draw_shots는 볼트 자신이 그려지는 발광을 위해 씁니다. 같은 표를 읽는 별개의
       함수이며, 바로 그런 배치가 어긋납니다. 어긋나서 도달하는 실패가 그 표 자신의 설명이
       지목하는 것입니다. 보라색으로 밝혀진 벽 앞의 파란 볼트.

       그래서 이 검사는 종류만 바꾸고 나머지는 아무것도 바꾸지 않습니다. 같은 위치, 같은 수명,
       같은 피해량, 같은 프레임이므로, 그림의 차이는 색 조회를 통해서만 올 수 있습니다.
       캐스터와 아귀인 이유는 표에서 그 둘의 행이 가장 멀기 때문입니다. 차가운 파랑과 따뜻한
       진홍입니다. 이웃한 색조끼리의 비교는 해상 패스가 양자화한 뒤 같아질 수 있고, 그러면
       아무것도 증명하지 못합니다. */
    printf("\n  --- a bolt is coloured by whoever cast it ---\n");

    clear_state(&w);
    {
        /* Placed by hand rather than fired: shot_fire is static to enemy.c and
           reaching it would mean standing a caster up, walking it into range
           and waiting out a wind-up -- three things that can fail for reasons
           that are enemytest's, not this file's.
           발사하지 않고 손으로 놓습니다. shot_fire는 enemy.c에 static이고 그것에 닿으려면
           캐스터를 세우고 사거리 안으로 걸어 들어오게 한 뒤 예비 동작을 기다려야 하는데, 그
           셋은 이 파일이 아니라 enemytest의 몫인 이유로 실패할 수 있습니다. */
        float cy = cosf(w.yaw), sy = sinf(w.yaw);
        v3 fwd = v3f(-sy, 0.0f, -cy);

        Shot *sh = &w.pools.enemy.shots[0];
        sh->pos    = v3add(w.player.pos, v3scale(fwd, 3.0f));
        sh->vel    = v3f(0.0f, 0.0f, 0.0f);
        sh->life   = 3.0f;
        sh->damage = 10;
        sh->active = 1;

        sh->type = MON_CASTER;
        unsigned as_caster = frame_hash(&w, &scene, 0);
        ok(rd_light_count() == 1, "the hand-placed bolt lights the frame");

        sh->type = MON_MAW;
        unsigned as_maw = frame_hash(&w, &scene, 0);

        ok(as_caster != as_maw,
           "and the same bolt from a different caster is a different frame");

        sh->active = 0;
        sh->life   = 0.0f;
    }
    frame_hash(&w, &scene, 0);
    ok(rd_light_count() == 0, "the bolt is gone again");

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
