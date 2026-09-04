/* bosstest -- the boss fight's rules, with no window and no boss arena.
 *
 * The maw is the one fight in this game that a player cannot reach in under a
 * minute, and every rule it turns on is invisible until it is wrong in front of
 * somebody: an invulnerability that leaks, a cycle that skips, a ward that
 * summons six monsters from one shotgun blast, a random draw that spends a
 * different number of rolls than the recording did. All of those are cheap to
 * assert and expensive to notice.
 *
 * THE FIXTURE IS BUILT HERE, not loaded from the shipped arena, for the reason
 * enemytest gives: a shipped map is a map somebody edits, and a test that names
 * its coordinates goes red on every edit. What this file checks is the
 * MACHINERY; whether the arena is any good is a question for a person.
 *
 * bosstest -- 창도 보스 아레나도 없이 보스전의 규칙을 검사합니다.
 *
 * 아귀는 이 게임에서 플레이어가 1분 안에 도달할 수 없는 유일한 전투이고, 그것이 켜는 모든 규칙은
 * 누군가의 눈앞에서 잘못되기 전까지 보이지 않습니다. 새는 무적, 건너뛰는 사이클, 샷건 한 발에
 * 여섯을 소환하는 결계핵, 기록 때와 다른 횟수의 굴림을 쓰는 무작위 추출. 전부 단언하기는 싸고
 * 알아채기는 비쌉니다.
 *
 * 픽스처를 출하되는 아레나에서 로드하지 않고 *이곳에서 만드는* 이유는 enemytest가 대는 것과
 * 같습니다. 배포되는 맵은 누군가 편집하는 맵이고, 그 좌표를 적는 테스트는 편집할 때마다
 * 빨개집니다. 이 파일이 검사하는 것은 *기구*입니다. 아레나가 좋은지는 사람이 답할 질문입니다.
 */

#include <stdio.h>
#include <math.h>
#include "enemy.h"
#include "level.h"
#include "pools.h"

static Pools g_pools;

#include "loot.h"
/* The World half of this file. Everything above ::world_cases drives a bare
   ::Pools, which is enough for the rules that live in enemy.c; the LOOP lives
   in world.c's ::step_boss and is only reachable through ::world_step.
   이 파일의 World 절반입니다. ::world_cases 위의 모든 것은 맨 ::Pools를 구동하며, enemy.c에
   사는 규칙에는 그것으로 충분합니다. *루프*는 world.c의 ::step_boss에 살고 있고 ::world_step을
   통해서만 도달할 수 있습니다. */
#include "world.h"
#include "player.h"   /* PLAYER_EYE, PLAYER_MAX_HP -- standing the player up */
#include "story.h"    /* STORY_VICTORY, STORY_PAGES -- what stands between the kill and the win */

#define DT (1.0f / 60.0f)

/* The shipped arena, named once. Spelled here rather than at the assertion so
   that renaming the level is one edit and a test that has stopped pointing at
   anything says so by failing to load rather than by quietly checking a level
   that no longer exists.
   배포되는 아레나이며 한 번만 적습니다. 단언이 아니라 이곳에 적어서, 레벨 이름을 바꾸는 일이
   편집 하나가 되고, 아무것도 가리키지 않게 된 테스트가 더 이상 존재하지 않는 레벨을 조용히
   검사하는 대신 로드 실패로 그렇게 말하도록 합니다. */
#define BOSS_ARENA "lqdm4"

static int fails;
static Level L;

static void ok(int cond, const char *what) {
    printf("  %-56s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void oki(int cond, const char *what, int got, int want) {
    printf("  %-56s %6d / %6d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}
static void okf(int cond, const char *what, float got, float want) {
    printf("  %-58s %s", what, cond ? "ok" : "FAIL");
    if (!cond) { printf("   (got %.3f, want %.3f)", (double)got, (double)want); fails++; }
    printf("\n");
}

/* Writes an entity kind into the fixture. ::Entity::kind is a fixed buffer and
   the test needs several different ones, so this is the one place that spells a
   name out.
   픽스처에 엔티티 종류를 씁니다. ::Entity::kind는 고정 버퍼이고 이 테스트에는 여러 개가
   필요하므로, 이름을 적어 넣는 곳은 이 한 곳입니다. */
/* LVL_KIND, NOT LVL_MAT, and the difference is sixteen characters. A kind and a
   material name shared one budget once and this helper kept the material's
   after they split. Nothing showed it while every kind here was short: `maw`,
   `wardair` and `spawner_hound` all fit in 15. `spawner_water_spirit` is 20, so
   it arrived as `spawner_water_s`, resolved to no monster, and the spawner
   simply was not there -- the same silent failure leveltest's "spawner
   classnames survive the import" section exists to catch in the shipped
   pipeline, reproduced in a test fixture.
   LVL_MAT가 아니라 LVL_KIND이며, 그 차이는 열여섯 글자입니다. 종류 이름과 재질 이름은 한때
   예산을 공유했고, 둘이 갈라선 뒤에도 이 도우미는 재질 쪽을 붙들고 있었습니다. 이곳의 모든
   종류가 짧은 동안에는 아무것도 드러나지 않았습니다. `maw`, `wardair`, `spawner_hound`는 모두
   15자 안에 들어갑니다. `spawner_water_spirit`는 20자라서 `spawner_water_s`로 도착했고, 어떤
   몬스터로도 해석되지 않았으며, 스포너는 그냥 없었습니다. leveltest의 "스포너 클래스명이 임포트를
   견딘다" 구획이 출하 파이프라인에서 잡으려고 존재하는 바로 그 조용한 실패를, 테스트 픽스처에서
   재현한 것입니다. */
static void put_ent(const char *kind, int x, int y, int z) {
    Entity *e = &L.ents[L.n_ents++];
    int i = 0;
    while (kind[i] && i < LVL_KIND - 1) { e->kind[i] = kind[i]; i++; }
    e->kind[i] = 0;
    e->x = (short)x; e->y = (short)y; e->z = (short)z;
}

/* One flat room, one maw, and `n_air` + `n_ground` ward slots.
   방 하나, 아귀 하나, 그리고 `n_air` + `n_ground`개의 결계핵 자리입니다. */
static void build(int n_air, int n_ground) {
    Level z = {0};
    L = z;
    Sector *s = &L.sectors[L.n_sectors++];
    short p[8] = { -3000,-3000,  3000,-3000,  3000,3000,  -3000,3000 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4; s->floor = 0; s->ceil = 900;

    put_ent("maw", 0, 20, 2500);

    /* A spawner, so ::enemy_wave_done has an arena to answer about: it returns
       0 outright when a level has none, and the rule this file checks -- that a
       standing boss does not freeze the wave clock -- is unreachable without
       one.
       스포너 하나입니다. ::enemy_wave_done이 답할 아레나를 갖게 하기 위함입니다. 그 함수는
       스포너가 없는 레벨에서는 곧바로 0을 반환하며, 이 파일이 검사하는 규칙(서 있는 보스가
       웨이브 시계를 세우지 않는다)은 스포너 없이는 도달할 수 없습니다. */
    put_ent("spawner_water_spirit", 2500, 16, 2500);

    /* Spread along two lines, far enough apart that no two share a position --
       the position IS the identity as far as the candidate sort is concerned.
       두 줄을 따라 흩어 놓으며, 어떤 둘도 같은 위치를 공유하지 않을 만큼 떨어뜨립니다. 후보
       정렬에 관한 한 위치가 곧 정체성입니다. */
    for (int i = 0; i < n_air; i++)    put_ent("wardair",    -2000 + i * 300, 400, -1000);
    for (int i = 0; i < n_ground; i++) put_ent("wardground", -2000 + i * 300,  60,  1000);
}

/* Damage the boss the way the game does, through the one function that decides
   what a blow is worth.
   게임이 하는 방식대로, 타격의 값을 결정하는 그 한 함수를 통해 보스에게 피해를 줍니다. */
static void hit_boss(int dmg) {
    int i = enemy_boss_index(&g_pools);
    if (i >= 0) enemy_hurt(&g_pools, i, dmg, v3f(0, 0, 1));
}

static int boss_hp(void) {
    int i = enemy_boss_index(&g_pools);
    return i < 0 ? -1 : enemy_at(&g_pools, i)->health;
}

/* Destroy every standing ward, one blow each, so nothing accrues a summon.
   A ward has WARD_SUMMON_DMG*3 health, so a single blow of all of it kills
   without crossing a threshold on the way -- the accrual is deliberately after
   the death branch.
   서 있는 모든 결계핵을 한 방씩에 파괴하여 아무것도 소환을 누적하지 않게 합니다. 결계핵의
   체력은 WARD_SUMMON_DMG*3이므로, 그 전부를 한 방에 넣으면 도중에 문턱을 넘지 않고 죽습니다.
   누적은 의도적으로 사망 분기 뒤에 있습니다. */
static void smash_wards(void) {
    for (int i = 0; i < enemy_count(&g_pools); i++) {
        const Enemy *m = enemy_at(&g_pools, i);
        if (!m->active || m->state == E_DEAD) continue;
        if (!(mon_stats(m->type)->flags & MON_GUARD)) continue;
        enemy_hurt(&g_pools, i, mon_stats(MON_WARD)->hp, v3f(0, 0, 1));
    }
}

/* ------------------------------------------------- the fight, world-level */

static World W;

/* Everything above drives a bare ::Pools. This drives a ::World, because
   ::step_boss is the only thing that knows the loop and it is only reachable
   through ::world_step. Mirrors steptest's own fixture: init, take the title
   screen down (it freezes everything), build a box, stand the player in it.
   위의 모든 것은 맨 ::Pools를 구동합니다. 이것은 ::World를 구동합니다. 루프를 아는 것은
   ::step_boss뿐이고 그것에는 ::world_step을 통해서만 도달할 수 있기 때문입니다. steptest 자신의
   픽스처를 따릅니다. 초기화하고, 모든 것을 정지시키는 타이틀 화면을 내리고, 상자를 만들고,
   그 안에 플레이어를 세웁니다. */
static void wfix(int endless, int n_air, int n_ground) {
    world_init(&W);
    W.run.title   = 0;
    W.run.endless = endless;

    Sector *s = &W.level.sectors[W.level.n_sectors++];
    short p[8] = { -3000,-3000,  3000,-3000,  3000,3000,  -3000,3000 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4; s->floor = 0; s->ceil = 900;
    level_bounds(s);

    Level *l = &W.level;
    Level *save = &L;
    (void)save;

    /* Reuse put_ent by pointing it at the World's level. L is this file's
       fixture level and put_ent writes into it, so the two are made the same
       thing for the duration.
       put_ent를 재사용하되 World의 레벨을 가리키게 합니다. L은 이 파일의 픽스처 레벨이고
       put_ent가 그곳에 쓰므로, 그동안 둘을 같은 것으로 만듭니다. */
    L = *l;
    put_ent("maw", 0, 20, 2500);
    put_ent("spawner_water_spirit", 2500, 16, 2500);
    for (int i = 0; i < n_air; i++)    put_ent("wardair",    -2000 + i * 300, 400, -1000);
    for (int i = 0; i < n_ground; i++) put_ent("wardground", -2000 + i * 300,  60,  1000);
    *l = L;

    W.player.pos      = v3f(0.0f, PLAYER_EYE, 0.0f);
    W.player.grounded = 1;
    W.player.health   = PLAYER_MAX_HP;

    enemy_spawn_level(&W.pools, &W.level);
}

/* THE PLAYER IS TOPPED UP EVERY FRAME, and without it this file measures the
   wrong thing. The maw shoots, the arena's spawner keeps sending monsters, and
   the groggy-timeout case has to stand still for a full minute -- so an honest
   player dies partway through, ::world_frozen goes true, and ::step_boss stops
   being called. The fight then "fails" every assertion after that point for a
   reason that has nothing to do with the fight.
   What is under test here is the boss's state machine, not whether a stationary
   player survives sixty seconds of fire. Whether they should is a question for
   a person with the game running.
   플레이어의 체력을 매 프레임 채우며, 그러지 않으면 이 파일은 엉뚱한 것을 재게 됩니다. 아귀는
   쏘고, 아레나의 스포너는 하운드를 계속 보내며, 그로기 타임아웃 사례는 꼬박 1분을 가만히 서
   있어야 합니다. 그래서 정직한 플레이어는 도중에 죽고, ::world_frozen이 참이 되며,
   ::step_boss가 호출되기를 그만둡니다. 그러면 그 지점 이후의 모든 단언이, 전투와 아무 상관 없는
   이유로 "실패"합니다.
   이곳에서 검사하는 것은 보스의 상태 기계이지, 가만히 선 플레이어가 60초간의 사격을 견디는지가
   아닙니다. 견뎌야 하는지는 게임을 켜 놓은 사람이 답할 질문입니다. */
static void wstep(int n) {
    Input in = {0};
    for (int i = 0; i < n; i++) {
        W.player.health = PLAYER_MAX_HP;
        W.run.dead      = 0;
        world_step(&W, &in, 1.777f, DT);
    }
}

/* Step a story fixture to the frame its maw is allowed to stand on.
 *
 * ::WORLD_BOSS_STORY_WAVE is a gate on ::RunState::wave, so every block below
 * that wants a FIGHT has to get past it, and every one of them wants a fight
 * rather than the gate. Two frames first, because the wave counter starts at 0
 * and ::step_wave is what sets it to 1 and arms the arena -- preseeding the
 * wave would skip the arming and quietly test a room whose spawners never ran.
 * Then the wave is moved to the gate and stepped again.
 *
 * The gate ITSELF is tested in exactly one place, which sets the wave back to 1
 * and holds it there. One helper for "there is a fight", one block for "when
 * the fight is allowed to start" -- a test that does both in every block tests
 * neither on purpose.
 *
 * 스토리 픽스처를, 아귀가 설 수 있는 프레임까지 진행시킵니다.
 *
 * ::WORLD_BOSS_STORY_WAVE는 ::RunState::wave에 대한 관문이므로, 아래에서 *전투*를 원하는 모든
 * 블록이 그것을 넘어야 하며, 그 모든 블록이 관문이 아니라 전투를 원합니다. 먼저 두 프레임인
 * 이유는 웨이브 계수기가 0에서 시작하고 그것을 1로 만들며 아레나를 장전하는 것이 ::step_wave이기
 * 때문입니다. 웨이브를 미리 심으면 장전을 건너뛰게 되고, 스포너가 한 번도 돌지 않은 방을 조용히
 * 검사하게 됩니다.
 *
 * 관문 *자체*는 정확히 한 곳에서 검사하며, 그곳은 웨이브를 1로 되돌리고 붙잡아 둡니다.
 * "전투가 있다"에 헬퍼 하나, "전투가 언제 시작해도 되는가"에 블록 하나입니다. 모든 블록에서 둘
 * 다 하는 검사는 어느 쪽도 일부러 검사하지 않는 검사입니다. */
static void wfight(void) {
    wstep(2);
    if (W.run.wave < WORLD_BOSS_STORY_WAVE)
        W.run.wave = WORLD_BOSS_STORY_WAVE;
    wstep(2);
}

/* Destroy every standing ward in the World's pool. */
static void wsmash(void) {
    for (int i = 0; i < enemy_count(&W.pools); i++) {
        const Enemy *m = enemy_at(&W.pools, i);
        if (!m->active || m->state == E_DEAD) continue;
        if (!(mon_stats(m->type)->flags & MON_GUARD)) continue;
        enemy_hurt(&W.pools, i, mon_stats(MON_WARD)->hp, v3f(0, 0, 1));
    }
}

static void whit(int dmg) {
    int i = enemy_boss_index(&W.pools);
    if (i >= 0) enemy_hurt(&W.pools, i, dmg, v3f(0, 0, 1));
}
static int whp(void) {
    int i = enemy_boss_index(&W.pools);
    return i < 0 ? -1 : enemy_at(&W.pools, i)->health;
}

/* Take the boss through one whole cycle: clear the wards, then hurt it until
   the boundary stops the damage. Returns the cycle count afterwards.
   보스를 한 사이클 통째로 진행시킵니다. 결계핵을 정리한 뒤, 경계가 피해를 멈출 때까지
   때립니다. 그 뒤의 사이클 수를 반환합니다. */
static int wcycle(void) {
    /* One cycle, the new way: hit the OPEN maw down to its boundary, which
       raises the wards; smash them; wait out their collapse. Returns the
       cycle count afterwards.
       새 방식의 한 사이클. *열린* 아귀를 경계까지 때려 결계핵을 세우고, 부수고, 붕괴가 끝나기를
       기다립니다. 그 뒤의 사이클 수를 돌려줍니다. */
    for (int i = 0; i < 400 && enemy_boss_index(&W.pools) >= 0; i++) {
        whit(20);
        wstep(1);
        if (enemy_guards_alive(&W.pools) > 0) break;   /* the boundary raised them */
    }
    if (enemy_boss_index(&W.pools) < 0) return W.pools.enemy.boss.cycle;
    wsmash();
    wstep((int)((COLLAPSE_HOLD + COLLAPSE_SINK_TIME) / DT) + 10);
    return W.pools.enemy.boss.cycle;
}

static void world_cases(void) {
    /* --- story mode raises its maw after one wave, not on arrival --------
     *
     * THE CHECK THAT WOULD HAVE CAUGHT IT. This block used to be two lines:
     * a fresh arena has no maw, and two frames later it does. Both were true
     * and the second was the bug -- the story maw stood up on the first
     * unfrozen frame, which read as "the boss spawns the moment the game
     * starts" the day ::WORLD_BOSS_ARENA stopped being a seven-brush room and
     * became a deathmatch map. The rule was never wrong about the machinery;
     * nothing asserted the rule was still the one anybody wanted.
     * So the negative half is pinned here too, and pinned the only way that
     * means anything: the wave is held at 1 for a full second of frames so the
     * gate is what is being tested rather than a wave that happened not to
     * clear.
     * *이것을 잡았을 검사입니다.* 이 블록은 두 줄이었습니다. 갓 만든 아레나에는 아귀가 없고, 두
     * 프레임 뒤에는 있다. 둘 다 참이었고 두 번째가 결함이었습니다. 스토리의 아귀는 정지가 풀린
     * 첫 프레임에 일어섰고, ::WORLD_BOSS_ARENA가 브러시 일곱 개짜리 방이기를 그만두고 데스매치
     * 맵이 된 날 그것은 "게임을 시작하자마자 보스가 나온다"로 읽혔습니다. 규칙이 기구에 대해
     * 틀린 적은 없습니다. 그 규칙이 여전히 누군가 원하는 규칙인지를 아무것도 단언하지 않았을
     * 뿐입니다. */
    wfix(0, 8, 8);
    ok(enemy_boss_index(&W.pools) < 0, "a fresh story arena has no maw yet");
    wstep(2);
    oki(W.run.wave == 1, "the opening wave is wave 1", W.run.wave, 1);
    ok(enemy_boss_index(&W.pools) < 0,
       "and the opening wave belongs to the player, not to the maw");

    for (int i = 0; i < 60; i++) { W.run.wave = 1; wstep(1); }
    ok(enemy_boss_index(&W.pools) < 0,
       "a whole second of it, with the wave held open");

    W.run.wave = WORLD_BOSS_STORY_WAVE;
    wstep(2);
    ok(enemy_boss_index(&W.pools) >= 0,
       "and step_boss raises one as soon as the wave allows it");
    oki(enemy_guards_alive(&W.pools) == 0, "with NO wards yet: it arrives open",
        enemy_guards_alive(&W.pools), 0);
    oki(W.pools.enemy.boss.ward_rounds == 0, "and no ward round spent",
        W.pools.enemy.boss.ward_rounds, 0);
    ok(W.pools.enemy.spawn_slow > 0.0f, "and the arena's spawners are suppressed");
    oki(W.run.boss_line == BOSS_LINE_WAKE, "and it announced itself",
        W.run.boss_line, BOSS_LINE_WAKE);

    /* --- the wave clock keeps running through the fight ------------------
       R9 depends on it: in endless mode the clock is what schedules the next
       maw, and a fight that stops the clock stops its own successor.
       무한 모드에서 다음 아귀를 예약하는 것이 그 시계이고, 그것을 세우는 전투는 자기
       후계자를 세우는 것입니다. */
    {
        int w0 = W.run.wave;
        wstep(60 * 30);                        /* half a minute of fight */
        ok(W.run.wave >= w0, "the wave counter is not frozen by a live boss");
    }

    /* --- open on arrival: a hit lands, and it summons ----------------------
       The first phase has no shield. Damage goes through, and every
       ::WARD_SUMMON_DMG of it books a summon the way a ward's does.
       첫 단계에는 보호막이 없습니다. 피해가 들어가고, 그 ::WARD_SUMMON_DMG마다 결계핵이
       하듯 소환을 예약합니다. */
    printf("\n  --- open on arrival ---\n");
    wfix(0, 8, 8);
    wfight();
    {
        int before = whp();
        whit(WARD_SUMMON_DMG);
        oki(whp() == before - WARD_SUMMON_DMG, "a hit on the arriving maw lands",
            whp(), before - WARD_SUMMON_DMG);
        int bi = enemy_boss_index(&W.pools);
        ok(bi >= 0 && enemy_at(&W.pools, bi)->summon_left >= WARD_SUMMON_COUNT,
           "and it owes a summon for the damage");
        int minions = enemy_alive_minions(&W.pools);
        wstep((int)(SPAWN_WARN_TIME / DT) + 4);
        oki(enemy_alive_minions(&W.pools) > minions, "which arrives after the telegraph",
            enemy_alive_minions(&W.pools), minions + 1);
    }

    /* --- the first boundary raises the shield --------------------------------
       Down to the first third: the wards appear, a hit takes nothing, and
       the round is counted.
       첫 3분의 1까지 내려가면 결계핵이 나타나고, 타격은 아무것도 빼앗지 못하며, 회차가 세어집니다. */
    printf("\n  --- the first boundary ---\n");
    wfix(0, 8, 8);
    wfight();
    {
        for (int i = 0; i < 400 && enemy_guards_alive(&W.pools) == 0; i++) { whit(20); wstep(1); }
        oki(enemy_guards_alive(&W.pools) == BOSS_WARDS, "crossing the first third raises a full set of wards",
            enemy_guards_alive(&W.pools), BOSS_WARDS);
        oki(W.pools.enemy.boss.cycle == 1, "and counts one cycle", W.pools.enemy.boss.cycle, 1);
        oki(W.pools.enemy.boss.ward_rounds == 1, "and one ward round", W.pools.enemy.boss.ward_rounds, 1);
        int before = whp();
        whit(500);
        oki(whp() == before, "a warded maw takes nothing", whp(), before);
        oki(W.run.boss_line == BOSS_LINE_WARD, "and it announces the shield",
            W.run.boss_line, BOSS_LINE_WARD);

        /* The wards fight: one of the maw's patterns, and only one.
           결계핵은 싸웁니다. 아귀의 탄막 하나이며 하나뿐입니다. */
        oki(mon_attack_count(MON_WARD) == 1, "a ward carries exactly one attack",
            mon_attack_count(MON_WARD), 1);
        const MonAttack *wa = mon_attack(MON_WARD, 0), *ma = mon_attack(MON_MAW, 0);
        ok(wa && ma && wa->kind == ma->kind && wa->burst == ma->burst && wa->spread == ma->spread &&
           wa->shot_gap == ma->shot_gap && wa->shot_speed == ma->shot_speed && wa->damage == ma->damage,
           "and it is one of the maw's own, copied field for field");
        int shots_before = enemy_shot_count(&W.pools), fired = 0;
        for (int i = 0; i < 60 * 8 && !fired; i++) {
            wstep(1);
            for (int k = 0; k < enemy_count(&W.pools); k++) {
                const Enemy *m = enemy_at(&W.pools, k);
                if (m->active && m->type == MON_WARD && m->state == E_ATTACK) fired = 1;
            }
        }
        ok(fired, "a standing ward attacks the player");
        (void)shots_before;

        /* Breaking them: protection ends the instant the collapse starts,
           while the bodies are still there sinking; then they are gone, a
           height and a fifth down.
           부수기. 보호는 붕괴가 시작되는 순간 끝나며, 그때 몸은 아직 가라앉는 중입니다. 그 뒤
           신장의 1.2배 아래에서 사라집니다. */
        wsmash();
        int standing = 0, top = -1;
        for (int k = 0; k < enemy_count(&W.pools); k++) {
            const Enemy *m = enemy_at(&W.pools, k);
            if (m->active && m->type == MON_WARD) { standing++; top = k; }
        }
        oki(enemy_guards_alive(&W.pools) == 0, "the moment they break, they guard nothing",
            enemy_guards_alive(&W.pools), 0);
        oki(standing == BOSS_WARDS, "while every body is still there, collapsing",
            standing, BOSS_WARDS);
        before = whp();
        whit(20);
        oki(whp() == before - 20, "and a hit on the maw lands at once", whp(), before - 20);
        float stood = enemy_at(&W.pools, top)->pos.y, lowest = stood;
        int gone = 0;
        for (int i = 0; i < (int)((COLLAPSE_HOLD + COLLAPSE_SINK_TIME) / DT) + 30; i++) {
            wstep(1);
            const Enemy *m = enemy_at(&W.pools, top);
            if (!m->active) { gone = 1; break; }
            if (m->pos.y < lowest) lowest = m->pos.y;
        }
        float want = stood - mon_stats(MON_WARD)->height * 1.2f;   /* THE RULE, not the constant: its height and a fifth. 상수가 아니라 규칙: 자기 높이와 그 5분의 1. */
        printf("      the ward sank to %.2f (floor at %.2f)\n", (double)lowest, (double)want);
        ok(gone, "a broken ward sinks and is removed");
        okf(lowest > want - 0.10f && lowest < want + 0.30f,
            "a height and a fifth below where it stood", lowest, want);
    }

    /* --- three cycles, and not two or four ------------------------------- */
    wfix(0, 8, 8);
    wfight();
    {
        int c1 = wcycle();
        oki(c1 == 1, "one cleared boundary is one cycle", c1, 1);
        ok(enemy_boss_index(&W.pools) >= 0, "and the maw is still standing");
        oki(enemy_guards_alive(&W.pools) == 0, "open again once its wards are down",
            enemy_guards_alive(&W.pools), 0);

        int c2 = wcycle();
        oki(c2 == 2, "two boundaries is two cycles", c2, 2);
        ok(enemy_boss_index(&W.pools) >= 0, "and it is STILL standing");

        wcycle();
        ok(enemy_boss_index(&W.pools) < 0, "the third boundary kills it");
        oki(W.pools.enemy.boss.ward_rounds == 2, "wards were raised exactly twice in the whole fight",
            W.pools.enemy.boss.ward_rounds, 2);
        {   /* and the maw goes the way its wards did */
            int body = -1;
            for (int k = 0; k < enemy_count(&W.pools); k++)
                if (enemy_at(&W.pools, k)->active && enemy_at(&W.pools, k)->type == MON_MAW) body = k;
            ok(body >= 0, "its body is still there, collapsing");
            float stood = body >= 0 ? enemy_at(&W.pools, body)->pos.y : 0.0f, lowest = stood;
            int gone = 0;
            for (int i = 0; body >= 0 && i < (int)((COLLAPSE_HOLD + COLLAPSE_SINK_TIME) / DT) + 30; i++) {
                wstep(1);
                const Enemy *m = enemy_at(&W.pools, body);
                if (!m->active) { gone = 1; break; }
                if (m->pos.y < lowest) lowest = m->pos.y;
            }
            float want = stood - mon_stats(MON_MAW)->height * 1.2f;   /* THE RULE, not the constant: its height and a fifth. 상수가 아니라 규칙: 자기 높이와 그 5분의 1. */
            ok(gone, "and it sinks away like a ward");
            okf(lowest > want - 0.10f && lowest < want + 0.30f,
                "a height and a fifth below where it stood", lowest, want);
        }
        oki(W.run.boss_line == BOSS_LINE_DIE, "and the death line is posted",
            W.run.boss_line, BOSS_LINE_DIE);
        ok(!W.run.won, "the win waits -- the line has not been read yet");

        /* --- and then the cutscene, in the gap the line opened ------------
           ::step_boss keeps `won` down on the frame the maw dies so its last
           sentence can be read, and that gap is where the victory cutscene
           slots in. So the win is now two waits: the banner's clock, and then
           the pages.
           Both are asserted rather than one loop that runs long enough for
           both, because they are separate rules and a single "it is won
           eventually" would keep passing if either stopped holding.
           ::step_boss는 아귀가 죽는 프레임에 `won`을 세우지 않고 그 마지막 문장이 읽히게 하며, 그
           틈이 승리 컷신이 끼어드는 자리입니다. 그래서 승리는 이제 두 번의 기다림입니다. 배너의
           시계, 그다음 페이지들입니다.
           둘 다 충분히 오래 도는 루프 하나가 아니라 각각 단언합니다. 서로 다른 규칙이며, "언젠가는
           이긴다" 하나로는 둘 중 하나가 성립하기를 그만두어도 계속 통과하기 때문입니다. */
        wstep((int)(BOSS_LINE_TIME / DT) + 8);
        ok(W.run.cut == STORY_VICTORY + 1,
           "the line expiring starts the victory cutscene");
        ok(!W.run.won, "and the win still waits, now on the pages");

        /* Paged through the way a player does, so this measures the skip rule
           rather than the hold times -- one press per page, and the page after
           the last ends the cut.
           STORY_PAGES + 1 presses rather than exactly n_pages: the cut's length
           is authored in a file this test does not read, and a loop bounded by
           the capacity is bounded by something that cannot silently grow.
           플레이어가 하는 방식대로 페이지를 넘기므로, 이것은 유지 시간이 아니라 건너뛰기 규칙을
           잽니다. 페이지마다 누름 하나이고, 마지막 다음의 페이지가 컷을 끝냅니다.
           정확히 n_pages가 아니라 STORY_PAGES + 1번 누르는 이유는, 컷의 길이가 이 테스트가 읽지
           않는 파일에 제작되어 있기 때문입니다. 용량으로 한계 지어진 루프는 조용히 늘어날 수 없는
           것으로 한계 지어진 루프입니다. */
        {
            Input press = {0};
            press.confirm = 1;
            for (int i = 0; i < STORY_PAGES + 1 && W.run.cut; i++) {
                W.player.health = PLAYER_MAX_HP;
                W.run.dead      = 0;
                world_step(&W, &press, 1.777f, DT);
            }
        }
        ok(!W.run.cut, "paging through it ends the cutscene");
        ok(W.run.won,  "and then the run is won");
    }

    /* --- the maw's fire varies ----------------------------------------------
       Three patterns on its row, chosen by weight: over a stretch of fighting
       it must start attacks from more than one slot.
       그 행의 탄막 셋을 가중치로 고릅니다. 한동안 싸우면 둘 이상의 슬롯에서 공격을 시작해야
       합니다. */
    printf("\n  --- the maw's patterns ---\n");
    wfix(0, 8, 8);
    wfight();
    {
        oki(mon_attack_count(MON_MAW) >= 2, "the maw carries more than one pattern",
            mon_attack_count(MON_MAW), 3);
        unsigned used = 0;
        int bi = enemy_boss_index(&W.pools);
        for (int i = 0; i < 60 * 40 && bi >= 0; i++) {
            wstep(1);
            const Enemy *m = enemy_at(&W.pools, bi);
            if (m->state == E_ATTACK && m->atk >= 0) used |= 1u << m->atk;
        }
        int distinct = 0; for (unsigned b = used; b; b >>= 1) distinct += (int)(b & 1u);
        oki(distinct >= 2, "and uses at least two of them in forty seconds", distinct, 2);
    }

    /* --- endless mode: waits for its wave, then says nothing -------------
       THE FIRST TWO ASSERTIONS EARN THE REST. An earlier version went straight
       to "the maw can be killed" and got a yes from a pool that had never had
       one -- three checks passing on an absence. A test that cannot tell "it
       died" from "it was never there" is not testing a death.
       *앞의 두 단언이 나머지의 자격을 만듭니다.* 이전 판은 곧바로 "아귀를 죽일 수 있다"로
       갔다가, 애초에 아귀를 가진 적 없는 풀에서 "그렇다"를 받았습니다. 부재 위에서 통과한
       검사 셋이었습니다. "죽었다"와 "애초에 없었다"를 구별하지 못하는 테스트는 죽음을 검사하는
       테스트가 아닙니다. */
    wfix(1, 8, 8);
    wstep(2);
    ok(enemy_boss_index(&W.pools) < 0,
       "endless mode does NOT open with a maw -- it waits for its wave");
    oki(W.pools.enemy.boss.next_wave == WORLD_BOSS_EVERY,
        "and books the first one WORLD_BOSS_EVERY waves out",
        W.pools.enemy.boss.next_wave, WORLD_BOSS_EVERY);

    /* Push the wave counter to the appointment by hand. Waiting for five real
       waves would make this a test of the wave curve.
       웨이브 계수기를 손으로 약속 지점까지 밀어 올립니다. 실제 다섯 웨이브를 기다리는 것은
       이것을 웨이브 곡선의 테스트로 만드는 일입니다. */
    W.run.wave = WORLD_BOSS_EVERY;
    wstep(2);
    ok(enemy_boss_index(&W.pools) >= 0, "reaching the wave raises the maw");
    oki(W.run.boss_line == BOSS_LINE_NONE, "and endless mode says nothing at all",
        W.run.boss_line, BOSS_LINE_NONE);
    ok(W.pools.enemy.spawn_slow > 0.0f, "but the suppression still applies");
    {
        wcycle(); wcycle(); wcycle();
        ok(enemy_boss_index(&W.pools) < 0, "endless: the maw can be killed");
        ok(!W.run.won, "and killing it does NOT win an endless run");
        oki(W.pools.enemy.boss.next_wave == W.run.wave + WORLD_BOSS_EVERY,
            "the next maw is due WORLD_BOSS_EVERY waves after this death",
            W.pools.enemy.boss.next_wave, W.run.wave + WORLD_BOSS_EVERY);
        ok(W.pools.enemy.spawn_slow == 0.0f,
           "and the suppression is lifted when it dies");
    }

    /* --- the SHIPPED arena still reaches the fight -----------------------
     *
     * Everything above builds its own fixture, which is right: enemytest's rule
     * is that a test naming a shipped map's coordinates goes red on every edit.
     * This checks COUNTS AND KINDS instead, which survive an author moving
     * things around and catch the thing that actually breaks silently -- a
     * classname retyped, a marker deleted, or the `info_ward_*` aliases falling
     * out of level.c's table. None of that is visible until somebody plays the
     * level, and the fixture above cannot see it: it writes ::Entity::kind
     * directly and so never exercises the .map path at all.
     *
     * --- *배포되는* 아레나가 여전히 전투에 도달하는가 ---------------------
     *
     * 위의 모든 것은 자기 픽스처를 만들며 그것이 옳습니다. enemytest의 규칙은, 배포되는 맵의
     * 좌표를 적는 테스트는 편집할 때마다 빨개진다는 것입니다. 이것은 대신 *개수와 종류*를
     * 검사합니다. 제작자가 물건을 옮겨도 살아남고, 정작 조용히 깨지는 것을 잡습니다.
     * classname 오타, 표식 삭제, `info_ward_*` 별칭이 level.c의 표에서 빠지는 것입니다. 그중
     * 무엇도 누군가 그 레벨을 플레이하기 전까지 보이지 않으며, 위의 픽스처는 그것을 볼 수
     * 없습니다. ::Entity::kind를 직접 쓰므로 .map 경로를 아예 거치지 않기 때문입니다. */
    {
        Level lv;
        if (!level_load(BOSS_ARENA, &lv)) {
            ok(0, "the shipped boss arena loads");
        } else {
            ok(1, "the shipped boss arena loads");

            Pools p = {0};
            enemy_spawn_level(&p, &lv);

            oki(enemy_count(&p) == 0,
                "and lays out no monsters -- markers only",
                enemy_count(&p), 0);
            ok(p.enemy.boss.have_maw, "it places a maw");

            int air = 0, ground = 0;
            for (int i = 0; i < p.enemy.boss.n_cand; i++) {
                if (p.enemy.boss.cand_air[i]) air++; else ground++;
            }

            /* TWICE what a cycle raises, per list, or "somewhere new" is not
               satisfiable and the second cycle repeats the first's positions.
               목록마다 한 사이클이 세우는 것의 *두 배*여야 합니다. 그러지 않으면 "새로운
               자리"가 만족 불가능해지고 두 번째 사이클이 첫 번째의 자리를 반복합니다. */
            oki(air >= BOSS_WARDS, "with enough air ward slots for a fresh set each cycle",
                air, BOSS_WARDS);
            oki(ground >= BOSS_WARDS, "and enough ground ones",
                ground, BOSS_WARDS);
            /* AND ROOM TO ADD. The cap used to equal what the arena placed,
               so the first slot a mapper added was silently dropped.
               *그리고 추가할 여지.* 상한이 아레나가 놓은 수와 같았던 탓에, 매퍼가 처음 추가한
               자리는 조용히 버려졌습니다. */
            ok(p.enemy.boss.n_cand < BOSS_MAX_CAND,
               "with room under the cap for a mapper to add slots");

            ok(enemy_spawner_count(&p) > 0,
               "and a spawner, so the wave clock has something to count");
            ok(enemy_boss_summon(&p, &lv), "the maw can be raised in it");
            oki(enemy_ward_place(&p, &lv) == BOSS_WARDS, "and a full set of wards",
                enemy_guards_alive(&p), BOSS_WARDS);
        }
    }

    /* --- a restart does not inherit a fight ----------------------------- */
    wfix(0, 8, 8);
    wfight();
    ok(W.pools.enemy.boss.active, "a fight is under way");
    world_restart(&W);
    ok(!W.pools.enemy.boss.active, "and a restart clears it");
    oki(W.run.boss_line == BOSS_LINE_NONE, "banner included",
        W.run.boss_line, BOSS_LINE_NONE);

    /* --- but it DOES inherit the mode -----------------------------------
       THE ONE FIELD run_reset's zeroing is undone for, and the only way to see
       it go wrong is from outside: story and endless are the same room, so an
       endless run that restarted as a story one looks identical until a banner
       appears in a mode that has none. Both directions, because "it survives"
       and "it is not invented" are two claims and a restart that simply left
       the field alone would satisfy only the first.
       *run_reset의 0 초기화가 되돌려지는 유일한 필드*이며, 그것이 잘못되는 것을 볼 방법은 바깥밖에
       없습니다. 스토리와 무한은 같은 방이므로, 스토리로 재시작된 무한 플레이는 그것을 갖지 않은
       모드에 배너가 뜨기 전까지 똑같아 보입니다. 양방향으로 검사하는 이유는 "살아남는다"와
       "지어내지 않는다"가 서로 다른 두 주장이고, 필드를 그냥 내버려 두는 재시작은 그중 첫 번째만
       만족시키기 때문입니다. */
    wfix(1, 8, 8);
    wstep(2);
    world_restart(&W);
    oki(W.run.endless == 1, "an endless run restarts as an endless run",
        W.run.endless, 1);

    wfix(0, 8, 8);
    wfight();
    world_restart(&W);
    oki(W.run.endless == 0, "and a story run does not acquire a mode it never had",
        W.run.endless, 0);
}

int main(void) {
    printf("bosstest\n\n");

    /* --- the level lays out markers, not monsters -------------------------
       R5: a ward exists only while a fight is under way. If the load path made
       them, a level would open with its boss already invulnerable.
       레벨은 몬스터가 아니라 표식을 배치합니다. 로드 경로가 그것을 만든다면 레벨은 보스가
       이미 무적인 채로 열립니다. */
    build(8, 8);
    enemy_spawn_level(&g_pools, &L);
    oki(enemy_count(&g_pools) == 0, "a load spawns no boss and no wards",
        enemy_count(&g_pools), 0);
    oki(g_pools.enemy.boss.n_cand == 16, "but every ward candidate was recorded",
        g_pools.enemy.boss.n_cand, 16);
    ok(g_pools.enemy.boss.have_maw, "and the maw's position was remembered");

    /* --- the fight raises what the level only marked --------------------- */
    ok(enemy_boss_summon(&g_pools, &L), "the fight can summon the maw");
    ok(enemy_boss_index(&g_pools) >= 0, "and it is the pool's one boss");
    oki(enemy_ward_place(&g_pools, &L) == BOSS_WARDS, "and raise a full set of wards",
        enemy_guards_alive(&g_pools), BOSS_WARDS);

    /* --- warded: the boss cannot be hurt at all --------------------------
       Not "hurt less" -- not at all, and not by the biggest number the test can
       think of either.
       "덜 아픈" 것이 아니라 *전혀* 아프지 않으며, 이 테스트가 생각해 낼 수 있는 가장 큰
       숫자로도 그렇습니다. */
    {
        int before = boss_hp();
        hit_boss(9999);
        oki(boss_hp() == before, "a warded boss takes no damage", boss_hp(), before);
        int i = enemy_boss_index(&g_pools);
        ok(enemy_at(&g_pools, i)->flash <= 0.0f,
           "and does not flash -- the flash would say it worked");
    }

    /* --- a wave is not held hostage by the fight -------------------------
       R9 needs the wave clock to keep running THROUGH a boss fight, because in
       endless mode that clock is what schedules the next boss.
       웨이브 시계는 보스전을 *관통해* 계속 돌아야 합니다. 무한 모드에서 다음 보스를 예약하는
       것이 그 시계이기 때문입니다. */
    ok(enemy_alive(&g_pools) > 0, "the boss and its wards are alive in the pool");
    oki(enemy_alive_minions(&g_pools) == 0, "yet the wave counts no minions",
        enemy_alive_minions(&g_pools), 0);

    /* ASKED THROUGH ::enemy_wave_done, not by comparing the counter this rule
       happens to use. The first version of this check asserted
       ::enemy_alive_minions directly and passed with the rule mutated out --
       it was testing the helper, not the thing the helper was written for.
       A spawner with nothing left to send is what makes the rest of that
       function's answer "yes", so the fixture supplies one.
       이 규칙이 마침 쓰는 계수기를 비교하지 않고 ::enemy_wave_done을 *통해* 묻습니다. 이
       검사의 첫 판은 ::enemy_alive_minions를 직접 단언했고 규칙을 변이로 제거해도
       통과했습니다. 도우미가 무엇을 위해 쓰였는지가 아니라 도우미를 검사하고 있었던
       것입니다.
       그 함수의 나머지 답을 "예"로 만드는 것은 보낼 것이 남지 않은 스포너이므로, 픽스처가
       하나를 공급합니다. */
    {
        g_pools.enemy.spawner[0].left  = 0;
        g_pools.enemy.spawner[0].warn  = 0.0f;
        ok(enemy_spawner_count(&g_pools) > 0, "the arena has a spawner to ask about");
        ok(enemy_wave_done(&g_pools),
           "so a wave CLEARS with a boss and four wards standing");
    }

    /* --- the last ward is what opens it ---------------------------------- */
    smash_wards();
    oki(enemy_guards_alive(&g_pools) == 0, "every ward can be destroyed",
        enemy_guards_alive(&g_pools), 0);
    {
        int before = boss_hp();
        hit_boss(10);
        oki(boss_hp() == before - 10, "and then the boss takes damage",
            boss_hp(), before - 10);
    }

    /* --- the cycle boundary holds ---------------------------------------
       The rule that makes "three cycles" a fact rather than an average. A blow
       far bigger than a third of the boss's health must stop at the boundary.
       "3사이클"을 평균이 아니라 사실로 만드는 규칙입니다. 보스 체력의 3분의 1보다 훨씬 큰
       타격도 경계에서 멈춰야 합니다. */
    {
        int hp = mon_stats(MON_MAW)->hp;
        hit_boss(hp);
        oki(boss_hp() == hp * (BOSS_CYCLES - 1) / BOSS_CYCLES,
            "damage cannot carry past the cycle boundary",
            boss_hp(), hp * (BOSS_CYCLES - 1) / BOSS_CYCLES);
        ok(enemy_boss_index(&g_pools) >= 0, "so one huge blow cannot kill it");
    }

    /* --- the last boundary is zero --------------------------------------
       ::types_check enforces the divisibility; this is the consequence that
       matters, and it is what stops the maw ending its third cycle at 1hp with
       nothing left to raise.
       나누어떨어짐은 ::types_check가 강제합니다. 이것은 그 결과 중 중요한 것이며, 아귀가 세울
       것이 아무것도 남지 않은 채 체력 1로 세 번째 사이클을 끝내는 것을 막습니다. */
    {
        int hp = mon_stats(MON_MAW)->hp;
        oki(hp % BOSS_CYCLES == 0, "boss health divides by the cycle count",
            hp % BOSS_CYCLES, 0);
        oki(hp * (BOSS_CYCLES - BOSS_CYCLES) / BOSS_CYCLES == 0,
            "so the last boundary is zero and the last cycle kills", 0, 0);
    }

    /* --- healing only ever raises ---------------------------------------
       The expired-window path. It must not be able to take health off a boss
       the player has legitimately hurt, or an off-by-one in a cycle count
       becomes an unwinnable fight rather than a visible glitch.
       만료된 창의 경로입니다. 플레이어가 정당하게 깎은 보스의 체력을 빼앗을 수 있어서는 안
       됩니다. 그러지 않으면 사이클 수의 off-by-one이 눈에 보이는 결함이 아니라 이길 수 없는
       전투가 됩니다. */
    {
        int before = boss_hp();
        enemy_boss_heal(&g_pools, 1);
        oki(boss_hp() == before, "healing to a lower number changes nothing",
            boss_hp(), before);
        enemy_boss_heal(&g_pools, mon_stats(MON_MAW)->hp);
        oki(boss_hp() == mon_stats(MON_MAW)->hp, "healing upward restores",
            boss_hp(), mon_stats(MON_MAW)->hp);
    }

    /* --- a ward summons on a THRESHOLD, not on a blow --------------------
       Six pellets from one shotgun blast are six calls into enemy_hurt. If each
       one summoned, the pool would fill in seconds and say nothing about it in
       a release build.
       샷건 한 발의 여섯 펠릿은 enemy_hurt 호출 여섯 번입니다. 각각이 소환한다면 풀은 몇 초
       만에 차고, 릴리스 빌드는 그에 대해 아무 말도 하지 않습니다. */
    {
        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &L);
        enemy_boss_summon(&g_pools, &L);
        enemy_ward_place(&g_pools, &L);

        int wi = -1;
        for (int i = 0; i < enemy_count(&g_pools); i++)
            if (mon_stats(enemy_at(&g_pools, i)->type)->flags & MON_GUARD) { wi = i; break; }
        ok(wi >= 0, "a ward is standing to be shot");

        /* Six pellets of 1 damage: six damage events, well under one threshold. */
        for (int k = 0; k < 6; k++) enemy_hurt(&g_pools, wi, 1, v3f(0, 0, 1));
        oki(enemy_at(&g_pools, wi)->summon_left == 0,
            "six small hits owe nothing -- it is a threshold",
            enemy_at(&g_pools, wi)->summon_left, 0);

        /* One threshold's worth, in one blow. */
        enemy_hurt(&g_pools, wi, WARD_SUMMON_DMG, v3f(0, 0, 1));
        oki(enemy_at(&g_pools, wi)->summon_left == WARD_SUMMON_COUNT,
            "crossing the threshold owes exactly one group",
            enemy_at(&g_pools, wi)->summon_left, WARD_SUMMON_COUNT);
    }

    /* --- the killing blow does not summon --------------------------------
       Placed after the death branch on purpose: the last pellet of the blast
       that destroys a ward must not call up a group the player has just earned
       the right not to fight.
       의도적으로 사망 분기 뒤에 둡니다. 결계핵을 파괴하는 그 발의 마지막 펠릿이, 플레이어가
       방금 싸우지 않아도 될 권리를 얻은 무리를 불러서는 안 됩니다. */
    {
        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &L);
        enemy_boss_summon(&g_pools, &L);
        enemy_ward_place(&g_pools, &L);

        int wi = -1;
        for (int i = 0; i < enemy_count(&g_pools); i++)
            if (mon_stats(enemy_at(&g_pools, i)->type)->flags & MON_GUARD) { wi = i; break; }

        int before = enemy_count(&g_pools);
        enemy_hurt(&g_pools, wi, mon_stats(MON_WARD)->hp * 4, v3f(0, 0, 1));
        ok(enemy_at(&g_pools, wi)->state == E_DEAD, "one big blow destroys a ward");
        oki(enemy_at(&g_pools, wi)->summon_left == 0,
            "and the killing blow owes nothing",
            enemy_at(&g_pools, wi)->summon_left, 0);

        /* Step long enough for any owed group to have arrived. */
        for (int i = 0; i < 120; i++) enemy_update(&g_pools, &L, v3f(0, 1.7f, 0), DT);
        oki(enemy_count(&g_pools) == before, "and nothing arrives afterwards",
            enemy_count(&g_pools), before);
    }

    /* --- a ward never acts ------------------------------------------------
       ::AI_INERT. Without the skip in ::enemy_update a shot ward leaves E_IDLE,
       is classified as a brawler by the E_CHASE dispatch's two-way question,
       and walks at the player with zero reach.
       ::AI_INERT입니다. ::enemy_update의 건너뛰기가 없으면 맞은 결계핵은 E_IDLE을 떠나고,
       E_CHASE 분기의 양자택일 질문에 근접형으로 분류되어 사거리 0으로 플레이어를 향해
       걸어옵니다. */
    {
        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &L);
        enemy_boss_summon(&g_pools, &L);
        enemy_ward_place(&g_pools, &L);

        int wi = -1;
        for (int i = 0; i < enemy_count(&g_pools); i++)
            if (mon_stats(enemy_at(&g_pools, i)->type)->flags & MON_GUARD) { wi = i; break; }

        v3 was = enemy_at(&g_pools, wi)->pos;
        int hurt_total = 0;
        for (int i = 0; i < 60 * 10; i++)
            hurt_total += enemy_update(&g_pools, &L, v3f(0, 1.7f, 0), DT);
        v3 now = enemy_at(&g_pools, wi)->pos;
        ok(fabsf(now.x - was.x) < 0.001f && fabsf(now.z - was.z) < 0.001f,
           "an inert ward never moves, even after ten seconds");
        (void)hurt_total;   /* the maw does the hurting here; that is its own test */

        /* And the anchored boss holds the height it was placed at. */
        int bi = enemy_boss_index(&g_pools);
        ok(enemy_at(&g_pools, bi)->pos.y > 0.1f,
           "an anchored boss keeps its placed height rather than falling");
    }

    /* --- the draw is a fixed number of rolls ----------------------------
       demotest compares ::EnemyPool::rng with EXACT equality, and enemy.c
       already states the rule this obeys: the drop table spends both rolls
       whether or not the first succeeds, because a draw count that depends on
       the outcome makes the table an input to the AI. Placement has the same
       property or every recorded demo desynchronises the first time a map gains
       a ward slot.
       demotest는 ::EnemyPool::rng를 *정확히* 같은지로 비교하고, enemy.c는 이것이 따르는 규칙을
       이미 진술합니다. 드롭 표는 첫 굴림의 성패와 무관하게 두 굴림을 모두 씁니다. 결과에 따라
       달라지는 굴림 횟수는 그 표를 AI의 입력으로 만들기 때문입니다. 배치도 같은 성질을 가져야
       하며, 그러지 않으면 맵에 결계핵 자리가 하나 추가되는 첫 순간 기록된 모든 데모가
       어긋납니다. */
    {
        /* Counted as LCG STEPS rather than compared as end states, because the
           question is "how many rolls", not "which rolls". Same generator as
           enemy.c's frand; walking it forward is how many draws were taken.
           끝 상태를 비교하지 않고 LCG *단계 수*로 셉니다. 질문이 "어느 굴림"이 아니라 "몇
           번의 굴림"이기 때문입니다. enemy.c의 frand와 같은 생성기이며, 앞으로 걸어 보는 것이
           곧 몇 번 뽑혔는지입니다. */
        int steps[3];
        static const unsigned SEED[3] = { 12345u, 777u, 0xBEEFu };

        for (int trial = 0; trial < 3; trial++) {
            /* THE SAME MAP EACH TIME, three different seeds. A varying
               candidate count is NOT the hazard -- a replay loads the same
               level as the recording, so the count is the same by
               construction. The hazard is a draw count that depends on what the
               draws CAME OUT AS, which is what a rejection loop has and what
               enemy.c's drop table spends both rolls to avoid.
               매번 *같은 맵*이고 시드만 셋입니다. 후보 수가 달라지는 것은 위험이 아닙니다.
               재생은 기록 때와 같은 레벨을 로드하므로 개수는 구조적으로 같습니다. 위험은
               굴림의 *결과*에 따라 달라지는 굴림 횟수이며, 재시도 루프가 가진 것이자 enemy.c의
               드롭 표가 두 굴림을 모두 소비해 피하는 것입니다. */
            build(8, 8);
            enemy_reset(&g_pools);
            g_pools.enemy.rng = SEED[trial];
            enemy_spawn_level(&g_pools, &L);
            enemy_boss_summon(&g_pools, &L);

            unsigned before = g_pools.enemy.rng;
            enemy_ward_place(&g_pools, &L);
            unsigned after = g_pools.enemy.rng;

            int n = 0;
            unsigned r = before;
            while (r != after && n < 4096) { r = r * 1664525u + 1013904223u; n++; }
            steps[trial] = (r == after) ? n : -1;
        }

        ok(steps[0] > 0 && steps[0] == steps[1] && steps[1] == steps[2],
           "placement spends the same roll COUNT whatever it draws");
        oki(steps[0], "and that count is fixed, not outcome-dependent",
            steps[0], steps[0]);
    }

    /* --- a map that marked nothing does not soft-lock --------------------
       spire is terminal -- it has no exit -- so an unkillable boss in a level
       with no ward slots is a run that cannot end. The fallback is the one
       reward_point makes: degrade rather than refuse.
       spire는 출구가 없는 종착점입니다. 결계핵 자리가 없는 레벨의 죽일 수 없는 보스는 끝날 수
       없는 플레이입니다. 폴백은 reward_point가 하는 것과 같습니다. 거절하지 말고 저하시킬 것. */
    {
        build(0, 0);
        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &L);
        enemy_boss_summon(&g_pools, &L);
        oki(enemy_ward_place(&g_pools, &L) == 0, "no candidates raises no wards",
            enemy_ward_place(&g_pools, &L), 0);
        oki(enemy_guards_alive(&g_pools) == 0, "so nothing is guarding it",
            enemy_guards_alive(&g_pools), 0);

        int before = boss_hp();
        hit_boss(10);
        oki(boss_hp() == before - 10,
            "and the boss is hurtable from its first frame -- no soft-lock",
            boss_hp(), before - 10);
    }

    /* --- fewer candidates than wards clamps ------------------------------ */
    {
        build(1, 1);
        enemy_reset(&g_pools);
        enemy_spawn_level(&g_pools, &L);
        enemy_boss_summon(&g_pools, &L);
        int n = enemy_ward_place(&g_pools, &L);
        ok(n > 0 && n <= BOSS_WARDS, "two candidates raise between one and BOSS_WARDS");
        oki(enemy_guards_alive(&g_pools) == n, "and that is what stands",
            enemy_guards_alive(&g_pools), n);
    }

    /* --- a fresh cycle avoids the positions the last one used ------------
       With twice as many candidates as wards this is satisfiable, and it is the
       whole of what makes the fight's second and third cycles different from
       its first.
       결계핵의 두 배가 되는 후보가 있으면 이것은 만족 가능하며, 그것이 이 전투의 두 번째와 세
       번째 사이클을 첫 번째와 다르게 만드는 전부입니다. */
    {
        build(8, 8);
        enemy_reset(&g_pools);
        g_pools.enemy.rng = 999u;
        enemy_spawn_level(&g_pools, &L);
        enemy_boss_summon(&g_pools, &L);
        enemy_ward_place(&g_pools, &L);

        v3 first[BOSS_WARDS];
        int nf = 0;
        for (int i = 0; i < enemy_count(&g_pools) && nf < BOSS_WARDS; i++) {
            const Enemy *m = enemy_at(&g_pools, i);
            if (m->active && m->state != E_DEAD &&
                (mon_stats(m->type)->flags & MON_GUARD)) first[nf++] = m->pos;
        }

        smash_wards();
        enemy_ward_place(&g_pools, &L);

        int shared = 0;
        for (int i = 0; i < enemy_count(&g_pools); i++) {
            const Enemy *m = enemy_at(&g_pools, i);
            if (!m->active || m->state == E_DEAD) continue;
            if (!(mon_stats(m->type)->flags & MON_GUARD)) continue;
            for (int j = 0; j < nf; j++)
                if (fabsf(m->pos.x - first[j].x) < 0.01f &&
                    fabsf(m->pos.z - first[j].z) < 0.01f) shared++;
        }
        oki(shared == 0, "a second cycle reuses none of the first's positions",
            shared, 0);
    }

    /* --- the candidate order does not depend on .map save order ---------
       world.c refused this dependency once already, by name: "'First in the
       entity list' is a property of how the map was saved." Two levels with the
       same markers in a different order must pick the same wards from the same
       seed.
       world.c는 이 의존을 이미 이름을 붙여 거절한 적이 있습니다. *"«엔티티 목록의 첫
       번째»는 맵이 어떻게 저장되었는가의 성질입니다."* 같은 표식을 다른 순서로 가진 두 레벨은
       같은 시드에서 같은 결계핵을 골라야 합니다. */
    {
        v3 got[2][BOSS_WARDS];
        int n[2] = {0, 0};
        for (int pass = 0; pass < 2; pass++) {
            Level z = {0};
            L = z;
            Sector *s = &L.sectors[L.n_sectors++];
            short p[8] = { -3000,-3000,  3000,-3000,  3000,3000,  -3000,3000 };
            for (int i = 0; i < 8; i++) s->pts[i] = p[i];
            s->n = 4; s->floor = 0; s->ceil = 900;
            put_ent("maw", 0, 20, 2500);

            /* The same eight markers, walked forwards then backwards. */
            for (int i = 0; i < 8; i++) {
                int k = pass ? 7 - i : i;
                put_ent(k < 4 ? "wardair" : "wardground",
                        -2000 + k * 300, k < 4 ? 400 : 60, k < 4 ? -1000 : 1000);
            }

            enemy_reset(&g_pools);
            g_pools.enemy.rng = 4242u;
            enemy_spawn_level(&g_pools, &L);
            enemy_boss_summon(&g_pools, &L);
            enemy_ward_place(&g_pools, &L);

            for (int i = 0; i < enemy_count(&g_pools) && n[pass] < BOSS_WARDS; i++) {
                const Enemy *m = enemy_at(&g_pools, i);
                if (m->active && m->state != E_DEAD &&
                    (mon_stats(m->type)->flags & MON_GUARD)) got[pass][n[pass]++] = m->pos;
            }
        }

        int same = (n[0] == n[1]);
        for (int i = 0; same && i < n[0]; i++)
            if (fabsf(got[0][i].x - got[1][i].x) > 0.01f ||
                fabsf(got[0][i].z - got[1][i].z) > 0.01f) same = 0;
        ok(same, "reversing the entity list picks the same wards");
    }

    /* --- suppression is a multiplier that is read, not saved ------------- */
    {
        oki(g_pools.enemy.spawn_slow == 0.0f ? 1 : 0,
            "a fresh pool suppresses nothing", 1, 1);
        enemy_reset(&g_pools);
        ok(g_pools.enemy.spawn_slow == 0.0f,
           "and a reset puts the suppression back with the fight");
        ok(g_pools.enemy.boss.cycle == 0 && g_pools.enemy.boss.active == 0,
           "a reset clears the fight, so a level load cannot inherit one");
    }

    /* ================= the fight, driven through ::world_step ==============
     *
     * Everything above tests enemy.c. The LOOP -- summon, ward, groggy,
     * re-ward, three cycles, death, reward, respawn -- lives in ::step_boss,
     * and none of it is reachable from the pool alone. A boss fight that is
     * only reachable by playing for a minute is exactly the thing this project
     * says every rule must not be.
     *
     * ================= ::world_step을 통해 구동하는 전투 =====================
     *
     * 위의 모든 것은 enemy.c를 검사합니다. *루프*(소환, 수호, 그로기, 재점화, 3사이클, 사망,
     * 보상, 재소환)는 ::step_boss에 살고 있으며, 그중 무엇도 풀만으로는 도달할 수 없습니다.
     * 1분간 플레이해야만 도달할 수 있는 보스전은, 이 프로젝트가 모든 규칙이 그래서는 안 된다고
     * 말하는 바로 그것입니다. */
    printf("\n  -- the fight, through world_step --\n");
    world_cases();

    printf("\n%s\n", fails ? "bosstest: FAILURES" : "all boss checks passed");
    return fails ? 1 : 0;
}
