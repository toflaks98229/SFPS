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

/* HOW MANY NUMBERS AN ENTITY MAY CARRY beyond its position. Three because that
   is what the gimmicks being brought over need at their widest -- a teleporter
   naming a destination and the facing to leave at -- and because a cap that is
   generous costs 6 bytes per entity while a cap that is tight costs a format
   change. 64 entities is 384 bytes of .bss, which the floppy never sees.
   엔티티가 위치 외에 담을 수 있는 수치의 개수입니다. 3인 이유는 가져오려는 기믹들이 가장
   넓게 필요로 하는 수가 그만큼이기 때문이며(목적지와 나갈 방향을 지정하는 텔레포터),
   넉넉한 상한은 엔티티당 6바이트지만 빠듯한 상한은 형식 변경을 치르기 때문입니다.
   엔티티 64개면 .bss 384바이트이고 플로피는 그것을 보지 않습니다. */
#define LVL_ENT_PARAMS   3

/* THE JUMP PAD. `e push <x> <z> [speed]`, speed in file units per second so
   the level text stays integers -- 1300 is 13 m/s.
   Quake's trigger_push is where this comes from. Quake's reason for SETTING
   the velocity rather than adding to it is that adding would make the height
   depend on how fast you were already falling -- but that reason does not
   apply here, and it is worth writing down that it does not: this pad only
   fires while grounded, and landing has already zeroed the fall. Set and add
   are indistinguishable, which was confirmed by changing one to the other and
   watching every check still pass.
   What actually makes the height fixed is the GROUND REQUIREMENT. `=` is kept
   anyway, because it is the form that stays correct if that requirement is
   ever relaxed, and because a pad that adds is a pad waiting to become
   unpredictable the moment anything upstream changes.
   The default is what a pad with no number gets. Chosen against this game's
   own numbers rather than Quake's: PLAYER_JUMP 7.5 against PLAYER_GRAVITY 22
   is a 1.28m hop, and 13 m/s is 3.8m -- high enough to be a route the player
   could not otherwise take, which is the only reason to place one.
   점프대입니다. `e push <x> <z> [speed]`이며 속력은 초당 파일 단위라 레벨 텍스트가 정수로
   유지됩니다. 1300이 13 m/s입니다. Quake의 trigger_push에서 왔고, 가져올 값어치가 있는
   단 하나는 이것이 속도를 *더하지 않고 설정한다*는 점입니다. 더하면 이미 얼마나 빨리
   떨어지고 있었는지에 따라 높이가 달라져, 같은 점프대가 매번 다른 곳으로 던지고 배울 수
   있는 레벨의 일부이기를 그만둡니다. 설정하면 고정 거리가 되고, 그것이 이것을 위험 요소가
   아니라 지형으로 만듭니다. 기본값은 Quake가 아니라 이 게임 수치를 기준으로 골랐습니다.
   PLAYER_GRAVITY 22에 대한 PLAYER_JUMP 7.5는 1.28m 도약이고, 13 m/s는 3.8m입니다.
   플레이어가 달리 갈 수 없는 경로가 될 만큼 높으며, 하나를 놓을 이유는 그것뿐입니다. */
#define LVL_PUSH_RADIUS   1.1f
#define LVL_PUSH_DEFAULT  1300

/**
 * @brief Maximum point lights per level.
 *
 * ENGLISH
 * -------
 * Every light is evaluated for every fragment -- there is no culling and no
 * tiling, because at this resolution and this light count a loop of eight is
 * cheaper than the machinery to avoid it. Raising this raises the per-pixel
 * cost linearly, so it is a real budget rather than a formality.
 *
 * 한국어
 * ------
 * @brief 레벨당 최대 점광원 수.
 *
 * 모든 광원은 모든 프래그먼트에 대해 계산됩니다. 컬링도 타일링도 없는데, 이 해상도와
 * 이 광원 수에서는 8회 루프가 그것을 피하기 위한 장치보다 저렴하기 때문입니다. 이 값을
 * 올리면 픽셀당 비용이 선형으로 증가하므로 형식적인 상한이 아니라 실제 예산입니다.
 */
#define LVL_MAX_DOORS   16     ///< @brief Maximum moving sectors per level. / 레벨당 최대 이동 섹터 수.

/* --- Doors and keys / 문과 열쇠 -------------------------------------------
 *
 * ENGLISH
 * -------
 * A door is a SECTOR that moves, which is how Doom did it and is the only
 * shape that costs this engine nothing: collision already asks a sector where
 * its floor and ceiling are and whether a point is inside its outline, so a
 * sector that moves is a solid that moves, for free. Nothing in level_ground,
 * open_at or level_trace has to learn what a door is.
 *
 * The authored geometry is the CLOSED state -- what you see when you walk up
 * to it. Opening displaces it by `amount`, and the direction says how:
 *
 *   up     the ceiling rises, revealing the gap underneath. The classic door.
 *   down   the floor sinks, opening a way through what was a step.
 *   x, z   the whole outline slides along that axis. A slab that opens
 *          sideways, which needs no vertical room at all.
 *
 * `up` and `down` move a height; `x` and `z` move the points. Both are things
 * the collision routines already read every frame, so the moving door is solid
 * in exactly the way its drawing says it is at every instant of its travel --
 * there is no second collision shape to keep in step.
 *
 * 한국어
 * ------
 * 문은 *움직이는 섹터*입니다. Doom이 그렇게 했고, 이 엔진에서 비용이 들지 않는 유일한
 * 형태입니다. 충돌 판정은 이미 섹터에게 바닥과 천장이 어디인지, 어떤 점이 외곽선 안에
 * 있는지를 묻고 있으므로, 움직이는 섹터는 곧 움직이는 고체입니다. level_ground, open_at,
 * level_trace 중 어느 것도 문이 무엇인지 배울 필요가 없습니다.
 *
 * 제작된 지오메트리가 *닫힌* 상태입니다. 다가갔을 때 보이는 모습입니다. 열리면 `amount`
 * 만큼 변위하며, 방향이 방식을 말합니다.
 *
 *   up     천장이 올라가 아래에 틈이 생깁니다. 고전적인 문입니다.
 *   down   바닥이 내려가 계단이던 곳이 통로가 됩니다.
 *   x, z   외곽선 전체가 해당 축을 따라 미끄러집니다. 수직 공간이 전혀 필요 없는
 *          옆으로 열리는 슬래브입니다.
 *
 * `up`과 `down`은 높이를, `x`와 `z`는 점을 움직입니다. 둘 다 충돌 루틴이 이미 매 프레임
 * 읽는 값이므로, 움직이는 문은 이동 중 모든 순간에 그림이 말하는 그대로 고체입니다.
 * 맞춰 두어야 할 두 번째 충돌 형상이 없습니다.
 */

/**
 * @brief Which way a door travels when it opens.
 * @note The order is the order ::DOOR_AXIS_NAMES lists them, which is what the
 *       level text is parsed against and what mapedit writes back.
 *
 * @brief 문이 열릴 때 이동하는 방향입니다.
 */
enum {
    DOOR_UP,    /**< Ceiling rises. / 천장이 올라갑니다. */
    DOOR_DOWN,  /**< Floor sinks. / 바닥이 내려갑니다. */
    DOOR_X,     /**< Outline slides along x. / 외곽선이 x축을 따라 미끄러집니다. */
    DOOR_Z,     /**< Outline slides along z. / 외곽선이 z축을 따라 미끄러집니다. */
    DOOR_AXES   /**< How many. / 방향의 수. */
};

/**
 * @brief Keys a door can demand, as a bit each.
 *
 * A mask rather than an index, because the player holds a set and a door names
 * one: `held & needed` is the whole check, and a door needing two keys is the
 * same expression.
 *
 * @brief 문이 요구할 수 있는 열쇠이며, 각각 한 비트입니다.
 * @note 인덱스가 아니라 마스크입니다. 플레이어는 집합을 들고 문은 하나를 지목하므로
 *       `held & needed`가 검사의 전부이며, 두 개를 요구하는 문도 같은 식입니다.
 */
enum {
    KEY_NONE   = 0,
    KEY_RED    = 1 << 0,
    KEY_BLUE   = 1 << 1,
    KEY_YELLOW = 1 << 2,
    KEY_KINDS  = 3          /**< How many distinct keys exist. / 존재하는 열쇠의 종류 수. */
};

/**
 * @struct DoorDef
 * @brief One door, as the level text authored it. Runtime state lives in door.c.
 *
 * ENGLISH
 * -------
 * @note Authored data only. Where a door is in its travel right now is not
 *       here, for the same reason ::Enemy is not in ::Level: the level is what
 *       was written down, and a door half open is something that happened
 *       since. Reloading the level text must not un-open a door by accident,
 *       and it cannot if the two never share storage.
 *
 * 한국어
 * ------
 * @brief 레벨 텍스트가 제작한 문 하나입니다. 실행 중 상태는 door.c에 있습니다.
 * @note 제작 데이터만 담습니다. 문이 지금 어디까지 열렸는지는 이곳에 없으며, ::Enemy가
 *       ::Level에 없는 것과 같은 이유입니다. 레벨은 기록된 것이고, 반쯤 열린 문은 그
 *       이후에 벌어진 일입니다. 레벨 텍스트를 다시 읽는 것이 실수로 문을 닫아서는 안
 *       되며, 둘이 저장 공간을 공유하지 않으면 그럴 수 없습니다.
 */
typedef struct {
    short sector;   /**< Index into ::Level::sectors. / ::Level::sectors의 인덱스. */
    short axis;     /**< One of the DOOR_* directions. / DOOR_* 방향 중 하나. */
    short amount;   /**< Travel in file units; signed for x and z. / 이동 거리(파일 단위). x와 z는 부호가 있습니다. */
    short speed;    /**< File units per second. / 초당 파일 단위. */
    short tag;      /**< 0 opens on touch; >0 waits for a matching switch. / 0이면 접촉 시 열리고, 0보다 크면 대응하는 스위치를 기다립니다. */
    short key;      /**< A KEY_* mask, or ::KEY_NONE. / KEY_* 마스크 또는 ::KEY_NONE. */
} DoorDef;

#define LVL_MAX_LIGHTS  8
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

/* --- Sector lookup grid / 섹터 조회 격자 --- */

/**
 * @brief Cells per axis in the sector lookup grid.
 *
 * ENGLISH
 * -------
 * ::level_trace samples ::level_ground every 5cm of ray, and each sample asked
 * every sector whether it contains the point -- so the cost of a trace scaled
 * with the sector count times the edges per sector. Measured with
 * tools/levelbench.c: 2.87us on a 2-sector level (8 edges) against 8.50us on a
 * 6-sector one (26 edges), which is linear in the edge count and extrapolates
 * to roughly 65us at ::LVL_MAX_SECTORS -- about 44% of a 60fps frame under the
 * `capped` trace load.
 *
 * The grid answers "which sectors could possibly contain this point" in one
 * indexing operation instead. 16x16 is chosen so the whole table stays small
 * enough to sit in cache while still emptying most cells on a typical map.
 *
 * @note The entire structure lives in .bss, so it costs nothing on disk. See
 *       the size rules in README.md -- a zero-filled static that reaches .bss
 *       is free, and only one that lands in .data is not.
 *
 * 한국어
 * ------
 * @brief 섹터 조회 격자의 축당 셀 수.
 *
 * ::level_trace는 광선 5cm마다 ::level_ground를 샘플링하고, 각 샘플이 모든 섹터에 해당
 * 점을 포함하는지 물었습니다. 따라서 판정 비용이 섹터 수 × 섹터당 모서리 수에 비례해
 * 증가했습니다. tools/levelbench.c로 측정한 결과, 섹터 2개(모서리 8개) 레벨에서
 * 2.87us, 6개(모서리 26개) 레벨에서 8.50us로 모서리 수에 선형이며,
 * ::LVL_MAX_SECTORS에서는 약 65us로 외삽됩니다. 이는 `capped` 판정 부하에서 60fps
 * 프레임의 약 44%에 해당합니다.
 *
 * 격자는 대신 "이 점을 포함할 수 있는 섹터는 무엇인가"에 인덱싱 한 번으로 답합니다.
 * 16x16은 전체 테이블이 캐시에 들어갈 만큼 작으면서도 일반적인 맵에서 대부분의 셀을
 * 비울 수 있는 값입니다.
 *
 * @note 구조체 전체가 .bss에 위치하므로 디스크 용량을 소모하지 않습니다. README.md의
 *       크기 규칙을 참조하십시오. .bss에 도달하는 0으로 채워진 정적 변수는 무료이며,
 *       .data에 놓이는 것만이 비용을 발생시킵니다.
 */
#define LVL_GRID_DIM 16

/**
 * @brief Maximum sectors recorded per grid cell.
 *
 * ENGLISH
 * -------
 * A cell that overflows is marked as UNUSABLE rather than truncated, and every
 * query against it falls back to the full sector scan. That distinction is the
 * whole safety argument: a truncated cell would silently forget a sector, and
 * forgetting a sector is a floor that vanishes or a wall the player walks
 * through. Falling back is merely slow, and slow is recoverable.
 *
 * 한국어
 * ------
 * @brief 격자 셀당 기록되는 최대 섹터 수.
 *
 * 초과한 셀은 잘라 내지 않고 *사용 불가*로 표시되며, 그 셀에 대한 모든 질의는 전체
 * 섹터 순회로 되돌아갑니다. 이 구분이 안전성의 핵심 논거입니다. 잘라 낸 셀은 섹터를
 * 조용히 잊어버리는데, 섹터를 잊는다는 것은 사라진 바닥이거나 플레이어가 통과하는
 * 벽입니다. 되돌아가는 것은 그저 느릴 뿐이며, 느린 것은 복구 가능합니다.
 */
#define LVL_GRID_MAX_PER_CELL 16

/**
 * @brief Sector count below which the grid is not built at all.
 *
 * ENGLISH
 * -------
 * The grid is not free. Every ::level_ground pays two integer divisions and a
 * bounds check to find its cell, and on a small level that is pure added cost:
 * the bounding-box reject in ::point_in_sector already discards nearly every
 * sector in four compares, so there is almost nothing left for the grid to
 * save. Measured with tools/levelbench.c:
 *
 *      level                     scan      grid
 *      vault      ( 2 sectors)   2.87us    4.97us   grid 1.7x SLOWER
 *      arena      ( 6 sectors)   8.50us   10.49us   grid 1.2x slower
 *      synthetic  (64 sectors)  24.68us    7.45us   grid 3.3x faster
 *
 * So the grid is built only where it pays. The threshold sits above both
 * shipped maps and well below the cap; the crossover is somewhere in the teens
 * and the exact point does not matter, because the curves are shallow on
 * either side of it.
 *
 * @note This is also why the full-scan path in ::sector_at is not dead code.
 *       It is what every level in the game currently runs.
 *
 * 한국어
 * ------
 * @brief 격자를 아예 생성하지 않는 섹터 수 하한.
 *
 * 격자는 공짜가 아닙니다. 모든 ::level_ground가 셀을 찾기 위해 정수 나눗셈 두 번과
 * 범위 검사를 치르는데, 작은 레벨에서는 이것이 순수한 추가 비용입니다.
 * ::point_in_sector의 바운딩 박스 기각이 이미 비교 4번으로 거의 모든 섹터를 걸러 내므로
 * 격자가 아낄 것이 거의 남아 있지 않습니다. tools/levelbench.c로 측정한 결과는 위의
 * 표와 같습니다.
 *
 * 따라서 격자는 이득이 되는 곳에서만 생성됩니다. 임계값은 배포되는 두 맵보다 위이고
 * 상한보다는 충분히 아래입니다. 교차점은 십몇 개 부근이며 정확한 지점은 중요하지
 * 않은데, 양쪽 모두 곡선이 완만하기 때문입니다.
 *
 * @note 이것이 ::sector_at의 전체 순회 경로가 죽은 코드가 아닌 이유이기도 합니다.
 *       현재 게임의 모든 레벨이 실제로 실행하는 경로입니다.
 */
#define LVL_GRID_MIN_SECTORS 16

/**
 * @struct SectorGrid
 * @brief A uniform grid over the level's plan, mapping a cell to its sectors.
 *
 * ENGLISH
 * -------
 * Purely an acceleration structure, never a second source of truth about where
 * a sector is -- exactly the contract ::Sector's cached bounding box follows.
 * The rules that make it safe:
 *
 *   - `built` zero means "not computed", which is what `Level l = {0}` gives.
 *     Every query then takes the full-scan path and is correct but slower. A
 *     Level assembled field by field -- which every headless fixture in tools/
 *     does -- therefore needs no knowledge that this exists.
 *   - A cell that overflowed is marked unusable and falls back per query, so
 *     no sector can ever be dropped from an answer.
 *   - A sector is recorded in every cell its bounding box touches, not just
 *     the cell of its centre, so a large sector is found from anywhere inside
 *     it.
 *
 * @warning Written by ::level_load. Anything that edits sector points
 *          afterward -- the map editor does -- must call ::level_grid_build to
 *          refresh it, exactly as it must call ::level_bounds. A STALE grid is
 *          the one genuinely dangerous state: it can omit a sector that now
 *          covers a point, which reads as a wall you can see and walk through.
 *
 * 한국어
 * ------
 * 레벨 평면도 위의 균일 격자로, 셀을 그 셀의 섹터들에 대응시킵니다.
 *
 * 순수한 가속 구조이며 섹터 위치에 대한 두 번째 진실 공급원이 아닙니다. ::Sector의
 * 캐시된 바운딩 박스가 따르는 것과 정확히 동일한 계약입니다. 안전을 보장하는 규칙은
 * 다음과 같습니다.
 *
 *   - `built`가 0이면 "계산되지 않음"이며, 이는 `Level l = {0}`이 주는 값입니다. 그러면
 *     모든 질의가 전체 순회 경로를 택하여 올바르되 느려집니다. 따라서 필드를 하나씩 채워
 *     만든 Level은(tools/의 모든 헤드리스 픽스처가 그렇습니다) 이것의 존재를 몰라도
 *     됩니다.
 *   - 초과한 셀은 사용 불가로 표시되어 질의마다 되돌아가므로, 어떤 섹터도 답에서
 *     누락될 수 없습니다.
 *   - 섹터는 중심이 속한 셀뿐 아니라 바운딩 박스가 닿는 *모든* 셀에 기록되므로, 큰
 *     섹터도 그 내부 어디에서나 발견됩니다.
 *
 * @warning ::level_load가 기록합니다. 이후 섹터의 점을 수정하는 쪽은(맵 에디터가
 *          그렇습니다) ::level_bounds를 호출해야 하는 것과 똑같이
 *          ::level_grid_build를 호출해 갱신해야 합니다. *갱신되지 않은* 격자는 진짜로
 *          위험한 유일한 상태입니다. 이제 어떤 점을 덮는 섹터를 누락시킬 수 있으며,
 *          이는 보이지만 통과할 수 있는 벽으로 나타납니다.
 */
typedef struct {
    short min_x, min_z;                  /**< Grid origin in file units. / 파일 단위의 격자 원점. */
    short cell_w, cell_h;                /**< Cell size in file units; never 0. / 파일 단위의 셀 크기. 0이 되지 않습니다. */
    unsigned char count[LVL_GRID_DIM * LVL_GRID_DIM];  /**< Sectors per cell. / 셀당 섹터 수. */
    /**
     * @brief Non-zero where a cell overflowed and must fall back to the scan.
     *
     * Separate from a sentinel in `count`, so the fallback is a decision this
     * structure records rather than one a reader has to infer from a count
     * that happens to equal the cap.
     *
     * `count`의 특별한 값이 아닌 별도 플래그입니다. 되돌아가기가, 우연히 상한과 같아진
     * 개수로부터 읽는 사람이 추론해야 하는 것이 아니라 이 구조체가 기록하는 결정이
     * 되도록 합니다.
     */
    unsigned char overflow[LVL_GRID_DIM * LVL_GRID_DIM];
    unsigned char sect[LVL_GRID_DIM * LVL_GRID_DIM][LVL_GRID_MAX_PER_CELL]; /**< Sector indices. / 섹터 인덱스. */
    short built;                         /**< Non-zero once populated. / 채워졌으면 0이 아닙니다. */
} SectorGrid;

/* LVL_MAX_SECTORS must fit in the unsigned char the grid stores indices in.
   Raising the sector cap past 256 without widening `sect` would wrap every
   index above it onto a different sector -- collision against the wrong
   polygon, with nothing to say why.
   LVL_MAX_SECTORS는 격자가 인덱스를 저장하는 unsigned char에 들어가야 합니다. `sect`를
   넓히지 않고 섹터 상한을 256 너머로 올리면 그 이상의 모든 인덱스가 다른 섹터로
   순환하게 되며, 이유를 알 수 없는 잘못된 다각형과의 충돌이 발생합니다. */
_Static_assert(LVL_MAX_SECTORS <= 256,
               "grid stores sector indices in an unsigned char");

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

    /**
     * @brief Bounding box of `pts`, in file units. Derived, not authored.
     *
     * ENGLISH
     * -------
     * A point outside this box cannot be inside the polygon, so the crossing
     * test can be skipped entirely -- four integer compares instead of walking
     * every edge. That matters because it sits in the innermost loop of the
     * simulation: ::level_trace samples ::level_ground every 5cm of ray, and
     * each sample asks every sector whether it contains the point.
     *
     * Computed once by ::level_load rather than per query, which is the whole
     * point: deriving it on each call costs the same scan it is trying to
     * avoid, and measured barely better than not having it at all.
     *
     * @note Purely an optimisation, never a second source of truth. A box that
     *       is INVALID (min > max), which is what zeroed memory and a
     *       hand-assembled Level both leave behind, is ignored and the full
     *       crossing test runs -- so a Level built field by field is correct
     *       without knowing this field exists. Only the speed is lost.
     * @warning Written by ::level_load. Anything that edits `pts` afterward --
     *          the map editor does -- should call ::level_bounds to refresh
     *          it. A STALE box (computed, then invalidated by an edit) is the
     *          one case that is genuinely wrong: it can reject a point the
     *          polygon now contains, which reads as a wall you can see and
     *          walk through.
     *
     * 한국어
     * ------
     * @brief 파일 단위로 표현된 `pts`의 바운딩 박스입니다. 제작값이 아니라 파생값입니다.
     *
     * 이 박스 바깥의 점은 다각형 내부일 수 없으므로 교차 판정을 완전히 건너뛸 수
     * 있습니다. 모든 모서리를 순회하는 대신 정수 비교 4번이면 됩니다. 이것이 중요한
     * 이유는 이 검사가 시뮬레이션의 가장 안쪽 루프에 있기 때문입니다. ::level_trace는
     * 광선 5cm마다 ::level_ground를 샘플링하고, 각 샘플은 모든 섹터에 해당 점을
     * 포함하는지 묻습니다.
     *
     * 질의마다가 아니라 ::level_load가 한 번만 계산하며, 그것이 핵심입니다. 호출할
     * 때마다 유도하면 회피하려던 바로 그 순회와 같은 비용이 들며, 측정 결과 아예 없는
     * 것보다 거의 나아지지 않았습니다.
     *
     * @note 이는 순전히 최적화이며 두 번째 진실 공급원이 아닙니다. *유효하지 않은*
     *       박스(min > max)는 무시되고 전체 교차 판정이 수행됩니다. 0으로 초기화된
     *       메모리와 손으로 조립한 Level이 모두 그 상태이므로, 필드를 하나씩 채워 만든
     *       Level은 이 필드의 존재를 몰라도 올바르게 동작합니다. 잃는 것은 속도뿐입니다.
     * @warning ::level_load가 기록합니다. 이후 `pts`를 수정하는 쪽은(맵 에디터가
     *          그렇습니다) ::level_bounds를 호출해 갱신해야 합니다. 진짜 문제가 되는
     *          경우는 *갱신되지 않은* 박스(계산된 뒤 편집으로 무효가 된 경우) 하나뿐이며,
     *          이제는 다각형이 포함하는 점을 기각할 수 있어 보이지만 통과할 수 있는 벽으로
     *          나타납니다.
     */
    short min_x, min_z, max_x, max_z;

    /**
     * @brief Non-zero once ::level_bounds has filled the box above.
     *
     * ENGLISH
     * -------
     * A separate flag rather than a sentinel inside the box itself, because
     * there is no box value that zeroed memory cannot also produce: `{0}`
     * leaves a box of (0,0)-(0,0), which is perfectly VALID and rejects
     * everything except the origin. That is not a hypothetical -- it is what
     * every headless fixture in tools/ builds, and it made the whole level
     * solid.
     *
     * Zero means "not computed", which is exactly what `Level l = {0}` gives,
     * so a Level assembled field by field is correct by default and merely
     * slower. Being wrong has to require an explicit act; being right must be
     * the thing that happens when nobody thought about it.
     *
     * 한국어
     * ------
     * @brief ::level_bounds가 위의 박스를 채웠으면 0이 아닙니다.
     *
     * 박스 안에 특별한 값을 두는 대신 별도의 플래그를 사용합니다. 0으로 초기화된 메모리가
     * 만들어 낼 수 없는 박스 값이 존재하지 않기 때문입니다. `{0}`은 (0,0)-(0,0) 박스를
     * 남기는데, 이는 완벽히 *유효하며* 원점을 제외한 모든 것을 기각합니다. 이는 가상의
     * 상황이 아니라 tools/의 모든 헤드리스 픽스처가 만들어 내는 상태이며, 레벨 전체를
     * 막힌 것으로 만들었습니다.
     *
     * 0은 "계산되지 않음"을 뜻하고 이는 `Level l = {0}`이 주는 값 그대로이므로, 필드를
     * 하나씩 채워 만든 Level은 기본적으로 올바르며 다만 느릴 뿐입니다. 틀리려면 명시적인
     * 행위가 필요해야 하고, 아무도 생각하지 않았을 때 일어나는 일이 옳은 쪽이어야 합니다.
     */
    short has_bounds;

    /**
     * @brief Damage per second dealt to anything standing on this floor. 0 is safe.
     *
     * ENGLISH
     * -------
     * Lava, acid, a burning grate -- authored as `hurt <dps>` inside a sector.
     * A rate rather than a flat amount per touch, because the player can walk
     * across a corner of it or stand in the middle, and those should not cost
     * the same. Multiplying by the frame time also makes the damage the same
     * on any machine, which a per-frame amount would not be.
     *
     * @note Stored on the SECTOR rather than inferred from the floor material.
     *       Inferring it would mean a material named "lava" was hazardous and
     *       one named "lava2" was not, and the first person to retexture a pit
     *       would make it safe without knowing they had. The two are
     *       independent on purpose: a level may use the lava material as
     *       decoration on a wall, or make an innocuous-looking floor lethal.
     * @note A short rather than a float, like every other authored number
     *       here, so the parser needs no float handling.
     *
     * 한국어
     * ------
     * @brief 이 바닥 위에 선 대상에게 초당 가하는 피해량입니다. 0이면 안전합니다.
     *
     * 용암, 산성 웅덩이, 불타는 격자 등이며 섹터 안에 `hurt <dps>`로 제작합니다. 접촉당
     * 고정량이 아니라 비율인 이유는, 플레이어가 모서리만 스쳐 지나갈 수도 있고 한가운데
     * 서 있을 수도 있는데 그 둘의 대가가 같아서는 안 되기 때문입니다. 프레임 시간을
     * 곱하면 어떤 기기에서도 피해량이 같아지는데, 프레임당 고정량은 그렇지 않습니다.
     *
     * @note 바닥 재질에서 유추하지 않고 *섹터*에 저장합니다. 유추하면 "lava"라는 이름의
     *       재질은 위험하고 "lava2"는 안전해지며, 구덩이의 텍스처를 바꾸는 첫 번째
     *       사람이 자기도 모르게 그곳을 안전하게 만들게 됩니다. 둘은 의도적으로
     *       독립적입니다. 레벨이 용암 재질을 벽 장식으로 쓸 수도 있고, 멀쩡해 보이는
     *       바닥을 치명적으로 만들 수도 있습니다.
     * @note 이곳의 다른 모든 제작 수치와 마찬가지로 float가 아닌 short이므로 파서에
     *       부동소수점 처리가 필요 없습니다.
     */
    short hurt;
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

    /**
     * @brief Optional numbers the entity's own module interprets.
     *
     * ENGLISH
     * -------
     * A jump pad needs a launch speed, a teleporter needs to name its
     * destination, a hazard needs a damage rate. None of those can be
     * expressed by a position alone, and every one of them belongs to a
     * different module.
     *
     * GENERIC ON PURPOSE, and this is the one place in the project where that
     * is the right answer rather than the lazy one. ::Entity is the only struct
     * here whose meaning is deliberately not known where it is declared -- the
     * comment above it already says so: pickup.c owns "ammo", enemy.c owns the
     * monster names, and level.c is not allowed an opinion. Naming these fields
     * `speed` and `target` would hand level.h an opinion about entities it
     * exists precisely not to know about, and the second module to want a
     * third meaning would have to either lie in a badly-named field or add a
     * fourth that nothing else uses.
     *
     * ALL ZERO WHEN UNWRITTEN, so every level authored before these existed
     * still parses to exactly what it did, and a module reading `p[0]` on an
     * entity that never set it gets a defined value rather than whatever was
     * in the slot from the last level.
     *
     * 한국어
     * ------
     * @brief 해당 엔티티를 담당하는 모듈이 해석하는 선택적 수치입니다.
     *
     * 점프대에는 발사 속력이, 텔레포터에는 목적지 지정이, 위험 지대에는 피해량이
     * 필요합니다. 어느 것도 위치만으로는 표현할 수 없고, 각각 서로 다른 모듈에 속합니다.
     *
     * 일부러 범용입니다. 이곳은 그것이 게으른 답이 아니라 *옳은* 답인 이 프로젝트의 유일한
     * 자리입니다. ::Entity는 선언된 곳에서 의미를 의도적으로 모르는 유일한 구조체이며, 위의
     * 주석이 이미 그렇게 말합니다. pickup.c가 "ammo"를, enemy.c가 몬스터 이름을 담당하고
     * level.c에는 의견이 허용되지 않습니다. 이 필드를 `speed`나 `target`으로 이름 붙이면,
     * 알지 않기 위해 존재하는 level.h에 엔티티에 대한 의견을 쥐여 주는 셈입니다. 그리고 세
     * 번째 의미를 원하는 두 번째 모듈은 잘못된 이름의 필드에서 거짓말을 하거나, 아무도 쓰지
     * 않는 네 번째 필드를 더해야 합니다.
     *
     * 기록되지 않으면 전부 0이므로, 이 필드가 생기기 전에 작성된 레벨도 정확히 그대로
     * 해석되고, 설정한 적 없는 엔티티에서 `p[0]`을 읽는 모듈은 이전 레벨의 잔여물이 아니라
     * 정의된 값을 얻습니다.
     */
    short p[LVL_ENT_PARAMS];
} Entity;

/**
 * @struct Light
 * @brief A point light: where it is, how far it reaches, and what colour it is.
 *
 * ENGLISH
 * -------
 * Declared separately from ::Entity rather than as another `kind`, because an
 * entity is a point on the floor plan -- x and z only -- and a light needs a
 * height, a radius and a colour. Bolting four more fields onto Entity would
 * cost every spawn and pickup marker the same bytes for fields they never use.
 *
 * @note There is no direction. These are omnidirectional, which is what a
 *       torch or a strip in a ceiling actually is, and a cone would need an
 *       axis and two angles for a look this project does not use.
 *
 * 한국어
 * ------
 * 점광원입니다. 위치, 도달 거리, 색상으로 구성됩니다.
 *
 * 또 다른 `kind`가 아니라 ::Entity와 별도로 선언합니다. 엔티티는 평면도상의 한 점(x와 z
 * 뿐)인 반면 광원은 높이, 반경, 색상이 필요하기 때문입니다. Entity에 필드 네 개를 더
 * 붙이면 모든 스폰·아이템 표식이 쓰지도 않는 필드에 같은 바이트를 지불하게 됩니다.
 *
 * @note 방향이 없습니다. 전방향 광원이며, 횃불이나 천장의 조명등이 실제로 그렇습니다.
 *       원뿔형은 축과 두 개의 각도가 필요한데 이 프로젝트가 쓰지 않는 룩입니다.
 */
typedef struct {
    short x, y, z;                /**< Position in 1/100 units. / 위치 (1/100 단위). */
    short radius;                 /**< Reach in 1/100 units; nothing past this is lit. / 도달 거리 (1/100 단위). 이를 넘으면 비추지 않습니다. */
    short r, g, b;                /**< Colour, 0..255. / 색상 (0..255). */
    short power;                  /**< Brightness, percent. 100 is the reference. / 밝기 (퍼센트). 100이 기준입니다. */
} Light;

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
    Light   lights[LVL_MAX_LIGHTS];       /**< Point lights. / 점광원. */
    int     n_lights;                     /**< Number of lights in use. / 사용 중인 광원의 수. */
    DoorDef doors[LVL_MAX_DOORS];         /**< Moving sectors. / 이동하는 섹터. */
    int     n_doors;                      /**< Number of doors in use. / 사용 중인 문의 수. */
    short   start[3];                     /**< x, z, and yaw in millidegrees. / x, z 좌표와 밀리도 단위의 yaw. */

    /**
     * @brief Sector lookup acceleration. Derived, not authored.
     *
     * Filled by ::level_load and refreshed by ::level_grid_build. Zeroed means
     * "not built", which every query handles by falling back to the full
     * sector scan -- so this field can be ignored entirely by anything that
     * assembles a Level by hand. See ::SectorGrid.
     *
     * ::level_load가 채우고 ::level_grid_build가 갱신합니다. 0이면 "생성되지 않음"이며,
     * 모든 질의가 전체 섹터 순회로 되돌아가 이를 처리합니다. 따라서 Level을 손으로
     * 조립하는 쪽은 이 필드를 완전히 무시해도 됩니다. ::SectorGrid를 참조하십시오.
     */
    SectorGrid grid;
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
 * @brief Recomputes one sector's cached bounding box from its points.
 *
 * ENGLISH
 * -------
 * @param[in,out] s Sector whose `min_x`/`min_z`/`max_x`/`max_z` are refreshed.
 * @note ::level_load calls this for every sector it parses, so a level read
 *       from disk needs nothing further. It is exposed for the map editor,
 *       which moves vertices after loading: the bounds are what collision
 *       rejects against, so a stale box makes a sector unenterable or lets the
 *       player walk into a wall, with no other symptom.
 * @note Safe on a degenerate sector: fewer than three points leaves an empty
 *       box, which rejects everything -- the same answer the crossing test
 *       gives for a polygon with no area.
 *
 * 한국어
 * ------
 * @brief 섹터의 캐시된 바운딩 박스를 점들로부터 다시 계산합니다.
 * @param[in,out] s `min_x`/`min_z`/`max_x`/`max_z`를 갱신할 섹터.
 * @note ::level_load가 파싱하는 모든 섹터에 대해 이 함수를 호출하므로, 디스크에서 읽은
 *       레벨에는 추가 작업이 필요 없습니다. 이 함수는 로드 이후 정점을 옮기는 맵
 *       에디터를 위해 공개되어 있습니다. 충돌 판정이 이 경계값으로 기각을 수행하므로,
 *       갱신되지 않은 박스는 섹터에 들어갈 수 없게 만들거나 플레이어가 벽을 통과하게
 *       만들며, 다른 증상은 나타나지 않습니다.
 * @note 축퇴된 섹터에도 안전합니다. 점이 3개 미만이면 빈 박스가 되어 모든 것을
 *       기각하는데, 이는 넓이가 없는 다각형에 대해 교차 판정이 내리는 답과 같습니다.
 */
void level_bounds(Sector *s);

/**
 * @brief Rebuilds the level's sector lookup grid from its sectors.
 *
 * ENGLISH
 * -------
 * @param[in,out] l Level whose ::SectorGrid is refreshed.
 * @note ::level_load calls this after computing every sector's bounds, so a
 *       level read from the text needs nothing further. It is exposed for the
 *       map editor, which moves vertices after loading.
 * @note Requires each sector's bounding box to be current, so call
 *       ::level_bounds on every edited sector FIRST -- the grid is built from
 *       those boxes, and a stale box puts a sector in the wrong cells.
 * @note Safe on a level with no sectors: the grid is marked unbuilt and every
 *       query takes the scan path, which is correct and merely slower.
 *
 * 한국어
 * ------
 * @brief 레벨의 섹터 조회 격자를 섹터들로부터 다시 생성합니다.
 * @param[in,out] l ::SectorGrid를 갱신할 레벨.
 * @note ::level_load가 모든 섹터의 경계값을 계산한 뒤 이 함수를 호출하므로, 텍스트에서
 *       읽은 레벨에는 추가 작업이 필요 없습니다. 로드 이후 정점을 옮기는 맵 에디터를
 *       위해 공개되어 있습니다.
 * @note 각 섹터의 바운딩 박스가 최신이어야 하므로, 편집된 모든 섹터에 대해
 *       ::level_bounds를 *먼저* 호출하십시오. 격자는 그 박스들로부터 생성되며, 갱신되지
 *       않은 박스는 섹터를 엉뚱한 셀에 넣습니다.
 * @note 섹터가 없는 레벨에도 안전합니다. 격자가 미생성으로 표시되고 모든 질의가 순회
 *       경로를 택하며, 이는 올바르되 다만 느립니다.
 */
void level_grid_build(Level *l);

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
 * @brief Whether anything solid lies along a ray. The visibility question only.
 *
 * ENGLISH
 * -------
 * @param[in] l        Level to trace against.
 * @param[in] origin   Ray origin in world units.
 * @param[in] dir      Ray direction; expected to be unit length.
 * @param[in] max_dist Maximum distance to search.
 * @return 1 when something blocks the ray within `max_dist`, 0 when it is clear.
 *
 * @note Answers exactly what a line-of-sight test asks and nothing more.
 *       ::level_trace also bisects the last open interval to find WHERE the hit
 *       was and derives the surface normal there -- ten extra samples plus, for
 *       a wall, a scan of every edge of every sector in ::nearest_edge_normal.
 *       A caller that discards both was paying for them, and the monster
 *       visibility test is called once per monster per frame where a shot is
 *       fired twice a second.
 * @note Marches with the same step as ::level_trace, from the same constant, so
 *       the two cannot disagree about whether a given wall is solid. A
 *       visibility test that found a gap the shot test would not is a monster
 *       shooting through a wall.
 * @warning An origin outside the map reports blocked, matching ::level_trace's
 *          immediate hit at distance zero. A fixture placed above the ceiling
 *          therefore sees nothing at all, which is easy to mistake for an AI
 *          bug.
 *
 * 한국어
 * ------
 * @brief 광선을 따라 막는 것이 있는지 여부입니다. 가시성 질문만을 다룹니다.
 * @param[in] l        판정 대상 레벨.
 * @param[in] origin   월드 단위의 광선 시작점.
 * @param[in] dir      광선 방향. 단위 길이여야 합니다.
 * @param[in] max_dist 탐색할 최대 거리.
 * @return `max_dist` 이내에서 광선을 막는 것이 있으면 1, 뚫려 있으면 0.
 *
 * @note 시야 판정이 묻는 것을 정확히, 그리고 그 이상은 답하지 않습니다.
 *       ::level_trace는 충돌 *지점*을 찾기 위해 마지막 열린 구간을 이분 탐색하고 그곳의
 *       표면 법선을 유도합니다. 추가 샘플 10회에 더해, 벽인 경우
 *       ::nearest_edge_normal이 모든 섹터의 모든 모서리를 순회합니다. 이 둘을 모두 버리는
 *       호출자는 그 비용을 지불하고 있었으며, 몬스터 가시성 판정은 몬스터마다 매 프레임
 *       호출되는 반면 사격은 초당 두 번입니다.
 * @note ::level_trace와 동일한 상수에서 나온 동일한 간격으로 전진하므로, 특정 벽이
 *       막혀 있는지에 대해 둘이 어긋날 수 없습니다. 사격 판정은 찾지 못했을 틈을 가시성
 *       판정이 찾아낸다면, 그것은 벽을 관통해 쏘는 몬스터입니다.
 * @warning 맵 바깥의 시작점은 막힘으로 보고되며, 이는 거리 0에서 즉시 충돌하는
 *          ::level_trace의 동작과 일치합니다. 따라서 천장 위에 배치된 픽스처는 아무것도
 *          보지 못하며, 이를 AI 버그로 오해하기 쉽습니다.
 */
int level_blocked(const Level *l, v3 origin, v3 dir, float max_dist);

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
 * @brief Damage per second the floor at a point deals.
 *
 * ENGLISH
 * -------
 * @param[in] l Level to query.
 * @param[in] x X coordinate in world units.
 * @param[in] z Z coordinate in world units.
 * @return The governing sector's ::Sector::hurt, or 0 outside the map and on
 *         any safe floor.
 * @note Uses the same last-declared-wins rule as ::level_ground, so a safe
 *       platform laid over a lava pit is safe to stand on -- which is what
 *       makes a lava room playable rather than merely lethal.
 * @note Answers only "how dangerous is the floor here". Whether the player is
 *       actually touching it is the caller's question, because the answer
 *       differs for a player standing on it, a monster wading through it and
 *       a projectile passing over it.
 *
 * 한국어
 * ------
 * @brief 특정 지점의 바닥이 가하는 초당 피해량입니다.
 * @param[in] l 조회할 레벨.
 * @param[in] x 월드 단위의 X 좌표.
 * @param[in] z 월드 단위의 Z 좌표.
 * @return 해당 지점을 지배하는 섹터의 ::Sector::hurt. 맵 바깥이거나 안전한 바닥이면 0.
 * @note ::level_ground와 동일한 "마지막 선언 우선" 규칙을 사용하므로, 용암 구덩이 위에
 *       놓인 안전한 발판은 밟고 설 수 있습니다. 그것이 용암 방을 단순히 치명적인 곳이
 *       아니라 플레이 가능한 곳으로 만듭니다.
 * @note "이곳의 바닥이 얼마나 위험한가"에만 답합니다. 플레이어가 실제로 닿아 있는지는
 *       호출자의 질문입니다. 그 답이 바닥에 선 플레이어, 그곳을 지나는 몬스터, 위를
 *       스치는 발사체마다 다르기 때문입니다.
 */
int level_hazard_at(const Level *l, float x, float z);

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

/**
 * @brief The launch speed of a jump pad under this point, or 0 for none.
 *
 * ENGLISH
 * -------
 * @param[in] l    The level.
 * @param[in] x,z  Where the player is standing, in metres.
 * @return Upward speed in metres per second, or 0 when no pad is here.
 * @note Returns the SPEED rather than a yes/no, so the caller has nothing left
 *       to look up and cannot pair the wrong pad with the wrong number. It is
 *       also why 0 doubles as "no pad": a pad that launched at nothing would
 *       be indistinguishable from floor, so the value and the question have
 *       the same answer.
 *
 * 한국어
 * ------
 * @brief 이 지점 아래 점프대의 발사 속력이며, 없으면 0입니다.
 * @return 초당 미터 단위의 상승 속력. 점프대가 없으면 0입니다.
 * @note 예/아니오가 아니라 *속력*을 반환하므로 호출자가 더 찾아볼 것이 없고, 엉뚱한
 *       점프대와 엉뚱한 수치를 짝지을 수 없습니다. 0이 "점프대 없음"을 겸하는 이유이기도
 *       합니다. 아무 속력도 내지 않는 점프대는 바닥과 구별되지 않으므로, 값과 질문의 답이
 *       같습니다.
 */
float level_push_at(const Level *l, float x, float z);

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
