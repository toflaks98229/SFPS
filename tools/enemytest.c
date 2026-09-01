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
#include "pools.h"
/* The pools this file drives, owned here the way a ::World owns its own. The
   five modules that used to keep these in file-scope arrays hand them back
   now, which is why a fixture no longer inherits the previous case's monsters.
   See src/pools.h.
   이 파일이 구동하는 풀이며, ::World가 자기 것을 소유하듯 이곳에서 소유합니다. 이것을 파일
   스코프 배열에 담고 있던 다섯 모듈이 이제 돌려주며, 그래서 픽스처가 이전 사례의 몬스터를
   물려받지 않습니다. src/pools.h를 참조하십시오. */
static Pools g_pools;

#include "player.h"   /* PLAYER_EYE -- projectiles are aimed at a standing body */
#include "loot.h"     /* the drop a kill decides, and the one it owes */

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

/* Names an entity. A kind written out character by character is a kind that
   goes stale silently the day the bestiary loses that row -- which it now has,
   twice -- so the fixtures that were added after this helper existed use it.
   엔티티에 이름을 붙입니다. 한 글자씩 적어 둔 종류 이름은 도감이 그 행을 잃는 날 조용히
   낡습니다. 그리고 실제로 두 번 그런 일이 있었습니다. 그래서 이 도우미가 생긴 뒤에 추가된
   픽스처는 이것을 씁니다. */
static void put_kind(Entity *e, const char *kind) {
    int i = 0;
    while (kind[i] && i < LVL_KIND - 1) { e->kind[i] = kind[i]; i++; }
    e->kind[i] = 0;
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

/* --- the wall fixture -----------------------------------------------------
   Walks one kind into a wall and returns how far its CENTRE settles from the
   face.

   The two-step player position is the whole trick. chase_brawler and
   chase_caster move toward the player with no ongoing sight test -- only the
   E_IDLE -> E_CHASE transition needs to see anything -- so the player is shown
   inside the room to start the chase and then placed far beyond the wall. The
   monster keeps pressing and settles at whatever the collision actually allows.
   A player left inside the room would stop the monster at its range band
   instead, which measures the AI and not the wall.
   벽 픽스처입니다. 한 종류를 벽으로 걸어 들어가게 하고, 그 *중심*이 벽면에서 얼마나 떨어진
   곳에 자리 잡는지를 반환합니다.

   플레이어 위치를 두 단계로 두는 것이 요령의 전부입니다. chase_brawler와 chase_caster는
   시야를 계속 확인하지 않고 플레이어를 향해 움직입니다. 무언가를 보아야 하는 것은
   E_IDLE -> E_CHASE 전이뿐입니다. 그래서 방 안에서 플레이어를 보여 추격을 시작시킨 뒤,
   벽 너머 멀리에 놓습니다. 몬스터는 계속 밀고 들어가 충돌이 실제로 허용하는 자리에 자리
   잡습니다. 플레이어를 방 안에 두면 몬스터는 벽이 아니라 자기 사거리 대역에서 멈추며, 그것은
   벽이 아니라 AI를 재는 것입니다. */
static Level WALL;

static float wall_gap(const MonType *M) {
    Level z = {0};
    WALL = z;
    Sector *s = &WALL.sectors[WALL.n_sectors++];
    short p[8] = { -1200,-400,  0,-400,  0,400,  -1200,400 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4; s->floor = 0; s->ceil = 700;      /* the east wall is x = 0 */

    Entity *e = &WALL.ents[WALL.n_ents++];
    for (int i = 0; M->name[i]; i++) e->kind[i] = M->name[i];
    e->x = -1000; e->z = 0; e->y = 100;

    enemy_reset(&g_pools);
    enemy_spawn_level(&g_pools, &WALL);
    if (enemy_count(&g_pools) != 1) { ok(0, "the kind spawned"); return -1.0f; }

    v3 seen = v3f(-2.0f, PLAYER_EYE, 0.0f);
    for (int i = 0; i < 30; i++) enemy_update(&g_pools, &WALL, seen, DT);
    if (enemy_at(&g_pools, 0)->state == E_IDLE)
        ok(0, "  (it never noticed the player -- sight, not collision)");

    v3 gone = v3f(50.0f, PLAYER_EYE, 0.0f);
    for (int i = 0; i < 60 * 20; i++) enemy_update(&g_pools, &WALL, gone, DT);

    return 0.0f - enemy_at(&g_pools, 0)->pos.x;
}

/* --- the staircase fixture -----------------------------------------------
   Two risers of `rise`, a long landing on top, and the monster starting one
   metre short of the first step. The player stands on the landing at this
   kind's OWN range band plus a margin: a caster that keeps its distance has
   then exactly as much reason to come up as a brawler closing does, and one
   fixture serves both archetypes instead of the test quietly only covering
   whichever one it was written against.
   계단 픽스처입니다. `rise` 높이의 단 둘, 그 위의 긴 층계참, 그리고 첫 단에서 1미터 못 미친
   곳에서 시작하는 몬스터입니다. 플레이어는 층계참 위, 그 종류 *자신의* 사거리 대역에 여유를
   더한 자리에 섭니다. 그러면 거리를 지키는 캐스터도 접근하는 근접형과 똑같이 올라올 이유를
   갖게 되고, 하나의 픽스처가 두 아키타입을 모두 담습니다. 그러지 않으면 검사는 자신이 쓰인
   쪽 하나만 조용히 덮게 됩니다. */
static Level STAIR;

static void stair_band(float x0, float x1, float floor_m) {
    Sector *s = &STAIR.sectors[STAIR.n_sectors++];
    short p[8] = { (short)(x0*100), -300, (short)(x1*100), -300,
                   (short)(x1*100),  300, (short)(x0*100),  300 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4; s->floor = (short)(floor_m * 100); s->ceil = 800;
}

static void stair_build(const MonType *M, float rise, int risers) {
    Level z = {0};
    STAIR = z;
    stair_band(-10.0f, 0.0f, 0.0f);
    for (int i = 0; i < risers; i++)
        stair_band((float)i, (float)(i + 1), rise * (float)(i + 1));
    stair_band((float)risers, 40.0f, rise * (float)risers);

    Entity *e = &STAIR.ents[STAIR.n_ents++];
    for (int i = 0; M->name[i]; i++) e->kind[i] = M->name[i];
    e->x = -100; e->z = 0; e->y = 100;
}

/* Walks one kind at the stairs for thirty seconds and returns the height it
   got to. Also asserts it noticed the player at all, so "never saw it" cannot
   arrive disguised as "could not climb it".
   한 종류를 30초 동안 계단으로 걷게 하고 도달한 높이를 반환합니다. 플레이어를 알아채기는
   했는지도 함께 단언하여, "보지 못했다"가 "오르지 못했다"로 위장해 도착하지 못하게 합니다. */
static float climb(const MonType *M, float rise, int risers) {
    stair_build(M, rise, risers);
    enemy_reset(&g_pools);
    enemy_spawn_level(&g_pools, &STAIR);
    if (enemy_count(&g_pools) != 1) { ok(0, "the kind spawned"); return -1.0f; }

    float top = rise * (float)risers;
    v3 player = v3f(M->attack + 4.0f, top + PLAYER_EYE, 0.0f);
    int noticed = 0;
    for (int i = 0; i < 60 * 30; i++) {
        enemy_update(&g_pools, &STAIR, player, DT);
        if (enemy_at(&g_pools, 0)->state != E_IDLE) noticed = 1;
    }
    if (!noticed) ok(0, "  (it never noticed the player -- sight, not stepping)");
    return enemy_at(&g_pools, 0)->pos.y;
}

/* The two heights must straddle every kind's limit, or the pair proves nothing.
   두 높이는 모든 종류의 상한을 사이에 두고 갈라져야 합니다. 그러지 않으면 이 쌍은 아무것도
   증명하지 않습니다. */
static void check_step_fixture(float riser, float wall) {
    okf(riser < PLAYER_STEP, "fixture: the riser is one the player clears",
        riser, PLAYER_STEP);
    for (int t = 0; t < MON_TYPES; t++) {
        const MonType *M = mon_stats(t);
        if (M->flags & (MON_FLIES | MON_ANCHORED)) continue;
        if (wall <= M->height / 3.0f || wall <= PLAYER_STEP) {
            ok(0, "fixture: the control wall is out of everything's reach");
            return;
        }
    }
    ok(1, "fixture: the control wall is out of everything's reach");
}

/* --- the approach is not a line, and it is fast enough to matter -----------
 *
 * ENGLISH
 * -------
 * TWO CLAIMS THAT ONLY A PATH CAN ANSWER. A monster's speed and its weave are
 * both single numbers in a table, and both are trivially "set" and easily
 * meaningless: a weave the approach never reads leaves a straight line, and a
 * speed nothing can reach with leaves a monster that never arrives.
 *
 * WHY IT MATTERS THAT THE LINE BENDS. `chase_brawler` moved along the vector to
 * the player and nothing else, so the approach was a straight line -- and a
 * straight line gets EASIER to avoid as it gets faster, because it arrives
 * sooner without arriving anywhere new. Raising the speed column without
 * bending the path would have made the bestiary quicker and no more dangerous.
 *
 * MEASURED AS OFF-AXIS TRAVEL. The straight-line path from the start to the
 * player is one axis; how far the monster gets from it is the other. A weave of
 * zero scores zero by construction, whatever the speed, so this cannot pass by
 * the monster merely moving.
 *
 * 한국어
 * ------
 * *경로만이 답할 수 있는 주장 둘입니다.* 몬스터의 속도와 갈지자는 둘 다 표 안의 수 하나이고,
 * 둘 다 "설정"하기는 쉬우며 무의미해지기도 쉽습니다. 접근이 읽지 않는 갈지자는 직선을 남기고,
 * 무엇에도 닿을 수 없는 속도는 결코 도착하지 않는 몬스터를 남깁니다.
 *
 * *선이 휘는 것이 왜 중요한가.* `chase_brawler`는 플레이어를 향한 벡터로만 움직였으므로 접근이
 * 직선이었습니다. 그리고 직선은 빨라질수록 피하기 *쉬워집니다*. 더 일찍 도착할 뿐 새로운 곳에
 * 도착하지 않기 때문입니다. 경로를 휘게 하지 않고 속도 열만 올렸다면 도감은 더 빨라지되 더
 * 위험해지지는 않았을 것입니다.
 *
 * *축에서 벗어난 거리로 잽니다.* 출발점에서 플레이어까지의 직선이 한 축이고, 몬스터가 그것에서
 * 얼마나 멀어지는지가 다른 축입니다. 갈지자가 0이면 속도가 얼마든 구조적으로 0점이므로, 몬스터가
 * 그저 움직이는 것만으로는 이 검사를 통과할 수 없습니다.
 */
static void check_weave(void) {
    printf("\nthe approach bends, and arrives\n");

    static const struct { const char *kind; float min_off; } WANT[] = {
        { "brute",        0.40f },   /* the straightest walker in the table */
        { "water_spirit", 1.00f },   /* the loosest */
    };

    for (int k = 0; k < 2; k++) {
        build();
        put_kind(&L.ents[0], WANT[k].kind);
        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &L);
        if (enemy_count(&g_pools) != 1) { ok(0, "the kind spawned"); continue; }

        v3 player = v3f(0.0f, PLAYER_EYE, 0.0f);
        v3 start  = enemy_at(&g_pools, 0)->pos;

        /* The straight line this approach is being compared against. */
        float ax = player.x - start.x, az = player.z - start.z;
        float alen = sqrtf(ax*ax + az*az);
        ax /= alen; az /= alen;

        float worst = 0.0f;
        int   flips = 0; char was = enemy_at(&g_pools, 0)->lefty;
        float d0 = dist_xz(start, player);
        int   frames = 0;
        for (; frames < 60 * 8; frames++) {
            enemy_update(&g_pools, &L, player, DT);
            const Enemy *m = enemy_at(&g_pools, 0);
            float rx = m->pos.x - start.x, rz = m->pos.z - start.z;
            /* Distance from the line, which is the component across it. */
            float off = rx * az - rz * ax;
            if (off < 0.0f) off = -off;
            if (off > worst) worst = off;
            if (m->lefty != was) { flips++; was = m->lefty; }
            /* THE APPROACH ONLY. A caster stops closing at its band and
               strafes there, and a strafe at seven metres is a long way
               from a line drawn to the player -- the first cut of this
               measured that and reported the water spirit as wandering
               seven metres off course, which was the close-range strafe
               wearing the weave's name.
               *접근 구간만입니다.* 캐스터는 자기 사거리에서 다가오기를 멈추고 그곳에서
               횡이동하며, 7미터에서의 횡이동은 플레이어까지 그은 선에서 한참
               떨어져 있습니다. 이 검사의 첫 판이 그것을 쟀고 물의 정령이 7미터를
               벗어났다고 보고했는데, 그것은 갈지자의 이름을 달고 있던 근접
               횡이동이었습니다. */
            if (dist_xz(m->pos, player) <= mon_stats(m->type)->attack) break;
        }

        float d1 = dist_xz(enemy_at(&g_pools, 0)->pos, player);
        printf("      %-13s closed %.1fm of %.1fm in %.2fs, %.2fm off the line, %d turns\n",
               WANT[k].kind, d0 - d1, d0, frames * DT, worst, flips);

        okf(worst > WANT[k].min_off,
            "the approach leaves the straight line",
            worst, WANT[k].min_off);
        okf(d1 < d0 * 0.5f,
            "and still closes most of the distance",
            d1, d0 * 0.5f);
    }
}

int main(void) {
    printf("enemytest\n\n");
    build();

    /* --- spawning --- */
    enemy_spawn_level(&g_pools, &L);
    ok(enemy_count(&g_pools) == 1, "one monster spawned from the spawn entity");
    ok(enemy_alive(&g_pools) == 1, "and it is alive");

    const Enemy *m = enemy_at(&g_pools, 0);
    okf(fabsf(m->pos.y) < 0.001f, "it stands on the floor", m->pos.y, 0.0f);

    /* --- chase closes the distance --- */
    {
        v3 player = v3f(15.0f, 1.7f, 0.0f);       /* opposite side of the room */
        float d0 = dist_xz(enemy_at(&g_pools, 0)->pos, player);
        for (int i = 0; i < 120; i++) enemy_update(&g_pools, &L, player, DT);   /* 2 s */
        float d1 = dist_xz(enemy_at(&g_pools, 0)->pos, player);
        ok(enemy_at(&g_pools, 0)->state != E_IDLE, "it noticed the player and gave chase");
        okf(d1 < d0 - 3.0f, "and closed at least 3 m in two seconds", d0 - d1, 3.0f);
    }

    /* --- it reaches melee range and swings, hurting the player --- */
    {
        v3 player = v3f(15.0f, 1.7f, 0.0f);
        int total = 0;
        for (int i = 0; i < 60 * 20; i++)      /* up to 20 s to arrive + swing */
            total += enemy_update(&g_pools, &L, player, DT);
        ok(total > 0, "it eventually lands a melee hit on the player");
        ok(total >= mon_stats(MON_WATER_SPIRIT)->damage, "for at least one swing of damage");
    }

    /* --- it stays inside the map the whole time --- */
    {
        float f, c;
        int inside = level_ground(&L, enemy_at(&g_pools, 0)->pos.x, enemy_at(&g_pools, 0)->pos.z,
                                  enemy_at(&g_pools, 0)->pos.y, 1e9f, &f, &c);
        ok(inside, "the monster never walked out of the map");
    }

    /* --- shooting it: hitscan connects, damage kills, corpse is inert --- */
    {
        enemy_spawn_level(&g_pools, &L);                 /* fresh, full health */
        const Enemy *e = enemy_at(&g_pools, 0);
        const MonType *S = mon_stats(MON_WATER_SPIRIT);
        v3 eye = v3f(e->pos.x, e->pos.y + S->eye, e->pos.z - 5.0f);
        v3 dir = v3f(0, 0, 1);                  /* straight at it */

        float t; int idx;
        ok(enemy_hitscan(&g_pools, eye, dir, 100.0f, &t, &idx),
           "a ray through the monster reports a hit");
        okf(fabsf(t - (5.0f - S->radius)) < 0.05f,
            "at the front of its body", t, 5.0f - S->radius);

        v3 miss_dir = v3f(0, 0, 1);
        v3 miss_eye = v3f(e->pos.x + 5.0f, e->pos.y + S->eye, e->pos.z - 5.0f);
        ok(!enemy_hitscan(&g_pools, miss_eye, miss_dir, 100.0f, &t, &idx),
           "a ray beside the monster misses");

        int swings = 0;
        while (enemy_alive(&g_pools) && swings < 100) {
            enemy_hurt(&g_pools, 0, 7, dir);             /* a shotgun pellet's worth */
            swings++;
        }
        int want = (int)ceilf(S->hp / 7.0f);
        ok(enemy_at(&g_pools, 0)->state == E_DEAD, "enough damage kills it");
        okf(swings == want, "in exactly the expected number of pellets",
            (float)swings, (float)want);

        /* A corpse must not still be shootable, or you keep 'killing' it. */
        ok(!enemy_hitscan(&g_pools, eye, dir, 100.0f, &t, &idx),
           "the corpse cannot be hit again");
        ok(enemy_alive(&g_pools) == 0, "no monster is counted as alive");

        /* --- and it is owed exactly once ---------------------------------
           A corpse lies there for CORPSE_FADE seconds and world.c sweeps the
           pool every frame, so "owed once" is the difference between a drop and
           a fountain. It is also the one part of the drop that cannot be seen
           in the game: an item arriving sixty times a second at the same point
           looks like one item.
           시체는 CORPSE_FADE초 동안 그 자리에 누워 있고 world.c는 매 프레임 풀을 훑으므로,
           "한 번만 빚진다"가 드롭과 분수의 차이입니다. 또한 이것은 게임 안에서 볼 수 없는
           유일한 부분이기도 합니다. 같은 지점에 초당 60번 도착하는 아이템은 아이템 하나처럼
           보입니다. */
        v3 at = v3f(0, 0, 0);
        int owed = enemy_take_drop(&g_pools, 0, &at);
        ok(owed == -1 || owed == LOOT_HELD || (owed >= 0 && owed < PK_KINDS),
           "a corpse owes a real kind, the held pseudo-kind, or nothing");
        ok(enemy_take_drop(&g_pools, 0, &at) == -1,
           "and having handed it over, owes nothing more");

        /* Wherever the body is, which is floor -- an item handed back at a
           point off the floor would be tossed into the air or under it.
           몸이 있는 자리이며 그곳은 바닥입니다. 바닥이 아닌 지점으로 돌려준 아이템은 공중이나
           바닥 아래로 던져집니다. */
        g_pools.enemy.m[0].drop = PK_HEALTH;
        ok(enemy_take_drop(&g_pools, 0, &at) == PK_HEALTH,
           "what it owes is what it hands over");
        ok(at.x == g_pools.enemy.m[0].pos.x && at.z == g_pools.enemy.m[0].pos.z,
           "handed over where the body actually fell");

        ok(enemy_take_drop(&g_pools, -1, &at) == -1 &&
           enemy_take_drop(&g_pools, 9999, &at) == -1,
           "and a slot that is not there owes nothing");
    }

    /* --- the types are actually distinct -------------------------------------
       A new monster is worthless if it plays like the old one. Assert the
       roles the table is meant to encode, so a careless edit that makes the
       brute as fast as the water spirit gets caught.

       THREE ROWS NOW, and the assertions below are what the third one costs.
       The hound was the fast frail one and the wraith was the flyer; the hound
       is gone and MON_FLIES moved onto the caster, so what separates the rows
       is toughness, speed, and whether the thing is standing on anything. */
    {
        const MonType *spirit = mon_stats(MON_WATER_SPIRIT);
        const MonType *brute  = mon_stats(MON_BRUTE);
        const MonType *cast   = mon_stats(MON_CASTER);
        const MonType *maw    = mon_stats(MON_MAW);

        ok(brute->hp > spirit->hp * 2,      "the brute is far tougher than the water spirit");
        ok(brute->damage > spirit->damage,  "and hits harder");
        ok(brute->speed < spirit->speed,    "but is slower");
        ok(cast->hp < spirit->hp,           "the caster is frailer than the baseline");
        ok(cast->attack > spirit->attack,   "and reaches further");
        ok(cast->flags & MON_FLIES,         "and it is the one that is not on the floor");

        ok(mon_type_for("water_spirit") == MON_WATER_SPIRIT,
           "entity 'water_spirit' spawns a water spirit");
        /* EVERY RETIRED NAME STILL RESOLVES, AND NOT ALL TO THE SAME ROW.
           `spawn` is from when there was one kind. `imp` is what this slot was
           called before the water spirit took it. `hound` and `wraith` were
           rows until the bestiary lost its fast melee creature and its second
           flyer. A rename or a retirement that emptied the levels already using
           the name would be one nobody could make -- so each name points at
           WHAT REPLACED IT, which is why `imp` and `wraith` land on the caster
           and `hound` lands here. See MON_LEGACY in enemy.c.
           은퇴한 이름은 모두 여전히 해석되며, 전부 같은 행으로 가지는 않습니다. `spawn`은
           종류가 하나뿐이던 시절의 것입니다. `imp`는 물의 정령이 이 자리를 차지하기 전 이
           슬롯의 이름이었습니다. `hound`와 `wraith`는 도감이 빠른 근접 생물과 두 번째 비행체를
           잃기 전까지 행이었습니다. 이미 그 이름을 쓰고 있는 레벨을 비우는 이름 변경이나 은퇴는
           아무도 할 수 없는 것이므로, 각 이름은 *그것을 대신한 것*을 가리킵니다. 그래서 `imp`와
           `wraith`는 캐스터에, `hound`는 이곳에 떨어집니다. enemy.c의 MON_LEGACY를
           참조하십시오. */
        ok(mon_type_for("spawn")  == MON_WATER_SPIRIT, "legacy 'spawn' still resolves");
        ok(mon_type_for("hound")  == MON_WATER_SPIRIT, "and retired 'hound' lands on the water spirit");
        ok(mon_type_for("imp")    == MON_CASTER, "retired 'imp' lands on the caster");
        ok(mon_type_for("wraith") == MON_CASTER, "and so does 'wraith', which is what the caster became");
        ok(mon_type_for("brute") == MON_BRUTE, "entity 'brute' spawns a brute");
        ok(mon_type_for("caster")== MON_CASTER,"entity 'caster' spawns a caster");
        ok(mon_type_for("ammo")  < 0,          "a pickup entity spawns no monster");

        /* NO RETIRED NAME MAY OUTLIVE ITS REPLACEMENT. An alias that resolves
           to a row which itself later goes is an alias that silently lands on
           whatever slid into that index, and nothing would say so. Checked as
           a property of the table rather than name by name, so the next
           retirement is covered without editing this.
           은퇴한 이름이 자기 대체물보다 오래 살아서는 안 됩니다. 나중에 사라지는 행으로
           해석되는 별칭은 그 인덱스로 밀려 들어온 무엇에든 조용히 떨어지는 별칭이며, 아무도
           말해 주지 않습니다. 이름마다가 아니라 표의 성질로 검사하므로, 다음 은퇴도 이곳을
           고치지 않고 함께 덮입니다. */
        static const char *RETIRED[] = { "spawn", "imp", "hound", "wraith" };
        int all_live = 1;
        for (int i = 0; i < (int)(sizeof RETIRED / sizeof RETIRED[0]); i++) {
            int t = mon_type_for(RETIRED[i]);
            if (t < 0 || t >= MON_TYPES) all_live = 0;
        }
        ok(all_live, "and every retired name lands on a row that still exists");

        /* Ranged is defined by shot_speed alone -- there is no second "is
           ranged" flag that could disagree with it.
           원거리 여부는 shot_speed 하나로 정의됩니다. 그것과 어긋날 수 있는 두 번째
           "원거리인가" 플래그는 없습니다. */
        ok(cast->shot_speed > 0.0f && spirit->shot_speed > 0.0f,
           "shot speed alone is what makes a type ranged");
        ok(brute->shot_speed == 0.0f,
           "and the melee type carries none");

        /* THREE DISTANCES, NOT TWO, and that is what the water spirit was
           changed for. It used to be melee, which made the roster "in your
           face or across the room" with nothing between -- so a player only
           ever had two answers. Mid range is a third: close enough that
           backing off does not break contact, far enough that closing costs
           you the cone.
           A ratio rather than a number, because 7.5 metres is a design
           decision somebody will move and the RELATION is what must survive
           the move.
           *둘이 아니라 세 개의 거리*이며, 그것이 물의 정령을 바꾼 이유입니다. 예전에는
           근접이었고, 그러면 진용이 "코앞이거나 방 건너"가 되어 그 사이가 없었습니다.
           플레이어에게는 늘 두 가지 답뿐이었습니다. 중거리는 세 번째입니다. 물러나도 접촉이
           끊기지 않을 만큼 가깝고, 붙으려면 원뿔을 감수해야 할 만큼 멉니다.
           숫자가 아니라 *비율*인 이유는 7.5미터가 누군가 옮길 설계 결정이고, 그 이동을
           견뎌야 하는 것은 *관계*이기 때문입니다. */
        ok(spirit->attack > brute->attack * 2.0f,
           "the water spirit outranges every melee type");
        ok(spirit->attack < cast->attack * 0.75f,
           "and still sits well inside the caster's, which is what mid means");

        /* And it sprays. A volley is not a faster single shot: one aimed bolt
           is answered by stepping aside and a stream is answered by getting out
           of where it is pointing, which is a different move. Without this the
           water spirit would be the caster at a shorter range.
           그리고 난사합니다. 일제 사격은 더 빠른 단발이 아닙니다. 조준된 볼트 하나에는 옆으로
           비켜서는 것으로 답하고, 줄기에는 그것이 겨누는 자리에서 벗어나는 것으로 답합니다.
           다른 동작입니다. 이것이 없으면 물의 정령은 사거리만 짧은 캐스터입니다. */
        ok(spirit->burst > 1 && spirit->spread > 0.0f,
           "it sprays rather than firing one aimed bolt");
        ok(cast->burst == 1 && brute->burst == 1,
           "while everything else still fires or swings once");

        /* OVER TIME, AND THAT IS THE WHOLE CHANGE. A cone and a stream can have
           the same burst count and the same spread and be different fights: the
           cone either hits or does not, and the stream gives the player the
           length of itself to be somewhere else in. The gap is what separates
           them, so it is what is asserted -- `burst > 1` alone would still pass
           on the shotgun this replaced.
           *시간에 걸쳐 나가며, 그것이 변경의 전부입니다.* 원뿔과 줄기는 같은 발수와 같은 산포를
           갖고도 서로 다른 싸움일 수 있습니다. 원뿔은 맞거나 맞지 않거나이고, 줄기는 자기
           길이만큼의 시간을 플레이어에게 다른 곳에 있으라고 내어 줍니다. 둘을 가르는 것이
           간격이므로 검사하는 것도 그것입니다. `burst > 1`만으로는 이것이 대체한 산탄도
           통과합니다. */
        ok(spirit->shot_gap > 0.0f,
           "and it sprays over time rather than all at once");
        ok(spirit->burst_min >= 5 && spirit->burst <= 10,
           "a volley is five to ten bolts");

        /* The maw keeps the cone, and a gap of zero is how it says so. The two
           readings of `burst` live side by side in one table, which is the point
           of `shot_gap` being a number rather than a flag.
           아귀는 원뿔을 유지하며, 간격 0이 그것을 말하는 방식입니다. `burst`의 두 해석이 한 표
           안에 나란히 살며, 그것이 `shot_gap`이 플래그가 아니라 수인 이유입니다. */
        ok(maw->burst > 1 && maw->shot_gap == 0.0f,
           "while the maw still throws its five together");
        ok(cast->hp < spirit->hp,    "but it is frailer than a water spirit");
        ok(cast->windup > spirit->windup,
           "and telegraphs longer, so the bolt can be avoided");
    }

    /* --- a brute really does soak more pellets than a water spirit ---------- */
    {
        Level b = {0};
        Sector *s = &b.sectors[b.n_sectors++];
        short p[8] = { -2000,-2000, 2000,-2000, 2000,2000, -2000,2000 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 600;
        Entity *e = &b.ents[b.n_ents++];
        e->kind[0]='b';e->kind[1]='r';e->kind[2]='u';e->kind[3]='t';e->kind[4]='e';e->kind[5]=0;
        e->x = 0; e->z = 0;

        enemy_spawn_level(&g_pools, &b);
        ok(enemy_at(&g_pools, 0)->type == MON_BRUTE, "the brute entity spawned a brute");
        int pellets = 0;
        while (enemy_alive(&g_pools) && pellets < 200) { enemy_hurt(&g_pools, 0, 7, v3f(0,0,1)); pellets++; }
        int spirit_pellets = (int)ceilf(mon_stats(MON_WATER_SPIRIT)->hp / 7.0f);
        ok(pellets > spirit_pellets * 2,
           "and took more than twice a water spirit's pellets");
    }

    /* --- the caster holds its range instead of closing ----------------------
       The whole point of a ranged enemy is that it does not come to you. If it
       drifted into melee it would just be a slow brute, so this asserts the
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
        enemy_spawn_level(&g_pools, &r);
        ok(enemy_at(&g_pools, 0)->type == MON_CASTER, "the caster entity spawned a caster");

        /* Starting well outside its range, it should walk in and then stop. */
        v3 player = v3f(0.0f, PLAYER_EYE, 30.0f);
        for (int i = 0; i < 60 * 12; i++) enemy_update(&g_pools, &r, player, DT);
        float held = dist_xz(enemy_at(&g_pools, 0)->pos, player);
        okf(held <= C->attack + 1.0f && held >= C->attack * 0.5f,
            "from far off it closes only to its firing range", held, C->attack);
        ok(held > mon_stats(MON_BRUTE)->attack * 2.0f,
           "which is nowhere near melee reach");

        /* Standing on top of it, it should give ground rather than stand there. */
        enemy_spawn_level(&g_pools, &r);
        v3 close = v3f(0.0f, PLAYER_EYE, 2.5f);
        float d0 = dist_xz(enemy_at(&g_pools, 0)->pos, close);
        for (int i = 0; i < 60 * 3; i++) enemy_update(&g_pools, &r, close, DT);
        float d1 = dist_xz(enemy_at(&g_pools, 0)->pos, close);
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

        enemy_spawn_level(&g_pools, &r);
        v3 player = v3f(0.0f, PLAYER_EYE, 10.0f);   /* inside its firing band */

        int seen_shot = 0, damage = 0;
        for (int i = 0; i < 60 * 8; i++) {
            damage += enemy_update(&g_pools, &r, player, DT);
            for (int k = 0; k < enemy_shot_count(&g_pools); k++)
                if (enemy_shot_at(&g_pools, k)->active) { seen_shot = 1; break; }
        }
        ok(seen_shot, "a bolt appears in the world once it attacks");
        ok(damage > 0, "and standing in front of it costs health");
        ok(damage >= mon_stats(MON_CASTER)->damage,
           "for at least one full bolt's worth");

        /* Nothing should still be in flight forever: bolts expire or land. */
        for (int i = 0; i < 60 * 10; i++) enemy_update(&g_pools, &r, v3f(200,PLAYER_EYE,200), DT);
        int stuck = 0;
        for (int k = 0; k < enemy_shot_count(&g_pools); k++)
            if (enemy_shot_at(&g_pools, k)->active) stuck++;
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

        enemy_spawn_level(&g_pools, &r);
        ok(enemy_count(&g_pools) == 1, "the walled-off caster spawned");

        v3 player = v3f(30.0f, PLAYER_EYE, 0.0f);    /* in the east room */
        int damage = 0, any_shot = 0;
        for (int i = 0; i < 60 * 8; i++) {
            damage += enemy_update(&g_pools, &r, player, DT);
            for (int k = 0; k < enemy_shot_count(&g_pools); k++)
                if (enemy_shot_at(&g_pools, k)->active) any_shot = 1;
        }
        okf(any_shot == 0, "with a wall between, it never fires",
            (float)any_shot, 0.0f);
        okf(damage == 0, "and the player takes no damage through cover",
            (float)damage, 0.0f);
    }


    /* --- Quake's AI: the three properties it added ------------------------
     *
     * All three are invisible to every check above, which is why they need
     * their own: the monster still spawns, still closes, still attacks and
     * still dies whether or not it turns at a finite rate, circles, or can be
     * held still forever by a fast enough weapon.
     *
     * 위의 모든 검사에게 셋 다 보이지 않으며, 그래서 각자의 검사가 필요합니다. 유한한
     * 속도로 돌든 말든, 원을 그리든 말든, 충분히 빠른 무기에 영원히 붙잡히든 말든, 몬스터는
     * 여전히 생성되고 다가오고 공격하고 죽습니다.
     */
    {
        /* --- turning takes time ---
           The monster is walked up to the player, then the player is moved to
           the OPPOSITE side in a single frame. A monster that snaps cannot be
           got behind and strafing wins no angle, which is the whole reason the
           rest of this exists.
           몬스터를 플레이어 앞까지 걷게 한 뒤, 플레이어를 한 프레임 만에 *반대편*으로
           옮깁니다. 즉시 도는 몬스터는 뒤를 잡을 수 없고 횡이동이 어떤 각도도 얻지
           못합니다. 나머지가 존재하는 이유 전체가 그것입니다. */
        Level r = L;
        enemy_spawn_level(&g_pools, &r);

        /* Placed relative to where the monster IS, once it has settled. The
           first draft put the player on the spawn point itself, so the monster
           stood inside them, `to` was the zero vector and atan2f(0,0) gave a
           facing that could not change -- the test reported "did not turn" for
           a monster that had nothing to turn towards.
           몬스터가 자리를 잡은 *뒤* 그 위치를 기준으로 배치합니다. 초안은 플레이어를 생성
           지점 자체에 두어 몬스터가 플레이어 안에 서 있었고, `to`가 영벡터가 되어
           atan2f(0,0)이 변할 수 없는 방향을 주었습니다. 돌아야 할 대상이 없는 몬스터에
           대해 검사가 "돌지 않았다"고 보고했습니다. */
        v3 home = enemy_at(&g_pools, 0)->pos;
        v3 player = v3f(home.x, PLAYER_EYE, home.z + 6.0f);
        for (int i = 0; i < 60 * 8; i++) enemy_update(&g_pools, &r, player, DT);

        v3 settled = enemy_at(&g_pools, 0)->pos;
        float faced = enemy_at(&g_pools, 0)->yaw;

        /* Straight through the monster to the far side, in one frame. */
        v3 behind = v3f(settled.x, PLAYER_EYE, settled.z - 6.0f);
        enemy_update(&g_pools, &r, behind, DT);
        float after = enemy_at(&g_pools, 0)->yaw;

        float turned = after - faced;
        while (turned >  3.14159265f) turned -= 6.28318531f;
        while (turned < -3.14159265f) turned += 6.28318531f;
        turned = turned < 0 ? -turned : turned;

        /* One frame at the water spirit's 220 deg/s is 3.67 deg = 0.064 rad. Checked
           generously against a quarter turn, because what must not happen is a
           SNAP -- the exact number is the table's business, the finiteness is
           this test's.
           물의 정령의 220 deg/s로 한 프레임은 3.67도, 즉 0.064 rad입니다. 4분의 1 회전을
           기준으로 넉넉하게 검사합니다. 일어나서는 안 되는 것은 *즉시 회전*이며, 정확한
           수치는 표의 몫이고 유한하다는 사실이 이 검사의 몫입니다. */
        okf(turned < 0.78f, "a monster cannot spin round in one frame",
            turned, 0.78f);
        ok(turned > 0.0f, "but it does start turning");
    }

    {
        /* --- pain does not lock ---
           Hit far faster than the flinch lasts, exactly as the rapid gun does.
           Before pain_finished this held a monster still until it died: the
           test is simply whether it ever gets a frame in which it is doing
           something other than flinching.
           경직보다 훨씬 빠르게, 속사 무기가 하는 그대로 때립니다. pain_finished 이전에는
           이것이 몬스터를 죽을 때까지 붙잡아 두었습니다. 검사는 단순합니다. 경직이 아닌
           무언가를 하고 있는 프레임을 한 번이라도 얻는가. */
        /* A BRUTE, because it has to SURVIVE the test. With a water spirit's 40 hit
           points the monster died partway through and every frame after that
           was a corpse in E_DEAD -- which is not E_HURT, so the corpse scored
           as "acting" and lifted the ratio back over the threshold. The check
           now counts E_CHASE specifically rather than "anything but flinching",
           which is the same mistake stated as a rule: absence of one state is
           not presence of the one you meant.
           브루트를 씁니다. 몬스터가 검사를 *견뎌야* 하기 때문입니다. 물의 정령의 체력 40으로는
           도중에 죽었고 그 뒤의 모든 프레임은 E_DEAD인 시체였습니다. 그것은 E_HURT가
           아니므로 시체가 "행동 중"으로 집계되어 비율을 기준선 위로 되돌렸습니다. 이제
           "경직이 아닌 무엇이든"이 아니라 E_CHASE를 특정해서 셉니다. 같은 실수를 규칙으로
           적으면 이렇습니다. 한 상태의 부재는 당신이 의도한 상태의 존재가 아닙니다. */
        Level r = L;
        r.n_ents = 0;
        Entity *be = &r.ents[r.n_ents++];
        be->kind[0]='b'; be->kind[1]='r'; be->kind[2]='u'; be->kind[3]='t';
        be->kind[4]='e'; be->kind[5]=0;
        be->x = -1500; be->z = -1500;
        enemy_spawn_level(&g_pools, &r);

        /* FAR ENOUGH THAT IT STAYS IN CHASE for the whole measurement. The
           first draft put the player on the spawn point, so the monster was
           standing inside them and permanently in E_ATTACK -- it never entered
           the flinch at all, and a test for "does the flinch lock it" scored a
           perfect 1.000 by never reaching the thing it was testing.
           A water spirit walks 3 m/s and this runs 4 seconds, so 25m leaves it still
           walking at the end.
           측정 내내 추격 상태로 남아 있을 만큼 멉니다. 초안은 플레이어를 생성 지점에 두어
           몬스터가 플레이어 안에 서서 영구히 E_ATTACK이었습니다. 경직에 아예 진입하지
           않았고, "경직이 몬스터를 가두는가"를 검사하는 테스트가 검사 대상에 닿지 못한 채
           1.000 만점을 받았습니다. 물의 정령은 초당 3m를 걷고 이 검사는 4초간 돌므로, 25m면
           끝까지 걷고 있습니다. */
        v3 home = enemy_at(&g_pools, 0)->pos;
        v3 player = v3f(home.x, PLAYER_EYE, home.z + 25.0f);
        for (int i = 0; i < 60 * 2; i++) enemy_update(&g_pools, &r, player, DT);

        int acted = 0, frames = 60 * 2;
        for (int i = 0; i < frames; i++) {
            /* 1 damage every other frame -- far inside the 0.16s flinch. 60
               damage total against a brute's 120, so it is alive throughout
               and every frame of the measurement is a frame of a live monster.
               경직 0.16초보다 훨씬 짧은 간격입니다. 총 60 피해이고 브루트의 체력은
               120이므로 내내 살아 있으며, 측정의 모든 프레임이 살아 있는 몬스터의
               프레임입니다. */
            if ((i & 1) == 0) enemy_hurt(&g_pools, 0, 1, v3f(0, 0, 1));
            enemy_update(&g_pools, &r, player, DT);
            if (enemy_count(&g_pools) > 0 && enemy_at(&g_pools, 0)->state == E_CHASE) acted++;
        }
        ok(enemy_at(&g_pools, 0)->state != E_DEAD, "and is still alive at the end");
        ok(enemy_count(&g_pools) > 0, "the monster survives the stun-lock test");

        /* THE FRACTION, not merely "more than none". The first draft asked
           whether the monster ever acted at all, and that passed with the
           cooldown deleted -- because the flinch is not re-entered from inside
           itself, so even the locked monster got the one frame between
           recovering and being hit again. One frame in ten is a monster that
           is not frozen only in the sense that a photograph is not.
           "0보다 많은가"가 아니라 *비율*입니다. 초안은 몬스터가 한 번이라도 행동하는지를
           물었고, 그것은 쿨다운을 지워도 통과했습니다. 경직은 자기 안에서 다시 진입되지
           않으므로 갇힌 몬스터도 회복과 다음 피격 사이의 한 프레임은 얻었기 때문입니다.
           열 프레임 중 하나는, 사진이 얼어붙지 않았다는 의미에서만 얼어붙지 않은
           몬스터입니다. */
        float free_ratio = (float)acted / (float)frames;
        okf(free_ratio > 0.25f,
            "and being shot faster than it flinches does not freeze it",
            free_ratio, 0.25f);
    }

    {
        /* --- it circles rather than standing still ---
           A caster at its preferred range has nothing it must do: it is not
           closing and it only sometimes fires. Before ai_run_slide it simply
           stopped. Measured as distance travelled, because a monster that
           circles ends up near where it began -- displacement would read as
           nearly zero for exactly the behaviour being checked.
           선호 거리에 있는 캐스터는 반드시 해야 할 일이 없습니다. 다가가지도 않고 가끔만
           쏩니다. ai_run_slide 이전에는 그냥 멈춰 있었습니다. *이동 거리*로 측정합니다.
           원을 그리는 몬스터는 시작한 자리 근처로 돌아오므로, 변위로 재면 검사하려는 바로
           그 행동이 0에 가깝게 나옵니다. */
        Level r = L;
        r.n_ents = 0;
        Entity *e = &r.ents[r.n_ents++];
        e->kind[0]='c'; e->kind[1]='a'; e->kind[2]='s'; e->kind[3]='t';
        e->kind[4]='e'; e->kind[5]='r'; e->kind[6]=0;
        e->x = 0; e->z = 0;
        enemy_spawn_level(&g_pools, &r);

        /* Parked at the caster's own preferred distance, so it neither closes
           nor backs off and the only thing left to do is circle. */
        v3 player = v3f(0.0f, PLAYER_EYE, 11.0f);
        for (int i = 0; i < 60; i++) enemy_update(&g_pools, &r, player, DT);

        /* TANGENTIAL travel only -- movement at right angles to the player.
           Total distance does not separate circling from the closing and
           backing-off the caster already did before any of this, and the first
           draft measured total and passed with the strafe deleted. Radial
           motion is the old behaviour; sideways motion is the new one.
           플레이어에 대해 직각인 *접선* 이동만 측정합니다. 총 이동 거리는 원을 그리는
           것과, 캐스터가 이미 하던 접근·후퇴를 구분하지 못하며, 초안은 총 거리를 재어
           횡이동을 지워도 통과했습니다. 반경 방향 운동이 예전 동작이고 옆으로 가는 운동이
           새 동작입니다. */
        v3 prev = enemy_at(&g_pools, 0)->pos;
        float sideways = 0.0f;
        for (int i = 0; i < 60 * 6; i++) {
            enemy_update(&g_pools, &r, player, DT);
            v3 now = enemy_at(&g_pools, 0)->pos;

            float rx = player.x - prev.x, rz = player.z - prev.z;
            float rl = sqrtf(rx*rx + rz*rz);
            if (rl > 0.001f) {
                rx /= rl; rz /= rl;
                float mx = now.x - prev.x, mz = now.z - prev.z;
                /* The component perpendicular to the line to the player. */
                float tangent = mx * (-rz) + mz * rx;
                sideways += tangent < 0 ? -tangent : tangent;
            }
            prev = now;
        }
        okf(sideways > 2.0f, "a caster at its range circles rather than parks",
            sideways, 2.0f);
    }

    /* --- the flags column --------------------------------------------------
       MON_FLIES used to be capacity with nothing using it, and this block
       asserted `flying == 0` -- a tripwire for the day a creature carried the
       bit, so that whoever added one had to come here and say what the branch
       now does instead of leaving a check that quietly meant nothing.

       That day came, and the bit has since MOVED: it was the wraith's, the
       wraith's row was a caster plus this flag and nothing else, so the row
       went and the flag came down onto the caster. What is worth asserting is
       not that the column is sound but that the bit REACHES THE WORLD -- a flag
       that stops a monster falling does nothing on its own, because
       make_monster put every monster on the floor before it could fall.

       EXACTLY ONE, still, and that is the assertion with teeth. Two flyers is
       how the wraith and the caster came to differ by four points of health,
       and one is how "the kind that is not on the floor" stays a fact the
       player can learn once.

       MON_FLIES는 쓰는 것이 없는 여지였고, 이 블록은 `flying == 0`을 단언했습니다. 어떤
       크리처가 그 비트를 갖는 날을 위한 덫이었습니다. 그것을 추가하는 사람이 이곳에 와서 이제 그
       분기가 무엇을 하는지 말하게 하고, 조용히 아무 뜻도 없어진 검사를 남겨 두지 않게 하려는
       것이었습니다.

       그날은 왔고, 그 뒤로 비트가 *옮겨 갔습니다.* 레이스의 것이었는데, 레이스의 행은 캐스터에 이
       플래그를 더한 것 이상이 아니었으므로 행이 사라지고 플래그가 캐스터로 내려왔습니다. 단언할
       가치가 있는 것은 그 열이 온전한지가 아니라 그 비트가 *세계에 닿는지*입니다. 몬스터가
       떨어지는 것을 막는 플래그는 그 자체로는 아무것도 하지 않습니다. make_monster가 떨어질 수
       있기 전에 모든 몬스터를 바닥에 놓았기 때문입니다.

       *여전히 정확히 하나*이며, 이빨을 가진 단언이 그것입니다. 비행체가 둘이면 레이스와 캐스터가
       체력 4점 차이로 갈라지게 되고, 하나여야 "바닥에 있지 않은 종류"가 플레이어가 한 번 배우면
       되는 사실로 남습니다. */
    printf("\nthe flags column\n");
    {
        int flying = 0, stray = 0;
        for (int t = 0; t < MON_TYPES; t++) {
            const MonType *S = mon_stats(t);
            if (S->flags & MON_FLIES)   flying++;
            if (S->flags & ~MON_FLAGS_ALL) stray++;
        }
        okf(stray == 0, "no row carries a bit this build does not define",
            (float)stray, 0.0f);
        okf(flying == 1, "exactly one kind ships flying", (float)flying, 1.0f);
        ok(mon_stats(MON_CASTER)->flags & MON_FLIES, "and it is the caster");
        ok((MON_FLAGS_ALL & MON_FLIES) != 0,
           "MON_FLAGS_ALL covers every bit there is");
    }

    /* --- a flyer is actually in the air -------------------------------------
       THE HALF THE FLAG DOES NOT COVER. MON_FLIES stops the fall; it does not
       put anything up there. Both are checked against the same marker height so
       a change to either shows here: the caster holds it and the water spirit
       does not.
       플래그가 덮지 않는 절반입니다. MON_FLIES는 낙하를 멈추게 할 뿐 무언가를 위로 올려놓지는
       않습니다. 둘을 같은 표식 높이에 대고 검사하므로 어느 쪽이 바뀌어도 이곳에 드러납니다.
       캐스터는 그 높이를 유지하고 물의 정령은 그러지 않습니다. */
    printf("\na flyer holds its height\n");
    {
        Level r = {0};
        Sector *s = &r.sectors[r.n_sectors++];
        short p[8] = { -2000,-2000,  2000,-2000,  2000,2000,  -2000,2000 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 1200;

        /* Two markers at the same height: one flyer, one that is not. */
        Entity *a = &r.ents[r.n_ents++];
        put_kind(a, "caster");
        a->x = -500; a->y = 600; a->z = -500;      /* six metres up */

        Entity *b = &r.ents[r.n_ents++];
        put_kind(b, "water_spirit");
        b->x = 500; b->y = 600; b->z = 500;

        enemy_spawn_level(&g_pools, &r);
        ok(enemy_count(&g_pools) == 2, "both markers made a monster");

        const Enemy *fly = 0, *walk = 0;
        for (int i = 0; i < enemy_count(&g_pools); i++) {
            const Enemy *m = enemy_at(&g_pools, i);
            if (m->type == MON_CASTER) fly = m; else walk = m;
        }
        ok(fly && walk, "one of each");
        if (fly && walk) {
            okf(fly->pos.y > 5.0f, "the caster spawns at its marker's height",
                fly->pos.y, 6.0f);
            okf(walk->pos.y < 0.1f, "and the water spirit is on the floor regardless",
                walk->pos.y, 0.0f);

            /* And it stays. A monster that merely started high and then sank is
               the failure this is really watching for.
               그리고 유지합니다. 높이 시작했다가 가라앉는 몬스터가 이 검사가 실제로 지켜보는
               실패입니다. */
            float y0 = fly->pos.y;
            for (int i = 0; i < 120; i++)
                enemy_update(&g_pools, &r, v3f(0, 1.7f, 0), 1.0f / 60.0f);
            okf(fabsf(fly->pos.y - y0) < 0.01f, "and holds it two seconds later",
                fly->pos.y, y0);
        }
    }

    /* --- and it keeps its body out of the wall ------------------------------
       ::MonType::radius SAYS "collision and hitscan radius" and for a long time
       only the second half was true. foot_ok and air_ok each asked about ONE
       column -- the monster's centre -- so a monster could walk until its
       middle touched the wall face with all of it inside the geometry. The
       player could never do that: player.c samples five points around
       PLAYER_RADIUS, and this sampled a point.

       It shows as a drawing bug. A monster is a billboard `height * aspect`
       wide that turns to face the camera, so half that width sweeps through
       whatever it is standing against -- for a brute, a metre of it. The
       billboard was doing exactly what it should; nothing was holding the body
       out of the wall.

       WHAT IS ASSERTED IS THE STANDOFF, NOT THE PIXELS. If the centre stays at
       least `radius` from the face, the most of itself a monster can bury is
       `height * aspect / 2 - radius`, which is a fact about the table and is
       checked as one below.

       그리고 몸을 벽 밖에 둡니다. ::MonType::radius는 "충돌 및 히트스캔 반경"이라고 말하지만
       오랫동안 뒤쪽 절반만 참이었습니다. foot_ok와 air_ok는 각각 기둥 *하나*, 몬스터의 중심만
       물었으므로, 몬스터는 자기 한가운데가 벽면에 닿을 때까지 걸어 들어갈 수 있었고 몸 전체가
       지오메트리 안에 있었습니다. 플레이어는 결코 그럴 수 없었습니다. player.c는 PLAYER_RADIUS
       둘레의 다섯 점을 표본하고 이곳은 점 하나를 표본했습니다.

       이것은 그리기 버그로 드러납니다. 몬스터는 `height * aspect` 너비의 빌보드이고 카메라를
       향해 돌므로, 그 너비의 절반이 기대 선 것을 휩씁니다. 브루트라면 1미터입니다. 빌보드는
       해야 할 일을 정확히 하고 있었고, 몸을 벽 밖에 붙들어 두는 것이 없었을 뿐입니다.

       *단언하는 것은 픽셀이 아니라 이격 거리입니다.* 중심이 벽면에서 최소 `radius`만큼 떨어져
       있으면 몬스터가 묻을 수 있는 최대치는 `height * aspect / 2 - radius`이며, 그것은 표에
       대한 사실이므로 아래에서 그렇게 검사합니다. */
    printf("\na monster keeps its body out of the wall\n");
    {
        for (int t = 0; t < MON_TYPES; t++) {
            const MonType *M = mon_stats(t);
            if (M->flags & MON_ANCHORED) continue;   /* it IS the wall */

            float gap = wall_gap(M);
            okf(gap > M->radius - 0.02f, M->name, gap, M->radius);

            /* The consequence, stated in the units the bug was reported in.
               버그가 보고된 단위로 표현한 결과입니다. */
            float half_w = M->height * M->aspect * 0.5f;
            okf(half_w - gap < half_w * 0.35f,
                "  and most of its sprite stays out of it",
                half_w - gap, half_w * 0.35f);
        }
    }

    /* --- everything that walks can walk the level ---------------------------
       A STAIRCASE IS A PROPERTY OF THE LEVEL, NOT OF WHO IS CLIMBING IT. An
       author builds risers to the height a player clears -- 16 map units, half
       a metre, the dimension brush.h keeps Quake's scale in order to inherit --
       so the step limit that matters is not "a third of this creature" but
       "what is this world made of".

       The hound was why, and the hound is gone. A third of its 1.25m was
       0.417m, under every riser in the game: it stopped dead at the first step
       of a staircase the brute walked up behind it, running on the spot. The
       kind whose whole design was that it closes on you was the one kind that
       could not follow you upstairs.

       THE TEST OUTLIVED IT DELIBERATELY. It walks every row MON_TYPES has, not
       a list of names, so what it asserts is the RULE rather than the case that
       taught it -- and the next short creature is covered on the day it is
       added rather than on the day somebody notices.

       TWO RISERS AND NOT A LONG FLIGHT, because a short creature at the foot of
       a tall staircase cannot SEE over it, and a monster that never noticed the
       player would fail this for a reason that has nothing to do with stepping.
       `sees` is asserted separately so that failure names itself instead of
       arriving disguised as a stuck monster.

       걷는 모든 것은 레벨을 걸을 수 있어야 합니다. 계단은 레벨의 속성이지 오르는 자의 속성이
       아닙니다. 제작자는 플레이어가 넘는 높이로 단을 만들며(16맵유닛, 0.5미터로, brush.h가
       Quake의 스케일을 지키며 물려받으려는 치수입니다), 따라서 중요한 단차 상한은 "이 생물의
       3분의 1"이 아니라 "이 세계가 무엇으로 이루어져 있는가"입니다.

       그 이유는 하운드였고, 하운드는 사라졌습니다. 1.25m의 3분의 1은 0.417m이었고 게임 안 모든
       단보다 낮았습니다. 브루트가 걸어 올라가는 계단의 첫 단에서 멈춰 서서 제자리걸음을 했습니다.
       달려든다는 것이 설계의 전부인 종류가, 위층까지 따라가지 못하는 유일한 종류였습니다.

       *검사는 의도적으로 그것보다 오래 남았습니다.* 이름 목록이 아니라 MON_TYPES의 모든 행을
       훑으므로, 단언하는 것은 그것을 가르친 사례가 아니라 *규칙*입니다. 다음에 올 키 작은
       생물은 누군가 알아채는 날이 아니라 추가되는 날에 함께 덮입니다.

       긴 계단이 아니라 단 둘인 이유는, 키 작은 생물이 높은 계단 아래에서는 그 너머를 *볼* 수
       없기 때문입니다. 플레이어를 알아채지도 못한 몬스터는 걸음과 무관한 이유로 이 검사에
       실패하게 됩니다. `sees`를 따로 단언하여, 그 실패가 갇힌 몬스터로 위장해 도착하지 않고
       스스로 이름을 대게 합니다. */
    printf("\nevery ground kind climbs a 0.5m riser\n");
    {
        /* 0.50m is the canonical riser tracetest calls climbable, and it is
           under PLAYER_STEP by design. 1.20m is over every kind's limit and is
           the control: without it this block would still pass if the step limit
           had become unbounded, which fixes stairs by deleting the walls.

           ONE riser for the control, not two: the top of a second one blocks
           the sight line, and a monster that cannot see the player does not
           walk at all -- so "did not climb" would stop meaning "could not" and
           the control would control nothing. 0.85m clears the largest limit
           there is (the brute's 0.783m) and not much more, because anything
           taller cannot be seen over.
           0.50m는 tracetest가 오를 수 있다고 부르는 표준 단 높이이며, 설계상 PLAYER_STEP
           아래입니다. 0.85m는 모든 종류의 상한을 넘으며 대조군입니다. 이것이 없으면 단차
           상한이 무한이 되어도 이 블록은 통과하는데, 그것은 벽을 지워서 계단을 고치는
           것입니다.

           대조군은 단이 *하나*입니다. 두 개면 두 번째 단의 윗면이 시야를 막아 몬스터가
           플레이어를 아예 보지 못하고, 그러면 "오르지 않았다"가 오르지 못해서가 아니라
           올라갈 이유를 몰라서가 되어 대조군이 아무것도 통제하지 않게 됩니다. 그리고 0.85m는
           가장 큰 상한(브루트의 0.783m)보다 겨우 넘습니다. 더 높이면 그 너머를 볼 수 없습니다. */
        const float RISER = 0.50f, WALL = 0.85f;
        check_step_fixture(RISER, WALL);

        for (int t = 0; t < MON_TYPES; t++) {
            const MonType *M = mon_stats(t);
            if (M->flags & (MON_FLIES | MON_ANCHORED)) continue;

            float got = climb(M, RISER, 2);
            okf(got > 2.0f * RISER - 0.05f, M->name, got, 2.0f * RISER);

            /* And the same kind, the same walk, against a riser nothing can
               climb. A monster that gets up THIS has stopped colliding.
               같은 종류가 같은 걸음으로, 무엇도 오를 수 없는 단을 만납니다. 이것을
               올라가는 몬스터는 충돌을 멈춘 것입니다. */
            got = climb(M, WALL, 1);
            okf(got < 0.05f, "  and is stopped by one nothing can climb", got, 0.0f);
        }
    }

    /* --- and its corpse does not -------------------------------------------
       THE OTHER HALF OF THE FLAG, and the half it did not have. MON_FLIES was
       read from the type alone, so it went on suppressing the fall after the
       monster stopped flying: a caster shot out of the air stayed at the height
       it died at, a sprite pinned to nothing for the rest of the level. Nothing
       on the ground could show it -- what dies standing is already standing on
       the floor -- so the flyer is the only kind that could ever have.

       Killed in the air and then given four seconds, the same budget the drop
       test below uses.

       플래그의 나머지 절반이며, 갖고 있지 않던 절반입니다. MON_FLIES를 타입만 보고 읽었으므로,
       몬스터가 나는 것을 그만둔 뒤에도 계속 낙하를 억제했습니다. 공중에서 격추된 캐스터는 죽은
       높이에 머물렀고, 레벨이 끝날 때까지 아무것에도 걸리지 않은 스프라이트로 남았습니다.
       지상의 어떤 것도 이것을 드러낼 수 없었습니다. 서서 죽는 것은 이미 바닥에 서 있기
       때문입니다. 그래서 이것을 드러낼 수 있던 종류는 비행체뿐입니다.

       공중에서 죽인 뒤 4초를 줍니다. 아래의 낙하 검사와 같은 예산입니다. */
    printf("\na flyer's corpse falls\n");
    {
        Level r = {0};
        Sector *s = &r.sectors[r.n_sectors++];
        short p[8] = { -2000,-2000,  2000,-2000,  2000,2000,  -2000,2000 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 1200;

        Entity *a = &r.ents[r.n_ents++];
        put_kind(a, "caster");
        a->x = 0; a->y = 600; a->z = 0;            /* six metres up */

        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &r);
        ok(enemy_count(&g_pools) == 1, "one caster in the air");

        const Enemy *m = enemy_at(&g_pools, 0);
        float died_at = m->pos.y;
        okf(died_at > 5.0f, "up there to begin with", died_at, 6.0f);

        while (enemy_alive(&g_pools)) enemy_hurt(&g_pools, 0, 7, v3f(0, 0, 1));
        ok(m->state == E_DEAD, "and shot down");
        okf(fabsf(m->pos.y - died_at) < 0.01f,
            "still at that height the frame it dies", m->pos.y, died_at);

        for (int i = 0; i < 240; i++)
            enemy_update(&g_pools, &r, v3f(0.0f, PLAYER_EYE, 300.0f), DT);

        okf(fabsf(m->pos.y) < 0.05f, "on the floor four seconds later",
            m->pos.y, 0.0f);

        /* ANCHORED IS THE OTHER ANSWER, and it must not have changed. A thing
           bolted to a wall is still bolted to it once it is dead, so the same
           corpse that falls as a caster must hang as a ward. The kind is
           written onto the slot rather than placed, because enemy_spawn_level
           refuses a MON_GUARD marker outright -- see its branch on that flag.
           What is under test here is the flag, not how a boss fight hands one
           out; tools/bosstest.c owns that.
           고정은 반대쪽 답이며, 바뀌지 않아야 합니다. 벽에 박힌 것은 죽은 뒤에도 여전히 박혀
           있으므로, 캐스터로서 떨어지는 그 시체가 결계핵으로서는 걸려 있어야 합니다. 종류를
           배치하지 않고 슬롯에 적어 넣는 이유는 enemy_spawn_level이 MON_GUARD 표식을 아예
           거절하기 때문입니다. 그 플래그에 대한 분기를 참조하십시오. 이곳에서 검사하는 것은
           플래그이지 보스전이 그것을 내주는 방식이 아닙니다. 그쪽은 tools/bosstest.c가
           맡습니다. */
        g_pools.enemy.m[0].type  = MON_WARD;
        g_pools.enemy.m[0].pos.y = 6.0f;
        g_pools.enemy.m[0].vel_y = 0.0f;

        for (int i = 0; i < 240; i++)
            enemy_update(&g_pools, &r, v3f(0.0f, PLAYER_EYE, 300.0f), DT);

        okf(fabsf(g_pools.enemy.m[0].pos.y - 6.0f) < 0.01f,
            "but an anchored corpse holds its height",
            g_pools.enemy.m[0].pos.y, 6.0f);
    }

    /* --- and an ordinary monster still falls -------------------------------
       The gate is a `continue` before the ground snap, so the way to get it
       wrong is to skip more than the fall. Dropped from well above the floor
       and given time to land.
       그 관문은 지면 스냅 앞의 `continue`이므로, 잘못되는 방식은 낙하보다 많은 것을 건너뛰는
       것입니다. 바닥에서 한참 위에서 떨어뜨리고 착지할 시간을 줍니다. */
    printf("\ngravity still applies to everything that has it\n");
    {
        build();
        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &L);
        ok(enemy_count(&g_pools) == 1, "one monster to drop");

        /* Lifted off the floor it spawned on, and left alone: the player is far
           enough away that nothing but the fall is acting on it.
           생성된 바닥에서 들어 올리고 내버려 둡니다. 플레이어가 충분히 멀어서 낙하 외에는
           아무것도 작용하지 않습니다. */
        g_pools.enemy.m[0].pos.y = 6.0f;
        g_pools.enemy.m[0].vel_y = 0.0f;

        for (int i = 0; i < 240; i++)
            enemy_update(&g_pools, &L, v3f(0.0f, PLAYER_EYE, 300.0f), DT);

        okf(fabsf(g_pools.enemy.m[0].pos.y) < 0.05f,
            "and it is on the floor four seconds later",
            g_pools.enemy.m[0].pos.y, 0.0f);
    }

    check_weave();

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall enemy checks passed\n", fails);
    return fails != 0;
}
