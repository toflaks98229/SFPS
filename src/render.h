/**
 * @file render.h
 * @brief Geometry building, GPU upload, and the single shader everything draws with.
 *
 * ENGLISH
 * -------
 * One program with a mode switch beats three programs: fewer GLSL strings in
 * .rdata, fewer uniform lookups, less code.
 *
 * Drawing anything follows the same three steps: build vertices into a
 * ::MeshBuf on the CPU, upload it into a ::Mesh on the GPU, then select a
 * draw mode and issue the draw. The mb_* builders are pure maths and touch no
 * GL at all, which is what lets headless tools verify geometry by reading the
 * vertices back out.
 *
 * 한국어
 * ------
 * 모드 전환이 가능한 하나의 프로그램이 세 개의 프로그램보다 낫습니다. .rdata에
 * 들어가는 GLSL 문자열이 줄고, 유니폼 조회가 줄고, 코드도 줄어듭니다.
 *
 * 무엇을 그리든 동일한 세 단계를 따릅니다. CPU에서 정점을 ::MeshBuf에 생성하고,
 * 이를 GPU의 ::Mesh로 업로드한 뒤, 그리기 모드를 선택하고 그리기 명령을 내립니다.
 * mb_* 계열 빌더는 순수한 수학 연산이며 GL을 전혀 사용하지 않습니다. 덕분에
 * 헤드리스 도구가 정점을 다시 읽어 지오메트리를 검증할 수 있습니다.
 */
#ifndef RENDER_H
#define RENDER_H

#include "gl.h"
#include "m.h"

/* --- Capacity limits / 용량 제한 --- */

#define MB_MAX_SILHOUETTE 64   ///< @brief Maximum points in an extruded silhouette. / 압출 실루엣이 가질 수 있는 최대 점의 수.

/**
 * @brief Point lights the shader can evaluate in one draw.
 *
 * ENGLISH
 * -------
 * These are for light that MOVES: a muzzle flash, an explosion, a projectile
 * in flight. Nothing that can be written into a level file belongs here.
 *
 * It used to have to be at least ::LVL_MAX_LIGHTS, because evaluating a lamp
 * in this loop was the only way a lamp was applied. Since the static bake it
 * is not: a level's lamps are compiled into its vertices at load, all of them,
 * and the two numbers now answer different questions. This one is a
 * PER-FRAGMENT cost paid on every pixel of every frame, which is why it is
 * small and why raising it is a rendering decision rather than an authoring
 * one. ::LVL_MAX_LIGHTS is a load-time cost and can be many times larger.
 *
 * @note There is no longer a static assert tying the two, and that is the
 *       point rather than an oversight -- see the note where the assert used
 *       to be, in level.c.
 *
 * 한국어
 * ------
 * @brief 셰이더가 한 번의 그리기에서 계산할 수 있는 점광원 수.
 *
 * *움직이는* 빛을 위한 것입니다. 총구 섬광, 폭발, 날아가는 발사체입니다. 레벨 파일에 적을 수
 * 있는 것은 어느 것도 이곳에 속하지 않습니다.
 *
 * 한때는 ::LVL_MAX_LIGHTS 이상이어야 했습니다. 이 반복문에서 평가하는 것이 등이 적용되는
 * 유일한 방법이었기 때문입니다. 정적 베이크 이후로는 아닙니다. 레벨의 등은 로드 시 정점에
 * 전부 구워지며, 이제 두 숫자는 서로 다른 질문에 답합니다. 이 값은 매 프레임 매 픽셀마다
 * 치르는 *프래그먼트별* 비용이며, 그래서 작고, 그래서 이 값을 올리는 것은 제작상의 결정이
 * 아니라 렌더링상의 결정입니다. ::LVL_MAX_LIGHTS는 로드 시점의 비용이며 몇 배 더 클 수
 * 있습니다.
 *
 * @note 둘을 묶는 정적 검사는 더 이상 없으며, 그것이 실수가 아니라 요점입니다. 그 검사가
 *       있던 자리인 level.c의 주석을 참조하십시오.
 */
#define RD_MAX_LIGHTS 8

/* --- Type definitions: geometry / 타입 정의: 지오메트리 --- */

/**
 * @struct Box
 * @brief The universal primitive: a centre, half-extents and a facing flag.
 *
 * ENGLISH
 * -------
 * A level is boxes, a gun is boxes, an enemy will be boxes.
 * @note `inward` flips winding and normals to turn a box into a room.
 *
 * 한국어
 * ------
 * 범용 기본 도형입니다. 중심, 절반 크기, 방향 플래그로 구성됩니다.
 *
 * 레벨도 상자이고, 총기도 상자이며, 적도 상자가 될 것입니다.
 * @note `inward`는 감기 순서와 법선을 뒤집어 상자를 방으로 바꿉니다.
 */
typedef struct {
    v3 c;        /**< Centre position. / 중심 위치. */
    v3 h;        /**< Half-extents along each axis. / 각 축 방향의 절반 크기. */
    int inward;  /**< Non-zero to face inward, making a room instead of a solid. / 0이 아니면 안쪽을 향하게 되어 고체가 아닌 방이 됩니다. */
} Box;

/**
 * @struct Vtx
 * @brief One vertex: position, normal, UV and baked static light -- 44 bytes.
 *
 * ENGLISH
 * -------
 * @note The size is stated because it is the number every batching decision
 *       gets argued from -- see the per-instance draw calls in fx.c, which
 *       trade a colour attribute against exactly this figure. It read 32 for
 *       as long as the format was position, normal and UV; `lr/lg/lb` made it
 *       44 and the sentence was not carried forward. The authority is
 *       ::mesh_upload's attribute offsets, where light sits at 32.
 * @note The UV costs nothing on disk: meshes are generated at startup from
 *       the Box and silhouette tables, so only those tables are stored and
 *       the vertices are built in RAM. The earlier no-UV format saved ~6KB of
 *       RAM out of a 1.4MB surplus while costing all mapping control, which
 *       was a bad trade.
 *
 * 한국어
 * ------
 * 정점 하나입니다. 위치, 법선, UV, 그리고 구워 넣은 정적 조명으로 구성되며 44바이트입니다.
 * @note 크기를 명시하는 이유는 이것이 모든 배칭 판단의 근거가 되는 숫자이기 때문입니다.
 *       fx.c의 인스턴스별 그리기 호출을 참조하십시오. 그곳은 색상 속성을 바로 이 수치와
 *       견주어 판단합니다. 형식이 위치·법선·UV이던 동안에는 32였고, `lr/lg/lb`가 44로
 *       만들었지만 그 문장이 따라오지 않았습니다. 기준은 ::mesh_upload의 속성 오프셋이며,
 *       그곳에서 조명은 32에 놓입니다.
 * @note UV는 디스크 용량을 전혀 소모하지 않습니다. 메시는 시작 시점에 Box와 실루엣
 *       테이블로부터 생성되므로 해당 테이블만 저장되고 정점은 RAM에서 만들어집니다.
 *       이전의 UV 없는 형식은 1.4MB의 여유 중 약 6KB의 RAM을 절약하면서 매핑 제어
 *       능력을 전부 잃었는데, 이는 손해 보는 거래였습니다.
 */
typedef struct {
    float px, py, pz, nx, ny, nz, u, v;

    /**
     * @brief Static light baked into this vertex at load, added to the eight
     *        dynamic lights rather than replacing them.
     *
     * ENGLISH
     * -------
     * Quake's answer to the same problem, in the shape this engine can hold.
     * Lighting here was eight point lights and nothing else, so a room was lit
     * within their radius and BLACK everywhere else -- which is not "dark", it
     * is unlit, and no amount of placing lights fixes it because the ninth one
     * is not looked at.
     *
     * Quake compiles static light into the surfaces once and lets the handful
     * of dynamic lights add on top. This does the same at the vertex rather
     * than the texel, because the geometry here is sectors with few, large
     * faces and a lightmap would need a second texture, a second set of UVs
     * and a packer to go with them.
     *
     * WHAT IS IN IT IS THE SUN, and it is worth knowing that this narrowed. A
     * level's point lamps were compiled in here too, and they left: at THIS
     * resolution -- one sample per corner of a face metres across -- a lamp's
     * pool is not a pool, it is a gradient smeared over a wall. A directional
     * term survives the same sampling because what it varies with is shadow,
     * and shadow changes at the edges of geometry, which is where the vertices
     * are. level.c's bake_light and scene.c's note above ::MoveLight are the
     * two halves of that argument; the lamps do not light anything from the
     * shader's loop either, and the second half says why.
     *
     * ZERO EVERYWHERE IT IS NOT BAKED, so the change is purely additive: a
     * model or a sprite carries 0 and is lit exactly as it was, and only level
     * geometry gains anything -- and a level that declares no sun now carries
     * 0 as well, and is lit entirely from the loop.
     *
     * 한국어
     * ------
     * @brief 로드 시 이 정점에 구워 넣은 정적 조명입니다. 동적 광원 8개를 대체하지 않고
     *        더해집니다.
     *
     * 같은 문제에 대한 Quake의 답을 이 엔진이 담을 수 있는 모양으로 옮긴 것입니다. 이곳의
     * 조명은 점광원 여덟 개가 전부였으므로 방은 그 반경 안에서만 밝고 나머지는 전부
     * *검었습니다*. 그것은 어두운 것이 아니라 조명이 없는 것이며, 아홉 번째 광원은 보지
     * 않으므로 광원을 더 놓아도 해결되지 않습니다.
     *
     * Quake는 정적 조명을 표면에 한 번 구워 넣고 소수의 동적 광원이 그 위에 더해지게
     * 합니다. 이것은 같은 일을 텍셀이 아니라 정점에서 합니다. 이곳의 지오메트리는 크고 적은
     * 면으로 이루어진 섹터이고, 라이트맵을 쓰려면 두 번째 텍스처와 두 번째 UV 세트, 그리고
     * 그것들을 담을 패커가 필요하기 때문입니다.
     *
     * *그 안에 든 것은 태양*이며, 범위가 좁아졌다는 사실은 알아 둘 가치가 있습니다. 레벨의
     * 점광원도 이곳에 구워졌지만 셰이더의 반복문으로 돌아갔습니다. *이* 해상도에서는(몇
     * 미터짜리 면의 모서리마다 표본 하나) 등의 웅덩이가 웅덩이가 아니라 벽에 번진
     * 그러데이션입니다. 방향성 항이 같은 표본추출을 견디는 이유는 그것이 변하는 대상이
     * 그림자이고, 그림자는 지오메트리의 모서리에서 바뀌며, 정점이 바로 그곳에 있기
     * 때문입니다. level.c의 bake_light와 scene.c의 ::MoveLight 위 설명이 그 논거의 두
     * 절반입니다. 등은 셰이더의 반복문에서도 아무것도 밝히지 않으며, 그 두 번째 절반이 왜
     * 그런지를 말합니다.
     *
     * 굽지 않은 곳에서는 전부 0이므로 변경이 순수하게 가산적입니다. 모델이나 스프라이트는
     * 0을 지니고 이전과 정확히 같이 조명되며, 레벨 지오메트리만 얻습니다. 그리고 태양을
     * 선언하지 않는 레벨도 이제 0을 지니며, 전적으로 반복문이 조명합니다.
     */
    float lr, lg, lb;
} Vtx;

/**
 * @struct MeshBuf
 * @brief A FIXED-CAPACITY CPU-side vertex buffer. Allocated once, never grown.
 *
 * ENGLISH
 * -------
 * Said plainly because it was documented as "growable" and is not: ::mb_vtx
 * drops the vertex and raises ::DIAG_VERTEX_BUF when the buffer is full, and
 * nothing in this project reallocates one. A caller who believed the old word
 * would size a cap optimistically and lose geometry for it -- silently in
 * release, which is the exact failure diag.h was written to end.
 *
 * The behaviour is correct for a size-bound game; only the description was
 * wrong. Callers size for the worst case up front, the way fx.c sizes for
 * every particle alive at once.
 *
 * @warning Owns heap memory: pair every ::mb_init with an ::mb_free.
 *
 * 한국어
 * ------
 * 고정 용량의 CPU 측 정점 버퍼입니다. 한 번 할당되며 절대 확장되지 않습니다.
 *
 * 분명히 적는 이유는 "확장 가능"으로 문서화되어 있었으나 사실이 아니기 때문입니다.
 * ::mb_vtx는 버퍼가 가득 차면 정점을 버리고 ::DIAG_VERTEX_BUF를 올리며, 이 프로젝트의
 * 어디에서도 재할당하지 않습니다. 기존 표현을 믿은 호출자는 용량을 낙관적으로 잡고 그
 * 대가로 지오메트리를 잃게 됩니다. 릴리스에서는 조용히 잃으며, 그것이 바로 diag.h가
 * 끝내려고 만들어진 실패 양상입니다.
 *
 * 동작 자체는 크기가 제한된 게임에 올바르며, 틀린 것은 설명뿐이었습니다. 호출자는
 * fx.c가 모든 입자가 동시에 살아 있는 경우를 기준으로 잡듯이, 처음부터 최악의 경우에
 * 맞춰 크기를 정합니다.
 *
 * @warning 힙 메모리를 소유합니다. 모든 ::mb_init은 ::mb_free와 짝을 이루어야 합니다.
 * @note The `struct MeshBuf` tag is deliberate, not decoration: it is what
 *       lets level.h forward-declare this type instead of including render.h,
 *       which is what keeps the whole GL stack out of the simulation headers.
 *       An anonymous typedef cannot be forward-declared in C.
 * @note `struct MeshBuf` 태그는 장식이 아니라 의도적인 것입니다. 이 태그 덕분에
 *       level.h가 render.h를 포함하는 대신 이 타입을 전방 선언할 수 있으며, 그것이
 *       GL 스택 전체를 시뮬레이션 헤더에서 배제하는 방법입니다. C에서 익명 typedef는
 *       전방 선언할 수 없습니다.
 */
typedef struct MeshBuf {
    Vtx *v;              /**< Vertex array, owned by this buffer. / 이 버퍼가 소유한 정점 배열. */
    int  count, cap;     /**< Vertices in use, and allocated capacity. / 사용 중인 정점 수와 할당된 용량. */
} MeshBuf;

/**
 * @struct Mesh
 * @brief A GPU-side vertex buffer and its vertex array object.
 *
 * ENGLISH
 * -------
 * @warning Owns GL objects. Zero-initialise before first ::mesh_upload.
 *
 * 한국어
 * ------
 * GPU 측 정점 버퍼와 그 정점 배열 객체입니다.
 * @warning GL 객체를 소유합니다. 최초 ::mesh_upload 이전에 0으로 초기화하십시오.
 */
typedef struct {
    GLuint vao, vbo;     /**< Vertex array and buffer names. / 정점 배열과 버퍼의 이름. */
    int    count, cap;   /**< Vertices uploaded, and allocated capacity. / 업로드된 정점 수와 할당된 용량. */
} Mesh;

/* --- Enumerations / 열거형 --- */

/**
 * @brief Draw modes selected by ::rd_mode.
 *
 * ENGLISH
 * -------
 * Draw modes selected by ::rd_mode.
 *
 * 한국어
 * ------
 * ::rd_mode로 선택하는 그리기 모드입니다.
 */
enum {
    RD_WORLD     = 0, /**< Textured, lit, distance fog. / 텍스처 적용, 조명 있음, 거리 안개 있음. */
    RD_VIEWMODEL = 1, /**< Textured, lit by a fixed gun-space light, no fog. / 텍스처 적용, 총기 공간의 고정 조명 사용, 안개 없음. */
    RD_FLAT      = 2, /**< Uniform colour, unlit -- tracers, flash, crosshair. / 단일 색상, 조명 없음. 예광탄, 화염, 조준점에 사용됩니다. */
    RD_TEXT      = 3, /**< uColor masked by the texture's alpha -- font atlas. / 텍스처 알파로 마스킹된 uColor. 폰트 아틀라스에 사용됩니다. */
    RD_SWATCH    = 4, /**< The raw material, unlit and unfogged -- editor palette. / 조명과 안개가 없는 원본 재질. 에디터 팔레트에 사용됩니다. */
    RD_SPRITE    = 5, /**< Alpha-tested billboard, fogged; uColor tints and flashes. / 알파 테스트된 빌보드, 안개 적용. uColor가 색조와 점멸을 담당합니다. */
    /**
     * @brief Alpha-tested sprite with no fog and no flash: the hand-drawn viewmodel.
     *
     * ENGLISH
     * -------
     * ::RD_SPRITE is for a sprite that stands somewhere in the world, and both
     * of the things it adds are wrong for one that is part of the FRAME. Its
     * fog is a function of the distance from the eye to the vertex, which for
     * screen coordinates is not a distance; and its `uColor.a` is a monster's
     * white hit-flash, so a viewmodel passing a reasonable-looking 1.0 asks
     * for a fully white gun.
     *
     * 한국어
     * ------
     * @brief 안개와 섬광이 없는 알파 테스트 스프라이트. 손으로 그린 뷰 모델용입니다.
     *
     * ::RD_SPRITE는 월드 어딘가에 *서 있는* 스프라이트를 위한 것이며, 그것이 추가하는
     * 두 가지 모두 *프레임의 일부*인 스프라이트에는 틀립니다. 안개는 눈에서 정점까지의
     * 거리의 함수인데 화면 좌표에서 그것은 거리가 아니고, `uColor.a`는 몬스터의 흰색
     * 피격 섬광이므로 뷰 모델이 그럴듯해 보이는 1.0을 넘기면 완전히 하얀 총기를
     * 요청하게 됩니다.
     */
    RD_SPRITE2D  = 6
};

/**
 * @brief Texels per UV unit a procedural material is quantised to.
 *
 * ENGLISH
 * -------
 * The procedural shaders' selling point was that they have NO resolution: a
 * brick wall stays crisp with the player's nose against it, where the 256x256
 * pixel recipes visibly blur. That is a real advantage, and against a
 * pixel-art presentation it is the wrong one.
 *
 * The whole look here is built on a small number of large pixels -- the world
 * is rendered at an art resolution, magnified with GL_NEAREST, and dithered to
 * four levels. A material with unlimited detail fights all of that: it keeps
 * resolving finer as the player walks toward it, so a wall reads as a
 * high-resolution photograph seen through a pixel filter rather than as
 * something drawn at the same scale as everything else. The pixel-path
 * materials, being 256x256, already agree with the presentation. The
 * procedural ones did not.
 *
 * So the UV is snapped to a grid of this many cells per unit BEFORE the
 * pattern is evaluated. The pattern then holds one colour across each cell,
 * which is what a texel is.
 *
 * 32 rather than the pixel path's 256. Matching the baked recipes was the
 * first choice and it was too timid: at 256 a texel is smaller than a screen
 * pixel at any normal viewing distance, so the quantisation is real but
 * invisible and the materials still read as smooth. A tileset drawn by hand
 * for a game that looks like this would be 32x32 -- that is the resolution the
 * era's artists actually worked at, and it is coarse enough that a player can
 * see the individual texels of a wall, which is the entire point.
 *
 * The baked 256x256 recipes are unaffected: they are authored pixel data and
 * their detail is drawn rather than computed, so there is nothing to quantise.
 * The two paths no longer match in density, and that is correct -- a
 * hand-painted texture and a generated tile are different kinds of surface and
 * always were.
 *
 * @note Costs nothing. It is a floor() and a multiply on the UV, and it runs
 *       once per fragment in place of nothing -- the pattern functions
 *       downstream are unchanged and do not know it happened.
 * @note Set to 0 to disable the snap entirely and restore the original
 *       infinite-resolution behaviour, which is still the right choice for a
 *       project that is not pixelated.
 * @warning The snap happens in the SCALED UV space, after uPScale has been
 *          applied, so a material's `proc` scale changes how many world
 *          metres a texel covers -- exactly as it changes how many metres a
 *          brick covers. A material tuned to look right will keep looking
 *          right; one that relied on sub-texel detail will lose it, which is
 *          the intended outcome.
 * @note Written as a plain decimal with no `f` suffix. render.c stringifies
 *       this straight into the shader source, where `32.0f` is a syntax
 *       error -- the same rule post.h's duotone constants follow, and for the
 *       same reason: one definition, used from both languages, so there is no
 *       second copy in GLSL to fall out of step.
 *
 * 한국어
 * ------
 * @brief 절차적 재질이 양자화되는 UV 단위당 텍셀 수입니다.
 *
 * 절차적 셰이더의 장점은 해상도가 *없다*는 것이었습니다. 256x256 픽셀 레시피가 눈에 띄게
 * 흐려지는 거리에서도 벽돌 벽이 선명하게 유지됩니다. 이는 실제 장점이지만, 픽셀 아트
 * 표현 방식에서는 잘못된 장점입니다.
 *
 * 이곳의 룩 전체가 적은 수의 큰 픽셀 위에 세워져 있습니다. 월드는 아트 해상도로
 * 렌더링되고, GL_NEAREST로 확대되며, 4단계로 디더링됩니다. 무한한 디테일을 가진 재질은
 * 그 모든 것과 충돌합니다. 플레이어가 다가갈수록 계속 더 세밀해지므로, 벽이 다른 모든
 * 것과 같은 크기로 그려진 무언가가 아니라 픽셀 필터를 거쳐 본 고해상도 사진처럼
 * 읽힙니다. 픽셀 경로의 재질들은 256x256이므로 이미 표현 방식과 일치합니다. 절차적
 * 재질들은 그렇지 않았습니다.
 *
 * 그래서 패턴을 계산하기 *전에* UV를 단위당 이만큼의 셀을 가진 격자에 맞춥니다. 그러면
 * 패턴이 각 셀 전체에 하나의 색을 유지하는데, 그것이 곧 텍셀입니다.
 *
 * 픽셀 경로의 256이 아니라 32입니다. 구워진 레시피에 맞추는 것이 첫 선택이었고 너무
 * 소심했습니다. 256에서는 일반적인 시야 거리에서 텍셀이 화면 픽셀보다 작으므로, 양자화가
 * 실제로 일어나되 보이지 않고 재질은 여전히 매끄럽게 읽힙니다. 이런 화면의 게임을 위해
 * 사람이 그린 타일셋은 32x32입니다. 그것이 그 시대의 아티스트들이 실제로 작업하던
 * 해상도이며, 플레이어가 벽의 개별 텍셀을 볼 수 있을 만큼 거친데 그것이 요점 전부입니다.
 *
 * 구워진 256x256 레시피는 영향을 받지 않습니다. 제작된 픽셀 데이터이며 그 디테일은
 * 계산된 것이 아니라 그려진 것이므로 양자화할 것이 없습니다. 이제 두 경로의 밀도가
 * 일치하지 않으며 그것이 옳습니다. 손으로 칠한 텍스처와 생성된 타일은 서로 다른 종류의
 * 표면이고 처음부터 그랬습니다.
 *
 * @note 비용이 없습니다. UV에 대한 floor()와 곱셈이며, 아무것도 없던 자리에서 프래그먼트당
 *       한 번 실행됩니다. 하위의 패턴 함수들은 변경되지 않았고 이 일이 일어났다는 것조차
 *       모릅니다.
 * @note 0으로 설정하면 스냅이 완전히 비활성화되어 원래의 무한 해상도 동작으로 돌아갑니다.
 *       픽셀화되지 않은 프로젝트에서는 여전히 그쪽이 옳은 선택입니다.
 * @warning 스냅은 uPScale이 적용된 뒤인 *배율 조정된* UV 공간에서 일어나므로, 재질의
 *          `proc` 배율이 텍셀 하나가 덮는 월드 거리를 바꿉니다. 벽돌 하나가 덮는 거리를
 *          바꾸는 것과 정확히 같습니다. 보기 좋게 조정된 재질은 계속 보기 좋게 유지되지만,
 *          텍셀보다 작은 디테일에 의존하던 재질은 그것을 잃게 되며 그것이 의도된
 *          결과입니다.
 */
#define RD_PROC_TEXELS 32.0

/**
 * @brief Procedural surface shaders, evaluated per pixel from the UV.
 *
 * ENGLISH
 * -------
 * Quantised to ::RD_PROC_TEXELS cells per UV unit, so they carry the same
 * texel density as the baked pixel recipes and read as part of the same
 * pixel-art presentation.
 *
 * @warning Keep this list in step with the switch in the fragment shader and
 *          with the name table in tex.c; there is no way to share an enum with
 *          GLSL. Adding a value here without updating both leaves the new
 *          shader silently drawing as PROC_TEXTURE.
 *
 * 한국어
 * ------
 * 텍스처에서 샘플링하지 않고 UV로부터 픽셀 단위로 계산되는 절차적 표면 셰이더입니다.
 *
 * 해상도가 무한하며(벽돌 벽에 코를 들이대도 선명하게 유지됩니다) 텍스처 메모리를
 * 전혀 사용하지 않습니다.
 *
 * @warning 이 목록은 프래그먼트 셰이더의 switch문 및 tex.c의 이름 테이블과 항상
 *          동기화되어야 합니다. GLSL과 열거형을 공유할 방법이 없기 때문입니다. 양쪽을
 *          갱신하지 않고 여기에 값을 추가하면, 새 셰이더는 조용히 PROC_TEXTURE로
 *          그려집니다.
 */
enum {
    PROC_TEXTURE = 0,   /**< Sample uTex, the original behaviour. / uTex를 샘플링하는 기존 동작. */
    PROC_BRICK,         /**< Brick lattice. / 벽돌 격자. */
    PROC_TILE,          /**< Tiled surface. / 타일 표면. */
    PROC_PANEL,         /**< Riveted metal plate. / 리벳이 박힌 금속판. */
    PROC_WOOD,          /**< Wood grain. / 나뭇결. */
    PROC_HEX,           /**< Hexagonal pattern. / 육각형 패턴. */
    PROC_MARBLE,        /**< Marble veining. / 대리석 무늬. */
    PROC_RUST,          /**< Rusted metal. / 녹슨 금속. */
    PROC_GRID,          /**< Emissive tech grid. / 발광하는 기술 격자. */
    PROC_LAVA,          /**< Molten rock: dark crust broken by glowing cracks. / 녹은 암석. 어두운 껍질이 빛나는 균열로 갈라져 있습니다. */
    PROC_WINDOW,        /**< Glazed facade: dark panes with lit rooms behind some of them. / 유리 외벽. 어두운 창살 중 일부 뒤에 불 켜진 방이 있습니다. */
    PROC_COUNT          /**< Total number of procedural shaders. / 절차적 셰이더의 총 개수. */
};

/* --- Public function prototypes: CPU-side builder lifecycle / 공개 함수 프로토타입: CPU 측 빌더 수명 주기 --- */

/**
 * @brief Allocates a vertex buffer with an initial capacity.
 *
 * ENGLISH
 * -------
 * @param[out] b   Buffer to initialise.
 * @param[in]  cap Capacity in vertices, for the buffer's whole life. Size it
 *                 for the worst case: it is never grown.
 * @warning Allocates heap memory the caller owns; pair with ::mb_free.
 *
 * @note ON ALLOCATION FAILURE the buffer comes up with a capacity of ZERO
 *       rather than the requested one, so it behaves as a buffer that is
 *       already full: ::mb_vtx drops every vertex and raises
 *       ::DIAG_VERTEX_BUF. Callers therefore do not need to test for failure
 *       -- the one path that handles a full buffer handles this too. Recording
 *       the requested capacity over a null pointer, as this once did, made the
 *       `count >= cap` guard pass and dereference null on the first vertex.
 *
 * 한국어
 * ------
 * @brief 정점 버퍼를 할당합니다.
 * @param[out] b   초기화할 버퍼.
 * @param[in]  cap 버퍼의 전 생애에 걸친 정점 단위 용량. 확장되지 않으므로 최악의 경우에
 *                 맞춰 정하십시오.
 * @warning 호출자가 소유하는 힙 메모리를 할당합니다. ::mb_free와 짝을 이루어야 합니다.
 *
 * @note 할당에 실패하면 요청한 용량이 아니라 용량 *0*으로 준비되므로, 이미 가득 찬 버퍼처럼
 *       동작합니다. ::mb_vtx가 모든 정점을 버리고 ::DIAG_VERTEX_BUF를 올립니다. 따라서
 *       호출자는 실패를 검사할 필요가 없습니다. 가득 찬 버퍼를 처리하는 그 하나의 경로가
 *       이 경우도 함께 처리합니다. 이전처럼 널 포인터 위에 요청 용량을 기록하면
 *       `count >= cap` 검사가 통과되어 첫 정점에서 널을 역참조하게 됩니다.
 */
void mb_init (MeshBuf *b, int cap);

/**
 * @brief Releases a vertex buffer's memory.
 *
 * ENGLISH
 * -------
 * @param[in,out] b Buffer to free. Safe to call on an already-freed buffer.
 *
 * 한국어
 * ------
 * @brief 정점 버퍼의 메모리를 해제합니다.
 * @param[in,out] b 해제할 버퍼. 이미 해제된 버퍼에 호출해도 안전합니다.
 */
void mb_free (MeshBuf *b);

/**
 * @brief Empties a buffer without releasing its memory.
 *
 * ENGLISH
 * -------
 * @param[in,out] b Buffer to clear.
 * @note This is how a per-frame effect buffer is reused without reallocating.
 *
 * 한국어
 * ------
 * @brief 메모리를 해제하지 않고 버퍼를 비웁니다.
 * @param[in,out] b 비울 버퍼.
 * @note 프레임마다 사용하는 효과 버퍼를 재할당 없이 재사용하는 방식입니다.
 */
void mb_reset(MeshBuf *b);

/**
 * @brief Appends one vertex.
 *
 * ENGLISH
 * -------
 * @param[in,out] b Buffer to append to.
 * @param[in]     p Position.
 * @param[in]     n Normal.
 * @param[in]     u Texture coordinate u.
 * @param[in]     v Texture coordinate v.
 *
 * 한국어
 * ------
 * @brief 정점 하나를 추가합니다.
 * @param[in,out] b 추가 대상 버퍼.
 * @param[in]     p 위치.
 * @param[in]     n 법선.
 * @param[in]     u 텍스처 좌표 u.
 * @param[in]     v 텍스처 좌표 v.
 */
void mb_vtx  (MeshBuf *b, v3 p, v3 n, float u, float v);

/* --- Public function prototypes: primitives / 공개 함수 프로토타입: 기본 도형 --- */

/**
 * @brief Appends a quad as two triangles.
 *
 * ENGLISH
 * -------
 * @param[in,out] b   Buffer to append to.
 * @param[in]     a   First corner.
 * @param[in]     bb  Second corner.
 * @param[in]     c   Third corner.
 * @param[in]     d   Fourth corner.
 * @param[in]     n   Face normal.
 * @param[in]     uvs Texels per world unit.
 * @note `uvs` is applied by projecting the position onto the plane the normal
 *       points out of. Baking the scale in here means the shader just reads
 *       the UV, and different meshes can use different densities without a
 *       uniform or a second draw call.
 *
 * 한국어
 * ------
 * @brief 사각형을 두 개의 삼각형으로 추가합니다.
 * @param[in,out] b   추가 대상 버퍼.
 * @param[in]     a   첫 번째 모서리.
 * @param[in]     bb  두 번째 모서리.
 * @param[in]     c   세 번째 모서리.
 * @param[in]     d   네 번째 모서리.
 * @param[in]     n   면 법선.
 * @param[in]     uvs 월드 단위당 텍셀 수.
 * @note `uvs`는 법선이 향하는 평면에 위치를 투영하는 방식으로 적용됩니다. 배율을
 *       여기서 미리 반영해 두면 셰이더는 UV를 읽기만 하면 되고, 서로 다른 메시가
 *       유니폼이나 추가 그리기 호출 없이 각기 다른 밀도를 사용할 수 있습니다.
 */
void mb_quad (MeshBuf *b, v3 a, v3 bb, v3 c, v3 d, v3 n, float uvs);

/**
 * @brief Appends a box as six quads.
 *
 * ENGLISH
 * -------
 * @param[in,out] b   Buffer to append to.
 * @param[in]     box Box to emit; its `inward` flag selects room or solid.
 * @param[in]     uvs Texels per world unit.
 *
 * 한국어
 * ------
 * @brief 상자를 여섯 개의 사각형으로 추가합니다.
 * @param[in,out] b   추가 대상 버퍼.
 * @param[in]     box 생성할 상자. `inward` 플래그가 방과 고체를 구분합니다.
 * @param[in]     uvs 월드 단위당 텍셀 수.
 */
void mb_box  (MeshBuf *b, Box box, float uvs);

/**
 * @brief Emits a box twice, the second mirrored across x=0.
 *
 * ENGLISH
 * -------
 * @param[in,out] b   Buffer to append to.
 * @param[in]     box Box to emit and mirror.
 * @param[in]     uvs Texels per world unit.
 * @note Storing one side and letting the engine reflect it halves the vertex
 *       data for anything symmetric -- the single biggest saving on
 *       hand-authored models.
 *
 * 한국어
 * ------
 * @brief 상자를 두 번 생성하며, 두 번째는 x=0을 기준으로 대칭 복사합니다.
 * @param[in,out] b   추가 대상 버퍼.
 * @param[in]     box 생성하고 대칭 복사할 상자.
 * @param[in]     uvs 월드 단위당 텍셀 수.
 * @note 한쪽만 저장하고 엔진이 반사하도록 하면 대칭인 모든 것의 정점 데이터가
 *       절반으로 줄어듭니다. 직접 제작한 모델에서 가장 큰 절약 효과입니다.
 */
void mb_box_mirror(MeshBuf *b, Box box, float uvs);

/**
 * @brief Extrudes a closed 2D silhouette into a solid.
 *
 * ENGLISH
 * -------
 * @param[in,out] b      Buffer to append to.
 * @param[in]     pts    z,y pairs in 1/100 units -- a side-on profile.
 * @param[in]     n      Point count, up to ::MB_MAX_SILHOUETTE.
 * @param[in]     half_x Half thickness to extrude to, in world units.
 * @param[in]     uvs    Texels per world unit.
 * @note This is the primitive weapons and props are built from: an 18-point
 *       shotgun outline is 36 integers, where the same shape as axis-aligned
 *       boxes took fifteen 28-byte structs and still looked like a pile of
 *       blocks.
 * @note UVs come out of the extrusion for free -- along the skirt, u follows
 *       the perimeter and v follows the thickness; on the flat caps, u,v are
 *       just the silhouette coordinates. Nothing has to be unwrapped or stored.
 * @note Winding is normalised internally, so points may be given either way
 *       round.
 * @warning The caps are ear-clipped, so the outline may be concave (a trigger
 *          guard, a pump) but must NOT self-intersect.
 *
 * 한국어
 * ------
 * @brief 닫힌 2D 실루엣을 압출하여 입체로 만듭니다.
 * @param[in,out] b      추가 대상 버퍼.
 * @param[in]     pts    1/100 단위의 z,y 좌표 쌍. 측면 단면입니다.
 * @param[in]     n      점의 개수. 최대 ::MB_MAX_SILHOUETTE까지 가능합니다.
 * @param[in]     half_x 압출할 절반 두께 (월드 단위).
 * @param[in]     uvs    월드 단위당 텍셀 수.
 * @note 무기와 소품을 만드는 기본 도형입니다. 18개 점으로 이루어진 샷건 외곽선은
 *       정수 36개인 반면, 동일한 형상을 축 정렬 상자로 만들면 28바이트 구조체
 *       15개를 쓰고도 블록 더미처럼 보였습니다.
 * @note UV는 압출 과정에서 자동으로 생성됩니다. 옆면에서는 u가 둘레를, v가 두께를
 *       따르며, 평평한 마개면에서는 u,v가 곧 실루엣 좌표입니다. UV를 펼치거나 저장할
 *       필요가 전혀 없습니다.
 * @note 감기 순서는 내부에서 정규화되므로, 점을 어느 방향으로 주어도 됩니다.
 * @warning 마개면은 귀 자르기(ear clipping)로 처리되므로 외곽선이 오목해도 되지만
 *          (방아쇠울, 펌프 등) 자기 자신과 교차해서는 안 됩니다.
 */
void mb_extrude(MeshBuf *b, const short *pts, int n, float half_x, float uvs);

/**
 * @brief Extrudes a silhouette with a per-point thickness.
 *
 * ENGLISH
 * -------
 * @param[in,out] b      Buffer to append to.
 * @param[in]     pts    z,y pairs in 1/100 units.
 * @param[in]     thick  Per-point half thickness in 1/100 units. Pass NULL
 *                       for a constant thickness.
 * @param[in]     n      Point count.
 * @param[in]     half_x Half thickness used when `thick` is NULL.
 * @param[in]     uvs    Texels per world unit.
 * @note A constant thickness makes every part a flat slab; varying it along
 *       the outline is what gives tapered muzzles, wedged stocks and anything
 *       that narrows toward one end.
 *
 * 한국어
 * ------
 * @brief 점마다 다른 두께를 적용하여 실루엣을 압출합니다.
 * @param[in,out] b      추가 대상 버퍼.
 * @param[in]     pts    1/100 단위의 z,y 좌표 쌍.
 * @param[in]     thick  1/100 단위의 점별 절반 두께. 일정한 두께를 원하면 NULL을
 *                       전달하십시오.
 * @param[in]     n      점의 개수.
 * @param[in]     half_x `thick`이 NULL일 때 사용되는 절반 두께.
 * @param[in]     uvs    월드 단위당 텍셀 수.
 * @note 두께가 일정하면 모든 부품이 평평한 판이 됩니다. 외곽선을 따라 두께를 변화시키는
 *       것이 끝이 좁아지는 총구, 쐐기 모양 개머리판 등 한쪽으로 가늘어지는 모든 형태를
 *       만들어 냅니다.
 */
void mb_extrude_taper(MeshBuf *b, const short *pts, const short *thick,
                      int n, float half_x, float uvs);

/**
 * @brief Triangulates a closed polygon lying flat at a given height.
 *
 * ENGLISH
 * -------
 * @param[in,out] b   Buffer to append to.
 * @param[in]     pts x,z pairs in 1/100 units.
 * @param[in]     n   Point count.
 * @param[in]     y   Height to place the polygon at, in world units.
 * @param[in]     up  Non-zero to face upward, zero to face down, so the same
 *                    call serves a sector floor and its ceiling.
 * @param[in]     uvs Texels per world unit.
 * @note Winding is normalised, so an outline may be authored either way round.
 * @note Shares the ear clipper with ::mb_extrude's caps -- a second
 *       triangulator for level floors would be the same code with a different
 *       bug.
 *
 * 한국어
 * ------
 * @brief 주어진 높이에 평평하게 놓인 닫힌 다각형을 삼각형으로 분할합니다.
 * @param[in,out] b   추가 대상 버퍼.
 * @param[in]     pts 1/100 단위의 x,z 좌표 쌍.
 * @param[in]     n   점의 개수.
 * @param[in]     y   다각형을 배치할 높이 (월드 단위).
 * @param[in]     up  0이 아니면 위를, 0이면 아래를 향합니다. 덕분에 동일한 호출이
 *                    섹터의 바닥과 천장 모두에 사용됩니다.
 * @param[in]     uvs 월드 단위당 텍셀 수.
 * @note 감기 순서가 정규화되므로 외곽선을 어느 방향으로 제작해도 됩니다.
 * @note ::mb_extrude의 마개면과 동일한 귀 자르기 알고리즘을 공유합니다. 레벨 바닥을
 *       위한 별도의 삼각분할기를 만든다면 같은 코드에 다른 버그가 생길 뿐입니다.
 */
void mb_polygon(MeshBuf *b, const short *pts, int n, float y, int up, float uvs);

/**
 * @brief Revolves an open profile around the z axis.
 *
 * ENGLISH
 * -------
 * @param[in,out] b        Buffer to append to.
 * @param[in]     pts      z,r pairs in 1/100 units, r being radius from the axis.
 * @param[in]     n        Point count.
 * @param[in]     segments Number of revolution steps; more is rounder.
 * @param[in]     uvs      Texels per world unit.
 * @note This is what extrusion cannot do at any thickness: round barrels,
 *       rocket tubes, drums.
 *
 * 한국어
 * ------
 * @brief 열린 단면을 z축을 중심으로 회전시킵니다.
 * @param[in,out] b        추가 대상 버퍼.
 * @param[in]     pts      1/100 단위의 z,r 좌표 쌍. r은 축으로부터의 반지름입니다.
 * @param[in]     n        점의 개수.
 * @param[in]     segments 회전 분할 수. 클수록 둥글어집니다.
 * @param[in]     uvs      월드 단위당 텍셀 수.
 * @note 압출로는 두께를 아무리 조절해도 만들 수 없는 형태를 담당합니다. 둥근 총열,
 *       로켓 튜브, 드럼 등이 해당합니다.
 */
void mb_lathe(MeshBuf *b, const short *pts, int n, int segments, float uvs);

/* --- Public function prototypes: billboards and lines / 공개 함수 프로토타입: 빌보드 및 선 --- */

/**
 * @brief Appends a camera-facing quad, for muzzle flashes and impact marks.
 *
 * ENGLISH
 * -------
 * @param[in,out] b      Buffer to append to.
 * @param[in]     centre Centre of the quad in world space.
 * @param[in]     right  Camera right basis vector.
 * @param[in]     up     Camera up basis vector.
 * @param[in]     w      Width in world units.
 * @param[in]     h      Height in world units.
 *
 * 한국어
 * ------
 * @brief 카메라를 향하는 사각형을 추가합니다. 총구 화염과 탄착 자국에 사용됩니다.
 * @param[in,out] b      추가 대상 버퍼.
 * @param[in]     centre 월드 공간에서 사각형의 중심.
 * @param[in]     right  카메라의 우측 기저 벡터.
 * @param[in]     up     카메라의 상향 기저 벡터.
 * @param[in]     w      월드 단위의 너비.
 * @param[in]     h      월드 단위의 높이.
 */
void mb_billboard(MeshBuf *b, v3 centre, v3 right, v3 up, float w, float h);

/**
 * @brief Appends a camera-facing quad with an explicit UV rectangle.
 *
 * ENGLISH
 * -------
 * @param[in,out] b      Buffer to append to.
 * @param[in]     centre Centre of the quad in world space.
 * @param[in]     right  Camera right basis vector.
 * @param[in]     up     Camera up basis vector.
 * @param[in]     w      Width in world units.
 * @param[in]     h      Height in world units.
 * @param[in]     u0     Left texture coordinate.
 * @param[in]     v0     Bottom texture coordinate.
 * @param[in]     u1     Right texture coordinate.
 * @param[in]     v1     Top texture coordinate.
 * @note The explicit UV rect is what lets one sprite atlas serve many frames
 *       -- the caller passes the sub-rect of the frame it wants.
 *
 * 한국어
 * ------
 * @brief 명시적인 UV 사각 영역을 지정하여 카메라를 향하는 사각형을 추가합니다.
 * @param[in,out] b      추가 대상 버퍼.
 * @param[in]     centre 월드 공간에서 사각형의 중심.
 * @param[in]     right  카메라의 우측 기저 벡터.
 * @param[in]     up     카메라의 상향 기저 벡터.
 * @param[in]     w      월드 단위의 너비.
 * @param[in]     h      월드 단위의 높이.
 * @param[in]     u0     좌측 텍스처 좌표.
 * @param[in]     v0     하단 텍스처 좌표.
 * @param[in]     u1     우측 텍스처 좌표.
 * @param[in]     v1     상단 텍스처 좌표.
 * @note UV 영역을 명시할 수 있기에 하나의 스프라이트 아틀라스가 여러 프레임을 담당할
 *       수 있습니다. 호출자가 원하는 프레임의 부분 영역을 전달합니다.
 */
void mb_billboard_uv(MeshBuf *b, v3 centre, v3 right, v3 up, float w, float h,
                     float u0, float v0, float u1, float v1);

/**
 * @brief Appends a quad strip running along a 3D segment, facing the camera.
 *
 * ENGLISH
 * -------
 * @param[in,out] b       Buffer to append to.
 * @param[in]     a       Segment start.
 * @param[in]     bpt     Segment end.
 * @param[in]     cam_pos Camera position, used to orient the strip.
 * @param[in]     width   Strip width in world units.
 * @param[in]     utile   How many times the texture repeats along the segment.
 * @note Rotated only around that segment's own axis -- the laser-beam/rope
 *       idiom, as opposed to ::mb_billboard's full spherical rotation, which
 *       would twist a taut line out of alignment with itself as the camera
 *       moved.
 * @note UVs run u:0..utile along the segment and v:0..1 across the width, so
 *       a tiling rope or chain texture repeats `utile` times over the
 *       segment's length. This is what lets a line be upgraded to a textured,
 *       tileable rope later by doing nothing but binding a material -- the
 *       geometry and its UVs do not change.
 * @note Degenerates gracefully (falls back to an arbitrary side axis) when
 *       the view looks straight down the segment, where no flat strip can
 *       face the camera at all. A zero-length segment emits nothing.
 *
 * 한국어
 * ------
 * @brief 3D 선분을 따라 이어지며 카메라를 향하는 사각형 띠를 추가합니다.
 * @param[in,out] b       추가 대상 버퍼.
 * @param[in]     a       선분의 시작점.
 * @param[in]     bpt     선분의 끝점.
 * @param[in]     cam_pos 띠의 방향을 결정하는 데 사용되는 카메라 위치.
 * @param[in]     width   월드 단위의 띠 폭.
 * @param[in]     utile   선분을 따라 텍스처가 반복되는 횟수.
 * @note 해당 선분 자체의 축을 중심으로만 회전합니다. 레이저 빔이나 로프에 쓰이는
 *       방식이며, ::mb_billboard의 완전한 구면 회전과는 다릅니다. 구면 회전을 쓰면
 *       카메라가 움직일 때 팽팽한 선이 자기 자신과 어긋나며 뒤틀립니다.
 * @note UV는 선분을 따라 u:0..utile, 폭을 가로질러 v:0..1로 진행되므로, 반복 가능한
 *       로프나 사슬 텍스처가 선분 길이에 걸쳐 `utile`번 반복됩니다. 덕분에 나중에
 *       재질을 바인딩하는 것만으로 단순한 선을 텍스처가 적용된 반복 가능한 로프로
 *       업그레이드할 수 있으며, 지오메트리와 UV는 그대로 유지됩니다.
 * @note 시선이 선분과 정확히 일치하여 어떤 평평한 띠도 카메라를 향할 수 없는 경우,
 *       임의의 측면 축으로 대체하여 무리 없이 처리합니다. 길이가 0인 선분은 아무것도
 *       생성하지 않습니다.
 */
void mb_ribbon(MeshBuf *b, v3 a, v3 bpt, v3 cam_pos, float width, float utile);

/**
 * @brief Appends a single line segment (two vertices, for GL_LINES).
 *
 * ENGLISH
 * -------
 * @param[in,out] b  Buffer to append to.
 * @param[in]     a  Segment start.
 * @param[in]     bb Segment end.
 * @warning Produces GL_LINES data; draw with ::mesh_draw_lines, not ::mesh_draw.
 *
 * 한국어
 * ------
 * @brief 하나의 선분을 추가합니다 (GL_LINES용 정점 2개).
 * @param[in,out] b  추가 대상 버퍼.
 * @param[in]     a  선분의 시작점.
 * @param[in]     bb 선분의 끝점.
 * @warning GL_LINES 데이터를 생성합니다. ::mesh_draw가 아닌 ::mesh_draw_lines로
 *          그리십시오.
 */
void mb_line(MeshBuf *b, v3 a, v3 bb);

/* --- Public function prototypes: GPU meshes / 공개 함수 프로토타입: GPU 메시 --- */

/**
 * @brief Uploads a CPU buffer's vertices to the GPU.
 *
 * ENGLISH
 * -------
 * @param[in,out] m       Mesh to upload into. Created on first use.
 * @param[in]     b       Source buffer.
 * @param[in]     dynamic Non-zero for data that changes every frame, which
 *                        selects a streaming usage hint and reuses the
 *                        existing allocation when it is large enough.
 * @warning Requires a current GL context. The mesh owns GL objects from this
 *          point on.
 *
 * 한국어
 * ------
 * @brief CPU 버퍼의 정점을 GPU로 업로드합니다.
 * @param[in,out] m       업로드 대상 메시. 최초 사용 시 생성됩니다.
 * @param[in]     b       원본 버퍼.
 * @param[in]     dynamic 매 프레임 변경되는 데이터이면 0이 아닌 값을 전달합니다.
 *                        스트리밍 사용 힌트가 선택되며, 기존 할당이 충분히 크면
 *                        재사용됩니다.
 * @warning 활성 GL 컨텍스트가 필요합니다. 이 시점부터 메시가 GL 객체를 소유합니다.
 */
void mesh_upload    (Mesh *m, const MeshBuf *b, int dynamic);

/**
 * @brief Re-sends only the vertices from `first` on, leaving the rest alone.
 *
 * ENGLISH
 * -------
 * For a mesh whose tail changes and whose head does not: the level, whose
 * moving half is built after its static half and is the only part a door
 * touches. Sending the whole buffer again would be sending an unchanged
 * majority across the bus every frame of a door's swing.
 *
 * @param[in,out] m     Mesh to update. Must already hold an upload.
 * @param[in]     b     Source buffer.
 * @param[in]     first Index of the first vertex to send.
 * @return Non-zero when the sub-upload happened, 0 when it could not and the
 *         caller must fall back to a whole ::mesh_upload.
 * @note RETURNS 0 RATHER THAN GROWING. A partial send is only valid while the
 *       total vertex count is what the existing allocation was sized for, and
 *       the count is checked rather than assumed -- a moving half whose vertex
 *       count changed is a caller whose premise has broken, and silently
 *       uploading half of it would leave the tail of the previous frame's
 *       geometry on screen. Translating a brush cannot change its vertex count,
 *       so this returning 0 during play means something other than a door
 *       moved.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @brief `first` 이후의 정점만 다시 보내고 나머지는 그대로 둡니다.
 *
 * 꼬리는 바뀌고 머리는 바뀌지 않는 메시를 위한 것입니다. 레벨이 그러합니다. 움직이는 절반이
 * 정적인 절반 뒤에 생성되며, 문이 건드리는 것은 그것뿐입니다. 버퍼 전체를 다시 보내는 것은
 * 문이 열리는 동안 매 프레임 바뀌지 않은 다수를 버스 너머로 보내는 일입니다.
 *
 * @param[in,out] m     갱신할 메시. 이미 업로드를 보유하고 있어야 합니다.
 * @param[in]     b     원본 버퍼.
 * @param[in]     first 보낼 첫 정점의 인덱스.
 * @return 부분 업로드가 수행되면 0이 아닌 값. 불가능하여 호출자가 전체 ::mesh_upload로
 *         되돌아가야 하면 0.
 * @note *확장하지 않고 0을 반환합니다.* 부분 전송은 전체 정점 수가 기존 할당이 상정한 값일
 *       때에만 유효하며, 그 수를 가정하지 않고 검사합니다. 정점 수가 달라진 움직이는 절반은
 *       전제가 깨진 호출자이고, 그 절반만 조용히 올리면 이전 프레임 지오메트리의 꼬리가 화면에
 *       남습니다. 브러시를 옮기는 것은 그 정점 수를 바꿀 수 없으므로, 플레이 중에 이것이 0을
 *       반환한다면 문이 아닌 무언가가 움직인 것입니다.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
int  mesh_upload_from(Mesh *m, const MeshBuf *b, int first);

/**
 * @brief Draws a mesh as triangles.
 *
 * ENGLISH
 * -------
 * @param[in] m Mesh to draw. An empty mesh draws nothing.
 * @warning Requires a current GL context and an active shader program.
 *
 * 한국어
 * ------
 * @brief 메시를 삼각형으로 그립니다.
 * @param[in] m 그릴 메시. 비어 있는 메시는 아무것도 그리지 않습니다.
 * @warning 활성 GL 컨텍스트와 활성화된 셰이더 프로그램이 필요합니다.
 */
void mesh_draw      (const Mesh *m);

/**
 * @brief Draws a mesh as lines.
 *
 * ENGLISH
 * -------
 * @param[in] m Mesh to draw; its vertices must have been built by ::mb_line.
 * @warning Requires a current GL context and an active shader program.
 *
 * 한국어
 * ------
 * @brief 메시를 선으로 그립니다.
 * @param[in] m 그릴 메시. 정점이 ::mb_line으로 생성된 것이어야 합니다.
 * @warning 활성 GL 컨텍스트와 활성화된 셰이더 프로그램이 필요합니다.
 */
void mesh_draw_lines(const Mesh *m);

/**
 * @brief Draws a vertex range, for meshes split into per-material runs.
 *
 * ENGLISH
 * -------
 * @param[in] m     Mesh to draw from.
 * @param[in] first First vertex index in the range.
 * @param[in] count How many vertices to draw.
 * @note This is what lets one upload serve a model whose parts use different
 *       materials: bind a material, draw its range, repeat.
 *
 * 한국어
 * ------
 * @brief 재질별 구간으로 나뉜 메시에서 특정 정점 범위를 그립니다.
 * @param[in] m     그리기 대상 메시.
 * @param[in] first 범위의 첫 정점 인덱스.
 * @param[in] count 그릴 정점의 개수.
 * @note 부품마다 다른 재질을 사용하는 모델을 한 번의 업로드로 처리할 수 있게 하는
 *       기능입니다. 재질을 바인딩하고 해당 구간을 그리는 과정을 반복합니다.
 */
void mesh_draw_range(const Mesh *m, int first, int count);

/* --- Public function prototypes: the shader / 공개 함수 프로토타입: 셰이더 --- */

/**
 * @brief Compiles and links the single shader program everything draws with.
 *
 * ENGLISH
 * -------
 * @warning Requires a current GL context. Must run before any rd_* or
 *          mesh_draw* call.
 *
 * 한국어
 * ------
 * @brief 모든 그리기에 사용되는 단일 셰이더 프로그램을 컴파일하고 링크합니다.
 * @warning 활성 GL 컨텍스트가 필요합니다. 모든 rd_* 또는 mesh_draw* 호출보다 먼저
 *          실행되어야 합니다.
 */
void rd_init (void);

/**
 * @brief Re-binds the renderer's shader program.
 *
 * ENGLISH
 * -------
 * @note ::rd_init binds the program once and everything after it assumes the
 *       binding is still current -- rd_mode, rd_color and rd_mvp only set
 *       uniforms, they do not bind. That held for as long as this was the
 *       only program in the process.
 *
 *       It stopped holding when post.c introduced a second one. A pass that
 *       binds its own program must call this before handing control back, or
 *       every later rd_* call writes uniforms into a program that is not the
 *       one drawing: the symptom is the whole screen filling with whatever
 *       texture happened to be bound last, stretched across a full-screen
 *       triangle.
 *
 * 한국어
 * ------
 * @brief 렌더러의 셰이더 프로그램을 다시 바인딩합니다.
 * @note ::rd_init이 프로그램을 한 번 바인딩하면 이후의 모든 코드가 그 바인딩이
 *       유효하다고 가정합니다. rd_mode, rd_color, rd_mvp는 유니폼만 설정할 뿐
 *       바인딩하지 않습니다. 이 가정은 프로세스에 프로그램이 하나뿐인 동안에는
 *       유효했습니다.
 *
 *       post.c가 두 번째 프로그램을 도입하면서 그 가정이 깨졌습니다. 자체 프로그램을
 *       바인딩하는 패스는 제어를 돌려주기 전에 반드시 이 함수를 호출해야 합니다.
 *       그렇지 않으면 이후의 모든 rd_* 호출이 실제로 그리는 프로그램이 아닌 다른
 *       프로그램에 유니폼을 씁니다. 증상은 마지막으로 바인딩된 텍스처가 전체 화면
 *       삼각형에 늘어난 채 화면 전체를 뒤덮는 것입니다.
 */
void rd_use  (void);

/**
 * @brief Selects the draw mode for subsequent draws.
 *
 * ENGLISH
 * -------
 * @param[in] mode One of the RD_* constants.
 * @warning Sets a uniform only. The renderer's program must be current --
 *          see ::rd_use.
 *
 * 한국어
 * ------
 * @brief 이후 그리기에 적용될 그리기 모드를 선택합니다.
 * @param[in] mode RD_* 상수 중 하나.
 * @warning 유니폼만 설정합니다. 렌더러의 프로그램이 현재 바인딩되어 있어야 합니다.
 *          ::rd_use를 참조하십시오.
 */
void rd_mode (int mode);

/**
 * @brief Sets the model-view-projection matrix for subsequent draws.
 *
 * ENGLISH
 * -------
 * @param[in] mvp Combined transform.
 *
 * 한국어
 * ------
 * @brief 이후 그리기에 적용될 모델-뷰-투영 행렬을 설정합니다.
 * @param[in] mvp 결합된 변환 행렬.
 */
void rd_mvp  (mat4 mvp);

/**
 * @brief Sets the eye position, used for lighting and fog.
 *
 * ENGLISH
 * -------
 * @param[in] eye Camera position in world space.
 *
 * 한국어
 * ------
 * @brief 조명과 안개 계산에 사용되는 시점 위치를 설정합니다.
 * @param[in] eye 월드 공간에서의 카메라 위치.
 */
void rd_eye  (v3 eye);

/**
 * @brief Uploads the point lights the world pass shades with.
 *
 * ENGLISH
 * -------
 * @param[in] pos_radius `n` vec4s: xyz world position, w reach in world units.
 * @param[in] col_power  `n` vec4s: rgb colour 0..1, a brightness multiplier.
 * @param[in] n          How many lights; clamped to ::RD_MAX_LIGHTS.
 * @note Affects ::RD_WORLD only. The view model is lit in its own space so it
 *       stays readable in a dark corner, and sprites have no normal to light.
 * @note The illumination is quantised into bands before the material is
 *       applied. That is the whole reason lights are worth having here: the
 *       resolve pass dithers to four levels, so smooth falloff would only
 *       shuffle pixels between two of them and read as noise rather than as
 *       light. Banding first gives each level an edge, and an edge reads as a
 *       layer.
 * @note Set once per frame before drawing the level; the values persist in the
 *       program until changed.
 *
 * 한국어
 * ------
 * @brief 월드 패스가 셰이딩에 사용할 점광원을 업로드합니다.
 * @param[in] pos_radius vec4 `n`개. xyz는 월드 위치, w는 도달 거리(월드 단위).
 * @param[in] col_power  vec4 `n`개. rgb는 0..1 색상, a는 밝기 배율.
 * @param[in] n          광원 개수. ::RD_MAX_LIGHTS로 제한됩니다.
 * @note ::RD_WORLD에만 영향을 줍니다. 뷰 모델은 어두운 구석에서도 읽히도록 자체
 *       공간에서 조명되며, 스프라이트에는 조명할 법선이 없습니다.
 * @note 조도는 재질이 적용되기 전에 단계로 양자화됩니다. 이것이 이곳에서 광원이 의미를
 *       갖는 이유입니다. 해상 패스가 4단계로 디더링하므로, 부드러운 감쇠는 그중 두
 *       단계 사이에서 픽셀을 섞을 뿐이며 빛이 아니라 잡음으로 읽힙니다. 먼저 단계로
 *       나누면 각 단계가 경계를 갖고, 경계는 레이어로 읽힙니다.
 * @note 레벨을 그리기 전 프레임당 한 번 설정합니다. 값은 변경 전까지 프로그램에
 *       유지됩니다.
 */
void rd_lights(const float *pos_radius, const float *col_power, int n);

/**
 * @brief How many dynamic lights the last ::rd_lights left in the shader.
 *
 * ENGLISH
 * -------
 * For tests. The number worth asserting is usually zero: a level's own lamps
 * are baked into the vertices when it loads, and putting them in these slots
 * as well applies each of them twice -- once smoothly and shadowed from the
 * bake, once per-pixel and unshadowed from the loop. A room lit twice does not
 * look broken. It looks bright, which is why this needs a test rather than an
 * eye.
 *
 * 한국어
 * ------
 * @brief 마지막 ::rd_lights가 셰이더에 남긴 동적 광원의 수입니다.
 *
 * 테스트용입니다. 단언할 가치가 있는 값은 대개 0입니다. 레벨 자신의 등은 로드될 때 정점에
 * 구워지며, 그것을 이 슬롯에도 넣으면 각 등이 두 번 적용됩니다. 한 번은 베이크에서 부드럽고
 * 그림자가 진 채로, 한 번은 반복문에서 픽셀 단위로 그림자 없이. 두 번 밝혀진 방은 고장 나
 * 보이지 않습니다. *밝아* 보이며, 그래서 이것은 눈이 아니라 테스트를 필요로 합니다.
 */
int rd_light_count(void);

/**
 * @brief Sets the pixel grid vertices snap to, reproducing the PSX wobble.
 *
 * ENGLISH
 * -------
 * @param[in] grid_w Grid width in pixels; pass the offscreen buffer's width.
 * @param[in] grid_h Grid height in pixels.
 * @note Pass 0 for either to disable snapping entirely, which is what the UI
 *       pass and any tool that wants exact geometry should do.
 * @note The grid is the OFFSCREEN buffer, not the window: the snap has to
 *       quantise to the pixels the image is actually rasterised on. Snapping
 *       to the window's resolution puts the steps at a finer spacing than the
 *       player can see and produces no visible wobble.
 * @note A COARSER grid than the buffer is the dial for "more PSX". Matching
 *       the buffer is the honest amount and is nearly invisible at 640x360,
 *       because the artefact is a whole-pixel jump and those pixels are small.
 * @warning Applies to every draw until changed, including the view model.
 *          ::wpview_draw_view disables it for exactly that reason -- the gun sits
 *          at a fixed distance in the centre of the screen, where a constant
 *          vibration is least forgivable.
 *
 * 한국어
 * ------
 * @brief 정점이 스냅되는 픽셀 격자를 설정하여 PSX의 흔들림을 재현합니다.
 * @param[in] grid_w 격자 너비(픽셀). 오프스크린 버퍼의 너비를 전달하십시오.
 * @param[in] grid_h 격자 높이(픽셀).
 * @note 어느 쪽이든 0을 전달하면 스냅이 완전히 비활성화됩니다. UI 패스와 정확한
 *       지오메트리가 필요한 도구가 그렇게 해야 합니다.
 * @note 격자는 창이 아니라 *오프스크린* 버퍼입니다. 스냅은 이미지가 실제로 래스터화되는
 *       픽셀에 맞춰 양자화해야 합니다. 창 해상도에 맞추면 플레이어가 볼 수 있는 것보다
 *       촘촘한 간격에 단계가 놓여 흔들림이 보이지 않습니다.
 * @note 버퍼보다 *성긴* 격자가 "더 PSX답게"를 위한 조정값입니다. 버퍼와 일치시키는 것이
 *       정직한 양이지만 640x360에서는 거의 보이지 않습니다. 이 아티팩트는 픽셀 단위
 *       도약인데 그 픽셀이 작기 때문입니다.
 * @warning 변경 전까지 뷰 모델을 포함한 모든 그리기에 적용됩니다. ::wpview_draw_view가 바로
 *          그 이유로 이를 비활성화합니다. 총기는 화면 중앙의 고정된 거리에 있으며,
 *          그곳에서의 지속적인 진동이 가장 용납하기 어렵습니다.
 */
void rd_snap(float grid_w, float grid_h);

/**
 * @brief How much of the PlayStation's affine texturing to use, 0 to 1.
 *
 * ENGLISH
 * -------
 * @param[in] amount 0 leaves the perspective-correct texturing every modern GPU
 *                   does; 1 is the hardware being imitated, which stepped UVs
 *                   linearly in screen space because it had no per-pixel
 *                   divide. Clamped, so a caller may hand over a raw setting.
 *
 * THE THIRD OF FOUR, and the one that was missing. ::rd_snap reproduces the
 * vertex wobble, post.c's dither and 15-bit quantisation reproduce the colour,
 * and this is the texture swim -- the artifact that shows up on any large
 * polygon seen at an angle and the one most people would name first.
 *
 * @note NOT free of consequence on this geometry. A brush level has faces far
 *       larger than anything a PlayStation drew, and the warp scales with the
 *       polygon, so 1.0 reads as a bug rather than as a period. The value the
 *       game ships with is scene.c's ::PSX_AFFINE, chosen the way
 *       ::PSX_SNAP_COARSE was: by looking at it.
 * @note Applies to every textured mode except ::RD_TEXT, which samples the UV
 *       directly. Text is already in screen coordinates, so the two
 *       interpolations agree -- but a glyph atlas is where a UV off by a texel
 *       fetches the next letter, so it is left alone rather than left to chance.
 *
 * 한국어
 * ------
 * @brief 플레이스테이션의 어파인 텍스처링을 얼마나 쓸지, 0에서 1까지.
 * @param[in] amount 0이면 모든 현대 GPU가 하는 원근 보정 텍스처링을 유지하고, 1이면 흉내 내려는
 *                   하드웨어 자신입니다. 픽셀당 나눗셈이 없어 UV를 화면 공간에서 선형으로
 *                   밟았습니다. 값을 제한하므로 호출자는 설정값을 그대로 넘겨도 됩니다.
 *
 * *넷 중 세 번째*이자 빠져 있던 하나입니다. ::rd_snap이 정점 흔들림을, post.c의 디더와 15비트
 * 양자화가 색을 재현하며, 이것이 텍스처 헤엄입니다. 비스듬히 본 큰 폴리곤이면 어디서나 드러나는
 * 아티팩트이자 대부분의 사람이 가장 먼저 이름을 댈 그것입니다.
 *
 * @note 이 지오메트리에서 *대가가 없지 않습니다.* 브러시 레벨은 플레이스테이션이 그리던 것보다
 *       훨씬 큰 면을 가지며 왜곡은 폴리곤 크기에 비례하므로, 1.0은 시대가 아니라 결함으로
 *       읽힙니다. 게임이 출하하는 값은 scene.c의 ::PSX_AFFINE이며, ::PSX_SNAP_COARSE가 정해진
 *       방식대로 눈으로 보고 골랐습니다.
 * @note ::RD_TEXT를 제외한 모든 텍스처 모드에 적용됩니다. 그 모드는 UV를 직접 샘플링합니다.
 *       텍스트는 이미 화면 좌표이므로 두 보간이 일치하지만, 글리프 아틀라스는 UV가 한 텍셀만
 *       어긋나도 다음 글자를 가져오는 곳이므로 우연에 맡기지 않고 그대로 둡니다.
 */
void rd_affine(float amount);

/**
 * @brief Sets the clock procedural materials animate against, in seconds.
 *
 * ENGLISH
 * -------
 * @param[in] t Seconds since startup. Wrapped by the caller so it never grows
 *              large enough to lose float precision.
 *
 * @note Only ::PROC_LAVA reads it. Every other procedural material is a
 *       function of the UV alone and is deliberately still -- a wall that
 *       breathes reads as a rendering fault, and stone does not move.
 * @note Wrapping is the caller's job because the period has to be a multiple
 *       of the animation's own period, or the surface jumps when the clock
 *       resets. A float holds whole seconds exactly to about 16 million, but
 *       the fractional precision that a sine needs is gone long before that:
 *       by an hour of runtime the flow visibly steps rather than flows.
 *
 * 한국어
 * ------
 * @brief 절차적 재질이 애니메이션에 사용할 시계를 초 단위로 설정합니다.
 * @param[in] t 시작 이후 경과 시간(초). float 정밀도를 잃을 만큼 커지지 않도록 호출자가
 *              순환시킵니다.
 *
 * @note ::PROC_LAVA만 이 값을 읽습니다. 다른 모든 절차적 재질은 UV만의 함수이며
 *       의도적으로 정지해 있습니다. 숨 쉬는 벽은 렌더링 결함으로 읽히고, 돌은 움직이지
 *       않습니다.
 * @note 순환은 호출자의 몫입니다. 주기가 애니메이션 자체 주기의 배수여야 하며, 그렇지
 *       않으면 시계가 초기화될 때 표면이 튑니다. float는 약 1600만까지 정수 초를 정확히
 *       담지만, 사인 함수에 필요한 소수부 정밀도는 그보다 훨씬 먼저 사라집니다. 한 시간쯤
 *       실행하면 흐름이 흐르는 것이 아니라 눈에 띄게 계단처럼 끊깁니다.
 */
void rd_time(float t);

/**
 * @brief Sets the uniform colour used by the flat, text and sprite modes.
 *
 * ENGLISH
 * -------
 * @param[in] r Red component, 0..1.
 * @param[in] g Green component, 0..1.
 * @param[in] b Blue component, 0..1.
 * @param[in] a Alpha component, 0..1.
 *
 * 한국어
 * ------
 * @brief 평면, 텍스트, 스프라이트 모드에서 사용되는 단일 색상을 설정합니다.
 * @param[in] r 빨강 성분 (0..1).
 * @param[in] g 초록 성분 (0..1).
 * @param[in] b 파랑 성분 (0..1).
 * @param[in] a 알파 성분 (0..1).
 */
void rd_color(float r, float g, float b, float a);

/**
 * @brief Selects the procedural shader and its parameters for the next draw.
 *
 * ENGLISH
 * -------
 * @param[in] proc   One of the PROC_* constants. ::PROC_TEXTURE restores
 *                   plain texture sampling.
 * @param[in] rgb    Base colour, three floats.
 * @param[in] scale  Pattern cells per UV unit.
 * @param[in] params Three spare parameters; `params[0]` is gloss.
 *
 * 한국어
 * ------
 * @brief 다음 그리기에 사용할 절차적 셰이더와 그 매개변수를 선택합니다.
 * @param[in] proc   PROC_* 상수 중 하나. ::PROC_TEXTURE는 일반 텍스처 샘플링으로
 *                   되돌립니다.
 * @param[in] rgb    기본 색상. float 3개입니다.
 * @param[in] scale  UV 단위당 패턴 셀의 수.
 * @param[in] params 예비 매개변수 3개. `params[0]`은 광택입니다.
 */
void rd_proc (int proc, const float rgb[3], float scale, const float params[3]);

#endif
