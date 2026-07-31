/* enemytest -- step the monster AI against a level, with no window.
 *
 * A chase bug or a "monster stuck on a wall" bug is as invisible from inside
 * the game as a movement bug was. Drop one on a floor, feed it a player
 * position, and assert on where it goes and what it does.
 *
 * The rules are checked against a fixture built here, not against `arena`,
 * for the reason movetest learned: `arena` is a map somebody edits, and a
 * test that names its coordinates goes red on every edit.
 */

#include <stdio.h>
#include <math.h>
#include "enemy.h"
#include "level.h"
#include "player.h"   /* PLAYER_EYE -- projectiles are aimed at a standing body */

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

/* One big flat room, plus a monster spawn near one corner. */
static void build(void) {
    Level z = {0};
    L = z;
    Sector *s = &L.sectors[L.n_sectors++];
    short p[8] = { -2000,-2000,  2000,-2000,  2000,2000,  -2000,2000 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4; s->floor = 0; s->ceil = 600;

    Entity *e = &L.ents[L.n_ents++];
    e->kind[0]='s'; e->kind[1]='p'; e->kind[2]='a'; e->kind[3]='w';
    e->kind[4]='n'; e->kind[5]=0;
    e->x = -1500; e->z = -1500;    /* far corner */
}

static float dist_xz(v3 a, v3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx*dx + dz*dz);
}

int main(void) {
    printf("enemytest\n\n");
    build();

    /* --- spawning --- */
    enemy_spawn_level(&L);
    ok(enemy_count() == 1, "one monster spawned from the spawn entity");
    ok(enemy_alive() == 1, "and it is alive");

    const Enemy *m = enemy_at(0);
    okf(fabsf(m->pos.y) < 0.001f, "it stands on the floor", m->pos.y, 0.0f);

    /* --- chase closes the distance --- */
    {
        v3 player = v3f(15.0f, 1.7f, 0.0f);       /* opposite side of the room */
        float d0 = dist_xz(enemy_at(0)->pos, player);
        for (int i = 0; i < 120; i++) enemy_update(&L, player, DT);   /* 2 s */
        float d1 = dist_xz(enemy_at(0)->pos, player);
        ok(enemy_at(0)->state != E_IDLE, "it noticed the player and gave chase");
        okf(d1 < d0 - 3.0f, "and closed at least 3 m in two seconds", d0 - d1, 3.0f);
    }

    /* --- it reaches melee range and swings, hurting the player --- */
    {
        v3 player = v3f(15.0f, 1.7f, 0.0f);
        int total = 0;
        for (int i = 0; i < 60 * 20; i++)      /* up to 20 s to arrive + swing */
            total += enemy_update(&L, player, DT);
        ok(total > 0, "it eventually lands a melee hit on the player");
        ok(total >= mon_stats(MON_IMP)->damage, "for at least one swing of damage");
    }

    /* --- it stays inside the map the whole time --- */
    {
        float f, c;
        int inside = level_ground(&L, enemy_at(0)->pos.x, enemy_at(0)->pos.z,
                                  enemy_at(0)->pos.y, 1e9f, &f, &c);
        ok(inside, "the monster never walked out of the map");
    }

    /* --- shooting it: hitscan connects, damage kills, corpse is inert --- */
    {
        enemy_spawn_level(&L);                 /* fresh, full health */
        const Enemy *e = enemy_at(0);
        const MonType *S = mon_stats(MON_IMP);
        v3 eye = v3f(e->pos.x, e->pos.y + S->eye, e->pos.z - 5.0f);
        v3 dir = v3f(0, 0, 1);                  /* straight at it */

        float t; int idx;
        ok(enemy_hitscan(eye, dir, 100.0f, &t, &idx),
           "a ray through the monster reports a hit");
        okf(fabsf(t - (5.0f - S->radius)) < 0.05f,
            "at the front of its body", t, 5.0f - S->radius);

        v3 miss_dir = v3f(0, 0, 1);
        v3 miss_eye = v3f(e->pos.x + 5.0f, e->pos.y + S->eye, e->pos.z - 5.0f);
        ok(!enemy_hitscan(miss_eye, miss_dir, 100.0f, &t, &idx),
           "a ray beside the monster misses");

        int swings = 0;
        while (enemy_alive() && swings < 100) {
            enemy_hurt(0, 7, dir);             /* a shotgun pellet's worth */
            swings++;
        }
        int want = (int)ceilf(S->hp / 7.0f);
        ok(enemy_at(0)->state == E_DEAD, "enough damage kills it");
        okf(swings == want, "in exactly the expected number of pellets",
            (float)swings, (float)want);

        /* A corpse must not still be shootable, or you keep 'killing' it. */
        ok(!enemy_hitscan(eye, dir, 100.0f, &t, &idx),
           "the corpse cannot be hit again");
        ok(enemy_alive() == 0, "no monster is counted as alive");
    }

    /* --- the types are actually distinct -------------------------------------
       A new monster is worthless if it plays like the old one. Assert the
       roles the table is meant to encode, so a careless edit that makes the
       brute as fast as the hound gets caught. */
    {
        const MonType *imp   = mon_stats(MON_IMP);
        const MonType *brute = mon_stats(MON_BRUTE);
        const MonType *hound = mon_stats(MON_HOUND);

        ok(brute->hp > imp->hp * 2,      "the brute is far tougher than the imp");
        ok(brute->damage > imp->damage,  "and hits harder");
        ok(brute->speed < imp->speed,    "but is slower");
        ok(hound->speed > imp->speed,    "the hound is faster than the imp");
        ok(hound->hp < imp->hp,          "but frail");
        ok(hound->height < imp->height,  "and lower to the ground");
        ok(hound->aspect > imp->aspect,  "and stockier than the imp");

        ok(mon_type_for("imp")   == MON_IMP,   "entity 'imp' spawns an imp");
        ok(mon_type_for("spawn") == MON_IMP,   "legacy 'spawn' still spawns an imp");
        ok(mon_type_for("brute") == MON_BRUTE, "entity 'brute' spawns a brute");
        ok(mon_type_for("hound") == MON_HOUND, "entity 'hound' spawns a hound");
        ok(mon_type_for("caster")== MON_CASTER,"entity 'caster' spawns a caster");
        ok(mon_type_for("ammo")  < 0,          "a pickup entity spawns no monster");

        /* The ranged type is defined by shot_speed alone -- there is no second
           "is ranged" flag that could disagree with it. */
        const MonType *cast = mon_stats(MON_CASTER);
        ok(cast->shot_speed > 0.0f,  "the caster is the only ranged type");
        ok(imp->shot_speed == 0.0f && brute->shot_speed == 0.0f &&
           hound->shot_speed == 0.0f, "and every melee type has no shot speed");
        ok(cast->attack > imp->attack * 4.0f,
           "its firing range dwarfs any melee reach");
        ok(cast->hp < imp->hp,       "but it is frailer than an imp");
        ok(cast->windup > imp->windup,
           "and telegraphs longer, so the bolt can be avoided");
    }

    /* --- a brute really does soak more pellets than an imp ------------------- */
    {
        Level b = {0};
        Sector *s = &b.sectors[b.n_sectors++];
        short p[8] = { -2000,-2000, 2000,-2000, 2000,2000, -2000,2000 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 600;
        Entity *e = &b.ents[b.n_ents++];
        e->kind[0]='b';e->kind[1]='r';e->kind[2]='u';e->kind[3]='t';e->kind[4]='e';e->kind[5]=0;
        e->x = 0; e->z = 0;

        enemy_spawn_level(&b);
        ok(enemy_at(0)->type == MON_BRUTE, "the brute entity spawned a brute");
        int pellets = 0;
        while (enemy_alive() && pellets < 200) { enemy_hurt(0, 7, v3f(0,0,1)); pellets++; }
        int imp_pellets = (int)ceilf(mon_stats(MON_IMP)->hp / 7.0f);
        ok(pellets > imp_pellets * 2, "and took more than twice an imp's pellets");
    }

    /* --- the caster holds its range instead of closing ----------------------
       The whole point of a ranged enemy is that it does not come to you. If it
       drifted into melee it would just be a slow imp, so this asserts the
       spacing band directly rather than trusting the state machine reads. */
    {
        Level r = {0};
        Sector *s = &r.sectors[r.n_sectors++];
        short p[8] = { -4000,-4000, 4000,-4000, 4000,4000, -4000,4000 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 600;
        Entity *e = &r.ents[r.n_ents++];
        e->kind[0]='c';e->kind[1]='a';e->kind[2]='s';e->kind[3]='t';
        e->kind[4]='e';e->kind[5]='r';e->kind[6]=0;
        e->x = 0; e->z = 0;

        const MonType *C = mon_stats(MON_CASTER);
        enemy_spawn_level(&r);
        ok(enemy_at(0)->type == MON_CASTER, "the caster entity spawned a caster");

        /* Starting well outside its range, it should walk in and then stop. */
        v3 player = v3f(0.0f, PLAYER_EYE, 30.0f);
        for (int i = 0; i < 60 * 12; i++) enemy_update(&r, player, DT);
        float held = dist_xz(enemy_at(0)->pos, player);
        okf(held <= C->attack + 1.0f && held >= C->attack * 0.5f,
            "from far off it closes only to its firing range", held, C->attack);
        ok(held > mon_stats(MON_IMP)->attack * 2.0f,
           "which is nowhere near melee reach");

        /* Standing on top of it, it should give ground rather than stand there. */
        enemy_spawn_level(&r);
        v3 close = v3f(0.0f, PLAYER_EYE, 2.5f);
        float d0 = dist_xz(enemy_at(0)->pos, close);
        for (int i = 0; i < 60 * 3; i++) enemy_update(&r, close, DT);
        float d1 = dist_xz(enemy_at(0)->pos, close);
        okf(d1 > d0 + 1.0f, "crowded, it backs away instead of standing still",
            d1 - d0, 1.0f);
    }

    /* --- it actually fires, and the bolt actually hurts ---------------------- */
    {
        Level r = {0};
        Sector *s = &r.sectors[r.n_sectors++];
        short p[8] = { -4000,-4000, 4000,-4000, 4000,4000, -4000,4000 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 600;
        Entity *e = &r.ents[r.n_ents++];
        e->kind[0]='c';e->kind[1]='a';e->kind[2]='s';e->kind[3]='t';
        e->kind[4]='e';e->kind[5]='r';e->kind[6]=0;
        e->x = 0; e->z = 0;

        enemy_spawn_level(&r);
        v3 player = v3f(0.0f, PLAYER_EYE, 10.0f);   /* inside its firing band */

        int seen_shot = 0, damage = 0;
        for (int i = 0; i < 60 * 8; i++) {
            damage += enemy_update(&r, player, DT);
            for (int k = 0; k < enemy_shot_count(); k++)
                if (enemy_shot_at(k)->active) { seen_shot = 1; break; }
        }
        ok(seen_shot, "a bolt appears in the world once it attacks");
        ok(damage > 0, "and standing in front of it costs health");
        ok(damage >= mon_stats(MON_CASTER)->damage,
           "for at least one full bolt's worth");

        /* Nothing should still be in flight forever: bolts expire or land. */
        for (int i = 0; i < 60 * 10; i++) enemy_update(&r, v3f(200,PLAYER_EYE,200), DT);
        int stuck = 0;
        for (int k = 0; k < enemy_shot_count(); k++)
            if (enemy_shot_at(k)->active) stuck++;
        okf(stuck == 0, "and no bolt is left hanging in the air once it misses",
            (float)stuck, 0.0f);
    }

    /* --- cover works: no line of sight, no bolt -----------------------------
       Two rooms that do not touch. The caster is well inside its sight radius
       of the player but has a wall in the way, so it must never fire -- if it
       did, cover would be decorative and the type would be unfightable. */
    {
        Level r = {0};
        Sector *a = &r.sectors[r.n_sectors++];
        short pa[8] = { -1000,-1000, 1000,-1000, 1000,1000, -1000,1000 };
        for (int i = 0; i < 8; i++) a->pts[i] = pa[i];
        a->n = 4; a->floor = 0; a->ceil = 600;

        Sector *b = &r.sectors[r.n_sectors++];       /* a separate room east */
        short pb[8] = { 2000,-1000, 4000,-1000, 4000,1000, 2000,1000 };
        for (int i = 0; i < 8; i++) b->pts[i] = pb[i];
        b->n = 4; b->floor = 0; b->ceil = 600;

        Entity *e = &r.ents[r.n_ents++];
        e->kind[0]='c';e->kind[1]='a';e->kind[2]='s';e->kind[3]='t';
        e->kind[4]='e';e->kind[5]='r';e->kind[6]=0;
        e->x = 0; e->z = 0;                          /* in the west room */

        enemy_spawn_level(&r);
        ok(enemy_count() == 1, "the walled-off caster spawned");

        v3 player = v3f(30.0f, PLAYER_EYE, 0.0f);    /* in the east room */
        int damage = 0, any_shot = 0;
        for (int i = 0; i < 60 * 8; i++) {
            damage += enemy_update(&r, player, DT);
            for (int k = 0; k < enemy_shot_count(); k++)
                if (enemy_shot_at(k)->active) any_shot = 1;
        }
        okf(any_shot == 0, "with a wall between, it never fires",
            (float)any_shot, 0.0f);
        okf(damage == 0, "and the player takes no damage through cover",
            (float)damage, 0.0f);
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall enemy checks passed\n", fails);
    return fails != 0;
}
