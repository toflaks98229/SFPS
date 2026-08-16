/* decaltest -- the marks a shot leaves, with no window.
 *
 * These were file-local to weapon.c and drawn straight to the screen, so the
 * only way to check any of this was to fire a shotgun at a wall and look. The
 * two lifetimes had already been wrong once in exactly the way looking does not
 * catch: a blood mark that faded against the WALL constant sat at 9% alpha from
 * the moment it appeared, and its spark was skipped entirely, so the bug read
 * as "hitting a monster feels less punchy than hitting stone".
 *
 * decal.c has no GL in anything but decal_draw, which this never calls.
 */

#include <stdio.h>
#include <math.h>
#include "decal.h"
#include "pools.h"
#include "door.h"     /* the door a mark rides, and how far it has gone */
#include "player.h"   /* PLAYER_EYE -- the height door_update is given */

/* The pools this file drives, owned here the way a ::World owns its own. The
   marks used to be a file-scope array inside decal.c, so a case inherited
   whatever the previous one left on the walls. See pools.h.
   이 파일이 구동하는 풀이며, ::World가 자기 것을 소유하듯 이곳에서 소유합니다. 자국은
   decal.c 안의 파일 스코프 배열이었으므로 한 사례가 이전 사례가 벽에 남긴 것을 그대로
   물려받았습니다. pools.h를 참조하십시오. */
static Pools g_pools;

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okf(int cond, const char *what, float got, float want) {
    printf("  %-56s %8.3f / %8.3f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Close enough for float arithmetic on positions in metres. */
static int near3(v3 a, v3 b) {
    return fabsf(a.x - b.x) < 1e-4f
        && fabsf(a.y - b.y) < 1e-4f
        && fabsf(a.z - b.z) < 1e-4f;
}

int main(void) {
    printf("decaltest -- bullet holes, blood and tracers\n");

    /* --- where a mark goes ------------------------------------------------
       The offset is the whole reason decal_hit reports back: the authored
       effect has to land on the same point, and it cannot if the rule lives in
       two places. */
    printf("\nplacement\n");
    {
        decal_reset(&g_pools);

        v3 end  = v3f(10.0f, 2.0f, 0.0f);
        v3 dir  = v3f(1.0f, 0.0f, 0.0f);      /* travelling +x, into a wall */
        v3 surf = v3f(-1.0f, 0.0f, 0.0f);     /* whose normal faces back at us */

        DecalPlace wall = decal_hit(&g_pools, 0, end, dir, surf, 0);
        ok(near3(wall.p, v3f(10.0f - 0.012f, 2.0f, 0.0f)),
           "a wall mark is nudged off the surface along its normal");
        ok(near3(wall.n, surf), "and faces the way the surface does");

        DecalPlace blood = decal_hit(&g_pools, 0, end, dir, surf, 1);
        ok(near3(blood.p, v3f(10.0f - 0.05f, 2.0f, 0.0f)),
           "blood is pulled back toward the shooter instead");
        ok(near3(blood.n, v3f(-1.0f, 0.0f, 0.0f)),
           "and faces back down the shot, not along the surface");

        okf(decal_live_marks(&g_pools) == 2, "both are in the pool",
            (float)decal_live_marks(&g_pools), 2.0f);
    }

    /* --- the two lifetimes ------------------------------------------------
       A wall does not move and keeps its scars. A monster moves, so a stain
       that outlived it would hang in the air where it used to be standing. */
    printf("\nlifetimes\n");
    {
        decal_reset(&g_pools);
        decal_hit(&g_pools, 0, v3f(0,0,0), v3f(1,0,0), v3f(-1,0,0), 0);   /* wall  */
        decal_hit(&g_pools, 0, v3f(0,0,0), v3f(1,0,0), v3f(-1,0,0), 1);   /* blood */
        okf(decal_live_marks(&g_pools) == 2, "two marks to start",
            (float)decal_live_marks(&g_pools), 2.0f);

        /* Past the blood's life, well short of the wall's. */
        decal_update(&g_pools, 0, DECAL_BLOOD_LIFE + 0.01f);
        okf(decal_live_marks(&g_pools) == 1, "the blood is gone while the body is still under it",
            (float)decal_live_marks(&g_pools), 1.0f);

        decal_update(&g_pools, 0, DECAL_WALL_LIFE);
        okf(decal_live_marks(&g_pools) == 0, "and eventually so is the hole in the wall",
            (float)decal_live_marks(&g_pools), 0.0f);

        ok(DECAL_BLOOD_LIFE > DECAL_SPARK_TIME,
           "a blood mark outlasts the spark that announced it");
        ok(DECAL_BLOOD_LIFE < DECAL_WALL_LIFE,
           "and does not linger like a wall mark");
    }

    /* --- the ring wraps ---------------------------------------------------
       Every trigger pull spawns one per pellet. The pool is meant to overwrite
       oldest-first and cap, not to grow or to start refusing. */
    printf("\nthe ring\n");
    {
        decal_reset(&g_pools);
        for (int i = 0; i < DECAL_MAX_MARKS * 3; i++)
            decal_hit(&g_pools, 0, v3f((float)i, 0, 0), v3f(1,0,0), v3f(-1,0,0), 0);
        okf(decal_live_marks(&g_pools) == DECAL_MAX_MARKS,
            "spawning three times the pool fills it exactly once",
            (float)decal_live_marks(&g_pools), (float)DECAL_MAX_MARKS);

        decal_reset(&g_pools);
        for (int i = 0; i < DECAL_MAX_TRACERS * 3; i++)
            decal_tracer(&g_pools, v3f(0,0,0), v3f((float)i, 0, 0));
        okf(decal_live_tracers(&g_pools) == DECAL_MAX_TRACERS,
            "and the same for tracers",
            (float)decal_live_tracers(&g_pools), (float)DECAL_MAX_TRACERS);
    }

    /* --- tracers are the short-lived half --------------------------------- */
    printf("\ntracers\n");
    {
        decal_reset(&g_pools);
        decal_tracer(&g_pools, v3f(0,0,0), v3f(10,0,0));
        okf(decal_live_tracers(&g_pools) == 1, "a tracer is laid",
            (float)decal_live_tracers(&g_pools), 1.0f);

        decal_update(&g_pools, 0, DECAL_TRACER_LIFE + 0.001f);
        okf(decal_live_tracers(&g_pools) == 0, "and is gone in well under a tenth of a second",
            (float)decal_live_tracers(&g_pools), 0.0f);
    }

    /* --- a level change takes them all --------------------------------------
       A bullet hole belongs to the wall it was shot into, and that wall is
       about to stop existing. */
    printf("\nreset\n");
    {
        decal_reset(&g_pools);
        for (int i = 0; i < 5; i++) {
            decal_hit(&g_pools, 0, v3f((float)i,0,0), v3f(1,0,0), v3f(-1,0,0), i & 1);
            decal_tracer(&g_pools, v3f(0,0,0), v3f((float)i,0,0));
        }
        ok(decal_live_marks(&g_pools) > 0 && decal_live_tracers(&g_pools) > 0, "there is something to clear");

        decal_reset(&g_pools);
        okf(decal_live_marks(&g_pools) == 0, "reset takes every mark",
            (float)decal_live_marks(&g_pools), 0.0f);
        okf(decal_live_tracers(&g_pools) == 0, "and every tracer",
            (float)decal_live_tracers(&g_pools), 0.0f);
    }

    /* --- ageing is not the same as clearing --------------------------------
       decal_update is handed the world's dt, and a frozen world hands it zero.
       A mark must not expire while the game is paused behind a menu. */
    printf("\na frozen world\n");
    {
        decal_reset(&g_pools);
        decal_hit(&g_pools, 0, v3f(0,0,0), v3f(1,0,0), v3f(-1,0,0), 1);   /* the short one */
        for (int i = 0; i < 600; i++) decal_update(&g_pools, 0, 0.0f);
        okf(decal_live_marks(&g_pools) == 1, "ten seconds of zero dt ages nothing",
            (float)decal_live_marks(&g_pools), 1.0f);
    }

    /* --- everything above ran with no decal_init and no GL context ---------
       Which is the point of the split: the simulation half of this module is
       reachable without a window. */
    /* --- a mark on a door rides the door -----------------------------------
       decal.h's note on DECAL_BLOOD_LIFE already says a decal does not follow
       what it hit, and answers it for monsters by making blood last half a
       second. A bullet hole cannot take that answer: DECAL_WALL_LIFE is six
       seconds and a door opens in under one, so shoot a door, open it, and the
       mark hangs in the air where the door used to be.

       THE FIXTURE IS A SECTOR DOOR because it can be built here in ten lines.
       The brush model reaches the same code through the same two calls --
       level_door_at answers for both and door_travel does not know which it is
       -- so what is checked below is the mechanism rather than one model's
       version of it.

       decal.h의 DECAL_BLOOD_LIFE 설명이 데칼은 맞은 대상을 따라가지 않는다고 이미 말하며,
       몬스터에 대해서는 혈흔을 반 초만 남기는 것으로 답합니다. 탄흔은 그 답을 쓸 수 없습니다.
       DECAL_WALL_LIFE가 6초이고 문은 1초 안에 열리므로, 문을 쏘고 열면 자국이 문이 있던 자리의
       허공에 남습니다.

       픽스처가 섹터 문인 이유는 이곳에서 열 줄로 지을 수 있기 때문입니다. 브러시 모델도 같은 두
       호출을 통해 같은 코드에 도달합니다. level_door_at이 양쪽에 답하고 door_travel은 어느
       쪽인지 모릅니다. 따라서 아래에서 검사하는 것은 한 모델의 판본이 아니라 기구 자체입니다. */
    printf("\na mark on a door rides it\n");
    {
        static Level L;
        Level zero = {0};
        L = zero;

        Sector *room = &L.sectors[L.n_sectors++];
        short rp[8] = { -2000, -2000,  2000, -2000,  2000, 2000,  -2000, 2000 };
        for (int i = 0; i < 8; i++) room->pts[i] = rp[i];
        room->n = 4; room->floor = 0; room->ceil = 600;
        level_bounds(room);

        /* Declared second, so it owns the step it shares with the room. */
        Sector *d = &L.sectors[L.n_sectors++];
        short dp[8] = { -200, -200,  200, -200,  200, 200,  -200, 200 };
        for (int i = 0; i < 8; i++) d->pts[i] = dp[i];
        d->n = 4; d->floor = 0; d->ceil = 20;
        level_bounds(d);

        DoorDef *def = &L.doors[L.n_doors++];
        def->sector = 1; def->axis = DOOR_UP;
        def->amount = 300; def->speed = 400;

        level_grid_build(&L);
        door_reset(&L);

        decal_reset(&g_pools);

        /* A shot into the leaf's south face, which faces -z out of the door.
           Hit exactly on the surface, the way a trace reports one.
           문에서 -z 방향으로 향한 문짝의 남쪽 면에 대한 사격입니다. 판정이 보고하는 방식대로
           표면 위에 정확히 맞습니다. */
        v3 hit  = v3f(0.0f, 3.0f, -2.0f);
        v3 dir  = v3f(0.0f, 0.0f, -1.0f);
        v3 surf = v3f(0.0f, 0.0f, -1.0f);

        ok(level_door_at(&L, hit, surf) == 0,
           "a hit on the leaf is recognised as being on the door");

        DecalPlace at = decal_hit(&g_pools, &L, hit, dir, surf, 0);
        v3 spawned = at.p;

        /* Nothing has moved yet, so nothing should move. */
        decal_update(&g_pools, &L, 0.0f);
        ok(near3(g_pools.decal.marks[0].p, spawned),
           "and does not drift while the door is shut");

        /* Open it. door_update is what advances the travel the mark rides. */
        for (int i = 0; i < 30; i++)
            door_update(&L, v3f(0.0f, PLAYER_EYE, 2.6f), KEY_NONE, 1.0f / 60.0f);

        float t = door_openness(&L, 0);
        ok(t > 0.05f, "the door actually moved");

        decal_update(&g_pools, &L, 0.0f);

        v3 want = v3add(spawned, door_travel(&L, 0, t));
        ok(near3(g_pools.decal.marks[0].p, want),
           "the mark has travelled exactly as far as the door");
        okf(fabsf(g_pools.decal.marks[0].p.y - spawned.y) > 0.05f,
            "which is somewhere it was not", g_pools.decal.marks[0].p.y, spawned.y);

        /* Shut again: the mark comes back to where it was made. */
        for (int i = 0; i < 900 && door_openness(&L, 0) > 0.0f; i++) {
            door_update(&L, v3f(0.0f, PLAYER_EYE, 18.0f), KEY_NONE, 1.0f / 60.0f);
            decal_update(&g_pools, &L, 0.0f);
        }
        okf(door_openness(&L, 0) == 0.0f, "the door shut again",
            door_openness(&L, 0), 0.0f);
        ok(near3(g_pools.decal.marks[0].p, spawned),
           "and the mark is back where the shot put it");

        /* A hit on the room's own wall is nobody's door. */
        v3 wall = v3f(0.0f, 3.0f, -20.0f);
        ok(level_door_at(&L, wall, v3f(0, 0, -1)) < 0,
           "a hit on a wall that does not move is attached to nothing");
    }

    printf("\nheadless\n");
    ok(1, "spawn, age, wrap and reset all ran with no GL context");

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall decal checks passed\n", fails);
    return fails != 0;
}
