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
#include "loot.h"
#include "txt.h"    /* txt_eq -- an altar marker is found by its kind name */
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

/* How many items assets\loot.txt asks a cleared wave to throw AT THIS PLAYER.
 *
 * ENGLISH: A literal here -- "five" -- would go red the first time somebody
 * retunes the purse, which is the whole reason the purse moved into a text
 * file. So the expectation is read from the same file the game reads, and what
 * this section actually proves is the plumbing: that the wave pays what loot.txt
 * says, that `held` resolves against the roster rather than throwing boxes for
 * guns the player has not found, and that nothing is silently lost between the
 * table and the floor.
 *
 * 한국어: 이곳의 리터럴("다섯")은 누군가 몫을 처음 조정하는 순간 빨개지며, 몫이 텍스트
 * 파일로 옮겨 간 이유가 바로 그것입니다. 그래서 기대값을 게임이 읽는 것과 같은 파일에서
 * 읽습니다. 이 절이 실제로 증명하는 것은 배관입니다. 웨이브가 loot.txt가 말하는 것을
 * 지급하는가, `held`가 찾지도 못한 총의 상자를 던지는 대신 보유 목록에 비추어 해석되는가,
 * 그리고 표와 바닥 사이에서 조용히 사라지는 것이 없는가입니다. */
static int reward_size(const World *w) {
    const LootReward *r = loot_reward();
    int n = 0, gun = 0;

    for (int i = 0; i < r->n_items; i++)
        for (int k = 0; k < r->item[i].n; k++) {
            if (n >= LOOT_REWARD_MAX) break;
            int kind = r->item[i].kind;
            if (kind == LOOT_HELD) {
                kind = loot_held_kind(&w->weapon, &gun);
                if (kind < 0) break;
            }
            n++;
        }
    return n;
}

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

/* --- a kind has a ceiling of its own, and the rate has a dial --------------
 *
 * ENGLISH
 * -------
 * ::Spawner::max_alive COUNTS THE ROOM, WHICH IS NOT THE SAME QUESTION. A cap
 * of eight is satisfied by eight brutes exactly as well as by a mixed wave, so
 * the composition of a fight was decided by which spawner happened to win the
 * tick -- a race nobody is steering, and a fight the author did not write.
 * ::MonType::cap asks the other question.
 *
 * THE SECOND CLAIM IS THE ONE THAT ROTS QUIETLY: a refusal here must be "not
 * now" and not "never". If the spawner spent budget on a room that was full of
 * its own kind, every wave after the first would be quietly smaller, and
 * nothing would report it -- the wave would still complete, just with fewer
 * monsters than it owed.
 *
 * 한국어
 * ------
 * *::Spawner::max_alive는 방을 세며, 그것은 같은 질문이 아닙니다.* 상한 여덟은 섞인 웨이브만큼이나
 * 브루트 여덟으로도 충족되므로, 전투의 구성은 어느 스포너가 틱을 이겼는지가 정했습니다. 아무도
 * 조종하지 않는 경주이고, 제작자가 쓰지 않은 전투입니다. ::MonType::cap이 다른 쪽 질문을 합니다.
 *
 * *두 번째 주장이 조용히 썩는 쪽입니다.* 이곳의 거절은 "절대"가 아니라 "지금은 아니다"여야
 * 합니다. 스포너가 자기 종류로 가득 찬 방에 예산을 써 버리면 첫 웨이브 이후 모든 웨이브가 조용히
 * 작아지고, 아무도 그것을 보고하지 않습니다. 웨이브는 여전히 완료되며, 다만 빚진 것보다 적은
 * 몬스터로 완료됩니다.
 */
static void check_type_cap(void) {
    printf("\na kind has a ceiling of its own\n");

    const MonType *S = mon_stats(mon_type_for("brute"));
    printf("      brute cap is %d\n", S->cap);
    ok(S->cap > 0, "the brute declares one");

    World w;
    fixture(&w);
    /* Room for far more than the kind allows, so the only thing that can hold
       the number down is the per-kind ceiling. */
    add_spawner(&w.level, "spawner_brute", 2500, 2500, 1, 99, 0);
    enemy_spawn_level(&w.pools, &w.level);
    pickup_spawn_level(&w.pools, &w.level);

    step_alive(&w, 60 * 30);

    int brutes = enemy_alive_of(&w.pools, mon_type_for("brute"));
    oki(brutes <= S->cap,
        "and thirty seconds of a hungry spawner does not exceed it",
        brutes, S->cap);
    oki(brutes == S->cap, "and does reach it", brutes, S->cap);

    /* The budget was not spent on the refusals: the spawner still owes what it
       could not deliver, which is what makes the group arrive late instead of
       never.
       거절에 예산을 쓰지 않았습니다. 스포너는 전달하지 못한 것을 여전히 빚지고 있으며, 그것이
       무리를 영영이 아니라 늦게 도착하게 만드는 것입니다. */
    const Spawner *sp = enemy_spawner_at(&w.pools, 0);
    ok(sp && sp->left != 0,
       "and the spawner still owes the ones it could not place");
}

/* --- the rate is a dial, and it is not the boss's ------------------------- */
static void check_spawn_rate(void) {
    printf("\nthe spawn rate is a dial of its own\n");

    /* TIME TO THE FIRST ONE, not how many arrive. A count over a fixed window
       is confounded by everything else that can refuse a spawn -- the kind's
       ceiling, the room's, how close the player is standing -- and the first
       cut of this measured 3 against 4 and called it a difference. The wait
       before the first monster is the interval and nothing else.
       *몇 마리가 오는지가 아니라 첫 마리까지의 시간입니다.* 고정된 창 안의 개수는 스폰을
       거절할 수 있는 다른 모든 것(종류의 상한, 방의 상한, 플레이어가 얼마나 가까이 서 있는지)에
       의해 교란되며, 이 검사의 첫 판은 3 대 4를 재고 그것을 차이라고 불렀습니다. 첫 몬스터
       이전의 기다림은 간격이며 그 밖의 무엇도 아닙니다. */
    int frames[2];
    for (int fast = 0; fast < 2; fast++) {
        World w;
        fixture(&w);
        add_spawner(&w.level, "spawner_water_spirit", 2500, 2500, 40, 99, 0);
        enemy_spawn_level(&w.pools, &w.level);
        pickup_spawn_level(&w.pools, &w.level);
        w.pools.enemy.spawn_rate = fast ? 4.0f : 1.0f;

        int n = 0;
        for (; n < 60 * 20 && enemy_alive(&w.pools) == 0; n++)
            step_alive(&w, 1);
        frames[fast] = n;
    }
    printf("      first monster after %.2fs at 1x, %.2fs at 4x\n",
           frames[0] * DT, frames[1] * DT);
    ok(frames[1] * 2 < frames[0],
       "four times the rate arrives in well under half the time");

    /* AND A LEVEL CAN SAY IT, which is the half that makes this a feature
       rather than a field. ::Level::spawn_rate is a percent and world.c
       copies it onto the pool -- but only if it does so AFTER
       ::enemy_spawn_level, which opens with ::enemy_reset and puts the rate
       back to 1 along with the rest of the pool. The first cut assigned
       before that call and was erased one line later; the test above did not
       catch it, because it sets the field by hand and by hand happens after.
       *그리고 레벨이 그것을 말할 수 있습니다.* 이것을 필드가 아니라 기능으로 만드는 절반입니다.
       ::Level::spawn_rate는 퍼센트이고 world.c가 풀로 복사합니다. 다만 ::enemy_spawn_level
       *뒤*에 할 때만 그렇습니다. 그 함수는 ::enemy_reset으로 시작하며 풀의 나머지와 함께
       비율을 1로 되돌립니다. 첫 판은 그 호출 앞에 대입했고 한 줄 뒤에 지워졌습니다. 위의
       검사는 그것을 잡지 못했습니다. 손으로 필드를 설정하고, 손으로 하는 일은 그 뒤에
       일어나기 때문입니다. */
    {
        World lw;
        fixture(&lw);
        lw.level.spawn_rate = 250;
        add_spawner(&lw.level, "spawner_water_spirit", 2500, 2500, 40, 99, 0);
        enemy_spawn_level(&lw.pools, &lw.level);
        pickup_spawn_level(&lw.pools, &lw.level);
        lw.pools.enemy.spawn_rate =
            lw.level.spawn_rate > 0 ? lw.level.spawn_rate / 100.0f : 1.0f;
        ok(lw.pools.enemy.spawn_rate > 2.4f,
           "a level that authors 250 reaches the pool as 2.5");
    }

    /* AND IT IS NOT THE BOSS'S FIELD. ::spawn_slow is set and cleared by the
       boss fight; a difficulty folded into it would be erased the moment the
       maw died. They multiply independently, so both can be true at once.
       *그리고 보스의 필드가 아닙니다.* ::spawn_slow는 보스전이 설정하고 지웁니다. 그것에 접어
       넣은 난이도는 아귀가 죽는 순간 지워집니다. 둘은 독립적으로 곱해지므로 동시에 참일 수
       있습니다. */
    World w;
    fixture(&w);
    add_spawner(&w.level, "spawner_water_spirit", 2500, 2500, 40, 99, 0);
    enemy_spawn_level(&w.pools, &w.level);
    pickup_spawn_level(&w.pools, &w.level);
    w.pools.enemy.spawn_rate = 4.0f;
    w.pools.enemy.spawn_slow = 3.0f;          /* the boss, suppressing */
    int n = 0;
    for (; n < 60 * 20 && enemy_alive(&w.pools) == 0; n++) step_alive(&w, 1);
    printf("      and with the boss suppressing as well, %.2fs\n", n * DT);
    ok(n > frames[1],
       "the boss still suppresses a raised rate");
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
        add_spawner(&w.level, "spawner_water_spirit", 2000, 0, 10, 0, 0);
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
        add_spawner(&w.level, "spawner_water_spirit", 2000, 0, 10, 0, 0);
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
        add_spawner(&w.level, "spawner_water_spirit", 2000, 0, 10, 0, 0);
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
        add_spawner(&w.level, "spawner_water_spirit", 100, 100, 10, 0, 0);
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
        add_spawner(&w.level, "spawner_water_spirit", 2000, 0, 5, 0, 0);
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
        add_spawner(&w.level, "spawner_water_spirit", 2000, 0, 40, 0, 0);   /* 4.0s */
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
        add_spawner(&w.level, "spawner_water_spirit", 2000, 0, 10, 0, 0);
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
        add_spawner(&w.level, "spawner_water_spirit", 2000, 0, 5, 0, 0);
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
        /* The whole purse. `held` goes round-robin over the weapons actually
           held, so a player with one gun gets all of its boxes for that gun
           rather than one box and two nothings.
           몫 전체입니다. `held`는 실제로 보유한 무기를 돌아가며 배정되므로, 총 한 자루를 든
           플레이어는 상자 하나와 빈자리 둘이 아니라 그 총의 상자를 모두 받습니다. */
        oki(after - before == reward_size(&w),
            "everything loot.txt asks a cleared wave to pay",
            after - before, reward_size(&w));

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

    /* --- what a kill leaves behind ---------------------------------------
       The roll is enemytest's and loottest's; what is checked here is the half
       that spans two modules -- that a corpse's debt is noticed by the world on
       the frame it is incurred, resolved against the player's roster, and
       thrown as a real item at the spot the body fell.
       ::Enemy::drop is written directly rather than rolled for, so this passes
       or fails on the plumbing rather than on the shipped rates: a `chance 26`
       fixture would go red the first time somebody retunes the imp, and would
       have needed a thousand kills to be sure of anything anyway.
       굴림은 enemytest와 loottest의 것입니다. 이곳에서 검사하는 것은 두 모듈에 걸친
       절반입니다. 시체의 빚이 발생한 프레임에 월드에 의해 알아채지고, 플레이어의 보유 목록에
       비추어 해석되고, 몸이 쓰러진 자리에 실제 아이템으로 던져지는가입니다.
       ::Enemy::drop을 굴리지 않고 직접 쓰므로, 이 검사는 배포된 확률이 아니라 배관에 따라
       통과하거나 실패합니다. `chance 26` 픽스처는 누군가 임프를 처음 조정하는 순간 빨개지고,
       애초에 무엇이든 확신하려면 천 번의 처치가 필요했을 것입니다. */
    printf("\nwhat a kill leaves behind\n");
    {
        fixture(&w);
        add_spawner(&w.level, "spawner_water_spirit", 2000, 0, 3, 0, 0);
        enemy_spawn_level(&w.pools, &w.level);
        w.weapon.owned[WP_SHOTGUN] = 1;
        step_alive(&w, 1);

        /* Wait for one to exist, then hand it a debt and one frame. */
        step_alive(&w, 200);
        ok(w.pools.enemy.count > 0, "a monster is standing there");

        if (w.pools.enemy.count > 0) {
            int before = 0;
            for (int i = 0; i < w.pools.pickup.count; i++)
                if (w.pools.pickup.p[i].active) before++;

            v3 fell = w.pools.enemy.m[0].pos;
            w.pools.enemy.m[0].drop = PK_HEALTH;
            step_alive(&w, 1);

            int after = 0, at_body = 0;
            for (int i = 0; i < w.pools.pickup.count; i++) {
                const Pickup *p = &w.pools.pickup.p[i];
                if (!p->active) continue;
                after++;
                float dx = p->pos.x - fell.x, dz = p->pos.z - fell.z;
                if (p->kind == PK_HEALTH && dx*dx + dz*dz < 0.01f) at_body++;
            }
            oki(after == before + 1, "one item, on the frame the debt was set",
                after - before, 1);
            ok(at_body > 0, "and it is lying where the body fell");

            /* NOT AGAIN on the next frame, which is what a corpse sweeping into
               a fountain would look like from here.
               다음 프레임에는 *다시* 나오지 않습니다. 시체가 분수가 되는 것은 이곳에서
               그렇게 보입니다. */
            step_alive(&w, 20);
            int later = 0;
            for (int i = 0; i < w.pools.pickup.count; i++)
                if (w.pools.pickup.p[i].active) later++;
            oki(later <= after, "and the corpse is not still paying out",
                later, after);
        }

        /* `held` is resolved by the world and not thrown raw. A pickup whose
           kind is the pseudo-kind would be an item with no sprite and no rule
           for collecting it -- invisible in the game until somebody walks
           through it and nothing happens.
           `held`는 월드가 해석하며 날것으로 던져지지 않습니다. 종류가 의사 종류인 아이템은
           스프라이트도 획득 규칙도 없는 아이템이며, 누군가 그 위를 지나가도 아무 일이
           일어나지 않을 때까지 게임 안에서 보이지 않습니다. */
        if (w.pools.enemy.count > 0) {
            w.pools.enemy.m[0].drop = LOOT_HELD;
            step_alive(&w, 1);

            int raw = 0;
            for (int i = 0; i < w.pools.pickup.count; i++)
                if (w.pools.pickup.p[i].active &&
                    w.pools.pickup.p[i].kind == LOOT_HELD) raw++;
            oki(raw == 0, "`held` never reaches the floor unresolved", raw, 0);
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

    /* --- the shipped arena ------------------------------------------------
       Everything above runs on a box this file built, for the reason movetest
       learned: a level is a thing somebody edits and a test that names its
       coordinates goes red on every edit. So this section asks only what any
       ARENA must satisfy, whatever it is shaped like -- the same bargain
       tracetest strikes with the shipped level.

       What it is really proving is that the four changes underneath it meet:
       the level format carries a spawner's height, make_monster hands it to a
       flyer, move_toward lets the flyer keep it, and step_wave notices any of
       it happened. Each is checked alone above; only a real level checks that
       they are wired to each other.

       위의 모든 것은 이 파일이 만든 상자에서 돌아갑니다. movetest가 배운 이유와 같습니다.
       레벨은 누군가 편집하는 것이고 그 좌표를 적은 검사는 편집할 때마다 빨개집니다. 그래서 이
       구획은 아레나가 어떤 모양이든 만족해야 하는 것만 묻습니다. tracetest가 출하 레벨과 맺는
       것과 같은 거래입니다.

       실제로 증명하는 것은 그 아래의 네 변경이 서로 만난다는 사실입니다. 레벨 형식이 스포너의
       높이를 나르고, make_monster가 그것을 비행체에게 건네고, move_toward가 비행체가 그것을
       유지하게 하고, step_wave가 그 일이 일어났음을 알아챕니다. 각각은 위에서 따로 검사했고,
       그것들이 서로 연결되어 있는지는 실제 레벨만이 검사합니다. */
    printf("\nthe shipped arena\n");
    {
        world_init(&w);
        w.run.title = 0;

        if (!world_load_level(&w, WORLD_START_LEVEL, WORLD_ENTER_NEW)) {
            ok(0, "spire loads");
        } else {
            ok(1, "spire loads");
            ok(w.level.brushes != 0,
               "and is a brush level, which is what a floating ledge needs");

            /* Solid ground under the start, which every level owes whatever
               else it does. */
            float f, c;
            ok(level_ground(&w.level, w.player.pos.x, w.player.pos.z,
                            w.player.pos.y, 1e9f, &f, &c),
               "there is floor under the player start");

            int spawners = enemy_spawner_count(&w.pools);
            ok(spawners > 0, "it is an arena: at least one spawner");


            /* At least one mouth in the air. Asked of the SPAWNER rather than
               of a coordinate, so moving it in the editor changes nothing here
               as long as it stays a flyer's.
               좌표가 아니라 *스포너*에게 묻습니다. 에디터에서 옮겨도 비행체의 것으로 남아
               있는 한 이곳은 달라지지 않습니다. */
            int air = 0;
            for (int i = 0; i < spawners; i++) {
                const Spawner *s = &w.pools.enemy.spawner[i];
                if (mon_stats(s->type)->flags & MON_FLIES) air++;
            }
            ok(air > 0, "and at least one of them is a flyer's");

            /* Play it. Long enough for the slowest spawner to have fired and
               its telegraph to have landed. */
            step_alive(&w, 1);
            oki(w.run.wave == 1, "stepping it starts wave 1", w.run.wave, 1);

            step_alive(&w, 60 * 30);
            ok(enemy_alive(&w.pools) > 0, "and thirty seconds in, it is populated");

            /* THE POINT OF THE WHOLE LEVEL: something is up there. Measured
               against the floor beneath each monster rather than against a
               number, so it stays true if the arena is rebuilt taller.
               레벨 전체의 요점입니다. 무언가가 위에 있습니다. 숫자가 아니라 각 몬스터 아래의
               바닥에 대고 재므로, 아레나를 더 높게 다시 지어도 참으로 남습니다. */
            int airborne = 0;
            for (int i = 0; i < w.pools.enemy.count; i++) {
                const Enemy *m = &w.pools.enemy.m[i];
                if (!m->active) continue;
                float mf, mc;
                if (!level_ground(&w.level, m->pos.x, m->pos.z, m->pos.y, 1e9f, &mf, &mc))
                    continue;
                if (m->pos.y > mf + 1.0f) airborne++;
            }
            ok(airborne > 0, "and something is off the ground");
        }
    }

    /* --- every shipped arena has somewhere to be paid --------------------
       `at altar` falls back to the player's feet in a map that placed no
       marker, which is what makes it safe to leave switched on -- and is also
       exactly how this feature would ship doing nothing at all. THE FALLBACK IS
       INVISIBLE: the reward still arrives, still bounces, still gets collected,
       and the only thing missing is the shrine nobody knew to look for. Nothing
       in the game says which of the two happened.

       So the question is asked of the campaign rather than of one level. A
       level is an arena if it has spawners -- the same test ::step_wave makes --
       so an arena added later is covered by this without being added to a list,
       and an ordinary level is skipped without being excluded from one.

       `at altar`는 표식을 배치하지 않은 맵에서 플레이어의 발치로 되돌아가며, 그것이 이 기능을
       켜 둔 채로 두어도 안전한 이유이자, 이 기능이 아무 일도 하지 않는 채로 출하될 수 있는
       경위이기도 합니다. *되돌아감은 보이지 않습니다.* 보상은 여전히 도착하고, 튀고,
       획득되며, 빠진 것은 아무도 찾을 줄 몰랐던 제단뿐입니다. 게임 안의 무엇도 둘 중 어느
       쪽이 일어났는지 말해 주지 않습니다.

       그래서 질문을 레벨 하나가 아니라 캠페인에 던집니다. 스포너가 있으면 아레나이며,
       ::step_wave가 하는 것과 같은 판정입니다. 나중에 추가된 아레나는 목록에 넣지 않아도
       이곳에 포함되고, 평범한 레벨은 목록에서 빼지 않아도 건너뛰어집니다. */
    if (loot_reward()->at == LOOT_AT_ALTAR) {
        printf("\nevery shipped arena has somewhere to be paid\n");

        /* glasstower and lqdm13 were missing, and each for its own reason worth
           recording. glasstower is the boss arena -- it shipped with spawners
           and an altar and was never added here, so the room the story mode
           actually drops the player into was the one arena this sweep did not
           look at. lqdm13 is the imported one, and its shrine was not placed by
           a person at all: import-librequake.py puts it at a deathmatch start,
           on the argument that the map's author verified a player can stand
           there. That argument is worth exactly as much as a check, and this is
           the check -- on the ground, clear of the start, and not in the lava.
           glasstower와 lqdm13이 빠져 있었고, 각각 기록할 만한 자기 이유가 있습니다.
           glasstower는 보스 아레나입니다. 스포너와 제단을 갖고 출하되었는데 이곳에 추가된 적이
           없었으므로, 스토리 모드가 플레이어를 실제로 떨어뜨리는 그 방이 이 훑기가 보지 않는
           유일한 아레나였습니다. lqdm13은 가져온 것이며, 그 제단은 애초에 사람이 놓지
           않았습니다. import-librequake.py가 데스매치 시작점에 놓으며, 그 근거는 맵 제작자가
           그곳에 플레이어가 설 수 있음을 확인했다는 것입니다. 그 근거의 값어치는 정확히 검사
           하나만큼이고, 이것이 그 검사입니다. 바닥 위에 있고, 시작 지점에서 떨어져 있고,
           용암 속이 아닐 것. */
        static const char *const LEVELS[] = { "arena", "vault", "dm03",
                                              "lqdm4" };
        int arenas = 0;

        /* ONE ::world_init FOR THE WHOLE SWEEP, and it matters. A brush level
           claims one of ::LVL_BRUSH_SLOTS and a Level KEEPS its claim across
           loads, so five levels through one World cost one slot -- while
           re-initialising between them abandons a claim each time and the third
           .map in the list simply fails to load. Which it did, silently: the
           sweep skipped spire and still passed, because a level that does not
           load is not an arena and an arena nobody asked about cannot fail.
           전체 훑기에 ::world_init 하나이며, 그것이 중요합니다. 브러시 레벨은
           ::LVL_BRUSH_SLOTS 중 하나를 주장하고 Level은 로드를 거쳐도 자기 주장을 *유지하므로*,
           World 하나로 도는 레벨 다섯은 슬롯 하나가 듭니다. 그 사이에 다시 초기화하면 매번
           주장을 버리게 되고 목록의 세 번째 .map은 그냥 로드에 실패합니다. 실제로 그랬고,
           조용했습니다. 훑기가 spire를 건너뛰고도 통과했는데, 로드되지 않은 레벨은 아레나가
           아니고 아무도 묻지 않은 아레나는 실패할 수 없기 때문입니다. */
        world_init(&w);
        w.run.title = 0;

        for (int n = 0; n < (int)(sizeof LEVELS / sizeof LEVELS[0]); n++) {
            if (!world_load_level(&w, LEVELS[n], WORLD_ENTER_NEW)) {
                ok(0, "a level in the campaign loads");
                continue;
            }
            if (!enemy_spawner_count(&w.pools)) continue;   /* not an arena */
            arenas++;

            int altars = 0, grounded = 0, clear_of_start = 0, safe = 0;
            for (int i = 0; i < w.level.n_ents; i++) {
                const Entity *e = &w.level.ents[i];
                if (!txt_eq(e->kind, "altar")) continue;
                altars++;

                float x = e->x * 0.01f, z = e->z * 0.01f, af, ac;
                if (!level_ground(&w.level, x, z, e->y * 0.01f, 1e9f, &af, &ac))
                    continue;
                grounded++;

                /* Clear of the start, or the shrine is a lamp shining on the
                   spot the player was already standing on and the reward is
                   collected without a decision -- which is the arrangement
                   `at altar` exists to replace. Four collection radii, which is
                   a distance rather than a coordinate: moving the altar in the
                   editor changes nothing here as long as it stays somewhere the
                   player has to go.
                   출발 지점에서 떨어져 있어야 합니다. 그러지 않으면 제단은 플레이어가 이미
                   서 있던 자리를 비추는 등이고 보상은 결정 없이 획득되는데, 그것이야말로
                   `at altar`가 대체하려고 존재하는 배치입니다. 획득 반경 네 배이며, 좌표가
                   아니라 거리입니다. 에디터에서 제단을 옮겨도, 플레이어가 가야 하는 자리로
                   남아 있는 한 이곳은 달라지지 않습니다. */
                float dx = x - w.player.pos.x, dz = z - w.player.pos.z;
                if (dx*dx + dz*dz > PICKUP_RADIUS * PICKUP_RADIUS * 16.0f)
                    clear_of_start++;

                /* NOT STANDING IN A HAZARD, which is the mistake this level
                   actually made: spire's middle is a sunken square and the
                   obvious centrepiece to build a shrine on, and it is full of
                   lava. Every check above passed -- the altar was real, it was
                   on floor, it was a walk away -- and the feature worked
                   perfectly while paying the player into a fire.
                   The floor is what is asked about rather than the marker,
                   because that is where the items come to rest: an altar
                   hovering a metre over a pool still drops its purse in.
                   *위험 지형에 서 있지 않아야 합니다.* 이 레벨이 실제로 저지른 실수입니다.
                   spire의 한가운데는 움푹 팬 사각형이며 제단을 세우기에 뻔한 자리이고,
                   용암으로 가득 차 있습니다. 위의 모든 검사는 통과했습니다. 제단은 실재했고,
                   바닥에 있었고, 걸어갈 거리였습니다. 그리고 기능은 플레이어를 불 속으로
                   지급하면서 완벽하게 동작했습니다.
                   표식이 아니라 *바닥*에 대해 묻는 이유는 아이템이 내려앉는 곳이 그곳이기
                   때문입니다. 웅덩이 1미터 위에 떠 있는 제단도 자기 몫은 그 안에
                   떨어뜨립니다. */
                if (!level_hazard_at(&w.level, x, af, z)) safe++;
            }

            printf("  %s\n", LEVELS[n]);
            ok(altars > 0, "    has an altar for the reward to land on");
            oki(grounded == altars, "    and every one of them stands on floor",
                grounded, altars);
            ok(clear_of_start > 0, "    and at least one is somewhere to walk to");
            oki(safe == grounded, "    and none of them stands in a hazard",
                safe, grounded);
        }

        ok(arenas > 0, "the campaign still contains an arena to ask about");
    }

    check_type_cap();
    check_spawn_rate();

    printf("\n%s\n", fails ? "  FAILED" : "  passed");
    return fails ? 1 : 0;
}
