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
#include "sprite.h"   /* WPN_* -- the poses the viewmodel cycles through */
#include "proj.h"
#include "enemy.h"
#include "player.h"

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
        proj_reset();
        enemy_reset();
        const WeaponType *S = wp_stats(WP_RAPID);
        v3 from = v3f(0, 5.0f, 0);
        proj_fire(from, v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, 0.0f, 0.0f);

        for (int i = 0; i < 10; i++) proj_update(&L, 1.0f / 60.0f);

        const Proj *p = 0;
        for (int i = 0; i < proj_count(); i++)
            if (proj_at(i)->active) { p = proj_at(i); break; }
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
        proj_reset();
        enemy_reset();
        const WeaponType *S = wp_stats(WP_GRENADE);
        proj_fire(v3f(0, 5.0f, 0), v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, PROJ_BLAST_RADIUS, PROJ_FUSE);

        for (int i = 0; i < 12; i++) proj_update(&L, 1.0f / 60.0f);

        const Proj *p = 0;
        for (int i = 0; i < proj_count(); i++)
            if (proj_at(i)->active) { p = proj_at(i); break; }
        ok(p != 0, "a grenade is still in flight");
        if (p) ok(p->pos.y < 5.0f, "and has fallen below where it was thrown");
    }

    /* --- a grenade goes off on its fuse, even at rest --------------------
       The property that makes one at your feet a threat rather than scenery:
       the fuse burns whether or not it is moving. */
    {
        proj_reset();
        enemy_reset();
        const WeaponType *S = wp_stats(WP_GRENADE);
        proj_fire(v3f(0, 1.0f, 0), v3f(0, 0, -1), S->proj_speed, S->proj_gravity,
                  S->damage, PROJ_BLAST_RADIUS, PROJ_FUSE);
        ok(proj_live() == 1, "the grenade launched");

        /* Well past the fuse. */
        for (int i = 0; i < (int)((PROJ_FUSE + 0.5f) * 60.0f); i++)
            proj_update(&L, 1.0f / 60.0f);
        okd(proj_live() == 0, "and is gone once its fuse has burned",
            proj_live(), 0);
    }

    /* --- the blast reaches a group, and falls off with distance ----------
       A grenade that hurt exactly one monster would be a slow shotgun. */
    {
        proj_reset();
        enemy_reset();

        /* Three imps: one at the centre, one near the rim, one outside. */
        Level E = L;
        E.n_ents = 0;
        for (int k = 0; k < 3; k++) {
            Entity *e = &E.ents[E.n_ents++];
            e->kind[0]='i'; e->kind[1]='m'; e->kind[2]='p'; e->kind[3]=0;
            e->x = (short)(k * 300);   /* 0m, 3m, 6m */
            e->z = 0;
        }
        enemy_spawn_level(&E);
        ok(enemy_alive() == 3, "three monsters to blast");

        int before[3];
        for (int i = 0; i < 3; i++) before[i] = enemy_at(i)->health;

        int hit = proj_blast(v3f(0, 0.85f, 0), PROJ_BLAST_RADIUS, 55);
        okd(hit == 2, "the blast reaches the two inside its radius", hit, 2);

        int d0 = before[0] - enemy_at(0)->health;
        int d1 = before[1] - enemy_at(1)->health;
        int d2 = before[2] - enemy_at(2)->health;
        ok(d0 > d1, "the near one takes more than the far one");
        okd(d2 == 0, "and the one outside takes nothing", d2, 0);
    }

    /* --- a bolt stops at the first monster it meets -----------------------
       Swept, not teleported: at 70 m/s a bolt crosses more than a metre a
       frame, and a monster between this frame and the next must still be hit. */
    {
        proj_reset();
        enemy_reset();
        Level E = L;
        E.n_ents = 0;
        Entity *e = &E.ents[E.n_ents++];
        e->kind[0]='i'; e->kind[1]='m'; e->kind[2]='p'; e->kind[3]=0;
        e->x = 0; e->z = -800;                 /* 8 m down the aim */
        enemy_spawn_level(&E);

        int before = enemy_at(0)->health;
        const WeaponType *S = wp_stats(WP_RAPID);
        proj_fire(v3f(0, 0.9f, 0), v3f(0, 0, -1), S->proj_speed, 0.0f,
                  S->damage, 0.0f, 0.0f);

        for (int i = 0; i < 30 && proj_live(); i++) proj_update(&L, 1.0f / 60.0f);

        ok(enemy_at(0)->health < before, "a bolt damages the monster it reaches");
        okd(proj_live() == 0, "and is consumed by the hit", proj_live(), 0);
    }

    /* --- the pool refuses rather than overruns ---------------------------- */
    {
        proj_reset();
        int made = 0;
        for (int i = 0; i < PROJ_MAX + 12; i++)
            made += proj_fire(v3f(0, 1, 0), v3f(0, 0, -1), 30.0f, 0.0f, 5, 0.0f, 0.0f);
        okd(made == PROJ_MAX, "the pool fills to its cap and then refuses",
            made, PROJ_MAX);
        okd(proj_live() == PROJ_MAX, "and holds exactly that many",
            proj_live(), PROJ_MAX);
    }

    /* --- the viewmodel's pump cycle --------------------------------------
       The animation is a table of (pose, how far through the pump), and the
       reason it is a table is that Doom's pump passes through the SAME pose
       going out and coming back. Two branches could not express that without
       a third drawing to return to, which is why the atlas used to carry two
       byte-identical cells.

       Nothing on screen asserts an animation, and an animation that drifts
       does not crash -- it just stops matching what the gun is doing. So the
       cycle is checked here, off the same timers the renderer reads.

       애니메이션은 (자세, 펌프의 어디까지)의 표이며, 표인 이유는 Doom의 펌프가 나갈 때와
       돌아올 때 *같은* 자세를 지나기 때문입니다. 화면의 그 무엇도 애니메이션을 단언하지
       않고, 어긋난 애니메이션은 크래시를 내지 않고 그저 총이 하는 일과 맞지 않게 될
       뿐입니다. */
    {
        const float T = weapon_pump_time();

        okd(weapon_sprite_frame_at(0.0f, 0.0f) == WPN_REST,
            "an idle gun is at rest",
            weapon_sprite_frame_at(0.0f, 0.0f), WPN_REST);
        okd(weapon_sprite_frame_at(0.05f, 0.0f) == WPN_RAISED,
            "a flash with no pump still reads as a shot",
            weapon_sprite_frame_at(0.05f, 0.0f), WPN_RAISED);

        /* pump_timer counts DOWN, so a full timer is the start of the cycle. */
        int a = weapon_sprite_frame_at(0.0f, T * 0.95f);   /*  5% through */
        int b = weapon_sprite_frame_at(0.0f, T * 0.60f);   /* 40% through */
        int c = weapon_sprite_frame_at(0.0f, T * 0.35f);   /* 65% through */
        int d = weapon_sprite_frame_at(0.0f, T * 0.05f);   /* 95% through */
        okd(a == WPN_RAISED, "the pump opens from the raised pose", a, WPN_RAISED);
        okd(b == WPN_OPEN,   "swings the pump back",               b, WPN_OPEN);
        okd(c == WPN_RAISED, "returns through the SAME pose",      c, WPN_RAISED);
        okd(d == WPN_REST,   "and settles",                        d, WPN_REST);

        /* Sample the whole pump and read off the poses in order. The four
           spot checks above prove those four instants; this proves there is
           nothing ELSE in between -- a row inserted, dropped or reordered
           changes this sequence, and no single sample would notice.

           Sampled rather than compared against the table's own numbers,
           because a test that reads the table only proves the table equals
           itself. Boundaries are deliberately not asserted: where exactly the
           pump snaps back is a feel decision that should be free to move
           without failing a test.

           펌프 전체를 표본으로 훑어 자세를 순서대로 읽습니다. 위의 네 지점 검사는 그
           네 순간을 증명하고, 이것은 그 사이에 *다른 것이 없음*을 증명합니다. 행이
           추가되거나 빠지거나 순서가 바뀌면 이 수열이 달라지며, 단일 표본은 그것을
           알아채지 못합니다. 표의 숫자와 비교하지 않고 표본을 쓰는 이유는, 표를 읽는
           테스트는 표가 자기 자신과 같다는 것만 증명하기 때문입니다. 경계값을 일부러
           단언하지 않는 이유는, 펌프가 정확히 어디서 꺾이는지는 감각의 문제이고
           테스트를 깨지 않고 움직일 수 있어야 하기 때문입니다. */
        int seq[8], n = 0;
        for (int i = 0; i <= 200; i++) {
            int f = weapon_sprite_frame_at(0.0f, T * (1.0f - i / 200.0f));
            if (n == 0 || seq[n - 1] != f) {
                if (n < 8) seq[n++] = f;
            }
        }
        static const int WANT[] = { WPN_RAISED, WPN_OPEN, WPN_RAISED, WPN_REST };
        int match = (n == 4);
        for (int i = 0; match && i < 4; i++) match = (seq[i] == WANT[i]);
        ok(match, "the pump plays raised -> open -> raised -> rest, and nothing else");
        if (!match) {
            printf("      got %d poses:", n);
            for (int i = 0; i < n; i++) printf(" %d", seq[i]);
            printf("  wanted 4: %d %d %d %d\n",
                   WANT[0], WANT[1], WANT[2], WANT[3]);
        }
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall weapon checks passed\n", fails);
    return fails != 0;
}
