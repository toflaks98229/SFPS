/* pickuptest -- step pickup collection with no window.
 *
 * "Does walking over it actually give me anything, and does it stop giving
 * once taken?" is invisible from inside the game until you happen to test the
 * exact spot at the exact health. Here it is a handful of assertions.
 */

#include <stdio.h>
#include <math.h>
#include "pickup.h"
#include "pools.h"
#include "player.h"
#include "level.h"
#include "weapon.h"       /* WEAPON_MAX_AMMO */

#define DT (1.0f / 60.0f)

/* The pools this file drives. Owned here the way a ::World owns its own --
   these were file-scope arrays inside pickup.c, so a case inherited whatever
   the previous one left standing. See pools.h.
   이 파일이 구동하는 풀입니다. ::World가 자기 것을 소유하듯 이곳에서 소유합니다. 이것들은
   pickup.c 안의 파일 스코프 배열이었으므로, 한 사례가 이전 사례가 남긴 것을 그대로
   물려받았습니다. pools.h를 참조하십시오. */
static Pools g_pools;

static int fails;
static Level L;

static void ok(int cond, const char *what) {
    printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okf(int cond, const char *what, float got, float want) {
    printf("  %-52s %8.3f / %8.3f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okd(int cond, const char *what, int got, int want) {
    printf("  %-52s %8d / %8d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* A flat room with an ammo box at the origin and a medkit to the east. */
static void build(void) {
    Level z = {0}; L = z;
    Sector *s = &L.sectors[L.n_sectors++];
    short p[8] = { -1000,-1000, 1000,-1000, 1000,1000, -1000,1000 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4; s->floor = 0; s->ceil = 600;

    Entity *a = &L.ents[L.n_ents++];
    a->kind[0]='a';a->kind[1]='m';a->kind[2]='m';a->kind[3]='o';a->kind[4]=0;
    a->x = 0; a->z = 0;
    Entity *h = &L.ents[L.n_ents++];
    h->kind[0]='h';h->kind[1]='e';h->kind[2]='a';h->kind[3]='l';h->kind[4]='t';h->kind[5]='h';h->kind[6]=0;
    h->x = 500; h->z = 0;
    /* A monster entity must NOT become a pickup. */
    Entity *m = &L.ents[L.n_ents++];
    m->kind[0]='i';m->kind[1]='m';m->kind[2]='p';m->kind[3]=0;
    m->x = -500; m->z = 0;
}

/* Eye position for a player standing at (x,z). */
static v3 eye_at(float x, float z) { return v3f(x, PLAYER_EYE, z); }

/* A player carrying the shotgun with `n` shells and nothing else.
 *
 * pickup_update takes the whole Weapon now: a box names the belt it fills, and
 * a weapon lying on the floor fills none of them. Passing one ammo pointer
 * could not express either.
 *
 * pickup_update가 이제 Weapon 전체를 받습니다. 상자는 자신이 채우는 탄약고를 지목하고,
 * 바닥의 무기는 그중 어느 것도 채우지 않습니다. 탄약 포인터 하나로는 둘 다 표현할 수
 * 없었습니다. */
static Weapon armed(int n) {
    Weapon w = {0};
    w.cur = WP_SHOTGUN;
    w.owned[WP_SHOTGUN] = 1;
    w.ammo[WP_SHOTGUN]  = n;
    return w;
}

int main(void) {
    printf("pickuptest\n\n");
    build();

    pickup_spawn_level(&g_pools, &L);
    ok(pickup_count(&g_pools) == 2, "two pickups spawned (the imp is not one)");

    /* --- standing away from anything collects nothing --- */
    {
        int hp = 50, keys = KEY_NONE; Weapon w = armed(5);
        pickup_update(&g_pools, eye_at(50.0f, 50.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(hp == 50 && w.ammo[WP_SHOTGUN] == 5, "far from every pickup, nothing is taken");
        ok(pickup_count(&g_pools) == 2, "and none are consumed");
    }

    /* --- walking onto the ammo box adds shells and consumes it --- */
    {
        int hp = 50, keys = KEY_NONE; Weapon w = armed(5);
        pickup_update(&g_pools, eye_at(0.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        okf(w.ammo[WP_SHOTGUN] == 5 + wp_stats(WP_SHOTGUN)->pickup_ammo,
            "ammo box gives shells", (float)w.ammo[WP_SHOTGUN],
            (float)(5 + wp_stats(WP_SHOTGUN)->pickup_ammo));
        ok(hp == 50, "and does not touch health");
        /* Standing on the now-empty spot gives nothing more. */
        int a2 = w.ammo[WP_SHOTGUN];
        pickup_update(&g_pools, eye_at(0.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(w.ammo[WP_SHOTGUN] == a2, "the collected box gives nothing the second time");
    }

    /* --- the medkit heals, and is left behind at full health --- */
    {
        pickup_spawn_level(&g_pools, &L);            /* fresh */
        int hp = PLAYER_MAX_HP, keys = KEY_NONE; Weapon w = armed(5);

        /* At full health, the medkit must be ignored and remain. */
        pickup_update(&g_pools, eye_at(5.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(hp == PLAYER_MAX_HP, "a medkit at full health heals nothing");
        int live = 0; for (int i = 0; i < pickup_count(&g_pools); i++)
            if (pickup_at(&g_pools, i)->active) live++;
        ok(live == 2, "and is left on the floor to come back for");

        /* Hurt, then walk over it: it heals, capped at max. */
        hp = PLAYER_MAX_HP - 10;
        pickup_update(&g_pools, eye_at(5.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(hp == PLAYER_MAX_HP, "hurt, the medkit heals but does not overfill");
    }

    /* --- ammo is capped too --- */
    {
        pickup_spawn_level(&g_pools, &L);
        int hp = 50, keys = KEY_NONE; Weapon w = armed(wp_stats(WP_SHOTGUN)->max_ammo);
        pickup_update(&g_pools, eye_at(0.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(w.ammo[WP_SHOTGUN] == wp_stats(WP_SHOTGUN)->max_ammo,
           "a full belt ignores the ammo box");
        int live = 0; for (int i = 0; i < pickup_count(&g_pools); i++)
            if (pickup_at(&g_pools, i)->active) live++;
        ok(live == 2, "and leaves it on the floor");
    }

    /* --- every kind is reachable by name, and no two share one ------------
       The sprite decoder addresses a pickup cell by the SAME name a level uses
       to place one, so this resolver is now load-bearing in two directions. A
       kind no name reaches is a drawing that can never be shown; two names
       reaching one kind is a drawing that silently replaces another.

       The `<weapon>ammo` before `<weapon>` ordering is the interesting case:
       `rapid` is a prefix of `rapidammo`, so the wrong order hands a level
       that asked for a box of ammunition a free weapon instead.

       스프라이트 디코더가 레벨이 아이템을 배치할 때 쓰는 것과 *같은* 이름으로 아이템
       셀을 지정하므로, 이 해석기는 이제 양방향으로 중요합니다. 어떤 이름도 닿지 못하는
       종류는 결코 보일 수 없는 그림이고, 두 이름이 한 종류에 닿으면 한 그림이 다른
       그림을 조용히 대체합니다. */
    {
        static const char *NAMES[] = {
            "health", "ammo",
            "shotgun", "grenade", "rapid", "axe",
            "shotgunammo", "grenadeammo", "rapidammo", "axeammo",
            "redkey", "bluekey", "yellowkey",
        };
        const int n = (int)(sizeof NAMES / sizeof NAMES[0]);

        int unresolved = 0, collided = 0;
        int seen[PK_KINDS];
        for (int i = 0; i < PK_KINDS; i++) seen[i] = 0;

        for (int i = 0; i < n; i++) {
            int len = 0; while (NAMES[i][len]) len++;
            int k = pickup_kind_for_n(NAMES[i], len);
            if (k < 0) { unresolved++; printf("      '%s' resolves to nothing\n", NAMES[i]); }
            else if (seen[k]++) { collided++; printf("      '%s' collides\n", NAMES[i]); }
        }
        okd(unresolved == 0, "every pickup name resolves to a kind", unresolved, 0);
        okd(collided == 0, "and no two names reach the same kind", collided, 0);

        /* PK_AMMO is the shotgun's box under its old name, so it is the one
           kind two names legitimately reach -- asserted rather than left as a
           hole in the count above. */
        int a = pickup_kind_for_n("ammo", 4);
        int b = pickup_kind_for_n("shotgunammo", 11);
        ok(a == PK_AMMO && b == PK_AMMO_FOR(WP_SHOTGUN),
           "the legacy 'ammo' and 'shotgunammo' are separate kinds");

        ok(pickup_kind_for_n("nosuchitem", 10) < 0,
           "and an unknown name resolves to nothing");

        /* The length is honoured, not the terminator: the decoder hands this a
           slice of one big text blob, so a resolver that ran to the NUL would
           match whatever followed the name. */
        ok(pickup_kind_for_n("healthXX", 6) == PK_HEALTH,
           "a name is matched by its length, not by a terminator");
        ok(pickup_kind_for_n("health", 3) < 0,
           "and a prefix of a name is not that name");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall pickup checks passed\n", fails);
    return fails != 0;
}
