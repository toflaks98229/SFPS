/* hooktest -- the Meat Hook's four beats, and recoil-jump momentum, headless.
 *
 * The hook is fire -> pull -> impact -> launch, and each beat is asserted
 * separately because each can break without the others noticing. A pull that
 * never arrives still flies; an arrival that deals no damage still launches;
 * a launch that fires on a timeout instead of an arrival looks identical from
 * the outside until you check what caused it.
 *
 * All of it is pure data + maths: wp_hook_fire/wp_hook_update take the level
 * explicitly rather than reading weapon.c's own g_level, and fire()'s recoil
 * kick only ever touches a v3* the caller owns. Neither needs a GL context,
 * so both are driven here the same way enemy.c and pickup.c are.
 *
 * These tests replaced a suite written for the rope-constraint hook that came
 * before. That version held a length and let gravity produce a swing; this
 * one closes distance and bounces you off the far end. The old assertions --
 * "the rope holds its length", "gravity becomes an arc" -- are not weaker
 * versions of the ones here, they are assertions about a different mechanic,
 * so they were removed rather than adapted.
 */

#include <stdio.h>
#include <math.h>
#include "player.h"
#include "weapon.h"
#include "hook.h"
#include "level.h"
#include "enemy.h"
/* The ribbon checks at the end build real MeshBuf geometry, so this one
   genuinely needs the renderer's CPU-side half. level.h no longer supplies it
   transitively -- it forward-declares MeshBuf so the simulation headers stay
   free of the GL stack -- so the dependency is stated here instead. */
#include "render.h"
#include "pools.h"


/* The pools a run spawns into. A test owns one the same way a ::World does --
   these used to be file-scope arrays inside their own modules, so a fixture
   inherited whatever the previous case left in them. See pools.h.
   플레이가 생성해 넣는 풀들입니다. ::World가 그러하듯 테스트도 자기 것을 소유합니다.
   이것들은 각자의 모듈 안 파일 스코프 배열이었으므로, 픽스처는 이전 사례가 남긴 것을
   그대로 물려받았습니다. pools.h를 참조하십시오. */
static Pools g_pools;

#define DT (1.0f / 60.0f)

static int fails;
static void ok(int cond, const char *what) {
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okf(int cond, const char *what, float got, float want) {
    printf("  %-56s %8.3f / %8.3f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Names an entity, because three fixtures here need the same monster and a
   kind written out character by character is a kind that goes stale silently
   when the bestiary loses a row. `imp` was three assignments in three places.
   엔티티에 이름을 붙입니다. 이곳의 픽스처 셋이 같은 몬스터를 필요로 하는데, 한 글자씩 적어 둔
   종류 이름은 도감이 행을 잃을 때 조용히 낡습니다. `imp`는 세 곳에서 세 번의 대입이었습니다. */
static void put_kind(Entity *e, const char *kind) {
    int i = 0;
    while (kind[i] && i < LVL_KIND - 1) { e->kind[i] = kind[i]; i++; }
    e->kind[i] = 0;
}

static void box(Level *l, short x0, short z0, short x1, short z1,
               short floor, short ceil) {
    Sector *s = &l->sectors[l->n_sectors++];
    short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4; s->floor = floor; s->ceil = ceil;
}

/* Runs the hook to completion (or to the frame cap) from a standing start,
   reporting how it ended. Every pull test needs this loop, and writing it
   once keeps the ordering -- hook first, then integrate -- consistent with
   how main.c actually drives it.
   서 있는 상태에서 훅을 완료(또는 프레임 상한)까지 실행하고 종료 방식을 보고합니다.
   모든 견인 테스트가 이 루프를 필요로 하며, 한 번만 작성함으로써 main.c가 실제로
   구동하는 순서(훅 먼저, 그다음 적분)를 일관되게 유지합니다. */
static int run_hook(Weapon *w, const Level *l, v3 *pos, v3 *vel, int max_frames) {
    int frames = 0;
    while (w->hook_state != HOOK_IDLE && frames < max_frames) {
        wp_hook_update(w, &g_pools, l, pos, vel, DT);
        /* Gravity and integration, as player_move would apply them. */
        vel->y -= PLAYER_GRAVITY * DT;
        *pos = v3add(*pos, v3scale(*vel, DT));
        frames++;
    }
    return frames;
}

int main(void) {
    printf("hooktest\n\n");

    /* A tall room. Sector units are centimetres, so this is 40m square and
       30m tall.

       Every fixture below keeps the player INSIDE this box. That is not
       incidental: level_trace hits instantly from a point outside the
       geometry, so a fixture placed above the ceiling attaches to itself at
       zero range. An early version of this suite did exactly that, and the
       resulting failures pointed at the physics when the fixture was what was
       wrong. */
    Level L = {0};
    box(&L, -2000, -2000, 2000, 2000, 0, 3000);
    L.start[0] = 0; L.start[1] = 0; L.start[2] = 0;

    /* Head height for a player standing in the middle of that room. */
    #define STAND_Y 15.0f

    /* --- beat 1: the claw is a projectile, not a raycast -------------------
       The whole reason wp_hook_fire takes no level: nothing is resolved when
       it returns. The claw has to travel. */
    {
        Weapon w = {0};
        v3 eye = v3f(0.0f, STAND_Y, 0.0f);

        ok(wp_hook_fire(&w, eye, 0.0f, 0.0f) == 1, "the press throws the claw");
        okf(w.hook_state == HOOK_FLYING, "which starts out flying, not attached",
            (float)w.hook_state, (float)HOOK_FLYING);

        /* The wall is 20m away at 90 m/s, so it cannot possibly have arrived
           after one 60Hz frame. If this ever passes instantly, the projectile
           has silently become a raycast again. */
        v3 pos = eye, vel = v3f(0,0,0);
        wp_hook_update(&w, &g_pools, &L, &pos, &vel, DT);
        ok(w.hook_state == HOOK_FLYING, "and is still in the air one frame later");
        ok(v3len(v3sub(w.hook_pos, eye)) > 0.5f, "having actually travelled");
        ok(v3len(vel) < 1e-4f, "and the player has not moved -- flight pulls nothing");
    }

    /* --- the claw reaches a wall and starts pulling ------------------------ */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);

        int frames = 0;
        while (w.hook_state == HOOK_FLYING && frames < 600) {
            wp_hook_update(&w, &g_pools, &L, &pos, &vel, DT);
            frames++;
        }
        ok(w.hook_state == HOOK_PULLING, "the claw reaches the wall and latches");
        ok(w.hook_enemy < 0, "with no monster hooked -- it hit geometry");
        okf(fabsf(w.hook_target.z - (-20.0f)) < 0.6f,
            "landing on the wall it actually hit", w.hook_target.z, -20.0f);
        /* 20m at 90 m/s is ~0.22s, about 13 frames. Being wildly off means
           the flight speed is not what the constant says. */
        okf(frames < 30, "in roughly the time the flight speed implies",
            (float)frames, 30.0f);

        /* --- the winch loop retriggers, and does not run free -------------
           The reel is the only sustained sound here, and the mixer has no
           loop: it is a 110ms recipe restarted on a timer. Two ways that goes
           wrong and neither makes a noise a test could hear, so the timer is
           checked instead.

           Firing every frame would push 60 voices a second into a 12-voice
           mixer and evict every other sound for the length of the pull. Never
           firing leaves the pull silent. What must hold is that the timer
           counts down and rearms -- a bounded number of triggers over a known
           span.
           감기 소리는 이곳의 유일한 지속음이며 믹서에는 루프가 없습니다. 110ms 레시피를
           타이머로 재시작하는 방식입니다. 잘못되는 경우가 둘인데 어느 쪽도 테스트가
           들을 수 있는 소리를 내지 않으므로, 대신 타이머를 검사합니다. */
        {
            /* Count the frames on which the timer was DUE, which is exactly
               the frames a trigger happens on. Watching the timer rise instead
               does not work and was the first attempt: firing every frame
               resets it every frame, so it never appears to jump relative to
               the previous sample and the broken version scored ONE. The
               condition the code itself branches on is the honest thing to
               sample.
               타이머가 *만료된* 프레임을 셉니다. 이것이 정확히 트리거가 발생하는
               프레임입니다. 타이머가 올라가는 것을 보는 방식은 동작하지 않으며 그것이
               첫 시도였습니다. 매 프레임 발사하면 타이머도 매 프레임 재설정되므로 이전
               표본 대비 상승으로 보이지 않고, 고장 난 버전이 1점을 받았습니다. 코드
               자신이 분기하는 조건을 표본으로 삼는 것이 정직한 방법입니다. */
            int triggers = 0, pulled = 0;
            float prev = -1.0f;
            for (int i = 0; i < 60 && w.hook_state == HOOK_PULLING; i++) {
                wp_hook_update(&w, &g_pools, &L, &pos, &vel, DT);
                if (w.hook_state != HOOK_PULLING) break;
                pulled++;
                /* Sample AFTER the update. wp_hook_update decrements and
                   rearms in the same call, so reading the timer before it runs
                   only ever sees the rearmed value and scores one trigger for
                   the whole pull however the code behaves -- which is what the
                   first two attempts at this check did.
                   갱신 *이후에* 표본을 얻습니다. wp_hook_update는 같은 호출 안에서
                   감소와 재장전을 모두 수행하므로, 실행 전에 타이머를 읽으면 재장전된
                   값만 보게 되어 코드가 어떻게 동작하든 견인 전체에서 트리거 1회로
                   집계됩니다. 이 검사의 첫 두 시도가 바로 그랬습니다. */
                if (prev >= 0.0f && w.hook_reel_timer > prev) triggers++;
                prev = w.hook_reel_timer;
            }

            ok(pulled > 10, "the pull lasts long enough to need a loop at all");
            ok(triggers > 0, "the winch loop actually retriggers while reeling");
            /* Firing every frame would give one per frame; the interval caps
               it near pulled*DT/HOOK_REEL_INTERVAL, which is well under half. */
            okf(triggers < pulled / 2,
                "and is paced rather than fired every frame",
                (float)triggers, (float)(pulled / 2));
        }
    }

    /* --- a throw into empty space misses and gives up ---------------------- */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);
        /* No level at all: nothing to hit, so the claw runs out of range. */
        wp_hook_fire(&w, pos, 0.0f, 0.0f);
        int frames = 0;
        while (w.hook_state != HOOK_IDLE && frames < 600) {
            wp_hook_update(&w, &g_pools, 0, &pos, &vel, DT);
            frames++;
        }
        ok(w.hook_state == HOOK_IDLE, "a throw that hits nothing ends on its own");
        ok(v3len(vel) < 1e-4f, "and never moves the player");
    }

    /* --- beat 2: the pull actually closes the distance ---------------------
       THE property that makes this a Meat Hook rather than the rope it
       replaced. A rope holds its length; this one must not. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);

        float start_dist = 20.0f;
        run_hook(&w, &L, &pos, &vel, 60 * 8);

        float end_dist = fabsf(pos.z - (-20.0f));
        ok(end_dist < start_dist * 0.25f,
           "the pull closes the distance -- a winch, not a rope");
        ok(w.hook_state == HOOK_IDLE, "and the cycle completes on arrival");
    }

    /* --- the pull cancels gravity, so a long hook does not sag -------------
       Without this a horizontal throw drops into the floor before arriving,
       which reads as the hook failing rather than as physics.

       The bound is a comparison against free fall rather than a flat number,
       because some sag is correct and unavoidable: the claw does NOT hold the
       player up while it flies, so they carry whatever downward velocity that
       ~0.2 s of falling gave them into the pull, and cancelling gravity from
       that point stops the fall getting worse without undoing it. Measured
       here: 0.56 m during flight, 1.79 m more while the pull arrests it.

       An earlier version of this test asserted a flat 1.5 m and failed at
       2.34 m -- the assertion was wrong, not the code. What actually matters
       is that the total is nowhere near what free fall over the same duration
       would produce, which is what this now checks. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);

        float lowest = pos.y;
        int frames = 0;
        while (w.hook_state != HOOK_IDLE && frames < 60 * 8) {
            wp_hook_update(&w, &g_pools, &L, &pos, &vel, DT);
            vel.y -= PLAYER_GRAVITY * DT;
            pos = v3add(pos, v3scale(vel, DT));
            if (pos.y < lowest) lowest = pos.y;
            frames++;
        }

        float sag  = STAND_Y - lowest;
        float secs = frames * DT;
        float free_fall = 0.5f * PLAYER_GRAVITY * secs * secs;

        okf(sag < free_fall * 0.5f,
            "a pulled player sags far less than free fall over the same time",
            sag, free_fall * 0.5f);
        ok(sag > 0.0f, "but is not held perfectly level -- flight is unsupported");
    }

    /* --- beat 4: arriving launches the player, automatically ---------------
       No button, no timing. The launch is the transition out of the pull, so
       an arrival that does not launch is a broken hook. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);
        run_hook(&w, &L, &pos, &vel, 60 * 8);

        ok(vel.y > 1.0f, "arriving launches the player upward, with no input");
        ok(w.hook_state == HOOK_IDLE, "and the hook has released itself");
        okf(v3len(vel) <= HOOK_LAUNCH_MAX + 0.01f,
            "the launch is capped, not a catapult", v3len(vel), HOOK_LAUNCH_MAX);
    }

    /* --- the launch keeps travel that is not into the surface --------------
       Straight up would stop a chain dead above each target, so the launch
       preserves part of the approach. But it must preserve only the part that
       can actually be travelled.

       This assertion used to read `vel.z < -0.5` on a hook fired dead-on at a
       wall -- that is, it required the launch to keep driving the player INTO
       the wall they had just arrived at. It passed, and the behaviour it was
       protecting was the bug: player_move refuses that move every frame, so
       the momentum neither travelled nor decayed and the player juddered
       against the surface at 20 m/s. Measured before the fix, the launch came
       out at 24.4 m/s with 13.3 of it aimed at the wall and position frozen at
       z=-19.57 while vel.z held at -20.5.

       So a dead-on hook now launches straight up: there IS no approach
       direction to keep that is not into the wall. What has to survive is
       sideways travel, which is what actually chains hooks together, and that
       is checked below.
       이 단언은 원래 벽에 정면으로 쏜 훅에 대해 `vel.z < -0.5`를 요구했습니다. 즉 도약이
       방금 도달한 벽 *안쪽으로* 플레이어를 계속 밀어 넣기를 요구한 것입니다. 통과했지만
       그것이 보호하던 동작이 곧 버그였습니다. 정면 훅은 이제 수직으로 도약합니다. 벽
       안쪽이 아니면서 유지할 만한 접근 방향이 존재하지 않기 때문입니다. 살아남아야 하는
       것은 실제로 훅을 연결해 주는 측면 이동이며, 아래에서 검사합니다. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);
        run_hook(&w, &L, &pos, &vel, 60 * 8);

        ok(vel.y > 1.0f, "a dead-on hook launches, upward");
        okf(vel.z > -0.5f,
            "and keeps nothing aimed at the wall it just arrived at",
            vel.z, 0.0f);
    }

    /* --- sideways travel DOES survive the launch ---------------------------
       The half of the rule the assertion above cannot check. A hook taken
       while moving across the wall has travel that is genuinely free to
       continue, and dropping it would make every launch identical and kill
       the chaining the ALONG term exists for.
       위의 단언이 검사할 수 없는 나머지 절반입니다. 벽을 가로지르며 걸린 훅은 실제로
       계속 진행할 수 있는 이동 성분을 가지며, 이를 버리면 모든 도약이 똑같아져
       ALONG 항이 존재하는 이유인 연결이 사라집니다. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f);
        v3 vel = v3f(12.0f, 0.0f, 0.0f);       /* moving across the wall */
        wp_hook_fire(&w, pos, 0.0f, 0.0f);
        run_hook(&w, &L, &pos, &vel, 60 * 8);

        ok(vel.x > 0.2f,
           "travel across the surface is carried into the launch");
    }

    /* ================= beat 3: hooking a monster is an attack =============
       The beat with no other symptom. A hook that flies, pulls and launches
       correctly but deals no damage looks completely normal in motion -- the
       only way to see it is to check the target's health. */

    /* Put a monster between the player and the north wall. Entity coordinates
       are centimetres, and "water_spirit" is the kind name mon_type_for
       recognises -- the baseline, and one that stands on the floor, which is
       what a claw fired level at 1.7m expects to find. */
    {
        Level M = {0};
        box(&M, -2000, -2000, 2000, 2000, 0, 3000);
        M.start[0] = 0; M.start[1] = 0; M.start[2] = 0;
        M.n_ents = 1;
        put_kind(&M.ents[0], "water_spirit");
        M.ents[0].x = 0; M.ents[0].z = -1000;      /* 10 m ahead */

        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &M);
        ok(enemy_count(&g_pools) == 1, "the fixture spawned exactly one monster");

        const Enemy *e = enemy_at(&g_pools, 0);
        int hp_before = e ? e->health : 0;
        ok(hp_before > 0, "and it starts alive");

        Weapon w = {0};
        /* Aim at the monster's centre of mass, not its feet. */
        v3 pos = v3f(0.0f, 1.7f, 0.0f), vel = v3f(0,0,0);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);

        /* Fly only, so the hit is attributable to the claw rather than to
           anything the pull might do. */
        int frames = 0;
        while (w.hook_state == HOOK_FLYING && frames < 600) {
            wp_hook_update(&w, &g_pools, &M, &pos, &vel, DT);
            frames++;
        }
        ok(w.hook_state == HOOK_PULLING, "the claw latches onto the monster");
        okf(w.hook_enemy == 0, "recording WHICH monster, not just a point",
            (float)w.hook_enemy, 0.0f);

        /* Damage is dealt on ARRIVAL, not on the hit -- so nothing yet. */
        e = enemy_at(&g_pools, 0);
        okf(e && e->health == hp_before,
            "latching alone deals no damage -- the impact is on arrival",
            e ? (float)e->health : -1.0f, (float)hp_before);

        run_hook(&w, &M, &pos, &vel, 60 * 8);

        e = enemy_at(&g_pools, 0);
        ok(e && e->health < hp_before, "arriving hurts the monster it hooked");
        okf(e && hp_before - e->health == HOOK_IMPACT_DAMAGE,
            "by exactly HOOK_IMPACT_DAMAGE, once -- not per frame of contact",
            e ? (float)(hp_before - e->health) : -1.0f, (float)HOOK_IMPACT_DAMAGE);
        ok(vel.y > 1.0f, "and still launches, the same as hooking geometry");

        enemy_reset(&g_pools);
    }

    /* --- hooking geometry damages nothing ---------------------------------
       A wall is a valid anchor, so the hook must not report a monster hit
       that never happened. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);
        enemy_reset(&g_pools);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);
        run_hook(&w, &L, &pos, &vel, 60 * 8);
        ok(w.hook_enemy < 0, "a wall hook never claims a monster target");
    }

    /* --- a target that dies mid-pull ends the hook without a launch --------
       There is nothing left to bounce off. Tracking the target by INDEX
       rather than by position is what makes this detectable at all. */
    {
        Level M = {0};
        box(&M, -2000, -2000, 2000, 2000, 0, 3000);
        M.n_ents = 1;
        put_kind(&M.ents[0], "water_spirit");
        M.ents[0].x = 0; M.ents[0].z = -1500;

        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &M);

        Weapon w = {0};
        v3 pos = v3f(0.0f, 1.7f, 0.0f), vel = v3f(0,0,0);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);
        while (w.hook_state == HOOK_FLYING)
            wp_hook_update(&w, &g_pools, &M, &pos, &vel, DT);
        ok(w.hook_state == HOOK_PULLING, "hooked a monster for the death test");

        /* Kill it outright, mid-pull. */
        const Enemy *e = enemy_at(&g_pools, 0);
        if (e) enemy_hurt(&g_pools, 0, e->health + 100, v3f(0,0,-1));

        v3 vel_before = vel;
        wp_hook_update(&w, &g_pools, &M, &pos, &vel, DT);
        ok(w.hook_state == HOOK_IDLE, "the hook ends when its target dies");
        okf(fabsf(vel.y - vel_before.y) < 1e-3f,
            "with no launch -- there was nothing left to bounce off",
            vel.y, vel_before.y);

        enemy_reset(&g_pools);
    }

    /* --- a pull that cannot finish ends, and ends QUICKLY -------------------
       This used to be a pure timeout test: hold the player still, watch the
       hook give up after HOOK_PULL_TIMEOUT. It still ends, but now it ends in
       about a fifth of a second instead of three seconds, because a player who
       is not moving is a player the pull has stopped closing on -- which is
       the same condition the floor case hits.

       That is the intended behaviour and worth stating rather than hiding: the
       timeout is now the last resort for a pull that is still making progress
       but will never get there (a target running away), while anything that
       has genuinely stopped resolves in HOOK_STALL_TIME.
       이전에는 순수한 시간 초과 테스트였습니다. 플레이어를 고정해 두고 훅이
       HOOK_PULL_TIMEOUT 후 포기하는지 확인했습니다. 여전히 끝나기는 하지만 이제 3초가
       아니라 약 0.2초 만에 끝납니다. 움직이지 않는 플레이어는 견인이 더 이상 좁히지
       못하는 플레이어이며, 이는 바닥 사례가 부딪히는 것과 동일한 조건입니다.

       이는 의도된 동작이므로 숨기지 않고 명시합니다. 시간 초과는 이제 여전히 전진하고
       있지만 결코 도달하지 못할 견인(도망치는 대상)을 위한 최후의 수단이며, 실제로 멈춘
       것은 HOOK_STALL_TIME 안에 해결됩니다. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);
        wp_hook_fire(&w, pos, 0.0f, 0.0f);
        while (w.hook_state == HOOK_FLYING)
            wp_hook_update(&w, &g_pools, &L, &pos, &vel, DT);

        /* Hold the player still: the pull can never close the distance. */
        v3 fixed = pos;
        int frames = 0;
        while (w.hook_state != HOOK_IDLE && frames < 60 * 20) {
            v3 scratch = fixed;
            v3 v2 = v3f(0,0,0);
            wp_hook_update(&w, &g_pools, &L, &scratch, &v2, DT);
            frames++;
        }
        ok(w.hook_state == HOOK_IDLE, "a pull that cannot arrive gives up");
        okf(frames < 60 * (HOOK_PULL_TIMEOUT + 1.0f),
            "within the timeout rather than hanging forever",
            (float)frames, 60 * (HOOK_PULL_TIMEOUT + 1.0f));
        /* The point of the stall detector: a pull going nowhere must not make
           the player sit through the full timeout. */
        okf(frames < 60 * 0.6f,
            "and well inside it -- a stalled pull resolves, it does not wait",
            (float)frames, 60 * 0.6f);
    }

    /* --- a hook at the FLOOR completes instead of timing out ----------------
       Reported from play: firing down at the ground left the player waiting
       for the loop to end. The cause is geometric, not a tuning miss -- the
       pull measures from the EYE and the player stands on the floor, so the
       distance bottoms out at PLAYER_EYE (1.70m) against a HOOK_ARRIVE_DIST of
       1.60m and stops changing. Measured before the fix: 1.70m reached in
       0.75s, then 2.25s of nothing until the timeout.

       Asserted as a DURATION rather than as "did it arrive", because timing
       out also ends the hook -- the old behaviour would satisfy any check that
       only asked whether the state went idle.
       플레이에서 보고된 문제입니다. 바닥을 향해 쏘면 플레이어가 루프가 끝나기를 기다려야
       했습니다. 원인은 튜닝 실수가 아니라 기하학적인 것입니다. "도달했는가"가 아니라
       *소요 시간*으로 단언하는 이유는, 시간 초과 역시 훅을 끝내기 때문입니다. 이전
       동작도 상태가 유휴가 되었는지만 묻는 검사라면 통과했을 것입니다. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);

        /* Straight down. STAND_Y is 15m up in a 30m room, so there is floor
           below and the claw has something to bite. */
        wp_hook_arm(&w);
        ok(wp_hook_fire(&w, pos, 0.0f, -1.5f), "a hook fired at the floor throws");

        int frames = 0, pull_frames = 0;
        while (w.hook_state != HOOK_IDLE && frames < 60 * 10) {
            int pulling = (w.hook_state == HOOK_PULLING);
            wp_hook_update(&w, &g_pools, &L, &pos, &vel, DT);
            if (pulling) pull_frames++;
            /* Integrate and stand on the floor, as player_move would -- the
               floor is the whole reason the distance stops shrinking. */
            vel.y -= PLAYER_GRAVITY * DT;
            pos = v3add(pos, v3scale(vel, DT));
            float fl, ce;
            if (level_ground(&L, pos.x, pos.z, pos.y - PLAYER_EYE, 1e9f, &fl, &ce) &&
                pos.y - PLAYER_EYE < fl) {
                pos.y = fl + PLAYER_EYE;
                if (vel.y < 0.0f) vel.y = 0.0f;
            }
            frames++;
        }

        ok(w.hook_state == HOOK_IDLE, "and the hook ends rather than hanging");
        okf(pull_frames < 60 * (HOOK_PULL_TIMEOUT - 0.5f),
            "well before the timeout -- the player is not made to wait",
            pull_frames * DT, HOOK_PULL_TIMEOUT - 0.5f);
    }

    /* ================= range, aim lock and the range indicator ============ */

    /* --- the range indicator agrees with what a throw would actually do ----
       The whole value of the crosshair brackets is that they never disagree
       with the launcher. Both use HOOK_RANGE and both trace from the eye, so
       "the indicator is lit" and "a throw connects" have to be the same
       answer -- otherwise the UI teaches the player something false. */
    {
        Weapon w = {0};
        enemy_reset(&g_pools);

        /* Facing the north wall, 20 m away: well inside the 40 m range. */
        v3 eye = v3f(0.0f, STAND_Y, 0.0f);
        ok(wp_hook_in_range(&w, &g_pools, &L, eye, 0.0f, 0.0f),
           "a wall inside HOOK_RANGE lights the indicator");

        /* And a throw from the same place does connect. */
        Weapon w2 = {0};
        v3 pos = eye, vel = v3f(0,0,0);
        wp_hook_fire(&w2, pos, 0.0f, 0.0f);
        int frames = 0;
        while (w2.hook_state == HOOK_FLYING && frames < 600) {
            wp_hook_update(&w2, &g_pools, &L, &pos, &vel, DT);
            frames++;
        }
        ok(w2.hook_state == HOOK_PULLING,
           "and a throw from the same spot really does connect");
    }

    /* --- out of range reads as out of range -------------------------------
       With no level there is nothing to hit at any distance, which is the
       cleanest way to express "past the range" without building a second
       fixture 40 m across. */
    {
        Weapon w = {0};
        enemy_reset(&g_pools);
        ok(!wp_hook_in_range(&w, &g_pools, 0, v3f(0.0f, STAND_Y, 0.0f), 0.0f, 0.0f),
           "nothing in range leaves the indicator dark");
    }

    /* --- the indicator reports "would this work", not "is a wall there" ----
       A crosshair that lights up while the launcher cannot fire is lying
       about what the button will do. Every refusal wp_hook_fire applies has
       to darken it too. */
    {
        Weapon w = {0};
        enemy_reset(&g_pools);
        v3 eye = v3f(0.0f, STAND_Y, 0.0f);

        w.hook_cooldown = 0.5f;
        ok(!wp_hook_in_range(&w, &g_pools, &L, eye, 0.0f, 0.0f),
           "on cooldown, the indicator stays dark despite the wall");

        w.hook_cooldown = 0.0f;
        w.hook_latched  = 1;
        ok(!wp_hook_in_range(&w, &g_pools, &L, eye, 0.0f, 0.0f),
           "and while the press is still latched");

        w.hook_latched = 0;
        ok(wp_hook_in_range(&w, &g_pools, &L, eye, 0.0f, 0.0f),
           "but lights again once the launcher is genuinely ready");
    }

    /* --- a monster lights it too, and takes priority over the wall --------- */
    {
        Level M = {0};
        box(&M, -2000, -2000, 2000, 2000, 0, 3000);
        M.n_ents = 1;
        put_kind(&M.ents[0], "water_spirit");
        M.ents[0].x = 0; M.ents[0].z = -1000;

        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &M);

        Weapon w = {0};
        ok(wp_hook_in_range(&w, &g_pools, &M, v3f(0.0f, 1.7f, 0.0f), 0.0f, 0.0f),
           "a monster in range lights the indicator as well");
        enemy_reset(&g_pools);
    }

    /* --- the aim is locked for the whole cycle ----------------------------
       Flight and pull both lock it, for different reasons: turning mid-flight
       swings the tether behind the player while the claw carries on, and
       turning mid-pull invites fighting a movement the player cannot steer. */
    {
        Weapon w = {0};
        v3 pos = v3f(0.0f, STAND_Y, 0.0f), vel = v3f(0,0,0);

        ok(!wp_hook_locks_aim(&w), "an idle hook leaves the aim free");

        wp_hook_fire(&w, pos, 0.0f, 0.0f);
        ok(wp_hook_locks_aim(&w), "throwing locks it immediately");

        while (w.hook_state == HOOK_FLYING)
            wp_hook_update(&w, &g_pools, &L, &pos, &vel, DT);
        ok(w.hook_state == HOOK_PULLING && wp_hook_locks_aim(&w),
           "and it stays locked through the pull, not just the flight");

        run_hook(&w, &L, &pos, &vel, 60 * 8);
        ok(!wp_hook_locks_aim(&w), "the aim is free again once the hook ends");
    }

    /* --- a cancelled hook releases the aim, or the player is stuck --------- */
    {
        Weapon w = {0};
        wp_hook_fire(&w, v3f(0.0f, STAND_Y, 0.0f), 0.0f, 0.0f);
        wp_hook_release(&w);
        ok(!wp_hook_locks_aim(&w), "cancelling releases the aim lock too");
    }

    /* ================= fire rate ==========================================
       Two limits that stop two different things: the cooldown paces repeated
       PRESSES, the latch stops a single HELD press repeating. A cooldown
       alone would only slow a held button to one claw every HOOK_COOLDOWN. */

    /* --- holding the button is exactly one throw --------------------------- */
    {
        Weapon w = {0};
        v3 eye = v3f(0.0f, STAND_Y, 0.0f);

        ok(wp_hook_fire(&w, eye, 0.0f, 0.0f), "the press throws");
        wp_hook_release(&w);                 /* end that hook */

        int refires = 0;
        for (int i = 0; i < 60 * 10; i++) {
            w.hook_cooldown -= DT;           /* as wp_update ticks it */
            if (wp_hook_fire(&w, eye, 0.0f, 0.0f)) refires++;
        }
        okf(refires == 0,
            "holding it down for ten seconds never throws a second claw",
            (float)refires, 0.0f);

        wp_hook_arm(&w);
        for (int i = 0; i < 60; i++) w.hook_cooldown -= DT;
        ok(wp_hook_fire(&w, eye, 0.0f, 0.0f),
           "but letting go and pressing again does throw");
    }

    /* --- one claw in the air at a time ------------------------------------- */
    {
        Weapon w = {0};
        v3 eye = v3f(0.0f, STAND_Y, 0.0f);
        wp_hook_fire(&w, eye, 0.0f, 0.0f);
        wp_hook_arm(&w);                     /* pretend the button was released */
        w.hook_cooldown = 0.0f;              /* and the cooldown expired */
        ok(wp_hook_fire(&w, eye, 0.0f, 0.0f) == 0,
           "a second throw is refused while one is still in the air");
    }

    /* --- a miss still spends the cooldown ---------------------------------- */
    {
        Weapon w = {0};
        v3 eye = v3f(0.0f, STAND_Y, 0.0f);
        wp_hook_fire(&w, eye, 0.0f, 0.0f);
        ok(w.hook_cooldown > 0.0f, "throwing spends the cooldown immediately");
    }

    /* --- a completed hook costs more than a bare throw ---------------------
       Chaining should be a decision, not a held button. */
    {
        Weapon a = {0}, b = {0};
        v3 eye = v3f(0.0f, STAND_Y, 0.0f);

        wp_hook_fire(&a, eye, 0.0f, 0.0f);   /* thrown, still flying */
        wp_hook_fire(&b, eye, 0.0f, 0.0f);
        wp_hook_release(&b);                 /* ...and completed */

        okf(b.hook_cooldown > a.hook_cooldown,
            "finishing a hook rearms slower than merely throwing one",
            b.hook_cooldown, a.hook_cooldown);
    }

    /* --- cancelling is safe from any state --------------------------------- */
    {
        Weapon w = {0};
        v3 eye = v3f(0.0f, STAND_Y, 0.0f);

        wp_hook_release(&w);                 /* already idle */
        ok(w.hook_state == HOOK_IDLE, "cancelling an idle hook is harmless");

        wp_hook_fire(&w, eye, 0.0f, 0.0f);
        wp_hook_release(&w);                 /* mid-flight */
        ok(w.hook_state == HOOK_IDLE, "and cancels a claw in flight");

        /* A cancel is not an arrival, so it must not launch. */
        v3 vel = v3f(0,0,0);
        wp_hook_release(&w);
        ok(v3len(vel) < 1e-4f, "a cancelled hook launches nothing");
    }

    /* --- the pull converges on the hook's line, whatever you brought to it ---
       Reported from play: firing the hook while already moving sideways
       carried that momentum through the whole pull, so the player flew a curve
       and landed well off the anchor rather than on it.

       The winch only ever ACCELERATED along the hook and capped that component;
       nothing touched the component across it, so entry momentum survived
       untouched to the end. Measured on this 20m fixture before the fix: 12 m/s
       of sideways entry missed the anchor by 3.6m, and 30 m/s -- which a
       chained hook reaches easily, HOOK_LAUNCH_MAX being 30 and airborne drag
       being almost nothing -- missed by 8.5m and took 30% longer to arrive.

       What is asserted here is ARRIVAL ACCURACY, not the shape of the path.
       The pull is still allowed to curve early, and does: HOOK_PULL_STRAIGHTEN
       is a decay rather than a hard zero precisely so a fast entry still reads
       as being yanked. What must not happen is finishing somewhere other than
       the thing you hooked.

       플레이에서 보고된 문제: 이미 측면으로 이동 중일 때 훅을 발사하면 그 운동량이 견인
       내내 유지되어, 플레이어가 곡선을 그리며 목표가 아닌 한참 벗어난 곳에 도달했습니다.
       여기서 단언하는 것은 경로의 *모양*이 아니라 *도달 정확도*입니다. 견인은 초반에
       여전히 휘어져도 되며 실제로 그렇습니다. */
    {
        /* Same entry speeds as the report, including the ones a chained hook
           actually reaches. The bound is generous -- this is not asserting a
           particular curve, only that the hook still puts you at its target. */
        const float SIDE[]   = { 0.0f, 6.0f, 12.0f, 20.0f, 30.0f };
        const float MAX_MISS = 2.5f;

        float worst = 0.0f;
        int   all_arrived = 1;

        for (int i = 0; i < (int)(sizeof(SIDE)/sizeof(SIDE[0])); i++) {
            Weapon w = {0};
            v3 pos = v3f(0.0f, STAND_Y, 0.0f);
            v3 vel = v3f(SIDE[i], 0.0f, 0.0f);      /* pure sideways entry */

            wp_hook_arm(&w);
            if (!wp_hook_fire(&w, pos, 0.0f, 0.0f)) { all_arrived = 0; continue; }

            /* Let the claw fly and bite, remembering where it actually landed.
               The anchor is read from the weapon rather than assumed, so this
               measures the pull and not the aim. */
            v3 anchor = v3f(0,0,0);
            int frames = 0, latched = 0;
            while (w.hook_state != HOOK_IDLE && frames < 600) {
                wp_hook_update(&w, &g_pools, &L, &pos, &vel, DT);
                if (w.hook_state == HOOK_PULLING && !latched) {
                    latched = 1;
                    anchor  = w.hook_target;
                }
                vel.y -= PLAYER_GRAVITY * DT;
                pos = v3add(pos, v3scale(vel, DT));
                frames++;
            }
            if (!latched) { all_arrived = 0; continue; }

            float dx = pos.x - anchor.x, dz = pos.z - anchor.z;
            float miss = sqrtf(dx*dx + dz*dz);
            if (miss > worst) worst = miss;
        }

        ok(all_arrived, "every entry speed throws a claw that bites");
        okf(worst < MAX_MISS,
            "the pull ends at its anchor however fast you entered it",
            worst, MAX_MISS);
    }

    /* --- momentum carries after release, and decays instead of vanishing ---
       `grounded` is not something a test can just assert into truth: it is
       recomputed from the real floor every call, and a player spawned at
       floor height is standing on it whatever the struct literal said a
       moment ago. That is exactly what the first version of this test got
       wrong -- it set grounded=0 by hand on a player standing on solid
       ground, so the very first player_move call put them right back to 1
       and the "airborne" case silently used ground drag instead. Lifting the
       player clear of the floor is what actually exercises air drag. */
    {
        Player p = {0};
        player_spawn(&p, &L);
        p.pos.y += 5.0f;                      /* clear of the floor, genuinely airborne */
        player_impulse(&p, v3f(6.0f, 0.0f, 0.0f));

        float x0 = p.pos.x;
        player_move(&p, &L, v3f(0,0,0), 0.0f, 0, DT);
        float moved_first_frame = p.pos.x - x0;
        ok(moved_first_frame > 0.03f, "an impulse moves the player next frame");
        ok(!p.grounded, "and the fixture is actually airborne, not resting on it");

        /* 30 frames at 5m of clearance: falling under PLAYER_GRAVITY covers
           only ~2.75m in that half-second (0.5*g*t^2), so the fixture is
           still genuinely airborne throughout and ground drag never gets a
           chance to sneak into this measurement the way it did before. */
        float speed0 = fabsf(p.vel.x);
        for (int i = 0; i < 30; i++) player_move(&p, &L, v3f(0,0,0), 0.0f, 0, DT);
        float speed1 = fabsf(p.vel.x);
        ok(!p.grounded, "and still airborne at the end of the measurement");
        ok(speed1 < speed0, "and bleeds off rather than lasting forever");
        /* MOMENTUM_DRAG_AIR is tuned to be nearly nothing -- the whole point
           of the hook and recoil jumping is a fast move that keeps going
           without a fight. This bound is what locks that promise in: it
           replaces a far looser one (speed1 > speed0*0.05) that would still
           have passed even if drag quietly crept back up to "kicks that die
           in a few frames." */
        okf(speed1 > speed0 * 0.90f,
            "and in the air that decay is almost nothing at all",
            speed1, speed0 * 0.90f);
    }

    /* --- ground drag is much harder, so a recoil kick does not leave you
           sliding across the floor indefinitely --------------------------- */
    {
        Player g = {0}, a = {0};
        player_spawn(&g, &L); player_spawn(&a, &L);
        a.pos.y += 5.0f;             /* airborne; g stays on the real floor */
        player_impulse(&g, v3f(6.0f, 0.0f, 0.0f));
        player_impulse(&a, v3f(6.0f, 0.0f, 0.0f));

        for (int i = 0; i < 30; i++) {
            player_move(&g, &L, v3f(0,0,0), 0.0f, 0, DT);
            player_move(&a, &L, v3f(0,0,0), 0.0f, 0, DT);
        }
        ok(g.grounded, "the ground fixture really is grounded throughout");
        ok(!a.grounded, "and the air fixture really is not");
        okf(fabsf(g.vel.x) < fabsf(a.vel.x),
            "so grounded momentum decays faster than airborne momentum",
            fabsf(g.vel.x), fabsf(a.vel.x));
    }

    /* --- firing kicks the player back, harder in the air -------------------- */
    {
        Weapon w = {0}; w.ammo[WP_SHOTGUN] = 5;
        v3 eye = v3f(0, PLAYER_EYE, 0);
        /* Facing -z (yaw 0, pitch 0): the kick should push +z (backward).
           NO LEVEL, which is what these fixtures always had: `Weapon w = {0}`
           left the stored level null and every pellet missed. The level is a
           parameter now, so the same fixture says so out loud -- what is being
           measured is the kick, and a wall for the pellets to hit would only
           add a variable to it.
           레벨이 없으며, 이 픽스처들이 언제나 그러했습니다. `Weapon w = {0}`은 저장된 레벨을
           널로 두었고 모든 산탄이 빗나갔습니다. 이제 레벨이 인자이므로 같은 픽스처가 그것을
           소리 내어 말합니다. 여기서 재는 것은 반동이며, 산탄이 맞을 벽은 변수를 하나 더할
           뿐입니다. */
        v3 vel_ground = v3f(0,0,0), vel_air = v3f(0,0,0);

        wp_update(&w, &g_pools, 0, DT, 1, eye, 0.0f, 0.0f, 0.0f, 0, 0, 1.4f, 1.6f,
                 &vel_ground, 1);
        Weapon w2 = {0}; w2.ammo[WP_SHOTGUN] = 5;
        wp_update(&w2, &g_pools, 0, DT, 1, eye, 0.0f, 0.0f, 0.0f, 0, 0, 1.4f, 1.6f,
                 &vel_air, 0);

        ok(vel_ground.z > 0.01f, "a grounded shot still kicks you back a little");
        ok(vel_air.z > vel_ground.z * 2.0f,
           "an airborne shot kicks much harder -- shotgun jumping");
        okf(fabsf(vel_ground.x) < 0.001f && fabsf(vel_ground.y) < 0.001f,
            "and the kick is purely along the aim, nothing sideways here",
            fabsf(vel_ground.x) + fabsf(vel_ground.y), 0.0f);
    }

    /* --- aiming down and firing launches you up, the rocket-jump trick ------ */
    {
        Weapon w = {0}; w.ammo[WP_SHOTGUN] = 5;
        v3 eye = v3f(0, PLAYER_EYE, 0);
        v3 vel = v3f(0,0,0);
        float look_down = -0.8f;   /* radians; negative is down in this engine */
        wp_update(&w, &g_pools, 0, DT, 1, eye, 0.0f, look_down, 0.0f, 0, 0, 1.4f, 1.6f,
                 &vel, 0);
        ok(vel.y > 0.0f, "shooting while aiming down launches you upward");
    }

    /* --- the tether's ribbon geometry, with no GL context at all -----------
       mb_ribbon is pure CPU-side MeshBuf math, the same as every other mesh
       builder in render.c, so its UVs -- the seam a future rope texture will
       actually depend on -- are checked the same way the geometry tests in
       leveltest check winding: by reading the vertices back out. */
    {
        MeshBuf b;
        mb_init(&b, 8);

        v3 a = v3f(0, 0, 0), c = v3f(4, 0, 0);   /* a 4m segment along +x */
        v3 cam = v3f(0, 5, 0);                   /* looking down at it */
        mb_ribbon(&b, a, c, cam, 0.2f, 3.0f);

        ok(b.count == 6, "a ribbon is one quad, six vertices");

        float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
        for (int i = 0; i < b.count; i++) {
            if (b.v[i].u < umin) umin = b.v[i].u;
            if (b.v[i].u > umax) umax = b.v[i].u;
            if (b.v[i].v < vmin) vmin = b.v[i].v;
            if (b.v[i].v > vmax) vmax = b.v[i].v;
        }
        okf(fabsf(umin) < 1e-5f, "u starts at 0 at the near end", umin, 0.0f);
        okf(fabsf(umax - 3.0f) < 1e-5f,
            "and reaches utile at the far end -- this is what lets a tiling "
            "rope texture repeat with distance", umax, 3.0f);
        okf(fabsf(vmin) < 1e-5f && fabsf(vmax - 1.0f) < 1e-5f,
            "v spans the strip's width, 0 to 1", vmax - vmin, 1.0f);

        /* Every vertex should sit within width/2 of the a-c axis: this is
           what distinguishes a ribbon from mb_billboard's full camera-facing
           rotation, which would NOT keep the strip aligned with the segment
           it is meant to represent. */
        float maxdev = 0.0f;
        for (int i = 0; i < b.count; i++) {
            v3 p = v3f(b.v[i].px, b.v[i].py, b.v[i].pz);
            float t = p.x / 4.0f;                    /* progress along a->c */
            v3 on_axis = v3f(t * 4.0f, 0, 0);
            float dev = v3len(v3sub(p, on_axis));
            if (dev > maxdev) maxdev = dev;
        }
        okf(maxdev <= 0.1f + 1e-4f,
            "every vertex stays within half the width of the segment itself",
            maxdev, 0.1f);

        mb_reset(&b);
        mb_ribbon(&b, a, a, cam, 0.2f, 1.0f);   /* zero-length: a to a */
        ok(b.count == 0, "a zero-length segment degrades to nothing, not a crash");

        mb_free(&b);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall hook/momentum checks passed\n", fails);
    return fails != 0;
}
