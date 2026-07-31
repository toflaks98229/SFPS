/**
 * @file model.h
 * @brief Models as text: a named silhouette plus a thickness, extruded or lathed.
 *
 * ENGLISH
 * -------
 * The library lives in one embedded string in model.c, in the same
 * integer-only language the texture recipes use -- no float parsing anywhere
 * in the project.
 *
 * This is the format tools/modeledit.exe reads and writes, which is the real
 * reason for text: it diffs, it round-trips through an editor, and one
 * grammar covers textures, models and levels. It also happens to beat a float
 * table on size (a 3-float vector is 12 bytes; "0 1 -62" is 7) and compresses
 * far better under an exe packer.
 *
 * 한국어
 * ------
 * 모델 라이브러리는 model.c의 내장 문자열 하나에 존재하며, 텍스처 레시피와 동일한
 * 정수 전용 언어로 작성됩니다. 프로젝트 어디에도 부동소수점 파싱이 없습니다.
 *
 * 이것은 tools/modeledit.exe가 읽고 쓰는 형식이며, 텍스트를 사용하는 진짜 이유이기도
 * 합니다. 텍스트는 diff가 가능하고, 에디터를 거쳐도 원본이 보존되며, 하나의 문법이
 * 텍스처와 모델과 레벨을 모두 아우릅니다. 덤으로 float 테이블보다 크기가 작고(3개의
 * float 벡터는 12바이트지만 "0 1 -62"는 7바이트입니다) exe 패커에서 훨씬 잘
 * 압축됩니다.
 */
#ifndef MODEL_H
#define MODEL_H

#include "render.h"

/* --- Capacity limits / 용량 제한 --- */

/**
 * @brief Maximum parts in one model.
 *
 * ENGLISH
 * -------
 * @note A model is a list of parts, each with its own thickness. One
 *       thickness per model cannot serve both a thin barrel and a chunky
 *       receiver -- the first shotgun was 18 units thick with a 12-unit-tall
 *       barrel, so the barrel came out wider than it was tall and the whole
 *       thing read as a slab.
 *
 * 한국어
 * ------
 * @brief 하나의 모델이 가질 수 있는 최대 부품 수입니다.
 * @note 모델은 각자 고유한 두께를 갖는 부품들의 목록입니다. 모델당 두께가 하나뿐이면
 *       얇은 총열과 두툼한 총몸을 동시에 표현할 수 없습니다. 최초의 샷건은 두께가
 *       18 단위인데 총열 높이가 12 단위여서, 총열이 높이보다 넓게 나와 전체가 판때기처럼
 *       보였습니다.
 */
#define MDL_MAX_PARTS 12

#define MDL_MAX_RANGES MDL_MAX_PARTS   ///< @brief Upper bound on material runs per model. / 모델당 재질 구간의 상한.

/**
 * @brief Upper bound on the vertices one model can emit.
 *
 * ENGLISH
 * -------
 * @note RAM only -- meshes are generated at startup, so this costs nothing on
 *       disk.
 *
 * 한국어
 * ------
 * @brief 하나의 모델이 생성할 수 있는 정점 수의 상한입니다.
 * @note RAM에만 해당합니다. 메시는 시작 시점에 생성되므로 디스크 용량은 소모하지
 *       않습니다.
 */
#define MDL_MAX_VERTS 2048

/* --- Enumerations / 열거형 --- */

/**
 * @brief How a part's silhouette becomes geometry.
 *
 * ENGLISH
 * -------
 * How a part's silhouette becomes geometry.
 *
 * 한국어
 * ------
 * 부품의 실루엣이 지오메트리로 변환되는 방식입니다.
 */
enum {
    MDL_EXTRUDE,  /**< Closed outline pushed out to +-thickness. / 닫힌 외곽선을 +-두께만큼 밀어냅니다. */
    MDL_LATHE,    /**< Open profile revolved around the z axis; y reads as radius. / 열린 단면을 z축 기준으로 회전시킵니다. y는 반지름으로 해석됩니다. */
    MDL_MESH      /**< An authored mesh from a .obj, referenced by name. / .obj에서 제작된 메시를 이름으로 참조합니다. */
};

/* --- Type definitions / 타입 정의 --- */

/**
 * @struct MdlPart
 * @brief One part of a model: a silhouette, a thickness and a material.
 *
 * ENGLISH
 * -------
 * One part of a model: a silhouette, a thickness and a material.
 *
 * 한국어
 * ------
 * 모델의 한 부품입니다. 실루엣, 두께, 재질로 구성됩니다.
 */
typedef struct {
    short pts[MB_MAX_SILHOUETTE * 2];   /**< z,y pairs in 1/100 units. / z,y 좌표 쌍 (1/100 단위). */
    short thick[MB_MAX_SILHOUETTE];     /**< Per-point half thickness, 1/100 units. / 점별 절반 두께 (1/100 단위). */
    int   n;                            /**< Point count. / 점의 개수. */
    int   th;                           /**< Half thickness when not tapered. / 테이퍼가 없을 때의 절반 두께. */

    /**
     * @brief Non-zero when `thick[]` is authoritative rather than `th`.
     *
     * ENGLISH
     * -------
     * Explicit, rather than inferred from `thick[]` disagreeing with `th`.
     * That inference made `th` and `thick[]` two sources of truth for the
     * same number, and adjusting one silently invalidated the other.
     *
     * 한국어
     * ------
     * `thick[]`이 `th`와 다르다는 점으로 추론하지 않고 명시적으로 지정합니다. 그
     * 추론 방식은 `th`와 `thick[]`을 동일한 값에 대한 두 개의 진실 공급원으로
     * 만들었고, 한쪽을 조정하면 다른 쪽이 조용히 무효화되었습니다.
     */
    int   tapered;

    int   kind;                         /**< MDL_EXTRUDE, MDL_LATHE or MDL_MESH. / MDL_EXTRUDE, MDL_LATHE 또는 MDL_MESH. */
    int   segments;                     /**< Lathe only: revolution segment count. / 선반 방식 전용. 회전 분할 수입니다. */
    short at[3];                        /**< Part offset x,y,z in 1/100 units. / 부품 오프셋 x,y,z (1/100 단위). */
    short rot;                          /**< Part rotation about x, millidegrees. / x축 기준 부품 회전 (밀리도). */
    char  mesh[24];                     /**< MDL_MESH only: name in ASSET_MESHES. / MDL_MESH 전용. ASSET_MESHES 내의 이름입니다. */
    char  mat[16];                      /**< Material recipe name. / 재질 레시피 이름. */
} MdlPart;

/**
 * @struct MdlRange
 * @brief A run of triangles sharing one material.
 *
 * ENGLISH
 * -------
 * @note Grouping the draw this way is what lets a blued barrel, a steel
 *       receiver and a walnut grip live in one model: material contrast is
 *       what makes a gun read as a gun, far more than any amount of detail
 *       inside a single texture.
 *
 * 한국어
 * ------
 * 하나의 재질을 공유하는 삼각형 구간입니다.
 * @note 이런 방식으로 그리기를 묶는 덕분에 블루잉 처리된 총열, 강철 총몸, 호두나무
 *       손잡이가 한 모델 안에 공존할 수 있습니다. 총을 총처럼 보이게 하는 것은 단일
 *       텍스처 내부의 세부 묘사보다 재질의 대비입니다.
 * @note The `struct MdlRange` tag exists so level.h can forward-declare this
 *       rather than including model.h -- see the matching note on ::MeshBuf.
 * @note `struct MdlRange` 태그는 level.h가 model.h를 포함하지 않고 이 타입을 전방
 *       선언할 수 있도록 존재합니다. ::MeshBuf의 동일한 참고 사항을 확인하십시오.
 */
typedef struct MdlRange {
    char mat[16];        /**< Material recipe name for this run. / 이 구간의 재질 레시피 이름. */
    int  first, count;   /**< Vertex range within the built MeshBuf. / 생성된 MeshBuf 내의 정점 범위. */
} MdlRange;

/**
 * @struct Model
 * @brief A complete parsed model: its parts, UV scale and muzzle point.
 *
 * ENGLISH
 * -------
 * A complete parsed model: its parts, UV scale and muzzle point.
 *
 * 한국어
 * ------
 * 파싱이 완료된 하나의 모델입니다. 부품, UV 배율, 총구 지점을 포함합니다.
 */
typedef struct {
    char    name[32];                  /**< Model name as written in the model text. / 모델 텍스트에 기록된 모델 이름. */
    int     uv;                        /**< Texels per unit, x100. / 단위당 텍셀 수 (x100). */
    MdlPart parts[MDL_MAX_PARTS];      /**< The parts making up this model. / 이 모델을 구성하는 부품들. */
    int     n_parts;                   /**< Number of parts in use. / 사용 중인 부품의 수. */

    /**
     * @brief Where the barrel ends, in 1/100 gun-local units.
     *
     * ENGLISH
     * -------
     * The muzzle flash is drawn here and tracers leave from here, so it
     * belongs to the model rather than to weapon.c -- redraw the gun and the
     * effects follow instead of needing a constant edited to match. Defaults
     * to the front-most point on the centreline when the file does not say.
     *
     * 한국어
     * ------
     * 총구 화염이 이곳에 그려지고 예광탄이 이곳에서 출발하므로, 이 값은 weapon.c가
     * 아니라 모델에 속합니다. 총기를 다시 그리면 상수를 맞춰 수정할 필요 없이 효과가
     * 따라옵니다. 파일에 명시되지 않은 경우 중심선상에서 가장 앞쪽 점이 기본값이 됩니다.
     */
    short   muzzle[3];                 /**< x, y, z. / x, y, z 좌표. */
    int     has_muzzle;                /**< Non-zero when the file specified a muzzle. / 파일이 총구를 명시한 경우 0이 아닙니다. */
} Model;

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Parses the named model out of the model text into a struct.
 *
 * ENGLISH
 * -------
 * @param[in]  name Model name to look for.
 * @param[out] out  Receives the parsed model.
 * @return 1 on success, 0 if there is no such model.
 * @note The editor needs the points back, not just a mesh, so parsing and
 *       geometry generation are separate steps.
 *
 * 한국어
 * ------
 * @brief 모델 텍스트에서 지정된 이름의 모델을 구조체로 파싱합니다.
 * @param[in]  name 찾을 모델 이름.
 * @param[out] out  파싱된 모델을 받습니다.
 * @return 성공하면 1, 해당 모델이 없으면 0.
 * @note 에디터는 메시뿐 아니라 점 데이터 자체를 필요로 하므로, 파싱과 지오메트리
 *       생성이 별개의 단계로 분리되어 있습니다.
 */
int mdl_load(const char *name, Model *out);

/**
 * @brief Turns a parsed model into geometry.
 *
 * ENGLISH
 * -------
 * @param[in,out] b          Buffer receiving the geometry.
 * @param[in]     m          Parsed model to build.
 * @param[out]    ranges     Optional; receives one entry per contiguous run
 *                           of parts sharing a material. May be NULL.
 * @param[in]     max_ranges Capacity of `ranges`.
 * @return How many ranges were written, or the part count when `ranges` is NULL.
 * @note Coordinates are gun-local: +x right, +y up, barrel down -z.
 * @note Consecutive parts with the same material merge into a single range.
 *
 * 한국어
 * ------
 * @brief 파싱된 모델을 지오메트리로 변환합니다.
 * @param[in,out] b          지오메트리를 받을 버퍼.
 * @param[in]     m          변환할 파싱된 모델.
 * @param[out]    ranges     선택 사항. 동일한 재질을 공유하는 연속된 부품 구간마다
 *                           하나의 항목을 받습니다. NULL이어도 됩니다.
 * @param[in]     max_ranges `ranges`의 용량.
 * @return 기록된 구간의 개수. `ranges`가 NULL이면 부품의 개수를 반환합니다.
 * @note 좌표계는 총기 로컬 기준입니다. +x는 오른쪽, +y는 위, 총열은 -z 방향입니다.
 * @note 동일한 재질을 가진 연속된 부품들은 하나의 구간으로 병합됩니다.
 */
int mdl_geometry(MeshBuf *b, const Model *m, MdlRange *ranges, int max_ranges);

/**
 * @brief Convenience: load then build.
 *
 * ENGLISH
 * -------
 * @param[in,out] b    Buffer receiving the geometry.
 * @param[in]     name Model name to load and build.
 * @return The number of parts built, or 0 if the model was not found.
 *
 * 한국어
 * ------
 * @brief 편의 함수. 로드와 생성을 한 번에 수행합니다.
 * @param[in,out] b    지오메트리를 받을 버퍼.
 * @param[in]     name 로드하여 생성할 모델 이름.
 * @return 생성된 부품의 개수. 모델을 찾지 못하면 0.
 */
int mdl_build(MeshBuf *b, const char *name);

#endif
