/**
 * @file gl_web.c
 * @brief Asks the browser for a WebGL 2 context. The whole of it.
 *
 * ENGLISH
 * -------
 * gl.c is 205 lines because Win32 makes bringing up a modern context
 * circular. This file is short for the opposite reason and not because
 * anything was left out: the circle does not exist here, so what is left is
 * the part that was always the point -- saying what kind of context this
 * renderer needs, and finding out whether it can have one.
 *
 * EVERY ATTRIBUTE BELOW IS A DECISION, and each one is written down because
 * the defaults are chosen for a page with a 3D widget on it rather than for a
 * game that owns the window. Most of them turn something OFF.
 *
 * 한국어
 * ------
 * @brief 브라우저에게 WebGL 2 컨텍스트를 요구합니다. 그것이 전부입니다.
 *
 * gl.c가 205줄인 것은 Win32가 최신 컨텍스트를 세우는 일을 순환으로 만들기 때문입니다. 이
 * 파일이 짧은 것은 그 반대의 이유이며 무언가를 빠뜨려서가 아닙니다. 이곳에는 그 순환이 없으므로
 * 남는 것은 애초에 요점이었던 부분, 즉 이 렌더러가 어떤 종류의 컨텍스트를 필요로 하는지 말하고
 * 그것을 가질 수 있는지 알아내는 일뿐입니다.
 *
 * *아래의 모든 속성은 결정이며*, 기본값이 창을 소유한 게임이 아니라 3D 위젯이 얹힌 페이지를
 * 기준으로 정해져 있기 때문에 하나하나 적어 둡니다. 그중 대부분은 무언가를 *끕니다*.
 */
#include "webgl.h"

#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

int glweb_make_context(const char *canvas) {
    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);

    /* WebGL 2, which is OpenGL ES 3.0. See webgl.h for why there is no
       fallback below it.
       WebGL 2이며 곧 OpenGL ES 3.0입니다. 그 아래로 내려가는 폴백이 없는 이유는 webgl.h를
       보십시오. */
    attr.majorVersion = 2;
    attr.minorVersion = 0;

    /* OPAQUE. An alpha channel on the canvas lets the page behind it show
       through and puts the compositor in the middle of every frame. This game
       fills every pixel it is given and has nothing to blend with.
       *불투명합니다.* 캔버스의 알파 채널은 뒤의 페이지가 비쳐 보이게 하고 모든 프레임의
       한가운데에 컴포지터를 놓습니다. 이 게임은 받은 픽셀을 전부 채우며 섞을 것이 없습니다. */
    attr.alpha = false;
    attr.premultipliedAlpha = false;

    /* DEPTH ON THE DEFAULT FRAMEBUFFER, which is easy to think unnecessary.
       The world is drawn into post.c's own offscreen buffer, and that buffer
       carries its own depth renderbuffer -- so the default one looks spare.
       It is not: F1 turns the pass off, and then the world is drawn straight
       to the canvas and needs somewhere to test depth. A context without it
       would run correctly until somebody pressed a key.
       *기본 프레임버퍼의 깊이*이며, 불필요하다고 생각하기 쉬운 항목입니다. 월드는 post.c 자신의
       오프스크린 버퍼에 그려지고 그 버퍼는 자기 깊이 렌더버퍼를 지니므로 기본 쪽은 남아도는
       것처럼 보입니다. 아닙니다. F1이 그 패스를 끄면 월드는 캔버스에 곧장 그려지고 깊이를
       검사할 곳이 필요합니다. 그것이 없는 컨텍스트는 누군가 키를 누를 때까지 올바르게
       돕니다. */
    attr.depth = true;
    attr.stencil = false;

    /* NO MULTISAMPLING, and this one is the look rather than the budget. The
       frame is rendered small and magnified with GL_NEAREST; that IS the
       pixelisation, and post.c resolves its own supersamples by averaging each
       block itself. MSAA on the default framebuffer would be paid for on every
       pixel and then thrown away by the blit.
       *멀티샘플링 없음*이며, 이것은 예산이 아니라 룩입니다. 프레임은 작게 그려져 GL_NEAREST로
       확대되고 그것이 곧 픽셀화입니다. post.c는 자기 슈퍼샘플을 블록마다 직접 평균 내어
       해상합니다. 기본 프레임버퍼의 MSAA는 모든 픽셀에서 값을 치른 뒤 블릿이 버립니다. */
    attr.antialias = false;

    /* The frame is redrawn from scratch every time, so there is nothing to
       preserve and asking to would cost a copy.
       프레임은 매번 처음부터 다시 그려지므로 보존할 것이 없고, 보존을 요구하면 복사 비용을
       치릅니다. */
    attr.preserveDrawingBuffer = false;

    /* A 3D game on a laptop with two GPUs wants the other one.
       GPU가 둘인 노트북의 3D 게임은 다른 쪽을 원합니다. */
    attr.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

    /* NOT failIfMajorPerformanceCaveat. A software rasteriser is slow, and
       slow is a judgement the player gets to make; refusing to start is not.
       The dialogue this game would otherwise have with somebody on a machine
       with no GPU driver is a blank page, which says less than a bad frame
       rate does.
       *failIfMajorPerformanceCaveat를 쓰지 않습니다.* 소프트웨어 래스터라이저는 느리고, 느리다는
       것은 플레이어가 내릴 판단입니다. 시작을 거부하는 것은 그렇지 않습니다. 그러지 않으면 GPU
       드라이버가 없는 기계의 누군가와 이 게임이 나눌 대화는 빈 페이지인데, 그것은 나쁜
       프레임률보다 말해 주는 것이 적습니다. */
    attr.failIfMajorPerformanceCaveat = false;

    /* WHAT gl.h ASKED THIS FILE FOR. tex.c queries the anisotropic filtering
       maximum by enum, and in WebGL an extension's enums do nothing until the
       extension has been obtained on the context. This is where that happens
       for every supported extension at once; without it, the query in tex.c
       would come back empty on every browser and the floors would band exactly
       as they did before anisotropy was added.
       *gl.h가 이 파일에 요구한 것입니다.* tex.c는 비등방성 필터링의 최댓값을 열거값으로
       조회하는데, WebGL에서 확장의 열거값은 그 확장이 컨텍스트에서 획득되기 전까지 아무 일도
       하지 않습니다. 지원되는 모든 확장에 대해 그것이 한꺼번에 일어나는 자리가 이곳입니다.
       이것이 없으면 tex.c의 조회는 모든 브라우저에서 빈 값으로 돌아오고, 바닥은 비등방성이
       추가되기 전과 똑같이 띠를 만듭니다. */
    attr.enableExtensionsByDefault = true;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
        emscripten_webgl_create_context(canvas, &attr);

    /* 0 is "no context"; the handle is otherwise an opaque positive number.
       Negative values are not returned by this call, but testing for <= 0
       costs nothing and is the shape every emscripten example uses.
       0은 "컨텍스트 없음"이며, 그 외에 핸들은 불투명한 양수입니다. 이 호출이 음수를 반환하지는
       않지만 <= 0으로 검사하는 데 드는 비용이 없고, 모든 emscripten 예제가 쓰는 형태입니다. */
    if (ctx <= 0) return 0;

    if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS)
        return 0;

    return 1;
}
