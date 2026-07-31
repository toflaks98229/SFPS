/**
 * @file player.h
 * @brief Player movement against a sector level.
 *
 * ENGLISH
 * -------
 * Pulled out of main.c so it can be exercised without a window: a movement
 * bug is far easier to pin down by stepping a simulation than by walking
 * around and squinting. See tools/movetest.c.
 *
 * 한국어
 * ------
 * 창 없이도 테스트할 수 있도록 main.c에서 분리했습니다. 이동 버그는 직접 걸어
 * 다니며 눈으로 확인하는 것보다 시뮬레이션을 단계별로 실행하는 편이 훨씬
 * 찾기 쉽기 때문입니다. tools/movetest.c를 참조하십시오.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "level.h"

/* --- Body and movement constants / 신체 및 이동 상수 --- */

#define PLAYER_EYE     1.70f   ///< @brief Eye height above the feet, metres. / 발에서 눈까지의 높이 (미터).
#define PLAYER_RADIUS  0.35f   ///< @brief Collision cylinder radius, metres. / 충돌 원기둥의 반경 (미터).
#define PLAYER_WALK    6.0f    ///< @brief Base walking speed, metres per second. / 기본 걷기 속도 (초당 미터).
#define PLAYER_GRAVITY 22.0f   ///< @brief Downward acceleration, m/s^2. Higher than reality for a snappier fall. / 하강 가속도 (m/s^2). 경쾌한 낙하감을 위해 현실보다 높게 설정되었습니다.
#define PLAYER_JUMP    7.5f    ///< @brief Upward velocity applied on jumping, m/s. / 점프 시 적용되는 상승 속도 (m/s).

/**
 * @brief How high a ledge the player walks straight up instead of being stopped by.
 *
 * ENGLISH
 * -------
 * @brief How high a ledge the player walks straight up instead of being stopped by.
 * @note Quake allows 18 units against a 56-unit player, so a third of eye
 *       height is the proportion that feels right; anything less and small
 *       trim geometry starts catching your feet.
 *
 * 한국어
 * ------
 * @brief 플레이어가 가로막히지 않고 그대로 걸어 올라가는 턱의 높이입니다.
 * @note Quake는 56 유닛 크기의 플레이어에 대해 18 유닛을 허용합니다. 따라서 눈
 *       높이의 3분의 1이 적절한 비율입니다. 이보다 낮으면 작은 장식용 지오메트리에
 *       발이 걸리기 시작합니다.
 */
#define PLAYER_STEP    (PLAYER_EYE / 3.0f)

#define PLAYER_MAX_HP  100     ///< @brief Health at spawn and the cap pickups top up to. / 스폰 시 체력이자 아이템으로 회복 가능한 상한값.

/* ==========================================================================
 * MOMENTUM TUNING -- how fast vel.x/z (see below) bleeds off, regardless of
 * what put it there. The forces that ADD to it -- the grapple's pull, the
 * shotgun's recoil kick -- are tuned separately in weapon.h; this is the
 * other half of that same feel and worth reading alongside it.
 *
 * Ground drag is hard so a recoil kick does not leave you sliding across the
 * floor in ordinary combat. Air drag is deliberately close to nothing: the
 * entire point of the hook and recoil jumping is a fast, continuous move
 * that should not need fighting to keep going, so nothing here should be
 * quietly braking it. Gravity is not part of this -- it is a real force, not
 * friction, and stays in player_move regardless of this dial.
 *
 * 운동량 튜닝 -- 무엇이 운동량을 발생시켰는지와 무관하게, vel.x/z(아래 참조)가
 * 얼마나 빨리 소멸하는지를 결정합니다. 운동량을 더하는 힘(그래플의 당김,
 * 샷건의 반동)은 weapon.h에서 별도로 조정합니다. 이 값은 동일한 감각의 나머지
 * 절반이므로 함께 읽어 볼 가치가 있습니다.
 *
 * 지상 저항은 강하게 설정되어, 일반 전투 중 반동으로 인해 바닥을 미끄러지지
 * 않도록 합니다. 공중 저항은 의도적으로 거의 0에 가깝습니다. 훅과 반동 점프의
 * 핵심은 유지하기 위해 애쓸 필요가 없는 빠르고 연속적인 이동이므로, 여기서
 * 조용히 제동을 걸어서는 안 됩니다. 중력은 이 항목에 포함되지 않습니다. 중력은
 * 마찰이 아닌 실제 힘이며, 이 값과 무관하게 player_move에 남아 있습니다.
 * ==========================================================================
 */
#define MOMENTUM_DRAG_GROUND  6.0f    ///< @brief Per second; a kick settles in well under half a second. / 초당 값. 반동이 0.5초 이내에 충분히 가라앉습니다.
#define MOMENTUM_DRAG_AIR     0.06f   ///< @brief Per second; airborne, speed is kept almost in full. / 초당 값. 공중에서는 속도가 거의 온전히 유지됩니다.

/* --- Type definitions / 타입 정의 --- */

/**
 * @struct Player
 * @brief The player's full simulation state.
 *
 * ENGLISH
 * -------
 * @brief The player's full simulation state.
 * @note `pos` is the EYE position, not the feet; subtract ::PLAYER_EYE to
 *       reach floor level. Mixing the two is the most common source of
 *       one-off collision bugs in this codebase.
 *
 * 한국어
 * ------
 * @brief 플레이어의 전체 시뮬레이션 상태입니다.
 * @note `pos`는 발이 아닌 눈의 위치입니다. 바닥 높이를 구하려면 ::PLAYER_EYE를
 *       빼십시오. 이 둘을 혼동하는 것이 이 코드베이스에서 발생하는 미묘한 충돌
 *       버그의 가장 흔한 원인입니다.
 */
typedef struct {
    v3    pos;          /**< Eye position in world space. / 월드 공간에서의 눈 위치. */
    /* Full 3D velocity, not just vertical. vel.y is gravity/jump as it always
       was; vel.x/z is momentum from external forces -- a grapple's pull, a
       shotgun's recoil kick -- layered on top of the direct WASD movement
       below rather than driving it. Normal walking never touches vel.x/z, so
       nothing here changes how walking alone feels.

       수직 성분만이 아닌 완전한 3D 속도입니다. vel.y는 기존과 같이 중력/점프를
       담당하며, vel.x/z는 외부 힘(그래플의 당김, 샷건의 반동)에서 비롯된
       운동량으로, 아래의 직접적인 WASD 이동을 대체하는 것이 아니라 그 위에
       덧씌워집니다. 일반적인 걷기는 vel.x/z를 건드리지 않으므로, 걷기 자체의
       감각은 이로 인해 달라지지 않습니다. */
    v3    vel;          /**< Velocity: y is gravity/jump, x/z is external momentum. / 속도. y는 중력/점프, x/z는 외부 운동량입니다. */
    int   grounded;     /**< Non-zero while standing on a floor. Recomputed every player_move. / 바닥에 서 있으면 0이 아닙니다. player_move마다 재계산됩니다. */
    int   health;       /**< 0 = dead; set by player_spawn, drained by monsters. / 0이면 사망. player_spawn이 설정하고 몬스터가 감소시킵니다. */
    float hurt;         /**< Red screen flash on taking a hit, decays to 0. / 피격 시 붉은 화면 점멸 효과. 0으로 감쇠합니다. */
} Player;

/* --- Public function prototypes / 공개 함수 프로토타입 --- */

/**
 * @brief Places the player at the level's start point.
 *
 * ENGLISH
 * -------
 * @brief Places the player at the level's start point.
 * @param[out] p Player to initialise. Position, velocity, health and hurt
 *               are all reset.
 * @param[in]  l Level supplying the start point and floor height.
 * @return The spawn yaw in radians, which the caller should apply to its camera.
 * @note Snaps the player down onto the floor beneath the start point when one
 *       exists, so a start marker placed at any height still lands correctly.
 *
 * 한국어
 * ------
 * @brief 플레이어를 레벨의 시작 지점에 배치합니다.
 * @param[out] p 초기화할 플레이어. 위치, 속도, 체력, 피격 효과가 모두
 *               재설정됩니다.
 * @param[in]  l 시작 지점과 바닥 높이를 제공하는 레벨.
 * @return 라디안 단위의 스폰 방향(yaw). 호출자는 이 값을 카메라에 적용해야 합니다.
 * @note 시작 지점 아래에 바닥이 존재하면 플레이어를 그 위로 붙여 놓으므로,
 *       시작 표식이 어떤 높이에 배치되어 있어도 올바르게 안착합니다.
 */
float player_spawn(Player *p, const Level *l);

/**
 * @brief Advances the player one frame.
 *
 * ENGLISH
 * -------
 * @brief Advances the player one frame.
 * @param[in,out] p     Player to move.
 * @param[in]     l     Level to collide against.
 * @param[in]     wish  Desired horizontal direction; need not be normalised,
 *                      and a zero vector simply means "no input".
 * @param[in]     speed Movement speed in metres per second.
 * @param[in]     jump  Non-zero to jump, which only takes effect while grounded.
 * @param[in]     dt    Timestep in seconds.
 * @note Resolves the vertical axis before the horizontal one. The other order
 *       lets the frame in which you drop onto a platform see you still below
 *       its floor, and the horizontal pass then refuses the move.
 * @note Direct walking SETS position, while `p->vel.x/z` momentum is
 *       integrated separately and decays. A caller that needs momentum to
 *       dominate -- swinging on the grapple -- must reduce `speed`, because
 *       position-setting cannot be overridden by any force.
 * @warning Assumes a fixed-ish `dt`. A very large timestep can tunnel the
 *          player through thin geometry, since collision is resolved by
 *          sampling the destination rather than sweeping the path.
 *
 * 한국어
 * ------
 * @brief 플레이어를 한 프레임 진행시킵니다.
 * @param[in,out] p     이동시킬 플레이어.
 * @param[in]     l     충돌 판정 대상 레벨.
 * @param[in]     wish  원하는 수평 이동 방향. 정규화할 필요는 없으며, 영벡터는
 *                      단순히 "입력 없음"을 의미합니다.
 * @param[in]     speed 이동 속도 (초당 미터).
 * @param[in]     jump  0이 아니면 점프하며, 지면에 있을 때만 적용됩니다.
 * @param[in]     dt    시간 간격 (초).
 * @note 수평축보다 수직축을 먼저 처리합니다. 순서가 반대이면 플랫폼에 착지하는
 *       프레임에서 아직 바닥보다 아래에 있는 것으로 판정되어, 수평 처리가 이동을
 *       거부하게 됩니다.
 * @note 직접적인 걷기는 위치를 직접 설정하는 반면, `p->vel.x/z` 운동량은 별도로
 *       적분되며 감쇠합니다. 운동량이 우선되어야 하는 호출자(그래플 스윙 등)는
 *       반드시 `speed`를 낮춰야 합니다. 위치를 직접 설정하는 방식은 어떤 힘으로도
 *       덮어쓸 수 없기 때문입니다.
 * @warning `dt`가 대체로 일정하다고 가정합니다. 충돌은 경로를 훑는 방식이 아니라
 *          목적지를 검사하는 방식으로 처리되므로, 시간 간격이 매우 크면 플레이어가
 *          얇은 지오메트리를 통과해 버릴 수 있습니다.
 */
void player_move(Player *p, const Level *l, v3 wish, float speed,
                 int jump, float dt);

/**
 * @brief Adds directly to the player's momentum -- a recoil kick, a grapple's pull.
 *
 * ENGLISH
 * -------
 * @brief Adds directly to the player's momentum -- a recoil kick, a grapple's pull.
 * @param[in,out] p       Player to push.
 * @param[in]     impulse Velocity to add, in metres per second.
 * @note Takes effect the next time ::player_move runs; it collides with walls
 *       and decays the same way any other momentum does, just like a jump pad
 *       or an explosion would in a game that had them.
 *
 * 한국어
 * ------
 * @brief 플레이어의 운동량에 직접 더합니다. 반동이나 그래플의 당김 등에 해당합니다.
 * @param[in,out] p       힘을 가할 플레이어.
 * @param[in]     impulse 더할 속도 (초당 미터).
 * @note 다음 ::player_move 실행 시점에 적용됩니다. 다른 운동량과 마찬가지로 벽과
 *       충돌하고 감쇠하며, 이는 점프대나 폭발이 있는 게임에서의 동작 방식과
 *       동일합니다.
 */
void player_impulse(Player *p, v3 impulse);

#endif
