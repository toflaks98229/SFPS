/* wavetest -- the arena: waves, bursts, the telegraph, and the spawner that waits.
 *
 * ENGLISH
 * -------
 * The arena rules are all about TIMING, which is the one thing looking at the
 * game cannot check. "Monsters arrive in groups", "a spawner under your feet
 * holds off", "the wave ends when the last one falls and not in the gap before
 * it" are each a claim about which frame something happens on, and each fails
 * in a way that reads as feel rather than as a fault: waves that end early
 * simply seem short, and a spawner that never holds off simply seems unfair.
 *
 * So they are stepped here, frame by frame, with no window. The fixture is a
 * box with spawners placed by hand rather than `arena` loaded from disk, for
 * the reason movetest learned: a level is a thing somebody edits, and a test
 * that names its coordinates goes red on every edit.
 *
 * 한국어
 * ------
 * 아레나의 규칙은 전부 *타이밍*에 관한 것이며, 그것은 게임을 보는 것으로 검사할 수 없는 단
 * 하나입니다. "몬스터는 무리로 도착한다", "발밑의 스포너는 참는다", "웨이브는 마지막 하나가
 * 쓰러질 때 끝나며 그 직전의 빈틈에서가 아니다"는 각각 무언가가 *어느 프레임에* 일어나는지에
 * 대한 주장이고, 각각 결함이 아니라 감각처럼 읽히는 방식으로 실패합니다. 일찍 끝나는 웨이브는
 * 그저 짧아 보이고, 결코 참지 않는 스포너는 그저 불공평해 보입니다.
 *
 * 그래서 이곳에서 창 없이 프레임 단위로 진행시킵니다. 픽스처는 디스크에서 불러온 `arena`가
 * 아니라 손으로 스포너를 놓은 상자입니다. movetest가 배운 이유와 같습니다. 레벨은 누군가
 * 편집하는 것이고, 그 좌표를 적은 검사는 편집할 때마다 빨개집니다.
 */

#include <stdio.h>
#include <math.h>

#include "world.h"
#include "enemy.h"
#include "pickup.h"
#include "door.h"
#include "diag.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void oki(int cond, const char *what, int got, int want) {
    printf("  %-58s %5d / %-5d %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* One frame at a fixed rate. A real dt would make every count below depend on
   how fast the machine running the test is.
   고정 프레임 하나입니다. 실제 dt를 쓰면 아래의 모든 개수가 이 검사를 돌리는 기계의 속도에
   의존하게 됩니다. */
#define DT (1.0f / 60.0f)

static Input idle(void) { Input in = {0}; return in; }

static void step_n(World *w, int frames) {
    Input in = idle();
    for (int i = 0; i < frames; i++) world_step(w, &in, 1.6f, DT);
}

/* Steps with the player kept alive.
 *
 * ENGLISH: A wave with monsters out for thirty seconds kills a player who does
 * not shoot back, and a dead player freezes the world -- so a test of the WAVE
 * rules would silently become a test of how long the fixture survives. Topping
 * the health up is narrower than making the player invulnerable: the damage
 * still happens, ::step_damage still runs, and only the freeze is prevented.
 *
 * 한국어: 30초 동안 몬스터가 나와 있는 웨이브는 반격하지 않는 플레이어를 죽이고, 죽은
 * 플레이어는 월드를 정지시킵니다. 그러면 *웨이브* 규칙에 대한 검사가 조용히 픽스처가 얼마나
 * 버티는지에 대한 검사가 됩니다. 체력을 채우는 것은 무적으로 만드는 것보다 좁은 조치입니다.
 * 피해는 여전히 발생하고 ::step_damage도 여전히 돌며, 정지만 막습니다. */
static void step_alive(World *w, int frames) {
    Input in = idle();
    for (int i = 0; i < frames; i++) {
        w->player.health = PLAYER_MAX_HP;
        world_step(w, &in, 1.6f, DT);
    }
    w->player.health = PLAYER_MAX_HP;
}

/* A room, sized so a spawner can sit further than SPAWN_MIN_DIST from the
   middle of it and still be indoors.
   방입니다. 스포너가 한가운데에서 SPAWN_MIN_DIST보다 멀리 앉으면서도 실내일 수 있는
   크기입니다. */
static void box(Level *l, short x0, short z0, short x1, short z1) {
    l->n_sectors = 1;
    Sector *s = &l->sectors[0];
    s->n = 4;
    s->pts[0] = x0; s->pts[1] = z0;
    s->pts[2] = x1; s->pts[3] = z0;
    s->pts[4] = x1; s->pts[5] = z1;
    s->pts[6] = x0; s->pts[7] = z1;
    s->floor = 0; s->ceil = 3000;
    level_bounds(s);
    level_grid_build(l);
}

/* A spawner is an ENTITY, because that is how a level says one and going
   through the same parser is what makes this a test of the shipped path rather
   than of a struct this file filled in.
   스포너는 *엔티티*입니다. 레벨이 스포너를 말하는 방식이 그것이며, 같은 파서를 통과하는 것이
   이 검사를 이 파일이 채운 구조체가 아니라 출하 경로에 대한 검사로 만듭니다. */
static void add_spawner(Level *l, const char *kind, short x, short z,
                        short interval_tenths, short budget, short max_alive) {
    Entity *e = &l->ents[l->n_ents++];
    int i = 0;
    while (kind[i] && i < (int)sizeof e->kind - 1) { e->kind[i] = kind[i]; i++; }
    e->kind[i] = 0;
    e->x = x; e->y = 0; e->z = z;
    e->p[0] = interval_tenths;
    e->p[1] = budget;
    e->p[2] = max_alive;
}

static void fixture(World *w) {
    world_init(w);
    w->run.title = 0;
    box(&w->level, -4000, -4000, 4000, 4000);
    w->player.pos      = v3f(0.0f, PLAYER_EYE, 0.0f);
    w->player.grounded = 1;
    w->player.health   = PLAYER_MAX_HP;
    door_reset(&w->level);
}

int main(void) {
    printf("wavetest -- the arena\n");
    static World w;

    /* --- a level with no spawners is not an arena ------------------------
       The gate every ordinary level goes through, checked first because
       everything else in this file assumes it did not fire.
       모든 평범한 레벨이 지나는 관문이며, 이 파일의 나머지 전부가 이것이 발동하지 않았다고
       가정하므로 가장 먼저 검사합니다. */
    printf("\na level with no spawners\n");
    {
        fixture(&w);
        enemy_spawn_level(&w.pools, &w.level);
        step_n(&w, 600);
        oki(w.run.wave == 0, "never starts a wave", w.run.wave, 0);
        oki(enemy_alive(&w.pools) == 0, "and never spawns anything",
            enemy_alive(&w.pools), 0);
        ok(!enemy_wave_done(&w.pools),
           "and is not 'done', which would reward a level that has no waves");
    }

    /* --- wave 1 arms the spawners ---------------------------------------- */
    printf("\nstarting the arena\n");
    {
        fixture(&w);
        add_spawner(&w.level, "spawner_imp", 2000, 0, 10, 0, 0);
        enemy_spawn_level(&w.pools, &w.level);

        oki(enemy_spawner_count(&w.pools) == 1, "the level's spawner is read",
            enemy_spawner_count(&w.pools), 1);

        step_n(&w, 1);
        oki(w.run.wave == 1, "the first frame starts wave 1", w.run.wave, 1);
        oki(w.run.wave_best == 1, "and records it as the best reached",
            w.run.wave_best, 1);
        ok(!enemy_wave_done(&w.pools), "which is not immediately done");
    }

    /* --- the telegraph ---------------------------------------------------
       The rule this exists to protect: nothing arrives on the frame the
       spawner fires. Stepped to just before and just after the warning, so a
       change to SPAWN_WARN_TIME moves both edges together and the test keeps
       meaning the same thing.
       이것이 지키려는 규칙입니다. 스포너가 발동하는 프레임에는 아무것도 도착하지 않습니다.
       예고 직전과 직후로 진행시키므로, SPAWN_WARN_TIME을 바꾸면 두 경계가 함께 움직이고
       검사는 계속 같은 것을 뜻합니다. */
    printf("\nthe telegraph\n");
    {
        fixture(&w);
        add_spawner(&w.level, "spawner_imp", 2000, 0, 10, 0, 0);
        enemy_spawn_level(&w.pools, &w.level);

        /* One authored interval is p[0] tenths = 1.0s. */
        step_n(&w, 61);
        oki(enemy_alive(&w.pools) == 0,
            "nothing has arrived on the frame the spawner fires",
            enemy_alive(&w.pools), 0);

        step_n(&w, (int)(SPAWN_WARN_TIME * 60.0f) - 2);
        oki(enemy_alive(&w.pools) == 0, "and nothing during the warning",
            enemy_alive(&w.pools), 0);

        step_n(&w, 3);
        ok(enemy_alive(&w.pools) > 0, "and they land when the warning ends");
    }

    /* --- the group ------------------------------------------------------- */
    printf("\nhow many arrive at once\n");
    {
        fixture(&w);
        add_spawner(&w.level, "spawner_imp", 2000, 0, 10, 0, 0);
        enemy_spawn_level(&w.pools, &w.level);
        step_n(&w, 1);

        /* Wave 1 is one at a time by construction: burst is 1 + (wave-1)/N. */
        step_n(&w, 61 + (int)(SPAWN_WARN_TIME * 60.0f) + 2);
        oki(enemy_alive(&w.pools) == 1, "wave 1 sends them one at a time",
            enemy_alive(&w.pools), 1);

        /* Armed for a deep wave directly, rather than played up to one: this
           is a test of enemy_wave_arm's curve and not of how long it takes to
           get there.
           깊은 웨이브까지 플레이하는 대신 직접 장전합니다. 이것은 enemy_wave_arm의 곡선에
           대한 검사이지 거기까지 가는 데 걸리는 시간에 대한 검사가 아닙니다. */
        enemy_reset(&w.pools);
        enemy_spawn_level(&w.pools, &w.level);
        enemy_wave_arm(&w.pools, 7);
        int before = enemy_alive(&w.pools);
        step_n(&w, 200);
        ok(enemy_alive(&w.pools) - before > 1, "a later wave sends groups");
    }

    /* --- the spawner under your feet -------------------------------------
       THE RULE THAT IS INVISIBLE WHEN IT WORKS. A monster that materialises on
       the player is not difficulty; the hit lands before the telegraph can be
       read. Checked in both directions, because a spawner that holds off
       FOREVER is the same bug wearing the opposite sign.
       동작할 때는 보이지 않는 규칙입니다. 플레이어 위에 나타나는 몬스터는 난이도가 아닙니다.
       예고를 읽기도 전에 타격이 들어옵니다. 양방향으로 검사하는데, *영원히* 참는 스포너는
       부호만 반대인 같은 결함이기 때문입니다. */
    printf("\na spawner too close to the player\n");
    {
        fixture(&w);
        /* Well inside SPAWN_MIN_DIST of the player at the origin. */
        add_spawner(&w.level, "spawner_imp", 100, 100, 10, 0, 0);
        enemy_spawn_level(&w.pools, &w.level);

        step_n(&w, 600);
        oki(enemy_alive(&w.pools) == 0, "holds its fire while stood on",
            enemy_alive(&w.pools), 0);
        ok(!enemy_wave_done(&w.pools),
           "and the wave does not clear on a spawner that is merely waiting");

        /* Step away. The budget was never spent, so it is all still owed. */
        w.player.pos = v3f(30.0f, PLAYER_EYE, 30.0f);
        step_n(&w, 120);
        ok(enemy_alive(&w.pools) > 0, "and fires once the player leaves");
    }

    /* --- clearing a wave -------------------------------------------------- */
    printf("\nclearing a wave\n");
    {
        fixture(&w);
        add_spawner(&w.level, "spawner_imp", 2000, 0, 5, 0, 0);
        enemy_spawn_level(&w.pools, &w.level);
        step_n(&w, 1);

        /* Run until the spawner's budget is spent. */
        step_alive(&w, 2000);
        ok(enemy_alive(&w.pools) > 0, "monsters are out");
        ok(!enemy_wave_done(&w.pools),
           "the wave is not done while they are still walking");

        /* Kill them where they stand. Reaching into the pool rather than
           shooting: what is being checked is the wave rule, and routing it
           through the weapon would make this a test of two things.
           쏘는 대신 풀에 직접 손을 넣습니다. 검사 대상은 웨이브 규칙이며, 무기를 거치게
           하면 이것이 두 가지에 대한 검사가 됩니다. */
        for (int i = 0; i < w.pools.enemy.count; i++)
            w.pools.enemy.m[i].active = 0;

        ok(enemy_wave_done(&w.pools), "and is done once the last one falls");

        int was = w.run.wave;
        step_alive(&w, 1);
        ok(w.run.wave_break > 0.0f, "which starts the breather");
        oki(w.run.wave == was, "without advancing the wave yet",
            w.run.wave, was);

        /* The breather does NOT freeze the world -- it is when the reward is
           collected, so it has to be playable. */
        ok(!world_frozen(&w, 0), "and the world keeps running through it");

        step_alive(&w, (int)(WORLD_WAVE_BREAK * 60.0f) + 4);
        oki(w.run.wave == was + 1, "then the next wave begins",
            w.run.wave, was + 1);
        oki(w.run.wave_best == was + 1, "and the best reached follows it",
            w.run.wave_best, was + 1);
    }

    /* --- the curve --------------------------------------------------------
       Wave 10 must be the authored interval taken down TEN STEPS, not taken
       down once ten times over. Compounding is the bug Spawner::base_interval
       exists to stop, and it hides: the arena still works, it just reaches its
       floor in four waves and stops getting harder.
       웨이브 10은 제작된 간격에서 *열 단계* 내려간 것이어야지, 한 번 내리는 것을 열 번 한 것이
       아니어야 합니다. 복리는 Spawner::base_interval이 막으려고 존재하는 결함이며 숨습니다.
       아레나는 여전히 동작하고, 다만 네 웨이브 만에 하한에 닿아 더는 어려워지지 않습니다. */
    printf("\nthe difficulty curve\n");
    {
        fixture(&w);
        add_spawner(&w.level, "spawner_imp", 2000, 0, 40, 0, 0);   /* 4.0s */
        enemy_spawn_level(&w.pools, &w.level);

        enemy_wave_arm(&w.pools, 1);
        float i1 = w.pools.enemy.spawner[0].interval;
        int   b1 = w.pools.enemy.spawner[0].left;

        enemy_wave_arm(&w.pools, 2);
        float i2 = w.pools.enemy.spawner[0].interval;
        int   b2 = w.pools.enemy.spawner[0].left;

        ok(i2 < i1, "a later wave comes faster");
        ok(b2 > b1, "and sends more");

        /* Arming the same wave twice must give the same answer. If interval
           were derived from itself this would shrink on the second call.
           같은 웨이브를 두 번 장전하면 같은 답이 나와야 합니다. 간격을 자기 자신에서
           유도했다면 두 번째 호출에서 줄어듭니다. */
        enemy_wave_arm(&w.pools, 2);
        ok(w.pools.enemy.spawner[0].interval == i2,
           "and arming a wave twice is not harder than arming it once");

        /* And the floor holds rather than going negative. */
        enemy_wave_arm(&w.pools, 500);
        ok(w.pools.enemy.spawner[0].interval >= WAVE_INTERVAL_MIN,
           "a very deep wave still has a positive interval");
        ok(w.pools.enemy.spawner[0].left <= WAVE_BUDGET_MAX,
           "and a bounded budget");
        ok(w.pools.enemy.spawner[0].burst <= WAVE_BURST_MAX,
           "and a bounded group");
    }

    /* --- arming clears a telegraph in flight ------------------------------
       A wave that ended while a spawn was warned must not deliver that group
       into the breather.
       생성이 예고된 채로 끝난 웨이브가 그 무리를 휴식 시간으로 배달해서는 안 됩니다. */
    printf("\nre-arming\n");
    {
        fixture(&w);
        add_spawner(&w.level, "spawner_imp", 2000, 0, 10, 0, 0);
        enemy_spawn_level(&w.pools, &w.level);
        step_n(&w, 62);           /* into the warning */
        ok(w.pools.enemy.spawner[0].warn > 0.0f, "a warning is in flight");

        enemy_wave_arm(&w.pools, 2);
        ok(w.pools.enemy.spawner[0].warn == 0.0f, "and arming clears it");
    }

    /* --- the reward, and the arc that announces it -----------------------
       Diablo's drop. The claim worth checking is not that items appear -- that
       is one line -- but that they are IN THE AIR for a while and cannot be
       absorbed by the player standing on the drop point, which is what makes
       the arc visible at all.
       디아블로의 드롭입니다. 검사할 가치가 있는 주장은 아이템이 나타난다는 것이 아니라(그것은
       한 줄입니다) 그것들이 한동안 *공중에* 있으며 낙하 지점에 서 있는 플레이어에게 흡수될 수
       없다는 것입니다. 그것이 포물선을 애초에 보이게 만드는 것입니다. */
    printf("\nthe wave reward\n");
    {
        fixture(&w);
        add_spawner(&w.level, "spawner_imp", 2000, 0, 5, 0, 0);
        enemy_spawn_level(&w.pools, &w.level);
        w.weapon.owned[WP_SHOTGUN] = 1;
        w.weapon.ammo[WP_SHOTGUN]  = 0;
        step_alive(&w, 1);

        int before = pickup_count(&w.pools);
        step_alive(&w, 2000);
        for (int i = 0; i < w.pools.enemy.count; i++)
            w.pools.enemy.m[i].active = 0;
        step_alive(&w, 1);

        int after = pickup_count(&w.pools);
        ok(after > before, "clearing a wave throws items down");
        /* All of both budgets. The ammo goes round-robin over the weapons
           actually held, so a player with one gun gets all three boxes for it
           rather than one box and two nothings.
           두 예산 모두입니다. 탄약은 실제로 보유한 무기를 돌아가며 배정되므로, 총 한 자루를 든
           플레이어는 상자 하나와 빈자리 둘이 아니라 세 상자를 모두 그 총으로 받습니다. */
        oki(after - before == WORLD_WAVE_MEDKITS + WORLD_WAVE_AMMO,
            "the medkits and a box for every point of the ammo budget",
            after - before, WORLD_WAVE_MEDKITS + WORLD_WAVE_AMMO);

        /* IN THE AIR. The player is standing exactly where they were thrown
           from, so any of these that were collectable would already be gone.
           공중에 있습니다. 플레이어는 던져진 바로 그 자리에 서 있으므로, 이 중 획득 가능한
           것이 있었다면 이미 사라졌을 것입니다. */
        int flying = 0;
        for (int i = 0; i < w.pools.pickup.count; i++) {
            const Pickup *p = &w.pools.pickup.p[i];
            if (p->active && (p->vel.x || p->vel.y || p->vel.z)) flying++;
        }
        oki(flying == after - before, "and every one of them is in the air",
            flying, after - before);
        oki(w.player.health == PLAYER_MAX_HP,
            "with none of it absorbed on the frame it was thrown",
            w.player.health, PLAYER_MAX_HP);

        /* They land, and only then can be had. Health is topped up by
           step_alive, so ammo is what proves collection happened.
           떨어지고 나서야 가질 수 있습니다. 체력은 step_alive가 채우므로, 획득이 일어났음을
           증명하는 것은 탄약입니다. */
        step_alive(&w, 180);
        int still_flying = 0;
        for (int i = 0; i < w.pools.pickup.count; i++) {
            const Pickup *p = &w.pools.pickup.p[i];
            if (p->active && (p->vel.x || p->vel.y || p->vel.z)) still_flying++;
        }
        oki(still_flying == 0, "three seconds later they have all landed",
            still_flying, 0);

        /* THROWN CLEAR OF WHERE THE PLAYER STANDS, which is the other half of
           the arc's job: a reward that lands under your feet is one you never
           went and got. So the player has to walk to it, and that is checked by
           walking to it -- standing still and asserting the ammo went up would
           only pass if the drop had failed to travel.
           플레이어가 서 있는 자리 *밖으로* 던져지며, 그것이 포물선이 하는 일의 나머지
           절반입니다. 발밑에 떨어지는 보상은 가지러 간 적 없는 보상입니다. 그래서 플레이어는
           그곳까지 걸어가야 하며, 걸어가서 검사합니다. 가만히 서서 탄약이 늘었다고 단언하는
           것은 드롭이 이동에 실패했을 때에만 통과합니다. */
        int found = -1;
        for (int i = 0; i < w.pools.pickup.count; i++) {
            const Pickup *p = &w.pools.pickup.p[i];
            if (p->active && PK_AMMO_WEAPON(p->kind) == WP_SHOTGUN) { found = i; break; }
        }
        ok(found >= 0, "an ammo box is lying there");
        if (found >= 0) {
            v3 at = w.pools.pickup.p[found].pos;
            float away = (at.x - 0.0f) * (at.x - 0.0f) + (at.z - 0.0f) * (at.z - 0.0f);
            ok(away > PICKUP_RADIUS * PICKUP_RADIUS,
               "out of reach of where it was thrown from");

            w.player.pos = v3f(at.x, at.y + PLAYER_EYE, at.z);
            step_alive(&w, 2);
            ok(w.weapon.ammo[WP_SHOTGUN] > 0, "and collectable once walked to");
        }
    }

    /* --- a collected slot is reused --------------------------------------
       An arena rewards every wave and the pool holds PICKUP_MAX. Without reuse
       it fills in a dozen waves and DIAG_PICKUP_CAP fires for the rest of the
       run, which is a counter reporting a design that did not scale.
       아레나는 웨이브마다 보상하고 풀은 PICKUP_MAX를 담습니다. 재사용이 없으면 열몇 웨이브면
       가득 차고 남은 플레이 내내 DIAG_PICKUP_CAP이 발생합니다. 그것은 확장되지 않은 설계를
       보고하는 카운터입니다. */
    printf("\nthe pool does not fill up\n");
    {
        fixture(&w);
        int cap_before = diag_count(DIAG_PICKUP_CAP);

        /* Throw and collect, many more times than there are slots. */
        for (int round = 0; round < PICKUP_MAX * 3; round++) {
            pickup_toss(&w.pools, PK_HEALTH, v3f(0, 0, 0), v3f(0, 0, 0));
            for (int i = 0; i < w.pools.pickup.count; i++)
                w.pools.pickup.p[i].active = 0;
        }
        ok(w.pools.pickup.count <= PICKUP_MAX, "the count never passes the cap");
        oki(diag_count(DIAG_PICKUP_CAP) == cap_before,
            "and nothing was ever refused for want of room",
            diag_count(DIAG_PICKUP_CAP) - cap_before, 0);
    }

    printf("\n%s\n", fails ? "  FAILED" : "  passed");
    return fails ? 1 : 0;
}
