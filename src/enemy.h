/**
 * @file enemy.h
 * @brief Sprite monsters: what makes each kind different, and how they fight.
 *
 * ENGLISH
 * -------
 * Doom's arrangement. The world is 3D but a monster is a flat billboard that
 * always faces the camera, drawn from the procedurally generated sprite sheet
 * -- see sprite.c. That is what a few hundred kilobytes of budget buys;
 * polygonal monsters would cost far more in model data than the whole renderer
 * costs in code.
 *
 * The AI and the collision live here, free of GL, so a monster can be tested
 * with no window at all. tools/enemytest.c stands one on a floor and checks
 * that it walks towards the player, stops to attack, and dies when shot. A
 * chase bug is invisible from inside the running game, which makes it worth
 * separating for the same reason a movement bug is.
 *
 * @note Everything here works through a ::Pools, never through module state.
 *       See proj.h for why the calls take the bundle.
 * @note ::MonTypeID doubles as the creature's row in the sprite atlas, so the
 *       two enums must stay in step.
 *
 * 한국어
 * ------
 * Doom의 구성입니다. 월드는 3D이지만 몬스터는 언제나 카메라를 향하는 평면 빌보드이며,
 * 절차적으로 생성된 스프라이트 시트에서 그려집니다. sprite.c를 참조하십시오. 수백 킬로바이트
 * 예산으로 살 수 있는 것이 그것입니다. 다각형 몬스터는 렌더러 전체가 코드로 쓰는 것보다 훨씬
 * 많은 용량을 모델 데이터로 쓰게 됩니다.
 *
 * AI와 충돌은 GL과 무관하게 이곳에 있으므로, 창 없이도 몬스터를 시험할 수 있습니다.
 * tools/enemytest.c는 몬스터를 바닥에 세워 두고 플레이어를 향해 걷는지, 공격하려고 멈추는지,
 * 총에 맞으면 죽는지를 확인합니다. 추격 결함은 실행 중인 게임 안에서는 보이지 않으며, 이동
 * 결함을 분리할 가치가 있는 것과 같은 이유로 이것도 분리할 가치가 있습니다.
 *
 * @note 이곳의 모든 것은 모듈 상태가 아니라 ::Pools를 통해 동작합니다. 호출이 그 묶음을 받는
 *       이유는 proj.h를 참조하십시오.
 * @note ::MonTypeID는 스프라이트 아틀라스에서 그 생물의 행을 겸하므로, 두 열거형은 보조를
 *       맞추어야 합니다.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "level.h"

/* --- Macros and constants / 매크로 및 상수 --- */

/** @brief Monsters one level may hold, corpses included. / 레벨 하나가 담는 몬스터 수. 시체를 포함합니다. */
#define ENEMY_MAX       64

/**
 * @brief Monster projectiles that may be in the air at once.
 *
 * ENGLISH
 * -------
 * RAISED FROM 48 WHEN THE WATER SPIRIT STOPPED FIRING A SHOTGUN. Five bolts
 * leaving together are five slots held for as long as one flight; up to ten
 * leaving over half a second are ten slots held for a flight *plus* that half
 * second, and the spirit is the monster the waves send in numbers. Four of them
 * volleying at once is 40 against the old 48, which is not a cap that has been
 * exceeded -- it is a cap with nothing left over for the caster standing behind
 * them, and the ring drops the OLDEST, so what a full pool deletes is the bolt
 * closest to arriving.
 *
 * @note A ring, not a refusal: ::DIAG_SHOT_CAP is where the overflow is
 *       reported, and it is worth watching after a change to ::MonType::burst
 *       rather than trusted.
 * @note .bss, so the 48 added here cost the floppy nothing.
 *
 * 한국어
 * ------
 * @brief 동시에 공중에 있을 수 있는 몬스터 발사체 수.
 *
 * *물의 정령이 산탄을 그만두면서 48에서 올렸습니다.* 함께 떠나는 다섯 발은 비행 한 번 동안
 * 다섯 칸을 붙듭니다. 0.5초에 걸쳐 떠나는 최대 열 발은 비행 시간 *더하기* 그 0.5초 동안 열
 * 칸을 붙들며, 물의 정령은 웨이브가 수로 보내는 몬스터입니다. 넷이 동시에 퍼부으면 40이고
 * 옛 48에 대해 그것은 초과된 상한이 아니라 *뒤에 선 캐스터에게 남길 것이 없는* 상한입니다.
 * 그리고 링은 가장 *오래된* 것을 버리므로, 꽉 찬 풀이 지우는 것은 도착에 가장 가까운
 * 볼트입니다.
 *
 * @note 거절이 아니라 링입니다. 초과는 ::DIAG_SHOT_CAP으로 보고되며, ::MonType::burst를
 *       바꾼 뒤에는 믿을 것이 아니라 지켜볼 것입니다.
 * @note .bss이므로 이곳에서 더한 48은 플로피에 아무 비용도 들지 않습니다.
 */
#define ENEMY_MAX_SHOTS  96

/**
 * @brief Collision radius of a projectile, in metres.
 *
 * ENGLISH
 * -------
 * SMALLER BECAUSE THERE ARE MORE OF THEM. At 0.22 a bolt was a thrown rock and
 * a water spirit threw five of them together; the answer was to leave the cone
 * and the size of any one of them barely entered into it. A stream of up to ten
 * asks the opposite question -- can I be between them -- and that question has
 * no good answer while each bolt is nearly half a metre across. Against
 * ::PLAYER_RADIUS the pair decide how wide the gap has to be, and this is the
 * half of it that was making the gap impossible.
 *
 * @note DRAWN SIZE FOLLOWS THIS, and not automatically: scene.c's
 *       `SHOT_CORE_SIZE`, `SHOT_HALO_SIZE` and `SHOT_GLOW_SIZE` are separate
 *       numbers that have to be moved with it, or the bolt is shot at a size it
 *       is not drawn at -- the fault ::MonType::radius records for monsters.
 *
 * 한국어
 * ------
 * @brief 발사체의 충돌 반경(미터).
 *
 * *수가 늘었으므로 작아졌습니다.* 0.22일 때 볼트는 던진 돌이었고 물의 정령은 그것을 다섯 개
 * 한꺼번에 던졌습니다. 답은 원뿔에서 벗어나는 것이었고 개별 볼트의 크기는 거의 상관이
 * 없었습니다. 최대 열 발의 줄기는 반대 질문을 합니다. *그 사이에 있을 수 있는가*이며, 볼트
 * 하나가 반 미터에 가까운 동안 그 질문에는 좋은 답이 없습니다. ::PLAYER_RADIUS와 짝을 이루어
 * 틈이 얼마나 넓어야 하는지를 정하며, 틈을 불가능하게 만들고 있던 절반이 이쪽이었습니다.
 *
 * @note *그려지는 크기가 이것을 따라가되 저절로는 아닙니다.* scene.c의 `SHOT_CORE_SIZE`,
 *       `SHOT_HALO_SIZE`, `SHOT_GLOW_SIZE`는 함께 옮겨야 하는 별도의 수이며, 그러지 않으면
 *       볼트는 그려지지 않는 크기로 맞히게 됩니다. ::MonType::radius가 몬스터에 대해 기록해 둔
 *       바로 그 결함입니다.
 */
#define SHOT_RADIUS      0.13f

/**
 * @brief How often a projectile drops a trail particle, in seconds.
 *
 * ENGLISH
 * -------
 * A TIME rather than a per-frame emit, so the spacing along the path is the
 * same whatever the frame rate. A caster's bolt travels at 11m/s, so 0.03s
 * leaves one about every 33cm -- dense enough that the trail reads as a line
 * rather than a dotted one, sparse enough that a single flight does not drain
 * the shared particle pool. Lowering it tightens the trail and spends more of
 * that pool.
 *
 * 한국어
 * ------
 * @brief 발사체가 궤적 파티클을 남기는 간격(초).
 *
 * 프레임당 방출이 아니라 *시간* 기준이므로, 프레임률과 무관하게 경로상의 간격이
 * 일정합니다. 캐스터의 볼트는 11m/s로 날아가므로 0.03초는 약 33cm마다 하나를 남깁니다.
 * 궤적이 점선이 아닌 선으로 읽히기에 충분히 조밀하면서도, 비행 한 번이 공유 파티클 풀을
 * 고갈시키지 않을 만큼 성깁니다. 값을 줄이면 궤적이 촘촘해지는 대신 그 풀을 더 씁니다.
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

/* --- Enumerations / 열거형 --- */

/**
 * @enum MonTypeID
 * @brief Which kind of monster this is.
 *
 * ENGLISH
 * -------
 * @note These indices double as the creature's row in the sprite atlas -- see
 *       sprite.h -- so the two enums must be kept in step. A row added here
 *       without one there draws the wrong creature rather than failing.
 *
 * 한국어
 * ------
 * @brief 몬스터의 종류.
 *
 * @note 이 인덱스는 스프라이트 아틀라스에서 그 생물의 행을 겸하므로(sprite.h 참조) 두
 *       열거형은 보조를 맞추어야 합니다. 이곳에만 행을 더하고 그곳에 더하지 않으면 실패하는
 *       대신 엉뚱한 생물이 그려집니다.
 */
enum MonTypeID {
    /**
     * @brief The baseline the others are measured against.
     *
     * ENGLISH: Named `imp` until a water spirit took the slot -- a melee
     * creature became a mid-range one, so the name stopped describing it. The
     * ENUM keeps its position because the position is the sprite atlas row.
     *
     * `imp` still resolves and no longer resolves HERE. The bestiary lost its
     * fast melee row and its flyer, and a retired name is pointed at whatever
     * replaced it rather than left aimed at where it used to live: `hound`
     * arrives at this row, `imp` at ::MON_CASTER. See ::MON_LEGACY.
     *
     * 한국어: 물의 정령이 이 자리를 차지하기 전까지 `imp`였습니다. 근접 생물이 중거리
     * 생물이 되면서 그 이름이 더 이상 그것을 설명하지 못하게 되었습니다. *열거형*은 자기
     * 위치를 유지합니다. 그 위치가 곧 스프라이트 아틀라스의 행이기 때문입니다.
     *
     * `imp`는 여전히 해석되며, 더 이상 *이곳으로* 해석되지 않습니다. 도감이 빠른 근접 행과
     * 비행체를 잃었고, 은퇴한 이름은 예전에 살던 자리를 계속 겨누는 대신 그것을 대신한 것을
     * 가리킵니다. `hound`는 이 행에, `imp`는 ::MON_CASTER에 도착합니다. ::MON_LEGACY를
     * 참조하십시오.
     */
    MON_WATER_SPIRIT,
    MON_BRUTE,      /**< A slow water spirit with more of everything, and it closes. / 모든 것이 더 많고 붙는 느린 물의 정령. */

    /**
     * @brief Fights at range with a projectile, and does it from the air.
     *
     * ENGLISH
     * -------
     * THE ONE MONSTER THAT ADDS AN AXIS RATHER THAN A STAT BLOCK. The other two
     * differ from each other by numbers -- the brute is a slow water spirit
     * with more of everything -- and both are solved by aiming forward and
     * backing up. This one holds the air over a chasm, where backing up is a
     * fall and aiming forward finds nothing, and it is the reason the grapple
     * exists in a game about a room.
     *
     * IT DID NOT ALWAYS FLY. There was a floor-bound caster and a `wraith`
     * hanging above it, and what separated them was a metre of air, four points
     * of health and this file's ::MON_FLIES bit -- one idea spread over two
     * rows, paying for two sprite bodies, two FGD boxes and two ::LOOT_TABLES
     * slots. The flying one was the reason either was worth having, so the flag
     * came down onto the caster and the second row went. What is left is a
     * single ranged creature that is never on the floor.
     *
     * Ranged, because a flyer that had to close would simply descend and become
     * a slow brute. Fragile, because something you cannot always reach must not
     * also take a magazine.
     *
     * 한국어
     * ------
     * @brief 발사체로 원거리에서 싸우며, 그것을 공중에서 합니다.
     *
     * *수치가 아니라 축을 더하는 유일한 몬스터입니다.* 나머지 둘은 서로 숫자로 다릅니다.
     * 브루트는 모든 것이 더 많은 느린 물의 정령이며, 둘 다 앞을 조준하고 뒤로 물러나면
     * 해결됩니다. 이것은 협곡 위 공중을 차지하는데, 그곳에서 물러나는 것은 추락이고 앞을
     * 조준하면 아무것도 없습니다. 그리고 그것이 방 하나짜리 게임에 그래플이 존재하는
     * 이유입니다.
     *
     * *처음부터 날지는 않았습니다.* 바닥에 묶인 캐스터가 있었고 그 위에 `wraith`가 걸려
     * 있었으며, 둘을 가른 것은 공중 1미터와 체력 4점과 이 파일의 ::MON_FLIES 비트였습니다.
     * 하나의 착상을 두 행에 펼친 것이며, 스프라이트 몸체 둘과 FGD 상자 둘과 ::LOOT_TABLES
     * 칸 둘을 치렀습니다. 둘 중 어느 쪽이든 가질 값어치가 있었던 이유는 나는 쪽이었으므로,
     * 플래그가 캐스터로 내려오고 두 번째 행이 사라졌습니다. 남은 것은 결코 바닥에 있지 않은
     * 원거리 생물 하나입니다.
     *
     * 원거리인 이유는, 접근해야 하는 비행체는 그냥 내려와서 느린 브루트가 되기 때문입니다.
     * 무른 이유는, 언제나 닿을 수는 없는 것이 탄창까지 먹어서는 안 되기 때문입니다.
     */
    MON_CASTER,

    /**
     * @brief The boss: a mouth in the wall that never leaves it.
     *
     * ENGLISH
     * -------
     * THE FIRST MONSTER THAT IS NOT SOLVED BY AIMING AT IT. Every other row in
     * this table dies to the gun pointed at it; the maw is invulnerable while a
     * single ::MON_GUARD stands, so the answer to it is somewhere else in the
     * room. That is the whole of the fight, and it is why the body is
     * ::MON_ANCHORED: a boss that walks is a boss you retreat from while facing
     * it, and then the wards are an errand rather than the fight.
     *
     * Its health is spent in thirds -- see ::BOSS_CYCLES and the boundary clamp
     * in ::enemy_hurt -- so "three cycles" is a fact about the health bar
     * rather than a counter kept beside it. There is no second death path.
     *
     * 한국어
     * ------
     * @brief 보스. 벽에서 떠나지 않는 아가리입니다.
     *
     * *겨누는 것으로 풀리지 않는 첫 몬스터입니다.* 이 표의 다른 모든 행은 자신을 향한 총에
     * 죽지만, 아귀는 ::MON_GUARD가 하나라도 서 있는 동안 무적이므로 그에 대한 답은 방의 다른
     * 곳에 있습니다. 그것이 이 전투의 전부이며, 본체가 ::MON_ANCHORED인 이유입니다. 걸어
     * 다니는 보스는 마주 본 채 물러나면 되는 보스이고, 그러면 결계핵은 전투가 아니라
     * 심부름이 됩니다.
     *
     * 체력은 3등분으로 소모됩니다(::BOSS_CYCLES와 ::enemy_hurt의 경계 고정 참조). 따라서
     * "3사이클"은 옆에 따로 둔 계수기가 아니라 체력바에 대한 사실입니다. 두 번째 사망 경로는
     * 없습니다.
     */
    MON_MAW,

    /**
     * @brief What keeps the maw invulnerable, and what makes the room fill up.
     *
     * ENGLISH
     * -------
     * ONE TYPE, NOT TWO. A ward that summons flyers and a ward that summons
     * brawlers differ in what comes out of them and in nothing else -- same
     * silhouette, same health, same behaviour, same box -- so the difference is
     * ::Enemy::ward_table, an instance field, rather than a second row here.
     * Two rows would have cost a second sprite body, a second FGD box, and the
     * eighth and last ::LOOT_TABLES slot, for a distinction the player reads
     * off what walks out rather than off the ward.
     *
     * It is ::AI_INERT: it never attacks. Its pressure is entirely in
     * ::Enemy::summon_left -- shooting it is what fills the room, so the player
     * sets the pace of their own fight. See enemy.c's note on ::WARD_SUMMON_DMG
     * for why that is a damage THRESHOLD and not a damage EVENT.
     *
     * 한국어
     * ------
     * @brief 아귀를 무적으로 유지하는 것이자, 방을 채우는 것.
     *
     * *두 종류가 아니라 한 종류입니다.* 비행체를 부르는 결계핵과 근접체를 부르는 결계핵은
     * 무엇이 나오는가에서만 다르고 나머지는 전부 같습니다. 같은 실루엣, 같은 체력, 같은 행동,
     * 같은 상자입니다. 그래서 그 차이는 이곳의 두 번째 행이 아니라 인스턴스 필드인
     * ::Enemy::ward_table입니다. 행을 둘로 두면 두 번째 스프라이트 몸통, 두 번째 FGD 상자,
     * 그리고 ::LOOT_TABLES의 여덟 번째이자 마지막 칸을 치르게 되는데, 정작 플레이어는 그
     * 구별을 결계핵이 아니라 걸어 나오는 것에서 읽습니다.
     *
     * ::AI_INERT이며 결코 공격하지 않습니다. 그 압박은 전부 ::Enemy::summon_left에 있습니다.
     * 그것을 쏘는 행위가 곧 방을 채우므로 플레이어가 자기 전투의 박자를 정합니다. 그것이 피해
     * *사건*이 아니라 피해 *문턱*인 이유는 enemy.c의 ::WARD_SUMMON_DMG 주석에 있습니다.
     */
    MON_WARD,

    MON_TYPES       /**< How many kinds there are. / 몬스터 종류의 총 수. */
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
/**
 * @def INFIGHT_TIME
 * @brief How long a monster stays angry at another monster, in seconds.
 *
 * ENGLISH
 * -------
 * QUAKE HAS NO SUCH TIMER: a monster keeps its `enemy` until that enemy dies
 * or it loses track of it, and it can spend the rest of the level on a feud.
 * That works there because a Quake level is a sequence of rooms with a fixed
 * population; it does not work in an arena, where the spawners keep arriving
 * and a permanent grudge is a monster permanently removed from the fight the
 * player is in.
 *
 * Eight seconds is long enough to watch happen -- to see two of them turn on
 * each other, close, and finish it -- and short enough that walking away is
 * not a strategy. A player who could start a feud and leave would have found
 * a way to delete monsters for free.
 *
 * 한국어
 * ------
 * @brief 몬스터가 다른 몬스터에게 화가 나 있는 시간(초).
 *
 * *Quake에는 그런 타이머가 없습니다.* 몬스터는 그 적이 죽거나 놓칠 때까지 `enemy`를 유지하며,
 * 레벨의 나머지를 원한에 쓸 수도 있습니다. 그곳에서는 통합니다. Quake의 레벨은 인구가 고정된
 * 방들의 연속이기 때문입니다. 투기장에서는 통하지 않습니다. 스포너가 계속 도착하고, 영구적인
 * 원한은 플레이어가 있는 전투에서 영구히 빠진 몬스터입니다.
 *
 * 8초는 벌어지는 것을 지켜보기에 충분히 길고(둘이 서로에게 돌아서서 붙고 끝내는 것을 보기에),
 * 자리를 뜨는 것이 전략이 되지 않을 만큼 짧습니다. 싸움을 붙여 놓고 떠날 수 있는 플레이어는
 * 몬스터를 공짜로 지우는 방법을 찾은 것입니다.
 */
#define INFIGHT_TIME 8.0f

/* Seconds between one charge mote and the next while a caster winds up. Paced
   here rather than by the recipe's `count` for the reason the lava smoke is:
   a burst that arrives all at once is a puff, and what reads as GATHERING is a
   stream that keeps coming for as long as the pose is held. Against the
   caster's 0.85s wind-up this is about ten motes, three at a time.
   캐스터가 준비동작을 하는 동안 충전 알갱이 사이의 간격(초)입니다. 레시피의 `count`가 아니라
   이곳에서 속도를 정하는 이유는 용암 연기와 같습니다. 한꺼번에 도착하는 다발은 연기 한 모금이고,
   *모이는 중*으로 읽히는 것은 자세가 유지되는 동안 계속 오는 흐름입니다. 캐스터의 0.85초
   준비동작에 대해 세 개씩 열 번쯤입니다. */
#define CAST_GATHER_INTERVAL 0.085f
#define MON_SLIDE_LEG     2.5f

/* AND WHY THAT IS A DISTANCE RATHER THAN THE TIME IT USED TO BE. It was 1.1
   seconds, which is a leg of about two metres at the speeds this bestiary had
   -- and about seven at the speeds it has now. The number that was tuned was
   never the second; it was the SHAPE of the zig-zag, and a shape held in
   seconds silently stretches every time something walks faster. Held in metres
   the shape survives the speed column: a quick monster flips more often, a
   heavy one commits longer, and both draw the same figure at different rates.
   *그리고 그것이 왜 예전의 시간이 아니라 거리인가.* 1.1초였고, 이 도감이 가지고 있던 속도에서는
   한 다리가 2미터쯤이었으며 지금의 속도에서는 7미터쯤입니다. 조율된 수는 애초에 초가 아니라
   갈지자의 *모양*이었고, 초로 붙잡아 둔 모양은 무언가가 더 빨리 걸을 때마다 조용히
   늘어납니다. 미터로 붙잡으면 모양이 속도 열을 견딥니다. 빠른 몬스터는 더 자주 꺾고 무거운
   몬스터는 더 오래 밀고 나가며, 둘이 서로 다른 속도로 같은 도형을 그립니다. */
#define MON_SLIDE_HOLD(spd) (MON_SLIDE_LEG / ((spd) > 0.1f ? (spd) : 0.1f))

/**
 * @brief How fast two monsters standing in each other push apart, per second
 *        of overlap.
 *
 * ENGLISH
 * -------
 * A SEPARATION, NOT A COLLISION, and the two solve different halves. Refusing
 * a step keeps a monster from walking INTO another one; it does nothing for
 * two that are already inside each other, and on its own it makes that state
 * permanent -- every direction out of an overlap still overlaps, so a refusal
 * welds the pair together for the rest of the level. Monsters arrive inside
 * each other for reasons that have nothing to do with walking: a spawner puts
 * two at one ward in the same second, and ::enemy_boss_summon puts a handful
 * at the maw at once.
 *
 * SO THE OVERLAP IS WHAT IS PUSHED, and the speed is proportional to it: deep
 * overlaps resolve in a few frames, a graze is a nudge. Each of the pair takes
 * half, and each half is capped at half the overlap, so the pair can close the
 * gap exactly and never past it. That cap is what makes this stable without a
 * damping term -- a push that can overshoot is a push that can oscillate, and
 * two monsters vibrating against each other is worse than two overlapping.
 *
 * SIX PER SECOND is about a third of a second for a brute and a caster stacked
 * at one point (1.35m of overlap), and under a tenth for the overlaps that
 * actually happen while walking. Slower reads as monsters melting apart;
 * faster reads as a shove, which is the wrong word for what is going on -- the
 * shove is the refused step, this is only the tidying up.
 *
 * 한국어
 * ------
 * @brief 서로 안에 선 몬스터 둘이 겹친 만큼에 대해 초당 얼마나 밀려나는가.
 *
 * *충돌이 아니라 분리이며*, 둘은 서로 다른 절반을 풉니다. 걸음을 거절하는 것은 몬스터가 다른
 * 몬스터 *안으로* 걸어 들어가는 것을 막습니다. 이미 서로 안에 있는 둘에 대해서는 아무것도 하지
 * 않으며, 그것만으로는 그 상태를 영구적으로 만듭니다. 겹침에서 나가는 모든 방향이 여전히
 * 겹치므로, 거절은 그 쌍을 레벨이 끝날 때까지 용접합니다. 몬스터는 걷는 것과 무관한 이유로
 * 서로 안에 도착합니다. 스포너가 같은 초에 한 워드에 둘을 놓고, ::enemy_boss_summon이 아귀
 * 자리에 여럿을 한꺼번에 놓습니다.
 *
 * *그래서 밀리는 것은 겹침 자체이고* 속도는 그것에 비례합니다. 깊은 겹침은 몇 프레임에
 * 풀리고 스치는 것은 살짝 밀칩니다. 쌍의 각각이 절반씩 가져가며 각 절반은 겹침의 절반으로
 * 제한되므로, 그 쌍은 간격을 정확히 닫을 수 있고 그 너머로는 갈 수 없습니다. 그 제한이
 * 감쇠 항 없이 이것을 안정되게 만듭니다. 지나칠 수 있는 밀기는 진동할 수 있는 밀기이고,
 * 서로에 대해 떠는 몬스터 둘은 겹친 둘보다 나쁩니다.
 *
 * *초당 6*은 브루트와 캐스터가 한 점에 쌓였을 때(겹침 1.35m) 3분의 1초쯤이고, 걷는 동안
 * 실제로 생기는 겹침에 대해서는 10분의 1초 미만입니다. 더 느리면 몬스터가 녹아 떨어지는
 * 것처럼 읽히고, 더 빠르면 떠미는 것처럼 읽히는데 그것은 여기서 벌어지는 일에 맞지 않는
 * 말입니다. 떠미는 것은 거절된 걸음이고 이것은 뒷정리일 뿐입니다.
 */
#define MON_PUSH_RATE 6.0f

/**
 * @enum EState
 * @brief What a monster is currently doing.
 *
 * ENGLISH
 * -------
 * @note ::E_DEAD is a state rather than a freed slot, because a corpse is
 *       still drawn. It is skipped by collision and by ::enemy_hitscan, which
 *       is what keeps a body from soaking shots meant for what is behind it.
 *
 * 한국어
 * ------
 * @brief 몬스터가 지금 무엇을 하고 있는가.
 *
 * @note ::E_DEAD는 해제된 슬롯이 아니라 하나의 상태입니다. 시체는 여전히 그려지기
 *       때문입니다. 충돌과 ::enemy_hitscan은 이를 건너뛰며, 그것이 시체가 뒤에 있는 것을
 *       겨눈 사격을 대신 받아 내지 않게 합니다.
 */
typedef enum {
    E_IDLE,    /**< Has not seen the player. / 플레이어를 보지 못한 상태. */
    E_CHASE,   /**< Walking towards the player. / 플레이어를 향해 걷는 중. */
    E_ATTACK,  /**< Winding up an attack, or resting after one. / 공격 준비 중이거나 공격 후 휴식 중. */
    E_HURT,    /**< Briefly flinching after a hit. / 피격 후 잠시 경직된 상태. */
    E_DEAD     /**< A corpse: drawn, but neither collides nor can be hit. / 시체. 그려지지만 충돌하지도 맞지도 않습니다. */
} EState;

/* --- Structures / 구조체 --- */

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

    /**
     * @brief Does nothing at all. Exists to be destroyed.
     *
     * ENGLISH: The other two answer "how does it come at you". This one answers
     * that it does not -- a ward is a target, not an opponent, and its threat
     * is entirely in what shooting it produces. Given a behaviour of its own
     * rather than an ::AI_BRAWLER with zero reach, because zero reach is a
     * number somebody would later "fix" and this is a decision.
     *
     * 한국어: 나머지 둘은 "어떻게 덤벼오는가"에 답합니다. 이것은 덤비지 않는다고 답합니다.
     * 결계핵은 상대가 아니라 표적이며, 그 위협은 전적으로 그것을 쏘았을 때 나오는 것에
     * 있습니다. 사거리 0인 ::AI_BRAWLER가 아니라 자기 행동을 준 이유는, 사거리 0은 나중에
     * 누군가 "고칠" 숫자이고 이것은 결정이기 때문입니다.
     */
    AI_INERT,

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
     * only a threat if something can be out over them. ::MON_CASTER carries it,
     * and is the only kind that does. It was carried by a `wraith` first, a row
     * that was a caster plus this bit; the bit turned out to be the whole of
     * the difference, so it moved and the row went.
     *
     * ONLY WHILE IT IS ALIVE. This suppresses the fall for a monster that is
     * flying, and a corpse is not flying: ::E_DEAD hands a flyer back to
     * gravity, so a caster shot out of the air drops rather than hanging at the
     * height it was killed at. ::holds_height in enemy.c is where that is said,
     * and it is the difference between this bit and ::MON_ANCHORED, which holds
     * through death because a thing bolted to a wall is still bolted to it.
     *
     * 한국어: 바닥으로 떨어지는 대신 생성된 높이를 유지합니다. 이 세계의 모든 것은 떨어지며,
     * 지금까지 그것은 종류별로 정해진 것이 아니라 ::enemy_update 안에 적혀 있었습니다. 비행체는
     * 로드맵의 수직 아레나가 원하는 것입니다. 떠 있는 발판과 협곡은 그 위로 나올 수 있는 무언가가
     * 있어야만 위협이 됩니다. ::MON_CASTER가 이 비트를 가지며, 가진 유일한 종류입니다. 처음
     * 이것을 가진 것은 `wraith`였습니다. 캐스터에 이 비트를 더한 행이었고, 결국 그 비트가 차이의
     * 전부였으므로 비트가 옮겨 가고 행은 사라졌습니다.
     *
     * *살아 있는 동안만입니다.* 이것은 *날고 있는* 몬스터의 낙하를 억제하며, 시체는 날고 있지
     * 않습니다. ::E_DEAD는 비행체를 중력에 돌려주므로, 공중에서 격추된 캐스터는 죽은 높이에
     * 걸려 있지 않고 떨어집니다. 그 말이 적힌 곳은 enemy.c의 ::holds_height이며, 그것이 이
     * 비트와 ::MON_ANCHORED의 차이입니다. 후자는 죽음을 넘어 유지됩니다. 벽에 박힌 것은 여전히
     * 벽에 박혀 있기 때문입니다.
     */
    MON_FLIES = 1 << 0,

    /**
     * @brief Never moves, and keeps the height it was placed at.
     *
     * ENGLISH
     * -------
     * A SEPARATE BIT FROM ::MON_FLIES, and the temptation to reuse that one is
     * exactly why. ::MON_FLIES already means two things to two readers --
     * ::make_monster reads it as "the placed origin is the feet", and the FGD
     * draws the editor box sitting ON the origin because of it. A wall-mounted
     * maw needs the second meaning and not the first, and teaching one bit a
     * third meaning is how a flag stops being checkable.
     *
     * What it suppresses is MOVEMENT, not aiming: an anchored caster still
     * turns to face the player and still shoots. ::AI_CASTER minus the footwork
     * is "it shoots", which is the whole of what the maw does.
     *
     * AND IT OUTLIVES THE MONSTER, which is the other place it parts company
     * with ::MON_FLIES. A dead flyer falls; a dead anchored thing does not,
     * because what held it up was never that it was alive. See ::holds_height.
     *
     * 한국어
     * ------
     * @brief 결코 움직이지 않으며, 놓인 높이를 그대로 지킵니다.
     *
     * *::MON_FLIES와 별개의 비트이며*, 그것을 재사용하고 싶은 유혹이 정확히 그 이유입니다.
     * ::MON_FLIES는 이미 두 독자에게 두 가지를 뜻합니다. ::make_monster는 "놓인 원점이 발"로
     * 읽고, FGD는 그 때문에 편집기 상자를 원점 *위에* 그립니다. 벽에 박힌 아귀는 두 번째
     * 의미만 필요하고 첫 번째는 필요 없으며, 비트 하나에 세 번째 의미를 가르치는 것이 곧
     * 플래그가 검사 불가능해지는 방식입니다.
     *
     * 억제하는 것은 *이동*이지 조준이 아닙니다. 고정된 캐스터도 여전히 플레이어를 향해 돌고
     * 여전히 쏩니다. 발놀림을 뺀 ::AI_CASTER는 "쏜다"이고, 그것이 아귀가 하는 일의 전부입니다.
     *
     * *그리고 몬스터보다 오래 갑니다.* ::MON_FLIES와 갈라서는 또 하나의 지점입니다. 죽은
     * 비행체는 떨어지지만 죽은 고정물은 떨어지지 않습니다. 그것을 떠받치던 것은 애초에 살아
     * 있다는 사실이 아니었기 때문입니다. ::holds_height를 참조하십시오.
     */
    MON_ANCHORED = 1 << 1,

    /**
     * @brief This is the boss. ::world_boss_present is a scan for this bit.
     *
     * ENGLISH
     * -------
     * world.h used to state that there was no boss flag and deliberately would
     * not add one, deriving "something big is up" from a live brute instead. It
     * also stated the condition under which that answer moves: "If a later
     * bestiary needs a real boss the answer moves into ::MonType and this
     * function keeps its signature." This is that move. The brute keeps its
     * stats and loses the soundtrack, which was the point of the derivation and
     * is now a thing the boss says for itself.
     *
     * 한국어
     * ------
     * @brief 이것이 보스입니다. ::world_boss_present는 이 비트에 대한 스캔입니다.
     *
     * world.h는 보스 플래그가 없으며 의도적으로 만들지 않는다고 밝히고, "큰 것이 떴다"를 살아
     * 있는 브루트에서 유도했습니다. 동시에 그 답이 옮겨 가는 조건도 밝혔습니다. "이후의 도감이
     * 진짜 보스를 필요로 하면 답은 ::MonType으로 옮겨 가고 이 함수는 시그니처를 유지합니다."
     * 이것이 그 이동입니다. 브루트는 수치를 유지하고 사운드트랙을 잃습니다. 그것이 유도의
     * 요점이었고, 이제는 보스가 스스로 말하는 것이 되었습니다.
     */
    MON_BOSS = 1 << 2,

    /**
     * @brief While one of these lives, every ::MON_BOSS is untouchable.
     *
     * ENGLISH: The rule is enforced in ::enemy_hurt and nowhere else, for the
     * reason the death tally is counted there and nowhere else -- a damage
     * source added later has to go through that function to exist at all, so it
     * cannot forget. That is what makes the hook's arrival damage, the
     * grenade's splash and the shotgun's pellets all obey a rule none of them
     * has heard of.
     *
     * 한국어: 이 규칙은 ::enemy_hurt에서만 강제되며, 사망 집계를 그곳에서만 세는 것과 같은
     * 이유입니다. 나중에 추가되는 피해원도 존재하려면 그 함수를 거쳐야 하므로 잊을 수가
     * 없습니다. 훅의 도달 피해, 유탄의 폭발, 샷건의 펠릿이 전부, 들어 본 적도 없는 규칙을
     * 따르게 되는 이유가 그것입니다.
     */
    MON_GUARD = 1 << 3,

    /**
     * @brief Drawn bobbing rather than standing still. Costs nothing but pixels.
     *
     * ENGLISH
     * -------
     * A LOOK, NOT A BEHAVIOUR, and the only flag here that is. ::MON_FLIES
     * changes where a monster may go; this changes only where its picture is
     * drawn, and ::Enemy::pos is untouched so collision, aim and every distance
     * in enemy.c are measured against the same point they always were.
     *
     * WHY A FLAG AND NOT A TYPE TEST. scene.c wanted `type == MON_WATER_SPIRIT
     * || (flags & MON_FLIES)`, which is the reading ::MonBehaviour's own note
     * calls out: three `shot_speed > 0` tests each meant "is this a caster" and
     * each had to be hunted down when a third archetype arrived. A water spirit
     * floats because it is water and a caster floats because it is off the
     * floor -- two reasons, one appearance, and the table is where a row says
     * which appearance it has.
     *
     * 한국어
     * ------
     * @brief 가만히 서 있는 대신 떠 있는 것으로 그립니다. 픽셀 말고는 아무 비용이 없습니다.
     *
     * *행동이 아니라 겉모습*이며, 이곳에서 그런 유일한 플래그입니다. ::MON_FLIES는 몬스터가
     * 어디로 갈 수 있는지를 바꿉니다. 이것은 그 그림이 어디에 그려지는지만 바꾸며,
     * ::Enemy::pos는 손대지 않으므로 충돌과 조준과 enemy.c의 모든 거리는 늘 재던 같은 점에
     * 대해 재집니다.
     *
     * *왜 종류 검사가 아니라 플래그인가.* scene.c는 `type == MON_WATER_SPIRIT ||
     * (flags & MON_FLIES)`를 원했는데, 그것은 ::MonBehaviour의 주석 자신이 지목하는 읽기입니다.
     * 세 개의 `shot_speed > 0` 검사가 각각 "이것은 캐스터인가"를 뜻했고 세 번째 아키타입이
     * 왔을 때 각각을 찾아내야 했습니다. 물 정령이 떠 있는 것은 물이기 때문이고 캐스터가 떠
     * 있는 것은 바닥에서 떨어져 있기 때문입니다. 이유는 둘, 겉모습은 하나이며, 어떤 겉모습을
     * 지니는지는 표의 행이 말합니다.
     */
    MON_FLOATS = 1 << 4,

    /**
     * @brief Takes a hit without showing it: the flash, but no flinch shake.
     *
     * ENGLISH
     * -------
     * THE ONE ROW THAT CARRIES IT IS THE BRUTE, and it is the same fact its
     * own stats already state twice -- ::MonType::pain_lock is 2.2 seconds
     * against everything else's fraction, and the comment above the row says
     * it "cannot be stun-locked". A wall that rocks when you shoot it is not a
     * wall. This is that character reaching the picture.
     *
     * NEGATIVE, WHICH IS UNUSUAL HERE AND DELIBERATE. Every other flag says
     * what a monster DOES; a positive `MON_SHAKES` would sit on four rows out
     * of five and the table would read as though shaking were the exception.
     * One row is the exception, so one row carries the flag.
     *
     * @note The flash is NOT exempted. A hit that shows nothing at all is a
     *       hit the player cannot tell from a miss, which is a different
     *       complaint from "it does not stagger".
     *
     * 한국어
     * ------
     * @brief 맞아도 티를 내지 않습니다. 점멸은 하되 경직 흔들림은 없습니다.
     *
     * *이것을 지닌 유일한 행은 브루트*이며, 자기 수치가 이미 두 번 말하는 것과 같은 사실입니다.
     * ::MonType::pain_lock이 나머지 전부의 소수점 아래에 대해 2.2초이고, 행 위의 주석이 "스턴
     * 락에 걸리지 않는다"고 적습니다. 쏘면 흔들리는 벽은 벽이 아닙니다. 이것은 그 성격이 그림에
     * 닿는 것입니다.
     *
     * *부정형이며, 이곳에서는 드물고 의도적입니다.* 다른 모든 플래그는 몬스터가 *하는* 일을
     * 말합니다. 긍정형 `MON_SHAKES`는 다섯 행 중 넷에 붙고, 표는 흔들리는 것이 예외인 것처럼
     * 읽힙니다. 예외는 한 행이므로 플래그도 한 행이 집니다.
     *
     * @note 점멸은 면제되지 않습니다. 아무것도 보여 주지 않는 타격은 플레이어가 빗나감과
     *       구별할 수 없는 타격이며, 그것은 "휘청이지 않는다"와는 다른 불만입니다.
     */
    MON_UNFLINCHING = 1 << 5
};

/** @brief Every bit above, for ::types_check to object to anything else. / 위의 모든 비트. ::types_check가 그 외의 것에 이의를 제기하기 위한 것입니다. */
#define MON_FLAGS_ALL (MON_FLIES | MON_ANCHORED | MON_BOSS | MON_GUARD | MON_FLOATS | MON_UNFLINCHING)

/**
 * @brief How many attacks one kind may carry.
 *
 * ENGLISH: Three, because the bands are three -- melee, near, mid -- and a kind
 * with an answer to each of them has an answer everywhere. A fourth would be
 * two answers in one band, which ::MonAttack::weight already expresses without
 * a slot. Slots past a kind's last are ::ATK_NONE and cost their bytes in
 * `.rodata`, which is the price of the table being readable as a table.
 *
 * 한국어: 셋입니다. 대역이 셋(근접·근거리·중거리)이고, 각각에 답을 가진 종류는 어디에서나 답을
 * 가집니다. 넷째는 한 대역 안의 두 답이며, 그것은 ::MonAttack::weight이 슬롯 없이 이미
 * 표현합니다. 종류의 마지막 다음 슬롯은 ::ATK_NONE이고 `.rodata`에서 자기 바이트를 씁니다.
 * 그것이 표가 표로 읽히는 값입니다.
 */
#define MON_MAX_ATTACKS 3

/**
 * @brief What a ::MonAttack slot does when it goes off.
 *
 * ENGLISH: Two, and they are the two `release_*` functions that already
 * existed -- this enum names the branch ::MonType::behaviour used to make. The
 * difference is that the branch is now per ATTACK rather than per KIND, which
 * is the whole of what the slots buy: an ::AI_CASTER may carry a swing.
 *
 * 한국어: 둘이며, 이미 있던 두 `release_*` 함수입니다. 이 열거형은 ::MonType::behaviour가
 * 내리던 분기에 이름을 붙입니다. 달라진 것은 그 분기가 이제 *종류*가 아니라 *공격*마다라는
 * 것이고, 그것이 슬롯이 사 주는 것의 전부입니다. ::AI_CASTER가 휘두르기를 지닐 수 있습니다.
 */
typedef enum {
    ATK_NONE = 0,  /**< An empty slot. / 빈 슬롯. */
    ATK_SWING,     /**< Reaches out and hits, at once. / 뻗어서 즉시 때립니다. */
    ATK_BOLT       /**< Launches a projectile. / 발사체를 쏘아 보냅니다. */
} AtkKind;

/**
 * @brief What one attack of a monster is.
 *
 * ENGLISH
 * -------
 * A MONSTER USED TO HAVE EXACTLY ONE, and that was a limit nobody chose. The
 * columns below all lived on ::MonType, one of each, so "what this monster does
 * when you are close" and "what it does across the room" could not both be
 * answered. ::MON_ODDS_ALSO_MELEE is the proof it was a limit rather than a
 * design: it is Quake's rule that a monster which can also bite shoots less
 * while closing, it has been written down with its reasoning since the bands
 * arrived, and nothing has ever been able to read it, because nothing could
 * both bite and shoot.
 *
 * THE BAND IS THE SLOT'S OWN, ::min to ::max. A monster picks among the slots
 * whose band it is standing in, so the same creature answers a charge and a
 * standoff differently -- and a gap in the union of a type's bands is a range
 * at which it does nothing, which ::types_check refuses.
 *
 * SLOT 0 IS ALSO WHERE THE ARCHETYPE STANDS. ::chase_caster keeps its distance
 * around a number and ::chase_brawler closes to one; both read slot 0's ::max,
 * because slot 0 is the attack the archetype was built around and the others
 * are what it does when the player is somewhere it did not plan for. That is
 * one number rather than a movement column beside an attack column, which is
 * the arrangement this replaced: `MonType::attack` answered both questions and
 * could only ever give them the same answer.
 *
 * NO SLOT CARRIES ART. Every attack a monster has is drawn with the one
 * `<name>_attack` frame it already has, and that is a constraint rather than an
 * oversight -- the atlas is a fixed grid on a floppy, and a second attack that
 * needed a second drawing would cost more than the attack is worth. What tells
 * a swing from a bolt is what the sprite DOES: scene.c drives it along the
 * facing for a swing, and motion is free where a frame is not.
 *
 * 한국어
 * ------
 * @brief 몬스터의 공격 하나가 무엇인가.
 *
 * *몬스터는 정확히 하나를 가졌고*, 그것은 아무도 고르지 않은 한계였습니다. 아래의 열들은 전부
 * ::MonType에 하나씩 살았으므로 "가까이 있을 때 이것이 하는 일"과 "방 건너에 있을 때 하는 일"에
 * 함께 답할 수 없었습니다. ::MON_ODDS_ALSO_MELEE이 그것이 설계가 아니라 한계였다는 증거입니다.
 * 물 수도 있는 몬스터는 거리를 좁히는 동안 덜 쏜다는 Quake의 규칙이고, 대역이 생긴 이래 근거와
 * 함께 적혀 있었으며, 무엇도 그것을 읽을 수 없었습니다. 물면서 쏠 수 있는 것이 없었기
 * 때문입니다.
 *
 * *대역은 슬롯 자신의 것*이며 ::min에서 ::max까지입니다. 몬스터는 자기가 서 있는 대역의 슬롯들
 * 중에서 고르므로, 같은 생물이 돌격과 대치에 다르게 답합니다. 한 종류의 대역 합집합에 난 구멍은
 * 아무것도 하지 않는 거리이며 ::types_check가 그것을 거절합니다.
 *
 * *슬롯 0은 아키타입이 서는 자리이기도 합니다.* ::chase_caster는 어떤 수 주위로 거리를 유지하고
 * ::chase_brawler는 그 수까지 붙습니다. 둘 다 슬롯 0의 ::max를 읽습니다. 슬롯 0이 아키타입이
 * 지어진 공격이고 나머지는 플레이어가 예정에 없던 자리에 있을 때 하는 일이기 때문입니다. 그것은
 * 공격 열 곁의 이동 열이 아니라 하나의 수입니다. 이것이 대체한 배치가 그러했고, `MonType::attack`
 * 은 두 질문에 답하면서 둘에게 같은 답만 줄 수 있었습니다.
 *
 * *어떤 슬롯도 그림을 지니지 않습니다.* 몬스터가 가진 모든 공격은 이미 있는 `<이름>_attack`
 * 프레임 하나로 그려지며, 그것은 실수가 아니라 제약입니다. 아틀라스는 플로피 위의 고정 격자이고,
 * 두 번째 그림을 필요로 하는 두 번째 공격은 그 공격의 값어치보다 비쌉니다. 휘두르기와 볼트를
 * 가르는 것은 스프라이트가 *하는 일*입니다. scene.c가 휘두르기에 대해 바라보는 방향으로 몰며,
 * 프레임이 공짜가 아닌 곳에서 움직임은 공짜입니다.
 */
typedef struct {
    /**
     * @brief Which release this slot calls.
     *
     * ENGLISH: ::ATK_NONE is zero so a slot nobody filled is not an attack, and
     * that is how a type's count is read -- the slots run to the first empty one
     * and there is no separate length to fall out of step with the table.
     *
     * 한국어: ::ATK_NONE이 0이므로 아무도 채우지 않은 슬롯은 공격이 아니며, 종류의 개수를 읽는
     * 방법이 그것입니다. 슬롯은 첫 번째 빈 것까지이고, 표와 어긋날 수 있는 별도의 길이가
     * 없습니다.
     */
    short kind;

    /**
     * @brief The range band this attack answers, metres.
     *
     * ENGLISH: Bands may overlap -- two slots offered at one distance is a
     * choice, which is what ::weight is for. They may not leave a hole between
     * them, because a hole is a distance at which the monster stands and does
     * nothing; ::types_check refuses one.
     *
     * 한국어: 이 공격이 답하는 사거리 대역(미터)입니다. 대역은 겹쳐도 됩니다. 한 거리에서 두
     * 슬롯이 함께 제안되는 것은 선택이며 ::weight이 그것을 위해 있습니다. 사이에 구멍을 남겨서는
     * 안 됩니다. 구멍은 몬스터가 서서 아무것도 하지 않는 거리이고, ::types_check가 그것을
     * 거절합니다.
     */
    float min, max;

    int   damage;       /**< Damage ONE BOLT or one swing deals -- see ::burst. / 볼트 *하나* 또는 휘두르기 한 번의 피해량. ::burst를 참조하십시오. */

    float windup;       /**< Seconds of telegraph before the attack lands. / 공격이 닿기 전의 예비 동작 시간(초). */

    /**
     * @brief Seconds after the LAST bolt before the next attack may start.
     *
     * ENGLISH
     * -------
     * AFTER THE LAST, NOT AFTER THE FIRST, and the difference is the whole of
     * what a volley costs. ::enemy_update starts this clock at
     * `windup + (n - 1) * shot_gap`, so a monster is planted for the wind-up,
     * then the firing, then this -- and a stream that began resting while still
     * shooting would let its next volley overlap the one in the air.
     *
     * @note WHICH MEANS IT IS NOT THE SAME NUMBER IT WAS. A row that fired
     *       everything in one frame spent nothing on the firing, so its rest
     *       WAS its exposure. Giving that row a ::shot_gap without lowering
     *       this leaves the monster standing still for the rest plus the whole
     *       volley -- and a monster that became easier to shoot because its
     *       attack got longer is a balance change nobody asked for. The water
     *       spirit's came down from 0.85 to 0.50 when it stopped firing a
     *       shotgun, which is roughly what the firing time added.
     *
     * 한국어
     * ------
     * @brief *마지막* 볼트 뒤부터 다음 공격이 시작될 수 있을 때까지의 초.
     *
     * *첫 발이 아니라 마지막 발 뒤이며*, 그 차이가 일제 사격이 치르는 값의 전부입니다.
     * ::enemy_update는 이 시계를 `windup + (n - 1) * shot_gap`에서 시작하므로, 몬스터는 준비
     * 동작 동안, 그다음 사격 동안, 그다음 이것 동안 붙박여 있습니다. 아직 쏘는 중에 휴식을
     * 시작하는 줄기는 다음 일제 사격이 공중의 것과 겹치게 만듭니다.
     *
     * @note *따라서 이것은 예전과 같은 수가 아닙니다.* 한 프레임에 전부 쏘던 행은 사격에
     *       아무것도 쓰지 않았으므로 그 행의 휴식이 곧 노출 시간이었습니다. 그런 행에
     *       ::shot_gap을 주면서 이것을 낮추지 않으면, 몬스터는 휴식 *더하기* 일제 사격 전체
     *       동안 서 있게 됩니다. 그리고 공격이 길어졌다는 이유로 쏘아 맞히기 쉬워진 몬스터는
     *       아무도 요청하지 않은 밸런스 변경입니다. 물의 정령의 값은 산탄을 그만두면서 0.85에서
     *       0.50으로 내려갔고, 그것은 사격 시간이 더한 양과 대략 같습니다.
     */
    float cooldown;

    float shot_speed;   /**< Projectile speed, m/s. Read only by ::AI_CASTER. / 발사체 속도(m/s). ::AI_CASTER만 읽습니다. */

    /**
     * @brief The MOST bolts one attack releases. 1 is a single aimed shot.
     *
     * ENGLISH
     * -------
     * A volley is not a faster single shot. One accurate bolt is answered by
     * stepping aside; a stream of them is answered by getting out of where it
     * is pointing, which is a different move at a different distance.
     *
     * RELEASED OVER TIME, ONE EVERY ::shot_gap, AND THAT IS A CHANGE. They used
     * to leave together in a cone -- a shotgun, arriving as one event that
     * either hit or did not. A stream is the opposite bargain: every bolt is a
     * separate chance to be somewhere else by the time it arrives, which is
     * what makes ::ENEMY_MAX_SHOTS worth spending and what pays for the smaller
     * ::SHOT_RADIUS. The counter that made it possible is ::Enemy::swung, which
     * was already there as a flag and is now a count.
     *
     * WITH ::burst_min, THIS IS A RANGE rather than a number -- see there for
     * why a volley whose length you cannot predict is the point of one.
     *
     * 한국어
     * ------
     * @brief 한 번의 공격이 내보내는 볼트의 *최대* 수. 1이면 조준된 단발입니다.
     *
     * 일제 사격은 더 빠른 단발이 아닙니다. 정확한 볼트 하나에는 옆으로 비켜서면 되고, 줄기로
     * 쏟아지는 쪽에는 그것이 겨누는 자리에서 벗어나야 합니다. 다른 동작이고 다른 거리입니다.
     *
     * *::shot_gap마다 하나씩, 시간에 걸쳐 나가며, 그것이 바뀐 점입니다.* 예전에는 원뿔로 함께
     * 떠났습니다. 산탄이었고, 맞거나 맞지 않거나인 하나의 사건으로 도착했습니다. 줄기는 반대의
     * 거래입니다. 볼트마다 도착할 즈음 다른 곳에 있을 기회가 따로 생기며, 그것이
     * ::ENEMY_MAX_SHOTS를 쓸 값어치를 만들고 작아진 ::SHOT_RADIUS의 값을 치릅니다. 이것을
     * 가능하게 한 계수기는 ::Enemy::swung이며, 이미 플래그로 있던 것이 이제 개수입니다.
     *
     * ::burst_min과 함께 이것은 수가 아니라 *범위*입니다. 길이를 예측할 수 없는 일제 사격이
     * 왜 요점인지는 그곳을 참조하십시오.
     */
    short burst;

    /**
     * @brief The FEWEST bolts one attack releases. Equal to ::burst means fixed.
     *
     * ENGLISH
     * -------
     * WHAT A RANGE BUYS IS THAT THE VOLLEY CANNOT BE COUNTED. A stream of
     * exactly eight is a rhythm, and a rhythm is learned once and then free:
     * the player leans out on the ninth beat forever. Rolled fresh per volley,
     * the only safe answer is to watch the monster rather than the metronome.
     *
     * NOT THE SAME KNOB AS ::spread. Spread says how wide the stream is, this
     * says how long it lasts, and they are answered by different moves --
     * sideways against patience.
     *
     * @note Must not exceed ::burst; ::types_check enforces it. A row with the
     *       two the wrong way round would roll an empty volley, which is a
     *       monster that winds up and does nothing.
     *
     * 한국어
     * ------
     * @brief 한 번의 공격이 내보내는 볼트의 *최소* 수. ::burst와 같으면 고정입니다.
     *
     * *범위가 사 주는 것은 일제 사격을 셀 수 없다는 것입니다.* 정확히 여덟 발의 줄기는
     * 박자이고, 박자는 한 번 익히면 그 뒤로는 공짜입니다. 플레이어는 아홉 번째 박에 몸을
     * 내밀면 되고 언제까지나 그렇습니다. 일제 사격마다 새로 굴리면, 안전한 답은 메트로놈이
     * 아니라 몬스터를 보는 것뿐입니다.
     *
     * *::spread와 같은 손잡이가 아닙니다.* 산포는 줄기가 얼마나 *넓은가*를 말하고 이것은
     * 얼마나 *오래*인가를 말하며, 답하는 동작이 다릅니다. 옆으로 비키는 것과 기다리는 것입니다.
     *
     * @note ::burst를 넘을 수 없으며 ::types_check가 강제합니다. 둘이 뒤바뀐 행은 빈 일제
     *       사격을 굴리게 되고, 그것은 준비 동작만 하고 아무것도 하지 않는 몬스터입니다.
     */
    short burst_min;

    /**
     * @brief How wide the volley scatters, as a fraction of the distance.
     *
     * ENGLISH: A fraction and not an angle, because what the player judges is
     * how far to move: a cone of fixed ANGLE doubles in width at twice the
     * range, so the sidestep would grow with distance. Ignored when ::burst is
     * 1 -- a single shot that scattered is a monster that misses, not one that
     * sprays.
     *
     * 한국어: 각도가 아니라 비율인 이유는 플레이어가 판단하는 것이 *얼마나 움직일까*이기
     * 때문입니다. 고정된 각도의 원뿔은 두 배 거리에서 두 배로 넓어져 옆걸음이 거리에 따라
     * 커집니다. ::burst가 1이면 무시됩니다. 흩어지는 단발은 난사가 아니라 빗맞히는
     * 몬스터입니다.
     */
    float spread;

    /**
     * @brief Seconds between one bolt of a volley and the next.
     *
     * ENGLISH
     * -------
     * THE CADENCE, AND IT IS WHAT MAKES A STREAM READABLE. Fast enough and the
     * volley is a wall arriving at once, which is the shotgun this replaced;
     * slow enough and it is a sequence of single shots with pauses to walk
     * through. The number worth having is the one where the player can see the
     * next bolt coming and still has to keep moving to be out of its way.
     *
     * @note Ignored when ::burst is 1 -- there is no second bolt to be apart
     *       from. Rows that fire once leave it 0 rather than a plausible-
     *       looking number nobody reads.
     * @note MULTIPLIED BY THE VOLLEY LENGTH, this is most of how long the
     *       monster is committed: ::MonAttack::windup opens the attack, then
     *       `(n - 1) * shot_gap` of firing, then ::MonAttack::cooldown. A long
     *       volley is a long time not turning, which is the counterplay --
     *       see ::Enemy::volley_at.
     *
     * 한국어
     * ------
     * @brief 일제 사격의 한 볼트와 다음 볼트 사이의 간격(초).
     *
     * *박자이며, 줄기를 읽을 수 있게 만드는 것이 이것입니다.* 충분히 빠르면 일제 사격은 한꺼번에
     * 도착하는 벽이 되는데 그것이 이 변경이 대체한 산탄입니다. 충분히 느리면 걸어서 지나갈 수
     * 있는 단발의 나열이 됩니다. 가질 값어치가 있는 수는, 플레이어가 다음 볼트가 오는 것을 볼 수
     * 있으면서도 비켜 있으려면 계속 움직여야 하는 그 값입니다.
     *
     * @note ::burst가 1이면 무시됩니다. 사이가 벌어질 두 번째 볼트가 없기 때문입니다. 단발인
     *       행은 아무도 읽지 않는 그럴듯한 수 대신 0을 둡니다.
     * @note *일제 사격의 길이와 곱해져*, 몬스터가 묶여 있는 시간의 대부분이 됩니다.
     *       ::MonAttack::windup이 공격을 열고, `(n - 1) * shot_gap` 동안 쏘고, 그다음
     *       ::MonAttack::cooldown입니다. 긴 일제 사격은 오래 돌지 못한다는 뜻이며 그것이
     *       대응 수단입니다. ::Enemy::volley_at을 참조하십시오.
     */
    float shot_gap;
    /**
     * @brief How much this slot is preferred where more than one is offered.
     *
     * ENGLISH
     * -------
     * TWO ROLLS, AND THEY ASK DIFFERENT THINGS. ::MON_ODDS_MELEE and its
     * neighbours decide WHETHER the monster attacks at all from where it is
     * standing -- Quake's CheckAttack, willingness by band. This decides WHICH
     * of the slots that band offers. Folding them into one number would make a
     * monster with two attacks twice as aggressive as the same monster with
     * one, which is a difficulty change nobody asked for, arrived at by adding
     * an option.
     *
     * RELATIVE, NOT A PROBABILITY. The weights of the offered slots are summed
     * and one is drawn against the sum, so a slot's number means nothing on its
     * own and a row can be tuned without touching its neighbours' figures.
     *
     * 한국어
     * ------
     * @brief 둘 이상이 제안될 때 이 슬롯이 얼마나 선호되는가.
     *
     * *굴림이 둘이고 서로 다른 것을 묻습니다.* ::MON_ODDS_MELEE와 그 이웃들은 몬스터가 지금 선
     * 자리에서 *공격을 하기는 하는지*를 정합니다. Quake의 CheckAttack이며 대역별 의향입니다.
     * 이것은 그 대역이 제안하는 슬롯 중 *무엇을* 정합니다. 둘을 한 수로 접으면 공격이 둘인
     * 몬스터가 하나인 같은 몬스터보다 두 배로 공격적이 되며, 그것은 선택지를 더한 것으로
     * 아무도 요청하지 않은 난이도 변경을 하는 일입니다.
     *
     * *확률이 아니라 상대값입니다.* 제안된 슬롯들의 가중치를 합하고 그 합에 대해 하나를
     * 뽑으므로, 슬롯의 수는 혼자서는 아무 뜻이 없고 한 행을 이웃의 수치를 건드리지 않고 조정할
     * 수 있습니다.
     */
    float weight;
} MonAttack;


/**
 * @struct MonType
 * @brief Everything that makes one kind of monster differ from another.
 *
 * ENGLISH
 * -------
 * A stat block, not code. Adding a kind is a row in this table plus a body in
 * sprite.c, and no new code path -- which is the whole reason the AI switches
 * on ::MonType::behaviour rather than on the type id.
 *
 * @note Read through ::mon_stats, never indexed directly, so an id outside the
 *       enum lands somewhere that exists.
 *
 * 한국어
 * ------
 * @brief 한 종류의 몬스터를 다른 몬스터와 다르게 만드는 모든 것.
 *
 * 코드가 아니라 수치 묶음입니다. 종류를 추가하는 것은 이 표의 행 하나와 sprite.c의 몸체
 * 하나이며 새로운 코드 경로는 없습니다. AI가 타입 식별자가 아니라 ::MonType::behaviour로
 * 분기하는 이유가 바로 그것입니다.
 *
 * @note 직접 인덱싱하지 않고 ::mon_stats를 통해 읽으므로, 열거형 바깥의 식별자도 존재하는
 *       곳에 떨어집니다.
 */
typedef struct {
    /** @brief The entity name that places this kind, as written in level text. Beside the stats so the two cannot drift. / 이 종류를 배치하는 엔티티 이름. 수치 옆에 두어 둘이 어긋날 수 없게 합니다. */
    const char *name;
    int   behaviour;    /**< A ::MonBehaviour: how this kind fights. / ::MonBehaviour 값. 이 종류가 어떻게 싸우는가. */
    int   hp;           /**< Starting health. / 시작 체력. */
    float speed;        /**< Walking speed, m/s. / 이동 속도(m/s). */

    /**
     * @brief How far off a straight line this kind closes, in radians.
     *
     * ENGLISH
     * -------
     * A MONSTER THAT WALKS STRAIGHT AT YOU IS A MONSTER YOU STRAFE PAST.
     * `chase_brawler` moved along the vector to the player and nothing else,
     * so the approach was a line: predictable at any speed, and the faster it
     * got the easier it was to sidestep, because a fast straight line arrives
     * sooner without arriving anywhere new.
     *
     * The approach vector is rotated by this much, to the side ::Enemy::lefty
     * is committed to, and that commitment flips on ::MON_SLIDE_HOLD -- the
     * same clock the close-range strafe uses, and deliberately the same: one
     * monster holds ONE direction at a time, so a weave and a strafe are the
     * same decision seen at two ranges rather than two systems that can
     * disagree.
     *
     * IT COSTS CLOSING SPEED, by `cos` of itself: at 0.62 rad a monster closes
     * at 81% of ::speed. That is paid for in the speed column rather than
     * hidden, because the two are read together and a weave tuned against a
     * speed it does not know is a weave that reads as slowness.
     *
     * Zero is a straight line, which is what ::AI_INERT and the anchored boss
     * want: neither goes anywhere, and a weave on something that does not walk
     * would be a column with a value and no meaning.
     *
     * 한국어
     * ------
     * @brief 이 종류가 직선에서 얼마나 벗어나 접근하는지, 라디안.
     *
     * *곧장 걸어오는 몬스터는 옆으로 지나쳐 버리면 그만인 몬스터입니다.* `chase_brawler`는
     * 플레이어를 향한 벡터로만 움직였으므로 접근이 직선이었습니다. 어떤 속도에서든
     * 예측 가능하며, 빨라질수록 오히려 피하기 쉬워집니다. 빠른 직선은 더 일찍 도착할 뿐
     * 새로운 곳에 도착하지 않기 때문입니다.
     *
     * 접근 벡터를 이만큼, ::Enemy::lefty가 정해 둔 쪽으로 회전시키며, 그 결정은
     * ::MON_SLIDE_HOLD마다 뒤집힙니다. 근접 횡이동이 쓰는 것과 같은 시계이고 일부러
     * 같습니다. 한 몬스터는 한 번에 *한* 방향을 지니므로, 갈지자와 횡이동은 서로 어긋날 수
     * 있는 두 시스템이 아니라 두 거리에서 본 같은 결정입니다.
     *
     * *접근 속도를 자기 `cos`만큼 치릅니다.* 0.62라디안이면 ::speed의 81%로 다가옵니다.
     * 숨기지 않고 속도 열에서 지불하는 이유는 둘이 함께 읽히기 때문이며, 자기가 모르는
     * 속도에 맞춰 조율된 갈지자는 느림으로 읽히는 갈지자입니다.
     *
     * 0은 직선이며 ::AI_INERT와 고정된 보스가 원하는 것입니다. 둘 다 아무 데도 가지 않고,
     * 걷지 않는 것에 붙은 갈지자는 값은 있고 뜻은 없는 열입니다.
     */
    float weave;

    /* --- HOW BIG IT IS -----------------------------------------------------
     *
     * ENGLISH
     * -------
     * These three plus ::aspect are the whole of a monster's size. There is no
     * per-instance scale, so a kind is exactly as big as its row says.
     *
     * THE DRAWN BILLBOARD IS `height` TALL AND `height * aspect` WIDE, which
     * makes ::height the one number that resizes a monster proportionally.
     *
     * WHAT FOLLOWS height ON ITS OWN: the top of the hitscan cylinder, the step
     * it can climb (::mon_step, height/3 but never under ::PLAYER_STEP -- a
     * kind shrunk below that keeps the player's stride and stops shrinking in
     * this one respect, because the stairs belong to the level), the ceiling
     * clearance a spawn needs, where the grapple attaches, and where blood
     * sprays.
     *
     * WHAT DOES NOT, and has to be scaled by hand in the same edit:
     *   ::radius  -- the cylinder is `radius` wide and `height` tall, so a
     *                taller monster left at the old radius is a bigger target
     *                that is no easier to hit.
     *   ::eye     -- bolts leave from here and sight is measured from here, so
     *                a monster that grew without it shoots from its navel.
     *   ::attack  -- reach is measured from the centre, and a longer body with
     *                the old reach swings and connects with nothing.
     *
     * ::aspect is per kind and NOT the art's own 64x96 cell, so the sprite is
     * already stretched to it. Leave it alone for a proportional resize.
     *
     * @note ::types_check does not look at any of this. `eye` above `height`
     *       is played rather than caught.
     *
     * 한국어
     * ------
     * 이 셋과 ::aspect가 몬스터 크기의 전부입니다. 개체별 배율은 없으므로, 한 종류는 정확히
     * 자기 행이 말하는 크기입니다.
     *
     * *그려지는 빌보드는 `height` 높이에 `height * aspect` 너비*이며, 그래서 ::height 하나가
     * 몬스터를 비례해서 키우고 줄이는 유일한 숫자입니다.
     *
     * height를 저절로 따라가는 것: 히트스캔 원기둥의 윗면, 오를 수 있는 턱
     * 높이(::mon_step이며, height/3이되 결코 ::PLAYER_STEP 아래로는 내려가지 않습니다. 그보다
     * 작아진 종류는 플레이어의 보폭을 그대로 가지며 이 한 가지에 대해서만 작아지기를 멈춥니다.
     * 계단은 레벨의 것이기 때문입니다), 스폰에 필요한 천장 높이, 갈고리가 붙는 자리, 피가 튀는
     * 자리.
     *
     * 따라가지 *않아* 같은 수정에서 손으로 함께 조정해야 하는 것:
     *   ::radius  -- 원기둥은 `radius` 굵기에 `height` 높이입니다. 반경을 그대로 둔 채 키운
     *                몬스터는 커졌는데 맞히기는 쉬워지지 않은 표적입니다.
     *   ::eye     -- 볼트가 나가는 자리이자 시야를 재는 기준입니다. 이것 없이 커진 몬스터는
     *                배꼽에서 쏩니다.
     *   ::attack  -- 사거리는 중심에서 잽니다. 몸만 길어지고 사거리가 그대로면 헛손질입니다.
     *
     * ::aspect는 종류별 값이며 아트의 64x96 셀 비율이 *아닙니다.* 스프라이트는 이미 이 값으로
     * 늘어나 있으므로, 비례 확대만 원한다면 건드리지 마십시오.
     *
     * @note ::types_check는 이 중 무엇도 보지 않습니다. `eye`가 `height`보다 높아도 잡히지
     *       않고 그대로 플레이됩니다. */

    /**
     * @brief Collision and hitscan radius, metres.
     *
     * ENGLISH
     * -------
     * BOTH HALVES OF THAT ARE NOW TRUE. For a long time only the hitscan half
     * was: ::enemy_hitscan was the single reader, while the movement path asked
     * about the monster's centre column alone. So this number said how wide a
     * monster was to SHOOT AT and nothing about how wide it was to the world,
     * and a monster could stand with its middle on a wall face and the rest of
     * itself inside the geometry. ::foot_ok and ::air_ok sample it now, the
     * same five points ::can_stand takes around ::PLAYER_RADIUS.
     *
     * IT IS ALSO WHAT KEEPS THE SPRITE OUT OF THE WALL, which is the surprising
     * part and the reason a drawing bug is fixed by a number in this column. A
     * monster is drawn as a billboard `height * aspect` wide that turns to face
     * the camera, so the most of itself it can bury in whatever it stands
     * against is `height * aspect / 2 - radius`. Raise this and that shrinks;
     * at `height * aspect / 2` it is zero.
     *
     * @note Keep it UNDER the drawn half-width. Past that the monster is wider
     *       to shoot at than it is to look at, and shots that visibly pass
     *       beside it register as hits.
     *
     * 한국어
     * ------
     * @brief 충돌 및 히트스캔 반경(미터).
     *
     * *이제 그 두 절반이 모두 참입니다.* 오랫동안 히트스캔 쪽 절반만 참이었습니다.
     * ::enemy_hitscan이 유일한 독자였고, 이동 경로는 몬스터의 중심 기둥 하나만 물었습니다.
     * 그래서 이 숫자는 몬스터가 *쏘아 맞히기에* 얼마나 넓은지를 말했을 뿐 세계에 대해 얼마나
     * 넓은지는 말하지 않았고, 몬스터는 한가운데를 벽면에 둔 채 나머지를 지오메트리 안에 넣고
     * 서 있을 수 있었습니다. 이제 ::foot_ok와 ::air_ok가 이것을 표본하며, ::can_stand가
     * ::PLAYER_RADIUS 둘레에서 취하는 것과 같은 다섯 점입니다.
     *
     * *그리고 이것이 스프라이트를 벽 밖에 붙들어 둡니다.* 놀라운 부분이자, 그리기 버그가 이
     * 열의 숫자로 고쳐지는 이유입니다. 몬스터는 `height * aspect` 너비의 빌보드로 그려지고
     * 카메라를 향해 돌므로, 기대 선 것에 묻을 수 있는 최대치는 `height * aspect / 2 - radius`
     * 입니다. 이 값을 올리면 그만큼 줄고, `height * aspect / 2`에서 0이 됩니다.
     *
     * @note 그려지는 반너비보다 *작게* 유지하십시오. 그것을 넘으면 몬스터는 보이는 것보다 쏘아
     *       맞히기에 더 넓어지고, 눈에 띄게 옆으로 지나가는 사격이 명중으로 처리됩니다.
     */
    float radius;
    float height;       /**< Standing height, metres. Also the drawn height. / 신장(미터). 그려지는 높이이기도 합니다. */
    float eye;          /**< Eye height above the feet, metres. Where it looks and shoots from. / 발 위의 시선 높이(미터). 보고 쏘는 자리입니다. */

    float sight;        /**< How far away it can first notice the player, metres. / 플레이어를 처음 알아챌 수 있는 거리(미터). */

    float aspect;       /**< Sprite width over height. See the size block above. / 스프라이트의 가로 대 세로 비율. 위의 치수 블록을 참조하십시오. */





    /**
     * @brief How fast this monster can turn, in degrees per second.
     *
     * ENGLISH: Quake's `yaw_speed`. A monster that snaps to face the player
     * every frame cannot be got behind, which makes strafing pointless -- there
     * is no angle left to win. Per monster because it is character: the brute
     * is a wall that cannot track you, the water spirit is quick enough to.
     *
     * 한국어: Quake의 `yaw_speed`입니다. 매 프레임 플레이어 쪽으로 즉시 도는 몬스터는 뒤를
     * 잡을 수 없고, 그러면 이길 각도가 없어 횡이동이 무의미해집니다. 몬스터별인 이유는 그것이
     * 성격이기 때문입니다. 브루트는 추적하지 못하는 벽이고 물의 정령은 추적할 만큼 빠릅니다.
     */
    float yaw_speed;

    /**
     * @brief Seconds of immunity to the flinch after being hurt.
     *
     * ENGLISH: Quake's `pain_finished`. Without it every hit restarts the
     * flinch, so any weapon firing faster than the flinch lasts (the rapid gun
     * fires every 0.085s against a 0.16s flinch) holds a monster still until it
     * dies. Per monster because it is a character lever: Quake's ogre gets 5
     * seconds, and that is what makes an ogre frightening.
     *
     * 한국어: Quake의 `pain_finished`입니다. 이것이 없으면 매 피격이 경직을 다시 시작하므로,
     * 경직보다 빠르게 발사되는 무기는(속사 무기는 0.16초 경직에 0.085초마다 발사됩니다)
     * 몬스터를 죽을 때까지 붙잡아 둡니다. 몬스터별인 이유는 성격을 정하는 조절 수단이기
     * 때문입니다. Quake의 오우거는 5초를 받고, 그것이 오우거를 무섭게 만듭니다.
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
    /**
     * @brief How many of this kind may be alive at once. 0 is no limit.
     *
     * ENGLISH
     * -------
     * PER KIND, WHERE ::Spawner::max_alive IS PER ROOM. That one asks how
     * many monsters are alive at all and holds the spawner back above its
     * number; this asks how many of THIS kind are, and the difference is
     * what a mixed wave feels like. A room capped at eight can be eight
     * brutes, which is not a harder version of the fight the author wrote,
     * it is a different fight -- and it happens by accident, because which
     * spawner wins the tick is a race nobody is steering.
     *
     * A REFUSAL HERE IS "NOT NOW", not "never", and it keeps the spawner's
     * budget for the same reason ::Spawner::max_alive does: the group still
     * arrives, once there is room for its kind. A spawner that spent its
     * budget on a full room would quietly shrink every wave after the
     * first.
     *
     * 한국어
     * ------
     * @brief 이 종류가 동시에 몇 마리까지 살아 있을 수 있는지. 0이면 제한 없음.
     *
     * *::Spawner::max_alive가 방 단위인 곳에서 이것은 종류 단위입니다.* 그쪽은 몬스터가
     * 통틀어 몇 마리 살아 있는지를 묻고 자기 수를 넘으면 스포너를 붙잡습니다. 이것은
     * *이 종류*가 몇 마리인지를 묻고, 그 차이가 곧 섞인 웨이브의 감각입니다. 여덟으로
     * 제한된 방은 브루트 여덟일 수 있고, 그것은 제작자가 쓴 전투의 더 어려운 판이 아니라
     * *다른 전투*입니다. 게다가 우연히 그렇게 됩니다. 어느 스포너가 틱을 이기는지는
     * 아무도 조종하지 않는 경주이기 때문입니다.
     *
     * *이곳의 거절은 "절대"가 아니라 "지금은 아니다"이며*, ::Spawner::max_alive와 같은
     * 이유로 스포너의 예산을 지킵니다. 그 종류의 자리가 나면 무리는 여전히 도착합니다.
     * 가득 찬 방에 예산을 써 버리는 스포너는 첫 웨이브 이후 모든 웨이브를 조용히
     * 줄어들게 만듭니다.
     */
    int   cap;

    int flags;
} MonType;

/**
 * @struct Enemy
 * @brief One monster in the level, and everything a frame may change about it.
 *
 * ENGLISH
 * -------
 * All state and no rules: what a monster IS lives in ::MonType, and what it is
 * DOING lives here. A slot with `active` clear is free; a slot in ::E_DEAD is
 * a corpse and still drawn.
 *
 * 한국어
 * ------
 * @brief 레벨에 있는 몬스터 하나와, 한 프레임이 바꿀 수 있는 모든 것.
 *
 * 규칙 없이 상태만 있습니다. 몬스터가 *무엇인가*는 ::MonType에 있고, *무엇을 하고 있는가*가
 * 이곳에 있습니다. `active`가 꺼진 슬롯은 비어 있고, ::E_DEAD인 슬롯은 시체이며 여전히
 * 그려집니다.
 */
typedef struct {
    int    type;        /**< Which kind, a ::MonTypeID. / 어느 종류인지. ::MonTypeID 값입니다. */
    v3     pos;         /**< Foot position, in world metres. / 발 위치. 월드 미터 단위입니다. */
    float  vel_y;       /**< Vertical velocity. / 수직 속도. */
    float  yaw;         /**< Which way it is actually facing, radians. / 실제로 향하고 있는 방향(라디안). */
    int    health;      /**< Health remaining. / 남은 체력. */
    EState state;       /**< What it is doing. / 무엇을 하고 있는지. */
    float  timer;       /**< Time within the current state, e.g. wind-up or rest. / 현재 상태 안에서의 시간. 예비 동작이나 휴식 등입니다. */
    float  anim;        /**< Free-running clock for the walk cycle. / 걷기 사이클을 위한 자유 진행 시계. */
    float  flash;       /**< White hit flash, decaying to 0. / 피격 시 흰 섬광. 0으로 감쇠합니다. */
    /**
     * @brief How many bolts of the current attack have gone out. 0 before any.
     *
     * ENGLISH: A COUNT, AND IT USED TO BE A FLAG -- "has this attack already
     * dealt its damage". For a swing and for a single bolt the two readings are
     * the same, because 1 is both "yes" and "one of one". A volley released over
     * ::MonType::shot_gap needed to know *how many so far*, and this field was
     * already sitting in ::E_ATTACK being set to 1 and back to 0.
     *
     * 한국어: *개수이며, 예전에는 플래그였습니다* — "이 공격이 이미 피해를 입혔는가". 휘두르기와
     * 단발 볼트에 대해서는 두 해석이 같습니다. 1이 "그렇다"이면서 "하나 중 하나"이기 때문입니다.
     * ::MonType::shot_gap에 걸쳐 나가는 일제 사격은 *지금까지 몇 발인가*를 알아야 했고, 이
     * 필드는 이미 ::E_ATTACK에서 1이 되었다가 0으로 돌아가며 그 자리에 있었습니다.
     */
    int    swung;

    /**
     * @brief How many bolts this volley will fire, rolled when it starts.
     *
     * ENGLISH: Rolled once from ::MonType::burst_min..::MonType::burst rather
     * than tested per bolt, because a length re-rolled every frame is not a
     * length. Meaningless outside ::E_ATTACK.
     *
     * 한국어: ::MonType::burst_min..::MonType::burst에서 *한 번* 굴립니다. 볼트마다 검사하지
     * 않는 이유는, 매 프레임 다시 굴려지는 길이는 길이가 아니기 때문입니다. ::E_ATTACK 바깥에서는
     * 의미가 없습니다.
     */
    short  volley_n;

    /**
     * @brief Where this volley is aimed, fixed when it starts.
     *
     * ENGLISH
     * -------
     * THE MONSTER COMMITS, AND THAT IS THE COUNTERPLAY. Every bolt used to be
     * aimed at wherever the player was standing at the instant it left, which
     * for a shotgun meant nothing -- the five left together, so there was only
     * one instant. Ask the same question ten times over half a second and the
     * volley follows the player around the room: a stream that tracks is not
     * dodged, it is out-run, and nothing in this game is faster than a bolt.
     *
     * So the aim is taken once and the rest of the volley goes THERE. Walking
     * out of it works, which is what makes ::MonType::shot_gap a duration the
     * player can spend rather than a rate they lose to.
     *
     * @note The monster still TURNS while it fires -- ::change_yaw runs before
     *       the state machine and is not gated on ::E_ATTACK. What is fixed is
     *       where the bolts go, not where the sprite looks, and the two coming
     *       apart for half a second is the visible tell that it is committed.
     *
     * 한국어
     * ------
     * @brief 이 일제 사격이 겨눈 자리. 시작할 때 고정됩니다.
     *
     * *몬스터가 스스로를 묶으며, 그것이 대응 수단입니다.* 예전에는 모든 볼트가 떠나는 순간
     * 플레이어가 서 있던 자리를 겨눴고, 산탄에게 그것은 아무 뜻도 없었습니다. 다섯이 함께
     * 떠났으므로 순간이 하나뿐이었기 때문입니다. 같은 질문을 0.5초에 걸쳐 열 번 하면 일제
     * 사격은 플레이어를 따라 방을 돕니다. 추적하는 줄기는 피하는 것이 아니라 *따돌려야* 하는
     * 것이고, 이 게임에서 볼트보다 빠른 것은 없습니다.
     *
     * 그래서 조준은 한 번 취해지고 나머지 볼트는 *그곳으로* 갑니다. 걸어서 벗어나는 것이
     * 통하며, 그것이 ::MonType::shot_gap을 플레이어가 잃는 비율이 아니라 쓸 수 있는 시간으로
     * 만듭니다.
     *
     * @note 몬스터는 쏘는 동안에도 *돕니다.* ::change_yaw는 상태 기계보다 먼저 돌고 ::E_ATTACK에
     *       걸려 있지 않습니다. 고정되는 것은 볼트가 가는 곳이지 스프라이트가 보는 곳이 아니며,
     *       그 둘이 0.5초 동안 어긋나는 것이 스스로를 묶었다는 눈에 보이는 신호입니다.
     */
    v3     volley_at;
    int    active;      /**< Whether this slot is in use at all. / 이 슬롯이 사용 중인지 여부. */

    /**
     * @brief What this corpse still owes the floor. -1 when it owes nothing.
     *
     * ENGLISH
     * -------
     * WHY THE DROP IS DECIDED HERE AND HANDED OVER SOMEWHERE ELSE. The die has
     * to be rolled at the instant of death, because ::EnemyPool::rng is what a
     * recorded demo replays and a roll taken a frame later is a roll taken
     * after some other frame's worth of randomness. But WHAT a box of
     * ammunition is for is a question about the player's roster, and the
     * player is not in this module and should not be dragged into it -- a
     * ::LOOT_HELD stored here is resolved by ::world_step, which knows.
     *
     * So this field is the gap between the two: one int, written once by
     * ::enemy_hurt and read once by ::enemy_take_drop, on the same frame.
     *
     * -1 AND NOT 0 for "nothing", because 0 is ::PK_AMMO. A sentinel that
     * happens to be a real kind is how every monster in the level comes to
     * drop shells.
     *
     * 한국어
     * ------
     * @brief 이 시체가 아직 바닥에 빚진 것. 빚진 것이 없으면 -1입니다.
     *
     * 왜 드롭은 이곳에서 정해지고 다른 곳에서 건네지는가. 주사위는 죽는 그 순간에 굴려져야
     * 합니다. 기록된 데모가 재생하는 것이 ::EnemyPool::rng이며, 한 프레임 뒤에 굴린 것은 다른
     * 프레임의 난수를 거친 뒤에 굴린 것이기 때문입니다. 그러나 탄약 상자가 *무엇을 위한
     * 것인가*는 플레이어의 보유 목록에 대한 질문이고, 플레이어는 이 모듈에 없으며 끌어들여서도
     * 안 됩니다. 이곳에 저장된 ::LOOT_HELD는 그것을 아는 ::world_step이 해석합니다.
     *
     * 그래서 이 필드가 둘 사이의 틈입니다. int 하나이며, ::enemy_hurt가 한 번 쓰고
     * ::enemy_take_drop이 같은 프레임에 한 번 읽습니다.
     *
     * "없음"이 0이 아니라 -1인 이유는 0이 ::PK_AMMO이기 때문입니다. 우연히 실제 종류이기도 한
     * 감시값은 레벨의 모든 몬스터가 산탄을 떨어뜨리게 되는 경위입니다.
     */
    int    drop;

    /**
     * @brief The last line-of-sight reading. Derived, never authored.
     *
     * ENGLISH
     * -------
     * The visibility trace (::level_blocked) was the most expensive thing the
     * AI did, so it is measured once every few frames and reused in between.
     * The full reasoning is beside ::SIGHT_PERIOD.
     *
     * It matters that this starts at 0, meaning "cannot see". A freshly
     * spawned monster then waits for its first real reading instead of acting
     * on an invented one: when it does not yet know, it should be wrong in the
     * direction of doing nothing.
     *
     * @warning NOT used for the check made as a bolt is RELEASED. That one has
     *          to be live, because it exists to catch a target ducking
     *          mid-wind-up.
     *
     * 한국어
     * ------
     * @brief 마지막으로 측정한 시야 확보 여부. 파생값이며 제작값이 아닙니다.
     *
     * 시야 판정(::level_blocked)은 AI가 수행하던 것 중 가장 비쌌으므로, 몇 프레임에 한 번만
     * 측정하고 그 사이에는 이 값을 재사용합니다. 자세한 근거는 ::SIGHT_PERIOD 곁에
     * 있습니다.
     *
     * 0(볼 수 없음)에서 시작하는 것이 중요합니다. 새로 생성된 몬스터는 지어낸 값으로
     * 행동하는 대신 첫 실제 측정이 나올 때까지 기다립니다. 아직 모를 때는 아무것도 하지 않는
     * 쪽으로 틀려야 합니다.
     *
     * @warning 볼트를 *발사하는 순간*의 검사에는 쓰이지 *않습니다*. 그 검사는 예비 동작 도중
     *          대상이 숨는 경우를 잡기 위해 존재하므로 반드시 최신이어야 합니다.
     */
    char   seen;

    /**
     * @brief Frames left before `seen` is measured again.
     *
     * ENGLISH
     * -------
     * Started at a different value per monster so the refreshes spread out.
     * Refreshing them all on the same frame costs the same on average but
     * arrives as a spike every period, which is the shape that lowers the
     * average and shows up as a stutter.
     *
     * 한국어
     * ------
     * @brief `seen`을 다시 측정하기까지 남은 프레임 수.
     *
     * 몬스터마다 서로 다른 값에서 시작하여 갱신 시점을 분산시킵니다. 전부 같은 프레임에
     * 갱신하면 평균 비용은 같으면서 주기마다 스파이크로 몰리는데, 그것은 평균을 낮추는 대신
     * 끊김으로 나타나는 형태입니다.
     */
    short  sight_age;

    /**
     * @brief Which monster this one is fighting instead of the player, or -1.
     *
     * ENGLISH
     * -------
     * THE WHOLE OF INFIGHTING IS THIS FIELD. Every function that steers a
     * monster already takes "the point to fight" as an argument -- it was
     * always the player's eye, and nothing below ::enemy_update ever asked
     * whose eye it was. So a monster fighting another monster is not a second
     * AI; it is the same AI handed a different point.
     *
     * SET ONLY BY BEING HURT, and only by a monster of a different kind. That
     * is Quake's rule from combat.qc: `T_Damage` retargets the victim onto its
     * attacker unless `targ.classname == attacker.classname`. The damage lands
     * either way -- two water spirits crossing fire still hurt each other,
     * they just do not take it personally.
     *
     * CLEARED BY THE PLAYER. Being shot by the player puts this back to -1,
     * which is the closest thing here to Quake's `oldenemy`: a monster you
     * have started shooting is a monster that should be coming for you, not
     * one still settling a grudge with something behind it.
     *
     * 한국어
     * ------
     * @brief 이 몬스터가 플레이어 대신 싸우고 있는 몬스터의 색인. 없으면 -1입니다.
     *
     * *내분의 전부가 이 필드입니다.* 몬스터를 조종하는 모든 함수가 이미 "싸울 지점"을 인자로
     * 받습니다. 그것이 늘 플레이어의 눈이었을 뿐이고, ::enemy_update 아래의 무엇도 그것이
     * *누구의* 눈인지 물은 적이 없습니다. 그러므로 다른 몬스터와 싸우는 몬스터는 두 번째 AI가
     * 아니라, 다른 점을 건네받은 같은 AI입니다.
     *
     * *맞았을 때만, 그리고 다른 종류에게 맞았을 때만 설정됩니다.* combat.qc의 Quake 규칙입니다.
     * `T_Damage`는 `targ.classname == attacker.classname`이 아닌 한 피해자를 가해자에게로
     * 돌립니다. 피해는 어느 쪽이든 꽂힙니다. 물 정령 둘이 서로의 사격에 걸리면 여전히 서로를
     * 다치게 하며, 다만 그것을 개인적으로 받아들이지 않을 뿐입니다.
     *
     * *플레이어가 지웁니다.* 플레이어에게 맞으면 -1로 돌아가며, 이곳에서 Quake의 `oldenemy`에
     * 가장 가까운 것입니다. 당신이 쏘기 시작한 몬스터는 당신에게 와야 하는 몬스터이지, 뒤쪽의
     * 무언가와 아직 원한을 정리하고 있는 몬스터가 아닙니다.
     */
    /**
     * @brief Which ::MonAttack slot the current attack is running, or -1.
     *
     * ENGLISH: Chosen once by ::pick_attack when ::E_ATTACK is entered and read
     * by every frame of it, for ::Enemy::volley_n's reason -- an attack that
     * re-decides which attack it is partway through is not one attack. It
     * outlives the state deliberately: the brawler's re-swing inside
     * ::E_ATTACK keeps the slot it committed to rather than rolling again.
     *
     * 한국어: 현재 공격이 실행 중인 ::MonAttack 슬롯. 없으면 -1입니다. ::E_ATTACK에 들어갈 때
     * ::pick_attack이 한 번 고르고 그 상태의 모든 프레임이 읽습니다. ::Enemy::volley_n과 같은
     * 이유이며, 도중에 자기가 무슨 공격인지 다시 정하는 공격은 하나의 공격이 아닙니다. 상태보다
     * 오래 사는 것은 의도적입니다. ::E_ATTACK 안의 근접형 재휘두르기는 다시 굴리지 않고 스스로
     * 약속한 슬롯을 지킵니다.
     */
    short  atk;

    short  foe;
    float  foe_time;    /**< Seconds the grudge has left. / 원한에 남은 시간(초). */
    float  cast_timer;  /**< Seconds to the next charge mote. / 다음 충전 알갱이까지의 초. */

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

    /* --- what a ward owes / 결계핵이 빚진 것 ---------------------------------
     *
     * ENGLISH
     * -------
     * Only ::MON_WARD ever moves these, and they are here rather than in a
     * side table for the reason ::Enemy::drop is: a ward that is destroyed
     * takes its unpaid debt with it, and a slot recycled into something else
     * gets them zeroed by ::make_monster's `Enemy zero = {0}` without anybody
     * remembering to.
     *
     * WHY A DEBT AND NOT A CALL. ::enemy_hurt is where damage is noticed, and
     * it has neither a ::Level -- which ::make_monster needs for its ground
     * search -- nor a frame in which to count a telegraph down. So the damage
     * site records what is owed and ::enemy_update, which has both, pays it.
     *
     * 한국어
     * ------
     * 이들을 움직이는 것은 ::MON_WARD뿐이며, 별도 표가 아니라 이곳에 두는 이유는
     * ::Enemy::drop과 같습니다. 파괴된 결계핵은 갚지 않은 빚을 함께 가져가고, 다른 것으로
     * 재활용된 슬롯은 아무도 기억하지 않아도 ::make_monster의 `Enemy zero = {0}`이 0으로
     * 만들어 줍니다.
     *
     * *왜 호출이 아니라 빚인가.* 피해를 알아채는 곳은 ::enemy_hurt인데, 그곳에는
     * ::make_monster가 지면 탐색에 필요로 하는 ::Level도 없고 예고를 세어 내릴 프레임도
     * 없습니다. 그래서 피해 지점은 빚진 것을 기록하고, 둘 다 가진 ::enemy_update가
     * 갚습니다.
     */
    short  summon_dmg;  /**< Damage accrued since the last summon. / 마지막 소환 이후 누적된 피해. */
    short  summon_left; /**< Monsters still owed. 0 is nothing pending. / 아직 빚진 마리 수. 0이면 대기 없음. */
    float  summon_warn; /**< Telegraph countdown, or 0 when none is running. / 예고 카운트다운. 진행 중이 아니면 0. */

    /**
     * @brief Which summon table this ward draws from. 0 ground, 1 air.
     *
     * ENGLISH: The whole of the difference between the two wards the author
     * places, and the reason ::MON_WARD is one row rather than two. Set from
     * the candidate marker's kind at ::enemy_ward_place; meaningless on every
     * other type.
     *
     * 한국어: 제작자가 배치하는 두 결계핵의 차이 전부이며, ::MON_WARD가 두 행이 아니라 한
     * 행인 이유입니다. ::enemy_ward_place에서 후보 표식의 종류를 보고 설정하며, 다른 모든
     * 종류에서는 의미가 없습니다.
     */
    short  ward_table;
} Enemy;

/**
 * @struct Shot
 * @brief One monster projectile in flight.
 *
 * ENGLISH
 * -------
 * Owned by this module the way weapon.c owns its own tracers and impacts.
 * These exist because a monster fired them, and they go when the level does.
 *
 * 한국어
 * ------
 * @brief 비행 중인 몬스터 발사체 하나.
 *
 * weapon.c가 자신의 예광탄과 착탄을 소유하는 것처럼 이 모듈이 소유합니다. 몬스터가
 * 발사했기 때문에 존재하며, 레벨과 함께 사라집니다.
 */
typedef struct {
    v3    pos;      /**< Current position. / 현재 위치. */
    v3    vel;      /**< Velocity. / 속도. */
    float life;     /**< Seconds until it expires; 0 means the slot is free. / 사라지기까지 남은 시간(초). 0이면 빈 슬롯입니다. */
    int   damage;   /**< Damage it deals on impact. / 명중 시 피해량. */
    int   active;   /**< Whether this slot is in use. / 이 슬롯이 사용 중인지 여부. */

    /**
     * @brief Which creature cast it, a ::MonTypeID.
     *
     * ENGLISH
     * -------
     * WHAT IT IS FOR IS COLOUR, and that is a small use for a field on a
     * struct this file is careful about. It earns the four bytes because the
     * alternative is worse: a bolt in the air has no other link back to the
     * monster that made it -- ::shot_fire copies a position, a velocity and a
     * damage number and the caster walks away -- so scene.c would otherwise
     * have to guess a creature from a damage value, which two of them share.
     *
     * The renderer is the only reader. Nothing in this module branches on it,
     * and a bolt behaves identically whoever threw it: ::MON_MAW's hurts more
     * because ::MonType::damage is larger, not because of this.
     *
     * 한국어
     * ------
     * @brief 이것을 시전한 생물. ::MonTypeID 값입니다.
     *
     * *쓰임은 색이며*, 이 파일이 신중하게 다루는 구조체의 필드치고는 작은 쓰임입니다. 그럼에도
     * 4바이트의 값을 하는 이유는 대안이 더 나쁘기 때문입니다. 공중의 볼트에는 자기를 만든
     * 몬스터로 돌아가는 다른 연결이 없습니다. ::shot_fire는 위치와 속도와 피해량을 복사하고
     * 시전자는 걸어가 버립니다. 그것이 없으면 scene.c는 피해량으로 생물을 추측해야 하는데, 그
     * 값을 공유하는 종류가 둘 있습니다.
     *
     * 읽는 것은 렌더러뿐입니다. 이 모듈의 무엇도 이것으로 분기하지 않으며, 누가 던졌든 볼트는
     * 똑같이 행동합니다. ::MON_MAW의 볼트가 더 아픈 것은 ::MonType::damage가 크기 때문이지
     * 이것 때문이 아닙니다.
     */
    int   type;

    /**
     * @brief Seconds until the next trail particle is emitted.
     *
     * ENGLISH
     * -------
     * A timer rather than a per-frame emit. Emitting per frame ties the
     * trail's density to the frame rate -- it looks different at 60 and at 144
     * -- and lets one bolt in flight fill the 256-entry shared particle pool
     * by itself, pushing out every other effect. An interval makes the trail
     * frame-rate independent. See ::SHOT_TRAIL_INTERVAL.
     *
     * 한국어
     * ------
     * @brief 다음 궤적 파티클을 방출하기까지 남은 시간(초).
     *
     * 프레임당 방출이 아니라 타이머입니다. 프레임 단위로 방출하면 궤적의 밀도가 프레임률에
     * 좌우되어 60fps와 144fps에서 다르게 보이고, 비행 중인 볼트 하나가 256칸짜리 공유 파티클
     * 풀을 혼자 채워 다른 모든 이펙트를 밀어냅니다. 간격을 두면 궤적이 프레임률과 무관해집니다.
     * ::SHOT_TRAIL_INTERVAL을 참조하십시오.
     */
    float trail_timer;

    /**
     * @brief Which monster fired this, so it cannot hit itself and the victim
     *        knows who to blame.
     *
     * ENGLISH: ::type was already here and is the CASTER'S KIND, which the
     * trail colour reads. It cannot serve as the owner: two water spirits are
     * the same type and must still be able to shoot each other, and one water
     * spirit must not be able to shoot itself.
     *
     * 한국어: ::type이 이미 있었고 그것은 *시전자의 종류*이며 궤적 색이 읽습니다. 소유자
     * 역할은 할 수 없습니다. 물 정령 둘은 같은 type이지만 서로를 쏠 수 있어야 하고, 물 정령
     * 하나는 자기를 쏠 수 없어야 하기 때문입니다.
     */
    short owner;
} Shot;

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

/* --- the boss fight / 보스전 --------------------------------------------- */

/**
 * @brief How many times the wards must be cleared before the maw can die.
 *
 * ENGLISH
 * -------
 * THIS IS A DIVISOR OF HEALTH, NOT A COUNTER BESIDE IT. ::enemy_hurt clamps a
 * boss's health at `hp * (BOSS_CYCLES - cycle - 1) / BOSS_CYCLES`, so a rocket
 * landed in the first groggy window cannot carry past the first boundary and
 * the third boundary is zero. Three cycles are then guaranteed by construction
 * and the death still happens inside ::enemy_hurt like every other death --
 * which matters, because a cycle counter that killed the boss would be the
 * second death path this pool's tally is documented to forbid.
 *
 * ::MON_MAW's `hp` must be divisible by this or the last boundary is not zero
 * and the boss survives its third cycle at one or two health. ::types_check
 * says so in a form that fails.
 *
 * 한국어
 * ------
 * @brief 아귀가 죽을 수 있게 되기까지 결계를 몇 번 걷어야 하는가.
 *
 * *이것은 체력 옆의 계수기가 아니라 체력의 약수입니다.* ::enemy_hurt가 보스의 체력을
 * `hp * (BOSS_CYCLES - cycle - 1) / BOSS_CYCLES`에서 고정하므로, 첫 그로기 창에 꽂은 로켓도 첫
 * 경계를 넘지 못하고 세 번째 경계는 0입니다. 그러면 3사이클이 구조적으로 보장되고, 사망은
 * 여전히 다른 모든 사망과 같이 ::enemy_hurt 안에서 일어납니다. 이것이 중요한 이유는, 보스를
 * 죽이는 사이클 계수기가 곧 이 풀의 집계 주석이 금지한다고 명시한 *두 번째 사망 경로*이기
 * 때문입니다.
 *
 * ::MON_MAW의 `hp`는 이 값으로 나누어떨어져야 합니다. 그러지 않으면 마지막 경계가 0이 아니고
 * 보스는 세 번째 사이클을 체력 1~2로 살아남습니다. ::types_check가 그것을 *실패할 수 있는*
 * 형태로 말합니다.
 */
#define BOSS_CYCLES 3

/** @brief Wards raised per cycle, before the candidate count clamps it. / 사이클당 세우는 결계핵 수. 후보 수가 이를 제한하기 전의 값입니다. */
#define BOSS_WARDS 4

/** @brief Candidate positions a level may mark. Two lists share it. / 레벨이 표시할 수 있는 후보 자리의 수. 두 목록이 나누어 씁니다. */
#define BOSS_MAX_CAND 16

/**
 * @brief Seconds a groggy window may last before it is taken away.
 *
 * ENGLISH
 * -------
 * THE DEFENCE AGAINST A FIGHT THAT CANNOT END. A groggy window has no timer by
 * design -- it ends when the health crosses the next boundary, which is what
 * makes three cycles exact -- and that is a promise the player can decline. A
 * player who stops shooting, runs out of ammunition, or simply cannot find the
 * maw from where they are standing would otherwise hold an open boss forever
 * with the wards gone and nothing arriving.
 *
 * So the window expires, the wards come back, and the health returns to the
 * ceiling of the segment it started at -- the cycle does NOT advance. The bar
 * visibly refills, which is the only honest way to say "that window is gone":
 * a bar that stayed where it was would report progress the player no longer
 * has. The guarantee weakens from "exactly three cycles" to "at least three",
 * and a wasted window is what adds one.
 *
 * A MINUTE, and not less, because the window is meant to be spent under
 * pressure from whatever the wards summoned. Anything short enough to expire
 * during a real fight is a second failure state nobody asked for.
 *
 * 한국어
 * ------
 * @brief 그로기 창이 회수되기까지 허용되는 시간(초).
 *
 * *끝날 수 없는 전투에 대한 방어입니다.* 그로기 창은 설계상 타이머가 없습니다. 체력이 다음
 * 경계를 넘을 때 끝나고, 그것이 3사이클을 정확하게 만듭니다. 그런데 그것은 플레이어가 거절할
 * 수 있는 약속입니다. 쏘기를 멈추거나, 탄약이 떨어지거나, 서 있는 자리에서 아귀를 찾지 못하는
 * 플레이어는 그러지 않으면 결계가 사라지고 아무것도 오지 않는 채로 열린 보스를 영원히 붙들게
 * 됩니다.
 *
 * 그래서 창은 만료되고, 결계핵이 돌아오며, 체력은 시작했던 구간의 천장으로 되돌아갑니다.
 * 사이클은 *오르지 않습니다.* 바가 눈에 띄게 되차오르는데, 그것이 "그 창은 날아갔다"를 말하는
 * 유일하게 정직한 방법입니다. 그대로 머무는 바는 플레이어가 더 이상 갖고 있지 않은 진척을
 * 보고하게 됩니다. 보장은 "정확히 3사이클"에서 "최소 3사이클"로 약해지고, 낭비된 창이 하나를
 * 더합니다.
 *
 * *1분이며 그보다 짧지 않은 이유는*, 이 창이 결계핵이 부른 것들의 압박 아래에서 쓰이도록
 * 되어 있기 때문입니다. 실제 전투 중에 만료될 만큼 짧은 값은 아무도 요청하지 않은 두 번째
 * 실패 상태입니다.
 */
#define BOSS_GROGGY_MAX 60.0f

/**
 * @brief Damage a ward must ACCRUE before it summons once.
 *
 * ENGLISH: A threshold rather than a per-hit event, and enemy.c's note in
 * ::enemy_hurt has the whole argument: one point-blank shotgun blast is six
 * calls into that function, so "summon when damaged" read literally fills
 * ::ENEMY_MAX in seconds and then says nothing about it in a release build.
 * ::MON_WARD's health is a whole multiple of this, so a ward pays a fixed
 * number of times on its way down however it is killed.
 *
 * 한국어: 타격마다의 사건이 아니라 문턱이며, ::enemy_hurt의 enemy.c 주석에 논거 전부가
 * 있습니다. 근접 샷건 한 발은 그 함수 호출 여섯 번이므로, "피해를 입으면 소환"을 문자 그대로
 * 받으면 몇 초 만에 ::ENEMY_MAX가 차고 릴리스 빌드는 그에 대해 아무 말도 하지 않습니다.
 * ::MON_WARD의 체력은 이 값의 정수배이므로, 결계핵은 어떻게 죽든 쓰러지는 동안 정해진 횟수만큼
 * 지급합니다.
 */
#define WARD_SUMMON_DMG 30

/** @brief Monsters one payment is worth. / 한 번의 지급이 내놓는 마리 수. */
#define WARD_SUMMON_COUNT 2

/**
 * @brief Live monsters past which a ward's summon is skipped.
 *
 * ENGLISH: ::Spawner::max_alive's argument, applied to the other thing in this
 * file that makes monsters. Deliberately well under ::ENEMY_MAX rather than at
 * it: the pool is shared with corpses and the eviction path has a defect at
 * saturation -- every recycled slot receives the same ::Enemy::sight_age, so a
 * whole regenerated ward set would refresh its sight on one frame, which is the
 * stagger that field exists to prevent.
 *
 * 한국어: ::Spawner::max_alive의 논거를, 이 파일에서 몬스터를 만드는 다른 것에 적용한 것입니다.
 * ::ENEMY_MAX가 아니라 그보다 한참 아래인 것은 의도적입니다. 풀은 시체와 공유되고, 포화 상태의
 * 축출 경로에는 결함이 있습니다. 재활용된 슬롯이 전부 같은 ::Enemy::sight_age를 받으므로, 재생된
 * 결계핵 무리 전체가 한 프레임에 시야를 갱신하게 됩니다. 그 필드가 존재하는 이유인 분산이 바로
 * 그것입니다.
 */
#define WARD_SUMMON_CAP 40

/** @brief Metres a summoned monster appears from the ward that owed it. / 소환된 몬스터가 빚진 결계핵에서 떨어져 나타나는 거리(미터). */
#define WARD_SUMMON_DIST 2.2f

/**
 * @struct BossFight
 * @brief Where the wards may stand, which are standing, and how far in we are.
 *
 * ENGLISH
 * -------
 * Zero is a valid empty state for every member, which is what lets ::enemy_reset
 * clear it by clearing the pool and is the promise pools.h makes for all of
 * these. `active` being the flag that means "there is a fight" rather than
 * deriving it from a live ::MON_BOSS is deliberate: the maw is dead for a few
 * frames before anybody notices, and the reward, the line and the respawn all
 * need to fire exactly once in that gap.
 *
 * 한국어
 * ------
 * @brief 결계핵이 설 수 있는 자리, 지금 서 있는 것, 그리고 어디까지 왔는가.
 *
 * 모든 멤버에 대해 0이 유효한 빈 상태입니다. 그것이 ::enemy_reset이 풀을 지우는 것만으로 이것을
 * 지울 수 있게 하며, pools.h가 이 모두에 대해 하는 약속입니다. 살아 있는 ::MON_BOSS에서
 * 유도하지 않고 `active`를 "전투가 있다"는 플래그로 두는 것은 의도적입니다. 아귀는 누군가
 * 알아채기 몇 프레임 전에 이미 죽어 있고, 보상과 대사와 재소환은 전부 그 틈에서 정확히 한 번
 * 발동해야 합니다.
 */
typedef struct {
    v3    cand[BOSS_MAX_CAND];      /**< Candidate positions, sorted. / 후보 자리. 정렬되어 있습니다. */
    char  cand_air[BOSS_MAX_CAND];  /**< 1 if that candidate summons flyers. / 그 후보가 비행체를 부르면 1. */
    char  cand_used[BOSS_MAX_CAND]; /**< 1 if the PREVIOUS cycle used it. / *이전* 사이클이 썼으면 1. */
    short n_cand;                   /**< Candidates the level marked. / 레벨이 표시한 후보의 수. */

    /**
     * @brief Where the level's `monster_maw` marker stood, and whether there was one.
     *
     * ENGLISH: Remembered rather than re-read, because endless mode summons its
     * second and third maw long after ::enemy_spawn_level has finished with the
     * entity list -- and because the first maw's slot may since have been
     * recycled into something standing somewhere else entirely.
     *
     * 한국어: 다시 읽지 않고 기억합니다. 무한 모드는 ::enemy_spawn_level이 엔티티 목록을 다 쓴
     * 한참 뒤에 두 번째와 세 번째 아귀를 소환하며, 첫 아귀의 슬롯은 그동안 전혀 다른 곳에 선
     * 무언가로 재활용되었을 수 있기 때문입니다.
     */
    v3    maw_pos;
    char  have_maw;

    short cycle;      /**< Boundaries crossed so far, 0..::BOSS_CYCLES. / 지금까지 넘은 경계의 수. */
    float groggy;     /**< Seconds the current window has run. 0 when warded. / 현재 창이 진행된 초. 수호 중이면 0. */
    short next_wave;  /**< Endless: the wave the next maw is due on. 0 is none. / 무한 모드에서 다음 아귀가 뜰 웨이브. 0이면 예정 없음. */
    char  active;     /**< A fight is under way. / 전투가 진행 중입니다. */
    char  was_groggy; /**< Last frame's phase, so the transition fires once. / 지난 프레임의 단계. 전이가 한 번만 발동하도록. */

    /**
     * @brief Story mode has already summoned its one maw.
     *
     * ENGLISH: Distinct from `active`, which goes back to 0 the moment the
     * fight ends. Without this the story arena would summon a fresh maw on the
     * frame after the old one died -- forever, since story mode has no wave
     * gate to hide behind.
     *
     * 한국어: `active`와 구별됩니다. 그것은 전투가 끝나는 순간 0으로 돌아갑니다. 이것이 없으면
     * 스토리 아레나는 이전 아귀가 죽은 다음 프레임에 새 아귀를 소환합니다. 영원히 그렇게 됩니다.
     * 스토리 모드에는 뒤에 숨을 웨이브 관문이 없기 때문입니다.
     */
    char  have_started;
} BossFight;

typedef struct {
    Enemy    m[ENEMY_MAX];             /**< Monsters, packed to `count`. / `count`까지 채워진 몬스터. */
    int      count;                    /**< How many the level laid out. / 레벨이 배치한 수. */
    Shot     shots[ENEMY_MAX_SHOTS];   /**< Their projectiles, a ring. / 그들의 발사체이며 링입니다. */
    unsigned rng;                      /**< Fight randomness. 0 means "seed me". / 전투 난수. 0이면 "씨앗을 채워라". */

    /**
     * @brief The boss fight, if this level has one.
     *
     * ENGLISH: Here rather than in ::RunState for the reason ::Spawner is here:
     * it is state ABOUT THE LEVEL that the level cannot re-derive per frame,
     * and ::enemy_reset -- which every level load goes through -- zeroes it for
     * free. Putting it on the run would mean a level reload kept a half-finished
     * fight, and putting it in two places would mean a restart put back one of
     * them. The BANNER, which is a fact about the playthrough rather than about
     * the room, is on ::RunState instead; that split is deliberate and is the
     * one thing a reader has to know about where this lives.
     *
     * 한국어: ::Spawner가 이곳에 있는 것과 같은 이유입니다. 이것은 레벨이 매 프레임 다시 유도할
     * 수 없는 *레벨에 관한* 상태이고, 모든 레벨 로드가 거치는 ::enemy_reset이 공짜로 0으로
     * 만들어 줍니다. 플레이 쪽에 두면 레벨 재로드가 끝나지 않은 전투를 유지하게 되고, 두
     * 군데에 두면 재시작이 둘 중 하나만 되돌립니다. *배너*는 방이 아니라 플레이에 관한
     * 사실이므로 ::RunState에 둡니다. 그 분할은 의도적이며, 이것이 어디에 사는지에 대해
     * 독자가 알아야 할 유일한 것입니다.
     */
    BossFight boss;

    /**
     * @brief How much SLOWER the spawners run, as a fraction added to 1.
     *
     * ENGLISH
     * -------
     * ZERO IS NO SUPPRESSION, and that is why it is a slowdown rather than a
     * scale. pools.h promises that a zeroed ::Pools is a valid empty state; a
     * `spawn_scale` of 0 would read as "spawn instantly" and break that promise
     * on the first field anyone added without thinking about it.
     *
     * STATED AS A CONDITION, NOT SAVED AND RESTORED. ::enemy_wave_arm rewrites
     * every spawner's interval on every wave, so a pre-boss value stashed
     * anywhere would be overwritten before it could be put back. Instead the
     * effective wait is multiplied wherever a spawner schedules its next group,
     * and clearing this field is the whole of lifting the suppression -- the
     * same bargain main.c makes with the soundtrack, which is a per-frame
     * statement of what should be playing rather than an event that switches it.
     *
     * 한국어
     * ------
     * @brief 스포너가 얼마나 *느려지는가*. 1에 더해지는 비율입니다.
     *
     * *0이 억제 없음이며*, 그것이 배율이 아니라 감속인 이유입니다. pools.h는 0으로 채운
     * ::Pools가 유효한 빈 상태라고 약속합니다. `spawn_scale`이 0이면 "즉시 생성"으로 읽히고,
     * 아무 생각 없이 필드를 추가하는 첫 사람에게서 그 약속이 깨집니다.
     *
     * *저장했다 되돌리지 않고 조건으로 진술합니다.* ::enemy_wave_arm이 매 웨이브 모든 스포너의
     * 간격을 덮어쓰므로, 어디에 보관한 보스 이전 값이든 되돌리기 전에 덮어써집니다. 대신
     * 스포너가 다음 무리를 예약하는 자리에서 실효 대기 시간에 곱하고, 이 필드를 지우는 것이
     * 억제 해제의 전부입니다. main.c가 사운드트랙과 맺는 것과 같은 약속입니다. 그것을 전환하는
     * 사건이 아니라 무엇이 재생되어야 하는지에 대한 매 프레임의 진술입니다.
     */
    /**
     * @brief Non-zero while nothing in this pool can see the player.
     *
     * ENGLISH: ::PW_SHADOW, and it is a pool knob for ::spawn_slow's reason --
     * the run owns it and sets it beside the ::enemy_update call, so this module
     * never learns what a ::Player is. What it suppresses is NOTICING and
     * TRACKING, not memory: a monster already in ::E_CHASE keeps walking to where
     * it last had you, which is what makes invisibility a way to break contact
     * rather than a way to stop time.
     *
     * 한국어: ::PW_SHADOW이며, ::spawn_slow와 같은 이유로 풀의 손잡이입니다. 플레이가 그것을
     * 소유하고 ::enemy_update 호출 곁에서 설정하므로, 이 모듈은 ::Player가 무엇인지 끝내
     * 배우지 않습니다. 억제하는 것은 *알아채기*와 *추적하기*이지 기억이 아닙니다. 이미
     * ::E_CHASE에 있는 몬스터는 당신이 마지막으로 있던 곳으로 계속 걸어가며, 그것이 투명을
     * 시간을 멈추는 방법이 아니라 *접촉을 끊는* 방법으로 만듭니다.
     */
    int      blinded;

    float    spawn_slow;

    /**
     * @brief Multiplies how OFTEN spawners fire. 1 is as authored.
     *
     * ENGLISH
     * -------
     * SEPARATE FROM ::spawn_slow BECAUSE THEY HAVE DIFFERENT OWNERS, and one
     * knob with two owners is a knob that gets overwritten. ::spawn_slow is
     * the BOSS's: ::world_step sets it when the fight starts and clears it
     * when it ends, and it describes an event. This is the RUN's -- a
     * difficulty, a level that wants to be busier than its spawner intervals
     * say, a test that wants a wave now rather than in six seconds -- and it
     * describes a setting. Folding the setting into the event's field would
     * mean the boss clearing the difficulty on its way out.
     *
     * A RATE, NOT AN INTERVAL, so bigger means more: ::spawn_wait divides by
     * it. That is the opposite of ::spawn_slow, which multiplies, and the two
     * read in opposite directions on purpose -- a field called `slow` that
     * made things faster at 2.0 would be worse than either name alone.
     *
     * @note Clamped away from zero where it is read. A rate of zero is not
     *       "no spawns", it is a division by zero, and a caller who wants no
     *       spawns has ::Spawner::active to say so with.
     *
     * 한국어
     * ------
     * @brief 스포너가 *얼마나 자주* 터지는지에 대한 배율. 1이면 저작된 그대로입니다.
     *
     * *::spawn_slow와 분리된 이유는 주인이 다르기 때문이며*, 주인이 둘인 손잡이는 덮어써지는
     * 손잡이입니다. ::spawn_slow는 *보스의 것*입니다. ::world_step이 전투가 시작될 때
     * 설정하고 끝날 때 지우며, *사건*을 서술합니다. 이것은 *런의 것*입니다. 난이도, 자기
     * 스포너 간격이 말하는 것보다 분주하고 싶은 레벨, 6초 뒤가 아니라 지금 웨이브를 원하는
     * 테스트이며, *설정*을 서술합니다. 설정을 사건의 필드에 접어 넣으면 보스가 나가면서
     * 난이도를 지우게 됩니다.
     *
     * *간격이 아니라 비율*이므로 클수록 많습니다. ::spawn_wait가 이것으로 나눕니다. 곱하는
     * ::spawn_slow와 반대이며, 둘이 반대 방향으로 읽히는 것은 의도적입니다. 2.0에서 빨라지는
     * `slow`라는 이름의 필드는 둘 중 어느 이름보다도 나쁩니다.
     *
     * @note 읽는 곳에서 0으로부터 떨어뜨려 고정합니다. 비율 0은 "스폰 없음"이 아니라 0으로
     *       나누기이며, 스폰을 원하지 않는 호출자에게는 ::Spawner::active가 있습니다.
     */
    float    spawn_rate;

    Spawner spawner[ENEMY_MAX_SPAWNERS]; /**< Markers that keep making monsters. / 몬스터를 계속 만들어 내는 표식. */
    int     n_spawners;                  /**< How many are in use. / 사용 중인 개수. */

    /**
     * @brief Monsters that have died since the last ::enemy_take_kills.
     *
     * ENGLISH
     * -------
     * A TALLY OWED, not a total. The run's total belongs to ::RunState, which
     * is what a restart clears; this pool is rebuilt by every level load and a
     * total kept here would be a total that resets when the player walks
     * through an exit. So the pool counts what it has not yet handed over and
     * the run counts what it has been handed -- the same split ::Enemy::drop
     * makes with ::enemy_take_drop, for the same reason.
     *
     * Raised at the ONE place a monster becomes ::E_DEAD, so it cannot
     * disagree with the corpses: a second death path added later has to go
     * through ::enemy_hurt to exist at all.
     *
     * 한국어
     * ------
     * @brief 마지막 ::enemy_take_kills 이후 죽은 몬스터의 수.
     *
     * 누계가 아니라 *빚진 수*입니다. 플레이의 누계는 재시작이 지우는 ::RunState에 속합니다.
     * 이 풀은 레벨을 로드할 때마다 다시 만들어지므로, 이곳에 둔 누계는 플레이어가 출구를
     * 지날 때 초기화되는 누계가 됩니다. 그래서 풀은 아직 건네지 않은 것을 세고 플레이는
     * 건네받은 것을 셉니다. ::Enemy::drop이 ::enemy_take_drop과 맺는 것과 같은 분담이며 같은
     * 이유입니다.
     *
     * 몬스터가 ::E_DEAD가 되는 *한 곳*에서 올리므로 시체와 어긋날 수 없습니다. 나중에 추가된
     * 두 번째 사망 경로도 존재하려면 ::enemy_hurt를 거쳐야 합니다.
     */
    int     deaths;
} EnemyPool;

/* The bundle that holds this pool. See proj.h for why the calls take it. */
typedef struct Pools Pools;


/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/* --- The type table / 종류 표 --- */

/**
 * @brief The stat block for one kind of monster.
 *
 * ENGLISH
 * -------
 * @param[in] type A ::MonTypeID.
 * @return The kind's stats. Never NULL -- an id outside the enum resolves to a
 *         row that exists rather than being rejected, so no caller has to
 *         null-check what it is about to read.
 *
 * @note The returned table is static and outlives every caller. Do not write
 *       through it.
 *
 * 한국어
 * ------
 * @brief 한 종류의 몬스터에 대한 수치 묶음.
 *
 * @param[in] type ::MonTypeID 값.
 * @return 그 종류의 수치. 결코 NULL이 아닙니다. 열거형 바깥의 식별자는 거절되지 않고
 *         존재하는 행으로 해석되므로, 읽으려는 쪽이 널 검사를 할 필요가 없습니다.
 *
 * @note 반환되는 표는 정적이며 모든 호출자보다 오래 살아남습니다. 그것을 통해 쓰지
 *       마십시오.
 */
const MonType *mon_stats(int type);

/**
 * @brief One attack slot of a kind, or 0 past its last.
 *
 * ENGLISH: The count is not stored: the slots run to the first ::ATK_NONE, so
 * there is no length beside the table that could disagree with it.
 * 한국어: 개수는 저장되지 않습니다. 슬롯은 첫 ::ATK_NONE까지이므로, 표 곁에서 표와 다른 말을
 * 할 수 있는 길이가 없습니다.
 *
 * @param[in] type A ::MonKind.
 * @param[in] slot 0 .. ::MON_MAX_ATTACKS - 1.
 * @return The slot, or 0 if `type` or `slot` is out of range or the slot is empty.
 */
const MonAttack *mon_attack(int type, int slot);

/**
 * @brief How many attacks a kind carries.
 * @param[in] type A ::MonKind.
 * @return 0 .. ::MON_MAX_ATTACKS. ::AI_INERT kinds return 0.
 */
int mon_attack_count(int type);

/**
 * @brief The monster kind an entity name in level text refers to.
 *
 * ENGLISH
 * -------
 * @param[in] kind Entity kind string, e.g. "water_spirit" or "brute".
 * @return The ::MonTypeID, or -1 if no kind has that name.
 *
 * @note Matched against ::MonType::name, so the names live in the stat table
 *       and cannot drift from the kinds they place.
 *
 * 한국어
 * ------
 * @brief 레벨 텍스트의 엔티티 이름이 가리키는 몬스터 종류.
 *
 * @param[in] kind 엔티티 종류 문자열. 예를 들어 "water_spirit"이나 "brute"입니다.
 * @return ::MonTypeID 값. 그 이름을 가진 종류가 없으면 -1입니다.
 *
 * @note ::MonType::name과 대조하므로 이름이 수치 표 안에 살며, 자신이 배치하는 종류와
 *       어긋날 수 없습니다.
 */
int mon_type_for(const char *kind);

/* --- Lifecycle / 수명 주기 --- */

/**
 * @brief Clears every monster and every shot.
 *
 * ENGLISH
 * -------
 * @param[out] pl Pools to clear.
 *
 * @note Call before spawning or respawning.
 * @note CLEARS THE SPAWNERS TOO, because they are the reason there would be
 *       more monsters: a reset that left them running would have the previous
 *       level still delivering into the new one.
 * @note Leaves ::EnemyPool::rng alone. A zeroed pool means "not seeded yet"
 *       and is seeded on first use, so clearing it here would restart the
 *       fight randomness on every level rather than only on a new run.
 *
 * 한국어
 * ------
 * @brief 모든 몬스터와 모든 발사체를 지웁니다.
 *
 * @param[out] pl 지울 풀.
 *
 * @note 생성이나 재생성 전에 호출하십시오.
 * @note *스포너도 함께 지웁니다.* 몬스터가 더 생길 이유가 바로 그것이기 때문입니다. 그것을
 *       돌려 둔 채로 초기화하면 이전 레벨이 새 레벨로 계속 배달하게 됩니다.
 * @note ::EnemyPool::rng는 건드리지 않습니다. 0인 풀은 "아직 씨앗이 채워지지 않음"을 뜻하며
 *       처음 쓰일 때 씨앗을 받습니다. 이곳에서 지우면 전투 난수가 새 플레이마다가 아니라
 *       레벨마다 다시 시작하게 됩니다.
 */
void enemy_reset(Pools *pl);

/**
 * @brief Places monsters at the level's spawn entities.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl Pools to fill.
 * @param[in]     l  Level whose entities say what goes where.
 *
 * @note Reads what the level AUTHORED. ::enemy_wave_arm is what multiplies it
 *       by how deep the run has got; this function knows nothing about waves.
 *
 * 한국어
 * ------
 * @brief 레벨의 생성 엔티티 자리에 몬스터를 배치합니다.
 *
 * @param[in,out] pl 채울 풀.
 * @param[in]     l  무엇이 어디에 놓이는지를 엔티티가 말해 주는 레벨.
 *
 * @note 레벨이 *제작한* 것을 읽습니다. 거기에 플레이가 얼마나 깊어졌는지를 곱하는 것은
 *       ::enemy_wave_arm이며, 이 함수는 웨이브에 대해 아무것도 알지 못합니다.
 */
void enemy_spawn_level(Pools *pl, const Level *l);

/* --- Waves / 웨이브 --- */

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
 * @brief How many spawners this pool is running.
 *
 * ENGLISH
 * -------
 * @param[in] pl Pools to ask.
 * @return The count. 0 means "not an arena" -- an ordinary level whose
 *         monsters were laid out rather than sent in waves.
 *
 * 한국어
 * ------
 * @brief 이 풀이 돌리고 있는 스포너의 수.
 *
 * @param[in] pl 질의할 풀.
 * @return 그 개수. 0은 "아레나가 아님"을 뜻합니다. 몬스터가 웨이브로 보내진 것이 아니라
 *         배치된 평범한 레벨입니다.
 */
int enemy_spawner_count(const Pools *pl);

/**
 * @brief Borrows one spawner by index.
 *
 * ENGLISH
 * -------
 * @param[in] pl Pool to query.
 * @param[in] i  Slot index, below ::enemy_spawner_count.
 * @return The spawner, or 0 when the index is out of range.
 * @note Exists because a spawner is WHERE THE FIGHT IS, and that is a question
 *       from outside this module: `at centre` in loot.txt pays a cleared wave
 *       at the average of them, which is the middle of the room the player was
 *       just fighting in rather than the middle of whatever the map's bounding
 *       box happens to be.
 *
 * 한국어
 * ------
 * @brief 인덱스로 스포너 하나를 빌려 옵니다.
 * @param[in] pl 질의할 풀.
 * @param[in] i  슬롯 인덱스. ::enemy_spawner_count 미만이어야 합니다.
 * @return 해당 스포너. 인덱스가 범위를 벗어나면 0.
 * @note 존재하는 이유는 스포너가 곧 *전투가 벌어지는 자리*이며, 그것이 이 모듈 바깥에서
 *       오는 질문이기 때문입니다. loot.txt의 `at centre`는 정리된 웨이브를 그것들의 평균
 *       지점에 지급하는데, 그 지점은 맵의 경계 상자 한가운데가 아니라 플레이어가 방금 싸운
 *       방의 한가운데입니다.
 */
const Spawner *enemy_spawner_at(const Pools *pl, int i);

/* --- Per-frame step / 프레임 단위 갱신 --- */

/**
 * @brief Advances every monster and every shot by one frame.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl         Pools to step.
 * @param[in]     l          Level the monsters collide against.
 * @param[in]     player_eye Where the player's eye is, in world metres.
 * @param[in]     dt         Seconds since the last frame.
 * @return Total damage dealt to the player this frame, melee and projectile
 *         hits together.
 *
 * @note RETURNS the damage rather than applying it. This module never touches
 *       the player's health; the caller owns that, which is what keeps the AI
 *       testable with no player at all.
 * @note `player_eye` is passed rather than read from a global on purpose. A
 *       cached copy would be a value whose freshness depended on where in the
 *       call stack it was read.
 *
 * 한국어
 * ------
 * @brief 모든 몬스터와 모든 발사체를 한 프레임 진행시킵니다.
 *
 * @param[in,out] pl         진행시킬 풀.
 * @param[in]     l          몬스터가 충돌하는 레벨.
 * @param[in]     player_eye 플레이어의 눈 위치. 월드 미터 단위입니다.
 * @param[in]     dt         지난 프레임 이후 경과 시간(초).
 * @return 이번 프레임에 플레이어가 입은 총 피해량. 근접 공격과 발사체 명중을 합한 값입니다.
 *
 * @note 피해를 적용하지 않고 *반환합니다*. 이 모듈은 플레이어의 체력을 결코 건드리지
 *       않습니다. 그것은 호출자의 몫이며, 그 덕분에 플레이어 없이도 AI를 시험할 수 있습니다.
 * @note `player_eye`를 전역에서 읽지 않고 인자로 받는 것은 의도적입니다. 사본을 두면 호출
 *       스택의 어디에서 읽느냐에 따라 신선도가 달라지는 값이 됩니다.
 */
int enemy_update(Pools *pl, const Level *l, v3 player_eye, float dt);

/* --- Queries / 조회 --- */

/**
 * @brief How many monster slots this level filled, corpses included.
 *
 * ENGLISH
 * -------
 * @param[in] pl Pools to ask.
 * @return The number of slots to scan, not the number that are alive. Pair it
 *         with ::enemy_at.
 *
 * 한국어
 * ------
 * @brief 이 레벨이 채운 몬스터 슬롯의 수. 시체를 포함합니다.
 *
 * @param[in] pl 질의할 풀.
 * @return 살아 있는 수가 아니라 훑어야 할 슬롯의 수입니다. ::enemy_at과 함께 쓰십시오.
 */
int enemy_count(const Pools *pl);

/**
 * @brief How many monsters are still alive.
 *
 * ENGLISH
 * -------
 * @param[in] pl Pools to ask.
 * @return The count, excluding anything in ::E_DEAD.
 *
 * 한국어
 * ------
 * @brief 아직 살아 있는 몬스터의 수.
 *
 * @param[in] pl 질의할 풀.
 * @return ::E_DEAD인 것을 제외한 개수.
 */
int enemy_alive(const Pools *pl);

/**
 * @brief How many of one ::MonTypeID are alive. ::enemy_alive, narrowed.
 *
 * ENGLISH: What ::MonType::cap is checked against, and the question
 * ::Spawner::max_alive cannot ask -- that one counts the room, so a cap of
 * eight is satisfied by eight brutes just as well as by a mixed wave.
 *
 * 한국어: ::MonType::cap이 대조되는 값이며, ::Spawner::max_alive가 물을 수 없는
 * 질문입니다. 그쪽은 *방*을 세므로 상한 여덟은 섞인 웨이브만큼이나 브루트 여덟으로도
 * 충족됩니다.
 */
int enemy_alive_of(const Pools *pl, int type);

/**
 * @brief One monster by index.
 *
 * ENGLISH
 * -------
 * @param[in] pl Pools to read.
 * @param[in] i  Slot index, below ::enemy_count.
 * @return The monster, or 0 if the index is out of range.
 *
 * @note Points into the pool, so it is valid only until that pool is stepped
 *       or reset.
 *
 * 한국어
 * ------
 * @brief 인덱스로 지정한 몬스터 하나.
 *
 * @param[in] pl 읽을 풀.
 * @param[in] i  슬롯 인덱스. ::enemy_count 미만이어야 합니다.
 * @return 해당 몬스터. 인덱스가 범위를 벗어나면 0입니다.
 *
 * @note 풀 안을 가리키므로, 그 풀이 진행되거나 초기화되기 전까지만 유효합니다.
 */
const Enemy *enemy_at(const Pools *pl, int i);

/**
 * @brief How many projectile slots there are to scan.
 *
 * ENGLISH
 * -------
 * @param[in] pl Pools to ask.
 * @return The slot count, inactive slots included. Check ::Shot::active on
 *         each.
 *
 * 한국어
 * ------
 * @brief 훑어야 할 발사체 슬롯의 수.
 *
 * @param[in] pl 질의할 풀.
 * @return 비활성 슬롯을 포함한 슬롯 수. 각각의 ::Shot::active를 확인하십시오.
 */
int enemy_shot_count(const Pools *pl);

/**
 * @brief One projectile by index.
 *
 * ENGLISH
 * -------
 * @param[in] pl Pools to read.
 * @param[in] i  Slot index, below ::enemy_shot_count.
 * @return The shot, or 0 if the index is out of range.
 *
 * @note Points into the pool, so it is valid only until that pool is stepped
 *       or reset.
 *
 * 한국어
 * ------
 * @brief 인덱스로 지정한 발사체 하나.
 *
 * @param[in] pl 읽을 풀.
 * @param[in] i  슬롯 인덱스. ::enemy_shot_count 미만이어야 합니다.
 * @return 해당 발사체. 인덱스가 범위를 벗어나면 0입니다.
 *
 * @note 풀 안을 가리키므로, 그 풀이 진행되거나 초기화되기 전까지만 유효합니다.
 */
const Shot *enemy_shot_at(const Pools *pl, int i);

/* --- Taking damage / 피해 처리 --- */

/**
 * @brief Finds the nearest living monster a ray hits.
 *
 * ENGLISH
 * -------
 * @param[in]  pl      Pools to test against.
 * @param[in]  o       Ray origin.
 * @param[in]  d       Ray direction. Expected to be normalised.
 * @param[in]  maxdist How far to look.
 * @param[out] out_t   Distance along the ray to the hit. Written on a hit.
 * @param[out] out_idx Index of the monster hit. Written on a hit.
 * @return 1 if a monster was hit within `maxdist`, 0 otherwise.
 *
 * @note Monsters are tested as upright CYLINDERS, not as their billboards. A
 *       billboard faces the camera, so hit detection against one would depend
 *       on where the shot was fired from rather than on where the monster is.
 * @note Skips ::E_DEAD, so a corpse does not absorb a shot meant for what is
 *       standing behind it.
 * @note Used by the shotgun so a pellet stops at a monster instead of passing
 *       through it.
 *
 * 한국어
 * ------
 * @brief 광선이 맞히는 가장 가까운 살아 있는 몬스터를 찾습니다.
 *
 * @param[in]  pl      대상이 되는 풀.
 * @param[in]  o       광선의 원점.
 * @param[in]  d       광선의 방향. 정규화되어 있다고 가정합니다.
 * @param[in]  maxdist 탐색할 최대 거리.
 * @param[out] out_t   명중 지점까지 광선을 따라간 거리. 명중했을 때 기록됩니다.
 * @param[out] out_idx 맞은 몬스터의 인덱스. 명중했을 때 기록됩니다.
 * @return `maxdist` 안에서 몬스터를 맞혔으면 1, 아니면 0입니다.
 *
 * @note 몬스터는 빌보드가 아니라 곧게 선 *원기둥*으로 판정합니다. 빌보드는 카메라를 향하므로,
 *       그것으로 판정하면 몬스터가 어디에 있는지가 아니라 어디에서 쏘았는지에 명중이
 *       좌우됩니다.
 * @note ::E_DEAD는 건너뜁니다. 시체가 뒤에 서 있는 것을 겨눈 사격을 대신 받아 내지 않게 하기
 *       위함입니다.
 * @note 샷건이 사용하며, 탄환이 몬스터를 통과하지 않고 그곳에서 멈추게 합니다.
 */
int enemy_hitscan(const Pools *pl, v3 o, v3 d, float maxdist, float *out_t, int *out_idx);

/**
 * @brief Deals damage to a monster, flinching or killing it.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl  Pools holding the monster.
 * @param[in]     idx Index of the monster to hurt. Out of range is ignored.
 * @param[in]     dmg Damage to deal.
 * @param[in]     dir Direction the shot came from, for knockback and for which
 *                    way a death faces.
 *
 * @note The flinch is rate-limited per kind by ::MonType::pain_lock, so a fast
 *       enough weapon cannot hold a monster still until it dies.
 *
 * 한국어
 * ------
 * @brief 몬스터에게 피해를 입혀 경직시키거나 죽입니다.
 *
 * @param[in,out] pl  그 몬스터를 담고 있는 풀.
 * @param[in]     idx 피해를 입힐 몬스터의 인덱스. 범위를 벗어나면 무시합니다.
 * @param[in]     dmg 피해량.
 * @param[in]     dir 사격이 날아온 방향. 넉백과 사망 방향에 쓰입니다.
 *
 * @note 경직은 ::MonType::pain_lock으로 종류마다 빈도가 제한되므로, 충분히 빠른 무기라도
 *       몬스터를 죽을 때까지 붙잡아 둘 수 없습니다.
 */
void enemy_hurt(Pools *pl, int idx, int dmg, v3 dir);

/**
 * @brief The same, told who did it -- which is how a monster learns to fight
 *        another monster.
 *
 * ENGLISH: ::enemy_hurt is this with `from` = -1, meaning the player, and
 * every one of its twenty-odd callers is a player weapon. Keeping the short
 * name for that case is not laziness: "the player hurt it" is the overwhelming
 * majority and a -1 threaded through every grapple and pellet would be noise
 * at each site to serve one caller that is not the player.
 *
 * @param from Index of the attacking monster, or -1 for the player.
 *
 * 한국어: ::enemy_hurt는 `from`이 -1인 이것이며, -1은 플레이어를 뜻합니다. 그 스무 곳 남짓의
 * 호출자가 전부 플레이어의 무기입니다. 그 경우에 짧은 이름을 남겨 두는 것은 게으름이
 * 아닙니다. "플레이어가 때렸다"가 압도적 다수이고, 모든 갈고리와 펠릿에 -1을 꿰는 것은
 * 플레이어가 아닌 호출자 하나를 위해 모든 자리에 소음을 두는 일입니다.
 *
 * @param from 공격한 몬스터의 색인. 플레이어이면 -1입니다.
 */
void enemy_hurt_by(Pools *pl, int idx, int dmg, v3 dir, int from);

/**
 * @brief Takes what a corpse owes the floor, clearing it so it is owed once.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl     Pools holding the monsters.
 * @param[in]     idx    Slot index, below ::enemy_count.
 * @param[out]    out_at Where to put it -- the body's own feet. Untouched when
 *                       nothing is owed.
 * @return A PK_* kind, ::LOOT_HELD, or -1 when this monster owes nothing.
 *
 * @note CLEARS AS IT READS, which is the whole contract: a caller that drains
 *       the pool every frame must not be paid twice by the corpse that lies
 *       there for ::CORPSE_FADE seconds afterwards.
 * @note Answers -1 for an empty or out-of-range slot, so a caller may sweep
 *       [0, ::enemy_count) without testing `active` first.
 * @note ::LOOT_HELD is returned UNRESOLVED. See ::Enemy::drop for why this
 *       module cannot answer it, and ::loot_held_kind for who can.
 *
 * 한국어
 * ------
 * @brief 시체가 바닥에 빚진 것을 가져오며, 한 번만 빚지도록 지웁니다.
 * @param[in,out] pl     몬스터를 담은 풀.
 * @param[in]     idx    슬롯 인덱스. ::enemy_count 미만이어야 합니다.
 * @param[out]    out_at 놓을 자리. 시체 자신의 발치입니다. 빚진 것이 없으면 건드리지 않습니다.
 * @return PK_* 종류, ::LOOT_HELD, 또는 이 몬스터가 빚진 것이 없으면 -1.
 *
 * @note *읽으면서 지우며*, 그것이 계약의 전부입니다. 매 프레임 풀을 훑는 호출자가, 그 뒤
 *       ::CORPSE_FADE초 동안 그 자리에 누워 있는 시체에게 두 번 지급받아서는 안 됩니다.
 * @note 비었거나 범위를 벗어난 슬롯에는 -1로 답하므로, 호출자는 `active`를 먼저 검사하지 않고
 *       [0, ::enemy_count)를 훑어도 됩니다.
 */
int enemy_take_drop(Pools *pl, int idx, v3 *out_at);

/**
 * @brief Takes the monsters that have died since the last call, clearing the tally.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl Pools holding the monsters.
 * @return How many died since this was last asked. Zero on almost every frame.
 *
 * @note CLEARS AS IT READS, exactly like ::enemy_take_drop: a caller that
 *       drains every frame and adds to a total of its own must not be paid
 *       twice for the same corpse.
 * @note A drain, not a query, because there is no question this could answer
 *       otherwise. Corpses keep their slots and are recycled, so counting the
 *       dead ones in the pool would undercount the moment a slot is reused.
 *
 * 한국어
 * ------
 * @brief 마지막 호출 이후 죽은 몬스터 수를 가져오며, 그 집계를 지웁니다.
 * @param[in,out] pl 몬스터를 담은 풀.
 * @return 마지막으로 물었을 때 이후 죽은 수. 거의 모든 프레임에서 0입니다.
 *
 * @note ::enemy_take_drop과 똑같이 *읽으면서 지웁니다*. 매 프레임 훑어 자기 누계에 더하는
 *       호출자가 같은 시체로 두 번 지급받아서는 안 됩니다.
 * @note 질의가 아니라 배수인 이유는, 달리 답할 방법이 없기 때문입니다. 시체는 슬롯을 유지하다
 *       재활용되므로, 풀에서 죽은 슬롯을 세는 방식은 슬롯이 재사용되는 순간 적게 셉니다.
 */
int enemy_take_kills(Pools *pl);

/* --- the boss fight / 보스전 --------------------------------------------- */

/**
 * @brief Which slot holds the living boss, or -1.
 *
 * ENGLISH
 * -------
 * @param[in] pl The pool.
 * @return The first live ::MON_BOSS slot, or -1 when none stands.
 *
 * A SLOT AND NOT A YES/NO, because ::world_boss_present's caller now wants one
 * more thing than "is something big up": the health bar needs a SUBJECT. The
 * predicate is this function compared against zero, which is how world.h keeps
 * the signature it promised to keep.
 *
 * @note First rather than nearest. There is never more than one -- ::step_boss
 *       refuses to summon while this is non-negative -- and "first" is a rule a
 *       test can state, where "nearest" would need a position it has no reason
 *       to take.
 *
 * 한국어
 * ------
 * @brief 살아 있는 보스가 있는 슬롯. 없으면 -1.
 * @param[in] pl 풀.
 * @return 첫 생존 ::MON_BOSS 슬롯. 서 있는 것이 없으면 -1.
 *
 * *예/아니오가 아니라 슬롯인 이유는*, ::world_boss_present의 호출자가 이제 "큰 것이 떴는가"
 * 외에 하나를 더 원하기 때문입니다. 체력바에는 *대상*이 필요합니다. 술어는 이 함수를 0과
 * 비교한 것이며, 그것이 world.h가 지키겠다고 한 시그니처를 지키는 방법입니다.
 *
 * @note 가장 가까운 것이 아니라 첫 번째입니다. 둘 이상은 결코 없으며(::step_boss가 이 값이
 *       음수가 아닌 동안 소환을 거절합니다), "첫 번째"는 테스트가 진술할 수 있는 규칙인 반면
 *       "가장 가까운"은 받을 이유가 없는 위치를 필요로 합니다.
 */
int enemy_boss_index(const Pools *pl);

/**
 * @brief How many wards are still standing.
 *
 * ENGLISH: Zero is what opens the boss, and ::enemy_hurt asks this on every
 * blow that lands on a ::MON_BOSS. Bounded by ::ENEMY_MAX and cheap, for the
 * reason ::world_boss_present's scan was.
 *
 * 한국어: 0이 보스를 여는 조건이며, ::enemy_hurt는 ::MON_BOSS에 닿는 모든 타격마다 이것을
 * 묻습니다. ::ENEMY_MAX로 제한되어 저렴하며, ::world_boss_present의 스캔이 그러했던 것과 같은
 * 이유입니다.
 */
int enemy_guards_alive(const Pools *pl);

/**
 * @brief Live monsters that a WAVE is responsible for -- boss and wards excluded.
 *
 * ENGLISH
 * -------
 * ::enemy_wave_done asks this instead of ::enemy_alive, and that one call site
 * is the whole of the difference. Without it a standing boss freezes the wave
 * counter: a wave completes only when the arena is empty, the maw is never
 * empty, and the clock that schedules the NEXT boss stops on the fight it was
 * scheduling. That is commit 9d8099a's failure approached from the other side.
 *
 * @note ::enemy_alive is deliberately NOT changed. ::Spawner::max_alive is a
 *       ceiling on what occupies a slot, and a boss occupies one.
 * @note A ward's SUMMONS are ordinary monsters and are counted. They are the
 *       wave's business; the thing that made them is not.
 *
 * 한국어
 * ------
 * @brief *웨이브*가 책임지는 생존 몬스터. 보스와 결계핵은 제외합니다.
 *
 * ::enemy_wave_done이 ::enemy_alive 대신 이것을 묻고, 그 한 호출 지점이 차이의 전부입니다.
 * 이것이 없으면 서 있는 보스가 웨이브 계수기를 세웁니다. 웨이브는 아레나가 비어야 끝나는데
 * 아귀는 결코 비지 않으므로, *다음* 보스를 예약하는 시계가 자신이 예약하던 그 전투에서
 * 멈춥니다. 커밋 9d8099a의 실패를 반대편에서 만난 것입니다.
 *
 * @note ::enemy_alive는 의도적으로 바꾸지 않습니다. ::Spawner::max_alive는 슬롯을 차지하는
 *       것에 대한 상한이고, 보스는 슬롯을 차지합니다.
 * @note 결계핵의 *소환물*은 평범한 몬스터이며 셉니다. 그들은 웨이브의 일이고, 그들을 만든
 *       것은 아닙니다.
 */
int enemy_alive_minions(const Pools *pl);

/**
 * @brief Reads the level's ward CANDIDATE markers into the fight.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl The pool whose ::BossFight is filled.
 * @param[in]     l  The level to read.
 *
 * CANDIDATES, NOT WARDS. Nothing is spawned here: a ward exists only while a
 * fight is under way, so the markers are recorded and ::enemy_ward_place is
 * what turns some of them into monsters. This is also why the markers are
 * `info_ward_*` aliases rather than `monster_ward` -- the `monster_` prefix
 * would have made ::enemy_spawn_level create them at load, which is the one
 * thing they must not do.
 *
 * SORTED BY POSITION before it returns. The entity list is in .map file order,
 * and world.c already refused that dependency once by name: "'First in the
 * entity list' is a property of how the map was saved." Without the sort,
 * regrouping two brushes in TrenchBroom changes which wards a given seed picks,
 * with nothing in the level diff to explain it.
 *
 * 한국어
 * ------
 * @brief 레벨의 결계핵 *후보* 표식을 전투로 읽어들입니다.
 * @param[in,out] pl ::BossFight가 채워질 풀.
 * @param[in]     l  읽을 레벨.
 *
 * *결계핵이 아니라 후보입니다.* 이곳에서 생성되는 것은 없습니다. 결계핵은 전투가 진행 중일
 * 때만 존재하므로 표식은 기록만 되고, 그중 일부를 몬스터로 만드는 것은 ::enemy_ward_place입니다.
 * 표식이 `monster_ward`가 아니라 `info_ward_*` 별칭인 이유이기도 합니다. `monster_` 접두사였다면
 * ::enemy_spawn_level이 로드 시점에 그것을 만들었을 텐데, 그것이야말로 해서는 안 되는 단 하나의
 * 일입니다.
 *
 * 반환 전에 *위치로 정렬합니다.* 엔티티 목록은 .map 파일 순서이고, world.c는 이미 그 의존을
 * 이름을 붙여 거절한 적이 있습니다. *"«엔티티 목록의 첫 번째»는 맵이 어떻게 저장되었는가의
 * 성질입니다."* 정렬이 없으면 TrenchBroom에서 브러시 둘을 재정렬한 것만으로 같은 시드가 다른
 * 결계핵을 고르고, 레벨 diff에는 그것을 설명하는 것이 없습니다.
 */
void enemy_ward_scan(Pools *pl, const Level *l);

/**
 * @brief Raises a fresh set of wards, avoiding the set the last cycle used.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl The pool.
 * @param[in]     l  The level, for the ground search each ward needs.
 * @return How many wards were actually raised. Fewer than asked is not a fault.
 *
 * EXACTLY ::BOSS_WARDS DRAWS FROM THE POOL RNG, whatever it picks and however
 * many candidates there are. A rejection loop -- "draw again until the set is
 * new" -- consumes a variable number, and enemy.c already states why that is
 * fatal here: the drop table spends both of its rolls whether or not the first
 * succeeds, "because a `chance` that short-circuits the second roll makes the
 * drop table an input to the AI, which is a demo that desynchronises the first
 * time a rate moves." A partial Fisher-Yates over the candidate list has the
 * same property and picks without replacement for free.
 *
 * @note Two independent draws, one per list. Drawing ::BOSS_WARDS from a single
 *       pool can produce an all-air or all-ground cycle by chance, and those
 *       are two different fights -- three times in a row is reachable.
 * @note With no candidates at all this raises nothing and returns 0, which
 *       leaves the maw open from its first frame. That is deliberate: a map
 *       that marked none would otherwise hold an unkillable boss in a level
 *       with no exit. ::DIAG_WARD_CAND is raised so it is not silent.
 *
 * 한국어
 * ------
 * @brief 새 결계핵 무리를 세웁니다. 지난 사이클이 쓴 자리는 피합니다.
 * @param[in,out] pl 풀.
 * @param[in]     l  각 결계핵에 필요한 지면 탐색을 위한 레벨.
 * @return 실제로 세운 결계핵의 수. 요청보다 적은 것은 결함이 아닙니다.
 *
 * 무엇을 고르든, 후보가 몇이든, *풀 RNG에서 정확히 ::BOSS_WARDS번 뽑습니다.* "새 조합이 나올
 * 때까지 다시 뽑기"는 소비하는 횟수가 가변이고, enemy.c는 그것이 왜 여기서 치명적인지 이미
 * 밝히고 있습니다. 드롭 표는 첫 굴림의 성패와 무관하게 두 굴림을 모두 소비하는데, *"두 번째
 * 굴림을 건너뛰는 `chance`는 드롭 표를 AI의 입력으로 만들며, 그것은 확률이 처음 움직이는 순간
 * 어긋나는 데모이기 때문"*입니다. 후보 목록에 대한 부분 Fisher-Yates는 같은 성질을 가지면서
 * 비복원 추출을 공짜로 줍니다.
 *
 * @note 목록마다 하나씩, *독립적인 두 번의 추출*입니다. 하나의 풀에서 ::BOSS_WARDS를 뽑으면
 *       우연히 전원 공중형이거나 전원 지상형인 사이클이 나올 수 있고, 그 둘은 서로 다른
 *       전투이며 3연속도 도달 가능합니다.
 * @note 후보가 아예 없으면 아무것도 세우지 않고 0을 반환하며, 그러면 아귀는 첫 프레임부터
 *       열린 채입니다. 의도적입니다. 그러지 않으면 후보를 표시하지 않은 맵이 출구 없는 레벨에
 *       죽일 수 없는 보스를 붙들게 됩니다. 조용하지 않도록 ::DIAG_WARD_CAND를 올립니다.
 */
int enemy_ward_place(Pools *pl, const Level *l);

/**
 * @brief Puts a boss in the level at its ::MON_MAW marker, or anywhere it can.
 *
 * ENGLISH: Endless mode summons a second maw long after the level's own marker
 * has been consumed, so the position is remembered from the marker rather than
 * re-read. Returns 0 when there is nowhere to put one, which a caller should
 * treat as "not yet" rather than as a fault -- the next wave will ask again.
 *
 * 한국어: 무한 모드는 레벨 자신의 표식이 소비되고 한참 뒤에 두 번째 아귀를 소환하므로, 위치는
 * 다시 읽지 않고 표식에서 기억해 둡니다. 놓을 자리가 없으면 0을 반환하며, 호출자는 이를 결함이
 * 아니라 "아직 아님"으로 다루어야 합니다. 다음 웨이브가 다시 물어봅니다.
 */
int enemy_boss_summon(Pools *pl, const Level *l);

/**
 * @brief Puts the boss's health back up to `to`, never down.
 *
 * ENGLISH
 * -------
 * @param[in,out] pl The pool.
 * @param[in]     to The health to restore to.
 *
 * WHAT AN EXPIRED GROGGY WINDOW COSTS. ::BOSS_GROGGY_MAX has the argument; in
 * short, a window with no timer is a promise the player can decline, and taking
 * the segment back is how declining it costs something.
 *
 * @note NEVER DOWNWARD, which is why this is not a plain assignment. Called
 *       with a ceiling computed from a cycle count, and a cycle count that is
 *       ever wrong by one would otherwise TAKE health off a boss the player has
 *       legitimately hurt -- turning an off-by-one into an unwinnable fight
 *       rather than into a visible glitch.
 * @note Not a death path and cannot become one: it only raises, so it can never
 *       reach zero and never has to decide anything. ::enemy_hurt stays the one
 *       place a monster dies.
 *
 * 한국어
 * ------
 * @brief 보스의 체력을 `to`까지 되돌립니다. 결코 내리지 않습니다.
 * @param[in,out] pl 풀.
 * @param[in]     to 되돌릴 체력.
 *
 * 만료된 그로기 창이 치르는 값입니다. 논거는 ::BOSS_GROGGY_MAX에 있습니다. 요약하면, 타이머가
 * 없는 창은 플레이어가 거절할 수 있는 약속이고, 구간을 회수하는 것이 그 거절에 값을 매기는
 * 방법입니다.
 *
 * @note *결코 내리지 않으며*, 그것이 이것이 단순 대입이 아닌 이유입니다. 사이클 수에서 계산한
 *       천장을 받는데, 사이클 수가 한 번이라도 1만큼 틀리면 그러지 않을 경우 플레이어가 정당하게
 *       깎은 보스의 체력을 *빼앗게* 됩니다. off-by-one을 눈에 보이는 결함이 아니라 이길 수 없는
 *       전투로 만드는 것입니다.
 * @note 사망 경로가 아니며 그렇게 될 수도 없습니다. 올리기만 하므로 결코 0에 닿지 않고 아무것도
 *       결정할 필요가 없습니다. ::enemy_hurt가 몬스터가 죽는 유일한 곳으로 남습니다.
 */
void enemy_boss_heal(Pools *pl, int to);

#endif
