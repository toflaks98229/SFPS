/**
 * @file loot.h
 * @brief Every number about an item ARRIVING, in one hot-reloadable file.
 *
 * ENGLISH
 * -------
 * The odds a kill leaves something behind, what a cleared wave pays, the spot
 * it is paid at, and how insistently a fresh item announces itself. All of it
 * comes from assets\loot.txt, which means retuning a drop rate is saving a
 * text file rather than editing a constant in three modules and rebuilding.
 *
 * WHY THESE FOUR THINGS ARE ONE FILE. They are not four subjects; they are one
 * question asked at four moments -- "did the player see that they were paid".
 * A drop rate tuned without the specks that show the drop is a rate nobody can
 * measure by playing, and a reward thrown at a spot with nothing marking it is
 * a rate that reads as zero. Splitting them across enemy.c, world.h and
 * scene.c is exactly how they came to disagree.
 *
 * NO GL AND NO POOLS. This module answers questions and owns nothing: enemy.c
 * rolls the die with its own RNG and calls ::pickup_toss, world.c reads the
 * reward and throws it, pickup.c reads the pacing and spawns the specks. That
 * is what lets
 * tools\loottest.c check a table without a window, and what keeps a demo
 * replaying identically -- the randomness stays where it already was.
 *
 * 한국어
 * ------
 * @brief 아이템이 *도착하는* 것에 관한 모든 수치를, 핫 리로드되는 파일 하나에 모읍니다.
 *
 * 처치가 무언가를 남길 확률, 정리된 웨이브가 지급하는 것, 그것이 지급되는 자리, 그리고
 * 갓 놓인 아이템이 자신을 얼마나 집요하게 알리는지입니다. 전부 assets\loot.txt에서 오므로,
 * 드롭 확률 조정은 세 모듈의 상수를 고치고 재빌드하는 일이 아니라 텍스트 파일을 저장하는
 * 일입니다.
 *
 * 왜 이 넷이 한 파일인가. 이것들은 네 개의 주제가 아니라 네 순간에 던져지는 하나의
 * 질문입니다. "플레이어가 자신이 보상받았음을 보았는가"입니다. 드롭을 보여 주는 알갱이
 * 없이 조정된 드롭 확률은 플레이로는 측정할 수 없는 확률이고, 아무 표식도 없는 자리에
 * 던져진 보상은 0으로 읽히는 확률입니다. 이것들을 enemy.c와 world.h와 scene.c에 나누어
 * 둔 것이 바로 그것들이 서로 어긋나게 된 경위입니다.
 *
 * GL도 Pools도 없습니다. 이 모듈은 질문에 답할 뿐 아무것도 소유하지 않습니다. enemy.c가
 * 자기 난수로 주사위를 굴려 ::pickup_toss를 부르고, world.c가 보상을 읽어 던지고,
 * pickup.c가 빈도를 읽어 알갱이를 생성합니다. 그 덕분에 tools\loottest.c가 창 없이 표를 검사할 수
 * 있고, 데모가 동일하게 재생됩니다. 난수가 원래 있던 자리에 그대로 있기 때문입니다.
 */
#ifndef LOOT_H
#define LOOT_H

/* Weapon and the PK_* kinds: a table names items by the same names a level
   places them under, and `held` is answered against a roster.
   Weapon과 PK_* 종류입니다. 표는 레벨이 배치할 때 쓰는 것과 같은 이름으로 아이템을
   가리키며, `held`는 보유 목록에 비추어 답해집니다. */
#include "pickup.h"

/* --- Macros and constants / 매크로 및 상수 --- */

/** @brief Items one table may list. / 한 표가 나열할 수 있는 아이템 수. */
#define LOOT_ENTRIES 8
/** @brief Monster kinds that may carry a table. / 표를 가질 수 있는 몬스터 종류의 수. */
#define LOOT_TABLES  8

/**
 * @brief Items one cleared wave may throw.
 *
 * ENGLISH: A ceiling on `give`, not on the file's entries: `give health 200`
 * is a typo and this is what stops it emptying ::PICKUP_MAX and taking every
 * item already on the floor with it. The surplus is dropped and
 * ::DIAG_PICKUP_CAP is what the caller raises, so a purse that did not
 * entirely arrive is counted rather than wondered about.
 *
 * 한국어: 파일의 항목 수가 아니라 `give`에 대한 상한입니다. `give health 200`은 오타이며,
 * 이것이 그 오타가 ::PICKUP_MAX를 비우고 바닥에 이미 있던 아이템까지 함께 쓸어 가는 것을
 * 막습니다. 초과분은 버려지고 호출자가 ::DIAG_PICKUP_CAP을 올리므로, 온전히 도착하지 않은
 * 몫은 궁금해할 대상이 아니라 세어지는 대상이 됩니다.
 */
#define LOOT_REWARD_MAX 16

/**
 * @brief The pseudo-kind `held`: ammo for a weapon the player actually owns.
 *
 * ENGLISH
 * -------
 * Not a ::PK_ value, because which box it means depends on the player and this
 * module does not have one. The caller resolves it -- see ::loot_held_kind --
 * at the moment of the drop, which is the only moment the answer is known.
 *
 * Negative so it can never collide with a real kind, and distinct from the -1
 * that ::loot_pick returns for "nothing", so a caller cannot confuse an empty
 * roll with an unresolved one.
 *
 * 한국어
 * ------
 * @brief 의사 종류 `held`. 플레이어가 실제로 보유한 무기의 탄약입니다.
 *
 * ::PK_ 값이 아닙니다. 어느 상자를 뜻하는지가 플레이어에 달려 있는데 이 모듈에는
 * 플레이어가 없기 때문입니다. 호출자가 드롭하는 순간에 해석하며(::loot_held_kind 참조),
 * 그 순간이 답을 알 수 있는 유일한 때입니다.
 *
 * 음수여서 실제 종류와 결코 충돌하지 않고, ::loot_pick이 "없음"으로 돌려주는 -1과도
 * 구분되므로 호출자가 빈 굴림과 미해석 굴림을 혼동할 수 없습니다.
 */
#define LOOT_HELD (-2)

/* --- Enumerations / 열거형 --- */

/**
 * @brief Where a cleared wave's reward lands.
 *
 * 한국어: 정리된 웨이브의 보상이 놓이는 자리입니다.
 */
enum LootAt {
    LOOT_AT_PLAYER = 0,  /**< The player's own feet. Diablo's drop. / 플레이어 자신의 발치. 디아블로의 드롭. */
    LOOT_AT_ALTAR,       /**< An `altar` entity the map placed. / 맵이 배치한 `altar` 엔티티. */
    LOOT_AT_CENTRE       /**< The average of the arena's spawners. / 아레나 스포너들의 평균 지점. */
};

/* --- Type definitions / 타입 정의 --- */

/**
 * @struct LootItem
 * @brief One line of a table: which item, and how much of it counts.
 *
 * ENGLISH: `n` is a WEIGHT in a drop table and a COUNT in the reward, and the
 * two share a struct because they share every other property. The file says
 * which it is by using `item` or `give`; nothing downstream has to ask.
 *
 * 한국어: `n`은 드롭 표에서는 *가중치*이고 보상에서는 *개수*입니다. 나머지 성질이 모두
 * 같으므로 구조체를 공유합니다. 어느 쪽인지는 파일이 `item`과 `give`로 말하며, 그
 * 아래쪽의 무엇도 물어볼 필요가 없습니다.
 */
typedef struct {
    int kind;   /**< A PK_* constant, or ::LOOT_HELD. / PK_* 상수, 또는 ::LOOT_HELD. */
    int n;      /**< Weight in a drop table, count in the reward. / 드롭 표에서는 가중치, 보상에서는 개수. */
} LootItem;

/**
 * @struct LootDrop
 * @brief One monster kind's table: how often, and then which.
 *
 * ENGLISH
 * -------
 * TWO ROLLS AND NOT ONE, deliberately. `chance` decides whether anything is
 * left behind at all and the weights decide what; folding them into a single
 * weighted table with an implicit "nothing" entry would mean that making a
 * monster drop medkits more often also made it drop everything more often. An
 * author retuning one number should be retuning one thing.
 *
 * 한국어
 * ------
 * @brief 몬스터 한 종류의 표입니다. 얼마나 자주인가, 그리고 무엇인가.
 *
 * 의도적으로 한 번이 아니라 *두 번* 굴립니다. `chance`가 무언가를 남기는지 자체를 정하고
 * 가중치가 무엇인지를 정합니다. 암묵적인 "없음" 항목을 둔 하나의 가중 표로 합치면, 어떤
 * 몬스터가 구급상자를 더 자주 떨어뜨리게 만드는 일이 곧 그 몬스터가 *모든 것*을 더 자주
 * 떨어뜨리게 만드는 일이 됩니다. 숫자 하나를 조정하는 제작자는 하나만 조정해야 합니다.
 */
typedef struct {
    int      chance;              /**< Percent of kills that leave anything, 0..100. / 무언가를 남기는 처치의 비율(퍼센트). */
    LootItem item[LOOT_ENTRIES];  /**< Weighted entries. / 가중치가 붙은 항목들. */
    int      n_items;             /**< How many are filled. / 채워진 개수. */
    int      weight;              /**< Sum of item[].n, so ::loot_pick need not re-add it. / item[].n의 합. ::loot_pick이 다시 더할 필요가 없게 합니다. */
} LootDrop;

/**
 * @struct LootReward
 * @brief What a cleared wave pays, and where.
 *
 * 한국어: 정리된 웨이브가 지급하는 것과 그 자리입니다.
 */
typedef struct {
    LootItem item[LOOT_ENTRIES];  /**< Counted entries. / 개수가 붙은 항목들. */
    int      n_items;             /**< How many are filled. / 채워진 개수. */
    int      at;                  /**< A ::LootAt. / ::LootAt 값. */
    float    out;                 /**< Outward speed of the ring, m/s. / 고리의 바깥 방향 속도(m/s). */
    float    up;                  /**< Upward speed, m/s. / 상승 속도(m/s). */
    float    altar;               /**< Seconds the drop point burns as a shrine; 0 for none. / 낙하 지점이 제단으로 타오르는 시간(초). 0이면 없음. */
} LootReward;

/**
 * @struct LootMote
 * @brief How often a floor item gives off a speck, in the units the emitter wants.
 *
 * ENGLISH
 * -------
 * FOUR TIMES AND A DISTANCE, and not one size or brightness among them. That
 * is deliberate and it is the shape of the decision: an item used to carry an
 * additive halo and a dynamic light, tuned by how big and how bright, and both
 * made it into a LAMP -- which is the language this game already spends on
 * bolts and muzzle flash, on the things that are about to hurt you. What
 * replaced them is `itemmote` rising off it, and the only thing left to tune
 * is how often. A struct that cannot express a size cannot drift back.
 *
 * Converted out of the file's integers ONCE, here, rather than at every tick.
 * The file talks in milliseconds and centimetres because that is what an author
 * can type; ::pickup_update wants seconds and metres, and a conversion written
 * per frame is a conversion that gets written differently in two places.
 *
 * 한국어
 * ------
 * @brief 바닥 아이템이 얼마나 자주 알갱이를 내보내는지. 내보내는 쪽이 원하는 단위입니다.
 *
 * *네 개의 시간과 하나의 거리*이며, 그중에 크기도 밝기도 없습니다. 의도한 것이고 그 결정의
 * 형태 자체입니다. 아이템은 가산 헤일로와 동적 광원을 지녔고 얼마나 크고 얼마나 밝은지로
 * 조정되었으며, 둘 다 아이템을 *등*으로 만들었습니다. 그것은 이 게임이 이미 볼트와 총구
 * 섬광에, 즉 당신을 아프게 할 것들에 쓰고 있는 언어입니다. 그것들을 대체한 것은 아이템에서
 * 피어오르는 `itemmote`이며, 조정할 것으로 남은 것은 얼마나 자주인가뿐입니다. 크기를
 * 표현할 수 없는 구조체는 그쪽으로 되돌아갈 수 없습니다.
 *
 * 파일의 정수를 매 틱이 아니라 이곳에서 *한 번* 변환합니다. 파일이 밀리초와 센티미터로
 * 말하는 것은 그것이 제작자가 칠 수 있는 단위이기 때문이고, ::pickup_update가 원하는 것은
 * 초와 미터입니다. 프레임마다 쓰이는 변환은 두 곳에서 서로 다르게 쓰이게 되는 변환입니다.
 */
typedef struct {
    float rate;    /**< Seconds between motes once it has settled. / 안정된 뒤 티끌 사이의 시간(초). */
    float hurry;   /**< Seconds between them while it is still announcing itself. / 아직 자신을 알리는 동안의 간격(초). */
    float hold;    /**< Seconds `hurry` lasts, from the moment it arrives. / 도착한 순간부터 `hurry`가 지속되는 시간(초). */
    float range;   /**< Metres past which an item emits nothing. / 이 거리 밖에서는 아무것도 내보내지 않습니다(미터). */
} LootMote;

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief The drop table for a monster kind, never NULL.
 *
 * ENGLISH
 * -------
 * @param[in] mon_type A ::MonTypeID.
 * @return A borrowed pointer to the table, valid until the next ::loot_reload.
 * @note A kind the file says nothing about gets an EMPTY table -- `chance` 0,
 *       no entries -- rather than a NULL, so a caller may roll against it
 *       without testing first. "This monster drops nothing" is an answer, not
 *       a missing one.
 * @note An out-of-range id gets the same empty table.
 *
 * 한국어
 * ------
 * @brief 몬스터 종류의 드롭 표이며 결코 NULL이 아닙니다.
 * @param[in] mon_type ::MonTypeID 값.
 * @return 표에 대한 참조 포인터. 다음 ::loot_reload까지 유효합니다.
 * @note 파일이 아무 말도 하지 않은 종류는 NULL이 아니라 *빈* 표(`chance` 0, 항목 없음)를
 *       받으므로, 호출자가 먼저 검사하지 않고 굴려도 됩니다. "이 몬스터는 아무것도
 *       떨어뜨리지 않는다"는 답이지 답이 없는 것이 아닙니다.
 * @note 범위를 벗어난 식별자도 같은 빈 표를 받습니다.
 */
const LootDrop *loot_drop(int mon_type);

/**
 * @brief What a cleared wave pays, never NULL.
 *
 * 한국어: 정리된 웨이브가 지급하는 것이며 결코 NULL이 아닙니다.
 */
const LootReward *loot_reward(void);

/** @brief How many times the wave purse a boss pays, when loot.txt says nothing. / loot.txt가 아무 말도 하지 않을 때 보스가 웨이브 지갑의 몇 배를 지급하는가. */
#define LOOT_BOSS_MULTIPLE 3

/** @brief Seconds the boss's shrine burns by default. / 기본값에서 보스의 제단이 타는 시간(초). */
#define LOOT_BOSS_ALTAR 12.0f

/**
 * @brief What a beaten boss pays: loot.txt's `b` block.
 *
 * ENGLISH
 * -------
 * @return The boss purse. Never null; defaults to ::LOOT_BOSS_MULTIPLE times
 *         the wave reward when the file opens no `b`.
 *
 * @note Same type as ::loot_reward's, deliberately -- a boss's purse and a
 *       wave's differ in their numbers, not in what a purse IS, and world.c
 *       throws both through one function because of it.
 * @note The default is DERIVED from the wave reward rather than written out, so
 *       retuning `r` and forgetting `b` cannot produce a boss that pays less
 *       than the wave before it.
 *
 * 한국어
 * ------
 * @brief 쓰러진 보스가 지급하는 것. loot.txt의 `b` 블록입니다.
 * @return 보스 지갑. 결코 null이 아니며, 파일이 `b`를 열지 않으면 웨이브 보상의
 *         ::LOOT_BOSS_MULTIPLE배가 기본값입니다.
 *
 * @note ::loot_reward의 것과 같은 타입이며 의도적입니다. 보스의 지갑과 웨이브의 지갑은 숫자가
 *       다를 뿐 지갑이 *무엇인가*가 다르지 않으며, 그래서 world.c가 둘을 하나의 함수로
 *       던집니다.
 * @note 기본값은 적어 넣은 것이 아니라 웨이브 보상에서 *유도*되므로, `r`을 다시 조율하고 `b`를
 *       잊어도 직전 웨이브보다 적게 지급하는 보스가 나올 수 없습니다.
 */
const LootReward *loot_boss_reward(void);

/**
 * @brief How often every floor item gives off a speck, never NULL.
 *
 * 한국어: 모든 바닥 아이템이 얼마나 자주 알갱이를 내보내는지이며 결코 NULL이 아닙니다.
 */
const LootMote *loot_mote(void);

/**
 * @brief Rolls one table and says what fell out.
 *
 * ENGLISH
 * -------
 * @param[in] d      A table from ::loot_drop.
 * @param[in] chance A roll in [0,1) against ::LootDrop::chance.
 * @param[in] which  A second roll in [0,1) against the weights.
 * @return A PK_* kind, ::LOOT_HELD, or -1 when nothing fell out.
 *
 * @note THE ROLLS ARE THE CALLER'S. This module owns no randomness, because
 *       the randomness it would own is the one thing that must not be new:
 *       enemy.c already has a pool-seeded generator that a recorded demo
 *       replays exactly, and a second one here would make a demo diverge at
 *       the first kill. See ::EnemyPool::rng.
 * @note Two rolls rather than one, matching ::LootDrop's own note: the caller
 *       may spend the second even when the first fails, and often will,
 *       because taking both up front keeps the RNG advancing by a fixed amount
 *       per kill -- which is what makes a demo replay through a rate change.
 *
 * 한국어
 * ------
 * @brief 표 하나를 굴려 무엇이 나왔는지 말합니다.
 * @param[in] d      ::loot_drop이 준 표.
 * @param[in] chance ::LootDrop::chance에 맞설 [0,1)의 굴림.
 * @param[in] which  가중치에 맞설 두 번째 [0,1)의 굴림.
 * @return PK_* 종류, ::LOOT_HELD, 또는 아무것도 나오지 않았으면 -1.
 *
 * @note *굴림은 호출자의 것입니다.* 이 모듈은 난수를 소유하지 않습니다. 소유하게 될 난수야말로
 *       새것이어서는 안 되는 유일한 것이기 때문입니다. enemy.c에는 기록된 데모가 정확히
 *       재생하는 풀 기반 생성기가 이미 있으며, 이곳의 두 번째 생성기는 첫 처치에서 데모를
 *       어긋나게 만듭니다. ::EnemyPool::rng를 참조하십시오.
 */
int loot_pick(const LootDrop *d, float chance, float which);

/**
 * @brief Resolves ::LOOT_HELD against a player's actual roster.
 *
 * ENGLISH
 * -------
 * @param[in]     w   The player's weapons, read for `owned`.
 * @param[in,out] gun Round-robin cursor. Pass the same one across a purse and
 *                    a player holding three guns is fed all three; pass a fresh
 *                    zero and every box is for the first weapon they own.
 * @return A PK_AMMO_FOR kind, or -1 when they hold nothing that takes ammo.
 *
 * @note THE ROUND ROBIN IS THE POINT. A box for a gun you have not found is
 *       the one thing ::pickup_update refuses to collect -- it would lie on the
 *       floor for the rest of the run looking like something you had missed --
 *       and three boxes for the first gun you own is the same failure spread
 *       over the belt you already filled.
 * @note Here rather than in world.c because enemy.c needs the same answer for
 *       ::LOOT_HELD in a drop table, and two spellings of "which gun is this
 *       for" is how the wave and the corpse come to disagree.
 *
 * 한국어
 * ------
 * @brief ::LOOT_HELD를 플레이어의 실제 보유 무기에 비추어 해석합니다.
 * @param[in]     w   플레이어의 무기. `owned`를 읽습니다.
 * @param[in,out] gun 돌아가며 채우는 커서. 한 몫 전체에 같은 것을 넘기면 총 세 자루를 든
 *                    플레이어가 셋 모두를 받고, 매번 0을 새로 넘기면 모든 상자가 그들이
 *                    보유한 첫 무기의 것이 됩니다.
 * @return PK_AMMO_FOR 종류. 탄약을 쓰는 무기를 하나도 들고 있지 않으면 -1.
 *
 * @note *돌아가며 채우는 것이 요점입니다.* 찾지 못한 총의 상자는 ::pickup_update가 획득을
 *       거부하는 유일한 것이며, 플레이가 끝날 때까지 바닥에 남아 놓친 무언가처럼 보입니다.
 *       그리고 보유한 첫 무기의 상자 세 개는 이미 채운 탄띠에 같은 실패를 펼쳐 놓은 것입니다.
 */
int loot_held_kind(const Weapon *w, int *gun);

/**
 * @brief Re-reads assets\loot.txt. Called by ::data_poll's consumers.
 *
 * 한국어: assets\loot.txt를 다시 읽습니다. ::data_poll의 소비자가 호출합니다.
 */
void loot_reload(void);

#endif
