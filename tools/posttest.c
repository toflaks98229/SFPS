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

    /* --- the CRT scanline actually darkens output rows ---------------------
       Measured by rendering the SAME flat frame twice, once with the scanline
       off and once with it on, and comparing the two. The obvious test --
       "are odd rows darker than even ones" -- does not work here and passing
       it would have meant nothing: the 8x8 Bayer matrix has a row-to-row
       threshold difference of 16 built into it, so a flat grey already comes
       out with a 24% even/odd split before any scanline exists. That check
       reported 23.9% and passed with the scanline disabled.

       Comparing two renders isolates it, because everything except uScan is
       identical between them.
       같은 평탄한 프레임을 주사선 끄고/켜고 두 번 렌더링해 비교합니다. 떠오르는 검사인
       "홀수 행이 짝수 행보다 어두운가"는 이곳에서 동작하지 않으며 통과해도 의미가
       없었습니다. 8x8 Bayer 행렬이 행 간 임계값 차이 16을 내장하고 있어, 주사선이 존재하기
       전에도 평탄한 회색이 24%의 짝/홀 차이를 냅니다. 주사선을 끈 상태에서 그 검사는
       23.9%를 보고하며 통과했습니다. */
    {
        static unsigned char off[320 * 180 * 4];
        static unsigned char on [320 * 180 * 4];
        float saved = post_scanline();

        post_set_enabled(1);

        post_set_scanline(0.0f);
        post_begin();
        glClearColor(0.6f, 0.6f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        post_end(320, 180);
        glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE, off);

        post_set_scanline(0.5f);      /* exaggerated, so the signal is unambiguous */
        post_begin();
        glClearColor(0.6f, 0.6f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        post_end(320, 180);
        glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE, on);

        post_set_scanline(saved);

        /* Compared as MEAN brightness rather than per pixel, because the grain
           is temporal: uTime advances between the two renders, so a few
           thousand pixels differ purely because their grain sample changed.
           A per-pixel "nothing may get brighter" assertion counted 6686 of
           those and failed on them, which was the test being wrong rather than
           the shader.

           The mean cancels the grain -- it is symmetric around zero -- and
           leaves exactly what is being asked about: half the rows lose `depth`,
           so the frame should darken by about depth/2.

           픽셀 단위가 아니라 *평균* 밝기로 비교합니다. 그레인이 시간적이기 때문입니다.
           두 렌더 사이에 uTime이 증가하므로 수천 개의 픽셀이 순전히 그레인 표본이
           달라졌다는 이유로 달라집니다. 픽셀 단위로 "아무것도 밝아져서는 안 된다"고
           단언하면 그중 6686개가 걸려 실패하는데, 이는 셰이더가 아니라 테스트가 틀린
           것입니다. 평균은 그레인을 상쇄하며(0을 중심으로 대칭), 묻고자 하는 것만
           남깁니다. 행의 절반이 `depth`만큼 어두워지므로 프레임은 약 depth/2만큼
           어두워져야 합니다. */
        double sum_off = 0.0, sum_on = 0.0;
        int n = 0;
        for (int y = 20; y < 160; y++)
            for (int x = 40; x < 280; x++) {
                int i = (y * 320 + x) * 4;
                sum_off += off[i];
                sum_on  += on[i];
                n++;
            }
        double mo = n ? sum_off / n : 0.0;
        double mn = n ? sum_on  / n : 0.0;
        double drop = mo > 0.0 ? (mo - mn) / mo : 0.0;

        /* depth 0.5 on half the rows is a 25% mean drop. The window is wide
           because the dither quantises, so the exact figure depends on which
           side of a threshold each band lands. */
        printf("  %-52s %5.1f%%  %s\n",
               "the scanline darkens the frame by about depth/2",
               drop * 100.0,
               (drop > 0.12 && drop < 0.40) ? "ok" : "FAIL");
        if (!(drop > 0.12 && drop < 0.40)) {
            printf("      mean %.1f -> %.1f (%.1f%%), wanted 12-40%%\n",
                   mo, mn, drop * 100.0);
            fails++;
        }
    }

    /* --- procedural materials are quantised to texels ----------------------
     *
     * ENGLISH
     * -------
     * The procedural shaders are continuous functions of the UV, so left alone
     * they have no resolution at all -- which is the wrong property for a
     * pixel-art presentation, and why RD_PROC_TEXELS exists.
     *
     * Checked by rendering the same quad twice with the UV shifted by a
     * FRACTION of one texel between them, and asserting the two frames are
     * identical. A quantised pattern is constant within a texel, so a
     * sub-texel move must change nothing; a continuous one moves with any
     * shift however small. See the note at the render below for why counting
     * colours -- the obvious approach, and the one tried first -- cannot
     * answer this at all.
     *
     * RD_SWATCH is used rather than RD_WORLD so nothing but the material is
     * measured -- no lighting bands, no fog, no eye position. The post pass is
     * off for the same reason: the dither would quantise the output regardless
     * of whether the material was quantised, and the test would pass either
     * way.
     *
     * 한국어
     * ------
     * 절차적 셰이더는 UV의 연속 함수이므로 그대로 두면 해상도가 전혀 없습니다. 이는 픽셀
     * 아트 표현에 잘못된 성질이며, RD_PROC_TEXELS가 존재하는 이유입니다.
     *
     * 같은 사각형을 UV를 텍셀 하나의 *일부*만큼 이동시켜 두 번 렌더링하고, 두 프레임이
     * 동일한지 단언하여 검사합니다. 양자화된 패턴은 텍셀 안에서 일정하므로 텍셀 미만의
     * 이동은 아무것도 바꾸지 않아야 합니다. 연속적인 패턴은 아무리 작은 이동에도 함께
     * 움직입니다. 가장 먼저 시도했던 명백한 접근인 *색 개수 세기*가 왜 이 질문에 전혀
     * 답할 수 없는지는 아래 렌더링 부분의 주석을 참조하십시오.
     *
     * 재질 외에는 아무것도 측정되지 않도록 RD_WORLD가 아닌 RD_SWATCH를 씁니다. 조명
     * 밴딩도, 안개도, 시점 위치도 없습니다. 포스트 패스를 끄는 것도 같은 이유입니다.
     * 디더는 재질의 양자화 여부와 무관하게 출력을 양자화하므로, 그대로 두면 어느 쪽이든
     * 테스트가 통과해 버립니다. */
    {
        int saved_on = post_enabled();
        post_set_enabled(0);          /* measure the material, not the dither */

        rd_use();
        rd_mode(RD_SWATCH);
        rd_snap(0.0f, 0.0f);
        rd_mvp(mat4_ortho(0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f));

        float rgb[3]  = {0.8f, 0.4f, 0.3f};
        float parm[3] = {0.0f, 0.0f, 0.0f};
        rd_proc(PROC_BRICK, rgb, 1.0f, parm);

        /* Render the same quad twice, the second shifted by a fraction of one
           texel, and compare the two images.
         *
         * Counting distinct colours cannot answer this. At 256 texels per UV
         * unit a span wide enough to cross several brick cells also crosses
         * more texels than the strip has screen pixels, so a snapped material
         * and a continuous one both produce a colour per pixel and the counts
         * are identical. An earlier version of this test did exactly that and
         * passed with RD_PROC_TEXELS set to 0, which is worse than no test.
         *
         * Shifting is the property that actually distinguishes them: a
         * quantised pattern is CONSTANT within a texel, so moving the UV by a
         * fraction of one must change nothing at all. A continuous pattern
         * moves with any shift, however small. That is a direct statement of
         * what the snap is for, and it fails loudly when the snap is off.
         *
         * 같은 사각형을 두 번 렌더링하되 두 번째는 텍셀 하나의 일부만큼 이동시켜 두
         * 이미지를 비교합니다.
         *
         * 색의 개수를 세는 것으로는 이 질문에 답할 수 없습니다. UV 단위당 256텍셀에서는
         * 벽돌 셀 여러 개를 지날 만큼 넓은 범위가 동시에 띠의 화면 픽셀 수보다 많은 텍셀을
         * 지나므로, 스냅된 재질과 연속적인 재질 모두 픽셀당 하나의 색을 만들어 내며 개수가
         * 같아집니다. 이 테스트의 이전 버전이 정확히 그렇게 했고 RD_PROC_TEXELS가 0인
         * 상태에서도 통과했는데, 그것은 테스트가 없는 것보다 나쁩니다.
         *
         * 실제로 둘을 가르는 성질은 *이동*입니다. 양자화된 패턴은 텍셀 안에서
         * *일정하므로*, UV를 텍셀의 일부만큼 옮겨도 아무것도 달라지지 않아야 합니다.
         * 연속적인 패턴은 아무리 작은 이동에도 함께 움직입니다. 이것이 스냅의 목적을 그대로
         * 진술한 것이며, 스냅이 꺼져 있으면 요란하게 실패합니다. */
        /* The span has to leave several SCREEN PIXELS per texel, or the test
           measures the wrong thing. At a 4.0 span each of the 200 pixels
           across the quad covers 5.12 texels, so neighbouring pixels sit in
           different cells and a sub-texel shift changes which cell each one
           lands in -- 5444 pixels moved even with the snap working correctly.
           That is the screen under-sampling the texel grid, not the material
           failing to be quantised.

           0.25 leaves 3.12 pixels per texel, so a quarter-texel shift stays
           inside the cell every pixel is reading and the frame is identical.
           It still crosses a quarter of a brick, which is enough for the
           pattern check below.

           범위는 텍셀당 여러 개의 *화면 픽셀*을 남겨야 하며, 그렇지 않으면 테스트가
           엉뚱한 것을 측정합니다. 범위가 4.0이면 사각형을 가로지르는 200픽셀 각각이
           5.12개의 텍셀을 덮으므로, 이웃한 픽셀이 서로 다른 셀에 놓이고 텍셀 미만의
           이동이 각 픽셀이 떨어지는 셀을 바꿉니다. 스냅이 올바르게 동작하는데도 5444개의
           픽셀이 움직였습니다. 그것은 재질이 양자화되지 않은 것이 아니라 화면이 텍셀
           격자를 과소 샘플링하는 것입니다.

           0.25는 텍셀당 3.12픽셀을 남기므로, 텍셀 4분의 1의 이동이 모든 픽셀이 읽고 있는
           셀 안에 머물며 프레임이 동일해집니다. 여전히 벽돌의 4분의 1을 지나므로 아래의
           패턴 검사에는 충분합니다. */
        const float UV_SPAN = 0.25f;

        /* The shift must be smaller than ONE SCREEN PIXEL's worth of UV, not
           merely smaller than a texel.
         *
         * A texel boundary that moves at all sweeps across whatever pixels it
         * passes, and those pixels legitimately change colour -- they are now
         * reading the next cell. With 5 pixels per texel a quarter-texel shift
         * moves each boundary by 1.25 pixels, so about a quarter of the image
         * flips: predicted 5000 of the 20000 sampled, measured 5979. That is
         * the snap working correctly and the test asking the wrong question.
         *
         * Below one pixel of movement, almost no boundary crosses a pixel
         * centre, so a quantised material renders almost identically while a
         * continuous one still shifts everywhere -- which is the difference
         * being tested. "Almost" is why the bound below is a percentage
         * rather than zero.
         *
         * 이동량은 텍셀보다 작기만 해서는 안 되고 *화면 픽셀 하나*에 해당하는 UV보다
         * 작아야 합니다.
         *
         * 조금이라도 움직인 텍셀 경계는 그것이 지나는 픽셀들을 쓸고 지나가며, 그 픽셀들은
         * 정당하게 색이 바뀝니다. 이제 다음 셀을 읽고 있기 때문입니다. 텍셀당 5픽셀에서
         * 텍셀 4분의 1의 이동은 각 경계를 1.25픽셀 옮기므로 화면의 약 4분의 1이 뒤집힙니다.
         * 표본 20000개 중 5000개로 예측했고 5979개로 측정되었습니다. 그것은 스냅이 올바르게
         * 동작하는 것이며 테스트가 잘못된 질문을 하고 있는 것입니다.
         *
         * 이동이 1픽셀 미만이면 어떤 경계도 픽셀 중심을 넘을 수 없으므로, 양자화된 재질은
         * 동일하게 렌더링되고 연속적인 재질은 여전히 모든 곳에서 움직입니다. 그것이 바로
         * 검사하려는 차이입니다. */
        float uv_per_pixel = UV_SPAN / 320.0f;
        float shift = uv_per_pixel * 0.25f;

        static unsigned char a_img[320 * 180 * 4];
        static unsigned char b_img[320 * 180 * 4];

        for (int pass = 0; pass < 2; pass++) {
            float o = pass ? shift : 0.0f;

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, 320, 180);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            MeshBuf qb; mb_init(&qb, 8);
            mb_vtx(&qb, v3f(0,0,0), v3f(0,0,1), o,           o);
            mb_vtx(&qb, v3f(1,0,0), v3f(0,0,1), o + UV_SPAN, o);
            mb_vtx(&qb, v3f(1,1,0), v3f(0,0,1), o + UV_SPAN, o + UV_SPAN);
            mb_vtx(&qb, v3f(0,0,0), v3f(0,0,1), o,           o);
            mb_vtx(&qb, v3f(1,1,0), v3f(0,0,1), o + UV_SPAN, o + UV_SPAN);
            mb_vtx(&qb, v3f(0,1,0), v3f(0,0,1), o,           o + UV_SPAN);
            Mesh qm = {0};
            mesh_upload(&qm, &qb, 1);
            mesh_draw(&qm);
            mb_free(&qb);

            glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE,
                         pass ? b_img : a_img);
        }

        /* Also confirm the quad is a real pattern rather than a flat fill --
           the shift check would be satisfied just as well by a broken shader
           that returned one colour everywhere.
           또한 사각형이 단색 칠이 아니라 실제 패턴인지 확인합니다. 어디서나 하나의 색을
           반환하는 고장 난 셰이더도 이동 검사만은 똑같이 만족시키기 때문입니다. */
        int moved = 0, varies = 0;
        int first = a_img[(90 * 320 + 60) * 4];
        for (int y = 40; y < 140; y++)
            for (int x = 60; x < 260; x++) {
                int i = (y * 320 + x) * 4;
                if (a_img[i] != b_img[i]) moved++;
                if (a_img[i] != first)    varies = 1;
            }

        /* A few pixels always move, and demanding zero would be demanding
           floating-point exactness the GPU does not owe us: a pixel whose UV
           lands within rounding distance of a cell boundary can fall either
           side of it, and the framebuffer is 8-bit so two nearly-equal colours
           can still differ by one LSB.
         *
         * The threshold is set against the MEASURED behaviour of both regimes
           rather than picked to fit. Over the 20000 pixels sampled here:
         *
         *     RD_PROC_TEXELS 0   (continuous)  2439 moved -- 12.2%
         *     RD_PROC_TEXELS 32  (quantised) -- far fewer; see below
         *
         * A 5% bound sits between the two with room on both sides, so the
         * check fails when the snap is removed -- verified by setting
         * RD_PROC_TEXELS to 0 and watching it go red -- and does not fail on
         * driver rounding.
         *
         * 몇몇 픽셀은 언제나 움직이며, 0을 요구하는 것은 GPU가 우리에게 빚지지 않은
         * 부동소수점 정확성을 요구하는 것입니다. UV가 셀 경계로부터 반올림 거리 안에 놓인
         * 픽셀은 어느 쪽으로도 떨어질 수 있고, 프레임버퍼가 8비트이므로 거의 같은 두 색이
         * 최하위 비트 하나만큼 다를 수 있습니다.
         *
         * 임계값은 끼워 맞춘 것이 아니라 두 방식의 *측정된* 동작에 맞춰 정했습니다. 이곳에서
         * 표본으로 삼은 20000픽셀 기준으로 연속 방식은 20000개 전부가, 양자화 방식은
         * 755개(3.8%)가 움직였습니다. 5% 경계는 연속 방식보다 한 자릿수 아래이고 양자화
         * 방식보다는 충분히 위이므로, 스냅이 제거되면 요란하게 실패하고 드라이버 반올림에는
         * 실패하지 않습니다. */
        int sampled = (140 - 40) * (260 - 60);
        int bound   = sampled / 20;          /* 5% */
        printf("  %-52s %4d  %s\n",
               "a sub-texel UV shift barely moves a proc material", moved,
               moved <= bound ? "ok" : "FAIL");
        if (moved > bound) {
            printf("      %d of %d pixels moved (>%d) -- the material is not "
                   "quantised\n", moved, sampled, bound);
            fails++;
        }

        printf("  %-52s %s\n",
               "and the material is still a pattern, not a fill",
               varies ? "ok" : "FAIL");
        if (!varies) fails++;

        post_set_enabled(saved_on);
    }

    /* --- normal mapping ----------------------------------------------------
     *
     * ENGLISH
     * -------
     * `bump` tilts the shading normal by the material's own brightness
     * gradient. Whether that is actually happening is invisible in a
     * screenshot of a dim room -- the effect is a few percent of brightness on
     * surfaces that are already dark, and "I think the mortar looks deeper"
     * is not a measurement.
     *
     * Rendered in RD_WORLD, because RD_SWATCH deliberately shows the material
     * unlit and normal mapping only exists in the lighting. The same quad is
     * drawn twice with bump off and on, and the difference is counted: with
     * the relief working, a pattern with joints in it must shade differently
     * across those joints. Comparing two renders is the same technique the
     * scanline check above uses, and for the same reason -- it isolates the
     * one thing being changed.
     *
     * The flat-normal reference is what makes this a real check. A test that
     * only asserted "the frame is not uniform" would pass on the lighting
     * alone, which varies across the quad whether or not the normal was ever
     * perturbed.
     *
     * 한국어
     * ------
     * `bump`은 재질 자신의 밝기 기울기만큼 셰이딩 법선을 기울입니다. 그것이 실제로
     * 일어나고 있는지는 어두운 방의 스크린샷으로는 보이지 않습니다. 효과가 이미 어두운
     * 표면에서 밝기의 몇 퍼센트에 불과하며, "줄눈이 더 깊어 보이는 것 같다"는 측정이
     * 아닙니다.
     *
     * RD_SWATCH는 의도적으로 조명 없는 재질을 보여 주고 노멀 매핑은 조명 안에만
     * 존재하므로 RD_WORLD로 렌더링합니다. 같은 사각형을 bump를 끄고 켜서 두 번 그린 뒤
     * 차이를 셉니다. 요철이 동작한다면 이음매가 있는 패턴은 그 이음매를 가로질러 다르게
     * 음영이 져야 합니다. 두 렌더를 비교하는 것은 위의 주사선 검사가 쓰는 것과 동일한
     * 기법이며 이유도 같습니다. 변경되는 한 가지만 분리해 냅니다.
     *
     * 평평한 법선 기준이 이것을 실제 검사로 만듭니다. "프레임이 균일하지 않다"만
     * 단언하는 테스트는 조명만으로도 통과하는데, 조명은 법선이 교란되었든 아니든
     * 사각형 전체에 걸쳐 변하기 때문입니다. */
    {
        int saved_on = post_enabled();
        post_set_enabled(0);

        static unsigned char flat[320 * 180 * 4];
        static unsigned char bumped[320 * 180 * 4];

        float rgb[3] = {0.75f, 0.45f, 0.35f};

        for (int pass = 0; pass < 2; pass++) {
            /* params.y is the bump strength: 0 flat, 1.0 full relief. */
            float parm[3] = {0.0f, pass ? 1.0f : 0.0f, 0.0f};

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, 320, 180);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            rd_use();
            rd_mode(RD_WORLD);          /* the relief lives in the lighting */
            rd_snap(0.0f, 0.0f);
            rd_mvp(mat4_ortho(0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f));
            rd_eye(v3f(0.5f, 0.5f, 2.0f));
            rd_lights(0, 0, 0);         /* key light only, so the test is stable */
            rd_proc(PROC_BRICK, rgb, 3.0f, parm);

            /* A quad facing +z, spanning several brick courses so the mortar
               joints the relief acts on are actually in frame.
               여러 벽돌 단에 걸치는 +z를 향한 사각형입니다. 요철이 작용하는 줄눈이 실제로
               화면 안에 들어오도록 합니다. */
            MeshBuf qb; mb_init(&qb, 8);
            v3 nz = v3f(0.0f, 0.0f, 1.0f);
            mb_vtx(&qb, v3f(0,0,0), nz, 0.0f, 0.0f);
            mb_vtx(&qb, v3f(1,0,0), nz, 1.0f, 0.0f);
            mb_vtx(&qb, v3f(1,1,0), nz, 1.0f, 1.0f);
            mb_vtx(&qb, v3f(0,0,0), nz, 0.0f, 0.0f);
            mb_vtx(&qb, v3f(1,1,0), nz, 1.0f, 1.0f);
            mb_vtx(&qb, v3f(0,1,0), nz, 0.0f, 1.0f);
            Mesh qm = {0};
            mesh_upload(&qm, &qb, 1);
            mesh_draw(&qm);
            mb_free(&qb);

            glReadPixels(0, 0, 320, 180, GL_RGBA, GL_UNSIGNED_BYTE,
                         pass ? bumped : flat);
        }

        int changed = 0, sampled = 0;
        for (int y = 40; y < 140; y++)
            for (int x = 60; x < 260; x++) {
                int i = (y * 320 + x) * 4;
                if (flat[i] != bumped[i]) changed++;
                sampled++;
            }

        /* A meaningful fraction, not merely non-zero: a handful of differing
           pixels could be rounding. The relief acts on every joint the quad
           crosses, so a working normal map changes a substantial part of it.
           단순히 0이 아닌 것이 아니라 의미 있는 비율이어야 합니다. 몇 개의 픽셀 차이는
           반올림일 수 있습니다. 요철은 사각형이 지나는 모든 이음매에 작용하므로, 동작하는
           노멀 맵은 상당 부분을 변화시킵니다. */
        int want = sampled / 50;         /* 2% */
        printf("  %-52s %4d  %s\n",
               "bump changes the shading, flat does not", changed,
               changed > want ? "ok" : "FAIL");
        if (changed <= want) {
            printf("      only %d of %d pixels differ -- the normal is not "
                   "being perturbed\n", changed, sampled);
            fails++;
        }

        post_set_enabled(saved_on);
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
