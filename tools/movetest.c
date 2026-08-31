/* movetest -- step the player simulation against sector geometry.
 *
 * Movement bugs are impossible to pin down by walking around and trivial to
 * see by dropping a simulated player onto geometry and printing where they
 * end up.
 *
 * The rules are checked against a level built here, not against `arena`.
 * `arena` is a map somebody edits, and every edit used to break assertions
 * that named its coordinates -- which teaches you to ignore the suite rather
 * than read it. The shipped level still gets a smoke test at the end, but one
 * that asks only what must be true of any level.
 */

#include <stdio.h>
#include <math.h>
#include "player.h"
#include "level.h"

#define DT (1.0f / 60.0f)

static int fails;
static Level L;

static void check(int ok, const char *what, float got, float want) {
    printf("  %-46s got %8.3f  want %8.3f  %s\n",
           what, got, want, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static void run(Player *p, v3 wish, float speed, int jump, int frames) {
    for (int i = 0; i < frames; i++)
        player_move(p, &L, wish, speed, jump, DT);
}

/* Appends an axis-aligned sector, in centimetres. */
static void box(Level *l, short x0, short z0, short x1, short z1,
                short floor, short ceil) {
    Sector *s = &l->sectors[l->n_sectors++];
    short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4;
    s->floor = floor;
    s->ceil  = ceil;
}

/* The fixture:  a 24m room at height 0, with
     - a 0.45m platform to the west, low enough to step onto
     - a 2.60m ledge to the north, far too high
   Heights are chosen either side of PLAYER_STEP so the two cases cannot
   accidentally swap places if that constant is retuned. */
static void build(void) {
    Level zero = {0};
    L = zero;
    box(&L, -1200, -1200,  1200,  1200,    0, 600);   /* room     */
    box(&L,  -800,  -600,  -400,  -200,   45, 600);   /* platform */
    box(&L,  -200,   600,   600,  1000,  260, 600);   /* ledge    */
    L.start[0] = 0; L.start[1] = 0; L.start[2] = 0;
}

int main(void) {
    printf("movetest\n\n");
    build();

    const float FLOOR = 0.0f, PLAT = 0.45f, LEDGE = 2.60f;

    check(PLAT < PLAYER_STEP && LEDGE > PLAYER_STEP,
          "fixture: one step in reach, one out of it", PLAYER_STEP, PLAYER_STEP);

    /* --- spawn puts you on the floor, not through it --- */
    {
        Player p = {0};
        player_spawn(&p, &L);
        check(fabsf(p.pos.y - (FLOOR + PLAYER_EYE)) < 0.05f,
              "spawn: standing on the room floor", p.pos.y, FLOOR + PLAYER_EYE);
    }

    /* --- the reported bug: dropping onto a platform must not shove you --- */
    {
        Player p = {0};
        p.pos = v3f(-6.0f, PLAT + PLAYER_EYE + 1.5f, -4.0f);   /* above it */
        float x0 = p.pos.x, z0 = p.pos.z;
        run(&p, v3f(0, 0, 0), 0.0f, 0, 120);                   /* fall, no input */

        float drift = sqrtf((p.pos.x-x0)*(p.pos.x-x0) + (p.pos.z-z0)*(p.pos.z-z0));
        check(drift < 0.001f, "land on platform: no sideways shove", drift, 0.0f);
        check(fabsf(p.pos.y - (PLAT + PLAYER_EYE)) < 0.02f,
              "land on platform: standing on its top", p.pos.y, PLAT + PLAYER_EYE);
        check(p.grounded == 1, "land on platform: grounded", (float)p.grounded, 1.0f);
    }

    /* --- a step within reach is walked up ---
       The height *reached*, not the height at some chosen frame: the platform
       is only 4m deep, so a walk long enough to be sure of arriving is also
       long enough to cross it and step back down the far side. Getting that
       wrong once already cost a false failure. */
    {
        Player p = {0};
        p.pos = v3f(-6.0f, PLAYER_EYE, -8.0f);     /* room floor, south of it */
        float top = p.pos.y;
        for (int i = 0; i < 120; i++) {
            run(&p, v3f(0, 0, 1), PLAYER_WALK, 0, 1);
            if (p.pos.y > top) top = p.pos.y;
        }
        check(fabsf(top - (PLAT + PLAYER_EYE)) < 0.05f,
              "low step: walked up onto it", top, PLAT + PLAYER_EYE);
    }

    /* --- a wall taller than step height stops you --- */
    {
        Player p = {0};
        p.pos = v3f(2.0f, PLAYER_EYE, 2.0f);       /* room, south of the ledge */
        run(&p, v3f(0, 0, 1), PLAYER_WALK, 0, 120);
        check(p.pos.y < PLAYER_EYE + 0.02f,
              "tall ledge: not climbed", p.pos.y, PLAYER_EYE);
        check(p.pos.z < 6.0f - PLAYER_RADIUS + 0.05f,
              "tall ledge: stopped outside it", p.pos.z, 6.0f - PLAYER_RADIUS);
    }

    /* --- you cannot walk out of the map --- */
    {
        Player p = {0};
        player_spawn(&p, &L);
        run(&p, v3f(0, 0, 1), PLAYER_WALK, 0, 240);   /* walk hard at a wall */
        float f, c;
        check(level_ground(&L, p.pos.x, p.pos.z, p.pos.y - PLAYER_EYE, 1e9f, &f, &c) != 0,
              "walk into a wall: still inside the map", 1.0f, 1.0f);
    }

    /* --- the shipped level, asked only what any level must satisfy --- */
    if (!level_load("lqdm4", &L)) {
        printf("\n  no level 'arena' to smoke test\n");
    } else {
        printf("\n  smoke testing '%s' (%d sectors)\n", L.name, L.n_sectors);
        Player p = {0};
        player_spawn(&p, &L);
        unsigned rng = 987654321u;
        int escaped = 0, sunk = 0;
        for (int i = 0; i < 4000; i++) {
            rng = rng * 1664525u + 1013904223u;
            float a = (rng >> 8) * (6.2831853f / 16777216.0f);
            run(&p, v3f(cosf(a), 0, sinf(a)), PLAYER_WALK * 1.8f,
                (rng & 0x400000) != 0, 1);

            float f, c;
            if (!level_ground(&L, p.pos.x, p.pos.z, p.pos.y - PLAYER_EYE, 1e9f, &f, &c))
                escaped = 1;
            else if (p.pos.y - PLAYER_EYE < f - 0.05f)
                sunk = 1;
        }
        check(!escaped, "4000 random frames: never left the map", (float)escaped, 0.0f);
        check(!sunk,    "4000 random frames: never sank through a floor",
              (float)sunk, 0.0f);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall movement checks passed\n", fails);
    return fails != 0;
}
