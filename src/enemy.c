/**
 * @file enemy.c
 * @brief Implements the monster AI, its collision, and the shots it fires.
 *
 * ENGLISH
 * -------
 * NO GL ANYWHERE, which is what lets tools/enemytest.c stand a monster on a
 * floor and check that it chases, stops to swing, and dies -- with no window
 * and no renderer. A chase bug is invisible from inside the running game.
 *
 * The AI is Quake's, named after the routines it came from so the borrowing is
 * findable: ::change_yaw is ChangeYaw, ::ai_run_slide is ai_run_slide,
 * ::pick_attack is CheckAttack. What those give a monster is inertia -- a
 * finite turn rate, a committed strafe direction, a random rest after
 * attacking, and dice rather than a threshold -- and inertia is the difference
 * between a creature and a mechanism.
 *
 * ONE SWITCH DECIDES HOW A MONSTER FIGHTS, on ::MonType::behaviour, and the
 * two archetypes below it are the only place the difference lives. Everything
 * else -- moving, turning, seeing, being hurt -- is shared, which is why a
 * fifth monster is a table row rather than a code path.
 *
 * @note All state lives in the caller's ::Pools. This file owns exactly one
 *       piece of module data, the ::TYPES stat table, and it is const.
 * @note The expensive thing here is the visibility trace. It is cached per
 *       monster and refreshed every ::SIGHT_PERIOD frames -- except where a
 *       bolt is released, which is always live. See ::sees_player.
 *
 * 한국어
 * ------
 * *GL이 전혀 없습니다.* 그 덕분에 tools/enemytest.c가 창도 렌더러도 없이 몬스터를 바닥에
 * 세워 두고 추격하는지, 휘두르려고 멈추는지, 죽는지를 확인할 수 있습니다. 추격 결함은 실행
 * 중인 게임 안에서는 보이지 않습니다.
 *
 * AI는 Quake의 것이며, 빌려 온 출처를 찾을 수 있도록 원래 루틴의 이름을 따랐습니다.
 * ::change_yaw는 ChangeYaw, ::ai_run_slide는 ai_run_slide, ::pick_attack은
 * CheckAttack입니다. 그것들이 몬스터에게 주는 것은 관성입니다. 유한한 회전 속도, 한 방향을
 * 밀고 나가는 횡이동, 공격 후의 무작위 휴식, 그리고 문턱값이 아닌 주사위입니다. 관성이야말로
 * 생물과 기계장치를 가르는 차이입니다.
 *
 * *몬스터가 어떻게 싸우는지는 switch 하나가 결정하며*, 그 기준은 ::MonType::behaviour입니다.
 * 아래의 두 아키타입이 그 차이가 사는 유일한 곳입니다. 나머지 전부(이동, 회전, 시야, 피격)는
 * 공유되며, 그래서 다섯 번째 몬스터는 코드 경로가 아니라 표의 한 행입니다.
 *
 * @note 모든 상태는 호출자의 ::Pools에 있습니다. 이 파일이 소유한 모듈 데이터는 ::TYPES 수치
 *       표 하나뿐이며, 그것은 const입니다.
 * @note 이곳에서 비싼 것은 가시성 판정입니다. 몬스터마다 캐시되고 ::SIGHT_PERIOD 프레임마다
 *       갱신됩니다. 볼트를 발사하는 지점만은 예외로 항상 실시간입니다. ::sees_player를
 *       참조하십시오.
 */

#include "enemy.h"
#include <math.h>
#include "pools.h"
#include "audio.h"
#include "fx.h"
/* The drop tables, and the pseudo-kind this module deliberately cannot
   resolve. See ::Enemy::drop.
   드롭 표와, 이 모듈이 의도적으로 해석하지 못하는 의사 종류입니다. ::Enemy::drop을
   참조하십시오. */
#include "loot.h"
#include "diag.h"
#include "player.h" /* PLAYER_EYE / PLAYER_RADIUS: the projectile hit box; PLAYER_STEP and
                       PLAYER_GRAVITY: the stairs and the fall, shared with whoever
                       else is in this world / 발사체 히트 박스, 그리고 이 세계에 함께 있는
                       누구와도 공유하는 계단과 낙하 */

/* --- File-local macros / 파일 지역 매크로 --- */
/* The seed a pool starts from; a zeroed EnemyPool means "not seeded yet".
   See fx.c for the same arrangement and the reason for it.
   풀이 출발하는 씨앗이며, 0인 EnemyPool은 "아직 씨앗이 채워지지 않음"을 뜻합니다. 같은
   구성과 그 이유는 fx.c를 참조하십시오. */
#define ENEMY_RNG_SEED 0x9e3779b9u

/* NAMES THAT NO LONGER NAME A ROW, and where each of them now arrives.
   `spawn` is from when there was one kind. `imp` is what MON_WATER_SPIRIT's
   slot was called before a water spirit took it. `hound` and `wraith` were rows
   until the bestiary lost its fast melee creature and its second flyer.

   A RETIRED NAME POINTS AT WHAT REPLACED IT, not at where it used to live, and
   that is the only judgement in this table. A `hound` was the thing that came
   at you in numbers, so it arrives as a water spirit; `imp` and `wraith` were
   the mid-range slot and the flying one, and both are the caster now that the
   caster flies. Renaming or retiring a creature must not empty the levels that
   already have it -- the same promise ::PK_AMMO keeps for `ammo` -- and a map
   that says `hound` should get a monster rather than a hole.

   Aliases rather than TYPES rows: a row would make each a further KIND,
   demanding a sprite atlas row apiece and changing the type count enemytest
   checks. That is exactly what removing them bought, so putting them back
   here as rows would spend it again.

   더 이상 어떤 행도 가리키지 않는 이름들과, 그 각각이 이제 도착하는 곳입니다.
   `spawn`은 종류가 하나뿐이던 시절의 것입니다. `imp`는 물의 정령이 차지하기 전
   MON_WATER_SPIRIT 슬롯의 이름이었습니다. `hound`와 `wraith`는 도감이 빠른 근접 생물과
   두 번째 비행체를 잃기 전까지 행이었습니다.

   *은퇴한 이름은 예전에 살던 자리가 아니라 그것을 대신한 것을 가리키며*, 이 표에 있는 판단은
   그것 하나뿐입니다. 하운드는 수로 달려들던 것이므로 물의 정령으로 도착합니다. `imp`와
   `wraith`는 중거리 자리와 나는 자리였고, 캐스터가 날게 된 지금 둘 다 캐스터입니다. 생물의
   이름을 바꾸거나 은퇴시키는 일이 이미 그것을 가진 레벨을 비워서는 안 됩니다. ::PK_AMMO가
   `ammo`에 대해 지키는 것과 같은 약속이며, `hound`라고 적은 맵은 구멍이 아니라 몬스터를 얻어야
   합니다.

   TYPES 행이 아니라 별칭인 이유는, 행으로 만들면 각각이 또 하나의 *종류*가 되어 스프라이트
   아틀라스 행을 하나씩 요구하고 enemytest가 검사하는 종류 수를 바꾸기 때문입니다. 행을 지워서
   산 것이 정확히 그것이므로, 이곳에 행으로 되돌리는 것은 그 값을 다시 치르는 일입니다. */
static const struct { const char *was; int now; } MON_LEGACY[] = {
    { "spawn",  MON_WATER_SPIRIT },
    { "imp",    MON_CASTER       },
    { "hound",  MON_WATER_SPIRIT },
    { "wraith", MON_CASTER       },
};

/**
 * @brief How close a caster will let the player get before it backs away.
 *
 * ENGLISH: A fraction of its attack range rather than an absolute distance, so
 * a caster with a longer reach keeps proportionally more room. Without a floor
 * like this a ranged monster walks into arm's length and becomes a brawler
 * that cannot punch.
 *
 * 한국어: 절대 거리가 아니라 공격 사거리에 대한 비율이므로, 사거리가 긴 캐스터는 그에 비례해
 * 더 넓은 자리를 지킵니다. 이런 하한이 없으면 원거리 몬스터는 팔 길이까지 걸어 들어와, 때릴 줄
 * 모르는 근접형이 됩니다.
 */
#define CASTER_KEEP 0.55f

/* --- Static variable definitions / 정적 변수 정의 -----------------------------
 *
 * There is one, and it is const: the bestiary below. The monsters, their
 * count, the projectiles and the random state all live in the caller's
 * ::EnemyPool -- see pools.h. Keep it that way: every helper here takes the
 * player's eye as an ARGUMENT rather than reading a file-scope copy, so
 * nothing can read a value whose freshness depends on where in the call stack
 * it happened to be.
 *
 * 하나뿐이며 const입니다. 아래의 베스티어리입니다. 몬스터, 그 개수, 발사체, 난수 상태는 모두
 * 호출자의 ::EnemyPool에 있습니다. pools.h를 참조하십시오. 그 상태를 유지하십시오. 이곳의 모든
 * 헬퍼는 플레이어의 눈 위치를 파일 스코프 사본에서 읽지 않고 *인자로* 받으므로, 호출 스택의
 * 어디에 있느냐에 따라 신선도가 달라지는 값을 읽을 수 있는 것이 없습니다.
 */

/**
 * @brief The bestiary: one row of stats per monster kind.
 *
 * ENGLISH
 * -------
 * Indexed by ::MonTypeID, and the same index is the creature's row in the
 * sprite atlas. Adding a kind is a row here and a body in sprite.c.
 *
 * THE COLUMNS ARE POSITIONAL, so the header line below is the only thing that
 * says which number is which. What each column DOES, and why it exists, is in
 * ::MonType -- one place, not two, so a rationale cannot come to describe a
 * number that is no longer the number.
 *
 * TO RESIZE A MONSTER: change `hgt`, then scale `rad`, `eye` and `atk` by the
 * same factor. See ::MonType's size block for what follows height on its own
 * and what does not.
 *
 * @note Read through ::mon_stats rather than indexed directly, so an id
 *       outside the enum lands on a row that exists.
 * @note ::types_check looks at behaviour against shot_speed and at the flag
 *       bits, and at nothing else. The dimensions are NOT checked: an `eye`
 *       above `hgt` is played, not caught.
 *
 * 한국어
 * ------
 * @brief 베스티어리. 몬스터 종류마다 수치 한 행씩입니다.
 *
 * ::MonTypeID로 인덱싱하며, 같은 인덱스가 스프라이트 아틀라스에서 그 생물의 행입니다. 종류를
 * 추가하는 것은 이곳의 행 하나와 sprite.c의 몸체 하나입니다.
 *
 * 열은 위치로 정해지므로, 어느 숫자가 무엇인지 말해 주는 것은 아래의 표제 줄뿐입니다. 각 열이
 * 무엇을 하는지와 왜 있는지는 ::MonType에 있습니다. 두 곳이 아니라 한 곳인 이유는, 근거가 더
 * 이상 그 숫자가 아닌 숫자를 설명하게 되지 않도록 하기 위해서입니다.
 *
 * 크기를 바꾸려면 `hgt`를 고치고 `rad`, `eye`, `atk`를 같은 배율로 함께 조정하십시오. 무엇이
 * height를 저절로 따라가고 무엇이 그러지 않는지는 ::MonType의 치수 블록에 있습니다.
 *
 * @note 직접 인덱싱하지 않고 ::mon_stats를 통해 읽으므로, 열거형 바깥의 식별자도 존재하는
 *       행에 떨어집니다.
 * @note ::types_check는 behaviour와 shot_speed의 정합성, 그리고 플래그 비트만 봅니다. 치수는
 *       검사하지 *않습니다.* `eye`가 `hgt`보다 높아도 잡히지 않고 그대로 플레이됩니다.
 */
/*    name,           behaviour,   hp,  spd, weave,   rad,   hgt,   eye, sight, aspct,    yaw, pain, cap, flags      */
static const MonType TYPES[MON_TYPES] = {
    /* the baseline: the fastest thing in the bestiary and the loosest weave,
       at 65% of ::PLAYER_WALK -- it cannot catch you, but it can be there
       when you turn round. One point-blank blast kills it.
       기준선. 도감에서 가장 빠르고 갈지자가 가장 큽니다. ::PLAYER_WALK의 65%이며,
       따라잡지는 못하지만 돌아섰을 때 거기 있을 수는 있습니다. 근접 샷건 한 방에
       죽습니다. */
    { "water_spirit",   AI_CASTER, 40, 7.0f, 0.62f, 0.52f, 1.70f, 1.30f, 34.0f, 0.70f, 260.0f, 0.6f, 8, MON_FLOATS },
    /* a wall with health -- hits hard, cannot be stun-locked, and closes at
       half your walking speed on the straightest line in the bestiary. Heavy
       is the small weave, not the speed: it commits to a direction and comes,
       and what makes it survivable is that you can read where.
       체력이 높은 벽. 강하게 때리고, 스턴 락에 걸리지 않으며, 도감에서 가장 곧은 선으로
       걷기 속도의 절반으로 다가옵니다. 무거움은 속도가 아니라 작은 갈지자입니다. 방향을
       정하고 오며, 살아남을 수 있게 하는 것은 그것이 어디로 올지 읽을 수 있다는 점입니다. */
    { "brute",          AI_BRAWLER, 120, 5.6f, 0.30f, 0.806f, 2.35f, 1.80f, 34.0f, 0.85f, 130.0f, 2.2f, 3, MON_UNFLINCHING },
    /* holds its range instead of closing, and holds it in the AIR -- so cover
       and angles matter, and so does the ceiling. Nothing about the numbers
       changed when MON_FLIES arrived: this is the same creature, no longer
       standing on anything. The row it replaced ("wraith") was these stats
       minus four health and a metre of reach, which is not a monster.
       접근하지 않고 사거리를 지키며, 그것을 *공중에서* 지킵니다. 그래서 엄폐와 각도의 문제이고
       천장의 문제이기도 합니다. MON_FLIES가 붙을 때 수치는 하나도 바뀌지 않았습니다. 아무것도
       딛고 있지 않게 되었을 뿐 같은 생물입니다. 이것이 대신한 행("wraith")은 여기서 체력 4점과
       사거리 1미터를 뺀 것이었고, 그것은 별개의 몬스터가 아닙니다. */
    { "caster",         AI_CASTER, 26, 5.8f, 0.46f, 0.546f, 1.90f, 1.45f, 40.0f, 0.80f, 180.0f, 0.9f, 4, MON_FLIES | MON_FLOATS },
    /* the boss: a caster with the footwork taken away. Its hp is spent in
       BOSS_CYCLES equal thirds and must divide by it -- types_check says so.
       The pain lock is effectively infinite because a boss that flinches is a
       boss a rapid gun holds still, and this one cannot step out of the way.
       보스. 발놀림을 뺀 캐스터입니다. 체력은 BOSS_CYCLES 등분으로 소모되며 그 값으로
       나누어떨어져야 합니다. types_check가 그렇게 말합니다. 경직 잠금이 사실상 무한인 이유는,
       경직하는 보스는 속사 무기가 붙잡아 두는 보스인데 이것은 비켜설 수조차 없기 때문입니다. */
    /* 3.6m, not the 5m it was first written at. The arena it is fought in has a
       5.12m ceiling -- a boss that reaches within a hand's width of it reads as
       a modelling error rather than as a large enemy, and the wards hanging
       above the tower would have nowhere to be. Height came down and radius,
       eye and reach came with it, which is the rule the MonType size block
       states for every row here.
       처음 적었던 5m가 아니라 3.6m입니다. 이것과 싸우는 아레나의 천장이 5.12m인데, 그것에 손
       한 뼘까지 닿는 보스는 큰 적이 아니라 모델링 오류로 읽히고, 탑 위에 걸린 결계핵은 있을
       자리가 없어집니다. 신장을 내리면서 반경, 시선, 사거리를 함께 내렸습니다. MonType의 치수
       블록이 이곳의 모든 행에 대해 진술하는 규칙입니다. */
    { "maw",            AI_CASTER, 900, 0.0f, 0.0f, 1.20f, 3.60f, 2.00f, 60.0f, 1.10f, 90.0f, 99.0f, 1, MON_BOSS | MON_ANCHORED },
    /* what guards it: no sight, no reach, no damage, and the only monster in
       the game that never acts. Ninety health is three WARD_SUMMON_DMG chunks,
       so a ward pays out exactly three times on its way down whatever kills it.
       그것을 지키는 것. 시야도 사거리도 피해도 없으며, 이 게임에서 유일하게 결코 행동하지 않는
       몬스터입니다. 체력 90은 WARD_SUMMON_DMG 세 덩어리이므로, 결계핵은 무엇에 죽든 쓰러지는
       동안 정확히 세 번 지급합니다. */
    { "ward",           AI_INERT, 90, 0.0f, 0.0f, 0.50f, 1.10f, 0.55f, 0.0f, 1.00f, 0.0f, 99.0f, 0, MON_GUARD | MON_ANCHORED },
};

/* WHAT EACH KIND CAN DO, ONE ROW PER ATTACK.
 *
 * SEPARATE FROM ::TYPES RATHER THAN NESTED IN IT, because that table is one
 * line per monster and has been readable as a table for exactly that reason.
 * Three slots of eleven columns inside a row would end that, and the row is
 * where somebody tuning a monster looks first.
 *
 * KEYED BY ::MonKind so the two cannot fall out of order. An index column in
 * TYPES pointing into a flat pool would be smaller by a few hundred bytes and
 * would be a magic number in every row -- the kind of number that is right
 * until a row is inserted above it.
 *
 * SLOTS RUN TO THE FIRST ::ATK_NONE. The rest of each row is zeroed by the
 * designated initialiser, and zero IS not-an-attack, so there is no count to
 * keep in step. `ward` is ::AI_INERT and has none at all.
 *
 * *종류마다 할 수 있는 일이며, 공격 하나에 한 행입니다.*
 * ::TYPES 안에 넣지 않고 따로 두는 이유는, 그 표가 몬스터 하나에 한 줄이고 바로 그 이유로
 * 표로 읽혀 왔기 때문입니다. 한 행 안의 열한 열짜리 슬롯 셋은 그것을 끝냅니다. 그리고 행은
 * 몬스터를 조정하는 사람이 가장 먼저 보는 곳입니다.
 * ::MonKind로 키를 잡으므로 둘이 순서를 잃을 수 없습니다. 평평한 풀을 가리키는 색인 열은 몇백
 * 바이트 작겠지만 모든 행에 마법의 수를 남기며, 그것은 위에 행이 하나 끼워지기 전까지만 맞는
 * 종류의 수입니다.
 * *슬롯은 첫 ::ATK_NONE까지입니다.* 각 행의 나머지는 지정 초기화가 0으로 채우고 0이 곧
 * *공격 아님*이므로, 맞춰 둘 개수가 없습니다. `ward`는 ::AI_INERT이고 아예 없습니다. */
/*        kind,      min,    max,  dmg,  wind,  cool,  shot, brst, bmin,  sprd,   gap, weight */
static const MonAttack ATTACKS[MON_TYPES][MON_MAX_ATTACKS] = {
    /* THE BOLT STARTS WHERE THE RETREAT ENDS. ::chase_caster backs away below
       `band * CASTER_KEEP` and the band is slot 0's max, so a bolt offered
       inside that distance would have the monster shooting from the place it
       is trying to leave -- it would stop kiting, which is the whole of what
       an ::AI_CASTER is. The number is that product, written out: 7.5 * 0.55.
       *볼트는 물러남이 끝나는 곳에서 시작합니다.* ::chase_caster는 `band * CASTER_KEEP`
       아래에서 물러나고 대역은 슬롯 0의 max이므로, 그 거리 안에서 제안되는 볼트는 몬스터가
       떠나려는 자리에서 쏘게 만듭니다. 그것은 거리 두기를 그만두는 것이고, ::AI_CASTER란
       그것의 전부입니다. 그 곱을 적어 둔 수입니다. 7.5 * 0.55. */
    [MON_WATER_SPIRIT] = {
        { ATK_BOLT,  4.2f,  7.5f,   3, 0.30f, 0.50f,  9.0f,   10,   5, 0.13f, 0.07f, 1.0f },
        { ATK_SWING, 0.0f,  1.8f,   6, 0.25f, 0.70f,  0.0f,    1,   1,  0.0f,  0.0f, 1.0f },
    },
    [MON_BRUTE] = {
        { ATK_SWING, 0.0f,  2.3f,  24, 0.55f, 1.50f,  0.0f,    1,   1,  0.0f,  0.0f, 1.0f },
    },
    /* AND THE SWING IS WHAT COSTS YOU FOR CLOSING. A caster that only ever
       backed off could be walked down and killed at leisure by a player who
       simply kept touching it -- the retreat has no floor, so the whole answer
       to a caster was "get close". Now getting close is answered. 12 damage
       against the bolt's 12 is deliberate: it is not a lesser attack, it is the
       same creature at a different range.
       *그리고 붙은 값을 치르게 하는 것이 휘두르기입니다.* 물러나기만 하는 캐스터는 계속
       붙어 있기만 하는 플레이어에게 느긋하게 걸어 죽일 수 있는 것이었습니다. 물러남에는
       바닥이 없으므로 캐스터에 대한 답 전체가 "붙어라"였습니다. 이제 붙는 것에도 답이
       있습니다. 볼트의 12에 대해 12인 것은 의도적입니다. 못한 공격이 아니라 다른 거리에 있는
       같은 생물입니다. */
    [MON_CASTER] = {
        { ATK_BOLT,  7.2f, 13.0f,  12, 0.85f, 1.40f, 11.0f,    1,   1,  0.0f,  0.0f, 1.0f },
        { ATK_SWING, 0.0f,  2.2f,  12, 0.40f, 0.95f,  0.0f,    1,   1,  0.0f,  0.0f, 1.0f },
    },
    [MON_MAW] = {
        { ATK_BOLT,  0.0f, 40.0f,  14, 0.90f, 1.10f, 14.0f,    5,   5, 0.22f,  0.0f, 1.0f },
    },
};

const MonAttack *mon_attack(int type, int slot)
{
    if (type < 0 || type >= MON_TYPES) return 0;
    if (slot < 0 || slot >= MON_MAX_ATTACKS) return 0;
    const MonAttack *a = &ATTACKS[type][slot];
    return a->kind == ATK_NONE ? 0 : a;
}

int mon_attack_count(int type)
{
    if (type < 0 || type >= MON_TYPES) return 0;
    int n = 0;
    while (n < MON_MAX_ATTACKS && ATTACKS[type][n].kind != ATK_NONE) n++;
    return n;
}


/* --- Static function prototypes / 정적 함수 프로토타입 --- */
static void types_check(void);
static int name_eq(const char *a, const char *b);

static float frand(EnemyPool *e);
static void play_at(v3 p, const char *name, int base);

static int make_monster(Pools *pl, const Level *l, int type, float x, float from_y, float z);
static v3 ward_summon_at(const Enemy *m);
static int ward_summon_type(Pools *pl, const Enemy *m);
static int ward_before(v3 a, v3 b);
static float spawn_wait(const EnemyPool *ep, float interval);
static void spawners_of(Pools *pl, const Level *l);
static int spawner_crowded(const Spawner *s, v3 player_eye);
static int spawners_update(Pools *pl, const Level *l, v3 player_eye, float dt);

static void shot_fire(Pools *pl, v3 from, v3 at, float speed, int damage,
                      int type, int owner);
static int shots_update(Pools *pl, const Level *l, v3 player_eye, float dt);

static float mon_step(const MonType *S);
static int floor_safe(const Level *l, float x, float f, float z);
static int foot_ok(const Level *l, const MonType *S, float x, float z, float feet, float *floor);
static int air_ok(const Level *l, const MonType *S, float x, float z, float y);
static int holds_height(const MonType *S, const Enemy *m);
static void monster_fall(const Level *l, const MonType *S, Enemy *m, float dt);
static int  mon_clear(const Pools *pl, const MonType *S, const Enemy *self,
                      v3 player_eye, float x, float z, float y);
static void move_toward(const Pools *pl, const Level *l, const MonType *S, Enemy *m,
                        v3 player_eye, float dx, float dz);
static void change_yaw(Enemy *m, float yaw_speed_deg, float dt);
static void ai_run_slide(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 player_eye, float dt);

static int can_see(const Level *l, const Enemy *m, v3 player_eye);
static int sees_player(const Pools *pl, const Level *l, Enemy *m, v3 player_eye);
static int pick_attack(Pools *pl, Enemy *m, float dist, float rise);

/**
 * The range the archetype keeps, metres: slot 0's upper bound.
 *
 * ENGLISH: Slot 0 is the attack a kind was built around, so where a monster
 * WANTS to stand is where that attack works -- ::chase_brawler closes to it and
 * ::chase_caster keeps just inside it. The other slots are what the monster
 * does when the player is somewhere it did not plan for, and letting them move
 * the band would make a creature drift toward whichever attack it happened to
 * use last. A kind with no attack at all (::AI_INERT) has no band and gets 0,
 * which the callers read as "already there" and stop.
 * 한국어: 아키타입이 유지하는 거리(미터)이며 슬롯 0의 상한입니다. 슬롯 0은 종류가 지어진
 * 공격이므로, 몬스터가 *서고 싶은* 자리는 그 공격이 통하는 자리입니다. 나머지 슬롯은 플레이어가
 * 예정에 없던 자리에 있을 때 하는 일이고, 그것이 대역을 움직이게 하면 생물이 마지막으로 쓴
 * 공격 쪽으로 표류하게 됩니다. 공격이 아예 없는 종류(::AI_INERT)는 대역이 없어 0을 받으며,
 * 호출자는 그것을 "이미 도착"으로 읽고 멈춥니다.
 */
static float mon_band(int type)
{
    const MonAttack *A = mon_attack(type, 0);
    return A ? A->max : 0.0f;
}

static void chase_brawler(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 to, float dist, v3 player_eye, float dt);
static void chase_caster(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 to, float dist, v3 player_eye, float dt);
static int release_swing(const MonAttack *A, Enemy *m, float dist);
static void begin_attack(Pools *pl, const MonAttack *A, Enemy *m);
static void release_bolt(Pools *pl, const Level *l, const MonType *S,
                         const MonAttack *A, Enemy *m, v3 player_eye);

/* --- Public function definitions / 공개 함수 정의 --- */
/* Ordered as enemy.h declares them. The contract for each is in the header and
   is deliberately not repeated here.
   enemy.h가 선언한 순서를 따릅니다. 각각의 계약은 헤더에 있으며 이곳에서 의도적으로
   되풀이하지 않습니다. */
const MonType *mon_stats(int type)
{
    if (type < 0 || type >= MON_TYPES)
        type = MON_WATER_SPIRIT;
    return &TYPES[type];
}

int mon_type_for(const char *kind)
{
    /* 테이블을 순회합니다. 새 몬스터는 TYPES에 행 하나를 추가하면 이곳이 자동으로
       알아보므로, 이 함수는 종류가 늘어나도 수정할 필요가 없습니다.
       Walks the table, so a new monster is a TYPES row and this function finds
       it without being edited. */
    for (int i = 0; i < MON_TYPES; i++)
        if (name_eq(TYPES[i].name, kind))
            return i;

    for (int i = 0; i < (int)(sizeof(MON_LEGACY) / sizeof(MON_LEGACY[0])); i++)
        if (name_eq(MON_LEGACY[i].was, kind))
            return MON_LEGACY[i].now;

    return -1;
}

void enemy_reset(Pools *pl)
{
    for (int i = 0; i < ENEMY_MAX; i++)
        pl->enemy.m[i].active = 0;
    for (int i = 0; i < ENEMY_MAX_SHOTS; i++)
        pl->enemy.shots[i].active = 0;
    pl->enemy.count = 0;

    /* The spawners go with the monsters, because they are the reason there
       would be more of them: a reset that left them running would have the
       previous level still delivering into the new one.
       스포너는 몬스터와 함께 사라집니다. 몬스터가 더 생길 이유가 바로 그것이기 때문입니다.
       그것을 돌려 둔 채로 초기화하면 이전 레벨이 새 레벨로 계속 배달하게 됩니다. */
    for (int i = 0; i < ENEMY_MAX_SPAWNERS; i++)
        pl->enemy.spawner[i].active = 0;
    pl->enemy.n_spawners = 0;

    /* The undrained tally goes with them, and it has to: a level load rebuilds
       this pool, so a kill left owed here would be paid into whatever run is
       playing when somebody next drains it. In practice there is never one --
       the intermission freezes the world for whole seconds before the load and
       every one of those frames drains -- and "in practice" is not a reason to
       leave a number that can outlive the monsters it counted.
       비우지 않은 집계도 함께 사라지며, 그래야 합니다. 레벨 로드가 이 풀을 다시 만들므로,
       이곳에 빚진 채 남은 처치는 다음에 누가 훑든 그때 진행 중인 플레이에 지급됩니다. 실제로
       그런 경우는 없습니다. 인터미션이 로드 전에 월드를 수 초간 정지시키고 그 모든 프레임이
       훑기 때문입니다. 그러나 "실제로는"은, 자신이 센 몬스터보다 오래 사는 숫자를 남겨 둘
       이유가 되지 못합니다. */
    pl->enemy.deaths = 0;

    /* The boss fight goes with them for the reason the spawners do, and it is
       the whole of why ::BossFight lives on this pool rather than on the run:
       every level load passes through here, so a half-finished fight cannot
       survive one. A cycle counter left behind would clamp the NEXT boss's
       health at a boundary belonging to a fight that is over.
       Cleared by assignment rather than field by field, the argument
       ::run_reset makes: a member added to ::BossFight is cleared by
       construction instead of by somebody remembering to extend a list.
       보스전은 스포너와 같은 이유로 함께 사라지며, 그것이 ::BossFight가 플레이가 아니라 이
       풀에 사는 이유 전부입니다. 모든 레벨 로드가 이곳을 지나므로 끝나지 않은 전투가 그것을
       견딜 수 없습니다. 남겨진 사이클 계수기는 이미 끝난 전투에 속한 경계에서 *다음* 보스의
       체력을 고정하게 됩니다.
       필드를 하나씩이 아니라 대입으로 지웁니다. ::run_reset이 펴는 논거입니다. ::BossFight에
       추가된 멤버는 누군가 목록을 늘려 주기를 기다리지 않고 구조적으로 지워집니다. */
    BossFight nofight = {0};
    pl->enemy.boss = nofight;

    /* And the suppression, which is a property of a fight in progress. Left
       set, a level loaded during a boss fight would run its spawners at a third
       speed for the rest of the run with nothing left to explain why.
       그리고 억제입니다. 진행 중인 전투의 성질입니다. 세워 둔 채로 두면, 보스전 도중에 로드된
       레벨이 남은 플레이 내내 스포너를 3분의 1 속도로 돌리게 되고, 왜 그런지 설명할 것이
       아무것도 남지 않습니다. */
    pl->enemy.spawn_slow = 0.0f;
    pl->enemy.spawn_rate = 1.0f;
}

void enemy_spawn_level(Pools *pl, const Level *l)
{
    /* Cheap, and this is the one call every level load and every headless test
       goes through, so a table that contradicts itself is reported on the first
       map anybody opens rather than on the one where somebody notices.
       비용이 적고, 이곳은 모든 레벨 로드와 모든 헤드리스 테스트가 반드시 거치는 호출입니다.
       그래서 자기모순인 표는 누군가 알아채는 맵이 아니라 처음 여는 맵에서 보고됩니다. */
    types_check();

    enemy_reset(pl);
    /* Runs over every entity even once full, rather than breaking out on the
       cap. Stopping early is the same amount of spawning but loses the count
       of what was skipped -- and "the level is missing monsters" is otherwise
       indistinguishable from "the level was authored that way".
       가득 찬 뒤에도 한계에서 루프를 빠져나가지 않고 모든 엔티티를 순회합니다. 조기
       종료해도 생성되는 수는 같지만 건너뛴 개수를 알 수 없게 되며, "레벨에 몬스터가
       빠졌다"와 "레벨을 원래 그렇게 만들었다"를 구분할 수 없게 됩니다. */
    for (int i = 0; i < l->n_ents; i++)
    {
        const Entity *e = &l->ents[i];
        int type = mon_type_for(e->kind);
        if (type < 0)
            continue;

        /* --- what a boss fight does NOT lay out at load ------------------
         *
         * A MARKER, NOT A MONSTER, for both halves of the fight. The maw's
         * position is remembered and ::step_boss decides when there is one --
         * story mode wants it immediately, endless mode wants it on a wave that
         * has not happened yet, and a maw standing in an empty arena for six
         * waves is neither. A ward is refused outright: a ward exists only
         * while a fight is under way, and one placed at load would be a
         * ::MON_GUARD standing in a room with no boss, which makes every future
         * boss invulnerable from the first frame it appears.
         *
         * Refusing here rather than trusting the FGD not to offer the classname
         * is the difference between a rule and a convention. `monster_maw` and
         * `monster_ward` both resolve through the `monster_` prefix whether or
         * not the editor lists them.
         *
         * --- 보스전이 로드 시점에 배치하지 *않는* 것 ----------------------
         *
         * 전투의 두 절반 모두에 대해 *몬스터가 아니라 표식입니다.* 아귀의 위치는 기억해 두고
         * 언제 하나가 있을지는 ::step_boss가 정합니다. 스토리 모드는 즉시 원하고, 무한 모드는
         * 아직 오지 않은 웨이브에 원하며, 빈 아레나에 여섯 웨이브 동안 서 있는 아귀는 둘 중
         * 어느 것도 아닙니다. 결계핵은 아예 거절합니다. 결계핵은 전투가 진행 중일 때만
         * 존재하며, 로드 시점에 놓인 것은 보스가 없는 방에 선 ::MON_GUARD가 되어 이후의 모든
         * 보스를 등장 첫 프레임부터 무적으로 만듭니다.
         *
         * FGD가 그 classname을 제공하지 않으리라 믿는 대신 이곳에서 거절하는 것이 규칙과 관례의
         * 차이입니다. `monster_maw`와 `monster_ward`는 편집기가 목록에 넣든 아니든 `monster_`
         * 접두사를 통해 해석됩니다. */
        if (TYPES[type].flags & MON_BOSS) {
            pl->enemy.boss.maw_pos  = v3f(e->x * 0.01f, e->y * 0.01f, e->z * 0.01f);
            pl->enemy.boss.have_maw = 1;
            continue;
        }
        if (TYPES[type].flags & MON_GUARD)
            continue;

        /* THROUGH ::make_monster, which is what a spawner already used. The
           twenty lines that used to be here were the same twenty, written out
           again -- the same ground search, the same cap check, the same fields,
           the same deterministic sight offset. Two ways to create a monster is
           one more than there are kinds of monster, and it cost exactly what
           duplication costs: the flyer's height was taught to one of them and
           the flyer's own marker went on making something that stood on the
           floor, while its spawner a metre away made one in the air.
           스포너가 이미 쓰던 ::make_monster를 통합니다. 이곳에 있던 스무 줄은 같은 스무 줄을 다시
           적은 것이었습니다. 같은 지면 탐색, 같은 상한 검사, 같은 필드, 같은 결정론적 시야
           오프셋입니다. 몬스터를 만드는 방법이 둘인 것은 몬스터의 종류 수보다 하나 많은
           것이며, 중복이 치르는 비용을 정확히 치렀습니다. 비행체의 높이를 둘 중 하나에만
           가르쳤고, 비행체의 표식은 계속 바닥에 선 것을 만들었으며 한 미터 옆의 그 스포너는
           공중에 만들었습니다. */
        make_monster(pl, l, type, e->x * 0.01f, e->y * 0.01f, e->z * 0.01f);
    }

    /* After the monsters the level drew, because a spawner's ceiling counts
       them: reading the markers first would let a spawner fire on its first
       tick into a level it thought was empty.
       레벨이 그린 몬스터 다음입니다. 스포너의 상한이 그들을 세기 때문입니다. 표식을 먼저
       읽으면 스포너가 비어 있다고 여긴 레벨에 첫 틱부터 발사하게 됩니다. */
    spawners_of(pl, l);

    /* The ward candidates, which spawn nothing and so may be read in any order
       relative to the two above. Here rather than beside the maw's marker in
       the loop, because the sort inside it wants the whole set.
       결계핵 후보이며, 아무것도 생성하지 않으므로 위의 둘에 대해 어떤 순서로 읽어도 됩니다.
       루프 안의 아귀 표식 옆이 아니라 이곳인 이유는, 그 안의 정렬이 전체 집합을 원하기
       때문입니다. */
    enemy_ward_scan(pl, l);
}

void enemy_wave_arm(Pools *pl, int wave)
{
    if (wave < 1) wave = 1;
    int step = wave - 1;

    for (int i = 0; i < pl->enemy.n_spawners; i++) {
        Spawner *s = &pl->enemy.spawner[i];

        /* Every slot, not only the active ones: a spawner retired by the
           previous wave is exactly the one this has to bring back.
           활성 슬롯만이 아니라 모든 슬롯입니다. 이전 웨이브가 은퇴시킨 스포너가 바로 이것이
           되살려야 할 대상입니다. */
        int budget = WAVE_BUDGET_BASE + step * WAVE_BUDGET_STEP;
        if (budget > WAVE_BUDGET_MAX) budget = WAVE_BUDGET_MAX;

        int burst = 1 + step / WAVE_BURST_EVERY;
        if (burst > WAVE_BURST_MAX) burst = WAVE_BURST_MAX;

        /* THE FLOOR IS NEVER ABOVE WHAT THE LEVEL ASKED FOR. Clamping straight
           to ::WAVE_INTERVAL_MIN makes wave 1 SLOWER than authored whenever a
           level wants a spawner faster than the floor -- the level says 1.0s,
           the floor says 1.2s, and wave 1 arrives at 1.2s having been made
           easier by the constant that exists to stop it getting harder. That
           also contradicts this function's own contract, which is that wave 1
           is the authored numbers.
           So the floor is the smaller of the two: deeper waves still cannot go
           below ::WAVE_INTERVAL_MIN, and a level that authored something faster
           keeps it from the first wave to the last.
           하한은 결코 레벨이 요청한 값보다 위가 아닙니다. ::WAVE_INTERVAL_MIN으로 곧장
           고정하면, 레벨이 하한보다 빠른 스포너를 원할 때마다 웨이브 1이 제작된 값보다
           *느려집니다*. 레벨은 1.0초를 말하고 하한은 1.2초를 말하며, 웨이브 1은 더 어려워지는
           것을 막으려고 존재하는 상수 때문에 더 쉬워진 채 1.2초로 도착합니다. 그것은 이 함수
           자신의 계약(웨이브 1은 제작된 수치 그대로)과도 모순됩니다.
           그래서 하한은 둘 중 작은 쪽입니다. 깊은 웨이브는 여전히 ::WAVE_INTERVAL_MIN 아래로
           갈 수 없고, 더 빠르게 제작한 레벨은 첫 웨이브부터 끝까지 그것을 유지합니다. */
        float floor_iv = s->base_interval < WAVE_INTERVAL_MIN
                       ? s->base_interval : WAVE_INTERVAL_MIN;
        float interval = s->base_interval - step * WAVE_INTERVAL_STEP;
        if (interval < floor_iv) interval = floor_iv;

        s->left     = (short)budget;
        s->burst    = (short)burst;
        s->interval = interval;

        /* The first group of a wave is due after a full interval, for the
           reason the first of a level is: a monster that arrives on the frame
           the banner appears is one the player never saw arrive.
           웨이브의 첫 무리는 온전한 한 주기 뒤에 나옵니다. 레벨의 첫 번째와 같은 이유입니다.
           배너가 뜨는 프레임에 도착하는 몬스터는 플레이어가 도착을 보지 못한 몬스터입니다. */
        s->timer  = spawn_wait(&pl->enemy, interval);
        s->warn   = 0.0f;
        s->active = 1;
    }
}

int enemy_wave_done(const Pools *pl)
{
    if (pl->enemy.n_spawners < 1) return 0;

    for (int i = 0; i < pl->enemy.n_spawners; i++) {
        const Spawner *s = &pl->enemy.spawner[i];
        if (s->warn > 0.0f) return 0;              /* already owed */
        if (s->active && s->left != 0) return 0;   /* still to send */
    }

    /* MINIONS, NOT EVERYTHING ALIVE. A boss and its wards are monsters in this
       pool, so ::enemy_alive is never zero while one stands -- the wave counter
       would freeze for the whole fight, and in endless mode the clock that
       schedules the NEXT boss is the one that stopped. That is commit 9d8099a's
       failure met from the other side: there, a wave that could not spawn could
       not end; here, a wave that cannot empty cannot end.
       A ward's SUMMONS are counted. They are ordinary monsters and clearing
       them is the wave's job; the thing that made them is not.
       살아 있는 모든 것이 아니라 *잡졸*입니다. 보스와 그 결계핵은 이 풀의 몬스터이므로 하나라도
       서 있으면 ::enemy_alive가 0이 되지 않습니다. 웨이브 계수기가 전투 내내 멈추고, 무한
       모드에서는 *다음* 보스를 예약하는 시계가 바로 그 멈춘 시계입니다. 커밋 9d8099a의 실패를
       반대편에서 만난 것입니다. 그곳에서는 생성할 수 없는 웨이브가 끝날 수 없었고, 이곳에서는
       비울 수 없는 웨이브가 끝날 수 없습니다.
       결계핵의 *소환물*은 셉니다. 평범한 몬스터이고 그들을 정리하는 것이 웨이브의 일이며,
       그들을 만든 것은 아닙니다. */
    return enemy_alive_minions(pl) == 0;
}

int enemy_spawner_count(const Pools *pl) { return pl->enemy.n_spawners; }

const Spawner *enemy_spawner_at(const Pools *pl, int i) {
    return (i >= 0 && i < pl->enemy.n_spawners) ? &pl->enemy.spawner[i] : 0;
}

/* WHERE THIS MONSTER IS FIGHTING, which is the player unless it is holding a
   grudge. Every function below ::enemy_update takes "the point to fight" and
   none of them ever asked whose it was, so infighting is not a second AI -- it
   is the same one handed a different point.
   THE GRUDGE LAPSES THREE WAYS and all of them end here rather than in
   ::enemy_hurt_by: the timer runs out, the foe stops being a live monster, or
   the index goes stale. Checking on READ rather than clearing on those events
   means there is no list of events to keep complete -- a foe that dies while
   this monster is mid-swing is answered the next time anybody asks, and nothing
   had to notice the death.
   *이 몬스터가 어디를 상대로 싸우고 있는가*이며, 원한을 품고 있지 않으면 플레이어입니다.
   ::enemy_update 아래의 모든 함수가 "싸울 지점"을 받고 그중 누구도 그것이 *누구의* 것인지 물은
   적이 없으므로, 내분은 두 번째 AI가 아니라 다른 점을 건네받은 같은 AI입니다.
   *원한은 세 가지로 풀리며* 전부 ::enemy_hurt_by가 아니라 이곳에서 끝납니다. 시간이 다하거나,
   상대가 살아 있는 몬스터이기를 그만두거나, 색인이 낡는 것입니다. 그 사건들에서 지우는 대신
   *읽을 때* 검사하는 것은, 빠짐없이 관리해야 할 사건 목록이 없다는 뜻입니다. 이 몬스터가
   휘두르는 도중에 죽은 상대는 다음에 누가 묻든 그때 답해지며, 그 죽음을 알아챌 필요가 있는
   것은 아무것도 없습니다. */
static v3 foe_point(const Pools *pl, const Enemy *m, v3 player_eye)
{
    if (m->foe < 0 || m->foe_time <= 0.0f || m->foe >= pl->enemy.count)
        return player_eye;
    const Enemy *f = &pl->enemy.m[m->foe];
    if (!f->active || f->state == E_DEAD)
        return player_eye;
    return v3f(f->pos.x, f->pos.y + TYPES[f->type].eye, f->pos.z);
}

/**
 * Pushes apart any two monsters standing inside each other.
 *
 * ENGLISH
 * -------
 * THE OTHER HALF OF ::mon_clear, and the half that makes it safe. Refusing a
 * step stops a monster walking into another; nothing about it can get two
 * apart that are already together, and monsters arrive together for reasons
 * that never involved walking -- a spawner putting two at one ward in the same
 * second, ::enemy_boss_summon putting a handful at the maw at once. Without
 * this, that pair is welded for the rest of the level, because every direction
 * out of an overlap is still an overlap.
 *
 * IT MOVES THROUGH ::move_toward RATHER THAN SETTING THE POSITION, which is
 * the whole reason it is short. A push has to respect everything a step
 * respects -- the walls, the lava ::floor_safe refuses, the height a flyer
 * holds, the ::MON_ANCHORED that means the maw does not budge -- and every one
 * of those is already in that function. Writing to `pos` here would be a
 * second mover with its own opinion about which of those it had remembered.
 * The maw and the wards get pushed by nothing and push everything, for free,
 * because ::move_toward already refuses their steps.
 *
 * EACH PAIR ONCE, `j` FROM `i + 1`, and both halves move. Doing it per monster
 * against all others would applayer_eye every push twice and make the rate mean half
 * of what it says.
 *
 * A LOOP THAT CANNOT RUN AWAY. Each push is capped at half the overlap, so a
 * pair closes the gap exactly at most and can never cross to the far side of
 * each other -- which is what would turn a crowd into a vibration. One pass per
 * frame, not iterated to convergence: three monsters wedged in a corner take a
 * few frames to sort themselves out, and a few frames of that is what a crowd
 * looks like anyway.
 *
 * 한국어
 * ------
 * *::mon_clear의 나머지 절반이며*, 그것을 안전하게 만드는 절반입니다. 걸음을 거절하는 것은
 * 몬스터가 다른 몬스터 안으로 걸어 들어가는 것을 막습니다. 이미 함께 있는 둘을 떼어놓는 일은
 * 그것으로 할 수 없고, 몬스터는 걷는 것과 무관한 이유로 함께 도착합니다. 스포너가 같은 초에 한
 * 워드에 둘을 놓고, ::enemy_boss_summon이 아귀 자리에 여럿을 한꺼번에 놓습니다. 이것이 없으면
 * 그 쌍은 레벨이 끝날 때까지 용접됩니다. 겹침에서 나가는 모든 방향이 여전히 겹침이기
 * 때문입니다.
 *
 * *위치를 직접 쓰지 않고 ::move_toward를 통해 움직이며*, 그것이 이 함수가 짧은 이유의
 * 전부입니다. 밀기는 걸음이 존중하는 모든 것을 존중해야 합니다. 벽, ::floor_safe가 거절하는
 * 용암, 비행체가 유지하는 고도, 아귀가 꿈쩍하지 않는다는 뜻의 ::MON_ANCHORED. 그 전부가 이미
 * 그 함수 안에 있습니다. 이곳에서 `pos`에 쓰는 것은 그중 무엇을 기억했는지에 대해 자기 의견을
 * 가진 두 번째 이동자를 만드는 일입니다. 아귀와 워드는 아무것에도 밀리지 않고 모든 것을
 * 밀어냅니다. 공짜로 그렇습니다. ::move_toward가 이미 그들의 걸음을 거절하기 때문입니다.
 *
 * *쌍마다 한 번*, `j`는 `i + 1`부터이며 양쪽이 다 움직입니다. 몬스터마다 나머지 전부에 대해
 * 하면 모든 밀기가 두 번 적용되어 비율이 적힌 것의 절반을 뜻하게 됩니다.
 *
 * *달아날 수 없는 반복.* 각 밀기는 겹침의 절반으로 제한되므로 한 쌍은 간격을 정확히 닫는 것이
 * 최대이고 서로의 반대편으로 건너갈 수 없습니다. 그것이 군중을 진동으로 바꾸는 일입니다.
 * 프레임당 한 번이며 수렴할 때까지 반복하지 않습니다. 모퉁이에 낀 셋은 스스로 정리하는 데 몇
 * 프레임이 걸리고, 몇 프레임의 그것이 어차피 군중의 모습입니다.
 */
static void separate_monsters(Pools *pl, const Level *l, v3 player_eye, float dt)
{
    for (int i = 0; i < pl->enemy.count; i++)
    {
        Enemy *a = &pl->enemy.m[i];
        if (!a->active || a->state == E_DEAD)
            continue;
        const MonType *AS = &TYPES[a->type];

        for (int j = i + 1; j < pl->enemy.count; j++)
        {
            Enemy *b = &pl->enemy.m[j];
            if (!b->active || b->state == E_DEAD)
                continue;
            const MonType *BS = &TYPES[b->type];

            if (a->pos.y >= b->pos.y + BS->height ||
                b->pos.y >= a->pos.y + AS->height)
                continue;

            float dx = a->pos.x - b->pos.x, dz = a->pos.z - b->pos.z;
            float r  = AS->radius + BS->radius;
            float d2 = dx * dx + dz * dz;
            if (d2 >= r * r)
                continue;

            /* EXACTLY ON TOP OF EACH OTHER HAS NO DIRECTION, and two monsters
               spawned at one marker are exactly that. Any fixed direction
               would do; index order is the one that is the same on every
               machine, which is what a demo replaying needs.
               *정확히 서로 위에 있는 것에는 방향이 없으며*, 한 표식에 생성된 둘이 바로
               그것입니다. 고정된 방향이면 무엇이든 되고, 인덱스 순서는 어느 기계에서나 같은
               것입니다. 데모의 재생이 필요로 하는 것이 그것입니다. */
            float d = sqrtf(d2);
            float ux, uz;
            if (d > 1e-4f) { ux = dx / d; uz = dz / d; }
            else           { ux = 1.0f;  uz = 0.0f; d = 0.0f; }

            float over = r - d;
            float push = over * MON_PUSH_RATE * dt;
            if (push > over * 0.5f)
                push = over * 0.5f;

            move_toward(pl, l, AS, a, player_eye,  ux * push,  uz * push);
            move_toward(pl, l, BS, b, player_eye, -ux * push, -uz * push);
        }
    }
}

int enemy_update(Pools *pl, const Level *l, v3 player_eye, float dt)
{
    int player_damage = shots_update(pl, l, player_eye, dt);

    /* Before the monsters are stepped, so one made this frame gets its first
       frame this frame rather than standing still for one.
       몬스터를 진행시키기 전입니다. 이번 프레임에 만들어진 몬스터가 한 프레임을 가만히 서
       있는 대신 이번 프레임에 첫 프레임을 얻도록 합니다. */
    spawners_update(pl, l, player_eye, dt);

    for (int i = 0; i < pl->enemy.count; i++)
    {
        Enemy *m = &pl->enemy.m[i];
        if (!m->active)
            continue;
        const MonType *S = &TYPES[m->type];

        if (m->foe_time > 0.0f) m->foe_time -= dt;

        /* Resolved ONCE, so every question this frame asks about the same
           thing. Splitting it -- chasing one point and shooting at another --
           is the bug this variable exists to make impossible.
           *한 번만 해결하므로* 이 프레임의 모든 질문이 같은 것에 대해 묻습니다. 나누는 것(한
           점을 쫓으며 다른 점을 쏘는 것)이 이 변수가 불가능하게 만들려고 존재하는 버그입니다. */
        v3 goal = foe_point(pl, m, player_eye);

        m->anim += dt;
        if (m->flash > 0.0f)
            m->flash -= dt * 4.0f;

        /* A CORPSE IS STILL SUBJECT TO THE FLOOR, and this `continue` used to
           skip that along with everything else. What follows is what a monster
           DOES, and a dead one does nothing -- but falling is not something it
           does, it is something done to it, and leaving the frame here left it
           out.

           On the ground the mistake was invisible: what dies standing on the
           floor is already on the floor, so the corpse lay where it should. The
           caster is the only kind that is somewhere else when it dies. It died
           six metres up and stayed there, a sprite pinned to the air at the
           height it was shot, for as long as the level lasted.

           ::monster_fall is the same block the living run at the bottom of this
           loop, called from both places rather than copied, so a body and a
           corpse can never come to disagree about gravity. ::holds_height is
           what differs between them, and it is where the caster stops flying.

           시체도 여전히 바닥의 지배를 받으며, 이 `continue`는 다른 모든 것과 함께 그것까지
           건너뛰었습니다. 뒤따르는 것은 몬스터가 *하는* 일이고 죽은 것은 아무것도 하지
           않습니다. 그러나 낙하는 그것이 하는 일이 아니라 그것에게 일어나는 일이며, 이곳에서
           프레임을 떠나면 그것이 빠집니다.

           지상에서는 이 실수가 보이지 않았습니다. 바닥에 서서 죽는 것은 이미 바닥에 있으므로
           시체는 있어야 할 자리에 누웠습니다. 죽을 때 다른 곳에 있는 종류는 캐스터뿐입니다.
           캐스터는 6미터 위에서 죽어 그 자리에 머물렀습니다. 총에 맞은 높이에 스프라이트가
           공중에 박힌 채로, 레벨이 끝날 때까지 말입니다.

           ::monster_fall은 이 루프 아래쪽에서 살아 있는 것이 실행하는 바로 그 블록이며,
           복제하지 않고 양쪽에서 호출합니다. 그래야 몸과 시체가 중력에 대해 서로 다른 말을
           하게 될 수 없습니다. 둘 사이에서 달라지는 것은 ::holds_height이고, 캐스터가 나는
           것을 그만두는 곳이 그곳입니다. */
        if (m->state == E_DEAD)
        {
            if (m->timer > 0.0f)
                m->timer -= dt;
            if (!holds_height(S, m))
                monster_fall(l, S, m, dt);
            continue;
        }

        /* --- pay what a ward owes ---------------------------------------
         *
         * ::enemy_hurt recorded the debt and could not pay it: it has no
         * ::Level for ::make_monster's ground search and no frame in which to
         * count a telegraph down. This loop has both, which is the whole reason
         * the debt is a field rather than a call.
         *
         * TELEGRAPHED, using the spawners' own two-step. A monster that appears
         * where the player was already looking was never fair, and a ward is
         * shot from close range by definition. The effect plays at the arrival
         * point and the monster follows ::SPAWN_WARN_TIME later.
         *
         * The cap is checked at ARRIVAL rather than when the debt is taken on,
         * so a room that was full when the ward was shot still delivers once it
         * has emptied. Refusing at accrual time would silently forgive a debt
         * the player has already paid for in ammunition.
         *
         * --- 결계핵이 빚진 것을 갚는다 ------------------------------------
         *
         * ::enemy_hurt는 빚을 기록했을 뿐 갚을 수 없었습니다. ::make_monster의 지면 탐색에
         * 필요한 ::Level도 없고, 예고를 세어 내릴 프레임도 없습니다. 이 루프는 둘 다 가지며,
         * 그것이 그 빚이 호출이 아니라 필드인 이유 전부입니다.
         *
         * 스포너 자신의 2단계를 써서 *예고합니다.* 플레이어가 이미 보고 있던 자리에 나타나는
         * 몬스터는 애초에 공정한 적이 없고, 결계핵은 정의상 가까이에서 쏘게 됩니다. 도착
         * 지점에서 이펙트가 재생되고 ::SPAWN_WARN_TIME 뒤에 몬스터가 따라옵니다.
         *
         * 상한은 빚을 질 때가 아니라 *도착할 때* 검사합니다. 결계핵을 쏘던 시점에 방이 가득
         * 찼더라도 비고 나면 배달됩니다. 누적 시점에 거절하면 플레이어가 이미 탄약으로 값을
         * 치른 빚을 조용히 탕감하게 됩니다. */
        if (m->summon_left > 0)
        {
            if (m->summon_warn <= 0.0f)
            {
                /* THE SPAWNER'S OWN PAIR, not effects of its own. A portal is a
                   portal wherever it opens, and a ward that announced itself
                   differently would be teaching the player a second vocabulary
                   for the same event. fx_spawn takes a bare position, so none
                   of this needs a ::Spawner.
                   스포너 자신의 쌍이며 자기만의 이펙트가 아닙니다. 관문은 어디서 열리든
                   관문이고, 다르게 자신을 알리는 결계핵은 같은 사건에 대한 두 번째 어휘를
                   플레이어에게 가르치는 셈입니다. fx_spawn은 위치만 받으므로 이 중 무엇도
                   ::Spawner를 필요로 하지 않습니다. */
                m->summon_warn = SPAWN_WARN_TIME;
                v3 warn_at = ward_summon_at(m);
                fx_spawn(pl, "spawnwarp", warn_at, v3f(0, 1, 0));
                fx_spawn(pl, "spawnring", warn_at, v3f(0, 1, 0));
                play_at(warn_at, "spawnwarn", 70);
            }
            else if ((m->summon_warn -= dt) <= 0.0f)
            {
                /* BOTH COMPUTED BEFORE THE DECREMENT, so the arrival lands
                   where the telegraph above played: ::ward_summon_at is a
                   function of ::Enemy::summon_left, and taking one off it first
                   would put the monster somewhere the warning never pointed.
                   둘 다 감소 *전에* 계산합니다. 그래야 도착이 위의 예고가 재생된 자리에
                   떨어집니다. ::ward_summon_at은 ::Enemy::summon_left의 함수이므로, 먼저 하나를
                   빼면 예고가 가리킨 적 없는 자리에 몬스터를 놓게 됩니다. */
                v3 at = ward_summon_at(m);

                /* DRAWN WHETHER OR NOT IT IS USED. The refusal below depends on
                   how full the room happens to be, and a draw that a full room
                   skips makes the generator advance by a different amount in a
                   replay than it did in the recording. That is exactly the trap
                   the drop table's two rolls are spent to avoid; see the note in
                   ::enemy_hurt's death branch.
                   쓰이든 쓰이지 않든 뽑습니다. 아래의 거절은 방이 마침 얼마나 찼는지에
                   달려 있고, 가득 찬 방이 건너뛰는 뽑기는 재생에서 생성기를 기록 때와 다른
                   만큼 전진시킵니다. 드롭 표가 두 굴림을 모두 소비해 피하는 함정이 정확히
                   그것입니다. ::enemy_hurt 사망 분기의 주석을 참조하십시오. */
                int type = ward_summon_type(pl, m);

                m->summon_warn = 0.0f;
                m->summon_left--;

                if (enemy_alive(pl) < WARD_SUMMON_CAP)
                {
                    make_monster(pl, l, type, at.x, at.y, at.z);
                    fx_spawn(pl, "spawnburst", at, v3f(0, 1, 0));
                    play_at(at, "spawnpop", 80);
                }
            }
        }

        /* AN INERT MONSTER IS DONE FOR THE FRAME, and the `continue` is the
           whole implementation of ::AI_INERT. Without it a ward falls through
           to the state machine below, where E_CHASE dispatches "caster or
           brawler" with no third answer -- so a ward shot once would leave
           E_IDLE, be classified as a brawler, and walk at the player with zero
           reach. Placed after the summon drain because paying its debt is the
           only thing a ward does; placed before the yaw so it does not even
           turn to look.
           행동하지 않는 몬스터는 이 프레임에서 끝이며, 이 `continue`가 ::AI_INERT 구현의
           전부입니다. 이것이 없으면 결계핵이 아래의 상태 기계로 떨어지는데, 그곳의 E_CHASE는
           세 번째 답이 없는 "캐스터냐 근접형이냐"로 분기합니다. 그래서 한 번 맞은 결계핵은
           E_IDLE을 떠나 근접형으로 분류되고 사거리 0으로 플레이어를 향해 걸어옵니다. 소환
           배출 뒤에 두는 이유는 빚을 갚는 것이 결계핵이 하는 유일한 일이기 때문이고, 조준
           앞에 두는 이유는 쳐다보기 위해 돌지조차 않게 하기 위해서입니다. */
        if (S->behaviour == AI_INERT)
            continue;

        /* Age the cached sight reading. Counted down here, once, rather than
           inside sees_player: the two polling sites below may each ask in the
           same frame, and a decrement per ASK would retire the reading in one
           frame instead of SIGHT_PERIOD. Decremented after the E_DEAD skip
           above, because a corpse asks nothing and refreshing for it would be
           the whole saving spent on monsters that no longer look at anything.
           캐시된 시야 판정 결과를 노화시킵니다. sees_player 안이 아니라 이곳에서 한 번만
           감소시킵니다. 아래의 두 폴링 지점이 같은 프레임에 각각 물어볼 수 있는데, *질문*
           마다 감소시키면 결과가 SIGHT_PERIOD가 아니라 한 프레임 만에 만료됩니다. 위의
           E_DEAD 건너뛰기 뒤에 두는 이유는 시체는 아무것도 묻지 않으며, 시체를 위해
           갱신하는 것은 더 이상 아무것도 보지 않는 몬스터에 절감분을 전부 쓰는
           일이기 때문입니다. */
        if (m->sight_age > 0)
            m->sight_age--;

        v3 to = v3sub(goal, v3f(m->pos.x, m->pos.y + S->eye, m->pos.z));
        float dist = sqrtf(to.x * to.x + to.z * to.z);

        /* WHERE IT WANTS TO LOOK, which is no longer the same as where it is
           looking. This line used to be `m->yaw = atan2f(...)` -- a monster
           faced the player exactly, every frame, however fast the player
           circled it. Nothing could ever get behind anything, so strafing won
           no angle and the whole of Quake's manoeuvring had nothing to bite
           on. change_yaw below turns towards this at the monster's own rate.
           *보고 싶은* 방향이며, 이제 보고 있는 방향과 같지 않습니다. 이 줄은 원래
           `m->yaw = atan2f(...)`였습니다. 플레이어가 아무리 빨리 돌아도 몬스터는 매 프레임
           정확히 플레이어를 향했습니다. 무엇도 무엇의 뒤를 잡을 수 없었으므로 횡이동은
           어떤 각도도 얻지 못했고, Quake식 기동 전체가 물고 늘어질 것이 없었습니다.
           아래의 change_yaw가 몬스터 자신의 속도로 이 방향을 향해 돕니다. */
        m->ideal_yaw = atan2f(-to.x, -to.z);
        change_yaw(m, S->yaw_speed, dt);

        if (m->pain_wait > 0.0f)
            m->pain_wait -= dt;
        if (m->attack_wait > 0.0f)
            m->attack_wait -= dt;
        if (m->slide_wait > 0.0f)
            m->slide_wait -= dt;

        switch (m->state)
        {
        case E_IDLE:
            /* Cached: noticing the player a frame or two late is imperceptible,
               and the distance test in front of it means a monster out of sight
               range never pays for the trace at all.
               캐시를 씁니다. 플레이어를 한두 프레임 늦게 알아채는 것은 지각되지 않으며,
               앞의 거리 검사 덕분에 시야 거리 밖의 몬스터는 판정 비용을 아예 치르지
               않습니다. */
            if (dist < S->sight && sees_player(pl, l, m, goal))
            {
                m->state = E_CHASE;
                play_at(m->pos, "sight", 80);
            }
            break;

        case E_CHASE:
            /* One question, asked once, of the column that answers it. The
               three `shot_speed > 0` tests this replaces each meant "is this a
               caster?" and each would have had to be found again the day a
               third archetype arrived.
               하나의 질문을, 그에 답하는 열에게, 한 번 묻습니다. 이것이 대체하는 세 개의
               `shot_speed > 0` 검사는 각각 "이것은 캐스터인가"를 뜻했고, 세 번째 아키타입이
               생기는 날 각각을 다시 찾아내야 했을 것입니다. */
            if (S->behaviour == AI_CASTER)
                chase_caster(pl, l, S, m, to, dist, goal, dt);
            else
                chase_brawler(pl, l, S, m, to, dist, player_eye, dt);
            break;

        case E_ATTACK:
            m->timer += dt;
            {
                /* THE VOLLEY, ONE BOLT PER ::MonType::shot_gap. A `while` and
                   not an `if`, because a gap shorter than a frame must not
                   throttle the stream to one bolt per frame -- and a gap of ZERO
                   is how the maw still fires all five of its at once, which is
                   the behaviour every caster had before the water spirit stopped
                   firing a shotgun. No branch says "together" anywhere; it falls
                   out of the arithmetic.
                   *일제 사격이며, ::MonType::shot_gap마다 한 발입니다.* `if`가 아니라
                   `while`인 이유는, 프레임보다 짧은 간격이 줄기를 프레임당 한 발로 조여서는 안
                   되기 때문입니다. 그리고 간격 *0*이 아귀가 여전히 다섯 발을 한꺼번에 쏘는
                   방식이며, 그것은 물의 정령이 산탄을 그만두기 전까지 모든 캐스터가 하던
                   행동입니다. "함께"라고 말하는 분기는 어디에도 없습니다. 산술에서 떨어져
                   나옵니다. */
                int shots = m->volley_n > 0 ? m->volley_n : 1;

                /* THE SLOT, RESOLVED ONCE. ::Enemy::atk was chosen when this
                   state was entered and every line below reads it rather than
                   the type -- the archetype no longer decides what an attack
                   IS, only where the monster stands while looking for one.
                   A slot that has gone missing (a save from an older table, a
                   type edited under a running fight) falls back to slot 0
                   rather than dereferencing nothing: an attack that plays the
                   wrong animation is a bug, and one that crashes is a crash.
                   *슬롯을 한 번 해결합니다.* ::Enemy::atk은 이 상태에 들어갈 때 골랐고 아래의
                   모든 줄이 종류가 아니라 그것을 읽습니다. 아키타입은 이제 공격이 *무엇인지*
                   정하지 않고, 몬스터가 공격을 찾는 동안 어디에 서는지만 정합니다.
                   사라진 슬롯(옛 표의 세이브, 전투 중에 편집된 종류)은 아무것도 아닌 것을
                   역참조하는 대신 슬롯 0으로 물러납니다. 틀린 동작을 재생하는 공격은 버그이고
                   죽는 공격은 죽음입니다. */
                const MonAttack *A = mon_attack(m->type, m->atk);
                if (!A) A = mon_attack(m->type, 0);
                if (!A) { m->state = E_CHASE; break; }

                /* THE CHARGE, WHILE THE POSE IS UP. `caster_attack` and
                   `water_spirit_attack` are drawings of a creature gathering
                   itself -- the caster's has a magic circle in it -- and for
                   the whole of the wind-up that drawing simplayer_eye stood there. A
                   pose is a still; what says CHARGING is something arriving.
                   BEFORE THE FIRST BOLT AND NOT AFTER. `swung` counts what has
                   left, so `!m->swung` is exactly the window the pose occupies
                   -- the same test scene.c uses to decide the frame, from the
                   other side of the line. Once the volley starts the motes
                   would be arriving at a circle that has already fired.
                   AT THE CHEST, not the feet: ::MonType::eye is where the bolt
                   is released from, so the gather lands where the bolt leaves.
                   *자세가 올라와 있는 동안의 충전입니다.* `caster_attack`과
                   `water_spirit_attack`은 생물이 힘을 모으는 그림이고(캐스터의 것에는 마법진이
                   있습니다), 준비동작 내내 그 그림은 그냥 서 있었습니다. 자세는 정지 화면이며,
                   *충전 중*이라고 말하는 것은 무언가가 도착하는 일입니다.
                   *첫 탄환 전이고 후가 아닙니다.* `swung`은 이미 떠난 것을 세므로 `!m->swung`은
                   정확히 그 자세가 차지하는 창입니다. scene.c가 프레임을 정할 때 쓰는 것과 같은
                   검사를 선 반대편에서 하는 것입니다. 일제사격이 시작되면 그 알갱이들은 이미
                   발사한 마법진에 도착하게 됩니다.
                   *발이 아니라 가슴에서*입니다. ::MonType::eye가 탄환이 떠나는 높이이므로,
                   모임은 탄환이 떠나는 자리에 내려앉습니다. */
                /* THE STEP INTO THE SWING, which is Quake's ai_charge inside
                   the attack frames. change_yaw above already turns the monster
                   toward the player every frame whatever it is doing -- that is
                   ai_charge's other half -- and this is the half that was
                   missing. Before the wind-up ends only: a swing that kept
                   closing through its own follow-through would walk the monster
                   past the player it had just hit.
                   *휘두르기 안으로 내딛는 걸음*이며, 공격 프레임 안의 Quake ai_charge입니다.
                   위의 change_yaw는 이미 몬스터가 무엇을 하든 매 프레임 플레이어 쪽으로
                   돌립니다. 그것이 ai_charge의 나머지 절반이고, 빠져 있던 것이 이 절반입니다.
                   준비동작이 끝나기 전까지만입니다. 자기 마무리 동작 내내 계속 붙는 휘두르기는
                   몬스터를 방금 때린 플레이어 너머로 걸어가게 합니다. */
                if (A->kind == ATK_SWING && m->timer < A->windup)
                {
                    float gx = goal.x - m->pos.x, gz = goal.z - m->pos.z;
                    float gd = sqrtf(gx * gx + gz * gz);
                    if (gd > 0.001f)
                    {
                        float step = S->speed * MON_SWING_CLOSE * dt / gd;
                        move_toward(pl, l, S, m, player_eye, gx * step, gz * step);
                    }
                }

                if (A->kind == ATK_BOLT && !m->swung)
                {
                    m->cast_timer -= dt;
                    if (m->cast_timer <= 0.0f)
                    {
                        m->cast_timer = CAST_GATHER_INTERVAL;
                        fx_spawn(pl, "castgather",
                                 v3f(m->pos.x, m->pos.y + S->eye, m->pos.z),
                                 v3f(0.0f, 1.0f, 0.0f));
                    }
                }

                if (A->kind == ATK_BOLT)
                {
                    while (m->swung < shots &&
                           m->timer >= A->windup + (float)m->swung * A->shot_gap)
                    {
                        release_bolt(pl, l, S, A, m, goal);
                        m->swung++;
                    }
                }
                else if (!m->swung && m->timer >= A->windup)
                {
                    m->swung = 1;
                    player_damage += release_swing(A, m, dist);
                }

                /* The cooldown starts after the LAST bolt, not after the first.
                   A stream that began its rest while still firing would let the
                   next volley overlap this one, and two overlapping volleys from
                   one monster is a wall of bolts nobody authored.
                   경직은 첫 볼트가 아니라 *마지막* 볼트 뒤에 시작합니다. 아직 쏘는 중에 휴식을
                   시작하는 줄기는 다음 일제 사격이 이번 것과 겹치게 만들고, 한 몬스터에게서
                   겹쳐 나오는 두 일제 사격은 아무도 저작하지 않은 볼트의 벽입니다. */
                float firing = (float)(shots - 1) * A->shot_gap;

            if (m->timer >= A->windup + firing + A->cooldown)
            {
                /* Quake's SUB_AttackFinished(2*random()): a RANDOM rest before
                   the next attack is even considered, on top of the animation's
                   own cooldown. Fixed rests make a group of monsters fire in a
                   chorus forever, because nothing ever pushes them out of step
                   once they have fallen into it. */
                m->attack_wait = frand(&pl->enemy) * MON_ATTACK_REST;

                /* A caster always returns to its band; a brawler that is still
                   in reach swings again without paying for the walk back.
                   캐스터는 언제나 자기 대역으로 돌아갑니다. 아직 사거리 안에 있는 근접형은
                   되돌아오는 비용을 치르지 않고 다시 휘두릅니다. */
                /* A BOLT ALWAYS RETURNS TO THE BAND; A SWING STILL IN REACH
                   SWINGS AGAIN, and it is the SLOT that says which -- a caster
                   that has just swung at something in its face is a brawler for
                   as long as the thing stays there.
                   *볼트는 언제나 대역으로 돌아가고 사거리 안의 휘두르기는 다시 휘두르며*,
                   어느 쪽인지는 *슬롯*이 말합니다. 코앞의 것에 방금 휘두른 캐스터는 그것이
                   거기 머무는 동안 근접형입니다. */
                if (A->kind == ATK_BOLT)
                    m->state = E_CHASE;
                else if (dist <= A->max)
                {
                    m->timer = 0.0f;
                    m->swung = 0;
                }
                else
                    m->state = E_CHASE;
            }
            }
            break;

        case E_HURT:
            m->timer -= dt;
            if (m->timer <= 0.0f)
                m->state = E_CHASE;
            break;

        default:
            break;
        }

        /* --- what holds it up ---------------------------------------------
           THE FALL, and the two assumptions ::MON_FLIES and ::MON_ANCHORED
           exist to vary. The whole of the question is ::holds_height, asked
           here and in the ::E_DEAD branch above -- two call sites because the
           two answers differ, which is the point: a flyer stops flying when it
           dies, and a thing bolted to a wall does not come off it.

           The rate lives in ::monster_fall and is ::PLAYER_GRAVITY rather than
           a literal, which it was -- a bare 22.0f here, the same number
           player.h names, with nothing tying them together. Retune the player's
           snappier-than-real fall and the monsters would have kept the old one,
           silently. They fall the same way because they are in the same world;
           if a kind ever needs its own rate, that is a column and not a second
           literal.

           무엇이 그것을 떠받치는가. *낙하*이며, ::MON_FLIES와 ::MON_ANCHORED가 달리하려고
           존재하는 두 가정입니다. 질문 전체가 ::holds_height이며, 이곳과 위의 ::E_DEAD
           분기에서 묻습니다. 호출 지점이 둘인 이유는 두 곳의 답이 다르기 때문이고, 그것이
           요점입니다. 비행체는 죽으면 나는 것을 그만두고, 벽에 박힌 것은 떨어져 나오지
           않습니다.

           비율은 ::monster_fall에 있으며 리터럴이 아니라 ::PLAYER_GRAVITY입니다. 이전에는
           리터럴이었습니다. 이곳의 맨 22.0f였고, player.h가 이름 붙인 것과 같은 숫자였으며,
           둘을 묶는 것이 없었습니다. 플레이어의 현실보다 경쾌한 낙하를 조정하면 몬스터는
           조용히 옛 값을 유지했을 것입니다. 그들이 같은 방식으로 떨어지는 이유는 같은 세계에
           있기 때문입니다. 어떤 종류가 자기 비율을 필요로 하게 된다면 그것은 열이지 두 번째
           리터럴이 아닙니다. */
        if (holds_height(S, m))
            continue;

        monster_fall(l, S, m, dt);
    }

    separate_monsters(pl, l, player_eye, dt);
    return player_damage;
}

int enemy_count(const Pools *pl) { return pl->enemy.count; }

int enemy_alive(const Pools *pl)
{
    int n = 0;
    for (int i = 0; i < pl->enemy.count; i++)
        if (pl->enemy.m[i].active && pl->enemy.m[i].state != E_DEAD)
            n++;
    return n;
}

/* The same count as ::enemy_alive, narrowed to one kind. A scan rather than
   a running total kept on the pool: a total has to be decremented on every
   road out of being alive -- death, eviction, reset, a wave clearing the room
   -- and one missed road is a kind that can never spawn again, which reads as
   a monster that vanished from the game rather than as a counter that drifted.
   ::ENEMY_MAX is small enough that the scan is not worth the risk.
   ::enemy_alive와 같은 세기를 한 종류로 좁힌 것입니다. 풀에 누적 합계를 두지 않고 훑는
   이유는, 합계는 살아 있음에서 나가는 *모든* 길에서 줄여야 하고(죽음, 퇴출, 초기화, 방을
   비우는 웨이브) 길 하나를 놓치면 그 종류가 다시는 스폰될 수 없기 때문입니다. 그것은 어긋난
   계수기가 아니라 게임에서 사라진 몬스터로 읽힙니다. ::ENEMY_MAX는 훑기가 그 위험만 못할
   만큼 작습니다. */
int enemy_alive_of(const Pools *pl, int type)
{
    int n = 0;
    for (int i = 0; i < pl->enemy.count; i++)
        if (pl->enemy.m[i].active && pl->enemy.m[i].state != E_DEAD &&
            pl->enemy.m[i].type == type)
            n++;
    return n;
}

const Enemy *enemy_at(const Pools *pl, int i)
{
    return (i >= 0 && i < pl->enemy.count) ? &pl->enemy.m[i] : 0;
}

int enemy_shot_count(const Pools *pl) { (void)pl; return ENEMY_MAX_SHOTS; }

const Shot *enemy_shot_at(const Pools *pl, int i)
{
    return (i >= 0 && i < ENEMY_MAX_SHOTS) ? &pl->enemy.shots[i] : 0;
}

int enemy_hitscan(const Pools *pl, v3 o, v3 d, float maxdist, float *out_t, int *out_idx)
{
    float best = maxdist;
    int hit = -1;

    for (int i = 0; i < pl->enemy.count; i++)
    {
        const Enemy *m = &pl->enemy.m[i];
        if (!m->active || m->state == E_DEAD)
            continue;
        const MonType *S = &TYPES[m->type];

        float ex = o.x - m->pos.x, ez = o.z - m->pos.z;
        float a = d.x * d.x + d.z * d.z;
        if (a < 1e-6f)
            continue;
        float b = 2.0f * (ex * d.x + ez * d.z);
        float cc = ex * ex + ez * ez - S->radius * S->radius;
        float disc = b * b - 4.0f * a * cc;
        if (disc < 0.0f)
            continue;

        float t = (-b - sqrtf(disc)) / (2.0f * a);
        if (t < 0.0f)
            t = (-b + sqrtf(disc)) / (2.0f * a);
        if (t < 0.0f || t >= best)
            continue;

        float y = o.y + d.y * t;
        if (y < m->pos.y || y > m->pos.y + S->height)
            continue;

        best = t;
        hit = i;
    }

    if (hit < 0)
        return 0;
    *out_t = best;
    *out_idx = hit;
    return 1;
}

void enemy_hurt(Pools *pl, int idx, int dmg, v3 dir)
{
    enemy_hurt_by(pl, idx, dmg, dir, -1);
}

void enemy_hurt_by(Pools *pl, int idx, int dmg, v3 dir, int from)
{
    if (idx < 0 || idx >= pl->enemy.count)
        return;
    Enemy *m = &pl->enemy.m[idx];
    if (!m->active || m->state == E_DEAD)
        return;

    /* WHO IT IS ANGRY AT NOW, decided before the damage lands so a killing blow
       still records it -- a monster that dies facing its killer is the picture
       the player is owed.
       QUAKE'S RULE, from combat.qc: the victim retargets onto its attacker
       unless `targ.classname == attacker.classname`. The DAMAGE is not exempted
       -- two of a kind crossing fire still hurt each other, they just do not
       take it personally -- so this sits beside the subtraction rather than in
       front of it.
       AND THE PLAYER CANCELS IT. `from` of -1 puts the grudge back to nobody,
       the nearest thing here to Quake's `oldenemy`: a monster you have started
       shooting should be coming for you.
       *지금 무엇에 화가 나 있는지*를 피해가 꽂히기 전에 정합니다. 치명타도 그것을 기록하게
       하기 위해서이며, 자기를 죽인 것을 마주 보고 죽는 몬스터는 플레이어가 받아야 할 그림입니다.
       *combat.qc의 Quake 규칙입니다.* 피해자는 `targ.classname == attacker.classname`이 아닌
       한 가해자로 표적을 돌립니다. *피해*는 면제되지 않습니다. 같은 종류 둘이 서로의 사격에
       걸리면 여전히 서로를 다치게 하며 다만 개인적으로 받아들이지 않을 뿐이므로, 이것은 뺄셈
       앞이 아니라 그 곁에 있습니다.
       *그리고 플레이어가 취소합니다.* `from`이 -1이면 원한은 아무에게도 없는 상태로 돌아갑니다.
       당신이 쏘기 시작한 몬스터는 당신에게 와야 합니다. */
    if (from < 0) {
        m->foe = -1;
        m->foe_time = 0.0f;
    } else if (from != idx && from < pl->enemy.count &&
               pl->enemy.m[from].type != m->type) {
        m->foe = (short)from;
        m->foe_time = INFIGHT_TIME;
    }

    /* Centre of mass rather than the feet, so the spray leaves the body and
       not the floor underneath it. Enemy stores only what varies per instance,
       so the height comes from the type table.
       발이 아니라 몸통 중심입니다. 그래야 분출이 발밑 바닥이 아니라 몸에서 나옵니다.
       Enemy는 개체별로 달라지는 값만 보관하므로 신장은 종류 테이블에서 가져옵니다. */
    const MonType *S = &TYPES[m->type];
    v3 mid = v3f(m->pos.x, m->pos.y + S->height * 0.5f, m->pos.z);

    /* --- warded: the blow does not land ---------------------------------
     *
     * BEFORE THE HEALTH AND BEFORE THE FLASH, and both of those are the point.
     * The hit flash is how this game says "you are hurting it", so a gate placed
     * after it would tell the player they are making progress on a boss that
     * cannot be hurt -- and then the health bar, which correctly would not move,
     * reads as broken. The bounce effect is not decoration; it is the sentence
     * the flash would otherwise have spoken wrongly.
     *
     * HERE AND NOWHERE ELSE, for the reason the death tally is counted here and
     * nowhere else: a damage source added later has to go through this function
     * to exist at all. The hook's arrival damage, the grenade's splash and each
     * of the shotgun's six pellets obey a rule none of them has heard of.
     *
     * --- 결계에 막힘: 타격이 닿지 않는다 -----------------------------------
     *
     * *체력보다 먼저, 그리고 섬광보다 먼저이며*, 그 둘이 요점입니다. 피격 섬광은 이 게임이
     * "네가 그것을 아프게 하고 있다"고 말하는 방식이므로, 그 뒤에 둔 차단은 아프게 할 수 없는
     * 보스에서 진척을 내고 있다고 플레이어에게 말하게 됩니다. 그러면 (올바르게) 움직이지 않는
     * 체력바가 고장으로 읽힙니다. 튕김 이펙트는 장식이 아니라, 그러지 않았다면 섬광이 잘못
     * 말했을 그 문장입니다.
     *
     * *이곳에서만* 하는 이유는 사망 집계를 이곳에서만 세는 것과 같습니다. 나중에 추가되는
     * 피해원도 존재하려면 이 함수를 거쳐야 합니다. 훅의 도달 피해, 유탄의 폭발, 샷건의 여섯
     * 펠릿 각각이 들어 본 적 없는 규칙을 따릅니다. */
    if ((S->flags & MON_BOSS) && enemy_guards_alive(pl) > 0)
    {
        fx_spawn(pl, "warded", mid, v3f(0, 1, 0));
        return;
    }

    m->health -= dmg;
    m->flash = 1.0f;

    /* --- the cycle boundary ---------------------------------------------
     *
     * A BOSS CANNOT BE CARRIED PAST THE THIRD IT IS IN. Without this a grenade
     * volley landed in the first groggy window takes the maw from full to
     * nearly dead, skipping the second and third ward cycles entirely -- and
     * "three cycles" stops being a fact about the fight and becomes a thing
     * that happens to average players.
     *
     * The last boundary is zero -- see ::types_check on hp divisibility -- so
     * the final cycle kills through the ordinary branch below. There is no
     * second death path, which is what ::EnemyPool::deaths is documented to
     * require.
     *
     * --- 사이클 경계 --------------------------------------------------------
     *
     * *보스는 자신이 있는 3분의 1 구간을 넘어 끌려갈 수 없습니다.* 이것이 없으면 첫 그로기 창에
     * 쏟아부은 유탄이 아귀를 만피에서 빈사까지 데려가고 두 번째와 세 번째 결계 사이클을 통째로
     * 건너뜁니다. 그러면 "3사이클"은 전투에 대한 사실이기를 그만두고 평범한 플레이어에게나
     * 일어나는 일이 됩니다.
     *
     * 마지막 경계는 0입니다(체력 나누어떨어짐에 대한 ::types_check 참조). 따라서 마지막
     * 사이클은 아래의 평범한 분기를 통해 죽입니다. 두 번째 사망 경로는 없으며, 그것이
     * ::EnemyPool::deaths가 요구한다고 문서화된 바입니다. */
    if (S->flags & MON_BOSS)
    {
        int floor_hp = S->hp * (BOSS_CYCLES - pl->enemy.boss.cycle - 1) / BOSS_CYCLES;
        if (m->health < floor_hp) m->health = floor_hp;
    }

    /* `dir` is the direction the blow travelled, so the spray goes back along
       it -- toward whoever landed the hit. This parameter was accepted and
       discarded before there was anything to point.
       `dir`은 타격이 진행한 방향이므로 분출은 그 반대, 즉 타격을 가한 쪽으로 향합니다.
       가리킬 대상이 생기기 전까지 이 매개변수는 받기만 하고 버려졌습니다. */
    v3 back = v3len(dir) > 1e-4f ? v3scale(v3norm(dir), -1.0f) : v3f(0, 1, 0);

    if (m->health <= 0)
    {
        m->state = E_DEAD;
        m->timer = 0.6f;

        /* Counted HERE, at the transition, rather than by anyone looking at the
           pool later. This branch is the only way a monster becomes E_DEAD, so
           the tally cannot drift from the corpses -- and a corpse is recycled
           once it has faded, which is what makes counting dead slots wrong.
           나중에 풀을 들여다보는 누군가가 아니라, 상태가 바뀌는 *이곳*에서 셉니다. 몬스터가
           E_DEAD가 되는 길은 이 분기뿐이므로 집계가 시체와 어긋날 수 없습니다. 그리고 시체는
           다 사라지면 재활용되는데, 그것이 죽은 슬롯을 세는 방식이 틀린 이유입니다. */
        pl->enemy.deaths++;

        /* BOTH ROLLS ARE SPENT, whether or not the first one succeeds. The
           generator advances by exactly two per kill either way, so an author
           who retunes a `chance` in loot.txt changes what drops without
           changing the fight that follows -- every monster after this one
           still gets the numbers it was going to get. A `chance` that
           short-circuits the second roll makes the drop table an input to the
           AI, which is a demo that desynchronises the first time a rate moves.
           첫 굴림의 성패와 무관하게 *두 굴림을 모두 소비합니다.* 어느 쪽이든 생성기는 처치당
           정확히 둘씩 전진하므로, loot.txt의 `chance`를 조정하는 제작자는 무엇이 떨어지는지를
           바꾸되 그 뒤의 전투는 바꾸지 않습니다. 이 몬스터 다음의 모든 몬스터가 원래 받을
           숫자를 그대로 받습니다. 두 번째 굴림을 건너뛰는 `chance`는 드롭 표를 AI의 입력으로
           만들며, 그것은 확률이 처음 움직이는 순간 어긋나는 데모입니다. */
        float roll_any  = frand(&pl->enemy);
        float roll_what = frand(&pl->enemy);
        m->drop = loot_pick(loot_drop(m->type), roll_any, roll_what);

        play_at(m->pos, "edie", 95);
        fx_spawn(pl, "gib", mid, back);
        return;
    }

    fx_spawn(pl, "blood", mid, back);

    play_at(m->pos, "epain", 70);

    /* --- a ward pays for being shot -------------------------------------
     *
     * A THRESHOLD, NOT AN EVENT. "Summon when damaged" taken literally is one
     * summon per ::enemy_hurt call, and a point-blank shotgun blast is six of
     * those from one trigger pull -- the rapid gun is dozens a second, and a
     * grenade adds one per monster in its radius. ::ENEMY_MAX is 64 shared
     * between the living and the corpses, so that fills the pool in seconds and
     * then raises ::DIAG_ENEMY_CAP forever. In a release build that is
     * completely silent: diag.h is compiled out and the counters only ever
     * surface in a dev window title.
     *
     * Accruing damage instead makes the ward's cadence a number in this file
     * rather than a property of which gun the player happens to be holding. A
     * ninety-health ward pays exactly three times whatever kills it.
     *
     * AFTER THE DEATH BRANCH, deliberately. Placed before it, the last pellet
     * of the blast that finally destroys a ward would summon a group the player
     * has just earned the right not to fight -- the ward's final hit would read
     * as a punishment instead of as the reward it is. Destruction is its own
     * event with its own effect; this is what SURVIVING damage costs.
     *
     * --- 결계핵은 맞은 값을 치른다 -----------------------------------------
     *
     * *사건이 아니라 문턱입니다.* "피해를 입으면 소환"을 문자 그대로 받으면 ::enemy_hurt 호출
     * 하나당 한 번 소환인데, 근접 샷건 한 발은 방아쇠 한 번에 그 호출이 여섯입니다. 속사 무기는
     * 초당 수십이고 유탄은 반경 안의 몬스터마다 하나를 더합니다. ::ENEMY_MAX는 64이며 산 것과
     * 시체가 나누어 쓰므로, 몇 초 만에 풀이 차고 그 뒤로 영원히 ::DIAG_ENEMY_CAP만 올립니다.
     * 릴리스 빌드에서 그것은 완전히 조용합니다. diag.h가 컴파일에서 빠지고 카운터는 개발 빌드
     * 창 제목에만 나타납니다.
     *
     * 대신 피해를 누적하면 결계핵의 박자가 "플레이어가 마침 무슨 총을 들었나"의 성질이 아니라
     * 이 파일의 숫자가 됩니다. 체력 90인 결계핵은 무엇에 죽든 정확히 세 번 지급합니다.
     *
     * *사망 분기 뒤이며, 의도적입니다.* 그 앞에 두면 결계핵을 마침내 부수는 그 발의 마지막
     * 펠릿이, 플레이어가 방금 싸우지 않아도 될 권리를 얻은 무리를 부릅니다. 결계핵의 마지막
     * 한 발이 보상이 아니라 벌로 읽히게 됩니다. 파괴는 자기 이펙트를 가진 별개의 사건이고,
     * 이것은 *살아남은* 피해가 치르는 값입니다. */
    if (S->flags & MON_GUARD)
    {
        m->summon_dmg = (short)(m->summon_dmg + dmg);
        while (m->summon_dmg >= WARD_SUMMON_DMG)
        {
            m->summon_dmg = (short)(m->summon_dmg - WARD_SUMMON_DMG);
            m->summon_left = (short)(m->summon_left + WARD_SUMMON_COUNT);
        }
    }

    /* QUAKE'S pain_finished. Every hit used to restart the flinch, so a weapon
       that fires faster than the flinch lasts held a monster still until it
       died -- it never got a frame in which it was allowed to act. The rapid
       gun fires every 0.085s against a 0.16s flinch, so that was already
       reachable, and raising walking speed to 10.8 made getting into position
       to do it trivial.
       Waking a sleeping monster is exempt: a monster shot from across a room it
       has not noticed still has to notice.
       Quake의 pain_finished입니다. 이전에는 매 피격이 경직을 다시 시작했으므로, 경직보다
       빠르게 발사되는 무기는 몬스터가 죽을 때까지 붙잡아 두었습니다. 행동할 수 있는
       프레임을 한 번도 얻지 못했습니다. 속사 무기는 0.16초 경직에 0.085초마다 발사되므로
       이미 도달 가능했고, 이동 속도가 10.8이 되면서 그 자리를 잡는 것이 쉬워졌습니다.
       자고 있던 몬스터를 깨우는 것은 예외입니다. 알아채지 못한 방 건너에서 총을 맞은
       몬스터는 그래도 알아채야 합니다. */
    if (m->state == E_IDLE)
    {
        m->state = E_HURT;
        m->timer = 0.16f;
        m->pain_wait = S->pain_lock;
    }
    else if (m->state == E_CHASE && m->pain_wait <= 0.0f)
    {
        m->state = E_HURT;
        m->timer = 0.16f;
        m->pain_wait = S->pain_lock;
    }
}

int enemy_take_kills(Pools *pl)
{
    int n = pl->enemy.deaths;
    pl->enemy.deaths = 0;
    return n;
}

/* --- the boss fight / 보스전 --------------------------------------------- */

int enemy_boss_index(const Pools *pl)
{
    for (int i = 0; i < pl->enemy.count; i++) {
        const Enemy *m = &pl->enemy.m[i];
        if (!m->active || m->state == E_DEAD) continue;
        if (TYPES[m->type].flags & MON_BOSS) return i;
    }
    return -1;
}

int enemy_guards_alive(const Pools *pl)
{
    int n = 0;
    for (int i = 0; i < pl->enemy.count; i++) {
        const Enemy *m = &pl->enemy.m[i];
        if (!m->active || m->state == E_DEAD) continue;
        if (TYPES[m->type].flags & MON_GUARD) n++;
    }
    return n;
}

int enemy_alive_minions(const Pools *pl)
{
    int n = 0;
    for (int i = 0; i < pl->enemy.count; i++) {
        const Enemy *m = &pl->enemy.m[i];
        if (!m->active || m->state == E_DEAD) continue;
        if (TYPES[m->type].flags & (MON_BOSS | MON_GUARD)) continue;
        n++;
    }
    return n;
}

/* Where the next monster this ward owes will appear.
 *
 * A FUNCTION OF ::Enemy::summon_left AND NOTHING ELSE, which is what lets the
 * telegraph and the arrival agree without either storing a position: both call
 * this with the same debt outstanding and get the same point. Successive
 * summons walk the golden angle so a ward that pays three times does not stack
 * three monsters on one spot, and it costs no draw from the pool generator --
 * a position that consumed randomness would have to be spent whether or not the
 * summon was refused, which is a rule easy to state and easy to forget.
 *
 * 이 결계핵이 빚진 다음 몬스터가 나타날 자리입니다.
 *
 * *::Enemy::summon_left만의 함수이며*, 그것이 예고와 도착이 어느 쪽도 위치를 저장하지 않고
 * 일치하게 하는 방법입니다. 둘 다 같은 빚이 남은 상태에서 이것을 부르고 같은 점을 받습니다.
 * 연속된 소환은 황금각을 따라 걸으므로 세 번 지급하는 결계핵이 한자리에 세 마리를 쌓지 않으며,
 * 풀 생성기에서 뽑는 비용이 전혀 없습니다. 난수를 소비하는 위치는 소환이 거절되든 아니든
 * 소비되어야 하는데, 그것은 진술하기 쉽고 잊기도 쉬운 규칙입니다. */
static v3 ward_summon_at(const Enemy *m)
{
    float a = (float)m->summon_left * 2.39996f;   /* golden angle, radians */
    return v3f(m->pos.x + cosf(a) * WARD_SUMMON_DIST,
               m->pos.y,
               m->pos.z + sinf(a) * WARD_SUMMON_DIST);
}

/* Which monster this ward's table sends.
 *
 * TWO TABLES, TWO PAIRS, AND THE WATER SPIRIT IS IN BOTH. The air ward is the
 * only thing in the game that can put a ::MON_FLIES creature in the room, and
 * the ground ward is the only thing that sends the brute; the baseline appears
 * on both sides because it is the baseline -- a pair with one entry is a
 * constant, and a boss cycle that always sends the same creature from a marker
 * stops being a draw. That is the whole of what ::Enemy::ward_table means and
 * the whole of what an author is choosing when they place one marker rather
 * than the other.
 *
 * A THREE-ROW BESTIARY IS WHY THE OVERLAP IS HERE rather than a third table.
 * These were two disjoint pairs when there were five kinds to draw from. There
 * are three, one of them flies, and inventing a fourth creature to keep the
 * tables disjoint would be the tables writing the bestiary.
 *
 * EXACTLY ONE DRAW, ALWAYS. The caller spends it whether or not the summon is
 * refused for a full room; see the note at that call site.
 *
 * 이 결계핵의 표가 보내는 몬스터입니다.
 *
 * *두 표, 두 쌍이며, 물의 정령은 양쪽에 있습니다.* 공중형 결계핵은 이 게임에서 ::MON_FLIES
 * 생물을 방에 들일 수 있는 유일한 것이고, 지상형 결계핵은 브루트를 보내는 유일한 것입니다.
 * 기준 몬스터가 양쪽에 나타나는 이유는 그것이 기준이기 때문입니다. 항목이 하나인 쌍은 상수이고,
 * 한 표식에서 언제나 같은 생물을 보내는 보스 사이클은 뽑기이기를 그만둡니다. 그것이
 * ::Enemy::ward_table이 뜻하는 전부이며, 제작자가 이 표식이 아니라 저 표식을 놓을 때 고르고
 * 있는 것의 전부입니다.
 *
 * *세 행짜리 도감이 세 번째 표가 아니라 겹침을 이곳에 둔 이유입니다.* 뽑을 종류가 다섯이던
 * 시절에 이 둘은 서로소인 두 쌍이었습니다. 이제 셋이고 그중 하나가 날며, 표를 서로소로
 * 유지하려고 네 번째 생물을 만들어 내는 것은 표가 도감을 쓰는 일입니다.
 *
 * *언제나 정확히 한 번 뽑습니다.* 방이 가득 차서 소환이 거절되든 아니든 호출자가 그것을
 * 소비합니다. 그 호출 지점의 주석을 참조하십시오. */
static int ward_summon_type(Pools *pl, const Enemy *m)
{
    static const int AIR[2]    = { MON_CASTER, MON_WATER_SPIRIT };
    static const int GROUND[2] = { MON_BRUTE,  MON_WATER_SPIRIT };
    const int *t = m->ward_table ? AIR : GROUND;
    return t[frand(&pl->enemy) < 0.5f ? 0 : 1];
}

/* A total order on candidate positions, so the sort below is stable in the
   only sense that matters: the same map always yields the same order however
   TrenchBroom happened to serialise it.
   Compared in centimetres -- the units Entity stores -- rather than in metres,
   because two markers a hair apart in the editor are two distinct integers
   there and two floats that may or may not compare equal here.
   후보 위치에 대한 전순서입니다. 그래야 아래의 정렬이, 중요한 유일한 의미에서 안정적입니다.
   TrenchBroom이 어떻게 직렬화했든 같은 맵은 언제나 같은 순서를 냅니다.
   미터가 아니라 Entity가 저장하는 단위인 센티미터로 비교합니다. 편집기에서 머리카락 하나
   차이로 떨어진 두 표식은 그곳에서는 서로 다른 두 정수이지만, 이곳에서는 같다고 비교될 수도
   아닐 수도 있는 두 float이기 때문입니다. */
static int ward_before(v3 a, v3 b)
{
    int ax = (int)(a.x * 100.0f), bx = (int)(b.x * 100.0f);
    if (ax != bx) return ax < bx;
    int az = (int)(a.z * 100.0f), bz = (int)(b.z * 100.0f);
    if (az != bz) return az < bz;
    return (int)(a.y * 100.0f) < (int)(b.y * 100.0f);
}

void enemy_ward_scan(Pools *pl, const Level *l)
{
    BossFight *b = &pl->enemy.boss;

    for (int i = 0; i < l->n_ents; i++) {
        const Entity *e = &l->ents[i];
        int air = name_eq(e->kind, "wardair");
        if (!air && !name_eq(e->kind, "wardground")) continue;

        if (b->n_cand >= BOSS_MAX_CAND) { DIAG(DIAG_WARD_CAND); continue; }
        b->cand[b->n_cand]     = v3f(e->x * 0.01f, e->y * 0.01f, e->z * 0.01f);
        b->cand_air[b->n_cand] = (char)air;
        b->n_cand++;
    }

    /* SORTED, and the sort is the point of doing this at load rather than
       reading the entity list when a cycle needs it. `Level::ents` is in .map
       file order, so without this the same seed picks a different ward set
       after an edit that changed nothing about the level -- and there is
       nothing in the diff to explain it. world.c refused this same dependency
       by name once already: "'First in the entity list' is a property of how
       the map was saved."
       Integer coordinates, so the comparison is exact and no float ordering
       hazard can reorder two markers that a rebuild placed identically.
       정렬하며, 그 정렬이 사이클이 필요할 때 엔티티 목록을 읽지 않고 로드 시점에 이것을 하는
       이유입니다. `Level::ents`는 .map 파일 순서이므로, 이것이 없으면 레벨에 대해 아무것도
       바꾸지 않은 편집 뒤에 같은 시드가 다른 결계핵 무리를 고릅니다. 그리고 diff에는 그것을
       설명하는 것이 없습니다. world.c는 이미 같은 의존을 이름을 붙여 거절한 적이 있습니다.
       *"«엔티티 목록의 첫 번째»는 맵이 어떻게 저장되었는가의 성질입니다."*
       정수 좌표이므로 비교가 정확하고, 재빌드가 동일하게 놓은 두 표식을 부동소수점 순서
       위험이 뒤바꿀 수 없습니다. */
    for (int i = 1; i < b->n_cand; i++) {
        v3   p = b->cand[i];
        char a = b->cand_air[i];
        int  j = i - 1;
        while (j >= 0 && ward_before(p, b->cand[j])) {
            b->cand[j + 1]     = b->cand[j];
            b->cand_air[j + 1] = b->cand_air[j];
            j--;
        }
        b->cand[j + 1]     = p;
        b->cand_air[j + 1] = a;
    }
}

int enemy_ward_place(Pools *pl, const Level *l)
{
    BossFight *b = &pl->enemy.boss;
    int placed = 0;

    /* Two independent draws, one per list. Drawing BOSS_WARDS from a single
       pool can hand out an all-air or an all-ground cycle by chance, and those
       are two different fights -- three in a row is reachable, and the author
       who placed a mix would have no way to tell that from a bug.
       목록마다 하나씩, 독립적인 두 번의 추출입니다. 하나의 풀에서 BOSS_WARDS를 뽑으면 우연히
       전원 공중형이거나 전원 지상형인 사이클을 내줄 수 있고, 그 둘은 서로 다른 전투입니다.
       3연속도 도달 가능하며, 혼합을 배치한 제작자는 그것을 버그와 구별할 방법이 없습니다. */
    for (int air = 0; air < 2; air++) {
        int want = BOSS_WARDS / 2;

        /* The candidates of this kind, preferring ones the last cycle did not
           use. Gathered as indices so the shuffle below moves two bytes rather
           than a v3 and a flag.
           이 종류의 후보이며, 지난 사이클이 쓰지 않은 것을 우선합니다. 아래의 섞기가 v3와
           플래그가 아니라 2바이트를 옮기도록 인덱스로 모읍니다. */
        char idx[BOSS_MAX_CAND];
        int  n = 0;
        for (int i = 0; i < b->n_cand; i++)
            if (b->cand_air[i] == air && !b->cand_used[i]) idx[n++] = (char)i;

        /* NOT ENOUGH FRESH ONES: fall back to every candidate of this kind
           rather than placing fewer. "Not the previous positions" needs twice
           as many candidates as wards to be satisfiable at all, and a map that
           marked fewer should get a repeated position rather than a thinner
           fight it never asked for.
           신선한 것이 부족하면 더 적게 놓는 대신 이 종류의 모든 후보로 되돌아갑니다.
           "이전 자리가 아님"은 애초에 결계핵의 두 배가 되는 후보가 있어야 만족될 수 있고,
           그보다 적게 표시한 맵은 요청한 적 없는 빈약한 전투가 아니라 반복된 자리를 받아야
           합니다. */
        if (n < want) {
            n = 0;
            for (int i = 0; i < b->n_cand; i++)
                if (b->cand_air[i] == air) idx[n++] = (char)i;
        }

        if (n < want) DIAG(DIAG_WARD_CAND);

        /* PARTIAL FISHER-YATES: exactly `want` draws, whatever it picks and
           however many candidates there are. A rejection loop would consume a
           variable number and desynchronise every recorded demo the first time
           a map gained a marker. enemy_hurt's drop-table note is the same rule
           written for the same reason.
           부분 Fisher-Yates입니다. 무엇을 고르든, 후보가 몇이든 정확히 `want`번 뽑습니다.
           재시도 루프는 가변 횟수를 소비하며, 맵에 표식이 하나 추가되는 첫 순간 기록된 모든
           데모를 어긋나게 합니다. enemy_hurt의 드롭 표 주석이 같은 이유로 적힌 같은 규칙입니다. */
        for (int k = 0; k < want; k++) {
            /* SPENT FIRST, AND UNCONDITIONALLY. The `continue` below is the
               empty-list case, and taking the draw after it would make the
               generator advance by a different amount on a map with no
               candidates than on one with some.
               먼저, 그리고 조건 없이 소비합니다. 아래의 `continue`는 빈 목록의 경우이며,
               그 뒤에서 뽑으면 후보가 없는 맵과 있는 맵에서 생성기가 서로 다른 만큼
               전진하게 됩니다. */
            float r = frand(&pl->enemy);
            if (n - k <= 0) continue;
            int   pick = k + (int)(r * (float)(n - k));
            /* frand cannot return 1.0 -- it is (rng>>8)/2^24 and the numerator
               tops out at 2^24-1 -- so this cannot fire today. It is here
               because the clamp costs one compare and the alternative failure
               is an out-of-bounds swap that only shows up on one draw in
               sixteen million.
               frand는 1.0을 반환할 수 없습니다. (rng>>8)/2^24이고 분자의 최댓값이 2^24-1이기
               때문입니다. 따라서 오늘은 발동할 수 없습니다. 이것이 있는 이유는 비교 하나의
               비용이고, 대안이 되는 실패는 1600만 번에 한 번 뽑기에서만 드러나는 범위 밖
               교환이기 때문입니다. */
            if (pick >= n) pick = n - 1;
            char  t = idx[k]; idx[k] = idx[pick]; idx[pick] = t;

            int ci = idx[k];
            if (!make_monster(pl, l, MON_WARD,
                              b->cand[ci].x, b->cand[ci].y, b->cand[ci].z))
                continue;

            /* The ward that was just made is the last slot make_monster
               touched. Its table comes from the marker, which is the whole of
               what distinguishes the two the author places.
               방금 만들어진 결계핵은 make_monster가 마지막으로 건드린 슬롯입니다. 그 표는
               표식에서 오며, 그것이 제작자가 배치하는 두 가지를 구별하는 전부입니다. */
            for (int s = pl->enemy.count - 1; s >= 0; s--) {
                if (pl->enemy.m[s].type != MON_WARD) continue;
                if (pl->enemy.m[s].state == E_DEAD) continue;
                pl->enemy.m[s].ward_table = (short)air;
                break;
            }
            b->cand_used[ci] = 1;
            placed++;
        }
    }

    /* --- roll the USED marks forward -------------------------------------
     *
     * THREE VALUES, NOT TWO: 0 is free, 2 is "the cycle before this one used
     * it", and 1 is "this call just used it". The selection above avoids
     * anything non-zero, so during the loop a 2 is what "the previous set"
     * means and a 1 is what this call is building. Only after the loop can the
     * two be collapsed -- 2s expire and 1s become the new 2s.
     *
     * Clearing the marks BEFORE the loop instead would make "not the previous
     * set" mean "not the set I am currently building", which is always true and
     * would let a cycle reuse every position the last one had.
     *
     * --- 사용 표시를 앞으로 넘긴다 -----------------------------------------
     *
     * *둘이 아니라 세 값입니다.* 0은 비어 있음, 2는 "직전 사이클이 썼음", 1은 "이번 호출이
     * 방금 썼음"입니다. 위의 선택은 0이 아닌 모든 것을 피하므로, 루프가 도는 동안 2가 곧
     * "이전 조합"이고 1이 이번 호출이 만들고 있는 것입니다. 둘을 합칠 수 있는 것은 루프가
     * 끝난 뒤뿐입니다. 2는 만료되고 1이 새로운 2가 됩니다.
     *
     * 대신 루프 *전에* 표시를 지우면 "이전 조합이 아님"이 "지금 내가 만들고 있는 조합이
     * 아님"을 뜻하게 되는데, 그것은 언제나 참이므로 한 사이클이 직전 사이클의 모든 자리를
     * 그대로 재사용할 수 있게 됩니다. */
    for (int i = 0; i < b->n_cand; i++)
        if (b->cand_used[i] == 2) b->cand_used[i] = 0;
    for (int i = 0; i < b->n_cand; i++)
        if (b->cand_used[i] == 1) b->cand_used[i] = 2;

    return placed;
}

int enemy_boss_summon(Pools *pl, const Level *l)
{
    BossFight *b = &pl->enemy.boss;
    if (!b->have_maw) return 0;
    return make_monster(pl, l, MON_MAW, b->maw_pos.x, b->maw_pos.y, b->maw_pos.z);
}

void enemy_boss_heal(Pools *pl, int to)
{
    int i = enemy_boss_index(pl);
    if (i < 0) return;
    if (pl->enemy.m[i].health < to) pl->enemy.m[i].health = to;
}

int enemy_take_drop(Pools *pl, int idx, v3 *out_at)
{
    if (idx < 0 || idx >= pl->enemy.count)
        return -1;
    Enemy *m = &pl->enemy.m[idx];
    if (!m->active || m->drop < 0)
        return -1;

    int kind = m->drop;
    m->drop  = -1;                     /* owed once; see the header */
    if (out_at)
        *out_at = m->pos;              /* the body's feet, which is floor */
    return kind;
}


/* --- Static helper definitions / 정적 헬퍼 정의 --- */

/* --- The type table / 종류 표 --- */
/* A caster that cannot throw anything stands in its band and does nothing, and
   a brawler with a bolt speed is a stat nobody reads -- both are the kind of
   silent wrong the behaviour column exists to make impossible to write by
   accident. Checked here rather than trusted, because the table is the one
   place a new monster is authored and this is the one place that can object.
   던질 것이 없는 캐스터는 자기 대역에 서서 아무것도 하지 않고, 볼트 속도를 가진 근접형은
   아무도 읽지 않는 수치입니다. 둘 다 behaviour 열이 실수로 작성될 수 없게 만들려는 바로 그
   조용한 오류입니다. 신뢰하지 않고 이곳에서 검사하는 이유는, 표가 새 몬스터를 저작하는 유일한
   곳이고 이곳이 이의를 제기할 수 있는 유일한 곳이기 때문입니다. */
static void types_check(void)
{
    for (int i = 0; i < MON_TYPES; i++)
    {
        /* THE INVARIANT MOVED FROM THE KIND TO THE SLOT, which is what the
           slots are for. It used to read `behaviour == AI_CASTER` against
           `shot_speed > 0` -- one archetype, one attack, and the two had to
           agree because there was nothing else they could disagree about. Now
           an ::AI_CASTER may carry a swing, so the pairing that still has to
           hold is per attack: a slot that launches something needs a speed for
           it, and a slot that reaches out must not name one.
           *불변식이 종류에서 슬롯으로 옮겨 갔고*, 슬롯이 있는 이유가 그것입니다. 예전에는
           `behaviour == AI_CASTER`와 `shot_speed > 0`을 맞대어 보았습니다. 아키타입 하나에
           공격 하나였고, 둘이 어긋날 수 있는 다른 것이 없었으므로 일치해야 했습니다. 이제
           ::AI_CASTER가 휘두르기를 지닐 수 있으므로 여전히 성립해야 하는 짝은 *공격마다*
           입니다. 무언가를 쏘아 보내는 슬롯에는 그 속도가 필요하고, 뻗는 슬롯은 속도를
           적어서는 안 됩니다. */
        int n_atk = mon_attack_count(i);

        /* EVERY FIGHTING KIND HAS AT LEAST ONE, AND AN INERT ONE HAS NONE.
           A kind with no slots would walk to its band and stand there, which
           looks exactly like a monster that has not noticed the player.
           *싸우는 종류는 최소 하나를 가지고 불활성인 것은 하나도 갖지 않습니다.* 슬롯이 없는
           종류는 자기 대역까지 걸어가 서 있으며, 그것은 플레이어를 알아채지 못한 몬스터와
           똑같이 보입니다. */
        if ((TYPES[i].behaviour == AI_INERT) != (n_atk == 0))
            DIAG(DIAG_MON_TABLE);

        for (int k = 0; k < n_atk; k++)
        {
            const MonAttack *A = &ATTACKS[i][k];

            if ((A->kind == ATK_BOLT) != (A->shot_speed > 0.0f))
                DIAG(DIAG_MON_TABLE);

            /* A BAND THAT IS NOT A BAND. `min >= max` is a slot no distance
               satisfies -- an attack written into the table and never offered,
               which is indistinguishable in play from one that was forgotten.
               *대역이 아닌 대역입니다.* `min >= max`는 어떤 거리도 만족시키지 못하는
               슬롯입니다. 표에 적혔으나 결코 제안되지 않는 공격이며, 플레이에서는 잊힌 공격과
               구별되지 않습니다. */
            if (A->min < 0.0f || A->min >= A->max)
                DIAG(DIAG_MON_TABLE);

            if (A->weight <= 0.0f)
                DIAG(DIAG_MON_TABLE);

            /* THE VOLLEY BOUNDS, and the empty one is the reason. ::begin_attack
               rolls in `burst_min..burst` and a slot with them the wrong way
               round rolls an empty range -- a monster that winds up, fires
               nothing and rests, which looks like a monster that has decided not
               to attack you and is very hard to tell from one that cannot see
               you. A floor of 1 for the same reason: 0 is the same empty volley
               written a different way.
               *일제 사격의 경계이며, 빈 것이 그 이유입니다.* ::begin_attack은
               `burst_min..burst`에서 굴리는데, 둘이 뒤바뀐 슬롯은 빈 범위를 굴립니다. 준비
               동작을 하고 아무것도 쏘지 않고 쉬는 몬스터이며, 그것은 당신을 공격하지 않기로 한
               몬스터처럼 보이고 당신을 보지 못하는 몬스터와 구별하기가 매우 어렵습니다. 하한
               1도 같은 이유입니다. 0은 같은 빈 일제 사격을 다르게 적은 것입니다. */
            if (A->burst < 1 || A->burst_min < 1 || A->burst_min > A->burst)
                DIAG(DIAG_MON_TABLE);

            /* A GAP WITH NOTHING TO SEPARATE. A slot that fires once and still
               names a cadence is a number with no reader, and the next person to
               change ::MonAttack::burst would inherit it as though it had been
               chosen. Zero on a multi-bolt slot is not an error -- it is
               "together", which is what the maw still does.
               *가를 것이 없는 간격입니다.* 단발이면서 박자를 적어 둔 슬롯은 독자가 없는
               수이고, 다음에 ::MonAttack::burst를 바꾸는 사람은 그것이 골라진 값인 양
               물려받게 됩니다. 여러 발인 슬롯의 0은 오류가 아니라 "함께"이며, 아귀가 여전히
               하는 일입니다. */
            if (A->burst == 1 && A->shot_gap != 0.0f)
                DIAG(DIAG_MON_TABLE);
            if (A->shot_gap < 0.0f)
                DIAG(DIAG_MON_TABLE);
        }

        /* SOMETHING MUST ANSWER CONTACT, and this is what replaced the rule
           that was here one revision ago.

           THAT RULE WAS "NO HOLE IN THE UNION OF THE BANDS", and it was wrong
           the moment a second attack existed to test it against. It read a gap
           as a distance at which the monster stands and does nothing. For an
           ::AI_CASTER a gap is where it BACKS AWAY: the caster's bolt begins at
           7.2m and its swing ends at 2.2m, and everything between is the range
           it is trying to leave. The rule would have forced a seven-metre swing
           or a point-blank bolt, either of which is a monster bent to fit a
           check.

           WHAT IS WORTH HOLDING IS THE OTHER END. A player can always walk up
           to a monster, and a monster with nothing at contact is one you kill
           by touching it -- which is exactly what a caster was until this
           revision. So: some slot must cover zero. It catches the typo the old
           rule caught (a band written far from where the kind fights) and it is
           a design claim rather than an arithmetic one.

           *무언가는 접촉에 답해야 하며*, 이것이 한 판 전 이 자리에 있던 규칙을 대신합니다.
           *그 규칙은 "대역 합집합에 구멍 없음"이었고*, 그것을 시험할 두 번째 공격이 생긴
           순간 틀린 것이 되었습니다. 그것은 구멍을 몬스터가 서서 아무것도 하지 않는 거리로
           읽었습니다. ::AI_CASTER에게 구멍은 *물러나는* 곳입니다. 캐스터의 볼트는 7.2m에서
           시작하고 휘두르기는 2.2m에서 끝나며, 그 사이 전부가 그것이 떠나려는 거리입니다. 그
           규칙은 7미터짜리 휘두르기나 코앞의 볼트를 강요했을 것이고, 둘 다 검사에 맞추려고
           구부린 몬스터입니다.
           *지킬 값어치가 있는 것은 반대쪽 끝입니다.* 플레이어는 언제나 몬스터에게 걸어갈 수
           있고, 접촉에 아무것도 없는 몬스터는 만지기만 하면 죽는 몬스터입니다. 이 판 전까지
           캐스터가 정확히 그러했습니다. 그러므로 어떤 슬롯은 0을 덮어야 합니다. 옛 규칙이
           잡던 오타(그 종류가 싸우는 곳에서 멀리 적힌 대역)를 잡으면서, 산술이 아니라 설계에
           대한 주장입니다. */
        if (n_atk > 0)
        {
            int at_contact = 0;
            for (int k = 0; k < n_atk; k++)
                if (ATTACKS[i][k].min <= 0.0f) at_contact = 1;
            if (!at_contact)
                DIAG(DIAG_MON_TABLE);
        }

        /* A BOSS WHOSE HEALTH DOES NOT DIVIDE BY ::BOSS_CYCLES survives its
           last cycle. ::enemy_hurt clamps at `hp * (CYCLES - cycle - 1)/CYCLES`
           and integer division makes that final boundary non-zero for, say,
           100/3 -- so the maw ends the third groggy window alive at 1hp with no
           wards left to raise and no fourth boundary to cross. The fight simplayer_eye
           stops. Checked here because it is a property of the TABLE, and the
           table is the thing somebody retunes.
           체력이 ::BOSS_CYCLES로 나누어떨어지지 않는 보스는 마지막 사이클을 살아남습니다.
           ::enemy_hurt는 `hp * (CYCLES - cycle - 1)/CYCLES`에서 고정하는데, 정수 나눗셈은
           예컨대 100/3에서 그 마지막 경계를 0이 아니게 만듭니다. 그러면 아귀는 세 번째 그로기
           창을 체력 1로 살아남고, 세울 결계핵도 넘을 네 번째 경계도 없습니다. 전투가 그냥
           멈춥니다. 이곳에서 검사하는 이유는 이것이 *표*의 성질이고, 누군가 다시 조율하는
           대상이 그 표이기 때문입니다. */
        if ((TYPES[i].flags & MON_BOSS) && (TYPES[i].hp % BOSS_CYCLES) != 0)
            DIAG(DIAG_MON_TABLE);
    }
}

/* 두 문자열이 같은지 검사합니다. <string.h>를 끌어오지 않기 위한 것이며, fx.c의
   find_def가 이펙트 이름에 쓰는 것과 동일한 루프입니다.
   Whether two strings match. Avoids pulling in <string.h>, and is the same loop
   fx.c's find_def uses on effect names. */
static int name_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return !*a && !*b;
}

/* The wait a spawner actually gets, after whatever is suppressing it.
 *
 * BOTH SITES THAT SCHEDULE A GROUP GO THROUGH HERE -- ::enemy_wave_arm when a
 * wave arms and ::spawners_update when a group has just left. Two call sites is
 * one more than it takes for a fix applied to one of them to be a fix applied
 * to one of them, which is the failure scene.c's build split is documented
 * against.
 *
 * The suppression is READ, never saved and restored. ::enemy_wave_arm rewrites
 * every spawner's interval on every wave, so a pre-boss value stashed anywhere
 * would be overwritten before there was anything to put it back into. See
 * ::EnemyPool::spawn_slow.
 *
 * 억제가 반영된 뒤 스포너가 실제로 받는 대기 시간입니다.
 *
 * *무리를 예약하는 두 지점 모두 이곳을 지납니다.* 웨이브가 무장할 때의 ::enemy_wave_arm과,
 * 무리가 막 떠난 뒤의 ::spawners_update입니다. 호출 지점이 둘이면, 그중 하나에 적용한 수정이
 * 말 그대로 하나에만 적용되기에 충분한 수입니다. scene.c의 빌드 분할이 대비하고 있다고
 * 문서화한 실패가 그것입니다.
 *
 * 억제는 *읽을* 뿐 저장했다 되돌리지 않습니다. ::enemy_wave_arm이 매 웨이브 모든 스포너의
 * 간격을 덮어쓰므로, 어디에 보관한 보스 이전 값이든 되돌릴 대상이 생기기도 전에
 * 덮어써집니다. ::EnemyPool::spawn_slow를 참조하십시오. */
static float spawn_wait(const EnemyPool *ep, float interval)
{
    float rate = ep->spawn_rate > 0.01f ? ep->spawn_rate : 1.0f;
    return interval * (1.0f + ep->spawn_slow) / rate;
}

/* --- Utilities / 보조 함수 --- */

/**
 * @brief Advances the pool's generator and returns 0.0 to 1.0.
 *
 * ENGLISH
 * -------
 * @param[in,out] e Pool whose random state to advance.
 * @return The next value, in 0.0 to 1.0.
 *
 * @note The state belongs to the POOL, not to this file, so a headless test
 *       that seeds a pool gets the same monsters every run. Every dice roll
 *       the AI makes comes through here.
 *
 * 한국어
 * ------
 * @brief 풀의 생성기를 진행시키고 0.0에서 1.0 사이의 값을 반환합니다.
 *
 * @param[in,out] e 난수 상태를 진행시킬 풀.
 * @return 0.0에서 1.0 사이의 다음 값.
 *
 * @note 상태는 이 파일이 아니라 *풀*의 것이므로, 풀에 씨앗을 준 헤드리스 테스트는 매번 같은
 *       몬스터를 얻습니다. AI가 굴리는 모든 주사위가 이곳을 지납니다.
 */
static float frand(EnemyPool *e)
{
    e->rng = e->rng * 1664525u + 1013904223u;
    return (e->rng >> 8) * (1.0f / 16777216.0f);
}

/**
 * @brief 특정 위치에서 사운드를 재생하며, 거리에 따라 볼륨을 조절합니다.
 *
 * ENGLISH
 * -------
 * A pass-through to ::audio_play_at, kept only so the call sites below stay
 * short. It used to hold its own falloff -- base * 12/(12+d) -- which was the
 * only distance model in the game and therefore fine, and stopped being fine
 * the moment audio grew one: two curves meant a monster and a door at the same
 * distance were different volumes for no reason anyone could point at, and the
 * enemy one never reached zero, so a growl four rooms away was still a quarter
 * as loud as one in your face.
 *
 * 한국어
 * ------
 * ::audio_play_at으로 넘기기만 하며, 아래의 호출부를 짧게 두려고 남겨 둡니다. 예전에는
 * 자체 감쇠 곡선을 들고 있었고, 그것이 게임의 유일한 거리 모델인 동안에는 괜찮았습니다.
 * 오디오 쪽에 곡선이 생기는 순간 괜찮지 않게 되었습니다. 곡선이 둘이면 같은 거리의
 * 몬스터와 문이 아무도 설명할 수 없는 이유로 다른 음량이 되고, 몬스터 쪽 곡선은 0에
 * 닿지 않아 네 방 건너의 으르렁거림이 코앞의 것의 4분의 1만큼 크게 들렸습니다.
 */
static void play_at(v3 p, const char *name, int base)
{
    audio_play_at(name, base, p);
}

/* --- Spawning / 생성 --- */
/**
 * @brief Puts one monster into the pool, on the floor under a point.
 *
 * ENGLISH
 * -------
 * Lifted out of ::enemy_spawn_level so a ::Spawner can make monsters the same
 * way the level does. Two paths that built a monster would drift the moment
 * either gained a field -- an animation offset, a starting state -- and the
 * difference would read as "the ones the spawner makes behave oddly".
 *
 * @return Non-zero when a monster was created.
 *
 * 한국어
 * ------
 * @brief 몬스터 하나를 어떤 점 아래의 바닥 위에 풀에 넣습니다.
 * @note ::Spawner가 레벨과 같은 방식으로 몬스터를 만들 수 있도록 ::enemy_spawn_level에서
 *       뽑아냈습니다. 몬스터를 만드는 경로가 둘이면 어느 한쪽이 필드를 얻는 순간 어긋나고
 *       (애니메이션 오프셋, 시작 상태) 그 차이는 "스포너가 만든 것들이 이상하게 군다"로
 *       읽힙니다.
 */
static int make_monster(Pools *pl, const Level *l, int type,
                        float x, float from_y, float z)
{
    if (type < 0 || type >= MON_TYPES) return 0;

    float f, c;
    if (!level_ground(l, x, z, from_y, 1e9f, &f, &c)) return 0;

    /* AND NOT ONTO LAVA. ::foot_ok refuses to walk a monster onto a hazard
       floor, and a spawn that ignored the same rule would put one where it
       could never have walked -- standing in a sea it will not step out of,
       burning, because every column around it is refused too. The arena
       makes this reachable rather than theoretical: `lqdm4` stands a brute
       spawner 0.48m above its lava and a ground ward slot 0.24m above it.
       Refused rather than nudged. Moving the spawn to the nearest safe
       column would be this function guessing at level layout, and a monster
       that appears somewhere the author did not mark is harder to explain
       than one that does not appear -- ::enemy_ward_place already treats a
       candidate it cannot use as one to skip.
       *그리고 용암 위로는 아닙니다.* ::foot_ok는 몬스터를 위험한 바닥으로 걷게
       하기를 거절하며, 같은 규칙을 무시하는 스폰은 걸어서는 결코 갈 수 없었을
       자리에 몬스터를 놓습니다. 빠져나오지 않을 바다에 서서 타는 것이며, 주위의
       모든 기둥도 함께 거절되기 때문입니다. 아레나가 이것을 이론이 아니라 도달
       가능한 일로 만듭니다. `lqdm4`는 브루트 스포너를 자기 용암 위 0.48m에,
       지상 결계핵 자리를 0.24m에 세웁니다.
       옮기지 않고 거절합니다. 가장 가까운 안전한 기둥으로 스폰을 옮기는 것은 이
       함수가 레벨 배치를 짐작하는 일이며, 제작자가 표시하지 않은 곳에 나타나는
       몬스터는 나타나지 않는 몬스터보다 설명하기 어렵습니다.
       ::enemy_ward_place는 쓸 수 없는 후보를 이미 건너뛰는 것으로 다룹니다. */
    if (!floor_safe(l, x, f, z)) return 0;

    /* WHICH SLOT, and until this existed the answer was always "the next one".
     *
     * A corpse keeps its slot: nothing ever clears ::Enemy::active once a
     * monster has been made, and ::enemy_reset -- the only thing that puts
     * `count` back to zero -- is on the level-load path and nowhere else. So a
     * wave mode spent ENEMY_MAX on bodies. Measured on spire, which has five
     * spawners and is the level the game starts on: wave 1 leaves 18 slots
     * used, wave 2 leaves 48, and wave 3 fills all 64. From that point nothing
     * could spawn again for the rest of the level.
     *
     * pools.h states what every pool here does when it fills -- "a fixed array,
     * a cap chosen so a frame never allocates, and oldest-first eviction when
     * it fills" -- and lists this one among them. It was the one that did not.
     * This is it keeping the promise the others keep.
     *
     * ONLY CORPSES ARE EVICTED. A living monster vanishing to make room for
     * another is a fight the player was having and then was not, and the cap
     * has to keep meaning something for the things that are still moving. A
     * room of sixty-four live monsters still refuses, and still says so.
     *
     * @note `anim` is age plus a fixed offset under 2*pi that ::make_monster
     *       gives each monster to desync its walk cycle, so "oldest" is oldest
     *       to within a few seconds rather than exactly. In an arena the
     *       corpses in the pool are minutes apart; the offset cannot reorder
     *       them and buying exactness would cost a field in every Enemy.
     *
     * 한국어: 어느 칸을 쓸 것인가이며, 이것이 생기기 전의 답은 언제나 "다음 칸"이었습니다.
     *
     * 시체는 자기 칸을 계속 가집니다. 몬스터가 만들어진 뒤 ::Enemy::active를 지우는 것은
     * 아무것도 없고, `count`를 0으로 되돌리는 유일한 ::enemy_reset은 레벨 로드 경로에만
     * 있습니다. 그래서 웨이브 모드는 ENEMY_MAX를 시체에 썼습니다. 스포너가 다섯이고 게임이
     * 시작하는 레벨인 spire에서 측정했습니다. 웨이브 1이 18칸, 웨이브 2가 48칸을 쓰고,
     * 웨이브 3이 64칸을 모두 채웁니다. 그 시점부터 그 레벨이 끝날 때까지 아무것도 생성될 수
     * 없었습니다.
     *
     * pools.h는 이곳의 모든 풀이 가득 찼을 때 무엇을 하는지 밝히며("고정 배열, 프레임이 결코
     * 할당하지 않도록 고른 상한, 그리고 가득 찼을 때 가장 오래된 것부터 버리는 축출") 이
     * 풀도 그 목록에 넣습니다. 그러지 않던 유일한 것이 이것이었습니다. 이제 다른 풀들이
     * 지키는 약속을 함께 지킵니다.
     *
     * 축출 대상은 *시체뿐*입니다. 살아 있는 몬스터가 다른 몬스터의 자리를 위해 사라지는 것은
     * 플레이어가 하고 있던 전투가 갑자기 없어지는 일이며, 상한은 여전히 움직이는 것들에
     * 대해 의미를 가져야 합니다. 살아 있는 몬스터 예순넷인 방은 여전히 거절하고, 여전히
     * 그렇게 말합니다.
     *
     * @note `anim`은 나이에 ::make_monster가 걸음 주기를 어긋내려고 각 몬스터에 주는 2*pi
     *       미만의 고정 오프셋을 더한 값입니다. 따라서 "가장 오래된"은 정확히가 아니라 몇 초
     *       이내로 오래된 것입니다. 아레나에서 풀에 있는 시체들은 몇 분씩 떨어져 있어 그
     *       오프셋이 순서를 뒤집을 수 없고, 정확성을 사려면 Enemy마다 필드 하나를 치러야
     *       합니다.
     */
    Enemy *m;
    if (pl->enemy.count < ENEMY_MAX) {
        m = &pl->enemy.m[pl->enemy.count++];
    } else {
        int   oldest_i = -1;
        float oldest_a = -1.0f;
        for (int i = 0; i < ENEMY_MAX; i++) {
            if (pl->enemy.m[i].state != E_DEAD) continue;
            if (pl->enemy.m[i].anim > oldest_a) {
                oldest_a = pl->enemy.m[i].anim;
                oldest_i = i;
            }
        }
        if (oldest_i < 0) { DIAG(DIAG_ENEMY_CAP); return 0; }
        m = &pl->enemy.m[oldest_i];
    }

    const MonType *S = &TYPES[type];
    Enemy zero = {0};
    *m = zero;
    m->type   = (short)type;

    /* A FLYER KEEPS THE HEIGHT IT WAS ASKED FOR, and without this the bit does
       nothing. ::MON_FLIES stops a monster FALLING, which is only half of being
       airborne -- this function put every monster on the floor before it ever
       got a chance to fall, so a flyer held its altitude at ground level and
       the flag looked like it worked while changing nothing anyone could see.
       Never BELOW the floor, though: a marker under the geometry would put one
       inside the world where nothing can be shot.
       비행체는 요청받은 높이를 유지하며, 이것이 없으면 그 비트는 아무 일도 하지 않습니다.
       ::MON_FLIES는 몬스터가 *떨어지는 것*을 막지만 그것은 공중에 있음의 절반일 뿐입니다. 이
       함수가 떨어질 기회를 얻기도 전에 모든 몬스터를 바닥에 놓았으므로, 비행체는 지면 높이에서
       고도를 유지했고 그 플래그는 아무도 볼 수 없는 것을 바꾸면서 동작하는 것처럼 보였습니다.
       다만 결코 바닥 *아래*는 아닙니다. 지오메트리 밑의 표식은 무엇도 쏠 수 없는 세계 안쪽에
       몬스터를 놓게 됩니다. */
    /* ::MON_ANCHORED joins ::MON_FLIES here and only here. The two bits mean
       different things everywhere else -- one suppresses falling, the other
       suppresses walking -- but both promise the placed height is kept, and
       that promise is made in this one line. A maw whose feet were dropped to
       the floor is a maw that is no longer in the wall it was drawn into.
       ::MON_ANCHORED는 오직 이곳에서만 ::MON_FLIES와 합류합니다. 두 비트는 다른 모든 곳에서
       다른 것을 뜻합니다. 하나는 낙하를, 다른 하나는 보행을 억제합니다. 그러나 둘 다 놓인
       높이가 유지된다고 약속하며, 그 약속은 이 한 줄에서 이루어집니다. 발이 바닥으로 내려간
       아귀는 자신이 그려져 들어간 벽에 더 이상 있지 않은 아귀입니다. */
    float y = f;
    if ((S->flags & (MON_FLIES | MON_ANCHORED)) && from_y > f) y = from_y;

    m->pos    = v3f(x, y, z);
    m->health = S->hp;
    m->state  = E_IDLE;
    m->active = 1;
    m->anim   = frand(&pl->enemy) * 6.28f;
    m->drop   = -1;
    /* -1 IS "NOBODY", AND ZERO IS MONSTER ZERO. Every other field here is
       content with the pool's zeroing; this one cannot be, because its zero is
       a valid index and means "already holding a grudge against the first
       monster in the room". Caught by a test that asserted a same-kind victim
       stays at -1 and got 0 -- and by the same token the assertion next to it,
       that a brute turns on caster 0, had been passing without the code doing
       anything at all.
       *-1이 "아무도 아님"이고 0은 몬스터 0입니다.* 이곳의 다른 모든 필드는 풀의 0 채우기로
       충분하지만 이것은 그럴 수 없습니다. 이것의 0은 유효한 색인이고 "방의 첫 몬스터에게 이미
       원한을 품고 있음"을 뜻하기 때문입니다. 같은 종류의 피해자가 -1로 남는지 단언한 테스트가
       0을 받아 잡았습니다. 그리고 같은 이유로, 그 곁의 단언(브루트가 캐스터 0에게 돌아선다)은
       코드가 아무 일도 하지 않는 채로 통과하고 있었습니다. */
    /* NO SLOT UNTIL ONE IS PICKED. -1 rather than 0 for ::Enemy::foe's reason:
       0 is a valid slot, so a fresh monster initialised to it would be carrying
       a decision nobody made -- and the ::E_ATTACK block would run slot 0 for a
       monster that never entered the state through ::pick_attack.
       *고를 때까지 슬롯은 없습니다.* 0이 아니라 -1인 것은 ::Enemy::foe와 같은 이유입니다.
       0은 유효한 슬롯이므로 그것으로 초기화된 새 몬스터는 아무도 내리지 않은 결정을 지니게
       되고, ::pick_attack을 거쳐 그 상태에 들어간 적 없는 몬스터에 대해 ::E_ATTACK 블록이
       슬롯 0을 실행하게 됩니다. */
    m->atk      = -1;
    m->foe      = -1;
    m->foe_time = 0.0f;
    m->sight_age = (short)((pl->enemy.count - 1) % SIGHT_PERIOD);
    return 1;
}

/**
 * @brief Reads the level's `spawner_*` markers into the pool.
 *
 * ENGLISH
 * -------
 * The kind carries the monster: `spawner_caster` makes casters. That is the idiom
 * this project already uses where a name has to say two things -- sprite.c
 * reads a frame number off the end of a sprite name, and door.c reads a tag off
 * the end of `switch<n>` -- and it costs no second field on ::Entity and no
 * table anywhere.
 *
 * 한국어
 * ------
 * @brief 레벨의 `spawner_*` 표식을 풀로 읽어들입니다.
 * @note 종류가 몬스터를 나릅니다. `spawner_caster`는 캐스터를 만듭니다. 이름이 두 가지를 말해야 할
 *       때 이 프로젝트가 이미 쓰는 어법입니다. sprite.c는 스프라이트 이름 끝에서 프레임 번호를
 *       읽고 door.c는 `switch<n>` 끝에서 태그를 읽습니다. ::Entity에 두 번째 필드도, 어디에도
 *       표 하나도 들지 않습니다.
 */
static void spawners_of(Pools *pl, const Level *l)
{
    pl->enemy.n_spawners = 0;

    for (int i = 0; i < l->n_ents; i++) {
        const Entity *e = &l->ents[i];

        /* "spawner_" then a monster name. */
        static const char PRE[] = "spawner_";
        int n = 0;
        while (PRE[n] && e->kind[n] == PRE[n]) n++;
        if (PRE[n] || !e->kind[n]) continue;

        int type = mon_type_for(e->kind + n);
        if (type < 0) continue;

        if (pl->enemy.n_spawners >= ENEMY_MAX_SPAWNERS) { DIAG(DIAG_ENEMY_CAP); continue; }
        Spawner *s = &pl->enemy.spawner[pl->enemy.n_spawners++];

        s->pos       = v3f(e->x * 0.01f, e->y * 0.01f, e->z * 0.01f);
        s->type      = (short)type;
        s->left      = e->p[1] > 0 ? e->p[1] : -1;      /* 0 authored means unlimited */
        s->max_alive = e->p[2];
        s->interval  = e->p[0] > 0 ? e->p[0] * 0.1f : 5.0f;
        s->base_interval = s->interval;
        s->burst     = 1;
        s->warn      = 0.0f;
        /* The first one is due after a full interval, not on the frame the
           level loads: a monster that materialises while the screen is still
           fading in is one the player never saw arrive.
           첫 번째는 레벨이 로드되는 프레임이 아니라 온전한 한 주기 뒤에 나옵니다. 화면이 아직
           밝아지는 중에 나타나는 몬스터는 플레이어가 도착을 보지 못한 몬스터입니다. */
        s->timer     = s->interval;
        s->active    = 1;
    }
}

/* How far the player is from a spawner, on the floor plane. See
   ::SPAWN_MIN_DIST for why height is deliberately not in this.
   플레이어가 스포너에서 얼마나 떨어져 있는가를 바닥 평면에서 잽니다. 높이를 의도적으로
   넣지 않는 이유는 ::SPAWN_MIN_DIST를 참조하십시오. */
static int spawner_crowded(const Spawner *s, v3 player_eye)
{
    float dx = player_eye.x - s->pos.x;
    float dz = player_eye.z - s->pos.z;
    return dx * dx + dz * dz < SPAWN_MIN_DIST * SPAWN_MIN_DIST;
}

/**
 * @brief Advances every spawner by one frame.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl         Pools whose spawners to run.
 * @param[in]     l          Level the new monsters are placed in.
 * @param[in]     player_eye Where the player is, for the crowding test.
 * @param[in]     dt         Seconds since the last frame.
 * @return Non-zero when at least one monster was made this frame.
 *
 * @note A spawner telegraphs before it delivers, so a group never simplayer_eye
 *       appears on top of the player. ::spawner_crowded is what keeps one from
 *       delivering into the space the player is standing in.
 *
 * 한국어
 * ------
 * @brief 모든 스포너를 한 프레임 진행시킵니다.
 *
 * @param[in,out] pl         돌릴 스포너를 가진 풀.
 * @param[in]     l          새 몬스터가 놓일 레벨.
 * @param[in]     player_eye 혼잡 판정을 위한 플레이어 위치.
 * @param[in]     dt         지난 프레임 이후 경과 시간(초).
 * @return 이번 프레임에 몬스터가 하나라도 만들어졌으면 0이 아닙니다.
 *
 * @note 스포너는 배달하기 전에 예고하므로, 무리가 플레이어 위에 그냥 나타나는 일은
 *       없습니다. 플레이어가 서 있는 자리로 배달하지 못하게 하는 것이
 *       ::spawner_crowded입니다.
 */
static int spawners_update(Pools *pl, const Level *l, v3 player_eye, float dt)
{
    int made = 0;

    for (int i = 0; i < pl->enemy.n_spawners; i++) {
        Spawner *s = &pl->enemy.spawner[i];
        if (!s->active) continue;

        /* --- the telegraph, before anything that could start another one ---
           A spawner in its warning is not also counting toward its next group;
           the two clocks never run at once. This is also why the budget is
           spent HERE rather than when the warning was raised: a wave that ends
           mid-telegraph has ::enemy_wave_arm clear it, and a budget already
           deducted would have paid for monsters that never arrived.
           예고 중인 스포너는 동시에 다음 무리를 향해 세고 있지 않습니다. 두 시계는 결코 함께
           돌지 않습니다. 예산을 예고를 올릴 때가 아니라 *이곳에서* 쓰는 이유이기도 합니다.
           예고 도중에 끝난 웨이브는 ::enemy_wave_arm이 그것을 지우며, 이미 차감된 예산은
           결코 도착하지 않은 몬스터의 값을 치른 것이 됩니다. */
        if (s->warn > 0.0f) {
            s->warn -= dt;
            if (s->warn > 0.0f) continue;
            s->warn = 0.0f;

            /* THIS spawner's own answer, which `made` cannot give. `made` is
               the whole function's return and stays set once any spawner in the
               loop has delivered, so reading it below would flash a portal over
               a spawner that produced nothing because the one before it did.
               이 스포너 자신의 답이며, `made`로는 얻을 수 없습니다. `made`는 이 함수 전체의
               반환값이고 루프 안의 어느 스포너든 한 번 배달하면 계속 설정된 채로 남으므로,
               아래에서 그것을 읽으면 앞선 스포너가 배달했다는 이유로 아무것도 만들지 못한
               스포너 위에 포탈이 번쩍이게 됩니다. */
            int arrived = 0;

            int n = s->burst > 0 ? s->burst : 1;
            for (int k = 0; k < n; k++) {
                if (s->left == 0) break;
                if (s->max_alive > 0 && enemy_alive(pl) >= s->max_alive) break;

                /* AND THE SAME REFUSAL FOR THIS KIND, which the line above
                   cannot make: it counts the ROOM, so eight brutes and eight
                   of a mixed wave are the same number to it. Kept as a
                   `break` beside it because it is the same class of answer --
                   "not now" -- and the budget stays owed either way.
                   *그리고 이 종류에 대한 같은 거절이며*, 위의 줄은 그것을 할 수 없습니다.
                   그것은 *방*을 세므로 브루트 여덟과 섞인 웨이브 여덟이 같은 수입니다.
                   같은 부류의 답("지금은 아니다")이므로 곁에 `break`로 두었고, 어느 쪽이든
                   예산은 빚진 채로 남습니다. */
                {
                    const MonType *ST = mon_stats(s->type);
                    if (ST->cap > 0 && enemy_alive_of(pl, s->type) >= ST->cap) break;
                }

                if (!make_monster(pl, l, s->type, s->pos.x, s->pos.y, s->pos.z)) {
                    /* A REFUSAL THAT COSTS NOTHING NEVER ENDS. The budget used
                       to be left untouched here, and ::enemy_wave_done asks
                       whether every spawner is done owing -- `s->active &&
                       s->left != 0`. So a spawner that could not deliver kept
                       what it owed, the wave could never complete, and the run
                       stopped on that wave for good. Measured before the
                       eviction above existed: the wave counter sat on 3 for
                       twelve minutes while the spawners retried twenty-odd
                       times a minute.

                       The two reasons for a refusal are told apart here rather
                       than in ::make_monster, because only one of them is
                       temporary. A room of living monsters is a queue -- the
                       same thing `max_alive` above treats as "not now", and the
                       budget is kept so the group still arrives once there is
                       room. Anything else is a spawn point that does not work,
                       which no amount of waiting will fix, so it costs one of
                       the budget and the spawner runs itself down instead of
                       holding the wave open forever.

                       거절에 값이 없으면 그 거절은 끝나지 않습니다. 이전에는 이곳에서 예산을
                       건드리지 않았고, ::enemy_wave_done은 모든 스포너가 빚을 갚았는지를
                       묻습니다(`s->active && s->left != 0`). 그래서 배달하지 못한 스포너가
                       빚을 그대로 안고 있었고, 웨이브는 결코 완료될 수 없었으며, 플레이는 그
                       웨이브에 영구히 멈췄습니다. 위의 축출이 있기 전에 측정한 결과, 웨이브
                       계수기가 12분 동안 3에 머무는 동안 스포너들은 분당 스무 번 남짓 다시
                       시도했습니다.

                       거절의 두 이유를 ::make_monster가 아니라 이곳에서 구분하는 이유는 둘 중
                       하나만 일시적이기 때문입니다. 살아 있는 몬스터로 가득한 방은 대기열이며,
                       이는 위의 `max_alive`가 "지금은 아님"으로 다루는 것과 같습니다. 자리가
                       생기면 무리가 여전히 도착하도록 예산을 유지합니다. 그 밖의 것은 동작하지
                       않는 생성 지점이고 아무리 기다려도 고쳐지지 않으므로, 예산 하나를
                       치르고 스포너가 웨이브를 영원히 열어 두는 대신 스스로 소진되게 합니다. */
                    if (enemy_alive(pl) < ENEMY_MAX && s->left > 0) s->left--;
                    break;
                }

                made    = 1;
                arrived = 1;
                if (s->left > 0) s->left--;
            }

            /* --- the portal discharging ----------------------------------
               ONCE PER GROUP, not once per monster. A burst of three arriving
               together is one event and one portal, and firing per monster
               would stack three copies of the same flash at the same point --
               additive quads that composite to white -- and spend three of the
               mixer's twelve voices saying one thing three times.

               Guarded on `arrived`, so a telegraph that promised monsters and
               then could not place them stays silent. The warning already
               played; adding an arrival to a spawn that never arrived would
               teach the player that the sound means nothing.

               무리마다 한 번이며 몬스터마다가 아닙니다. 함께 도착하는 셋은 하나의 사건이자
               하나의 포탈이고, 몬스터마다 발생시키면 같은 지점에 같은 섬광이 셋 겹쳐
               가산 합성되어 흰색이 되며, 한 가지를 세 번 말하자고 믹서의 열두 보이스 중 셋을
               씁니다.

               `arrived`로 지킵니다. 몬스터를 약속했으나 배치하지 못한 예고는 조용히
               끝납니다. 예고음은 이미 울렸고, 도착하지 않은 생성에 도착음을 붙이는 것은
               플레이어에게 그 소리가 아무 뜻도 없음을 가르치는 일입니다. */
            if (arrived) {
                fx_spawn(pl, "spawnburst", s->pos, v3f(0.0f, 1.0f, 0.0f));
                play_at(s->pos, "spawnpop", 85);
            }
            continue;
        }

        /* Retired only once the telegraph above has had its turn, so a spawner
           that spent its last of the budget still delivers what it warned about.
           위의 예고가 차례를 마친 뒤에만 은퇴시킵니다. 예산의 마지막을 쓴 스포너도 자신이
           예고한 것은 배달합니다. */
        if (s->left == 0) { s->active = 0; continue; }

        s->timer -= dt;
        if (s->timer > 0.0f) continue;

        /* The ceiling is checked HERE and not by skipping the tick, so a
           spawner held back by a crowded level makes its next monster as soon
           as there is room rather than waiting out another full interval. It
           is a queue, not a metronome that misses beats.
           상한은 틱을 건너뛰는 대신 *이곳에서* 검사합니다. 붐비는 레벨에 막힌 스포너가 또
           한 주기를 온전히 기다리는 대신 자리가 나는 즉시 다음 몬스터를 만들게 하기
           위함입니다. 박자를 놓치는 메트로놈이 아니라 대기열입니다. */
        if (s->max_alive > 0 && enemy_alive(pl) >= s->max_alive) continue;

        /* THE SAME KIND OF WAIT, for the same reason: the timer is not reset
           and the budget is not touched, so standing on a spawner postpones it
           rather than disarming it. Step away and it fires on the next frame.
           같은 종류의 기다림이며 이유도 같습니다. 타이머를 초기화하지 않고 예산도 건드리지
           않으므로, 스포너 위에 서 있는 것은 그것을 해제하는 것이 아니라 미루는 것입니다.
           물러나면 다음 프레임에 발동합니다. */
        if (spawner_crowded(s, player_eye)) continue;

        s->timer = spawn_wait(&pl->enemy, s->interval);
        s->warn  = SPAWN_WARN_TIME;

        /* --- the telegraph -------------------------------------------------
           Where the monsters will be, not where the spawner is: the effect has
           to read as the ground opening under the group, so it is placed at the
           feet and pointed up.

           TWO EFFECTS, ONE PORTAL. The column says WHEN and the ring says
           WHERE. A column alone hangs in the air and leaves the player to guess
           how much floor is about to be occupied, which is the question that
           decides whether they back off or keep shooting.

           스포너가 있는 곳이 아니라 몬스터가 있게 될 곳입니다. 이펙트는 무리 아래에서 땅이
           열리는 것으로 읽혀야 하므로 발치에 놓고 위를 향하게 합니다.

           이펙트는 둘이지만 포탈은 하나입니다. 기둥은 *언제*를, 고리는 *어디*를 말합니다.
           기둥만으로는 공중에 떠 있어, 바닥이 얼마나 점거될지를 플레이어가 짐작하게 남깁니다.
           그것이 물러설지 계속 쏠지를 결정하는 질문입니다. */
        fx_spawn(pl, "spawnwarp", s->pos, v3f(0.0f, 1.0f, 0.0f));
        fx_spawn(pl, "spawnring", s->pos, v3f(0.0f, 1.0f, 0.0f));

        /* Quieter than the arrival below, and quieter than any weapon. This is
           a thing to notice, not a thing to react to yet -- the reaction is due
           in SPAWN_WARN_TIME, and a warning as loud as the event trains the
           player to treat the two as one.
           아래의 도착음보다, 그리고 어떤 무기보다도 조용합니다. 이것은 알아차릴 것이지 아직
           반응할 것이 아닙니다. 반응은 SPAWN_WARN_TIME 뒤에 필요하며, 사건만큼 큰 예고는
           플레이어가 둘을 하나로 취급하도록 길들입니다. */
        play_at(s->pos, "spawnwarn", 70);
    }
    return made;
}

/* --- Projectiles / 발사체 --- */

/**
 * @brief Launches one monster projectile from `from` towards `at`.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl     Pools holding the shot ring.
 * @param[in]     from   Muzzle position, in world metres.
 * @param[in]     at     Point to aim at. The direction is taken once, here.
 * @param[in]     speed  Projectile speed, m/s.
 * @param[in]     damage Damage the shot deals on impact.
 *
 * @note The bolt is BALLISTIC, not homing: the velocity is fixed at launch, so
 *       a player who moves after it is fired can step out of its way. A shot
 *       that re-aimed would make dodging impossible and cover pointless.
 * @note Drops the bolt and raises ::DIAG_SHOT_CAP when every shot slot is in
 *       use, rather than failing silently -- a caster that finished its whole
 *       wind-up and produced nothing looks exactly like one that never
 *       attacked. See the note in the body.
 * @note Also returns without firing when `at` and `from` coincide, since there
 *       would be no direction to launch along.
 *
 * 한국어
 * ------
 * @brief 몬스터 발사체 하나를 `from`에서 `at`을 향해 발사합니다.
 *
 * @param[in,out] pl     발사체 링을 담고 있는 풀.
 * @param[in]     from   총구 위치. 월드 미터 단위입니다.
 * @param[in]     at     조준할 지점. 방향은 이곳에서 한 번만 정해집니다.
 * @param[in]     speed  발사체 속도(m/s).
 * @param[in]     damage 명중 시 피해량.
 * @param[in]     type   시전한 생물의 ::MonTypeID. 볼트가 어떤 색으로 그려지고 어떤 색으로
 *                       빛나는지를 결정하며, 그 외의 무엇도 결정하지 않습니다.
 *
 * @note 볼트는 유도가 아니라 *탄도*입니다. 속도가 발사 시점에 고정되므로, 발사된 뒤에
 *       움직인 플레이어는 피할 수 있습니다. 다시 조준하는 발사체는 회피를 불가능하게 하고
 *       엄폐를 무의미하게 만듭니다.
 * @note 모든 발사체 슬롯이 사용 중이면 조용히 실패하지 않고, 볼트를 버린 뒤
 *       ::DIAG_SHOT_CAP을 올립니다. 예비 동작을 다 마치고도 아무것도 내놓지 않은 캐스터는
 *       애초에 공격하지 않은 캐스터와 똑같이 보이기 때문입니다. 본문의 설명을 참조하십시오.
 * @note `at`과 `from`이 같은 지점이면 발사할 방향이 없으므로 역시 발사하지 않고 반환합니다.
 */
static void shot_fire(Pools *pl, v3 from, v3 at, float speed, int damage,
                      int type, int owner)
{
    Shot *s = 0;
    for (int i = 0; i < ENEMY_MAX_SHOTS; i++)
        if (!pl->enemy.shots[i].active)
        {
            s = &pl->enemy.shots[i];
            break;
        }
    /* Every other pool in the project reports when it turns something away,
       and this one did not. A caster that finished its whole wind-up and then
       produced no bolt is indistinguishable from one that never attacked --
       the animation plays either way -- so the symptom is "the caster
       sometimes just doesn't shoot", which names no cause at all. Costs
       nothing in release; see diag.h.
       이 프로젝트의 다른 모든 풀은 무언가를 거절할 때 보고하는데 이곳만 그러지
       않았습니다. 시전 동작을 전부 마치고도 볼트를 만들어 내지 못한 캐스터는 애초에
       공격하지 않은 캐스터와 구분되지 않습니다. 어느 쪽이든 애니메이션은 재생되기
       때문입니다. 그래서 증상은 "캐스터가 가끔 그냥 안 쏜다"가 되며, 이것만으로는 원인을
       전혀 알 수 없습니다. 릴리스에서는 비용이 없습니다. diag.h를 참조하십시오. */
    if (!s)
    {
        DIAG(DIAG_SHOT_CAP);
        return;
    }

    v3 d = v3sub(at, from);
    float len = v3len(d);
    if (len < 0.001f)
        return;
    d = v3scale(d, 1.0f / len);

    s->pos = from;
    s->vel = v3scale(d, speed);
    s->life = 6.0f;
    s->damage = damage;
    s->active = 1;
    s->owner  = (short)owner;
    /* Carried for the renderer and nothing else -- see ::Shot::type. Set here
       rather than at the call site's convenience because this is the one place
       a Shot comes into existence, and a field initialised anywhere else is a
       field some future second call site forgets.
       렌더러만을 위해 나릅니다. ::Shot::type을 참조하십시오. 호출 지점의 편의가 아니라
       이곳에서 설정하는 이유는, Shot이 생겨나는 곳이 이곳 하나이기 때문입니다. 다른 곳에서
       초기화되는 필드는 언젠가 생길 두 번째 호출 지점이 잊어버리는 필드입니다. */
    s->type   = type;
    /* Zero, not the interval: the first trail particle is laid on the frame
       the bolt appears, so the wake starts at the muzzle rather than a
       fraction of a second down the flight path.
       간격이 아니라 0입니다. 첫 궤적 파티클이 볼트가 나타나는 프레임에 놓이므로,
       흔적이 비행 경로 중간이 아니라 총구에서 시작됩니다. */
    s->trail_timer = 0.0f;
}

/**
 * @brief Advances every projectile and reports what it cost the player.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl         Pools holding the shots.
 * @param[in]     l          Level the shots collide against.
 * @param[in]     player_eye Where the player is, for the hit test.
 * @param[in]     dt         Seconds since the last frame.
 * @return Total damage the shots dealt to the player this frame.
 *
 * @note RETURNS damage rather than applayer_eyeing it, exactly as ::enemy_update
 *       does. Nothing in this file reaches the player's health.
 * @note The player is tested as a cylinder around `player_eye`, using
 *       ::PLAYER_RADIUS and ::PLAYER_EYE, so a shot cannot pass through a
 *       player who is standing still.
 *
 * 한국어
 * ------
 * @brief 모든 발사체를 진행시키고 플레이어가 치른 대가를 보고합니다.
 *
 * @param[in,out] pl         발사체를 담고 있는 풀.
 * @param[in]     l          발사체가 충돌하는 레벨.
 * @param[in]     player_eye 명중 판정을 위한 플레이어 위치.
 * @param[in]     dt         지난 프레임 이후 경과 시간(초).
 * @return 이번 프레임에 발사체가 플레이어에게 입힌 총 피해량.
 *
 * @note ::enemy_update와 똑같이, 피해를 적용하지 않고 *반환합니다*. 이 파일의 무엇도
 *       플레이어의 체력에 닿지 않습니다.
 * @note 플레이어는 ::PLAYER_RADIUS와 ::PLAYER_EYE를 써서 `player_eye` 둘레의 원기둥으로
 *       판정하므로, 가만히 서 있는 플레이어를 발사체가 통과할 수 없습니다.
 */
static int shots_update(Pools *pl, const Level *l, v3 player_eye, float dt)
{
    int dealt = 0;

    for (int i = 0; i < ENEMY_MAX_SHOTS; i++)
    {
        Shot *s = &pl->enemy.shots[i];
        if (!s->active)
            continue;

        s->life -= dt;
        if (s->life <= 0.0f)
        {
            s->active = 0;
            continue;
        }

        v3 step = v3scale(s->vel, dt);
        float dist = v3len(step);
        if (dist < 1e-6f)
            continue;
        v3 dir = v3scale(step, 1.0f / dist);

        /* Lay down the trail at a fixed interval rather than once per frame.
           Per-frame emission makes the trail's density depend on the frame
           rate -- visibly denser at 144fps than at 60 -- and lets one bolt in
           flight fill the shared 256-particle pool by itself, starving every
           other effect. The interval is in seconds, so the spacing along the
           bolt's path is the same however fast the machine runs.
           프레임마다가 아니라 고정 간격으로 궤적을 남깁니다. 프레임 단위 방출은 궤적
           밀도를 프레임률에 의존하게 만들고(144fps에서 60fps보다 눈에 띄게 조밀해집니다),
           비행 중인 볼트 하나가 공유 256개 파티클 풀을 혼자 채워 다른 이펙트를
           고갈시킵니다. 간격이 초 단위이므로 경로상의 간격은 기기 속도와 무관하게
           일정합니다. */
        s->trail_timer -= dt;
        if (s->trail_timer <= 0.0f)
        {
            s->trail_timer = SHOT_TRAIL_INTERVAL;
            /* Thrown backward along the flight, so what little spread the
               trail has drifts behind the bolt rather than ahead of it.
               비행 방향의 반대로 던집니다. 그래야 궤적이 가진 약간의 산포가 볼트 앞이
               아니라 뒤로 흩어집니다. */
            fx_spawn(pl, "bolttrail", s->pos, v3scale(dir, -1.0f));

            /* AND THE RIBBON, on the same timer and for the same reason it
               cannot be the same effect. `bolttrail` is additive and 280ms:
               it is the bolt's heat, it sits where the bolt was, and it is
               gone before the bolt has crossed the room. What a projectile
               leaves BEHIND is longer than its own flight, widens away from
               the head, and has to be able to be darker than the wall behind
               it -- and nothing additive can ever be darker than anything.
               Two layers with opposite blend modes is the only way one
               definition's single colour ramp can say both.
               그리고 *리본*이며, 같은 타이머로, 그리고 같은 이펙트가 될 수 없는 것과 같은
               이유로 그렇습니다. `bolttrail`은 가산이고 280ms입니다. 볼트의 열이고, 볼트가
               있던 자리에 놓이고, 볼트가 방을 가로지르기 전에 사라집니다. 발사체가 *뒤에*
               남기는 것은 자기 비행보다 오래가고, 머리에서 멀어질수록 넓어지며, 뒤의 벽보다
               어두울 수 있어야 합니다. 그런데 가산인 것은 무엇보다도 어두워질 수 없습니다.
               블렌드 모드가 반대인 두 겹이, 정의 하나의 색 램프 하나로 둘 다 말할 수 있는
               유일한 방법입니다. */
            fx_spawn(pl, "boltwake", s->pos, v3scale(dir, -1.0f));
        }

        float t;
        v3 n;
        int hit_wall = level_trace(l, s->pos, dir, dist + SHOT_RADIUS, &t, &n);

        v3 feet = v3f(player_eye.x, player_eye.y - PLAYER_EYE, player_eye.z);
        v3 rel = v3sub(s->pos, feet);
        float body_hit = -1.0f;
        {
            float rx = rel.x, rz = rel.z;
            float a = dir.x * dir.x + dir.z * dir.z;
            float b = 2.0f * (rx * dir.x + rz * dir.z);
            float rr = PLAYER_RADIUS + SHOT_RADIUS;
            float c = rx * rx + rz * rz - rr * rr;
            if (c <= 0.0f)
            {
                body_hit = 0.0f;
            }
            else if (a > 1e-6f)
            {
                float disc = b * b - 4.0f * a * c;
                if (disc >= 0.0f)
                {
                    float root = (-b - sqrtf(disc)) / (2.0f * a);
                    if (root >= 0.0f && root <= dist)
                        body_hit = root;
                }
            }
            if (body_hit >= 0.0f)
            {
                float y = s->pos.y + dir.y * body_hit;
                if (y < feet.y - 0.2f || y > feet.y + PLAYER_EYE + 0.35f)
                    body_hit = -1.0f;
            }
        }

        /* AND THE OTHER BODIES IN THE ROOM. Until this existed a monster's
           bolt tested the player's capsule and the level and nothing else, so
           two monsters could stand in each other's fire all day -- which is
           why there was no infighting to have: not a missing rule, a missing
           collision.
           NEAREST WINS, and it is compared against the wall on the same terms
           the player already was. A bolt that would have hit a wall first must
           not reach past it to a monster standing behind, and one that hits a
           monster in front of the player must not also charge the player.
           ::Shot::owner IS SKIPPED, because a caster stands where its own bolt
           is born. Skipping by TYPE instead would make two water spirits
           immune to each other, which is not the exemption Quake has -- that
           one is about who gets ANGRY, not about what lands.
           *그리고 방 안의 다른 몸들입니다.* 이것이 있기 전까지 몬스터의 탄환은 플레이어의
           캡슐과 레벨만 검사했으므로, 몬스터 둘은 온종일 서로의 사격 속에 서 있을 수
           있었습니다. 내분이 없었던 이유가 그것입니다. 빠진 규칙이 아니라 빠진 충돌입니다.
           *가장 가까운 것이 이기며*, 플레이어가 이미 그랬던 것과 같은 조건으로 벽과
           비교합니다. 벽에 먼저 맞았을 탄환이 그 너머의 몬스터에게 닿아서는 안 되고,
           플레이어 앞의 몬스터에 맞은 탄환이 플레이어에게까지 값을 물려서는 안 됩니다.
           *::Shot::owner를 건너뜁니다.* 캐스터는 자기 탄환이 태어나는 자리에 서 있기
           때문입니다. 대신 *종류*로 건너뛰면 물 정령 둘이 서로에게 무적이 되는데, 그것은
           Quake가 가진 면제가 아닙니다. 그쪽은 누가 *화를 내는가*에 대한 것이지 무엇이
           꽂히는가에 대한 것이 아닙니다. */
        int   mon_i = -1;
        float mon_t = -1.0f;
        for (int j = 0; j < pl->enemy.count; j++)
        {
            if (j == s->owner) continue;
            const Enemy *o = &pl->enemy.m[j];
            if (!o->active || o->state == E_DEAD) continue;

            /* AND A BOLT PASSES THROUGH ITS OWN KIND, which is where this
               parts company with Quake. There `T_Damage` exempts nothing: the
               classname test decides who gets ANGRY and the damage lands on
               everyone. Faithful, and wrong here, for a reason that is about
               this game's shape rather than about the rule.
               A Quake level places a mixed, spread-out population by hand. An
               arena spawns from a spawner, and a spawner makes ONE kind --
               so same-kind fire is not the occasional crossfire it is there,
               it is four casters clustered at a spawn point emptying into each
               other. Measured before this line existed: a ceiling of four fell
               to two over two minutes with the player standing still and doing
               nothing.
               THE DECIDING ARGUMENT IS THAT IT BUYS NOTHING. Same-kind damage
               can never start a feud -- the grudge is exempted by the very
               same classname rule -- so it is attrition the player did not
               cause, cannot cause, and cannot use. Infighting is a thing the
               player engineers by making two DIFFERENT kinds cross; the rest
               is a spawner quietly eating itself.
               *그리고 탄환은 자기 종류를 통과하며*, 이곳이 Quake와 갈라지는 지점입니다.
               그쪽의 `T_Damage`는 아무것도 면제하지 않습니다. classname 검사는 누가 *화를
               내는가*를 정하고 피해는 모두에게 꽂힙니다. 충실하지만 이곳에서는 틀렸으며,
               이유는 규칙이 아니라 이 게임의 모양에 있습니다.
               Quake의 레벨은 섞이고 흩어진 인구를 손으로 배치합니다. 투기장은 스포너에서
               나오고 스포너는 *한 종류*를 만듭니다. 그러므로 같은 종류의 사격은 그곳에서처럼
               이따금의 교차 사격이 아니라, 스폰 지점에 뭉친 캐스터 넷이 서로에게 쏟아붓는
               일입니다. 이 줄이 있기 전에 쟀습니다. 상한 넷이 2분 만에 둘로 떨어졌고,
               플레이어는 가만히 서서 아무것도 하지 않았습니다.
               *결정적인 논거는 그것이 아무것도 사지 않는다는 것입니다.* 같은 종류의 피해는
               결코 반목을 시작할 수 없습니다. 원한이 바로 그 classname 규칙으로 면제되기
               때문입니다. 그러므로 그것은 플레이어가 일으키지 않았고, 일으킬 수도 없고, 쓸
               수도 없는 소모입니다. 내분은 플레이어가 서로 *다른* 두 종류를 엇갈리게 만들어
               설계하는 것이고, 나머지는 스포너가 조용히 자기를 먹는 일입니다. */
            if (o->type == s->type) continue;

            const MonType *OS = &TYPES[o->type];
            v3 mid = v3f(o->pos.x, o->pos.y + OS->height * 0.5f, o->pos.z);
            v3 rel = v3sub(mid, s->pos);
            float along = v3dot(rel, dir);
            if (along < 0.0f || along > dist) continue;

            v3 near_p = v3add(s->pos, v3scale(dir, along));
            float dx = near_p.x - mid.x, dy = near_p.y - mid.y, dz = near_p.z - mid.z;
            float r = OS->radius + SHOT_RADIUS;
            float hy = OS->height * 0.5f + SHOT_RADIUS;
            if (dx*dx + dz*dz > r*r || dy*dy > hy*hy) continue;

            if (mon_i < 0 || along < mon_t) { mon_i = j; mon_t = along; }
        }

        if (mon_i >= 0 && (!hit_wall || mon_t <= t) &&
            (body_hit < 0.0f || mon_t <= body_hit))
        {
            enemy_hurt_by(pl, mon_i, s->damage, dir, s->owner);
            s->active = 0;
            play_at(s->pos, "ehit", 85);
            fx_spawn(pl, "boltburst", s->pos, v3scale(dir, -1.0f));
            fx_spawn(pl, "boltshard", s->pos, v3scale(dir, -1.0f));
            continue;
        }

        if (body_hit >= 0.0f && (!hit_wall || body_hit <= t))
        {
            dealt += s->damage;
            s->active = 0;
            play_at(s->pos, "ehit", 85);
            /* Burst back along the bolt's own travel, so it reads as coming
               off the thing it struck rather than continuing through it.
               볼트의 진행 방향 반대로 터뜨립니다. 그래야 대상을 통과하는 것이 아니라
               맞은 지점에서 튀어나오는 것으로 읽힙니다. */
            fx_spawn(pl, "boltburst", s->pos, v3scale(dir, -1.0f));
            fx_spawn(pl, "boltshard", s->pos, v3scale(dir, -1.0f));

            /* AND NOTHING ELSE, WHICH IS THE POINT OF THE TWO BRANCHES BEING
               TWO. The wall below gets a scorch and a puff of stone dust; this
               is a hit on the PLAYER, and a burn mark hanging in the air where
               a body was, with grit coming off it, is the effect weapon.c
               already refuses to put on a monster for the same reason -- it
               reads as having missed. Until this pair split, both landings
               played the same two lines and the difference between being shot
               and being shot AT was not in the picture anywhere.
               *그리고 그 외에는 아무것도 없으며, 그것이 두 갈래가 둘인 이유입니다.* 아래의 벽은
               그을음과 돌먼지를 받습니다. 이쪽은 *플레이어*에 대한 명중이며, 몸이 있던 자리의
               허공에 걸린 화상 자국과 거기서 이는 모래는 weapon.c가 같은 이유로 몬스터에게
               붙이기를 이미 거부하는 것입니다. 빗맞은 것으로 읽힙니다. 이 쌍이 갈라지기
               전까지 두 착탄은 같은 두 줄을 재생했고, 맞은 것과 맞을 뻔한 것의 차이는 화면
               어디에도 없었습니다. */
            proj_flash(pl, s->pos, PROJ_HIT_RADIUS, PROJ_HIT_POWER,
                       FLASH_SHOT, s->type, 0);
            continue;
        }

        if (hit_wall && t <= dist)
        {
            s->pos = v3add(s->pos, v3scale(dir, t));
            s->active = 0;
            play_at(s->pos, "ehit", 45);
            fx_spawn(pl, "boltburst", s->pos, n);
            fx_spawn(pl, "boltshard", s->pos, n);

            /* THE STONE HALF: a burn and a puff, which is what makes the hit
               happen TO the wall rather than in front of it. `smokepuff` is
               the shotgun's -- shared deliberately, the way the axe's slam
               borrows `blastwave`, because dust off a wall is dust off a wall
               and a second definition for it would only be a second place to
               retune. The mark is a `scorch` and not a `bullethole`: a bolt burns and a pellet chips,
               which is the distinction ioquake3 draws by picking
               energyMarkShader over bulletMarkShader at the same line.
               *돌 쪽 절반입니다.* 화상과 퍼프이며, 그것이 피탄을 벽 *앞*이 아니라 벽 *에*
               일어나게 만듭니다. `smokepuff`는 샷건의 것이고 의도적으로 공유합니다. 도끼의
               내려찍기가 `blastwave`를 빌리는 것과 같습니다. 벽에서 이는 먼지는 벽에서 이는
               먼지이고, 그것을 위한 두 번째 정의는 다시 조정할 곳이 하나 더 생기는 것일
               뿐입니다. 자국은 `bullethole`이 아니라 `scorch`입니다. 볼트는 태우고 산탄은 쪼며, ioquake3이 같은
               줄에서 bulletMarkShader가 아니라 energyMarkShader를 골라 긋는 구분이 그것입니다. */
            fx_spawn(pl, "scorch",    s->pos, n);
            fx_spawn(pl, "smokepuff", s->pos, n);
            proj_flash(pl, s->pos, PROJ_HIT_RADIUS, PROJ_HIT_POWER,
                       FLASH_SHOT, s->type, 0);
            continue;
        }

        s->pos = v3add(s->pos, step);
    }

    return dealt;
}

/* --- Collision and movement / 충돌과 이동 --- */

/**
 * @brief How high a ledge this kind walks straight up instead of being stopped by.
 *
 * ENGLISH
 * -------
 * NEVER LESS THAN ::PLAYER_STEP, and that floor is the whole of this function.
 * A staircase is a property of the LEVEL, not of whoever is climbing it: an
 * author builds risers to the height a player can clear -- 16 map units, half a
 * metre, the dimension brush.h keeps Quake's scale in order to inherit -- and
 * everything that walks the level has to be able to walk what the level is made
 * of. A monster with a shorter stride than the player is a monster that cannot
 * follow you anywhere the level says you may go, and it does not read as a
 * short creature. It reads as a broken one, standing at the foot of the stairs
 * running on the spot.
 *
 * The hound is who this was written for, and it is gone. A third of its 1.25m
 * was 0.417m, which clears nothing a map contains; it stopped dead at the first
 * riser of every staircase in the game while the brute walked up behind it. The
 * kind whose whole design was that it closes on you was the one kind that could
 * not.
 *
 * THE FLOOR OUTLIVED IT, and deliberately. Nothing walking today is short
 * enough to need it -- the shortest thing that moves is the water spirit at
 * 1.70m -- so this looks like a clamp that never fires. It is not a fact about
 * the current bestiary; it is a fact about what levels are built out of, and it
 * is what a short creature added tomorrow will land on instead of landing in a
 * bug report.
 *
 * A THIRD OF THE HEIGHT IS KEPT AS THE OTHER HALF of the max, because above
 * ::PLAYER_STEP it is doing something the floor cannot: a brute steps over
 * crates and low walls that a player has to go around, and that is the size of
 * the thing being read off the geometry rather than announced. The floor only
 * catches the kinds that fall below what the world is built from.
 *
 * @param[in] S The monster's kind.
 * @return The step height in metres, at least ::PLAYER_STEP.
 *
 * @note ::MON_FLIES does not come through here at all -- a flyer does not step,
 *       and ::air_ok passes its own large limit for exactly that reason.
 *
 * 한국어
 * ------
 * @brief 이 종류가 가로막히지 않고 그대로 걸어 올라가는 턱의 높이.
 *
 * *::PLAYER_STEP보다 낮을 수 없으며*, 그 하한이 이 함수의 전부입니다. 계단은 *레벨*의
 * 속성이지 오르는 자의 속성이 아닙니다. 제작자는 플레이어가 넘을 수 있는 높이로 단을
 * 만듭니다. 16맵유닛, 0.5미터이며, brush.h가 Quake의 스케일을 지키는 이유가 물려받으려는
 * 바로 그 치수입니다. 그리고 레벨을 걷는 모든 것은 레벨을 이루는 것을 걸을 수 있어야 합니다.
 * 플레이어보다 보폭이 짧은 몬스터는 레벨이 가도 된다고 말하는 어디로도 당신을 따라올 수 없는
 * 몬스터이며, 그것은 작은 생물로 읽히지 않습니다. 계단 아래에서 제자리걸음을 하는 고장 난
 * 것으로 읽힙니다.
 *
 * 이것이 필요했던 것은 하운드이며, 그것은 사라졌습니다. 1.25m의 3분의 1은 0.417m이었고, 맵에
 * 담긴 무엇도 넘지 못했습니다. 게임 안 모든 계단의 첫 단에서 멈춰 섰고 그 뒤로 브루트가 걸어
 * 올라갔습니다. 달려든다는 것이 설계의 전부인 종류가, 그러지 못하는 유일한 종류였습니다.
 *
 * *하한은 그것보다 오래 남았고, 그것은 의도된 일입니다.* 오늘 걷는 것 중에 이것이 필요할 만큼
 * 낮은 것은 없습니다. 움직이는 것 중 가장 낮은 것이 1.70m의 물의 정령입니다. 그래서 이것은 결코
 * 발동하지 않는 고정으로 보입니다. 그러나 이것은 현재 도감에 대한 사실이 아니라 레벨이 무엇으로
 * 지어지는가에 대한 사실이며, 내일 추가되는 낮은 생물이 버그 리포트가 아니라 이곳에 내려앉게
 * 하는 것입니다.
 *
 * *신장의 3분의 1은 max의 나머지 절반으로 남깁니다.* ::PLAYER_STEP 위에서는 하한이 할 수 없는
 * 일을 하기 때문입니다. 브루트는 플레이어가 돌아가야 하는 상자와 낮은 벽을 넘어서며, 그것은
 * 덩치를 선언하는 대신 지오메트리에서 읽히게 하는 방식입니다. 하한이 잡아내는 것은 세계를
 * 이루는 것보다 낮게 떨어진 종류뿐입니다.
 *
 * @param[in] S 몬스터의 종류.
 * @return 단차 높이(미터). 최소 ::PLAYER_STEP입니다.
 *
 * @note ::MON_FLIES는 이곳을 아예 지나지 않습니다. 비행체는 단을 딛지 않으며, ::air_ok가 자기
 *       큰 상한을 넘기는 이유가 정확히 그것입니다.
 */
static float mon_step(const MonType *S)
{
    float own = S->height / 3.0f;
    return own > PLAYER_STEP ? own : PLAYER_STEP;
}


/**
 * @brief Whether a monster may put its feet on the floor found at this column.
 *
 * ENGLISH
 * -------
 * @param[in] l   The level.
 * @param[in] x,z Where.
 * @param[in] f   The floor height ::level_ground found there.
 * @return 1 if standing there is survivable, 0 if it is lava.
 *
 * ONE RULE, TWO CALLERS, and that is why it is a function rather than a line.
 * ::foot_ok asks it before a monster STEPS somewhere and ::make_monster asks it
 * before one is PUT somewhere; the two answering differently is a monster that
 * spawns into a lava sea it would never have walked into. Psychofuge makes that
 * a real risk rather than a hypothetical -- its brute spawner stands 0.48m above
 * the lava and one ground ward slot 0.24m above it, so the room genuinely does
 * put furniture at the water's edge.
 *
 * ASKED AT THE FLOOR, not at the monster's middle. ::level_hazard_at takes a
 * point, and the lava brush is SOLID, so the point that is inside it is the one
 * on its surface -- which is exactly where a monster standing there would have
 * its feet. Asking at the eye finds nothing and reports a lava sea as walkable.
 *
 * @note ::air_ok DELIBERATELY DOES NOT CALL THIS. A flyer is not standing on
 *       anything, so lava underneath it is scenery -- and that asymmetry is the
 *       whole gameplay consequence of a lava floor: the caster crosses the sea
 *       and nothing else can. ::MON_FLIES's own note promised exactly this
 *       ("floating platforms and a chasm are only a threat if something can be
 *       out over them"), and until there was a floor worth fearing it was a
 *       promise nothing had collected on.
 *
 * 한국어
 * ------
 * @brief 몬스터가 이 기둥에서 찾은 바닥에 발을 놓아도 되는지.
 *
 * *하나의 규칙, 두 호출자*이며, 그것이 이것이 한 줄이 아니라 함수인 이유입니다. ::foot_ok는
 * 몬스터가 어딘가로 *딛기* 전에 묻고 ::make_monster는 어딘가에 *놓이기* 전에 묻습니다. 둘이
 * 다르게 답하는 것은 걸어 들어가지도 않았을 용암 바다에 스폰되는 몬스터입니다. Psychofuge는
 * 그것을 가정이 아니라 실제 위험으로 만듭니다. 브루트 스포너가 용암 위 0.48m, 지상 결계핵
 * 자리 하나가 0.24m에 있어, 이 방은 정말로 물가에 가구를 놓습니다.
 *
 * *몬스터의 한가운데가 아니라 바닥에서 묻습니다.* ::level_hazard_at은 점을 받고 용암 브러시는
 * *고체*이므로, 그 안에 있는 점은 그 표면 위의 점입니다. 그리고 그것이 바로 그곳에 선 몬스터의
 * 발이 놓일 자리입니다. 눈에서 물으면 아무것도 찾지 못하고 용암 바다를 걸을 수 있다고
 * 보고합니다.
 *
 * @note *::air_ok는 의도적으로 이것을 부르지 않습니다.* 비행체는 아무것도 딛고 있지 않으므로
 *       그 아래의 용암은 배경입니다. 그리고 그 비대칭이 용암 바닥이 낳는 게임플레이 결과의
 *       전부입니다. 캐스터는 바다를 건너고 다른 무엇도 건너지 못합니다. ::MON_FLIES 자신의
 *       주석이 정확히 이것을 약속했고("떠 있는 발판과 협곡은 그 위로 나올 수 있는 무언가가
 *       있어야만 위협이 됩니다"), 두려워할 만한 바닥이 생기기 전까지 그것은 아무도 현금화하지
 *       않은 약속이었습니다.
 */
static int floor_safe(const Level *l, float x, float f, float z) {
    return level_hazard_at(l, x, f, z) <= 0;
}

/**
 * @brief Whether this kind of monster can stand at a column.
 *
 * ENGLISH
 * -------
 * @param[in]  l     Level to test against.
 * @param[in]  S     The monster's kind, for its height.
 * @param[in]  x     Column x.
 * @param[in]  z     Column z.
 * @param[in]  feet  The monster's current foot height, so a step is measured
 *                   from where it actually is.
 * @param[out] floor Floor height it would stand on. Written only on success.
 * @return 1 if it fits and can reach the floor, 0 otherwise.
 *
 * @note Two separate refusals: no ground reachable within a step, and not
 *       enough headroom for ::MonType::height. A monster that fitted through
 *       one but not the other would walk into a crawlspace and stick.
 * @note The step limit is ::mon_step, which is where the stairs are decided.
 *
 * 한국어
 * ------
 * @brief 이 종류의 몬스터가 어떤 기둥에 설 수 있는가.
 *
 * @param[in]  l     판정할 레벨.
 * @param[in]  S     몬스터의 종류. 신장을 위해 필요합니다.
 * @param[in]  x     기둥의 x.
 * @param[in]  z     기둥의 z.
 * @param[in]  feet  몬스터의 현재 발 높이. 계단 오르기를 실제 위치에서 재기 위함입니다.
 * @param[out] floor 서게 될 바닥 높이. 성공했을 때만 기록됩니다.
 * @return 들어맞고 바닥에 닿을 수 있으면 1, 아니면 0입니다.
 *
 * @note 거절 사유가 둘로 나뉩니다. 한 걸음 안에 닿을 바닥이 없는 경우와,
 *       ::MonType::height만큼의 머리 공간이 없는 경우입니다. 한쪽만 통과하는 몬스터는 기어야
 *       하는 좁은 틈으로 걸어 들어가 끼게 됩니다.
 * @note 걸음 높이 제한은 신장의 3분의 1이며, 그래서 키 큰 몬스터는 키 작은 몬스터가 돌아가야
 *       결정되는 곳입니다.
 */
static int foot_ok(const Level *l, const MonType *S, float x, float z,
                   float feet, float *floor)
{
    /* FIVE SAMPLES, THE SAME FIVE ::can_stand TAKES, and until this existed
       ::MonType::radius was not a collision size at all. This function asked
       about ONE column -- the monster's centre -- so a monster could walk until
       its middle touched the wall face, with the whole of its body inside the
       geometry. The player has never been able to do that, because player.c
       samples its circle and this sampled a point.
       What that looked like: a sprite is drawn `height * aspect` wide and turns
       to face the camera, so half of that width swings through whatever the
       monster is standing against. A brute at a wall buried a metre of itself
       in it. The billboard was blamed, and the billboard was right -- there was
       simplayer_eye nothing holding the body out of the wall.
       다섯 개의 표본이며, ::can_stand가 취하는 바로 그 다섯 개입니다. 이것이 생기기 전까지
       ::MonType::radius는 충돌 크기가 아니었습니다. 이 함수는 기둥 *하나*, 몬스터의 중심만
       물었으므로, 몬스터는 자기 한가운데가 벽면에 닿을 때까지 걸어 들어갈 수 있었고 몸통
       전체가 지오메트리 안에 있었습니다. 플레이어는 결코 그럴 수 없었습니다. player.c는 자기
       원을 표본하고 이곳은 점을 표본했기 때문입니다.
       그 모습은 이렇습니다. 스프라이트는 `height * aspect` 너비로 그려지고 카메라를 향해
       돌므로, 그 너비의 절반이 몬스터가 기대 선 것을 휩쓸고 지나갑니다. 벽에 붙은 브루트는
       자기 몸 1미터를 벽에 묻었습니다. 빌보드가 탓을 들었지만 빌보드는 옳았습니다. 애초에
       몸을 벽 밖에 붙들어 두는 것이 없었을 뿐입니다. */
    static const float OX[5] = { 0.0f,  1.0f, -1.0f,  0.0f,  0.0f };
    static const float OZ[5] = { 0.0f,  0.0f,  0.0f,  1.0f, -1.0f };

    float step    = mon_step(S);
    float highest = -1e30f;

    for (int i = 0; i < 5; i++)
    {
        float f, c;
        if (!level_ground(l, x + OX[i] * S->radius, z + OZ[i] * S->radius,
                          feet, step, &f, &c))
            return 0;
        if (c - f < S->height)
            return 0;
        if (!floor_safe(l, x + OX[i] * S->radius, f, z + OZ[i] * S->radius))
            return 0;
        if (f > highest)
            highest = f;
    }

    /* THE HIGHEST SAMPLE, not the centre's, for ::can_stand's reason: standing
       on the middle of a step that only one edge of the circle is over sinks
       the monster into it. No second step test is needed after this -- every
       sample was measured against the same `feet` and the same limit, so their
       maximum is inside it too.
       중심이 아니라 가장 높은 표본입니다. ::can_stand와 같은 이유로, 원의 한쪽 가장자리만
       걸친 단의 중간에 서면 몬스터가 그 안으로 가라앉습니다. 이 뒤에 단차 검사를 또 할
       필요는 없습니다. 모든 표본이 같은 `feet`과 같은 상한으로 재어졌으므로 그 최댓값도
       상한 안입니다. */
    *floor = highest;
    return 1;
}

/**
 * @brief Whether a flyer may occupy a column at the height it is already at.
 *
 * ENGLISH
 * -------
 * ::foot_ok asks "is there floor to stand on", which is the wrong question for
 * something that is not standing. This asks the only one that matters in the
 * air: is the gap at this column open where the creature actually is. A flyer
 * over a chasm has no floor under it and must still be allowed to be there;
 * one under a low ceiling must not.
 *
 * @note The step limit is large rather than ::mon_step, because a flyer does
 *       not step. A small limit makes ::level_ground refuse the column the
 *       moment the floor below drops away, which is exactly the chasm a flyer
 *       exists to hold.
 *
 * 한국어
 * ------
 * @brief 비행체가 지금 있는 높이에서 어떤 기둥을 차지해도 되는가.
 *
 * ::foot_ok는 "딛고 설 바닥이 있는가"를 묻는데, 서 있지 않은 것에게는 틀린 질문입니다. 이
 * 함수는 공중에서 유일하게 중요한 것을 묻습니다. 이 기둥의 빈 공간이 그 생물이 실제로 있는
 * 높이에서 열려 있는가입니다. 협곡 위의 비행체는 아래에 바닥이 없어도 그곳에 있을 수 있어야
 * 하고, 낮은 천장 아래의 비행체는 그럴 수 없어야 합니다.
 *
 * @note 단차 상한이 ::mon_step이 아니라 큰 값인 이유는 비행체가 단차를 딛지 않기
 *       때문입니다. 작은 상한은 아래의 바닥이 떨어지는 순간 ::level_ground가 그 기둥을
 *       거부하게 만들며, 그곳이 바로 비행체가 존재하는 이유인 협곡입니다.
 */
static int air_ok(const Level *l, const MonType *S, float x, float z, float y)
{
    /* THE SAME FIVE SAMPLES ::foot_ok TAKES, for the same reason. A flyer is a
       body of the same width as anything else, and asking about its centre
       alone let the caster hold station with its middle on the wall face and the
       rest of it inside. Fixing the walkers and leaving this would have left
       the one kind that hovers as the one kind still buried -- and it is the
       kind most likely to be found pressed against something, because backing
       away from the player is what the caster does.
       ::foot_ok가 취하는 바로 그 다섯 표본이며, 이유도 같습니다. 비행체도 다른 것과 같은
       너비의 몸이고, 중심만 묻는 것은 캐스터가 자기 한가운데를 벽면에 둔 채 나머지를 안에
       넣고 정지 비행하도록 허용했습니다. 걷는 것들만 고치고 이곳을 두었다면, 떠 있는 유일한
       종류가 여전히 묻혀 있는 유일한 종류로 남았을 것입니다. 그리고 무언가에 눌려 있기
       가장 쉬운 것이 그 종류입니다. 플레이어에게서 물러나는 것이 캐스터가 하는 일이기
       때문입니다. */
    static const float OX[5] = { 0.0f,  1.0f, -1.0f,  0.0f,  0.0f };
    static const float OZ[5] = { 0.0f,  0.0f,  0.0f,  1.0f, -1.0f };

    for (int i = 0; i < 5; i++)
    {
        float f, c;
        if (!level_ground(l, x + OX[i] * S->radius, z + OZ[i] * S->radius,
                          y, 1e9f, &f, &c))
            return 0;
        if (y < f)              return 0;   /* inside the floor */
        if (y + S->height > c)  return 0;   /* head through the ceiling */
    }
    return 1;
}

/**
 * @brief Whether something holds its height instead of being pulled to the floor.
 *
 * ENGLISH
 * -------
 * Everything in this world falls. Two flags say otherwise, they say it for
 * different reasons, and this function is the one place that difference is
 * spelled out.
 *
 * ::MON_ANCHORED HOLDS THROUGH DEATH. An anchored thing keeps its height
 * because it is part of the wall, and being killed does not take it out of the
 * wall -- a maw whose corpse sank to the floor would be a maw that is no longer
 * in the geometry it was drawn into. Left out of this test entirely, one placed
 * two metres up sinks over the first seconds of the fight and the flag looks
 * broken when what is broken is that "does not move" was only ever taught to
 * the walk. tools/bosstest.c found that by standing one for ten seconds.
 *
 * ::MON_FLIES DOES NOT. It holds a monster up because the monster is FLYING,
 * and a corpse is not flying -- it is a body, and the floor is owed it. This is
 * the whole of the fix for a caster that died six metres up and hung there; see
 * the ::E_DEAD branch in ::enemy_update for what that looked like.
 *
 * @param[in] S The monster's kind, for its flags.
 * @param[in] m The monster, for ::Enemy::state -- the flags alone cannot answer.
 * @return 1 if it keeps the height it is at, 0 if gravity has it.
 *
 * @note ::make_monster asks the same question of a monster that does not exist
 *       yet, and asks it of the flags alone -- it has no state to consult and
 *       nothing is ever spawned dead. That is the one place the two bits may
 *       still be read together.
 *
 * 한국어
 * ------
 * @brief 바닥으로 끌려 내려가는 대신 자기 높이를 유지하는가.
 *
 * 이 세계의 모든 것은 떨어집니다. 두 플래그가 그렇지 않다고 말하며, 서로 다른 이유로 그렇게
 * 말합니다. 그 차이를 적어 두는 단 한 곳이 이 함수입니다.
 *
 * *::MON_ANCHORED는 죽음을 넘어 유지됩니다.* 고정된 것이 높이를 지키는 이유는 벽의 일부이기
 * 때문이고, 죽는다고 벽에서 빠져나오지는 않습니다. 시체가 바닥으로 가라앉은 아귀는 자신이
 * 그려져 들어간 지오메트리에 더 이상 있지 않은 아귀입니다. 이 검사에서 통째로 빠지면 2미터
 * 위에 놓인 것이 전투 첫 몇 초 동안 가라앉고, 정작 고장 난 것은 "움직이지 않는다"를 걷기에만
 * 가르쳤다는 사실인데 플래그가 고장 난 것처럼 보입니다. tools/bosstest.c가 하나를 10초간 세워
 * 두어 그것을 찾았습니다.
 *
 * *::MON_FLIES는 그렇지 않습니다.* 그것이 몬스터를 떠받치는 이유는 몬스터가 *날고 있기*
 * 때문이며, 시체는 날고 있지 않습니다. 시체는 몸이고, 바닥은 그것을 받을 몫이 있습니다. 6미터
 * 위에서 죽어 그 자리에 걸린 캐스터에 대한 수정의 전부가 이것입니다. 그것이 어떤 모습이었는지는
 * ::enemy_update의 ::E_DEAD 분기를 참조하십시오.
 *
 * @param[in] S 몬스터의 종류. 플래그를 얻기 위함입니다.
 * @param[in] m 몬스터. ::Enemy::state를 얻기 위함이며, 플래그만으로는 답할 수 없습니다.
 * @return 지금 높이를 유지하면 1, 중력이 잡고 있으면 0.
 *
 * @note ::make_monster는 아직 존재하지 않는 몬스터에게 같은 질문을 하며, 플래그만으로 묻습니다.
 *       참조할 상태가 없고 죽은 채로 생성되는 것도 없기 때문입니다. 두 비트를 여전히 함께 읽어도
 *       되는 유일한 곳입니다.
 */
static int holds_height(const MonType *S, const Enemy *m)
{
    if (S->flags & MON_ANCHORED)
        return 1;
    return (S->flags & MON_FLIES) != 0 && m->state != E_DEAD;
}

/**
 * @brief Pulls a monster down to whatever is under it, one frame's worth.
 *
 * ENGLISH
 * -------
 * @param[in]     l  Level to ask for the floor.
 * @param[in]     S  The monster's kind, for the step limit its height sets.
 * @param[in,out] m  Monster to pull down. Position and vertical velocity change.
 * @param[in]     dt Timestep in seconds.
 *
 * @note Run for the living and for corpses alike -- ::holds_height decides who
 *       is exempt, and this function does not ask. It was inline in
 *       ::enemy_update and reached by only one of those two, which is how a
 *       flyer's corpse came to hang in the air.
 * @note A column with no floor at all -- outside the map -- leaves the monster
 *       where it is. ::level_ground answering 0 is not a height to fall to, and
 *       falling to zero would drop it out of the world.
 *
 * 한국어
 * ------
 * @brief 몬스터를 그 아래에 있는 것으로 한 프레임만큼 끌어내립니다.
 *
 * @param[in]     l  바닥을 물어볼 레벨.
 * @param[in]     S  몬스터의 종류. 신장이 정하는 단차 상한을 얻기 위함입니다.
 * @param[in,out] m  끌어내릴 몬스터. 위치와 수직 속도가 바뀝니다.
 * @param[in]     dt 시간 간격(초).
 *
 * @note 살아 있는 것과 시체 모두에 대해 실행됩니다. 누가 면제인지는 ::holds_height가 정하며 이
 *       함수는 묻지 않습니다. 이것은 ::enemy_update 안에 인라인으로 있었고 그 둘 중 하나에서만
 *       닿았습니다. 비행체의 시체가 공중에 걸리게 된 경위가 그것입니다.
 * @note 바닥이 아예 없는 기둥, 즉 맵 바깥에서는 몬스터를 그대로 둡니다. ::level_ground의 0은
 *       떨어질 높이가 아니며, 0으로 떨어뜨리면 세계 밖으로 빠집니다.
 */
static void monster_fall(const Level *l, const MonType *S, Enemy *m, float dt)
{
    float f, c;
    if (!level_ground(l, m->pos.x, m->pos.z, m->pos.y, mon_step(S), &f, &c))
        return;

    if (m->pos.y > f + 0.01f)
    {
        m->vel_y -= PLAYER_GRAVITY * dt;
        m->pos.y += m->vel_y * dt;
        if (m->pos.y <= f)
        {
            m->pos.y = f;
            m->vel_y = 0.0f;
        }
    }
    else
    {
        m->pos.y = f;
        m->vel_y = 0.0f;
    }
}

/**
 * @brief Moves a monster by (dx, dz), sliding along whatever blocks it.
 *
 * ENGLISH
 * -------
 * @param[in]     l  Level to collide against.
 * @param[in]     S  The monster's kind.
 * @param[in,out] m  Monster to move. Its position and floor height are updated.
 * @param[in]     dx Requested movement along x, in metres.
 * @param[in]     dz Requested movement along z, in metres.
 *
 * @note SLIDES rather than stopping: a blocked diagonal is retried as each
 *       axis alone, so a monster brushing a wall keeps walking along it
 *       instead of standing still and looking broken. That is the same
 *       resolution the player's own movement uses.
 * @note A flyer holds its height through ::air_ok; everything else is put on
 *       the floor ::foot_ok found.
 *
 * 한국어
 * ------
 * @brief 몬스터를 (dx, dz)만큼 이동시키며, 막히는 것을 따라 미끄러집니다.
 *
 * @param[in]     l  충돌 판정할 레벨.
 * @param[in]     S  몬스터의 종류.
 * @param[in,out] m  이동시킬 몬스터. 위치와 바닥 높이가 갱신됩니다.
 * @param[in]     dx x축으로 요청된 이동량(미터).
 * @param[in]     dz z축으로 요청된 이동량(미터).
 *
 * @note 멈추지 않고 *미끄러집니다*. 막힌 대각선 이동은 각 축을 따로 다시 시도하므로, 벽을
 *       스치는 몬스터가 제자리에 서서 고장 난 것처럼 보이는 대신 벽을 따라 계속 걷습니다.
 *       플레이어 자신의 이동이 쓰는 것과 같은 해결 방식입니다.
 * @note 비행체는 ::air_ok를 통해 높이를 유지하고, 그 밖의 모든 것은 ::foot_ok가 찾은 바닥에
 *       놓입니다.
 */
/**
 * Is `self` free to stand at (x, z) with its feet at `y`, as far as the OTHER
 * monsters are concerned?
 *
 * ENGLISH
 * -------
 * THE THIRD READER OF ::MonType::radius. That field's own note says two halves
 * of it are true -- it is what a monster is to shoot at, and what it is to the
 * level's geometry -- and this is the half that was missing: what it is to
 * another monster. Until now four of them converging on the player arrived as
 * one sprite with four healths, because nothing anywhere asked whether the
 * space was taken.
 *
 * CYLINDERS, NOT SPHERES, because a flyer and a walker are allowed to share a
 * column of floor. The caster crosses the room six metres up and the brute
 * walks under it; a sphere test would have them shove each other through the
 * ceiling of the fight. The vertical test is the exact one -- two spans overlap
 * unless one ends before the other starts -- rather than a distance between
 * centres, because ::MonType::height ranges from the ward's 1.1 to the maw's
 * 3.6 and a single margin cannot be right for both.
 *
 * A CORPSE IS NOT IN THE WAY. ::E_DEAD is skipped for the reason
 * ::enemy_hitscan skips it: what is left is a sprite on the floor, and a body
 * you cannot walk over is a body that blocks a doorway forever.
 *
 * AND A PAIR THAT ALREADY OVERLAPS IS NOT THIS FUNCTION'S BUSINESS, which is
 * the subtle half and the one a naive version gets wrong. If being inside
 * another monster made every step illegal, then two that started inside each
 * other could never separate -- every direction out of an overlap is still an
 * overlap until the last centimetre of it, so the refusal would weld them
 * together permanently. Spawners stack monsters at one ward and the maw summons
 * a handful at one point, so that state is ordinary rather than exotic. It
 * belongs to ::separate_monsters, which pushes rather than refuses, and this
 * function steps over any pair already in it.
 *
 * 한국어
 * ------
 * *::MonType::radius의 세 번째 독자입니다.* 그 필드의 주석은 두 절반이 참이라고 말합니다.
 * 쏘아 맞히기에 얼마나 넓은가와 레벨 지오메트리에 대해 얼마나 넓은가입니다. 빠져 있던 절반이
 * 이것입니다. *다른 몬스터에 대해* 얼마나 넓은가. 지금까지 플레이어에게 모여드는 넷은
 * 체력이 넷인 스프라이트 하나로 도착했습니다. 어디에서도 그 자리가 찼는지 묻지 않았기
 * 때문입니다.
 *
 * *구가 아니라 원기둥*인 이유는 비행체와 보행체가 같은 바닥 기둥을 나눠 써도 되기
 * 때문입니다. 캐스터는 6미터 위로 방을 가로지르고 브루트는 그 아래를 걷습니다. 구 검사는
 * 둘이 서로를 전투의 천장 너머로 밀어내게 만듭니다. 수직 검사는 중심 사이의 거리가 아니라
 * 정확한 것(한쪽이 다른 쪽이 시작하기 전에 끝나지 않는 한 두 구간은 겹칩니다)입니다.
 * ::MonType::height가 워드의 1.1에서 아귀의 3.6까지 걸쳐 있고, 하나의 여유값이 둘 다에게
 * 맞을 수는 없기 때문입니다.
 *
 * *시체는 길을 막지 않습니다.* ::E_DEAD를 건너뛰는 이유는 ::enemy_hitscan이 건너뛰는 이유와
 * 같습니다. 남은 것은 바닥의 스프라이트이고, 넘어갈 수 없는 시체는 문간을 영원히 막는
 * 시체입니다.
 *
 * *그리고 이미 겹쳐 있는 쌍은 이 함수의 일이 아니며*, 그것이 미묘한 절반이자 순진한 판이
 * 틀리는 곳입니다. 다른 몬스터 안에 있다는 것이 모든 걸음을 불법으로 만든다면, 서로 안에서
 * 시작한 둘은 결코 떨어질 수 없습니다. 겹침에서 나가는 모든 방향은 마지막 1센티미터까지
 * 여전히 겹침이므로, 거절은 그들을 영구히 용접합니다. 스포너는 한 워드에 몬스터를 쌓고
 * 아귀는 한 점에 여럿을 소환하므로, 그 상태는 예외가 아니라 일상입니다. 그것은 거절이 아니라
 * 밀어내는 ::separate_monsters의 몫이고, 이 함수는 이미 그 상태에 있는 쌍을 넘어갑니다.
 */
static int mon_clear(const Pools *pl, const MonType *S, const Enemy *self,
                     v3 player_eye, float x, float z, float y)
{
    /* AND THE PLAYER IS ONE OF THE CYLINDERS, which world.c's note said the
       opposite of and was wrong about.

       That note argued the asymmetry was deliberate: "a monster the player's
       own cylinder could hold at bay is a monster the player can never be hit
       by". The arithmetic says otherwise. A brute reaches 2.3m and its contact
       distance is 0.35 + 0.806 = 1.16m; a caster's swing reaches 2.2m against
       0.90m. Every melee attack in the table reaches roughly twice as far as
       the bodies touch, so being stopped at contact costs a monster nothing it
       needs. What the asymmetry actually bought was a monster standing INSIDE
       the player, and until the swing began to close nothing had a reason to
       walk there -- a brawler stopped at its band, which is further out than
       contact. ::MON_SWING_CLOSE gave it the reason and steptest measured the
       result: holding forward at a brute put the player 0.18m from its centre,
       well inside a body 0.81m wide.

       THE SAME TWO-LINE RULE AS THE MONSTERS'. Already overlapping is not this
       function's business -- a monster that finds itself inside the player, by
       a spawn or by this rule arriving mid-fight, must be able to walk out.

       *그리고 플레이어도 그 원기둥 중 하나이며*, world.c의 주석은 그 반대를 말했고 틀렸습니다.
       그 주석은 비대칭이 의도적이라고 논했습니다. "플레이어의 원기둥이 밀어낼 수 있는 몬스터는
       플레이어가 결코 맞을 수 없는 몬스터"라고요. 산술이 아니라고 말합니다. 브루트는 2.3m를
       뻗고 접촉 거리는 0.35 + 0.806 = 1.16m이며, 캐스터의 휘두르기는 0.90m에 대해 2.2m입니다.
       표의 모든 근접 공격은 몸이 닿는 거리의 두 배쯤을 뻗으므로, 접촉에서 멈추는 것은 몬스터가
       필요로 하는 무엇도 앗아가지 않습니다. 그 비대칭이 실제로 사 준 것은 *플레이어 안에 선
       몬스터*였고, 휘두르기가 붙기 시작하기 전까지는 그곳으로 걸어갈 이유가 없었습니다.
       근접형은 자기 대역에서 멈췄고 그것은 접촉보다 바깥입니다. ::MON_SWING_CLOSE가 이유를
       주었고 steptest가 결과를 쟀습니다. 브루트에게 전진을 누르고 있으면 플레이어가 그 중심에서
       0.18m 거리에 놓였고, 그것은 0.81m 폭인 몸의 한참 안쪽입니다.
       *몬스터끼리와 똑같은 두 줄 규칙입니다.* 이미 겹쳐 있는 것은 이 함수의 일이 아닙니다.
       생성으로든 이 규칙이 전투 도중 도착해서든 플레이어 안에 있게 된 몬스터는 걸어 나올 수
       있어야 합니다. */
    {
        float pr = S->radius + PLAYER_RADIUS;
        float pfeet = player_eye.y - PLAYER_EYE;
        if (y < pfeet + PLAYER_EYE && pfeet < y + S->height)
        {
            float cx = self->pos.x - player_eye.x, cz = self->pos.z - player_eye.z;
            float dx = x - player_eye.x, dz = z - player_eye.z;
            if (cx * cx + cz * cz >= pr * pr && dx * dx + dz * dz < pr * pr)
                return 0;
        }
    }

    for (int i = 0; i < pl->enemy.count; i++)
    {
        const Enemy *o = &pl->enemy.m[i];
        if (o == self || !o->active || o->state == E_DEAD)
            continue;

        const MonType *OS = &TYPES[o->type];
        if (y >= o->pos.y + OS->height || o->pos.y >= y + S->height)
            continue;

        float r = S->radius + OS->radius;

        float cx = self->pos.x - o->pos.x, cz = self->pos.z - o->pos.z;
        if (cx * cx + cz * cz < r * r)
            continue;                      /* already inside it: see above */

        float dx = x - o->pos.x, dz = z - o->pos.z;
        if (dx * dx + dz * dz < r * r)
            return 0;
    }
    return 1;
}

static void move_toward(const Pools *pl, const Level *l, const MonType *S,
                        Enemy *m, v3 player_eye, float dx, float dz)
{
    /* AN ANCHORED MONSTER REFUSES EVERY STEP, and refusing it HERE is what
       makes the flag mean one thing. The maw is an ::AI_CASTER, which is a
       whole archetype's worth of behaviour -- band-keeping, strafing, backing
       off -- and every bit of it arrives as a call to this function. Denying
       the movement rather than teaching ::chase_caster about a boss leaves that
       function saying what it has always said, and leaves ::MON_ANCHORED
       readable as "it does not move" rather than as a list of the four callers
       that were remembered.
       고정된 몬스터는 모든 걸음을 거절하며, *이곳에서* 거절하는 것이 이 플래그를 한 가지
       뜻으로 만듭니다. 아귀는 ::AI_CASTER이고 그것은 대역 유지, 횡이동, 후퇴라는 아키타입
       하나 분량의 행동인데, 그 전부가 이 함수 호출로 도착합니다. ::chase_caster에게 보스를
       가르치는 대신 이동을 거부하면 그 함수는 언제나 하던 말을 그대로 하게 되고,
       ::MON_ANCHORED는 기억해 낸 네 호출자의 목록이 아니라 "움직이지 않는다"로 읽힙니다. */
    if (S->flags & MON_ANCHORED)
        return;

    /* A FLYER KEEPS ITS ALTITUDE THROUGH A MOVE, and this is the third and last
       place that had to be told. ::MON_FLIES skips the fall and ::make_monster
       places it high, and then this function put `m->pos.y = f` on every step
       it took -- so a caster spawned six metres up sank to the floor over the
       first second of walking, one move at a time, and the flag looked broken
       when what was broken was the assumption three functions deep that a
       monster's height is whatever is under it.
       비행체는 이동 중에도 고도를 유지하며, 이곳이 그것을 알려야 했던 세 번째이자 마지막
       지점입니다. ::MON_FLIES가 낙하를 건너뛰고 ::make_monster가 높이 배치하는데, 그다음 이
       함수가 걸음마다 `m->pos.y = f`를 놓았습니다. 그래서 6미터 위에 생성된 캐스터가 걷기
       시작한 첫 1초 동안 한 걸음씩 바닥으로 가라앉았고, 플래그가 고장 난 것처럼 보였지만
       실제로 고장 난 것은 몬스터의 높이가 그 아래에 있는 것이라는, 함수 셋 깊이의
       가정이었습니다. */
    if (S->flags & MON_FLIES)
    {
        if (air_ok(l, S, m->pos.x + dx, m->pos.z + dz, m->pos.y) &&
            mon_clear(pl, S, m, player_eye, m->pos.x + dx, m->pos.z + dz, m->pos.y))
        {
            m->pos.x += dx;
            m->pos.z += dz;
            return;
        }
        if (air_ok(l, S, m->pos.x + dx, m->pos.z, m->pos.y) &&
            mon_clear(pl, S, m, player_eye, m->pos.x + dx, m->pos.z, m->pos.y))
        {
            m->pos.x += dx;
            return;
        }
        if (air_ok(l, S, m->pos.x, m->pos.z + dz, m->pos.y) &&
            mon_clear(pl, S, m, player_eye, m->pos.x, m->pos.z + dz, m->pos.y))
            m->pos.z += dz;
        return;
    }

    /* `f` AND NOT `m->pos.y` IN THE MONSTER TEST, because the step may change
       the height it stands at and the cylinder it is asking about is the one it
       would occupy AFTER the step. A monster stepping up onto a ledge beside
       another one is asking whether it fits up there, not down here.
       몬스터 검사에서 `m->pos.y`가 아니라 `f`인 이유는, 걸음이 서 있는 높이를 바꿀 수 있고
       묻고 있는 원기둥은 걸음 *뒤에* 차지할 그것이기 때문입니다. 다른 하나 옆의 턱으로 올라서는
       몬스터는 저 위에 자리가 있는지를 묻고 있지 이 아래를 묻고 있지 않습니다. */
    float f;
    if (foot_ok(l, S, m->pos.x + dx, m->pos.z + dz, m->pos.y, &f) &&
        mon_clear(pl, S, m, player_eye, m->pos.x + dx, m->pos.z + dz, f))
    {
        m->pos.x += dx;
        m->pos.z += dz;
        m->pos.y = f;
        return;
    }
    if (foot_ok(l, S, m->pos.x + dx, m->pos.z, m->pos.y, &f) &&
        mon_clear(pl, S, m, player_eye, m->pos.x + dx, m->pos.z, f))
    {
        m->pos.x += dx;
        m->pos.y = f;
        return;
    }
    if (foot_ok(l, S, m->pos.x, m->pos.z + dz, m->pos.y, &f) &&
        mon_clear(pl, S, m, player_eye, m->pos.x, m->pos.z + dz, f))
    {
        m->pos.z += dz;
        m->pos.y = f;
    }
}

/* --- Quake's ChangeYaw, in radians per second ----------------------------
 *
 * Turns `m->yaw` towards `m->ideal_yaw` by at most `yaw_speed`, the short way
 * round. Quake's version works in degrees on a 0.1s think; this takes a dt so
 * the turn rate is a property of the monster rather than of the frame rate.
 *
 * The short-way-round arithmetic is the part that is easy to get wrong and
 * invisible when it is: a monster that takes the long way round spins almost
 * all the way about to face something just behind it, which looks less like a
 * bug than like a very confused animal.
 *
 * `m->yaw`를 `m->ideal_yaw` 쪽으로 최대 `yaw_speed`만큼, *가까운 쪽으로* 돌립니다.
 * Quake의 것은 0.1초 think에서 도(度)로 동작하지만, 이것은 dt를 받으므로 회전 속도가
 * 프레임률이 아니라 몬스터의 속성이 됩니다. 가까운 쪽으로 도는 계산이 틀리기 쉽고 틀려도
 * 눈에 띄지 않는 부분입니다. 먼 쪽으로 도는 몬스터는 바로 뒤에 있는 것을 보려고 거의 한
 * 바퀴를 도는데, 결함이라기보다 몹시 혼란스러운 짐승처럼 보입니다. */
static void change_yaw(Enemy *m, float yaw_speed_deg, float dt)
{
    float move = m->ideal_yaw - m->yaw;
    while (move > M_PI_F)
        move -= M_TAU;
    while (move < -M_PI_F)
        move += M_TAU;

    float step = yaw_speed_deg * (M_PI_F / 180.0f) * dt;
    if (move > step)
        move = step;
    if (move < -step)
        move = -step;

    m->yaw += move;
    while (m->yaw > M_PI_F)
        m->yaw -= M_TAU;
    while (m->yaw < -M_PI_F)
        m->yaw += M_TAU;
}

/* --- Quake's ai_run_slide ------------------------------------------------
 *
 * Circle the player: face them, move at right angles. If that side is blocked,
 * flip and take the other -- which is the whole of Quake's obstacle handling
 * here and is enough, because the only thing a circling monster needs to do
 * about a wall is circle the other way.
 *
 * The direction is HELD for MON_SLIDE_HOLD rather than re-picked per frame.
 * Doom keeps a movecount for the same reason: a monster that re-decides at
 * frame rate vibrates in place, because the choice flips as fast as the
 * geometry under it changes.
 *
 * 플레이어 주위를 돕니다. 마주 본 채 직각으로 움직이고, 그쪽이 막히면 뒤집어 반대쪽으로
 * 갑니다. 그것이 여기서 Quake가 하는 장애물 처리의 전부이며 그것으로 충분합니다. 원을
 * 그리는 몬스터가 벽에 대해 해야 할 일은 반대로 도는 것뿐이기 때문입니다. 방향은 매
 * 프레임 다시 고르지 않고 MON_SLIDE_HOLD 동안 유지합니다. Doom이 movecount를 두는 이유도
 * 같습니다. 프레임률로 다시 결정하는 몬스터는 제자리에서 떱니다. */
/* THE ONE COMMITTED DIRECTION, and both the weave and the strafe ask for it
   here so they cannot disagree. Held for ::MON_SLIDE_HOLD for Doom's reason,
   written above ::ai_run_slide: a monster that re-decides at frame rate
   vibrates in place, because the choice flips as fast as the geometry under it
   changes.
   *하나의 정해진 방향*이며, 갈지자와 횡이동이 모두 이곳에서 그것을 물으므로 서로 어긋날 수
   없습니다. ::ai_run_slide 위에 적힌 Doom의 이유로 ::MON_SLIDE_HOLD 동안 유지합니다. 프레임
   단위로 다시 정하는 몬스터는 제자리에서 진동합니다. 아래의 지형이 바뀌는 속도만큼 빠르게
   선택이 뒤집히기 때문입니다. */
static float committed_side(Pools *pl, const MonType *S, Enemy *m)
{
    if (m->slide_wait <= 0.0f)
    {
        m->lefty = (char)(frand(&pl->enemy) < 0.5f);
        m->slide_wait = MON_SLIDE_HOLD(S->speed);
    }
    return m->lefty ? 1.0f : -1.0f;
}

static void ai_run_slide(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 player_eye, float dt)
{
    float step = S->speed * dt;

    (void)committed_side(pl, S, m);

    /* Perpendicular to where it is FACING, not to where the player is. The
       two differ while the monster is still turning, and using the player
       direction would let a monster strafe correctly around a target it has
       not managed to look at yet.
       플레이어 방향이 아니라 *바라보는* 방향의 직각입니다. 아직 도는 중에는 둘이 다르며,
       플레이어 방향을 쓰면 아직 쳐다보지도 못한 대상 주위를 정확히 도는 몬스터가
       됩니다. */
    float side = m->lefty ? (m->yaw + M_PI_F * 0.5f) : (m->yaw - M_PI_F * 0.5f);
    float dx = -sinf(side) * step;
    float dz = -cosf(side) * step;

    float before_x = m->pos.x, before_z = m->pos.z;
    move_toward(pl, l, S, m, player_eye, dx, dz);

    /* Blocked: flip, and let the next frame take the other side. Measured by
       whether it actually moved rather than by asking the level again, so the
       test and the movement can never disagree.
       막혔습니다. 뒤집어서 다음 프레임이 반대쪽을 쓰게 합니다. 레벨에 다시 묻지 않고
       실제로 움직였는지로 판정하므로, 검사와 이동이 어긋날 수 없습니다. */
    float moved = fabsf(m->pos.x - before_x) + fabsf(m->pos.z - before_z);
    if (moved < step * 0.25f)
    {
        m->lefty = (char)!m->lefty;
        m->slide_wait = MON_SLIDE_HOLD(S->speed);
    }
}

/* --- Perception and decision / 지각과 판단 --- */

/**
 * @brief Whether this monster has an unobstructed line to the player's eye.
 *
 * ENGLISH
 * -------
 * @param[in] l          Level whose geometry may block the line.
 * @param[in] m          The monster looking.
 * @param[in] player_eye Where the player's eye is.
 * @return 1 if the line is clear, 0 if anything blocks it.
 *
 * @note LIVE, every time. This is the expensive call; ::sees_player is the
 *       cached wrapper and is what the polling paths use. Call this directly
 *       only where the answer must be current -- ::release_bolt does, because
 *       it exists to catch a target ducking mid-wind-up.
 * @note Traced from the monster's eye, at ::MonType::eye above its feet, not
 *       from its origin. A monster whose feet can see you but whose eyes
 *       cannot is behind cover.
 *
 * 한국어
 * ------
 * @brief 이 몬스터가 플레이어의 눈까지 막힘없는 직선을 가지고 있는가.
 *
 * @param[in] l          그 직선을 막을 수 있는 지형을 가진 레벨.
 * @param[in] m          바라보는 몬스터.
 * @param[in] player_eye 플레이어의 눈 위치.
 * @return 직선이 트여 있으면 1, 무엇이든 막고 있으면 0입니다.
 *
 * @note 호출할 때마다 *실시간*입니다. 이것이 비싼 호출이며, ::sees_player가 캐시를 씌운
 *       껍데기이고 폴링 경로는 그쪽을 씁니다. 답이 반드시 최신이어야 하는 곳에서만 이것을
 *       직접 호출하십시오. ::release_bolt가 그렇게 하며, 예비 동작 도중 대상이 숨는 경우를
 *       잡기 위해 존재하기 때문입니다.
 * @note 원점이 아니라 발에서 ::MonType::eye만큼 위인 몬스터의 눈에서 판정합니다. 발은 당신을
 *       볼 수 있지만 눈은 볼 수 없는 몬스터는 엄폐물 뒤에 있는 것입니다.
 */
static int can_see(const Level *l, const Enemy *m, v3 player_eye)
{
    v3 eye = v3f(m->pos.x, m->pos.y + mon_stats(m->type)->eye, m->pos.z);
    v3 d = v3sub(player_eye, eye);
    float dist = v3len(d);
    if (dist < 0.001f)
        return 1;
    d = v3scale(d, 1.0f / dist);

    /* level_blocked rather than level_trace: this asks only whether the line is
       clear, and level_trace additionally bisects to locate the hit and derives
       a surface normal there -- work this function used to compute and throw
       away on every call. That is once per monster per frame, against a shot
       twice a second, so it was the bulk of what tracing cost the game.

       The old form traced to `dist` and then compared the hit distance back
       against it to tell "hit the player's own position" from "hit a wall in
       between". Tracing SHORT of the player answers the same question without
       the comparison: nothing solid within that range means the line is clear.
       The margin is the marcher's own step, so a wall the trace would have
       found at the very last sample is still counted as blocking.

       level_trace가 아니라 level_blocked를 씁니다. 이 함수는 선이 뚫려 있는지만 묻는데,
       level_trace는 그에 더해 충돌 지점을 찾기 위해 이분 탐색을 하고 그곳의 표면 법선까지
       유도합니다. 이 함수가 매 호출마다 계산하고는 버리던 작업입니다. 그것이 몬스터마다
       매 프레임이고 사격은 초당 두 번이므로, 게임이 광선 판정에 치르던 비용의 대부분이
       이것이었습니다.

       이전 형태는 `dist`까지 판정한 뒤 충돌 거리를 다시 `dist`와 비교하여 "플레이어의
       위치 자체에 맞음"과 "중간의 벽에 맞음"을 구분했습니다. 플레이어에 *못 미치는*
       거리까지 판정하면 그 비교 없이 같은 질문에 답할 수 있습니다. 그 범위 안에 막는
       것이 없다는 것이 곧 선이 뚫려 있다는 뜻입니다. 여유분은 마처 자신의 간격이므로,
       판정이 마지막 샘플에서 발견했을 벽도 여전히 막는 것으로 계산됩니다. */
    return !level_blocked(l, eye, d, dist - 0.05f);
}

/**
 * @brief ::can_see, answered from a cache refreshed every ::SIGHT_PERIOD frames.
 *
 * ENGLISH
 * -------
 * For the POLLING questions only -- "has this monster noticed the player yet",
 * "may this caster plant and start a cast". Both are asked every frame of every
 * monster and neither needs an answer newer than a few frames: a monster
 * reacting to cover 50ms late is not perceptible, and a slight reaction delay
 * reads as the monster registering what happened rather than as lag.
 *
 * @warning NOT for the moment a bolt is released. That check exists precisely
 *          because a target can duck mid-wind-up, and answering it from a
 *          reading taken before the duck is the bug the check was added to
 *          prevent -- a caster shooting through a wall. That site calls
 *          ::can_see directly and always will; see the note there.
 *
 * The refresh is staggered across the pool by ::enemy_spawn_level, which seeds
 * each monster's counter from its index, rather than refreshing every monster
 * on the same frame. A whole-pool refresh costs the same on average and arrives
 * as a spike every ::SIGHT_PERIOD frames, which is the shape of stall that
 * shows up as a stutter rather than as a lower average.
 *
 * `seen` starts at zero, which is "cannot see" -- so a monster spawned this
 * frame stays idle until its first real reading rather than acting on a
 * fabricated one. Being wrong for one refresh interval has to fail toward doing
 * nothing.
 *
 * 한국어
 * ------
 * @brief ::SIGHT_PERIOD 프레임마다 갱신되는 캐시로 답하는 ::can_see입니다.
 *
 * *폴링* 질문 전용입니다. "이 몬스터가 플레이어를 알아챘는가", "이 캐스터가 자리를 잡고
 * 시전을 시작해도 되는가"입니다. 둘 다 모든 몬스터에 대해 매 프레임 묻지만 몇 프레임보다
 * 새로운 답을 필요로 하지 않습니다. 몬스터가 엄폐에 50ms 늦게 반응하는 것은 지각되지
 * 않으며, 약간의 반응 지연은 지연이 아니라 몬스터가 상황을 인지하는 것으로 읽힙니다.
 *
 * @warning 볼트를 *발사하는 순간*에는 사용하면 안 됩니다. 그 검사는 시전 도중에 대상이
 *          숨을 수 있기 때문에 존재하며, 숨기 *이전에* 측정한 값으로 답하는 것이야말로 그
 *          검사가 막으려던 버그입니다. 벽을 관통해 쏘는 캐스터가 됩니다. 그 지점은
 *          ::can_see를 직접 호출하며 앞으로도 그럴 것입니다. 그곳의 주석을 참조하십시오.
 *
 * 갱신은 모든 몬스터를 같은 프레임에 갱신하지 않고 몬스터 인덱스로 분산합니다. 풀 전체를
 * 한꺼번에 갱신하면 평균 비용은 같으면서 ::SIGHT_PERIOD 프레임마다 스파이크로 도착하는데,
 * 이는 평균이 낮아지는 대신 끊김으로 나타나는 형태의 지연입니다.
 *
 * `seen`은 0에서 시작하며 이는 "볼 수 없음"입니다. 따라서 이번 프레임에 생성된 몬스터는
 * 지어낸 값으로 행동하는 대신 첫 실제 측정이 나올 때까지 대기합니다. 한 갱신 주기 동안
 * 틀린다면 아무것도 하지 않는 쪽으로 틀려야 합니다.
 */
static int sees_player(const Pools *pl, const Level *l, Enemy *m, v3 player_eye)
{
    /* BLINDED IS ANSWERED HERE AND NOT CACHED, which is the whole of what
       makes ::PW_SHADOW feel like shadow. The cache is a frame or two stale
       by design -- noticing late is imperceptible -- but a powerup that took
       ::SIGHT_PERIOD to switch off would let a monster fire one more time at
       somebody who had already vanished, and take just as long to notice them
       coming back. So the cached answer is what the eye WOULD see and this is
       whether there is anything to see.
       *가려짐은 이곳에서 답하고 캐시하지 않으며*, 그것이 ::PW_SHADOW를 그림자처럼
       느끼게 하는 전부입니다. 캐시는 설계상 한두 프레임 낡아 있습니다. 늦게
       알아채는 것은 지각되지 않기 때문입니다. 그러나 꺼지는 데 ::SIGHT_PERIOD가
       걸리는 파워업은 이미 사라진 사람에게 몬스터가 한 번 더 쏘게 하고, 돌아온
       것을 알아채는 데도 꼭 그만큼 걸립니다. 그래서 캐시된 답은 눈이 *보게 될*
       것이고, 이것은 볼 것이 *있는가*입니다. */
    if (pl->enemy.blinded) return 0;

    if (m->sight_age <= 0)
    {
        m->sight_age = SIGHT_PERIOD;
        m->seen = (char)can_see(l, m, player_eye);
    }
    return m->seen;
}

/* --- Quake's CheckAttack -------------------------------------------------
 *
 * Whether to start an attack, given how far away the player is. Returns 1 to
 * attack, 0 to keep manoeuvring.
 *
 * The odds come straight from fight.qc, including its halving for a monster
 * that also has a melee attack -- something that can bite prefers to close, so
 * it shoots less on the way in.
 *
 * WHO IS ASKED, AND WHY IT IS NOT `shot_speed`. This line read
 * `S->shot_speed <= 0.0f` and meant "is this a brawler", which is the reading
 * ::MonBehaviour was introduced to abolish -- its own note says three such
 * tests "each meant 'is this a caster'" and would each have to be hunted down
 * the day a third archetype arrived. ::MonType::shot_speed's field comment
 * still promises "Read only by ::AI_CASTER". Neither was true here: this was
 * the fourth case, missed by the sweep rather than hidden from it, and it is
 * the one that would have made a third archetype answer as a brawler by
 * default. The behaviour column is asked directly now, so both notes are true
 * again.
 *
 * *누구에게 묻는가, 그리고 왜 `shot_speed`가 아닌가.* 이 줄은 `S->shot_speed <= 0.0f`
 * 였고 "이것은 근접형인가"를 뜻했습니다. ::MonBehaviour가 없애려고 도입된 바로 그
 * 읽기입니다. 그 주석 자신이 그런 검사 셋이 "각각 '이것은 캐스터인가'를 뜻했다"고,
 * 세 번째 아키타입이 생기는 날 각각을 다시 찾아내야 했을 것이라고 적었습니다.
 * ::MonType::shot_speed의 필드 주석도 여전히 "::AI_CASTER만 읽습니다"라고 약속합니다.
 * 이곳에서는 둘 다 참이 아니었습니다. 이것이 네 번째 사례이며, 숨어 있던 것이 아니라
 * 정리에서 빠진 것이고, 세 번째 아키타입이 기본적으로 근접형으로 답하게 만들었을 바로
 * 그것입니다. 이제 behaviour 열에 직접 물으므로 두 주석이 다시 참입니다.
 *
 * 플레이어와의 거리에 따라 공격을 시작할지 결정합니다. 확률은 fight.qc에서 그대로 왔으며,
 * 근접 공격도 가진 몬스터에 대한 절반 감소도 포함합니다. 물 수 있는 것은 거리를 좁히기를
 * 선호하므로 다가오는 동안 덜 쏩니다. 우리의 `shot_speed > 0`이 이미 "원거리"를 말하는
 * 필드이므로, 두 번째 플래그가 아니라 그것이 여기서도 결정합니다. */
static int pick_attack(Pools *pl, Enemy *m, float dist, float rise)
{
    if (m->attack_wait > 0.0f)
        return -1;

    float chance;
    if (dist <= MON_RANGE_MELEE)
        chance = MON_ODDS_MELEE;
    else if (dist <= MON_RANGE_NEAR)
        chance = MON_ODDS_NEAR;
    else if (dist <= MON_RANGE_MID)
        chance = MON_ODDS_MID;
    else
        return -1;

    /* A melee monster out of its reach cannot attack at all, whatever the dice
       say. The bands are about willingness; this is about arms.
       근접 몬스터는 사거리 밖에서는 주사위와 무관하게 공격할 수 없습니다. 대역은
       의사에 관한 것이고 이것은 팔 길이에 관한 것입니다. */
    /* AN ARM IS NOT A WILLINGNESS, and that is why the two kinds of slot are
       offered on different terms. A swing is offered whenever the player is
       inside its band, because a monster with its fist already back does not
       reconsider; the dice are Quake's answer to "should I shoot from here",
       and a creature at arm's length is past the question. That is exactly
       what the line this replaces said with `behaviour != AI_CASTER` -- the
       difference is that it is now a property of the ATTACK, so a caster's
       swing gets the same certainty a brute's does.
       *팔은 의향이 아니며*, 두 종류의 슬롯이 서로 다른 조건으로 제안되는 이유가 그것입니다.
       휘두르기는 플레이어가 그 대역 안에 있으면 언제나 제안됩니다. 이미 주먹을 뒤로 뺀
       몬스터는 다시 생각하지 않기 때문입니다. 주사위는 "여기서 쏠까"에 대한 Quake의 답이고,
       팔 길이에 있는 생물은 그 질문을 지났습니다. 이것이 대체하는 줄이
       `behaviour != AI_CASTER`로 말하던 것과 정확히 같습니다. 달라진 것은 이것이 이제
       *공격*의 성질이라는 점이며, 캐스터의 휘두르기도 브루트의 것과 같은 확실함을 얻습니다. */
    int n = mon_attack_count(m->type);

    /* QUAKE'S HALVING, WHICH FINALLY HAS SOMETHING TO HALVE. CheckAttack cuts
       the ranged odds of a monster that can also bite, because something which
       prefers to close should shoot less on the way in -- otherwise the melee
       is a bonus rather than a choice, and the creature is simplayer_eye better than
       it was. ::MON_ODDS_ALSO_MELEE has carried that reasoning since the bands
       arrived and nothing could read it: one attack per kind meant nothing
       could both bite and shoot.
       NOT AT ARM'S LENGTH. Quake exempts the melee band and so does this. The
       question there is not "shoot or close" -- the closing is done.
       *Quake의 절반 감소이며, 드디어 반으로 줄일 것이 생겼습니다.* CheckAttack은 물 수도 있는
       몬스터의 원거리 확률을 깎습니다. 붙기를 선호하는 것은 들어오는 동안 덜 쏘아야 하기
       때문입니다. 그러지 않으면 근접은 선택이 아니라 덤이 되고, 그 생물은 그냥 예전보다 나은
       것이 됩니다. ::MON_ODDS_ALSO_MELEE는 대역이 생긴 이래 그 근거를 지녀 왔고 무엇도 그것을
       읽을 수 없었습니다. 종류당 공격 하나는 물면서 쏘는 것이 없다는 뜻이었습니다.
       *팔 길이에서는 아닙니다.* Quake가 근접 대역을 면제하고 이것도 그렇게 합니다. 그곳의
       질문은 "쏠까 붙을까"가 아닙니다. 붙는 일은 이미 끝났습니다. */
    if (dist > MON_RANGE_MELEE)
        for (int k = 0; k < n; k++)
            if (ATTACKS[m->type][k].kind == ATK_SWING) {
                chance *= MON_ODDS_ALSO_MELEE;
                break;
            }

    float total = 0.0f;
    int offered = 0, only = -1;
    unsigned mask = 0;

    for (int k = 0; k < n; k++)
    {
        const MonAttack *A = &ATTACKS[m->type][k];
        if (dist < A->min || dist > A->max) continue;

        /* AN ARM REACHES IN THREE DIMENSIONS, and until a flyer could swing
           nothing had to say so. `dist` is horizontal -- every band in this
           table is a floor plan -- which is right for a bolt, aimed in three
           dimensions once it leaves. It is not right for a reach: a caster
           hovering five metres over the player is 1.5m away on the plan and
           cannot touch them. Grounded monsters are unaffected, which is why
           this went unnoticed while the only things that swung stood on the
           floor.
           *팔은 3차원으로 뻗으며*, 비행체가 휘두를 수 있게 되기 전까지는 그것을 말해야 할
           것이 없었습니다. `dist`는 수평이며 이 표의 모든 대역은 평면도입니다. 볼트에는 옳은
           일입니다. 떠난 뒤 3차원으로 겨냥되기 때문입니다. 닿는 거리에는 옳지 않습니다.
           플레이어 5미터 위에 떠 있는 캐스터는 평면도상 1.5m 떨어져 있고 그를 만질 수
           없습니다. 지상의 몬스터는 영향을 받지 않으며, 휘두르는 것이 바닥에 선 것들뿐인 동안
           이것이 눈에 띄지 않은 이유가 그것입니다. */
        if (A->kind == ATK_SWING && rise > A->max) continue;

        if (A->kind == ATK_BOLT && !(frand(&pl->enemy) < chance)) continue;
        mask |= 1u << k;
        total += A->weight;
        offered++;
        only = k;
    }
    if (!offered) return -1;

    /* A CHOICE AMONG ONE IS NOT A CHOICE, AND IT MUST NOT COST A DRAW.
       ::EnemyPool::rng is one stream shared by the weave, the attack rest and
       the spawners, so a number taken here and not used moves every decision
       downstream of it. That is not a style point: the first cut of this
       function rolled unconditionally, and the whole suite noticed -- two
       brutes converged to 2.397m instead of 1.634m and demotest's golden went
       red, because a monster with exactly one attack was drawing where it never
       used to. A kind that still has one attack must consume exactly what it
       consumed before, or "the slots changed nothing" is a claim with no way to
       be true.
       *하나 중에서 고르는 것은 고르는 것이 아니며, 뽑기를 써서는 안 됩니다.*
       ::EnemyPool::rng는 갈지자와 공격 휴식과 스포너가 함께 쓰는 하나의 스트림이므로, 이곳에서
       뽑고 쓰지 않은 수는 그 아래의 모든 결정을 밀어냅니다. 취향의 문제가 아닙니다. 이 함수의
       첫 판은 무조건 굴렸고 스위트 전체가 알아챘습니다. 브루트 둘이 1.634m 대신 2.397m까지만
       모였고 demotest의 골든이 빨개졌습니다. 공격이 정확히 하나인 몬스터가 예전에는 뽑지 않던
       자리에서 뽑았기 때문입니다. 여전히 공격이 하나인 종류는 예전과 정확히 같은 만큼을 써야
       하며, 그러지 않으면 "슬롯은 아무것도 바꾸지 않았다"는 참일 방법이 없는 주장입니다. */
    if (offered == 1) return only;

    /* ONE DRAW ACROSS THE OFFERED WEIGHTS, and over the ones the band and the
       dice already agreed to -- `mask` is why the loop is not simplayer_eye run again.
       Walking the slots a second time would re-roll every ::ATK_BOLT chance and
       could offer a slot the first pass refused.
       *제안된 가중치들에 대해 한 번 뽑으며*, 대역과 주사위가 이미 동의한 것들에 대해서입니다.
       `mask`가 반복문을 그냥 다시 돌리지 않는 이유입니다. 슬롯을 두 번째로 걸으면 모든
       ::ATK_BOLT 확률을 다시 굴리게 되고, 첫 번째 통과가 거절한 슬롯을 제안할 수 있습니다. */
    float roll = frand(&pl->enemy) * total;
    for (int k = 0; k < n; k++)
    {
        if (!(mask & (1u << k))) continue;
        roll -= ATTACKS[m->type][k].weight;
        if (roll <= 0.0f) return k;
    }
    return only;
}

/**
 * @brief Starts an attack, and decides how long it will be.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl Pools, for ::EnemyPool::rng.
 * @param[in]     S  The monster's type, for the volley bounds.
 * @param[in,out] m  The monster. State, timer, shot count and length change.
 *
 * ROLLED ONCE, HERE, because a volley whose length is re-decided every frame
 * has no length. ::Enemy::volley_n is the roll and ::Enemy::swung counts against
 * it; between them ::E_ATTACK knows both how far along the stream is and when it
 * is finished.
 *
 * @note ONE FUNCTION FOR BOTH ARCHETYPES, and it is why this exists at all --
 *       the three lines it replaces sat in ::chase_brawler and ::chase_caster
 *       as two copies, and a fourth field to set at the start of an attack
 *       would have had to be remembered in both. A brawler rolls a length of
 *       one because ::MonType::burst_min and ::MonType::burst are both 1 on its
 *       row, not because this function knows what a brawler is.
 * @note The brawler's re-swing inside ::E_ATTACK deliberately does NOT come
 *       through here. It resets the clock without re-rolling, which is correct
 *       for a swing and unreachable for a caster -- a caster always returns to
 *       ::E_CHASE and so always arrives back through this function.
 *
 * 한국어
 * ------
 * @brief 공격을 시작하며, 그것이 얼마나 길지를 정합니다.
 *
 * *이곳에서 한 번 굴립니다.* 매 프레임 길이를 다시 정하는 일제 사격에는 길이가 없기
 * 때문입니다. ::Enemy::volley_n이 그 굴림이고 ::Enemy::swung이 그것에 대해 셉니다. 둘 사이에서
 * ::E_ATTACK은 줄기가 얼마나 진행되었는지와 언제 끝나는지를 모두 압니다.
 *
 * @note *두 아키타입에 대해 하나의 함수이며*, 이것이 존재하는 이유가 바로 그것입니다. 이것이
 *       대체하는 세 줄은 ::chase_brawler와 ::chase_caster에 사본 둘로 있었고, 공격 시작에
 *       설정할 네 번째 필드는 양쪽 모두에서 기억되어야 했을 것입니다. 근접형이 길이 1을
 *       굴리는 것은 자기 행의 ::MonType::burst_min과 ::MonType::burst가 둘 다 1이기
 *       때문이지, 이 함수가 근접형이 무엇인지 알기 때문이 아닙니다.
 * @note ::E_ATTACK 안의 근접형 재휘두르기는 의도적으로 이곳을 지나지 *않습니다.* 다시 굴리지
 *       않고 시계만 되돌리며, 그것은 휘두르기에 대해 옳고 캐스터에게는 도달 불가능합니다.
 *       캐스터는 언제나 ::E_CHASE로 돌아가므로 언제나 이 함수를 통해 되돌아옵니다.
 */
static void begin_attack(Pools *pl, const MonAttack *A, Enemy *m)
{
    int lo = A->burst_min > 0 ? A->burst_min : 1;
    int hi = A->burst     > lo ? A->burst     : lo;

    m->state    = E_ATTACK;
    m->timer    = 0.0f;
    m->swung    = 0;
    m->volley_n = (short)(lo + (int)(frand(&pl->enemy) * (float)(hi - lo + 1)));

    /* frand can return 1.0 on some inputs, and that would land one past the
       top of the range -- a volley one bolt longer than any row asked for,
       once in a very long while, which is exactly the kind of bug that is
       never reproduced.
       frand는 입력에 따라 1.0을 돌려줄 수 있고 그것은 범위의 꼭대기를 한 칸 넘어섭니다. 어떤
       행도 요청하지 않은 길이의 일제 사격이 아주 가끔 나오는 것이며, 그것은 결코 재현되지 않는
       종류의 버그입니다. */
    if (m->volley_n > (short)hi) m->volley_n = (short)hi;
}

/* --- Archetypes / 아키타입 --- */
/**
 * @brief A brawler chasing: close to arm's length, then swing or reposition.
 *
 * ENGLISH
 * -------
 * @param[in]     l    The level, for the walk.
 * @param[in]     S    This monster's type.
 * @param[in,out] m    The monster.
 * @param[in]     to   Vector from the monster's eye to the player's.
 * @param[in]     dist Horizontal distance to the player, metres.
 * @param[in]     dt   Timestep in seconds.
 *
 * 한국어
 * ------
 * @brief 추격 중인 근접형입니다. 팔 길이까지 붙은 뒤 휘두르거나 자리를 바꿉니다.
 * @param[in]     l    이동에 사용할 레벨.
 * @param[in]     S    이 몬스터의 종류.
 * @param[in,out] m    몬스터.
 * @param[in]     to   몬스터의 눈에서 플레이어의 눈으로 향하는 벡터.
 * @param[in]     dist 플레이어까지의 수평 거리 (미터).
 * @param[in]     dt   시간 간격 (초).
 */
/* THE APPROACH, TURNED ASIDE. ::MonType::weave radians toward the side this
   monster is committed to, so closing is a zig-zag rather than a line and the
   player cannot get away by backing along one axis.
   ROTATING the vector rather than adding a sideways step on top of it: an
   added step makes the monster travel faster than ::MonType::speed and arrive
   early from an angle nobody budgeted for. A rotation spends the same metres
   per second and spends them elsewhere. What it costs in closing rate is
   `cos(weave)`, and the speed column is set knowing that.
   *접근을 옆으로 틉니다.* 이 몬스터가 정해 둔 쪽으로 ::MonType::weave 라디안만큼 틀어서,
   다가오는 길이 직선이 아니라 갈지자가 되고, 플레이어가 한 축으로 물러나는 것만으로는
   벗어날 수 없게 합니다.
   옆걸음을 *더하는* 것이 아니라 벡터를 *회전*시킵니다. 더하면 몬스터가 ::MonType::speed보다
   빠르게 이동해, 아무도 예산에 넣지 않은 각도에서 일찍 도착합니다. 회전은 초당 같은 미터를
   쓰고 다만 다른 곳에 씁니다. 접근 속도로 치르는 값은 `cos(weave)`이며, 속도 열은 그것을
   알고 정해져 있습니다. */
static void move_weaving(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 player_eye,
                         float ux, float uz, float step)
{
    if (S->weave > 0.0f)
    {
        float a = S->weave * committed_side(pl, S, m);
        float c = cosf(a), sn = sinf(a);
        float rx = ux * c - uz * sn;
        float rz = ux * sn + uz * c;
        ux = rx; uz = rz;
    }
    move_toward(pl, l, S, m, player_eye, ux * step, uz * step);
}

static void chase_brawler(Pools *pl, const Level *l, const MonType *S, Enemy *m,
                          v3 to, float dist, v3 player_eye, float dt)
{
    float inv = dist > 0.001f ? 1.0f / dist : 0.0f;
    float step = S->speed * dt;
    float band = mon_band(m->type);

    if (dist > band)
    {
        move_weaving(pl, l, S, m, player_eye, to.x * inv, to.z * inv, step);
        return;
    }

    /* Within reach. The roll here is Quake's 0.9 at melee range -- high enough
       that closing is still lethal, low enough that a monster occasionally
       repositions instead of grinding out its swing timer nose-to-nose.
       사거리 안입니다. 여기의 굴림은 근접 대역에서 Quake의 0.9입니다. 거리를 좁히는 것이
       여전히 치명적일 만큼 높고, 몬스터가 코앞에서 공격 타이머만 돌리는 대신 이따금 자리를
       바꿀 만큼 낮습니다. */
    int slot = pick_attack(pl, m, dist, fabsf(to.y));
    if (slot >= 0)
    {
        m->atk = (short)slot;
        begin_attack(pl, mon_attack(m->type, slot), m);
    }
    else
    {
        ai_run_slide(pl, l, S, m, player_eye, dt);
    }
}

/**
 * @brief A caster chasing: hold a band of distance, and only sometimes fire.
 *
 * ENGLISH
 * -------
 * @param[in]     l    The level, for the walk and the sight test.
 * @param[in]     S    This monster's type.
 * @param[in,out] m    The monster.
 * @param[in]     to   Vector from the monster's eye to the player's.
 * @param[in]     dist Horizontal distance to the player, metres.
 * @param[in]     dt   Timestep in seconds.
 *
 * 한국어
 * ------
 * @brief 추격 중인 캐스터입니다. 일정 거리 대역을 유지하며 가끔만 발사합니다.
 * @param[in]     l    이동과 시야 판정에 사용할 레벨.
 * @param[in]     S    이 몬스터의 종류.
 * @param[in,out] m    몬스터.
 * @param[in]     to   몬스터의 눈에서 플레이어의 눈으로 향하는 벡터.
 * @param[in]     dist 플레이어까지의 수평 거리 (미터).
 * @param[in]     dt   시간 간격 (초).
 */
static void chase_caster(Pools *pl, const Level *l, const MonType *S, Enemy *m,
                         v3 to, float dist, v3 player_eye, float dt)
{
    float inv = dist > 0.001f ? 1.0f / dist : 0.0f;
    float step = S->speed * dt;
    float band = mon_band(m->type);

    if (dist > band)
    {
        move_weaving(pl, l, S, m, player_eye, to.x * inv, to.z * inv, step);
        return;
    }
    if (dist < band * CASTER_KEEP)
    {
        /* CORNERED, IT ASKS BEFORE IT BACKS AWAY. The retreat has no floor: a
           player who simply kept touching a caster walked it into a wall and
           killed it at leisure, because this branch returned before anything
           could be offered. It asks now, and the only thing the table offers
           this close is the swing -- the bolt's band starts where this retreat
           ends, so nothing here makes a caster stand and shoot at point-blank.
           *궁지에 몰리면 물러나기 전에 묻습니다.* 물러남에는 바닥이 없었습니다. 계속 붙어
           있기만 한 플레이어는 캐스터를 벽까지 몰아 느긋하게 죽였습니다. 이 분기가 무엇이든
           제안되기 전에 돌아갔기 때문입니다. 이제 묻습니다. 이렇게 가까운 곳에서 표가 내놓는
           것은 휘두르기뿐입니다. 볼트의 대역은 이 물러남이 끝나는 곳에서 시작하므로, 이곳의
           무엇도 캐스터를 코앞에서 서서 쏘게 만들지 않습니다. */
        int slot = pick_attack(pl, m, dist, fabsf(to.y));
        if (slot >= 0)
        {
            m->atk = (short)slot;
            begin_attack(pl, mon_attack(m->type, slot), m);
            return;
        }
        move_toward(pl, l, S, m, player_eye, -to.x * inv * step, -to.z * inv * step);
        return;
    }

    /* Cached: this decides whether to PLANT and begin a wind-up, and the
       wind-up is long enough that a frame or two of staleness cannot matter --
       ::release_bolt checks again, live, and that is the check that actually
       guards the wall.
       캐시를 씁니다. 이것은 자리를 잡고 시전을 *시작할지*를 결정하며, 시전 시간이 충분히
       길어 한두 프레임의 지연은 문제가 될 수 없습니다. ::release_bolt가 실시간으로 다시
       검사하며, 벽을 실제로 지키는 것은 그 검사입니다. */
    if (!sees_player(pl, l, m, player_eye))
    {
        move_toward(pl, l, S, m, player_eye, to.x * inv * step, to.z * inv * step);
        return;
    }

    /* In its preferred band and looking right at the player -- and it still
       only sometimes shoots. The rest of the time it circles, which is what
       turns a caster from a turret into something you have to chase around a
       room.
       선호하는 대역 안에서 플레이어를 정면으로 보고 있으면서도, 여전히 *가끔만* 쏩니다.
       나머지 시간에는 원을 그립니다. 그것이 캐스터를 포탑에서 방 안을 쫓아다녀야 하는
       무언가로 바꿉니다. */
    int slot = pick_attack(pl, m, dist, fabsf(to.y));
    if (slot >= 0)
    {
        m->atk = (short)slot;
        begin_attack(pl, mon_attack(m->type, slot), m);
    }
    else
    {
        ai_run_slide(pl, l, S, m, player_eye, dt);
    }
}

/**
 * @brief A brawler's swing lands, if the player is still inside it.
 *
 * ENGLISH
 * -------
 * @param[in]     S    This monster's type.
 * @param[in,out] m    The monster, for the sound's position.
 * @param[in]     dist Horizontal distance to the player, metres.
 * @return Damage to deal to the player. 0 if they got clear in time.
 * @note Returned rather than applied, for the reason ::enemy_update returns a
 *       total rather than subtracting one: this module does not own the
 *       player's health, and one place noticing an emptied bar is what stops a
 *       new damage source forgetting to kill them.
 *
 * 한국어
 * ------
 * @brief 근접형의 공격이 적중합니다. 플레이어가 아직 그 안에 있다면 말입니다.
 * @param[in]     S    이 몬스터의 종류.
 * @param[in,out] m    몬스터. 소리의 위치에 사용합니다.
 * @param[in]     dist 플레이어까지의 수평 거리 (미터).
 * @return 플레이어에게 줄 피해량. 제때 벗어났으면 0입니다.
 * @note 적용하지 않고 반환하는 이유는 ::enemy_update가 차감하지 않고 합계를 반환하는 것과
 *       같습니다. 이 모듈은 플레이어의 체력을 소유하지 않으며, 비워진 체력을 한 곳에서
 *       감지하는 것이 새로운 피해원이 플레이어를 죽이는 것을 잊지 않게 합니다.
 */
static int release_swing(const MonAttack *A, Enemy *m, float dist)
{
    if (dist > A->max + 0.3f)
        return 0;
    play_at(m->pos, "eatt", 90);
    return A->damage;
}

/**
 * @brief A caster's bolt leaves, if the shot is still there to take.
 *
 * ENGLISH
 * -------
 * @param[in]     l          The level, for the sight test.
 * @param[in]     S          This monster's type.
 * @param[in,out] m          The monster.
 * @param[in]     player_eye Where to aim.
 *
 * @warning The visibility test here is NOT cached and must never be. It exists
 *          because a target can duck DURING the wind-up: answering from a
 *          reading taken up to SIGHT_PERIOD frames ago is answering from before
 *          the duck, which is a caster shooting through a wall -- the exact bug
 *          this second check was added to prevent. Once per released bolt is
 *          also a rate the trace can afford; it is the per-frame polling in
 *          ::chase_caster that could not.
 *
 * 한국어
 * ------
 * @brief 캐스터의 볼트가 발사됩니다. 쏠 수 있는 상황이 아직 유지된다면 말입니다.
 * @param[in]     l          시야 판정에 사용할 레벨.
 * @param[in]     S          이 몬스터의 종류.
 * @param[in,out] m          몬스터.
 * @param[in]     player_eye 조준 대상.
 *
 * @warning 이곳의 가시성 검사는 캐시를 쓰지 *않으며* 앞으로도 써서는 안 됩니다. 이 검사는
 *          시전 *도중에* 대상이 숨을 수 있기 때문에 존재합니다. 최대 SIGHT_PERIOD 프레임 전에
 *          측정한 값으로 답하는 것은 숨기 이전의 상황으로 답하는 것이며, 그것은 벽을 관통해
 *          쏘는 캐스터입니다. 이 두 번째 검사가 추가된 이유가 바로 그 버그입니다. 또한 발사된
 *          볼트당 한 번이라면 판정 비용을 감당할 수 있습니다. 감당할 수 없었던 것은
 *          ::chase_caster의 매 프레임 폴링입니다.
 */
static void release_bolt(Pools *pl, const Level *l, const MonType *S,
                         const MonAttack *A, Enemy *m, v3 player_eye)
{
    /* CHECKED PER BOLT, NOT PER VOLLEY, and a blocked one is spent rather than
       postponed. ::Enemy::swung advances in ::enemy_update whether this fires
       or not, so ducking behind a pillar mid-stream costs the monster the rest
       of its volley instead of pausing it -- the reward for taking cover is the
       bolts that never come, and a stream that waited for you would make cover
       a way of storing them up.
       *일제 사격당이 아니라 볼트당 검사하며*, 막힌 볼트는 미뤄지지 않고 소비됩니다.
       ::Enemy::swung은 이것이 발사되든 아니든 ::enemy_update에서 진행하므로, 줄기 도중에 기둥
       뒤로 숨는 것은 일제 사격을 멈추는 것이 아니라 그 나머지를 몬스터에게서 빼앗습니다.
       엄폐의 보상은 오지 않은 볼트들이며, 기다려 주는 줄기는 엄폐를 볼트를 모아 두는 방법으로
       만들었을 것입니다. */
    if (!can_see(l, m, player_eye))
        return;

    v3 from = v3f(m->pos.x, m->pos.y + S->eye, m->pos.z);

    /* THE FIRST BOLT TAKES THE AIM AND THE REST INHERIT IT. ::Enemy::volley_at
       carries the whole argument for why a stream must not track; what is here
       is only the moment it is taken, which is the first RELEASE and not the
       start of the wind-up. Taking it at the wind-up would aim
       ::MonType::windup seconds behind the player, a lead nobody chose.
       *첫 볼트가 조준을 취하고 나머지가 물려받습니다.* 줄기가 추적해서는 안 되는 이유 전체는
       ::Enemy::volley_at에 있고, 이곳에 있는 것은 그것을 취하는 *시점*뿐입니다. 준비 동작의
       시작이 아니라 첫 *발사*입니다. 준비 동작에서 취하면 플레이어보다 ::MonType::windup
       초만큼 뒤를 겨누게 되는데, 그것은 아무도 고르지 않은 리드입니다. */
    if (m->swung == 0)
        m->volley_at = v3f(player_eye.x,
                           player_eye.y - PLAYER_EYE * 0.35f,
                           player_eye.z);

    v3 at = m->volley_at;

    /* A basis across the line of fire, built per bolt because the monster has
       moved since the last one: the scatter has to be perpendicular to THIS
       shot, and a cone computed from world axes would be wider sideways than
       vertically for a monster standing beside you and the other way round for
       one in front.
       사격선을 가로지르는 기저이며, 지난 볼트 이후 몬스터가 움직였으므로 볼트마다 만듭니다.
       흩어짐은 *이번* 사격에 수직이어야 하고, 월드 축에서 계산한 원뿔은 옆에 선 몬스터에게는
       세로보다 가로로 넓고 앞에 선 몬스터에게는 그 반대가 됩니다. */
    v3 d = v3sub(at, from);
    float dist = v3len(d);
    v3 fwd = dist > 1e-4f ? v3scale(d, 1.0f / dist) : v3f(0, 0, 1);
    v3 hint = (fwd.y > 0.9f || fwd.y < -0.9f) ? v3f(1, 0, 0) : v3f(0, 1, 0);
    v3 right = v3norm(v3cross(hint, fwd));
    v3 up    = v3cross(fwd, right);

    v3 aim = at;

    /* THE FIRST BOLT IS THE AIMED ONE. A volley whose every shot is scattered
       is a monster that can miss entirely from four metres, which reads as a
       bug rather than as a spray -- and the player has no way to tell a wide
       cone from bad aim. One true shot at the head of the stream says "this is
       where it meant to hit" and the rest say how much room there is around it.
       *첫 볼트는 조준된 것입니다.* 모든 발이 흩어지는 일제 사격은 4미터에서 전부 빗맞힐 수
       있는 몬스터이며, 그것은 난사가 아니라 버그로 읽힙니다. 그리고 플레이어에게는 넓은 원뿔과
       나쁜 조준을 구별할 방법이 없습니다. 줄기의 머리에 놓인 정확한 한 발이 "여기를 맞히려
       했다"고 말하고, 나머지가 그 둘레에 얼마나 여유가 있는지를 말합니다. */
    if (m->swung > 0 && A->spread > 0.0f) {
        float rx = (frand(&pl->enemy) * 2.0f - 1.0f) * A->spread * dist;
        float ry = (frand(&pl->enemy) * 2.0f - 1.0f) * A->spread * dist;
        aim = v3add(aim, v3add(v3scale(right, rx), v3scale(up, ry)));
    }
    shot_fire(pl, from, aim, A->shot_speed, A->damage, m->type,
              (int)(m - pl->enemy.m));

    /* ONCE PER VOLLEY, not once per bolt, and that is what it already was: the
       call sat after the loop when the five left together. Ten of these inside
       0.7s would be a sound effect rather than a monster, and the mixer has a
       voice table a stream would spend entirely on itself.
       *일제 사격당 한 번이지 볼트당 한 번이 아니며*, 그것은 이미 그러했습니다. 다섯이 함께
       떠나던 시절 이 호출은 반복문 뒤에 있었습니다. 0.7초 안에 이것을 열 번 내는 것은 몬스터가
       아니라 효과음이며, 믹서에는 줄기가 자기 자신에게 다 써 버릴 보이스 표가 있습니다. */
    if (m->swung == 0)
        play_at(m->pos, "ecast", 90);
}
