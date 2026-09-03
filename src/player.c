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
static int  move_axis(Player *p, const Level *l, const Blocker *solid,
                      int n_solid, int axis, float delta);

/* --- Public function definitions / 공개 함수 정의 --- */

float player_spawn(Player *p, const Level *l) {
    /* Level coordinates are stored in centimetres as integers.
       레벨 좌표는 센티미터 단위의 정수로 저장되어 있습니다. */
    p->pos = v3f(l->start[0] * 0.01f, PLAYER_EYE, l->start[1] * 0.01f);
    p->vel = v3f(0.0f, 0.0f, 0.0f);
    p->grounded = 0;
    p->health = PLAYER_MAX_HP;
    p->hurt = 0.0f;

    /* WHERE TO START LOOKING DOWN FROM, which is the one thing a spawn cannot
       borrow from the other model.
       On a sector level the marker's own height is irrelevant -- a plan point
       has exactly one floor -- so 1e9 says "unlimited" and loses nothing.
       A brush level has storeys, and a search that begins above the roof finds
       the outside of the roof. So it starts from the height the .map's
       `info_player_start` actually had, which ::Level::start_h carries.
       The step limit stays unlimited either way: whatever is under the marker
       is where the player belongs, however far down it turns out to be.
       어디서부터 아래를 볼지이며, 스폰이 다른 모델에서 빌려 올 수 없는 유일한 값입니다.
       섹터 레벨에서는 표식 자신의 높이가 무의미하므로(평면상의 한 점에 바닥이 정확히
       하나입니다) 1e9가 "무제한"을 뜻하고 잃는 것이 없습니다. 브러시 레벨에는 층이 있고,
       지붕 위에서 시작한 탐색은 지붕의 바깥면을 찾습니다. 그래서 .map의
       `info_player_start`가 실제로 지녔던 높이에서 시작하며, 그 값을 ::Level::start_h가
       나릅니다. 단차 제한은 어느 쪽이든 무제한으로 둡니다. 표식 아래에 있는 것이, 그것이
       아무리 아래에 있더라도, 플레이어가 있어야 할 곳입니다. */
    float from = l->brushes ? l->start_h * 0.01f : 1e9f;

    float f, c;
    if (level_ground(l, p->pos.x, p->pos.z, from, 1e9f, &f, &c))
        p->pos.y = f + PLAYER_EYE + SKIN;

    return l->start[2] * 0.0000174533f;   /* millidegrees -> radians */
}

/* MOMENTUM_DRAG_GROUND/AIR live in player.h now, next to the tuning banner
   that cross-references weapon.h's hook/recoil constants -- the three
   numbers are one dial split across two files by necessity, not by choice,
   and are worth reading together. */

void player_move(Player *p, const Level *l,
                 const Blocker *solid, int n_solid,
                 v3 wish, float speed, int jump, float dt) {
    /* The climb budget refills on the flag as it ARRIVES, before the jump
       clears it and before the vertical pass derives it afresh. Refilling
       later -- from the flag this frame ends with -- would mean the frame you
       leap in is a frame you were never grounded for, and the climb would be
       unavailable for exactly the jump that wanted it.
       등반 예산은 접지 플래그가 *들어온* 그대로에서 채워집니다. 점프가 그것을 지우기 전이자
       수직 처리가 다시 유도하기 전입니다. 더 나중에, 이 프레임이 끝날 때의 플래그로 채우면,
       도약하는 프레임은 접지된 적이 없는 프레임이 되고 등반은 정확히 그것을 원한 그 점프에
       쓸 수 없게 됩니다. */
    if (p->grounded) p->climb = PLAYER_CLIMB_TIME;

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
    int walked = move_axis(p, l, solid, n_solid, 0, dir.x * speed * dt);
    walked    &= move_axis(p, l, solid, n_solid, 2, dir.z * speed * dt);

    /* THE WALL CLIMB, and what it does NOT do is the point.
     *
     * It never asks what is on top. The version this replaced probed for a
     * standable lip within a hand's reach and rose only when it found one,
     * which sounds like the careful choice and is the reason it almost never
     * fired: the arena's walls are 1m or they are 6m, and a player at the foot
     * of one is usually at the foot of the second kind. ::PLAYER_CLIMB_SPEED
     * carries the measurement. Overwatch's climb looks at nothing either, and
     * that is precisely why it works on geometry nobody drew for it.
     *
     * THREE CONDITIONS. Off the ground, so this is something a jump or a fall
     * begins and never a way to walk up a wall from standing. Pushing into
     * something that refused the step, which is what `walked` reports and the
     * only reason ::move_axis returns anything. And budget left.
     *
     * IT ENDS THREE WAYS AND ALL OF THEM ARE THE SAME LINE. The budget runs
     * out; or the wall does, and ::can_stand accepts the step so `walked` is
     * true and the ordinary walk carries the player over the top; or forward
     * is released, `dir` is zero, and gravity has them back. There is no
     * separate hop at the lip because ::move_axis already performs one -- its
     * "walked up onto something: rise with it" is the top-out, and a second
     * impulse there would throw the player off the far side of the ledge they
     * just reached.
     *
     * *벽 등반이며, 하지 않는 일이 핵심입니다.*
     * 이것은 위에 무엇이 있는지 결코 묻지 않습니다. 이것이 대체한 판은 손 닿는 거리 안에서 설
     * 수 있는 턱을 탐사하고 찾았을 때만 상승했으며, 그것은 신중한 선택처럼 들리지만 거의
     * 발동하지 않은 이유 그 자체입니다. 아레나의 벽은 1미터이거나 6미터이고, 그 앞에 선
     * 플레이어는 보통 두 번째 종류 앞에 섭니다. 측정은 ::PLAYER_CLIMB_SPEED가 나릅니다.
     * 오버워치의 등반도 아무것도 보지 않으며, 바로 그래서 그것을 위해 그려지지 않은 기하에서도
     * 통합니다.
     * *조건은 셋입니다.* 땅에서 떨어져 있을 것. 그래야 이것이 점프나 낙하가 시작하는 것이지 선
     * 채로 벽을 걸어 오르는 방법이 아닙니다. 자기를 거절한 무언가를 향해 누르고 있을 것.
     * `walked`가 보고하는 것이고 ::move_axis가 무언가를 돌려주는 유일한 이유입니다. 그리고
     * 예산이 남아 있을 것.
     * *끝나는 길은 셋이고 모두 같은 줄입니다.* 예산이 바닥나거나, 벽이 끝나 ::can_stand가
     * 걸음을 받아들여 `walked`가 참이 되고 평범한 걷기가 플레이어를 꼭대기 너머로 데려가거나,
     * 전진에서 손을 떼어 `dir`이 0이 되고 중력이 도로 가져갑니다. 턱에서의 별도 도약이 없는
     * 이유는 ::move_axis가 이미 하나를 수행하기 때문입니다. 그것의 "무언가 위로 올라섰다.
     * 함께 올린다"가 곧 정상 넘기이며, 그곳의 두 번째 충격량은 방금 닿은 턱의 반대쪽으로
     * 플레이어를 던져 버립니다. */
    if (!walked && !p->grounded && p->climb > 0.0f &&
        (dir.x != 0.0f || dir.z != 0.0f)) {
        p->vel.y = PLAYER_CLIMB_SPEED;
        p->climb -= dt;
    }

    /* External momentum rides on top of the walk above rather than replacing
       it: it moves through the same wall collision, then decays on its own.
       Nothing feeds vel.x/z under normal walking, so this is a no-op until
       something (the grapple, a recoil kick) actually adds to it. */
    move_axis(p, l, solid, n_solid, 0, p->vel.x * dt);
    move_axis(p, l, solid, n_solid, 2, p->vel.z * dt);
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
static int move_axis(Player *p, const Level *l, const Blocker *solid,
                     int n_solid, int axis, float delta) {
    if (delta == 0.0f) return 1;

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
        return 0;
    }

    /* AND THE SAME ROLLBACK FOR A CYLINDER SOMETHING IS STANDING IN. Refused
       per axis like the wall above, so brushing a monster on the diagonal
       slides past it rather than stopping dead -- the sliding is the whole
       reason move_axis is called twice, and a monster the player stops flat
       against would feel like a pillar rather than like a body.

       ALREADY INSIDE ONE IS NOT REFUSED, which is the half a naive version
       gets wrong and the half that matters more here than it does between two
       monsters. Nothing stops a monster walking into a standing player -- the
       enemy side tests other monsters and not this one, on purpose, because a
       brawler that cannot reach the player cannot fight -- so the player being
       inside a monster is ordinary. If that made every step illegal the player
       would be pinned there until the thing died. Every direction out of an
       overlap is still an overlap, so the test is against the cylinders the
       player is NOT already in.

       *그리고 무언가 서 있는 원기둥에 대해서도 같은 되돌림입니다.* 위의 벽과 마찬가지로 축마다
       거절하므로, 대각선으로 몬스터를 스치면 멈춰 서지 않고 미끄러져 지나갑니다. 미끄러짐이
       move_axis를 두 번 부르는 이유의 전부이고, 플레이어가 정면으로 멈춰 서는 몬스터는 몸이
       아니라 기둥처럼 느껴집니다.
       *이미 안에 있는 것은 거절하지 않으며*, 그것이 순진한 판이 틀리는 절반이자 몬스터 둘
       사이에서보다 이곳에서 더 중요한 절반입니다. 몬스터가 서 있는 플레이어 안으로 걸어
       들어오는 것을 막는 것은 없습니다. 적 쪽은 일부러 다른 몬스터만 검사합니다. 플레이어에게
       닿을 수 없는 근접형은 싸울 수 없기 때문입니다. 그러므로 플레이어가 몬스터 안에 있는 것은
       일상입니다. 그것이 모든 걸음을 불법으로 만든다면 플레이어는 그것이 죽을 때까지 그 자리에
       박혀 있게 됩니다. 겹침에서 나가는 모든 방향은 여전히 겹침이므로, 검사는 플레이어가 아직
       *안에 있지 않은* 원기둥에 대해서만 합니다. */
    for (int i = 0; i < n_solid; i++) {
        const Blocker *b = &solid[i];

        /* ::PLAYER_EYE IS THE TOP, because this game has no other number for
           it. There is no PLAYER_HEIGHT: the ceiling test five lines up in
           ::player_move is `pos.y > c - 0.05f`, which is the eye itself kept a
           handspan below the ceiling, so the engine already treats the eye as
           the crown of the head. Inventing a taller figure here would make the
           player duckable-under by a hovering caster in one function and not in
           the other.
           *::PLAYER_EYE가 정수리인 이유는* 이 게임에 그것을 가리키는 다른 수가 없기
           때문입니다. PLAYER_HEIGHT는 없습니다. ::player_move의 천장 검사는
           `pos.y > c - 0.05f`이며 그것은 눈 자체를 천장 아래 한 뼘에 붙잡아 두는 것이므로,
           엔진은 이미 눈을 머리 꼭대기로 다룹니다. 이곳에서 더 큰 수를 지어내면 떠 있는
           캐스터 밑으로 지나갈 수 있는지가 한 함수와 다른 함수에서 달라집니다. */
        if (feet >= b->pos.y + b->height || b->pos.y >= feet + PLAYER_EYE)
            continue;

        float r = PLAYER_RADIUS + b->radius;

        float wx = before, wz = p->pos.z;
        if (axis == 2) { wx = p->pos.x; wz = before; }
        float cx = wx - b->pos.x, cz = wz - b->pos.z;
        if (cx * cx + cz * cz < r * r)
            continue;               /* already inside it: see above */

        float dx = p->pos.x - b->pos.x, dz = p->pos.z - b->pos.z;
        if (dx * dx + dz * dz < r * r) {
            *coord = before;
            return 0;
        }
    }

    /* Walked up onto something: rise with it. */
    if (floor_y > feet) {
        p->pos.y = floor_y + PLAYER_EYE + SKIN;
        p->vel.y = 0.0f;
        p->grounded = 1;
    }
    return 1;
}
