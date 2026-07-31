/**
 * @file level.h
 * @brief Doom-style sector levels: 2D floor plans with floor and ceiling heights.
 *
 * ENGLISH
 * -------
 * A sector is a 2D floor plan polygon plus a floor and ceiling height. Walls
 * are vertical. That buys rooms that are not rectangles, corridors at angles
 * and real height variation -- none of which the axis-aligned boxes this
 * replaces could express.
 *
 * Doom tiles the plane with sectors and defines their edges with linedefs.
 * That is fiddly to author, so here **sectors may overlap, and the last one
 * declared wins** at any given point. A platform inside a room is just a small
 * sector laid on top of the big one; no polygon has to be cut.
 *
 * Last-wins rather than highest-floor-wins, because the latter makes a pit
 * impossible: the room's floor would always beat the lower one dug into it.
 * With file order deciding, a platform and a pit are the same operation.
 *
 * Movement follows Doom's P_TryMove rather than doing circle-versus-segment
 * collision: ask whether the player can stand at the target point, and if
 * they cannot, refuse the move. See ::level_ground.
 *
 * @note Coordinates and heights are stored as shorts in 1/100 units
 *       (centimetres) and converted to metres on use, which is what keeps a
 *       whole level small enough to embed as text.
 *
 * 한국어
 * ------
 * 섹터는 2D 평면도 다각형에 바닥과 천장 높이를 더한 것입니다. 벽은 수직입니다.
 * 이를 통해 직사각형이 아닌 방, 비스듬한 복도, 실제 높이 변화를 표현할 수
 * 있으며, 이는 이 구조가 대체한 축 정렬 상자로는 불가능한 것들입니다.
 *
 * Doom은 섹터로 평면을 채우고 linedef로 그 경계를 정의합니다. 그 방식은 제작이
 * 번거로우므로, 여기서는 **섹터가 서로 겹칠 수 있으며, 임의의 지점에서는 마지막에
 * 선언된 섹터가 우선합니다**. 방 안의 단상은 큰 섹터 위에 놓인 작은 섹터일 뿐이며,
 * 어떤 다각형도 잘라 낼 필요가 없습니다.
 *
 * 가장 높은 바닥이 아닌 마지막 선언이 우선하는 이유는, 전자의 경우 구덩이를 만들
 * 수 없기 때문입니다. 방의 바닥이 그 안에 파인 더 낮은 바닥을 항상 이겨 버립니다.
 * 파일 순서가 결정하도록 하면 단상과 구덩이가 동일한 작업이 됩니다.
 *
 * 이동은 원-선분 충돌 대신 Doom의 P_TryMove를 따릅니다. 목표 지점에 플레이어가 설
 * 수 있는지 묻고, 설 수 없다면 이동을 거부합니다. ::level_ground를 참조하십시오.
 *
 * @note 좌표와 높이는 1/100 단위(센티미터)의 short로 저장되어 사용 시 미터로
 *       변환됩니다. 이것이 레벨 전체를 텍스트로 내장할 수 있을 만큼 작게 유지하는
 *       비결입니다.
 */
#ifndef LEVEL_H
#define LEVEL_H

/* Only the maths, deliberately -- NOT render.h.
 *
 * ENGLISH
 * -------
 * level.h is the root of the simulation half of this project: player.h,
 * enemy.h, pickup.h and weapon.h all include it. Including render.h here
 * would drag gl.h, and therefore windows.h and the whole OpenGL API, into
 * every one of them -- so a headless movement or AI test would depend on the
 * GUI stack it exists precisely to avoid.
 *
 * The only render types this header names are `MeshBuf` and `MdlRange`, both
 * used solely as pointers in one function (::level_geometry). A forward
 * declaration is all a pointer needs, so that is what is used. Callers of
 * level_geometry include render.h and model.h themselves; nobody else pays
 * for them.
 *
 * 한국어
 * ------
 * level.h는 이 프로젝트에서 시뮬레이션 영역의 뿌리입니다. player.h, enemy.h,
 * pickup.h, weapon.h가 모두 이 파일을 포함합니다. 여기서 render.h를 포함하면 gl.h가
 * 딸려 오고, 따라서 windows.h와 OpenGL API 전체가 그 모든 헤더로 끌려 들어옵니다.
 * 그렇게 되면 헤드리스 이동/AI 테스트가, 정작 피하려고 만든 GUI 스택에 의존하게
 * 됩니다.
 *
 * 이 헤더가 언급하는 렌더 타입은 `MeshBuf`와 `MdlRange`뿐이며, 둘 다 함수 하나
 * (::level_geometry)에서 포인터로만 사용됩니다. 포인터에는 전방 선언으로 충분하므로
 * 그렇게 처리했습니다. level_geometry를 호출하는 쪽이 직접 render.h와 model.h를
 * 포함하며, 그 외에는 아무도 그 비용을 치르지 않습니다.
 */
#include "m.h"

/* Forward declarations: pointers only, so the full definitions are not needed
   here. Both carry a struct tag for exactly this reason.
   전방 선언입니다. 포인터로만 사용되므로 전체 정의가 필요하지 않습니다. 두 타입 모두
   바로 이 목적을 위해 struct 태그를 가지고 있습니다. */
typedef struct MeshBuf  MeshBuf;
typedef struct MdlRange MdlRange;

/* --- Capacity limits / 용량 제한 --- */

#define LVL_MAX_SECTORS 64     ///< @brief Maximum sectors per level. / 레벨당 최대 섹터 수.
#define LVL_MAX_PTS     32     ///< @brief Maximum vertices per sector. / 섹터당 최대 정점 수.
#define LVL_MAX_ENTS    64     ///< @brief Maximum entities per level. / 레벨당 최대 엔티티 수.
#define LVL_MAT         16     ///< @brief Maximum length of a material or entity kind name. / 재질 또는 엔티티 종류 이름의 최대 길이.

/**
 * @brief Maximum material runs a level's geometry can produce.
 *
 * ENGLISH
 * -------
 * @note Levels need their own range bound: ::MDL_MAX_RANGES is sized for a
 *       weapon's handful of parts, and a level can reach three materials per
 *       sector. Six materials in the arena already overflowed it, and the
 *       surplus walls were silently dropped. All of this lives in .bss, which
 *       costs nothing on disk.
 *
 * 한국어
 * ------
 * @brief 레벨 지오메트리가 생성할 수 있는 최대 재질 구간 수입니다.
 * @note 레벨에는 자체적인 구간 한계가 필요합니다. ::MDL_MAX_RANGES는 무기의 몇 안
 *       되는 부품을 기준으로 정해진 값인 반면, 레벨은 섹터당 최대 3개의 재질에
 *       도달할 수 있습니다. 아레나의 재질 6개만으로도 이미 한계를 넘어섰고, 초과된
 *       벽은 조용히 누락되었습니다. 이 데이터는 모두 .bss에 위치하므로 디스크
 *       용량을 차지하지 않습니다.
 */
#define LVL_MAX_RANGES (LVL_MAX_SECTORS * 3)

/**
 * @brief Enough spans for an edge cut into eight pieces.
 *
 * ENGLISH
 * -------
 * @note Each piece may contribute a floor step and a ceiling step. Well past
 *       anything hand-authored, and it all lives on the stack.
 *
 * 한국어
 * ------
 * @brief 하나의 모서리가 여덟 조각으로 잘릴 경우를 감당하기에 충분한 구간 수입니다.
 * @note 각 조각은 바닥 단차와 천장 단차를 하나씩 만들어 낼 수 있습니다. 사람이
 *       직접 제작하는 수준을 훨씬 상회하며, 전부 스택에 위치합니다.
 */
#define LVL_MAX_SPANS 32

#define LVL_EXIT_RADIUS 0.9f   ///< @brief How close to an `exit` entity ends the level, metres. / `exit` 엔티티에 이만큼 가까워지면 레벨이 종료됩니다 (미터).

/* --- Type definitions / 타입 정의 --- */

/**
 * @struct Sector
 * @brief A floor plan polygon with floor and ceiling heights and materials.
 *
 * ENGLISH
 * -------
 * @note Overlapping sectors are the authoring model, not an error: the LAST
 *       one declared that contains a point governs it.
 *
 * 한국어
 * ------
 * 바닥과 천장의 높이 및 재질을 가진 평면도 다각형입니다.
 * @note 섹터가 겹치는 것은 오류가 아니라 제작 방식 그 자체입니다. 어떤 지점을
 *       포함하는 섹터 중 *마지막에* 선언된 것이 그 지점을 지배합니다.
 */
typedef struct {
    short pts[LVL_MAX_PTS * 2];   /**< x,z pairs in 1/100 units. / x,z 좌표 쌍 (1/100 단위). */
    int   n;                      /**< Number of vertices in use. / 사용 중인 정점의 수. */
    short floor, ceil;            /**< Heights in 1/100 units. / 높이 (1/100 단위). */
    char  mat_floor[LVL_MAT];     /**< Floor material name. / 바닥 재질 이름. */
    char  mat_wall[LVL_MAT];      /**< Wall material name. / 벽 재질 이름. */
    char  mat_ceil[LVL_MAT];      /**< Ceiling material name. / 천장 재질 이름. */
} Sector;

/**
 * @struct Entity
 * @brief A point marker placed in the level: a spawn, a pickup, a monster, an exit.
 *
 * ENGLISH
 * -------
 * A point marker placed in the level: a spawn, a pickup, a monster, an exit.
 * @note Kind names are interpreted by whichever module owns them -- pickup.c
 *       claims "ammo" and "health", enemy.c the monster names -- so a new
 *       entity type needs no change here.
 *
 * 한국어
 * ------
 * 레벨에 배치된 지점 표식입니다. 시작 지점, 아이템, 몬스터, 출구 등이 해당합니다.
 * @note 종류 이름은 각각을 담당하는 모듈이 해석합니다. pickup.c가 "ammo"와
 *       "health"를, enemy.c가 몬스터 이름을 담당하므로, 새로운 엔티티 종류를
 *       추가해도 이곳은 수정할 필요가 없습니다.
 */
typedef struct {
    char  kind[LVL_MAT];          /**< "spawn", "ammo", ... / "spawn", "ammo" 등. */
    short x, z;                   /**< Position in 1/100 units. / 위치 (1/100 단위). */
} Entity;

/**
 * @struct Level
 * @brief One complete level: its sectors, entities, start point and exit target.
 *
 * ENGLISH
 * -------
 * One complete level: its sectors, entities, start point and exit target.
 *
 * 한국어
 * ------
 * 하나의 완전한 레벨입니다. 섹터, 엔티티, 시작 지점, 출구 대상을 포함합니다.
 */
typedef struct {
    char    name[32];                     /**< Level name as written in the level text. / 레벨 텍스트에 기록된 레벨 이름. */
    char    next[32];                     /**< Level the exit leads to; empty means none. / 출구가 이어지는 레벨. 비어 있으면 없음을 뜻합니다. */
    Sector  sectors[LVL_MAX_SECTORS];     /**< Sectors, in declaration order -- last wins. / 선언 순서대로의 섹터. 마지막이 우선합니다. */
    int     n_sectors;                    /**< Number of sectors in use. / 사용 중인 섹터의 수. */
    Entity  ents[LVL_MAX_ENTS];           /**< Entity markers. / 엔티티 표식. */
    int     n_ents;                       /**< Number of entities in use. / 사용 중인 엔티티의 수. */
    short   start[3];                     /**< x, z, and yaw in millidegrees. / x, z 좌표와 밀리도 단위의 yaw. */
} Level;

/**
 * @struct EdgeSpan
 * @brief A solid piece of one sector edge.
 *
 * ENGLISH
 * -------
 * The sub-range [t0,t1] along the edge (0 at its first vertex, 1 at its
 * second) and the vertical range that is solid there. `face_out` says which
 * side it is seen from -- outward for the side of a platform, inward for the
 * wall of a room or the side of a pit.
 *
 * @note An edge is not uniform. Overlapping sectors are the whole authoring
 *       model here, and one may cover only part of an edge: beside it the
 *       wall is just the step between the two floors, and past its end the
 *       same edge faces the void and is solid floor to ceiling. So the edge
 *       is cut wherever any other sector's outline crosses it, and each piece
 *       answers the question for itself.
 * @note The first version sampled a single point at the edge's midpoint and
 *       applied the answer to the whole edge, which deleted an entire wall
 *       whenever anything overlapped any part of it.
 *
 * 한국어
 * ------
 * 하나의 섹터 모서리 중 막혀 있는 한 조각입니다.
 *
 * 모서리를 따르는 부분 구간 [t0,t1](첫 정점이 0, 두 번째 정점이 1)과 그 지점에서
 * 막혀 있는 수직 범위를 나타냅니다. `face_out`은 어느 쪽에서 보이는지를 나타내며,
 * 단상의 측면이면 바깥쪽, 방의 벽이나 구덩이의 측면이면 안쪽입니다.
 *
 * @note 모서리는 균일하지 않습니다. 여기서는 섹터가 겹치는 것이 제작 방식 전체이며,
 *       어떤 섹터는 모서리의 일부만 덮을 수 있습니다. 그 옆에서 벽은 두 바닥 사이의
 *       단차일 뿐이지만, 그 끝을 지나면 동일한 모서리가 빈 공간을 마주하며 바닥부터
 *       천장까지 막히게 됩니다. 따라서 다른 섹터의 외곽선이 교차하는 모든 지점에서
 *       모서리를 자르고, 각 조각이 스스로 이 질문에 답합니다.
 * @note 최초 버전은 모서리 중점의 한 지점만 검사하여 그 결과를 모서리 전체에
 *       적용했으며, 그 결과 무언가가 모서리의 일부라도 겹치면 벽 전체가 사라졌습니다.
 */
typedef struct {
    float t0, t1;      /**< Sub-range along the edge, 0..1. / 모서리를 따르는 부분 구간 (0..1). */
    float y0, y1;      /**< Solid vertical range, world units. / 막혀 있는 수직 범위 (월드 단위). */
    int   face_out;    /**< Non-zero when seen from outside the sector. / 섹터 바깥에서 보이는 경우 0이 아닙니다. */
} EdgeSpan;

/* --- Public function prototypes: loading / 공개 함수 프로토타입: 로드 --- */

/**
 * @brief Parses the named level out of the level text.
 *
 * ENGLISH
 * -------
 * @param[in]  name Level name to look for.
 * @param[out] out  Receives the parsed level; left partially written on failure.
 * @return 1 on success, 0 if no level of that name exists.
 * @warning `out` is a large struct (tens of kilobytes) and should not be a
 *          stack local in a deep call chain.
 *
 * 한국어
 * ------
 * @brief 레벨 텍스트에서 지정된 이름의 레벨을 파싱합니다.
 * @param[in]  name 찾을 레벨 이름.
 * @param[out] out  파싱된 레벨을 받습니다. 실패 시 일부만 기록된 상태로 남습니다.
 * @return 성공하면 1, 해당 이름의 레벨이 없으면 0.
 * @warning `out`은 수십 킬로바이트에 달하는 큰 구조체이므로, 깊은 호출 체인의 스택
 *          지역 변수로 사용해서는 안 됩니다.
 */
int level_load(const char *name, Level *out);

/**
 * @brief Builds floors, ceilings and walls into a vertex buffer.
 *
 * ENGLISH
 * -------
 * @param[in,out] b          Buffer receiving the geometry.
 * @param[in]     l          Level to build.
 * @param[out]    ranges     Receives one entry per run of triangles sharing a
 *                           material.
 * @param[in]     max_ranges Capacity of `ranges`; use ::LVL_MAX_RANGES.
 * @return How many ranges were written.
 * @warning Passing a `max_ranges` that is too small silently drops the
 *          surplus geometry rather than failing -- which is exactly the bug
 *          ::LVL_MAX_RANGES exists to prevent.
 *
 * 한국어
 * ------
 * @brief 바닥, 천장, 벽을 정점 버퍼에 생성합니다.
 * @param[in,out] b          지오메트리를 받을 버퍼.
 * @param[in]     l          생성할 대상 레벨.
 * @param[out]    ranges     동일한 재질을 공유하는 삼각형 구간마다 하나의 항목을
 *                           받습니다.
 * @param[in]     max_ranges `ranges`의 용량. ::LVL_MAX_RANGES를 사용하십시오.
 * @return 기록된 구간의 개수.
 * @warning `max_ranges`가 너무 작으면 실패하는 대신 초과된 지오메트리를 조용히
 *          누락시킵니다. 이것이 바로 ::LVL_MAX_RANGES가 방지하려는 버그입니다.
 */
int level_geometry(MeshBuf *b, const Level *l, MdlRange *ranges, int max_ranges);

/* --- Public function prototypes: queries / 공개 함수 프로토타입: 조회 --- */

/**
 * @brief Finds the floor and ceiling the player would stand between at a point.
 *
 * ENGLISH
 * -------
 * @param[in]  l         Level to query.
 * @param[in]  x         X coordinate in world units.
 * @param[in]  z         Z coordinate in world units.
 * @param[in]  feet      Current feet height, used to reject unreachable floors.
 * @param[in]  step      How far above `feet` a floor may be and still count;
 *                       pass a large value to ignore the limit entirely.
 * @param[out] out_floor Receives the floor height in world units.
 * @param[out] out_ceil  Receives the ceiling height in world units.
 * @return 1 when a sector covers the point, 0 outside the map.
 * @note Returns the HIGHEST qualifying sector floor, which is what makes a
 *       platform laid over a room work without cutting the room's polygon.
 *
 * 한국어
 * ------
 * @brief 특정 지점에서 플레이어가 서게 될 바닥과 천장을 찾습니다.
 * @param[in]  l         조회할 레벨.
 * @param[in]  x         월드 단위의 X 좌표.
 * @param[in]  z         월드 단위의 Z 좌표.
 * @param[in]  feet      현재 발 높이. 도달할 수 없는 바닥을 제외하는 데 쓰입니다.
 * @param[in]  step      `feet`보다 얼마나 높은 바닥까지 인정할지를 지정합니다.
 *                       큰 값을 전달하면 이 제한을 완전히 무시합니다.
 * @param[out] out_floor 월드 단위의 바닥 높이를 받습니다.
 * @param[out] out_ceil  월드 단위의 천장 높이를 받습니다.
 * @return 해당 지점을 덮는 섹터가 있으면 1, 맵 바깥이면 0.
 * @note 조건을 만족하는 섹터 중 *가장 높은* 바닥을 반환합니다. 이 덕분에 방 위에
 *       놓인 단상이 방의 다각형을 잘라 내지 않고도 동작합니다.
 */
int level_ground(const Level *l, float x, float z, float feet, float step,
                 float *out_floor, float *out_ceil);

/**
 * @brief Finds the nearest hit along a ray, for weapon fire and the grapple.
 *
 * ENGLISH
 * -------
 * @param[in]  l          Level to trace against.
 * @param[in]  origin     Ray origin in world units.
 * @param[in]  dir        Ray direction; expected to be unit length.
 * @param[in]  max_dist   Maximum distance to search.
 * @param[out] out_t      Receives the hit distance along the ray.
 * @param[out] out_normal Receives the surface normal at the hit.
 * @return 1 when something was hit within `max_dist`, 0 otherwise.
 * @warning Outputs are untouched on a miss; callers that need a fallback must
 *          supply it themselves.
 * @warning An origin outside the map hits immediately at distance zero, which
 *          is easy to mistake for a physics bug when a test fixture is placed
 *          above the ceiling.
 *
 * 한국어
 * ------
 * @brief 광선을 따라 가장 가까운 충돌 지점을 찾습니다. 사격과 그래플에 사용됩니다.
 * @param[in]  l          판정 대상 레벨.
 * @param[in]  origin     월드 단위의 광선 시작점.
 * @param[in]  dir        광선 방향. 단위 길이여야 합니다.
 * @param[in]  max_dist   탐색할 최대 거리.
 * @param[out] out_t      광선을 따른 충돌 거리를 받습니다.
 * @param[out] out_normal 충돌 지점의 표면 법선을 받습니다.
 * @return `max_dist` 이내에서 충돌하면 1, 그렇지 않으면 0.
 * @warning 빗나간 경우 출력값은 변경되지 않습니다. 대체 값이 필요한 호출자는 직접
 *          제공해야 합니다.
 * @warning 맵 바깥의 시작점은 거리 0에서 즉시 충돌합니다. 테스트 픽스처를 천장 위에
 *          배치한 경우 이를 물리 버그로 오해하기 쉽습니다.
 */
int level_trace(const Level *l, v3 origin, v3 dir, float max_dist,
                float *out_t, v3 *out_normal);

/**
 * @brief Returns the sector governing a point.
 *
 * ENGLISH
 * -------
 * @param[in] l Level to query.
 * @param[in] x X coordinate in world units.
 * @param[in] z Z coordinate in world units.
 * @return The index of the LAST declared sector containing the point, or -1
 *         outside the map.
 * @note The same rule geometry, collision and tracing use; the editor needs
 *       it so a new sector can inherit from where it is dropped.
 *
 * 한국어
 * ------
 * @brief 특정 지점을 지배하는 섹터를 반환합니다.
 * @param[in] l 조회할 레벨.
 * @param[in] x 월드 단위의 X 좌표.
 * @param[in] z 월드 단위의 Z 좌표.
 * @return 해당 지점을 포함하는 섹터 중 *마지막에* 선언된 것의 인덱스. 맵 바깥이면 -1.
 * @note 지오메트리 생성, 충돌, 광선 판정이 사용하는 것과 동일한 규칙입니다. 에디터는
 *       새 섹터가 놓인 위치의 속성을 물려받도록 하기 위해 이 함수를 필요로 합니다.
 */
int level_sector_at(const Level *l, float x, float z);

/**
 * @brief Tests whether a point is within ::LVL_EXIT_RADIUS of an `exit` entity.
 *
 * ENGLISH
 * -------
 * @param[in] l Level to query.
 * @param[in] x X coordinate in world units.
 * @param[in] z Z coordinate in world units.
 * @return 1 when standing on the exit trigger, 0 otherwise.
 * @note Kept next to the level data so the game and any test agree on where
 *       the exit is.
 *
 * 한국어
 * ------
 * @brief 특정 지점이 `exit` 엔티티로부터 ::LVL_EXIT_RADIUS 이내인지 검사합니다.
 * @param[in] l 조회할 레벨.
 * @param[in] x 월드 단위의 X 좌표.
 * @param[in] z 월드 단위의 Z 좌표.
 * @return 출구 트리거 위에 있으면 1, 그렇지 않으면 0.
 * @note 게임과 테스트가 출구 위치에 대해 동일한 판단을 내리도록 레벨 데이터 곁에
 *       두었습니다.
 */
int level_exit_at(const Level *l, float x, float z);

/* --- Public function prototypes: edge geometry / 공개 함수 프로토타입: 모서리 지오메트리 --- */

/**
 * @brief Cuts one sector edge into its solid spans.
 *
 * ENGLISH
 * -------
 * @param[in]  l      Level to query.
 * @param[in]  sector Sector index owning the edge.
 * @param[in]  edge   Edge index within that sector.
 * @param[out] out    Receives the spans; see ::EdgeSpan.
 * @param[in]  max    Capacity of `out`; use ::LVL_MAX_SPANS.
 * @return How many spans were written.
 * @note Exposed so the geometry builder and the editor's 3D picking share it.
 *       Two copies of this rule would drift, and the symptom would be
 *       clicking a wall and selecting nothing, or selecting something else.
 *
 * 한국어
 * ------
 * @brief 하나의 섹터 모서리를 막혀 있는 구간들로 잘라 냅니다.
 * @param[in]  l      조회할 레벨.
 * @param[in]  sector 해당 모서리를 소유한 섹터의 인덱스.
 * @param[in]  edge   그 섹터 내에서의 모서리 인덱스.
 * @param[out] out    구간들을 받습니다. ::EdgeSpan을 참조하십시오.
 * @param[in]  max    `out`의 용량. ::LVL_MAX_SPANS를 사용하십시오.
 * @return 기록된 구간의 개수.
 * @note 지오메트리 생성기와 에디터의 3D 선택 기능이 공유하도록 공개되었습니다. 이
 *       규칙의 사본이 두 개 존재하면 서로 어긋나게 되며, 그 증상은 벽을 클릭했을 때
 *       아무것도 선택되지 않거나 엉뚱한 것이 선택되는 형태로 나타납니다.
 */
int level_edge_spans(const Level *l, int sector, int edge,
                     EdgeSpan *out, int max);

/**
 * @brief Returns the outward normal of a sector edge in the xz plane.
 *
 * ENGLISH
 * -------
 * @param[in] l      Level to query.
 * @param[in] sector Sector index owning the edge.
 * @param[in] edge   Edge index within that sector.
 * @return The unit outward normal, with a zero y component.
 *
 * 한국어
 * ------
 * @brief xz 평면에서 섹터 모서리의 바깥 방향 법선을 반환합니다.
 * @param[in] l      조회할 레벨.
 * @param[in] sector 해당 모서리를 소유한 섹터의 인덱스.
 * @param[in] edge   그 섹터 내에서의 모서리 인덱스.
 * @return y 성분이 0인 단위 바깥 방향 법선.
 */
v3 level_edge_normal(const Level *l, int sector, int edge);

#endif
