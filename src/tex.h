/**
 * @file tex.h
 * @brief Procedural textures, driven by a text recipe language.
 *
 * ENGLISH
 * -------
 * Nothing here loads an image. A material is a short list of integer ops --
 * "fill with this colour, lay a brick lattice, add grain, bevel the edges" --
 * and the interpreter reconstructs the pixels at startup.
 *
 * The recipes live in TEX_RECIPES as one embedded string. Text beats a float
 * table for storage (a 3-float vector is 12 bytes; "0 1 -62" is 7), it is what
 * the editor reads and writes, and its narrow alphabet compresses far better
 * under a final exe packer than binary floats do.
 *
 * Every number in the language is an INTEGER with a fixed implied scale, so
 * the parser needs no float handling at all -- see the op table in tex.c.
 *
 * 한국어
 * ------
 * 이곳의 어떤 코드도 이미지를 로드하지 않습니다. 재질은 "이 색으로 채우고, 벽돌
 * 격자를 깔고, 결을 넣고, 가장자리를 깎아라" 같은 짧은 정수 명령 목록이며,
 * 인터프리터가 시작 시점에 픽셀을 재구성합니다.
 *
 * 레시피는 TEX_RECIPES에 하나의 내장 문자열로 존재합니다. 저장 측면에서 텍스트가
 * float 테이블보다 유리하고(3개의 float 벡터는 12바이트지만 "0 1 -62"는
 * 7바이트입니다), 에디터가 읽고 쓰는 형식이며, 좁은 문자 집합 덕분에 최종 exe
 * 패커에서 이진 float보다 훨씬 잘 압축됩니다.
 *
 * 이 언어의 모든 숫자는 고정된 암묵적 배율을 가진 *정수*이므로, 파서는 부동소수점
 * 처리를 전혀 필요로 하지 않습니다. tex.c의 명령 테이블을 참조하십시오.
 */
#ifndef TEX_H
#define TEX_H

#include "gl.h"

/* --- Capacity limits / 용량 제한 --- */

/**
 * @brief Longest material name ::tex_mat can cache, including the terminator.
 *
 * ENGLISH
 * -------
 * Published because it is a real constraint on callers, not an internal
 * detail: a longer name is rebuilt from its recipe on every lookup instead of
 * being cached, because a truncated copy could never match the full name.
 *
 * @note Must be at least `LVL_MAT`, the authoring limit for a material name.
 *       weapon.c asserts that at compile time -- it already includes both this
 *       header and level.h and passes level-authored names here, so the check
 *       lives there rather than dragging the simulation header in.
 *
 * 한국어
 * ------
 * @brief ::tex_mat이 캐시할 수 있는 재질 이름의 최대 길이입니다. 종료 문자를 포함합니다.
 *
 * 내부 구현이 아니라 호출자에 대한 실제 제약이므로 공개합니다. 이보다 긴 이름은
 * 캐시되지 않고 조회할 때마다 레시피로부터 재생성됩니다. 잘린 사본은 전체 이름과 결코
 * 일치할 수 없기 때문입니다.
 *
 * @note 재질 이름의 제작 상한인 `LVL_MAT` 이상이어야 합니다. weapon.c가 이를 컴파일
 *       시점에 검사합니다. 그 파일은 이 헤더와 level.h를 모두 포함하며 레벨에서 제작된
 *       이름을 이곳에 전달하므로, 시뮬레이션 헤더를 이곳으로 끌어들이는 대신 그곳에
 *       검사를 두었습니다.
 */
#define TEX_NAME_MAX 16

/** @brief Edge of a rasterised material, in pixels. / 래스터화된 재질 한 변의 픽셀 수. */
#define TEX_SIZE 256

/* --- Type definitions / 타입 정의 --- */

/**
 * @struct Mat
 * @brief A material as the renderer wants it: either pixels, or a formula.
 *
 * ENGLISH
 * -------
 * @note A procedural material has no texture at all -- the fragment shader
 *       computes the surface from the UV (see the PROC_* list in render.h),
 *       so it stays sharp with the player's nose against it and costs four
 *       uniforms instead of 256KB.
 * @note `base` and `gloss` in the recipe supply `rgb` and `params[0]`, so
 *       switching a material between the two paths is a one-line edit.
 *
 * 한국어
 * ------
 * 렌더러가 필요로 하는 형태의 재질입니다. 픽셀이거나 수식입니다.
 * @note 절차적 재질은 텍스처를 전혀 갖지 않습니다. 프래그먼트 셰이더가 UV로부터
 *       표면을 계산하므로(render.h의 PROC_* 목록 참조), 플레이어가 코앞까지
 *       다가가도 선명하게 유지되며 256KB 대신 4개의 유니폼만 소모합니다.
 * @note 레시피의 `base`와 `gloss`가 `rgb`와 `params[0]`을 제공하므로, 두 방식
 *       사이에서 재질을 전환하는 것은 한 줄 수정으로 끝납니다.
 */
typedef struct {
    GLuint tex;        /**< GL texture name; 0 when procedural. / GL 텍스처 이름. 절차적 재질이면 0입니다. */
    int    proc;       /**< Procedural shader id; PROC_TEXTURE (0) means "sample tex". / 절차적 셰이더 id. PROC_TEXTURE(0)는 "텍스처를 샘플링하라"는 의미입니다. */
    float  rgb[3];     /**< Base colour the shader tints with. / 셰이더가 색조로 사용하는 기본 색상. */
    float  scale;      /**< Pattern cells per UV unit. / UV 단위당 패턴 셀의 수. */
    /**
     * @brief x = gloss, y = normal-map strength, z = flow speed.
     *
     * ENGLISH
     * -------
     * `y` is set by the `bump` op and read only on the procedural path. The
     * shader differences the material's own luminance a texel apart to get a
     * surface gradient and tilts the shading normal by it, so relief costs no
     * second texture and no per-vertex tangent -- see procNormal in render.c.
     * Zero leaves the surface flat, which is what every material that does not
     * mention `bump` gets.
     *
     * `z` is set by the `flow` op and read on BOTH paths, unlike `y`. It makes
     * the surface move: the UV drifts and the shading normal rocks, so a liquid
     * is a material that says it is one rather than a texture name the renderer
     * has to recognise. Zero is a surface that holds still.
     *
     * 한국어
     * ------
     * @brief x는 광택, y는 노멀 맵 강도, z는 흐름 속도입니다.
     *
     * `y`는 `bump` 명령이 설정하며 절차적 경로에서만 읽습니다. 셰이더가 재질 자신의
     * 휘도를 텍셀 간격으로 차분해 표면 기울기를 구하고 그만큼 셰이딩 법선을 기울이므로,
     * 요철에 두 번째 텍스처도 정점별 탄젠트도 들지 않습니다. render.c의 procNormal을
     * 참조하십시오. 0이면 표면이 평평하게 유지되며, `bump`를 언급하지 않는 모든 재질이
     * 그 값을 갖습니다.
     */
    float  params[3];
} Mat;

/* --- Public function prototypes: materials / 공개 함수 프로토타입: 재질 --- */

/**
 * @brief Looks up a material by name, building and caching it on first use.
 *
 * ENGLISH
 * -------
 * @param[in] name Material name as written in the recipe text.
 * @return The material. An unknown name yields a usable fallback rather than
 *         an error, so a typo shows up as a wrong-looking surface instead of
 *         a crash.
 * @warning Requires a current GL context: a cache miss uploads a texture.
 * @note The returned ::Mat is always owned by the cache, never by the caller:
 *       do not delete its texture. When the material cannot be cached -- the
 *       cache is full, or the name is longer than ::TEX_NAME_MAX -- the
 *       texture is released before returning rather than handed over, so the
 *       result is a valid procedural material with `tex` of 0. Draws stay
 *       correct; only the caching is lost, and DIAG_TEX_CACHE records it.
 *
 * 한국어
 * ------
 * @brief 이름으로 재질을 조회하며, 최초 사용 시 생성하여 캐시합니다.
 * @param[in] name 레시피 텍스트에 기록된 재질 이름.
 * @return 해당 재질. 알 수 없는 이름은 오류 대신 사용 가능한 대체 값을 반환하므로,
 *         오타는 충돌이 아니라 잘못 보이는 표면으로 나타납니다.
 * @warning 활성 GL 컨텍스트가 필요합니다. 캐시에 없으면 텍스처를 업로드하기
 *          때문입니다.
 * @note 반환된 ::Mat은 항상 캐시가 소유하며 호출자가 소유하지 않습니다. 해당 텍스처를
 *       삭제하지 마십시오. 재질을 캐시할 수 없는 경우(캐시가 가득 찼거나 이름이
 *       ::TEX_NAME_MAX보다 긴 경우)에는 텍스처를 넘겨주지 않고 반환 전에 해제하므로,
 *       결과는 `tex`가 0인 유효한 절차적 재질이 됩니다. 그리기는 계속 올바르며 캐싱만
 *       사라집니다. 그 사실은 DIAG_TEX_CACHE에 기록됩니다.
 */
Mat  tex_mat(const char *name);

/**
 * @brief Selects a material for the next draw.
 *
 * ENGLISH
 * -------
 * @param[in] m Material to bind.
 * @note Binds the texture for a sampled material, or uploads the shader
 *       uniforms for a procedural one; the caller does not need to know which.
 * @warning Requires a current GL context and an active shader program.
 *
 * 한국어
 * ------
 * @brief 다음 그리기 작업에 사용할 재질을 선택합니다.
 * @param[in] m 바인딩할 재질.
 * @note 샘플링 방식 재질이면 텍스처를 바인딩하고, 절차적 재질이면 셰이더 유니폼을
 *       업로드합니다. 호출자는 둘 중 어느 쪽인지 알 필요가 없습니다.
 * @warning 활성 GL 컨텍스트와 활성화된 셰이더 프로그램이 필요합니다.
 */
void tex_use(const Mat *m);

/**
 * @brief Runs the named recipe and returns a mipmapped, repeating GL texture.
 *
 * ENGLISH
 * -------
 * @param[in] name Recipe name to execute.
 * @return The GL texture name, or 0 if no recipe by that name exists.
 * @note The CPU staging buffer is allocated, used and freed inside -- it
 *       never reaches .data.
 * @note RGB is colour; ALPHA is gloss, set by the `gloss` op. Nothing samples
 *       alpha for transparency, so per-pixel gloss rides along for free -- no
 *       second texture, no second sampler, no per-draw uniform.
 * @warning The returned texture is owned by the caller unless it came through
 *          ::tex_mat, whose cache owns it and frees it on ::tex_flush.
 *
 * 한국어
 * ------
 * @brief 지정된 이름의 레시피를 실행하여 밉맵이 적용된 반복 GL 텍스처를 반환합니다.
 * @param[in] name 실행할 레시피 이름.
 * @return GL 텍스처 이름. 해당 이름의 레시피가 없으면 0.
 * @note CPU 측 준비 버퍼는 내부에서 할당되고 사용된 뒤 해제됩니다. .data 영역에
 *       도달하지 않습니다.
 * @note RGB는 색상이고 ALPHA는 `gloss` 명령이 설정하는 광택입니다. 알파를 투명도로
 *       샘플링하는 곳이 없으므로 픽셀 단위 광택이 추가 비용 없이 함께 실립니다.
 *       두 번째 텍스처도, 두 번째 샘플러도, 그리기마다 전달하는 유니폼도 필요
 *       없습니다.
 * @warning 반환된 텍스처는 호출자의 소유입니다. 단, ::tex_mat을 통해 얻은 경우에는
 *          캐시가 소유하며 ::tex_flush 시점에 해제됩니다.
 */
/**
 * @brief Rasterises one material into `buf`, touching no GL.
 *
 * @param[in]  name The material's name in assets/textures.txt.
 * @param[out] buf  ::TEX_SIZE x ::TEX_SIZE x 4 bytes, RGB plus gloss in alpha.
 * @return 1 if the material exists, 0 if no recipe of that name was found.
 * @note Split from ::tex_make so a headless test can inspect exactly what the
 *       GPU would receive. Three bugs shipped in these pixels and none was
 *       visible from outside -- each still built a texture, uploaded it and
 *       drew it, so the only witness was the screen, where a blown-out wall
 *       under a coloured light looks exactly like a wall.
 *
 * @brief GL을 건드리지 않고 재질 하나를 `buf`에 래스터화합니다.
 * @note 헤드리스 테스트가 GPU에 전달될 내용을 그대로 검사할 수 있도록 ::tex_make에서
 *       분리했습니다. 이 픽셀 단계에서 버그 셋이 커밋되었고 어느 것도 밖에서 보이지
 *       않았습니다.
 */
int tex_pixels(const char *name, unsigned char *buf);

GLuint tex_make(const char *name);

/**
 * @brief Drops the cache so the next ::tex_mat re-runs the recipes.
 *
 * ENGLISH
 * -------
 * @note This is how hot reload picks up an edited recipe.
 * @warning Invalidates every ::Mat previously returned by ::tex_mat: their
 *          `tex` handles are deleted. Re-fetch materials after calling this.
 *
 * 한국어
 * ------
 * @brief 캐시를 비워 다음 ::tex_mat 호출이 레시피를 다시 실행하도록 합니다.
 * @note 핫 리로드가 수정된 레시피를 반영하는 방식입니다.
 * @warning 이전에 ::tex_mat이 반환한 모든 ::Mat이 무효화됩니다. 해당 `tex` 핸들이
 *          삭제되기 때문입니다. 이 함수를 호출한 뒤에는 재질을 다시 가져와야 합니다.
 */
void tex_flush(void);

/* --- Public function prototypes: noise / 공개 함수 프로토타입: 노이즈 --- */


#endif
