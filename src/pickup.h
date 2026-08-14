/**
 * @file pickup.h
 * @brief Floor pickups: ammo and health.
 *
 * ENGLISH
 * -------
 * They come from the same level entities the editor already places -- an
 * "ammo" or "health" entity becomes a bobbing billboard that tops you up when
 * you walk over it. The collection logic touches no GL, so it is stepped
 * headless by tools/pickuptest.c the same way movement and monsters are: a
 * "does walking over it actually give me anything" bug is invisible from
 * inside the running game otherwise.
 *
 * 한국어
 * ------
 * 에디터가 이미 배치하는 레벨 엔티티에서 생성됩니다. "ammo" 또는 "health"
 * 엔티티는 위아래로 움직이는 빌보드가 되며, 그 위를 지나가면 수치를 채워 줍니다.
 * 수집 로직은 GL을 사용하지 않으므로, 이동이나 몬스터와 마찬가지로
 * tools/pickuptest.c가 창 없이 단계별로 실행합니다. 그렇지 않으면 "밟았는데
 * 실제로 획득이 되는가" 같은 버그는 실행 중인 게임 안에서 보이지 않습니다.
 */
#ifndef PICKUP_H
#define PICKUP_H

#include "level.h"
/* WP_TYPES and the WP_* order: a pickup kind is derived from a weapon index
   rather than listed beside it, so adding a weapon adds its ammo box and its
   world item without a second table to keep in step.
   WP_TYPES와 WP_* 순서가 필요합니다. 아이템 종류를 무기 인덱스에서 유도하므로, 무기를
   추가하면 별도의 표를 맞출 필요 없이 탄약 상자와 월드 아이템이 함께 추가됩니다. */
#include "weapon.h"

/* --- Macros and constants / 매크로 및 상수 --- */

#define PICKUP_MAX     64       ///< @brief Maximum pickups tracked per level. / 레벨당 관리되는 최대 아이템 수.
#define PICKUP_RADIUS  0.75f    ///< @brief How close your feet must get, metres. / 발이 얼마나 가까워져야 하는지 (미터).
#define PICKUP_AMMO    8        ///< @brief Shells per ammo box. / 탄약 상자 하나당 탄환 수.
#define PICKUP_HEALTH  25       ///< @brief Points per medkit. / 구급상자 하나당 회복량.

/* --- Enumerations / 열거형 --- */

/**
 * @brief Kinds a pickup can be.
 *
 * ENGLISH
 * -------
 * @brief Kinds a pickup can be.
 * @note The index is the column in the pickup sprite atlas (see sprite.h),
 *       so the two must stay in step.
 *
 * 한국어
 * ------
 * @brief 아이템의 종류입니다.
 * @note 이 인덱스는 아이템 스프라이트 아틀라스(sprite.h 참조)의 열 번호와
 *       일치하므로, 양쪽을 항상 동기화해야 합니다.
 */
enum {
    PK_AMMO,    /**< Ammo box, refills shells. / 탄약 상자. 탄환을 보충합니다. */
    PK_HEALTH,  /**< Medkit, restores health. / 구급상자. 체력을 회복시킵니다. */

    /* --- one ammo box per weapon, and one of each weapon ------------------
     *
     * ENGLISH
     * -------
     * Laid out as two runs of WP_TYPES so a kind can be turned into a weapon
     * index by subtraction rather than by a lookup table. ::PK_AMMO_FOR and
     * ::PK_WEAPON_FOR are the only two places that arithmetic is written.
     *
     * PK_AMMO above is the shotgun's box under its old name, kept because
     * every authored level says `ammo` and a rename would silently empty them.
     * PK_AMMO_SHOTGUN is an alias for it rather than a second kind, so the two
     * cannot drift into meaning different things.
     *
     * 한국어
     * ------
     * WP_TYPES 길이의 두 구간으로 배치하여, 종류를 조회표가 아니라 뺄셈으로 무기
     * 인덱스로 바꿀 수 있게 합니다. 그 산술이 기록된 곳은 ::PK_AMMO_FOR와
     * ::PK_WEAPON_FOR 두 곳뿐입니다.
     *
     * 위의 PK_AMMO는 샷건의 상자를 옛 이름으로 둔 것입니다. 제작된 모든 레벨이 `ammo`라고
     * 적고 있으며 이름을 바꾸면 그것들이 조용히 비기 때문입니다.
     */
    PK_AMMO0,                        /**< Ammo for WP_SHOTGUN. / WP_SHOTGUN용 탄약. */
    PK_AMMO_LAST = PK_AMMO0 + WP_TYPES - 1,

    PK_WEAPON0,                      /**< The WP_SHOTGUN weapon itself. / WP_SHOTGUN 무기 자체. */
    PK_WEAPON_LAST = PK_WEAPON0 + WP_TYPES - 1,

    /* --- keycards ------------------------------------------------------
       One per KEY_* bit, in the same order, so a kind converts to a mask by
       shifting rather than by a table. A fourth key is a bit in level.h and a
       colour here, and nothing else.
       KEY_* 비트마다 하나이며 순서도 같으므로, 종류를 마스크로 바꾸는 것이 표가 아니라
       시프트입니다. 네 번째 열쇠는 level.h의 비트 하나와 이곳의 색 하나가 전부입니다. */
    PK_KEY0,
    PK_KEY_LAST = PK_KEY0 + KEY_KINDS - 1,

    PK_KINDS    /**< Total number of pickup kinds. / 아이템 종류의 총 수. */
};

/** @brief The ammo-box kind that fills weapon `w`'s belt. / 무기 `w`의 탄약을 채우는 상자 종류. */
#define PK_AMMO_FOR(w)    (PK_AMMO0   + (w))
/** @brief The pickup kind that grants weapon `w`. / 무기 `w`를 주는 아이템 종류. */
#define PK_WEAPON_FOR(w)  (PK_WEAPON0 + (w))

/** @brief The weapon an ammo kind fills, or -1. / 탄약 종류가 채우는 무기. 아니면 -1. */
#define PK_AMMO_WEAPON(k) \
    ((k) == PK_AMMO ? WP_SHOTGUN \
     : ((k) >= PK_AMMO0 && (k) <= PK_AMMO_LAST) ? (k) - PK_AMMO0 : -1)
/** @brief The KEY_* mask a pickup kind grants, or ::KEY_NONE. / 아이템 종류가 주는 KEY_* 마스크. */
#define PK_KEY_MASK(k)     (((k) >= PK_KEY0 && (k) <= PK_KEY_LAST) ? (1 << ((k) - PK_KEY0)) : KEY_NONE)

/** @brief The weapon a pickup kind grants, or -1. / 아이템 종류가 주는 무기. 아니면 -1. */
#define PK_WEAPON_WEAPON(k) \
    (((k) >= PK_WEAPON0 && (k) <= PK_WEAPON_LAST) ? (k) - PK_WEAPON0 : -1)

/* --- Type definitions / 타입 정의 --- */

/**
 * @struct Pickup
 * @brief One collectable item resting on the floor.
 *
 * ENGLISH
 * -------
 * @brief One collectable item resting on the floor.
 *
 * 한국어
 * ------
 * @brief 바닥에 놓인 획득 가능한 아이템 하나입니다.
 */
typedef struct {
    int   kind;        /**< One of the PK_* constants. / PK_* 상수 중 하나. */
    v3    pos;         /**< Feet position, on the floor it spawned above. / 발 위치. 생성된 지점 아래의 바닥에 놓입니다. */
    float anim;        /**< Free-running clock for the bob and spin. / 위아래 움직임과 회전을 위한 자유 진행 시계. */
    int   active;      /**< 0 once collected. / 획득되면 0이 됩니다. */
} Pickup;

/**
 * @struct PickupPool
 * @brief Every item standing in the level, owned by the caller.
 *
 * `count` rather than an `active` scan for the extent, because items are laid
 * out once when the level loads and only ever cleared after that -- the array
 * is packed to `count` and stays that way. A consumed item clears its own
 * `active` and leaves the hole, so that ::pickup_at keeps handing back the
 * same index for the same item and the drawer's tint does not jump when
 * something is picked up.
 *
 * 레벨에 서 있는 모든 아이템이며 호출자가 소유합니다. 범위를 `active` 순회가 아니라
 * `count`로 두는 이유는, 아이템이 레벨 로드 시 한 번 배치되고 그 뒤로는 지워지기만 하기
 * 때문입니다. 배열은 `count`까지 채워진 채로 유지됩니다. 획득된 아이템은 자기 `active`만
 * 지우고 자리를 남기므로, ::pickup_at이 같은 아이템에 같은 인덱스를 계속 돌려주고 무언가를
 * 주웠을 때 그리는 쪽의 색조가 튀지 않습니다.
 */
typedef struct {
    Pickup p[PICKUP_MAX];   /**< Slots, packed to `count`. / `count`까지 채워진 슬롯. */
    int    count;           /**< How many the level laid out. / 레벨이 배치한 개수. */
} PickupPool;

/* The bundle that holds this pool. See proj.h for why the calls take it.
   이 풀을 담는 묶음입니다. 호출들이 그것을 받는 이유는 proj.h를 참조하십시오. */
typedef struct Pools Pools;

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Clears every pickup, leaving none active.
 *
 * ENGLISH
 * -------
 * @brief Clears every pickup, leaving none active.
 * @note Called automatically by ::pickup_spawn_level, so a caller loading a
 *       level does not need to invoke it separately.
 *
 * 한국어
 * ------
 * @brief 모든 아이템을 제거하여 활성 상태인 것이 남지 않도록 합니다.
 * @note ::pickup_spawn_level이 자동으로 호출하므로, 레벨을 로드하는 호출자가
 *       따로 실행할 필요는 없습니다.
 */
void pickup_reset(Pools *pl);

/**
 * @brief Spawns pickups from a level's entity list.
 *
 * ENGLISH
 * -------
 * @brief Spawns pickups from a level's entity list.
 * @param[in] l Level whose entities are scanned for "ammo" and "health".
 * @note Resets the existing set first. Entities of any other kind are
 *       ignored, and one with no floor beneath it is skipped rather than
 *       spawned floating.
 * @note Silently stops at ::PICKUP_MAX; a level with more pickup entities
 *       than that will not spawn the remainder.
 *
 * 한국어
 * ------
 * @brief 레벨의 엔티티 목록으로부터 아이템을 생성합니다.
 * @param[in] l "ammo"와 "health" 엔티티를 탐색할 대상 레벨.
 * @note 먼저 기존 목록을 초기화합니다. 다른 종류의 엔티티는 무시되며, 아래에
 *       바닥이 없는 엔티티는 공중에 생성되는 대신 건너뜁니다.
 * @note ::PICKUP_MAX에 도달하면 조용히 중단됩니다. 그보다 많은 아이템 엔티티를
 *       가진 레벨에서는 나머지가 생성되지 않습니다.
 */
void pickup_spawn_level(Pools *pl, const Level *l);

/**
 * @brief Returns how many pickups the current level spawned.
 *
 * ENGLISH
 * -------
 * @brief Returns how many pickups the current level spawned.
 * @return The count, including those already collected.
 * @note Collected pickups remain in the list with `active` cleared, so this
 *       count does not shrink as the player picks things up.
 *
 * 한국어
 * ------
 * @brief 현재 레벨에서 생성된 아이템의 개수를 반환합니다.
 * @return 이미 획득된 것을 포함한 총 개수.
 * @note 획득된 아이템은 `active`가 해제된 채로 목록에 남으므로, 플레이어가
 *       아이템을 주워도 이 값은 줄어들지 않습니다.
 */
int pickup_count(const Pools *pl);

/**
 * @brief Borrows a pointer to one pickup by index.
 *
 * ENGLISH
 * -------
 * @brief Borrows a pointer to one pickup by index.
 * @param[in] i Index in the range [0, ::pickup_count()).
 * @return A borrowed pointer to the pickup, or NULL when `i` is out of range.
 * @warning The pointer refers to module-owned storage and must not be freed.
 *          It is invalidated by ::pickup_reset and ::pickup_spawn_level.
 *
 * 한국어
 * ------
 * @brief 인덱스로 아이템 하나에 대한 포인터를 빌려 옵니다.
 * @param[in] i [0, ::pickup_count()) 범위의 인덱스.
 * @return 해당 아이템에 대한 참조 포인터. `i`가 범위를 벗어나면 NULL.
 * @warning 이 포인터는 모듈이 소유한 저장 공간을 가리키므로 해제해서는 안 됩니다.
 *          ::pickup_reset과 ::pickup_spawn_level 호출 시 무효화됩니다.
 */
const Pickup *pickup_at(const Pools *pl, int i);

/**
 * @brief Advances the bob animation and collects any pickup the player overlaps.
 *
 * ENGLISH
 * -------
 * @brief Advances the bob animation and collects any pickup the player overlaps.
 * @param[in]     player_eye Player's eye position; feet are derived internally.
 * @param[in,out] health     Read and written in place when a medkit is taken.
 * @param[in]     health_max Cap that health is topped up to, never exceeded.
 * @param[in,out] ammo       Read and written in place when an ammo box is taken.
 * @param[in]     ammo_max   Cap that ammo is topped up to, never exceeded.
 * @param[in]     dt         Timestep in seconds.
 * @note A pickup is only taken if it would actually help -- a medkit is left
 *       on the floor at full health so you can come back for it -- which is
 *       also the rule pickuptest checks.
 * @note Overlap is tested horizontally against ::PICKUP_RADIUS plus a
 *       vertical tolerance, so a pickup on a different floor of a stacked
 *       sector is not collected through the ceiling.
 *
 * 한국어
 * ------
 * @brief 위아래 움직임 애니메이션을 진행시키고, 플레이어와 겹친 아이템을 획득합니다.
 * @param[in]     player_eye 플레이어의 눈 위치. 발 위치는 내부에서 계산됩니다.
 * @param[in,out] health     구급상자 획득 시 값을 읽고 직접 수정합니다.
 * @param[in]     health_max 체력 회복의 상한값. 이 값을 초과하지 않습니다.
 * @param[in,out] ammo       탄약 상자 획득 시 값을 읽고 직접 수정합니다.
 * @param[in]     ammo_max   탄약 보충의 상한값. 이 값을 초과하지 않습니다.
 * @param[in]     dt         시간 간격 (초).
 * @note 아이템은 실제로 도움이 될 때만 획득됩니다. 체력이 가득 찬 상태에서는
 *       구급상자가 바닥에 남아 나중에 다시 가지러 올 수 있으며, 이는
 *       pickuptest가 검증하는 규칙이기도 합니다.
 * @note 겹침 판정은 ::PICKUP_RADIUS를 기준으로 한 수평 판정과 수직 허용 범위를
 *       함께 사용하므로, 층이 겹친 섹터에서 다른 층의 아이템을 천장 너머로
 *       획득하지 않습니다.
 */
void pickup_update(Pools *pl, v3 player_eye, int *health, int health_max,
                   Weapon *w, int *keys, float dt);

/**
 * @brief Resolves a pickup name to its ::PK_ kind, for a name that is not
 *        NUL-terminated.
 *
 * ENGLISH
 * -------
 * @param[in] k   First character of the name.
 * @param[in] len How many characters it has.
 * @return A PK_* kind, or -1 when the name names no pickup.
 * @note Exposed so the sprite decoder can address a pickup cell by the SAME
 *       name a level uses to place one. A second table mapping drawings to
 *       kinds would be a list that agrees with this one until somebody adds a
 *       pickup, and then quietly does not.
 * @note The vocabulary is `health`, `ammo`, `<weapon>ammo`, `<weapon>`, and
 *       `redkey`/`bluekey`/`yellowkey`. Most of it is derived from the weapon
 *       table rather than listed, so a new weapon brings its box and its
 *       floor item with it.
 *
 * 한국어
 * ------
 * @brief NUL로 끝나지 않는 이름을 ::PK_ 종류로 해석합니다.
 * @param[in] k   이름의 첫 문자.
 * @param[in] len 이름의 길이.
 * @return PK_* 종류, 또는 어떤 아이템도 가리키지 않으면 -1.
 * @note 스프라이트 디코더가 레벨이 아이템을 배치할 때 쓰는 것과 *같은* 이름으로 아이템
 *       셀을 지정할 수 있도록 노출합니다. 그림을 종류에 대응시키는 두 번째 표는 누군가
 *       아이템을 추가하기 전까지만 이것과 일치하다가 조용히 어긋나는 목록이 됩니다.
 */
int pickup_kind_for_n(const char *k, int len);

#endif
