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
/* THE ONLY SPEED. This was 6.0 with Shift multiplying it by 1.8, and the dash
   was not a choice anybody made: holding Shift was strictly better everywhere
   -- it cost nothing, ran out of nothing, and traded away nothing -- so the
   real walking speed of this game was already 10.8 and the key was a tax on
   the player's left hand for it.
   Removing the modifier and keeping the number it produced is what makes this
   a deletion rather than a nerf. 6.0 x 1.8 = 10.8.
   유일한 속도입니다. 6.0에 Shift가 1.8을 곱하는 구조였고, 대쉬는 누구도 내리지 않는
   선택이었습니다. 어디서나 Shift를 누르는 편이 엄격히 나았고, 비용도 소모도 대가도
   없었습니다. 따라서 이 게임의 실제 이동 속도는 이미 10.8이었으며 그 키는 그것을 위해
   플레이어의 왼손에 매긴 세금이었습니다. 수정자를 없애고 그것이 만들어 내던 수치를
   유지하는 것이, 이것을 하향이 아니라 삭제로 만듭니다. 6.0 x 1.8 = 10.8입니다. */
#define PLAYER_WALK    10.8f   ///< @brief Walking speed, metres per second. / 걷기 속도 (초당 미터).
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
 * DEATH COLLAPSE -- the camera falling to the floor when the player dies.
 *
 * ENGLISH
 * -------
 * Three things move, all driven by the same eased clock so they land together
 * rather than finishing at three different moments:
 *
 *   DROP  the eye sinks from standing height to just above the floor. This is
 *         the part that actually reads as dying -- the world grows past you.
 *   ROLL  the horizon tilts. A drop on its own reads as crouching; the tilt is
 *         what says the body is no longer holding itself up.
 *   PITCH the view falls toward the floor, but only part way. Looking straight
 *         down at the end fills the screen with floor texture and hides the
 *         thing that killed you, which is the one thing a player wants to see.
 *
 * Eased out rather than linear: a body drops fastest at the start and settles.
 * Linear motion here reads as a lift descending.
 *
 * Applied to the CAMERA, never to Player::pos. Sinking the real position would
 * put the eye inside the floor, where level_trace reports an immediate hit at
 * zero range.
 *
 * Kept here rather than in main.c because tools/dithershot.c previews the pose
 * and must use these values rather than a copy -- a preview that has drifted
 * from the thing it previews is worse than no preview.
 *
 * 한국어
 * ------
 * 사망 시 카메라가 바닥으로 쓰러지는 연출입니다.
 *
 * 세 가지가 움직이며, 서로 다른 세 시점에 끝나지 않고 함께 안착하도록 전부 동일한
 * 이징 시계로 구동됩니다.
 *
 *   DROP  눈이 선 높이에서 바닥 바로 위까지 내려앉습니다. 실제로 죽는 것으로 읽히는
 *         부분이며, 월드가 당신을 지나쳐 커집니다.
 *   ROLL  수평선이 기울어집니다. 내려앉기만 하면 웅크리는 것으로 읽히고, 기울어짐이
 *         몸이 더 이상 스스로를 지탱하지 못한다고 말합니다.
 *   PITCH 시선이 바닥을 향해 떨어지되 끝까지 가지는 않습니다. 마지막에 수직으로
 *         내려다보면 화면이 바닥 텍스처로 가득 차 당신을 죽인 것을 가리는데, 그것이야말로
 *         플레이어가 가장 보고 싶어 하는 것입니다.
 *
 * 선형이 아니라 감속 이징입니다. 몸은 처음에 가장 빠르게 떨어졌다가 가라앉습니다.
 * 이곳의 선형 운동은 엘리베이터가 내려가는 것처럼 읽힙니다.
 *
 * Player::pos가 아니라 *카메라*에 적용합니다. 실제 위치를 내리면 눈이 바닥 안으로
 * 들어가고, 그곳에서 level_trace는 거리 0에서 즉시 충돌을 보고합니다.
 *
 * main.c가 아니라 이곳에 두는 이유는 tools/dithershot.c가 이 자세를 미리보기 때문이며,
 * 사본이 아니라 이 값을 사용해야 합니다. 미리보기 대상과 어긋난 미리보기는 없느니만
 * 못합니다.
 * ==========================================================================
 */
#define DEATH_ANIM_TIME  0.7f   ///< @brief Seconds the collapse takes. / 쓰러짐에 걸리는 시간 (초).
#define DEATH_DROP       1.15f  ///< @brief Metres the eye sinks. Just under PLAYER_EYE, so it stops above the floor. / 눈이 내려앉는 거리 (미터). PLAYER_EYE보다 약간 작아 바닥 위에서 멈춥니다.
#define DEATH_ROLL       0.62f  ///< @brief Radians the horizon tilts -- about 35 degrees. / 수평선이 기울어지는 각도 (라디안). 약 35도입니다.
#define DEATH_PITCH      0.30f  ///< @brief Radians the view falls, well short of straight down. / 시선이 떨어지는 각도 (라디안). 수직 아래에는 한참 못 미칩니다.

/**
 * @brief Seconds the death screen ignores input for.
 *
 * ENGLISH
 * -------
 * The trigger that killed you is usually still held. Without this the death
 * screen appears and vanishes inside one frame, which reads as the game not
 * having one.
 *
 * @note Beside the collapse it follows, rather than in main.c where it began.
 *       Its two readers no longer share a file: ::wnd_proc turns a keypress
 *       into a restart, and the death overlay -- now drawn by ::scene_frame --
 *       tells the player the key will work. A constant read from two files is
 *       a constant that belongs to neither, and the delay is a property of the
 *       death sequence rather than of the window that happens to poll it.
 *
 * 한국어
 * ------
 * @brief 사망 화면이 입력을 무시하는 시간(초)입니다.
 *
 * 당신을 죽인 방아쇠는 대개 아직 눌린 상태입니다. 이것이 없으면 사망 화면이 한 프레임
 * 안에 나타났다 사라지며, 게임에 사망 화면이 없는 것처럼 보입니다.
 *
 * @note 처음 있던 main.c가 아니라, 뒤따르는 쓰러짐 연출 곁에 둡니다. 이 값을 읽는 두 곳은
 *       더 이상 한 파일에 있지 않습니다. ::wnd_proc은 키 입력을 재시작으로 바꾸고, 이제
 *       ::scene_frame이 그리는 사망 오버레이는 키가 동작한다고 알립니다. 두 파일이 읽는
 *       상수는 어느 쪽에도 속하지 않는 상수이며, 이 지연은 마침 그것을 폴링하는 창이
 *       아니라 사망 연출의 성질입니다.
 */
#define DEATH_INPUT_DELAY 0.8f

/* The eye must stop ABOVE the floor. A drop of PLAYER_EYE or more puts it at
   or below floor level, where open_at() reports solid and level_trace hits at
   zero range -- the camera would end the animation inside the ground.
   눈은 바닥 *위에서* 멈춰야 합니다. PLAYER_EYE 이상 떨어지면 바닥 높이이거나 그 아래가
   되는데, 그곳에서 open_at()은 막힌 것으로 보고하고 level_trace는 거리 0에서 충돌합니다.
   카메라가 애니메이션을 땅속에서 끝내게 됩니다. */
_Static_assert(DEATH_DROP < PLAYER_EYE,
               "the death camera must settle above the floor, not inside it");

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

    /**
     * @brief Keycards held, a KEY_* mask from level.h.
     *
     * On the player rather than in the level, because it is something the
     * player carries between rooms and across a level transition. A level
     * reload must not take a key away, and it cannot if the key was never
     * stored in the level.
     *
     * @brief 보유한 키카드입니다. level.h의 KEY_* 마스크입니다.
     * @note 레벨이 아니라 플레이어에 둡니다. 방 사이와 레벨 전환을 넘어 플레이어가
     *       지니고 다니는 것이기 때문입니다. 레벨을 다시 읽는 것이 열쇠를 빼앗아서는
     *       안 되며, 열쇠가 레벨에 저장된 적이 없다면 그럴 수 없습니다.
     */
    int   keys;
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
