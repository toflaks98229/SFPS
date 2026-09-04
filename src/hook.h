/**
 * @file hook.h
 * @brief The grapple: what its four beats cost, and the six calls that run them.
 *
 * ENGLISH
 * -------
 * hook.c had no header. Its six functions were declared in weapon.h and its
 * tuning sat there too, among the shotgun's, so the file's boundary was a
 * thing you could only find by reading both files and sorting the names by
 * prefix. The grapple had already been moved out of weapon.c for being its own
 * mechanism; this is the other half of that move, and without it the split was
 * one a reader could not see.
 *
 * WHAT IS HERE: the numbers that decide how the hook feels, and the calls that
 * advance it. Four beats -- fire, pull, impact, launch -- and one constant
 * block per beat, in the order they happen.
 *
 * WHAT IS NOT: the state. ::Weapon owns `hook_state` and the fields around it,
 * and ::HookState stays in weapon.h with them, because a struct cannot hold a
 * type its own header does not have. That is not a leftover -- the grapple IS
 * a weapon here, fired from the same launcher with the same trigger discipline,
 * and giving it a second struct to keep in step with the first would buy a
 * tidier header at the price of two things that can disagree.
 *
 * So this header includes weapon.h rather than the other way round: the hook
 * operates on a Weapon, and a Weapon does not know the hook exists.
 *
 * 한국어
 * ------
 * @brief 그래플. 네 박자의 비용과, 그것을 진행시키는 여섯 개의 호출입니다.
 *
 * hook.c에는 헤더가 없었습니다. 여섯 개의 함수가 weapon.h에 선언되어 있었고 수치 조정값도
 * 샷건의 것들 사이에 함께 있었으므로, 이 파일의 경계는 두 파일을 모두 읽고 이름을 접두사로
 * 분류해야만 찾을 수 있는 것이었습니다. 그래플은 자기만의 메커니즘이라는 이유로 이미
 * weapon.c에서 분리되었습니다. 이것이 그 이동의 나머지 절반이며, 이것이 없으면 그 분리는
 * 읽는 사람이 볼 수 없는 분리였습니다.
 *
 * 이곳에 있는 것: 훅의 감각을 결정하는 수치들과 그것을 진행시키는 호출들입니다. 네 박자
 * (발사·견인·충격·도약)와 박자마다 하나의 상수 블록이, 일어나는 순서대로 있습니다.
 *
 * 이곳에 없는 것: *상태*입니다. ::Weapon이 `hook_state`와 그 주변 필드를 소유하며,
 * ::HookState는 그것들과 함께 weapon.h에 남습니다. 구조체는 자기 헤더에 없는 타입을 담을 수
 * 없기 때문입니다. 이는 잔재가 아닙니다. 이곳에서 그래플은 *무기*이며, 같은 발사기에서 같은
 * 방아쇠 규율로 발사됩니다. 두 번째 구조체를 주어 첫 번째와 보조를 맞추게 하는 것은 서로
 * 어긋날 수 있는 두 가지를 대가로 더 깔끔한 헤더를 사는 일입니다.
 *
 * 그래서 이 헤더가 weapon.h를 포함하며 그 반대가 아닙니다. 훅은 Weapon에 대해 동작하고,
 * Weapon은 훅이 존재한다는 것을 모릅니다.
 */
#ifndef HOOK_H
#define HOOK_H

#include "weapon.h"   /* Weapon, HookState, and Level through it */

/* --- the grapple hook: a DOOM Eternal Meat Hook ---
 *
 * ENGLISH
 * -------
 * Four beats, in order, and the whole design follows from keeping them
 * distinct:
 *
 *   1. FIRE     the claw leaves the launcher and flies. It is a projectile
 *               with a travel time, not an instant raycast -- the flight is
 *               what makes the hook feel thrown rather than teleported.
 *   2. PULL     on a hit, the player is reeled to the target under their own
 *               momentum. This is a winch, deliberately: a Meat Hook closes
 *               distance, where the rope constraint it replaces held a length
 *               and let you swing. Both are valid hooks; they are not the
 *               same mechanic, and this one is the aggressive one.
 *   3. IMPACT   arriving deals damage. Hooking a monster is an attack, not
 *               just travel, which is what makes it a combat tool.
 *   4. LAUNCH   the player bounces off automatically, up and along their
 *               travel. No button, no timing -- arriving IS the launch, so a
 *               hook chains into the next one instead of dumping you at the
 *               target's feet.
 *
 * Why a winch here when the previous version was a rope: a rope's whole point
 * is that it does NOT close distance -- it holds a length so gravity can turn
 * a fall into an arc. That is the right model for swinging and the wrong one
 * for this. A Meat Hook is supposed to yank you in, and the arc it produces
 * comes from the launch at the end rather than from the tether.
 *
 * 한국어
 * ------
 * 네 단계가 순서대로 진행되며, 이 단계들을 명확히 구분하는 것에서 전체 설계가
 * 도출됩니다:
 *
 *   1. 발사(FIRE)   클로가 발사기를 떠나 날아갑니다. 즉발 레이캐스트가 아니라
 *                   비행 시간을 가진 발사체입니다. 이 비행이 훅을 순간이동이
 *                   아닌 "던진 것"처럼 느끼게 합니다.
 *   2. 견인(PULL)   명중하면 플레이어가 자신의 운동량을 받아 대상 쪽으로
 *                   감겨 들어갑니다. 이는 의도적인 윈치 방식입니다. 미트 훅은
 *                   거리를 좁히는 도구인 반면, 이것이 대체하는 로프 구속은
 *                   길이를 유지하며 스윙하게 하는 도구였습니다. 둘 다 유효한
 *                   훅이지만 같은 메커니즘이 아니며, 이쪽이 공격적인 쪽입니다.
 *   3. 충격(IMPACT) 도달 시 피해를 입힙니다. 몬스터를 거는 것은 단순한 이동이
 *                   아니라 공격이며, 이것이 훅을 전투 도구로 만듭니다.
 *   4. 도약(LAUNCH) 플레이어가 진행 방향과 위쪽으로 자동으로 튀어 오릅니다.
 *                   버튼도 타이밍도 필요 없습니다. 도달하는 것 자체가 도약이며,
 *                   따라서 훅이 대상의 발밑에 떨구는 대신 다음 훅으로
 *                   이어집니다.
 *
 * 이전 버전이 로프였는데 여기서 윈치를 쓰는 이유: 로프의 핵심은 거리를 좁히지
 * *않는다*는 것입니다. 길이를 유지하여 중력이 낙하를 궤도로 바꾸게 합니다. 그것은
 * 스윙에는 옳고 이 메커니즘에는 그른 모델입니다. 미트 훅은 플레이어를 끌어당겨야
 * 하며, 이때 생기는 궤적은 로프가 아니라 마지막의 도약에서 나옵니다.
 */
#define HOOK_RANGE          20.0f  ///< @brief Metres the claw can reach before giving up. / 클로가 포기하기 전까지 도달할 수 있는 거리 (미터).
#define HOOK_ARRIVE_DIST     1.6f  ///< @brief Distance at which the pull counts as arrival. / 견인이 도달로 인정되는 거리.

/* --- arriving when the anchor cannot physically be reached ---
 *
 * ENGLISH
 * -------
 * A hook fired at the FLOOR never satisfies HOOK_ARRIVE_DIST, and the reason
 * is geometric rather than a tuning mistake. The pull measures from the
 * player's EYE, but the player stands on the floor, so the eye can never get
 * closer to a floor anchor than PLAYER_EYE -- 1.70m, which is larger than the
 * 1.60m arrival radius. Measured on a straight-down hook: the distance falls
 * to exactly 1.70m within 0.75s and then stops changing for the remaining
 * 2.25s until HOOK_PULL_TIMEOUT gives up. The player has visibly arrived and
 * the hook refuses to admit it.
 *
 * Raising HOOK_ARRIVE_DIST past PLAYER_EYE would "fix" it and break the rest:
 * every wall hook would then complete nearly two metres early, which is where
 * the launch comes from, so every hook would end short.
 *
 * The honest condition is not "am I close enough" but "am I still getting
 * closer". A pull that has stopped closing has finished, wherever it stopped:
 * against the floor, against a ledge the player is wedged under, or against a
 * monster backed into a corner. STALL_DIST is how much progress counts as
 * progress, and STALL_TIME is how long a lack of it is tolerated before the
 * hook accepts that this is as close as it gets.
 *
 * Deliberately NOT a velocity test. The winch keeps accelerating into the
 * floor for as long as it is running, so the player's speed stays high while
 * they go nowhere -- the thing that has stalled is the distance, so the
 * distance is what is watched.
 *
 * 한국어
 * ------
 * *바닥*을 향해 쏜 훅은 HOOK_ARRIVE_DIST를 결코 만족하지 못하며, 이는 튜닝 실수가 아니라
 * 기하학적 문제입니다. 견인은 플레이어의 *눈*에서 거리를 재는데 플레이어는 바닥 위에
 * 서므로, 눈은 바닥 앵커에 PLAYER_EYE(1.70m)보다 가까워질 수 없고 이는 도달 반경
 * 1.60m보다 큽니다. 수직 하향 훅에서 측정한 결과, 거리는 0.75초 만에 정확히 1.70m로
 * 떨어진 뒤 HOOK_PULL_TIMEOUT이 포기할 때까지 남은 2.25초 동안 전혀 변하지 않았습니다.
 * 플레이어는 눈에 보이게 도착했는데 훅이 그것을 인정하지 않는 것입니다.
 *
 * HOOK_ARRIVE_DIST를 PLAYER_EYE보다 크게 올리면 이 문제는 "해결"되지만 나머지가
 * 망가집니다. 그러면 모든 벽 훅이 2미터 가까이 일찍 완료되는데, 도약이 바로 그 지점에서
 * 나오므로 모든 훅이 짧게 끝나게 됩니다.
 *
 * 정직한 조건은 "충분히 가까운가"가 아니라 "아직도 가까워지고 있는가"입니다. 더 이상
 * 좁혀지지 않는 견인은 어디서 멈췄든 끝난 것입니다. 바닥이든, 플레이어가 끼어 버린 선반
 * 아래든, 구석에 몰린 몬스터든 마찬가지입니다. STALL_DIST는 얼마만큼의 진전을 진전으로
 * 인정할지이고, STALL_TIME은 진전이 없는 상태를 얼마나 참아 준 뒤 이것이 도달할 수 있는
 * 최선이라고 받아들일지입니다.
 *
 * 의도적으로 *속도* 판정이 아닙니다. 윈치는 작동하는 한 계속 바닥을 향해 가속하므로
 * 플레이어가 제자리에 있어도 속도는 높게 유지됩니다. 멈춘 것은 거리이므로 거리를
 * 감시합니다. */
#define HOOK_STALL_DIST      0.05f ///< @brief Metres of progress that still counts as closing. / 좁혀지는 것으로 인정되는 진전 거리 (미터).
#define HOOK_STALL_TIME      0.18f ///< @brief Seconds without progress before the pull counts as arrived. / 진전 없이 이 시간이 지나면 도달로 간주합니다 (초).

/* --- 1. the claw in flight ---
 * A projectile, so the throw reads as a throw. Fast enough that it never
 * feels like waiting -- at 90 m/s the full 40 m range takes under half a
 * second -- but slow enough that you can watch it travel.
 *
 * 발사체이므로 던지는 동작이 던지는 것처럼 읽힙니다. 기다린다는 느낌이 전혀 들지
 * 않을 만큼 빠르지만(초속 90m에서 최대 사거리 40m를 0.5초 이내에 주파), 날아가는
 * 것을 눈으로 좇을 수 있을 만큼은 느립니다. */
#define HOOK_FLY_SPEED      90.0f  ///< @brief Claw travel speed, m/s. / 클로의 비행 속도 (m/s).
#define HOOK_FLY_STEP        0.5f  ///< @brief Metres per collision sample while flying. / 비행 중 충돌 검사 간격 (미터).

/* The claw's own appearance while it travels. Without something drawn at
   hook_pos the throw is a rope growing toward nothing: the flight time exists
   and the tether tracks it, but the object supposedly doing the travelling is
   invisible. The spin stops the instant it bites, which is what sells the
   bite.
   비행 중 클로 자체의 외형입니다. hook_pos에 무언가 그려지지 않으면 투척은 아무것도
   없는 곳으로 자라나는 로프일 뿐입니다. 비행 시간도 있고 로프도 추적하지만, 정작
   날아간다는 대상이 보이지 않습니다. 회전은 물리는 순간 멈추며, 그것이 물림을 설득력
   있게 만듭니다. */
#define HOOK_CLAW_SIZE       0.22f ///< @brief Billboard size of the flying claw, metres. / 비행 중인 클로 빌보드의 크기 (미터).
#define HOOK_CLAW_SPIN      22.0f  ///< @brief Radians per second the claw tumbles in flight. / 비행 중 클로가 구르는 속도 (초당 라디안).

/* --- 2. the pull ---
 * Acceleration toward the target, capped. A cap is required here in a way it
 * was not for the rope: a pure accelerator over 40 m arrives at a speed that
 * throws the player through the level, and the impact should be decisive
 * rather than lethal to the frame rate.
 *
 * 대상을 향한 가속이며 상한이 있습니다. 로프에서는 필요 없었던 상한이 여기서는
 * 필수입니다. 순수 가속만으로 40m를 이동하면 플레이어를 레벨 밖으로 던져 버릴
 * 속도로 도달하게 되며, 충격은 결정적이어야지 프레임률에 치명적이어서는 안 됩니다. */
#define HOOK_PULL_ACCEL    120.0f  ///< @brief m/s^2 toward the target. / 대상을 향한 가속도 (m/s^2).
#define HOOK_PULL_MAX       38.0f  ///< @brief Top reel-in speed, m/s. / 최대 견인 속도 (m/s).

/* Gravity is cancelled while being pulled. Without this a long horizontal
   hook sags into the floor before it arrives, which reads as the hook failing
   rather than as physics.
   견인 중에는 중력이 상쇄됩니다. 그렇지 않으면 긴 수평 훅이 도달하기 전에 바닥으로
   처지며, 이는 물리 현상이 아니라 훅이 실패한 것처럼 보입니다. */
#define HOOK_PULL_ANTIGRAV   1.0f  ///< @brief Fraction of gravity cancelled while reeling. 1 = fully. / 견인 중 상쇄되는 중력의 비율. 1이면 완전 상쇄.

/* A pull that cannot finish must not last forever -- the target may be moving
   away, or wedged behind geometry the player cannot reach.
   완료될 수 없는 견인이 영원히 지속되어서는 안 됩니다. 대상이 멀어지고 있거나,
   플레이어가 도달할 수 없는 지오메트리 뒤에 끼어 있을 수 있습니다. */
#define HOOK_PULL_TIMEOUT    3.0f  ///< @brief Seconds before an unfinished pull gives up. / 완료되지 않은 견인을 포기하기까지의 시간 (초).

/* How often the winch loop restarts while reeling.
 *
 * Slightly SHORTER than the `hreel` recipe is long (110ms), so each repeat
 * begins a hair before the last has faded and the loop has no gap in it. A
 * longer interval than the sound leaves a stutter; a much shorter one stacks
 * voices and the winch gets louder the longer the pull lasts.
 *
 * 감기 중 윈치 루프가 재시작되는 간격입니다.
 *
 * `hreel` 레시피의 길이(110ms)보다 약간 *짧게* 잡아, 각 반복이 이전 소리가 사라지기
 * 직전에 시작되어 루프에 빈틈이 생기지 않습니다. 소리보다 긴 간격은 끊김을 남기고,
 * 훨씬 짧은 간격은 보이스를 겹쳐 쌓아 견인이 길어질수록 윈치가 커집니다. */
#define HOOK_REEL_INTERVAL   0.09f ///< @brief Seconds between winch loop retriggers. / 윈치 루프 재시작 간격 (초).

/* How fast momentum ACROSS the hook bleeds away while reeling.
 *
 * ENGLISH
 * -------
 * The winch accelerates along the hook and caps that component, but for a long
 * time it did nothing at all to the component across it. Momentum carried into
 * the throw therefore survived the entire pull, and the player flew a curve
 * instead of a line: measured on a 20 m hook, entering at 12 m/s sideways swung
 * the player 5.4 m wide and landed them 3.6 m from the anchor. Entering at
 * 30 m/s -- which a chained hook reaches easily, since HOOK_LAUNCH_MAX is 30
 * and MOMENTUM_DRAG_AIR keeps airborne speed almost in full -- swung 14.3 m
 * wide, missed by 8.5 m, and took 30% longer to arrive.
 *
 * That is not a winch. A Meat Hook is supposed to put you AT the thing you hit,
 * and arriving eight metres to one side of it reads as the hook being broken
 * rather than as physics.
 *
 * Expressed as a per-second decay rather than a hard zero, deliberately. Wiping
 * the perpendicular component outright would make the pull a rail: every hook
 * would arrive along the same line whatever the player was doing, and the
 * momentum they built up would vanish the instant the claw bit. Bleeding it
 * lets the first moments of a pull still curve -- the approach reads as being
 * yanked, and a fast entry still feels different from a standing one -- while
 * guaranteeing the line is straight by the time it matters.
 *
 * At 12.0 the component is down to ~2% of its entry value after a third of a
 * second, which is shorter than every pull that is not nearly at HOOK_RANGE. Lower
 * it for wider, more preserved arcs; raise it for a pull that snaps to the line
 * almost at once. Zero restores the old behaviour exactly.
 *
 * IT WAS 8.0, TUNED WHEN HOOK_RANGE WAS 40m AND THE FIXTURE PULLED 20m. On a 15m
 * pull -- an ordinary shot now that the range is 20m -- 8.0 did not finish, and
 * hooktest measured a 30 m/s entry (what a chained hook reaches, HOOK_LAUNCH_MAX
 * being 30) arriving 16m from its anchor: the player crossed the anchor plane while
 * still far off to the side, and the pull ended there.
 *
 * 12 RATHER THAN 16, because the two properties pull against each other. 16.0 fixed
 * arrival and then failed the other half of the rule -- sideways travel has to
 * SURVIVE into the launch, which is what chains hooks together -- by straightening
 * so hard there was nothing left to carry. Measured at 12.0: every entry speed from
 * 0 to 30 m/s arrives within 1.5m, and a 12 m/s crossing still launches with 0.34
 * m/s of it. Both halves hold, which neither 8 nor 16 manages.
 *
 * 한국어
 * ------
 * 견인 중 훅을 *가로지르는* 방향의 운동량이 감쇠하는 속도입니다.
 *
 * 윈치는 훅 방향으로 가속하고 그 성분을 제한하지만, 오랫동안 그것을 가로지르는 성분에는
 * 아무 조치도 하지 않았습니다. 따라서 투척 시점에 가지고 있던 운동량이 견인 내내
 * 유지되었고, 플레이어는 직선이 아니라 곡선을 그리며 날아갔습니다. 20m 훅에서 측정한
 * 결과, 측면 12m/s로 진입하면 5.4m 옆으로 휘어져 목표에서 3.6m 떨어진 곳에 도달했습니다.
 * 30m/s로 진입하면(HOOK_LAUNCH_MAX가 30이고 MOMENTUM_DRAG_AIR가 공중 속도를 거의 온전히
 * 유지하므로 연속 훅에서 쉽게 도달합니다) 14.3m 옆으로 휘어져 8.5m를 빗나갔고 도달까지
 * 30% 더 걸렸습니다.
 *
 * 그것은 윈치가 아닙니다. 미트 훅은 자신이 맞힌 대상 *에게* 데려다 놓아야 하며, 그
 * 8미터 옆에 도착하는 것은 물리 현상이 아니라 훅이 고장 난 것으로 읽힙니다.
 *
 * 즉시 0으로 만들지 않고 초당 감쇠율로 표현한 것은 의도적입니다. 수직 성분을 그대로
 * 지워 버리면 견인이 레일이 됩니다. 플레이어가 무엇을 하고 있었든 모든 훅이 동일한 직선을
 * 따라 도달하게 되고, 쌓아 올린 운동량이 클로가 무는 순간 사라집니다. 감쇠시키면 견인
 * 초반에는 여전히 곡선을 그리므로 접근이 "끌려가는 것"으로 읽히고 빠른 진입이 정지
 * 상태와 다르게 느껴지면서도, 정작 중요한 시점에는 직선이 보장됩니다.
 *
 * 8.0에서는 0.5초 후 성분이 진입값의 약 2%까지 떨어지며, 이는 HOOK_RANGE에 가까운 경우를
 * 제외한 모든 견인보다 짧습니다. 값을 낮추면 더 넓고 운동량이 보존되는 궤적이 되고, 높이면
 * 거의 즉시 직선에 붙는 견인이 됩니다. 0이면 이전 동작과 정확히 같아집니다. */
#define HOOK_PULL_STRAIGHTEN 12.0f ///< @brief Per-second decay of velocity across the hook. 0 keeps the old curving pull. / 훅을 가로지르는 속도의 초당 감쇠율. 0이면 이전의 휘어지는 견인이 유지됩니다.

/* --- 3. the impact ---
 * Hooking a monster is an attack. The damage is deliberately modest -- this
 * is a mobility tool that happens to hurt, not a second shotgun -- and it is
 * dealt once, on arrival, not per frame of contact.
 *
 * 몬스터를 거는 것은 공격입니다. 피해량은 의도적으로 적당한 수준입니다. 이것은
 * 피해를 주기도 하는 이동 도구이지 두 번째 샷건이 아닙니다. 또한 접촉하는 매
 * 프레임이 아니라 도달 시점에 한 번만 적용됩니다. */
#define HOOK_IMPACT_DAMAGE     12  ///< @brief Damage dealt on arrival at a hooked monster. / 걸린 몬스터에 도달했을 때 입히는 피해량.

/* --- 4. the launch ---
 * Arriving bounces the player off automatically. Split into two components
 * because they do different jobs: UP gets you clear of whatever you just hit
 * so the next hook has somewhere to go, and ALONG preserves the direction you
 * were already travelling so a chain of hooks keeps its momentum.
 *
 * 도달하면 플레이어가 자동으로 튀어 오릅니다. 두 성분으로 나눈 이유는 각자 하는
 * 일이 다르기 때문입니다. UP은 방금 부딪힌 대상에서 벗어나게 하여 다음 훅이 갈 곳을
 * 확보하고, ALONG은 이미 진행 중이던 방향을 보존하여 연속된 훅이 운동량을 유지하게
 * 합니다. */
#define HOOK_LAUNCH_UP      11.0f  ///< @brief Upward speed given on arrival, m/s. / 도달 시 부여되는 상승 속도 (m/s).
#define HOOK_LAUNCH_ALONG    0.55f ///< @brief Fraction of arrival speed kept along the travel direction. / 진행 방향으로 보존되는 도달 속도의 비율.
#define HOOK_LAUNCH_MAX     30.0f  ///< @brief Ceiling on the resulting launch speed. / 최종 도약 속도의 상한.

/* --- fire rate ---
 * The claw is a launched object, not a thought, so firing it costs time the
 * same way racking the shotgun does. Two separate limits, because they stop
 * two different things:
 *
 * HOOK_COOLDOWN is the rate limit -- seconds before the launcher can fire
 * again, started on every attempt including a miss. Without it, sweeping the
 * aim across a wall fires the claw at frame rate.
 *
 * HOOK_REFIRE_DELAY is the *rearm* delay after a hook that actually
 * connected. A completed hook ends in a launch, and being able to re-fire
 * instantly at the top of that arc removes any cost from chaining -- the
 * delay is what makes a chain a decision rather than a held button.
 */
#define HOOK_COOLDOWN        0.35f  ///< @brief Seconds between launches, spent on every attempt including a miss. / 발사 간격 (초). 빗나간 경우를 포함해 모든 시도에서 소모됩니다.
#define HOOK_REFIRE_DELAY    0.55f  ///< @brief Seconds after a hook that connected. Must exceed HOOK_COOLDOWN. / 명중한 훅 이후의 재장전 시간 (초). HOOK_COOLDOWN보다 커야 합니다.

/* An overall ceiling, high enough that a normal hook never touches it -- it
   exists so a pathological case cannot fling the player off the map, not as a
   tuning dial for how fast the hook feels.
   전체 상한입니다. 정상적인 훅은 결코 여기에 도달하지 않을 만큼 높습니다. 병적인
   상황에서 플레이어가 맵 밖으로 날아가는 것을 막기 위한 장치이지, 훅의 속도감을
   조정하는 값이 아닙니다. */
#define HOOK_MAX_SPEED      45.0f   ///< @brief Overall safety ceiling on hook-induced speed. / 훅으로 인한 속도의 전체 안전 상한.

/* Where the tether visually leaves the gun, gun-local metres relative to the
   barrel's own muzzle: a separate launcher slung underneath it, purely for
   looks (see hook_muzzle() in weapon.c). Does not affect aim or the hit test
   at all -- only wp_hook_fire's use of `eye` does that. */
#define HOOK_MUZZLE_DROP     0.07f  ///< @brief Metres below the main muzzle. Purely visual. / 주 총구 아래로의 거리 (미터). 순수하게 시각적 요소입니다.
#define HOOK_MUZZLE_BACK     0.05f  ///< @brief Metres back toward the receiver. Purely visual. / 총몸 쪽으로 물러난 거리 (미터). 순수하게 시각적 요소입니다.

/* The tether's rope strip (see mb_ribbon in render.c): how wide it is drawn,
   and how many world metres one repeat of the `rope` material's twist covers.
   Smaller ROPE_TILE_LENGTH is a tighter-looking twist. */
#define ROPE_WIDTH           0.05f  ///< @brief Drawn width of the tether ribbon, metres. / 로프 리본의 그려지는 폭 (미터).
#define ROPE_TILE_LENGTH     0.6f   ///< @brief World metres per repeat of the rope texture. Smaller is a tighter twist. / 로프 텍스처 한 번 반복에 해당하는 월드 거리 (미터). 작을수록 꼬임이 촘촘해집니다.

/**
 * @brief Fires the claw. Beat 1 of the hook cycle.
 *
 * ENGLISH
 * -------
 * @param[in,out] w     Weapon whose hook is fired.
 * @param[in]     eye   Origin of the throw, the player's eye.
 * @param[in]     yaw   Aim yaw in radians.
 * @param[in]     pitch Aim pitch in radians.
 * @return 1 when the claw was launched, 0 when it was refused.
 * @retval 0 A hook is already in flight or pulling, this press has already
 *           fired, or the launcher is still on cooldown.
 * @note Launches a PROJECTILE rather than resolving a raycast here: whether
 *       it hits, and what it hits, is decided by ::wp_hook_update as the claw
 *       travels. That is what gives the throw a visible flight time, and it
 *       is why this takes no level -- nothing is traced yet.
 * @note Safe to call every frame with a held button. It refuses unless the
 *       launcher is BOTH off cooldown and rearmed, so one press is one claw:
 *       holding will not fire a second, and will not re-fire when a hook
 *       completes. ::wp_hook_arm clears that, driven from the release edge.
 * @note Spends the cooldown immediately, because the claw is away the moment
 *       the trigger goes. A throw that finds nothing still cost a throw.
 *
 * 한국어
 * ------
 * @brief 클로를 발사합니다. 훅 주기의 1단계입니다.
 * @param[in,out] w     훅을 발사할 무기.
 * @param[in]     eye   던지는 시작점인 플레이어의 눈 위치.
 * @param[in]     yaw   조준 방향 (라디안).
 * @param[in]     pitch 조준 피치 (라디안).
 * @return 클로가 발사되면 1, 거부되면 0.
 * @retval 0 이미 비행 중이거나 견인 중인 훅이 있거나, 현재 누름에서 이미 발사했거나,
 *           발사기가 아직 쿨다운 중인 경우.
 * @note 여기서 레이캐스트를 처리하지 않고 *발사체*를 발사합니다. 명중 여부와 명중
 *       대상은 클로가 날아가는 동안 ::wp_hook_update가 결정합니다. 이것이 던지는
 *       동작에 눈에 보이는 비행 시간을 부여하며, 이 함수가 레벨을 인자로 받지 않는
 *       이유이기도 합니다. 아직 아무것도 판정하지 않기 때문입니다.
 * @note 버튼을 누른 상태로 매 프레임 호출해도 안전합니다. 쿨다운이 끝나고 재장전까지
 *       완료된 경우에만 발사하므로, 한 번 누르면 클로 하나입니다. 누르고 있어도 두
 *       번째가 발사되지 않으며, 훅이 완료되어도 재발사되지 않습니다. 이를 해제하는
 *       것은 버튼을 놓는 시점에 호출되는 ::wp_hook_arm입니다.
 * @note 방아쇠를 당기는 순간 클로가 떠나므로 쿨다운을 즉시 소모합니다. 아무것도 맞히지
 *       못한 투척도 투척 비용을 치릅니다.
 */
int wp_hook_fire(Weapon *w, v3 eye, float yaw, float pitch);

/**
 * @brief Whether the hook is holding the aim still.
 *
 * ENGLISH
 * -------
 * @param[in] w Weapon to query.
 * @return Non-zero while the claw is in flight or the player is being pulled.
 * @note The aim is locked for the whole cycle, and for a reason in each half.
 *       During FLIGHT, letting the player turn would swing the tether around
 *       behind them while the claw carries on toward where they *were*
 *       aiming, which reads as the rope detaching from the gun. During the
 *       PULL, the player is not steering -- the hook is -- so leaving the
 *       camera free invites them to fight a movement they cannot influence.
 * @note Locks the aim only. Firing, walking and the weapon's own timers all
 *       continue; this is not a cutscene.
 *
 * 한국어
 * ------
 * @brief 훅이 조준을 고정하고 있는지 여부입니다.
 * @param[in] w 조회할 무기.
 * @return 클로가 비행 중이거나 플레이어가 견인되는 동안 0이 아닌 값.
 * @note 주기 전체에 걸쳐 조준이 고정되며, 각 구간마다 이유가 다릅니다. *비행* 중에
 *       플레이어가 시점을 돌리면 클로는 *조준했던* 방향으로 계속 날아가는 동안 로프가
 *       등 뒤로 휘둘리게 되어, 마치 로프가 총에서 떨어져 나간 것처럼 보입니다. *견인*
 *       중에는 플레이어가 아니라 훅이 조종하고 있으므로, 시점을 열어 두면 영향을 줄 수
 *       없는 움직임과 싸우도록 유도하는 셈이 됩니다.
 * @note 조준만 고정합니다. 사격, 걷기, 무기의 타이머는 모두 계속됩니다. 컷신이
 *       아닙니다.
 */
int wp_hook_locks_aim(const Weapon *w);

/**
 * @brief Whether something latchable is within range along the current aim.
 *
 * ENGLISH
 * -------
 * @param[in] w     Weapon, for the cooldown and latch state.
 * @param[in] l     Level to trace against. May be NULL.
 * @param[in] eye   Player's eye, the origin of the trace.
 * @param[in] yaw   Aim yaw in radians.
 * @param[in] pitch Aim pitch in radians.
 * @return 1 when a throw right now would connect, 0 otherwise.
 * @note Answers "would it connect", not merely "is a wall visible". A hook on
 *       cooldown or already latched returns 0 even with a wall dead ahead,
 *       because the honest thing for the crosshair to report is whether
 *       pressing the button would achieve anything.
 * @note Traces from the eye with the same ::HOOK_RANGE ::wp_hook_fire uses,
 *       so the indicator cannot disagree with the throw. Sharing the range
 *       constant is the point -- a separate one would drift.
 * @warning Traces the level every call, which is once per frame from the HUD.
 *          Cheap at this level size, but not free: do not call it in a loop.
 *
 * 한국어
 * ------
 * @brief 현재 조준 방향의 사거리 내에 걸 수 있는 대상이 있는지 여부입니다.
 * @param[in] w     쿨다운과 래치 상태 확인을 위한 무기.
 * @param[in] l     판정 대상 레벨. NULL이어도 됩니다.
 * @param[in] eye   판정의 시작점인 플레이어의 눈 위치.
 * @param[in] yaw   조준 방향 (라디안).
 * @param[in] pitch 조준 피치 (라디안).
 * @return 지금 발사하면 명중할 경우 1, 그렇지 않으면 0.
 * @note "벽이 보이는가"가 아니라 "명중하겠는가"에 답합니다. 쿨다운 중이거나 이미
 *       걸려 있는 훅은 정면에 벽이 있어도 0을 반환합니다. 조준점이 정직하게 알려야 할
 *       것은 지금 버튼을 눌러 무언가를 이룰 수 있는지이기 때문입니다.
 * @note ::wp_hook_fire와 동일한 ::HOOK_RANGE로 눈에서 판정하므로, 표시가 실제 발사와
 *       어긋날 수 없습니다. 사거리 상수를 공유하는 것이 핵심입니다. 별도 값을 두면
 *       서로 어긋나게 됩니다.
 * @warning 호출할 때마다 레벨을 판정하며, HUD에서 프레임당 한 번 호출됩니다. 이
 *          레벨 크기에서는 저렴하지만 공짜는 아니므로 루프 안에서 호출하지 마십시오.
 */
int wp_hook_in_range(const Weapon *w, const Pools *pl, const Level *l,
                     v3 eye, float yaw, float pitch);

/**
 * @brief Rearms the launcher; call on the button's RELEASE edge.
 *
 * ENGLISH
 * -------
 * @param[in,out] w Weapon to rearm.
 * @note Until this runs, a press that has already fired will not fire again
 *       however long the button is held.
 * @note Deliberately does NOT touch the cooldown, so tapping quickly still
 *       cannot beat the rate limit.
 *
 * 한국어
 * ------
 * @param[in,out] w 재장전할 무기.
 * @note 이 함수가 실행되기 전까지는, 이미 발사한 상태에서 버튼을 아무리 오래
 *       누르고 있어도 다시 발사되지 않습니다.
 * @note 의도적으로 쿨다운은 건드리지 않으므로, 빠르게 연타해도 발사 속도 제한을
 *       우회할 수 없습니다.
 */
void wp_hook_arm(Weapon *w);

/**
 * @brief Advances the hook one frame: flight, pull, impact and launch.
 *
 * ENGLISH
 * -------
 * @param[in,out] w   Weapon holding the hook state.
 * @param[in]     l   Level the claw collides against. May be NULL, in which
 *                    case only monsters can be hit.
 * @param[in,out] pos Player's eye position. Read, not written -- the pull
 *                    works through velocity so it collides with walls the
 *                    same way any other movement does.
 * @param[in,out] vel Player's momentum. Receives the reel-in acceleration
 *                    while pulling, and the launch impulse on arrival.
 * @param[in]     dt  Timestep in seconds.
 * @return Non-zero while the hook is still doing something (flying or
 *         pulling), 0 once it is idle.
 *
 * @note Runs all four beats. In ::HOOK_FLYING it steps the claw forward and
 *       tests for a hit; in ::HOOK_PULLING it accelerates the player toward
 *       the target, and on arrival it deals ::HOOK_IMPACT_DAMAGE to a hooked
 *       monster and applies the launch impulse.
 * @note Gravity is cancelled while pulling (::HOOK_PULL_ANTIGRAV), so a long
 *       horizontal hook does not sag into the floor before it arrives. The
 *       caller must therefore run this BEFORE ::player_move, so the
 *       cancellation lands on the same frame's gravity.
 * @note A pull that cannot finish gives up after ::HOOK_PULL_TIMEOUT --
 *       the target may be moving away or wedged where the player cannot
 *       follow. Giving up launches nothing; it is a failure, not an arrival.
 * @warning Deals damage as a side effect, so it must be called exactly once
 *          per frame. Calling it twice would hit a hooked monster twice.
 *
 * 한국어
 * ------
 * @brief 훅을 한 프레임 진행시킵니다. 비행, 견인, 충격, 도약을 모두 처리합니다.
 * @param[in,out] w   훅 상태를 보유한 무기.
 * @param[in]     l   클로가 충돌 판정할 레벨. NULL이어도 되며, 이 경우 몬스터만
 *                    맞힐 수 있습니다.
 * @param[in,out] pos 플레이어의 눈 위치. 읽기만 하고 쓰지 않습니다. 견인은 속도를
 *                    통해 작동하므로 다른 이동과 동일하게 벽과 충돌합니다.
 * @param[in,out] vel 플레이어의 운동량. 견인 중에는 감기 가속을, 도달 시에는 도약
 *                    충격량을 받습니다.
 * @param[in]     dt  시간 간격 (초).
 * @return 훅이 아직 무언가를 하고 있으면(비행 또는 견인) 0이 아닌 값, 대기 상태가
 *         되면 0.
 *
 * @note 네 단계를 모두 실행합니다. ::HOOK_FLYING에서는 클로를 전진시키며 명중을
 *       검사하고, ::HOOK_PULLING에서는 플레이어를 대상 쪽으로 가속하며, 도달 시에는
 *       걸린 몬스터에게 ::HOOK_IMPACT_DAMAGE를 입히고 도약 충격량을 적용합니다.
 * @note 견인 중에는 중력이 상쇄되므로(::HOOK_PULL_ANTIGRAV) 긴 수평 훅이 도달 전에
 *       바닥으로 처지지 않습니다. 따라서 호출자는 이 함수를 ::player_move보다 *먼저*
 *       실행하여 상쇄가 같은 프레임의 중력에 적용되도록 해야 합니다.
 * @note 완료될 수 없는 견인은 ::HOOK_PULL_TIMEOUT 후 포기합니다. 대상이 멀어지고
 *       있거나 플레이어가 따라갈 수 없는 곳에 끼어 있을 수 있습니다. 포기는 도약을
 *       발생시키지 않습니다. 도달이 아니라 실패이기 때문입니다.
 * @warning 부수 효과로 피해를 입히므로 프레임당 정확히 한 번만 호출해야 합니다. 두 번
 *          호출하면 걸린 몬스터가 두 번 피해를 입습니다.
 */
int wp_hook_update(Weapon *w, Pools *pl, const Level *l, v3 *pos, v3 *vel, float dt);

/**
 * @brief Cancels the hook immediately, wherever it is in its cycle.
 *
 * ENGLISH
 * -------
 * @param[in,out] w Weapon to cancel. Safe to call when already idle.
 * @note For focus loss, death and level changes. Applies no launch and deals
 *       no damage: a cancelled hook did not arrive.
 * @note Charges the rearm delay, so cancelling is not a way to skip the
 *       cooldown.
 *
 * 한국어
 * ------
 * @brief 주기의 어느 단계에 있든 훅을 즉시 취소합니다.
 * @param[in,out] w 취소할 무기. 이미 대기 상태여도 안전합니다.
 * @note 포커스 상실, 사망, 레벨 전환에 사용됩니다. 도약을 적용하지 않고 피해도 주지
 *       않습니다. 취소된 훅은 도달한 것이 아니기 때문입니다.
 * @note 재장전 지연을 부과하므로, 취소가 쿨다운을 건너뛰는 수단이 되지 않습니다.
 */
void wp_hook_release(Weapon *w);

#endif
