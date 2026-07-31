/**
 * @file enemy.c
 * @brief 몬스터 AI 및 충돌 탐지 로직을 구현합니다. GL 관련 코드는 포함하지 않습니다.
 */

#include "enemy.h"
#include "audio.h"
#include "diag.h"
#include "player.h"    /* PLAYER_EYE / PLAYER_RADIUS -- 발사체 히트 박스용 */
#include <math.h>

/* --- 정적 변수 --- */

/** @brief 모든 몬스터의 배열. */
static Enemy g_enemies[ENEMY_MAX];
/** @brief 현재 활성화된 몬스터의 수. */
static int   g_count;
/** @brief 난수 생성을 위한 시드. */
static unsigned g_rng = 0x9e3779b9u;
/** @brief 마지막 업데이트 시점의 플레이어 눈 위치. */
static v3 g_player_eye;
/** @brief 활성화된 모든 발사체의 배열. */
static Shot g_shots[ENEMY_MAX_SHOTS];

/**
 * @brief 몬스터 타입별 스탯 테이블 (베스티어리).
 * 행은 MON_* 열거형으로 인덱싱되며, 동일한 인덱스는 스프라이트 행입니다.
 * 순서: hp, spd, rad, hgt, eye, sight, att, dmg, wind, cool, aspct, shot
 */
static const MonType TYPES[MON_TYPES] = {
    /* IMP: 기준선. 충분히 빠르며, 근접 샷건 한 방에 죽습니다. */
    {  40, 3.0f, 0.40f, 1.70f, 1.30f, 34.0f,  1.8f,  9, 0.35f, 1.10f, 0.70f,  0.0f },
    /* BRUTE: 체력이 높은 벽. 느리게 다가오지만 강력한 공격을 하므로, 피하기보다 계획적으로 대처해야 하는 위협입니다. */
    { 120, 1.9f, 0.62f, 2.35f, 1.80f, 34.0f,  2.3f, 24, 0.55f, 1.50f, 0.85f,  0.0f },
    /* HOUND: 빠르고 약한 야수. 가만히 있는 것을 응징합니다. 한 번의 공격 피해는 적지만, 경고를 알아차리기 전에 덮칩니다. */
    {  18, 5.3f, 0.38f, 1.25f, 0.70f, 40.0f,  1.5f,  5, 0.18f, 0.65f, 1.00f,  0.0f },
    /* CASTER: 계속 움직여야 하는 이유. 접근하지 않고 사정거리를 유지하며 주문을 시전하므로, 발놀림 대신 엄폐와 각도가 중요합니다. */
    {  26, 2.4f, 0.42f, 1.90f, 1.45f, 40.0f, 13.0f, 12, 0.85f, 1.40f, 0.80f, 11.0f },
};

/** @brief 원거리 몬스터(Caster)가 플레이어에게 허용하는 최소 접근 거리. 이보다 가까워지면 뒤로 물러납니다. */
#define CASTER_KEEP  0.55f

/* --- 정적 함수 --- */

/**
 * @brief 의사 난수를 생성합니다.
 * @return 0.0f에서 1.0f 사이의 float 값.
 */
static float frand(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (g_rng >> 8) * (1.0f / 16777216.0f);
}

/**
 * @brief 특정 위치에서 사운드를 재생하며, 거리에 따라 볼륨을 조절합니다.
 * @param p 사운드 발생 위치.
 * @param name 재생할 사운드의 이름.
 * @param base 기본 볼륨 게인.
 */
static void play_at(v3 p, const char *name, int base) {
    float dx = p.x - g_player_eye.x, dy = p.y - g_player_eye.y, dz = p.z - g_player_eye.z;
    float d = sqrtf(dx*dx + dy*dy + dz*dz);
    int g = (int)(base * (12.0f / (12.0f + d)));
    if (g > 0) audio_play(name, g);
}

/* ---------------------------------------------------------- 발사체 로직 */

/**
 * @brief 지정된 위치에서 목표를 향해 발사체를 발사합니다.
 * @param from 발사 시작 위치.
 * @param at 목표 위치.
 * @param speed 발사체 속도.
 * @param damage 발사체 피해량.
 */
static void shot_fire(v3 from, v3 at, float speed, int damage) {
    Shot *s = 0;
    for (int i = 0; i < ENEMY_MAX_SHOTS; i++)
        if (!g_shots[i].active) { s = &g_shots[i]; break; }
    if (!s) return;

    v3 d = v3sub(at, from);
    float len = v3len(d);
    if (len < 0.001f) return;
    d = v3scale(d, 1.0f / len);

    s->pos    = from;
    s->vel    = v3scale(d, speed);
    s->life   = 6.0f;
    s->damage = damage;
    s->active = 1;
}

/**
 * @brief 모든 활성 발사체를 업데이트하고 플레이어에게 가한 총 피해량을 반환합니다.
 * @param l 현재 레벨 데이터.
 * @param player_eye 플레이어의 눈 위치.
 * @param dt 프레임 시간.
 * @return 플레이어에게 가해진 총 피해량.
 */
static int shots_update(const Level *l, v3 player_eye, float dt) {
    int dealt = 0;

    for (int i = 0; i < ENEMY_MAX_SHOTS; i++) {
        Shot *s = &g_shots[i];
        if (!s->active) continue;

        s->life -= dt;
        if (s->life <= 0.0f) { s->active = 0; continue; }

        v3 step = v3scale(s->vel, dt);
        float dist = v3len(step);
        if (dist < 1e-6f) continue;
        v3 dir = v3scale(step, 1.0f / dist);

        float t; v3 n;
        int hit_wall = level_trace(l, s->pos, dir, dist + SHOT_RADIUS, &t, &n);

        v3 feet = v3f(player_eye.x, player_eye.y - PLAYER_EYE, player_eye.z);
        v3 rel  = v3sub(s->pos, feet);
        float body_hit = -1.0f;
        {
            float rx = rel.x, rz = rel.z;
            float a = dir.x*dir.x + dir.z*dir.z;
            float b = 2.0f * (rx*dir.x + rz*dir.z);
            float rr = PLAYER_RADIUS + SHOT_RADIUS;
            float c = rx*rx + rz*rz - rr*rr;
            if (c <= 0.0f) {
                body_hit = 0.0f;
            } else if (a > 1e-6f) {
                float disc = b*b - 4.0f*a*c;
                if (disc >= 0.0f) {
                    float root = (-b - sqrtf(disc)) / (2.0f * a);
                    if (root >= 0.0f && root <= dist) body_hit = root;
                }
            }
            if (body_hit >= 0.0f) {
                float y = s->pos.y + dir.y * body_hit;
                if (y < feet.y - 0.2f || y > feet.y + PLAYER_EYE + 0.35f)
                    body_hit = -1.0f;
            }
        }

        if (body_hit >= 0.0f && (!hit_wall || body_hit <= t)) {
            dealt += s->damage;
            s->active = 0;
            play_at(s->pos, "ehit", 85);
            continue;
        }

        if (hit_wall && t <= dist) {
            s->pos = v3add(s->pos, v3scale(dir, t));
            s->active = 0;
            play_at(s->pos, "ehit", 45);
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
                   float feet, float *floor) {
    float f, c;
    if (!level_ground(l, x, z, feet, S->height / 3.0f, &f, &c)) return 0;
    if (c - f < S->height) return 0;
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
                        float dx, float dz) {
    float f;
    if (foot_ok(l, S, m->pos.x + dx, m->pos.z + dz, m->pos.y, &f)) {
        m->pos.x += dx; m->pos.z += dz; m->pos.y = f;
        return;
    }
    if (foot_ok(l, S, m->pos.x + dx, m->pos.z, m->pos.y, &f)) {
        m->pos.x += dx; m->pos.y = f;
        return;
    }
    if (foot_ok(l, S, m->pos.x, m->pos.z + dz, m->pos.y, &f)) {
        m->pos.z += dz; m->pos.y = f;
    }
}

/**
 * @brief 몬스터가 플레이어를 볼 수 있는지 (직선 시야가 확보되는지) 확인합니다.
 * @param l 현재 레벨 데이터.
 * @param m 몬스터.
 * @param player_eye 플레이어의 눈 위치.
 * @return 볼 수 있으면 1, 그렇지 않으면 0.
 */
static int can_see(const Level *l, const Enemy *m, v3 player_eye) {
    v3 eye = v3f(m->pos.x, m->pos.y + mon_stats(m->type)->eye, m->pos.z);
    v3 d = v3sub(player_eye, eye);
    float dist = v3len(d);
    if (dist < 0.001f) return 1;
    d = v3scale(d, 1.0f / dist);
    float t; v3 n;
    if (!level_trace(l, eye, d, dist, &t, &n)) return 1;
    return t >= dist - 0.05f;
}

/* --- 공개 API 함수 --- */

const MonType *mon_stats(int type) {
    if (type < 0 || type >= MON_TYPES) type = MON_IMP;
    return &TYPES[type];
}

int mon_type_for(const char *kind) {
    const char *k = kind;
    if ((k[0]=='i'&&k[1]=='m'&&k[2]=='p'&&k[3]==0) ||
        (k[0]=='s'&&k[1]=='p'&&k[2]=='a'&&k[3]=='w'&&k[4]=='n'&&k[5]==0))
        return MON_IMP;
    if (k[0]=='b'&&k[1]=='r'&&k[2]=='u'&&k[3]=='t'&&k[4]=='e'&&k[5]==0) return MON_BRUTE;
    if (k[0]=='h'&&k[1]=='o'&&k[2]=='u'&&k[3]=='n'&&k[4]=='d'&&k[5]==0) return MON_HOUND;
    if (k[0]=='c'&&k[1]=='a'&&k[2]=='s'&&k[3]=='t'&&k[4]=='e'&&k[5]=='r'&&k[6]==0)
        return MON_CASTER;
    return -1;
}

int enemy_shot_count(void) { return ENEMY_MAX_SHOTS; }

const Shot *enemy_shot_at(int i) {
    return (i >= 0 && i < ENEMY_MAX_SHOTS) ? &g_shots[i] : 0;
}

void enemy_reset(void) {
    for (int i = 0; i < ENEMY_MAX; i++) g_enemies[i].active = 0;
    for (int i = 0; i < ENEMY_MAX_SHOTS; i++) g_shots[i].active = 0;
    g_count = 0;
}

int enemy_count(void) { return g_count; }

int enemy_alive(void) {
    int n = 0;
    for (int i = 0; i < g_count; i++)
        if (g_enemies[i].active && g_enemies[i].state != E_DEAD) n++;
    return n;
}

const Enemy *enemy_at(int i) {
    return (i >= 0 && i < g_count) ? &g_enemies[i] : 0;
}

void enemy_spawn_level(const Level *l) {
    enemy_reset();
    /* Runs over every entity even once full, rather than breaking out on the
       cap. Stopping early is the same amount of spawning but loses the count
       of what was skipped -- and "the level is missing monsters" is otherwise
       indistinguishable from "the level was authored that way".
       가득 찬 뒤에도 한계에서 루프를 빠져나가지 않고 모든 엔티티를 순회합니다. 조기
       종료해도 생성되는 수는 같지만 건너뛴 개수를 알 수 없게 되며, "레벨에 몬스터가
       빠졌다"와 "레벨을 원래 그렇게 만들었다"를 구분할 수 없게 됩니다. */
    for (int i = 0; i < l->n_ents; i++) {
        const Entity *e = &l->ents[i];
        int type = mon_type_for(e->kind);
        if (type < 0) continue;
        const MonType *S = &TYPES[type];

        float x = e->x * 0.01f, z = e->z * 0.01f;
        float f, c;
        if (!level_ground(l, x, z, 1000.0f, S->height, &f, &c)) continue;

        if (g_count >= ENEMY_MAX) { DIAG(DIAG_ENEMY_CAP); continue; }

        Enemy *m = &g_enemies[g_count++];
        Enemy zero = {0};
        *m = zero;
        m->type   = type;
        m->pos    = v3f(x, f, z);
        m->health = S->hp;
        m->state  = E_IDLE;
        m->active = 1;
        m->anim   = frand() * 6.28f;
    }
}

int enemy_update(const Level *l, v3 player_eye, float dt) {
    g_player_eye = player_eye;
    int player_damage = shots_update(l, player_eye, dt);

    for (int i = 0; i < g_count; i++) {
        Enemy *m = &g_enemies[i];
        if (!m->active) continue;
        const MonType *S = &TYPES[m->type];

        m->anim += dt;
        if (m->flash > 0.0f) m->flash -= dt * 4.0f;

        if (m->state == E_DEAD) {
            if (m->timer > 0.0f) m->timer -= dt;
            continue;
        }

        v3 to = v3sub(player_eye, v3f(m->pos.x, m->pos.y + S->eye, m->pos.z));
        float dist = sqrtf(to.x*to.x + to.z*to.z);
        m->yaw = atan2f(-to.x, -to.z);

        switch (m->state) {
        case E_IDLE:
            if (dist < S->sight && can_see(l, m, player_eye)) {
                m->state = E_CHASE;
                play_at(m->pos, "sight", 80);
            }
            break;

        case E_CHASE: {
            float inv = dist > 0.001f ? 1.0f / dist : 0.0f;
            float step = S->speed * dt;

            if (S->shot_speed > 0.0f) {
                if (dist > S->attack) {
                    move_toward(l, S, m, to.x * inv * step, to.z * inv * step);
                } else if (dist < S->attack * CASTER_KEEP) {
                    move_toward(l, S, m, -to.x * inv * step, -to.z * inv * step);
                } else if (can_see(l, m, player_eye)) {
                    m->state = E_ATTACK;
                    m->timer = 0.0f;
                    m->swung = 0;
                } else {
                    move_toward(l, S, m, to.x * inv * step, to.z * inv * step);
                }
            } else if (dist <= S->attack) {
                m->state = E_ATTACK;
                m->timer = 0.0f;
                m->swung = 0;
            } else {
                move_toward(l, S, m, to.x * inv * step, to.z * inv * step);
            }
            break;
        }

        case E_ATTACK:
            m->timer += dt;
            if (!m->swung && m->timer >= S->windup) {
                m->swung = 1;
                if (S->shot_speed > 0.0f) {
                    if (can_see(l, m, player_eye)) {
                        v3 from = v3f(m->pos.x, m->pos.y + S->eye, m->pos.z);
                        v3 at   = v3f(player_eye.x,
                                      player_eye.y - PLAYER_EYE * 0.35f,
                                      player_eye.z);
                        shot_fire(from, at, S->shot_speed, S->damage);
                        play_at(m->pos, "ecast", 90);
                    }
                } else if (dist <= S->attack + 0.3f) {
                    player_damage += S->damage;
                    play_at(m->pos, "eatt", 90);
                }
            }
            if (m->timer >= S->windup + S->cooldown) {
                if (S->shot_speed > 0.0f)      m->state = E_CHASE;
                else if (dist <= S->attack)  { m->timer = 0.0f; m->swung = 0; }
                else                           m->state = E_CHASE;
            }
            break;

        case E_HURT:
            m->timer -= dt;
            if (m->timer <= 0.0f) m->state = E_CHASE;
            break;

        default: break;
        }

        float f, c;
        if (level_ground(l, m->pos.x, m->pos.z, m->pos.y, S->height / 3.0f, &f, &c)) {
            if (m->pos.y > f + 0.01f) {
                m->vel_y -= 22.0f * dt;
                m->pos.y += m->vel_y * dt;
                if (m->pos.y <= f) { m->pos.y = f; m->vel_y = 0.0f; }
            } else {
                m->pos.y = f; m->vel_y = 0.0f;
            }
        }
    }

    return player_damage;
}

int enemy_hitscan(v3 o, v3 d, float maxdist, float *out_t, int *out_idx) {
    float best = maxdist;
    int   hit = -1;

    for (int i = 0; i < g_count; i++) {
        const Enemy *m = &g_enemies[i];
        if (!m->active || m->state == E_DEAD) continue;
        const MonType *S = &TYPES[m->type];

        float ex = o.x - m->pos.x, ez = o.z - m->pos.z;
        float a = d.x*d.x + d.z*d.z;
        if (a < 1e-6f) continue;
        float b = 2.0f * (ex*d.x + ez*d.z);
        float cc = ex*ex + ez*ez - S->radius*S->radius;
        float disc = b*b - 4.0f*a*cc;
        if (disc < 0.0f) continue;

        float t = (-b - sqrtf(disc)) / (2.0f * a);
        if (t < 0.0f) t = (-b + sqrtf(disc)) / (2.0f * a);
        if (t < 0.0f || t >= best) continue;

        float y = o.y + d.y * t;
        if (y < m->pos.y || y > m->pos.y + S->height) continue;

        best = t; hit = i;
    }

    if (hit < 0) return 0;
    *out_t = best; *out_idx = hit;
    return 1;
}

void enemy_hurt(int idx, int dmg, v3 dir) {
    (void)dir;
    if (idx < 0 || idx >= g_count) return;
    Enemy *m = &g_enemies[idx];
    if (!m->active || m->state == E_DEAD) return;

    m->health -= dmg;
    m->flash = 1.0f;

    if (m->health <= 0) {
        m->state = E_DEAD;
        m->timer = 0.6f;
        play_at(m->pos, "edie", 95);
        return;
    }

    play_at(m->pos, "epain", 70);
    if (m->state == E_CHASE || m->state == E_IDLE) {
        m->state = E_HURT;
        m->timer = 0.16f;
    }
}
