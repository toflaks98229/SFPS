/**
 * @file enemy.h
 * @brief 스프라이트 기반 몬스터의 AI 및 충돌을 관리합니다.
 *
 * Doom 스타일: 월드는 3D이지만 몬스터는 항상 카메라를 향하는 평면 빌보드입니다.
 * 이 빌보드는 절차적으로 생성된 스프라이트 시트(sprite.c 참조)에서 그려집니다.
 * 이는 수백 킬로바이트 예산으로 얻을 수 있는 결과물입니다. 다각형 몬스터는
 * 모델 데이터에 훨씬 더 많은 비용이 들 것입니다.
 *
 * AI와 충돌 로직은 GL과 관련 없이 여기에 있으며, 윈도우 없이도 몬스터를
 * 테스트할 수 있습니다. tools/enemytest.c는 몬스터를 바닥에 놓고 플레이어를
 * 향해 걷는지, 공격하기 위해 멈추는지, 총에 맞으면 죽는지 등을 확인합니다.
 * 추격 버그는 게임 내에서 보이지 않으므로, 이동 버그와 동일한 방식으로 분리하는
 * 것이 가치가 있습니다.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "level.h"

/* --- 매크로 및 상수 --- */
#define ENEMY_MAX       64      ///< @brief 레벨 당 최대 몬스터 수.
#define ENEMY_MAX_SHOTS  48     ///< @brief 동시에 활성화될 수 있는 최대 몬스터 발사체 수.
#define SHOT_RADIUS      0.22f  ///< @brief 발사체의 충돌 반경 (미터).

/**
 * @brief 궤적 파티클 방출 간격 (초).
 *
 * 시간 단위이므로 경로상의 간격은 프레임률과 무관하게 일정합니다. 캐스터의 발사체 속도는
 * 11m/s이므로 0.03초 간격은 약 33cm마다 하나를 남기며, 이는 궤적이 점선이 아닌 선으로
 * 읽히기에 충분히 조밀하면서도 비행 한 번이 공유 파티클 풀을 고갈시키지 않을 만큼
 * 성깁니다. 값을 줄이면 궤적이 촘촘해지는 대신 풀 소비가 늘어납니다.
 */
#define SHOT_TRAIL_INTERVAL 0.03f

/**
 * @brief How many frames a cached line-of-sight reading is reused for.
 *
 * ENGLISH
 * -------
 * The visibility trace was the single most expensive thing the AI did -- once
 * per monster per frame, where a shot fires twice a second. Answering it every
 * fourth frame instead cuts that by 75%, and 4 frames is ~67ms at 60fps: below
 * the point where a monster's reaction to cover reads as a delay rather than as
 * the monster noticing.
 *
 * Raising it further keeps paying, and stops being free. At 8 frames (~133ms) a
 * player who steps out and back behind cover inside that window is never seen
 * at all, which is a monster that can be walked past rather than one that
 * reacts slowly.
 *
 * @note Frames rather than seconds, deliberately. The cost this exists to
 *       bound is per FRAME, so a frame count keeps the saving constant however
 *       fast the machine runs. A time-based period would do less work per frame
 *       on a slow machine -- the opposite of what a slow machine needs.
 * @note Applies to the POLLING sight questions only -- whether a monster has
 *       noticed the player, and whether a caster may plant and begin a cast.
 *       The check made as a bolt is RELEASED is always live, because it exists
 *       to catch a target ducking mid-wind-up. See enemy.c.
 * @note In the header rather than enemy.c because tools/levelbench.c computes
 *       the AI's per-frame trace budget from it. A second copy of the number
 *       there would drift from this one, and the benchmark would then report a
 *       saving the game does not make.
 *
 * 한국어
 * ------
 * @brief 캐시된 시야 판정 결과를 몇 프레임 동안 재사용하는지입니다.
 *
 * 가시성 판정은 AI가 수행하던 것 중 가장 비쌌습니다. 사격이 초당 두 번인 데 반해 이것은
 * 몬스터마다 매 프레임이었습니다. 네 프레임에 한 번만 답하면 그 비용이 75% 줄고, 60fps에서
 * 4프레임은 약 67ms입니다. 몬스터의 엄폐 반응이 지연으로 읽히지 않고 몬스터가 상황을
 * 알아채는 것으로 읽히는 범위 안입니다.
 *
 * 더 올리면 이득은 계속 늘지만 더 이상 공짜가 아닙니다. 8프레임(약 133ms)에서는 그 창
 * 안에 엄폐물 밖으로 나왔다 들어간 플레이어가 아예 목격되지 않는데, 이는 느리게 반응하는
 * 몬스터가 아니라 그냥 지나쳐 갈 수 있는 몬스터입니다.
 *
 * @note 초가 아니라 프레임 단위인 것은 의도적입니다. 이 값이 억제하려는 비용이 *프레임*
 *       단위이므로, 프레임 수로 세면 기기 속도와 무관하게 절감량이 일정합니다. 시간
 *       기반 주기는 느린 기기에서 프레임당 작업량을 줄이는데, 이는 느린 기기에 필요한
 *       것의 정반대입니다.
 * @note *폴링* 시야 질문에만 적용됩니다. 몬스터가 플레이어를 알아챘는지, 캐스터가 자리를
 *       잡고 시전을 시작해도 되는지입니다. 볼트를 *발사하는* 시점의 검사는 항상
 *       실시간이며, 그 검사는 시전 도중 대상이 숨는 경우를 잡기 위해 존재합니다.
 *       enemy.c를 참조하십시오.
 * @note enemy.c가 아니라 헤더에 두는 이유는 tools/levelbench.c가 이 값으로 AI의 프레임당
 *       판정 예산을 계산하기 때문입니다. 그곳에 숫자의 사본을 두면 이 값과 어긋나게 되고,
 *       그러면 벤치마크가 게임이 실제로 얻지 못하는 절감량을 보고하게 됩니다.
 */
#define SIGHT_PERIOD 4

/* --- 열거형 --- */

/**
 * @enum MonTypeID
 * @brief 몬스터 종류를 식별합니다.
 *
 * 이 인덱스는 스프라이트 아틀라스(sprite.h 참조)에서 생물체의 행과도
 * 일치하므로 두 열거형은 동기화되어야 합니다.
 */
enum MonTypeID {
    MON_IMP,        /**< 임프 */
    MON_BRUTE,      /**< 브루트 */
    MON_HOUND,      /**< 하운드 */
    MON_CASTER,     /**< 캐스터 */

    /**
     * @brief The one that is not on the floor.
     *
     * ENGLISH
     * -------
     * THE FIRST MONSTER THAT ADDS AN AXIS RATHER THAN A STAT BLOCK. The other
     * four differ by numbers -- the brute is a slow imp with more of everything,
     * the hound a fast one with less -- and all four are solved by aiming
     * forward and backing up. A wraith holds the air over a chasm, where backing
     * up is a fall and aiming forward finds nothing, and it is the reason the
     * grapple exists in a game about a room.
     *
     * Ranged, because a flyer that had to close would simply descend and become
     * a slow imp. Fragile, because something you cannot always reach must not
     * also take a magazine.
     *
     * 한국어
     * ------
     * @brief 바닥에 있지 않은 유일한 몬스터.
     *
     * 수치가 아니라 *축*을 더하는 첫 몬스터입니다. 나머지 넷은 숫자로 다릅니다. 브루트는 모든
     * 것이 더 많은 느린 임프이고 하운드는 더 적은 빠른 임프이며, 넷 모두 앞을 조준하고 뒤로
     * 물러나면 해결됩니다. 레이스는 협곡 위 공중을 차지하는데, 그곳에서 물러나는 것은 추락이고
     * 앞을 조준하면 아무것도 없습니다. 그리고 그것이 방 하나짜리 게임에 그래플이 존재하는
     * 이유입니다.
     *
     * 원거리인 이유는, 접근해야 하는 비행체는 그냥 내려와서 느린 임프가 되기 때문입니다.
     * 무른 이유는, 언제나 닿을 수는 없는 것이 탄창까지 먹어서는 안 되기 때문입니다.
     */
    MON_WRAITH,

    MON_TYPES       /**< 몬스터 종류의 총 수 */
};

/* --- Quake's fight.qc, in metres / Quake의 fight.qc를 미터로 --------------
 *
 * ENGLISH
 * -------
 * Quake decides what a monster does by which BAND the player is in, not by a
 * single "in range" test, and then rolls dice inside that band. The bands are
 * RANGE_MELEE/NEAR/MID/FAR at 120/500/1000 Quake units; a Quake player is 56
 * units to our 1.8m, so a unit is about 0.032m and those become 3.8/16/32
 * metres.
 *
 * THE DICE ARE THE POINT. A monster that attacks the instant it is in range is
 * a monster whose behaviour you can compute, and once you can compute it the
 * fight is a timing puzzle with one answer. Rolling means the same approach
 * plays differently twice, and it means a monster sometimes closes when you
 * expected it to shoot -- which is what makes Quake's monsters read as
 * aggressive rather than as mechanisms.
 *
 * The odds are Quake's own numbers from CheckAttack, including the halving for
 * monsters that also have a melee attack: something that can bite you prefers
 * to close the distance, so it shoots less often on the way in.
 *
 * 한국어
 * ------
 * Quake는 몬스터의 행동을 "사거리 안인가" 하나로 정하지 않고 플레이어가 어느 *대역*에
 * 있는지로 정한 뒤 그 안에서 주사위를 굴립니다. 대역은 Quake 단위로 120/500/1000이며,
 * Quake 플레이어의 키 56단위가 우리의 1.8m이므로 1단위는 약 0.032m, 따라서
 * 3.8/16/32미터가 됩니다.
 *
 * 주사위가 핵심입니다. 사거리에 들어온 즉시 공격하는 몬스터는 행동을 계산할 수 있는
 * 몬스터이고, 계산이 가능해지는 순간 전투는 정답이 하나인 타이밍 퍼즐이 됩니다. 굴림이
 * 있으면 같은 접근이 두 번 다르게 흘러가고, 쏠 줄 알았던 몬스터가 때때로 거리를
 * 좁힙니다. Quake의 몬스터가 기계가 아니라 공격적으로 읽히는 이유가 그것입니다.
 *
 * 확률은 CheckAttack의 Quake 자체 수치이며, 근접 공격도 가진 몬스터에 대한 절반 감소도
 * 포함합니다. 물 수 있는 것은 거리를 좁히기를 선호하므로 다가오는 동안 덜 쏩니다. */
#define MON_RANGE_MELEE   3.8f   /**< Quake RANGE_MELEE (120 units). */
#define MON_RANGE_NEAR   16.0f   /**< Quake RANGE_NEAR (500 units). */
#define MON_RANGE_MID    32.0f   /**< Quake RANGE_MID (1000 units). */

#define MON_ODDS_MELEE    0.90f  /**< At arm's length, almost always. */
#define MON_ODDS_NEAR     0.40f  /**< Ranged-only monster, near band. */
#define MON_ODDS_MID      0.10f  /**< Ranged-only monster, mid band. */
#define MON_ODDS_ALSO_MELEE 0.5f /**< Halved for anything that can also bite. */

/* HOW LONG A MONSTER RESTS AFTER ATTACKING, before it will even consider
   attacking again. Quake's SUB_AttackFinished(2*random()) -- a RANDOM rest, not
   a fixed one, so a pair of monsters that started together drift out of step
   instead of firing in a chorus forever.
   공격 후 다음 공격을 *고려하기까지*의 휴식 시간입니다. Quake의
   SUB_AttackFinished(2*random())이며, 고정이 아니라 *무작위* 휴식입니다. 함께 시작한 두
   몬스터가 영원히 합창하는 대신 서로 어긋나게 됩니다. */
#define MON_ATTACK_REST   2.0f

/* HOW LONG A MONSTER COMMITS TO A STRAFE DIRECTION. Doom's movecount and
   Quake's lefty both exist because a monster that re-decides every frame
   jitters in place: the choice flips as fast as the geometry under it changes,
   and the result reads as a bug rather than as movement.
   몬스터가 하나의 횡이동 방향을 유지하는 시간입니다. Doom의 movecount와 Quake의 lefty가
   모두 존재하는 이유는, 매 프레임 다시 결정하는 몬스터가 제자리에서 떨기 때문입니다.
   발밑의 지형이 바뀌는 속도로 선택이 뒤집히고, 그 결과는 움직임이 아니라 결함으로
   읽힙니다. */
#define MON_SLIDE_HOLD    1.1f

/**
 * @enum EState
 * @brief 몬스터의 AI 상태를 정의합니다.
 */
typedef enum {
    E_IDLE,    /**< 플레이어를 보지 못한 상태 */
    E_CHASE,   /**< 플레이어를 향해 걷는 중 */
    E_ATTACK,  /**< 공격 준비 또는 재사용 대기 중 */
    E_HURT,    /**< 피격 후 잠시 경직 상태 */
    E_DEAD     /**< 시체: 그려지지만 충돌 및 히트스캔 없음 */
} EState;

/* --- 구조체 --- */

/**
 * @enum MonBehaviour
 * @brief How a monster fights, which is a different question from its stats.
 *
 * ENGLISH
 * -------
 * The AI used to ask `S->shot_speed > 0.0f` at three separate points -- the
 * chase, the release of an attack, and the transition out of one -- and mean
 * "is this a caster?" every time. That works only while "has a projectile
 * speed" and "fights at range" are the same fact, and it is one table row away
 * from not being: a brawler that also lobs something, or a caster whose bolt is
 * a hitscan, and three unrelated pieces of code quietly change together.
 *
 * So the archetype is a column and the stats are stats. ::MonType::shot_speed
 * is now read only where a caster reads it, and adding a third way to fight is
 * a value here, a row in the table and one case in ::enemy_update -- rather
 * than a fourth reading of a number that was never about that.
 *
 * @note Not a table of function pointers, and that is deliberate. This project
 *       links with `-ffunction-sections -Wl,--gc-sections`, and a pointer table
 *       is a reference: every behaviour it names is kept in the binary whether
 *       or not anything reaches it. A switch over an enum lets the linker drop
 *       what a build does not use -- the same reason the loader in gl.h resolves
 *       only the entry points actually called. Two archetypes do not pay for
 *       indirection; if this grows to where they would, the switch is what a
 *       table replaces.
 *
 * 한국어
 * ------
 * @brief 몬스터가 *어떻게 싸우는가*이며, 이는 그 몬스터의 수치와는 다른 질문입니다.
 *
 * AI는 이전에 세 곳(추격, 공격 발동, 공격에서의 전이)에서 각각 `S->shot_speed > 0.0f`를
 * 물었고, 매번 "이것은 캐스터인가"를 뜻했습니다. 그것은 "발사체 속도를 가진다"와 "원거리에서
 * 싸운다"가 같은 사실인 동안에만 성립하며, 그렇지 않게 되기까지 표의 한 행 거리입니다.
 * 무언가를 던지기도 하는 근접형이나 볼트가 히트스캔인 캐스터가 생기면, 서로 무관한 코드 세
 * 곳이 조용히 함께 바뀝니다.
 *
 * 그래서 아키타입은 열이고 수치는 수치입니다. ::MonType::shot_speed는 이제 캐스터가 읽는
 * 곳에서만 읽힙니다. 세 번째 전투 방식을 추가하는 것은 이곳의 값 하나, 표의 행 하나,
 * ::enemy_update의 case 하나이며, 애초에 그것에 관한 것이 아니었던 숫자에 대한 네 번째
 * 해석이 아닙니다.
 *
 * @note 함수 포인터 표가 아니며, 이는 의도적입니다. 이 프로젝트는
 *       `-ffunction-sections -Wl,--gc-sections`로 링크하며, 포인터 표는 곧 참조입니다.
 *       표가 지목하는 모든 동작은 무엇도 그곳에 도달하지 않더라도 바이너리에 남습니다.
 *       열거형에 대한 switch는 링커가 그 빌드가 쓰지 않는 것을 버릴 수 있게 하며, 이는
 *       gl.h의 로더가 실제로 호출하는 엔트리포인트만 해석하는 것과 같은 이유입니다.
 *       아키타입 두 개는 간접화의 값을 치르지 않습니다. 그 값을 치를 만큼 늘어난다면, 그때
 *       표가 대체할 대상이 바로 이 switch입니다.
 */
typedef enum {
    AI_BRAWLER,     /**< Closes to arm's length and swings. / 팔 길이까지 붙어 휘두릅니다. */
    AI_CASTER,       /**< Keeps a band of distance and shoots. / 일정 거리를 유지하며 발사합니다. */
    AI_BEHAVIOURS   /**< How many. / 개수. */
} MonBehaviour;

/**
 * @brief Per-kind switches, one bit each, for things every monster did the same way.
 *
 * ENGLISH
 * -------
 * SEPARATE FROM ::MonBehaviour ON PURPOSE, and the difference is what each is
 * for. A behaviour is a whole way of fighting and exactly one applies, which is
 * why it is an enum and a switch. A flag is one assumption the code makes about
 * every monster, made per kind instead -- and any number of them can be true at
 * once, which is what makes a bitfield the shape rather than a second enum.
 *
 * @note Adding one costs a bit here, a column entry in the table, and ONE
 *       condition where that assumption currently lives. If it costs more than
 *       that, it is a behaviour rather than a flag and belongs above.
 *
 * 한국어
 * ------
 * @brief 종류별 스위치. 모든 몬스터가 같은 방식으로 하던 것들에 대해 비트 하나씩.
 *
 * ::MonBehaviour와 의도적으로 분리되어 있으며, 그 차이가 각자의 용도입니다. behaviour는 싸우는
 * 방식 전체이고 정확히 하나만 적용되므로 enum이자 switch입니다. 플래그는 코드가 *모든* 몬스터에
 * 대해 하던 가정 하나를 종류별로 정하는 것이며, 몇 개든 동시에 참일 수 있습니다. 그것이 두 번째
 * enum이 아니라 비트필드가 형태인 이유입니다.
 *
 * @note 하나를 추가하는 비용은 이곳의 비트 하나, 표의 열 항목 하나, 그리고 그 가정이 현재 사는
 *       곳의 조건 *하나*입니다. 그보다 비싸다면 그것은 플래그가 아니라 behaviour이며 위쪽에
 *       속합니다.
 */
enum {
    /**
     * @brief Holds the height it was spawned at instead of falling to the floor.
     *
     * ENGLISH: Everything in this world falls, and until now that was written
     * into ::enemy_update rather than decided per kind. A flyer is what the
     * vertical arena on the roadmap wants -- floating platforms and a chasm are
     * only a threat if something can be out over them.
     *
     * @warning NO SHIPPED MONSTER CARRIES THIS YET. The mechanism is here and
     *          checked; the creature that uses it is a design decision and a
     *          sprite, neither of which belongs in the same change as the bit.
     *
     * 한국어: 바닥으로 떨어지는 대신 생성된 높이를 유지합니다. 이 세계의 모든 것은 떨어지며,
     * 지금까지 그것은 종류별로 정해진 것이 아니라 ::enemy_update 안에 적혀 있었습니다. 비행체는
     * 로드맵의 수직 아레나가 원하는 것입니다. 떠 있는 발판과 협곡은 그 위로 나올 수 있는 무언가가
     * 있어야만 위협이 됩니다.
     *
     * @warning 아직 어떤 배포 몬스터도 이 비트를 갖고 있지 않습니다. 기구는 이곳에 있고 검사도
     *          됩니다. 그것을 쓰는 크리처는 설계 결정과 스프라이트이며, 둘 다 이 비트와 같은
     *          변경에 속하지 않습니다.
     */
    MON_FLIES = 1 << 0
};

/** @brief Every bit above, for ::types_check to object to anything else. / 위의 모든 비트. ::types_check가 그 외의 것에 이의를 제기하기 위한 것입니다. */
#define MON_FLAGS_ALL (MON_FLIES)

/**
 * @struct MonType
 * @brief 한 종류의 몬스터를 다른 몬스터와 다르게 만드는 모든 특성을 정의합니다.
 *
 * 새로운 타입을 추가하는 것은 이 테이블에 한 행을 추가하고 sprite.c에 바디를
 * 추가하는 것으로 충분하며, 새로운 코드 경로는 필요하지 않습니다.
 */
typedef struct {
    /**
     * @brief 이 종류를 배치하는 엔티티 이름. 레벨 텍스트에 기록되는 이름입니다.
     *
     * 테이블 안에 두는 이유는, 종류 추가가 "행 하나 + `_pixel` 함수 하나"라는 원칙을
     * 지키기 위함입니다. 이전에는 mon_type_for가 이름들을 손으로 펼친 문자 비교로
     * 들고 있어서, 새 몬스터를 추가할 때 손대야 할 곳이 세 군데였고 그중 하나는
     * 이 테이블에서 멀리 떨어져 있었습니다. 이름이 스탯 옆에 있으면 둘이 어긋날 수
     * 없습니다.
     */
    const char *name;
    int   behaviour;    /**< A ::MonBehaviour: how this kind fights. / ::MonBehaviour 값. 이 종류가 어떻게 싸우는가. */
    int   hp;           /**< 체력. */
    float speed;        /**< 이동 속도 (m/s). */
    float radius;       /**< 충돌 반경 (미터). */
    float height;       /**< 신장 (미터). */
    float eye;          /**< 발 위의 시선 높이 (미터). */
    float sight;        /**< 플레이어를 처음 감지할 수 있는 거리 (미터). */
    float attack;       /**< 근접 공격 거리 또는 원거리 공격 사정거리 (미터). */
    int   damage;       /**< 공격 당 피해량. */
    float windup;       /**< 공격 전 준비 시간 (초). */
    float cooldown;     /**< 공격 후 재사용 대기 시간 (초). */
    float aspect;       /**< 스프라이트의 가로/세로 비율. */
    float shot_speed;   /**< 0이 아니면 근접 공격 대신 이 속도(m/s)로 발사체를 발사합니다. */

    /**
     * @brief How fast this monster can turn, in degrees per second.
     *
     * ENGLISH
     * -------
     * Quake's `yaw_speed`, and the single biggest reason its monsters feel like
     * they have mass. Before this the AI wrote `m->yaw = atan2f(...)` every
     * frame, so a monster faced the player exactly, always, however fast the
     * player moved around it -- which makes strafing pointless, because there
     * is no angle you can win. A monster that turns at a finite rate can be
     * got behind, and that is a whole layer of play that a single assignment
     * was throwing away.
     * A per-monster number rather than a constant because it is character: the
     * brute is a wall that cannot track you, the hound is a beast that can.
     *
     * 한국어
     * ------
     * Quake의 `yaw_speed`이며, 그 몬스터들이 질량을 가진 것처럼 느껴지는 가장 큰
     * 이유입니다. 이전에는 AI가 매 프레임 `m->yaw = atan2f(...)`를 썼으므로 플레이어가
     * 아무리 빨리 돌아도 몬스터는 언제나 정확히 플레이어를 향했습니다. 그러면 횡이동이
     * 무의미해집니다. 이길 수 있는 각도가 없기 때문입니다. 유한한 속도로 도는 몬스터는
     * 뒤를 잡을 수 있고, 그것은 대입문 하나가 버리고 있던 플레이의 한 층 전체입니다.
     * 상수가 아니라 몬스터별 수치인 이유는 그것이 성격이기 때문입니다. 브루트는 당신을
     * 추적하지 못하는 벽이고, 하운드는 추적하는 짐승입니다.
     */
    float yaw_speed;

    /**
     * @brief Seconds of immunity to the flinch after being hurt.
     *
     * ENGLISH
     * -------
     * Quake's `pain_finished`. Without it every hit restarts the flinch, so a
     * fast enough weapon holds a monster still until it dies -- it never gets
     * a frame in which it is allowed to act. That was already reachable here
     * and got worse the moment walking speed went to 10.8: the rapid gun fires
     * every 0.085s and the flinch lasted 0.16s, so two shots a second past the
     * flinch's own length was a permanent stun.
     * Per monster because it is the same lever Quake uses it as: the ogre's 5
     * seconds is what makes an ogre frightening.
     *
     * 한국어
     * ------
     * Quake의 `pain_finished`입니다. 이것이 없으면 매 피격이 경직을 다시 시작하므로,
     * 충분히 빠른 무기는 몬스터가 죽을 때까지 붙잡아 둡니다. 행동할 수 있는 프레임을 단
     * 한 번도 얻지 못합니다. 여기서도 이미 도달 가능했고, 이동 속도가 10.8이 된 순간 더
     * 나빠졌습니다. 속사 무기는 0.085초마다 발사되고 경직은 0.16초였으므로, 경직 자체의
     * 길이를 넘는 초당 두 발이면 영구 스턴이었습니다. 몬스터별인 이유는 Quake가 쓰는
     * 것과 같은 조절 수단이기 때문입니다. 오우거의 5초가 오우거를 무섭게 만듭니다.
     */
    float pain_lock;

    /**
     * @brief ::MON_FLIES and whatever joins it. Zero is the ordinary monster.
     *
     * ENGLISH: Zero means every assumption the code makes applies, which is what
     * every kind shipped so far wants -- so a row written before this column
     * existed means exactly what it meant.
     *
     * 한국어: ::MON_FLIES와 이후에 합류할 것들입니다. 0은 평범한 몬스터입니다. 0은 코드가 하는
     * 모든 가정이 적용된다는 뜻이며, 지금까지 배포된 모든 종류가 원하는 바입니다. 따라서 이 열이
     * 생기기 전에 작성된 행은 그것이 뜻하던 바를 정확히 그대로 뜻합니다.
     */
    int flags;
} MonType;

/**
 * @struct Enemy
 * @brief 레벨에 있는 단일 몬스터의 상태입니다.
 */
typedef struct {
    int    type;        /**< 몬스터 종류 (MonTypeID). */
    v3     pos;         /**< 발 위치 (월드 좌표). */
    float  vel_y;       /**< 수직 속도. */
    float  yaw;         /**< 현재 마주보는 방향 (라디안). */
    int    health;      /**< 현재 체력. */
    EState state;       /**< 현재 AI 상태. */
    float  timer;       /**< 상태 내에서 사용하는 타이머 (예: 공격 준비, 재사용 대기). */
    float  anim;        /**< 걷기 사이클을 위한 자유 실행 시계. */
    float  flash;       /**< 피격 시 흰색 섬광 효과 (0으로 감소). */
    int    swung;       /**< 현재 공격의 피해가 이미 적용되었는지 여부. */
    int    active;      /**< 이 슬롯이 사용 중인지 여부. */

    /**
     * @brief 마지막으로 측정한 플레이어 시야 확보 여부. 파생값이며 제작값이 아닙니다.
     *
     * 시야 판정(level_blocked)은 AI가 수행하던 것 중 가장 비쌌으므로, 매 프레임이 아니라
     * 몇 프레임에 한 번만 측정하고 그 사이에는 이 값을 재사용합니다. 자세한 근거는
     * enemy.c의 SIGHT_PERIOD를 참조하십시오.
     *
     * 0(볼 수 없음)에서 시작하는 것이 중요합니다. 새로 생성된 몬스터는 지어낸 값으로
     * 행동하는 대신 첫 실제 측정이 나올 때까지 대기합니다. 아직 모를 때는 아무것도 하지
     * 않는 쪽으로 틀려야 합니다.
     *
     * @warning 볼트를 *발사하는 순간*의 판정에는 쓰이지 않습니다. 그 검사는 시전 도중
     *          대상이 숨는 경우를 잡기 위해 존재하므로 반드시 최신이어야 합니다.
     */
    char   seen;

    /**
     * @brief `seen`을 다시 측정하기까지 남은 프레임 수.
     *
     * 몬스터마다 서로 다른 값에서 시작하여 갱신 시점을 분산시킵니다. 전부 같은 프레임에
     * 갱신하면 평균 비용은 같으면서 주기마다 스파이크로 몰리는데, 그것은 평균이 낮아지는
     * 대신 끊김으로 나타나는 형태입니다.
     */
    short  sight_age;

    /**
     * @brief Where the monster WANTS to face. Its actual yaw turns towards it.
     *
     * Quake's `ideal_yaw`, and the reason it is stored rather than computed at
     * the point of use: turning is rate-limited, so "where I want to look" and
     * "where I am looking" are two different facts and both have to survive the
     * frame.
     * Quake의 `ideal_yaw`이며, 쓰는 자리에서 계산하지 않고 저장하는 이유는 회전에 속도
     * 제한이 있기 때문입니다. "보고 싶은 방향"과 "보고 있는 방향"은 서로 다른 두 사실이며
     * 둘 다 프레임을 넘어 살아남아야 합니다.
     */
    float  ideal_yaw;

    /**
     * @brief Which way this monster is currently circling. Quake's `lefty`.
     *
     * Flipped when the chosen side is blocked, and otherwise held for
     * ::MON_SLIDE_HOLD so the monster commits instead of vibrating.
     * 선택한 쪽이 막히면 뒤집히고, 그 외에는 ::MON_SLIDE_HOLD 동안 유지되어 몬스터가
     * 떨지 않고 한 방향을 밀고 나갑니다.
     */
    char   lefty;

    float  pain_wait;   /**< Seconds until the flinch can fire again. / 경직이 다시 발동할 수 있기까지의 시간. */
    float  attack_wait; /**< Seconds until an attack may even be considered. / 공격을 고려할 수 있기까지의 시간. */
    float  slide_wait;  /**< Seconds left on the current strafe direction. / 현재 횡이동 방향에 남은 시간. */
} Enemy;

/**
 * @struct Shot
 * @brief 몬스터가 발사한 비행 중인 발사체입니다.
 *
 * weapon.c가 자신의 트레이서와 임팩트를 소유하는 것처럼, 이 구조체는
 * enemy 모듈에 의해 소유됩니다. 이들은 몬스터가 발사했기 때문에 존재하며,
 * 레벨과 함께 사라집니다.
 */
typedef struct {
    v3    pos;      /**< 현재 위치. */
    v3    vel;      /**< 속도 벡터. */
    float life;     /**< 발사체가 사라지기까지 남은 시간 (초). 0이면 슬롯이 비어있음. */
    int   damage;   /**< 피해량. */
    int   active;   /**< 이 슬롯이 활성 상태인지 여부. */

    /**
     * @brief 다음 궤적 파티클을 방출하기까지 남은 시간 (초).
     *
     * 프레임마다 방출하지 않고 일정 간격으로 방출하기 위한 타이머입니다. 프레임 단위로
     * 방출하면 궤적의 밀도가 프레임률에 좌우되고(60fps와 144fps에서 다르게 보임),
     * 비행 중인 볼트 하나가 256개짜리 공유 파티클 풀을 혼자 채워 다른 모든 이펙트를
     * 밀어냅니다. 간격을 두면 궤적이 프레임률과 무관하게 일정해집니다.
     */
    float trail_timer;
} Shot;

/**
 * @struct EnemyPool
 * @brief The monsters in this level and the shots they have in the air.
 *
 * The last of the five pools to move out of a module and into the run that
 * owns it. It is also the one the others were waiting on: ::proj_blast has
 * carried a `(void)pl` since the projectiles moved, because the monsters it
 * damages were still reachable only through enemy.c's own arrays.
 *
 * `count` bounds the monsters -- they are laid out once when the level loads
 * and a dead one keeps its slot so its corpse can lie there -- while the shots
 * are a ring with no count, because they are spawned and retired constantly
 * and their order does not matter.
 *
 * The RNG moved with them for the reason fx's did: it decides which way a
 * monster dodges, and sharing it would make one World's fights depend on how
 * many the other had run.
 *
 * 다섯 개 풀 중 마지막으로 모듈을 떠나 그것을 소유하는 플레이로 옮겨 온 것입니다. 동시에
 * 나머지가 기다리고 있던 것이기도 합니다. ::proj_blast는 발사체가 옮겨 간 이후로 `(void)pl`을
 * 달고 있었는데, 그것이 피해를 주는 몬스터에 enemy.c 자신의 배열로만 닿을 수 있었기
 * 때문입니다.
 *
 * `count`가 몬스터의 범위를 정합니다. 레벨 로드 시 한 번 배치되고 죽은 몬스터도 시체가 그
 * 자리에 누울 수 있도록 슬롯을 유지합니다. 반면 발사체는 개수 없는 링입니다. 끊임없이
 * 생성되고 회수되며 순서가 중요하지 않기 때문입니다.
 *
 * RNG가 함께 옮겨 온 이유는 fx의 것과 같습니다. 몬스터가 어느 쪽으로 피할지를 정하므로,
 * 공유하면 한 World의 전투가 다른 World가 몇 번을 돌렸는지에 의존하게 됩니다.
 */
/**
 * @brief How many spawners one level may run.
 *
 * Each is a marker the level laid out, so this is a count of authored things
 * rather than of things in flight. Eight because a level that wants more than
 * eight places monsters keep arriving from is a level that wants a different
 * mechanism -- and because every one of them is a monster every few seconds
 * into a pool of ::ENEMY_MAX.
 *
 * @brief 한 레벨이 돌릴 수 있는 스포너의 수입니다.
 * @note 각각은 레벨이 배치한 표식이므로, 이것은 비행 중인 것이 아니라 *제작된* 것의
 *       개수입니다. 8인 이유는, 몬스터가 계속 도착하는 자리가 여덟 곳보다 많기를 바라는
 *       레벨은 다른 장치를 바라는 레벨이기 때문이며, 그 하나하나가 몇 초마다 몬스터 하나를
 *       ::ENEMY_MAX 크기의 풀에 넣기 때문입니다.
 */
#define ENEMY_MAX_SPAWNERS 8

/**
 * @struct Spawner
 * @brief A marker that keeps making monsters, rather than being one.
 *
 * ENGLISH
 * -------
 * The level lays monsters out once, at load, and that is the whole of what a
 * level could say about them: a room has the monsters it was drawn with, and
 * once they are dead it is empty. A spawner is the other thing a level might
 * want to say -- that they keep coming -- and it needs somewhere to count from,
 * which is why it is state in the pool rather than a second reading of the
 * entity every frame.
 *
 * WHAT LIMITS IT, and it needs limiting: `left` is how many more it will ever
 * make, and `max_alive` is a ceiling on the monsters in the level while it
 * runs. Without the second one an unlimited spawner fills ::ENEMY_MAX in a
 * minute and then raises ::DIAG_ENEMY_CAP every few seconds forever, which is a
 * counter reporting a decision somebody made rather than a fault.
 *
 * 한국어
 * ------
 * @brief 몬스터가 되는 대신 몬스터를 계속 만들어 내는 표식입니다.
 *
 * 레벨은 로드 시점에 몬스터를 한 번 배치하며, 그것이 레벨이 몬스터에 대해 말할 수 있는
 * 전부였습니다. 방에는 그려질 때 함께 그려진 몬스터가 있고, 그들이 죽으면 비어 있습니다.
 * 스포너는 레벨이 말하고 싶어 할 법한 다른 것, 즉 "계속 온다"이며, 세어 나갈 자리가
 * 필요합니다. 매 프레임 엔티티를 다시 읽는 대신 풀 안의 상태인 이유입니다.
 *
 * 무엇이 그것을 제한하는가, 그리고 제한이 필요합니다. `left`는 앞으로 몇 개나 더 만들지이고,
 * `max_alive`는 그것이 돌아가는 동안 레벨에 있을 수 있는 몬스터의 상한입니다. 두 번째가 없으면
 * 무제한 스포너가 1분 만에 ::ENEMY_MAX를 채우고 그 뒤로 몇 초마다 영원히 ::DIAG_ENEMY_CAP을
 * 올립니다. 그것은 결함이 아니라 누군가 내린 결정을 보고하는 카운터입니다.
 */
typedef struct {
    v3    pos;        /**< Where the monsters appear, feet on the floor. / 몬스터가 나타나는 자리. 발이 바닥에 닿습니다. */
    short type;       /**< Which MON_* it makes. / 어떤 MON_*를 만드는지. */
    short left;       /**< How many more it will make; -1 is unlimited. / 앞으로 만들 개수. -1이면 무제한. */
    short max_alive;  /**< Ceiling on monsters in the level; 0 is none. / 레벨 내 몬스터 상한. 0이면 없음. */

    /**
     * @brief How many to make each time it fires. One is the old behaviour.
     *
     * ENGLISH: An arena wants a GROUP to arrive, not a queue of individuals --
     * a monster every five seconds is a corridor's pacing, and four at once
     * from the same mouth is a fight. Separate from ::interval because they
     * tune different things: the interval is how often you are interrupted and
     * this is how much of a problem each interruption is.
     *
     * 한국어: 아레나는 줄 서서 오는 개체가 아니라 *무리*가 도착하기를 바랍니다. 5초에 한
     * 마리는 복도의 박자이고, 같은 입에서 한 번에 넷은 전투입니다. ::interval과 분리한 이유는
     * 둘이 서로 다른 것을 조율하기 때문입니다. 간격은 얼마나 자주 방해받는가이고, 이것은 그
     * 방해 하나가 얼마나 큰 문제인가입니다.
     */
    short burst;

    float interval;   /**< Seconds between one group and the next. / 한 무리와 다음 무리 사이의 초. */

    /**
     * @brief The interval the LEVEL authored, which no wave ever changes.
     *
     * ENGLISH: ::enemy_wave_arm derives ::interval from this rather than from
     * the previous wave's value. Deriving it from itself compounds -- wave 10
     * would be the wave-1 number shrunk ten times over rather than shrunk by
     * ten steps -- and the floor would be reached in about four waves no matter
     * what the level asked for. The authored number has to survive being scaled.
     *
     * 한국어: ::enemy_wave_arm은 ::interval을 이전 웨이브의 값이 아니라 이 값에서 유도합니다.
     * 자기 자신에서 유도하면 복리로 줄어듭니다. 웨이브 10은 웨이브 1의 값이 열 단계만큼 줄어든
     * 것이 아니라 열 번 줄어든 것이 되며, 레벨이 무엇을 요청했든 네 웨이브쯤이면 하한에
     * 닿습니다. 제작된 수치는 배율이 적용되어도 살아남아야 합니다.
     */
    float base_interval;

    float timer;      /**< Seconds until the next. / 다음까지 남은 초. */

    /**
     * @brief Seconds left in the telegraph, or 0 when nothing is coming.
     *
     * ENGLISH
     * -------
     * A monster that appears where the player was already looking is a monster
     * that was never fair. So firing is two steps: the effect plays at the
     * spawn point, and the monsters arrive ::SPAWN_WARN_TIME later. This field
     * is what makes the gap a state rather than a sleep -- ::enemy_update is
     * called once per frame and cannot block.
     *
     * @note Counted down BEFORE ::timer is looked at, so a spawner in its
     *       telegraph is not also counting toward its next group. The two
     *       clocks never run at once.
     *
     * 한국어
     * ------
     * @brief 예고에 남은 시간(초). 오는 것이 없으면 0입니다.
     *
     * 플레이어가 이미 보고 있던 자리에 나타나는 몬스터는 애초에 공정한 적이 없습니다. 그래서
     * 발동은 두 단계입니다. 생성 지점에서 이펙트가 재생되고, ::SPAWN_WARN_TIME 뒤에 몬스터가
     * 도착합니다. 이 필드가 그 간격을 대기가 아니라 *상태*로 만듭니다. ::enemy_update는
     * 프레임마다 한 번 불리며 멈춰 있을 수 없습니다.
     *
     * @note ::timer를 보기 *전에* 감소시키므로, 예고 중인 스포너가 동시에 다음 무리를 향해
     *       세고 있지 않습니다. 두 시계는 결코 함께 돌지 않습니다.
     */
    float warn;

    int   active;     /**< Non-zero while this slot is a spawner. / 이 슬롯이 스포너이면 0이 아닙니다. */
} Spawner;

/**
 * @brief Seconds between a spawn's telegraph and the monsters arriving.
 *
 * ENGLISH: Long enough to turn toward and short enough not to be a lull. It is
 * the same number for every spawner on purpose -- a player learns one delay,
 * and a per-kind delay would be a tell that means something different at each
 * mouth.
 *
 * 한국어: 돌아볼 수 있을 만큼 길고 소강 상태가 되지 않을 만큼 짧습니다. 모든 스포너가 같은
 * 값인 것은 의도입니다. 플레이어는 하나의 지연을 익히며, 종류별 지연은 입마다 다른 것을
 * 뜻하는 신호가 됩니다.
 */
#define SPAWN_WARN_TIME 0.55f

/**
 * @brief How close the player must be for a spawner to hold its fire, metres.
 *
 * ENGLISH
 * -------
 * A monster that materialises on top of the player is not difficulty, it is the
 * game reaching past what they could have done about it: there is no reaction
 * that helps, and the hit lands before the telegraph is even read. The spawner
 * simply waits instead -- it does not consume its budget, does not reset its
 * interval, and fires the moment the player gives it room.
 *
 * @note Measured on the HORIZONTAL PLANE, ignoring height. A vertical arena has
 *       spawners above and below the player that are metres away in y and
 *       directly overhead in x/z, and dropping a monster onto someone's head is
 *       exactly the case this exists to stop. Comparing full 3D distance would
 *       call that far away and let it through.
 *
 * 한국어
 * ------
 * @brief 스포너가 발동을 멈출 만큼 플레이어가 가까운 거리(미터).
 *
 * 플레이어 머리 위에 나타나는 몬스터는 난이도가 아니라, 게임이 플레이어가 할 수 있었던 것
 * 너머로 손을 뻗는 일입니다. 도움이 되는 반응이 없고, 예고를 읽기도 전에 타격이 들어옵니다.
 * 스포너는 대신 그저 기다립니다. 예산을 소모하지도, 간격을 초기화하지도 않으며, 플레이어가
 * 자리를 내주는 즉시 발동합니다.
 *
 * @note 높이를 무시하고 *수평면*에서 잽니다. 수직 아레나에는 플레이어의 위아래로 y 기준
 *       몇 미터 떨어져 있으면서 x/z로는 바로 머리 위인 스포너가 있으며, 누군가의 머리 위로
 *       몬스터를 떨어뜨리는 것이 바로 이것이 막으려는 경우입니다. 3차원 거리를 비교하면
 *       그것을 멀다고 판정하고 통과시킵니다.
 */
#define SPAWN_MIN_DIST 6.0f

/* --- what a wave multiplies, per spawner / 웨이브가 스포너마다 곱하는 것 ---------
 *
 * ENGLISH
 * -------
 * ::enemy_wave_arm reads these and nothing else does. They are the difficulty
 * curve, written as five numbers in one place rather than as arithmetic spread
 * through that function, because tuning an arena is changing these and looking
 * -- and a curve you have to read code to find is a curve nobody tunes.
 *
 * EVERY ONE IS CLAMPED. The budget and the group stop growing and the interval
 * stops shrinking, so the curve flattens into a hard but finite steady state
 * instead of running to a wave that no reflex can survive. Where it flattens is
 * the real difficulty of the game; how fast it gets there is the ramp.
 *
 * 한국어
 * ------
 * ::enemy_wave_arm만이 이 값들을 읽습니다. 이것이 난이도 곡선이며, 그 함수 전반에 흩어진
 * 산술이 아니라 한곳의 숫자 다섯 개로 적었습니다. 아레나를 조율하는 일은 이 값을 바꾸고
 * 보는 것이고, 찾으려면 코드를 읽어야 하는 곡선은 아무도 조율하지 않는 곡선이기 때문입니다.
 *
 * 전부 상한이 있습니다. 예산과 무리는 커지기를 멈추고 간격은 줄어들기를 멈추므로, 곡선은 어떤
 * 반사신경으로도 살아남을 수 없는 웨이브로 달려가는 대신 험하지만 유한한 정상 상태로
 * 평평해집니다. 어디서 평평해지는가가 이 게임의 실제 난이도이고, 거기까지 얼마나 빨리
 * 가는가가 경사입니다. */

/** @brief Monsters one spawner sends in wave 1. / 웨이브 1에서 스포너 하나가 보내는 몬스터 수. */
#define WAVE_BUDGET_BASE 4
/** @brief Added to that budget each wave. / 웨이브마다 그 예산에 더해지는 수. */
#define WAVE_BUDGET_STEP 2
/** @brief Where the budget stops growing. / 예산이 커지기를 멈추는 지점. */
#define WAVE_BUDGET_MAX  24
/** @brief Waves between each increase of the group size. / 무리 크기가 한 번 커지는 데 걸리는 웨이브 수. */
#define WAVE_BURST_EVERY 3
/** @brief Largest group one spawner delivers at once. / 스포너 하나가 한 번에 배달하는 최대 무리. */
#define WAVE_BURST_MAX   5
/** @brief Seconds taken off the interval each wave. / 웨이브마다 간격에서 깎이는 초. */
#define WAVE_INTERVAL_STEP 0.35f
/** @brief Shortest the interval ever gets, seconds. / 간격이 도달할 수 있는 최솟값 (초). */
#define WAVE_INTERVAL_MIN  1.2f

_Static_assert(WAVE_BURST_MAX <= ENEMY_MAX,
               "one group must fit the pool it spawns into");
_Static_assert(WAVE_INTERVAL_MIN > 0.0f,
               "a zero interval is a spawner that fires every frame");

typedef struct {
    Enemy    m[ENEMY_MAX];             /**< Monsters, packed to `count`. / `count`까지 채워진 몬스터. */
    int      count;                    /**< How many the level laid out. / 레벨이 배치한 수. */
    Shot     shots[ENEMY_MAX_SHOTS];   /**< Their projectiles, a ring. / 그들의 발사체이며 링입니다. */
    unsigned rng;                      /**< Fight randomness. 0 means "seed me". / 전투 난수. 0이면 "씨앗을 채워라". */

    Spawner spawner[ENEMY_MAX_SPAWNERS]; /**< Markers that keep making monsters. / 몬스터를 계속 만들어 내는 표식. */
    int     n_spawners;                  /**< How many are in use. / 사용 중인 개수. */
} EnemyPool;

/* The bundle that holds this pool. See proj.h for why the calls take it. */
typedef struct Pools Pools;


/* --- 함수 --- */

/**
 * @brief 몬스터 타입 인덱스로 몬스터의 기본 스탯을 가져옵니다.
 * @param type 몬스터 타입 ID (MON_*).
 * @return MonType 구조체에 대한 const 포인터.
 */
const MonType *mon_stats(int type);

/**
 * @brief 엔티티 종류 문자열에 해당하는 몬스터 타입을 반환합니다.
 * @param kind 엔티티 종류 문자열 (예: "imp", "brute").
 * @return 해당 몬스터 타입 ID, 없으면 -1.
 */
int mon_type_for(const char *kind);


/**
 * @brief 현재 활성화된 발사체의 수를 반환합니다 (비활성 슬롯 포함).
 * @return 스캔할 총 발사체 슬롯 수.
 */
int enemy_shot_count(const Pools *pl);

/**
 * @brief 지정된 인덱스의 발사체 정보를 가져옵니다.
 * @param i 발사체 인덱스.
 * @return Shot 구조체에 대한 const 포인터, 유효하지 않은 인덱스 시 NULL.
 */
const Shot *enemy_shot_at(const Pools *pl, int i);

/**
 * @brief 모든 몬스터와 발사체를 초기화합니다. (재)스폰 전에 호출해야 합니다.
 */
void enemy_reset(Pools *pl);

/**
 * @brief 레벨의 "spawn" 종류 엔티티 위치에 몬스터를 스폰합니다.
 * @param l 몬스터를 스폰할 레벨 데이터.
 */
void enemy_spawn_level(Pools *pl, const Level *l);

/**
 * @brief 현재 레벨의 총 몬스터 수를 반환합니다 (시체 포함).
 * @return 몬스터 수.
 */
int enemy_count(const Pools *pl);

/**
 * @brief 살아있는 몬스터의 수를 반환합니다 (E_DEAD 상태 제외).
 * @return 살아있는 몬스터 수.
 */
int enemy_alive(const Pools *pl);

/**
 * @brief Re-arms every spawner for a wave, scaled by which wave it is.
 *
 * ENGLISH
 * -------
 * A wave is a budget the spawners spend and then stop. ::enemy_spawn_level
 * reads what the level AUTHORED -- the kinds, the places, the base rate -- and
 * this multiplies it by how deep the run has got. The level says what this
 * arena is; the wave number says how bad it is right now.
 *
 * WHAT SCALES AND WHY EACH. The budget grows so a wave lasts longer than the
 * one before it, the group grows so the room fills faster than the player can
 * clear it, and the interval shrinks so the gaps stop being rests. All three
 * are clamped: an unbounded curve is a wave that cannot be survived by anyone,
 * which is a different game from a hard one.
 *
 * @param[in,out] pl   Pools whose spawners to re-arm.
 * @param[in]     wave Which wave, 1-based. Wave 1 is the authored numbers.
 * @note Sets ::Spawner::left, so a wave ENDS: every spawner runs out and
 *       ::enemy_wave_done can become true. A spawner left unlimited would make
 *       a wave that never clears and a reward that never arrives.
 * @note Clears any telegraph in flight. A wave that ended while a spawn was
 *       warned must not deliver that group into the breather.
 *
 * 한국어
 * ------
 * @brief 모든 스포너를 한 웨이브 분량으로, 웨이브 수에 따라 키워서 재장전합니다.
 *
 * 웨이브는 스포너가 쓰고 나면 멈추는 예산입니다. ::enemy_spawn_level은 레벨이 *제작한*
 * 것(종류, 자리, 기본 속도)을 읽고, 이 함수가 거기에 플레이가 얼마나 깊어졌는지를 곱합니다.
 * 레벨은 이 아레나가 무엇인지 말하고, 웨이브 수는 지금 그것이 얼마나 험한지 말합니다.
 *
 * 무엇이 커지고 각각 왜인가. 예산이 커지는 것은 한 웨이브가 앞의 것보다 오래 가게 하기
 * 위함이고, 무리가 커지는 것은 플레이어가 정리하는 속도보다 방이 빨리 차게 하기 위함이며,
 * 간격이 줄어드는 것은 빈틈이 휴식이기를 그만두게 하기 위함입니다. 셋 모두 상한이 있습니다.
 * 경계 없는 곡선은 누구도 살아남을 수 없는 웨이브이며, 그것은 어려운 게임과는 다른 게임입니다.
 *
 * @param[in,out] pl   재장전할 스포너를 가진 풀.
 * @param[in]     wave 몇 번째 웨이브인지. 1부터 셉니다. 웨이브 1이 제작된 수치 그대로입니다.
 * @note ::Spawner::left를 설정하므로 웨이브가 *끝납니다*. 모든 스포너가 소진되고
 *       ::enemy_wave_done이 참이 될 수 있습니다. 무제한으로 둔 스포너는 결코 정리되지 않는
 *       웨이브와 결코 도착하지 않는 보상을 만듭니다.
 * @note 진행 중인 예고를 지웁니다. 생성이 예고된 채로 끝난 웨이브가 그 무리를 휴식 시간으로
 *       배달해서는 안 됩니다.
 */
void enemy_wave_arm(Pools *pl, int wave);

/**
 * @brief Whether the current wave has nothing left to send and nothing alive.
 *
 * ENGLISH
 * -------
 * BOTH HALVES, and the second is the one that is easy to forget: a spawner
 * whose budget is spent has not cleared the wave while its last group is still
 * walking around. A telegraph in flight counts as "still to come" for the same
 * reason -- the monsters are already owed.
 *
 * @param[in] pl Pools to ask.
 * @return Non-zero when the wave is over.
 * @note A level with NO spawners is never done by this test, and that is
 *       correct: it is not an arena, it has no waves, and ::step_wave never
 *       asks. The laid-out monsters of an ordinary level are not a wave.
 *
 * 한국어
 * ------
 * @brief 현재 웨이브가 보낼 것도 남지 않고 살아 있는 것도 없는가.
 *
 * *양쪽 모두*이며, 잊기 쉬운 쪽은 두 번째입니다. 예산을 다 쓴 스포너는 마지막 무리가 아직
 * 돌아다니는 동안에는 웨이브를 정리한 것이 아닙니다. 진행 중인 예고도 같은 이유로 "아직 올 것"에
 * 셉니다. 그 몬스터들은 이미 빚진 것입니다.
 *
 * @param[in] pl 질의할 풀.
 * @return 웨이브가 끝났으면 0이 아닙니다.
 * @note 스포너가 *없는* 레벨은 이 검사로 결코 끝나지 않으며 그것이 옳습니다. 그런 레벨은
 *       아레나가 아니고 웨이브도 없으며 ::step_wave가 묻지도 않습니다. 평범한 레벨이 배치한
 *       몬스터는 웨이브가 아닙니다.
 */
int enemy_wave_done(const Pools *pl);

/**
 * @brief How many spawners this pool is running. 0 means "not an arena".
 *
 * 한국어: 이 풀이 돌리는 스포너의 수입니다. 0이면 "아레나가 아님"입니다.
 */
int enemy_spawner_count(const Pools *pl);

/**
 * @brief 지정된 인덱스의 몬스터 정보를 가져옵니다.
 * @param i 몬스터 인덱스.
 * @return Enemy 구조체에 대한 const 포인터, 유효하지 않은 인덱스 시 NULL.
 */
const Enemy *enemy_at(const Pools *pl, int i);

/**
 * @brief 모든 몬스터와 발사체의 상태를 한 프레임 업데이트합니다.
 *
 * 이 프레임 동안 플레이어에게 가해진 총 피해량(근접 공격 및 발사체 피격 모두)을
 * 반환합니다. 이 모듈이 플레이어의 체력에 직접 접근하는 대신, 호출자가
 * 플레이어 체력을 관리하도록 합니다.
 * @param l 현재 레벨 데이터.
 * @param player_eye 플레이어의 시점 위치.
 * @param dt 마지막 프레임 이후 경과 시간 (초).
 * @return 플레이어가 입은 총 피해량.
 */
int enemy_update(Pools *pl, const Level *l, v3 player_eye, float dt);

/**
 * @brief 광선이 부딪히는 가장 가까운 살아있는 몬스터를 찾습니다 (수직 실린더로 판정).
 *
 * 샷건에서 사용되어 탄환이 몬스터를 통과하지 않고 멈추도록 합니다.
 * @param o 광선의 원점.
 * @param d 광선의 방향 벡터.
 * @param maxdist 최대 탐지 거리.
 * @param out_t [out] 광선이 몬스터에 부딪힌 거리를 저장할 포인터.
 * @param out_idx [out] 부딪힌 몬스터의 인덱스를 저장할 포인터.
 * @return `maxdist` 내에서 몬스터를 맞추면 1, 그렇지 않으면 0.
 */
int enemy_hitscan(const Pools *pl, v3 o, v3 d, float maxdist, float *out_t, int *out_idx);

/**
 * @brief 몬스터에게 피해를 입히고, 경직시키거나 죽입니다.
 * @param idx 피해를 입힐 몬스터의 인덱스.
 * @param dmg 피해량.
 * @param dir 사격 방향, 향후 넉백 및 사망 방향에 사용.
 */
void enemy_hurt(Pools *pl, int idx, int dmg, v3 dir);

#endif
