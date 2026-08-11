/* levelbench -- how much the level's spatial queries actually cost.
 *
 * The analysis that motivated this said level_trace was the first thing that
 * would fall over as levels or monster counts grew, and that the fix should be
 * measured before it was written. This is that measurement.
 *
 * It reports three things:
 *
 *   1. The SHAPE of the level: sectors, and points per sector. Everything
 *      below scales with the product of those two, because sector_at walks
 *      every sector and point_in_sector walks every edge of each.
 *
 *   2. The cost of one level_trace, level_ground and level_sector_at, timed
 *      over enough repetitions to be worth trusting.
 *
 *   3. A FRAME BUDGET: what the game actually issues per frame at the caller
 *      counts the analysis found -- one hook range test from the HUD, one
 *      can_see per monster, one collision step per projectile -- and what
 *      share of a 16.6ms frame that comes to.
 *
 * Point 3 is the one that decides whether the optimisation is worth doing. A
 * microbenchmark that says "this call takes 40us" means nothing on its own;
 * "the level costs 9% of the frame at full monster load" is a decision.
 *
 * 이 도구는 3순위 최적화에 착수하기 전에 실제 비용을 측정합니다. 마이크로벤치마크
 * 수치 자체가 아니라, 프레임 예산에서 차지하는 비율이 최적화의 착수 여부를 결정합니다.
 */

#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "level.h"
#include "enemy.h"
#include "weapon.h"

/* Caller counts per frame, from the analysis of who calls level_trace:
     - wp_hook_in_range, once from the HUD crosshair
     - can_see, once per active monster
     - the projectile collision step, once per live shot
   These are worst-case: a level with every slot full. */
#define TRACES_HUD    1

static double now_ms(LARGE_INTEGER f) {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

/* A deterministic spread of sample points and directions, so two runs of this
   benchmark measure the same work. rand() would make the numbers wobble. */
static unsigned g_rng = 0x2545f491u;
static float frand(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return ((g_rng >> 8) & 0xffff) / 65536.0f;
}

/* A full LVL_MAX_SECTORS grid of overlapping squares -- the arrangement
   leveltest already uses to time a rebuild, reused here to time the QUERIES.
 *
 * This exists because the sector grid was justified by an extrapolation from
 * two authored levels (2 and 6 sectors) out to the 64-sector cap, and an
 * extrapolation is not a measurement. The shipped maps are far too small to
 * show what the grid is for -- on those it is a slight LOSS, because the
 * bounding-box reject already rejects nearly everything and the cell lookup is
 * pure added cost. The case it was written for is this one, so this is the one
 * that has to be on the record.
 *
 * 이 함수는 섹터 격자가 제작된 레벨 두 개(섹터 2개와 6개)로부터 64개 상한까지 외삽하여
 * 정당화되었기 때문에 존재하며, 외삽은 측정이 아닙니다. 배포되는 맵은 격자의 효용을
 * 보여 주기에 너무 작습니다. 그런 맵에서는 오히려 약간 손해인데, 바운딩 박스 기각이
 * 이미 거의 모든 것을 걸러 내므로 셀 조회가 순수한 추가 비용이기 때문입니다. 격자가
 * 작성된 목적은 바로 이 경우이므로, 기록되어야 할 것도 이 경우입니다. */
static void build_grid_level(Level *big, int with_grid) {
    Level zero = {0};
    *big = zero;

    int dim = 8;   /* 8x8 = 64 = LVL_MAX_SECTORS */
    for (int gz = 0; gz < dim; gz++) {
        for (int gx = 0; gx < dim; gx++) {
            Sector *s = &big->sectors[big->n_sectors++];
            short x0 = (short)(gx * 300 - 1200), z0 = (short)(gz * 300 - 1200);
            short x1 = (short)(x0 + 400),        z1 = (short)(z0 + 400);
            short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
            for (int i = 0; i < 8; i++) s->pts[i] = p[i];
            s->n = 4;
            s->floor = (short)((gx + gz) * 20);
            s->ceil = 600;
        }
    }

    /* Bounds always -- they are what point_in_sector rejects against, and
       leaving them off would measure a different thing entirely. The grid is
       the variable, so that both paths can be timed on identical geometry.
       경계값은 항상 계산합니다. point_in_sector가 그것으로 기각하므로, 빼면 완전히 다른
       것을 측정하게 됩니다. 격자만 변수로 두어, 동일한 지오메트리에서 두 경로를 모두
       측정할 수 있게 합니다. */
    for (int i = 0; i < big->n_sectors; i++) level_bounds(&big->sectors[i]);
    if (with_grid) level_grid_build(big);
    /* else: grid stays zeroed, which sector_at reads as "not built" and
       handles by taking the full-scan path. That fallback is the pre-grid
       behaviour, so leaving it unbuilt measures exactly what the grid
       replaced.
       그렇지 않으면 격자가 0인 채로 남고, sector_at은 이를 "생성되지 않음"으로 읽어
       전체 순회 경로를 택합니다. 그 폴백이 곧 격자 도입 이전의 동작이므로, 생성하지
       않은 채로 측정하면 격자가 대체한 것을 정확히 측정하게 됩니다. */
}

int main(int argc, char **argv) {
    const char *name = (argc > 1) ? argv[1] : "arena";

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    static Level lv;

    /* `-grid` / `-scan`: the synthetic 64-sector level, with the lookup grid
       on or off. Anything else is a level name from the level text. */
    int synth = 0, synth_grid = 0;
    if (name[0] == '-') {
        synth = 1;
        synth_grid = (name[1] == 'g');
        build_grid_level(&lv, synth_grid);
        name = synth_grid ? "64-sector synthetic (grid)"
                          : "64-sector synthetic (full scan)";
    }

    if (!synth && !level_load(name, &lv)) {
        printf("no level named '%s'\n", name);
        return 1;
    }

    /* --- 1. the shape of the level ------------------------------------- */
    int total_pts = 0, max_pts = 0;
    for (int i = 0; i < lv.n_sectors; i++) {
        total_pts += lv.sectors[i].n;
        if (lv.sectors[i].n > max_pts) max_pts = lv.sectors[i].n;
    }

    printf("levelbench: %s\n\n", name);
    printf("  sectors                     %6d  (cap %d)\n",
           lv.n_sectors, LVL_MAX_SECTORS);
    printf("  points, total / max sector  %6d / %d\n", total_pts, max_pts);
    printf("  entities                    %6d\n\n", lv.n_ents);

    /* Work per sector_at call: one point-in-polygon per sector, each walking
       that sector's edges. This is the number every timing below scales with. */
    printf("  edge tests per sector_at    %6d\n", total_pts);

    /* --- pick sample points inside the map -----------------------------
       Sampling the bounding box would spend most of its time outside the
       level, where sector_at bails after finding nothing -- which is the
       cheap case and would flatter the result. Points on the floor of a real
       sector are what the game actually queries. */
    enum { NSAMP = 64 };
    v3 pts[NSAMP];
    int got = 0;

    /* Rejection-sample the level's bounding box and KEEP only the points a
       sector actually covers. A sector centroid would be simpler and is wrong:
       the centroid of a concave outline can lie outside it, which is exactly
       what happens in vault and left this with no samples at all. */
    {
        float lo_x = 1e30f, hi_x = -1e30f, lo_z = 1e30f, hi_z = -1e30f;
        for (int i = 0; i < lv.n_sectors; i++) {
            const Sector *s = &lv.sectors[i];
            for (int k = 0; k < s->n; k++) {
                float x = s->pts[k*2] * 0.01f, z = s->pts[k*2+1] * 0.01f;
                if (x < lo_x) lo_x = x;
                if (x > hi_x) hi_x = x;
                if (z < lo_z) lo_z = z;
                if (z > hi_z) hi_z = z;
            }
        }
        for (int guard = 0; guard < 200000 && got < NSAMP; guard++) {
            float x = lo_x + frand() * (hi_x - lo_x);
            float z = lo_z + frand() * (hi_z - lo_z);
            float fl, ce;
            if (level_ground(&lv, x, z, -1e9f, 1e9f, &fl, &ce))
                pts[got++] = v3f(x, fl + 0.5f, z);
        }
    }
    if (!got) { printf("\n  could not find a point inside the map\n"); return 1; }
    printf("  sample points inside map    %6d\n\n", got);

    /* --- 2. cost of each query ------------------------------------------ */
    printf("  --- per-call cost ---\n");

    /* level_sector_at: one sector_at, nothing more. The floor of every other
       number here. */
    {
        const int N = 200000;
        double t0 = now_ms(freq);
        volatile int sink = 0;
        for (int i = 0; i < N; i++) {
            v3 p = pts[i % got];
            sink += level_sector_at(&lv, p.x, p.z);
        }
        double us = (now_ms(freq) - t0) * 1000.0 / N;
        printf("  level_sector_at             %8.3f us\n", us);
        (void)sink;
    }

    /* level_ground: sector_at plus a height compare. What player_move and
       every monster step pays. */
    {
        const int N = 200000;
        double t0 = now_ms(freq);
        volatile int sink = 0;
        for (int i = 0; i < N; i++) {
            v3 p = pts[i % got];
            float fl, ce;
            sink += level_ground(&lv, p.x, p.z, -1e9f, 1e9f, &fl, &ce);
        }
        double us = (now_ms(freq) - t0) * 1000.0 / N;
        printf("  level_ground                %8.3f us\n", us);
        (void)sink;
    }

    /* level_trace at the hook's range: the marcher, so sector_at once per
       0.05m step plus ten bisection steps. The expensive one. */
    double trace_us = 0.0;
    {
        const int N = 4000;
        double t0 = now_ms(freq);
        volatile int hits = 0;
        for (int i = 0; i < N; i++) {
            v3 o = pts[i % got];
            float a = frand() * 6.2831853f;
            float e = (frand() - 0.5f) * 0.6f;
            v3 d = v3norm(v3f(cosf(a), e, sinf(a)));
            float t; v3 n;
            hits += level_trace(&lv, o, d, HOOK_RANGE, &t, &n);
        }
        trace_us = (now_ms(freq) - t0) * 1000.0 / N;
        printf("  level_trace (%.0fm hook range) %7.3f us\n", (double)HOOK_RANGE, trace_us);
        (void)hits;
    }

    /* level_blocked over the same rays: the visibility question on its own,
       without the bisection that locates the hit or the edge scan that derives
       a normal there. What a monster's line-of-sight test now costs, against
       the level_trace above that it used to pay.
       동일한 광선에 대한 level_blocked입니다. 충돌 지점을 찾는 이분 탐색도, 그곳의 법선을
       유도하는 모서리 순회도 없는 순수한 가시성 질문입니다. 몬스터의 시야 판정이 이제
       치르는 비용이며, 위의 level_trace는 그것이 이전에 치르던 비용입니다. */
    double blocked_us = 0.0;
    {
        const int N = 4000;
        unsigned save = g_rng;
        g_rng = 0x2545f491u;      /* the same rays level_trace just walked */
        double t0 = now_ms(freq);
        volatile int hits = 0;
        for (int i = 0; i < N; i++) {
            v3 o = pts[i % got];
            float a = frand() * 6.2831853f;
            float e = (frand() - 0.5f) * 0.6f;
            v3 d = v3norm(v3f(cosf(a), e, sinf(a)));
            hits += level_blocked(&lv, o, d, HOOK_RANGE);
        }
        blocked_us = (now_ms(freq) - t0) * 1000.0 / N;
        printf("  level_blocked (same rays)     %7.3f us   %.2fx cheaper\n",
               blocked_us, trace_us / (blocked_us > 0 ? blocked_us : 1));
        g_rng = save;
        (void)hits;
    }

    /* --- 3. the frame budget -------------------------------------------
       The number that actually decides whether to optimise. */
    printf("\n  --- frame budget at 60fps (16.67ms) ---\n");

    /* Monster sight is now level_blocked rather than level_trace, and cached
       for SIGHT_PERIOD frames, so only 1/SIGHT_PERIOD of the monsters refresh
       on any given frame. Projectile steps still pay a full level_trace: a bolt
       needs the hit POSITION to place its burst, which is exactly what
       level_blocked does not compute.
       몬스터 시야는 이제 level_trace가 아니라 level_blocked이며 SIGHT_PERIOD 프레임 동안
       캐시되므로, 특정 프레임에 갱신하는 것은 몬스터의 1/SIGHT_PERIOD뿐입니다. 발사체
       단계는 여전히 온전한 level_trace를 치릅니다. 볼트는 폭발을 배치할 충돌 *지점*이
       필요한데, 그것이 바로 level_blocked가 계산하지 않는 값입니다. */
    struct { const char *what; int mon, shot; } load[] = {
        { "quiet   (hud only)",                      0,  0 },
        { "typical (hud + 8 monsters)",              8,  0 },
        { "heavy   (hud + 32 monsters + 16 shots)", 32, 16 },
        { "capped  (hud + ENEMY_MAX + MAX_SHOTS)",  ENEMY_MAX, ENEMY_MAX_SHOTS },
    };

    printf("  %-38s %10s %10s\n", "", "before", "after");
    for (int i = 0; i < (int)(sizeof(load)/sizeof(load[0])); i++) {
        int mon = load[i].mon, shot = load[i].shot;

        /* Before: every monster and every shot ran a full level_trace, every
           frame, plus the HUD's hook range test. */
        double before = trace_us * (TRACES_HUD + mon + shot) / 1000.0;

        /* After: the HUD and the shots still trace; monster sight is the
           cheaper call and only a quarter of them refresh per frame. */
        double after = (trace_us * (TRACES_HUD + shot)
                        + blocked_us * mon / (double)SIGHT_PERIOD) / 1000.0;

        printf("  %-38s %7.3fms %7.3fms  %5.1f%% -> %5.1f%%  (%.2fx)\n",
               load[i].what, before, after,
               before / 16.67 * 100.0, after / 16.67 * 100.0,
               after > 0 ? before / after : 0.0);
    }

    printf("\n  Note: monster sight traces stop at the player rather than\n"
           "  running the full hook range, so both columns are an upper\n"
           "  bound. Treat them as the ceiling, not the expected cost.\n");

    return 0;
}
