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
 *       blast radius are all arithmetic over a struct, so tools/weapontest.c
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
 *       대한 산술이므로, tools/weapontest.c가 창 없이 유탄을 복도로 굴려 어디에 떨어지는지
 *       단언합니다. player.c, enemy.c, hook.c가 기반한 것과 같은 분리입니다.
 * @note enemy.c의 `Shot`과 별개입니다. 멀리서 보면 같아 보이지만 아닙니다. 그쪽은
 *       *플레이어*를, 이쪽은 *몬스터*를 향해 훑습니다. 합치면 발사체가 어느 편인지를
 *       나타내는 플래그를 지니게 되고, 모든 충돌 판정이 그것을 묻게 됩니다.
 */
#ifndef PROJ_H
#define PROJ_H

#include "level.h"
/* FxTint -- a blast carries the colour it goes off in, and ::proj_boom_fx is
   where it is spent. fx.h names no GL and includes only m.h, so this costs the
   headless side nothing.
   FxTint입니다. 폭발은 자신이 터질 색을 지니고 다니며 ::proj_boom_fx가 그것을 씁니다.
   fx.h는 GL을 부르지 않고 m.h만 포함하므로, 헤드리스 쪽이 치르는 비용은 없습니다. */
#include "fx.h"

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

/* --- what colour an explosion is -------------------------------------------
 *
 * ENGLISH
 * -------
 * A LEGEND, NOT DECORATION, and it is the legend scene.c's LIGHT_COL_* table is
 * written to: what the player causes is warm, what a monster casts is cold, and
 * the shrine's gold belongs to the one thing in a room that is a REWARD -- see
 * the note above LIGHT_COL_ALTAR, which is where that last rule is argued. An
 * explosion is the loudest thing that happens in this game, so it is the worst
 * place to file something under the wrong side of those lines -- which is
 * exactly what had happened twice, both times by borrowing the monster bolt's
 * `boltburst` for a player's blast.
 *
 * The table lives here rather than beside the weapons that throw them because
 * a legend has to be readable in one place. Two entries is where a table is
 * cheapest to start and hardest to start later.
 *
 * 한국어
 * ------
 * *장식이 아니라 범례이며*, scene.c의 LIGHT_COL_* 표가 따라 쓰인 그 범례입니다. 플레이어가
 * 일으키는 것은 따뜻하고, 몬스터가 시전하는 것은 차가우며, 제단의 금색은 방에서 유일하게
 * *보상*인 것의 몫입니다. 마지막 규칙이 논해지는 자리는 LIGHT_COL_ALTAR 위의 설명입니다.
 * 폭발은 이 게임에서 가장 요란한 사건이므로 그 선들의 잘못된 쪽에 무언가를 분류해 넣기에 가장
 * 나쁜 자리입니다. 그리고 정확히 그 일이 두 번 일어났습니다. 두 번 모두 플레이어의 폭발에
 * 몬스터 볼트의 `boltburst`를 빌려 쓰면서였습니다.
 *
 * 이 표가 그것을 던지는 무기 곁이 아니라 이곳에 있는 이유는, 범례는 한자리에서 읽혀야 하기
 * 때문입니다. 항목이 둘일 때가 표를 시작하기 가장 싸고, 나중에 시작하기 가장 어려운 때입니다. */

/**
 * @brief The launcher's blast: the orange effects.txt already cools into.
 *
 * ENGLISH: What the text already authors rather than a new colour, so the
 * default and the legend agree: every layer converges on the hue `blastburst`
 * cools into, which is the deep end of the same orange LIGHT_COL_PROJ throws on
 * the wall while the grenade is still in the air. The grenade is the blast this
 * project's effects were written for, so its entry is the one that had to
 * change nothing -- what the tint buys here is that the colour is now SAID
 * somewhere, and the next explosion has a line to be different from.
 *
 * 한국어: 새 색이 아니라 텍스트가 이미 작성해 둔 색입니다. 기본값과 범례가 일치하도록 하기
 * 위해서입니다. 모든 겹이 `blastburst`가 식어 가는 색상으로 수렴하며, 그것은 유탄이 아직
 * 공중에 있는 동안 LIGHT_COL_PROJ가 벽에 던지는 것과 같은 주황의 짙은 끝입니다. 이 프로젝트의
 * 이펙트들이 그것을 위해 쓰인 폭발이 유탄이므로, 아무것도 바꾸지 않아야 하는 항목이 이것입니다.
 * 색조가 이곳에서 사 오는 것은 그 색이 이제 *어딘가에 적혀 있다*는 것과, 다음 폭발이 그것과
 * 달라질 기준선을 갖는다는 것입니다.
 */
#define BLAST_TINT_GRENADE ((FxTint){ 255, 104,  26 })

/**
 * @brief The axe's landing slam: an impact, so almost no colour at all.
 *
 * ENGLISH
 * -------
 * NOTHING IS BURNING HERE. A grenade is fuel going off and it has the deep
 * orange of one; a slam is a floor being broken by something heavy landing on
 * it, and what that throws is white. So this is nearly white by design, and the
 * saturation is the difference the player reads: from behind and in the dark, a
 * white flash is the axe and an orange one is a grenade, which is the whole
 * reason a blast carries a colour at all.
 *
 * WHAT IT LEANS AWAY FROM IS GOLD. The obvious pale warm colour for struck
 * metal sits within a few percent of LIGHT_COL_ALTAR, and the shrine's rule is
 * that nothing which hurts may wear its hue -- an explosion that read as a
 * reward for a second would be the most expensive second in the room. The blue
 * channel is lifted well past the shrine's to keep the two apart.
 *
 * 한국어
 * ------
 * @brief 도끼의 착지 내려찍기. 충격이므로 색이라 할 것이 거의 없습니다.
 *
 * *이곳에서는 아무것도 타지 않습니다.* 유탄은 연료가 터지는 것이고 그에 맞는 짙은 주황을
 * 가지지만, 내려찍기는 무거운 것이 떨어져 바닥이 깨지는 것이며 그것이 던지는 것은 흰색입니다.
 * 그래서 이 색은 의도적으로 거의 흰색이고, 플레이어가 읽는 차이는 채도입니다. 등 뒤에서
 * 어둠 속에서도 흰 섬광은 도끼이고 주황 섬광은 유탄입니다. 폭발이 색을 지니는 이유 전부가
 * 그것입니다.
 *
 * *이 색이 피해 가는 것은 금색입니다.* 두들겨 맞은 금속에 어울리는 뻔한 밝고 따뜻한 색은
 * LIGHT_COL_ALTAR와 몇 퍼센트 이내로 붙어 있는데, 제단의 규칙은 아프게 하는 무엇도 그 색조를
 * 입을 수 없다는 것입니다. 잠깐이라도 보상으로 읽히는 폭발은 방에서 가장 비싼 1초가 됩니다.
 * 둘을 갈라 두기 위해 파랑 채널을 제단의 것보다 충분히 높였습니다.
 */
#define BLAST_TINT_SLAM    ((FxTint){ 255, 196, 172 })

/**
 * @brief Explosions one frame may hand over before the extras are dropped.
 *
 * ENGLISH
 * -------
 * A frame with nine detonations in it is a frame with a grenade volley landing
 * on the same tile, and the tenth would be raising a shake that the first nine
 * have already raised louder. Eight is chosen against what a frame can
 * plausibly hold rather than against ::PROJ_MAX, which counts what is in the
 * AIR: they go off one at a time, on their own fuses.
 *
 * The overflow is reported through ::DIAG_BLAST_CAP rather than silently
 * dropped -- the same rule ::proj_fire follows when the pool turns a shot away.
 *
 * 한국어
 * ------
 * @brief 한 프레임이 넘겨줄 수 있는 폭발의 수. 초과분은 버려집니다.
 *
 * 한 프레임에 폭발이 아홉이라는 것은 유탄 일제 사격이 같은 칸에 떨어졌다는 뜻이며, 열 번째는
 * 앞의 아홉이 이미 더 크게 올려 둔 흔들림을 다시 올릴 뿐입니다. 8은 ::PROJ_MAX가 아니라 한
 * 프레임이 실제로 담을 수 있는 양을 기준으로 골랐습니다. ::PROJ_MAX가 세는 것은 *공중에* 있는
 * 것이고, 그것들은 각자의 도화선에 따라 하나씩 터집니다.
 *
 * 초과는 조용히 버리지 않고 ::DIAG_BLAST_CAP으로 보고합니다. 풀이 사격을 거절할 때
 * ::proj_fire가 따르는 것과 같은 규칙입니다.
 */
#define PROJ_BLAST_LOG 8

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
/**
 * @struct Blast
 * @brief One explosion that happened, kept until somebody with a LISTENER asks.
 *
 * ENGLISH
 * -------
 * WHY THE EXPLOSION IS RECORDED INSTEAD OF ACTING. Everything a blast does to
 * the world it does here: it damages monsters, it throws particles, it makes a
 * sound. The one thing it cannot do is shake the camera, because how hard a
 * blast shakes depends on where the PLAYER is standing and this module has
 * never been told -- ::proj_update takes a ::Pools and a ::Level, and giving it
 * the player as well would put the camera behind every projectile in the game.
 *
 * So the blast states what it was and where, and ::world_step -- which owns the
 * player, and is the only thing that does -- decides what that is worth from
 * where the player happens to be. It is the shape ::enemy_take_kills already
 * has: the pool counts what it has not yet handed over, and the run does the
 * arithmetic that needs to know about the run.
 *
 * 한국어
 * ------
 * @brief 일어난 폭발 하나. *듣는 이*를 가진 쪽이 물어볼 때까지 보관됩니다.
 *
 * *왜 폭발이 행동하지 않고 기록되는가.* 폭발이 월드에 하는 일은 전부 이곳에서 일어납니다.
 * 몬스터에 피해를 주고, 입자를 던지고, 소리를 냅니다. 유일하게 할 수 없는 일이 카메라를 흔드는
 * 것인데, 폭발이 얼마나 세게 흔드는가는 *플레이어*가 어디에 서 있는지에 달렸고 이 모듈은 그것을
 * 들은 적이 없기 때문입니다. ::proj_update는 ::Pools와 ::Level을 받으며, 여기에 플레이어까지
 * 주면 게임의 모든 발사체 뒤에 카메라가 따라붙게 됩니다.
 *
 * 그래서 폭발은 자신이 무엇이었고 어디였는지를 진술하고, 플레이어를 소유한 유일한 것인
 * ::world_step이 플레이어가 선 자리에서 그것이 얼마짜리인지 판단합니다. ::enemy_take_kills가
 * 이미 가진 형태입니다. 풀은 아직 넘기지 않은 것을 세고, 플레이는 플레이를 알아야 하는 계산을
 * 합니다.
 */
typedef struct {
    v3    at;      /**< Where it went off, world units. / 터진 지점 (월드 단위). */
    float radius;  /**< The radius its DAMAGE reached, metres. / 그 *피해*가 닿은 반경 (미터). */
} Blast;

typedef struct {
    Proj p[PROJ_MAX];   /**< Slots. `active` says which are in use. / 슬롯. `active`가 사용 중인 것을 말합니다. */

    /**
     * @brief Explosions since the last ::proj_take_blasts, and how many.
     *
     * ENGLISH: A DEBT, not a history. Nothing here is meant to survive the
     * frame it was made in -- a shake owed to a blast the player was three
     * rooms away from when it happened is a shake that arrives as a bug -- so
     * ::world_step drains this every frame whether or not anything is in it.
     *
     * 한국어: 기록이 아니라 *빚*입니다. 이곳의 무엇도 만들어진 프레임보다 오래 살 이유가
     * 없습니다. 터질 당시 플레이어가 세 방 건너에 있던 폭발에 진 흔들림은 버그로 도착하는
     * 흔들림입니다. 그래서 ::world_step이 내용물이 있든 없든 매 프레임 이것을 비웁니다.
     */
    Blast blast[PROJ_BLAST_LOG];
    int   n_blast;
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
void proj_reset(Pools *pl);

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
int proj_fire(Pools *pl, v3 from, v3 dir, float speed, float gravity,
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
void proj_update(Pools *pl, const Level *l, float dt);

/* --- Read-back, for the renderer and for tests / 렌더러와 테스트를 위한 조회 --- */

/** @brief How many slots exist; walk them and skip the inactive. / 슬롯의 개수. 순회하며 비활성은 건너뛰십시오. */
int proj_count(const Pools *pl);

/** @brief Borrowed pointer to slot `i`, or NULL. / 슬롯 `i`에 대한 참조 포인터. 없으면 NULL. */
const Proj *proj_at(const Pools *pl, int i);

/** @brief How many are in flight right now. / 지금 비행 중인 개수. */
int proj_live(const Pools *pl);

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
 *       tune. See ::wp_axe_land.
 *
 * @brief `at`을 중심으로 `radius` 안의 모든 몬스터에 거리에 따라 감소하는 피해를 줍니다.
 * @note 공개된 이유는 도끼의 착지 내려찍기가 유탄 폭발과 동일한 연산이기 때문입니다.
 *       감쇠 곡선의 사본이 둘이면 조정할 곡선이 둘이 됩니다.
 */
int proj_blast(Pools *pl, v3 at, float radius, int damage);

/**
 * @brief Draws an explosion: the six layers, in the colour of what went off.
 *
 * ENGLISH
 * -------
 * @param[in] at     Centre of the blast.
 * @param[in] normal Surface it went off against, or any unit vector in the air.
 * @param[in] radius The DAMAGE radius, which is what the dome is scaled to.
 * @param[in] tint   BLAST_TINT_*, or {0,0,0} for the colours effects.txt wrote.
 *
 * @note SEPARATE FROM ::proj_blast, which is the damage. They are called
 *       together every time so far, and they are still two calls: the axe's
 *       slam wants both and a future decoration -- a barrel with nothing near
 *       it, a scripted detonation -- wants only this one, and a damage
 *       parameter it had to pass 0 to would be a lie in the call.
 * @note Public for the reason ::proj_blast is: the slam is the same explosion
 *       as a grenade, and a second copy of six layers is six chances for the
 *       two to drift apart. wp_axe_land had that copy -- two effects, one of
 *       them the monsters' -- which is how the slam ended up with no visible
 *       radius and the wrong colour.
 *
 * 한국어
 * ------
 * @brief 폭발을 그립니다. 여섯 겹을, 터진 것의 색으로.
 * @param[in] radius *피해* 반경. 돔의 크기가 여기에 맞춰집니다.
 * @param[in] tint   BLAST_TINT_* 또는 {0,0,0}(effects.txt가 쓴 색).
 * @note 피해인 ::proj_blast와 *분리되어* 있습니다. 지금까지는 항상 함께 호출되지만 여전히 두
 *       개의 호출입니다. 도끼의 내려찍기는 둘 다 원하고, 이후의 연출(주변에 아무것도 없는 드럼통,
 *       각본된 폭발)은 이것만 원하며, 0을 넘겨야 하는 피해 인자는 호출문에 적힌 거짓말입니다.
 * @note ::proj_blast와 같은 이유로 공개입니다. 내려찍기는 유탄과 같은 폭발이며, 여섯 겹의 사본이
 *       하나 더 있다는 것은 둘이 어긋날 기회가 여섯이라는 뜻입니다. wp_axe_land가 그 사본을
 *       가지고 있었고(이펙트 둘, 그중 하나는 몬스터의 것), 그래서 내려찍기는 보이는 반경이 없고
 *       색도 틀린 채로 남아 있었습니다.
 */
void proj_boom_fx(Pools *pl, v3 at, v3 normal, float radius, FxTint tint);

/**
 * @brief Hands over the explosions since the last call, and forgets them.
 *
 * ENGLISH
 * -------
 * @param[out] out How many blasts fit; may be NULL when `max` is 0.
 * @param[in]  max Room in `out`. ::PROJ_BLAST_LOG takes everything.
 * @return How many were written.
 *
 * @note DRAINS EVEN WHEN `max` IS 0, so a caller that does not care still
 *       clears the debt. A log that is only emptied by whoever reads it is a
 *       log that grows in the one build nobody reads it in.
 * @note Both blast sources reach here: a grenade detonating inside
 *       ::proj_update, and the axe's slam, which happens earlier in the same
 *       frame. One drain after both collects both -- the argument
 *       ::enemy_take_kills makes about the three places a monster can die.
 *
 * 한국어
 * ------
 * @brief 지난 호출 이후의 폭발들을 넘겨주고 잊습니다.
 * @return 기록된 개수.
 * @note `max`가 0이어도 *비웁니다*. 관심 없는 호출자도 빚은 청산하게 하려는 것입니다. 읽는
 *       쪽만이 비우는 로그는, 아무도 읽지 않는 바로 그 빌드에서 자라나는 로그입니다.
 * @note 두 폭발 원천이 모두 이곳에 도달합니다. ::proj_update 안에서 터지는 유탄과, 같은 프레임
 *       더 앞에서 일어나는 도끼의 내려찍기입니다. 둘 뒤의 한 번의 배수가 둘을 모두 거둡니다.
 *       몬스터가 죽을 수 있는 세 자리에 대해 ::enemy_take_kills가 펴는 것과 같은 논거입니다.
 */
int proj_take_blasts(Pools *pl, Blast *out, int max);

#endif
