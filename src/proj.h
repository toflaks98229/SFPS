/**
 * @file proj.h
 * @brief The player's projectiles: grenades that arc and bounce, bolts that fly.
 *
 * ENGLISH
 * -------
 * The shotgun is hitscan -- it hits the instant you pull the trigger, and the
 * only question is where you were aiming. Everything here travels, and that
 * travel time is the whole design: a grenade can be banked around a corner and
 * a bolt has to be led onto a moving target, which are decisions the shotgun
 * never asks for.
 *
 * @note Touches no GL, deliberately. The flight, the bouncing, the fuse and the
 *       blast radius are all arithmetic over a struct, so tools/projtest.c
 *       steps a grenade down a corridor and asserts where it lands without a
 *       window -- the same split player.c, enemy.c and hook.c are built on.
 * @note Separate from enemy.c's `Shot`, which looks the same from a distance
 *       and is not: that one sweeps against the PLAYER and these sweep against
 *       MONSTERS. Merging them would mean a projectile carrying a flag for
 *       whose side it is on, and every collision test asking.
 *
 * 한국어
 * ------
 * 샷건은 히트스캔입니다. 방아쇠를 당기는 즉시 명중하며, 유일한 질문은 어디를 겨눴는가입니다.
 * 이곳의 모든 것은 *날아가며*, 그 비행 시간이 설계의 전부입니다. 유탄은 모퉁이 너머로
 * 튕겨 넣을 수 있고 탄은 움직이는 표적에 예측 사격을 해야 하는데, 둘 다 샷건은 결코 묻지
 * 않는 판단입니다.
 *
 * @note 의도적으로 GL을 전혀 건드리지 않습니다. 비행·도탄·도화선·폭발 반경이 모두 구조체에
 *       대한 산술이므로, tools/projtest.c가 창 없이 유탄을 복도로 굴려 어디에 떨어지는지
 *       단언합니다. player.c, enemy.c, hook.c가 기반한 것과 같은 분리입니다.
 * @note enemy.c의 `Shot`과 별개입니다. 멀리서 보면 같아 보이지만 아닙니다. 그쪽은
 *       *플레이어*를, 이쪽은 *몬스터*를 향해 훑습니다. 합치면 발사체가 어느 편인지를
 *       나타내는 플래그를 지니게 되고, 모든 충돌 판정이 그것을 묻게 됩니다.
 */
#ifndef PROJ_H
#define PROJ_H

#include "level.h"

/* --- Capacity / 용량 --- */

/**
 * @brief Projectiles in flight at once.
 *
 * Sized for the rapid weapon, which is the only one that can fill it: at one
 * shot every 0.085s over a 70 m/s flight across a 35m arena, roughly six are
 * airborne at any moment. The rest is headroom for a grenade volley thrown
 * into the same room.
 *
 * 연사 무기를 기준으로 정했습니다. 이 풀을 채울 수 있는 유일한 무기입니다.
 */
#define PROJ_MAX 48



/** @brief Metres a grenade's blast reaches. / 유탄 폭발이 도달하는 거리 (미터). */
#define PROJ_BLAST_RADIUS 4.2f

/**
 * @brief Seconds a grenade burns before going off on its own.
 *
 * Long enough to bank a shot off a wall and around a corner, short enough that
 * one landing at your feet is your problem rather than something you stroll
 * away from. Contact with a monster ends it early.
 *
 * 벽에 튕겨 모퉁이를 돌 만큼 길고, 발밑에 떨어진 것이 태연히 걸어 나갈 일이 아니라
 * 스스로의 문제가 될 만큼 짧습니다. 몬스터와 접촉하면 즉시 끝납니다.
 */
#define PROJ_FUSE 1.6f

/** @brief Speed kept after bouncing off a surface. / 표면에 튕긴 뒤 유지되는 속도의 비율. */
#define PROJ_BOUNCE 0.42f

/** @brief Radius used against walls and monsters, metres. / 벽과 몬스터에 대한 판정 반경 (미터). */
#define PROJ_RADIUS 0.18f

/* --- Types / 타입 --- */

/**
 * @struct Proj
 * @brief One projectile in flight.
 *
 * @note `gravity` is what separates a grenade from a bolt, and it is the only
 *       thing that does -- a bolt is a grenade that does not fall and does not
 *       bounce. One field, the same way enemy.c's `shot_speed` decides melee
 *       from ranged, rather than a `kind` enum that every branch has to read.
 *
 * @brief 비행 중인 발사체 하나입니다.
 * @note `gravity`가 유탄과 탄을 가르며, 그것이 유일한 구분입니다. 탄은 떨어지지도 튕기지도
 *       않는 유탄입니다. enemy.c의 `shot_speed`가 근접과 원거리를 가르는 것과 같이 필드
 *       하나이며, 모든 분기가 읽어야 하는 `kind` 열거형이 아닙니다.
 */
typedef struct {
    v3    pos;        /**< Current position. / 현재 위치. */
    v3    vel;        /**< Velocity, m/s. / 속도 (m/s). */
    float gravity;    /**< m/s^2 downward; 0 flies straight and never bounces. / 하강 가속도. 0이면 직진하며 튕기지 않습니다. */
    float fuse;       /**< Seconds until it goes off; <=0 means on contact only. / 폭발까지의 시간(초). 0 이하이면 접촉 시에만 폭발합니다. */
    float life;       /**< Seconds before it gives up, for the non-exploding kind. / 폭발하지 않는 종류가 소멸하기까지의 시간. */
    float blast;      /**< Blast radius in metres; 0 means it damages one target. / 폭발 반경(미터). 0이면 하나의 대상에만 피해를 줍니다. */
    int   damage;     /**< Damage at the centre. / 중심에서의 피해량. */
    float spin;       /**< Free-running clock, so a grenade tumbles. / 자유 진행 시계. 유탄이 구르게 합니다. */
    int   active;     /**< 0 when the slot is free. / 0이면 빈 슬롯입니다. */
} Proj;

/**
 * @struct ProjPool
 * @brief Every projectile in flight, owned by the caller rather than by proj.c.
 *
 * A plain array in a struct, so that a ::World holds its own and two of them do
 * not share one. It was a file-scope `static Proj g_proj[PROJ_MAX]`, which is
 * why tools\steptest.c had to call ::proj_reset by hand between fixtures: the
 * previous case's grenades were still in the air.
 *
 * 호출자가 소유하는, 비행 중인 모든 발사체입니다. 구조체 안의 평범한 배열이므로 ::World가
 * 자기 것을 가지며 두 개가 하나를 공유하지 않습니다. 이것은 파일 스코프
 * `static Proj g_proj[PROJ_MAX]`였고, 그래서 tools\steptest.c가 픽스처 사이에 ::proj_reset을
 * 손으로 불러야 했습니다. 이전 사례의 유탄이 아직 공중에 있었기 때문입니다.
 */
typedef struct {
    Proj p[PROJ_MAX];   /**< Slots. `active` says which are in use. / 슬롯. `active`가 사용 중인 것을 말합니다. */
} ProjPool;

/* The bundle that holds this pool and its neighbours, by name only: the calls
   below take it because a projectile's detonation reaches monsters and
   particles, not only other projectiles. pools.h defines it, and includes this
   file to do so -- so this end of the pair can only forward-declare.
   이 풀과 그 이웃들을 담는 묶음이며 이름으로만 참조합니다. 아래의 호출들이 그것을 받는
   이유는, 발사체의 폭발이 다른 발사체가 아니라 몬스터와 입자에 닿기 때문입니다. pools.h가
   그것을 정의하며 그러기 위해 이 파일을 포함하므로, 이 쪽 끝은 전방 선언만 할 수 있습니다. */
typedef struct Pools Pools;

/* --- Lifecycle / 수명 주기 --- */

/** @brief Clears every projectile. Called on a level load. / 모든 발사체를 제거합니다. */
void proj_reset(Pools *w);

/**
 * @brief Launches one projectile.
 *
 * @param[in] from    Muzzle position.
 * @param[in] dir     Unit direction.
 * @param[in] speed   m/s along `dir`.
 * @param[in] gravity Downward acceleration; 0 for a straight bolt.
 * @param[in] damage  Damage at the centre of the hit.
 * @param[in] blast   Blast radius in metres, or 0 to damage a single target.
 * @param[in] fuse    Seconds before it goes off on its own, or 0 for never.
 * @return Non-zero when a slot was free.
 *
 * @brief 발사체 하나를 발사합니다.
 * @return 빈 슬롯이 있었으면 0이 아닌 값.
 */
int proj_fire(Pools *w, v3 from, v3 dir, float speed, float gravity,
              int damage, float blast, float fuse);

/**
 * @brief Advances every projectile, resolving walls, monsters and fuses.
 *
 * @param[in] l  Level, for wall collision.
 * @param[in] dt Timestep in seconds.
 *
 * @note Sweeps rather than teleports: a bolt at 70 m/s covers over a metre in
 *       a frame, so testing only the arrival point would let it pass through a
 *       monster standing between the two.
 *
 * @brief 모든 발사체를 진행시키며 벽·몬스터·도화선을 처리합니다.
 * @note 순간이동이 아니라 훑습니다. 70m/s의 탄은 한 프레임에 1미터 이상 이동하므로,
 *       도착 지점만 검사하면 그 사이에 서 있는 몬스터를 통과해 버립니다.
 */
void proj_update(Pools *w, const Level *l, float dt);

/* --- Read-back, for the renderer and for tests / 렌더러와 테스트를 위한 조회 --- */

/** @brief How many slots exist; walk them and skip the inactive. / 슬롯의 개수. 순회하며 비활성은 건너뛰십시오. */
int proj_count(const Pools *w);

/** @brief Borrowed pointer to slot `i`, or NULL. / 슬롯 `i`에 대한 참조 포인터. 없으면 NULL. */
const Proj *proj_at(const Pools *w, int i);

/** @brief How many are in flight right now. / 지금 비행 중인 개수. */
int proj_live(const Pools *w);

/**
 * @brief Damages every monster within `radius` of `at`, falling off with distance.
 *
 * @param[in] at     Centre of the blast.
 * @param[in] radius Metres.
 * @param[in] damage Damage at the centre; scales to 0 at the rim.
 * @return How many monsters were hit.
 *
 * @note Public because the axe's landing slam is the same operation as a
 *       grenade going off, and two copies of a falloff curve is two curves to
 *       tune. See ::wp_axe_slam.
 *
 * @brief `at`을 중심으로 `radius` 안의 모든 몬스터에 거리에 따라 감소하는 피해를 줍니다.
 * @note 공개된 이유는 도끼의 착지 내려찍기가 유탄 폭발과 동일한 연산이기 때문입니다.
 *       감쇠 곡선의 사본이 둘이면 조정할 곡선이 둘이 됩니다.
 */
int proj_blast(Pools *w, v3 at, float radius, int damage);

#endif
