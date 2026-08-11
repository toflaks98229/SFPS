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

#include "level.h"

/* --- Ammunition constants / 탄약 상수 --- */

#define WEAPON_START_AMMO 20    ///< @brief Shells you spawn with. / 스폰 시 보유하는 탄환 수.
#define WEAPON_MAX_AMMO   50    ///< @brief What a belt holds -- pickups stop here. / 탄띠의 최대 수용량. 아이템 획득도 여기서 멈춥니다.

/* --- View model placement / 뷰 모델 배치 --- */

/**
 * @struct GunPose
 * @brief Placement and projection of the first-person view model.
 *
 * ENGLISH
 * -------
 * @brief Placement and projection of the first-person view model.
 * @note These are runtime values rather than #defines so tools/modelview.exe
 *       can drive the *same* transform the game uses while you nudge it -- a
 *       copy of the maths inside the tool would drift from the game the first
 *       time either side was edited.
 *
 * 한국어
 * ------
 * @brief 1인칭 뷰 모델의 배치와 투영 정보입니다.
 * @note #define이 아닌 런타임 값인 이유는, tools/modelview.exe가 위치를 조정하는
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
#define HOOK_RANGE          40.0f  ///< @brief Metres the claw can reach before giving up. / 클로가 포기하기 전까지 도달할 수 있는 거리 (미터).
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
 * At 8.0 the component is down to ~2% of its entry value after half a second,
 * which is shorter than every pull that is not nearly at HOOK_RANGE. Lower it
 * for wider, more preserved arcs; raise it for a pull that snaps to the line
 * almost at once. Zero restores the old behaviour exactly.
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
#define HOOK_PULL_STRAIGHTEN 8.0f  ///< @brief Per-second decay of velocity across the hook. 0 keeps the old curving pull. / 훅을 가로지르는 속도의 초당 감쇠율. 0이면 이전의 휘어지는 견인이 유지됩니다.

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

/* --- recoil jumping ---
 * Every shot kicks the player back along -aim. Small on the ground, or
 * ordinary combat would shove you around by surprise; a real shove in the
 * air, which is what makes shotgun-jumping and mid-swing redirects off the
 * grapple work. Raise RECOIL_MOVE_AIR for punchier jumps; raise _GROUND if
 * ground combat should feel more physical at the cost of some stability. */
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
    int   ammo;          /**< Shells; one per trigger pull, refilled by pickups. / 탄환. 방아쇠를 당길 때마다 하나씩 소모되며 아이템으로 보충됩니다. */
    float cooldown;      /**< Seconds until the next shot is allowed. / 다음 사격이 허용되기까지의 시간 (초). */
    float recoil;        /**< Extra camera pitch in radians, springs back to 0. / 카메라에 추가되는 피치 (라디안). 0으로 복원됩니다. */
    float punch;         /**< View model kickback in metres, springs back to 0. / 뷰 모델의 후퇴 거리 (미터). 0으로 복원됩니다. */
    float flash;         /**< Muzzle flash timer. / 총구 화염 타이머. */
    float bob_phase;     /**< Walk cycle accumulator. / 걷기 주기 누적값. */
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
} Weapon;

/* --- Public function prototypes: lifecycle / 공개 함수 프로토타입: 수명 주기 --- */

/**
 * @brief Builds the gun mesh and materials, and records the level shots trace against.
 *
 * ENGLISH
 * -------
 * @param[out] w     Weapon to initialise. Fully reset, including ammo.
 * @param[in]  level Level that shots and the hook are traced against.
 * @warning `level` must outlive the weapon: only the pointer is stored.
 * @warning Touches GL, unlike the wp_hook_* family. Requires a current
 *          context, which is why headless tests drive the hook functions
 *          directly instead of calling this.
 *
 * 한국어
 * ------
 * @param[out] w     초기화할 무기. 탄약을 포함하여 완전히 재설정됩니다.
 * @param[in]  level 사격과 훅의 판정 대상이 되는 레벨.
 * @warning `level`은 무기보다 오래 유지되어야 합니다. 포인터만 저장되기 때문입니다.
 * @warning wp_hook_* 계열과 달리 GL을 사용합니다. 활성 컨텍스트가 필요하며,
 *          헤드리스 테스트가 이 함수를 호출하지 않고 훅 함수를 직접 구동하는
 *          이유이기도 합니다.
 */
void wp_init(Weapon *w, const Level *level);

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
void wp_update(Weapon *w, float dt, int firing, v3 eye, float yaw, float pitch,
               float move_speed, float mouse_dx, float mouse_dy,
               float world_fov, float aspect, v3 *player_vel, int player_grounded);

/* --- Public function prototypes: the grapple hook / 공개 함수 프로토타입: 그래플 훅 --- */

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
int wp_hook_in_range(const Weapon *w, const Level *l,
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
int wp_hook_update(Weapon *w, const Level *l, v3 *pos, v3 *vel, float dt);

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

/* --- Public function prototypes: rendering / 공개 함수 프로토타입: 렌더링 --- */

/**
 * @brief Draws world-space effects: bullet holes, impact sparks, tracers and the tether.
 *
 * ENGLISH
 * -------
 * @param[in] w         Weapon whose effects are drawn.
 * @param[in] view_proj Combined view-projection matrix.
 * @param[in] cam_pos   Camera position, used to billboard sparks and to
 *                      orient the tether ribbon toward the viewer.
 * @param[in] cam_right Camera right basis vector.
 * @param[in] cam_up    Camera up basis vector.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @param[in] w         효과를 그릴 대상 무기.
 * @param[in] view_proj 뷰-투영 결합 행렬.
 * @param[in] cam_pos   카메라 위치. 스파크를 빌보드 처리하고 로프 리본을 시점
 *                      방향으로 정렬하는 데 사용됩니다.
 * @param[in] cam_right 카메라의 우측 기저 벡터.
 * @param[in] cam_up    카메라의 상향 기저 벡터.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
void wp_draw_world(const Weapon *w, mat4 view_proj,
                   v3 cam_pos, v3 cam_right, v3 cam_up);

/**
 * @brief Draws the gun itself over a cleared depth buffer so it never clips walls.
 *
 * ENGLISH
 * -------
 * @param[in] w      Weapon to draw.
 * @param[in] aspect Viewport aspect ratio.
 * @warning Clears the depth buffer as a side effect; call after all
 *          world geometry has been drawn.
 *
 * 한국어
 * ------
 * @param[in] w      그릴 무기.
 * @param[in] aspect 뷰포트 종횡비.
 * @warning 부수 효과로 깊이 버퍼를 지웁니다. 모든 월드 지오메트리를 그린 뒤에
 *          호출하십시오.
 */
void wp_draw_view(const Weapon *w, float aspect);

/**
 * @brief Draws the crosshair, sized to the current spread.
 *
 * ENGLISH
 * -------
 * @param[in] w      Weapon whose spread sets the crosshair size.
 * @param[in] aspect Viewport aspect ratio, keeping the crosshair square.
 *
 * 한국어
 * ------
 * @param[in] w      조준점 크기를 결정하는 산포도를 가진 무기.
 * @param[in] aspect 뷰포트 종횡비. 조준점을 정사각형으로 유지합니다.
 */
/**
 * @param[in] hook_ready Non-zero to draw the hook's range brackets around the
 *                       crosshair. Pass the result of ::wp_hook_in_range.
 * @note Passed in rather than queried here so this stays a pure draw call:
 *       the range test traces the level, and a draw that silently does
 *       collision work is a surprise waiting to happen.
 * @param[in] hook_ready 0이 아니면 조준점 주위에 훅의 사거리 괄호를 그립니다.
 *                       ::wp_hook_in_range의 결과를 전달하십시오.
 * @note 이 함수를 순수한 그리기로 유지하기 위해 내부에서 조회하지 않고 인자로 받습니다.
 *       사거리 판정은 레벨을 탐색하며, 그리기 호출이 조용히 충돌 연산을 수행하는 것은
 *       언젠가 문제가 될 놀라움입니다.
 */
void wp_draw_hud(const Weapon *w, float aspect, int hook_ready);

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

/* --- Public function prototypes: asset reloading / 공개 함수 프로토타입: 에셋 재로드 --- */

/**
 * @brief Uploads the named model as the gun mesh.
 *
 * ENGLISH
 * -------
 * @param[in] name Model name to look up in the baked model text.
 * @note Called by ::wp_init; exposed so the preview tool can hot-swap models
 *       and so hot reload can rebuild it.
 * @warning Requires a current GL context.
 *
 * 한국어
 * ------
 * @param[in] name 내장된 모델 텍스트에서 찾을 모델 이름.
 * @note ::wp_init이 호출합니다. 프리뷰 도구가 모델을 즉시 교체할 수 있도록,
 *       그리고 핫 리로드가 이를 재생성할 수 있도록 외부에 공개되어 있습니다.
 * @warning 활성 GL 컨텍스트가 필요합니다.
 */
void wp_set_model(const char *name);

/**
 * @brief Regenerates the gun texture from the current recipe text.
 *
 * ENGLISH
 * -------
 * @warning Requires a current GL context. Replaces the existing texture, so
 *          any cached binding of it becomes stale.
 *
 * 한국어
 * ------
 * @warning 활성 GL 컨텍스트가 필요합니다. 기존 텍스처를 교체하므로, 캐시된
 *          바인딩은 무효가 됩니다.
 */
void wp_reload_texture(void);

#endif
