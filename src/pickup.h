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
#include "player.h"  /* PowerKind and PLAYER_POWER_TIME: an artifact is a clock */

/* --- Macros and constants / 매크로 및 상수 --- */

#define PICKUP_MAX     64       ///< @brief Maximum pickups tracked per level. / 레벨당 관리되는 최대 아이템 수.
#define PICKUP_RADIUS  0.75f    ///< @brief How close your feet must get, metres. / 발이 얼마나 가까워져야 하는지 (미터).
/* PICKUP_AMMO WAS HERE and said "shells per ammo box". It has not been true
   since ammo boxes learned to give different amounts: ::pickup_take reads
   ::WeaponStats::start_ammo, a column, so a shotgun box and a grenade box give
   what their own rows say. A constant nobody reads costs nothing; one that
   states a rule the code no longer follows costs the next person the time to
   discover it is a lie. ::PICKUP_HEALTH beside it is still read and stays.
   *PICKUP_AMMO이 이곳에 있었고* "탄약 상자 하나당 탄환 수"라고 말했습니다. 탄약 상자가 서로
   다른 양을 주게 된 이래 참이 아닙니다. ::pickup_take는 열인 ::WeaponStats::start_ammo를
   읽으므로 샷건 상자와 유탄 상자는 자기 행이 말하는 것을 줍니다. 아무도 읽지 않는 상수는
   비용이 없지만, 코드가 더 이상 따르지 않는 규칙을 진술하는 상수는 다음 사람에게 그것이
   거짓임을 알아내는 시간을 물립니다. 곁의 ::PICKUP_HEALTH는 여전히 읽히므로 남습니다. */
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

    /* One per ::PowerKind, in the same order, so a kind converts to a
       ::PowerKind by subtracting rather than by a table -- the arrangement
       ::PK_KEY0 already uses for the keys. A fourth artifact is a value in
       ::PowerKind, a name below and a drawing in sprite.c.
       ::PowerKind마다 하나이며 순서도 같으므로, 종류를 ::PowerKind로 바꾸는 것이
       표가 아니라 뺄셈입니다. ::PK_KEY0이 열쇠에 대해 이미 쓰는 배치입니다. 네
       번째 아티팩트는 ::PowerKind의 값 하나, 아래의 이름 하나, 그리고 sprite.c의
       그림 하나가 전부입니다. */
    PK_POWER0,
    PK_POWER_LAST = PK_POWER0 + PW_KINDS - 1,

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

    /**
     * @brief Velocity while it is still in the air; zero once it has landed.
     *
     * ENGLISH
     * -------
     * ZERO IS THE ORDINARY CASE and costs nothing. An item the level laid out
     * has never moved and never will, so it starts at zero and ::pickup_update
     * skips the whole of the flight path for it. Only ::pickup_toss ever writes
     * a non-zero one.
     *
     * Doubling as the "am I flying" flag rather than carrying a separate one:
     * a pickup with velocity is in the air by definition, and a second field
     * saying so is a second thing that can disagree with the first.
     *
     * 한국어
     * ------
     * @brief 아직 공중에 있는 동안의 속도. 착지하면 0입니다.
     *
     * 0이 평범한 경우이며 비용이 들지 않습니다. 레벨이 배치한 아이템은 움직인 적도 없고 앞으로도
     * 없으므로 0에서 시작하며, ::pickup_update가 그런 아이템에 대해서는 비행 경로 전체를
     * 건너뜁니다. 0이 아닌 값을 쓰는 것은 ::pickup_toss뿐입니다.
     *
     * 별도의 플래그를 두는 대신 "날고 있는가"를 겸합니다. 속도를 가진 아이템은 정의상 공중에
     * 있으며, 그렇다고 말하는 두 번째 필드는 첫 번째와 어긋날 수 있는 두 번째 것입니다.
     */
    v3    vel;

    /**
     * @brief Seconds of "I just arrived" left on it. Zero for a level's own items.
     *
     * ENGLISH
     * -------
     * The other half of ::vel doubling as the flight flag, and the same trick:
     * ZERO IS THE ORDINARY CASE. An item the level laid out has always been
     * there, so it starts at zero and announces nothing; only ::pickup_toss
     * ever sets it, because only a tossed item is one the player has to be told
     * about.
     *
     * HELD, NOT COUNTED DOWN, WHILE IT IS STILL FLYING. The hurried rate is
     * supposed to still be running when the item lands -- that is the frame the
     * player looks at it -- and an arc lasting most of ::LootMote::hold would
     * otherwise spend the whole announcement in mid-air and settle on arrival.
     *
     * A DURATION AND NOT A FLAG, so that shortening ::LootMote::hold in the
     * file cuts short the items already on the floor rather than only the next
     * one to land, which is exactly the edit an author is watching for.
     *
     * 한국어
     * ------
     * @brief "방금 도착했다"가 남아 있는 시간(초). 레벨 자신의 아이템은 0입니다.
     *
     * ::vel이 비행 플래그를 겸하는 것의 나머지 절반이며 같은 수법입니다. *0이 평범한
     * 경우입니다.* 레벨이 배치한 아이템은 늘 그 자리에 있었으므로 0에서 시작하고 아무것도
     * 알리지 않습니다. 이것을 설정하는 것은 ::pickup_toss뿐인데, 플레이어에게 알려야 하는
     * 것은 던져진 아이템뿐이기 때문입니다.
     *
     * 아직 날고 있는 동안에는 줄지 않고 *유지됩니다*. 서둘러 내보내는 속도는 아이템이 착지할
     * 때에도 여전히 돌고 있어야 하며(그 프레임이 플레이어가 그것을 보는 프레임입니다),
     * 그러지 않으면 ::LootMote::hold의 대부분을 차지하는 포물선이 알림 전체를 공중에서
     * 소진하고 도착하는 순간 잦아들게 됩니다.
     *
     * 플래그가 아니라 *지속 시간*이므로, 파일에서 ::LootMote::hold를 줄이면 다음에 착지할
     * 아이템뿐 아니라 바닥에 이미 있는 아이템들도 함께 짧아집니다. 그것이야말로 제작자가
     * 지켜보고 있는 편집입니다.
     */
    float flare;

    /**
     * @brief Seconds until this item gives off its next mote.
     *
     * ENGLISH
     * -------
     * The countdown ::pickup_update paces `itemmote` off, the same arrangement
     * world.c has with the lava smoke: one particle per spawn and a timer out
     * here deciding how often, because ::fx_spawn only knows how to be a burst
     * and what an item needs is a trickle that is still going in five seconds.
     *
     * PER ITEM RATHER THAN ONE CLOCK FOR THE POOL, because a single timer makes
     * every item on the floor emit on the same frame -- twelve specks appearing
     * together and then nothing, which reads as the room blinking rather than
     * as twelve items. Staggered at birth and never resynchronised after.
     *
     * IT KEEPS RUNNING OUT OF RANGE, and the spawn is what is skipped. A timer
     * paused past ::LootMote::range would hand its whole backlog over the
     * instant the player walked close enough, which is a puff of specks with no
     * cause -- and holding at zero instead would emit on the first frame in
     * range for every item at once, which is the blink again.
     *
     * 한국어
     * ------
     * @brief 이 아이템이 다음 티끌을 내보내기까지의 시간(초).
     *
     * ::pickup_update가 `itemmote`를 조절해 뿌리는 카운트다운이며, world.c가 용암 연기와
     * 맺는 것과 같은 배치입니다. 생성당 입자 하나이고 얼마나 자주인지는 바깥의 이 타이머가
     * 정합니다. ::fx_spawn은 폭발이 될 줄만 알지만 아이템에게 필요한 것은 5초 뒤에도
     * 이어지고 있는 흐름이기 때문입니다.
     *
     * *풀 전체의 시계 하나가 아니라 아이템마다인* 이유는, 타이머가 하나면 바닥의 모든
     * 아이템이 같은 프레임에 내보내기 때문입니다. 알갱이 열둘이 함께 나타났다가 아무것도
     * 없는 것은 아이템 열둘이 아니라 방이 깜박이는 것으로 읽힙니다. 태어날 때 어긋나게 하고
     * 그 뒤로 다시 맞추지 않습니다.
     *
     * *사거리 밖에서도 계속 돌며*, 건너뛰는 것은 생성입니다. ::LootMote::range 밖에서 멈춘
     * 타이머는 플레이어가 충분히 가까이 걸어온 순간 밀린 것을 통째로 내놓는데, 그것은 원인
     * 없는 알갱이 뭉치입니다. 대신 0에서 붙들어 두면 사거리에 든 첫 프레임에 모든 아이템이
     * 한꺼번에 내보내며, 그것은 다시 깜박임입니다.
     */
    float mote;

    int   active;      /**< 0 once collected. / 획득되면 0이 됩니다. */
} Pickup;

/* --- what a tossed item does / 던져진 아이템이 하는 일 ---------------------- */

/** @brief Downward acceleration on an item in the air, m/s^2. / 공중의 아이템에 걸리는 하향 가속도. */
#define PICKUP_GRAVITY  16.0f
/* PICKUP_TOSS_OUT WAS HERE and named an outward speed nothing leaves at. The
   two throws in the game both ignored it: ::reward_items takes `out` from
   loot.txt so an author can shape the ring, and a monster's drop is
   `v3f(0, PICKUP_TOSS_UP * 0.55f, 0)` -- straight up, no outward component at
   all. That second one is why a corpse and the thing it dropped land on each
   other, which ::PICKUP_NUDGE now pushes apart at the drawing end. Giving
   drops a spread would fix it at the throwing end instead; that is a change to
   how the game moves and not a tidy-up, so what goes here is the constant and
   not the behaviour.
   *PICKUP_TOSS_OUT이 이곳에 있었고* 아무것도 그 속도로 떠나지 않는 바깥 방향 속도에 이름을
   붙였습니다. 게임의 두 던지기가 모두 그것을 무시했습니다. ::reward_items는 제작자가 고리를
   빚을 수 있도록 loot.txt에서 `out`을 가져오고, 몬스터의 드롭은
   `v3f(0, PICKUP_TOSS_UP * 0.55f, 0)`입니다. 곧장 위이며 바깥 성분이 아예 없습니다. 시체와
   그것이 떨군 물건이 서로 위에 놓이는 이유가 그 두 번째이고, 지금은 ::PICKUP_NUDGE가 그리는
   쪽에서 밀어냅니다. 드롭에 퍼짐을 주면 던지는 쪽에서 고쳐지겠지만, 그것은 게임이 움직이는
   방식에 대한 변경이지 정리가 아닙니다. 그래서 이곳에서 사라지는 것은 동작이 아니라
   상수입니다. */
/** @brief Upward speed it leaves at, m/s. / 떠날 때의 상승 속도. */
#define PICKUP_TOSS_UP  4.2f

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
void pickup_update(Pools *pl, const Level *l, v3 player_eye,
                   int *health, int health_max,
                   Weapon *w, int *keys, float *power, float dt);

/**
 * @brief Throws an item into the air from a point, to land and be collected.
 *
 * ENGLISH
 * -------
 * Diablo's drop, and the reason it is worth the arc: an item that simply
 * APPEARS on the floor has to be noticed, and one that is thrown has already
 * been noticed by the time it lands. In an arena, where the reward arrives at
 * the same moment the room goes quiet, that difference is the whole of whether
 * the player knows they were paid.
 *
 * @param[in,out] pl   Pools to place it in.
 * @param[in]     kind One of the PK_* constants.
 * @param[in]     from Where the throw starts, feet-height metres.
 * @param[in]     vel  How it leaves, m/s. Zero simply places it.
 * @return Non-zero if it was placed.
 *
 * @note REUSES A COLLECTED SLOT before growing the array. A level lays its
 *       items out once and only ever loses them, so the pool was written to
 *       fill and stay filled; an arena rewards every wave and would reach
 *       ::PICKUP_MAX in a dozen of them. A hole is what a collected item leaves
 *       and it is exactly the right size.
 * @note Raises ::DIAG_PICKUP_CAP and places nothing when there is no room, so a
 *       reward that silently did not arrive is counted rather than wondered
 *       about.
 * @warning Where it LANDS is found by ::level_ground from the point it reaches,
 *          so an item thrown off a ledge lands under the ledge rather than in
 *          the air. It is not swept: it passes through walls on the way out.
 *          The throw is short and the drop point is where the player is
 *          standing, which is the case that has floor under it.
 *
 * 한국어
 * ------
 * @brief 아이템을 한 지점에서 공중으로 던져, 떨어진 뒤 획득되게 합니다.
 *
 * 디아블로의 드롭이며, 포물선이 값어치를 하는 이유입니다. 그냥 바닥에 *나타나는* 아이템은
 * 알아채져야 하지만, 던져진 아이템은 떨어질 무렵 이미 알아채진 상태입니다. 방이 조용해지는
 * 바로 그 순간 보상이 도착하는 아레나에서, 그 차이가 플레이어가 자신이 보상받았음을 아는지
 * 여부의 전부입니다.
 *
 * @param[in,out] pl   아이템을 놓을 풀.
 * @param[in]     kind PK_* 상수 중 하나.
 * @param[in]     from 던지기가 시작되는 지점. 발 높이 기준 미터.
 * @param[in]     vel  떠나는 속도 (m/s). 0이면 그냥 놓습니다.
 * @return 놓였으면 0이 아닙니다.
 *
 * @note 배열을 늘리기 전에 *획득된 슬롯을 재사용합니다.* 레벨은 아이템을 한 번 배치하고 잃기만
 *       하므로 풀은 채워진 뒤 그대로 유지되도록 작성되었습니다. 그러나 아레나는 웨이브마다
 *       보상하며 열몇 번이면 ::PICKUP_MAX에 닿습니다. 획득된 아이템이 남기는 구멍이 정확히
 *       알맞은 크기입니다.
 * @note 자리가 없으면 ::DIAG_PICKUP_CAP을 올리고 아무것도 놓지 않으므로, 조용히 도착하지 않은
 *       보상은 궁금해할 대상이 아니라 세어지는 대상이 됩니다.
 * @warning *착지 지점*은 도달한 지점에서 ::level_ground로 찾으므로, 난간 밖으로 던져진 아이템은
 *          공중이 아니라 난간 아래에 떨어집니다. 스윕하지는 않습니다. 나가는 도중에는 벽을
 *          통과합니다. 던지기는 짧고 낙하 지점은 플레이어가 서 있는 자리이며, 그곳은 아래에
 *          바닥이 있는 경우입니다.
 */
int pickup_toss(Pools *pl, int kind, v3 from, v3 vel);

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
