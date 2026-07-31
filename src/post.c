/**
 * @file post.c
 * @brief Implements the offscreen target and the pixelise + dither resolve pass.
 *
 * ENGLISH
 * -------
 * One FBO, one full-screen triangle, one shader. The triangle is generated in
 * the vertex shader from gl_VertexID rather than uploaded, so this file owns
 * an empty VAO and no vertex buffer at all -- three vertices are not worth a
 * MeshBuf, and it keeps the pass independent of render.c's vertex format.
 *
 * A triangle rather than a quad: a full-screen quad is two triangles meeting
 * on the diagonal, and pixels along that seam get shaded twice on most
 * hardware. One oversized triangle clipped to the viewport covers the same
 * area with no seam and one fewer primitive.
 *
 * 한국어
 * ------
 * FBO 하나, 전체 화면 삼각형 하나, 셰이더 하나입니다. 삼각형은 업로드하지 않고
 * gl_VertexID로부터 정점 셰이더에서 생성되므로, 이 파일은 빈 VAO만 소유하고 정점
 * 버퍼는 전혀 갖지 않습니다. 정점 3개를 위해 MeshBuf를 쓸 이유가 없고, 이렇게 하면
 * 패스가 render.c의 정점 형식으로부터 독립적으로 유지됩니다.
 *
 * 사각형이 아닌 삼각형인 이유: 전체 화면 사각형은 대각선에서 만나는 삼각형 두 개이며,
 * 대부분의 하드웨어에서 그 이음매의 픽셀이 두 번 셰이딩됩니다. 뷰포트로 잘리는 큰
 * 삼각형 하나는 같은 영역을 이음매 없이, 프리미티브 하나 적게 덮습니다.
 */

#include "post.h"
#include "gl.h"
#include "render.h"   /* rd_use -- this pass binds its own program and must
                         put the renderer's back before returning. */

/* --- Shader source generation / 셰이더 소스 생성 --- */

/**
 * @def POST_STR
 * @brief Stringifies a macro's VALUE for injection into the shader source.
 *
 * ENGLISH
 * -------
 * The two-level expansion is required, not decoration: `#x` in a single-level
 * macro stringifies the argument's spelling, so it would produce the text
 * "POST_SUPERSAMPLE" rather than "2". The outer macro forces one expansion
 * before the inner one stringifies.
 *
 * This is what lets a constant be defined once in C and used in GLSL, which
 * removes the hand-kept pairing that went out of sync once already.
 *
 * 한국어
 * ------
 * 2단계 확장은 장식이 아니라 필수입니다. 단일 단계 매크로에서 `#x`는 인자의 *철자*를
 * 문자열화하므로 "2"가 아니라 "POST_SUPERSAMPLE"이라는 텍스트가 만들어집니다. 바깥
 * 매크로가 먼저 한 번 확장을 강제한 뒤 안쪽 매크로가 문자열화합니다.
 *
 * 이 덕분에 상수를 C에서 한 번만 정의하고 GLSL에서 사용할 수 있으며, 이미 한 번
 * 어긋난 적이 있는 수동 동기화 관계가 제거됩니다.
 */
#define POST_STR_(x) #x
#define POST_STR(x)  POST_STR_(x)

/* --- Static variable definitions / 정적 변수 정의 --- */

/** @brief The offscreen framebuffer, its colour texture and its depth buffer. / 오프스크린 프레임버퍼와 그 색상 텍스처 및 깊이 버퍼. */
static GLuint g_fbo, g_colour, g_depth;
/** @brief Empty VAO for the attribute-less full-screen triangle. / 속성 없는 전체 화면 삼각형을 위한 빈 VAO. */
static GLuint g_vao;
/** @brief The resolve program and its uniform locations. / 해상 프로그램과 그 유니폼 위치. */
static GLuint g_prog;
static GLint  g_u_tex, g_u_res;
/** @brief Offscreen dimensions in pixels. / 오프스크린 크기(픽셀). */
static int    g_w, g_h;
/** @brief Non-zero once init succeeded; cleared if the FBO was incomplete. / 초기화 성공 시 0이 아님. FBO가 불완전하면 해제됩니다. */
static int    g_ready;
/** @brief Runtime on/off, independent of whether the path exists. / 경로 존재 여부와 무관한 런타임 on/off. */
static int    g_on = 1;

/* --- Shaders / 셰이더 --- */

/* The triangle is built from gl_VertexID: (0,0), (2,0), (0,2) in UV space
   maps to a triangle that covers the whole [0,1] square after the *2-1
   transform, with the surplus clipped away by the viewport.
   삼각형은 gl_VertexID로부터 생성됩니다. UV 공간의 (0,0), (2,0), (0,2)는 *2-1 변환
   이후 [0,1] 정사각형 전체를 덮는 삼각형이 되며, 남는 부분은 뷰포트가 잘라 냅니다. */
static const char *VS =
"#version 330 core\n"
"out vec2 vUV;\n"
"void main(){\n"
"  vUV = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
"  gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);\n"
"}\n";

/* The dither.
 *
 * ENGLISH
 * -------
 * Four things matter here, and they were not all right the first time. What
 * changed and why, in order of how much it showed:
 *
 * 1. GAMMA. The renderer's output is sRGB-ish, and quantising it directly
 *    puts the steps in the wrong places: equal steps in gamma space are
 *    wildly unequal in perceived brightness, so the dark end crushes while
 *    the bright end wastes levels. Measured on a real frame before this fix,
 *    the dark sky had FOUR distinct colours and topped out at luminance 85,
 *    while the lit floor got twelve. Linearising, quantising, and encoding
 *    back is what spreads the levels evenly across what the eye actually
 *    sees, and it is the single largest quality change in this shader.
 *
 * 2. AN 8x8 MATRIX instead of 4x4. Sixty-four threshold values instead of
 *    sixteen, so the pattern resolves four times as many intermediate tones
 *    before it has to repeat. Still O(1) per pixel with no error buffer --
 *    ordered dithering's whole appeal -- and the larger tile is markedly less
 *    obviously a grid at this pixel size.
 *
 * 3. PER-CHANNEL luminance drive. Biasing all three channels by the same
 *    scalar shifts them together, which drags saturated colours toward grey
 *    as the pattern opens. Applying the bias to the quantisation of each
 *    channel while deriving it from the pixel's luminance keeps the hue and
 *    still opens the pattern in shadow.
 *
 * 4. The GAMMA-SPACE THRESHOLD. The threshold has to be applied where the
 *    quantisation happens -- in linear space here -- or the correction in (1)
 *    is undone by biasing in the wrong space.
 *
 * What did NOT change, deliberately: this stays a pure ordered dither with no
 * temporal component. Animating the matrix over frames (the standard trick
 * for more apparent levels) makes a still image better and a moving one
 * worse: at 320x180 the pattern crawls, and this game is almost never still.
 *
 * 한국어
 * ------
 * 네 가지가 중요하며, 처음부터 전부 옳았던 것은 아닙니다. 무엇을 왜 바꾸었는지를
 * 눈에 띄는 정도 순으로 정리하면:
 *
 * 1. 감마. 렌더러의 출력은 sRGB에 가까운 값이며, 이를 곧바로 양자화하면 단계가
 *    엉뚱한 곳에 놓입니다. 감마 공간에서의 균등한 단계는 지각 밝기로는 전혀
 *    균등하지 않으므로, 어두운 쪽은 뭉개지고 밝은 쪽은 단계를 낭비합니다. 수정
 *    전 실제 프레임을 측정한 결과 어두운 하늘은 색이 *네 가지*뿐이고 휘도 85에서
 *    한계에 달한 반면, 밝은 바닥은 열두 가지를 받았습니다. 선형화하고 양자화한 뒤
 *    다시 인코딩하는 것이 눈이 실제로 보는 범위에 단계를 고르게 펼치며, 이
 *    셰이더에서 가장 큰 품질 변화입니다.
 *
 * 2. 4x4 대신 8x8 행렬. 임계값이 16개가 아닌 64개이므로, 패턴이 반복되기 전까지
 *    네 배 많은 중간 톤을 표현합니다. 오차 버퍼 없이 픽셀당 O(1)이라는 정렬
 *    디더링의 장점은 그대로이며, 이 픽셀 크기에서 큰 타일은 격자처럼 보이는
 *    정도가 눈에 띄게 덜합니다.
 *
 * 3. 채널별 휘도 구동. 세 채널을 같은 스칼라로 편향시키면 함께 이동하므로, 패턴이
 *    열릴 때 채도 높은 색이 회색 쪽으로 끌려갑니다. 편향값은 픽셀의 휘도에서
 *    유도하되 각 채널의 양자화에 적용하면 색상은 유지하면서도 그림자에서 패턴이
 *    열립니다.
 *
 * 4. 임계값을 적용하는 공간. 임계값은 양자화가 일어나는 곳, 즉 여기서는 선형
 *    공간에 적용해야 합니다. 그렇지 않으면 (1)의 보정이 잘못된 공간에서의 편향에
 *    의해 무효가 됩니다.
 *
 * 의도적으로 바꾸지 *않은* 것: 시간 성분 없는 순수 정렬 디더로 유지합니다. 프레임에
 * 걸쳐 행렬을 애니메이션하는 것(더 많은 계조를 얻는 표준 기법)은 정지 화면은
 * 개선하지만 움직이는 화면은 악화시킵니다. 320x180에서는 패턴이 기어 다니며, 이
 * 게임은 정지해 있는 순간이 거의 없습니다.
 */
static const char *FS =
"#version 330 core\n"
"in vec2 vUV;\n"
"out vec4 FragColor;\n"
"uniform sampler2D uTex;\n"
"uniform vec2 uRes;\n"          /* offscreen size, for the matrix lookup */

/* 8x8 Bayer, values 0..63. Written as a flat array indexed by hand because
   GLSL 330 cannot portably initialise a const matrix from a nested list.
   8x8 Bayer, 값 0..63. GLSL 330에서는 중첩 목록으로 const 행렬을 이식성 있게
   초기화할 수 없으므로 직접 인덱싱하는 평면 배열로 작성했습니다. */
"const float BAYER[64] = float[64](\n"
"   0.0, 32.0,  8.0, 40.0,  2.0, 34.0, 10.0, 42.0,\n"
"  48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,\n"
"  12.0, 44.0,  4.0, 36.0, 14.0, 46.0,  6.0, 38.0,\n"
"  60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,\n"
"   3.0, 35.0, 11.0, 43.0,  1.0, 33.0,  9.0, 41.0,\n"
"  51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,\n"
"  15.0, 47.0,  7.0, 39.0, 13.0, 45.0,  5.0, 37.0,\n"
"  63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0);\n"

/* Steps per channel. 4 keeps enough colour to read the materials apart -- a
   true 1-bit look would throw away the blued/steel/walnut contrast the gun
   depends on.
   채널당 단계 수입니다. 4단계는 재질을 구분할 만큼의 색을 남깁니다. 진정한 1비트
   룩은 총기가 의존하는 블루잉/강철/호두나무의 대비를 버리게 됩니다. */
"const float LEVELS = 4.0;\n"

/* How strongly luminance opens and closes the pattern. 0 is a plain ordered
   dither; 1 is the full shading effect. Above ~1.2 the dark end crushes.
   휘도가 패턴을 얼마나 강하게 여닫는지를 정합니다. 0이면 평범한 정렬 디더이고 1이면
   음영 효과가 온전히 적용됩니다. 약 1.2를 넘으면 어두운 쪽이 뭉갭니다. */
"const float LUMA_DRIVE = 0.50;\n"

/* How much per-channel colour survives the dither. 0 keeps the hue exactly
   and reads flatter; 1 is the raw per-channel quantisation, which speckles
   neutral surfaces with coloured dots. 0.35 keeps materials distinguishable
   without the confetti.
   채널별 색상이 디더를 얼마나 견디는지를 정합니다. 0이면 색상이 정확히 보존되지만 더
   평평해 보이고, 1이면 중성 표면에 색점이 흩뿌려지는 원본 채널별 양자화입니다.
   0.35는 반점 없이 재질을 구분할 수 있게 합니다. */
"const float SATURATION = 0.35;\n"

/* Size of one dither cell, in offscreen pixels.
 *
 * The dither does NOT have to share the render resolution. At 1.0 each cell
 * is one rendered pixel, which is the densest the pattern can be and makes it
 * read as noise at this scale. Above 1.0 the matrix is sampled on a coarser
 * grid, so each dither dot covers several pixels and the stipple becomes
 * legible as a pattern rather than as static -- the visible texture of the
 * effect, independent of how blocky the geometry is.
 *
 * This is the dial for "less pixelated dithering": raising the render
 * resolution and raising this together keeps the dots the same apparent size
 * while the world itself gets sharper.
 *
 * 디더 셀 하나의 크기이며 오프스크린 픽셀 단위입니다.
 *
 * 디더가 렌더 해상도를 공유해야 할 이유는 없습니다. 1.0이면 각 셀이 렌더링된 픽셀
 * 하나이며, 이는 패턴이 가장 조밀해지는 값으로 이 크기에서는 잡음처럼 보입니다.
 * 1.0보다 크면 더 성긴 격자에서 행렬을 샘플링하므로 디더 점 하나가 여러 픽셀을 덮게
 * 되고, 스티플이 노이즈가 아닌 하나의 패턴으로 읽힙니다. 지오메트리가 얼마나 각졌는지와
 * 무관한, 효과 자체의 시각적 질감입니다.
 *
 * "덜 픽셀화된 디더링"을 위한 조정값입니다. 렌더 해상도와 이 값을 함께 올리면 점의
 * 겉보기 크기는 유지되면서 월드 자체는 선명해집니다. */
"const float DITHER_SCALE = 1.0;\n"

/* Rendered samples per art pixel, per axis, GENERATED from the C constant.
 *
 * This used to be a literal `2.0` with a comment asking whoever edited
 * POST_SUPERSAMPLE to remember to change it here too. That is exactly the
 * arrangement PROC_* has with the fragment shader in render.c, and it is
 * tolerable there because a mismatch draws the wrong pattern. Here a mismatch
 * puts the block boundaries in the wrong place: the C side allocates a buffer
 * of one size and this side averages blocks of another, so the image tears
 * into misaligned tiles. It went out of sync once during development and the
 * symptom did not name its cause.
 *
 * The shader is assembled from C string literals, so the preprocessor can
 * simply write the number in. One definition, no pairing to keep.
 *
 * 아트 픽셀당 렌더링 샘플 수이며 축별 값으로, C 상수로부터 *생성*됩니다.
 *
 * 이전에는 리터럴 `2.0`을 두고, POST_SUPERSAMPLE을 수정하는 사람이 이곳도 함께 바꾸기를
 * 당부하는 주석이 붙어 있었습니다. 이는 render.c의 프래그먼트 셰이더와 PROC_*가 맺고
 * 있는 것과 동일한 관계이며, 그쪽에서는 불일치가 잘못된 패턴을 그리는 데 그치므로
 * 감수할 만합니다. 그러나 이곳에서 불일치는 블록 경계를 엉뚱한 위치에 놓습니다. C 쪽은
 * 한 가지 크기로 버퍼를 할당하는데 이쪽은 다른 크기로 블록을 평균 내므로, 화면이
 * 어긋난 타일로 찢어집니다. 개발 중 실제로 한 번 어긋났으며, 그 증상은 원인을 알려
 * 주지 않았습니다.
 *
 * 셰이더는 C 문자열 리터럴로 조립되므로 전처리기가 숫자를 그대로 써넣을 수 있습니다.
 * 정의는 하나뿐이며, 유지해야 할 짝이 없습니다. */
"const float SUPERSAMPLE = " POST_STR(POST_SUPERSAMPLE) ".0;\n"

/* sRGB <-> linear. The cheap pow(2.2) approximation rather than the exact
   piecewise curve: this is a stylised 4-level output, and the difference
   between the two is far below one quantisation step. The exact curve would
   cost two more branches per channel for no visible gain.
   sRGB <-> 선형 변환입니다. 정확한 구간별 곡선 대신 저렴한 pow(2.2) 근사를
   사용합니다. 이것은 양식화된 4단계 출력이며 두 방식의 차이는 양자화 한 단계보다
   훨씬 작습니다. 정확한 곡선은 채널당 분기를 두 개 더 쓰면서 눈에 보이는 이득이
   없습니다. */
"vec3 toLinear(vec3 c){ return pow(max(c, 0.0), vec3(2.2)); }\n"
"vec3 toGamma (vec3 c){ return pow(max(c, 0.0), vec3(1.0/2.2)); }\n"

"void main(){\n"

/* Resolve one art pixel by AVERAGING the block of rendered samples it covers.
 *
 * This is the difference between a pixel-shaped filter and actual
 * pixelisation, and the pass did not have it at first. Rendering small and
 * magnifying makes the image blocky, but the rasteriser still produces each
 * of those pixels from a SINGLE sample at its centre -- so a wall edge, a
 * distant railing or a thin sliver of geometry either lands on that centre
 * and appears at full strength, or misses it and vanishes. Nothing in
 * between, and nothing combining a pixel with what is around it. Measured on
 * a real frame, 44% of the colour transitions across a floor/wall boundary
 * were hard jumps with no intermediate value at all.
 *
 * Real pixel art averages the area a pixel covers, which is what gives an
 * edge its intermediate tones. So the world is now rendered at SUPERSAMPLE
 * times the art resolution and each art pixel is the mean of its block: an
 * edge crossing a quarter of the block contributes a quarter of its colour.
 * The image stays exactly as blocky -- the art resolution has not changed --
 * but each block is now a considered average instead of a lucky sample.
 *
 * Averaged in LINEAR light, not gamma. Averaging gamma-encoded values
 * underestimates the result: two samples of 0.0 and 1.0 average to 0.5 in
 * gamma, which is only 21% of the light they actually carry, so edges against
 * a bright background come out too dark. This is the same two-spaces rule the
 * quantisation below follows.
 *
 * 하나의 아트 픽셀을, 그것이 덮는 렌더링 샘플 블록을 *평균 내어* 해상합니다.
 *
 * 이것이 픽셀 모양 필터와 실제 픽셀화의 차이이며, 이 패스에는 처음에 이 기능이
 * 없었습니다. 작게 렌더링하고 확대하면 화면이 각져 보이지만, 래스터라이저는 여전히 각
 * 픽셀을 그 중심의 *단 하나의* 샘플로 생성합니다. 따라서 벽의 모서리나 멀리 있는
 * 난간, 얇은 지오메트리 조각은 그 중심에 걸리면 온전한 강도로 나타나고 빗나가면
 * 사라집니다. 중간이 없고, 픽셀을 주변과 합치는 과정도 없습니다. 실제 프레임을 측정한
 * 결과, 바닥과 벽 경계를 가로지르는 색 전환의 44%가 중간값이 전혀 없는 급격한
 * 도약이었습니다.
 *
 * 실제 픽셀 아트는 픽셀이 덮는 면적을 평균 내며, 그것이 모서리에 중간 톤을 부여합니다.
 * 그래서 이제 월드를 아트 해상도의 SUPERSAMPLE배로 렌더링하고 각 아트 픽셀을 그
 * 블록의 평균으로 만듭니다. 블록의 4분의 1을 가로지르는 모서리는 자기 색의 4분의 1을
 * 기여합니다. 아트 해상도는 그대로이므로 화면이 각진 정도는 정확히 동일하지만, 이제 각
 * 블록은 운 좋게 걸린 샘플이 아니라 계산된 평균입니다.
 *
 * 감마가 아닌 *선형* 광량에서 평균 냅니다. 감마 인코딩된 값을 평균 내면 결과가
 * 과소평가됩니다. 0.0과 1.0 두 샘플은 감마 공간에서 0.5로 평균되는데 이는 실제로 그
 * 둘이 운반하는 빛의 21%에 불과하므로, 밝은 배경을 배경으로 한 모서리가 지나치게
 * 어둡게 나옵니다. 아래의 양자화가 따르는 것과 동일한 두 공간 규칙입니다. */
"  vec3 accum = vec3(0.0);\n"
"  vec2 texel = 1.0 / (uRes * SUPERSAMPLE);\n"
"  for (int sy = 0; sy < int(SUPERSAMPLE); ++sy)\n"
"    for (int sx = 0; sx < int(SUPERSAMPLE); ++sx) {\n"
/* Sample at the CENTRE of each sub-texel (+0.5), or the block is offset by
   half a sample and the average drifts toward one corner.
   각 서브 텍셀의 *중심*에서 샘플링합니다(+0.5). 그렇지 않으면 블록이 반 샘플만큼
   어긋나 평균이 한쪽 모서리로 치우칩니다. */
"      vec2 off = (vec2(sx, sy) + 0.5) * texel;\n"
"      vec2 base = floor(vUV * uRes) / uRes;\n"
"      accum += toLinear(texture(uTex, base + off).rgb);\n"
"    }\n"
"  vec3 src = toGamma(accum / (SUPERSAMPLE * SUPERSAMPLE));\n"

/* Matrix cell from the OFFSCREEN pixel coordinate, not the window's. Using
   the window's would shrink the pattern as the window grew, so the dither
   would stop being locked to the pixel grid it belongs to.
   창이 아닌 *오프스크린* 픽셀 좌표로 행렬 셀을 결정합니다. 창 좌표를 쓰면 창이
   커질수록 패턴이 작아져, 디더가 원래 속해야 할 픽셀 격자에서 분리됩니다. */
"  ivec2 p = ivec2(vUV * uRes / DITHER_SCALE);\n"
"  float t = BAYER[(p.y & 7) * 8 + (p.x & 7)] / 64.0;\n"

/* Luminance of the already-lit pixel, in LINEAR light -- Rec. 709 weights are
   defined against linear intensity, and applying them to gamma-encoded values
   overstates the darks.
   이미 조명이 적용된 픽셀의 휘도이며 *선형* 광량 기준입니다. Rec. 709 가중치는 선형
   강도에 대해 정의되어 있으며, 감마 인코딩된 값에 적용하면 어두운 부분이 과장됩니다. */
"  vec3  lin  = toLinear(src);\n"
"  float lumL = dot(lin, vec3(0.2126, 0.7152, 0.0722));\n"
/* ...then re-encoded, because the BIAS wants a perceptual quantity. Linear
   luminance is tiny across the whole shadow range -- src 0.05 and 0.20 差
   only slightly -- so a bias driven by it saturates and every dark
   pixel gets the same threshold, which is flat banding rather than dither.
   Re-encoding spreads the shadows back out so the drive stays proportional.
   (src 0.05 and 0.20 differ by only 0.001 vs 0.029 in linear light.)
   ...그런 다음 다시 인코딩합니다. *편향값*은 지각적인 양을 필요로 하기 때문입니다.
   선형 휘도는 어두운 영역 전체에서 극히 작아서(src 0.05와 0.20이 각각 0.001과
   0.029에 불과합니다), 이를 기준으로 한 편향은 포화되어 모든 어두운 픽셀이 동일한
   임계값을 받게 되며, 이는 디더가 아니라 평평한 밴딩입니다. 다시 인코딩하면 어두운
   영역이 도로 펼쳐져 구동값이 비례를 유지합니다. */
"  float lum  = toGamma(vec3(lumL)).r;\n"

/* The luminance drive. Centring on 0.5 leaves mid grey alone and pushes the
   threshold in opposite directions at the two ends, so the pattern opens in
   shadow and closes in light instead of merely getting darker.
   휘도 구동부입니다. 0.5를 중심으로 삼으면 중간 회색은 그대로 두고 양 끝에서 임계값이
   반대 방향으로 밀리므로, 패턴이 단순히 어두워지는 대신 그림자에서 열리고 빛에서
   닫힙니다. */
"  float bias = (0.5 - lum) * LUMA_DRIVE;\n"
"  float th   = clamp(t + bias, 0.0, 1.0) - 0.5;\n"

/* Quantise in GAMMA space, on the source value.
 *
 * This is the opposite of what the usual advice says, and getting it backwards
 * is what the first attempt did. Ordinary dithering targets a fixed output
 * format with many levels, so linear quantisation spreads its error correctly.
 * With only FOUR levels the reverse is true: linear space packs almost all of
 * its resolution into the top of the range, so four evenly spaced linear steps
 * land at roughly 0%, 62%, 82% and 100% perceptual brightness and everything
 * below mid grey collapses onto a single step. Simulated across the full
 * ramp, every source value from 0.05 to 0.50 produced the same two outputs --
 * which on screen was a washed-out speckle with the gun almost invisible.
 *
 * Gamma space is already perceptually spaced, so four steps there are four
 * evenly perceived tones. Linear light is still used for the LUMINANCE above,
 * because the Rec. 709 weights are defined against physical intensity; the
 * two spaces are used for the two things each is correct for.
 *
 * *감마* 공간에서, 원본 값을 대상으로 양자화합니다.
 *
 * 이는 일반적인 조언과 반대이며, 첫 시도가 바로 그 반대로 했습니다. 통상적인 디더링은
 * 단계가 많은 고정 출력 형식을 목표로 하므로 선형 양자화가 오차를 올바르게 분산시킵니다.
 * 그러나 단계가 *네 개*뿐이면 정반대가 됩니다. 선형 공간은 해상도의 대부분을 범위
 * 상단에 몰아넣으므로, 균등한 선형 4단계는 지각 밝기 기준 약 0%, 62%, 82%, 100%에
 * 놓이게 되고 중간 회색 이하의 모든 것이 한 단계로 붕괴합니다. 전 구간을 시뮬레이션한
 * 결과 0.05부터 0.50까지의 모든 원본 값이 동일한 두 가지 출력만 냈으며, 화면에서는
 * 총기가 거의 보이지 않는 바랜 얼룩으로 나타났습니다.
 *
 * 감마 공간은 이미 지각적으로 균등하게 배치되어 있으므로, 그곳에서의 네 단계는 지각적으로
 * 균등한 네 가지 톤이 됩니다. 위쪽의 *휘도*에는 여전히 선형 광량을 사용하는데, Rec. 709
 * 가중치가 물리적 강도에 대해 정의되어 있기 때문입니다. 두 공간을 각각이 옳은 용도에
 * 맞게 사용합니다.
 *
 * Dividing by LEVELS-1 after the floor puts the top step at exactly 1.0;
 * dividing by LEVELS leaves the brightest output at 0.75 and the whole image
 * reads washed out.
 * floor 이후 LEVELS-1로 나누면 최상위 단계가 정확히 1.0이 됩니다. LEVELS로 나누면
 * 가장 밝은 출력이 0.75에 머물러 화면 전체가 바랜 것처럼 보입니다. */
"  vec3 q = floor(src * (LEVELS - 1.0) + 0.5 + th) / (LEVELS - 1.0);\n"

/* Pull the result back toward a hue-preserving version of itself.
 *
 * The quantisation above is per channel, and independent channels are what
 * break the hue: a near-neutral dark grey like (0.22, 0.23, 0.26) has its
 * three channels sitting either side of the same threshold, so at some matrix
 * cells it resolves to (0, 0, 85) -- a pure blue dot in what should be grey.
 * Across a wall that reads as coloured confetti, and it was the most visible
 * flaw left after the gamma work.
 *
 * The fix is to quantise the pixel's LUMINANCE once, with the same threshold,
 * and rescale the original colour to that brightness. One decision for the
 * whole pixel means the three channels can no longer disagree, so the hue
 * survives the dither. SATURATION mixes between the two: 0 is fully
 * hue-preserving and slightly flatter, 1 is the original per-channel result.
 *
 * 결과를 색상이 보존되는 형태로 되돌립니다.
 *
 * 위의 양자화는 채널별로 이루어지며, 채널이 서로 독립적인 것이 색상을 깨뜨리는
 * 원인입니다. (0.22, 0.23, 0.26) 같은 중성에 가까운 어두운 회색은 세 채널이 동일한
 * 임계값의 양쪽에 걸쳐 있으므로, 일부 행렬 셀에서는 (0, 0, 85), 즉 회색이어야 할 곳에
 * 순수한 파란 점으로 해상됩니다. 벽 전체에 걸치면 색색의 반점처럼 보이며, 감마 작업
 * 이후 남아 있던 가장 눈에 띄는 결함이었습니다.
 *
 * 해결책은 픽셀의 *휘도*를 동일한 임계값으로 한 번만 양자화하고, 원본 색상을 그 밝기에
 * 맞춰 재조정하는 것입니다. 픽셀 전체에 대해 결정이 하나뿐이므로 세 채널이 서로
 * 어긋날 수 없고, 따라서 색상이 디더를 견뎌 냅니다. SATURATION이 둘 사이를
 * 혼합합니다. 0이면 색상이 완전히 보존되고 약간 평평해지며, 1이면 기존의 채널별
 * 결과가 됩니다. */
"  float ql = floor(lum * (LEVELS - 1.0) + 0.5 + th) / (LEVELS - 1.0);\n"
"  vec3  hue = src / max(lum, 1e-3);\n"
"  vec3  qh  = clamp(hue * ql, 0.0, 1.0);\n"

"  FragColor = vec4(clamp(mix(qh, q, SATURATION), 0.0, 1.0), 1.0);\n"
"}\n";

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static GLuint compile(GLenum type, const char *src);

/* --- Public function definitions / 공개 함수 정의 --- */

int post_init(int width, int height) {
    if (width  < 1) width  = 1;
    if (height < 1) height = 1;
    if (width > POST_MAX_WIDTH) width = POST_MAX_WIDTH;
    /* g_w/g_h stay the ART resolution -- the size the image is quantised and
       dithered at, and what uRes reports to the shader. The framebuffer
       itself is POST_SUPERSAMPLE times larger in each axis, and the shader
       averages each block back down. Keeping the art size as the primary
       value means the dither grid and the pixel grid are unaffected by the
       supersample factor: raising it makes edges smoother without making the
       pixels smaller.
       g_w/g_h는 *아트* 해상도로 유지됩니다. 이미지가 양자화되고 디더링되는 크기이며
       uRes가 셰이더에 보고하는 값입니다. 프레임버퍼 자체는 각 축으로
       POST_SUPERSAMPLE배 크며, 셰이더가 각 블록을 다시 평균 냅니다. 아트 크기를
       기준값으로 두면 디더 격자와 픽셀 격자가 슈퍼샘플 배수의 영향을 받지 않습니다.
       이 값을 올리면 픽셀이 작아지지 않으면서 모서리만 부드러워집니다. */
    g_w = width; g_h = height;
    int fbw = g_w * POST_SUPERSAMPLE, fbh = g_h * POST_SUPERSAMPLE;

    /* Colour target. GL_NEAREST on BOTH filters is the pixelisation: nothing
       else in this file makes the image blocky, and switching these to
       GL_LINEAR turns the whole effect off while leaving the dither on.
       색상 타깃입니다. 양쪽 필터 모두 GL_NEAREST인 것이 곧 픽셀화입니다. 이 파일의
       다른 어떤 코드도 화면을 각지게 만들지 않으며, 이 값을 GL_LINEAR로 바꾸면
       디더는 남은 채 픽셀화 효과만 사라집니다. */
    glGenTextures(1, &g_colour);
    glBindTexture(GL_TEXTURE_2D, g_colour);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbw, fbh, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    /* Clamp, or the dither's edge pixels sample from the opposite side.
       클램프하지 않으면 디더의 가장자리 픽셀이 반대편에서 샘플링됩니다. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Depth as a renderbuffer rather than a texture: nothing samples it, and
       a renderbuffer is the cheaper object for a write-only attachment.
       깊이는 텍스처가 아닌 렌더버퍼로 둡니다. 아무것도 이를 샘플링하지 않으며, 쓰기
       전용 첨부물로는 렌더버퍼가 더 저렴한 객체입니다. */
    glGenRenderbuffers(1, &g_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, fbw, fbh);

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_colour, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, g_depth);

    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) { post_shutdown(); return 0; }

    /* Both stages, then one failure check that cleans up whichever survived.
     *
     * The obvious form -- `if (!vs || !fs) { post_shutdown(); return 0; }` --
     * leaks. post_shutdown only knows about g_prog, and g_prog does not exist
     * yet at this point, so a vertex shader that compiled while the fragment
     * shader failed is never deleted. The shaders belong to this function
     * until glCreateProgram takes them, and this is the window where that
     * ownership has to be discharged by hand.
     *
     * mesh.c's scratch allocation does the same thing for the same reason:
     * two acquisitions, either of which can fail, and a cleanup that has to
     * release whichever half succeeded.
     *
     * 두 단계를 모두 컴파일한 뒤, 살아남은 쪽을 정리하는 실패 검사를 한 번 수행합니다.
     *
     * 명백해 보이는 형태(`if (!vs || !fs) { post_shutdown(); return 0; }`)는
     * 누수를 일으킵니다. post_shutdown은 g_prog만 알고 있는데 이 시점에는 g_prog가
     * 아직 존재하지 않으므로, 프래그먼트 셰이더가 실패하는 동안 컴파일에 성공한 정점
     * 셰이더가 결코 삭제되지 않습니다. 셰이더는 glCreateProgram이 가져가기 전까지 이
     * 함수의 소유이며, 이곳이 그 소유권을 직접 해제해야 하는 구간입니다.
     *
     * mesh.c의 임시 메모리 할당도 동일한 이유로 동일한 처리를 합니다. 각각 실패할 수
     * 있는 두 번의 획득과, 성공한 쪽을 해제해야 하는 정리 과정입니다. */
    GLuint vs = compile(GL_VERTEX_SHADER,   VS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        post_shutdown();
        return 0;
    }

    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &linked);
    if (!linked) { post_shutdown(); return 0; }

    g_u_tex = glGetUniformLocation(g_prog, "uTex");
    g_u_res = glGetUniformLocation(g_prog, "uRes");

    /* An empty VAO is still required: core profile refuses to draw with no
       vertex array bound, even when the shader reads no attributes.
       빈 VAO라도 반드시 필요합니다. 코어 프로파일은 셰이더가 어떤 속성도 읽지 않더라도
       정점 배열이 바인딩되지 않으면 그리기를 거부합니다. */
    glGenVertexArrays(1, &g_vao);

    g_ready = 1;
    return 1;
}

int post_enabled(void) { return g_ready && g_on; }

void post_set_enabled(int on) { g_on = on ? 1 : 0; }

float post_begin(void) {
    if (!post_enabled()) return 0.0f;
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    /* The world is rasterised at the FULL framebuffer size; the shader
       averages it down to the art resolution on resolve.
       월드는 프레임버퍼 전체 크기로 래스터화되며, 셰이더가 해상 시점에 아트
       해상도로 평균 냅니다. */
    glViewport(0, 0, g_w * POST_SUPERSAMPLE, g_h * POST_SUPERSAMPLE);
    /* The offscreen aspect, NOT the window's -- see the warning in post.h. */
    return (float)g_w / (float)g_h;
}

void post_end(int win_w, int win_h) {
    if (!post_enabled()) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w > 0 ? win_w : 1, win_h > 0 ? win_h : 1);

    /* The resolve overwrites every pixel, so there is nothing to test against
       and nothing to blend with. Both are restored by the caller for the HUD.
       해상 패스는 모든 픽셀을 덮어쓰므로 비교할 대상도 혼합할 대상도 없습니다. 양쪽 다
       HUD를 위해 호출자가 복원합니다. */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    glUseProgram(g_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_colour);
    glUniform1i(g_u_tex, 0);
    glUniform2f(g_u_res, (float)g_w, (float)g_h);

    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    /* Hand the renderer's program back.
     *
     * rd_init binds it once and everything after assumes it is still current:
     * rd_mode, rd_color and rd_mvp set uniforms without binding. Leaving this
     * pass's program bound therefore does not merely draw the wrong thing --
     * it sends every later uniform to a program that is not drawing, and the
     * HUD's font texture ends up stretched across this triangle over the
     * entire screen. That is exactly what it did before this line existed.
     *
     * Restoring here rather than asking main.c to do it keeps the invariant
     * with the code that breaks it: a pass that binds its own program is
     * responsible for putting the renderer's back.
     *
     * 렌더러의 프로그램을 되돌려 줍니다.
     *
     * rd_init이 한 번 바인딩하면 이후의 모든 코드가 그것이 유효하다고 가정합니다.
     * rd_mode, rd_color, rd_mvp는 바인딩 없이 유니폼만 설정합니다. 따라서 이 패스의
     * 프로그램을 바인딩된 채로 두면 단순히 잘못된 것을 그리는 데 그치지 않고, 이후의
     * 모든 유니폼이 실제로 그리지 않는 프로그램으로 전달되며, HUD의 폰트 텍스처가 이
     * 삼각형에 늘어난 채 화면 전체를 덮게 됩니다. 이 줄이 없었을 때 실제로 그런 일이
     * 벌어졌습니다.
     *
     * main.c에 맡기지 않고 여기서 복원하는 이유는, 불변식을 깨뜨리는 코드 곁에 그
     * 불변식을 두기 위해서입니다. 자체 프로그램을 바인딩하는 패스가 렌더러의 것을
     * 되돌려 놓을 책임을 집니다. */
    rd_use();
}

void post_shutdown(void) {
    if (g_fbo)    { glDeleteFramebuffers(1, &g_fbo);      g_fbo = 0; }
    if (g_depth)  { glDeleteRenderbuffers(1, &g_depth);   g_depth = 0; }
    if (g_colour) { glDeleteTextures(1, &g_colour);       g_colour = 0; }
    if (g_vao)    { glDeleteVertexArrays(1, &g_vao);      g_vao = 0; }
    if (g_prog)   { glDeleteProgram(g_prog);              g_prog = 0; }
    g_ready = 0;
}

/* --- Static helper function definitions / 정적 헬퍼 함수 정의 --- */

/**
 * @brief Compiles one shader stage.
 *
 * ENGLISH
 * -------
 * @param[in] type GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
 * @param[in] src  Null-terminated GLSL source.
 * @return The shader name, or 0 on a compile error.
 * @note Deletes the shader itself on failure, so a caller that gets 0 owns
 *       nothing and can simply give up.
 *
 * 한국어
 * ------
 * @brief 셰이더 단계 하나를 컴파일합니다.
 * @param[in] type GL_VERTEX_SHADER 또는 GL_FRAGMENT_SHADER.
 * @param[in] src  널로 끝나는 GLSL 소스.
 * @return 셰이더 이름. 컴파일 오류 시 0.
 * @note 실패 시 셰이더 자체를 삭제하므로, 0을 받은 호출자는 아무것도 소유하지 않으며
 *       그대로 포기하면 됩니다.
 */
static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}
