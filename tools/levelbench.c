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
#include "brush.h"    /* brush_translate -- how a .map door actually moves */
#include "enemy.h"
#include "weapon.h"
#include "hook.h"
#include "render.h"   /* MeshBuf, for timing what a rebuild costs. No GL call
                         is made: mb_init/mb_reset/mb_free are CPU-side, and
                         only mesh_upload would need a context. */
#include "model.h"    /* MdlRange by value -- level.h only forward-declares it,
                         the same reason scene.h includes this. */

/* Caller counts per frame, from the analysis of who calls level_trace:
     - wp_hook_in_range, once from the HUD crosshair
     - can_see, once per active monster
     - the projectile collision step, once per live shot
   These are worst-case: a level with every slot full. */
#define TRACES_HUD    1

/* Advances every door by `dt` of its travel, the way door.c's apply() does.
   Enough of a door to make the geometry change; nothing here needs the touch
   tests, the keys or the sounds.
   door.c의 apply()가 하는 방식으로 모든 문을 이동 구간의 `dt`만큼 진행시킵니다. 지오메트리를
   바꾸기에 충분한 만큼의 문이며, 이곳의 어떤 것도 접촉 판정이나 열쇠나 소리를 필요로 하지
   않습니다. */
static void nudge_doors(Level *l, float dt) {
    int n = l->n_doors > LVL_MAX_DOORS ? LVL_MAX_DOORS : l->n_doors;
    for (int i = 0; i < n; i++) {
        const DoorDef *d = &l->doors[i];

        /* A brush door moves brushes, which this used to walk straight past --
           so a .map level reported its doors and then measured a rebuild with
           nothing moving in it, which is the flattering case and not the one
           the number is about. ::brush_translate is what ::apply_brush calls.
           브러시 문은 브러시를 움직이며, 이 함수는 이전에 그것을 그냥 지나쳤습니다. 그래서
           .map 레벨은 문이 있다고 보고한 뒤 아무것도 움직이지 않는 재생성을 측정했습니다.
           좋게 보이는 경우이지 이 수치가 다루는 경우가 아닙니다. ::brush_translate가
           ::apply_brush가 호출하는 것입니다. */
        if (d->sector < 0) {
            if (l->brushes && d->n_brushes > 0)
                brush_translate(l->brushes, d->first_brush, d->n_brushes,
                                v3f(0.0f, d->amount * 0.01f * dt, 0.0f));
            continue;
        }
        if (d->sector >= l->n_sectors) continue;
        Sector *s = &l->sectors[d->sector];

        switch (d->axis) {
        case DOOR_UP:   s->ceil  = (short)(s->ceil  + d->amount * dt); break;
        case DOOR_DOWN: s->floor = (short)(s->floor - d->amount * dt); break;
        case DOOR_X:
        case DOOR_Z: {
            int off = (d->axis == DOOR_X) ? 0 : 1;
            for (int k = 0; k < s->n; k++)
                s->pts[k*2 + off] = (short)(s->pts[k*2 + off] + d->amount * dt);
            level_bounds(s);
            break;
        }
        default: break;
        }
    }
    level_grid_build(l);
}

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
    const char *name = (argc > 1) ? argv[1] : "lqdm1";

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
    /* NOT A FAILURE ANY MORE, and the level that proved it is a brush level.
       Every sample above comes from ::level_ground, which answers for sectors;
       a .map level has none, so the rejection sampler finds nothing and this
       used to return 1 before reaching the geometry section -- which is the one
       section a brush level most needs measured, since the static/moving split
       only applies to brush levels in the first place.
       The query timings genuinely cannot run without points. The rebuild
       timings never needed them.
       더 이상 실패가 아니며, 그것을 드러낸 것이 브러시 레벨입니다. 위의 모든 표본은 섹터에
       대해 답하는 ::level_ground에서 나옵니다. .map 레벨에는 섹터가 없으므로 기각 샘플러가
       아무것도 찾지 못하고, 이전에는 지오메트리 구간에 닿기도 전에 1을 반환했습니다. 정작
       브러시 레벨이 가장 측정을 필요로 하는 구간이 그것인데, 정적·이동 분할이 애초에 브러시
       레벨에만 적용되기 때문입니다.
       질의 시간 측정은 점 없이는 정말로 실행할 수 없습니다. 재생성 시간 측정은 점을 필요로 한
       적이 없습니다. */
    if (!got)
        printf("\n  no sector to sample: the query timings below are skipped,\n"
               "  and the geometry rebuild section still runs.\n");
    else
        printf("  sample points inside map    %6d\n\n", got);

    if (got) {

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

    }   /* end of the sections that need sample points */

    /* --- 4. what a geometry rebuild costs -------------------------------
       The queries above are paid per monster; this one is paid per FRAME, and
       only while a door is moving -- but then it is paid whole. door_update
       returning non-zero sets World::geometry_dirty, and the rebuild that
       answers it re-triangulates every sector and re-bakes every vertex
       against every light, whether or not the door touched them.

       Split into the two halves because they scale differently and only one
       of them is avoidable: triangulation is linear in points, while the bake
       runs a level_blocked per vertex per light in range -- the same 8us call
       timed above, several hundred times over.

       위의 질의들은 몬스터마다 치르지만, 이것은 *프레임마다* 치릅니다. 문이 움직이는
       동안에만이지만 그때는 통째로 치릅니다. 두 절반으로 나누어 재는 이유는 둘이 다르게
       증가하고 그중 하나만 피할 수 있기 때문입니다. 삼각형화는 점 수에 선형인 반면,
       라이트 베이크는 정점마다 사거리 안의 광원마다 level_blocked를 돌립니다. 위에서 잰
       바로 그 8us짜리 호출을 수백 번입니다. */
    printf("\n  --- geometry rebuild (what a moving door pays per frame) ---\n");
    {
        MeshBuf  gb;
        MdlRange ranges[LVL_MAX_RANGES];
        mb_init(&gb, 8192);

        const int REPS = 40;
        LARGE_INTEGER t0, t1;

        /* --- the load build, with nothing remembered ---------------------
           What a level costs the frame it appears. The cache is emptied before
           each repetition, so this is the honest cold number and the one the
           per-frame figures below should be read against.
           레벨이 나타나는 프레임에 드는 비용입니다. 반복마다 캐시를 비우므로 이것이 정직한
           차가운 수치이며, 아래의 프레임별 수치는 이것에 견주어 읽어야 합니다. */
        QueryPerformanceCounter(&t0);
        for (int r = 0; r < REPS; r++) {
            level_light_cache_reset();
            mb_reset(&gb);
            level_geometry(&gb, &lv, ranges, LVL_MAX_RANGES);
        }
        QueryPerformanceCounter(&t1);
        double cold_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0
                       / (double)freq.QuadPart / REPS;

        int verts = gb.count;
        int n_ranges = level_geometry(&gb, &lv, ranges, LVL_MAX_RANGES);
        (void)n_ranges;
        mb_reset(&gb);
        n_ranges = level_geometry(&gb, &lv, ranges, LVL_MAX_RANGES);

        /* --- the same build with no lights at all -------------------------
           Which leaves the triangulation on its own: bake_light's inner loop
           runs zero times per vertex. Restored afterwards.
           삼각형화만 남습니다. 이후 복원합니다. */
        int saved_lights = lv.n_lights;
        lv.n_lights = 0;
        QueryPerformanceCounter(&t0);
        for (int r = 0; r < REPS; r++) {
            level_light_cache_reset();
            mb_reset(&gb);
            level_geometry(&gb, &lv, ranges, LVL_MAX_RANGES);
        }
        QueryPerformanceCounter(&t1);
        double geom_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0
                       / (double)freq.QuadPart / REPS;
        lv.n_lights = saved_lights;

        printf("  vertices / material runs    %6d / %d\n", verts, n_ranges);
        printf("  lights / doors              %6d / %d\n\n",
               lv.n_lights, lv.n_doors);
        printf("  %-38s %9.3fms  %5.1f%% of a 60fps frame\n",
               "triangulation only", geom_ms, geom_ms / 16.67 * 100.0);
        printf("  %-38s %9.3fms  %5.1f%%\n",
               "load build (cold cache, every vtx traced)", cold_ms,
               cold_ms / 16.67 * 100.0);

        /* --- a door actually in motion ------------------------------------
           The number this whole exercise is about, and the one that is easy to
           fake: rebuilding the SAME geometry forty times is a 100% cache hit
           on every repetition after the first, which flatters the cache by
           measuring a case the game never runs. So the doors are advanced a
           fortieth of their travel per repetition, and each rebuild sees
           geometry the previous one did not -- which is what a frame during a
           door's swing actually is.

           이 작업 전체가 다루는 수치이며, 속이기 쉬운 수치이기도 합니다. *같은* 형상을 마흔
           번 다시 만드는 것은 첫 번째 이후 매번 100% 적중이며, 게임이 결코 실행하지 않는
           경우를 재어 캐시를 좋게 보이게 만듭니다. 그래서 반복마다 문을 이동 구간의 1/40씩
           진행시키고, 각 재생성은 이전 것이 보지 못한 형상을 보게 합니다. 문이 열리는 동안의
           한 프레임이 실제로 그렇습니다. */
        if (lv.n_doors > 0) {
            Level *mv = &lv;

            level_light_cache_reset();
            mb_reset(&gb);
            level_geometry(&gb, mv, ranges, LVL_MAX_RANGES);   /* the load build */

            QueryPerformanceCounter(&t0);
            for (int r = 0; r < REPS; r++) {
                nudge_doors(mv, 1.0f / REPS);
                mb_reset(&gb);
                level_geometry(&gb, mv, ranges, LVL_MAX_RANGES);
            }
            QueryPerformanceCounter(&t1);
            double warm_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0
                           / (double)freq.QuadPart / REPS;

            printf("  %-38s %9.3fms  %5.1f%%\n",
                   "DOOR IN MOTION, whole rebuild", warm_ms,
                   warm_ms / 16.67 * 100.0);

            /* --- the same frame, with only the moving half rebuilt ---------
               What ::scene_rebuild_moving actually runs. The static half is
               built once here, exactly as ::scene_build_level builds it once,
               and then only the suffix is thrown away and remade -- which is
               the whole claim, timed rather than asserted.
               ::scene_rebuild_moving이 실제로 실행하는 것입니다. 정적인 절반은
               ::scene_build_level이 한 번 생성하듯 이곳에서 한 번 생성하고, 그다음에는
               접미사만 버리고 다시 만듭니다. 그것이 주장의 전부이며, 단언이 아니라 측정으로
               제시합니다. */
            if (level_geometry_split(mv)) {
                level_light_cache_reset();
                mb_reset(&gb);
                level_geometry_part(&gb, mv, ranges, LVL_MAX_RANGES,
                                    LVL_PART_STATIC);
                int n_static = gb.count;
                level_geometry_part(&gb, mv, ranges, LVL_MAX_RANGES,
                                    LVL_PART_MOVING);

                QueryPerformanceCounter(&t0);
                for (int r = 0; r < REPS; r++) {
                    nudge_doors(mv, 1.0f / REPS);
                    gb.count = n_static;          /* truncate, do not reset */
                    level_geometry_part(&gb, mv, ranges, LVL_MAX_RANGES,
                                        LVL_PART_MOVING);
                }
                QueryPerformanceCounter(&t1);
                double split_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0
                                / (double)freq.QuadPart / REPS;

                printf("  %-38s %9.3fms  %5.1f%%\n",
                       "DOOR IN MOTION, moving half only", split_ms,
                       split_ms / 16.67 * 100.0);
                printf("\n  %d of %d vertices move, and the split rebuilds %.1fx\n"
                       "  less work per frame of a swing.\n",
                       gb.count - n_static, gb.count,
                       split_ms > 0 ? warm_ms / split_ms : 0.0);
            } else {
                printf("\n  a moving frame costs %.2fx a load build, and the light\n"
                       "  cache is what stands between them. This level does not\n"
                       "  split (see level_geometry_split), so that is the whole\n"
                       "  cost every frame of a swing.\n",
                       cold_ms > 0 ? warm_ms / cold_ms : 0.0);
            }
        } else {
            printf("\n  This level has no doors, so it is built once and never\n"
                   "  rebuilt: the per-frame figure does not apply to it.\n");
        }

        mb_free(&gb);
    }

    return 0;
}
