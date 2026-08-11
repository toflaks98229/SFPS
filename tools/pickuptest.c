/* pickuptest -- step pickup collection with no window.
 *
 * "Does walking over it actually give me anything, and does it stop giving
 * once taken?" is invisible from inside the game until you happen to test the
 * exact spot at the exact health. Here it is a handful of assertions.
 */

#include <stdio.h>
#include <math.h>
#include "pickup.h"
#include "player.h"
#include "level.h"
#include "weapon.h"       /* WEAPON_MAX_AMMO */

#define DT (1.0f / 60.0f)

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

    pickup_spawn_level(&L);
    ok(pickup_count() == 2, "two pickups spawned (the imp is not one)");

    /* --- standing away from anything collects nothing --- */
    {
        int hp = 50, keys = KEY_NONE; Weapon w = armed(5);
        pickup_update(eye_at(50.0f, 50.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(hp == 50 && w.ammo[WP_SHOTGUN] == 5, "far from every pickup, nothing is taken");
        ok(pickup_count() == 2, "and none are consumed");
    }

    /* --- walking onto the ammo box adds shells and consumes it --- */
    {
        int hp = 50, keys = KEY_NONE; Weapon w = armed(5);
        pickup_update(eye_at(0.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        okf(w.ammo[WP_SHOTGUN] == 5 + wp_stats(WP_SHOTGUN)->pickup_ammo,
            "ammo box gives shells", (float)w.ammo[WP_SHOTGUN],
            (float)(5 + wp_stats(WP_SHOTGUN)->pickup_ammo));
        ok(hp == 50, "and does not touch health");
        /* Standing on the now-empty spot gives nothing more. */
        int a2 = w.ammo[WP_SHOTGUN];
        pickup_update(eye_at(0.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(w.ammo[WP_SHOTGUN] == a2, "the collected box gives nothing the second time");
    }

    /* --- the medkit heals, and is left behind at full health --- */
    {
        pickup_spawn_level(&L);            /* fresh */
        int hp = PLAYER_MAX_HP, keys = KEY_NONE; Weapon w = armed(5);

        /* At full health, the medkit must be ignored and remain. */
        pickup_update(eye_at(5.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(hp == PLAYER_MAX_HP, "a medkit at full health heals nothing");
        int live = 0; for (int i = 0; i < pickup_count(); i++)
            if (pickup_at(i)->active) live++;
        ok(live == 2, "and is left on the floor to come back for");

        /* Hurt, then walk over it: it heals, capped at max. */
        hp = PLAYER_MAX_HP - 10;
        pickup_update(eye_at(5.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(hp == PLAYER_MAX_HP, "hurt, the medkit heals but does not overfill");
    }

    /* --- ammo is capped too --- */
    {
        pickup_spawn_level(&L);
        int hp = 50, keys = KEY_NONE; Weapon w = armed(wp_stats(WP_SHOTGUN)->max_ammo);
        pickup_update(eye_at(0.0f, 0.0f), &hp, PLAYER_MAX_HP, &w, &keys, DT);
        ok(w.ammo[WP_SHOTGUN] == wp_stats(WP_SHOTGUN)->max_ammo,
           "a full belt ignores the ammo box");
        int live = 0; for (int i = 0; i < pickup_count(); i++)
            if (pickup_at(i)->active) live++;
        ok(live == 2, "and leaves it on the floor");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall pickup checks passed\n", fails);
    return fails != 0;
}
