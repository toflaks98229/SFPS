/**
 * @file proj.c
 * @brief Player projectile flight, bouncing, fuses and blasts. No GL.
 *
 * ENGLISH
 * -------
 * See proj.h for why this is separate from enemy.c's Shot.
 *
 * 한국어
 * ------
 * enemy.c의 Shot과 분리한 이유는 proj.h를 참조하십시오.
 */

#include "proj.h"
#include "pools.h"   /* the bundle the calls below are handed */
#include "enemy.h"
#include "audio.h"
#include "fx.h"
#include "diag.h"
#include <math.h>

void proj_reset(Pools *pl) {
    for (int i = 0; i < PROJ_MAX; i++) pl->proj.p[i].active = 0;
}

int proj_count(const Pools *pl) { (void)pl; return PROJ_MAX; }

const Proj *proj_at(const Pools *pl, int i) {
    return (i >= 0 && i < PROJ_MAX) ? &pl->proj.p[i] : 0;
}

int proj_live(const Pools *pl) {
    int n = 0;
    for (int i = 0; i < PROJ_MAX; i++) if (pl->proj.p[i].active) n++;
    return n;
}

int proj_fire(Pools *pl, v3 from, v3 dir, float speed, float gravity,
              int damage, float blast, float fuse) {
    Proj *p = 0;
    for (int i = 0; i < PROJ_MAX; i++)
        if (!pl->proj.p[i].active) { p = &pl->proj.p[i]; break; }

    /* Every other pool here reports when it turns something away, and a shot
       that produced no projectile is indistinguishable from one that missed --
       the animation and the sound play either way. Costs nothing in release.
       이 프로젝트의 다른 모든 풀은 무언가를 거절할 때 보고합니다. 발사체를 만들어 내지
       못한 사격은 빗나간 사격과 구분되지 않습니다. 어느 쪽이든 애니메이션과 소리는
       재생되기 때문입니다. 릴리스에서는 비용이 없습니다. */
    if (!p) { DIAG(DIAG_SHOT_CAP); return 0; }

    float len = v3len(dir);
    if (len < 1e-6f) return 0;
    dir = v3scale(dir, 1.0f / len);

    p->pos     = from;
    p->vel     = v3scale(dir, speed);
    p->gravity = gravity;
    p->damage  = damage;
    p->blast   = blast;
    p->fuse    = fuse;
    p->life    = 6.0f;
    p->spin    = 0.0f;
    p->active  = 1;
    return 1;
}

int proj_blast(Pools *pl, v3 at, float radius, int damage) {
    if (radius <= 0.0f) return 0;
    int hits = 0;

    for (int i = 0; i < enemy_count(pl); i++) {
        const Enemy *m = enemy_at(pl, i);
        if (!m || !m->active || m->state == E_DEAD) continue;

        /* Measured to the monster's MIDDLE, not its feet. A blast beside a
           brute's boots should not count as further away than one beside its
           head, and the feet are what `pos` holds.
           발이 아니라 몬스터의 *몸통 중심*까지 잽니다. 브루트의 발 옆에서 터진 폭발이
           머리 옆에서 터진 것보다 멀다고 계산되어서는 안 되며, `pos`가 담고 있는 것이
           발입니다. */
        const MonType *S = mon_stats(m->type);
        v3 mid = v3f(m->pos.x, m->pos.y + S->height * 0.5f, m->pos.z);
        v3 d   = v3sub(mid, at);
        float dist = v3len(d);
        if (dist > radius) continue;

        /* Linear falloff to zero at the rim, with a floor of one point so a
           monster inside the radius is never told it was hit for nothing.
           A quadratic curve was the alternative and it makes the rim useless:
           past about 60% of the radius it rounds to zero, so the blast reads
           as much smaller than it draws.
           가장자리에서 0이 되는 선형 감쇠이며, 반경 안의 몬스터가 피해 0을 통보받지
           않도록 최소 1을 보장합니다. 이차 곡선이 대안이었지만 가장자리를 쓸모없게
           만듭니다. 반경의 약 60%를 넘으면 0으로 반올림되어, 폭발이 그려지는 것보다 훨씬
           작게 느껴집니다. */
        float t = 1.0f - dist / radius;
        int dmg = (int)(damage * t + 0.5f);
        if (dmg < 1) dmg = 1;

        /* Pushed outward from the blast, so the spray leaves the body away
           from where the explosion was. */
        v3 away = dist > 1e-4f ? v3scale(d, 1.0f / dist) : v3f(0, 1, 0);
        enemy_hurt(pl, i, dmg, away);
        hits++;
    }
    return hits;
}

/* A projectile's end: the blast if it has one, otherwise a single-target hit
   that the caller has already applied.
   발사체의 최후입니다. 폭발 반경이 있으면 폭발이고, 없으면 호출자가 이미 적용한 단일
   대상 피격입니다. */
static void detonate(Pools *pl, Proj *p, v3 at, v3 normal) {
    if (p->blast > 0.0f) {
        proj_blast(pl, at, p->blast, p->damage);

        /* THE DOME IS SCALED BY THE RADIUS IT IS DRAWING. Its speed is
           authored so speed x life reaches one metre, so passing the blast
           radius makes the shell stop exactly where the damage does -- the
           point of drawing it at all is that the radius is a gameplay number
           and a player who cannot see it is guessing.
           Spawned along +Y rather than the surface normal: a blast is a
           hemisphere standing on the ground, and one leaning off a wall's
           normal would claim a shape the damage does not have.
           돔은 자신이 그리는 반경으로 배율이 정해집니다. speed x life가 1미터에 닿도록
           작성했으므로 폭발 반경을 넘기면 껍질이 데미지가 멈추는 바로 그 자리에서
           멈춥니다. 애초에 이것을 그리는 이유가, 반경이 게임플레이 수치이고 그것을 볼 수
           없는 플레이어는 짐작하게 되기 때문입니다. 표면 법선이 아니라 +Y로 생성하는
           이유는, 폭발이 지면에 선 반구이고 벽의 법선을 따라 기울어진 돔은 데미지가 갖지
           않은 모양을 주장하기 때문입니다. */
        fx_spawn_scaled(pl, "blastdome", at, v3f(0, 1, 0), p->blast);

        fx_spawn(pl, "blastcore",   at, normal);
        fx_spawn(pl, "blastsmoke",  at, v3f(0, 1, 0));
        fx_spawn(pl, "blastdebris", at, normal);
        fx_spawn(pl, "boltburst",   at, normal);

        /* ITS OWN SOUND, AND FROM WHERE IT HAPPENED. This was `impact`, which
           is DSPUNCH -- a punch -- at a flat gain of 100 wherever in the level
           it went off. Two faults in one line: the wrong sound, and a blast
           across the map as loud as one at your feet. A grenade you cannot
           place by ear is one you cannot learn to avoid.
           자기 소리이며, 일어난 자리에서 납니다. 이것은 `impact`, 즉 DSPUNCH(주먹질)였고,
           레벨 어디서 터지든 고정 게인 100이었습니다. 한 줄에 결함이 둘입니다. 틀린
           소리, 그리고 맵 건너편의 폭발이 발밑의 폭발과 같은 크기라는 것. 귀로 위치를
           짚을 수 없는 유탄은 피하는 법을 배울 수 없는 유탄입니다. */
        audio_play_at("blast", 100, at);
    } else {
        fx_spawn(pl, "spark", at, normal);
        audio_play_at("impact", 45, at);
    }
    p->active = 0;
}

void proj_update(Pools *pl, const Level *l, float dt) {
    for (int i = 0; i < PROJ_MAX; i++) {
        Proj *p = &pl->proj.p[i];
        if (!p->active) continue;

        p->spin += dt;
        p->life -= dt;
        if (p->life <= 0.0f) { p->active = 0; continue; }

        /* The fuse burns whether or not the grenade is moving, which is what
           makes one resting at your feet a threat rather than scenery.
           도화선은 유탄이 움직이든 아니든 탑니다. 발밑에 멈춰 있는 것이 배경이 아니라
           위협이 되는 이유입니다. */
        if (p->fuse > 0.0f) {
            p->fuse -= dt;
            if (p->fuse <= 0.0f) { detonate(pl, p, p->pos, v3f(0, 1, 0)); continue; }
        }

        if (p->gravity > 0.0f) p->vel.y -= p->gravity * dt;

        v3 step = v3scale(p->vel, dt);
        float dist = v3len(step);
        if (dist < 1e-6f) continue;
        v3 dir = v3scale(step, 1.0f / dist);

        /* --- monsters first, along the whole step ------------------------
           Swept, because at 70 m/s a bolt crosses more than a metre in a frame
           and a monster standing between this frame's position and the next
           would otherwise be passed straight through.
           한 스텝 전체에 걸쳐 훑습니다. 70m/s의 탄은 한 프레임에 1미터 이상 이동하므로,
           이번 프레임 위치와 다음 위치 사이에 선 몬스터를 그대로 통과하게 됩니다. */
        float et; int eidx;
        if (enemy_hitscan(pl, p->pos, dir, dist + PROJ_RADIUS, &et, &eidx)) {
            v3 at = v3add(p->pos, v3scale(dir, et));
            if (p->blast <= 0.0f) enemy_hurt(pl, eidx, p->damage, dir);
            detonate(pl, p, at, v3scale(dir, -1.0f));
            continue;
        }

        /* --- then the level ---------------------------------------------- */
        float t; v3 n;
        if (level_trace(l, p->pos, dir, dist + PROJ_RADIUS, &t, &n)) {
            v3 at = v3add(p->pos, v3scale(dir, t));

            /* A bolt stops at the wall; a grenade bounces off it. `gravity` is
               what tells them apart -- see Proj.
               탄은 벽에서 멈추고 유탄은 튕깁니다. 둘을 가르는 것은 `gravity`입니다. */
            if (p->gravity <= 0.0f) { detonate(pl, p, at, n); continue; }

            /* Reflect, damped. Backed off along the normal so the grenade does
               not begin the next step inside the surface it just left, which
               reports an immediate hit at zero range and pins it to the wall.
               감쇠된 반사입니다. 법선 방향으로 약간 물러나게 하여, 유탄이 방금 떠난 표면
               안에서 다음 스텝을 시작하지 않게 합니다. 그 경우 거리 0에서 즉시 충돌이
               보고되어 벽에 붙어 버립니다. */
            float vn = v3dot(p->vel, n);
            p->vel = v3sub(p->vel, v3scale(n, 2.0f * vn));
            p->vel = v3scale(p->vel, PROJ_BOUNCE);
            p->pos = v3add(at, v3scale(n, PROJ_RADIUS));

            /* Below a crawl it has stopped, and a grenade that keeps micro-
               bouncing rattles for the rest of its fuse.
               기어가는 수준 이하이면 멈춘 것입니다. 계속 미세하게 튕기는 유탄은 남은
               도화선 내내 덜그럭거립니다. */
            if (v3len(p->vel) < 1.2f) p->vel = v3f(0, 0, 0);
            else audio_play("impact", 25);
            continue;
        }

        p->pos = v3add(p->pos, step);
    }
}
