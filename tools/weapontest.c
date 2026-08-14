/* weapontest -- the roster's invariants, and what its projectiles actually do.
 *
 * Two kinds of check, and the first matters more than it looks:
 *
 *   1. TABLE INVARIANTS. Exactly one of pellets/proj_speed/melee_range is set
 *      per row, every weapon has a distinct name, every belt is orderable.
 *      These are the rules weapon.c's dispatch relies on, and a row that
 *      breaks one does not fail to compile -- it produces a weapon that fires
 *      twice, or does nothing at all when you pull the trigger.
 *
 *   2. PROJECTILE PHYSICS. A grenade arcs, bounces off a wall, keeps its fuse
 *      burning while it rests, and takes a group down when it goes off. A bolt
 *      flies flat and stops at the first thing it meets. All of it is
 *      arithmetic over a struct, so none of it needs a window.
 *
 * 두 종류의 검사이며, 첫 번째가 보이는 것보다 중요합니다. 표의 불변식은 weapon.c의 분배가
 * 의존하는 규칙인데, 이를 깨는 행은 컴파일에 실패하지 않습니다. 두 번 발사되거나, 방아쇠를
 * 당겨도 아무 일도 일어나지 않는 무기가 될 뿐입니다.
 */

#include <stdio.h>
#include <math.h>
#include "weapon.h"
#include "pools.h"
#include "sprite.h"   /* WPN_* -- the poses the viewmodel cycles through */
#include "proj.h"
#include "enemy.h"
#include "player.h"

/* The pools a run spawns into. A test owns one the same way a ::World does --
   these used to be file-scope arrays inside their own modules, so a fixture
   inherited whatever the previous case left in them. See pools.h.
   플레이가 생성해 넣는 풀들입니다. ::World가 그러하듯 테스트도 자기 것을 소유합니다.
   이것들은 각자의 모듈 안 파일 스코프 배열이었으므로, 픽스처는 이전 사례가 남긴 것을
   그대로 물려받았습니다. pools.h를 참조하십시오. */
static Pools g_pools;

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okf(int cond, const char *what, float got, float want) {
    printf("  %-58s %8.2f / %8.2f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okd(int cond, const char *what, int got, int want) {
    printf("  %-58s %8d / %8d  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* A room 40m across with a floor at 0 and a wall the grenade can be thrown at.
   The fixture is a hand-built Level, which leaves the sector grid unbuilt --
   sector_at falls back to the full scan, which is correct and merely slower.
   손으로 조립한 Level이므로 섹터 격자가 생성되지 않습니다. sector_at이 전체 순회로
   폴백하며, 이는 올바르고 다만 느릴 뿐입니다. */
static Level L;

static void build_room(void) {
    Level zero = {0};
    L = zero;
    Sector *s = &L.sectors[L.n_sectors++];
    short p[8] = { -2000, -2000,  2000, -2000,  2000, 2000,  -2000, 2000 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4;
    s->floor = 0;
    s->ceil  = 800;
    level_bounds(s);
}

static int name_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return !*a && !*b;
}

int main(void) {
    printf("weapontest\n\n");
    build_room();

    /* --- 1. exactly one attack kind per row ------------------------------
       weapon.c's attack() tests pellets, then proj_speed, then melee_range and
       takes the first that is set. If a row set two, the second would be dead
       and the weapon would quietly be something other than what the table
       says. If a row set none, the trigger would do nothing at all.
       weapon.c의 attack()은 pellets, proj_speed, melee_range를 차례로 검사하여 처음
       설정된 것을 취합니다. 한 행이 둘을 설정하면 두 번째는 죽은 코드가 되고 무기는 표가
       말하는 것과 다른 무언가가 됩니다. 아무것도 설정하지 않으면 방아쇠가 아무 일도 하지
       않습니다. */
    {
        int bad = 0;
        for (int i = 0; i < WP_TYPES; i++) {
            const WeaponType *S = wp_stats(i);
            int kinds = (S->pellets > 0) + (S->proj_speed > 0.0f) + (S->melee_range > 0.0f);
            if (kinds != 1) {
                bad++;
                printf("      '%s' declares %d attack kinds\n", S->name, kinds);
            }
        }
        okd(bad == 0, "every weapon declares exactly one attack kind", bad, 0);
    }

    /* --- names are distinct, and resolvable ------------------------------
       The name is the sprite prefix, the pickup entity and the HUD label at
       once, so two weapons sharing one would collide in three places.
       이름은 스프라이트 접두사이자 아이템 엔티티이자 HUD 표시명입니다. 두 무기가 하나를
       공유하면 세 곳에서 충돌합니다. */
    {
        int dup = 0, unresolved = 0;
        for (int i = 0; i < WP_TYPES; i++) {
            if (wp_type_for(wp_stats(i)->name) != i) unresolved++;
            for (int j = i + 1; j < WP_TYPES; j++)
                if (name_eq(wp_stats(i)->name, wp_stats(j)->name)) dup++;
        }
        okd(dup == 0, "no two weapons share a name", dup, 0);
        okd(unresolved == 0, "and each name resolves back to its own index",
            unresolved, 0);
        ok(wp_type_for("nosuchweapon") < 0, "an unknown name resolves to -1");
    }

    /* --- belts are orderable --------------------------------------------- */
    {
        int bad = 0;
        for (int i = 0; i < WP_TYPES; i++) {
            const WeaponType *S = wp_stats(i);
            if (S->start_ammo > S->max_ammo)  bad++;
            if (S->pickup_ammo > S->max_ammo) bad++;
            if (S->max_ammo <= 0)             bad++;
            if (S->cooldown <= 0.0f)          bad++;
            if (S->damage <= 0)               bad++;
        }
        okd(bad == 0, "every belt and rate is orderable", bad, 0);
    }

    /* --- the axe is the one weapon without the grapple -------------------- */
    {
        int hooks = 0;
        for (int i = 0; i < WP_TYPES; i++) hooks += wp_stats(i)->hook ? 1 : 0;
        okd(hooks == WP_TYPES - 1, "every weapon but one throws the grapple",
            hooks, WP_TYPES - 1);
        ok(!wp_stats(WP_AXE)->hook, "and the one that does not is the axe");
    }

    /* --- 2. a bolt flies flat -------------------------------------------
       No gravity means the height it was fired at is the height it arrives at.
       A bolt that sagged would make the crosshair a lie at range. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        const WeaponType *S = wp_stats(WP_RAPID);
        v3 from = v3f(0, 5.0f, 0);
        proj_fire(&g_pools, from, v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, 0.0f, 0.0f);

        for (int i = 0; i < 10; i++) proj_update(&g_pools, &L, 1.0f / 60.0f);

        const Proj *p = 0;
        for (int i = 0; i < proj_count(&g_pools); i++)
            if (proj_at(&g_pools, i)->active) { p = proj_at(&g_pools, i); break; }
        ok(p != 0, "a bolt is still in flight after ten frames");
        if (p) {
            okf(fabsf(p->pos.y - 5.0f) < 0.001f, "and has not dropped at all",
                p->pos.y, 5.0f);
            ok(p->pos.z < -5.0f, "having travelled down the aim");
        }
    }

    /* --- a grenade arcs --------------------------------------------------
       Gravity means a grenade thrown level ends up lower than it started, and
       that fall is what lets it be lobbed over things. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        const WeaponType *S = wp_stats(WP_GRENADE);
        proj_fire(&g_pools, v3f(0, 5.0f, 0), v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, PROJ_BLAST_RADIUS, PROJ_FUSE);

        for (int i = 0; i < 12; i++) proj_update(&g_pools, &L, 1.0f / 60.0f);

        const Proj *p = 0;
        for (int i = 0; i < proj_count(&g_pools); i++)
            if (proj_at(&g_pools, i)->active) { p = proj_at(&g_pools, i); break; }
        ok(p != 0, "a grenade is still in flight");
        if (p) ok(p->pos.y < 5.0f, "and has fallen below where it was thrown");
    }

    /* --- a grenade goes off on its fuse, even at rest --------------------
       The property that makes one at your feet a threat rather than scenery:
       the fuse burns whether or not it is moving. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        const WeaponType *S = wp_stats(WP_GRENADE);
        proj_fire(&g_pools, v3f(0, 1.0f, 0), v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, PROJ_BLAST_RADIUS, PROJ_FUSE);
        ok(proj_live(&g_pools) == 1, "the grenade launched");

        /* Well past the fuse. */
        for (int i = 0; i < (int)((PROJ_FUSE + 0.5f) * 60.0f); i++)
            proj_update(&g_pools, &L, 1.0f / 60.0f);
        okd(proj_live(&g_pools) == 0, "and is gone once its fuse has burned",
            proj_live(&g_pools), 0);
    }

    /* --- the blast reaches a group, and falls off with distance ----------
       A grenade that hurt exactly one monster would be a slow shotgun. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);

        /* Three imps: one at the centre, one near the rim, one outside. */
        Level E = L;
        E.n_ents = 0;
        for (int k = 0; k < 3; k++) {
            Entity *e = &E.ents[E.n_ents++];
            e->kind[0]='i'; e->kind[1]='m'; e->kind[2]='p'; e->kind[3]=0;
            e->x = (short)(k * 300);   /* 0m, 3m, 6m */
            e->z = 0;
        }
        enemy_spawn_level(&g_pools, &E);
        ok(enemy_alive(&g_pools) == 3, "three monsters to blast");

        int before[3];
        for (int i = 0; i < 3; i++) before[i] = enemy_at(&g_pools, i)->health;

        int hit = proj_blast(&g_pools, v3f(0, 0.85f, 0), PROJ_BLAST_RADIUS, 55);
        okd(hit == 2, "the blast reaches the two inside its radius", hit, 2);

        int d0 = before[0] - enemy_at(&g_pools, 0)->health;
        int d1 = before[1] - enemy_at(&g_pools, 1)->health;
        int d2 = before[2] - enemy_at(&g_pools, 2)->health;
        ok(d0 > d1, "the near one takes more than the far one");
        okd(d2 == 0, "and the one outside takes nothing", d2, 0);
    }

    /* --- a bolt stops at the first monster it meets -----------------------
       Swept, not teleported: at 70 m/s a bolt crosses more than a metre a
       frame, and a monster between this frame and the next must still be hit. */
    {
        proj_reset(&g_pools);
        enemy_reset(&g_pools);
        Level E = L;
        E.n_ents = 0;
        Entity *e = &E.ents[E.n_ents++];
        e->kind[0]='i'; e->kind[1]='m'; e->kind[2]='p'; e->kind[3]=0;
        e->x = 0; e->z = -800;                 /* 8 m down the aim */
        enemy_spawn_level(&g_pools, &E);

        int before = enemy_at(&g_pools, 0)->health;
        const WeaponType *S = wp_stats(WP_RAPID);
        proj_fire(&g_pools, v3f(0, 0.9f, 0), v3f(0, 0, -1), S->proj_speed, 0.0f,
                  S->damage, 0.0f, 0.0f);

        for (int i = 0; i < 30 && proj_live(&g_pools); i++) proj_update(&g_pools, &L, 1.0f / 60.0f);

        ok(enemy_at(&g_pools, 0)->health < before, "a bolt damages the monster it reaches");
        okd(proj_live(&g_pools) == 0, "and is consumed by the hit", proj_live(&g_pools), 0);
    }

    /* --- the pool refuses rather than overruns ---------------------------- */
    {
        proj_reset(&g_pools);
        int made = 0;
        for (int i = 0; i < PROJ_MAX + 12; i++)
            made += proj_fire(&g_pools, v3f(0, 1, 0), v3f(0, 0, -1), 30.0f, 0.0f, 5, 0.0f, 0.0f);
        okd(made == PROJ_MAX, "the pool fills to its cap and then refuses",
            made, PROJ_MAX);
        okd(proj_live(&g_pools) == PROJ_MAX, "and holds exactly that many",
            proj_live(&g_pools), PROJ_MAX);
    }

    /* --- every weapon's cycle, against Doom's own state table -------------
       These tables are transcribed from info.c, and the reason to assert them
       is that reading the ART instead got two of them wrong and both shipped.
       The shotgun idled on its first PUMP frame, because its real idle
       (SHTGA0) is little more than the end of a barrel and had been dropped as
       unusable; and the chainsaw had its idle and its cut swapped, because
       SAWG C and D -- the frames A_WeaponReady alternates between -- are the
       wider drawings and read as a lunge.

       Neither failed to compile, neither crashed, and neither is visible in a
       screenshot unless you already know what to look for.

       이 표들은 info.c에서 옮긴 것이며, 이를 단언하는 이유는 대신 *아트*를 읽고
       판단했다가 둘을 틀렸고 둘 다 배포되었기 때문입니다. 어느 쪽도 컴파일에 실패하지
       않았고, 크래시도 나지 않았으며, 무엇을 찾아야 하는지 이미 알지 않는 한 스크린샷
       으로도 보이지 않습니다. */
    {
        /* Walk the whole recovery and read the poses off in order. A single
           sample cannot see a row inserted, dropped or reordered. Sampled
           rather than compared against the table's own numbers, because a test
           that reads the table proves only that the table equals itself. */
        struct { int type; const char *name; int want[8]; int n; } W[] = {
            /* A B C D C B A -- out and back, all four drawings */
            { WP_SHOTGUN, "shotgun",
              { SG_IDLE, SG_PUMP0, SG_PUMP1, SG_PUMP2, SG_PUMP1, SG_PUMP0, SG_IDLE }, 7 },
            /* B held for the whole shot, then back to A */
            { WP_GRENADE, "grenade", { LN_FIRE, LN_IDLE }, 2 },
            /* A(4) B(4): both states fire, so the alternation is the fire rate */
            { WP_RAPID,   "rapid",   { RP_IDLE, RP_SPIN }, 2 },
            /* A_Saw alternates A and B -- the bite, never the rev */
            { WP_AXE,     "axe",     { AX_CUT0, AX_CUT1 }, 2 },
        };

        for (int k = 0; k < 4; k++) {
            const float T = weapon_pump_time(W[k].type);
            int seq[12], n = 0;
            /* i < 400, not <= : at exactly 400 the timer is zero, which is
               not "the end of the animation" but "not animating", and the
               idle frame it returns then is a fifth pose that is not part of
               the cycle. Two weapons hid that because their idle happens to
               equal their cycle's last pose.
               i <= 400이 아니라 i < 400입니다. 정확히 400에서 타이머는 0이 되는데 그것은
               "애니메이션의 끝"이 아니라 "애니메이션 중이 아님"이며, 그때 반환되는 대기
               프레임은 주기에 속하지 않는 다섯 번째 자세입니다. */
            for (int i = 0; i < 400; i++) {
                int f = weapon_sprite_frame_at(W[k].type, 0.0f,
                                               T * (1.0f - i / 400.0f), 0.0f);
                if (n == 0 || seq[n - 1] != f) { if (n < 12) seq[n++] = f; }
            }
            int match = (n == W[k].n);
            for (int i = 0; match && i < n; i++) match = (seq[i] == W[k].want[i]);
            ok(match, W[k].name);
            if (!match) {
                printf("      got %d poses:", n);
                for (int i = 0; i < n; i++) printf(" %d", seq[i]);
                printf("   wanted %d:", W[k].n);
                for (int i = 0; i < W[k].n; i++) printf(" %d", W[k].want[i]);
                printf("\n");
            }
        }

        /* THE IDLE IS A CYCLE TOO, and for the chainsaw it is the whole point.
           A_WeaponReady shows one frame for three of these weapons and
           alternates two for the saw, so "at rest" cannot be a single drawing.
           Driven by a free-running clock rather than by bob_phase, because a
           saw revs while you stand still and bob_phase does not. */
        ok(weapon_sprite_frame_at(WP_SHOTGUN, 0.0f, 0.0f, 0.0f) == SG_IDLE,
           "a resting shotgun shows its IDLE frame, not a pump frame");

        int saw_seen0 = 0, saw_seen1 = 0, gun_moved = 0;
        for (int i = 0; i < 60; i++) {
            float clock = i / 60.0f;      /* a second of standing still */
            int a = weapon_sprite_frame_at(WP_AXE, 0.0f, 0.0f, clock);
            if (a == AX_REV0) saw_seen0 = 1;
            if (a == AX_REV1) saw_seen1 = 1;
            if (weapon_sprite_frame_at(WP_SHOTGUN, 0.0f, 0.0f, clock) != SG_IDLE)
                gun_moved = 1;
        }
        ok(saw_seen0 && saw_seen1, "a resting chainsaw revs between two frames");
        ok(!gun_moved, "and a weapon with a one-frame idle stays still");
    }

    /* --- wp_init needs no GL context -------------------------------------
       This is the property the weapon/weaponview split exists to produce, and
       the only one of its claims a test can hold. There is no context in this
       process -- no window, no gl_bootstrap, nothing -- so before the split
       this call uploaded a texture through a null function pointer.
       tools\hooktest.c worked around that by never calling wp_init and
       building its Weapon with `= {0}`, which meant every hook fixture ran
       against a weapon the game never produces: no belt, no rng seed, and
       hook_enemy 0 rather than -1.

       If this ever needs a context again, something drawable has moved back
       into weapon.c, and the crash lands here rather than in a fixture that
       looked unrelated.

       wp_init에 GL 컨텍스트가 필요 없다는 것. weapon/weaponview 분리가 만들어 내려는 성질
       자체이며, 그 주장 중 테스트가 붙잡을 수 있는 유일한 것입니다. 이 프로세스에는
       컨텍스트가 없습니다. 창도, gl_bootstrap도 없습니다. 따라서 분리 이전에 이 호출은 널
       함수 포인터를 통해 텍스처를 업로드했습니다. tools\hooktest.c는 wp_init을 아예
       호출하지 않고 `= {0}`으로 Weapon을 만들어 우회했는데, 그것은 모든 훅 픽스처가 게임이
       결코 만들지 않는 무기(탄약대 없음, 난수 시드 없음, hook_enemy가 -1이 아니라 0)를
       대상으로 돌았다는 뜻입니다. */
    {
        Level lv = {0};
        Weapon w;
        wp_init(&w, &lv);

        ok(w.level == &lv, "wp_init records the level with no GL context in the process");
        ok(w.owned[WP_SHOTGUN] && w.ammo[WP_SHOTGUN] == WEAPON_START_AMMO,
           "and hands over the boot belt");
        ok(w.hook_enemy == -1, "and marks the hook as attached to nothing");
        ok(w.rng != 0u, "and seeds the rng, which `= {0}` never did");

        /* The muzzle a headless weapon fires from: no model has been loaded,
           so this is the default rather than whatever a previous one left.
           헤드리스 무기가 발사하는 총구입니다. 모델이 로드된 적 없으므로 이전 모델이 남긴
           값이 아니라 기본값입니다. */
        v3 d = WP_MUZZLE_DEFAULT;
        ok(w.muzzle.x == d.x && w.muzzle.y == d.y && w.muzzle.z == d.z,
           "and starts at the default muzzle, not at the camera origin");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall weapon checks passed\n", fails);
    return fails != 0;
}
