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

/**
 * @brief Scanline depth the game runs at when scanlines are switched on.
 *
 * ENGLISH
 * -------
 * The value ::post_set_scanline starts at, named so the settings menu can put
 * it back after switching scanlines off. A menu that restored a hardcoded
 * number of its own would be a second copy of this decision, and the two would
 * drift the first time either was retuned.
 *
 * @note 0.18 costs the frame about 9% of its brightness -- see the note on
 *       ::post_set_scanline for why it is not higher.
 *
 * 한국어
 * ------
 * @brief 주사선을 켰을 때 게임이 사용하는 주사선 세기입니다.
 *
 * ::post_set_scanline이 시작하는 값이며, 설정 메뉴가 주사선을 껐다가 되돌릴 수 있도록
 * 이름을 붙였습니다. 메뉴가 자체적으로 하드코딩한 숫자로 복원한다면 이 결정의 두 번째
 * 사본이 되고, 어느 한쪽을 재조정하는 순간 둘이 어긋납니다.
 *
 * @note 0.18은 프레임 밝기의 약 9%를 소모합니다. 이보다 높지 않은 이유는
 *       ::post_set_scanline의 참고 사항을 확인하십시오.
 */
#define POST_SCANLINE_DEFAULT 0.18f

/* WHAT THE DITHER STARTS AT.
 *
 * Twelve steps per channel, not the four this shipped with. The dither's
 * loudness is not set by the dither -- it is set by the level count, because
 * an ordered pattern has to swing half a quantisation step to fake the shades
 * between two levels. At four levels a step is a third of full scale, so the
 * pattern swings a sixth of it, and measured on a real frame a THIRD of the
 * screen had neighbouring pixels differing by more than 16%. That is what was
 * fighting the silhouettes.
 *
 * Twelve puts a step at 9% and the swing at 4.5%, which still bands visibly --
 * the look survives -- while letting an imp read as an imp. The PlayStation
 * this borrows from was 15-bit, five bits a channel, THIRTY-TWO levels; the
 * PSX shader collections on GitHub quantise to exactly that, and it is why
 * their dithering reads as texture rather than as interference.
 *
 * 디더가 시작하는 값입니다. 채널당 4단계가 아니라 12단계입니다. 디더의 시끄러움은 디더가
 * 아니라 *단계 수*가 정합니다. 정렬 패턴은 두 단계 사이의 음영을 흉내 내려고 양자화
 * 한 단계의 절반만큼 흔들어야 하기 때문입니다. 4단계에서는 한 단계가 전체 범위의
 * 3분의 1이므로 패턴은 6분의 1을 흔들고, 실제 프레임에서 화면의 3분의 1이 이웃
 * 픽셀과 16% 넘게 차이 났습니다. 실루엣과 싸우고 있던 것이 그것입니다. */
#define POST_LEVELS_DEFAULT 12.0f

/* Per-frame grain. Cut from 0.05, and the reason is that it is the one effect
   here that changes every frame: a still image tolerates it and a moving one
   turns it into shimmer, because the eye tracks a surface and the noise on it
   does not travel with it. 0.015 is enough to break up a flat gradient.
   프레임별 그레인입니다. 0.05에서 줄였습니다. 이곳에서 매 프레임 바뀌는 유일한 효과이며,
   정지 화면은 견디지만 움직이는 화면에서는 어른거림이 됩니다. 눈은 표면을 따라가는데
   그 위의 잡음은 함께 움직이지 않기 때문입니다. */
#define POST_GRAIN_DEFAULT 0.015f

/* --- The single-hue (duotone) look / 단색조 룩 --- */

/**
 * @brief How far the image collapses onto one hue. 0 keeps scene colour, 1 is fully duotone.
 *
 * ENGLISH
 * -------
 * The dither above preserves each material's own colour: measured across the
 * whole Bayer tile, brick goes in at chroma 0.370 and comes out at 0.376,
 * tech green 0.280 and 0.287. That is a colour dither and it is a deliberate
 * choice.
 *
 * Return of the Obra Dinn and Who's Lila do the opposite -- they discard scene
 * hue entirely and map luminance onto a ramp between two fixed colours, so the
 * whole image reads as one ink on one paper and a material becomes a DENSITY
 * rather than a colour. This dial mixes between the two treatments.
 *
 * @note Applied after the quantisation, not instead of it, so every decision
 *       above about WHERE the dots fall is untouched -- this only changes what
 *       colour they are.
 * @note At 0 the stage is mathematically inert and the pass behaves exactly as
 *       it did before it existed, so the colour look is not lost.
 * @note Partial values are useful and not merely a crossfade: around 0.7 the
 *       image is dominated by the ink while a trace of the material's own hue
 *       survives, which is closer to Who's Lila than either extreme.
 *
 * 한국어
 * ------
 * @brief 이미지가 하나의 색상으로 수렴하는 정도입니다. 0이면 장면 색을 유지하고 1이면
 *        완전한 단색조가 됩니다.
 *
 * 위의 디더는 각 재질의 고유한 색을 보존합니다. Bayer 타일 전체에 걸쳐 측정하면 벽돌은
 * 채도 0.370으로 들어가 0.376으로 나오고, 기술 녹색은 0.280과 0.287입니다. 이것은 컬러
 * 디더이며 의도된 선택입니다.
 *
 * 오브라 딘 호의 귀환과 후즈 라일라는 그 반대입니다. 장면의 색상을 완전히 버리고 휘도를
 * 두 고정색 사이의 램프에 매핑하므로, 화면 전체가 한 종이 위의 한 잉크로 읽히고 재질은
 * 색이 아니라 *농도*가 됩니다. 이 값이 두 방식 사이를 혼합합니다.
 *
 * @note 양자화를 대체하지 않고 그 이후에 적용되므로, 점이 *어디에* 떨어질지에 대한 위의
 *       모든 결정은 그대로입니다. 이 단계는 그 점이 무슨 색인지만 바꿉니다.
 * @note 0이면 이 단계는 수학적으로 아무 영향이 없어 이 기능이 없던 때와 정확히 동일하게
 *       동작하므로, 컬러 룩은 사라지지 않습니다.
 * @note 중간값도 단순한 교차 페이드가 아니라 쓸모가 있습니다. 0.7 부근에서는 잉크가
 *       화면을 지배하면서도 재질 고유의 색이 희미하게 남는데, 이는 양극단 어느 쪽보다
 *       후즈 라일라에 가깝습니다.
 */
#define POST_DUOTONE 0.0

/**
 * @brief The dark end of the duotone ramp -- what luminance 0 becomes.
 *
 * ENGLISH
 * -------
 * @note Written as plain decimals with no `f` suffix: these are stringified
 *       straight into GLSL, where `0.09f` is a syntax error.
 * @note The default is Obra Dinn's pairing -- a very dark blue-black ink on a
 *       warm bone paper, rather than pure #000 on #fff. Neither end being
 *       neutral is what stops the image reading as a black-and-white photo.
 *
 * 한국어
 * ------
 * @brief 듀오톤 램프의 어두운 끝이며, 휘도 0이 이 색이 됩니다.
 * @note `f` 접미사 없는 일반 십진수로 작성합니다. 이 값들은 GLSL에 그대로 문자열로
 *       삽입되는데, GLSL에서 `0.09f`는 문법 오류입니다.
 * @note 기본값은 오브라 딘의 조합입니다. 순수한 #000과 #fff가 아니라, 따뜻한 뼈색 종이
 *       위의 매우 어두운 청흑색 잉크입니다. 양 끝 모두 중성이 아닌 것이 화면을 흑백
 *       사진처럼 보이지 않게 만듭니다.
 */
#define POST_INK_R   0.05
#define POST_INK_G   0.06
#define POST_INK_B   0.11

/**
 * @brief The bright end of the duotone ramp -- what luminance 1 becomes.
 *
 * ENGLISH
 * -------
 * @note Swapping INK and PAPER gives a negative image, which is a legitimate
 *       look and costs nothing to try.
 *
 * 한국어
 * ------
 * @brief 듀오톤 램프의 밝은 끝이며, 휘도 1이 이 색이 됩니다.
 * @note INK와 PAPER를 바꾸면 음화가 되며, 이 역시 유효한 룩이고 시도하는 데 드는 비용이
 *       없습니다.
 */
#define POST_PAPER_R 0.93
#define POST_PAPER_G 0.90
#define POST_PAPER_B 0.78

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
 * @brief The offscreen buffer's size, for anything that must match its grid.
 *
 * ENGLISH
 * -------
 * @param[out] w Receives the width in pixels. May be NULL.
 * @param[out] h Receives the height in pixels. May be NULL.
 * @note Reports 0,0 when ::post_init failed or the effect is off, so a caller
 *       can tell "no offscreen grid exists" from "the grid is this size"
 *       without a second query.
 * @note Exists for the vertex snap. The snap has to quantise to the pixel grid
 *       the image is actually rasterised on, and that is this buffer rather
 *       than the window -- snapping to the window's resolution would put the
 *       steps at a finer spacing than the pixels the player sees, which
 *       produces no visible wobble at all.
 *
 * 한국어
 * ------
 * @brief 오프스크린 버퍼의 크기입니다. 그 격자에 맞춰야 하는 쪽을 위해 제공됩니다.
 * @param[out] w 너비(픽셀)를 받습니다. NULL이어도 됩니다.
 * @param[out] h 높이(픽셀)를 받습니다. NULL이어도 됩니다.
 * @note ::post_init이 실패했거나 효과가 꺼져 있으면 0,0을 보고하므로, 호출자가 추가
 *       조회 없이 "오프스크린 격자가 없음"과 "격자 크기가 이것임"을 구분할 수 있습니다.
 * @note 정점 스냅을 위해 존재합니다. 스냅은 이미지가 실제로 래스터화되는 픽셀 격자에
 *       맞춰 양자화해야 하며, 그것은 창이 아니라 이 버퍼입니다. 창 해상도에 맞춰
 *       스냅하면 플레이어가 보는 픽셀보다 촘촘한 간격에 단계가 놓여 흔들림이 전혀
 *       보이지 않습니다.
 */
void post_size(int *w, int *h);

/**
 * @brief Sets how much the CRT scanlines darken alternate output rows.
 *
 * ENGLISH
 * -------
 * @param[in] depth 0 disables scanlines entirely; 1 blacks out every other
 *                  row. Clamped to that range.
 * @note The scanline is indexed against OUTPUT rows, not art pixels, because
 *       it is a property of the display rather than of the image. At a 2x
 *       integer scale that puts a line every art pixel, which is the real CRT
 *       spacing; at 1x it would be every output row and too fine to see.
 * @note The depth is a real brightness cost: half the rows lose `depth`, so
 *       the frame averages `1 - depth/2` of what it was. The default 0.18 is
 *       9%, and higher crushes the dither's darkest band into solid black.
 * @note Runtime-settable for the same reason the vertex snap is: a look like
 *       this has to be judged in motion. It is also what lets ::posttest
 *       measure the scanline at all -- the Bayer matrix has its own row-to-row
 *       brightness difference, so the only way to isolate the scanline is to
 *       render the same frame with it on and off.
 *
 * 한국어
 * ------
 * @brief CRT 주사선이 출력 행을 얼마나 어둡게 할지 설정합니다.
 * @param[in] depth 0이면 주사선이 완전히 비활성화되고, 1이면 한 행 걸러 완전히
 *                  검게 만듭니다. 이 범위로 제한됩니다.
 * @note 주사선은 아트 픽셀이 아니라 *출력 행*을 기준으로 인덱싱됩니다. 이미지가 아니라
 *       디스플레이의 속성이기 때문입니다. 2배 정수 배율에서는 아트 픽셀마다 한 줄이
 *       놓이며 이것이 실제 CRT 간격입니다. 1배였다면 출력 행마다가 되어 너무 촘촘해
 *       보이지 않습니다.
 * @note 세기는 실제 밝기 비용입니다. 행의 절반이 `depth`만큼 어두워지므로 프레임 평균이
 *       `1 - depth/2`가 됩니다. 기본값 0.18은 9%이며, 이보다 높이면 디더의 가장 어두운
 *       밴드가 완전한 검정으로 뭉갭니다.
 * @note 정점 스냅과 같은 이유로 런타임에 설정 가능합니다. 이런 종류의 룩은 움직임 속에서
 *       판단해야 합니다. 또한 이것이 ::posttest가 주사선을 측정할 수 있게 하는 유일한
 *       수단이기도 합니다. Bayer 행렬 자체가 행 간 밝기 차이를 갖고 있어, 주사선을
 *       분리하려면 같은 프레임을 켜고/끄고 렌더링하는 수밖에 없습니다.
 */
void post_set_scanline(float depth);

/**
 * @brief Sets how hard the resolve pass quantises and grains the picture.
 *
 * ENGLISH
 * -------
 * @param[in] levels Steps per colour channel, 2..64. Clamped.
 * @param[in] grain  Per-frame noise amplitude, 0..0.5. Clamped.
 * @note `levels` is the one that decides readability. An ordered dither swings
 *       half a quantisation step, so the pattern's amplitude is 1/(2*(levels-1))
 *       of full scale -- the dither constants do not come into it. Four levels
 *       is a sixth; twelve is a twenty-second; the PlayStation's own thirty-two
 *       is a sixty-second.
 *
 * 한국어
 * ------
 * @brief 해상 패스가 그림을 얼마나 강하게 양자화하고 거칠게 만들지를 설정합니다.
 * @param[in] levels 색 채널당 단계 수(2..64). 제한됩니다.
 * @param[in] grain  프레임별 잡음의 세기(0..0.5). 제한됩니다.
 * @note 가독성을 결정하는 것은 `levels`입니다. 정렬 디더는 양자화 한 단계의 절반을
 *       흔들므로 패턴의 진폭은 전체 범위의 1/(2*(levels-1))이며, 디더 상수는 여기에
 *       관여하지 않습니다.
 */
void post_set_dither(float levels, float grain);

/**
 * @brief The current scanline depth.
 *
 * 한국어
 * ------
 * @brief 현재 주사선 세기입니다.
 */
float post_scanline(void);

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
