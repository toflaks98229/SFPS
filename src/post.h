/**
 * @file post.h
 * @brief Offscreen render target and the post-process pass: pixelise + dither.
 *
 * ENGLISH
 * -------
 * The world is drawn into a small offscreen buffer and then blitted to the
 * window through one full-screen triangle. Everything the look depends on
 * follows from that one change:
 *
 *  - PIXELISATION is not a filter at all. Rendering at 320x180 and magnifying
 *    with GL_NEAREST *is* the pixel look; there is no shader involved. It
 *    also cuts the procedural materials' per-pixel cost by the same factor,
 *    which is the one optimisation this project has that costs nothing.
 *  - DITHERING quantises the colour in the blit shader, using an ordered
 *    Bayer threshold so the error becomes a stable pattern rather than noise.
 *  - The pattern DENSITY is driven by luminance, which is what makes it read
 *    as shading rather than as a screen-door laid over the image.
 *
 * The HUD and text are deliberately NOT part of this. They are drawn after
 * the blit, at native resolution, because 5x7 glyphs magnified 4x are
 * unreadable and dithered text is worse.
 *
 * 한국어
 * ------
 * 월드를 작은 오프스크린 버퍼에 그린 뒤 전체 화면 삼각형 하나로 창에 옮깁니다. 이
 * 룩을 구성하는 모든 요소가 이 한 가지 변경에서 파생됩니다:
 *
 *  - 픽셀화는 필터가 아닙니다. 320x180으로 렌더하고 GL_NEAREST로 확대하는 것
 *    *자체가* 픽셀 룩이며 셰이더가 개입하지 않습니다. 덤으로 절차적 재질의 픽셀당
 *    비용이 같은 배율만큼 줄어드는데, 이는 이 프로젝트에서 비용 없이 얻는 유일한
 *    최적화입니다.
 *  - 디더링은 블릿 셰이더에서 색을 양자화하며, 정렬된 Bayer 임계값을 사용해 오차가
 *    잡음이 아닌 안정적인 패턴이 되도록 합니다.
 *  - 패턴 *밀도*는 휘도에 의해 결정되며, 이것이 화면에 덧씌운 방충망이 아니라
 *    음영으로 읽히게 만드는 핵심입니다.
 *
 * HUD와 텍스트는 의도적으로 이 과정에서 제외됩니다. 블릿 이후 원해상도로 그려지는데,
 * 5x7 글리프를 4배 확대하면 읽을 수 없고 디더링된 텍스트는 더 나쁘기 때문입니다.
 */
#ifndef POST_H
#define POST_H

#include "m.h"

/* --- Tuning / 튜닝 --- */

/**
 * @brief Target height of the offscreen buffer, in pixels.
 *
 * ENGLISH
 * -------
 * @note A TARGET, not the exact height. main.c divides the window height by
 *       this to get a whole-number magnification, then sizes the buffer from
 *       that -- so a 720-tall window gives scale 4 and a 180-tall buffer,
 *       while a 700-tall window gives scale 3 and a 233-tall one.
 * @note The integer scale is the point. A buffer that does not divide the
 *       window evenly makes GL_NEAREST magnify most source pixels to N screen
 *       pixels and some to N+1, and which ones get the extra row changes as
 *       the camera moves -- edges crawl and the stipple creeps. Lowering this
 *       number makes the pixels bigger, which is the intended dial.
 *
 * 한국어
 * ------
 * @brief 오프스크린 버퍼의 목표 높이(픽셀).
 * @note 정확한 높이가 아니라 *목표*입니다. main.c가 창 높이를 이 값으로 나누어 정수
 *       확대 배율을 구한 뒤 그로부터 버퍼 크기를 정합니다. 높이 720인 창은 배율 4와
 *       높이 180인 버퍼를, 높이 700인 창은 배율 3과 높이 233인 버퍼를 갖게 됩니다.
 * @note 정수 배율이 핵심입니다. 창을 정확히 나누지 못하는 버퍼는 GL_NEAREST가
 *       대부분의 원본 픽셀을 N개의 화면 픽셀로, 일부는 N+1개로 확대하게 만들며, 어느
 *       픽셀이 한 줄을 더 받는지가 카메라 이동에 따라 바뀝니다. 가장자리가 기어
 *       다니고 스티플이 흐릅니다. 이 값을 낮추면 픽셀이 커지며, 그것이 의도된
 *       조정 방식입니다.
 */
#define POST_HEIGHT 360

/**
 * @brief Rendered samples per art pixel, per axis.
 *
 * ENGLISH
 * -------
 * @note This is what makes the pixelisation real rather than cosmetic. The
 *       world is rendered at POST_SUPERSAMPLE times the art resolution and
 *       each art pixel is resolved as the AVERAGE of its block, so an edge
 *       crossing part of a pixel contributes part of its colour. Without it
 *       the rasteriser gives each art pixel one sample from its centre, and
 *       thin geometry either lands on that centre or disappears.
 * @note Costs the square of this in fill rate: 2 means a framebuffer four
 *       times the area and four texture fetches per output pixel. 1 disables
 *       supersampling entirely and restores the single-sample behaviour.
 * @note This is the ONLY definition. post.c stringifies it straight into the
 *       shader source, so there is no second copy in GLSL to keep in step --
 *       unlike PROC_*, which still pairs by hand with render.c. An earlier
 *       version did duplicate it, and the two went out of sync once: the C
 *       side allocated a buffer of one size while the shader averaged blocks
 *       of another, which tore the image into misaligned tiles without naming
 *       its cause.
 *
 * 한국어
 * ------
 * @brief 아트 픽셀당 렌더링 샘플 수이며 축별 값입니다.
 * @note 이것이 픽셀화를 겉모습이 아닌 실제 동작으로 만듭니다. 월드를 아트 해상도의
 *       POST_SUPERSAMPLE배로 렌더링하고 각 아트 픽셀을 그 블록의 *평균*으로
 *       해상하므로, 픽셀의 일부를 가로지르는 모서리가 자기 색의 일부를 기여합니다.
 *       이것이 없으면 래스터라이저가 각 아트 픽셀에 중심의 샘플 하나만 제공하며, 얇은
 *       지오메트리는 그 중심에 걸리거나 사라집니다.
 * @note 픽셀 채우기 비용이 이 값의 제곱만큼 듭니다. 2이면 프레임버퍼 면적이 4배가
 *       되고 출력 픽셀당 텍스처 페치가 4회입니다. 1이면 슈퍼샘플링이 완전히
 *       비활성화되어 단일 샘플 동작으로 되돌아갑니다.
 * @note 이것이 *유일한* 정의입니다. post.c가 이 값을 셰이더 소스에 그대로 문자열로
 *       삽입하므로 GLSL에 동기화를 유지해야 할 두 번째 사본이 없습니다. 여전히
 *       render.c와 수동으로 짝을 이루는 PROC_*와는 다릅니다. 이전 버전은 값을
 *       중복해서 두었고 실제로 한 번 어긋났습니다. C 쪽은 한 크기로 버퍼를 할당하는데
 *       셰이더는 다른 크기로 블록을 평균 내어, 원인을 알려 주지 않은 채 화면이 어긋난
 *       타일로 찢어졌습니다.
 */
#define POST_SUPERSAMPLE 2

/** @brief Widest offscreen buffer we will allocate, for a very wide window. / 매우 넓은 창을 위해 할당하는 최대 오프스크린 버퍼 폭. */
#define POST_MAX_WIDTH 1024

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Creates the offscreen target and compiles the blit shader.
 *
 * ENGLISH
 * -------
 * @param[in] width  Offscreen width in pixels, clamped to ::POST_MAX_WIDTH.
 * @param[in] height Offscreen height in pixels.
 * @return 1 on success, 0 if the framebuffer could not be completed.
 * @note A return of 0 is not fatal for the caller: ::post_enabled reports it,
 *       and the game can draw straight to the window instead. A driver that
 *       cannot complete a plain RGBA + depth FBO is unlikely, but refusing to
 *       start over a cosmetic pass would be the wrong trade.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @brief 오프스크린 타깃을 생성하고 블릿 셰이더를 컴파일합니다.
 * @param[in] width  오프스크린 너비(픽셀). ::POST_MAX_WIDTH로 제한됩니다.
 * @param[in] height 오프스크린 높이(픽셀).
 * @return 성공 시 1, 프레임버퍼를 완성하지 못하면 0.
 * @note 0을 반환해도 호출자에게 치명적이지 않습니다. ::post_enabled가 이를 보고하며
 *       게임은 창에 직접 그릴 수 있습니다. 평범한 RGBA + 깊이 FBO를 완성하지 못하는
 *       드라이버는 드물지만, 순전히 미관을 위한 패스 때문에 실행을 거부하는 것은
 *       잘못된 선택입니다.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
int post_init(int width, int height);

/**
 * @brief Whether the offscreen path is available and in use.
 *
 * ENGLISH
 * -------
 * @return Non-zero when ::post_init succeeded and the effect is on.
 *
 * 한국어
 * ------
 * @brief 오프스크린 경로를 사용할 수 있고 활성화되어 있는지 여부.
 * @return ::post_init이 성공했고 효과가 켜져 있으면 0이 아닌 값.
 */
int post_enabled(void);

/**
 * @brief Turns the effect on or off at runtime.
 *
 * ENGLISH
 * -------
 * @param[in] on Non-zero to render through the offscreen buffer.
 * @note A no-op when ::post_init failed -- the effect cannot be switched on
 *       if there is no target to draw into.
 *
 * 한국어
 * ------
 * @brief 런타임에 효과를 켜거나 끕니다.
 * @param[in] on 0이 아니면 오프스크린 버퍼를 거쳐 렌더링합니다.
 * @note ::post_init이 실패한 경우 아무 동작도 하지 않습니다. 그릴 대상이 없으면
 *       효과를 켤 수 없기 때문입니다.
 */
void post_set_enabled(int on);

/**
 * @brief Binds the offscreen target and sets the viewport to match.
 *
 * ENGLISH
 * -------
 * @return The aspect ratio to render with, or 0 when the effect is off.
 * @note Call INSTEAD of the usual glViewport at the top of the frame. The
 *       returned aspect comes from the offscreen buffer rather than the
 *       window, and using the window's would stretch the world.
 * @warning A caller that ignores the return value and keeps its own aspect
 *          will render a distorted image whenever the two disagree.
 *
 * 한국어
 * ------
 * @brief 오프스크린 타깃을 바인딩하고 뷰포트를 그에 맞게 설정합니다.
 * @return 렌더링에 사용할 종횡비. 효과가 꺼져 있으면 0.
 * @note 프레임 시작 시 기존 glViewport 호출을 *대체하여* 호출하십시오. 반환되는
 *       종횡비는 창이 아니라 오프스크린 버퍼에서 나온 값이며, 창의 값을 쓰면 월드가
 *       늘어납니다.
 * @warning 반환값을 무시하고 자체 종횡비를 유지하는 호출자는 두 값이 다를 때마다
 *          왜곡된 화면을 렌더링하게 됩니다.
 */
float post_begin(void);

/**
 * @brief Resolves the offscreen buffer to the window through the blit shader.
 *
 * ENGLISH
 * -------
 * @param[in] win_w Window width in pixels.
 * @param[in] win_h Window height in pixels.
 * @note Restores the default framebuffer and the window viewport, so anything
 *       drawn after this -- the HUD, the crosshair, text -- lands at native
 *       resolution and is neither pixelised nor dithered. That separation is
 *       the whole reason this is a function the caller places rather than
 *       something that happens at SwapBuffers.
 * @note A no-op when the effect is off.
 * @warning Leaves depth testing DISABLED, because the full-screen pass does
 *          not want it. The caller must re-enable it before drawing anything
 *          three-dimensional afterward.
 *
 * 한국어
 * ------
 * @brief 블릿 셰이더를 통해 오프스크린 버퍼를 창으로 해상합니다.
 * @param[in] win_w 창 너비(픽셀).
 * @param[in] win_h 창 높이(픽셀).
 * @note 기본 프레임버퍼와 창 뷰포트를 복원하므로, 이 호출 이후에 그려지는 것들
 *       (HUD, 조준점, 텍스트)은 원해상도로 그려지며 픽셀화도 디더링도 되지 않습니다.
 *       이 분리가 바로 이것이 SwapBuffers 시점에 자동으로 일어나는 것이 아니라
 *       호출자가 위치를 정하는 함수인 이유입니다.
 * @note 효과가 꺼져 있으면 아무 동작도 하지 않습니다.
 * @warning 전체 화면 패스는 깊이 테스트를 필요로 하지 않으므로 깊이 테스트를 *꺼진*
 *          상태로 둡니다. 이후 3차원 요소를 그리려면 호출자가 다시 켜야 합니다.
 */
void post_end(int win_w, int win_h);

/**
 * @brief Which side of the pass boundary the frame is currently on.
 *
 * ENGLISH
 * -------
 * @return 1 between ::post_begin and ::post_end (the WORLD pass, which is
 *         pixelised and dithered), 0 outside it (the UI pass, at native
 *         resolution).
 * @note Exists so the boundary can be asserted rather than only commented.
 *       Which side a draw belongs on is a real decision -- the view model is
 *       deliberately in the world pass because it shares the scene's
 *       lighting, while 5x7 glyphs must not be, because magnified four times
 *       and dithered they are unreadable. Getting it wrong produces a
 *       cosmetic bug with no error, which is the kind that survives review.
 * @note Cheap: reads one flag. Intended for a POST_ASSERT_WORLD_PASS /
 *       POST_ASSERT_UI_PASS check at the top of a draw routine in dev builds.
 *
 * 한국어
 * ------
 * @brief 현재 프레임이 패스 경계의 어느 쪽에 있는지를 나타냅니다.
 * @return ::post_begin과 ::post_end 사이(픽셀화·디더링되는 *월드* 패스)이면 1,
 *         그 바깥(원해상도 UI 패스)이면 0.
 * @note 경계를 주석으로만 두지 않고 단언할 수 있도록 존재합니다. 어떤 그리기가 어느
 *       쪽에 속하는지는 실제 판단이 필요한 문제입니다. 뷰 모델은 장면의 조명을
 *       공유하므로 의도적으로 월드 패스에 두는 반면, 5x7 글리프는 4배 확대되어
 *       디더링되면 읽을 수 없으므로 그쪽에 있어서는 안 됩니다. 잘못 두면 오류 없이
 *       외관상 버그만 발생하는데, 이런 종류가 리뷰를 통과해 살아남습니다.
 * @note 플래그 하나를 읽을 뿐이라 저렴합니다. 개발 빌드에서 그리기 루틴 상단에
 *       POST_ASSERT_WORLD_PASS / POST_ASSERT_UI_PASS 검사를 두는 용도입니다.
 */
int post_in_world_pass(void);

/**
 * @brief Releases the offscreen target and the blit shader.
 *
 * ENGLISH
 * -------
 * @warning Requires a current GL context. Safe to call when ::post_init failed.
 *
 * 한국어
 * ------
 * @brief 오프스크린 타깃과 블릿 셰이더를 해제합니다.
 * @warning 활성 GL 컨텍스트가 필요합니다. ::post_init이 실패한 경우에 호출해도
 *          안전합니다.
 */
void post_shutdown(void);

#endif
