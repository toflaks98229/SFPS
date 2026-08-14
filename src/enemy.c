/**
 * @file enemy.c
 * @brief 몬스터 AI 및 충돌 탐지 로직을 구현합니다. GL 관련 코드는 포함하지 않습니다.
 */

#include "enemy.h"
#include "pools.h"
#include "audio.h"
#include "fx.h"
#include "diag.h"
#include "player.h" /* PLAYER_EYE / PLAYER_RADIUS -- 발사체 히트 박스용 */
#include <math.h>

/* --- 정적 변수 --- */

/** @brief 모든 몬스터의 배열. */
/** @brief 현재 활성화된 몬스터의 수. */
/** @brief 난수 생성을 위한 시드. */
/* The seed a pool starts from; a zeroed EnemyPool means "not seeded yet".
   See fx.c for the same arrangement and the reason for it.
   풀이 출발하는 씨앗이며, 0인 EnemyPool은 "아직 씨앗이 채워지지 않음"을 뜻합니다. 같은
   구성과 그 이유는 fx.c를 참조하십시오. */
#define ENEMY_RNG_SEED 0x9e3779b9u
/* A `g_player_eye` used to sit here, assigned at the top of every enemy_update
   and read by nothing at all. Every helper below takes the eye as an argument
   -- can_see, sees_player and shot_fire all do -- so the global was a copy the
   AI never consulted: twelve bytes of .bss and a store per frame, and worse, an
   invitation. The next helper that needed the eye could have read it instead of
   asking for it, and would then have been reading a value whose freshness
   depended on where in the call stack it happened to be.
   이곳에 `g_player_eye`가 있었습니다. 모든 enemy_update의 맨 위에서 대입되었고 어디에서도
   읽히지 않았습니다. 아래의 모든 헬퍼는 눈 위치를 인자로 받습니다(can_see, sees_player,
   shot_fire 모두 그렇습니다). 따라서 그 전역은 AI가 결코 참조하지 않는 사본이었습니다.
   .bss 12바이트와 프레임당 저장 한 번, 그리고 더 나쁜 것은 유혹입니다. 눈 위치가 필요한 다음
   헬퍼가 그것을 요청하는 대신 읽을 수 있었고, 그러면 호출 스택의 어디에 있느냐에 따라
   신선도가 달라지는 값을 읽게 됩니다. */
/** @brief 활성화된 모든 발사체의 배열. */

/**
 * @brief 몬스터 타입별 스탯 테이블 (베스티어리).
 * 행은 MON_* 열거형으로 인덱싱되며, 동일한 인덱스는 스프라이트 행입니다.
 * 순서: hp, spd, rad, hgt, eye, sight, att, dmg, wind, cool, aspct, shot
 */
/* The last two columns are Quake's: how fast it turns, and how long it is
   deaf to pain. Both are character rather than tuning -- the brute cannot
   track a circling player and cannot be stun-locked, and those two facts are
   the same fact about what a brute is.
   마지막 두 열은 Quake의 것입니다. 얼마나 빨리 도는가, 그리고 얼마나 오래 고통에
   무감각한가. 둘 다 조정값이 아니라 성격입니다. 브루트는 원을 그리는 플레이어를 추적하지
   못하고 스턴 락에도 걸리지 않는데, 그 두 사실은 브루트가 무엇인가에 대한 하나의
   사실입니다. */
/*         name     behaviour     hp  spd   rad    hgt    eye   sight  atk  dmg  wind   cool  aspct shot   yaw    pain */
static const MonType TYPES[MON_TYPES] = {
    /* IMP: 기준선. 충분히 빠르며, 근접 샷건 한 방에 죽습니다. */
    {"imp", AI_BRAWLER, 40, 3.0f, 0.40f, 1.70f, 1.30f, 34.0f, 1.8f, 9, 0.35f, 1.10f, 0.70f, 0.0f, 220.0f, 0.6f},
    /* BRUTE: 체력이 높은 벽. 느리게 다가오지만 강력한 공격을 하므로, 피하기보다 계획적으로 대처해야 하는 위협입니다. */
    {"brute", AI_BRAWLER, 120, 1.9f, 0.62f, 2.35f, 1.80f, 34.0f, 2.3f, 24, 0.55f, 1.50f, 0.85f, 0.0f, 130.0f, 2.2f},
    /* HOUND: 빠르고 약한 야수. 가만히 있는 것을 응징합니다. 한 번의 공격 피해는 적지만, 경고를 알아차리기 전에 덮칩니다. */
    {"hound", AI_BRAWLER, 18, 5.3f, 0.38f, 1.25f, 0.70f, 40.0f, 1.5f, 5, 0.18f, 0.65f, 1.00f, 0.0f, 400.0f, 0.3f},
    /* CASTER: 계속 움직여야 하는 이유. 접근하지 않고 사정거리를 유지하며 주문을 시전하므로, 발놀림 대신 엄폐와 각도가 중요합니다. */
    {"caster", AI_CASTER, 26, 2.4f, 0.42f, 1.90f, 1.45f, 40.0f, 13.0f, 12, 0.85f, 1.40f, 0.80f, 11.0f, 180.0f, 0.9f},
};

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
        int caster = TYPES[i].behaviour == AI_CASTER;
        if (caster != (TYPES[i].shot_speed > 0.0f))
            DIAG(DIAG_MON_TABLE);
    }
}

/* `spawn`은 종류가 하나뿐이던 시절의 이름이며, 기존 맵들이 여전히 사용합니다.
   TYPES에 넣지 않고 별칭으로 두는 이유는, 그것이 실제로 별칭이기 때문입니다. 테이블에
   넣으면 다섯 번째 *종류*가 되어 스프라이트 아틀라스에 행 하나를 요구하고
   enemytest가 검사하는 종류 수를 바꾸게 됩니다.
   A legacy name from when there was only one kind, still used by existing maps.
   Kept as an alias rather than a TYPES row because that is what it is: a row
   would make it a fifth KIND, demanding an atlas row and changing the type
   count enemytest checks. */
#define MON_LEGACY_NAME "spawn"
#define MON_LEGACY_TYPE MON_IMP

/** @brief 원거리 몬스터(Caster)가 플레이어에게 허용하는 최소 접근 거리. 이보다 가까워지면 뒤로 물러납니다. */
#define CASTER_KEEP 0.55f

/* --- 정적 함수 --- */

/**
 * @brief 의사 난수를 생성합니다.
 * @return 0.0f에서 1.0f 사이의 float 값.
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

/* ---------------------------------------------------------- 발사체 로직 */

/**
 * @brief 지정된 위치에서 목표를 향해 발사체를 발사합니다.
 * @param from 발사 시작 위치.
 * @param at 목표 위치.
 * @param speed 발사체 속도.
 * @param damage 발사체 피해량.
 */
static void shot_fire(Pools *pl, v3 from, v3 at, float speed, int damage)
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
    /* Zero, not the interval: the first trail particle is laid on the frame
       the bolt appears, so the wake starts at the muzzle rather than a
       fraction of a second down the flight path.
       간격이 아니라 0입니다. 첫 궤적 파티클이 볼트가 나타나는 프레임에 놓이므로,
       흔적이 비행 경로 중간이 아니라 총구에서 시작됩니다. */
    s->trail_timer = 0.0f;
}

/**
 * @brief 모든 활성 발사체를 업데이트하고 플레이어에게 가한 총 피해량을 반환합니다.
 * @param l 현재 레벨 데이터.
 * @param player_eye 플레이어의 눈 위치.
 * @param dt 프레임 시간.
 * @return 플레이어에게 가해진 총 피해량.
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
            continue;
        }

        if (hit_wall && t <= dist)
        {
            s->pos = v3add(s->pos, v3scale(dir, t));
            s->active = 0;
            play_at(s->pos, "ehit", 45);
            fx_spawn(pl, "boltburst", s->pos, n);
            continue;
        }

        s->pos = v3add(s->pos, step);
    }

    return dealt;
}

/* ------------------------------------------------------------- 충돌 및 이동 */

/**
 * @brief 특정 몬스터 타입이 지정된 위치에 설 수 있는지 확인합니다.
 * @param l 현재 레벨 데이터.
 * @param S 몬스터 타입 스탯.
 * @param x 확인할 x 좌표.
 * @param z 확인할 z 좌표.
 * @param feet 현재 몬스터의 발 높이.
 * @param floor [out] 몬스터가 서게 될 바닥의 높이를 저장할 포인터.
 * @return 설 수 있으면 1, 그렇지 않으면 0.
 */
static int foot_ok(const Level *l, const MonType *S, float x, float z,
                   float feet, float *floor)
{
    float f, c;
    if (!level_ground(l, x, z, feet, S->height / 3.0f, &f, &c))
        return 0;
    if (c - f < S->height)
        return 0;
    *floor = f;
    return 1;
}

/**
 * @brief 몬스터를 (dx, dz) 방향으로 이동시킵니다. 벽을 따라 미끄러지도록 합니다.
 * @param l 현재 레벨 데이터.
 * @param S 몬스터 타입 스탯.
 * @param m 이동시킬 몬스터.
 * @param dx x축 이동량.
 * @param dz z축 이동량.
 */
static void move_toward(const Level *l, const MonType *S, Enemy *m,
                        float dx, float dz)
{
    float f;
    if (foot_ok(l, S, m->pos.x + dx, m->pos.z + dz, m->pos.y, &f))
    {
        m->pos.x += dx;
        m->pos.z += dz;
        m->pos.y = f;
        return;
    }
    if (foot_ok(l, S, m->pos.x + dx, m->pos.z, m->pos.y, &f))
    {
        m->pos.x += dx;
        m->pos.y = f;
        return;
    }
    if (foot_ok(l, S, m->pos.x, m->pos.z + dz, m->pos.y, &f))
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
static void ai_run_slide(Pools *pl, const Level *l, const MonType *S, Enemy *m, float dt)
{
    float step = S->speed * dt;

    if (m->slide_wait <= 0.0f)
    {
        m->lefty = (char)(frand(&pl->enemy) < 0.5f);
        m->slide_wait = MON_SLIDE_HOLD;
    }

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
    move_toward(l, S, m, dx, dz);

    /* Blocked: flip, and let the next frame take the other side. Measured by
       whether it actually moved rather than by asking the level again, so the
       test and the movement can never disagree.
       막혔습니다. 뒤집어서 다음 프레임이 반대쪽을 쓰게 합니다. 레벨에 다시 묻지 않고
       실제로 움직였는지로 판정하므로, 검사와 이동이 어긋날 수 없습니다. */
    float moved = fabsf(m->pos.x - before_x) + fabsf(m->pos.z - before_z);
    if (moved < step * 0.25f)
    {
        m->lefty = (char)!m->lefty;
        m->slide_wait = MON_SLIDE_HOLD;
    }
}

/* --- Quake's CheckAttack -------------------------------------------------
 *
 * Whether to start an attack, given how far away the player is. Returns 1 to
 * attack, 0 to keep manoeuvring.
 *
 * The odds come straight from fight.qc, including its halving for a monster
 * that also has a melee attack -- something that can bite prefers to close, so
 * it shoots less on the way in. Our `shot_speed > 0` is already the field that
 * says "ranged", so it decides here too rather than a second flag.
 *
 * 플레이어와의 거리에 따라 공격을 시작할지 결정합니다. 확률은 fight.qc에서 그대로 왔으며,
 * 근접 공격도 가진 몬스터에 대한 절반 감소도 포함합니다. 물 수 있는 것은 거리를 좁히기를
 * 선호하므로 다가오는 동안 덜 쏩니다. 우리의 `shot_speed > 0`이 이미 "원거리"를 말하는
 * 필드이므로, 두 번째 플래그가 아니라 그것이 여기서도 결정합니다. */
static int check_attack(Pools *pl, const MonType *S, Enemy *m, float dist)
{
    if (m->attack_wait > 0.0f)
        return 0;

    float chance;
    if (dist <= MON_RANGE_MELEE)
        chance = MON_ODDS_MELEE;
    else if (dist <= MON_RANGE_NEAR)
        chance = MON_ODDS_NEAR;
    else if (dist <= MON_RANGE_MID)
        chance = MON_ODDS_MID;
    else
        return 0;

    /* A melee monster out of its reach cannot attack at all, whatever the dice
       say. The bands are about willingness; this is about arms.
       근접 몬스터는 사거리 밖에서는 주사위와 무관하게 공격할 수 없습니다. 대역은
       의사에 관한 것이고 이것은 팔 길이에 관한 것입니다. */
    if (S->shot_speed <= 0.0f)
        return dist <= S->attack;

    return frand(&pl->enemy) < chance;
}

/**
 * @brief 몬스터가 플레이어를 볼 수 있는지 (직선 시야가 확보되는지) 확인합니다.
 * @param l 현재 레벨 데이터.
 * @param m 몬스터.
 * @param player_eye 플레이어의 눈 위치.
 * @return 볼 수 있으면 1, 그렇지 않으면 0.
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
static int sees_player(const Level *l, Enemy *m, v3 player_eye)
{
    if (m->sight_age <= 0)
    {
        m->sight_age = SIGHT_PERIOD;
        m->seen = (char)can_see(l, m, player_eye);
    }
    return m->seen;
}

/* --- 공개 API 함수 --- */

const MonType *mon_stats(int type)
{
    if (type < 0 || type >= MON_TYPES)
        type = MON_IMP;
    return &TYPES[type];
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

int mon_type_for(const char *kind)
{
    /* 테이블을 순회합니다. 새 몬스터는 TYPES에 행 하나를 추가하면 이곳이 자동으로
       알아보므로, 이 함수는 종류가 늘어나도 수정할 필요가 없습니다.
       Walks the table, so a new monster is a TYPES row and this function finds
       it without being edited. */
    for (int i = 0; i < MON_TYPES; i++)
        if (name_eq(TYPES[i].name, kind))
            return i;

    if (name_eq(MON_LEGACY_NAME, kind))
        return MON_LEGACY_TYPE;

    return -1;
}

int enemy_shot_count(const Pools *pl) { (void)pl; return ENEMY_MAX_SHOTS; }

const Shot *enemy_shot_at(const Pools *pl, int i)
{
    return (i >= 0 && i < ENEMY_MAX_SHOTS) ? &pl->enemy.shots[i] : 0;
}

void enemy_reset(Pools *pl)
{
    for (int i = 0; i < ENEMY_MAX; i++)
        pl->enemy.m[i].active = 0;
    for (int i = 0; i < ENEMY_MAX_SHOTS; i++)
        pl->enemy.shots[i].active = 0;
    pl->enemy.count = 0;
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

const Enemy *enemy_at(const Pools *pl, int i)
{
    return (i >= 0 && i < pl->enemy.count) ? &pl->enemy.m[i] : 0;
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
        const MonType *S = &TYPES[type];

        float x = e->x * 0.01f, z = e->z * 0.01f;
        float f, c;
        if (!level_ground(l, x, z, 1000.0f, S->height, &f, &c))
            continue;

        if (pl->enemy.count >= ENEMY_MAX)
        {
            DIAG(DIAG_ENEMY_CAP);
            continue;
        }

        Enemy *m = &pl->enemy.m[pl->enemy.count++];
        Enemy zero = {0};
        *m = zero;
        m->type = type;
        m->pos = v3f(x, f, z);
        m->health = S->hp;
        m->state = E_IDLE;
        m->active = 1;
        m->anim = frand(&pl->enemy) * 6.28f;

        /* Spread the sight refreshes across the period rather than lining them
           all up on the frame after a spawn. Derived from the index so it is
           deterministic -- the headless tests step this simulation and compare
           exact outcomes, and frand(&pl->enemy) here would make which monster refreshes
           on which frame depend on how many spawned before it.
           시야 갱신을 생성 직후의 한 프레임에 몰지 않고 주기 전체에 분산시킵니다. 인덱스
           에서 유도하므로 결정론적입니다. 헤드리스 테스트가 이 시뮬레이션을 진행시키며
           정확한 결과를 비교하는데, 이곳에서 frand(&pl->enemy)를 쓰면 어느 몬스터가 어느 프레임에
           갱신되는지가 그 앞에 몇 마리가 생성되었는지에 좌우됩니다. */
        m->sight_age = (short)((pl->enemy.count - 1) % SIGHT_PERIOD);
    }
}

/* ------------------------------------------------------------- archetypes */

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
static void chase_brawler(Pools *pl, const Level *l, const MonType *S, Enemy *m,
                          v3 to, float dist, float dt)
{
    float inv = dist > 0.001f ? 1.0f / dist : 0.0f;
    float step = S->speed * dt;

    if (dist > S->attack)
    {
        move_toward(l, S, m, to.x * inv * step, to.z * inv * step);
        return;
    }

    /* Within reach. The roll here is Quake's 0.9 at melee range -- high enough
       that closing is still lethal, low enough that a monster occasionally
       repositions instead of grinding out its swing timer nose-to-nose.
       사거리 안입니다. 여기의 굴림은 근접 대역에서 Quake의 0.9입니다. 거리를 좁히는 것이
       여전히 치명적일 만큼 높고, 몬스터가 코앞에서 공격 타이머만 돌리는 대신 이따금 자리를
       바꿀 만큼 낮습니다. */
    if (check_attack(pl, S, m, dist))
    {
        m->state = E_ATTACK;
        m->timer = 0.0f;
        m->swung = 0;
    }
    else
    {
        ai_run_slide(pl, l, S, m, dt);
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

    if (dist > S->attack)
    {
        move_toward(l, S, m, to.x * inv * step, to.z * inv * step);
        return;
    }
    if (dist < S->attack * CASTER_KEEP)
    {
        move_toward(l, S, m, -to.x * inv * step, -to.z * inv * step);
        return;
    }

    /* Cached: this decides whether to PLANT and begin a wind-up, and the
       wind-up is long enough that a frame or two of staleness cannot matter --
       ::release_bolt checks again, live, and that is the check that actually
       guards the wall.
       캐시를 씁니다. 이것은 자리를 잡고 시전을 *시작할지*를 결정하며, 시전 시간이 충분히
       길어 한두 프레임의 지연은 문제가 될 수 없습니다. ::release_bolt가 실시간으로 다시
       검사하며, 벽을 실제로 지키는 것은 그 검사입니다. */
    if (!sees_player(l, m, player_eye))
    {
        move_toward(l, S, m, to.x * inv * step, to.z * inv * step);
        return;
    }

    /* In its preferred band and looking right at the player -- and it still
       only sometimes shoots. The rest of the time it circles, which is what
       turns a caster from a turret into something you have to chase around a
       room.
       선호하는 대역 안에서 플레이어를 정면으로 보고 있으면서도, 여전히 *가끔만* 쏩니다.
       나머지 시간에는 원을 그립니다. 그것이 캐스터를 포탑에서 방 안을 쫓아다녀야 하는
       무언가로 바꿉니다. */
    if (check_attack(pl, S, m, dist))
    {
        m->state = E_ATTACK;
        m->timer = 0.0f;
        m->swung = 0;
    }
    else
    {
        ai_run_slide(pl, l, S, m, dt);
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
static int release_swing(const MonType *S, Enemy *m, float dist)
{
    if (dist > S->attack + 0.3f)
        return 0;
    play_at(m->pos, "eatt", 90);
    return S->damage;
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
static void release_bolt(Pools *pl, const Level *l, const MonType *S, Enemy *m, v3 player_eye)
{
    if (!can_see(l, m, player_eye))
        return;

    v3 from = v3f(m->pos.x, m->pos.y + S->eye, m->pos.z);
    v3 at = v3f(player_eye.x, player_eye.y - PLAYER_EYE * 0.35f, player_eye.z);
    shot_fire(pl, from, at, S->shot_speed, S->damage);
    play_at(m->pos, "ecast", 90);
}

int enemy_update(Pools *pl, const Level *l, v3 player_eye, float dt)
{
    int player_damage = shots_update(pl, l, player_eye, dt);

    for (int i = 0; i < pl->enemy.count; i++)
    {
        Enemy *m = &pl->enemy.m[i];
        if (!m->active)
            continue;
        const MonType *S = &TYPES[m->type];

        m->anim += dt;
        if (m->flash > 0.0f)
            m->flash -= dt * 4.0f;

        if (m->state == E_DEAD)
        {
            if (m->timer > 0.0f)
                m->timer -= dt;
            continue;
        }

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

        v3 to = v3sub(player_eye, v3f(m->pos.x, m->pos.y + S->eye, m->pos.z));
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
            if (dist < S->sight && sees_player(l, m, player_eye))
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
                chase_caster(pl, l, S, m, to, dist, player_eye, dt);
            else
                chase_brawler(pl, l, S, m, to, dist, dt);
            break;

        case E_ATTACK:
            m->timer += dt;
            if (!m->swung && m->timer >= S->windup)
            {
                m->swung = 1;
                if (S->behaviour == AI_CASTER)
                    release_bolt(pl, l, S, m, player_eye);
                else
                    player_damage += release_swing(S, m, dist);
            }
            if (m->timer >= S->windup + S->cooldown)
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
                if (S->behaviour == AI_CASTER)
                    m->state = E_CHASE;
                else if (dist <= S->attack)
                {
                    m->timer = 0.0f;
                    m->swung = 0;
                }
                else
                    m->state = E_CHASE;
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

        float f, c;
        if (level_ground(l, m->pos.x, m->pos.z, m->pos.y, S->height / 3.0f, &f, &c))
        {
            if (m->pos.y > f + 0.01f)
            {
                m->vel_y -= 22.0f * dt;
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
    }
    return player_damage;
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
    if (idx < 0 || idx >= pl->enemy.count)
        return;
    Enemy *m = &pl->enemy.m[idx];
    if (!m->active || m->state == E_DEAD)
        return;

    m->health -= dmg;
    m->flash = 1.0f;

    /* Centre of mass rather than the feet, so the spray leaves the body and
       not the floor underneath it. Enemy stores only what varies per instance,
       so the height comes from the type table.
       발이 아니라 몸통 중심입니다. 그래야 분출이 발밑 바닥이 아니라 몸에서 나옵니다.
       Enemy는 개체별로 달라지는 값만 보관하므로 신장은 종류 테이블에서 가져옵니다. */
    const MonType *S = &TYPES[m->type];
    v3 mid = v3f(m->pos.x, m->pos.y + S->height * 0.5f, m->pos.z);

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
        play_at(m->pos, "edie", 95);
        fx_spawn(pl, "gib", mid, back);
        return;
    }

    fx_spawn(pl, "blood", mid, back);

    play_at(m->pos, "epain", 70);

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
