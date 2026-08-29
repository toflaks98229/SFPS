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
       brute as fast as the hound gets caught. */
    {
        const MonType *imp   = mon_stats(MON_WATER_SPIRIT);
        const MonType *brute = mon_stats(MON_BRUTE);
        const MonType *hound = mon_stats(MON_HOUND);

        ok(brute->hp > imp->hp * 2,      "the brute is far tougher than the imp");
        ok(brute->damage > imp->damage,  "and hits harder");
        ok(brute->speed < imp->speed,    "but is slower");
        ok(hound->speed > imp->speed,    "the hound is faster than the imp");
        ok(hound->hp < imp->hp,          "but frail");
        ok(hound->height < imp->height,  "and lower to the ground");
        ok(hound->aspect > imp->aspect,  "and stockier than the imp");

        ok(mon_type_for("water_spirit") == MON_WATER_SPIRIT,
           "entity 'water_spirit' spawns a water spirit");
        /* BOTH OLD NAMES STILL RESOLVE. `spawn` is from when there was one
           kind; `imp` is what this slot was called before the water spirit
           took it, and two shipped maps place it. A rename that emptied the
           levels already using it would be a rename nobody could make.
           옛 이름 둘 다 여전히 해석됩니다. `spawn`은 종류가 하나뿐이던 시절의 것이고, `imp`는
           물의 정령이 이 자리를 차지하기 전 이 슬롯의 이름이며 배포된 맵 둘이 그것을
           배치합니다. 이미 그것을 쓰고 있는 레벨을 비우는 이름 변경은 아무도 할 수 없는
           이름 변경입니다. */
        ok(mon_type_for("imp")   == MON_WATER_SPIRIT, "legacy 'imp' still resolves");
        ok(mon_type_for("spawn") == MON_WATER_SPIRIT, "and so does legacy 'spawn'");
        ok(mon_type_for("brute") == MON_BRUTE, "entity 'brute' spawns a brute");
        ok(mon_type_for("hound") == MON_HOUND, "entity 'hound' spawns a hound");
        ok(mon_type_for("caster")== MON_CASTER,"entity 'caster' spawns a caster");
        ok(mon_type_for("ammo")  < 0,          "a pickup entity spawns no monster");

        /* Ranged is defined by shot_speed alone -- there is no second "is
           ranged" flag that could disagree with it.
           원거리 여부는 shot_speed 하나로 정의됩니다. 그것과 어긋날 수 있는 두 번째
           "원거리인가" 플래그는 없습니다. */
        const MonType *cast = mon_stats(MON_CASTER);
        ok(cast->shot_speed > 0.0f && imp->shot_speed > 0.0f,
           "shot speed alone is what makes a type ranged");
        ok(brute->shot_speed == 0.0f && hound->shot_speed == 0.0f,
           "and the melee types carry none");

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
        ok(imp->attack > brute->attack * 2.0f,
           "the water spirit outranges every melee type");
        ok(imp->attack < cast->attack * 0.75f,
           "and still sits well inside the caster's, which is what mid means");

        /* And it sprays. A volley is not a faster single shot: one aimed bolt
           is answered by stepping aside and a cone is answered by leaving the
           cone, which is a different move. Without this the water spirit would
           be the caster at a shorter range.
           그리고 난사합니다. 일제 사격은 더 빠른 단발이 아닙니다. 조준된 볼트 하나에는 옆으로
           비켜서는 것으로 답하고 원뿔에는 그 원뿔에서 벗어나는 것으로 답합니다. 다른
           동작입니다. 이것이 없으면 물의 정령은 사거리만 짧은 캐스터입니다. */
        ok(imp->burst > 1 && imp->spread > 0.0f,
           "it sprays a cone rather than firing one aimed bolt");
        ok(cast->burst == 1 && brute->burst == 1,
           "while everything else still fires or swings once");
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

        enemy_spawn_level(&g_pools, &b);
        ok(enemy_at(&g_pools, 0)->type == MON_BRUTE, "the brute entity spawned a brute");
        int pellets = 0;
        while (enemy_alive(&g_pools) && pellets < 200) { enemy_hurt(&g_pools, 0, 7, v3f(0,0,1)); pellets++; }
        int imp_pellets = (int)ceilf(mon_stats(MON_WATER_SPIRIT)->hp / 7.0f);
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

        /* One frame at the imp's 220 deg/s is 3.67 deg = 0.064 rad. Checked
           generously against a quarter turn, because what must not happen is a
           SNAP -- the exact number is the table's business, the finiteness is
           this test's.
           임프의 220 deg/s로 한 프레임은 3.67도, 즉 0.064 rad입니다. 4분의 1 회전을
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
        /* A BRUTE, because it has to SURVIVE the test. With an imp's 40 hit
           points the monster died partway through and every frame after that
           was a corpse in E_DEAD -- which is not E_HURT, so the corpse scored
           as "acting" and lifted the ratio back over the threshold. The check
           now counts E_CHASE specifically rather than "anything but flinching",
           which is the same mistake stated as a rule: absence of one state is
           not presence of the one you meant.
           브루트를 씁니다. 몬스터가 검사를 *견뎌야* 하기 때문입니다. 임프의 체력 40으로는
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
           An imp walks 3 m/s and this runs 4 seconds, so 25m leaves it still
           walking at the end.
           측정 내내 추격 상태로 남아 있을 만큼 멉니다. 초안은 플레이어를 생성 지점에 두어
           몬스터가 플레이어 안에 서서 영구히 E_ATTACK이었습니다. 경직에 아예 진입하지
           않았고, "경직이 몬스터를 가두는가"를 검사하는 테스트가 검사 대상에 닿지 못한 채
           1.000 만점을 받았습니다. 임프는 초당 3m를 걷고 이 검사는 4초간 돌므로, 25m면
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

       That day is the wraith. What is worth asserting now is not that the
       column is sound but that the bit REACHES THE WORLD: a flag that stops a
       monster falling does nothing on its own, because make_monster put every
       monster on the floor before it could fall.

       MON_FLIES는 쓰는 것이 없는 여지였고, 이 블록은 `flying == 0`을 단언했습니다. 어떤
       크리처가 그 비트를 갖는 날을 위한 덫이었습니다. 그것을 추가하는 사람이 이곳에 와서 이제 그
       분기가 무엇을 하는지 말하게 하고, 조용히 아무 뜻도 없어진 검사를 남겨 두지 않게 하려는
       것이었습니다.

       그날이 레이스입니다. 이제 단언할 가치가 있는 것은 그 열이 온전한지가 아니라 그 비트가
       *세계에 닿는지*입니다. 몬스터가 떨어지는 것을 막는 플래그는 그 자체로는 아무것도 하지
       않습니다. make_monster가 떨어질 수 있기 전에 모든 몬스터를 바닥에 놓았기 때문입니다. */
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
        ok(mon_stats(MON_WRAITH)->flags & MON_FLIES, "and it is the wraith");
        ok((MON_FLAGS_ALL & MON_FLIES) != 0,
           "MON_FLAGS_ALL covers every bit there is");
    }

    /* --- a flyer is actually in the air -------------------------------------
       THE HALF THE FLAG DOES NOT COVER. MON_FLIES stops the fall; it does not
       put anything up there. Both are checked against the same marker height so
       a change to either shows here: the wraith holds it and the imp does not.
       플래그가 덮지 않는 절반입니다. MON_FLIES는 낙하를 멈추게 할 뿐 무언가를 위로 올려놓지는
       않습니다. 둘을 같은 표식 높이에 대고 검사하므로 어느 쪽이 바뀌어도 이곳에 드러납니다.
       레이스는 그 높이를 유지하고 임프는 그러지 않습니다. */
    printf("\na flyer holds its height\n");
    {
        Level r = {0};
        Sector *s = &r.sectors[r.n_sectors++];
        short p[8] = { -2000,-2000,  2000,-2000,  2000,2000,  -2000,2000 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 1200;

        /* Two markers at the same height: one flyer, one that is not. */
        Entity *a = &r.ents[r.n_ents++];
        const char *wn = "wraith";
        for (int i = 0; i < 7; i++) a->kind[i] = wn[i];
        a->x = -500; a->y = 600; a->z = -500;      /* six metres up */

        Entity *b = &r.ents[r.n_ents++];
        const char *in = "imp";
        for (int i = 0; i < 4; i++) b->kind[i] = in[i];
        b->x = 500; b->y = 600; b->z = 500;

        enemy_spawn_level(&g_pools, &r);
        ok(enemy_count(&g_pools) == 2, "both markers made a monster");

        const Enemy *fly = 0, *walk = 0;
        for (int i = 0; i < enemy_count(&g_pools); i++) {
            const Enemy *m = enemy_at(&g_pools, i);
            if (m->type == MON_WRAITH) fly = m; else walk = m;
        }
        ok(fly && walk, "one of each");
        if (fly && walk) {
            okf(fly->pos.y > 5.0f, "the wraith spawns at its marker's height",
                fly->pos.y, 6.0f);
            okf(walk->pos.y < 0.1f, "and the imp is on the floor regardless",
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

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall enemy checks passed\n", fails);
    return fails != 0;
}
