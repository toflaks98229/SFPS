/**
 * @file player.c
 * @brief Implements player movement, gravity and collision against a sector level.
 *
 * ENGLISH
 * -------
 * Contains no GL code, so the whole movement model can be stepped headlessly
 * by tools/movetest.c. Collision follows Doom's approach: rather than
 * intersecting the player against wall segments, every move asks the simpler
 * question "can the player stand here?" -- which, with overlapping sectors,
 * is the only question that needs asking.
 *
 * 한국어
 * ------
 * GL 코드를 포함하지 않으므로, 전체 이동 모델을 tools/movetest.c로 창 없이
 * 단계별 실행할 수 있습니다. 충돌 처리는 Doom의 방식을 따릅니다. 플레이어를 벽
 * 선분과 교차 판정하는 대신, 모든 이동에서 "플레이어가 여기 설 수 있는가?"라는
 * 더 단순한 질문을 던집니다. 섹터가 겹치는 구조에서는 이 질문 하나면 충분합니다.
 */

#include "player.h"

/* --- File-local macros and constants / 파일 지역 매크로 및 상수 --- */

/**
 * @brief Clearance kept between a contact position and the surface it rests on.
 *
 * ENGLISH
 * -------
 * @brief Clearance kept between a contact position and the surface it rests on.
 * @note Feet landed exactly on a ledge do not survive the round trip through
 *       `pos.y = floor + PLAYER_EYE` and back through `pos.y - PLAYER_EYE`:
 *       the result can come out a hair below, and every ground test then
 *       disagrees with the one before it.
 *
 * 한국어
 * ------
 * @brief 접촉 위치와 그것이 놓인 표면 사이에 유지되는 여유 간격입니다.
 * @note 턱에 정확히 착지한 발은 `pos.y = floor + PLAYER_EYE`로 갔다가
 *       `pos.y - PLAYER_EYE`로 돌아오는 왕복 연산을 견디지 못합니다. 결과가
 *       아주 미세하게 아래로 내려갈 수 있으며, 그러면 매 지면 판정이 직전
 *       판정과 어긋나게 됩니다.
 */
#define SKIN 0.002f

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static int  can_stand(const Level *l, float x, float z, float feet,
                      float *out_floor);
static void move_axis(Player *p, const Level *l, int axis, float delta);

/* --- Public function definitions / 공개 함수 정의 --- */

float player_spawn(Player *p, const Level *l) {
    /* Level coordinates are stored in centimetres as integers.
       레벨 좌표는 센티미터 단위의 정수로 저장되어 있습니다. */
    p->pos = v3f(l->start[0] * 0.01f, PLAYER_EYE, l->start[1] * 0.01f);
    p->vel = v3f(0.0f, 0.0f, 0.0f);
    p->grounded = 0;
    p->health = PLAYER_MAX_HP;
    p->hurt = 0.0f;

    /* Unlimited search distance and step height: the start marker's own
       height is irrelevant, only the floor beneath it matters.
       탐색 거리와 단차 높이를 무제한으로 둡니다. 시작 표식 자체의 높이는 중요하지
       않고, 그 아래의 바닥만이 의미를 갖기 때문입니다. */
    float f, c;
    if (level_ground(l, p->pos.x, p->pos.z, 1e9f, 1e9f, &f, &c))
        p->pos.y = f + PLAYER_EYE + SKIN;

    return l->start[2] * 0.0000174533f;   /* millidegrees -> radians */
}

/* MOMENTUM_DRAG_GROUND/AIR live in player.h now, next to the tuning banner
   that cross-references weapon.h's hook/recoil constants -- the three
   numbers are one dial split across two files by necessity, not by choice,
   and are worth reading together. */

void player_move(Player *p, const Level *l, v3 wish, float speed,
                 int jump, float dt) {
    /* Jump before gravity, so the upward velocity gets a full frame of
       travel before being pulled back.
       중력보다 점프를 먼저 처리하여, 상승 속도가 되돌아가기 전에 온전한 한
       프레임만큼 이동할 수 있게 합니다. */
    if (p->grounded && jump) {
        p->vel.y = PLAYER_JUMP;
        p->grounded = 0;
    }
    p->vel.y -= PLAYER_GRAVITY * dt;

    /* Vertical resolves BEFORE horizontal. The other order lets the frame in
       which you drop onto a platform see you still below its floor, and the
       horizontal pass then refuses the move -- you get stuck against thin
       air, or shoved, depending on how the pushout is written. */
    p->pos.y += p->vel.y * dt;
    /* Cleared here and re-established below: grounded is derived from the
       actual floor every frame, never remembered.
       여기서 초기화한 뒤 아래에서 다시 설정합니다. 접지 여부는 기억하는 값이
       아니라 매 프레임 실제 바닥으로부터 유도되는 값입니다. */
    p->grounded = 0;

    float f, c;
    if (level_ground(l, p->pos.x, p->pos.z, p->pos.y - PLAYER_EYE,
                     1e9f, &f, &c)) {
        /* Landed: settle onto the floor and cancel the downward velocity.
           착지했습니다. 바닥에 안착시키고 하강 속도를 제거합니다. */
        if (p->pos.y - PLAYER_EYE <= f) {
            p->pos.y = f + PLAYER_EYE + SKIN;
            p->vel.y = 0.0f;
            p->grounded = 1;
        }
        /* Head hit the ceiling: stop rising, but let a downward velocity
           through so the player is not pinned there.
           머리가 천장에 닿았습니다. 상승은 멈추되 하강 속도는 통과시켜 플레이어가
           그 자리에 고정되지 않도록 합니다. */
        if (p->pos.y > c - 0.05f) {
            p->pos.y = c - 0.05f;
            if (p->vel.y > 0.0f) p->vel.y = 0.0f;
        }
    }

    /* Axes move independently, which is what produces wall sliding: blocking
       X while leaving Z free lets the player slide along the surface.
       각 축은 독립적으로 이동하며, 이것이 벽을 따라 미끄러지는 효과를 만듭니다.
       X를 막고 Z를 열어 두면 플레이어가 표면을 따라 미끄러집니다. */
    v3 dir = v3norm(wish);
    move_axis(p, l, 0, dir.x * speed * dt);
    move_axis(p, l, 2, dir.z * speed * dt);

    /* External momentum rides on top of the walk above rather than replacing
       it: it moves through the same wall collision, then decays on its own.
       Nothing feeds vel.x/z under normal walking, so this is a no-op until
       something (the grapple, a recoil kick) actually adds to it. */
    move_axis(p, l, 0, p->vel.x * dt);
    move_axis(p, l, 2, p->vel.z * dt);
    float drag = p->grounded ? MOMENTUM_DRAG_GROUND : MOMENTUM_DRAG_AIR;
    /* Exponential decay, framerate-independent: halving dt and stepping twice
       produces the same result as one full step.
       프레임률에 독립적인 지수 감쇠입니다. dt를 절반으로 줄여 두 번 실행해도
       한 번에 실행한 것과 동일한 결과가 나옵니다. */
    float decay = 1.0f - expf(-drag * dt);
    p->vel.x -= p->vel.x * decay;
    p->vel.z -= p->vel.z * decay;
}

void player_impulse(Player *p, v3 impulse) {
    p->vel = v3add(p->vel, impulse);
}

/* --- Static helper function definitions / 정적 헬퍼 함수 정의 --- */

/**
 * @brief Tests whether the player can stand with their feet at a position.
 *
 * ENGLISH
 * -------
 * @brief Tests whether the player can stand with their feet at a position.
 * @param[in]  l         Level to test against.
 * @param[in]  x         Candidate X coordinate.
 * @param[in]  z         Candidate Z coordinate.
 * @param[in]  feet      Current feet height, used to judge step height.
 * @param[out] out_floor Receives the resulting floor height on success;
 *                       untouched on failure.
 * @return 1 when the position is standable, 0 otherwise.
 * @note Doom's P_TryMove asks this question rather than intersecting the
 *       player against wall segments, and with overlapping sectors it is the
 *       only thing that needs asking: being outside every sector, facing too
 *       high a step and having no headroom are all the same "no".
 * @note Five samples are taken -- the centre and four points on the radius --
 *       because the player is a circle. A single centre test would let you
 *       cut corners and clip through the join between two sectors.
 *
 * 한국어
 * ------
 * @brief 플레이어가 해당 위치에 발을 딛고 설 수 있는지 검사합니다.
 * @param[in]  l         판정 대상 레벨.
 * @param[in]  x         검사할 X 좌표.
 * @param[in]  z         검사할 Z 좌표.
 * @param[in]  feet      현재 발 높이. 단차 높이를 판정하는 데 사용됩니다.
 * @param[out] out_floor 성공 시 결정된 바닥 높이를 받습니다. 실패 시에는 변경되지
 *                       않습니다.
 * @return 설 수 있는 위치이면 1, 그렇지 않으면 0.
 * @note Doom의 P_TryMove는 플레이어를 벽 선분과 교차 판정하는 대신 이 질문을
 *       던집니다. 섹터가 겹치는 구조에서는 이것만 물으면 충분합니다. 모든 섹터
 *       바깥에 있는 경우, 너무 높은 단차를 마주한 경우, 머리 위 공간이 없는 경우가
 *       모두 동일한 "불가"이기 때문입니다.
 * @note 플레이어가 원형이므로 중심과 반경 위의 네 지점, 총 다섯 곳을 검사합니다.
 *       중심 한 곳만 검사하면 모서리를 가로지르거나 두 섹터의 접합부를 통과할 수
 *       있습니다.
 */
static int can_stand(const Level *l, float x, float z, float feet,
                     float *out_floor) {
    static const float OX[5] = { 0.0f,  1.0f, -1.0f,  0.0f,  0.0f };
    static const float OZ[5] = { 0.0f,  0.0f,  0.0f,  1.0f, -1.0f };

    float highest = -1e30f;

    for (int i = 0; i < 5; i++) {
        float sx = x + OX[i] * PLAYER_RADIUS;
        float sz = z + OZ[i] * PLAYER_RADIUS;

        float f, c;
        /* Outside every sector: not standable, and there is no floor to
           report.
           모든 섹터 바깥입니다. 설 수 없으며 보고할 바닥도 없습니다. */
        if (!level_ground(l, sx, sz, feet, PLAYER_STEP, &f, &c)) return 0;
        if (c - f < PLAYER_EYE) return 0;              /* will not fit */
        if (f > highest) highest = f;
    }

    /* Standing on the highest sample keeps you from sinking into a step that
       only one edge of the circle is over. */
    if (highest - feet > PLAYER_STEP) return 0;
    *out_floor = highest;
    return 1;
}

/**
 * @brief Moves the player along one horizontal axis, resolving collision.
 *
 * ENGLISH
 * -------
 * @brief Moves the player along one horizontal axis, resolving collision.
 * @param[in,out] p     Player to move.
 * @param[in]     l     Level to collide against.
 * @param[in]     axis  0 for X, 2 for Z, indexing into the position vector.
 * @param[in]     delta Distance to travel along that axis this frame.
 * @note Only one axis moves per call, so blocking X while leaving Z free is
 *       what produces sliding along a wall.
 * @note A blocked move is reverted outright rather than partially applied:
 *       the player stops short of the wall instead of being pushed out of it,
 *       which avoids the jitter a push-out solver produces in a corner.
 * @warning Takes `axis` as an index into `&p->pos.x`, so it depends on ::v3
 *          having contiguous x, y, z floats.
 *
 * 한국어
 * ------
 * @brief 플레이어를 하나의 수평축을 따라 이동시키며 충돌을 처리합니다.
 * @param[in,out] p     이동시킬 플레이어.
 * @param[in]     l     충돌 판정 대상 레벨.
 * @param[in]     axis  X는 0, Z는 2이며, 위치 벡터의 인덱스로 사용됩니다.
 * @param[in]     delta 이번 프레임에 해당 축으로 이동할 거리.
 * @note 호출당 하나의 축만 이동하므로, X를 막고 Z를 열어 두는 것이 벽을 따라
 *       미끄러지는 동작을 만들어 냅니다.
 * @note 막힌 이동은 부분적으로 적용되지 않고 통째로 취소됩니다. 플레이어가 벽에서
 *       밀려나는 대신 벽 앞에서 멈추므로, 밀어내기 방식이 모서리에서 유발하는
 *       떨림 현상을 피할 수 있습니다.
 * @warning `axis`를 `&p->pos.x`에 대한 인덱스로 사용하므로, ::v3의 x, y, z가
 *          연속된 float이라는 점에 의존합니다.
 */
static void move_axis(Player *p, const Level *l, int axis, float delta) {
    if (delta == 0.0f) return;

    float *coord = (&p->pos.x) + axis;
    float before = *coord;
    float feet = p->pos.y - PLAYER_EYE;

    *coord += delta;

    /* Speculative move: apply it, test it, and roll it back if the
       destination turns out to be unstandable.
       투기적 이동입니다. 먼저 적용한 뒤 검사하고, 목적지에 설 수 없는 것으로
       판명되면 되돌립니다. */
    float floor_y;
    if (!can_stand(l, p->pos.x, p->pos.z, feet, &floor_y)) {
        *coord = before;
        return;
    }

    /* Walked up onto something: rise with it. */
    if (floor_y > feet) {
        p->pos.y = floor_y + PLAYER_EYE + SKIN;
        p->vel.y = 0.0f;
        p->grounded = 1;
    }
}
