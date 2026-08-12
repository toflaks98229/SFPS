/* steptest -- run whole frames of the game with no window.
 *
 * main.c used to say, in its own header comment, that the order the per-frame
 * update ran in was "load-bearing rather than incidental" -- and then nothing
 * checked any of it, because the order lived in the body of WinMain and the
 * only way to run it was to open a window and play. Every other test in this
 * folder drops one module onto a fixture; none of them could reach the frame
 * that puts the modules together.
 *
 * world.c exists so that this file can. It names no GL function, no Win32 call
 * and no menu, so a World can be stepped here exactly as the game steps one.
 * What is asserted below is therefore not "does the player walk" -- movetest
 * owns that -- but "does a frame do its parts in the right order, and does it
 * stop the right things when the world is frozen".
 *
 * The fixtures are built here rather than loaded, for the reason movetest
 * learned: `arena` is a map somebody edits, and a test that names its
 * coordinates goes red on every edit. The one test that does load a level
 * asserts nothing about what is inside it.
 */

#include <stdio.h>
#include <math.h>

#include "world.h"
#include "enemy.h"    /* enemy_reset -- the monster pool is global, and shared */
#include "pickup.h"   /* pickup_spawn_level, for the same reason */
#include "proj.h"     /* proj_reset, likewise */
#include "door.h"     /* door_reset, and the DOOR_* axes */

#define DT     (1.0f / 60.0f)
#define ASPECT 1.7777f          /* 16:9. Only the muzzle solve reads it. */

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okf(int cond, const char *what, float got, float want) {
    printf("  %-58s %8.3f / %8.3f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* ------------------------------------------------------------- fixtures */

/* Sector units are centimetres. `hurt` is damage per second standing on it. */
static void box(Level *l, short x0, short z0, short x1, short z1,
                short floor, short ceil, short hurt) {
    Sector *s = &l->sectors[l->n_sectors++];
    short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4;
    s->floor = floor;
    s->ceil  = ceil;
    s->hurt  = hurt;
    /* level_load does this after parsing; a hand-built sector has to as well,
       or min_x..max_z are zero and the smoke sampler never finds it. */
    level_bounds(s);
}

/* A world mid-run, standing in the middle of one flat 40m room.
 *
 * world_init leaves the run on the title screen, which freezes everything --
 * correct for the game and useless for a test, so the fixture clears it. The
 * entity pools are file-scope globals shared between tests, so they are reset
 * here rather than left holding whatever the previous case put in them. */
static void fixture(World *w, short hurt) {
    world_init(w);
    w->run.title = 0;

    box(&w->level, -2000, -2000, 2000, 2000, 0, 3000, hurt);

    w->player.pos      = v3f(0.0f, PLAYER_EYE, 0.0f);
    w->player.vel      = v3f(0.0f, 0.0f, 0.0f);
    w->player.grounded = 1;
    w->player.health   = PLAYER_MAX_HP;

    enemy_reset();
    proj_reset();
    pickup_spawn_level(&w->level);
    door_reset(&w->level);
}

static Input idle(void) {
    Input in = {0};
    return in;
}

/* ------------------------------------------------------------------ main */

int main(void) {
    printf("steptest\n\n");

    /* --- what a freeze actually stops --------------------------------------
       Four different states mean "the world does not advance", and they have to
       mean it identically. Checking each one separately is the point: a freeze
       that half works -- the player stops but the monsters do not, or the menu
       stops the world but the win screen does not -- is exactly the failure
       world_frozen exists to make impossible. */
    printf("freezing\n");
    {
        struct { const char *name; int title, won, dead, paused; } CASE[] = {
            { "the title screen",   1, 0, 0, 0 },
            { "the win screen",     0, 1, 0, 0 },
            { "the death screen",   0, 0, 1, 0 },
            { "an open menu",       0, 0, 0, 1 },
        };

        for (int c = 0; c < 4; c++) {
            World w;
            fixture(&w, 0);
            Input in = idle();
            in.forward = 1;

            /* Live first, so the fixture is known to be one a player can walk
               out of -- otherwise "did not move" proves nothing. */
            v3 from = w.player.pos;
            int frozen = world_step(&w, &in, ASPECT, DT);
            float walked = v3len(v3sub(w.player.pos, from));
            ok(!frozen && walked > 0.05f, "a live frame walks the player forward");

            w.run.title = CASE[c].title;
            w.run.won   = CASE[c].won;
            w.run.dead  = CASE[c].dead;
            in.paused   = CASE[c].paused;

            from = w.player.pos;
            frozen = world_step(&w, &in, ASPECT, DT);
            float after = v3len(v3sub(w.player.pos, from));

            char what[96];
            snprintf(what, sizeof(what), "%s stops the player dead", CASE[c].name);
            okf(after < 1e-5f, what, after, 0.0f);

            snprintf(what, sizeof(what), "...and the step reports it as frozen");
            ok(frozen != 0, what);
        }
    }

    /* --- the frozen state a step RETURNS is the one it used -----------------
       Not the one that is true afterwards. A frame that kills the player ran
       live -- the damage that killed them was dealt by it -- and the renderer
       has to draw it that way. Re-deriving the flag after the step instead
       hides the crosshair one frame before the death screen it belongs to
       appears, which reads as a dropped frame rather than as a death. */
    printf("\nthe frame a death happens on\n");
    {
        World w;
        fixture(&w, 30000);           /* lava that empties the bar in one frame */
        Input in = idle();

        int frozen = world_step(&w, &in, ASPECT, DT);

        ok(w.player.health == 0,   "a hazard that empties the bar takes it to zero");
        ok(w.run.dead == 1,        "and one place notices, wherever the damage came from");
        ok(frozen == 0,            "the step still reports the frame it ran as LIVE");
        ok(world_frozen(&w, 0) == 1, "even though asking again afterwards says frozen");

        /* The hook is let go on death, or it keeps reeling a corpse across the
           room with the claw still out there. */
        ok(w.weapon.hook_state == HOOK_IDLE, "and death lets go of the grapple");

        /* death_time is zeroed when the death is noticed and then advanced by
           the clocks below it in the same step. If it comes out of the killing
           frame at exactly zero, the clocks are running BEFORE the detection
           and the death screen is a frame late. */
        okf(w.run.death_time > DT * 0.5f && w.run.death_time < DT * 1.5f,
            "the death clock starts on the killing frame, not after it",
            w.run.death_time, DT);
    }

    /* --- hazard floors ------------------------------------------------------ */
    printf("\nhazard floors\n");
    {
        /* A hazard is the FLOOR. Jumping a lava channel, or being pulled across
           it by the hook, has to be a way through -- otherwise the room is a
           wall rather than an obstacle and the momentum systems this game is
           built on have nothing to do there. */
        World w;
        fixture(&w, 100);
        w.player.pos.y = PLAYER_EYE + 6.0f;   /* mid-air over the same lava */
        Input in = idle();

        world_step(&w, &in, ASPECT, DT);
        ok(!w.player.grounded,                 "the fixture really is airborne");
        ok(w.player.health == PLAYER_MAX_HP,   "lava does not burn a player in the air");
    }
    {
        /* One point a second, stepped at 60Hz, is 1/60th of a point per frame.
           Truncated per frame that is zero for ever, and a slow hazard is
           entirely harmless -- which is why the fraction is carried in the run
           rather than discarded. */
        World w;
        fixture(&w, 1);
        Input in = idle();

        for (int i = 0; i < 30; i++) world_step(&w, &in, ASPECT, DT);
        okf(w.player.health == PLAYER_MAX_HP,
            "half a second on 1dps lava has not yet cost a point",
            (float)w.player.health, (float)PLAYER_MAX_HP);

        for (int i = 0; i < 60; i++) world_step(&w, &in, ASPECT, DT);
        okf(w.player.health == PLAYER_MAX_HP - 1,
            "and a second and a half has cost exactly one",
            (float)w.player.health, (float)(PLAYER_MAX_HP - 1));

        /* Walking off it stops the charge rather than banking it. */
        w.player.pos = v3f(0.0f, PLAYER_EYE, 0.0f);
        w.level.sectors[0].hurt = 0;
        for (int i = 0; i < 120; i++) world_step(&w, &in, ASPECT, DT);
        okf(w.player.health == PLAYER_MAX_HP - 1,
            "dry ground clears the accumulator instead of holding it",
            (float)w.player.health, (float)(PLAYER_MAX_HP - 1));
    }

    /* --- clocks that run whether or not the world does ---------------------
       The death and title screens animate while frozen; they are what the
       freeze is FOR. The hurt flash has to fade even if the player opened the
       menu on the frame they were hit, or it stays painted on the screen for as
       long as the menu is up. */
    printf("\nclocks under a freeze\n");
    {
        World w;
        fixture(&w, 0);
        Input in = idle();
        in.paused = 1;

        w.run.dead       = 1;
        w.run.death_time = 0.0f;
        w.run.title      = 1;
        w.run.title_time = 0.0f;
        w.player.hurt    = 1.0f;

        for (int i = 0; i < 6; i++) world_step(&w, &in, ASPECT, DT);

        okf(w.run.death_time > DT * 5.5f, "the death screen animates while stopped",
            w.run.death_time, DT * 6.0f);
        okf(w.run.title_time > DT * 5.5f, "so does the title screen",
            w.run.title_time, DT * 6.0f);
        okf(w.player.hurt < 1.0f, "and the hurt flash still fades out",
            w.player.hurt, 1.0f - DT * 12.0f);
    }

    /* --- the clock the lava flows against ---------------------------------- */
    printf("\nthe world clock\n");
    {
        World w;
        fixture(&w, 0);
        Input in = idle();

        world_step(&w, &in, ASPECT, DT);
        okf(w.run.world_time > 0.0f, "a live frame advances the material clock",
            w.run.world_time, DT);

        in.paused = 1;
        float held = w.run.world_time;
        for (int i = 0; i < 10; i++) world_step(&w, &in, ASPECT, DT);
        okf(w.run.world_time == held,
            "a pause menu stops the lava churning behind it",
            w.run.world_time, held);

        /* Wrapped rather than left to grow: a float clock that runs for an hour
           loses the precision the shader's own pulse needs. */
        in.paused = 0;
        w.run.world_time = WORLD_TIME_WRAP - 0.001f;
        world_step(&w, &in, ASPECT, 0.01f);
        okf(w.run.world_time < 1.0f, "and it wraps instead of growing without bound",
            w.run.world_time, 0.009f);
    }

    /* --- the exit is tested AFTER the player has moved ----------------------
       An ordering assertion, and the cheapest one to state: the exit marker is
       placed just outside reach and one frame of walking arrives inside it. If
       the exit were tested before the move, that frame would not win -- the
       player would have to spend a second frame standing on it, which is how a
       "walk over the exit at speed and nothing happens" bug feels. */
    printf("\nreaching the exit\n");
    {
        World w;
        fixture(&w, 0);

        /* Forward is -z at yaw 0, and PLAYER_WALK * DT is 18cm a frame. The
           marker sits 102cm away: outside LVL_EXIT_RADIUS now, inside it after
           one step. */
        Entity *e = &w.level.ents[w.level.n_ents++];
        e->kind[0] = 'e'; e->kind[1] = 'x'; e->kind[2] = 'i'; e->kind[3] = 't';
        e->kind[4] = 0;
        e->x = 0;
        e->z = -102;

        Input in = idle();
        world_step(&w, &in, ASPECT, DT);
        ok(!w.run.won, "standing still, an exit 1.02m away is out of reach");

        in.forward = 1;
        world_step(&w, &in, ASPECT, DT);
        ok(w.run.won, "one frame of walking reaches it -- the move runs first");
    }

    /* --- a door that moved makes the drawn geometry stale ------------------- */
    printf("\ndoors and the geometry they invalidate\n");
    {
        World w;
        fixture(&w, 0);

        /* A doorway 1m to the player's right, inside DOOR_TOUCH_DIST of them.
           Untagged, so it opens on touch and needs no switch and no key. */
        box(&w.level, 100, -100, 200, 100, 0, 200, 0);
        DoorDef *d = &w.level.doors[w.level.n_doors++];
        d->sector = 1;
        d->axis   = DOOR_UP;
        d->amount = 200;
        d->speed  = 100;
        d->tag    = 0;
        d->key    = KEY_NONE;
        door_reset(&w.level);

        w.geometry_dirty = 0;

        Input in = idle();
        world_step(&w, &in, ASPECT, DT);
        ok(w.geometry_dirty,
           "a door the player touched raises the rebuild flag");

        /* Frozen, nothing moves the sectors, so nothing needs rebuilding.
           A door that kept opening behind a pause menu would also keep asking
           for a rebuild of a frame nobody is stepping. */
        w.geometry_dirty = 0;
        in.paused = 1;
        world_step(&w, &in, ASPECT, DT);
        ok(!w.geometry_dirty, "and a paused door asks for nothing");
    }

    /* --- claiming the rebuild ---------------------------------------------- */
    printf("\nthe rebuild handshake\n");
    {
        World w;
        fixture(&w, 0);
        w.geometry_dirty = 0;

        int dynamic = -1;
        ok(!world_take_geometry(&w, &dynamic), "a settled world asks for no rebuild");

        w.geometry_dirty = 1;
        ok(world_take_geometry(&w, &dynamic) && dynamic == 0,
           "the first rebuild CREATES the mesh");
        ok(!world_take_geometry(&w, &dynamic),
           "and taking it once is taking it -- twice does not rebuild twice");

        w.geometry_dirty = 1;
        ok(world_take_geometry(&w, &dynamic) && dynamic == 1,
           "every rebuild after that replaces an existing allocation");
    }

    /* --- restart ------------------------------------------------------------
       The one case that loads a level, because a restart is a reload. It
       asserts nothing about what is IN the level -- only that a run which had
       been won, lost and half torn down comes back as a run that has not
       started yet. */
    printf("\nrestart\n");
    {
        World w;
        world_init(&w);

        w.run.title          = 0;
        w.run.won            = 1;
        w.run.dead           = 1;
        w.run.death_time     = 5.0f;
        w.run.hazard_accum   = 0.7f;
        w.run.restart_wanted = 1;
        w.player.health      = 3;

        world_restart(&w);

        ok(!w.run.won,            "a restart clears the win");
        ok(!w.run.dead,           "and the death");
        ok(!w.run.title,          "and does NOT go back to the title -- play was asked for");
        ok(!w.run.restart_wanted, "and clears the request, so nothing does it by hand");
        okf(w.run.death_time == 0.0f, "and every clock the run owns",
            w.run.death_time, 0.0f);
        okf(w.run.hazard_accum == 0.0f, "including the ones that were function statics",
            w.run.hazard_accum, 0.0f);
        okf(w.player.health == PLAYER_MAX_HP, "a fresh run starts at full health",
            (float)w.player.health, (float)PLAYER_MAX_HP);
        ok(w.geometry_dirty, "and the level it reloaded needs its geometry rebuilt");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall frame-order checks passed\n", fails);
    return fails != 0;
}
