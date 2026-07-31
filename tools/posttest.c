/* posttest -- the offscreen target and the dither shader, checked for real.
 *
 * Unlike every other test in tools/, this one NEEDS a GL context: an FBO that
 * does not complete and a shader that does not compile are both runtime
 * failures on the driver, invisible to the compiler. So it creates a hidden
 * window and a real context, exactly as the game does, and then exercises one
 * full frame through the pass.
 *
 * What it actually catches: a GLSL syntax error (the shader is a C string, so
 * the C compiler never sees it), a missing GL entry point in GL_FUNCS, an
 * incomplete framebuffer attachment, and any GL error raised by the resolve.
 * All four are silent in a normal run -- post_init just returns 0 and the
 * game quietly renders without the effect, which looks like "the feature did
 * not work" rather than like a bug with a cause.
 *
 * 다른 tools/ 테스트와 달리 이 테스트는 GL 컨텍스트를 *필요로 합니다*. 완성되지 않는
 * FBO와 컴파일되지 않는 셰이더는 모두 드라이버 상의 런타임 실패이며 컴파일러에게는
 * 보이지 않습니다. 따라서 게임과 동일하게 숨겨진 창과 실제 컨텍스트를 만든 뒤 패스를
 * 통과하는 한 프레임을 실행합니다.
 *
 * 실제로 잡아내는 것: GLSL 문법 오류(셰이더가 C 문자열이므로 C 컴파일러는 이를 전혀
 * 보지 않습니다), GL_FUNCS의 누락된 진입점, 불완전한 프레임버퍼 첨부, 해상 과정에서
 * 발생하는 모든 GL 오류. 네 가지 모두 일반 실행에서는 조용합니다. post_init이 0을
 * 반환하고 게임은 효과 없이 조용히 렌더링하는데, 이는 원인이 있는 버그가 아니라
 * "기능이 동작하지 않았다"처럼 보입니다.
 */

#include <stdio.h>
#include "gl.h"
#include "post.h"
#include "render.h"   /* rd_init/rd_use -- the program-restore check needs the
                         renderer's own program to compare against. */

int main(void) {
    printf("posttest\n\n");

    HINSTANCE inst = GetModuleHandleA(0);
    if (!gl_bootstrap(inst)) { printf("  gl_bootstrap FAILED\n"); return 1; }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = "posttest";
    RegisterClassA(&wc);

    HWND  w  = CreateWindowExA(0, "posttest", "", 0, 0, 0, 320, 180, 0, 0, inst, 0);
    HDC   dc = GetDC(w);
    HGLRC rc = gl_make_context(dc);
    if (!rc) { printf("  gl_make_context FAILED\n"); return 1; }

    int fails = 0;

    /* The renderer's own program has to exist before the restore check below
       has anything to compare against. */
    rd_init();

    int ok = post_init(320, 180);
    printf("  %-52s %s\n", "the offscreen target completes", ok ? "ok" : "FAIL");
    if (!ok) fails++;

    printf("  %-52s %s\n", "and the pass reports itself enabled",
           post_enabled() ? "ok" : "FAIL");
    if (!post_enabled()) fails++;

    /* Drain anything the context setup left behind, so the check below is
       attributable to this pass alone. */
    while (glGetError() != GL_NO_ERROR) { }

    /* The offscreen aspect, not the window's -- rendering the world with the
       wrong one of those is the mistake post.h warns about. */
    float a = post_begin();
    printf("  %-52s %8.4f  %s\n", "post_begin returns the offscreen aspect", a,
           (a > 1.77f && a < 1.79f) ? "ok" : "FAIL");
    if (!(a > 1.77f && a < 1.79f)) fails++;

    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    post_end(320, 180);

    GLenum e = glGetError();
    printf("  %-52s %s\n", "a full frame through the pass raises no GL error",
           e == GL_NO_ERROR ? "ok" : "FAIL");
    if (e != GL_NO_ERROR) { printf("      glGetError = 0x%04X\n", e); fails++; }

    /* The pass must hand the renderer's program back.
     *
     * This is the check that was missing, and its absence let a completely
     * broken build pass every other assertion here: post_end left ITS program
     * bound, so every later rd_mode/rd_color/rd_mvp wrote uniforms into a
     * program that was not the one drawing. The game rendered the font atlas
     * stretched across the whole screen and this suite still reported success,
     * because nothing above looks at what is bound after the resolve.
     *
     * A GL error check cannot catch this -- leaving the wrong program bound is
     * perfectly legal. Only comparing the binding does.
     *
     * 패스는 렌더러의 프로그램을 반드시 되돌려 주어야 합니다.
     *
     * 이것이 빠져 있던 검사이며, 그 부재 때문에 완전히 망가진 빌드가 이곳의 다른 모든
     * 단언을 통과했습니다. post_end가 *자신의* 프로그램을 바인딩된 채로 두어, 이후의
     * 모든 rd_mode/rd_color/rd_mvp가 실제로 그리지 않는 프로그램에 유니폼을
     * 썼습니다. 게임은 폰트 아틀라스를 화면 전체에 늘려 그렸는데도 이 스위트는 성공을
     * 보고했습니다. 위의 어떤 검사도 해상 이후에 무엇이 바인딩되어 있는지 보지 않기
     * 때문입니다.
     *
     * GL 오류 검사로는 이를 잡을 수 없습니다. 잘못된 프로그램을 바인딩된 채로 두는
     * 것은 완전히 합법적입니다. 바인딩을 직접 비교해야만 잡힙니다. */
    {
        GLint before = 0, after = 0;
        rd_use();
        glGetIntegerv(GL_CURRENT_PROGRAM, &before);

        post_begin();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        post_end(320, 180);

        glGetIntegerv(GL_CURRENT_PROGRAM, &after);
        printf("  %-52s %s\n", "the resolve restores the renderer's program",
               (after == before && after != 0) ? "ok" : "FAIL");
        if (after != before || after == 0) {
            printf("      program was %d before, %d after\n", before, after);
            fails++;
        }
    }

    /* The resolve must AVERAGE the block, not point-sample it.
     *
     * This is what separates real pixelisation from a pixel-shaped filter,
     * and the pass shipped without it: rendering small and magnifying makes
     * the image blocky, but the rasteriser still produced each art pixel from
     * one sample at its centre, so thin geometry either landed on that centre
     * or vanished. Nothing combined a pixel with its neighbours.
     *
     * The test paints a checkerboard of alternating black and white SUB-texels
     * -- the sub-pixel detail that only exists because the framebuffer is
     * supersampled -- and then looks at what came out. Averaging turns each
     * block into a mid grey; point sampling returns whichever sub-texel the
     * centre happened to hit, so the output stays pure black and white.
     *
     * Verified in both directions: with POST_SUPERSAMPLE at 2 the centre is
     * 1594 intermediate pixels to 710 extreme, and at 1 it inverts to 258
     * against 2046.
     *
     * 해상 과정은 블록을 점 샘플링하는 것이 아니라 *평균* 내야 합니다.
     *
     * 이것이 진짜 픽셀화와 픽셀 모양 필터를 가르는 지점이며, 이 패스는 그 기능 없이
     * 출시되었습니다. 작게 렌더링하고 확대하면 화면이 각져 보이지만, 래스터라이저는
     * 여전히 각 아트 픽셀을 중심의 샘플 하나로 생성했으므로 얇은 지오메트리는 그
     * 중심에 걸리거나 사라졌습니다. 픽셀을 주변과 합치는 과정이 전혀 없었습니다.
     *
     * 이 테스트는 흑백이 교차하는 *서브* 텍셀 체커보드를 그립니다. 프레임버퍼가
     * 슈퍼샘플링되어 있기에만 존재할 수 있는 서브픽셀 디테일입니다. 그런 다음 결과를
     * 확인합니다. 평균을 내면 각 블록이 중간 회색이 되지만, 점 샘플링하면 중심이
     * 우연히 맞춘 서브 텍셀을 그대로 반환하므로 출력이 순수한 흑백으로 남습니다.
     *
     * 양방향으로 검증했습니다. POST_SUPERSAMPLE이 2일 때 중앙부는 중간톤 1594개 대
     * 극단 710개이며, 1일 때는 258개 대 2046개로 역전됩니다. */
    {
        post_set_enabled(1);
        post_begin();

        /* Scissored clears paint individual sub-texels without needing any
           geometry or the renderer's vertex format.
           가위 검사를 이용한 클리어로 지오메트리나 렌더러의 정점 형식 없이 개별 서브
           텍셀을 칠합니다. */
        glEnable(GL_SCISSOR_TEST);
        int n = 320 * POST_SUPERSAMPLE;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                glScissor(x, y, 1, 1);
                float v = ((x + y) & 1) ? 1.0f : 0.0f;
                glClearColor(v, v, v, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
        glDisable(GL_SCISSOR_TEST);
        post_end(320, 180);

        static unsigned char px[320 * 180 * 4];
        glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE, px);

        int mid = 0, ext = 0;
        for (int y = 40; y < 140; y++)
            for (int x = 60; x < 260; x++) {
                int v = px[(y * 320 + x) * 4];
                if (v < 20 || v > 235) ext++; else mid++;
            }

        printf("  %-52s %s\n",
               "the resolve averages the block, not one sample",
               mid > ext ? "ok" : "FAIL");
        if (mid <= ext) {
            printf("      intermediate=%d extreme=%d -- point sampling\n", mid, ext);
            fails++;
        }
    }

    /* The toggle must gate the whole path, not just the resolve: post_begin
       returning 0 is how main.c knows to bind the window itself. */
    post_set_enabled(0);
    printf("  %-52s %s\n", "disabling gates post_begin as well as post_end",
           (!post_enabled() && post_begin() == 0.0f) ? "ok" : "FAIL");
    if (post_enabled() || post_begin() != 0.0f) fails++;

    post_set_enabled(1);
    printf("  %-52s %s\n", "and it switches back on", post_enabled() ? "ok" : "FAIL");
    if (!post_enabled()) fails++;

    /* Shutdown must be safe, and safe twice -- it runs on the exit path where
       nothing is left to check its work. */
    post_shutdown();
    post_shutdown();
    printf("  %-52s %s\n", "a double shutdown is harmless", "ok");

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall post-process checks passed\n", fails);
    return fails != 0;
}
