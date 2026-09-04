/**
 * @file enemy.c
 * @brief Implements the monster AI, its collision, and the shots it fires.
 *
 * Contains no GL. ::MonType::behaviour selects the archetype (::chase_brawler or ::chase_caster);
 * moving, turning, sight and damage are shared. All state lives in the caller's ::Pools; the only
 * module data is the const ::TYPES table. Sight traces are cached per monster for ::SIGHT_PERIOD frames.
 *
 * GL을 쓰지 않습니다. ::MonType::behaviour가 아키타입(::chase_brawler 또는 ::chase_caster)을 고르며,
 * 이동·회전·시야·피격은 공유됩니다. 모든 상태는 호출자의 ::Pools에 있고, 모듈 데이터는 const ::TYPES
 * 표뿐입니다. 시야 판정은 몬스터마다 캐시되어 ::SIGHT_PERIOD 프레임 동안 유지됩니다.
 */

#include "enemy.h"
#include <math.h>
#include "pools.h"
#include "audio.h"
#include "fx.h"
/* The drop tables and the pseudo-kind this module does not resolve; see ::Enemy::drop.
   드롭 표와, 이 모듈이 해석하지 않는 의사 종류입니다. ::Enemy::drop을 참조하십시오. */
#include "loot.h"
#include "diag.h"
#include "player.h" /* PLAYER_EYE / PLAYER_RADIUS: the projectile hit box; PLAYER_STEP / PLAYER_GRAVITY: stairs and falling
                       PLAYER_EYE / PLAYER_RADIUS: 발사체 히트 박스, PLAYER_STEP / PLAYER_GRAVITY: 계단과 낙하 */

/* --- File-local macros / 파일 지역 매크로 --- */
/* The seed a pool starts from; a zeroed EnemyPool means "not seeded yet".
   풀이 출발하는 씨앗이며, 0인 EnemyPool은 "아직 씨앗이 채워지지 않음"을 뜻합니다. */
#define ENEMY_RNG_SEED 0x9e3779b9u

/* Kind names that no longer have a TYPES row, mapped to the row that now takes them:
   `spawn` and `hound` become MON_WATER_SPIRIT, `imp` and `wraith` become MON_CASTER.
   ::mon_type_for consults this after the TYPES names, so a map using a retired name still spawns a monster.
   더 이상 TYPES 행이 없는 종류 이름과, 이제 그것을 받는 행입니다. `spawn`과 `hound`는
   MON_WATER_SPIRIT, `imp`와 `wraith`는 MON_CASTER가 됩니다. ::mon_type_for가 TYPES 이름 다음에
   이 표를 보므로, 은퇴한 이름을 쓴 맵도 여전히 몬스터를 생성합니다. */
static const struct
{
    const char *was;
    int now;
} MON_LEGACY[] = {
    {"spawn", MON_WATER_SPIRIT},
    {"imp", MON_CASTER},
    {"hound", MON_WATER_SPIRIT},
    {"wraith", MON_CASTER},
};

/**
 * @brief How close a caster will let the player get before it backs away.
 *
 * A fraction of the caster's attack band (slot 0's max), not an absolute distance;
 * ::chase_caster retreats below `band * CASTER_KEEP`.
 * 절대 거리가 아니라 캐스터 공격 대역(슬롯 0의 max)에 대한 비율입니다. ::chase_caster는
 * `band * CASTER_KEEP` 아래에서 물러납니다.
 */
#define CASTER_KEEP 0.55f

/* --- Static variable definitions / 정적 변수 정의 -----------------------------
 *
 * One const table, ::TYPES. Monsters, their count, projectiles and RNG state live in the
 * caller's ::EnemyPool; every helper takes the player's eye as an argument.
 *
 * const 표 ::TYPES 하나뿐입니다. 몬스터, 개수, 발사체, 난수 상태는 호출자의 ::EnemyPool에
 * 있으며, 모든 헬퍼는 플레이어의 눈 위치를 인자로 받습니다.
 */

/**
 * @brief The bestiary: one row of stats per monster kind, indexed by ::MonTypeID.
 *
 * The same index is the creature's row in the sprite atlas. Columns are positional and named
 * by the header line below; each column's meaning is in ::MonType. To resize a kind, change
 * `hgt` and scale `rad` and `eye` by the same factor. A ::MON_BOSS row's `hp` must divide by
 * ::BOSS_CYCLES, which ::types_check enforces.
 *
 * ::MonTypeID로 인덱싱하며, 같은 인덱스가 스프라이트 아틀라스에서 그 생물의 행입니다. 열은
 * 위치로 정해지고 아래 표제 줄이 이름을 붙이며, 각 열의 의미는 ::MonType에 있습니다. 크기를
 * 바꾸려면 `hgt`를 고치고 `rad`와 `eye`를 같은 배율로 조정하십시오. ::MON_BOSS 행의 `hp`는
 * ::BOSS_CYCLES로 나누어떨어져야 하며 ::types_check가 검사합니다.
 *
 * @note Read through ::mon_stats rather than indexing directly, so an id outside the enum lands
 *       on a row that exists. / 직접 인덱싱하지 말고 ::mon_stats를 통해 읽으십시오.
 */
static const MonType TYPES[MON_TYPES] = {
    /* The baseline: fastest speed and loosest weave in the table; floats. Twice the size it
       was: height, radius and eye doubled together, which is the whole of a resize. `aspect` is
       the 64x96 sprite's own ratio and is NOT a size -- see the size block in enemy.h.
       기준 종류. 표에서 가장 빠르고 갈지자가 가장 크며 떠 있습니다. 예전의 두 배 크기이며,
       height와 radius와 eye를 함께 두 배로 했습니다. 그것이 크기 조정의 전부입니다. `aspect`는
       64x96 스프라이트 자신의 비율이고 크기가 *아닙니다*. enemy.h의 치수 블록을 참조하십시오. */
    {"water_spirit", /* name       entity name a level places it by / 레벨이 배치할 때 쓰는 이름 */
     AI_CASTER,      /* behaviour  AI_CASTER holds range, AI_BRAWLER closes / 사거리 유지 · 접근 */
     60,             /* hp         starting health / 시작 체력 */
     7.0f,           /* speed      walking, m/s / 이동 속도 */
     0.62f,          /* weave      how far off a straight line it closes, rad / 갈지자 폭 */
     1.04f,          /* radius     collision and hitscan, m / 충돌·히트스캔 반경 */
     3.40f,          /* height     standing, and what is drawn, m / 신장이자 그려지는 높이 */
     2.60f,          /* eye        above the feet, m -- looks and shoots from here / 시선 높이 */
     34.0f,          /* sight      first notices the player at, m / 인지 거리 */
     0.70f,          /* aspect     sprite width / height -- the 64x96 art's own ratio / 스프라이트 가로세로 비율 */
     260.0f,         /* yaw_speed  turning, deg/s / 회전 속도 */
     0.6f,           /* pain_lock  flinch immunity after a hit, s / 피격 경직 면역 시간 */
     12,             /* cap        alive at once; 0 is no limit / 동시 생존 상한 */
     MON_FLOATS},    /* flags      MON_* bits; 0 is an ordinary monster / 플래그 */

    /* Heavy melee: high hp, MON_UNFLINCHING so it cannot be pain-locked, smallest weave. Walks slower
       than the player (10.8) and only closes the gap inside slot 1's charge -- see ATTACKS.
       무거운 근접형. 높은 hp, MON_UNFLINCHING이라 경직 잠금에 걸리지 않으며 갈지자가 가장 작습니다.
       플레이어(10.8)보다 느리게 걷고, 간격은 슬롯 1의 돌진 안에서만 좁힙니다. ATTACKS를 참조하십시오. */
    {"brute",          /* name */
     AI_BRAWLER,       /* behaviour  closes to its band / 대역까지 접근 */
     200,              /* hp */
     4.0f,             /* speed      m/s -- slower than the player / 플레이어보다 느림 */
     0.30f,            /* weave      rad -- smallest in the table / 표에서 가장 작음 */
     0.806f,           /* radius     m */
     2.35f,            /* height     m */
     1.80f,            /* eye        m */
     34.0f,            /* sight      m */
     1.2f,             /* aspect */
     130.0f,           /* yaw_speed  deg/s */
     2.5f,             /* pain_lock  s -- MON_UNFLINCHING never flinches anyway / 어차피 경직 없음 */
     5,                /* cap */
     MON_UNFLINCHING}, /* flags */

    /* Holds its range instead of closing, and does so in the air: MON_FLIES | MON_FLOATS.
       접근하지 않고 사거리를 지키며, 공중에서 그렇게 합니다. MON_FLIES | MON_FLOATS. */
    {"caster",                /* name */
     AI_CASTER,               /* behaviour  holds its range / 사거리 유지 */
     90,                      /* hp */
     5.8f,                    /* speed      m/s */
     0.46f,                   /* weave      rad */
     0.546f,                  /* radius     m */
     1.90f,                   /* height     m */
     1.45f,                   /* eye        m */
     40.0f,                   /* sight      m -- reaches across the arena / 아레나를 가로지름 */
     1.20f,                   /* aspect */
     180.0f,                  /* yaw_speed  deg/s */
     1.2f,                    /* pain_lock  s */
     7,                       /* cap */
     MON_FLIES | MON_FLOATS}, /* flags      flies, so it keeps its placed height / 비행. 배치된 높이 유지 */

    /* The boss: a caster that cannot move (MON_ANCHORED). Height leaves room under the arena
       ceiling for the wards above it; rad and eye are scaled with it.
       보스. 움직이지 못하는 캐스터입니다(MON_ANCHORED). 높이는 천장 아래 결계핵 자리를 남기며
       rad와 eye를 함께 줄였습니다. */
    {"maw",                                    /* name */
     AI_CASTER,                                /* behaviour */
     990,                                      /* hp         must divide by BOSS_CYCLES -- types_check enforces it / BOSS_CYCLES로 나누어떨어져야 함 */
     0.0f,                                     /* speed      anchored, so it never walks / 고정. 걷지 않음 */
     0.0f,                                     /* weave */
     1.20f,                                    /* radius     m */
     3.60f,                                    /* height     m -- leaves room for the wards above / 위쪽 결계핵 자리를 남김 */
     2.00f,                                    /* eye        m */
     60.0f,                                    /* sight      m */
     3.00f,                                    /* aspect */
     90.0f,                                    /* yaw_speed  deg/s */
     99.0f,                                    /* pain_lock  s -- effectively never flinches / 사실상 경직 없음 */
     1,                                        /* cap */
     MON_BOSS | MON_ANCHORED | MON_COLLAPSES}, /* flags */

    /* Guards the boss. Pays out one summon per WARD_SUMMON_DMG of damage taken, so hp sets how
       many. rad is deliberately wider than the drawn pillar; sight 40 reaches across the arena.
       보스를 지킵니다. 받은 피해 WARD_SUMMON_DMG마다 한 번 지급하므로 hp가 횟수를 정합니다.
       rad는 그려진 기둥보다 의도적으로 넓고, sight 40은 투기장을 가로지릅니다. */
    {"ward",                                    /* name */
     AI_CASTER,                                 /* behaviour */
     200,                                       /* hp         WARD_SUMMON_DMG x 13 summons / 소환 13회분 */
     0.0f,                                      /* speed      anchored / 고정 */
     0.0f,                                      /* weave */
     0.50f,                                     /* radius     m -- wider than the drawn pillar / 그려진 기둥보다 넓음 */
     4.00f,                                     /* height     m */
     2.70f,                                     /* eye        m -- fires from the gem / 보석에서 발사 */
     40.0f,                                     /* sight      m */
     0.5f,                                      /* aspect     a narrow pillar / 좁은 기둥 */
     180.0f,                                    /* yaw_speed  deg/s */
     99.0f,                                     /* pain_lock  s */
     0,                                         /* cap        no limit -- the fight places them / 제한 없음. 전투가 배치 */
     MON_GUARD | MON_ANCHORED | MON_COLLAPSES}, /* flags */
};

/* What each kind can do, one row per attack, indexed by ::MonTypeID.
 * A kind's slots run to the first ::ATK_NONE; the rest of each row is zeroed by the
 * designated initialiser. Each column's meaning is in ::MonAttack.
 *
 * DAMAGE IS PER BOLT, not per attack. What a volley takes off is `damage * burst` -- the water
 * spirit's 2 x 5..10 is 10..20 a cast, and the maw's 14 x 5 is 70. Reading the column alone
 * understates every multi-bolt slot in the table.
 *
 * 종류마다 할 수 있는 공격이며 공격 하나에 한 행입니다. 슬롯은 첫 ::ATK_NONE까지이고,
 * 각 행의 나머지는 지정 초기화가 0으로 채웁니다. 각 열의 의미는 ::MonAttack에 있습니다.
 * *피해는 공격 한 번이 아니라 볼트 하나의 값입니다.* 일제 사격이 깎는 양은 `damage * burst`이며,
 * 물 정령의 2 x 5~10은 한 번에 10~20이고 아귀의 14 x 5는 70입니다. 열만 읽으면 표의 모든
 * 다발 슬롯을 과소평가하게 됩니다. */

/* The maw's three patterns, named so the ward's row can carry one verbatim. Same speed and
   range; only the shape of the fire differs.
   아귀의 세 가지 탄막이며, 결계핵의 행이 그중 하나를 그대로 싣습니다. 속도와 사거리는 같고
   사격의 형태만 다릅니다. */

/* Five at once, wide: a wall to dodge through. / 넓게 다섯 발 동시. 뚫고 지나갈 벽. */
#define MAW_PATTERN_VOLLEY                                                                        \
    {ATK_BOLT, /* kind       ATK_BOLT fires a projectile, ATK_SWING is melee / 발사체 · 근접 */   \
     0.0f,     /* min        offered from this distance, m / 이 거리부터 제안 */                  \
     40.0f,    /* max        and up to this one, m / 이 거리까지 */                               \
     40.0f,    /* reach      how far it CONNECTS, m / 실제로 닿는 거리 */                         \
     0.0f,     /* close      walking speed kept through the windup / 준비동작 중 유지하는 속도 */ \
     14,       /* damage     PER BOLT -- see the note above / 볼트 *하나*의 피해. 위 주석 참조 */ \
     0.90f,    /* windup     telegraph before it lands, s / 예비 동작 시간 */                     \
     1.10f,    /* cooldown   after the LAST bolt, s / 마지막 볼트 뒤의 대기 */                    \
     20.0f,    /* shot_speed projectile, m/s -- 0 for a swing / 발사체 속도. 휘두르기는 0 */      \
     5,        /* burst      most bolts one attack releases / 한 번에 나가는 최대 발수 */         \
     5,        /* burst_min  fewest; equal to burst is a fixed count / 최소 발수. 같으면 고정 */  \
     0.22f,    /* spread     scatter, as a fraction of the distance / 흩어짐. 거리에 대한 비율 */ \
     0.00f,    /* shot_gap   between bolts, s -- 0 fires them together / 볼트 간격. 0이면 동시 */ \
     1.0f}     /* weight     pick chance against the other slots / 다른 슬롯 대비 선택 확률 */

/* Eight in a line, tight, a short gap each: a beam to step out of.
   촘촘한 여덟 발이 짧은 간격으로. 옆으로 빠져나올 빔. */
#define MAW_PATTERN_STREAM                                                                    \
    {ATK_BOLT, 0.0f, 40.0f, 40.0f, 0.0f,                                                      \
     8, /* damage     lighter per bolt, and there are more of them / 발당 가볍고 수가 많음 */ \
     0.70f, 1.30f,                                                                            \
     18.0f, /* shot_speed the fastest of the three / 셋 중 가장 빠름 */                       \
     8, 8,                                                                                    \
     0.05f, /* spread     tight enough to read as a line / 선으로 읽힐 만큼 좁음 */           \
     0.11f, /* shot_gap   one after another, not together / 동시가 아니라 차례로 */           \
     1.0f}

/* One slow heavy bolt after a long windup: the one to watch for.
   긴 예비 동작 뒤의 느리고 무거운 한 발. 지켜봐야 할 것. */
#define MAW_PATTERN_HEAVY                                                                     \
    {ATK_BOLT, 0.0f, 40.0f, 40.0f, 0.0f,                                                      \
     40,    /* damage     the heaviest single hit in the game / 게임에서 가장 무거운 한 발 */ \
     1.30f, /* windup     long enough to see coming / 오는 것이 보일 만큼 긺 */               \
     1.60f,                                                                                   \
     9.0f, /* shot_speed slow, so it can be walked out of / 느려서 걸어서 피할 수 있음 */     \
     1, 1, 0.00f, 0.00f,                                                                      \
     0.6f} /* weight     rarer than the other two / 나머지 둘보다 드물게 */

static const MonAttack ATTACKS[MON_TYPES][MON_MAX_ATTACKS] = {
    /* Slot 0 bolt, slot 1 swing. min 4.2 is `band * CASTER_KEEP` (7.5 * 0.55), the retreat distance.
       THE SWING DOUBLED WITH THE BODY, 1.8 -> 3.6 in both max and reach, because the kind is twice
       the size it was and enemy.h asks for a melee reach to be scaled by hand in that same edit.
       It is the same distance beyond its own hide as before: 3.6 / 1.04 is 1.8 / 0.52.
       슬롯 0은 볼트, 슬롯 1은 휘두르기. min 4.2는 `band * CASTER_KEEP`(7.5 * 0.55)이며 물러나는 거리입니다.
       *휘두르기는 몸과 함께 두 배가 되었습니다.* max와 reach 모두 1.8에서 3.6이며, 이 종류가 예전의 두 배
       크기가 되었고 enemy.h가 근접 사거리를 같은 수정에서 손으로 조정하라고 요구하기 때문입니다.
       자기 몸 바깥으로 뻗는 거리는 예전과 같습니다. 3.6 / 1.04 = 1.8 / 0.52입니다. */
    [MON_WATER_SPIRIT] = {
        {ATK_BOLT,  /* kind */
         4.2f,      /* min        `band * CASTER_KEEP` -- the retreat distance / 물러나는 거리 */
         7.5f,      /* max */
         7.5f,      /* reach */
         0.0f,      /* close      holds its ground / 제자리를 지킴 */
         3,         /* damage     PER BOLT: 10..20 a cast / 볼트 하나. 한 번에 10~20 */
         0.30f,     /* windup */
         0.50f,     /* cooldown */
         9.0f,      /* shot_speed */
         10,        /* burst      up to ten / 최대 열 발 */
         5,         /* burst_min  at least five / 최소 다섯 발 */
         0.5f,      /* spread     a cone rather than a line / 선이 아니라 원뿔 */
         0.07f,     /* shot_gap */
         1.0f},     /* weight */
        {ATK_SWING, /* kind */
         0.0f,      /* min */
         3.6f,      /* max        doubled with the body / 몸과 함께 두 배 */
         3.6f,      /* reach      same distance beyond its hide as before / 몸 바깥 거리는 예전과 같음 */
         0.15f,     /* close */
         8,         /* damage */
         0.25f,     /* windup */
         0.70f,     /* cooldown */
         0.0f,      /* shot_speed a swing throws nothing / 휘두르기는 아무것도 던지지 않음 */
         1,         /* burst */
         1,         /* burst_min */
         0.0f,      /* spread */
         0.0f,      /* shot_gap */
         1.0f},     /* weight */
    },
    /* Slot 0 standing swing, slot 1 the charge: `close` 2.5 is two and a half times walking speed
       through the windup (10 m/s on a 4.0 walk, just under the player's 10.8, so a strafe still
       escapes it), and its band (2.3..4.0) is wider than its reach (2.8), so it lands only if
       the gap closes.
       슬롯 0은 서서 하는 휘두르기, 슬롯 1은 돌진입니다. `close` 2.5는 준비동작 동안 걷기의 2.5배
       (4.0 걷기에 10 m/s, 플레이어의 10.8 바로 아래라 옆걸음으로는 여전히 벗어납니다)이고,
       대역(2.3~4.0)이 사거리(2.8)보다 넓으므로 간격이 닫혀야 닿습니다. */
    [MON_BRUTE] = {
        {ATK_SWING,        /* kind */
         0.0f,             /* min */
         2.3f,             /* max */
         2.3f,             /* reach */
         0.15f,            /* close */
         30,               /* damage */
         0.55f,            /* windup */
         1.50f,            /* cooldown */
         0.0f,             /* shot_speed */
         1, 1, 0.0f, 0.0f, /* burst, burst_min, spread, shot_gap -- a single swing / 휘두르기 한 번 */
         1.0f},            /* weight */
        {ATK_SWING,        /* kind */
         2.3f,             /* min        starts where the standing swing ends / 서서 하는 휘두르기가 끝나는 곳부터 */
         4.0f,             /* max */
         2.8f,             /* reach      narrower than the band: the gap must close / 대역보다 좁음. 간격이 닫혀야 함 */
         2.50f,            /* close      2.5x walking speed through the windup / 준비동작 동안 걷기의 2.5배 */
         34,               /* damage     the heaviest melee in the table / 표에서 가장 무거운 근접 */
         0.75f,            /* windup */
         2.20f,            /* cooldown */
         0.0f,             /* shot_speed */
         1, 1, 0.0f, 0.0f, /* burst, burst_min, spread, shot_gap */
         1.0f},            /* weight */
    },
    /* Slot 0 bolt, slot 1 a swing for a player inside the retreat distance. The bolt's max is 18, a
       little over half the arena's 33.8 m floor diagonal; it used to be 13.
       슬롯 0은 볼트, 슬롯 1은 물러나는 거리 안으로 들어온 플레이어를 위한 휘두르기입니다. 볼트의 max
       18은 아레나 바닥 대각선 33.8m의 절반을 조금 넘습니다. 13이었습니다. */
    [MON_CASTER] = {
        {ATK_BOLT,         /* kind */
         9.9f,             /* min        the retreat distance / 물러나는 거리 */
         18.0f,            /* max        half the arena's floor diagonal / 아레나 바닥 대각선의 절반 */
         18.0f,            /* reach */
         0.0f,             /* close */
         24,               /* damage     one aimed bolt / 조준한 한 발 */
         0.85f,            /* windup */
         1.40f,            /* cooldown */
         11.0f,            /* shot_speed */
         1, 1, 0.0f, 0.0f, /* burst, burst_min, spread, shot_gap -- a single shot / 한 발 */
         1.0f},            /* weight */
        {ATK_SWING,        /* kind */
         0.0f,             /* min        for a player inside the retreat / 물러나는 거리 안의 플레이어용 */
         2.2f,             /* max */
         2.2f,             /* reach */
         0.15f,            /* close */
         12,               /* damage */
         0.40f,            /* windup */
         0.95f,            /* cooldown */
         0.0f,             /* shot_speed */
         1, 1, 0.0f, 0.0f, /* burst, burst_min, spread, shot_gap */
         1.0f},            /* weight */
    },
    /* Three patterns, picked by ::MonAttack::weight. / 세 가지 탄막이며 ::MonAttack::weight로 고릅니다. */
    [MON_MAW] = {
        MAW_PATTERN_VOLLEY,
        MAW_PATTERN_STREAM,
        MAW_PATTERN_HEAVY,
    },
    /* The volley, copied from the maw. Changing the macro moves both.
       아귀에게서 베낀 일제 사격입니다. 매크로를 바꾸면 둘 다 움직입니다. */
    [MON_WARD] = {
        MAW_PATTERN_VOLLEY,
    },
};

const MonAttack *mon_attack(int type, int slot)
{
    if (type < 0 || type >= MON_TYPES)
        return 0;
    if (slot < 0 || slot >= MON_MAX_ATTACKS)
        return 0;
    const MonAttack *a = &ATTACKS[type][slot];
    return a->kind == ATK_NONE ? 0 : a;
}

int mon_attack_count(int type)
{
    if (type < 0 || type >= MON_TYPES)
        return 0;
    int n = 0;
    while (n < MON_MAX_ATTACKS && ATTACKS[type][n].kind != ATK_NONE)
        n++;
    return n;
}

/* --- Static function prototypes / 정적 함수 프로토타입 --- */
static void types_check(void);
static int name_eq(const char *a, const char *b);

static float frand(EnemyPool *e);
static void play_at(v3 p, const char *name, int base);

/* Returns the slot it filled PLUS ONE, or 0 when it refused, so `if (make_monster(...))` still
   reads as a yes/no while a caller that must tag what it made can have the index. A backward walk
   for "the newest live one of that type" is what this replaces, and that walk was wrong: the slot
   reused is the OLDEST CORPSE's, which can be anywhere in the pool, so the walk could claim a
   monster the level itself had placed.
   채운 칸에 *1을 더해* 돌려주며 거절하면 0입니다. 그래서 `if (make_monster(...))`는 여전히
   예·아니오로 읽히고, 만든 것에 표를 달아야 하는 호출자는 색인을 가질 수 있습니다. 이것이
   대체하는 것은 "그 종류의 가장 새로운 살아 있는 것"을 뒤로 훑는 방식인데, 그 걸음은 틀렸습니다.
   재사용되는 칸은 *가장 오래된 시체*의 것이고 그것은 풀의 어디든 될 수 있으므로, 훑기가 레벨이
   직접 배치한 몬스터를 주장할 수 있었습니다. */
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
static int mon_clear(const Pools *pl, const MonType *S, const Enemy *self,
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
 * ::chase_brawler closes to it and ::chase_caster keeps just inside it; a kind with no
 * attack (::AI_INERT) gets 0, which the callers read as "already there".
 * 아키타입이 유지하는 거리(미터)이며 슬롯 0의 상한입니다. ::chase_brawler는 그곳까지
 * 다가가고 ::chase_caster는 그 바로 안쪽을 지킵니다. 공격이 없는 종류(::AI_INERT)는 0을
 * 받으며, 호출자는 그것을 "이미 도착"으로 읽습니다.
 */
static float mon_band(int type)
{
    const MonAttack *A = mon_attack(type, 0);
    return A ? A->max : 0.0f;
}

static void chase_brawler(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 to, float dist, v3 player_eye, float dt);
static void chase_caster(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 to, float dist, v3 player_eye, float dt);
static int release_swing(const MonAttack *A, Enemy *m, float dist, float off);
static void begin_attack(Pools *pl, const MonAttack *A, Enemy *m);
static void release_bolt(Pools *pl, const Level *l, const MonType *S,
                         const MonAttack *A, Enemy *m, v3 player_eye);

/* --- Public function definitions / 공개 함수 정의 --- */
/* Ordered as enemy.h declares them; each function's contract is in the header.
   enemy.h가 선언한 순서를 따르며, 각 함수의 계약은 헤더에 있습니다. */
const MonType *mon_stats(int type)
{
    if (type < 0 || type >= MON_TYPES)
        type = MON_WATER_SPIRIT;
    return &TYPES[type];
}

int mon_type_for(const char *kind)
{
    /* Walks the TYPES table by name, then the MON_LEGACY aliases.
       TYPES 표를 이름으로 순회한 뒤 MON_LEGACY 별칭을 봅니다. */
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

    /* The spawners are cleared with the monsters.
       스포너도 몬스터와 함께 지웁니다. */
    for (int i = 0; i < ENEMY_MAX_SPAWNERS; i++)
        pl->enemy.spawner[i].active = 0;
    pl->enemy.n_spawners = 0;

    /* The undrained kill tally is cleared too, so a kill left here cannot be paid into a later run.
       비우지 않은 처치 집계도 지웁니다. 이곳에 남은 처치가 나중의 플레이에 지급되지 않도록 합니다. */
    pl->enemy.deaths = 0;

    /* The boss fight state is cleared, by whole-struct assignment so a member added to
       ::BossFight is cleared without anyone extending a list.
       보스전 상태를 지웁니다. 구조체 전체 대입으로 지우므로 ::BossFight에 추가된 멤버도
       누군가 목록을 늘리지 않아도 지워집니다. */
    BossFight nofight = {0};
    pl->enemy.boss = nofight;

    /* The spawn suppression, a property of a fight in progress, is reset with it.
       진행 중인 전투의 성질인 스포너 억제도 함께 초기화합니다. */
    pl->enemy.spawn_slow = 0.0f;
    pl->enemy.spawn_rate = 1.0f;
    pl->enemy.lull = 0.0f;
    pl->enemy.hp_mul = 1.0f;
}

void enemy_spawn_level(Pools *pl, const Level *l)
{
    /* Validates the stat tables on every level load and headless test.
       모든 레벨 로드와 헤드리스 테스트에서 수치 표를 검증합니다. */
    types_check();

    enemy_reset(pl);
    /* Runs over every entity even once the pool is full, so what was skipped is still counted.
       풀이 가득 찬 뒤에도 모든 엔티티를 순회하여, 건너뛴 것도 빠짐없이 셉니다. */
    for (int i = 0; i < l->n_ents; i++)
    {
        const Entity *e = &l->ents[i];
        int type = mon_type_for(e->kind);
        if (type < 0)
            continue;

        /* --- what a boss fight does NOT lay out at load ------------------
         *
         * A ::MON_BOSS entity only records its position in `maw_pos` and sets `have_maw`;
         * ::step_boss decides when the maw exists. A ::MON_GUARD entity is skipped
         * entirely, since a ward exists only while a fight is under way.
         *
         * --- 보스전이 로드 시점에 배치하지 *않는* 것 ----------------------
         *
         * ::MON_BOSS 엔티티는 위치만 `maw_pos`에 기록하고 `have_maw`를 세우며, 아귀가 언제
         * 존재할지는 ::step_boss가 정합니다. ::MON_GUARD 엔티티는 결계핵이 전투 중에만
         * 존재하므로 아예 건너뜁니다. */
        if (TYPES[type].flags & MON_BOSS)
        {
            pl->enemy.boss.maw_pos = v3f(e->x * 0.01f, e->y * 0.01f, e->z * 0.01f);
            pl->enemy.boss.have_maw = 1;
            continue;
        }
        if (TYPES[type].flags & MON_GUARD)
            continue;

        /* Through ::make_monster, the same path a spawner uses: ground search, cap check,
           fields and sight offset in one place.
           스포너와 같은 경로인 ::make_monster를 통합니다. 지면 탐색, 상한 검사, 필드, 시야
           오프셋이 한 곳에 있습니다. */
        make_monster(pl, l, type, e->x * 0.01f, e->y * 0.01f, e->z * 0.01f);
    }

    /* After the level's own monsters, because a spawner's ceiling counts them.
       레벨이 배치한 몬스터 다음입니다. 스포너의 상한이 그들을 세기 때문입니다. */
    spawners_of(pl, l);

    /* The ward candidates spawn nothing, so they may be read in any order relative to the
       two above; the sort inside wants the whole set.
       결계핵 후보는 아무것도 생성하지 않으므로 위의 둘과 어떤 순서로 읽어도 되며, 그 안의
       정렬은 전체 집합을 원합니다. */
    enemy_ward_scan(pl, l);
}

void enemy_wave_arm(Pools *pl, int wave)
{
    if (wave < 1)
        wave = 1;
    int step = wave - 1;

    /* The lull: every spawner holds for ::WAVE_LULL after a rollover, so a wave has a
       valley before its step up. Not on the first wave, which has its full interval anyway.
       휴지기입니다. 롤오버 뒤 모든 스포너가 ::WAVE_LULL 동안 멈추므로, 웨이브는 올라서기 전에
       골짜기를 가집니다. 첫 웨이브는 어차피 온전한 간격을 가지므로 제외합니다. */
    pl->enemy.lull = step > 0 ? WAVE_LULL : 0.0f;

    /* The health ladder, and it STOPS. See ::WAVE_HP_MAX for the arithmetic the ceiling
       comes from; past it the curve is carried by the interval and the alive ceiling below.
       체력 사다리이며 *멈춥니다.* 천장이 어떤 계산에서 나왔는지는 ::WAVE_HP_MAX를 보십시오.
       그 뒤로 곡선을 이어 가는 것은 아래의 간격과 생존 천장입니다. */
    float hp = 1.0f + (float)step * WAVE_HP_STEP;
    if (hp > WAVE_HP_MAX)
        hp = WAVE_HP_MAX;
    pl->enemy.hp_mul = hp;

    for (int i = 0; i < pl->enemy.n_spawners; i++)
    {
        Spawner *s = &pl->enemy.spawner[i];

        /* Every slot, not only the active ones: a spawner retired by the previous wave is brought back.
           활성 슬롯만이 아니라 모든 슬롯입니다. 이전 웨이브가 은퇴시킨 스포너를 되살립니다. */
        /* Unlimited budget: -1 is the value ::spawners_update reads as "unlimited", so a
           spawner never goes quiet; the clock in ::step_wave is what ends a wave.
           무제한 예산입니다. -1은 ::spawners_update가 "무제한"으로 읽는 값이므로 스포너는
           조용해지지 않으며, 웨이브를 끝내는 것은 ::step_wave의 시계입니다. */
        int budget = -1;

        /* The alive ceiling climbs by ::WAVE_ALIVE_STEP per wave, clamped to ENEMY_MAX;
           `max_alive` is a level-wide count.
           살아 있는 수의 천장이 웨이브마다 ::WAVE_ALIVE_STEP씩 오르며 ENEMY_MAX로 고정됩니다.
           `max_alive`는 레벨 전체 수입니다. */
        if (s->base_alive > 0)
        {
            int ceil = s->base_alive + step * WAVE_ALIVE_STEP;
            if (ceil > ENEMY_MAX)
                ceil = ENEMY_MAX;
            s->max_alive = (short)ceil;
        }

        int burst = 1 + step / WAVE_BURST_EVERY;
        if (burst > WAVE_BURST_MAX)
            burst = WAVE_BURST_MAX;

        /* The interval floor is the smaller of the authored interval and ::WAVE_INTERVAL_MIN:
           deeper waves cannot go below ::WAVE_INTERVAL_MIN, and a level authored faster than
           the floor keeps its own interval from wave 1 on.
           MULTIPLIED, NOT SUBTRACTED: a 6 s spawner and a 12 s spawner keep their 2:1 ratio
           all the way down, where a flat subtraction pinned the fast one to the floor at
           wave 15 and left the slow one ramping alone until wave 32.
           간격의 하한은 제작된 간격과 ::WAVE_INTERVAL_MIN 중 작은 쪽입니다. 깊은 웨이브는
           ::WAVE_INTERVAL_MIN 아래로 갈 수 없고, 하한보다 빠르게 제작된 레벨은 웨이브 1부터
           자기 간격을 유지합니다.
           *빼지 않고 곱합니다.* 6초 스포너와 12초 스포너는 끝까지 2:1 비율을 유지합니다. 고정
           감산은 빠른 쪽을 웨이브 15에 하한에 못 박고 느린 쪽만 웨이브 32까지 홀로 올렸습니다. */
        float floor_iv = s->base_interval < WAVE_INTERVAL_MIN
                             ? s->base_interval
                             : WAVE_INTERVAL_MIN;
        float interval = s->base_interval;
        for (int k = 0; k < step && interval > floor_iv; k++)
            interval *= WAVE_INTERVAL_DECAY;
        if (interval < floor_iv)
            interval = floor_iv;

        s->left = (short)budget;
        s->burst = (short)burst;
        s->interval = interval;

        /* The first group of a wave is due after a full interval.
           웨이브의 첫 무리는 온전한 한 주기 뒤에 나옵니다. */
        s->timer = spawn_wait(&pl->enemy, interval);
        s->warn = 0.0f;
        s->active = 1;
    }
}

int enemy_wave_done(const Pools *pl)
{
    if (pl->enemy.n_spawners < 1)
        return 0;

    for (int i = 0; i < pl->enemy.n_spawners; i++)
    {
        const Spawner *s = &pl->enemy.spawner[i];
        if (s->warn > 0.0f)
            return 0; /* already owed / 이미 예고된 상태 */
        if (s->active && s->left != 0)
            return 0; /* still to send / 아직 보낼 것이 남음 */
    }

    /* Minions only, not everything alive: a boss and its wards are in this pool, so ::enemy_alive
       would never reach zero during a fight. A ward's summons are ordinary monsters and are counted.
       살아 있는 모든 것이 아니라 잡졸만 셉니다. 보스와 결계핵은 이 풀에 있어 전투 중에는
       ::enemy_alive가 0이 되지 않기 때문입니다. 결계핵의 소환물은 평범한 몬스터이므로 셉니다. */
    return enemy_alive_minions(pl) == 0;
}

int enemy_spawner_count(const Pools *pl) { return pl->enemy.n_spawners; }

const Spawner *enemy_spawner_at(const Pools *pl, int i)
{
    return (i >= 0 && i < pl->enemy.n_spawners) ? &pl->enemy.spawner[i] : 0;
}

/* The point this monster fights: its foe's eye while a grudge holds, otherwise the player.
   The grudge lapses when the timer runs out, the foe is no longer a live monster, or the
   index is stale; all three are checked here on read rather than cleared in ::enemy_hurt_by.
   이 몬스터가 싸우는 지점입니다. 원한이 유지되는 동안은 상대의 눈 위치이고, 아니면 플레이어입니다.
   원한은 시간이 다하거나, 상대가 살아 있는 몬스터가 아니거나, 색인이 낡으면 풀리며, 셋 다
   ::enemy_hurt_by에서 지우지 않고 이곳에서 읽을 때 검사합니다. */
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
 * Each pair once (`j` from `i + 1`), both halves moved through ::move_toward so walls, ::floor_safe,
 * held height and ::MON_ANCHORED apply; each push is capped at half the overlap, one pass per frame.
 * 서로 안에 서 있는 두 몬스터를 밀어 떼어놓습니다. 쌍마다 한 번(`j`는 `i + 1`부터)이며 양쪽 다
 * ::move_toward로 움직이므로 벽, ::floor_safe, 유지 고도, ::MON_ANCHORED가 모두 적용됩니다. 각
 * 밀기는 겹침의 절반으로 제한되고 프레임당 한 번입니다.
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
            float r = AS->radius + BS->radius;
            float d2 = dx * dx + dz * dz;
            if (d2 >= r * r)
                continue;

            /* Two monsters exactly on top of each other have no direction; index order supplies a fixed one that is the same on every machine.
               정확히 겹친 두 몬스터에는 방향이 없으므로, 어느 기계에서나 같은 인덱스 순서로 고정 방향을 줍니다. */
            float d = sqrtf(d2);
            float ux, uz;
            if (d > 1e-4f)
            {
                ux = dx / d;
                uz = dz / d;
            }
            else
            {
                ux = 1.0f;
                uz = 0.0f;
                d = 0.0f;
            }

            float over = r - d;
            float push = over * MON_PUSH_RATE * dt;
            if (push > over * 0.5f)
                push = over * 0.5f;

            move_toward(pl, l, AS, a, player_eye, ux * push, uz * push);
            move_toward(pl, l, BS, b, player_eye, -ux * push, -uz * push);
        }
    }
}

int enemy_update(Pools *pl, const Level *l, v3 player_eye, float dt)
{
    int player_damage = shots_update(pl, l, player_eye, dt);

    /* Before the monsters are stepped, so one made this frame gets its first frame this frame.
       몬스터를 진행시키기 전입니다. 이번 프레임에 만들어진 몬스터가 이번 프레임에 첫 프레임을 얻습니다. */
    spawners_update(pl, l, player_eye, dt);

    for (int i = 0; i < pl->enemy.count; i++)
    {
        Enemy *m = &pl->enemy.m[i];
        if (!m->active)
            continue;
        const MonType *S = &TYPES[m->type];

        if (m->foe_time > 0.0f)
            m->foe_time -= dt;

        /* Resolved once per frame, so chasing and shooting use the same point.
           프레임마다 한 번 해결하여, 추격과 사격이 같은 지점을 씁니다. */
        v3 goal = foe_point(pl, m, player_eye);

        m->anim += dt;
        if (m->flash > 0.0f)
            m->flash -= dt * 4.0f;

        /* A corpse still falls: a dead monster does nothing else, but ::monster_fall runs for it
           unless ::holds_height keeps it up -- the same call the living make at the bottom of this loop.
           시체도 여전히 떨어집니다. 죽은 몬스터는 다른 일을 하지 않지만, ::holds_height가 떠 있게
           하지 않는 한 ::monster_fall이 실행됩니다. 루프 아래쪽에서 살아 있는 것이 하는 것과 같은 호출입니다. */
        if (m->state == E_DEAD)
        {
            if (S->flags & MON_COLLAPSES)
            {
                /* Hold and burst, then sink ::COLLAPSE_SINK heights over
                   ::COLLAPSE_SINK_TIME, then go. Bursts land at random points
                   over the body so the sequence reads as the thing coming
                   apart rather than one effect looping.
                   자리를 지키며 터지다가 ::COLLAPSE_SINK_TIME 동안 ::COLLAPSE_SINK 신장만큼
                   가라앉고 사라집니다. 폭발은 몸 위의 무작위 지점에 놓여, 효과 하나가
                   반복되는 것이 아니라 그것이 부서지는 것으로 읽힙니다. */
                m->timer += dt;
                m->cast_timer -= dt;
                if (m->cast_timer <= 0.0f && m->timer < COLLAPSE_HOLD + COLLAPSE_SINK_TIME * 0.6f)
                {
                    m->cast_timer = COLLAPSE_BOOM_GAP;
                    float ax = (frand(&pl->enemy) - 0.5f) * S->height * S->aspect;
                    float ay = frand(&pl->enemy) * S->height;
                    float az = (frand(&pl->enemy) - 0.5f) * S->height * S->aspect;
                    v3 at = v3f(m->volley_at.x + ax, m->pos.y + ay, m->volley_at.z + az);
                    fx_spawn(pl, "blastburst", at, v3f(0, 1, 0));
                    fx_spawn(pl, "debris", at, v3f(0, 1, 0));
                    play_at(at, "impact", 90);
                }
                if (m->timer > COLLAPSE_HOLD)
                {
                    float depth = S->height * COLLAPSE_SINK;
                    m->pos.y -= depth * (dt / COLLAPSE_SINK_TIME);
                    if (m->pos.y <= m->volley_at.y - depth)
                        m->active = 0;
                }
                continue;
            }
            if (m->timer > 0.0f)
                m->timer -= dt;
            if (!holds_height(S, m))
                monster_fall(l, S, m, dt);
            continue;
        }

        /* --- pay what a ward owes ---------------------------------------
         *
         * ::enemy_hurt recorded the debt in `summon_left`; this loop pays it, telegraphing each
         * arrival with the spawners' effects and delivering the monster ::SPAWN_WARN_TIME later.
         * The ::WARD_SUMMON_CAP check is made at arrival, so a full room still delivers once it empties.
         *
         * --- 결계핵이 빚진 것을 갚는다 ------------------------------------
         *
         * ::enemy_hurt가 `summon_left`에 기록한 빚을 이 루프가 갚습니다. 도착마다 스포너의
         * 이펙트로 예고하고 ::SPAWN_WARN_TIME 뒤에 몬스터를 배달합니다. ::WARD_SUMMON_CAP
         * 검사는 도착할 때 하므로, 가득 찬 방도 비고 나면 배달됩니다. */
        if (m->summon_left > 0)
        {
            if (m->summon_warn <= 0.0f)
            {
                /* The spawner's own warning effects and sound at the arrival point; fx_spawn takes
                   a bare position, so no ::Spawner is needed.
                   스포너 자신의 예고 이펙트와 소리를 도착 지점에서 재생합니다. fx_spawn은 위치만
                   받으므로 ::Spawner가 필요 없습니다. */
                m->summon_warn = SPAWN_WARN_TIME;
                v3 warn_at = ward_summon_at(m);
                fx_spawn(pl, "spawnwarp", warn_at, v3f(0, 1, 0));
                fx_spawn(pl, "spawnring", warn_at, v3f(0, 1, 0));
                play_at(warn_at, "spawnwarn", 70);
            }
            else if ((m->summon_warn -= dt) <= 0.0f)
            {
                /* Computed before the decrement: ::ward_summon_at is a function of ::Enemy::summon_left,
                   so the arrival lands where the telegraph played.
                   감소 전에 계산합니다. ::ward_summon_at은 ::Enemy::summon_left의 함수이므로, 도착이
                   예고가 재생된 자리에 떨어집니다. */
                v3 at = ward_summon_at(m);

                /* Drawn whether or not it is used, so the generator advances by the same amount
                   whether the room is full or not.
                   쓰이든 아니든 뽑습니다. 방이 가득 찼든 아니든 생성기가 같은 만큼 전진합니다. */
                int type = ward_summon_type(pl, m);

                m->summon_warn = 0.0f;
                m->summon_left--;

                if (enemy_alive(pl) < WARD_SUMMON_CAP)
                {
                    make_monster(pl, l, type, at.x, at.y, at.z);
                    fx_spawn(pl, "spawnburst", at, v3f(0, 1, 0));
                    play_at(at, "spawnpop", 80);
                }
                /* The maw has no marker to say which pair; it alternates.
                   아귀에는 어느 쌍인지 말할 표식이 없으므로 번갈아 씁니다. */
                if (S->flags & MON_BOSS)
                    m->ward_table = (short)!m->ward_table;
            }
        }

        /* An ::AI_INERT monster is done for the frame: it pays its summon debt above and never
           enters the state machine below or turns toward the player.
           ::AI_INERT 몬스터는 이 프레임에서 끝입니다. 위에서 소환 빚을 갚을 뿐, 아래의 상태 기계에
           들어가지 않고 플레이어 쪽으로 돌지도 않습니다. */
        if (S->behaviour == AI_INERT)
            continue;

        /* Ages the cached sight reading once per frame, here rather than per ::sees_player call,
           and only for the living.
           캐시된 시야 판정을 ::sees_player 호출마다가 아니라 프레임마다 한 번, 살아 있는 몬스터에
           대해서만 노화시킵니다. */
        if (m->sight_age > 0)
            m->sight_age--;

        v3 to = v3sub(goal, v3f(m->pos.x, m->pos.y + S->eye, m->pos.z));
        float dist = sqrtf(to.x * to.x + to.z * to.z);

        /* Where the monster wants to look; ::change_yaw turns it toward this at ::MonType::yaw_speed.
           몬스터가 보고 싶은 방향이며, ::change_yaw가 ::MonType::yaw_speed로 이 방향을 향해 돌립니다. */
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
            /* Uses the cached sight reading; the distance test in front of it means a monster
               out of sight range never pays for the trace.
               캐시된 시야 판정을 씁니다. 앞의 거리 검사 덕분에 시야 거리 밖의 몬스터는 판정
               비용을 치르지 않습니다. */
            if (dist < S->sight && sees_player(pl, l, m, goal))
            {
                m->state = E_CHASE;
                play_at(m->pos, "sight", 80);
            }
            break;

        case E_CHASE:
            /* The archetype dispatch: ::AI_CASTER chases as a caster, everything else as a brawler.
               아키타입 분기입니다. ::AI_CASTER는 캐스터로, 나머지는 근접형으로 추격합니다. */
            if (S->behaviour == AI_CASTER)
                chase_caster(pl, l, S, m, to, dist, goal, dt);
            else
                chase_brawler(pl, l, S, m, to, dist, player_eye, dt);
            break;

        case E_ATTACK:
            m->timer += dt;
            {
                /* The volley: `volley_n` bolts, one per ::MonType::shot_gap, released in a `while`
                   so a gap shorter than a frame (or zero) fires several in one frame.
                   일제 사격입니다. `volley_n`발을 ::MonType::shot_gap마다 한 발씩 `while`로 쏘므로,
                   프레임보다 짧거나 0인 간격은 한 프레임에 여러 발을 쏩니다. */
                int shots = m->volley_n > 0 ? m->volley_n : 1;

                /* The attack slot, resolved once: ::Enemy::atk was chosen on entering this state and
                   everything below reads it. A missing slot falls back to slot 0; no attack at all
                   returns to E_CHASE.
                   공격 슬롯을 한 번 해결합니다. ::Enemy::atk은 이 상태에 들어갈 때 골랐고 아래의 모든
                   줄이 그것을 읽습니다. 사라진 슬롯은 슬롯 0으로 물러나고, 공격이 아예 없으면
                   E_CHASE로 돌아갑니다. */
                const MonAttack *A = mon_attack(m->type, m->atk);
                if (!A)
                    A = mon_attack(m->type, 0);
                if (!A)
                {
                    m->state = E_CHASE;
                    break;
                }

                /* The charge effect while the bolt pose is up: `castgather` is spawned every
                   CAST_GATHER_INTERVAL at ::MonType::eye height, before the first bolt only (`!m->swung`).
                   볼트 자세가 올라와 있는 동안의 충전 이펙트입니다. `castgather`를 CAST_GATHER_INTERVAL마다
                   ::MonType::eye 높이에 생성하며, 첫 탄환 전(`!m->swung`)에만 그렇게 합니다. */
                /* The step into the swing: while `close` is set and the wind-up has not ended, the
                   monster advances along its yaw at `speed * close`.
                   휘두르기 안으로 내딛는 걸음입니다. `close`가 설정되어 있고 준비동작이 끝나기 전이면
                   자기 yaw 방향으로 `speed * close`만큼 전진합니다. */
                if (A->close > 0.0f && m->timer < A->windup)
                {
                    float step = S->speed * A->close * dt;

                    /* NEVER PAST THE TARGET. The step is capped at the gap to the goal, so a
                       charge that has arrived stands still for the rest of its wind-up instead
                       of sliding round the player and ending up behind them -- which, at a
                       charge faster than a walk, put the brute back in its own charge band and
                       had it charging forever.
                       *목표를 지나치지 않습니다.* 걸음을 목표까지의 간격으로 제한하므로, 도착한
                       돌진은 남은 준비동작 동안 제자리에 섭니다. 플레이어 옆으로 미끄러져 등 뒤에
                       서는 대신입니다. 걷기보다 빠른 돌진에서 그것은 브루트를 자기 돌진 대역에
                       되돌려 놓아 영원히 돌진하게 만들었습니다. */
                    float gx = goal.x - m->pos.x, gz = goal.z - m->pos.z;
                    float gd = sqrtf(gx * gx + gz * gz);
                    float gap = gd - (S->radius + PLAYER_RADIUS);
                    if (step > gap)
                        step = gap > 0.0f ? gap : 0.0f;

                    /* AND ONLY WHILE FACING IT. The step runs along the yaw, and a monster that
                       begins its wind-up still turned away would charge AWAY from the target at
                       full charge speed -- at 10 m/s, faster than the player can follow. So the
                       step is taken only when the facing is within 60 degrees of the goal; a
                       brute caught facing the wrong way stands its wind-up out and misses.
                       *그리고 바라보고 있을 때만입니다.* 걸음은 yaw를 따라가므로, 아직 등을 돌린
                       채 준비동작을 시작한 몬스터는 목표에서 *멀어지는* 쪽으로 돌진 속도 그대로
                       달립니다. 10 m/s면 플레이어가 따라잡을 수 없습니다. 그래서 걸음은 바라보는
                       방향이 목표의 60도 안에 있을 때만 내딛으며, 등을 돌린 채 걸린 브루트는
                       준비동작을 제자리에서 흘려보내고 빗나갑니다. */
                    float facing = gd > 0.001f
                                       ? (-sinf(m->yaw) * gx - cosf(m->yaw) * gz) / gd
                                       : 1.0f;

                    if (step > 0.0f && facing > 0.5f)
                        move_toward(pl, l, S, m, player_eye,
                                    -sinf(m->yaw) * step, -cosf(m->yaw) * step);
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

                    /* How far the target is off the facing: the yaw toward
                       `goal` minus `m->yaw`, wrapped to -pi..pi and taken as an
                       absolute angle. Measured from the facing, not ::Enemy::ideal_yaw.
                       목표가 바라보는 방향에서 벗어난 각도입니다. `goal`을 향한 yaw에서
                       `m->yaw`를 뺀 뒤 -pi..pi로 감싸고 절댓값을 취합니다. ::Enemy::ideal_yaw가
                       아니라 바라보는 방향 기준입니다. */
                    float want = atan2f(-(goal.x - m->pos.x),
                                        -(goal.z - m->pos.z));
                    float off = want - m->yaw;
                    while (off > M_PI_F)
                        off -= 2.0f * M_PI_F;
                    while (off < -M_PI_F)
                        off += 2.0f * M_PI_F;
                    off = fabsf(off);

                    player_damage += release_swing(A, m, dist, off);

                    /* Spawns the "clawarc" effect at 0.6 of ::MonType::eye above
                       the feet, along the facing, with radius `A->reach` and angle
                       ::MON_SWING_CONE. Drawn whether or not the swing connected.
                       "clawarc" 이펙트를 발에서 ::MonType::eye의 0.6 높이에, 바라보는 방향으로,
                       반지름 `A->reach`와 각도 ::MON_SWING_CONE으로 생성합니다. 휘두르기가
                       맞았든 아니든 그립니다. */
                    fx_spawn_arc(pl, "clawarc",
                                 v3f(m->pos.x, m->pos.y + S->eye * 0.6f, m->pos.z),
                                 v3f(-sinf(m->yaw), 0.0f, -cosf(m->yaw)),
                                 A->reach, MON_SWING_CONE);
                }

                /* Time from the first bolt to the last: `(shots - 1) * A->shot_gap`.
                   The cooldown below is counted from the last bolt.
                   첫 볼트에서 마지막 볼트까지의 시간, `(shots - 1) * A->shot_gap`입니다.
                   아래의 경직은 마지막 볼트부터 셉니다. */
                float firing = (float)(shots - 1) * A->shot_gap;

                if (m->timer >= A->windup + firing + A->cooldown)
                {
                    /* Random rest of 0..::MON_ATTACK_REST before the next attack is considered.
                       다음 공격을 고려하기 전의 0..::MON_ATTACK_REST 사이 무작위 휴식입니다. */
                    m->attack_wait = frand(&pl->enemy) * MON_ATTACK_REST;

                    /* A bolt attack returns to ::E_CHASE; a swing still in reach restarts its timer.
                       볼트 공격은 ::E_CHASE로 돌아가고, 사거리 안의 휘두르기는 타이머를 다시 시작합니다. */
                    /* Which of the two applies is decided by the slot's kind (`A->kind`),
                       not by the monster's archetype.
                       둘 중 어느 쪽인지는 몬스터의 아키타입이 아니라 슬롯의 종류(`A->kind`)가
                       정합니다. */
                    /* The re-swing happens only while `dist` is within `A->min..A->reach`,
                       the arm's reach rather than the band's `A->max`; otherwise the
                       monster returns to ::E_CHASE.
                       재휘두르기는 `dist`가 `A->min..A->reach` 안일 때만 일어납니다. 대역의
                       `A->max`가 아니라 팔의 사거리입니다. 그 밖이면 몬스터는 ::E_CHASE로
                       돌아갑니다. */
                    if (A->kind == ATK_BOLT)
                        m->state = E_CHASE;
                    else if (dist >= A->min && dist <= A->reach)
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
           A monster that ::holds_height (::MON_FLIES or ::MON_ANCHORED) is
           skipped; every other one falls through ::monster_fall at
           ::PLAYER_GRAVITY.
           무엇이 그것을 떠받치는가. ::holds_height인 몬스터(::MON_FLIES 또는
           ::MON_ANCHORED)는 건너뛰고, 나머지는 모두 ::monster_fall을 통해
           ::PLAYER_GRAVITY로 떨어집니다. */
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

/* The same count as ::enemy_alive, narrowed to monsters of `type`. Scans the
   pool on every call; no running total is kept.
   ::enemy_alive와 같은 세기를 `type` 종류로 좁힌 것입니다. 호출마다 풀을 훑으며,
   누적 합계는 두지 않습니다. */
int enemy_alive_from(const Pools *pl, int si)
{
    int n = 0;
    for (int i = 0; i < pl->enemy.count; i++)
        if (pl->enemy.m[i].active && pl->enemy.m[i].state != E_DEAD &&
            pl->enemy.m[i].from_spawner == (short)si)
            n++;
    return n;
}

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

int enemy_shot_count(const Pools *pl)
{
    (void)pl;
    return ENEMY_MAX_SHOTS;
}

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

    /* Retargets before the damage lands. `from` of -1 (the player) clears
       ::Enemy::foe; another monster of a different type becomes the foe for
       ::INFIGHT_TIME. A same-type attacker leaves the foe unchanged.
       피해가 꽂히기 전에 표적을 바꿉니다. `from`이 -1(플레이어)이면 ::Enemy::foe를
       지우고, 다른 종류의 몬스터라면 ::INFIGHT_TIME 동안 그것을 적으로 삼습니다.
       같은 종류의 가해자는 적을 바꾸지 않습니다. */
    if (from < 0)
    {
        m->foe = -1;
        m->foe_time = 0.0f;
    }
    else if (from != idx && from < pl->enemy.count &&
             pl->enemy.m[from].type != m->type)
    {
        m->foe = (short)from;
        m->foe_time = INFIGHT_TIME;
    }

    /* Effect origin at half ::MonType::height above the feet; the height comes from the type table.
       이펙트 원점은 발에서 ::MonType::height의 절반 위이며, 신장은 종류 테이블에서 가져옵니다. */
    const MonType *S = &TYPES[m->type];
    v3 mid = v3f(m->pos.x, m->pos.y + S->height * 0.5f, m->pos.z);

    /* --- warded: the blow does not land ---------------------------------
     * A ::MON_BOSS with any ::MON_GUARD alive (::enemy_guards_alive > 0) takes
     * no damage: the "warded" effect is spawned and the function returns before
     * the health and the hit flash are touched. Every damage source passes here.
     * --- 결계에 막힘: 타격이 닿지 않는다 -----------------------------------
     * ::MON_GUARD가 하나라도 살아 있는(::enemy_guards_alive > 0) ::MON_BOSS는 피해를
     * 받지 않습니다. "warded" 이펙트를 생성하고 체력과 피격 섬광을 건드리기 전에
     * 반환합니다. 모든 피해원이 이곳을 지납니다. */
    if ((S->flags & MON_BOSS) && enemy_guards_alive(pl) > 0)
    {
        fx_spawn(pl, "warded", mid, v3f(0, 1, 0));
        return;
    }

    m->health -= dmg;
    m->flash = 1.0f;

    /* --- the cycle boundary ---------------------------------------------
     * A ::MON_BOSS's health is clamped to `hp * (BOSS_CYCLES - cycle - 1) /
     * BOSS_CYCLES`, so one cycle's damage cannot carry it past its current
     * third. The last boundary is zero; the final cycle dies through the branch below.
     * --- 사이클 경계 --------------------------------------------------------
     * ::MON_BOSS의 체력은 `hp * (BOSS_CYCLES - cycle - 1) / BOSS_CYCLES`로 고정되어,
     * 한 사이클의 피해가 현재 3분의 1 구간을 넘길 수 없습니다. 마지막 경계는 0이므로
     * 마지막 사이클은 아래의 사망 분기를 통해 죽입니다. */
    if (S->flags & MON_BOSS)
    {
        int floor_hp = S->hp * (BOSS_CYCLES - pl->enemy.boss.cycle - 1) / BOSS_CYCLES;
        if (m->health < floor_hp)
            m->health = floor_hp;
    }

    /* `back` is `dir` reversed, so the spray points at whoever landed the hit; straight up if `dir` is zero.
       `back`은 `dir`의 반대 방향이라 분출이 타격을 가한 쪽을 향하며, `dir`이 0이면 위쪽입니다. */
    v3 back = v3len(dir) > 1e-4f ? v3scale(v3norm(dir), -1.0f) : v3f(0, 1, 0);

    if (m->health <= 0)
    {
        m->state = E_DEAD;
        m->timer = 0.6f;
        /* A collapsing kind counts UP from here and keeps where it stood in
           `volley_at`; ::enemy_update runs the bursts and the sink.
           붕괴하는 종류는 여기서부터 위로 세고, 서 있던 자리를 `volley_at`에 둡니다.
           ::enemy_update가 폭발과 침강을 진행합니다. */
        if (S->flags & MON_COLLAPSES)
        {
            m->timer = 0.0f;
            m->volley_at = m->pos;
            m->cast_timer = 0.0f;
            fx_spawn(pl, "blastburst", mid, v3f(0, 1, 0));
            fx_spawn(pl, "smokepuff", mid, v3f(0, 1, 0));
        }

        /* ::EnemyPool::deaths is incremented here, at the only transition into
           ::E_DEAD. Corpses are recycled after fading, so dead slots are not counted later.
           ::EnemyPool::deaths는 ::E_DEAD로 들어가는 유일한 전이인 이곳에서 증가합니다.
           시체는 사라진 뒤 재활용되므로 죽은 슬롯을 나중에 세지 않습니다. */
        pl->enemy.deaths++;

        /* Two draws from the pool generator on every kill, spent whether or not
           the first succeeds, then ::loot_pick chooses the drop from the type's
           loot table. The generator advances by exactly two per kill.
           처치마다 풀 생성기에서 두 번 뽑으며, 첫 굴림의 성패와 무관하게 둘 다 소비한
           뒤 ::loot_pick이 그 종류의 드롭 표에서 드롭을 고릅니다. 생성기는 처치당
           정확히 둘씩 전진합니다. */
        float roll_any = frand(&pl->enemy);
        float roll_what = frand(&pl->enemy);
        m->drop = loot_pick(loot_drop(m->type), roll_any, roll_what);

        play_at(m->pos, "edie", 95);
        fx_spawn(pl, "gib", mid, back);
        return;
    }

    fx_spawn(pl, "blood", mid, back);

    play_at(m->pos, "epain", 70);

    /* --- a ward pays for being shot -------------------------------------
     * A ::MON_GUARD accrues damage in ::Enemy::summon_dmg; each ::WARD_SUMMON_DMG
     * accrued adds ::WARD_SUMMON_COUNT to ::Enemy::summon_left. Only survived
     * damage counts: the death branch above returns before this.
     * --- 결계핵은 맞은 값을 치른다 -----------------------------------------
     * ::MON_GUARD는 피해를 ::Enemy::summon_dmg에 누적하고, ::WARD_SUMMON_DMG가 쌓일
     * 때마다 ::Enemy::summon_left에 ::WARD_SUMMON_COUNT를 더합니다. 살아남은 피해만
     * 셉니다. 위의 사망 분기가 이보다 먼저 반환합니다. */
    if (S->flags & (MON_GUARD | MON_BOSS))
    {
        m->summon_dmg = (short)(m->summon_dmg + dmg);
        while (m->summon_dmg >= WARD_SUMMON_DMG)
        {
            m->summon_dmg = (short)(m->summon_dmg - WARD_SUMMON_DMG);
            m->summon_left = (short)(m->summon_left + WARD_SUMMON_COUNT);
        }
    }

    /* Flinch: an ::E_IDLE monster always enters ::E_HURT for 0.16s and sets
       ::Enemy::pain_wait to ::MonType::pain_lock; an ::E_CHASE monster does so
       only once `pain_wait` has run out. Other states are left as they are.
       경직입니다. ::E_IDLE 몬스터는 언제나 0.16초 동안 ::E_HURT로 들어가며
       ::Enemy::pain_wait를 ::MonType::pain_lock으로 설정합니다. ::E_CHASE 몬스터는
       `pain_wait`가 다 된 뒤에만 그렇게 합니다. 다른 상태는 그대로 둡니다. */
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
    for (int i = 0; i < pl->enemy.count; i++)
    {
        const Enemy *m = &pl->enemy.m[i];
        if (!m->active || m->state == E_DEAD)
            continue;
        if (TYPES[m->type].flags & MON_BOSS)
            return i;
    }
    return -1;
}

int enemy_guards_alive(const Pools *pl)
{
    int n = 0;
    for (int i = 0; i < pl->enemy.count; i++)
    {
        const Enemy *m = &pl->enemy.m[i];
        if (!m->active || m->state == E_DEAD)
            continue;
        if (TYPES[m->type].flags & MON_GUARD)
            n++;
    }
    return n;
}

int enemy_alive_minions(const Pools *pl)
{
    int n = 0;
    for (int i = 0; i < pl->enemy.count; i++)
    {
        const Enemy *m = &pl->enemy.m[i];
        if (!m->active || m->state == E_DEAD)
            continue;
        if (TYPES[m->type].flags & (MON_BOSS | MON_GUARD))
            continue;
        n++;
    }
    return n;
}

/* Where this ward's next summon appears: ::WARD_SUMMON_DIST from the ward at an
 * angle of ::Enemy::summon_left times the golden angle, at the ward's height.
 * A function of `summon_left` only; it draws nothing from the pool generator.
 * 이 결계핵의 다음 소환이 나타날 자리입니다. 결계핵에서 ::WARD_SUMMON_DIST 거리,
 * ::Enemy::summon_left 곱하기 황금각의 각도, 결계핵과 같은 높이입니다.
 * `summon_left`만의 함수이며 풀 생성기에서 아무것도 뽑지 않습니다. */
static v3 ward_summon_at(const Enemy *m)
{
    float a = (float)m->summon_left * 2.39996f; /* golden angle, radians */
    return v3f(m->pos.x + cosf(a) * WARD_SUMMON_DIST,
               m->pos.y,
               m->pos.z + sinf(a) * WARD_SUMMON_DIST);
}

/* Which monster this ward summons. ::Enemy::ward_table selects the AIR pair
 * (::MON_CASTER, ::MON_WATER_SPIRIT) or the GROUND pair (::MON_BRUTE, ::MON_WATER_SPIRIT);
 * one draw from the pool generator picks either entry with equal chance. Always exactly one draw.
 * 이 결계핵이 소환하는 몬스터입니다. ::Enemy::ward_table이 AIR 쌍(::MON_CASTER,
 * ::MON_WATER_SPIRIT) 또는 GROUND 쌍(::MON_BRUTE, ::MON_WATER_SPIRIT)을 고르고,
 * 풀 생성기에서 한 번 뽑아 두 항목 중 하나를 같은 확률로 고릅니다. 언제나 정확히 한 번 뽑습니다. */
static int ward_summon_type(Pools *pl, const Enemy *m)
{
    static const int AIR[2] = {MON_CASTER, MON_WATER_SPIRIT};
    static const int GROUND[2] = {MON_BRUTE, MON_WATER_SPIRIT};
    const int *t = m->ward_table ? AIR : GROUND;
    return t[frand(&pl->enemy) < 0.5f ? 0 : 1];
}

/* Strict ordering of candidate positions by x, then z, then y, compared as
   integer centimetres (`* 100`) so the sort in ::enemy_ward_scan gives the same order for the same map.
   후보 위치를 x, z, y 순으로 정하는 전순서이며, 정수 센티미터(`* 100`)로 비교하여
   ::enemy_ward_scan의 정렬이 같은 맵에 언제나 같은 순서를 내게 합니다. */
static int ward_before(v3 a, v3 b)
{
    int ax = (int)(a.x * 100.0f), bx = (int)(b.x * 100.0f);
    if (ax != bx)
        return ax < bx;
    int az = (int)(a.z * 100.0f), bz = (int)(b.z * 100.0f);
    if (az != bz)
        return az < bz;
    return (int)(a.y * 100.0f) < (int)(b.y * 100.0f);
}

void enemy_ward_scan(Pools *pl, const Level *l)
{
    BossFight *b = &pl->enemy.boss;

    for (int i = 0; i < l->n_ents; i++)
    {
        const Entity *e = &l->ents[i];
        int air = name_eq(e->kind, "wardair");
        if (!air && !name_eq(e->kind, "wardground"))
            continue;

        if (b->n_cand >= BOSS_MAX_CAND)
        {
            DIAG(DIAG_WARD_CAND);
            continue;
        }
        b->cand[b->n_cand] = v3f(e->x * 0.01f, e->y * 0.01f, e->z * 0.01f);
        b->cand_air[b->n_cand] = (char)air;
        b->n_cand++;
    }

    /* Insertion sort of the candidates by ::ward_before, moving `cand_air`
       alongside, so the order depends on position and not on `Level::ents` file order.
       후보를 ::ward_before로 삽입 정렬하며 `cand_air`를 함께 옮깁니다. 그래서 순서는
       `Level::ents`의 파일 순서가 아니라 위치에 따라 정해집니다. */
    for (int i = 1; i < b->n_cand; i++)
    {
        v3 p = b->cand[i];
        char a = b->cand_air[i];
        int j = i - 1;
        while (j >= 0 && ward_before(p, b->cand[j]))
        {
            b->cand[j + 1] = b->cand[j];
            b->cand_air[j + 1] = b->cand_air[j];
            j--;
        }
        b->cand[j + 1] = p;
        b->cand_air[j + 1] = a;
    }
}

int enemy_ward_place(Pools *pl, const Level *l)
{
    BossFight *b = &pl->enemy.boss;
    int placed = 0;
    b->ward_rounds++;

    /* One independent draw per list: `BOSS_WARDS / 2` from the air candidates
       and the same number from the ground candidates.
       목록마다 독립적으로 뽑습니다. 공중형 후보에서 `BOSS_WARDS / 2`개, 지상형 후보에서
       같은 수입니다. */
    for (int air = 0; air < 2; air++)
    {
        int want = BOSS_WARDS / 2;

        /* Indices of this kind's candidates that the previous cycle did not use (`cand_used` == 0).
           지난 사이클이 쓰지 않은(`cand_used` == 0) 이 종류의 후보 인덱스입니다. */
        char idx[BOSS_MAX_CAND];
        int n = 0;
        for (int i = 0; i < b->n_cand; i++)
            if (b->cand_air[i] == air && !b->cand_used[i])
                idx[n++] = (char)i;

        /* Fewer than `want` unused candidates: the list is rebuilt from every
           candidate of this kind, so positions repeat rather than fewer wards being placed.
           쓰지 않은 후보가 `want`보다 적으면 이 종류의 모든 후보로 목록을 다시 만들어,
           결계핵을 더 적게 놓는 대신 자리를 반복합니다. */
        if (n < want)
        {
            n = 0;
            for (int i = 0; i < b->n_cand; i++)
                if (b->cand_air[i] == air)
                    idx[n++] = (char)i;
        }

        if (n < want)
            DIAG(DIAG_WARD_CAND);

        /* Partial Fisher-Yates: exactly `want` draws from the pool generator,
           whatever is picked and however many candidates there are.
           부분 Fisher-Yates입니다. 무엇을 고르든, 후보가 몇이든 풀 생성기에서 정확히
           `want`번 뽑습니다. */
        for (int k = 0; k < want; k++)
        {
            /* The draw is taken before the empty-list check, so the generator
               advances by the same amount whether or not candidates remain.
               빈 목록 검사보다 먼저 뽑으므로, 후보가 남아 있든 아니든 생성기가 같은
               만큼 전진합니다. */
            float r = frand(&pl->enemy);
            if (n - k <= 0)
                continue;
            int pick = k + (int)(r * (float)(n - k));
            /* Clamps `pick` into range. ::frand never returns 1.0, so this only
               guards against an out-of-bounds swap.
               `pick`을 범위 안으로 고정합니다. ::frand는 1.0을 반환하지 않으므로 범위
               밖 교환을 막는 보호일 뿐입니다. */
            if (pick >= n)
                pick = n - 1;
            char t = idx[k];
            idx[k] = idx[pick];
            idx[pick] = t;

            int ci = idx[k];
            if (!make_monster(pl, l, MON_WARD,
                              b->cand[ci].x, b->cand[ci].y, b->cand[ci].z))
                continue;

            /* The ward just made is the last live ::MON_WARD slot; its ::Enemy::ward_table is set from the marker kind (`air`).
               방금 만든 결계핵은 마지막 살아 있는 ::MON_WARD 슬롯이며, 그 ::Enemy::ward_table을 표식 종류(`air`)로 설정합니다. */
            for (int s = pl->enemy.count - 1; s >= 0; s--)
            {
                if (pl->enemy.m[s].type != MON_WARD)
                    continue;
                if (pl->enemy.m[s].state == E_DEAD)
                    continue;
                pl->enemy.m[s].ward_table = (short)air;
                break;
            }
            b->cand_used[ci] = 1;
            placed++;
        }
    }

    /* --- roll the USED marks forward -------------------------------------
     * `cand_used` holds three values: 0 free, 1 used by this call, 2 used by
     * the previous cycle. After the selection, 2s are cleared to 0 and 1s
     * become 2, so the next cycle avoids this cycle's positions.
     * --- 사용 표시를 앞으로 넘긴다 -----------------------------------------
     * `cand_used`는 세 값을 가집니다. 0은 비어 있음, 1은 이번 호출이 씀, 2는 직전
     * 사이클이 씀입니다. 선택이 끝난 뒤 2는 0으로 지우고 1은 2가 되어, 다음 사이클이
     * 이번 사이클의 자리를 피합니다. */
    for (int i = 0; i < b->n_cand; i++)
        if (b->cand_used[i] == 2)
            b->cand_used[i] = 0;
    for (int i = 0; i < b->n_cand; i++)
        if (b->cand_used[i] == 1)
            b->cand_used[i] = 2;

    return placed;
}

int enemy_boss_summon(Pools *pl, const Level *l)
{
    BossFight *b = &pl->enemy.boss;
    if (!b->have_maw)
        return 0;
    return make_monster(pl, l, MON_MAW, b->maw_pos.x, b->maw_pos.y, b->maw_pos.z);
}

void enemy_boss_heal(Pools *pl, int to)
{
    int i = enemy_boss_index(pl);
    if (i < 0)
        return;
    if (pl->enemy.m[i].health < to)
        pl->enemy.m[i].health = to;
}

int enemy_take_drop(Pools *pl, int idx, v3 *out_at)
{
    if (idx < 0 || idx >= pl->enemy.count)
        return -1;
    Enemy *m = &pl->enemy.m[idx];
    if (!m->active || m->drop < 0)
        return -1;

    int kind = m->drop;
    m->drop = -1; /* owed once; see the header */
    if (out_at)
        *out_at = m->pos; /* the body's feet, which is floor */
    return kind;
}

/* --- Static helper definitions / 정적 헬퍼 정의 --- */

/* --- The type table / 종류 표 --- */
/* Validates the type table and its attack slots at start-up, raising
   ::DIAG_MON_TABLE for every violated invariant.
   시작 시 종류 표와 그 공격 슬롯을 검사하며, 어긋난 불변식마다 ::DIAG_MON_TABLE을
   올립니다. */
static void types_check(void)
{
    for (int i = 0; i < MON_TYPES; i++)
    {
        /* The number of attack slots for this kind. The per-slot checks below
           require an ::ATK_BOLT slot to have `shot_speed > 0` and a swing slot
           to have none; the pairing is checked per attack, not per kind.
           이 종류의 공격 슬롯 수입니다. 아래의 슬롯별 검사는 ::ATK_BOLT 슬롯에
           `shot_speed > 0`을, 휘두르기 슬롯에는 속도가 없음을 요구합니다. 짝은
           종류가 아니라 공격마다 검사합니다. */
        int n_atk = mon_attack_count(i);

        /* An ::AI_INERT kind must have no attack slots and every other kind must
           have at least one.
           ::AI_INERT 종류는 공격 슬롯이 없어야 하고, 다른 모든 종류는 최소 하나를
           가져야 합니다. */
        if ((TYPES[i].behaviour == AI_INERT) != (n_atk == 0))
            DIAG(DIAG_MON_TABLE);

        for (int k = 0; k < n_atk; k++)
        {
            const MonAttack *A = &ATTACKS[i][k];

            if ((A->kind == ATK_BOLT) != (A->shot_speed > 0.0f))
                DIAG(DIAG_MON_TABLE);

            /* `min` must be non-negative and below `max`; a slot with `min >= max`
               is satisfied by no distance and is never offered.
               `min`은 음수가 아니고 `max`보다 작아야 합니다. `min >= max`인 슬롯은
               어떤 거리도 만족시키지 못해 결코 제안되지 않습니다. */
            if (A->min < 0.0f || A->min >= A->max)
                DIAG(DIAG_MON_TABLE);

            if (A->weight <= 0.0f)
                DIAG(DIAG_MON_TABLE);

            /* `burst` and `burst_min` must both be at least 1 with `burst_min <=
               burst`. ::begin_attack rolls the volley size in `burst_min..burst`,
               and an inverted or zero range fires nothing.
               `burst`와 `burst_min`은 둘 다 1 이상이고 `burst_min <= burst`여야 합니다.
               ::begin_attack은 일제 사격 크기를 `burst_min..burst`에서 굴리며, 뒤집힌
               범위나 0은 아무것도 쏘지 않습니다. */
            if (A->burst < 1 || A->burst_min < 1 || A->burst_min > A->burst)
                DIAG(DIAG_MON_TABLE);

            /* A single-shot slot (`burst == 1`) must have a `shot_gap` of 0, and
               `shot_gap` may never be negative. 0 on a multi-bolt slot means the
               bolts fire together.
               단발 슬롯(`burst == 1`)의 `shot_gap`은 0이어야 하고, `shot_gap`은 음수일
               수 없습니다. 여러 발인 슬롯의 0은 볼트를 함께 쏜다는 뜻입니다. */
            if (A->burst == 1 && A->shot_gap != 0.0f)
                DIAG(DIAG_MON_TABLE);
            if (A->shot_gap < 0.0f)
                DIAG(DIAG_MON_TABLE);
        }

        /* Every fighting kind must have at least one slot whose `min` is 0 or
           less, so some attack answers a player at contact. Gaps between bands
           are allowed; an ::AI_CASTER backs away through them.
           싸우는 모든 종류는 `min`이 0 이하인 슬롯을 최소 하나 가져야 하며, 그래야
           접촉한 플레이어에게 어떤 공격이든 답합니다. 대역 사이의 구멍은 허용됩니다.
           ::AI_CASTER는 그 구멍을 통해 물러납니다. */
        if (n_atk > 0)
        {
            int at_contact = 0;
            for (int k = 0; k < n_atk; k++)
                if (ATTACKS[i][k].min <= 0.0f)
                    at_contact = 1;
            if (!at_contact)
                DIAG(DIAG_MON_TABLE);
        }

        /* A ::MON_BOSS's `hp` must divide by ::BOSS_CYCLES. ::enemy_hurt clamps at
           `hp * (CYCLES - cycle - 1)/CYCLES`, and a remainder would leave the last
           boundary above zero, so the boss could never die.
           ::MON_BOSS의 `hp`는 ::BOSS_CYCLES로 나누어떨어져야 합니다. ::enemy_hurt는
           `hp * (CYCLES - cycle - 1)/CYCLES`에서 고정하는데, 나머지가 있으면 마지막
           경계가 0보다 커서 보스가 결코 죽을 수 없습니다. */
        if ((TYPES[i].flags & MON_BOSS) && (TYPES[i].hp % BOSS_CYCLES) != 0)
            DIAG(DIAG_MON_TABLE);
    }
}

/* Returns non-zero when the two strings are identical.
   두 문자열이 완전히 같으면 0이 아닌 값을 반환합니다. */
static int name_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return !*a && !*b;
}

/* The wait a spawner gets: `interval * (1 + ::EnemyPool::spawn_slow) /
 * ::EnemyPool::spawn_rate`, with a rate of 1.0 when `spawn_rate` is 0.01 or
 * less. Used by ::enemy_wave_arm and ::spawners_update; the suppression is read each call, never stored.
 * 스포너가 받는 대기 시간, `interval * (1 + ::EnemyPool::spawn_slow) /
 * ::EnemyPool::spawn_rate`입니다. `spawn_rate`가 0.01 이하이면 비율 1.0을 씁니다.
 * ::enemy_wave_arm과 ::spawners_update가 쓰며, 억제는 호출마다 읽을 뿐 저장하지 않습니다. */
static float spawn_wait(const EnemyPool *ep, float interval)
{
    float rate = ep->spawn_rate > 0.01f ? ep->spawn_rate : 1.0f;
    return interval * (1.0f + ep->spawn_slow) / rate;
}

/* --- Utilities / 보조 함수 --- */

/**
 * @brief Advances ::EnemyPool::rng and returns the next value in 0.0 to 1.0.
 * @param[in,out] e Pool whose generator state is advanced.
 * @return The next value, in 0.0 to 1.0; never 1.0 itself.
 * @brief ::EnemyPool::rng를 진행시키고 0.0에서 1.0 사이의 다음 값을 반환합니다.
 * @param[in,out] e 생성기 상태를 진행시킬 풀.
 * @return 0.0에서 1.0 사이의 다음 값이며, 1.0 자체는 나오지 않습니다.
 */
static float frand(EnemyPool *e)
{
    e->rng = e->rng * 1664525u + 1013904223u;
    return (e->rng >> 8) * (1.0f / 16777216.0f);
}

/**
 * @brief Plays `name` at `p` with base volume `base`, through ::audio_play_at.
 * @brief `name`을 `p` 위치에서 기본 음량 `base`로 ::audio_play_at을 통해 재생합니다.
 */
static void play_at(v3 p, const char *name, int base)
{
    audio_play_at(name, base, p);
}

/* --- Spawning / 생성 --- */
/**
 * @brief Puts one monster of `type` into the pool, on the floor under (`x`, `from_y`, `z`).
 * @return Non-zero when a monster was created; 0 for an invalid type, no floor, a hazard floor, or a full pool with no corpse to evict.
 * @brief `type` 종류의 몬스터 하나를 (`x`, `from_y`, `z`) 아래의 바닥 위에 풀에 넣습니다.
 * @return 몬스터가 만들어졌으면 0이 아닌 값이며, 잘못된 종류, 바닥 없음, 위험 바닥, 축출할 시체가 없는 가득 찬 풀이면 0입니다.
 */
static int make_monster(Pools *pl, const Level *l, int type,
                        float x, float from_y, float z)
{
    if (type < 0 || type >= MON_TYPES)
        return 0;

    float f, c;
    if (!level_ground(l, x, z, from_y, 1e9f, &f, &c))
        return 0;

    /* Refuses the spawn when ::floor_safe rejects the floor (a hazard such as
       lava), the same rule ::foot_ok applies to walking. Refused, not moved to
       a nearby safe column.
       ::floor_safe가 바닥을 거절하면(용암 같은 위험 바닥) 스폰을 거절합니다. ::foot_ok가
       보행에 적용하는 것과 같은 규칙입니다. 근처의 안전한 기둥으로 옮기지 않고
       거절합니다.
       *높이를 유지하는 종류는 예외입니다.* MON_FLIES와 MON_ANCHORED는 결코 바닥에 서지 않으며,
       이동 경로는 그것들에게 ::air_ok를 묻지 ::foot_ok를 묻지 않습니다. 그러므로 그 아래의
       위험은 그것들이 닿지 않는 바닥입니다. 출하 아레나에서 측정했습니다. 용암 바다 위에
       저작된 마녀 스포너 다섯이 평생 아무것도 배달하지 않았고, 같은 바다 위의 보스 표식은
       아귀를 아예 세울 수 없었습니다. 둘 다 이유를 말하는 것이 아무 데도 없이 "작동하지 않는
       스포너"로 읽힙니다.
       NOT FOR A KIND THAT HOLDS ITS HEIGHT: MON_FLIES and MON_ANCHORED never stand on the
       floor -- the movement path asks ::air_ok about them and never ::foot_ok -- so the hazard
       under them is a floor they do not touch. Measured on the shipped arena: five caster
       spawners authored out over the lava sea delivered nothing for their whole lives, and the
       boss marker over the same sea could not raise the maw at all. */
    if (!(TYPES[type].flags & (MON_FLIES | MON_ANCHORED)) && !floor_safe(l, x, f, z))
        return 0;

    /* Which slot the monster takes: the next one while the pool is below
     * ::ENEMY_MAX, otherwise the ::E_DEAD slot with the largest ::Enemy::anim (the
     * oldest corpse). Live monsters are never evicted; no corpse raises ::DIAG_ENEMY_CAP and refuses.
     * 몬스터가 쓸 칸입니다. 풀이 ::ENEMY_MAX 미만이면 다음 칸, 아니면 ::Enemy::anim이
     * 가장 큰 ::E_DEAD 칸(가장 오래된 시체)을 재사용합니다. 살아 있는 몬스터는 결코
     * 축출하지 않으며, 시체가 없으면 ::DIAG_ENEMY_CAP을 올리고 거절합니다.
     */
    Enemy *m;
    if (pl->enemy.count < ENEMY_MAX)
    {
        m = &pl->enemy.m[pl->enemy.count++];
    }
    else
    {
        int oldest_i = -1;
        float oldest_a = -1.0f;
        for (int i = 0; i < ENEMY_MAX; i++)
        {
            if (pl->enemy.m[i].state != E_DEAD)
                continue;
            if (TYPES[pl->enemy.m[i].type].flags & MON_COLLAPSES)
                continue; /* mid-sequence */
            if (pl->enemy.m[i].anim > oldest_a)
            {
                oldest_a = pl->enemy.m[i].anim;
                oldest_i = i;
            }
        }
        if (oldest_i < 0)
        {
            DIAG(DIAG_ENEMY_CAP);
            return 0;
        }
        m = &pl->enemy.m[oldest_i];
    }

    const MonType *S = &TYPES[type];
    Enemy zero = {0};
    *m = zero;
    m->type = (short)type;

    /* A ::MON_FLIES monster keeps the requested height instead of being placed on the floor, but never below it.
       ::MON_FLIES 몬스터는 바닥에 놓이는 대신 요청받은 높이를 유지하되, 결코 바닥 아래로는 가지 않습니다. */
    /* ::MON_ANCHORED is treated like ::MON_FLIES here: an anchored monster keeps the height it was placed at.
       ::MON_ANCHORED는 이곳에서 ::MON_FLIES와 같이 다뤄집니다. 고정된 몬스터는 놓인 높이를 유지합니다. */
    float y = f;
    if ((S->flags & (MON_FLIES | MON_ANCHORED)) && from_y > f)
        y = from_y;

    m->pos = v3f(x, y, z);
    /* THE WAVE LADDER, and never on a boss or a ward. ::step_boss reads the maw's cycle
       boundaries off ::MonType::hp -- the TABLE value -- so a scaled instance would cross
       them in the wrong places; and a ward's health is a whole multiple of
       ::WARD_SUMMON_DMG, which a multiplier does not preserve.
       *웨이브 사다리이며, 보스와 결계핵에는 결코 적용하지 않습니다.* ::step_boss는 아귀의
       사이클 경계를 ::MonType::hp, 즉 *표의* 값에서 읽으므로 배율이 적용된 개체는 엉뚱한
       곳에서 경계를 넘습니다. 그리고 결계핵의 체력은 ::WARD_SUMMON_DMG의 정수배인데
       배율은 그것을 보존하지 않습니다. */
    if (S->flags & (MON_BOSS | MON_GUARD))
        m->health = S->hp;
    else
        m->health = (int)((float)S->hp * pl->enemy.hp_mul + 0.5f);
    m->state = E_IDLE;
    m->active = 1;
    m->anim = frand(&pl->enemy) * 6.28f;
    m->drop = -1;
    /* Unowned unless a spawner claims it. `Enemy zero` would leave 0, which is
       spawner 0 -- a real index.
       스포너가 주장하지 않는 한 주인이 없습니다. `Enemy zero`는 0을 남기는데, 0은 실재하는
       색인인 0번 스포너입니다. */
    m->from_spawner = -1;
    /* ::Enemy::foe starts at -1, meaning no grudge target; 0 would be a valid monster index.
       ::Enemy::foe는 -1로 시작하며 원한 대상이 없음을 뜻합니다. 0은 유효한 몬스터 색인입니다. */
    /* ::Enemy::atk starts at -1, meaning no attack slot chosen; 0 is a valid slot that ::pick_attack assigns before ::E_ATTACK runs it.
       ::Enemy::atk는 -1로 시작하며 공격 슬롯이 선택되지 않았음을 뜻합니다. 0은 유효한 슬롯이며 ::E_ATTACK이 실행하기 전에 ::pick_attack이 지정합니다. */
    m->atk = -1;
    m->foe = -1;
    m->foe_time = 0.0f;
    m->sight_age = (short)((pl->enemy.count - 1) % SIGHT_PERIOD);
    return (int)(m - pl->enemy.m) + 1; /* the slot, plus one; see the declaration */
}

/**
 * @brief Reads the level's `spawner_*` markers into the pool; the name after `spawner_` is the monster type.
 *
 * @brief 레벨의 `spawner_*` 표식을 풀로 읽어들입니다. `spawner_` 뒤의 이름이 몬스터 종류입니다.
 */
static void spawners_of(Pools *pl, const Level *l)
{
    pl->enemy.n_spawners = 0;

    for (int i = 0; i < l->n_ents; i++)
    {
        const Entity *e = &l->ents[i];

        /* Matches the "spawner_" prefix; the rest of the kind is the monster name.
           "spawner_" 접두사를 맞추고, 종류 이름의 나머지가 몬스터 이름입니다. */
        static const char PRE[] = "spawner_";
        int n = 0;
        while (PRE[n] && e->kind[n] == PRE[n])
            n++;
        if (PRE[n] || !e->kind[n])
            continue;

        int type = mon_type_for(e->kind + n);
        if (type < 0)
            continue;

        if (pl->enemy.n_spawners >= ENEMY_MAX_SPAWNERS)
        {
            DIAG(DIAG_ENEMY_CAP);
            continue;
        }
        Spawner *s = &pl->enemy.spawner[pl->enemy.n_spawners++];

        s->pos = v3f(e->x * 0.01f, e->y * 0.01f, e->z * 0.01f);
        s->type = (short)type;
        s->left = e->p[1] > 0 ? e->p[1] : -1; /* 0 authored means unlimited / 작성값 0은 무제한 */
        s->max_alive = e->p[2];
        s->base_alive = s->max_alive;
        s->interval = e->p[0] > 0 ? e->p[0] * 0.1f : 5.0f;
        s->base_interval = s->interval;
        s->burst = 1;
        s->warn = 0.0f;
        /* The first group is due after one full interval, not on the frame the level loads.
           첫 무리는 레벨이 로드되는 프레임이 아니라 온전한 한 주기 뒤에 나옵니다. */
        s->timer = s->interval;
        s->active = 1;
    }
}

/* Whether the player is within ::SPAWN_MIN_DIST of the spawner, measured on the floor plane only.
   플레이어가 스포너로부터 ::SPAWN_MIN_DIST 안에 있는지를 바닥 평면에서만 잽니다. */
static int spawner_crowded(const Spawner *s, v3 player_eye)
{
    float dx = player_eye.x - s->pos.x;
    float dz = player_eye.z - s->pos.z;
    return dx * dx + dz * dz < SPAWN_MIN_DIST * SPAWN_MIN_DIST;
}

/**
 * @brief Advances every spawner by one frame: runs the warning telegraph, then places the group.
 * @param[in,out] pl         Pools whose spawners to run.
 * @param[in]     l          Level the new monsters are placed in.
 * @param[in]     player_eye Player position, for the ::spawner_crowded test.
 * @param[in]     dt         Seconds since the last frame.
 * @return Non-zero when at least one monster was made this frame.
 *
 * @brief 모든 스포너를 한 프레임 진행시킵니다. 예고를 돌린 뒤 무리를 배치합니다.
 * @param[in,out] pl         돌릴 스포너를 가진 풀.
 * @param[in]     l          새 몬스터가 놓일 레벨.
 * @param[in]     player_eye ::spawner_crowded 판정을 위한 플레이어 위치.
 * @param[in]     dt         지난 프레임 이후 경과 시간(초).
 * @return 이번 프레임에 몬스터가 하나라도 만들어졌으면 0이 아닙니다.
 */
static int spawners_update(Pools *pl, const Level *l, v3 player_eye, float dt)
{
    int made = 0;

    /* The lull counts down here and holds every timer below; a telegraph already running
       still delivers, because a warning that was shown must be kept.
       휴지기는 이곳에서 감소하며 아래의 모든 타이머를 멈춥니다. 이미 진행 중인 예고는 그대로
       배달합니다. 보여 준 경고는 지켜야 하기 때문입니다. */
    if (pl->enemy.lull > 0.0f)
        pl->enemy.lull -= dt;

    for (int i = 0; i < pl->enemy.n_spawners; i++)
    {
        Spawner *s = &pl->enemy.spawner[i];
        if (!s->active)
            continue;

        /* --- the telegraph ---
           A spawner in its warning only counts the warning down; the interval timer does not run, and the budget is spent when the group is placed, not when the warning was raised.
           예고 중인 스포너는 예고만 세어 내려갑니다. 주기 타이머는 돌지 않고, 예산은 예고를 올릴 때가 아니라 무리를 배치할 때 씁니다. */
        if (s->warn > 0.0f)
        {
            s->warn -= dt;
            if (s->warn > 0.0f)
                continue;
            s->warn = 0.0f;

            /* Per-spawner flag: set only when this spawner placed a monster, unlike `made`, which covers the whole loop.
               스포너별 플래그입니다. 이 스포너가 몬스터를 배치했을 때만 설정되며, 루프 전체를 아우르는 `made`와 다릅니다. */
            int arrived = 0;

            int n = s->burst > 0 ? s->burst : 1;
            for (int k = 0; k < n; k++)
            {
                if (s->left == 0)
                    break;
                if (s->max_alive > 0 && enemy_alive_from(pl, i) >= s->max_alive)
                    break;

                /* Also stops when this kind is already at its ::MonType::cap; the budget is not spent.
                   이 종류가 이미 ::MonType::cap에 이르렀을 때도 멈추며, 예산은 쓰지 않습니다. */
                {
                    const MonType *ST = mon_stats(s->type);
                    if (ST->cap > 0 && enemy_alive_of(pl, s->type) >= ST->cap)
                        break;
                }

                int slot = make_monster(pl, l, s->type, s->pos.x, s->pos.y, s->pos.z);
                if (!slot)
                {
                    /* When ::make_monster refuses: a room full of living monsters keeps the budget and retries later;
                       any other refusal costs one of the budget, so the spawner runs down and ::enemy_wave_done can complete.
                       ::make_monster가 거절했을 때, 살아 있는 몬스터로 가득한 방이면 예산을 유지하고 나중에 다시 시도합니다.
                       그 밖의 거절은 예산 하나를 치러 스포너가 소진되고 ::enemy_wave_done이 완료될 수 있게 합니다. */
                    if (enemy_alive(pl) < ENEMY_MAX && s->left > 0)
                        s->left--;
                    break;
                }

                /* CLAIMED, so this spawner's ceiling counts its own.
                 *주장합니다.* 그래야 이 스포너의 상한이 자기 것을 셉니다. */
                pl->enemy.m[slot - 1].from_spawner = (short)i;
                made = 1;
                arrived = 1;
                if (s->left > 0)
                    s->left--;
            }

            /* --- the portal discharging ---
               Fires once per group, not once per monster, and only when `arrived` is set; a telegraph that placed nothing stays silent.
               무리마다 한 번이며 몬스터마다가 아니고, `arrived`가 설정되었을 때만 발생합니다. 아무것도 배치하지 못한 예고는 조용히 끝납니다. */
            if (arrived)
            {
                fx_spawn(pl, "spawnburst", s->pos, v3f(0.0f, 1.0f, 0.0f));
                play_at(s->pos, "spawnpop", 85);
            }
            continue;
        }

        /* Retired only after the telegraph above has run, so a spawner that spent its last budget still delivers the group it warned about.
           위의 예고가 돌고 난 뒤에만 은퇴시키므로, 예산의 마지막을 쓴 스포너도 예고한 무리를 배달합니다. */
        if (s->left == 0)
        {
            s->active = 0;
            continue;
        }

        if (pl->enemy.lull > 0.0f)
            continue;

        s->timer -= dt;
        if (s->timer > 0.0f)
            continue;

        /* The alive ceiling is checked after the timer expires, so a spawner held back by its OWN crowd fires as soon as there is room instead of waiting another interval.
           생존 상한은 타이머가 만료된 뒤에 검사하므로, *자기* 무리에 막힌 스포너는 또 한 주기를 기다리지 않고 자리가 나는 즉시 발동합니다. */
        if (s->max_alive > 0 && enemy_alive_from(pl, i) >= s->max_alive)
            continue;

        /* A player standing on the spawner postpones it: the timer is not reset and the budget is untouched, so it fires the frame they step away.
           플레이어가 스포너 위에 서 있으면 미뤄집니다. 타이머를 초기화하지 않고 예산도 건드리지 않으므로, 물러나는 프레임에 발동합니다. */
        if (spawner_crowded(s, player_eye))
            continue;

        s->timer = spawn_wait(&pl->enemy, s->interval);
        s->warn = SPAWN_WARN_TIME;

        /* --- the telegraph ---
           Two effects at the spawner's feet, pointed up: `spawnwarp` is the column that shows when, `spawnring` is the ring that shows where.
           스포너 발치에 위를 향한 이펙트 둘입니다. `spawnwarp` 기둥은 언제를, `spawnring` 고리는 어디를 보여 줍니다. */
        fx_spawn(pl, "spawnwarp", s->pos, v3f(0.0f, 1.0f, 0.0f));
        fx_spawn(pl, "spawnring", s->pos, v3f(0.0f, 1.0f, 0.0f));

        /* The warning sound is played quieter than the arrival sound below.
           예고음은 아래의 도착음보다 작게 재생합니다. */
        play_at(s->pos, "spawnwarn", 70);
    }
    return made;
}

/* --- Projectiles / 발사체 --- */

/**
 * @brief Launches one monster projectile from `from` towards `at`; the velocity is fixed at launch.
 * @param[in,out] pl     Pools holding the shot ring.
 * @param[in]     from   Muzzle position, in world metres.
 * @param[in]     at     Point to aim at.
 * @param[in]     speed  Projectile speed, m/s.
 * @param[in]     damage Damage the shot deals on impact.
 * @param[in]     type   ::MonTypeID of the caster; stored in ::Shot::type for the renderer.
 * @param[in]     owner  Pool index of the caster; stored in ::Shot::owner.
 * @note Does nothing when every shot slot is in use (raises ::DIAG_SHOT_CAP) or when `at` equals `from`.
 *
 * @brief 몬스터 발사체 하나를 `from`에서 `at`을 향해 발사합니다. 속도는 발사 시점에 고정됩니다.
 * @param[in,out] pl     발사체 링을 담고 있는 풀.
 * @param[in]     from   총구 위치. 월드 미터 단위입니다.
 * @param[in]     at     조준할 지점.
 * @param[in]     speed  발사체 속도(m/s).
 * @param[in]     damage 명중 시 피해량.
 * @param[in]     type   시전한 생물의 ::MonTypeID. 렌더러를 위해 ::Shot::type에 저장됩니다.
 * @param[in]     owner  시전자의 풀 색인. ::Shot::owner에 저장됩니다.
 * @note 모든 발사체 슬롯이 사용 중이면(::DIAG_SHOT_CAP을 올림) 또는 `at`이 `from`과 같으면 아무것도 하지 않습니다.
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
    /* Raises ::DIAG_SHOT_CAP and drops the bolt when no shot slot is free.
       빈 발사체 슬롯이 없으면 ::DIAG_SHOT_CAP을 올리고 볼트를 버립니다. */
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
    s->owner = (short)owner;
    /* ::Shot::type is read only by the renderer; it is set here because this is the one place a Shot is created.
       ::Shot::type은 렌더러만 읽습니다. Shot이 생겨나는 유일한 곳이므로 이곳에서 설정합니다. */
    s->type = type;
    /* Zero so the first trail particle is laid on the frame the bolt appears, at the muzzle.
       첫 궤적 파티클이 볼트가 나타나는 프레임에 총구에서 놓이도록 0으로 둡니다. */
    s->trail_timer = 0.0f;
}

/**
 * @brief Advances every projectile, resolves hits on the level, monsters and the player, and returns the damage dealt to the player.
 * @param[in,out] pl         Pools holding the shots.
 * @param[in]     l          Level the shots collide against.
 * @param[in]     player_eye Player eye position; the player is tested as a cylinder of ::PLAYER_RADIUS and ::PLAYER_EYE around it.
 * @param[in]     dt         Seconds since the last frame.
 * @return Total damage the shots dealt to the player this frame; nothing here applies it.
 *
 * @brief 모든 발사체를 진행시키고 레벨, 몬스터, 플레이어에 대한 명중을 처리하며 플레이어가 입은 피해를 반환합니다.
 * @param[in,out] pl         발사체를 담고 있는 풀.
 * @param[in]     l          발사체가 충돌하는 레벨.
 * @param[in]     player_eye 플레이어 눈 위치. 플레이어는 그 둘레의 ::PLAYER_RADIUS, ::PLAYER_EYE 원기둥으로 판정합니다.
 * @param[in]     dt         지난 프레임 이후 경과 시간(초).
 * @return 이번 프레임에 발사체가 플레이어에게 입힌 총 피해량. 이곳에서는 적용하지 않습니다.
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

        /* Trail particles are laid every SHOT_TRAIL_INTERVAL seconds, not every frame, so spacing along the path is independent of frame rate.
           궤적 파티클은 프레임마다가 아니라 SHOT_TRAIL_INTERVAL초마다 놓으므로 경로상의 간격이 프레임률과 무관합니다. */
        s->trail_timer -= dt;
        if (s->trail_timer <= 0.0f)
        {
            s->trail_timer = SHOT_TRAIL_INTERVAL;
            /* Emitted backward along the flight so the trail's spread drifts behind the bolt.
               비행 방향의 반대로 방출하여 궤적의 산포가 볼트 뒤로 흩어지게 합니다. */
            fx_spawn(pl, "bolttrail", s->pos, v3scale(dir, -1.0f));

            /* `boltwake` is a second, longer-lived layer with the opposite blend mode, laid on the same timer as `bolttrail`.
               `boltwake`는 `bolttrail`과 같은 타이머로 놓이는, 블렌드 모드가 반대이고 더 오래가는 두 번째 겹입니다. */
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

        /* Tests the bolt against every living monster except ::Shot::owner; the nearest hit wins and is compared against the wall and the player on the same terms.
           ::Shot::owner를 제외한 살아 있는 모든 몬스터에 대해 볼트를 검사합니다. 가장 가까운 명중이 이기며, 벽과 플레이어와 같은 조건으로 비교합니다. */
        int mon_i = -1;
        float mon_t = -1.0f;
        for (int j = 0; j < pl->enemy.count; j++)
        {
            if (j == s->owner)
                continue;
            const Enemy *o = &pl->enemy.m[j];
            if (!o->active || o->state == E_DEAD)
                continue;

            /* A bolt passes through monsters of its own type; same-kind fire never lands.
               볼트는 자기 종류의 몬스터를 통과합니다. 같은 종류의 사격은 결코 꽂히지 않습니다. */
            if (o->type == s->type)
                continue;

            const MonType *OS = &TYPES[o->type];
            v3 mid = v3f(o->pos.x, o->pos.y + OS->height * 0.5f, o->pos.z);
            v3 rel = v3sub(mid, s->pos);
            float along = v3dot(rel, dir);
            if (along < 0.0f || along > dist)
                continue;

            v3 near_p = v3add(s->pos, v3scale(dir, along));
            float dx = near_p.x - mid.x, dy = near_p.y - mid.y, dz = near_p.z - mid.z;
            float r = OS->radius + SHOT_RADIUS;
            float hy = OS->height * 0.5f + SHOT_RADIUS;
            if (dx * dx + dz * dz > r * r || dy * dy > hy * hy)
                continue;

            if (mon_i < 0 || along < mon_t)
            {
                mon_i = j;
                mon_t = along;
            }
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
            /* Burst effects face back along the bolt's travel, off the thing it struck.
               버스트 이펙트는 볼트 진행 방향의 반대, 맞은 대상에서 튀어나오는 쪽을 향합니다. */
            fx_spawn(pl, "boltburst", s->pos, v3scale(dir, -1.0f));
            fx_spawn(pl, "boltshard", s->pos, v3scale(dir, -1.0f));

            /* A hit on the player gets the burst and flash only; the scorch and dust below are for walls.
               플레이어 명중은 버스트와 섬광만 냅니다. 아래의 그을음과 먼지는 벽을 위한 것입니다. */
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

            /* A wall hit adds a `scorch` mark and the `smokepuff` shared with the shotgun at the impact point.
               벽 명중은 충돌 지점에 `scorch` 자국과 샷건과 공유하는 `smokepuff`를 더합니다. */
            fx_spawn(pl, "scorch", s->pos, n);
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
 * @brief How high a ledge this kind walks straight up: a third of its height, never less than ::PLAYER_STEP.
 * @param[in] S The monster's kind.
 * @return The step height in metres, at least ::PLAYER_STEP.
 *
 * @brief 이 종류가 그대로 걸어 올라가는 턱의 높이. 신장의 3분의 1이며 ::PLAYER_STEP보다 낮지 않습니다.
 * @param[in] S 몬스터의 종류.
 * @return 단차 높이(미터). 최소 ::PLAYER_STEP입니다.
 */
static float mon_step(const MonType *S)
{
    float own = S->height / 3.0f;
    return own > PLAYER_STEP ? own : PLAYER_STEP;
}

/**
 * @brief Whether a monster may put its feet on the floor found at this column: true unless ::level_hazard_at reports lava at the floor point.
 * @param[in] l   The level.
 * @param[in] x,z Where.
 * @param[in] f   The floor height ::level_ground found there; the hazard is asked at this height, not at the monster's middle.
 * @return 1 if standing there is survivable, 0 if it is lava.
 *
 * @brief 몬스터가 이 기둥에서 찾은 바닥에 발을 놓아도 되는가. ::level_hazard_at이 바닥 지점에서 용암을 보고하지 않으면 참입니다.
 * @param[in] l   레벨.
 * @param[in] x,z 위치.
 * @param[in] f   ::level_ground가 그곳에서 찾은 바닥 높이. 위험 판정은 몬스터의 한가운데가 아니라 이 높이에서 묻습니다.
 * @return 서도 살 수 있으면 1, 용암이면 0.
 */
static int floor_safe(const Level *l, float x, float f, float z)
{
    return level_hazard_at(l, x, f, z) <= 0;
}

/**
 * @brief Whether this kind of monster can stand at a column: ground within ::mon_step, ::MonType::height of headroom and a ::floor_safe floor at all five samples of its radius.
 * @param[in]  l     Level to test against.
 * @param[in]  S     The monster's kind, for its height and radius.
 * @param[in]  x     Column x.
 * @param[in]  z     Column z.
 * @param[in]  feet  The monster's current foot height; the step is measured from it.
 * @param[out] floor Floor height it would stand on. Written only on success.
 * @return 1 if it fits and can reach the floor, 0 otherwise.
 *
 * @brief 이 종류의 몬스터가 어떤 기둥에 설 수 있는가. 반지름의 다섯 표본 모두에서 ::mon_step 안의 바닥, ::MonType::height만큼의 머리 공간, ::floor_safe인 바닥이 있어야 합니다.
 * @param[in]  l     판정할 레벨.
 * @param[in]  S     몬스터의 종류. 신장과 반지름을 위해 필요합니다.
 * @param[in]  x     기둥의 x.
 * @param[in]  z     기둥의 z.
 * @param[in]  feet  몬스터의 현재 발 높이. 단차는 이것을 기준으로 잽니다.
 * @param[out] floor 서게 될 바닥 높이. 성공했을 때만 기록됩니다.
 * @return 들어맞고 바닥에 닿을 수 있으면 1, 아니면 0입니다.
 */
static int foot_ok(const Level *l, const MonType *S, float x, float z,
                   float feet, float *floor)
{
    /* Five samples -- the centre and four points at ::MonType::radius -- the same set ::can_stand uses, so the whole body is kept out of the geometry.
       다섯 표본, 즉 중심과 ::MonType::radius 거리의 네 점이며 ::can_stand가 쓰는 것과 같은 집합입니다. 그래서 몸 전체가 지오메트리 밖에 유지됩니다. */
    static const float OX[5] = {0.0f, 1.0f, -1.0f, 0.0f, 0.0f};
    static const float OZ[5] = {0.0f, 0.0f, 0.0f, 1.0f, -1.0f};

    float step = mon_step(S);
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

    /* The floor is the highest sample, not the centre's, so a monster on the edge of a step is not sunk into it; every sample already passed the same step limit.
       바닥은 중심이 아니라 가장 높은 표본입니다. 단의 가장자리에 선 몬스터가 그 안으로 가라앉지 않게 하며, 모든 표본이 이미 같은 단차 상한을 통과했습니다. */
    *floor = highest;
    return 1;
}

/**
 * @brief Whether a flyer may occupy a column at the height it is already at: at all five samples the gap must be open there for ::MonType::height.
 * ::level_ground is given a large step limit instead of ::mon_step, so a floor that drops away below does not refuse the column.
 *
 * @brief 비행체가 지금 있는 높이에서 어떤 기둥을 차지해도 되는가. 다섯 표본 모두에서 그 높이의 빈 공간이 ::MonType::height만큼 열려 있어야 합니다.
 * ::level_ground에 ::mon_step 대신 큰 단차 상한을 넘겨, 아래의 바닥이 떨어져도 그 기둥을 거부하지 않게 합니다.
 */
static int air_ok(const Level *l, const MonType *S, float x, float z, float y)
{
    /* The same five samples ::foot_ok takes, so a hovering body is kept out of walls by its radius too.
       ::foot_ok가 취하는 것과 같은 다섯 표본이며, 떠 있는 몸도 자기 반지름만큼 벽 밖에 유지됩니다. */
    static const float OX[5] = {0.0f, 1.0f, -1.0f, 0.0f, 0.0f};
    static const float OZ[5] = {0.0f, 0.0f, 0.0f, 1.0f, -1.0f};

    for (int i = 0; i < 5; i++)
    {
        float f, c;
        if (!level_ground(l, x + OX[i] * S->radius, z + OZ[i] * S->radius,
                          y, 1e9f, &f, &c))
            return 0;
        if (y < f)
            return 0; /* inside the floor / 바닥 안 */
        if (y + S->height > c)
            return 0; /* head through the ceiling / 머리가 천장을 뚫음 */
    }
    return 1;
}

/**
 * @brief Whether a monster keeps its height instead of falling: ::MON_ANCHORED always, ::MON_FLIES only while not ::E_DEAD.
 * @param[in] S The monster's kind, for its flags.
 * @param[in] m The monster, for ::Enemy::state.
 * @return 1 if it keeps the height it is at, 0 if gravity has it.
 *
 * @brief 몬스터가 떨어지는 대신 높이를 유지하는가. ::MON_ANCHORED는 항상, ::MON_FLIES는 ::E_DEAD가 아닐 때만 유지합니다.
 * @param[in] S 몬스터의 종류. 플래그를 얻기 위함입니다.
 * @param[in] m 몬스터. ::Enemy::state를 얻기 위함입니다.
 * @return 지금 높이를 유지하면 1, 중력이 잡고 있으면 0.
 */
static int holds_height(const MonType *S, const Enemy *m)
{
    if (S->flags & MON_ANCHORED)
        return 1;
    return (S->flags & MON_FLIES) != 0 && m->state != E_DEAD;
}

/**
 * @brief Pulls a monster down to the floor under it by one frame of gravity; leaves it in place when ::level_ground finds no floor.
 * @param[in]     l  Level to ask for the floor.
 * @param[in]     S  The monster's kind, for its ::mon_step limit.
 * @param[in,out] m  Monster to pull down. Position and vertical velocity change.
 * @param[in]     dt Timestep in seconds.
 * @note Runs for the living and for corpses alike; the caller decides who is exempt with ::holds_height.
 *
 * @brief 몬스터를 그 아래의 바닥으로 한 프레임만큼의 중력으로 끌어내립니다. ::level_ground가 바닥을 찾지 못하면 그대로 둡니다.
 * @param[in]     l  바닥을 물어볼 레벨.
 * @param[in]     S  몬스터의 종류. ::mon_step 상한을 얻기 위함입니다.
 * @param[in,out] m  끌어내릴 몬스터. 위치와 수직 속도가 바뀝니다.
 * @param[in]     dt 시간 간격(초).
 * @note 살아 있는 것과 시체 모두에 실행됩니다. 누가 면제인지는 호출자가 ::holds_height로 정합니다.
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
 * @brief Moves a monster by (dx, dz), sliding: a blocked diagonal is retried along each axis alone.
 *
 * Every step must pass the level test and ::mon_clear. A ::MON_FLIES monster keeps its height through
 * ::air_ok; anything else rises onto a higher floor ::foot_ok finds and never steps down.
 *
 * @param[in]     l  Level to collide against.
 * @param[in]     S  The monster's kind.
 * @param[in,out] m  Monster to move. Its position and floor height are updated.
 * @param[in]     dx Requested movement along x, in metres.
 * @param[in]     dz Requested movement along z, in metres.
 *
 * @brief 몬스터를 (dx, dz)만큼 이동시키며, 막힌 대각선 이동은 각 축을 따로 다시 시도합니다.
 *
 * 모든 걸음은 레벨 검사와 ::mon_clear를 통과해야 합니다. ::MON_FLIES 몬스터는 ::air_ok로 높이를
 * 유지하고, 그 밖에는 ::foot_ok가 찾은 더 높은 바닥 위로 올라서기만 하며 내려서지는 않습니다.
 *
 * @param[in]     l  충돌 판정할 레벨.
 * @param[in]     S  몬스터의 종류.
 * @param[in,out] m  이동시킬 몬스터. 위치와 바닥 높이가 갱신됩니다.
 * @param[in]     dx x축으로 요청된 이동량(미터).
 * @param[in]     dz z축으로 요청된 이동량(미터).
 */
/**
 * Returns 1 if `self` can stand at (x, z) with its feet at `y` without its cylinder (::MonType::radius,
 * ::MonType::height) overlapping the player or another live monster; ::E_DEAD is ignored, and a pair
 * already overlapping at `self`'s current position is skipped here and left to ::separate_monsters.
 *
 * `self`가 (x, z)에 발 높이 `y`로 설 때 그 원기둥(::MonType::radius, ::MonType::height)이 플레이어나
 * 다른 살아 있는 몬스터와 겹치지 않으면 1을 돌려줍니다. ::E_DEAD는 무시하며, `self`의 현재 위치에서
 * 이미 겹쳐 있는 쌍은 여기서 건너뛰고 ::separate_monsters에 맡깁니다.
 */
static int mon_clear(const Pools *pl, const MonType *S, const Enemy *self,
                     v3 player_eye, float x, float z, float y)
{
    /* The player is one of the cylinders: the step is refused if it would enter the player's
       cylinder (::PLAYER_RADIUS wide, ::PLAYER_EYE tall) from outside it. Already overlapping the
       player is not refused, so a monster inside the player can walk out.
       플레이어도 원기둥 중 하나입니다. 걸음이 플레이어의 원기둥(반경 ::PLAYER_RADIUS, 높이
       ::PLAYER_EYE) 바깥에서 안으로 들어가면 거절합니다. 이미 플레이어와 겹쳐 있는 경우는 거절하지
       않으므로, 플레이어 안에 있는 몬스터는 걸어 나올 수 있습니다. */
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
            continue; /* already overlapping: skipped / 이미 겹쳐 있음: 건너뜀 */

        float dx = x - o->pos.x, dz = z - o->pos.z;
        if (dx * dx + dz * dz < r * r)
            return 0;
    }
    return 1;
}

static void move_toward(const Pools *pl, const Level *l, const MonType *S,
                        Enemy *m, v3 player_eye, float dx, float dz)
{
    /* A ::MON_ANCHORED monster refuses every step, whichever caller asks for it.
       ::MON_ANCHORED 몬스터는 어느 호출자가 요청하든 모든 걸음을 거절합니다. */
    if (S->flags & MON_ANCHORED)
        return;

    /* A ::MON_FLIES monster keeps its height through a move: each step is tested with ::air_ok at
       the current `m->pos.y`, and the height is never changed.
       ::MON_FLIES 몬스터는 이동 중 고도를 유지합니다. 각 걸음을 현재 `m->pos.y`에서 ::air_ok로
       검사하며 높이는 바꾸지 않습니다. */
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

    /* The monster test uses the height the step would end at, not `m->pos.y`: the cylinder asked
       about is the one occupied after the step.
       몬스터 검사는 `m->pos.y`가 아니라 걸음이 끝날 높이를 씁니다. 묻는 원기둥은 걸음 뒤에
       차지할 것입니다. */
    /* The step rises onto a higher floor and never descends onto a lower one: `ny` is the larger
       of `f` and `m->pos.y`, and drops are left to ::monster_fall. ::mon_clear is tested at `ny`,
       the height the monster will actually occupy.
       걸음은 더 높은 바닥 위로 올라서기만 하고 낮은 바닥으로 내려서지는 않습니다. `ny`는 `f`와
       `m->pos.y` 중 큰 쪽이며 낙하는 ::monster_fall에 맡깁니다. ::mon_clear는 몬스터가 실제로
       차지할 높이인 `ny`에서 검사합니다. */
    float f, ny;
    if (foot_ok(l, S, m->pos.x + dx, m->pos.z + dz, m->pos.y, &f) &&
        (ny = f > m->pos.y ? f : m->pos.y,
         mon_clear(pl, S, m, player_eye, m->pos.x + dx, m->pos.z + dz, ny)))
    {
        m->pos.x += dx;
        m->pos.z += dz;
        m->pos.y = ny;
        return;
    }
    if (foot_ok(l, S, m->pos.x + dx, m->pos.z, m->pos.y, &f) &&
        (ny = f > m->pos.y ? f : m->pos.y,
         mon_clear(pl, S, m, player_eye, m->pos.x + dx, m->pos.z, ny)))
    {
        m->pos.x += dx;
        m->pos.y = ny;
        return;
    }
    if (foot_ok(l, S, m->pos.x, m->pos.z + dz, m->pos.y, &f) &&
        (ny = f > m->pos.y ? f : m->pos.y,
         mon_clear(pl, S, m, player_eye, m->pos.x, m->pos.z + dz, ny)))
    {
        m->pos.z += dz;
        m->pos.y = ny;
    }
}

/* Turns `m->yaw` towards `m->ideal_yaw` the short way round, by at most `yaw_speed_deg` degrees
   per second scaled by `dt`, and keeps the result within [-pi, pi].
   `m->yaw`를 `m->ideal_yaw` 쪽으로 가까운 방향으로, 초당 최대 `yaw_speed_deg`도에 `dt`를 곱한
   만큼 돌리고, 결과를 [-pi, pi] 안에 유지합니다. */
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

/* Circles the player: moves at right angles to the facing on the committed side, and flips to the
   other side when the move is blocked. The side is held for ::MON_SLIDE_HOLD rather than re-picked
   every frame.
   플레이어 주위를 돕니다. 정해 둔 쪽으로 바라보는 방향의 직각으로 움직이고, 이동이 막히면
   반대쪽으로 뒤집습니다. 방향은 매 프레임 다시 고르지 않고 ::MON_SLIDE_HOLD 동안 유지합니다. */
/* Returns the side this monster is committed to, +1 for left and -1 for right. When `slide_wait`
   has run out a new side is drawn from ::EnemyPool::rng and held for ::MON_SLIDE_HOLD; the weave and
   the strafe both read it here.
   이 몬스터가 정해 둔 쪽을 돌려줍니다. 왼쪽이면 +1, 오른쪽이면 -1입니다. `slide_wait`가 다 되면
   ::EnemyPool::rng에서 새 쪽을 뽑아 ::MON_SLIDE_HOLD 동안 유지하며, 갈지자와 횡이동이 모두 이곳에서
   읽습니다. */
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

    /* The strafe direction is perpendicular to the monster's facing `m->yaw`, not to the direction
       of the player.
       횡이동 방향은 플레이어 방향이 아니라 몬스터가 바라보는 `m->yaw`의 직각입니다. */
    float side = m->lefty ? (m->yaw + M_PI_F * 0.5f) : (m->yaw - M_PI_F * 0.5f);
    float dx = -sinf(side) * step;
    float dz = -cosf(side) * step;

    float before_x = m->pos.x, before_z = m->pos.z;
    move_toward(pl, l, S, m, player_eye, dx, dz);

    /* If the move covered less than a quarter of `step`, flip `lefty` and restart the hold.
       이동량이 `step`의 4분의 1에 못 미치면 `lefty`를 뒤집고 유지 시간을 다시 시작합니다. */
    float moved = fabsf(m->pos.x - before_x) + fabsf(m->pos.z - before_z);
    if (moved < step * 0.25f)
    {
        m->lefty = (char)!m->lefty;
        m->slide_wait = MON_SLIDE_HOLD(S->speed);
    }
}

/* --- Perception and decision / 지각과 판단 --- */

/**
 * @brief Whether the line from the monster's eye (::MonType::eye above its feet) to the player's eye is unobstructed.
 * @param[in] l          Level whose geometry may block the line.
 * @param[in] m          The monster looking.
 * @param[in] player_eye Where the player's eye is.
 * @return 1 if the line is clear, 0 if anything blocks it.
 * @note Traces live on every call; use ::sees_player where a cached answer is acceptable.
 *
 * @brief 몬스터의 눈(발에서 ::MonType::eye 위)에서 플레이어의 눈까지의 직선이 막혀 있지 않은가.
 * @param[in] l          그 직선을 막을 수 있는 지형을 가진 레벨.
 * @param[in] m          바라보는 몬스터.
 * @param[in] player_eye 플레이어의 눈 위치.
 * @return 직선이 트여 있으면 1, 무엇이든 막고 있으면 0입니다.
 * @note 호출마다 실시간으로 판정합니다. 캐시된 답으로 충분한 곳에서는 ::sees_player를 쓰십시오.
 */
static int can_see(const Level *l, const Enemy *m, v3 player_eye)
{
    v3 eye = v3f(m->pos.x, m->pos.y + mon_stats(m->type)->eye, m->pos.z);
    v3 d = v3sub(player_eye, eye);
    float dist = v3len(d);
    if (dist < 0.001f)
        return 1;
    d = v3scale(d, 1.0f / dist);

    /* ::level_blocked only asks whether the line is clear. The trace stops 0.05 short of the
       player, so anything solid within that range counts as blocking.
       ::level_blocked는 선이 뚫려 있는지만 묻습니다. 판정은 플레이어에 0.05 못 미치는 곳에서
       멈추므로, 그 범위 안의 막는 것은 모두 막는 것으로 계산됩니다. */
    return !level_blocked(l, eye, d, dist - 0.05f);
}

/**
 * @brief ::can_see, answered from a cache refreshed every ::SIGHT_PERIOD frames.
 *
 * Returns 0 while ::EnemyPool::blinded is set. Otherwise refreshes `m->seen` from ::can_see when
 * `m->sight_age` has run out and returns the cached value; `seen` starts at 0, so a newly spawned
 * monster reports not seeing until its first refresh.
 * @note Not for the moment a bolt is released; ::release_bolt calls ::can_see directly.
 *
 * @brief ::SIGHT_PERIOD 프레임마다 갱신되는 캐시로 답하는 ::can_see입니다.
 *
 * ::EnemyPool::blinded가 켜져 있으면 0을 돌려줍니다. 그 밖에는 `m->sight_age`가 다 되었을 때
 * ::can_see로 `m->seen`을 갱신하고 캐시된 값을 돌려줍니다. `seen`은 0에서 시작하므로 갓 생성된
 * 몬스터는 첫 갱신까지 보지 못한다고 답합니다.
 * @note 볼트를 발사하는 순간에는 쓰지 않습니다. ::release_bolt는 ::can_see를 직접 호출합니다.
 */
static int sees_player(const Pools *pl, const Level *l, Enemy *m, v3 player_eye)
{
    /* ::EnemyPool::blinded (::PW_SHADOW) is answered here and never cached, so the answer follows
       the powerup on the frame it changes.
       ::EnemyPool::blinded(::PW_SHADOW)는 이곳에서 답하고 캐시하지 않으므로, 파워업이 바뀌는
       프레임에 바로 답이 따라갑니다. */
    if (pl->enemy.blinded)
        return 0;

    if (m->sight_age <= 0)
    {
        m->sight_age = SIGHT_PERIOD;
        m->seen = (char)can_see(l, m, player_eye);
    }
    return m->seen;
}

/* Picks which attack slot to start, or -1 to keep manoeuvring. Returns -1 while `attack_wait` runs
   or beyond ::MON_RANGE_MID; otherwise the chance to shoot is ::MON_ODDS_MELEE, ::MON_ODDS_NEAR or
   ::MON_ODDS_MID by distance band, and one slot is chosen among those the bands and dice allow.
   시작할 공격 슬롯을 고르거나, 계속 기동하려면 -1을 돌려줍니다. `attack_wait`가 남아 있거나
   ::MON_RANGE_MID 밖이면 -1이고, 그 밖에는 거리 대역에 따라 쏠 확률이 ::MON_ODDS_MELEE,
   ::MON_ODDS_NEAR, ::MON_ODDS_MID이며, 대역과 주사위가 허락한 슬롯 중 하나를 고릅니다. */
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

    /* `n` is the number of attack slots on this kind's row of ::ATTACKS.
       `n`은 이 종류의 ::ATTACKS 행에 있는 공격 슬롯의 수입니다. */
    /* Slots are offered on different terms: a swing whose reach covers the player is offered without
       a dice roll, while a bolt (and a charge) must pass `chance`.
       슬롯은 서로 다른 조건으로 제안됩니다. 닿는 거리 안의 휘두르기는 주사위 없이 제안되고,
       볼트(그리고 돌진)는 `chance`를 통과해야 합니다. */
    int n = mon_attack_count(m->type);

    /* Outside ::MON_RANGE_MELEE, a kind that also has an ::ATK_SWING slot has its ranged chance
       multiplied by ::MON_ODDS_ALSO_MELEE.
       ::MON_RANGE_MELEE 밖에서는 ::ATK_SWING 슬롯도 가진 종류의 원거리 확률에
       ::MON_ODDS_ALSO_MELEE를 곱합니다. */
    if (dist > MON_RANGE_MELEE)
        for (int k = 0; k < n; k++)
            if (ATTACKS[m->type][k].kind == ATK_SWING)
            {
                chance *= MON_ODDS_ALSO_MELEE;
                break;
            }

    float total = 0.0f;
    int offered = 0, only = -1;
    unsigned mask = 0;

    for (int k = 0; k < n; k++)
    {
        const MonAttack *A = &ATTACKS[m->type][k];
        if (dist < A->min || dist > A->max)
            continue;

        /* `dist` is horizontal; a swing is also refused when the vertical offset `rise` exceeds the
           slot's `max`, so a flyer above the player cannot reach them.
           `dist`는 수평 거리입니다. 수직 차이 `rise`가 슬롯의 `max`를 넘으면 휘두르기도 거절하므로,
           플레이어 위에 뜬 비행체는 닿을 수 없습니다. */
        if (A->kind == ATK_SWING && rise > A->max)
            continue;

        /* A swing whose `reach` already covers the player is offered outright; one that must travel
           (`dist` > ::MonAttack::reach) is a charge and has to pass the cone and the `chance` roll.
           `reach`가 이미 플레이어를 덮는 휘두르기는 그대로 제안됩니다. 이동해야 하는 것(`dist` >
           ::MonAttack::reach)은 돌진이며, 원뿔과 `chance` 굴림을 통과해야 합니다. */
        if (A->kind == ATK_SWING && dist > A->reach)
        {
            /* A charge is offered only when the facing is within ::MON_CHARGE_CONE of `ideal_yaw`.
               돌진은 바라보는 방향이 `ideal_yaw`에서 ::MON_CHARGE_CONE 안에 있을 때만 제안됩니다. */
            float off = m->ideal_yaw - m->yaw;
            while (off > M_PI_F)
                off -= 2.0f * M_PI_F;
            while (off < -M_PI_F)
                off += 2.0f * M_PI_F;
            if (fabsf(off) > MON_CHARGE_CONE)
                continue;

            if (!(frand(&pl->enemy) < chance))
                continue;
        }

        if (A->kind == ATK_BOLT && !(frand(&pl->enemy) < chance))
            continue;
        mask |= 1u << k;
        total += A->weight;
        offered++;
        only = k;
    }
    if (!offered)
        return -1;

    /* A single offered slot is returned without drawing from ::EnemyPool::rng, so a kind with one
       attack consumes no extra random numbers.
       제안된 슬롯이 하나뿐이면 ::EnemyPool::rng에서 뽑지 않고 그대로 돌려주므로, 공격이 하나인
       종류는 난수를 더 쓰지 않습니다. */
    if (offered == 1)
        return only;

    /* One draw across the offered weights, restricted by `mask` to the slots the first pass accepted
       so no ::ATK_BOLT chance is re-rolled.
       제안된 가중치들에 대해 한 번 뽑으며, `mask`로 첫 통과가 받아들인 슬롯에만 한정하므로
       ::ATK_BOLT 확률을 다시 굴리지 않습니다. */
    float roll = frand(&pl->enemy) * total;
    for (int k = 0; k < n; k++)
    {
        if (!(mask & (1u << k)))
            continue;
        roll -= ATTACKS[m->type][k].weight;
        if (roll <= 0.0f)
            return k;
    }
    return only;
}

/**
 * @brief Starts an attack: sets ::E_ATTACK, resets the timer and swing count, and rolls the volley length.
 * @param[in,out] pl Pools, for ::EnemyPool::rng.
 * @param[in]     A  The attack slot; its `burst_min` and `burst` bound the volley length.
 * @param[in,out] m  The monster. State, timer, shot count and length change.
 *
 * ::Enemy::volley_n is drawn once here, between `burst_min` (at least 1) and `burst`, and ::E_ATTACK
 * counts ::Enemy::swung against it.
 *
 * @brief 공격을 시작합니다. ::E_ATTACK을 설정하고 타이머와 휘두른 횟수를 초기화하며 일제 사격 길이를 굴립니다.
 * @param[in,out] pl 풀. ::EnemyPool::rng에 사용합니다.
 * @param[in]     A  공격 슬롯. `burst_min`과 `burst`가 일제 사격 길이의 범위입니다.
 * @param[in,out] m  몬스터. 상태, 타이머, 발사 수, 길이가 바뀝니다.
 *
 * ::Enemy::volley_n은 이곳에서 한 번만 `burst_min`(최소 1)과 `burst` 사이에서 뽑으며, ::E_ATTACK이
 * ::Enemy::swung을 그것에 대해 셉니다.
 */
static void begin_attack(Pools *pl, const MonAttack *A, Enemy *m)
{
    int lo = A->burst_min > 0 ? A->burst_min : 1;
    int hi = A->burst > lo ? A->burst : lo;

    m->state = E_ATTACK;
    m->timer = 0.0f;
    m->swung = 0;
    m->volley_n = (short)(lo + (int)(frand(&pl->enemy) * (float)(hi - lo + 1)));

    /* frand can return 1.0, which would land one past `hi`; the roll is clamped to `hi`.
       frand는 1.0을 돌려줄 수 있고 그러면 `hi`를 한 칸 넘어서므로, 굴림을 `hi`로 자릅니다. */
    if (m->volley_n > (short)hi)
        m->volley_n = (short)hi;
}

/* --- Archetypes / 아키타입 --- */
/**
 * @brief A brawler chasing: closes to its band while weaving, then swings or circles.
 * @param[in]     l    The level, for the walk.
 * @param[in]     S    This monster's type.
 * @param[in,out] m    The monster.
 * @param[in]     to   Vector from the monster's eye to the player's.
 * @param[in]     dist Horizontal distance to the player, metres.
 * @param[in]     dt   Timestep in seconds.
 *
 * @brief 추격 중인 근접형입니다. 갈지자로 대역까지 붙은 뒤 휘두르거나 주위를 돕니다.
 * @param[in]     l    이동에 사용할 레벨.
 * @param[in]     S    이 몬스터의 종류.
 * @param[in,out] m    몬스터.
 * @param[in]     to   몬스터의 눈에서 플레이어의 눈으로 향하는 벡터.
 * @param[in]     dist 플레이어까지의 수평 거리 (미터).
 * @param[in]     dt   시간 간격 (초).
 */
/* Rotates the approach direction (ux, uz) by ::MonType::weave radians toward the monster's committed
   side, then steps `step` metres along it through ::move_toward; the rotation keeps the speed at
   ::MonType::speed.
   접근 방향 (ux, uz)를 몬스터가 정해 둔 쪽으로 ::MonType::weave 라디안만큼 회전시킨 뒤 ::move_toward로
   그 방향에 `step` 미터를 내딛습니다. 회전이므로 속도는 ::MonType::speed 그대로입니다. */
static void move_weaving(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 player_eye,
                         float ux, float uz, float step)
{
    if (S->weave > 0.0f)
    {
        float a = S->weave * committed_side(pl, S, m);
        float c = cosf(a), sn = sinf(a);
        float rx = ux * c - uz * sn;
        float rz = ux * sn + uz * c;
        ux = rx;
        uz = rz;
    }
    move_toward(pl, l, S, m, player_eye, ux * step, uz * step);
}

static void chase_brawler(Pools *pl, const Level *l, const MonType *S, Enemy *m,
                          v3 to, float dist, v3 player_eye, float dt)
{
    float inv = dist > 0.001f ? 1.0f / dist : 0.0f;
    float step = S->speed * dt;
    float band = mon_band(m->type);

    /* ::pick_attack is asked before the band check, so a slot whose band lies beyond the monster's
       arm (a charge) can be chosen while still approaching; a chosen slot is started and the walk skipped.
       대역 검사보다 먼저 ::pick_attack에 물으므로, 몬스터의 팔 바깥에 대역을 둔 슬롯(돌진)을
       접근 중에도 고를 수 있습니다. 골라지면 그것을 시작하고 걸음은 건너뜁니다. */
    {
        int far = pick_attack(pl, m, dist, fabsf(to.y));
        if (far >= 0)
        {
            m->atk = (short)far;
            begin_attack(pl, mon_attack(m->type, far), m);
            return;
        }
    }

    if (dist > band)
    {
        move_weaving(pl, l, S, m, player_eye, to.x * inv, to.z * inv, step);
        return;
    }

    /* Within the band: start the attack ::pick_attack offers, or circle with ::ai_run_slide.
       대역 안입니다. ::pick_attack이 제안한 공격을 시작하거나, ::ai_run_slide로 주위를 돕니다. */
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
 * @brief A caster chasing: holds a band of distance, backs off when too close, and only sometimes fires.
 * @param[in]     l    The level, for the walk and the sight test.
 * @param[in]     S    This monster's type.
 * @param[in,out] m    The monster.
 * @param[in]     to   Vector from the monster's eye to the player's.
 * @param[in]     dist Horizontal distance to the player, metres.
 * @param[in]     dt   Timestep in seconds.
 *
 * @brief 추격 중인 캐스터입니다. 일정 거리 대역을 유지하고, 너무 가까우면 물러나며, 가끔만 발사합니다.
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
        /* Inside `band * CASTER_KEEP` it asks ::pick_attack before backing away; a slot offered this
           close is started, otherwise it steps directly away from the player.
           `band * CASTER_KEEP` 안에서는 물러나기 전에 ::pick_attack에 묻습니다. 이 거리에서 제안된
           슬롯이 있으면 시작하고, 없으면 플레이어에게서 곧장 멀어집니다. */
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

    /* Uses the cached ::sees_player to decide whether to plant and begin a wind-up; without sight it
       walks toward the player. ::release_bolt re-checks live before each bolt.
       자리를 잡고 시전을 시작할지는 캐시된 ::sees_player로 정하며, 보이지 않으면 플레이어 쪽으로
       걷습니다. ::release_bolt가 볼트마다 실시간으로 다시 검사합니다. */
    if (!sees_player(pl, l, m, player_eye))
    {
        move_toward(pl, l, S, m, player_eye, to.x * inv * step, to.z * inv * step);
        return;
    }

    /* In its band with sight: start the attack ::pick_attack offers, or circle with ::ai_run_slide.
       대역 안에서 시야가 있으면 ::pick_attack이 제안한 공격을 시작하거나, ::ai_run_slide로 주위를
       돕니다. */
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
 * @brief A swing lands if the player is still within reach and in front of the monster.
 * @param[in]     A    The attack slot; its `reach` and `damage` are used.
 * @param[in,out] m    The monster, for the sound's position.
 * @param[in]     dist Horizontal distance to the player, metres.
 * @param[in]     off  Angle between the monster's facing and the player, radians.
 * @return Damage to deal to the player. 0 if they got clear in time.
 *
 * @brief 플레이어가 아직 닿는 거리 안에 있고 몬스터 앞에 있으면 휘두르기가 적중합니다.
 * @param[in]     A    공격 슬롯. `reach`와 `damage`를 사용합니다.
 * @param[in,out] m    몬스터. 소리의 위치에 사용합니다.
 * @param[in]     dist 플레이어까지의 수평 거리 (미터).
 * @param[in]     off  몬스터가 바라보는 방향과 플레이어 사이의 각도 (라디안).
 * @return 플레이어에게 줄 피해량. 제때 벗어났으면 0입니다.
 */
static int release_swing(const MonAttack *A, Enemy *m, float dist, float off)
{
    if (dist > A->reach + 0.3f)
        return 0;

    /* The player must also be in front: the swing misses when `off` exceeds ::MON_SWING_CONE degrees.
       플레이어가 앞에 있어야 합니다. `off`가 ::MON_SWING_CONE도를 넘으면 휘두르기는 빗나갑니다. */
    if (off > (float)MON_SWING_CONE * 0.0174533f)
        return 0;
    play_at(m->pos, "eatt", 90);
    return A->damage;
}

/**
 * @brief Releases one bolt of a volley, if the player is still visible.
 * @param[in]     l          The level, for the sight test.
 * @param[in]     S          This monster's type, for the eye height.
 * @param[in]     A          The attack slot: shot speed, damage and spread.
 * @param[in,out] m          The monster.
 * @param[in]     player_eye Where to aim.
 *
 * Checks ::can_see live for every bolt; a blocked bolt is skipped. The first bolt fixes
 * ::Enemy::volley_at and plays the cast sound; later bolts inherit that aim with `A->spread` scatter.
 *
 * @brief 일제 사격의 볼트 하나를 발사합니다. 플레이어가 아직 보이는 경우에 한합니다.
 * @param[in]     l          시야 판정에 사용할 레벨.
 * @param[in]     S          이 몬스터의 종류. 눈 높이에 사용합니다.
 * @param[in]     A          공격 슬롯. 탄속, 피해량, 흩어짐입니다.
 * @param[in,out] m          몬스터.
 * @param[in]     player_eye 조준 대상.
 *
 * 볼트마다 ::can_see를 실시간으로 검사하며, 막힌 볼트는 건너뜁니다. 첫 볼트가 ::Enemy::volley_at을
 * 정하고 시전음을 내며, 이후 볼트는 그 조준을 물려받아 `A->spread`만큼 흩어집니다.
 */
static void release_bolt(Pools *pl, const Level *l, const MonType *S,
                         const MonAttack *A, Enemy *m, v3 player_eye)
{
    /* Sight is checked live per bolt; a blocked bolt is skipped, not postponed, since ::Enemy::swung
       advances in ::enemy_update either way.
       시야는 볼트마다 실시간으로 검사합니다. 막힌 볼트는 미뤄지지 않고 건너뛰며, ::Enemy::swung은
       ::enemy_update에서 어느 쪽이든 진행합니다. */
    if (!can_see(l, m, player_eye))
        return;

    v3 from = v3f(m->pos.x, m->pos.y + S->eye, m->pos.z);

    /* On the first release (`swung == 0`) ::Enemy::volley_at is set to the player's eye lowered by
       0.35 of ::PLAYER_EYE; the rest of the volley aims at that same point.
       첫 발사(`swung == 0`)에서 ::Enemy::volley_at을 플레이어의 눈에서 ::PLAYER_EYE의 0.35만큼 내린
       점으로 정하고, 일제 사격의 나머지는 같은 점을 겨눕니다. */
    if (m->swung == 0)
        m->volley_at = v3f(player_eye.x,
                           player_eye.y - PLAYER_EYE * 0.35f,
                           player_eye.z);

    v3 at = m->volley_at;

    /* Builds a `right`/`up` basis perpendicular to this bolt's line of fire, so the scatter below is
       applied across the shot.
       이번 볼트의 사격선에 수직인 `right`/`up` 기저를 만들어, 아래의 흩어짐을 사격선을 가로질러
       적용합니다. */
    v3 d = v3sub(at, from);
    float dist = v3len(d);
    v3 fwd = dist > 1e-4f ? v3scale(d, 1.0f / dist) : v3f(0, 0, 1);
    v3 hint = (fwd.y > 0.9f || fwd.y < -0.9f) ? v3f(1, 0, 0) : v3f(0, 1, 0);
    v3 right = v3norm(v3cross(hint, fwd));
    v3 up = v3cross(fwd, right);

    v3 aim = at;

    /* The first bolt is aimed exactly; later bolts (`swung > 0`) are offset by up to `A->spread`
       times the distance along `right` and `up`.
       첫 볼트는 정확히 겨눕니다. 이후 볼트(`swung > 0`)는 `right`와 `up` 방향으로 최대 `A->spread`
       곱하기 거리만큼 어긋납니다. */
    if (m->swung > 0 && A->spread > 0.0f)
    {
        float rx = (frand(&pl->enemy) * 2.0f - 1.0f) * A->spread * dist;
        float ry = (frand(&pl->enemy) * 2.0f - 1.0f) * A->spread * dist;
        aim = v3add(aim, v3add(v3scale(right, rx), v3scale(up, ry)));
    }
    shot_fire(pl, from, aim, A->shot_speed, A->damage, m->type,
              (int)(m - pl->enemy.m));

    /* The cast sound plays once per volley, on the first bolt.
       시전음은 일제 사격당 한 번, 첫 볼트에서 냅니다. */
    if (m->swung == 0)
        play_at(m->pos, "ecast", 90);
}
