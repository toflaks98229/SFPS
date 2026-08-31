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
typedef struct BrushMap BrushMap;

/**
 * @brief Where the brush maps loaded levels point at actually live.
 *
 * ENGLISH: Forward-declared and never defined here, on purpose. The definition
 * needs ::BrushMap by value and therefore brush.h, and this header is inside
 * pools.h -> world.h -- so defining it here would push brush.h into every
 * translation unit that touches a ::World. brushstore.h holds the definition and
 * only the files that allocate a store include it.
 *
 * 한국어: 의도적으로 전방 선언만 하고 이곳에서 정의하지 않습니다. 정의에는 값으로서의
 * ::BrushMap이, 따라서 brush.h가 필요합니다. 그런데 이 헤더는 pools.h -> world.h 안에 있으므로
 * 이곳에서 정의하면 ::World에 닿는 모든 번역 단위로 brush.h가 밀려듭니다. 정의는
 * brushstore.h가 담으며, 저장소를 할당하는 파일만 그것을 포함합니다.
 */
typedef struct BrushStore BrushStore;

/* --- Capacity limits / 용량 제한 --- */

/* Overridable by the build so a second binary can be compiled with caps the
   shipped levels overrun, the same way LIGHT_CACHE_SLOTS and MAX_CACHED are.
   Both refusals below them -- the sector the loader has no slot for, the point
   that does not fit an outline -- report through ::DIAG_SECTOR_CAP and
   ::DIAG_POINT_CAP, and a counter whose branch no binary reaches is a counter
   nobody has seen work. dm03 is 59 sectors against 64 and its largest outline
   is 38 against 48, so nothing here overflows on its own.
   build.ps1이 출하 레벨이 초과하는 상한으로 두 번째 바이너리를 컴파일할 수 있도록
   재정의를 허용합니다. LIGHT_CACHE_SLOTS와 MAX_CACHED가 그러한 것과 같습니다. 아래 두
   거절(로더에 자리가 없는 섹터, 외곽선에 들어가지 못하는 점)은 ::DIAG_SECTOR_CAP과
   ::DIAG_POINT_CAP으로 보고하며, 어떤 바이너리도 도달하지 않는 분기를 가진 카운터는
   아무도 동작을 본 적 없는 카운터입니다. dm03은 64에 대해 섹터 59개이고 가장 큰 외곽선은
   48에 대해 38이므로, 이곳의 무엇도 저절로 넘치지 않습니다. */
#ifndef LVL_MAX_SECTORS
#define LVL_MAX_SECTORS 64     ///< @brief Maximum sectors per level. / 레벨당 최대 섹터 수.
#endif
/* Raised from 32 when the first imported Doom level was measured against it.
   Hand-authored sectors here are four to eight points; a room somebody drew in
   a Doom editor is whatever shape the walls took, and dm03's largest is 38.
   48 rather than 38 because the number is a .bss cost of two bytes per point
   per sector and nothing else -- a cap set to exactly what today's map needs
   is a cap that fails on tomorrow's.
   첫 Doom 레벨을 상한과 대조하면서 32에서 올렸습니다. 이곳에서 손으로 작성한 섹터는
   4~8점이지만, Doom 에디터에서 그린 방은 벽이 이룬 모양 그대로이고 dm03의 최대는
   38입니다. 38이 아니라 48인 이유는, 이 값이 섹터당 점당 2바이트의 .bss 비용일 뿐이며
   오늘의 맵에 정확히 맞춘 상한은 내일의 맵에서 실패하기 때문입니다. */
#ifndef LVL_MAX_PTS
#define LVL_MAX_PTS     48     ///< @brief Maximum vertices per sector. / 섹터당 최대 정점 수.
#endif
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
 * @warning THE NAMES ARE SPELLED OUT TWICE and neither copy is this enum.
 *          level.c parses them as a `txt_is` chain and tools\mapedit.c writes
 *          them back from a local `AXIS[DOOR_AXES]` table, so adding an axis
 *          here compiles clean, loads as ::DOOR_UP, and saves as "up" -- a
 *          door that quietly changes direction when a map is opened and
 *          resaved. There is no shared table to add it to; there are two
 *          places to remember, and this note is the only thing that says so.
 *
 * @brief 문이 열릴 때 이동하는 방향입니다.
 * @warning *이름이 두 번 따로 적혀 있으며* 그 어느 쪽도 이 열거형이 아닙니다. level.c는
 *          `txt_is` 사슬로 파싱하고 tools\mapedit.c는 자기 지역 `AXIS[DOOR_AXES]` 표에서
 *          되씁니다. 따라서 이곳에 축을 추가하면 컴파일은 통과하고, ::DOOR_UP으로 로드되며,
 *          "up"으로 저장됩니다. 맵을 열었다 다시 저장하면 조용히 방향이 바뀌는 문입니다.
 *          추가할 공유 표는 없습니다. 기억해야 할 곳이 둘 있고, 그렇게 말해 주는 것은 이
 *          주석뿐입니다.
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
    /**
     * @brief Index into ::Level::sectors, or -1 when this door moves brushes.
     *
     * The two models both fit here because a door is the same idea in each: a
     * solid that is somewhere else when it is open. Which one this is decides
     * what ::door_update translates -- a Sector's heights and outline, or a run
     * of brushes -- and nothing past that has to care.
     *
     * ::Level::sectors의 인덱스. 이 문이 브러시를 움직이면 -1입니다.
     * @note 두 모델이 모두 이곳에 들어맞는 이유는, 문이 어느 쪽에서나 같은 개념이기
     *       때문입니다. 열려 있을 때 다른 곳에 있는 고체입니다. 어느 쪽인지가
     *       ::door_update가 무엇을 옮길지 결정하며(Sector의 높이와 외곽선인지, 브러시
     *       구간인지) 그 너머의 무엇도 신경 쓸 필요가 없습니다.
     */
    short sector;

    /**
     * @brief The brushes this door moves, when `sector` is -1.
     *
     * Copied from the ::BrushEnt that owned them, which already recorded them
     * as a run -- so `func_door`'s leaf needs no list and no second table.
     * Meaningless on a sector door, where both are 0.
     *
     * `sector`가 -1일 때 이 문이 움직이는 브러시입니다.
     * @note 그것들을 소유한 ::BrushEnt에서 복사합니다. 그쪽이 이미 연속 구간으로 기록해
     *       두었으므로 `func_door`의 문짝에는 목록도, 두 번째 표도 필요 없습니다. 섹터 문에서는
     *       의미가 없으며 둘 다 0입니다.
     */
    short first_brush, n_brushes;

    short axis;     /**< One of the DOOR_* directions. / DOOR_* 방향 중 하나. */
    short amount;   /**< Travel in file units; signed for x and z. / 이동 거리(파일 단위). x와 z는 부호가 있습니다. */
    short speed;    /**< File units per second. / 초당 파일 단위. */
    short tag;      /**< 0 opens on touch; >0 waits for a matching switch. / 0이면 접촉 시 열리고, 0보다 크면 대응하는 스위치를 기다립니다. */
    short key;      /**< A KEY_* mask, or ::KEY_NONE. / KEY_* 마스크 또는 ::KEY_NONE. */
} DoorDef;

/**
 * @struct DoorState
 * @brief Where one door is in its travel, and what shape it started as.
 *
 * ENGLISH
 * -------
 * DECLARED HERE, WRITTEN ONLY BY door.c. level.c does not read a field of this
 * and never should: the storage is the level's because it must be matched to
 * ::Level::doors index for index, and the MEANING is door.c's. That is the
 * split ::Level::brushes already keeps with brush.c, one layer down -- the
 * difference is only that a ::DoorSet is small enough to hold by value.
 *
 * The closed shape is copied here at ::door_reset because ::door_update
 * overwrites the sector: after one frame of motion the level no longer holds
 * the door's starting position, so anything that derived "closed" from the
 * level would drift a little further open every frame.
 *
 * 한국어
 * ------
 * @brief 문 하나가 이동 중 어디에 있는지, 그리고 무슨 형상으로 출발했는지.
 *
 * 이곳에 선언되지만 기록하는 것은 door.c뿐입니다. level.c는 이 구조체의 필드를 읽지 않으며
 * 그래서도 안 됩니다. 저장 공간이 레벨의 것인 이유는 ::Level::doors와 인덱스 대 인덱스로
 * 대응해야 하기 때문이고, *의미*는 door.c의 것입니다. 한 계층 아래에서 ::Level::brushes가
 * brush.c와 이미 지키고 있는 분리이며, 차이는 ::DoorSet이 값으로 담을 만큼 작다는 것뿐입니다.
 *
 * 닫힌 형상을 ::door_reset에서 이곳으로 복사합니다. ::door_update가 섹터를 덮어쓰므로,
 * 한 프레임만 움직여도 레벨은 문의 출발 위치를 더 이상 담고 있지 않습니다. "닫힘"을
 * 레벨에서 유도하는 것은 무엇이든 매 프레임 조금씩 더 열리게 됩니다.
 */
typedef struct {
    float t;                        /**< 0 closed .. 1 open. / 0이면 닫힘, 1이면 열림. */
    float wait;                     /**< Seconds left of the open pause. / 열린 채 대기하는 남은 시간. */
    int   opening;                  /**< Non-zero while travelling open. / 열리는 중이면 0이 아닙니다. */
    short floor0, ceil0;            /**< Closed heights. / 닫힌 상태의 높이. */
    short pts0[LVL_MAX_PTS * 2];    /**< Closed outline. / 닫힌 상태의 외곽선. */
    int   n0;                       /**< Points in `pts0`; 0 marks a slot nothing captured. / `pts0`의 점 개수. 0이면 포착된 적 없는 슬롯입니다. */

    /**
     * @brief How far a BRUSH door has already been translated, in metres.
     *
     * ENGLISH
     * -------
     * The sector door needs nothing like this: `pts0` and `floor0` are the
     * closed shape, and ::apply writes the target absolutely from them every
     * frame, so where it currently is never has to be remembered.
     *
     * Brush planes carry no such snapshot -- ::brush_translate is relative and
     * there are up to ::BR_MAX_FACES of them per brush -- so what is stored is
     * the one number that makes the move reversible: how much has been applied.
     * The next frame asks for the difference.
     *
     * @warning Zero means the brushes are where the .map drew them, which is
     *          true immediately after ::level_load and is why ::door_reset must
     *          follow one. A reset against a level whose doors are part way
     *          open would call that position closed and the leaf would never
     *          return to the doorway.
     *
     * 한국어
     * ------
     * @brief *브러시* 문이 이미 평행이동된 거리이며 미터 단위입니다.
     *
     * 섹터 문에는 이런 것이 필요 없습니다. `pts0`와 `floor0`가 닫힌 형상이고 ::apply가 매
     * 프레임 그것으로부터 목표를 절대적으로 기록하므로, 지금 어디에 있는지는 기억할 필요가
     * 없습니다.
     *
     * 브러시 평면에는 그런 스냅숏이 없습니다. ::brush_translate는 상대적이고 브러시마다
     * 최대 ::BR_MAX_FACES개가 있습니다. 그래서 저장하는 것은 이동을 되돌릴 수 있게 만드는 단
     * 하나의 숫자, 즉 얼마나 적용했는가입니다. 다음 프레임은 그 차이를 요청합니다.
     *
     * @warning 0은 브러시가 .map이 그린 자리에 있다는 뜻이며, ::level_load 직후에 참입니다.
     *          ::door_reset이 그 뒤를 따라야 하는 이유입니다. 문이 반쯤 열린 레벨에 대해
     *          리셋하면 그 위치를 닫힘이라 부르게 되고 문짝은 결코 출입구로 돌아오지
     *          않습니다.
     */
    float applied;

    /**
     * @brief A brush door's CLOSED extent, in world units.
     *
     * The counterpart of `pts0` for the other model, and it exists for the same
     * reason: the touch test and the sound both measure against where the door
     * is when shut, so that a door already sliding does not walk away from the
     * player who opened it and stall halfway across.
     *
     * 브러시 문의 *닫힌* 크기이며 월드 단위입니다.
     * @note 다른 모델에서의 `pts0`에 해당하며 이유도 같습니다. 접촉 판정과 소리는 모두 문이
     *       닫혀 있을 때의 자리를 기준으로 재며, 그래야 이미 미끄러지고 있는 문이 그것을 연
     *       플레이어에게서 멀어져 중간에 멈추지 않습니다.
     */
    v3 lo0, hi0;

    /**
     * @brief Which sector the shape above was copied from, or -1.
     *
     * ENGLISH
     * -------
     * PROVENANCE, NOT A SECOND COPY OF THE TRUTH. The note on ::DoorSet::keys
     * argues against copying a door's `key` into this struct, and the argument
     * is right: nothing writes `key`, so a copy of it would be a second source
     * kept in step by nothing. This field is the opposite case and has to be
     * read as such. It is not the sector the door acts on -- ::apply still asks
     * the definition for that, and always will. It is a record of where the
     * snapshot beside it came from, which is a fact about `pts0` and belongs
     * with `pts0`.
     *
     * A snapshot that does not know what it is a snapshot OF is the actual
     * defect here. This array and the level's `doors[]` are matched by index
     * and by nothing else; ::door_reset is what agrees them, and until this
     * field existed, a ::door_update run against a level that reset never saw
     * would write one sector's geometry out of another's closed shape and look
     * exactly like a door working. Now it is a ::DIAG_DOOR_STALE and a door
     * that does not move.
     *
     * 한국어
     * ------
     * @brief 위의 형상을 복사해 온 섹터, 또는 -1.
     *
     * *출처*이지 진실의 두 번째 사본이 아닙니다. ::DoorSet::keys의 주석은 문의 `key`를 이
     * 구조체로 복사하는 것에 반대하며, 그 주장은 옳습니다. `key`는 아무도 쓰지 않으므로 그
     * 사본은 무엇으로도 일치가 유지되지 않는 두 번째 진실이 됩니다. 이 필드는 정반대의
     * 경우이며 그렇게 읽어야 합니다. 이것은 문이 작용하는 섹터가 아닙니다. ::apply는 여전히
     * 그것을 정의에게 묻고 앞으로도 그럴 것입니다. 이것은 옆에 있는 스냅숏이 어디에서 왔는지에
     * 대한 기록이며, `pts0`에 관한 사실이므로 `pts0` 옆에 있어야 합니다.
     *
     * 자신이 무엇의 스냅숏인지 모르는 스냅숏이 이곳의 실제 결함입니다. 이 배열과 레벨의
     * `doors[]`는 인덱스로만 대응하며 다른 무엇으로도 대응하지 않습니다. 둘을 일치시키는 것은
     * ::door_reset이고, 이 필드가 생기기 전에는 reset이 본 적 없는 레벨에 대해 실행된
     * ::door_update가 한 섹터의 지오메트리를 다른 섹터의 닫힌 형상으로부터 쓰면서도 정확히
     * 정상 동작하는 문처럼 보였습니다. 이제 그것은 ::DIAG_DOOR_STALE이며 움직이지 않는
     * 문입니다.
     */
    short sector;
} DoorState;

/**
 * @struct DoorSet
 * @brief Every door's motion, owned by the level whose doors they are.
 *
 * ENGLISH
 * -------
 * WHY THIS IS A FIELD OF ::Level AND NOT A GLOBAL IN door.c. It was the
 * latter, and the cost was the one ::Pools was extracted to remove one layer
 * up: a second ::Level in play shared the first one's doors, so a ::World whose
 * whole premise is that a caller cannot hold half a game held exactly half of
 * one. ::DIAG_DOOR_STALE exists because of it -- the counter was added to
 * detect state that had outlived the level it described, which is a thing that
 * can only happen while the state and the level are separable.
 *
 * WHY ::Level AND NOT ::World, which is where ::Pools went. The pools are what
 * a RUN spawns and they outlive any one level; these are matched to
 * ::Level::doors index for index and mean nothing without it. Holding them in
 * ::World would have kept two things that must agree in two structs a caller
 * passes separately -- which is the same shape as the bug, with the global
 * spelled differently. Here the mismatch is not detected, it is unconstructable:
 * ::door_update takes one ::Level and there is no second one to get wrong.
 *
 * @note ~3.8KB against this struct's 24KB, and ::Level is a stack local all
 *       over the test suite. That is the trade, made deliberately: ::BrushMap
 *       at 420KB was too big and stayed a pointer, this is not, and unlike the
 *       brushes it has to agree with an array that is already in here.
 * @note All-zero is a valid empty set -- `count` of 0, every `n0` of 0 -- so
 *       `Level l = {0}` needs no further reset and a fixture that builds a
 *       Level field by field stays a level whose doors do not move.
 *
 * 한국어
 * ------
 * @brief 모든 문의 움직임이며, 그 문들이 속한 레벨이 소유합니다.
 *
 * 왜 door.c의 전역이 아니라 ::Level의 필드인가. 이전에는 전역이었고, 그 대가는 한 계층
 * 위에서 ::Pools를 추출해 없앤 것과 같았습니다. 진행 중인 두 번째 ::Level이 첫 번째의 문을
 * 공유했으므로, 호출자가 게임의 절반만 들고 있을 수 없다는 것이 존재 이유인 ::World가 정확히
 * 절반만 들고 있었습니다. ::DIAG_DOOR_STALE이 존재하는 이유가 그것입니다. 그 카운터는 자신이
 * 서술하던 레벨보다 오래 살아남은 상태를 잡아내려고 추가되었고, 그것은 상태와 레벨이 분리
 * 가능한 동안에만 일어날 수 있는 일입니다.
 *
 * 왜 ::Pools가 간 ::World가 아니라 ::Level인가. 풀은 한 *플레이*가 생성하는 것이며 어느 한
 * 레벨보다 오래 삽니다. 이것들은 ::Level::doors와 인덱스 대 인덱스로 대응하며 그것 없이는
 * 아무 의미도 없습니다. ::World에 두었다면, 일치해야 하는 두 가지를 호출자가 따로 전달하는 두
 * 구조체에 두는 셈이 됩니다. 전역의 철자만 바꾼 것일 뿐 버그와 같은 형태입니다. 이곳에서는
 * 불일치가 *감지*되는 것이 아니라 *구성 불가능*합니다. ::door_update는 ::Level 하나를 받고,
 * 잘못 짝지을 두 번째 것이 없습니다.
 *
 * @note 이 구조체의 24KB에 대해 약 3.8KB이며, ::Level은 테스트 묶음 곳곳에서 스택 지역
 *       변수입니다. 그것이 의도적으로 감수한 거래입니다. 420KB의 ::BrushMap은 너무 커서
 *       포인터로 남았지만 이것은 그렇지 않고, 브러시와 달리 이것은 이미 이 안에 있는 배열과
 *       일치해야 합니다.
 * @note 전부 0이 유효한 빈 집합입니다(`count`가 0, 모든 `n0`가 0). 따라서 `Level l = {0}`에는
 *       추가 초기화가 필요 없고, 필드를 하나씩 채워 Level을 만드는 픽스처는 문이 움직이지 않는
 *       레벨로 남습니다.
 */
typedef struct {
    DoorState d[LVL_MAX_DOORS];  /**< Matched to ::Level::doors by index. / ::Level::doors와 인덱스로 대응합니다. */

    /**
     * @brief How many slots ::door_reset captured.
     *
     * Not the same number as ::Level::n_doors, and the difference is the point:
     * this says how many states exist, that says how many definitions do, and
     * ::door_update walking to the larger would read a slot nobody filled in.
     *
     * ::door_reset이 포착한 슬롯의 수입니다. ::Level::n_doors와 같은 수가 아니며 그 차이가
     * 요점입니다. 이것은 상태가 몇 개인지를, 그쪽은 정의가 몇 개인지를 말하고, ::door_update가
     * 큰 쪽까지 순회하면 아무도 채우지 않은 슬롯을 읽게 됩니다.
     */
    int   count;

    /**
     * @brief The key a door refused for THIS FRAME, or ::KEY_NONE.
     *
     * Cleared at the top of every ::door_update, which is right for asking "was
     * the player turned away just now" and useless for telling them so.
     * ::notice_key below is the same fact kept long enough to read.
     *
     * *이번 프레임에* 문이 거절한 열쇠, 또는 ::KEY_NONE입니다. 매 ::door_update의 맨 위에서
     * 초기화되며, "방금 거절당했는가"를 묻기에는 맞지만 그것을 알리기에는 쓸모없습니다.
     * 아래의 ::notice_key가 같은 사실을 읽을 수 있을 만큼 오래 붙잡아 둔 것입니다.
     */
    int   refused;

    /* THE REFUSAL HAS TO OUTLIVE THE FRAME IT HAPPENED IN. A message that is
       true for one frame at 60Hz is a message nobody reads. Kept beside the
       door rather than in the HUD because the door is what knows, and a timer
       on the drawing side would be a second copy of an event that already has
       an owner. door_update is also the only place with a dt to count down.
       거절은 그것이 일어난 프레임보다 오래 살아남아야 합니다. 60Hz에서 한 프레임 동안만 참인
       메시지는 아무도 읽지 못합니다. HUD가 아니라 문 곁에 두는 이유는 아는 쪽이 문이기
       때문이며, 그리는 쪽의 타이머는 이미 주인이 있는 사건의 사본이 됩니다. door_update는 셀
       dt를 가진 유일한 곳이기도 합니다. */
    int   notice_key;
    float notice_t;

    /**
     * @brief The union of every door's key, folded once at ::door_reset.
     *
     * Derived rather than stored per door, because the key is the LEVEL's and
     * does not change: copying it into ::DoorState beside `floor0` and `pts0`
     * would look like the same pattern and is not. Those are snapshots of
     * fields ::door_update overwrites, and this one nothing ever writes -- a
     * copy of it would be a second source of truth kept in step by nothing.
     *
     * 모든 문의 열쇠의 합집합이며 ::door_reset에서 한 번 접어 둡니다. 문마다 저장하지 않고
     * 유도하는 이유는 열쇠가 *레벨*의 것이고 변하지 않기 때문입니다. `floor0`나 `pts0` 옆에
     * ::DoorState로 복사하면 같은 패턴처럼 보이지만 아닙니다. 그것들은 ::door_update가 덮어쓰는
     * 필드의 스냅숏이고, 이것은 아무도 쓰지 않습니다. 사본을 두면 무엇으로도 일치가 유지되지
     * 않는 두 번째 진실이 됩니다.
     */
    int   keys;
} DoorSet;

/**
 * @brief How many lamps one level may declare.
 *
 * ENGLISH
 * -------
 * Was 8, and was 8 because ::RD_MAX_LIGHTS was: lighting was point lights
 * evaluated per fragment, so a lamp the shader could not hold was a lamp the
 * level did not have. The static bake ended that, the lamps went back to the
 * shader, and then they were switched off altogether -- see scene.c's note
 * above ::MoveLight for what each of those two attempts looked like and why
 * neither held. A lamp lights NOTHING now, from either place.
 *
 * So this cap no longer answers a question about light at all, and at present
 * it is not answering a question about anything: no shipped level declares a
 * lamp, so the array is empty in every level the game loads. It is how many an
 * author COULD write down, kept because the parser still reads the word and a
 * format that silently drops a word it can read is worse than one that reads a
 * word nothing uses.
 *
 * 64, matching ::LVL_MAX_SECTORS, because the case that raised it is a level
 * where every sector carries its own brightness -- which is what a converted
 * Doom map is. One lamp per sector is the shape that data arrives in.
 *
 * @note What this costs is `.bss` and nothing else -- no ray, no per-frame
 *       scan, no per-fragment slot. The bake charged a trace per vertex per
 *       lamp in range and the frame briefly charged a walk of this array; both
 *       are gone, so the cap is purely about how much zeroed memory a Level
 *       carries.
 * @note Lamps past this are parsed and dropped, and that is reported through
 *       ::DIAG_LIGHT_CAP. A room darker than its author intended gives no hint
 *       that a cap was the cause.
 *
 * 한국어
 * ------
 * @brief 한 레벨이 선언할 수 있는 등의 개수입니다.
 *
 * 8이었고, 8이었던 이유는 ::RD_MAX_LIGHTS가 8이었기 때문입니다. 조명이 프래그먼트마다
 * 평가되는 점광원이었으므로, 셰이더가 담을 수 없는 등은 곧 레벨에 없는 등이었습니다. 정적
 * 베이크가 그것을 끝냈고, 등은 다시 셰이더로 돌아갔다가, 결국 완전히 꺼졌습니다. 그 두 번의
 * 시도가 각각 어떠했고 왜 어느 쪽도 유지되지 않았는지는 scene.c의 ::MoveLight 위 설명을
 * 참조하십시오. 이제 등은 어느 자리에서도 *아무것도* 밝히지 않습니다.
 *
 * 따라서 이 상한은 더 이상 빛에 관한 질문에 답하지 않으며, 현재로서는 어떤 질문에도 답하고
 * 있지 않습니다. 등을 선언하는 출하 레벨이 없으므로 게임이 로드하는 모든 레벨에서 이 배열은
 * 비어 있습니다. 제작자가 몇 개까지 적어 *둘 수 있는가*이며, 남겨 두는 이유는 파서가 여전히
 * 그 단어를 읽기 때문이고, 읽을 수 있는 단어를 조용히 버리는 형식이 아무도 쓰지 않는 단어를
 * 읽는 형식보다 나쁘기 때문입니다.
 *
 * ::LVL_MAX_SECTORS와 같은 64입니다. 이 값을 올리게 만든 사례가 *섹터마다 자기 밝기를 지닌*
 * 레벨이고, 변환된 Doom 맵이 바로 그것이기 때문입니다. 섹터당 등 하나가 그 데이터가 도착하는
 * 형태입니다.
 *
 * @note 이것이 치르는 비용은 `.bss`뿐이며 그 외에는 없습니다. 광선도, 프레임마다의 훑기도,
 *       프래그먼트마다의 슬롯도 없습니다. 베이크는 정점마다 사거리 안의 등마다 판정 하나를
 *       물렸고 프레임은 한동안 이 배열을 훑는 비용을 물었지만 둘 다 사라졌으므로, 이 상한은
 *       순전히 Level이 0으로 채워진 메모리를 얼마나 나르는가에 관한 것입니다.
 * @note 이를 넘는 등은 파싱되되 버려지며 ::DIAG_LIGHT_CAP으로 보고됩니다. 제작자의 의도보다
 *       어두운 방은 상한이 원인이라는 단서를 주지 않기 때문입니다.
 */
#define LVL_MAX_LIGHTS  64

/**
 * @brief How far a sun or sky shadow ray runs, metres.
 *
 * ENGLISH: A directional light has no position, so the ray cannot stop at one.
 * This is "past anything in the level": ::BRUSH_MAX_COORD is 16384 map units
 * either way, and a level cannot be wider than that, so twice it in metres
 * clears any geometry a ray could meet. It is a bound rather than a distance.
 * 한국어: 방향성 광원에는 위치가 없으므로 광선이 어느 점에서도 멈출 수 없습니다. 이것은
 * "레벨의 무엇보다 먼"이라는 뜻입니다. 거리가 아니라 경계입니다.
 */
#define LVL_SUN_REACH 1024.0f

/** @brief How many sky faces one light ray may pass before it gives up. / 광선 하나가 포기하기 전에 지날 수 있는 하늘 면의 수. */
#define LVL_LIGHT_SKY_PASSES 4

/** @brief How far past a sky face a light ray resumes, metres. Small enough not to skip geometry, large enough not to restart inside what it left. / 광선이 하늘 면을 지나 재개하는 거리(미터). */
#define LVL_LIGHT_BIAS 0.02f


/**
 * @brief What one unit of ericw's `_sunlight` is worth to ::bake_light.
 *
 * ENGLISH
 * -------
 * The yardstick this was chosen against was a point lamp: one at the
 * reference power was worth 1.0 of the shader's illumination range, and this
 * put a Quake sun on the same scale. The lamps light nothing now and the
 * yardstick is gone with them, but the number it produced is unchanged and
 * still correct -- it was never a ratio to a lamp, only chosen beside one.
 * `_sunlight 120`
 * -- what `lqdm1` declares, and an ordinary value for an outdoor map -- lands
 * at 0.47, which is a strong sun that does not on its own saturate a surface
 * that also has ambient and a key light on it.
 *
 * @note Deliberately not tuned per map. A number that has to be chosen again
 *       for every level is a number nobody can predict, and the whole reason
 *       to read `_sunlight` at all is that the author already chose one.
 * @note No shipped map declares one now, so nothing multiplies by this. It is
 *       what a `_sunlight` would still be worth, kept alongside the parse that
 *       would still read it.
 *
 * 한국어
 * ------
 * 이 값을 고를 때의 잣대는 점광원이었습니다. 기준 세기의 등 하나가 셰이더 조도 범위에서
 * 1.0의 값어치였고, 이 값이 Quake의 태양을 같은 눈금에 올렸습니다. 이제 등은 아무것도 밝히지
 * 않으므로 잣대도 함께 사라졌지만, 그것이 만들어 낸 수는 그대로이고 여전히 옳습니다. 등에
 * 대한 비율이었던 적은 없고 다만 그 곁에서 골라졌을 뿐입니다. `lqdm1`이 선언하던 `_sunlight
 * 120`은 0.47이 되며, 주변광과 주광이 함께
 * 얹힌 표면을 혼자서 포화시키지는 않는 강한 태양입니다.
 *
 * @note 맵마다 다시 맞추지 않습니다. 레벨마다 다시 골라야 하는 수는 아무도 예측할 수 없는
 *       수이고, 애초에 `_sunlight`를 읽는 이유가 제작자가 이미 골랐다는 데 있습니다.
 */
#define LVL_SUN_SCALE (1.0f / 255.0f)

#define LVL_MAT         16     ///< @brief Maximum length of a material name. / 재질 이름의 최대 길이.

/**
 * @brief Maximum length of an ::Entity::kind, terminator included.
 *
 * ENGLISH
 * -------
 * SEPARATE FROM ::LVL_MAT BECAUSE THE TWO ARE BOUND BY DIFFERENT THINGS. A
 * material name is bound at the far end by ::TEX_NAME_MAX, the key width of
 * tex_mat's cache, and by ::BR_TEX, which is Quake's 15+nul because that is
 * what a .map face stores. weaponview.c and textest both assert that
 * relationship. Raising LVL_MAT to make room for an entity kind would drag
 * the texture cache along behind it to buy space for names that are not
 * textures -- `scratch`, at seven characters, is the longest material this
 * project ships.
 *
 * A kind is bound by something else entirely, and it is NOT the classname.
 * The classname has its family prefix stripped on the way in (see
 * brush_ents_of in level.c), so what has to fit is the REMAINDER -- and for a
 * spawner that remainder is `spawner_` plus a monster name, which enemy.c
 * then strips again:
 *
 *     monster_spawner_water_spirit  ->  spawner_water_spirit  ->  20 + nul
 *     ^^^^^^^^ level.c strips            ^^^^^^^^ enemy.c strips next
 *
 * Sharing ::LVL_MAT gave the kind fifteen characters, which fitted
 * `spawner_caster` with ONE to spare. The first monster whose name ran past
 * seven characters had a working `monster_<name>` and a
 * `monster_spawner_<name>` that truncated; ::mon_type_for compares whole
 * names, found nothing, and the spawner was dropped without a word. That is
 * what ::DIAG_ENT_KIND now says out loud.
 *
 * 32 is not the smallest number that works today (21 is). It is chosen so a
 * monster name may be as long as the FGD makes it look like it may be, and so
 * the next long name is a truncation this file already anticipated rather than
 * one it has to be edited for again. The cost is one name per entity -- about
 * 1KB across a whole ::Level, in .bss, none of it on disk or per frame.
 *
 * @note leveltest asserts the invariant this number exists to hold: that every
 *       monster the engine knows is placeable as `monster_spawner_<name>`. A
 *       name too long fails there, at the table that named it, rather than in
 *       a room that quietly never fills.
 *
 * 한국어
 * ------
 * @brief ::Entity::kind의 최대 길이. 종료 문자를 포함합니다.
 *
 * ::LVL_MAT와 분리하는 이유는 둘을 묶는 것이 서로 다르기 때문입니다. 재질 이름은 반대쪽 끝에서
 * tex_mat 캐시의 키 폭인 ::TEX_NAME_MAX에, 그리고 .map의 면이 저장하는 형식이라 Quake의
 * 15+널인 ::BR_TEX에 묶여 있습니다. weaponview.c와 textest가 그 관계를 단언합니다. 엔티티
 * 종류를 위해 LVL_MAT를 올리는 것은, 텍스처가 아닌 이름들을 위한 자리를 사면서 텍스처 캐시를
 * 뒤에 끌고 가는 일입니다. 이 프로젝트가 출하하는 가장 긴 재질은 일곱 글자 `scratch`입니다.
 *
 * 종류를 묶는 것은 전혀 다른 것이며, 그것은 classname이 *아닙니다*. classname은 들어오는 길에
 * 계열 접두사가 떨어져 나가므로(level.c의 brush_ents_of 참조) 들어가야 하는 것은 *나머지*이고,
 * 스포너의 경우 그 나머지는 `spawner_`에 몬스터 이름을 더한 것이며 enemy.c가 그것을 다시
 * 뗍니다.
 *
 *     monster_spawner_water_spirit  ->  spawner_water_spirit  ->  20 + 널
 *     ^^^^^^^^ level.c가 뗍니다          ^^^^^^^^ enemy.c가 다음으로 뗍니다
 *
 * ::LVL_MAT를 함께 쓰면 종류에 열다섯 글자가 주어졌고, 그것은 `spawner_caster`를 *한 글자*
 * 남기고 담았습니다. 이름이 일곱 글자를 넘는 첫 몬스터는 `monster_<name>`은 동작하는데
 * `monster_spawner_<name>`은 잘렸습니다. ::mon_type_for는 이름 전체를 비교하므로 아무것도 찾지
 * 못했고, 스포너는 한마디도 없이 버려졌습니다. 그것을 이제 ::DIAG_ENT_KIND가 소리 내어
 * 말합니다.
 *
 * 32는 오늘 동작하는 가장 작은 수가 아닙니다(그것은 21입니다). FGD가 그래도 될 것처럼 보이게
 * 하는 만큼 몬스터 이름이 길어도 되도록, 그리고 다음번 긴 이름이 이 파일을 또 고쳐야 하는
 * 절단이 아니라 이 파일이 이미 내다본 절단이 되도록 고른 값입니다. 비용은 엔티티마다 이름
 * 하나이며 ::Level 전체로 약 1KB, .bss에 있고 디스크에도 프레임에도 없습니다.
 *
 * @note leveltest가 이 숫자의 존재 이유인 불변식을 단언합니다. 엔진이 아는 모든 몬스터는
 *       `monster_spawner_<name>`으로 배치될 수 있어야 한다는 것입니다. 너무 긴 이름은 조용히
 *       채워지지 않는 방이 아니라 그것을 이름 지은 표에서 실패합니다.
 */
#define LVL_KIND        32

/**
 * @brief Maximum material runs a level's geometry can produce.
 *
 * ENGLISH
 * -------
 * @note Levels need their own range bound: ::MDL_MAX_RANGES is sized for a
 *       weapon's handful of parts, and a level reaches every material its
 *       author used. All of this lives in .bss, which costs nothing on disk.
 * @note The number is derived rather than sampled -- see the derivation below.
 *
 * 한국어
 * ------
 * @brief 레벨 지오메트리가 생성할 수 있는 최대 재질 구간 수입니다.
 * @note 레벨에는 자체적인 구간 한계가 필요합니다. ::MDL_MAX_RANGES는 무기의 몇 안
 *       되는 부품을 기준으로 정해진 값인 반면, 레벨은 제작자가 쓴 모든 재질에
 *       도달합니다. 이 데이터는 모두 .bss에 위치하므로 디스크 용량을 차지하지
 *       않습니다.
 * @note 이 수는 표본이 아니라 유도된 것입니다. 아래의 유도를 참조하십시오.
 */
/* WHAT A RUN IS CHANGED UNDER THIS CAP, so the number had to be derived again
 * from what a run now means.
 *
 * THE OLD MEANING. `LVL_MAX_SECTORS * 3` is exact reasoning for the loader it
 * was written for: a sector has a floor, a ceiling and walls, so three runs per
 * sector bounds it. A BRUSH level has no sectors. Its run count was the number
 * of times the material CHANGED as ::brush_geometry walked the brush list,
 * which is a fact about the order the author happened to build in and was
 * bounded by nothing in this header.
 *
 * WHAT THAT COST, measured by tools/mapcap.c: both imported arenas were already
 * over. lqdm11 wanted 338 runs and lqdm13 262, against a cap of 192 -- so 734
 * and 428 runs were MERGED into their neighbours, and brush.c merges rather
 * than drops on purpose ("the surplus draws with the wrong texture, which is
 * visible; dropping it would delete the wall, which is not"). Both maps shipped
 * with surfaces drawing the wrong material and nothing above the diagnostic
 * counter said so.
 *
 * THE NEW MEANING. ::brush_geometry emits one material at a time rather than
 * one brush at a time, so a call's runs are the DISTINCT materials among the
 * brushes it was handed, not the changes between them. The same two maps now
 * want 4 and 6. Measured through the path the game actually builds by:
 *
 *     spire       9        atrium      8   (splits)
 *     glasstower  4        lqdm13      6   (splits)
 *     lqdm11      4
 *
 * WHY IT IS A FORMULA AGAIN, and this time of the right thing.
 * ::scene_build_level splits a level with a brush door into a static half and a
 * moving one, and ::level_geometry_part calls ::brush_geometry once per
 * contiguous STRETCH of brushes on its side of that split -- grouping cannot
 * cross a call. The demand is therefore (stretches x materials per stretch),
 * and both factors are bounded: ::LVL_MAX_DOORS doors cut the brush list into
 * at most `2 * LVL_MAX_DOORS + 1` stretches, and the materials are bounded by
 * the palette an author picks from -- assets/textures.txt defines 30, and
 * tools/mapedit.c offers MAX_MATS (32) of them. So 32 is the editor's palette,
 * and the product is every stretch of a level using every material in it.
 *
 * THE ASSUMPTION IS THE MATERIAL LIBRARY, which is worth saying out loud
 * because a map may name whatever it likes. An unknown name still gets its own
 * run -- `spire` carries two, see the truncation note in tools/mapcap.c -- so a
 * map inventing forty names per stretch overflows this however it is sized.
 * ::DIAG_MAT_RANGES counts exactly that and mapcap fails on it, which is the
 * difference between a cap that CAN be exceeded and one that IS exceeded
 * quietly.
 *
 * WHAT IT COSTS: 60 bytes a run in ::Scene -- a ::MdlRange and a ::Mat -- so
 * 62KB of .bss, free on disk and the cheapest thing in the machine. The measured
 * demand is nine. This cap is not sized to the maps that exist. It is sized so
 * that no map the other caps admit can reach it.
 *
 * *이 상한 아래에서 구간이 무엇인지가 바뀌었으므로*, 수를 구간의 새로운 의미로부터 다시
 * 유도해야 했습니다.
 *
 * *옛 의미.* `LVL_MAX_SECTORS * 3`은 그것이 쓰인 로더에 대해서는 정확한 추론입니다. 섹터에는
 * 바닥과 천장과 벽이 있으므로 섹터당 세 구간이 그것을 한정합니다. *브러시* 레벨에는 섹터가
 * 없습니다. 그 구간 수는 ::brush_geometry가 브러시 목록을 훑는 동안 재질이 *바뀌는*
 * 횟수였으며, 그것은 제작자가 마침 어떤 순서로 만들었는가에 대한 사실이고 이 헤더의 무엇도
 * 그것을 한정하지 않았습니다.
 *
 * *그것이 치른 대가*, tools/mapcap.c의 측정: 가져온 두 아레나가 이미 초과 상태였습니다.
 * lqdm11은 338구간, lqdm13은 262구간을 원했는데 상한은 192였습니다. 그래서 734건과 428건이
 * 이웃으로 *병합*되었고, brush.c는 의도적으로 버리지 않고 병합합니다("초과분은 잘못된
 * 텍스처로 그려지며 그것은 눈에 보입니다. 버리면 벽이 사라지고 그것은 보이지 않습니다").
 * 두 맵 모두 일부 면이 틀린 재질로 그려지는 채로 출하되었고, 진단 카운터 위의 어느 것도
 * 그렇다고 말하지 않았습니다.
 *
 * *새 의미.* ::brush_geometry는 브러시를 하나씩이 아니라 재질을 하나씩 내보내므로, 한 호출의
 * 구간 수는 건네받은 브러시들 안의 *서로 다른* 재질의 수이지 그것들 사이의 변화 횟수가
 * 아닙니다. 같은 두 맵이 이제 4와 6을 원합니다. 게임이 실제로 생성하는 경로로 잰 값:
 *
 *     spire       9        atrium      8   (분할)
 *     glasstower  4        lqdm13      6   (분할)
 *     lqdm11      4
 *
 * *왜 다시 공식인가*, 그리고 이번에는 옳은 것에 대한 공식인가. ::scene_build_level은 브러시
 * 문이 있는 레벨을 정적인 절반과 움직이는 절반으로 나누고, ::level_geometry_part는 자기 쪽
 * 브러시의 *연속된 덩어리*마다 ::brush_geometry를 한 번씩 부릅니다. 묶기는 호출을 넘어가지
 * 못합니다. 따라서 요구량은 (덩어리 수 x 덩어리당 재질 수)이며, 두 인수 모두 한정됩니다.
 * ::LVL_MAX_DOORS개의 문은 브러시 목록을 많아야 `2 * LVL_MAX_DOORS + 1`개의 덩어리로 자르고,
 * 재질 쪽은 제작자가 고르는 팔레트가 한정합니다. assets/textures.txt는 30개를 정의하고
 * tools/mapedit.c는 그중 MAX_MATS(32)개를 제시합니다. 그러므로 32는 에디터의 팔레트이며,
 * 그 곱은 레벨의 모든 덩어리가 그 안의 모든 재질을 쓰는 경우입니다.
 *
 * *가정은 재질 라이브러리이며*, 맵은 무엇이든 이름 댈 수 있으므로 소리 내어 말해 둘 값어치가
 * 있습니다. 알 수 없는 이름도 자기 구간을 갖습니다. `spire`가 둘을 지니고 있으며
 * tools/mapcap.c의 잘림 각주를 보십시오. 그러므로 덩어리마다 이름 마흔 개를 지어내는 맵은
 * 크기를 어떻게 잡든 이것을 넘칩니다. ::DIAG_MAT_RANGES가 바로 그것을 세고 mapcap이 그것으로
 * 실패하며, 그것이 넘길 *수 있는* 상한과 조용히 넘겨진 상한의 차이입니다.
 *
 * *드는 비용:* ::Scene에서 구간당 60바이트(::MdlRange 하나와 ::Mat 하나)이므로 .bss 62KB이며,
 * 디스크에서 공짜이고 기계에서 가장 싼 것입니다. 측정된 요구량은 아홉입니다. 이 상한은 존재하는
 * 맵에 맞춰 잡은 것이 아닙니다. 다른 상한들이 허용하는 어떤 맵도 이것에 닿을 수 없도록 잡은
 * 것입니다.
 */
#define LVL_MAX_RANGES ((2 * LVL_MAX_DOORS + 1) * 32)

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

    /**
     * @brief How far this sector's moved surface has travelled, in file units.
     *
     * ENGLISH
     * -------
     * A TEXTURING FACT, not a geometric one. The geometry is already in
     * ::Sector::floor and ::Sector::ceil and everything that collides reads it
     * there without knowing a door exists. What those two numbers cannot say is
     * where a surface came FROM, and a wall's texture needs that: `v` is
     * anchored to world height, so a door whose ceiling rises leaves its
     * texture pinned in space while the quad's bottom edge eats upward into it.
     * The leaf reads as being erased from below rather than as rising.
     *
     * Written by ::door_update, read only by the wall builder. Zero for
     * everything that has not moved, which is every sector of a level with no
     * doors and every sector of one that has not been stepped yet.
     *
     * @note Signed. Positive is the CEILING having risen, negative is the FLOOR
     *       having sunk, and which one it is decides which of the sector's walls
     *       the offset belongs to -- see ::add_wall. A SLIDING door needs
     *       nothing here: `u` is measured from the edge's own start vertex,
     *       which travels with the wall, so its texture already follows.
     *
     * 한국어
     * ------
     * @brief 이 섹터의 움직인 면이 이동한 거리(파일 단위).
     *
     * *텍스처링에 관한 사실*이며 기하에 관한 것이 아닙니다. 기하는 이미 ::Sector::floor와
     * ::Sector::ceil에 있고 충돌하는 모든 것이 문의 존재를 모른 채 그것을 읽습니다. 그 두
     * 숫자가 말할 수 없는 것은 어떤 면이 *어디에서 왔는가*이며, 벽의 텍스처는 그것을
     * 필요로 합니다. `v`가 월드 높이에 고정되어 있으므로, 천장이 올라가는 문은 텍스처를
     * 공간에 박아 둔 채 사각형의 아래 모서리가 위로 파고들게 만듭니다. 문짝이 올라가는 것이
     * 아니라 아래에서 지워지는 것으로 읽힙니다.
     *
     * ::door_update가 기록하고 벽 생성기만 읽습니다. 움직이지 않은 모든 것에 대해 0이며,
     * 문이 없는 레벨의 모든 섹터와 아직 진행되지 않은 레벨의 모든 섹터가 그렇습니다.
     *
     * @note 부호가 있습니다. 양수는 *천장*이 올라간 것, 음수는 *바닥*이 내려간 것이며, 어느
     *       쪽인지가 그 오프셋이 섹터의 어느 벽에 속하는지를 결정합니다. ::add_wall을
     *       참조하십시오. *미닫이* 문에는 이곳의 값이 필요 없습니다. `u`는 모서리 자신의 시작
     *       정점에서부터 재는데 그 정점이 벽과 함께 이동하므로 텍스처가 이미 따라갑니다.
     */
    short uv_y;
} Sector;

#define LVL_MAX_TRIGGERS 16   ///< @brief Trigger volumes per level. / 레벨당 트리거 부피 수.

/**
 * @struct TriggerDef
 * @brief A volume that fires a tag while something stands in it.
 *
 * ENGLISH
 * -------
 * The brush model's answer to the sector model's `switch<n>` marker, and the
 * difference is the shape: a switch is a point with a radius around it, and a
 * trigger is the space an author drew. A round pad in a corridor and the whole
 * end of a room are the same entity here and cannot be the same one there.
 *
 * ITS BRUSHES ARE NOT SOLID -- see ::Brush::solid. That is the point: a volume
 * the player walks into is one the player must be able to walk into.
 *
 * @note `tag` is a small integer, but nobody types one. The .map links by name
 *       -- a `trigger_*` names its `target` and a ::DoorDef its `targetname` --
 *       and ::level_load interns those strings to numbers in the order it meets
 *       them. Numbers are what the door state machine already compares; names
 *       are what an editor and every Quake tutorial use.
 *
 * 한국어
 * ------
 * @brief 무언가가 그 안에 서 있는 동안 태그를 발동시키는 부피입니다.
 *
 * 섹터 모델의 `switch<n>` 표식에 대한 브러시 모델의 답이며, 차이는 형태입니다. 스위치는 점과
 * 그 둘레의 반경이고, 트리거는 제작자가 그린 공간입니다. 복도의 둥근 발판과 방의 한쪽 끝 전체가
 * 이곳에서는 같은 엔티티이며 그곳에서는 같은 것일 수 없습니다.
 *
 * 그 브러시는 고체가 아닙니다. ::Brush::solid를 참조하십시오. 그것이 요점입니다. 플레이어가
 * 걸어 들어가는 부피는 플레이어가 걸어 들어갈 수 있어야 하는 부피입니다.
 *
 * @note `tag`는 작은 정수이지만 아무도 그것을 입력하지 않습니다. .map은 이름으로 연결합니다.
 *       `trigger_*`가 자신의 `target`을, ::DoorDef가 자신의 `targetname`을 지목하며,
 *       ::level_load가 그 문자열을 마주친 순서대로 숫자로 사상합니다. 숫자는 문 상태 기계가
 *       이미 비교하는 것이고, 이름은 에디터와 모든 Quake 강좌가 쓰는 것입니다.
 */
typedef struct {
    short first_brush, n_brushes;  /**< The volume, in ::BrushMap::brushes. / ::BrushMap::brushes 안의 부피. */
    short tag;                     /**< What it fires; matches ::DoorDef::tag. / 발동시키는 대상. ::DoorDef::tag와 대응합니다. */
} TriggerDef;

#define LVL_MAX_HAZARDS 8     ///< @brief Hurting volumes per level. / 레벨당 피해 부피 수.

/**
 * @def LVL_HURT_DEFAULT
 * @brief Damage per second for a `trigger_hurt` that names no `dmg`.
 *
 * Quake's own default, and the reason to keep it is that an author who drew a
 * lava pit and typed nothing should get lava rather than a decorative pool.
 * The sector model has the same rule for `hurt` with no number.
 *
 * `dmg`를 적지 않은 `trigger_hurt`의 초당 피해량입니다. Quake의 기본값이며, 유지하는 이유는
 * 용암 웅덩이를 그려 놓고 아무것도 입력하지 않은 제작자가 장식용 웅덩이가 아니라 용암을 얻어야
 * 하기 때문입니다.
 */
#define LVL_HURT_DEFAULT 5

/**
 * @struct HazardDef
 * @brief A volume that burns whatever stands in it.
 *
 * ENGLISH
 * -------
 * The brush model's answer to ::Sector::hurt, and as with ::TriggerDef the
 * difference is the shape -- but here it is also the DIMENSION. A sector's
 * hazard is a property of a floor, so the question "is this point hurting?" had
 * only x and z to work with and the height was implied. A volume has a top and
 * a bottom, so ::level_hazard_at takes a y, and lava you can stand beside
 * without burning is expressible for the first time.
 *
 * ITS BRUSHES ARE NOT SOLID, for the same reason a trigger's are not: a pit you
 * fall into is not a pit if it holds you up. Author them with a liquid or lava
 * material and let the surface be the surface.
 *
 * @note There is no "last declared wins" here, and none is needed. The sector
 *       model needed that rule because a safe dais had to be drawn as a second
 *       sector OVER the lava; a brush author draws the dais as a solid brush
 *       inside the pit, and the lava volume simply does not contain the points
 *       the dais occupies -- or rather it does, and the player standing on the
 *       dais is above it. Geometry answers what a precedence rule had to.
 *
 * 한국어
 * ------
 * @brief 그 안에 서 있는 것을 태우는 부피입니다.
 *
 * ::Sector::hurt에 대한 브러시 모델의 답이며, ::TriggerDef와 마찬가지로 차이는 형태입니다.
 * 다만 이곳에서는 *차원*이기도 합니다. 섹터의 위험 지형은 바닥의 속성이므로 "이 지점이
 * 아픈가"라는 질문에는 x와 z만 있었고 높이는 암묵적이었습니다. 부피에는 위와 아래가 있으므로
 * ::level_hazard_at은 y를 받으며, 타지 않고 옆에 설 수 있는 용암을 처음으로 표현할 수
 * 있습니다.
 *
 * 그 브러시는 고체가 아니며, 트리거의 것이 아닌 이유와 같습니다. 빠지는 구덩이가 떠받쳐 준다면
 * 그것은 구덩이가 아닙니다. 액체나 용암 재질로 제작하고 표면이 표면이도록 두십시오.
 *
 * @note 이곳에는 "마지막 선언 우선"이 없고, 필요하지도 않습니다. 섹터 모델이 그 규칙을 필요로
 *       했던 것은 안전한 단상을 용암 *위에* 덮는 두 번째 섹터로 그려야 했기 때문입니다. 브러시
 *       제작자는 단상을 구덩이 안의 고체 브러시로 그리고, 용암 부피는 단상이 차지한 지점을
 *       포함하지 않습니다. 더 정확히는 포함하더라도, 단상 위에 선 플레이어가 그 위에 있습니다.
 *       우선순위 규칙이 해야 했던 답을 지오메트리가 합니다.
 */
typedef struct {
    short first_brush, n_brushes;  /**< The volume, in ::BrushMap::brushes. / ::BrushMap::brushes 안의 부피. */
    short dps;                     /**< Damage per second while inside. / 안에 있는 동안의 초당 피해량. */
} HazardDef;

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
    char  kind[LVL_KIND];          /**< "spawn", "ammo", ... / "spawn", "ammo" 등. */
    short x, z;                   /**< Position in 1/100 units. / 위치 (1/100 단위). */

    /**
     * @brief Where to start looking DOWN from for the floor. File units.
     *
     * ENGLISH
     * -------
     * Not where the entity is -- where the search for its footing begins.
     * Everything placed by an entity settles onto the floor beneath the marker,
     * so what the marker needs to supply is which floor that is.
     *
     * A sector level has one per point on the plan, so this stays 0 and the
     * consumers pass it with an unlimited step, which is the same answer they
     * got when they passed 1000 to mean "no limit". A brush level has storeys,
     * and a search that begins above the roof finds the outside of the roof --
     * so for those it carries the height the `origin` had, which is a thing the
     * .map always knew.
     *
     * @note Zero for a sector level, where it changes nothing.
     *
     * 한국어
     * ------
     * @brief 바닥을 찾기 위해 *아래로* 훑기 시작할 높이입니다. 파일 단위.
     *
     * 엔티티가 있는 곳이 아니라, 그것이 딛고 설 자리를 찾는 탐색이 시작되는 곳입니다. 엔티티가
     * 배치하는 모든 것은 표식 아래의 바닥에 안착하므로, 표식이 제공해야 하는 것은 *그 바닥이
     * 어느 것인가*입니다.
     *
     * 섹터 레벨은 평면상의 한 점에 바닥이 하나이므로 이 값은 0으로 남고, 소비자는 제한 없는
     * 단차와 함께 그것을 넘깁니다. "제한 없음"을 뜻하려고 1000을 넘겼을 때와 같은 답입니다.
     * 브러시 레벨에는 층이 있고, 지붕 위에서 시작한 탐색은 지붕의 바깥면을 찾습니다. 그래서
     * 그쪽에서는 `origin`이 지녔던 높이를 나릅니다. .map이 언제나 알고 있던 값입니다.
     *
     * @note 섹터 레벨에서는 0이며 아무것도 바꾸지 않습니다.
     */
    short y;

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
 * @note NOT APPLIED, AND NOT DECLARED EITHER. A lamp was tried in the
 *       vertices and in the shader's point-light loop, neither looked right on
 *       a brush level, and the room is lit by the shader's ambient plus what
 *       is in the air instead -- so the lamps were switched off, and then
 *       removed from the maps. Nothing reads this array and no shipped level
 *       fills it. scene.c's note above ::MoveLight is where that decision and
 *       both measurements live. The fields below still mean what they say;
 *       there is simply nothing in them.
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
 * @note *적용되지 않으며, 선언되지도 않습니다.* 등은 정점에서도, 셰이더의 점광원
 *       반복문에서도 시도되었고 브러시 레벨에서는 어느 쪽도 옳아 보이지 않았으며, 대신 방은
 *       셰이더의 주변광과 공중에 있는 것들로 밝혀집니다. 그래서 등은 꺼졌고, 그다음 맵에서
 *       제거되었습니다. 이 배열을 읽는 것도 없고 그것을 채우는 출하 레벨도 없습니다. 그
 *       결정과 두 번의 측정은 scene.c의 ::MoveLight 위 설명에 있습니다. 아래의 필드들은
 *       여전히 적힌 그대로를 뜻하며, 다만 그 안에 아무것도 없을 뿐입니다.
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

    /**
     * @brief The direction the sun is IN, unit length, or all zero for none.
     *
     * ENGLISH
     * -------
     * WHAT AN IMPORTED MAP TURNED OUT TO BE LIT BY, AND NO LONGER CARRIES.
     * `lqdm1`'s worldspawn HAD `_sun_mangle "136 -73 0"`, `_sunlight "120"` and
     * `_sunlight2 "50"` -- a directional sun and a sky dome, which is how
     * ericw-tools lights an outdoor Quake level. Those three keys were taken
     * out of it, so this field is all zero in every level the game loads and
     * ::bake_light returns on its first line; what follows is why it mattered
     * while it was there. Its thirty-two point lamps were ACCENTS, and
     * measured by tools/lightprobe.c they were exactly that:
     * of 798,624 vertex-light pairs, 93.3% fail on distance alone and 0.5%
     * light anything. The room is 82 x 63 x 43 metres and the lamps reach 9 to
     * 14, so importing only the lamps imported the garnish and left the meal.
     *
     * WITHOUT IT EVERY FACE IS ONE FLAT TONE. The shader's key light is a
     * constant direction, so `dot(n, key)` does not vary across a face; with
     * no baked light either, a wall is one value from corner to corner and its
     * neighbour is another, which reads as light that bleeds along one side of
     * a mesh and stops dead at a seam. That is the reported defect, and it is
     * not a shading bug -- it is a room with almost no light in it.
     *
     * @note Points TOWARD the sun, which is the opposite of the mangle in the
     *       file: `_sun_mangle` names the direction light TRAVELS. Stored the
     *       way the bake wants to use it, so the negation happens once at
     *       parse rather than at every vertex.
     * @note Zero for a level that declares none, and ::bake_light skips the
     *       whole term on zero. Every hand-authored level in this project is
     *       such a level, so this is purely additive: nothing that looked
     *       right before changes.
     *
     * 한국어
     * ------
     * @brief 태양이 있는 방향의 단위 벡터. 태양이 없으면 전부 0입니다.
     *
     * *가져온 맵이 무엇으로 조명되고 있었는지, 그리고 이제는 무엇을 지니지 않는지.* `lqdm1`의
     * worldspawn은 `_sun_mangle`, `_sunlight`, `_sunlight2`를 지니고 *있었습니다*. 방향성
     * 태양과 하늘 돔이며, ericw-tools가 야외 Quake 레벨을 조명하는 방식입니다. 그 세 키는
     * 그 맵에서 제거되었으므로 이 필드는 게임이 로드하는 모든 레벨에서 전부 0이고
     * ::bake_light는 첫 줄에서 반환합니다. 아래는 그것이 있던 동안 왜 중요했는지에 대한
     * 설명입니다. 그 맵의 점광원 서른둘은 *장식*이었고, tools/lightprobe.c로 재어 보면 정확히
     * 그러했습니다. 정점-광원 쌍 798,624개 중 93.3%가 거리에서만 걸러지고 0.5%만
     * 무언가를 밝힙니다. 방은 82 x 63 x 43미터이고 램프는 9~14미터를 미치므로, 램프만 가져온
     * 것은 곁들임만 가져오고 본식을 두고 온 것입니다.
     *
     * *그것이 없으면 모든 면이 하나의 평평한 톤입니다.* 셰이더의 주광은 고정된 방향이므로
     * `dot(n, key)`는 면 안에서 변하지 않습니다. 구워진 빛도 없으면 벽은 모서리에서 모서리까지
     * 한 값이고 이웃 벽은 다른 한 값이며, 그것이 빛이 메쉬의 한쪽을 따라 번지다 이음매에서 뚝
     * 끊기는 것으로 읽힙니다. 그것이 보고된 결함이며, 셰이딩 버그가 아니라 *빛이 거의 없는 방*
     * 입니다.
     */
    float   sun[3];

    /**
     * @brief Sun brightness, and the sky dome's, as `_sunlight` / `_sunlight2`.
     *
     * ENGLISH: Kept in the file's own numbers and scaled at the bake, so the
     * one place that decides what a Quake brightness is worth here is the one
     * place that uses it. 0 disables each independently -- a map may declare a
     * sun and no sky. `lqdm1` declared both and declares neither now, so both
     * are 0 in every level the game loads.
     * 한국어: 파일 자신의 수 그대로 두고 베이크에서 환산합니다. Quake의 밝기가 이곳에서
     * 얼마인지를 정하는 곳이 그것을 쓰는 곳 하나가 되게 하기 위함입니다. 각각 0이면 꺼집니다.
     */
    short   sun_power, sky_power;
    DoorDef doors[LVL_MAX_DOORS];         /**< Moving sectors, as authored. / 제작된 그대로의 이동 섹터. */
    int     n_doors;                      /**< Number of doors in use. / 사용 중인 문의 수. */

    /**
     * @brief Where those doors have got to. Written only by door.c.
     *
     * ENGLISH: The runtime half of `doors` above, in the same struct so the two
     * cannot be separated and therefore cannot disagree. ::level_load clears it,
     * ::door_reset fills it, ::door_update advances it. Nothing in level.c ever
     * reads a field of it. See ::DoorSet.
     *
     * 한국어: 위 `doors`의 실행 시점 절반이며, 둘이 분리될 수 없고 따라서 어긋날 수 없도록
     * 같은 구조체에 둡니다. ::level_load가 비우고, ::door_reset이 채우고, ::door_update가
     * 진행시킵니다. level.c의 무엇도 이 필드를 읽지 않습니다. ::DoorSet을 참조하십시오.
     */
    DoorSet door_run;
    TriggerDef triggers[LVL_MAX_TRIGGERS];/**< Volumes that fire tags. / 태그를 발동시키는 부피. */
    int     n_triggers;                   /**< Number of triggers in use. / 사용 중인 트리거의 수. */
    HazardDef hazards[LVL_MAX_HAZARDS];   /**< Volumes that burn. / 태우는 부피. */
    int     n_hazards;                    /**< Number of hazards in use. / 사용 중인 위험 부피의 수. */
    short   start[3];                     /**< x, z, and yaw in millidegrees. / x, z 좌표와 밀리도 단위의 yaw. */

    /**
     * @brief The spawn's HEIGHT, in file units. Meaningful only for a brush level.
     *
     * ENGLISH
     * -------
     * ::start has no y because a sector level does not need one: a plan point
     * has exactly one floor, so the marker's own height is irrelevant and
     * ::player_spawn says so by asking for ground from 1e9 with no step limit.
     *
     * A brush level has storeys. The floor under a point depends on which one
     * you are standing on, and a search that begins above the roof finds the
     * outside of the roof. So the spawn carries the height its `origin` had --
     * which is a thing the .map always knew and the sector format had no way to
     * write down.
     *
     * @note 0 for a sector level, where nothing reads it.
     *
     * 한국어
     * ------
     * @brief 스폰 지점의 *높이*이며 파일 단위입니다. 브러시 레벨에서만 의미가 있습니다.
     *
     * ::start에 y가 없는 이유는 섹터 레벨에 그것이 필요 없기 때문입니다. 평면상의 한 점에
     * 바닥이 정확히 하나이므로 표식 자신의 높이는 무의미하며, ::player_spawn은 단차 제한 없이
     * 1e9에서 지면을 물어 그 사실을 말합니다.
     *
     * 브러시 레벨에는 층이 있습니다. 한 점 아래의 바닥은 어느 층에 서 있는지에 달려 있고,
     * 지붕 위에서 시작한 탐색은 지붕의 바깥면을 찾습니다. 그래서 스폰은 자기 `origin`이 지녔던
     * 높이를 함께 지닙니다. .map은 언제나 알고 있었고 섹터 형식에는 적어 둘 방법이 없던
     * 값입니다.
     *
     * @note 섹터 레벨에서는 0이며 아무도 읽지 않습니다.
     */
    short   start_h;

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

    /**
     * @brief The brushes this level is made of, or NULL when it is sectors.
     *
     * ENGLISH
     * -------
     * THE ONE FIELD THAT DECIDES WHICH MODEL ANSWERS. ::level_ground,
     * ::level_trace, ::level_blocked, ::level_geometry and ::level_hazard_at
     * all branch on it: non-NULL and they ask the brushes, NULL and they walk
     * the sectors exactly as they always did. ::level_exit_at and
     * ::level_push_at need no branch, because they read ::Level::ents and both
     * models fill that.
     *
     * THAT IS NOW EVERY QUESTION THE RUNNING GAME ASKS. ::level_sector_at,
     * ::level_edge_normal and ::level_edge_spans have no brush answer and need
     * none: outside level.c's own sector geometry, their only callers are the
     * sector editor in tools/mapedit.c and the tests. Nothing on the frame path
     * reaches them.
     *
     * That is what lets a .map be used without a converter AND without
     * rewriting the eight modules that hold a `const Level *` -- player, enemy,
     * pickup, proj, weapon, hook, world and scene. None of them can tell, and
     * none of them had to change. The sector levels in assets/levels.txt go on
     * working, and so does every test built on them, which matters because
     * those tests are the only safety net the brush path has until it grows its
     * own.
     *
     * A POINTER, and it has to be. ::BrushMap is 420KB against this struct's
     * 24KB, and Levels are stack locals all over the test suite -- world.c
     * keeps one to walk the level chain, steptest.c has twenty. Embedding it
     * overflows the stack before anything runs. The storage is a ::BrushStore,
     * which a caller owns -- ::level_load puts one in a default store and
     * ::level_load_in takes the caller's; running out of a store's slots is
     * reported through ::DIAG_LEVEL_SLOTS.
     *
     * @note ZERO MEANS SECTORS, which is what `Level l = {0}` gives. Every
     *       fixture that builds a Level field by field therefore stays a sector
     *       level without knowing this field exists -- the same contract
     *       ::Sector::has_bounds and ::SectorGrid::built already keep.
     * @warning Not owned, and WRITTEN THROUGH by exactly one thing: a moving
     *          door. ::brush_translate slides the brushes a `func_door` owns,
     *          which is what makes the leaf solid where it is and not where it
     *          was. Everything else that touches this pointer only reads.
     * @warning Copying a Level copies the pointer, so two Levels share one map.
     *          That is safe for reading and is what the level-chain scan in
     *          world.c does; it is NOT safe to run doors on both, because they
     *          would each translate the same brushes. Nor is it safe to
     *          ::level_load into both, because the copy carries ::brush_key and
     *          would be granted the original's slot. Nothing does either, and
     *          nothing should start.
     *
     * 한국어
     * ------
     * @brief 이 레벨을 이루는 브러시이며, 섹터 레벨이면 NULL입니다.
     *
     * 어느 모델이 답할지를 결정하는 단 하나의 필드입니다. ::level_ground, ::level_trace,
     * ::level_blocked, ::level_geometry, ::level_hazard_at이 모두 이것으로 분기합니다. NULL이
     * 아니면 브러시에 묻고, NULL이면 늘 그랬듯 섹터를 순회합니다. ::level_exit_at과
     * ::level_push_at은 분기가 필요 없습니다. ::Level::ents를 읽고 두 모델 모두 그것을 채우기
     * 때문입니다.
     *
     * 이것이 이제 실행 중인 게임이 던지는 질문의 전부입니다. ::level_sector_at,
     * ::level_edge_normal, ::level_edge_spans에는 브러시 쪽 답이 없고 필요하지도 않습니다.
     * level.c 자신의 섹터 지오메트리 바깥에서 그것들의 호출자는 tools/mapedit.c의 섹터 에디터와
     * 테스트뿐입니다. 프레임 경로의 무엇도 그것들에 닿지 않습니다.
     *
     * 이것이 변환기 없이, 그리고 `const Level *`를 들고 있는 여덟 모듈(player, enemy, pickup,
     * proj, weapon, hook, world, scene)을 다시 쓰지 않고도 .map을 쓸 수 있게 하는 장치입니다.
     * 그들 중 누구도 구별할 수 없고, 누구도 바뀌지 않았습니다. assets/levels.txt의 섹터 레벨은
     * 계속 동작하며 그 위에 세워진 모든 테스트도 그렇습니다. 그 테스트들이 브러시 경로가 자기
     * 그물을 갖출 때까지 가진 유일한 안전망이므로 중요합니다.
     *
     * 포인터이며 그럴 수밖에 없습니다. ::BrushMap은 이 구조체의 24KB에 대해 420KB이고, Level은
     * 테스트 묶음 곳곳에서 스택 지역 변수입니다. world.c는 레벨 사슬을 걷기 위해 하나를 두고,
     * steptest.c에는 스무 개가 있습니다. 값으로 넣으면 무엇이 실행되기도 전에 스택이 넘칩니다.
     * 저장 공간은 호출자가 소유하는 ::BrushStore입니다. ::level_load는 기본 저장소에 넣고
     * ::level_load_in은 호출자의 것을 받으며, 한 저장소의 슬롯 고갈은 ::DIAG_LEVEL_SLOTS로
     * 보고됩니다.
     *
     * @note 0이면 섹터이며, `Level l = {0}`이 주는 값입니다. 따라서 필드를 하나씩 채워 Level을
     *       만드는 모든 픽스처는 이 필드의 존재를 모른 채 섹터 레벨로 남습니다.
     *       ::Sector::has_bounds와 ::SectorGrid::built가 이미 지키는 것과 같은 계약입니다.
     * @warning 소유하지 않으며, 이것을 통해 *쓰는* 것은 정확히 하나입니다. 움직이는 문입니다.
     *          ::brush_translate가 `func_door`가 소유한 브러시를 미끄러뜨리며, 그것이 문짝을
     *          지금 있는 곳에서 고체이고 있던 곳에서 아니게 만듭니다. 이 포인터에 닿는 그 밖의
     *          모든 것은 읽기만 합니다.
     * @warning Level을 복사하면 포인터가 복사되어 두 Level이 하나의 맵을 공유합니다. 읽기에는
     *          안전하며 world.c의 레벨 사슬 스캔이 그렇게 합니다. 그러나 양쪽에서 문을 돌리는
     *          것은 안전하지 *않습니다*. 각자 같은 브러시를 옮기게 됩니다. 그렇게 하는 것은
     *          없으며, 시작해서도 안 됩니다.
     */
    BrushMap *brushes;

    /**
     * @brief Which brush slot this level was granted, as a serial. 0 means none.
     *
     * ENGLISH
     * -------
     * THE SLOT POOL USED TO BE KEYED BY THIS LEVEL'S ADDRESS, and that was the
     * defect. `level.c` held a `Level *owner[]` and matched a load against it
     * with `owner[i] == out`, so a Level that had gone out of scope left a dead
     * address claiming a slot -- and world.c's level-chain scan builds exactly
     * such a Level, a stack local it abandons when it returns. A later Level at
     * the same address inherited a brush map it never loaded, and the eviction
     * path went further still: it wrote `owner[0]->brushes = 0` THROUGH the dead
     * pointer to tell the evicted level it had been evicted.
     *
     * A serial cannot be reused, so it cannot be mistaken. ::level_load issues a
     * fresh one when it grants a slot and stores it here; the pool holds the
     * same number. A stale key matches nothing and is simply not a claim. The
     * address of a ::Level is never recorded anywhere, which is what makes the
     * question "is this Level still alive" one nothing has to ask.
     *
     * @note Give a slot back with ::level_release when a Level is about to go
     *       out of scope, AND THIS IS NOT OPTIONAL. There are
     *       ::LVL_BRUSH_SLOTS of them, which is two; a scope that abandons one
     *       without releasing it has taken a slot nothing can ever reclaim,
     *       and the next brush level to ask is refused rather than served
     *       somebody else's storage. The refusal is counted
     *       (::DIAG_LEVEL_SLOTS) and shows up as a `.map` that suddenly will
     *       not load -- which is loud, and is the point, but it is a leak and
     *       not a warning.
     *
     *       The old address-keyed pool appeared not to need this: a scratch
     *       Level declared inside a loop landed on the same stack address every
     *       iteration, so the pool handed back the slot it had given the last
     *       one. That is the mistaken identity this field exists to stop, so
     *       what looked like a pool that reclaimed slots was a pool that could
     *       not tell two Levels apart.
     * @note Not cleared by ::level_load: reloading the SAME Level must reuse the
     *       storage it already holds rather than take the other one's. That is
     *       what makes a hot reload, and a restart, cost one slot and not two.
     *
     * 한국어
     * ------
     * @brief 이 레벨이 배정받은 브러시 슬롯의 일련번호. 0이면 없음.
     *
     * 슬롯 풀은 이전에 이 레벨의 *주소*로 키잉되었고, 그것이 결함이었습니다. level.c가
     * `Level *owner[]`를 들고 `owner[i] == out`으로 로드를 대응시켰으므로, 스코프를 벗어난
     * Level이 죽은 주소로 슬롯을 계속 주장했습니다. world.c의 레벨 사슬 스캔이 바로 그런
     * Level을 만듭니다. 반환할 때 버리는 스택 지역 변수입니다. 같은 주소에 놓인 나중의 Level은
     * 자신이 로드한 적 없는 브러시 맵을 물려받았고, 축출 경로는 한 걸음 더 나아가
     * `owner[0]->brushes = 0`을 죽은 포인터를 *통해* 기록해 축출 사실을 알렸습니다.
     *
     * 일련번호는 재사용되지 않으므로 오인될 수 없습니다. ::level_load가 슬롯을 부여할 때 새
     * 번호를 발급해 이곳에 저장하고 풀이 같은 번호를 보관합니다. 낡은 키는 어느 것과도 맞지
     * 않으며 그저 주장이 아닐 뿐입니다. ::Level의 주소는 어디에도 기록되지 않으며, 그것이
     * "이 Level이 아직 살아 있는가"를 아무도 물을 필요가 없게 만드는 장치입니다.
     *
     * @note Level이 스코프를 벗어나기 직전에 ::level_release로 슬롯을 돌려주십시오. 잊어도
     *       무엇도 망가지지 않지만(어느 쪽이든 일련번호는 죽습니다) 슬롯은 계속 점유되며,
     *       슬롯은 둘뿐입니다.
     * @note ::level_load가 지우지 않습니다. *같은* Level을 다시 로드할 때는 다른 쪽의 것을
     *       빼앗지 않고 이미 보유한 저장 공간을 재사용해야 하기 때문입니다. 그것이 핫 리로드와
     *       재시작이 슬롯 하나만 쓰고 둘을 쓰지 않게 하는 장치입니다.
     */
    unsigned brush_key;
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
 * @brief ::level_load, into a store the caller owns.
 *
 * ENGLISH
 * -------
 * @param[in,out] bs   Where a brush level's geometry is put. NULL means the
 *                     default store, which is exactly what ::level_load passes.
 * @param[in]     name Level name, as ::level_load takes it.
 * @param[out]    out  The level.
 * @return As ::level_load.
 *
 * THE ONLY DIFFERENCE IS WHOSE SLOTS GET USED. Everything else -- the .map-first
 * search order, the fall through to levels.txt, leaving `out` untouched on a
 * name that does not resolve -- is the same code and the same contract.
 *
 * This exists because ::LVL_BRUSH_SLOTS used to be a budget for the PROCESS. An
 * editor holding a level beside the running game was a third live level against
 * a pool of two, and it was refused. Against its own store it is the first.
 *
 * @note Release into the SAME store with ::level_release_in. A level released
 *       against the wrong store keeps its slot in the right one, which is a
 *       leak of 420KB that nothing reports.
 * @note Sector levels never touch a store at all -- they have no brushes -- so
 *       a caller that only loads levels.txt levels may pass NULL forever and
 *       never allocate one.
 *
 * 한국어
 * ------
 * @brief 호출자가 소유한 저장소에 로드하는 ::level_load입니다.
 * @param[in,out] bs   브러시 레벨의 지오메트리가 놓일 곳. NULL이면 기본 저장소이며,
 *                     ::level_load가 넘기는 것이 정확히 그것입니다.
 * @param[in]     name ::level_load가 받는 것과 같은 레벨 이름.
 * @param[out]    out  레벨.
 * @return ::level_load와 같습니다.
 *
 * *유일한 차이는 누구의 슬롯을 쓰는가입니다.* 그 밖의 모든 것(.map 우선 검색 순서, levels.txt로
 * 내려가는 경로, 해석되지 않는 이름에 대해 `out`을 건드리지 않는 것)은 같은 코드이고 같은
 * 계약입니다.
 *
 * 이것이 존재하는 이유는 ::LVL_BRUSH_SLOTS가 한때 *프로세스*의 예산이었기 때문입니다. 실행 중인
 * 게임 곁에 레벨을 쥔 에디터는 슬롯 둘짜리 풀에 대한 세 번째 살아 있는 레벨이었고 거절되었습니다.
 * 자기 저장소에 대해서는 첫 번째입니다.
 *
 * @note *같은* 저장소에 ::level_release_in으로 반납하십시오. 엉뚱한 저장소에 대해 반납된 레벨은
 *       올바른 저장소에서 슬롯을 계속 쥐고 있으며, 그것은 아무도 보고하지 않는 420KB의
 *       누수입니다.
 * @note 섹터 레벨은 브러시가 없으므로 저장소를 전혀 건드리지 않습니다. levels.txt 레벨만 로드하는
 *       호출자는 영원히 NULL을 넘기고 저장소를 한 번도 할당하지 않아도 됩니다.
 */
int level_load_in(BrushStore *bs, const char *name, Level *out);

/**
 * @brief Gives back the brush slot a level holds, and forgets its geometry.
 *
 * ENGLISH
 * -------
 * @param[in,out] l Level to release. Safe on one that holds no slot, and safe
 *                  to call twice.
 *
 * THE PAIR TO THE GRANT ::level_load MAKES. There are ::LVL_BRUSH_SLOTS of them
 * per store and no destructor anywhere in this project runs by itself, so a
 * Level that is about to go out of scope has to say so or its slot is claimed
 * for as long as that store exists -- which for the default store is until the
 * process ends. world.c's level-chain scan is the case this was written
 * for: it loads up to ::WORLD_STAGE_MAX_HOPS levels into one stack local and
 * then returns.
 *
 * @note Clears ::Level::brushes as well as the claim. A released level is a
 *       SECTOR level -- which `Level l = {0}` already is, and which every query
 *       in this header already handles -- rather than one pointing into storage
 *       that now belongs to somebody else.
 * @note Does NOT clear the sectors, entities or lights. Releasing is about the
 *       brush storage, not about emptying a level; a released level still
 *       answers every question a sector level can.
 *
 * 한국어
 * ------
 * @brief 레벨이 보유한 브러시 슬롯을 반납하고 그 지오메트리를 잊습니다.
 * @param[in,out] l 반납할 레벨. 슬롯이 없어도 안전하며 두 번 호출해도 안전합니다.
 *
 * ::level_load가 하는 부여에 대응하는 짝입니다. 슬롯은 저장소당 ::LVL_BRUSH_SLOTS개뿐이고 이
 * 프로젝트의 어떤 소멸자도 저절로 실행되지 않으므로, 스코프를 벗어나려는 Level은 그 사실을
 * 말해야 합니다. 그러지 않으면 슬롯은 그 저장소가 존재하는 동안 계속 점유되며, 기본 저장소의
 * 경우 그것은 프로세스가 끝날 때까지입니다. 이 함수가 쓰인
 * 계기는 world.c의 레벨 사슬 스캔입니다. 하나의 스택 지역 변수에 최대
 * ::WORLD_STAGE_MAX_HOPS개의 레벨을 로드한 뒤 반환합니다.
 *
 * @note 주장과 함께 ::Level::brushes도 비웁니다. 반납된 레벨은 이제 남의 것이 된 저장 공간을
 *       가리키는 레벨이 아니라 *섹터* 레벨입니다. `Level l = {0}`이 이미 그러하고, 이 헤더의
 *       모든 질의가 이미 그 경우를 처리합니다.
 * @note 섹터·엔티티·광원은 지우지 *않습니다*. 반납은 브러시 저장 공간에 관한 것이지 레벨을
 *       비우는 일이 아니며, 반납된 레벨도 섹터 레벨이 답할 수 있는 모든 질문에 답합니다.
 */
void level_release(Level *l);

/**
 * @brief ::level_release, against a store the caller owns.
 *
 * ENGLISH
 * -------
 * @param[in,out] bs Store the level was loaded into. NULL means the default
 *                   store, which is what ::level_release passes.
 * @param[in,out] l  Level to release. Safe on one that holds no slot, and safe
 *                   to call twice.
 *
 * @note Pairs with ::level_load_in and must name the SAME store. Releasing
 *       against the wrong one clears ::Level::brushes and ::Level::brush_key --
 *       so the level is correctly a sector level afterwards -- but leaves the
 *       slot held in the store that actually granted it, and nothing will ever
 *       reclaim it. The pairing is a caller contract because the alternative is
 *       a back-pointer from every ::Level to its store, and a pointer to a store
 *       is exactly the kind of claim ::Level::brush_key exists to avoid.
 *
 * 한국어
 * ------
 * @brief 호출자가 소유한 저장소에 대해 수행하는 ::level_release입니다.
 * @param[in,out] bs 그 레벨이 로드된 저장소. NULL이면 기본 저장소이며 ::level_release가 넘기는
 *                   것이 그것입니다.
 * @param[in,out] l  반납할 레벨. 슬롯이 없어도 안전하며 두 번 호출해도 안전합니다.
 *
 * @note ::level_load_in과 짝을 이루며 *같은* 저장소를 지목해야 합니다. 엉뚱한 저장소에 대해
 *       반납하면 ::Level::brushes와 ::Level::brush_key는 비워지므로 그 레벨은 이후 올바르게 섹터
 *       레벨이 되지만, 실제로 슬롯을 부여한 저장소에서는 그 슬롯이 계속 점유되고 무엇도 그것을
 *       회수하지 않습니다. 이 짝맞춤이 호출자의 계약인 이유는, 대안이 모든 ::Level에서 자기
 *       저장소로 향하는 역포인터이고, 저장소를 가리키는 포인터야말로 ::Level::brush_key가
 *       피하려고 존재하는 바로 그 종류의 주장이기 때문입니다.
 */
void level_release_in(BrushStore *bs, Level *l);

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

/**
 * @enum LevelPart
 * @brief Which half of a level's geometry to build.
 *
 * ENGLISH
 * -------
 * A door moves geometry, and the geometry it does not move is the overwhelming
 * majority of a level. Rebuilding all of it every frame of a door's swing is
 * what this exists to stop: levelbench put a moving frame at 0.82x the cost of
 * a whole load build, so a door in motion costs very nearly what appearing in
 * the level costs, sixty times a second.
 *
 * ::LVL_PART_STATIC and ::LVL_PART_MOVING partition the level between them --
 * every brush lands in exactly one, and both walk brushes in ascending index
 * order, so STATIC followed by MOVING is a stable partition of ::LVL_PART_ALL
 * rather than merely the same vertices in some order. tools\splittest.c
 * asserts that vertex for vertex; it is the only way to state "the split
 * changes nothing about what is drawn" as something a machine can check.
 *
 * @note ::LVL_PART_ALL is the existing contract and what ::level_geometry
 *       passes. Nothing that does not care about doors has to learn about this.
 * @warning ONLY THE BRUSH MODEL SPLITS. Ask ::level_geometry_split first --
 *          the sector model returns 0 there and both halves then build
 *          everything, which is correct and pointless. See that function for
 *          why the sector path cannot do this safely.
 *
 * 한국어
 * ------
 * @brief 레벨 지오메트리의 어느 절반을 생성할지 지정합니다.
 *
 * 문은 지오메트리를 움직이며, 문이 움직이지 *않는* 지오메트리가 레벨의 압도적 다수입니다.
 * 문이 열리는 동안 매 프레임 그 전부를 다시 만드는 것이 이것이 막으려는 대상입니다.
 * levelbench는 움직이는 프레임을 로드 시 전체 생성의 0.82배로 측정했습니다. 즉 움직이는 문은
 * 레벨에 나타나는 것과 거의 같은 비용을 초당 60번 치릅니다.
 *
 * ::LVL_PART_STATIC과 ::LVL_PART_MOVING은 레벨을 둘로 분할합니다. 모든 브러시가 정확히 한쪽에
 * 속하며, 양쪽 모두 브러시를 인덱스 오름차순으로 순회하므로 STATIC 다음 MOVING은 단지 같은
 * 정점들의 어떤 순서가 아니라 ::LVL_PART_ALL의 *안정 분할*입니다. tools\splittest.c가 이를
 * 정점 단위로 단언합니다. "분할은 그려지는 것을 바꾸지 않는다"를 기계가 검사할 수 있는 형태로
 * 진술하는 유일한 방법입니다.
 *
 * @note ::LVL_PART_ALL이 기존 계약이며 ::level_geometry가 넘기는 값입니다. 문에 관심 없는
 *       무엇도 이것을 배울 필요가 없습니다.
 * @warning *브러시 모델만* 분할됩니다. 먼저 ::level_geometry_split에 물으십시오. 섹터 모델은
 *          그곳에서 0을 반환하며, 그 경우 양쪽 절반이 모두 전체를 생성합니다. 올바르지만
 *          무의미합니다. 섹터 경로가 이것을 안전하게 할 수 없는 이유는 그 함수를 참조하십시오.
 */
typedef enum {
    LVL_PART_ALL,     /**< Everything. The existing contract. / 전체. 기존 계약입니다. */
    LVL_PART_STATIC,  /**< Only what no door moves. / 어떤 문도 움직이지 않는 것만. */
    LVL_PART_MOVING   /**< Only what a door moves. / 문이 움직이는 것만. */
} LevelPart;

/**
 * @brief Whether this level's geometry can be built in halves at all.
 *
 * ENGLISH
 * -------
 * TWO CONDITIONS, and the second is the interesting one.
 *
 * A level splits when it is a BRUSH level and at least one door moves brushes.
 * The first half of that is a capability and the second is simply whether
 * there is anything to gain.
 *
 * WHY THE SECTOR MODEL IS EXCLUDED, and it is not an omission. ::level_edge_spans
 * builds a wall by asking ::level_sector_at what is on the far side of each
 * edge and reading THAT sector's floor and ceiling. So a door that rises does
 * not only change its own walls -- it changes the step every neighbouring
 * sector draws against it, and the step is owned by whichever of the two has
 * the higher index. A "moving set" would have to include those neighbours.
 *
 * That much could be computed once at load. What cannot is ::DOOR_X and
 * ::DOOR_Z: a sliding door moves the sector's POINTS, so which sectors it is
 * adjacent to changes as it slides. A set computed at load is not conservative
 * enough for it, and being wrong here does not fail loudly -- it drops the
 * walls that should have appeared and leaves a hole you can see through and
 * walk into. That is precisely the silent truncation diag.h was written to end,
 * so the sector path keeps rebuilding everything until there is a way to state
 * its moving set that a test can check.
 *
 * A brush face has no such dependency: it is the intersection of its own
 * brush's half-spaces and reads nothing outside it, so translating a brush
 * changes exactly that brush's vertices.
 *
 * @param[in] l Level to ask about.
 * @return Non-zero when ::LVL_PART_STATIC and ::LVL_PART_MOVING partition this
 *         level, 0 when both would build all of it.
 *
 * 한국어
 * ------
 * @brief 이 레벨의 지오메트리를 애초에 절반으로 나누어 생성할 수 있는가.
 *
 * 조건은 둘이며, 흥미로운 쪽은 두 번째입니다.
 *
 * 레벨이 *브러시* 레벨이고 최소 하나의 문이 브러시를 움직일 때 분할됩니다. 앞쪽 절반은
 * 가능한가이고 뒤쪽 절반은 얻을 것이 있는가입니다.
 *
 * 섹터 모델이 제외된 이유이며, 빠뜨린 것이 아닙니다. ::level_edge_spans는 각 모서리 건너편에
 * 무엇이 있는지를 ::level_sector_at에 묻고 *그* 섹터의 바닥과 천장을 읽어 벽을 만듭니다.
 * 따라서 올라가는 문은 자기 벽만 바꾸는 것이 아니라, 이웃한 모든 섹터가 그것에 맞대어 그리는
 * 단차를 바꿉니다. 그리고 그 단차는 둘 중 인덱스가 큰 쪽이 소유합니다. "이동 집합"은 그
 * 이웃들까지 포함해야 합니다.
 *
 * 거기까지는 로드 시 한 번 계산할 수 있습니다. 할 수 *없는* 것은 ::DOOR_X와 ::DOOR_Z입니다.
 * 미끄러지는 문은 섹터의 *점*을 움직이므로, 미끄러지는 동안 어느 섹터와 인접한지가 달라집니다.
 * 로드 시 계산한 집합은 그것에 대해 충분히 보수적이지 않으며, 이곳에서 틀리는 것은 요란하게
 * 실패하지 않습니다. 나타났어야 할 벽을 누락시켜, 들여다보이고 걸어 들어갈 수 있는 구멍을
 * 남깁니다. 그것이 바로 diag.h가 끝내려고 만들어진 조용한 절단이므로, 섹터 경로는 자신의 이동
 * 집합을 테스트가 검사할 수 있는 형태로 진술할 방법이 생길 때까지 전체를 계속 다시 만듭니다.
 *
 * 브러시 면에는 그런 의존이 없습니다. 자기 브러시의 반공간들의 교집합이며 그 바깥의 무엇도
 * 읽지 않으므로, 브러시를 옮기면 정확히 그 브러시의 정점만 바뀝니다.
 *
 * @param[in] l 질의할 레벨.
 * @return ::LVL_PART_STATIC과 ::LVL_PART_MOVING이 이 레벨을 분할하면 0이 아닌 값,
 *         양쪽이 모두 전체를 생성하게 되면 0.
 */
int level_geometry_split(const Level *l);

/**
 * @brief Builds one half of a level's geometry. See ::LevelPart.
 *
 * ENGLISH
 * -------
 * @param[in,out] b          Buffer receiving the geometry. Appended to, never
 *                           cleared -- which is what lets STATIC and MOVING be
 *                           built into one buffer back to back.
 * @param[in]     l          Level to build.
 * @param[out]    ranges     Receives one entry per run of triangles sharing a
 *                           material. May be NULL.
 * @param[in]     max_ranges Capacity of `ranges`.
 * @param[in]     part       Which half. ::LVL_PART_ALL is ::level_geometry.
 * @return How many ranges were written.
 * @note THE STATIC LIGHT IS BAKED WHERE THE DOORS WERE. Each half bakes only
 *       its own vertices, so a static wall keeps the shadow the door cast at
 *       load and does not brighten when it opens. That is Quake's behaviour and
 *       for Quake's reason -- a lightmap is compiled once and a door does not
 *       recompile it -- and it is the price of not re-tracing every vertex in
 *       the level sixty times a second. The moving half re-bakes as it moves,
 *       so the door itself is lit correctly wherever it is.
 * @warning A `max_ranges` too small drops the surplus silently and raises
 *          ::DIAG_MAT_RANGES. Building in halves produces at least as many runs
 *          as building whole, because a run cannot merge across the two calls.
 *
 * 한국어
 * ------
 * @brief 레벨 지오메트리의 한쪽 절반을 생성합니다. ::LevelPart를 참조하십시오.
 * @param[in,out] b          지오메트리를 받을 버퍼. 비우지 않고 *덧붙입니다*. 그것이 STATIC과
 *                           MOVING을 하나의 버퍼에 연달아 생성할 수 있게 하는 것입니다.
 * @param[in]     l          생성할 대상 레벨.
 * @param[out]    ranges     동일 재질 구간마다 하나의 항목을 받습니다. NULL이어도 됩니다.
 * @param[in]     max_ranges `ranges`의 용량.
 * @param[in]     part       어느 절반인지. ::LVL_PART_ALL은 ::level_geometry입니다.
 * @return 기록된 구간의 개수.
 * @note *정적 조명은 문이 있던 자리를 기준으로 구워집니다.* 각 절반은 자기 정점만 굽므로,
 *       정적인 벽은 로드 시 문이 드리운 그림자를 유지하며 문이 열려도 밝아지지 않습니다.
 *       이는 Quake의 동작이며 Quake의 이유와 같습니다. 라이트맵은 한 번 컴파일되고 문이 그것을
 *       다시 컴파일하지 않습니다. 그리고 이것이 레벨의 모든 정점을 초당 60번 다시 판정하지 않는
 *       대가입니다. 움직이는 절반은 움직이며 다시 굽므로 문 자체는 어디에 있든 올바르게
 *       조명됩니다.
 * @warning `max_ranges`가 너무 작으면 초과분을 조용히 버리고 ::DIAG_MAT_RANGES를 올립니다.
 *          절반씩 생성하면 두 호출에 걸쳐 구간이 병합될 수 없으므로, 통째로 생성할 때보다
 *          구간 수가 같거나 많습니다.
 */
int level_geometry_part(MeshBuf *b, const Level *l, MdlRange *ranges,
                        int max_ranges, LevelPart part);

/**
 * @brief Forgets every vertex the light bake has cached.
 *
 * ENGLISH
 * -------
 * ::level_geometry bakes each vertex's static light against the level and
 * keeps the answer under that vertex's position and normal, so that a rebuild
 * does not re-trace the vertices that came back to exactly where they were --
 * 79.9% of them with a door at half travel, which tools\leveltest.c measures
 * rather than this comment claiming. See level.c for why the key is sufficient
 * and what the cache deliberately stops following.
 *
 * @note WHAT THE CACHE IS STILL FOR, now that ::LVL_PART_STATIC exists. A brush
 *       level no longer rebuilds the whole of itself when a door moves, so the
 *       unmoved vertices are not re-traced because they are not rebuilt at all
 *       -- the cache is not what saves them any more. It still earns its place
 *       on the sector model, which ::level_geometry_split refuses and which
 *       therefore does rebuild everything, and on a load, where a level built
 *       twice in one session pays once.
 *
 * ::level_load calls this, which covers the game: a level becoming a different
 * level is the only thing that invalidates a reading, and that is the one
 * place it can happen.
 *
 * @note An editor that moves a LIGHT rather than a wall has to call this
 *       itself. The cache is keyed on the vertex, not on the lamp, so nothing
 *       about moving a lamp tells it that anything changed -- the level would
 *       keep the lighting it had until it was reloaded. Rebuild cost is not a
 *       concern where an author is waiting for the picture anyway.
 * @warning Not needed for a door, a switch, or anything else that moves a
 *          sector at runtime. Calling it there gives back exactly the
 *          per-frame cost this exists to remove.
 *
 * 한국어
 * ------
 * @brief 라이트 베이크가 캐시한 모든 정점을 잊습니다.
 *
 * ::level_geometry는 각 정점의 정적 조명을 레벨에 대해 굽고 그 답을 해당 정점의 위치와
 * 법선 아래에 보관합니다. 그래서 레벨 지오메트리 전체를 다시 만드는 문의 움직임이, 정확히
 * 있던 자리로 되돌아온 80%의 정점을 다시 판정하지 않게 됩니다. 왜 그 키로 충분한지, 그리고
 * 캐시가 의도적으로 무엇을 따라가지 않게 되는지는 level.c를 참조하십시오.
 *
 * ::level_load가 이것을 호출하며, 그것으로 게임은 충분합니다. 판정 결과를 무효로 만드는
 * 것은 레벨이 다른 레벨이 되는 일뿐이고, 그 일이 일어날 수 있는 곳이 그곳 하나입니다.
 *
 * @note 벽이 아니라 *광원*을 옮기는 에디터는 이것을 직접 호출해야 합니다. 캐시의 키는
 *       등이 아니라 정점이므로, 등을 옮긴 사실은 캐시에 아무것도 알려 주지 않습니다.
 *       레벨은 다시 로드할 때까지 이전 조명을 유지하게 됩니다. 어차피 제작자가 화면을
 *       기다리는 상황에서 재생성 비용은 문제가 되지 않습니다.
 * @warning 문이나 스위치, 그 밖에 런타임에 섹터를 움직이는 것에는 필요하지 *않습니다*.
 *          그곳에서 호출하면 이것이 없애려는 프레임별 비용을 그대로 되돌려받습니다.
 */
void level_light_cache_reset(void);

/**
 * @brief How many vertices the light cache is holding. For tests and tools.
 * @brief 라이트 캐시가 보유 중인 정점의 수입니다. 테스트와 도구용입니다.
 */
int level_light_cache_count(void);

/**
 * @brief How many slots the cache was built with, and how many bytes of .bss
 *        that came to.
 *
 * ENGLISH
 * -------
 * Both are compile-time constants, reported through a call so that a test can
 * branch on the size it was actually built with rather than on a macro it
 * would have to be given a second copy of. build.ps1 compiles a second
 * leveltest with the slot count forced small, and the same source has to make
 * opposite assertions in the two binaries: one that the table overflowed, one
 * that it did not.
 *
 * The byte figure is the honest cost of the cache and belongs in the commit
 * that introduces it. It is `.bss`, so the floppy budget does not count it --
 * but the machine does.
 *
 * 한국어
 * ------
 * @brief 캐시가 몇 개의 슬롯으로 만들어졌는지, 그리고 그것이 `.bss` 몇 바이트인지입니다.
 *
 * 둘 다 컴파일 타임 상수이지만 호출로 보고합니다. 테스트가 매크로의 두 번째 사본을
 * 받아야 하는 대신 *실제로 빌드된 크기*를 기준으로 분기할 수 있게 하기 위함입니다.
 * build.ps1은 슬롯 수를 작게 강제한 두 번째 leveltest를 컴파일하며, 같은 소스가 두
 * 바이너리에서 정반대의 단언을 해야 합니다. 하나는 테이블이 넘쳤다고, 다른 하나는 넘치지
 * 않았다고.
 *
 * 바이트 수치는 이 캐시의 정직한 비용이며 그것을 도입하는 커밋에 들어가야 합니다.
 * `.bss`이므로 플로피 예산은 세지 않지만, 기계는 셉니다.
 */
int level_light_cache_slots(void);
int level_light_cache_bytes(void);

/**
 * @brief Turns the light cache off, leaving ::level_geometry to trace every
 *        vertex the way it did before the cache existed.
 *
 * ENGLISH
 * -------
 * Exists so that the claim the cache rests on can be RUN rather than asserted
 * in a comment: that a build with an empty cache comes out identical, vertex
 * for vertex, to a build with no cache at all. Those are two different code
 * paths -- one looks up and stores, the other does neither -- and nothing
 * makes them agree except the key being sufficient. tools\leveltest.c builds
 * both and compares them.
 *
 * @note Not a setting. The game never calls this, and a build that did would
 *       pay the full bake on every frame a door moved.
 * @warning Switching it back on does not repopulate anything. The table keeps
 *          whatever it held when it was switched off, which is why the test
 *          resets around it rather than relying on the toggle to clear it.
 *
 * 한국어
 * ------
 * @brief 라이트 캐시를 끄고, ::level_geometry가 캐시가 있기 전처럼 모든 정점을 판정하게
 *        합니다.
 *
 * 캐시가 딛고 선 주장을 주석 속 단언이 아니라 *실행할 수 있는 것*으로 만들기 위해
 * 존재합니다. 빈 캐시로 만든 결과가 캐시가 아예 없는 결과와 정점 하나까지 동일하다는
 * 주장입니다. 그 둘은 서로 다른 코드 경로이며(한쪽은 찾아보고 저장하고, 다른 쪽은 둘 다 하지
 * 않습니다) 키가 충분하다는 사실 외에는 그 둘을 일치시키는 것이 없습니다.
 * tools\leveltest.c가 양쪽을 만들어 비교합니다.
 *
 * @note 설정이 아닙니다. 게임은 이것을 호출하지 않으며, 호출하는 빌드는 문이 움직이는 매
 *       프레임마다 전체 베이크 비용을 치르게 됩니다.
 * @warning 다시 켜도 아무것도 다시 채우지 않습니다. 껐을 때 담고 있던 것을 그대로 유지하며,
 *          그래서 테스트는 이 토글이 비워 주기를 기대하지 않고 그 주위에서 직접
 *          리셋합니다.
 */
void level_light_cache_enable(int on);

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

/* Does the sun this level declares reach `from`? 0 when it declares none.
   `from` must already be lifted off the surface it belongs to.
   Exists for tools/lightprobe.c: the walk that answers this passes THROUGH sky
   brushes, and a test that replicated it would carry the same mistakes.
   이 레벨이 선언한 태양이 `from`에 닿습니까. 선언하지 않았다면 0입니다. 이 답을 내는 걸음은
   하늘 브러시를 통과하며, 그것을 복제한 테스트는 같은 실수를 그대로 가질 것입니다. */
int level_sun_reaches(const Level *l, v3 from);

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
 * @brief Which door owns the surface at `p`, or -1.
 *
 * ENGLISH
 * -------
 * @param[in] l Level to ask.
 * @param[in] p A point ON a surface -- a trace's hit point.
 * @param[in] n That surface's outward normal, facing the ray that found it.
 * @return A door index into ::Level::doors, or -1 for a surface no door moves.
 *
 * ONE QUESTION, BOTH MODELS, and the same trick answers it for each: step a
 * little way INTO the surface and ask what is there. A hit point sits exactly
 * on a boundary, where every lookup is a coin toss; a point just inside the
 * solid is unambiguously part of whatever the solid is. For a sector door that
 * is a ::sector_at landing in the door's own footprint; for a brush door it is
 * ::brush_point_in on the run of brushes the door owns.
 *
 * @note Exists because a bullet hole has to know what it is stuck to. A decal
 *       is a world position and nothing else, which is right for a wall and
 *       wrong for a door -- shoot one and open it and the mark hangs in the
 *       air behind it. ::decal_hit asks this once, at the moment of impact.
 * @note Cheap enough to ask per shot and nowhere near cheap enough to ask per
 *       frame: it walks every door. The answer is recorded when the mark is
 *       made rather than re-derived, which is also the only correct thing --
 *       a mark belongs to the door it was made on even after that door has
 *       moved out from under the point it was made at.
 *
 * 한국어
 * ------
 * @brief `p`의 표면을 소유한 문의 인덱스, 없으면 -1.
 * @param[in] p 표면 *위의* 점. 판정의 충돌 지점입니다.
 * @param[in] n 그 표면의 바깥 법선. 그것을 찾아낸 광선을 향합니다.
 *
 * 하나의 질문, 두 모델, 그리고 각각에 같은 요령으로 답합니다. 표면 *안쪽으로* 조금 들어가 그곳에
 * 무엇이 있는지 묻는 것입니다. 충돌 지점은 정확히 경계 위에 있고 그곳에서는 모든 조회가 동전
 * 던지기입니다. 고체 안쪽으로 조금 들어간 점은 명백히 그 고체의 일부입니다. 섹터 문에서는 그것이
 * 문 자신의 발자국 안에 떨어지는 ::sector_at이고, 브러시 문에서는 문이 소유한 브러시 구간에 대한
 * ::brush_point_in입니다.
 *
 * @note 탄흔이 자기가 무엇에 붙어 있는지 알아야 해서 존재합니다. 데칼은 월드 좌표일 뿐이며 벽에는
 *       맞고 문에는 틀립니다. 문을 쏘고 열면 자국이 그 뒤 허공에 남습니다. ::decal_hit이 충돌
 *       순간에 이것을 한 번 묻습니다.
 * @note 사격마다 물을 만큼은 싸고 프레임마다 물기에는 전혀 싸지 않습니다. 모든 문을 순회합니다.
 *       답은 다시 유도하지 않고 자국을 만들 때 기록하며, 그것이 유일하게 옳기도 합니다. 자국은
 *       그것이 만들어진 문에 속하며, 그 문이 자국이 만들어진 지점 아래에서 빠져나간 뒤에도
 *       그렇습니다.
 */
int level_door_at(const Level *l, v3 p, v3 n);

/**
 * @brief Damage per second a point takes.
 *
 * ENGLISH
 * -------
 * @param[in] l Level to query.
 * @param[in] x X coordinate in world units.
 * @param[in] y Y coordinate in world units. A brush level tests it; a sector
 *              level cannot and ignores it. Pass the FEET, not the eye.
 * @param[in] z Z coordinate in world units.
 * @return The hurt rate at that point, or 0 outside the map and anywhere safe.
 *
 * TWO MODELS, TWO SHAPES OF ANSWER.
 *
 * A sector level answers from the governing ::Sector::hurt under the same
 * last-declared-wins rule as ::level_ground, so a safe platform laid over a
 * lava pit is safe to stand on -- which is what makes a lava room playable
 * rather than merely lethal. The height is ignored because a sector has no way
 * to use it: its hazard is a property of the floor, and a plan point has
 * exactly one floor.
 *
 * A brush level answers from the ::HazardDef volumes, and the point is simply
 * in one or it is not. No precedence rule, because a dais in a lava pit is a
 * solid brush that the player stands ON TOP OF -- their feet end up above the
 * lava rather than inside it, and geometry has already given the answer that
 * "last declared wins" had to be invented for.
 *
 * @note Answers only "how dangerous is this point". Whether the player is
 *       actually touching it is the caller's question, because the answer
 *       differs for a player standing on it, a monster wading through it and
 *       a projectile passing over it.
 *
 * 한국어
 * ------
 * @brief 특정 지점이 받는 초당 피해량입니다.
 * @param[in] l 조회할 레벨.
 * @param[in] x 월드 단위의 X 좌표.
 * @param[in] y 월드 단위의 Y 좌표. 브러시 레벨은 이것을 판정하고, 섹터 레벨은 판정할 수
 *              없으므로 무시합니다. 눈이 아니라 *발*을 넘기십시오.
 * @param[in] z 월드 단위의 Z 좌표.
 * @return 그 지점의 피해율. 맵 바깥이거나 안전한 곳이면 0.
 *
 * 두 모델, 두 가지 형태의 답입니다.
 *
 * 섹터 레벨은 ::level_ground와 동일한 "마지막 선언 우선" 규칙 아래 그 지점을 지배하는
 * ::Sector::hurt로 답합니다. 그래서 용암 구덩이 위에 놓인 안전한 발판은 밟고 설 수 있고,
 * 그것이 용암 방을 단순히 치명적인 곳이 아니라 플레이 가능한 곳으로 만듭니다. 높이를 무시하는
 * 이유는 섹터가 그것을 쓸 방법이 없기 때문입니다. 섹터의 위험 지형은 바닥의 속성이고, 평면상의
 * 한 점에는 바닥이 정확히 하나뿐입니다.
 *
 * 브러시 레벨은 ::HazardDef 부피로 답하며, 지점은 그 안에 있거나 없거나 둘 중 하나입니다.
 * 우선순위 규칙이 없는 이유는 용암 구덩이 속의 단상이 플레이어가 그 *위에* 서는 고체
 * 브러시이기 때문입니다. 발은 용암 안이 아니라 그 위에 놓이며, "마지막 선언 우선"이 발명되어야
 * 했던 답을 지오메트리가 이미 내놓습니다.
 *
 * @note "이 지점이 얼마나 위험한가"에만 답합니다. 플레이어가 실제로 닿아 있는지는 호출자의
 *       질문입니다. 그 답이 그 위에 선 플레이어, 그곳을 헤치는 몬스터, 위를 스치는 발사체마다
 *       다르기 때문입니다.
 */
int level_hazard_at(const Level *l, float x, float y, float z);

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
