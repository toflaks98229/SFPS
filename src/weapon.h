/**
 * @file weapon.h
 * @brief Hitscan shotgun and Meat Hook: firing, recoil, grappling, view model and effects.
 *
 * ENGLISH
 * -------
 * The gun is built from the same Box primitive as the level -- roughly half
 * of its parts are stored once and mirrored across x=0 by the engine.
 *
 * Two systems share this module because they push the player the same way:
 * both add to a `v3 *player_vel` the caller owns rather than reporting an
 * event back for main.c to act on. A shot's recoil kick and the hook's pull
 * are two things doing the same kind of push.
 *
 * The hook functions take the level as an explicit parameter and touch no GL,
 * so tools/hooktest.c drives all four beats without ever creating a
 * context.
 *
 * 한국어
 * ------
 * 총기는 레벨과 동일한 Box 기본 도형으로 제작됩니다. 부품의 약 절반은 한 번만
 * 저장되고 엔진이 x=0을 기준으로 대칭 복사합니다.
 *
 * 두 시스템이 이 모듈을 공유하는 이유는 플레이어를 밀어내는 방식이 동일하기
 * 때문입니다. 양쪽 모두 main.c가 처리하도록 이벤트를 보고하는 대신, 호출자가
 * 소유한 `v3 *player_vel`에 직접 값을 더합니다. 사격의 반동과 훅의 견인은 같은
 * 종류의 밀어내기를 수행하는 두 가지 방식입니다.
 *
 * 훅 관련 함수들은 레벨을 명시적 인자로 받고 GL을 사용하지 않으므로,
 * tools/hooktest.c가 컨텍스트를 생성하지 않고도 네 단계 전부를 실행합니다.
 */
#ifndef WEAPON_H
#define WEAPON_H

/* The run's pools, by name only. Firing reaches them -- a projectile weapon
   puts something in the projectile pool, and the axe's slam damages monsters
   through it -- but nothing about what a Weapon IS depends on their contents,
   so this header takes the name and pools.h keeps the definition. Repeating
   the typedef in proj.h is legal C11 and is what lets either header be
   included first.
   플레이의 풀들이며 이름으로만 참조합니다. 사격이 그것에 닿습니다. 발사체 무기는 발사체
   풀에 무언가를 넣고, 도끼의 내려찍기는 그것을 통해 몬스터에 피해를 줍니다. 그러나 Weapon이
   *무엇인가*는 그 내용물에 의존하지 않으므로, 이 헤더는 이름만 받고 정의는 pools.h가
   유지합니다. proj.h에서 같은 typedef를 반복하는 것은 적법한 C11이며, 두 헤더 중 어느
   쪽이 먼저 포함되어도 되게 만드는 것이 그것입니다. */
typedef struct Pools Pools;

#include "level.h"

/* --- Ammunition constants / 탄약 상수 --- */

#define WEAPON_START_AMMO 20    ///< @brief Shells you spawn with. / 스폰 시 보유하는 탄환 수.
#define WEAPON_MAX_AMMO   50    ///< @brief What a belt holds -- pickups stop here. / 탄띠의 최대 수용량. 아이템 획득도 여기서 멈춥니다.

/* --- The weapon roster / 무기 구성 ---------------------------------------
 *
 * ENGLISH
 * -------
 * One row per weapon in ::WEAPONS, and a new one is a row plus whatever art it
 * wants -- the same shape enemy.c's bestiary has, and for the same reason: a
 * kind that needs a new code path is a kind that will drift from the others.
 *
 * Each weapon answers a question the others answer badly. That is the whole
 * design brief for a shooter's roster, and it is why the differences here are
 * in HOW an attack reaches its target rather than in how much it hurts:
 *
 *   shotgun  a wall of hitscan pellets, now, at close range
 *   grenade  around a corner, or into a group, at the cost of travel time
 *   rapid    sustained pressure on one target, with lead to work out
 *   axe      no ammo problem at all, if you are willing to close the distance
 *
 * 한국어
 * ------
 * ::WEAPONS에 무기당 한 행이며, 새 무기는 행 하나에 원하는 아트를 더한 것이 전부입니다.
 * enemy.c의 몬스터 도감과 같은 형태이고 이유도 같습니다. 새 코드 경로가 필요한 종류는
 * 결국 다른 것들과 어긋나게 됩니다.
 *
 * 각 무기는 다른 무기들이 잘 답하지 못하는 질문에 답합니다. 그것이 슈터 구성의 설계
 * 요지 전부이며, 이곳의 차이가 피해량이 아니라 공격이 *어떻게* 목표에 도달하는가에 있는
 * 이유입니다.
 */

/**
 * @brief The weapons, in the order the number keys select them.
 *
 * @note Also the order of their sprite prefixes and pickup kinds, so a weapon
 *       added here is added everywhere by index rather than by a table that
 *       has to agree with this one.
 *
 * @brief 숫자 키가 선택하는 순서대로의 무기 목록입니다.
 * @note 스프라이트 접두사와 아이템 종류의 순서이기도 하므로, 이곳에 추가된 무기는 이
 *       목록과 일치시켜야 하는 별도의 표가 아니라 인덱스로 모든 곳에 추가됩니다.
 */
enum {
    WP_SHOTGUN,   /**< Hitscan pellets. / 히트스캔 산탄. */
    WP_GRENADE,   /**< Arcing, bouncing, timed explosive. / 곡사·도탄·시한 폭발물. */
    WP_RAPID,     /**< Fast projectile stream. / 빠른 발사체 연사. */
    WP_AXE,       /**< Melee, with a dash. / 근접. 대쉬를 동반합니다. */
    WP_TYPES      /**< How many. / 무기 종류의 수. */
};

/**
 * @struct WeaponType
 * @brief Everything that makes one weapon behave unlike another.
 *
 * ENGLISH
 * -------
 * @note HOW an attack reaches its target is decided by which of `pellets`,
 *       `proj_speed` and `melee_range` is non-zero -- exactly one per row, the
 *       way enemy.c's `shot_speed` decides melee from ranged without a second
 *       flag to disagree with it. tools/weapontest.c asserts that.
 * @note `hook` says whether right-click throws the grapple. The axe is the one
 *       row that does not: it leaps instead, and a weapon that offered both
 *       would need a third button.
 *
 * 한국어
 * ------
 * @note 공격이 목표에 *어떻게* 도달하는지는 `pellets`, `proj_speed`, `melee_range` 중
 *       무엇이 0이 아닌지로 결정되며, 행마다 정확히 하나입니다. enemy.c의 `shot_speed`가
 *       별도의 플래그 없이 근접과 원거리를 가르는 것과 같습니다. tools/weapontest.c가
 *       이를 단언합니다.
 * @note `hook`은 우클릭이 그래플을 던지는지를 나타냅니다. 도끼가 유일하게 그렇지 않은
 *       행이며, 대신 도약합니다. 둘 다 제공하는 무기는 세 번째 버튼이 필요해집니다.
 */
/**
 * @brief What each weapon's atlas slots hold.
 *
 * ENGLISH
 * -------
 * A slot is just a drawing, and WHICH drawing means something different per
 * weapon, because Doom drew each weapon with the frames that weapon needed --
 * two for the chaingun, two for the launcher, three for the shotgun, four for
 * the chainsaw. One shared list of moments would make the short weapons pad
 * their slots with duplicates.
 *
 * Public because they pair with the sprite names in the WEAPONS table, which
 * are public for the same reason: they are the contract between a drawing in
 * assets/sprites and the weapon that shows it.
 *
 * 한국어
 * ------
 * @brief 각 무기의 아틀라스 슬롯이 무엇을 담는지입니다.
 *
 * 슬롯은 그저 그림이며 *어느* 그림인지는 무기마다 다릅니다. Doom이 각 무기를 그 무기에
 * 필요한 프레임으로 그렸기 때문입니다. 공유된 순간 목록 하나는 짧은 무기가 빈 슬롯을
 * 복제로 채우게 만듭니다. 공개인 이유는 WEAPONS 표의 스프라이트 이름과 짝을 이루기
 * 때문이며, 그 이름이 공개인 것과 같은 이유입니다. 둘은 assets/sprites의 그림과 그것을
 * 보여 주는 무기 사이의 계약입니다.
 */
enum { SG_IDLE, SG_PUMP0, SG_PUMP1, SG_PUMP2 };  /**< shotgun: SHTG A, B, C, D */
enum { LN_IDLE, LN_FIRE };                       /**< grenade: MISG A, B */
enum { RP_IDLE, RP_SPIN };                       /**< rapid: CHGG A, B */
enum { AX_REV0, AX_REV1, AX_CUT0, AX_CUT1 };     /**< axe: SAWG C, D, A, B */

typedef struct {
    const char *name;       /**< Shown on the HUD; also the sprite prefix and pickup name. / HUD 표시명이자 스프라이트 접두사, 아이템 이름. */
    const char *model;      /**< models.txt entry, drawn when there is no sprite art. / 스프라이트 아트가 없을 때 그리는 models.txt 항목. */
    const char *fire_snd;   /**< sounds.txt entry for the attack. / 공격 사운드. */
    /** sounds.txt entry played when this weapon is brought up, or NULL.
        A column rather than a check for WP_AXE, for the same reason
        ::reload_snd is one: the saw is the only weapon that announces itself
        today, and the moment a second one does, a check would have to become a
        table anyway -- with the first weapon's behaviour already written into
        an if.
        이 무기를 꺼낼 때 재생할 사운드이며 없으면 NULL입니다. WP_AXE 검사가 아니라 표의
        열인 이유는 ::reload_snd가 그런 것과 같습니다. 오늘은 톱만이 자기를 알리지만, 두
        번째가 생기는 순간 검사는 어차피 표가 되어야 하며 그때는 첫 무기의 동작이 이미 if
        문 안에 적혀 있게 됩니다. */
    const char *draw_snd;
    /** sounds.txt entry played when the recovery animation finishes, or NULL.
        The shotgun's rack is the reason this is a column rather than a check
        for WP_SHOTGUN: the recovery timer drives every weapon's viewmodel
        animation, so once every weapon set it, every weapon racked a shotgun.
        회복 애니메이션이 끝날 때 재생할 사운드이며 없으면 NULL입니다. 이것이
        WP_SHOTGUN 검사가 아니라 표의 열인 이유는 샷건의 장전음 때문입니다. 회복
        타이머가 모든 무기의 뷰 모델 애니메이션을 구동하므로, 모든 무기가 그것을
        설정하게 되자 모든 무기가 샷건을 장전했습니다. */
    const char *reload_snd;

    int   start_ammo;       /**< Rounds you spawn with, or pick the weapon up with. / 스폰 또는 획득 시의 탄약. */
    int   max_ammo;         /**< What the belt holds for this type. / 이 종류의 최대 탄약. */
    int   pickup_ammo;      /**< Rounds one ammo box gives. / 탄약 상자 하나가 주는 양. */

    int   damage;           /**< Per pellet, per projectile, or per swing. / 산탄 하나·발사체 하나·휘두르기 한 번당 피해량. */
    float cooldown;         /**< Seconds between attacks. / 공격 간격 (초). */
    float spread;           /**< Cone half-angle, radians. / 산포 원뿔의 반각 (라디안). */

    short pellets;          /**< >0: hitscan, this many rays. / 0보다 크면 히트스캔이며 이 수만큼의 광선. */
    float proj_speed;       /**< >0: launches a projectile at this m/s. / 0보다 크면 이 속도(m/s)로 발사체를 발사. */
    float proj_gravity;     /**< m/s^2 pulling the projectile down; 0 flies straight. / 발사체를 끌어내리는 가속도. 0이면 직진. */
    float melee_range;      /**< >0: a swing reaching this far, in metres. / 0보다 크면 이 거리(미터)까지 닿는 근접 공격. */

    float recoil, punch;    /**< Camera kick and view model kickback. / 카메라 반동과 뷰 모델 후퇴. */
    int   hook;             /**< Non-zero when right-click throws the grapple. / 0이 아니면 우클릭이 그래플을 던집니다. */
} WeaponType;

/* --- The axe's movement / 도끼의 이동 -------------------------------------
 *
 * ENGLISH
 * -------
 * The axe is the only weapon whose attacks are also movement, and that is what
 * makes a melee weapon viable here at all. Everything else in this game outruns
 * a walk, so a weapon with 2.2m of reach that did not move you would only ever
 * hit what had already caught you.
 *
 * These sit beside the grapple's tuning in this header for the reason stated
 * there: the feel of being thrown across a room is one thing to tune, and
 * splitting its numbers between two files is how half of them get tuned.
 *
 * 한국어
 * ------
 * 도끼는 공격이 곧 이동이기도 한 유일한 무기이며, 그것이 이곳에서 근접 무기를 성립하게
 * 하는 요소입니다. 이 게임의 다른 모든 것이 걷기보다 빠르므로, 사거리 2.2m짜리 무기가
 * 플레이어를 움직여 주지 않으면 이미 자신을 붙잡은 것만 때리게 됩니다.
 *
 * 그래플의 튜닝 값 옆 이 헤더에 두는 이유는 그곳에 적힌 것과 같습니다. 방을 가로질러
 * 던져지는 감각은 하나의 조정 대상이며, 그 수치를 두 파일에 나누는 것이 그중 절반만
 * 조정되는 경로입니다.
 */

/** @brief m/s added along the aim by a swing. / 휘두르기가 조준 방향으로 더하는 속도 (m/s). */
#define AXE_DASH_SPEED   14.0f

/** @brief Upward m/s the leap gives. / 도약이 부여하는 상승 속도 (m/s). */
#define AXE_LEAP_UP      13.0f
/** @brief Forward m/s the leap gives along the aim. / 도약이 조준 방향으로 부여하는 속도 (m/s). */
#define AXE_LEAP_FWD      9.0f

/**
 * @brief Radius of the slam that lands with you, metres.
 *
 * Wider than a grenade's blast, because the grenade is thrown at a place you
 * chose and this one goes off wherever you happened to come down. Paying for
 * the loss of aim with area is what keeps it worth a charge.
 *
 * 유탄의 폭발보다 넓습니다. 유탄은 고른 지점으로 던지지만 이것은 착지한 곳에서 터지기
 * 때문입니다. 조준을 잃는 대가를 범위로 치르는 것이 충전량을 쓸 가치를 유지시킵니다.
 */
#define AXE_SLAM_RADIUS   5.5f
/** @brief Damage at the centre of the slam. / 내려찍기 중심에서의 피해량. */
#define AXE_SLAM_DAMAGE     70

/**
 * @brief How big an event the slam is, for ::proj_flash. 1 is a charge going off.
 *
 * ENGLISH
 * -------
 * A THIRD OF A GRENADE, and the radius is what makes the number necessary
 * rather than redundant. A slam reaches further than a blast does -- 5.5m
 * against 4.2 -- so anything deriving brightness from reach would light the
 * room harder for the impact that has no fire in it than for the charge that
 * does. What a falling axe actually throws is sparks off stone and a shock
 * through the floor: enough light to be seen happening, nowhere near enough to
 * blow the room out the way ::LIGHT_BLAST_POWER does at full strength.
 *
 * The SHAKE takes the same number, and that half is not a discount at all:
 * ::WORLD_SHAKE_BLAST_REACH is measured in radii, and the slam's is the larger,
 * so the jolt from coming down carries further than the one from a grenade
 * while hitting less hard at the middle. Which is what landing on something is.
 *
 * 한국어
 * ------
 * @brief 내려찍기가 얼마나 큰 사건인지. ::proj_flash용이며, 1은 장약이 터지는 것입니다.
 *
 * *유탄의 3분의 1이며*, 이 수를 불필요한 것이 아니라 필요한 것으로 만드는 것은 반경입니다.
 * 내려찍기는 폭발보다 멀리 닿습니다(4.2m에 대해 5.5m). 그래서 밝기를 도달 거리에서 끌어내는
 * 것은 무엇이든, 불이 있는 장약보다 불이 없는 충격에 대해 방을 더 세게 밝히게 됩니다. 떨어지는
 * 도끼가 실제로 던지는 것은 돌에서 튀는 불꽃과 바닥을 통한 충격입니다. 일어났다는 것이 보일
 * 만큼의 빛이지, ::LIGHT_BLAST_POWER가 최대 세기에서 하듯 방을 날려 버릴 정도는 전혀 아닙니다.
 *
 * *흔들림*도 같은 수를 받으며, 그쪽 절반은 전혀 할인이 아닙니다.
 * ::WORLD_SHAKE_BLAST_REACH는 반경의 배수로 재고 내려찍기 쪽이 더 크므로, 착지의 충격은
 * 유탄의 것보다 멀리 퍼지면서 한가운데에서는 덜 세게 때립니다. 무언가 위로 내려앉는다는 것이
 * 바로 그런 일입니다.
 */
#define AXE_SLAM_FLASH    0.33f

/**
 * @brief Seconds a leap may stay airborne before the slam is forced.
 *
 * A leap that carries you off a ledge would otherwise never land, and its
 * charge would be spent on nothing. This is the same argument the grapple's
 * pull timeout makes.
 *
 * 난간 너머로 데려간 도약은 그렇지 않으면 결코 착지하지 않으며, 충전량이 헛되이
 * 소모됩니다. 그래플의 견인 시간 초과와 같은 논거입니다.
 */
#define AXE_LEAP_TIMEOUT  2.5f

/**
 * @brief Stats for one weapon. Never returns NULL; clamps a bad index.
 * @param[in] type A WP_* index.
 *
 * @brief 무기 하나의 특성입니다. NULL을 반환하지 않으며 잘못된 인덱스는 제한합니다.
 */
const WeaponType *wp_stats(int type);


/**
 * @brief The weapon index whose name matches, or -1.
 *
 * @note Walks the table, so a weapon added to ::WEAPONS is found here without
 *       this function being edited -- the same rule ::mon_type_for follows.
 *
 * @brief 이름이 일치하는 무기 인덱스. 없으면 -1입니다.
 * @note 표를 순회하므로 무기를 추가해도 이 함수를 고칠 필요가 없습니다.
 */
int wp_type_for(const char *name);

/* --- View model placement / 뷰 모델 배치 --- */

/**
 * @struct GunPose
 * @brief Placement and projection of the first-person view model.
 *
 * ENGLISH
 * -------
 * @brief Placement and projection of the first-person view model.
 * @note These are runtime values rather than #defines so build/modelview.exe
 *       can drive the *same* transform the game uses while you nudge it -- a
 *       copy of the maths inside the tool would drift from the game the first
 *       time either side was edited.
 *
 * 한국어
 * ------
 * @brief 1인칭 뷰 모델의 배치와 투영 정보입니다.
 * @note #define이 아닌 런타임 값인 이유는, build/modelview.exe가 위치를 조정하는
 *       동안 게임이 사용하는 것과 *동일한* 변환을 사용할 수 있게 하기 위함입니다.
 *       도구 안에 수식을 복사해 두면 어느 한쪽을 수정하는 순간 게임과 어긋나게
 *       됩니다.
 */
typedef struct {
    float scale, fov;      /**< Model scale and the view model's own field of view. / 모델 크기와 뷰 모델 전용 시야각. */
    float off_x, off_y, off_z; /**< Offset from the eye, gun-local metres. / 눈으로부터의 오프셋 (총기 로컬 좌표계, 미터). */
    float yaw, pitch;      /**< Base orientation, radians. / 기본 방향 (라디안). */
    float pivot_y, pivot_z;/**< Sway pivot, gun-local units. / 흔들림의 회전 중심 (총기 로컬 단위). */
} GunPose;

/* --- Global variable declarations / 전역 변수 선언 --- */

/** @brief The live view model pose, shared with the preview tool. / 프리뷰 도구와 공유되는 실시간 뷰 모델 포즈. */
extern GunPose g_gun_pose;

/* ==========================================================================
 * MOVEMENT-FEEL TUNING -- everything that governs how the hook and the
 * recoil kick push the player around lives in this one block, so retuning
 * either does not mean hunting through the file. The other half of this
 * feel -- how fast that momentum bleeds off afterward -- is
 * MOMENTUM_DRAG_GROUND/AIR in player.h; it lives there because it applies to
 * momentum from ANY source, not just these two, but conceptually it is the
 * same dial and worth reading alongside this block.
 * ==========================================================================
 */

/* The grapple's tuning moved to hook.h, and so did the six calls that read it.
   What stays here is what a Weapon IS -- including the hook's state, because
   the launcher holds it -- and hook.h includes this file to reach it. The
   grapple was moved out of weapon.c long before this; leaving its numbers and
   its declarations behind meant the boundary existed in one file and not in
   the other, which is the same as not existing.
   그래플의 수치 조정값이 hook.h로 옮겨 갔고 그것을 읽는 여섯 개의 호출도 함께 갔습니다.
   이곳에 남는 것은 Weapon이 *무엇인가*이며, 발사기가 들고 있으므로 훅의 상태도 여기
   포함됩니다. hook.h가 그것에 닿기 위해 이 파일을 포함합니다. 그래플은 이보다 훨씬 전에
   weapon.c에서 분리되었습니다. 그 수치와 선언을 남겨 둔 것은 경계가 한 파일에는 있고 다른
   파일에는 없다는 뜻이었고, 그것은 경계가 없는 것과 같습니다. */

/* --- recoil jumping ---
 * Every shot kicks the player back along -aim. Small on the ground, or
 * ordinary combat would shove you around by surprise; a real shove in the
 * air, which is what makes shotgun-jumping and mid-swing redirects off the
 * grapple work. Raise RECOIL_MOVE_AIR for punchier jumps; raise _GROUND if
 * ground combat should feel more physical at the cost of some stability. */
/**
 * @brief Radians a second the ring turns, per second of the weapon's cooldown.
 *
 * ENGLISH
 * -------
 * THE RATE IS THE FIRE RATE, and that is the whole idea: a weapon's cooldown
 * already says how fast it shoots, so dividing by it makes the ring's speed the
 * thing the player is holding rather than a number somebody tuned per weapon.
 * At the shipped cooldowns that is 0.60 rad/s on the shotgun (1.05s), 1.05 on
 * the grenade launcher (0.60s), 1.26 on the axe (0.50s) and 6.30 on the rapid
 * (0.10s) -- ten and a half times the shotgun, which is exactly the ratio of
 * their fire rates. Nothing has to be kept in step because there is only one
 * number and it is already in ::WeaponType.
 *
 * 0.63 IS ONE TURN EVERY TEN SECONDS PER SECOND OF COOLDOWN, picked so the
 * slowest weapon in the roster reads as turning at all without the fastest
 * becoming a blur: 2*pi/10.
 *
 * 한국어
 * ------
 * @brief 무기 대기 시간 1초당 고리가 도는 초당 라디안.
 *
 * *속도가 곧 연사 속도이며*, 그것이 발상의 전부입니다. 무기의 대기 시간이 이미 얼마나 빨리
 * 쏘는지를 말하므로, 그것으로 나누면 고리의 속도가 누군가 무기마다 조율한 수가 아니라
 * *플레이어가 쥐고 있는 것*이 됩니다. 출하된 대기 시간에서 샷건(1.05초) 0.60 rad/s, 유탄
 * 발사기(0.60초) 1.05, 도끼(0.50초) 1.26, 래피드(0.10초) 6.30입니다. 샷건의 10.5배이며 그것은
 * 정확히 두 무기 연사 속도의 비입니다. 맞춰 둘 것이 없습니다. 수가 하나뿐이고 그것은 이미
 * ::WeaponType 안에 있습니다.
 * *0.63은 대기 시간 1초당 10초에 한 바퀴*이며, 구성에서 가장 느린 무기도 돈다고 읽히되 가장
 * 빠른 것이 뭉개지지 않도록 골랐습니다. 2*pi/10입니다.
 */
#define WPN_SPIN_RATE 0.63f

/**
 * @brief Radians a second added to the ring the instant a shot goes off.
 *
 * ENGLISH: What makes firing visible on the ring rather than only inferable
 * from its steady rate. Four is between six and seven times the shotgun's
 * resting rate, so a single blast is a clear lurch; on the rapid it is
 * two thirds of a rate that is already fast, which reads as the ring being
 * driven rather than kicked -- the same input feeling different in the hand
 * of a different weapon, for free.
 *
 * 한국어: @brief 발사가 일어나는 순간 고리에 더해지는 초당 라디안.
 * @note 발사를 고리에서 *보이게* 만드는 것입니다. 꾸준한 속도에서 추론만 되지 않도록 합니다.
 *       4는 샷건의 정지 속도의 예닐곱 배이므로 한 발이 뚜렷한 요동입니다. 래피드에서는 이미
 *       빠른 속도의 3분의 2라서 걷어차이는 것이 아니라 *구동되는* 것으로 읽힙니다. 같은
 *       입력이 다른 무기의 손에서 다르게 느껴지며, 그 값은 공짜입니다.
 */
#define WPN_SPIN_KICK 4.0f

/**
 * @brief Extra rad/s the saw's ring keeps while it is cutting.
 *
 * ENGLISH: THE ONE WEAPON WHOSE RING IS A BLADE. Every other weapon shoves the
 * ring once per shot and lets ::WPN_SPIN_DECAY spend it, which reads as recoil.
 * A circular saw does not recoil, it RUNS: the ring should be up to speed for
 * as long as the cut lasts and wind down after, which is a level held rather
 * than an impulse decayed. So this is added to the rate for ::WPN_SAW_SPIN_TIME
 * after a swing instead of being handed to `spin_kick`.
 *
 * Twenty-five is high on purpose. At the axe's 0.50s cooldown its resting rate
 * is 1.26 rad/s, so the blade turns twenty times faster while cutting: past the
 * point where an eye can follow a spoke, which is exactly how a saw reads.
 *
 * 한국어: *고리가 곧 날인 유일한 무기입니다.* 다른 모든 무기는 발사마다 고리를 한 번 떠밀고
 * ::WPN_SPIN_DECAY가 그것을 쓰며, 그것은 반동으로 읽힙니다. 원형톱은 반동하지 않고
 * *돕니다.* 절삭이 이어지는 동안 고리는 최고 속도여야 하고 그 뒤에 잦아들어야 하며, 그것은
 * 감쇠하는 충격이 아니라 유지되는 수준입니다. 그래서 이 값은 `spin_kick`에 넘겨지는 대신
 * 휘두른 뒤 ::WPN_SAW_SPIN_TIME 동안 회전 속도에 더해집니다.
 * 25가 높은 것은 의도입니다. 도끼의 대기 시간 0.50초에서 휴지 속도는 1.26 rad/s이므로,
 * 절삭 중에는 날이 스무 배 빨리 돕니다. 눈이 살을 따라갈 수 있는 지점을 넘어서며, 그것이
 * 바로 톱이 읽히는 방식입니다.
 */
#define WPN_SAW_SPIN 25.0f

/**
 * @brief Seconds the saw's ring stays at speed after a swing.
 *
 * ENGLISH: Longer than the axe's own 0.50s cooldown, so holding the trigger
 * keeps the blade at a steady scream rather than pulsing it once per swing --
 * each swing renews the timer before the last one runs out. Released, it stops
 * within a beat, which is what says the cut is over.
 *
 * 한국어: 도끼 자신의 대기 시간 0.50초보다 길므로, 방아쇠를 누르고 있으면 날이 휘두를 때마다
 * 맥동하지 않고 한결같은 비명을 유지합니다. 각 휘두르기가 이전 것이 끝나기 전에 타이머를
 * 갱신하기 때문입니다. 놓으면 한 박자 안에 멎으며, 그것이 절삭이 끝났다고 말합니다.
 */
#define WPN_SAW_SPIN_TIME 0.65f

/**
 * @brief How fast the shot's kick bleeds out of the ring, per second.
 *
 * ENGLISH: Exponential, like ::MOMENTUM_DRAG_AIR and for its reason -- halving
 * dt and stepping twice must land where one full step does, or the lurch
 * changes shape with the frame rate. Six is a lurch that is over in about half
 * a second, which is inside the shotgun's own cooldown: the ring settles just
 * as the gun becomes ready, so the picture finishes saying "fired" exactly when
 * the weapon stops being busy.
 *
 * 한국어: @brief 발사의 충격이 고리에서 빠져나가는 초당 비율.
 * @note ::MOMENTUM_DRAG_AIR처럼 지수 감쇠이며 이유도 같습니다. dt를 절반으로 줄여 두 번
 *       실행해도 한 번에 실행한 곳에 도착해야 하며, 아니면 요동의 모양이 프레임률에 따라
 *       달라집니다. 6은 약 0.5초에 끝나는 요동이고 그것은 샷건 자신의 대기 시간 안입니다.
 *       총이 준비되는 바로 그때 고리가 가라앉으므로, 그림은 무기가 바빠지기를 그만두는
 *       순간에 "쐈다"는 말을 마칩니다.
 */
#define WPN_SPIN_DECAY 6.0f

/** @brief Turn the ring's angle back at, radians. / 고리의 각도를 되감는 지점 (라디안). */
/**
 * @brief Seconds the switch flourish lasts.
 *
 * ENGLISH: SHORT BECAUSE IT IS A SMEAR, not a draw animation. Doom's raise and
 * lower are about a fifth of a second each and they are a COST -- the gun is
 * gone while they play. This one costs nothing, so the only thing setting its
 * length is how long a smear frame can be held before it stops reading as
 * motion and starts reading as a different picture. Hand-drawn animation gives
 * a smear one or two frames of a three-frame move; 0.18s is eleven frames at
 * 60Hz, split either side of white, so each half is about five.
 *
 * 한국어: *꺼내는 동작이 아니라 스미어이므로 짧습니다.* Doom의 올리기와 내리기는 각각
 * 5분의 1초쯤이고 그것들은 *비용*입니다. 재생되는 동안 총이 없습니다. 이것은 비용이 없으므로
 * 길이를 정하는 유일한 것은, 스미어 프레임이 움직임으로 읽히기를 그만두고 다른 그림으로
 * 읽히기 시작하기까지 얼마나 붙잡아 둘 수 있는가입니다. 손그림 애니메이션은 세 프레임 동작에
 * 스미어를 한둘 줍니다. 0.18초는 60Hz에서 열한 프레임이고 흰색을 사이에 두고 갈리므로
 * 한쪽이 다섯쯤입니다.
 */
#define WPN_SWAP_TIME 0.18f

#define WPN_SPIN_WRAP 6.283185307f

#define RECOIL_MOVE_GROUND   1.4f   ///< @brief Kick speed added when firing while grounded, m/s. / 지상에서 사격 시 추가되는 반동 속도 (m/s).
#define RECOIL_MOVE_AIR      5.5f   ///< @brief Kick speed added when firing airborne -- shotgun jumping. / 공중에서 사격 시 추가되는 반동 속도. 샷건 점프의 핵심입니다.

/* --- Type definitions / 타입 정의 --- */

/**
 * @brief The beats of one hook cycle.
 *
 * ENGLISH
 * -------
 * A hook is always in exactly one of these. IDLE and the two active states
 * are mutually exclusive by construction, which is the point of using an enum
 * rather than a pair of flags -- "flying and attached at once" is not a state
 * that can be represented, so it is not a state that has to be handled.
 *
 * The launch is not a state: it is the instantaneous transition out of
 * ::HOOK_PULLING, applied once and done. Giving it a duration would mean the
 * player is not in control during it, which is exactly the feeling this
 * mechanic is trying to avoid.
 *
 * 한국어
 * ------
 * 훅은 항상 이 중 정확히 하나의 상태에 있습니다. IDLE과 두 활성 상태는 구조적으로
 * 상호 배타적이며, 이것이 플래그 쌍 대신 열거형을 사용하는 이유입니다. "비행 중이면서
 * 동시에 부착됨"은 표현할 수 없는 상태이므로 처리할 필요도 없는 상태가 됩니다.
 *
 * 도약은 상태가 아닙니다. ::HOOK_PULLING에서 빠져나오는 순간적인 전이이며, 한 번
 * 적용되고 끝납니다. 여기에 지속 시간을 부여하면 그동안 플레이어가 조작할 수 없게
 * 되는데, 이는 이 메커니즘이 피하려는 바로 그 느낌입니다.
 */
typedef enum {
    HOOK_IDLE = 0,  /**< Nothing in the air; the launcher is at rest. / 공중에 아무것도 없으며 발사기가 대기 상태입니다. */
    HOOK_FLYING,    /**< The claw is travelling toward its target. / 클로가 대상을 향해 날아가는 중입니다. */
    HOOK_PULLING    /**< The claw has landed and is reeling the player in. / 클로가 착지하여 플레이어를 끌어당기는 중입니다. */
} HookState;

/**
 * @struct Weapon
 * @brief Complete state of the shotgun and the grapple hook.
 *
 * ENGLISH
 * -------
 * @note Zero-initialise before first use; ::wp_init resets it fully. All
 *       timers count DOWN toward zero and are ticked by ::wp_update, so a
 *       caller that skips wp_update will find every cooldown frozen.
 *
 * 한국어
 * ------
 * 샷건과 그래플 훅의 전체 상태입니다.
 * @note 처음 사용하기 전에 0으로 초기화해야 하며, ::wp_init이 전체를 재설정합니다.
 *       모든 타이머는 0을 향해 감소하며 ::wp_update가 이를 진행시키므로,
 *       wp_update를 건너뛰는 호출자는 모든 쿨다운이 멈춘 상태를 보게 됩니다.
 */
typedef struct {
    /**
     * @brief Rounds held per weapon, indexed by WP_*.
     *
     * ENGLISH
     * -------
     * Separate pools rather than one shared number, because shared ammo makes
     * every weapon the same weapon with a different animation: the choice
     * stops being "which of these answers this room" and becomes "which spends
     * my one resource most efficiently", which has a single right answer and
     * therefore is not a choice.
     *
     * 한국어
     * ------
     * @brief WP_*로 인덱싱되는 무기별 보유 탄약입니다.
     *
     * 하나의 공유 수치가 아니라 별도의 탄약고입니다. 탄약을 공유하면 모든 무기가 애니메이션만
     * 다른 같은 무기가 됩니다. 선택이 "이 방에 무엇이 답인가"가 아니라 "무엇이 내 유일한
     * 자원을 가장 효율적으로 쓰는가"가 되는데, 후자는 정답이 하나뿐이므로 선택이 아닙니다.
     */
    int   ammo[WP_TYPES];

    /**
     * @brief Which weapon is in hand, a WP_* index.
     *
     * @note Switching is instant and costs no time. A draw animation would be
     *       the obvious addition and it is a tax on the one decision this
     *       roster exists to make -- picking the right tool has to be cheap
     *       enough to do mid-fight.
     *
     * @brief 손에 든 무기의 WP_* 인덱스입니다.
     * @note 전환은 즉시 이루어지며 시간이 들지 않습니다. 꺼내는 동작을 추가하는 것은
     *       뻔한 선택이지만, 이 구성이 존재하는 이유인 그 하나의 판단에 세금을 매기는
     *       일입니다. 올바른 도구를 고르는 일은 교전 중에도 할 만큼 저렴해야 합니다.
     */
    int   cur;

    /** @brief Non-zero once picked up, indexed by WP_*. / 획득했으면 0이 아닙니다. WP_*로 인덱싱됩니다. */
    int   owned[WP_TYPES];

    /**
     * @brief An axe leap is airborne and owes a slam on landing.
     *
     * Latched rather than derived from the player being off the ground,
     * because those are different questions: falling off a ledge is also being
     * airborne and does not owe anybody an explosion.
     *
     * 바닥에서 떨어져 있다는 사실에서 유도하지 않고 래치로 둡니다. 둘은 다른 질문입니다.
     * 난간에서 떨어지는 것도 공중에 있는 것이지만 누구에게도 폭발을 빚지고 있지
     * 않습니다.
     */
    int   leaping;
    /** @brief Seconds the current leap has been airborne. / 현재 도약이 공중에 있던 시간(초). */
    float leap_timer;
    /**
     * @brief What every damage number this weapon deals is multiplied by. 1 is normal.
     *
     * ENGLISH: ::PW_QUAD, arriving as a number rather than as a question. This
     * module has no idea what a powerup is and does not need one -- ::world_step
     * sets this from the player's clock every frame, so the multiplier is simply
     * what the weapon does now. An INT, because the damage it scales is an int and
     * this way there is no rounding to choose.
     * @note Applied where the damage LEAVES the weapon, not where it lands. A
     *       grenade already in the air was thrown by an armed player and should
     *       still be quadded when it goes off after the clock runs out.
     *
     * 한국어: ::PW_QUAD가 질문이 아니라 수로 도착한 것입니다. 이 모듈은 파워업이 무엇인지
     * 모르고 알 필요도 없습니다. ::world_step이 매 프레임 플레이어의 시계로부터 이것을
     * 설정하므로, 배율은 그저 *지금 이 무기가 하는 일*입니다. 배율을 적용하는 피해가
     * 정수이므로 이것도 정수이며, 그래야 고를 반올림이 없습니다.
     * @note 피해가 *닿는* 곳이 아니라 무기를 *떠나는* 곳에서 적용합니다. 이미 공중에 있는
     *       유탄은 강화된 플레이어가 던진 것이며, 시계가 끝난 뒤에 터져도 여전히 쿼드여야
     *       합니다.
     */
    int   damage_mul;

    float cooldown;      /**< Seconds until the next shot is allowed. / 다음 사격이 허용되기까지의 시간 (초). */
    float recoil;        /**< Extra camera pitch in radians, springs back to 0. / 카메라에 추가되는 피치 (라디안). 0으로 복원됩니다. */
    float punch;         /**< View model kickback in metres, springs back to 0. / 뷰 모델의 후퇴 거리 (미터). 0으로 복원됩니다. */
    float flash;         /**< Muzzle flash timer. / 총구 화염 타이머. */
    float bob_phase;     /**< Walk cycle accumulator. / 걷기 주기 누적값. */
    /** Free-running seconds, for animations that play while nothing happens.
        Separate from bob_phase because that one stops when the player does,
        and a chainsaw revs whether or not you are walking.
        아무 일도 없을 때 재생되는 애니메이션을 위한 자유 진행 시간(초)입니다.
        bob_phase와 분리한 이유는 그것이 플레이어가 멈추면 함께 멈추기 때문이며,
        전기톱은 걷고 있든 아니든 떨립니다. */
    float anim_clock;
    /**
     * @brief Radians the wand's magic ring has turned, free-running.
     *
     * ENGLISH
     * -------
     * THE RING TURNS AND THE STONE IN IT DOES NOT, which is why the emblem is
     * two drawings -- see ::EMB_SMEAR and the layers in ::wpview_draw_view. The
     * angle is the whole of what this field is; how fast it grows is
     * ::wp_spin_rate, and it is asked rather than stored so a weapon swap takes
     * effect on the next frame with nothing to keep in step.
     *
     * WRAPPED, for ::anim_clock's reason: a float that has been accumulating
     * for an hour has lost the precision a smooth turn needs.
     *
     * ONE ANGLE FOR EVERY WEAPON, not one per weapon. Switching mid-turn should
     * carry the ring's position across rather than snapping it back to zero --
     * the wand is one object and the emblem in it changed, so the thing that
     * was already spinning keeps spinning.
     *
     * 한국어
     * ------
     * @brief 지팡이의 마법 고리가 돈 각도(라디안). 자유 진행합니다.
     *
     * *고리는 돌고 그 안의 돌은 돌지 않으며*, 그것이 문양이 두 그림인 이유입니다.
     * ::EMB_SMEAR와 ::wpview_draw_view의 레이어를 보십시오. 이 필드의 전부는 각도이고, 얼마나
     * 빨리 자라는지는 ::wp_spin_rate입니다. 저장하지 않고 물어보므로, 무기를 바꾸면 맞춰 둘
     * 것 없이 다음 프레임에 적용됩니다.
     * *감아 돌립니다.* ::anim_clock의 이유와 같습니다. 한 시간 동안 누적된 실수는 매끄러운
     * 회전에 필요한 정밀도를 잃습니다.
     * *무기마다가 아니라 하나의 각도입니다.* 회전 도중에 바꾸면 고리의 위치가 0으로 튕기지
     * 않고 이어져야 합니다. 지팡이는 하나의 물건이고 그 안의 문양이 바뀐 것이므로, 이미 돌고
     * 있던 것은 계속 돕니다.
     */
    float spin;

    /**
     * @brief Extra radians a second the ring still owes from the last shot.
     *
     * ENGLISH: The kick. A ring that turned at a constant rate would say what
     * weapon is held and nothing about firing it; this is added to the rate and
     * decays, so every shot is a lurch that settles. ::WPN_SPIN_KICK sets it
     * and ::WPN_SPIN_DECAY spends it.
     *
     * 한국어: @brief 지난 발사에서 고리가 아직 빚진 초당 추가 라디안.
     * @note 발차기입니다. 일정한 속도로 도는 고리는 어느 무기를 쥐었는지만 말하고 발사에
     *       대해서는 아무 말도 하지 않습니다. 이것은 속도에 더해진 뒤 감쇠하므로, 모든 발사가
     *       가라앉는 요동이 됩니다. ::WPN_SPIN_KICK이 설정하고 ::WPN_SPIN_DECAY가 씁니다.
     */
    float spin_kick;

    /**
     * @brief Seconds the saw's blade is still up to speed. 0 when it is not.
     *
     * Set by a swing of a ::WeaponType with a `melee_range`, spent by
     * ::wp_update, and read by ::wp_spin_rate. Held rather than decayed -- see
     * ::WPN_SAW_SPIN.
     *
     * @brief 톱날이 아직 최고 속도인 시간(초). 아니면 0입니다.
     * `melee_range`를 가진 ::WeaponType의 휘두르기가 설정하고, ::wp_update가 소비하며,
     * ::wp_spin_rate가 읽습니다. 감쇠가 아니라 유지입니다. ::WPN_SAW_SPIN을 보십시오.
     */
    float saw_spin;

    /**
     * @brief Seconds left of the switch flourish, 0 when there is none.
     *
     * ENGLISH: COSMETIC, AND IT HAS TO BE. The field beside this one says
     * switching is instant and costs no time, and that is a design decision
     * this must not quietly reverse -- picking the right tool has to stay cheap
     * enough to do mid-fight. Nothing reads this except the draw: the new
     * weapon is in hand, loaded and firable on the frame the key is pressed,
     * and what this times is the ring's flash, not the player's wait.
     *
     * 한국어: *장식이며 그래야만 합니다.* 바로 옆의 필드는 전환이 즉시이고 시간이 들지
     * 않는다고 말하며, 그것은 이것이 조용히 뒤집어서는 안 되는 설계 결정입니다. 올바른
     * 도구를 고르는 일은 교전 중에도 할 만큼 저렴해야 합니다. 그리기 외에는 아무도 이것을
     * 읽지 않습니다. 새 무기는 키를 누른 프레임에 손에 있고 장전되어 있으며 발사할 수
     * 있습니다. 이것이 재는 것은 고리의 섬광이지 플레이어의 대기가 아닙니다.
     */
    float swap;
    /** @brief Which weapon the flourish is coming from, a WP_* index. / 연출이 떠나오는 무기의 WP_* 인덱스. */
    int   swap_from;

    float sway_x, sway_y;/**< View model lag behind mouse movement. / 마우스 움직임에 뒤따르는 뷰 모델의 지연. */
    float spread;        /**< Aim bloom in radians, grows per shot. / 조준 산포도 (라디안). 사격할 때마다 증가합니다. */
    float pump_timer;    /**< Counts down to the pump sound after a shot. / 사격 후 펌프 소리까지의 카운트다운. */
    float dry_timer;     /**< Rate-limits the empty click. / 빈 탄창 클릭음의 발생 빈도를 제한합니다. */
    unsigned rng;        /**< Per-weapon random state, so spread is reproducible. / 무기별 난수 상태. 산포를 재현 가능하게 합니다. */

    /**
     * @brief Which beat of the hook cycle is currently running.
     *
     * ENGLISH
     * -------
     * One of the ::HookState values. This is the field that makes the
     * four-beat sequence explicit rather than implied by a combination of
     * flags -- an earlier version tracked "attached" as a single int, and
     * adding a flight phase to that would have meant a second flag whose
     * valid combinations with the first were not obvious.
     *
     * 한국어
     * ------
     * ::HookState 값 중 하나입니다. 이 필드가 4단계 시퀀스를 여러 플래그의 조합으로
     * 암시하는 대신 명시적으로 표현합니다. 이전 버전은 "부착됨"을 int 하나로
     * 관리했는데, 거기에 비행 단계를 추가하려면 첫 플래그와의 유효한 조합이 분명하지
     * 않은 두 번째 플래그가 필요했을 것입니다.
     */
    int   hook_state;
    v3    hook_target;    /**< Where the claw is heading, or where it landed. / 클로가 향하는 지점, 또는 착지한 지점. */
    v3    hook_pos;       /**< The claw's current position while in flight. / 비행 중인 클로의 현재 위치. */
    float hook_cooldown;  /**< Seconds until the launcher can fire again. / 발사기가 다시 발사할 수 있기까지의 시간 (초). */
    float hook_timer;     /**< Time spent in the current state, for the pull timeout. / 현재 상태에서 경과한 시간. 견인 시간 초과 판정에 사용됩니다. */

    /**
     * @brief Seconds until the winch loop is retriggered.
     *
     * ENGLISH
     * -------
     * The reel is the only sustained sound in the game, and the mixer has no
     * concept of a loop -- ::audio_play starts a voice and it runs to the end
     * of its recipe. A sustained sound is therefore a short one restarted on a
     * timer, which is what this counts down.
     *
     * It has to be a timer rather than a per-frame call: firing every frame
     * would start sixty voices a second into a twelve-voice mixer, evicting
     * every other sound in the game for as long as the pull lasted.
     *
     * 한국어
     * ------
     * @brief 윈치 루프를 재시작하기까지 남은 시간 (초).
     *
     * 감기 소리는 이 게임에서 유일하게 지속되는 사운드인데, 믹서에는 루프 개념이
     * 없습니다. ::audio_play는 보이스를 시작하고 그 레시피의 끝까지 재생할 뿐입니다.
     * 따라서 지속음은 짧은 소리를 타이머로 재시작하는 것으로 구현되며, 이 필드가 그
     * 카운트다운입니다.
     *
     * 프레임마다 호출하지 않고 타이머를 쓰는 것은 필수입니다. 매 프레임 재생하면 12개
     * 보이스짜리 믹서에 초당 60개의 보이스를 밀어 넣게 되어, 견인이 지속되는 동안 게임의
     * 다른 모든 소리가 밀려납니다.
     */
    float hook_reel_timer;

    /**
     * @brief Closest the player has come to the anchor, and how long ago.
     *
     * ENGLISH
     * -------
     * The pair is what makes "the pull has stopped closing" answerable. Only
     * the BEST distance so far is kept, not the previous frame's: comparing
     * against the last frame would read the small oscillation of a winch
     * fighting gravity as progress and never trigger, where comparing against
     * the best keeps the bar rising and only resets when real ground is gained.
     *
     * `hook_best` is initialised on the first pull frame rather than at the
     * throw, because the claw is still flying then and the distance to the
     * target is not yet the distance to an anchor.
     *
     * 한국어
     * ------
     * @brief 플레이어가 앵커에 도달한 최단 거리와 그 이후 경과 시간입니다.
     *
     * 이 한 쌍이 "견인이 더 이상 좁혀지지 않는다"를 판정 가능하게 만듭니다. 직전
     * 프레임이 아니라 *지금까지의 최단* 거리만 보관합니다. 직전 프레임과 비교하면 중력에
     * 맞서는 윈치의 미세한 진동을 진전으로 읽어 영원히 발동하지 않지만, 최단 거리와
     * 비교하면 기준이 계속 낮아지며 실제로 전진했을 때만 초기화됩니다.
     *
     * `hook_best`는 발사 시점이 아니라 첫 견인 프레임에 초기화됩니다. 발사 시점에는
     * 클로가 아직 비행 중이므로 목표까지의 거리가 앵커까지의 거리가 아니기 때문입니다.
     */
    float hook_best;
    float hook_stall;

    /**
     * @brief Index of the hooked monster, or -1 when hooked to geometry.
     *
     * ENGLISH
     * -------
     * A monster target is tracked by index rather than by position, because a
     * monster moves: the pull has to follow it, and the impact damage needs
     * to know who to hit. -1 means the claw hit a wall, which is a valid hook
     * -- it just pulls to a fixed point and deals no damage on arrival.
     *
     * 한국어
     * ------
     * 몬스터 대상은 위치가 아닌 인덱스로 추적합니다. 몬스터는 움직이기 때문입니다.
     * 견인이 대상을 따라가야 하고, 충격 피해가 누구를 때릴지 알아야 합니다. -1은
     * 클로가 벽에 맞았다는 뜻이며 이 역시 유효한 훅입니다. 다만 고정된 지점으로
     * 견인될 뿐 도달 시 피해를 주지 않습니다.
     */
    int   hook_enemy;

    /**
     * @brief Non-zero once the current press has fired its claw.
     *
     * ENGLISH
     * -------
     * Set once the button has fired, cleared only by ::wp_hook_arm. Holding
     * the button down is therefore ONE launch, however long it is held and
     * whatever the cooldown does -- the cooldown paces repeated presses, but
     * only letting go actually rearms. Without this a held button re-fires
     * the instant a hook completes, which turns a chain of hooks into a held
     * button rather than a sequence of decisions.
     *
     * 한국어
     * ------
     * 버튼이 발사를 수행하면 설정되며, ::wp_hook_arm으로만 해제됩니다. 따라서
     * 버튼을 누르고 있는 것은 얼마나 오래 누르든, 쿨다운이 어떻게 되든 단 한 번의
     * 발사입니다. 쿨다운은 반복적인 누름의 간격을 조절할 뿐이며, 실제 재장전은
     * 버튼을 놓아야만 이루어집니다. 이것이 없으면 버튼을 누른 상태에서 훅이 완료되는
     * 즉시 재발사되어, 연속 훅이 일련의 판단이 아니라 그냥 버튼을 누르고 있는 것이
     * 되어 버립니다.
     */
    int   hook_latched;

    /* --- what firing needs to know, which used to be file-scope in weapon.c --
       These five were `g_level`, `g_muzzle`, `g_world_fov`/`g_aspect` and the
       two flash randomisers. Every one of them is a property of THIS weapon in
       THIS run -- what its shots hit, where its barrel ends, the camera its
       effects are placed against, and how the current shot's flash is turned --
       and holding them per-process meant a second Weapon silently shared the
       first one's answers.

       The same argument pools.h makes for the five spawn pools, applied to the
       one module that was left out of it. What did NOT move here is the drawn
       view model: the mesh, its materials and the buffers they are built in are
       one gun per GL context, and they now live in ::WeaponView.

       이 다섯은 `g_level`, `g_muzzle`, `g_world_fov`/`g_aspect`, 그리고 두 개의 화염
       무작위화 값이었습니다. 전부 *이번 플레이의 이 무기*에 속한 성질입니다. 사격이 무엇에
       맞는지, 총열이 어디서 끝나는지, 효과를 배치할 기준 카메라가 무엇인지, 이번 사격의
       화염이 얼마나 돌아가는지입니다. 이것들을 프로세스당 하나로 두면 두 번째 Weapon이 첫
       번째의 답을 조용히 공유하게 됩니다.

       pools.h가 다섯 개 스폰 풀에 대해 편 논거를, 거기서 빠져 있던 하나의 모듈에 적용한
       것입니다. 이곳으로 오지 *않은* 것은 그려지는 뷰 모델입니다. 메시와 그 재질, 그리고
       그것들을 만드는 버퍼는 GL 컨텍스트당 총 하나이며 이제 ::WeaponView에 있습니다. */

    /* A `const Level *level` used to sit here, set by ::wp_init to the address
       of the ::World's own level -- a field of a struct pointing at another
       field of the same struct. It made `World a = b;` produce a weapon firing
       into the geometry of the struct it was copied FROM, and C offers no way
       to prevent that, so ::World carried a @warning saying not to. A warning
       is not a mechanism. The level arrives as a parameter now, which is what
       ::wp_hook_update and ::wp_hook_in_range already did.
       이곳에 `const Level *level`이 있었고 ::wp_init이 ::World 자신의 레벨 주소로
       설정했습니다. 어떤 구조체의 필드가 같은 구조체의 다른 필드를 가리키는 것입니다.
       그 때문에 `World a = b;`는 자신이 복사되어 나온 구조체의 지오메트리를 향해 사격하는
       무기를 만들었고, C에는 그것을 막을 방법이 없으므로 ::World가 그러지 말라는 @warning을
       달고 있었습니다. 경고는 기구가 아닙니다. 이제 레벨은 인자로 도착하며,
       ::wp_hook_update와 ::wp_hook_in_range가 이미 그렇게 하고 있었습니다. */

    /**
     * @brief Barrel tip in gun-local units, where muzzle effects leave from.
     *
     * ENGLISH: Comes from the loaded model, so ::wpview_set_model writes it.
     * Until then it holds ::WP_MUZZLE_DEFAULT, which is where the shotgun's
     * barrel is -- so a weapon that has never had a model loaded still fires
     * from a sensible place instead of from the camera's origin.
     *
     * 한국어: 로드된 모델에서 오므로 ::wpview_set_model이 기록합니다. 그 전까지는
     * ::WP_MUZZLE_DEFAULT를 담습니다. 샷건 총열의 위치이며, 따라서 모델을 한 번도
     * 로드하지 않은 무기도 카메라 원점이 아니라 그럴듯한 자리에서 발사합니다.
     */
    v3    muzzle;

    /**
     * @brief The world camera this frame, for placing effects on screen.
     *
     * ENGLISH: Set by ::wp_update from its arguments. Held rather than passed
     * down because the firing path is four calls deep and every one of them
     * would otherwise carry two floats it does not use itself.
     *
     * 한국어: ::wp_update가 자신의 인자로부터 설정합니다. 아래로 전달하지 않고 보관하는
     * 이유는 발사 경로가 네 단계 깊이이며, 그러지 않으면 그 전부가 스스로는 쓰지 않는
     * float 두 개를 들고 다녀야 하기 때문입니다.
     */
    float world_fov, aspect;

    /** @brief This shot's muzzle flash size, randomised at fire time. / 이번 사격의 총구 화염 크기. 발사 시점에 무작위로 정해집니다. */
    float flash_scale;
    /** @brief This shot's muzzle flash roll in radians. / 이번 사격의 총구 화염 회전 (라디안). */
    float flash_roll;
} Weapon;

/**
 * @brief Where the muzzle sits before any model has been loaded.
 *
 * ENGLISH: The shotgun's barrel tip, which is what ::wp_init assumes. A
 * headless fixture never loads a model, so this is the muzzle every test sees.
 *
 * 한국어: 샷건 총열 끝이며 ::wp_init이 가정하는 값입니다. 헤드리스 픽스처는 모델을 로드하지
 * 않으므로, 모든 테스트가 보게 되는 총구 위치가 이것입니다.
 */
#define WP_MUZZLE_DEFAULT v3f(0.0f, 0.01f, -1.02f)

/**
 * @brief How long a muzzle flash lasts, in seconds.
 *
 * ENGLISH: In the header rather than in weapon.c because it is shared across
 * the split: firing sets ::Weapon::flash to it, ::wp_update counts it down,
 * and weaponview.c fades the drawn flash by the fraction remaining. A copy in
 * each file is a copy that drifts, and the symptom -- a flash that fades at a
 * different rate than it lives -- is invisible until someone changes one.
 *
 * 한국어: weapon.c가 아니라 헤더에 두는 이유는 분리된 양쪽이 공유하기 때문입니다. 발사가
 * ::Weapon::flash를 이 값으로 설정하고, ::wp_update가 그것을 감소시키며, weaponview.c가
 * 남은 비율로 그려지는 화염을 사라지게 합니다. 파일마다 사본을 두면 어긋나게 되며, 그
 * 증상(화염이 지속 시간과 다른 속도로 사라지는 것)은 누군가 한쪽을 고치기 전까지 보이지
 * 않습니다.
 */
#define FLASH_TIME 0.075f

/**
 * @brief Right-click with the axe: leap, then slam where you land.
 *
 * ENGLISH
 * -------
 * @param[in,out] w          Weapon; spends one charge from the axe's belt.
 * @param[in]     yaw,pitch  Aim, for the forward part of the leap.
 * @param[in,out] player_vel Receives the launch.
 * @return Non-zero when the leap started.
 *
 * @note Refuses in mid-air. A leap that could be chained would be a flight
 *       mode, and the grapple is already this game's answer to crossing a room
 *       without touching the floor.
 * @note The slam is not applied here -- it happens on landing, in
 *       ::wp_axe_land. Splitting them is what makes the airborne moment
 *       readable: the player can see where they are about to come down and
 *       still steer it.
 *
 * 한국어
 * ------
 * @brief 도끼의 우클릭입니다. 도약한 뒤 착지 지점에서 내려찍습니다.
 * @return 도약이 시작되었으면 0이 아닌 값.
 *
 * @note 공중에서는 거절합니다. 연속으로 이어지는 도약은 비행 모드가 되며, 바닥을 딛지
 *       않고 방을 가로지르는 것에 대한 이 게임의 답은 이미 그래플입니다.
 * @note 내려찍기는 이곳에서 적용되지 않습니다. 착지 시 ::wp_axe_land에서 일어납니다.
 *       둘을 나누는 것이 체공 순간을 읽을 수 있게 합니다. 플레이어는 자신이 어디로
 *       떨어질지 보면서 여전히 방향을 조정할 수 있습니다.
 */
int wp_axe_leap(Weapon *w, float yaw, float pitch, v3 *player_vel);

/**
 * @brief Resolves a leap that has come back down.
 *
 * @param[in,out] w         Weapon.
 * @param[in]     feet      Where the player landed.
 * @param[in]     grounded  Non-zero when the player is on a floor.
 * @param[in]     dt        Timestep, for the airborne timeout.
 * @return Non-zero on the frame the slam went off.
 *
 * @note Called every frame regardless, like ::wp_hook_update: a leap in
 *       progress has to be advanced by something, and a caller that only
 *       called this when it thought a landing had happened would be deciding
 *       what landing means in a second place.
 *
 * @brief 다시 내려온 도약을 처리합니다.
 * @return 내려찍기가 발동한 프레임이면 0이 아닌 값.
 * @note ::wp_hook_update와 마찬가지로 매 프레임 호출됩니다. 진행 중인 도약은 무언가가
 *       진행시켜야 하며, 착지했다고 판단될 때만 호출하는 호출자는 착지의 정의를 두 번째
 *       장소에서 내리게 됩니다.
 */
int wp_axe_land(Weapon *w, Pools *pl, v3 feet, int grounded, float dt);

/** @brief Non-zero while an axe leap is in the air. / 도끼 도약이 공중에 있는 동안 0이 아닙니다. */
/**
 * @brief Radians a second the wand's magic ring is turning right now.
 *
 * ENGLISH: ::WPN_SPIN_RATE over the held weapon's cooldown, plus whatever the
 * last shot's kick has not yet decayed. Derived rather than stored -- see the
 * definition for why a field would be a copy to keep in step.
 * 한국어: @brief 지팡이의 마법 고리가 지금 돌고 있는 초당 라디안.
 * @note 쥔 무기의 대기 시간으로 나눈 ::WPN_SPIN_RATE에, 지난 발사의 충격 중 아직 감쇠하지
 *       않은 만큼을 더한 값입니다. 저장하지 않고 유도합니다. 필드가 왜 맞춰 두어야 할
 *       사본이 되는지는 정의부를 보십시오.
 */
float wp_spin_rate(const Weapon *w);

/**
 * @brief Put a weapon in hand and start the switch flourish.
 *
 * The one way ::Weapon::cur changes during play. Assigning it directly still
 * works and is still wrong: the flourish is how the switch is ANNOUNCED, and a
 * change the player cannot see is a change they will make twice.
 *
 * @param[in,out] w    The belt.
 * @param[in]     type A WP_* index; ignored if out of range or already in hand.
 *
 * @brief 무기를 손에 쥐여 주고 전환 연출을 시작합니다.
 * @note 플레이 중 ::Weapon::cur가 바뀌는 유일한 경로입니다. 직접 대입해도 여전히 동작하고
 *       여전히 틀립니다. 연출이 곧 전환을 *알리는* 방식이며, 플레이어가 볼 수 없는 변화는
 *       그들이 두 번 하게 되는 변화입니다.
 */
void wp_swap_to(Weapon *w, int type);

/**
 * @brief How far through the switch flourish, 0 at the press and 1 at rest.
 *
 * 1 when there is no flourish, so a caller may use it without asking first.
 *
 * @brief 전환 연출의 진행도. 누른 순간 0, 끝나면 1입니다.
 * @note 연출이 없으면 1이므로, 호출자는 먼저 묻지 않고 써도 됩니다.
 */
float wp_swap_t(const Weapon *w);

int wp_axe_leaping(const Weapon *w);

/* --- Public function prototypes: lifecycle / 공개 함수 프로토타입: 수명 주기 --- */

/**
 * @brief Resets a weapon to the belt a run starts with.
 *
 * ENGLISH
 * -------
 * @param[out] w Weapon to initialise. Fully reset, including ammo.
 *
 * @note TAKES NO LEVEL any more. It used to store one, which is how a field of
 *       ::World came to point at another field of the same ::World; the level a
 *       shot is traced against arrives with the shot instead. See the note where
 *       that field used to be.
 *
 * 한국어
 * ------
 * @param[out] w 초기화할 무기. 탄약을 포함하여 완전히 재설정됩니다.
 *
 * @note 더 이상 레벨을 받지 *않습니다*. 이전에는 그것을 저장했고, 그것이 ::World의 한 필드가
 *       같은 ::World의 다른 필드를 가리키게 된 경위입니다. 사격이 판정하는 레벨은 이제 그
 *       사격과 함께 도착합니다. 그 필드가 있던 자리의 설명을 참조하십시오.
 */
void wp_init(Weapon *w);

/**
 * @brief Sets the belt to what a fresh run starts with: a shotgun and shells.
 *
 * ENGLISH
 * -------
 * @param[in,out] w Weapon whose `ammo`, `owned` and `cur` are overwritten.
 *                  Nothing else is touched.
 *
 * @note Split out of ::wp_init so that a fresh run can have a starting belt
 *       without a GL context. ::wp_init uploads the view model's texture and so
 *       runs exactly once; a level loaded with `carry_state = 0` is also a fresh
 *       start and had nothing to call. It got health back from ::player_spawn
 *       and kept every weapon, shell and keycard it had -- which meant a restart
 *       handed the player the roster they had earned on a map whose doors had
 *       just been re-locked behind them.
 * @note One definition of "what you start with", here, beside
 *       ::WEAPON_START_AMMO. A second copy in the level loader would be a
 *       starting belt that drifted from the one the game boots with.
 *
 * 한국어
 * ------
 * @brief 탄약대를 새 플레이의 시작 상태(샷건과 탄환)로 설정합니다.
 * @param[in,out] w `ammo`, `owned`, `cur`가 덮어써집니다. 그 밖에는 아무것도 건드리지
 *                  않습니다.
 *
 * @note GL 컨텍스트 없이도 새 플레이가 시작 탄약대를 가질 수 있도록 ::wp_init에서 분리했습니다.
 *       ::wp_init은 뷰 모델의 텍스처를 업로드하므로 정확히 한 번만 실행됩니다. `carry_state`가
 *       0인 레벨 로드도 새 시작이지만 호출할 것이 없었습니다. ::player_spawn에게서 체력만
 *       돌려받고 보유하던 모든 무기·탄환·키카드를 그대로 유지했으며, 그 결과 재시작은 방금 다시
 *       잠긴 문 뒤에서 플레이어가 얻어 낸 구성을 그대로 손에 쥐여 주었습니다.
 * @note "무엇을 갖고 시작하는가"의 정의는 ::WEAPON_START_AMMO 옆인 이곳 하나입니다. 레벨
 *       로더에 둔 두 번째 사본은 게임이 부팅하는 것과 어긋나는 시작 탄약대가 됩니다.
 */
void wp_start_belt(Weapon *w);

/**
 * @brief Advances one frame of weapon logic: timers, firing, bob and sway.
 *
 * ENGLISH
 * -------
 * @param[in,out] w               Weapon to update.
 * @param[in]     dt              Timestep in seconds.
 * @param[in]     firing          Non-zero while the fire button is held.
 * @param[in]     eye             Player's eye position, the origin shots trace from.
 * @param[in]     yaw             Aim yaw in radians.
 * @param[in]     pitch           Player's own pitch WITHOUT recoil; the trace
 *                                adds the weapon's recoil internally.
 * @param[in]     move_speed      Current movement speed, driving the walk bob.
 * @param[in]     mouse_dx        Horizontal mouse delta this frame, driving sway.
 * @param[in]     mouse_dy        Vertical mouse delta this frame, driving sway.
 * @param[in]     world_fov       World camera field of view, radians.
 * @param[in]     aspect          Viewport aspect ratio.
 * @param[in,out] player_vel      Receives the recoil kick directly.
 * @param[in]     player_grounded Non-zero when grounded, selecting the smaller
 *                                ground kick over the airborne one.
 * @note Ticks every timer in the struct, including the hook's cooldown. A
 *       caller that skips this call freezes all of them.
 * @note `player_vel`/`player_grounded` are how a shot's recoil reaches the
 *       player: firing adds a kick to `*player_vel` directly, the same way
 *       ::wp_hook_update applies the rope, rather than reporting "a shot
 *       fired" back to the caller for main.c to apply itself.
 * @note The world camera's fov and aspect are needed to place the tracer
 *       origin at the drawn muzzle, which lives under a different projection.
 *
 * 한국어
 * ------
 * @param[in,out] w               갱신할 무기.
 * @param[in]     dt              시간 간격 (초).
 * @param[in]     firing          발사 버튼을 누르고 있으면 0이 아닙니다.
 * @param[in]     eye             플레이어의 눈 위치. 사격 판정의 시작점입니다.
 * @param[in]     yaw             조준 방향 (라디안).
 * @param[in]     pitch           반동이 적용되지 않은 플레이어 자신의 피치.
 *                                판정 시 무기의 반동은 내부에서 더해집니다.
 * @param[in]     move_speed      현재 이동 속도. 걷기 흔들림을 유발합니다.
 * @param[in]     mouse_dx        이번 프레임의 수평 마우스 변화량. 스웨이를 유발합니다.
 * @param[in]     mouse_dy        이번 프레임의 수직 마우스 변화량. 스웨이를 유발합니다.
 * @param[in]     world_fov       월드 카메라의 시야각 (라디안).
 * @param[in]     aspect          뷰포트 종횡비.
 * @param[in,out] player_vel      반동을 직접 전달받습니다.
 * @param[in]     player_grounded 지면에 있으면 0이 아니며, 공중 반동 대신 더 작은
 *                                지상 반동이 선택됩니다.
 * @note 훅의 쿨다운을 포함한 구조체의 모든 타이머를 진행시킵니다. 이 호출을
 *       건너뛰면 모든 타이머가 멈춥니다.
 * @note `player_vel`/`player_grounded`는 사격 반동이 플레이어에게 전달되는
 *       경로입니다. 발사는 "사격이 발생했다"고 호출자에게 보고하여 main.c가
 *       처리하게 하는 대신, ::wp_hook_update가 로프를 적용하는 것과 동일한
 *       방식으로 `*player_vel`에 직접 반동을 더합니다.
 * @note 월드 카메라의 시야각과 종횡비는 다른 투영 아래에 존재하는, 화면에 그려진
 *       총구 위치에 예광탄의 시작점을 맞추는 데 필요합니다.
 */
void wp_update(Weapon *w, Pools *pl, const Level *l,
               float dt, int firing, v3 eye, float yaw, float pitch,
               float move_speed, float mouse_dx, float mouse_dy,
               float world_fov, float aspect, v3 *player_vel, int player_grounded);

/* --- Public function prototypes: the grapple hook / 공개 함수 프로토타입: 그래플 훅 --- */

/* wp_hook_fire, _locks_aim, _in_range, _arm, _update and _release are declared
   in hook.h now. They kept the wp_ prefix: they take a Weapon and they are
   fired from the weapon's own trigger, and renaming six functions across five
   files to make a header boundary look tidier is a change with no reader on
   the other side of it.
   wp_hook_fire, _locks_aim, _in_range, _arm, _update, _release는 이제 hook.h에
   선언됩니다. wp_ 접두사는 유지했습니다. 그것들은 Weapon을 받고 무기 자신의 방아쇠에서
   발사되며, 헤더 경계를 더 깔끔해 *보이게* 하려고 다섯 개 파일에 걸쳐 여섯 함수의 이름을
   바꾸는 것은 반대편에 읽는 사람이 없는 변경입니다. */

/**
 * @brief Builds the model-to-view matrix for the gun.
 *
 * ENGLISH
 * -------
 * @param[in] w Weapon supplying live bob, sway and recoil.
 * @return The transform, combining ::g_gun_pose with this weapon's animation.
 * @note Shared with the preview tool so both drive the same maths; see the
 *       note on ::GunPose.
 *
 * 한국어
 * ------
 * @param[in] w 실시간 흔들림, 스웨이, 반동 값을 제공하는 무기.
 * @return ::g_gun_pose와 해당 무기의 애니메이션을 결합한 변환 행렬.
 * @note 프리뷰 도구와 공유되어 양쪽이 동일한 수식을 사용합니다. ::GunPose의 참고
 *       사항을 확인하십시오.
 */
mat4 wp_gun_matrix(const Weapon *w);

/* --- Public function prototypes: what the view half asks this one
       / 공개 함수 프로토타입: 뷰 쪽이 이 파일에 묻는 것 --- */

/**
 * @brief Re-projects a gun-local point into the world so effects leave the gun on screen.
 *
 * ENGLISH
 * -------
 * @param[in] w     Weapon supplying the live view model transform and camera.
 * @param[in] local Point in gun-local space to project.
 * @param[in] eye   Camera position.
 * @param[in] right Camera right basis vector.
 * @param[in] up    Camera up basis vector.
 * @param[in] fwd   Camera forward basis vector.
 * @return A world position that lines up with the drawn point on screen.
 *
 * @note The view model is drawn with its own, narrower projection, so no point
 *       on it has an honest world position. To make an effect leave the gun
 *       *on screen* anyway, this takes the point's normalised device
 *       coordinates under the view-model projection and re-projects them into
 *       the world camera at a fixed distance.
 * @note Maths, not drawing, which is why it stayed on this side of the split:
 *       the tracer that leaves the barrel is placed by the firing code, and
 *       weaponview.c calls this for the tether's anchor.
 * @note Reads ::Weapon::world_fov and ::Weapon::aspect, which ::wp_update sets.
 *       Before the first ::wp_update those hold their defaults.
 *
 * 한국어
 * ------
 * @brief 총기 로컬 좌표의 점을 월드로 재투영하여, 효과가 화면상 총기에서 나가도록 합니다.
 * @param[in] w     실시간 뷰 모델 변환과 카메라를 제공하는 무기.
 * @param[in] local 투영할 총기 로컬 공간의 점.
 * @param[in] eye   카메라 위치.
 * @param[in] right 카메라의 우측 기저 벡터.
 * @param[in] up    카메라의 상향 기저 벡터.
 * @param[in] fwd   카메라의 전방 기저 벡터.
 * @return 화면에 그려진 지점과 일치하는 월드 좌표.
 *
 * @note 뷰 모델은 더 좁은 자체 투영으로 그려지므로 그 위의 어떤 점도 정확한 월드 좌표를
 *       갖지 않습니다. 그럼에도 효과가 *화면상* 총기에서 나가도록, 뷰 모델 투영 기준
 *       정규화 장치 좌표를 구해 고정 거리에서 월드 카메라로 재투영합니다.
 * @note 그리기가 아니라 수학이며, 그래서 분리 이후에도 이쪽에 남았습니다. 총열에서 나가는
 *       예광탄은 발사 코드가 배치하고, weaponview.c는 로프의 고정점을 위해 호출합니다.
 * @note ::wp_update가 설정하는 ::Weapon::world_fov와 ::Weapon::aspect를 읽습니다. 첫
 *       ::wp_update 이전에는 기본값이 들어 있습니다.
 */
v3 wp_muzzle_world_at(const Weapon *w, v3 local, v3 eye, v3 right, v3 up, v3 fwd);

/**
 * @brief Where the hook launcher sits, in gun-local units.
 *
 * ENGLISH: Slung below and behind the barrel, so the tether does not leave
 * from the same point the tracers do. Derived from ::Weapon::muzzle rather
 * than authored separately, so moving the gun model moves both.
 *
 * 한국어: 총열 아래 뒤쪽에 매달려 있으므로, 로프는 예광탄과 같은 지점에서 나가지
 * 않습니다. 따로 제작하지 않고 ::Weapon::muzzle에서 유도하므로, 총기 모델을 옮기면 둘 다
 * 함께 움직입니다.
 */
v3 wp_hook_muzzle(const Weapon *w);

/**
 * @brief Which sprite frame the view model should be drawing right now.
 *
 * ENGLISH: State, not drawing -- it reads the pump timer, the flash and the
 * idle clock and picks a row from the animation tables. It stayed on this side
 * of the split for that reason, and because ::weapon_sprite_frame_at exists to
 * let a headless test check exactly this without a context.
 *
 * 한국어: 그리기가 아니라 상태입니다. 펌프 타이머와 화염과 대기 시계를 읽어 애니메이션
 * 표에서 한 행을 고릅니다. 그 이유로 분리 이후 이쪽에 남았으며, ::weapon_sprite_frame_at이
 * 헤드리스 테스트가 컨텍스트 없이 바로 이것을 검사하도록 존재하기 때문이기도 합니다.
 */
int wp_sprite_frame(const Weapon *w);

#ifdef HOT_RELOAD
/**
 * @brief How long the pump animation runs, in seconds.
 *
 * ENGLISH
 * -------
 * @return The pump's duration.
 * @note Exposed so a headless test can drive ::weapon_sprite_frame_at with
 *       real timer values instead of carrying its own copy of the number --
 *       a test that hardcodes a duration passes after the duration changes.
 *
 * 한국어
 * ------
 * @brief 펌프 애니메이션이 지속되는 시간(초)입니다.
 * @note 헤드리스 테스트가 자체 사본을 들고 다니는 대신 실제 타이머 값으로
 *       ::weapon_sprite_frame_at을 구동할 수 있도록 노출합니다. 지속 시간을 하드코딩한
 *       테스트는 그 값이 바뀐 뒤에도 통과합니다.
 */
float weapon_pump_time(int type);

/**
 * @brief Which drawing the viewmodel would show for these timer values.
 *
 * ENGLISH
 * -------
 * @param[in] flash      Seconds of muzzle flash remaining.
 * @param[in] pump_timer Seconds of pump remaining; counts down.
 * @param[in] anim_clock The free-running idle clock, in seconds.
 * @return One of the WPN_* poses.
 * @note The frame choice is otherwise reachable only through a GL draw, and
 *       an animation nothing can assert is one that drifts silently.
 *
 * 한국어
 * ------
 * @brief 주어진 타이머 값에서 뷰 모델이 보일 그림입니다.
 * @param[in] flash      남은 총구 화염 시간(초).
 * @param[in] pump_timer 남은 펌프 시간(초). 감소합니다.
 * @return WPN_* 자세 중 하나.
 * @note 그렇지 않으면 프레임 선택은 GL 드로우를 통해서만 도달할 수 있으며, 아무것도
 *       단언할 수 없는 애니메이션은 조용히 어긋나는 애니메이션입니다.
 */
int weapon_sprite_frame_at(int type, float flash, float pump_timer,
                           float anim_clock);
#endif

#endif
