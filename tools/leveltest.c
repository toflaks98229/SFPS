/* leveltest -- check the sector level parses, builds and answers queries.
 *
 * Geometry bugs and collision bugs look identical from inside the game (you
 * fall through something, or you cannot walk somewhere) so they are worth
 * separating before any of it is rendered.
 */

#include <stdio.h>
#include <math.h>
#include <time.h>
#include "level.h"
#include "player.h"
/* Builds real geometry to check winding and spans, so it needs the renderer's
   CPU-side half and MdlRange by value. level.h forward-declares both rather
   than including them, keeping the GL stack out of the simulation headers.
   감기 순서와 구간을 검사하기 위해 실제 지오메트리를 생성하므로, 렌더러의 CPU 측
   절반과 값으로 사용되는 MdlRange가 필요합니다. level.h는 GL 스택을 시뮬레이션
   헤더에서 배제하기 위해 이 둘을 포함하지 않고 전방 선언합니다. */
#include "render.h"
#include "model.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okf(int cond, const char *what, float got, float want) {
    printf("  %-52s %8.3f / %8.3f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void) {
    Level l;
    printf("leveltest\n\n");

    if (!level_load("arena", &l)) { printf("  level 'arena' not found\n"); return 1; }
    printf("  loaded '%s': %d sectors, %d entities, start %d %d %d\n\n",
           l.name, l.n_sectors, l.n_ents, l.start[0], l.start[1], l.start[2]);

    /* Shape, not contents: `arena` is a map somebody edits, and a test that
       says "six sectors" fails the moment they add a seventh, which teaches
       you to ignore the suite rather than to read it. */
    ok(l.n_sectors >= 2, "the level parsed more than one sector");
    ok(l.n_ents   >= 1,  "and at least one entity");
    ok(l.sectors[0].n >= 3, "the first sector is a real polygon");

    /* --- geometry --- */
    MeshBuf b;
    mb_init(&b, 32768);
    MdlRange ranges[LVL_MAX_RANGES];
    int nr = level_geometry(&b, &l, ranges, LVL_MAX_RANGES);

    printf("\n  geometry: %d verts, %d material ranges\n", b.count, nr);
    for (int i = 0; i < nr; i++)
        printf("    %-8s first %5d  count %5d\n",
               ranges[i].mat, ranges[i].first, ranges[i].count);

    ok(b.count > 0,       "geometry produced vertices");
    ok(nr > 0 && nr <= LVL_MAX_RANGES, "material ranges within bounds");

    int covered = 0;
    for (int i = 0; i < nr; i++) covered += ranges[i].count;
    ok(covered == b.count, "ranges cover every vertex exactly once");

    ok(b.count % 3 == 0, "vertex count is a whole number of triangles");

    /* A triangle whose winding disagrees with its normal is culled from
       exactly the side it is lit on, so it reads as a missing face rather
       than as a shading bug. Every wall in the level was once wrong this way. */
    {
        int flipped = 0, up = 0, down = 0, side = 0;
        for (int i = 0; i + 2 < b.count; i += 3) {
            v3 p0 = v3f(b.v[i].px,   b.v[i].py,   b.v[i].pz);
            v3 p1 = v3f(b.v[i+1].px, b.v[i+1].py, b.v[i+1].pz);
            v3 p2 = v3f(b.v[i+2].px, b.v[i+2].py, b.v[i+2].pz);
            v3 geo = v3norm(v3cross(v3sub(p1, p0), v3sub(p2, p0)));
            v3 nrm = v3f(b.v[i].nx, b.v[i].ny, b.v[i].nz);
            if (nrm.y > 0.9f) up++; else if (nrm.y < -0.9f) down++; else side++;
            if (v3dot(geo, nrm) < 0.5f) flipped++;
        }
        printf("    %d up, %d down, %d side\n", up, down, side);
        ok(flipped == 0, "every triangle's winding agrees with its normal");
        ok(up > 0 && down > 0 && side > 0, "floors, ceilings and walls all present");
    }

    /* A pit is invisible if the sector above it is still drawn across the
       hole: the pit, its walls and its collision are all there and none of it
       can be seen. So no floor triangle at the room's height may contain a
       point that a lower sector governs.

       The pit is found rather than named, so this keeps working as the map
       is edited. */
    {
        int pit = -1;
        for (int i = 1; i < l.n_sectors; i++)
            if (l.sectors[i].floor < l.sectors[0].floor) { pit = i; break; }
        float px = 0.0f, pz = 0.0f;
        if (pit >= 0) {
            const Sector *ps = &l.sectors[pit];
            for (int k = 0; k < ps->n; k++) {
                px += ps->pts[k*2] * 0.01f;
                pz += ps->pts[k*2+1] * 0.01f;
            }
            px /= ps->n; pz /= ps->n;
        }
        float room_y = l.sectors[0].floor * 0.01f;
        int over = 0;
        for (int i = 0; pit >= 0 && i + 2 < b.count; i += 3) {
            if (b.v[i].ny < 0.9f) continue;                  /* floors only */
            if (fabsf(b.v[i].py - room_y) > 0.001f) continue;
            float ax = b.v[i].px,   az = b.v[i].pz;
            float bx = b.v[i+1].px, bz = b.v[i+1].pz;
            float cx = b.v[i+2].px, cz = b.v[i+2].pz;
            /* Area first: a degenerate triangle satisfies every side test at
               once and would read as covering the whole map. */
            float a2 = (bx-ax)*(cz-az) - (cx-ax)*(bz-az);
            if (a2 > -1e-6f && a2 < 1e-6f) continue;
            float d0 = (bx-ax)*(pz-az) - (bz-az)*(px-ax);
            float d1 = (cx-bx)*(pz-bz) - (cz-bz)*(px-bx);
            float d2 = (ax-cx)*(pz-cz) - (az-cz)*(px-cx);
            if ((d0 >= 0 && d1 >= 0 && d2 >= 0) ||
                (d0 <= 0 && d1 <= 0 && d2 <= 0)) over++;
        }
        ok(pit >= 0, "the level has a sunken sector to test");
        okf(over == 0, "no floor is drawn over it", (float)over, 0.0f);
    }

    /* --- ground queries -----------------------------------------------------
       Derived from the level rather than written out, because `arena` is a map
       somebody edits. Pinning these to its numbers meant every map edit broke
       the test suite, which teaches you to ignore it. What matters is the
       rules, not this week's floor heights. */
    float f, c;
    printf("\n");

    const Sector *room = &l.sectors[0];
    float rf = room->floor * 0.01f, rc = room->ceil * 0.01f;

    /* Centroid of the first sector, which is the room every level starts as. */
    float rx = 0.0f, rz = 0.0f;
    for (int i = 0; i < room->n; i++) {
        rx += room->pts[i*2] * 0.01f;
        rz += room->pts[i*2+1] * 0.01f;
    }
    rx /= room->n; rz /= room->n;

    ok(level_ground(&l, rx, rz, rf, 10.0f, &f, &c),
       "the room's centroid is inside a sector");
    okf(fabsf(f - rf) < 0.001f, "and reports the room's own floor", f, rf);
    okf(fabsf(c - rc) < 0.001f, "and the room's own ceiling", c, rc);

    ok(!level_ground(&l, 1000.0f, 1000.0f, 0.0f, 10.0f, &f, &c),
       "far outside the map is in no sector");

    /* Overlapping sectors: whichever was declared last governs the point,
       whether it is above the room (a platform) or below it (a pit). */
    {
        int checked = 0, wrong = 0;
        for (int i = 1; i < l.n_sectors; i++) {
            const Sector *sec = &l.sectors[i];
            float cx = 0.0f, cz = 0.0f;
            for (int k = 0; k < sec->n; k++) {
                cx += sec->pts[k*2] * 0.01f;
                cz += sec->pts[k*2+1] * 0.01f;
            }
            cx /= sec->n; cz /= sec->n;
            /* Only sectors nothing later overlaps can be predicted this way. */
            if (level_sector_at(&l, cx, cz) != i) continue;
            checked++;
            float want = sec->floor * 0.01f;
            if (!level_ground(&l, cx, cz, want, 10.0f, &f, &c)) { wrong++; continue; }
            if (fabsf(f - want) > 0.001f) wrong++;
        }
        ok(checked > 0, "the level has overlapping sectors to test");
        okf(wrong == 0, "each one governs its own centre", (float)wrong, 0.0f);
    }

    /* A floor out of stepping reach must be refused rather than silently
       dropping through to whatever is underneath. */
    {
        int high = 0;
        for (int i = 1; i < l.n_sectors; i++)
            if (l.sectors[i].floor > l.sectors[high].floor) high = i;
        const Sector *sec = &l.sectors[high];
        float cx = 0.0f, cz = 0.0f;
        for (int k = 0; k < sec->n; k++) {
            cx += sec->pts[k*2] * 0.01f;
            cz += sec->pts[k*2+1] * 0.01f;
        }
        cx /= sec->n; cz /= sec->n;
        float top = sec->floor * 0.01f;
        ok(top - rf > PLAYER_STEP, "the level has a ledge too high to step onto");
        ok(!level_ground(&l, cx, cz, rf, PLAYER_STEP, &f, &c),
           "which cannot be stood on from the floor below");
        ok(level_ground(&l, cx, cz, top, PLAYER_STEP, &f, &c) &&
           fabsf(f - top) < 0.001f, "but can be once you are up there");
    }

    /* --- traces --- */
    printf("\n");
    float t; v3 n;

    v3 eye = v3f(rx, rf + 1.7f, rz);
    ok(level_trace(&l, eye, v3f(0, -1, 0), 120.0f, &t, &n),
       "straight down hits the floor");
    okf(fabsf(t - 1.7f) < 0.02f, "the floor is 1.7 below the eye", t, 1.7f);
    okf(n.y > 0.9f, "floor normal points up", n.y, 1.0f);

    ok(level_trace(&l, eye, v3f(0, 1, 0), 120.0f, &t, &n),
       "straight up hits the ceiling");
    okf(fabsf(t - (rc - rf - 1.7f)) < 0.02f, "at the room's ceiling height",
        t, rc - rf - 1.7f);
    okf(n.y < -0.9f, "ceiling normal points down", n.y, -1.0f);

    ok(level_trace(&l, eye, v3f(-1, 0, 0), 200.0f, &t, &n),
       "sideways hits a wall");
    okf(fabsf(n.y) < 0.2f, "wall normal is horizontal", n.y, 0.0f);

    /* --- a hit normal must FACE the ray that found it ----------------------
       Checking only that the wall normal is horizontal was not enough, and the
       gap shipped: nearest_edge_normal returns the sector's OUTWARD normal,
       which for a player standing inside a room points into the wall. All four
       walls came back pointing away from the shooter while the floor and
       ceiling were correct -- those two derive their normal from the ray's own
       direction rather than from the polygon.

       Nothing crashed and nothing looked wrong in the geometry, so the only
       symptom was impact particles being thrown into the wall and never seen.
       Every direction is checked here, not just one, because the previous
       version passed on the single case it happened to try.
       충돌 법선은 자신을 찾아낸 광선을 *향해야* 합니다. 벽 법선이 수평인지만 검사한
       것으로는 부족했고 그 틈이 출시되었습니다. 아무것도 중단되지 않고 지오메트리도
       멀쩡해 보였으므로, 유일한 증상은 피격 파티클이 벽 속으로 던져져 보이지 않는
       것이었습니다. 이전 버전이 우연히 시도한 한 가지 경우에서 통과했기 때문에, 여기서는
       한 방향이 아니라 모든 방향을 검사합니다. */
    {
        v3 DIRS[6] = { v3f( 1,0,0), v3f(-1,0,0), v3f(0,0, 1),
                       v3f(0,0,-1), v3f(0, 1,0), v3f(0,-1,0) };
        int away = 0, tested = 0;
        for (int i = 0; i < 6; i++) {
            float tt; v3 nn;
            if (!level_trace(&l, eye, DIRS[i], 200.0f, &tt, &nn)) continue;
            tested++;
            if (v3dot(nn, DIRS[i]) > 0.0f) away++;
        }
        ok(tested >= 5, "the fixture presents surfaces in every direction");
        okf(away == 0,
            "every hit normal faces the ray, so particles spawn outward",
            (float)away, 0.0f);
    }

    /* --- level_blocked must agree with level_trace -------------------------
       level_blocked is the visibility half of level_trace, split out so a
       line-of-sight test stops paying for the bisection that locates the hit
       and the edge scan that derives a normal there. It is the call the monster
       AI now makes once per monster instead of a full trace.

       The two share ::march precisely so they cannot disagree, and this is what
       holds that claim to account. A disagreement has one visible symptom and
       it is a bad one: level_blocked reporting clear where level_trace reports
       a hit is a monster that sees, and therefore shoots, through a wall --
       exactly what the caster's second line-of-sight check exists to prevent.

       Rays are cast in every direction from a point inside the map rather than
       along the axes only, because a disagreement would come from the marcher's
       step landing differently on a grazing angle, and an axis-aligned ray is
       the case least likely to expose that.

       level_blocked는 level_trace의 가시성 부분을 분리한 것으로, 시야 판정이 충돌 지점을
       찾는 이분 탐색과 그곳의 법선을 유도하는 모서리 순회 비용을 더 이상 치르지 않게
       합니다. 몬스터 AI가 이제 온전한 판정 대신 몬스터마다 한 번씩 호출하는 함수입니다.

       두 함수는 서로 어긋날 수 없도록 ::march를 공유하며, 이 검사가 그 주장을 실제로
       책임집니다. 어긋남의 증상은 하나뿐이고 그것은 나쁜 증상입니다. level_trace가 충돌을
       보고하는 곳에서 level_blocked가 뚫려 있다고 답하면, 그것은 벽을 통해 보고 따라서 벽을
       통해 쏘는 몬스터입니다. 캐스터의 두 번째 시야 검사가 막으려는 것이 정확히 그것입니다.

       축 방향만이 아니라 모든 방향으로 광선을 쏩니다. 어긋남이 생긴다면 스치는 각도에서
       마처의 간격이 다르게 놓이는 데서 올 텐데, 축에 정렬된 광선은 그것을 드러낼 가능성이
       가장 낮은 경우이기 때문입니다. */
    {
        int disagree = 0, cast = 0, blocked_n = 0;
        unsigned rng = 0x13579bdfu;
        for (int i = 0; i < 400; i++) {
            rng = rng * 1664525u + 1013904223u;
            float a = ((rng >> 8) & 0xffff) / 65536.0f * 6.2831853f;
            rng = rng * 1664525u + 1013904223u;
            float e = (((rng >> 8) & 0xffff) / 65536.0f - 0.5f) * 1.6f;

            v3 d = v3norm(v3f(cosf(a), e, sinf(a)));
            float tt; v3 nn;
            int hit = level_trace(&l, eye, d, 60.0f, &tt, &nn);
            int blk = level_blocked(&l, eye, d, 60.0f);
            cast++;
            if (blk) blocked_n++;
            if (hit != blk) disagree++;
        }
        ok(cast > 0 && blocked_n > 0,
           "the fixture has rays that actually hit something");
        okf(disagree == 0,
            "level_blocked agrees with level_trace on every ray",
            (float)disagree, 0.0f);
    }

    /* --- partial overlap ---------------------------------------------------
       Two sectors overlapping only part of an edge -- the case the whole
       last-wins authoring model invites, and the one that was broken.

           room   -400..400 x -400..400
           slab    100..900 x -100..100     <- a corridor pushed out east

       The room's east edge is x=400, running z=-400..400. The slab meets it
       only over z=-100..100. There the two sectors join and nothing should be
       solid; north and south of that the same edge faces the void and must be
       a full-height wall.

       The old rule asked "what is beyond this edge?" once, at the edge's
       midpoint, and applied the answer to the whole edge. The midpoint here
       lands inside the slab, so the entire east wall vanished along its full
       length even though only a quarter of it was actually open -- and with
       differing heights what survived was just the step, which is why the
       opening looked as tall as the overlap. */
    printf("\n");
    {
        Level o;
        Level zero = {0};
        o = zero;
        o.n_sectors = 2;

        Sector *room = &o.sectors[0];
        room->n = 4;
        short rp[8] = { -400,-400,  400,-400,  400,400,  -400,400 };
        for (int i = 0; i < 8; i++) room->pts[i] = rp[i];
        room->floor = 0; room->ceil = 600;

        Sector *slab = &o.sectors[1];
        slab->n = 4;
        short qp[8] = { 100,-100,  900,-100,  900,100,  100,100 };
        for (int i = 0; i < 8; i++) slab->pts[i] = qp[i];
        slab->floor = 0; slab->ceil = 600;

        EdgeSpan sp[LVL_MAX_SPANS];
        int n = level_edge_spans(&o, 0, 1, sp, LVL_MAX_SPANS);

        /* Edge 1 runs z=-400..400, so t is linear in z. */
        float ez[3]        = { -300.0f, 0.0f, 300.0f };
        float ewant[3]     = { 6.0f,    0.0f, 6.0f };
        const char *ename[3] = {
            "east edge south of the opening is a full wall",
            "east edge across the opening is not solid",
            "east edge north of the opening is a full wall",
        };
        for (int k = 0; k < 3; k++) {
            float t = (ez[k] + 400.0f) / 800.0f, solid = 0.0f;
            for (int i = 0; i < n; i++)
                if (t >= sp[i].t0 && t <= sp[i].t1) solid += sp[i].y1 - sp[i].y0;
            okf(fabsf(solid - ewant[k]) < 0.01f, ename[k], solid, ewant[k]);
        }

        /* And through the geometry, which is what the player walks into. */
        MeshBuf ob;
        mb_init(&ob, 8192);
        level_geometry(&ob, &o, 0, 0);

        /* The plane x=4 is also touched by the room's floor corners and by
           the slab's own north and south walls, so select on the normal too:
           only the east wall faces along x. */
        int on_east = 0, in_gap = 0;
        for (int i = 0; i < ob.count; i++) {
            if (fabsf(ob.v[i].px - 4.0f) > 0.001f) continue;
            if (fabsf(ob.v[i].nx) < 0.9f) continue;
            on_east++;
            if (fabsf(ob.v[i].pz) < 0.99f) in_gap++;
        }
        /* Two quads, six vertices each: the wall either side of the opening. */
        okf(on_east == 12, "east wall is two pieces, not none and not one",
            (float)on_east, 12.0f);
        okf(in_gap == 0, "and no wall is built across the opening",
            (float)in_gap, 0.0f);
        mb_free(&ob);
    }

    /* --- a box dropped into a room ------------------------------------------
       A sector laid inside another is a platform. If it keeps a lower ceiling
       than the room it sits in, the volume above it is solid and gets built
       as walls -- a pillar standing on the box, which is what a reported bug
       looked like. The editor now inherits the ceiling from the sector under
       the cursor; this is the rule that makes that the right thing to do. */
    printf("\n");
    {
        for (int variant = 0; variant < 2; variant++) {
            Level o;
            Level zero = {0};
            o = zero;
            o.n_sectors = 2;

            Sector *rm = &o.sectors[0];
            short rp[8] = { -800,-800,  800,-800,  800,800,  -800,800 };
            for (int i = 0; i < 8; i++) rm->pts[i] = rp[i];
            rm->n = 4; rm->floor = 0; rm->ceil = 600;

            Sector *bx = &o.sectors[1];
            short bp[8] = { -200,-200,  200,-200,  200,200,  -200,200 };
            for (int i = 0; i < 8; i++) bx->pts[i] = bp[i];
            bx->n = 4; bx->floor = 50;
            bx->ceil = variant ? 300 : 600;   /* 300 is the old hardcoded value */

            MeshBuf ob;
            mb_init(&ob, 16384);
            level_geometry(&ob, &o, 0, 0);

            int above = 0;
            for (int i = 0; i < ob.count; i++) {
                if (fabsf(ob.v[i].ny) > 0.5f) continue;          /* walls only */
                if (fabsf(ob.v[i].px) > 2.01f) continue;
                if (fabsf(ob.v[i].pz) > 2.01f) continue;
                if (ob.v[i].py > 0.55f) above++;                 /* above its top */
            }
            if (variant == 0)
                okf(above == 0, "a box sharing the room's ceiling has nothing on top",
                    (float)above, 0.0f);
            else
                ok(above > 0, "and a lower ceiling really does build that pillar");
            mb_free(&ob);
        }
    }

    /* --- worst-case build cost ---------------------------------------------
       Cutting edges made level_geometry quadratic in sector count: every edge
       is now tested against every other sector's outline. The editor rebuilds
       on every frame of a drag, so this needs to stay well inside a frame at
       the format's limit, not just on a six-sector test map. */
    printf("\n");
    {
        Level big;
        Level zero = {0};
        big = zero;
        /* A full grid of overlapping squares -- every sector touching its
           neighbours, which is the expensive arrangement. */
        for (int gz = 0; gz < 8; gz++) {
            for (int gx = 0; gx < 8; gx++) {
                Sector *s = &big.sectors[big.n_sectors++];
                short x0 = (short)(gx * 300 - 1200), z0 = (short)(gz * 300 - 1200);
                short x1 = (short)(x0 + 400),        z1 = (short)(z0 + 400);
                short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
                for (int i = 0; i < 8; i++) s->pts[i] = p[i];
                s->n = 4;
                s->floor = (short)((gx + gz) * 20);
                s->ceil = 600;
            }
        }

        MeshBuf bb;
        mb_init(&bb, 65536);
        clock_t t0 = clock();
        int reps = 20;
        for (int i = 0; i < reps; i++) { mb_reset(&bb); level_geometry(&bb, &big, 0, 0); }
        float ms = (float)(clock() - t0) * 1000.0f / CLOCKS_PER_SEC / reps;
        printf("  %d sectors, %d verts: %.2f ms per rebuild\n",
               big.n_sectors, bb.count, ms);
        ok(ms < 16.0f, "a full level rebuilds inside one frame");
        mb_free(&bb);
    }

    /* --- the bounding-box fast path must never change an ANSWER ------------
       point_in_sector rejects against a cached box before running the
       crossing test. That is an optimisation, and an optimisation that
       disagrees with the thing it optimises is just a bug with better timing.

       Two properties are checked, and the second is the one that actually bit:

         1. With bounds computed, every query agrees with the same query on a
            sector whose bounds were never computed.
         2. A Level assembled FIELD BY FIELD -- which every headless fixture in
            tools/ does, and which no one thinks of as an initialisation step --
            is correct without anyone calling level_bounds.

       Property 2 failed once already. `Level l = {0}` leaves a box of
       (0,0)-(0,0), which is a perfectly valid box that contains only the
       origin, so every sector rejected every point and the entire level became
       solid. Five test suites went red at once. The fix was a separate
       has_bounds flag, whose zero value means "not computed" -- so the safe
       answer is the one zeroed memory already gives. */
    {
        Level hand = {0};
        Sector *s = &hand.sectors[hand.n_sectors++];
        short p[8] = { -1200,-1200,  1200,-1200,  1200,1200,  -1200,1200 };
        for (int i = 0; i < 8; i++) s->pts[i] = p[i];
        s->n = 4; s->floor = 0; s->ceil = 600;

        ok(!s->has_bounds,
           "a hand-built sector starts with no bounds -- zeroed means unknown");

        /* Sample a grid over and around the square, recording the answer
           before and after the bounds exist. */
        int disagreed = 0, inside_seen = 0, outside_seen = 0;
        float fa, ca, fb, cb;
        float probe[9] = { -30.0f, -12.0f, -11.9f, -6.0f, 0.0f,
                            6.0f, 11.9f, 12.0f, 30.0f };

        int before[9][9];
        for (int i = 0; i < 9; i++)
            for (int j = 0; j < 9; j++) {
                before[i][j] = level_ground(&hand, probe[i], probe[j],
                                            -1e9f, 1e9f, &fa, &ca);
                if (before[i][j]) inside_seen++; else outside_seen++;
            }

        level_bounds(s);
        ok(s->has_bounds, "and level_bounds marks them computed");

        for (int i = 0; i < 9; i++)
            for (int j = 0; j < 9; j++) {
                int after = level_ground(&hand, probe[i], probe[j],
                                         -1e9f, 1e9f, &fb, &cb);
                if (after != before[i][j]) disagreed++;
            }

        /* Both outcomes must appear, or the comparison proved nothing: a grid
           entirely outside the square would agree trivially. */
        ok(inside_seen > 0 && outside_seen > 0,
           "the probe grid straddles the sector's edge");
        okf(disagreed == 0,
            "the box rejects only points the crossing test rejects anyway",
            (float)disagreed, 0.0f);
    }

    /* --- point lights parse, and stay inside their cap ---------------------
       A light is eight integers on one line, and the parser has to consume
       exactly those eight -- a miscount would leave the reader mid-line and
       every declaration after it would be read as garbage. That failure is
       silent: the level still loads, it is just darker or lit wrongly.

       The cap matters as much as the parse. LVL_MAX_LIGHTS bounds what a level
       may declare and RD_MAX_LIGHTS bounds what the shader evaluates; a level
       between the two would store lights that never appear. level.c asserts
       the relationship at compile time, and this checks the runtime half.
       광원은 한 줄에 정수 여덟 개이며, 파서는 정확히 그 여덟 개를 소비해야 합니다. 개수를
       잘못 세면 읽기 위치가 줄 중간에 남아 이후의 모든 선언이 쓰레기 값으로 읽힙니다. 이
       실패는 조용합니다. 레벨은 여전히 로드되며 다만 더 어둡거나 잘못 조명될 뿐입니다. */
    {
        Level lit;
        ok(level_load("arena", &lit), "the arena loads for the light check");

        okf(lit.n_lights > 0,
            "and declares at least one point light",
            (float)lit.n_lights, 1.0f);
        okf(lit.n_lights <= LVL_MAX_LIGHTS,
            "never more than the cap, whatever the file says",
            (float)lit.n_lights, (float)LVL_MAX_LIGHTS);

        /* Every field has to survive the parse. A radius of zero lights
           nothing and a power of zero is invisible, so either would make the
           light exist in memory and not on screen -- exactly the kind of
           silent nothing this suite exists to catch. */
        int sane = 1;
        for (int i = 0; i < lit.n_lights; i++) {
            const Light *L = &lit.lights[i];
            if (L->radius <= 0) sane = 0;
            if (L->power  <= 0) sane = 0;
            if (L->r < 0 || L->g < 0 || L->b < 0) sane = 0;
            if (L->r > 255 || L->g > 255 || L->b > 255) sane = 0;
        }
        ok(sane, "every light has a usable radius, power and colour");

        /* The parser must not have lost its place: entities are declared
           around the lights in this file, so a light that consumed the wrong
           number of tokens would eat them. */
        ok(lit.n_ents > 0,
           "and the entities around them still parsed -- the reader kept its place");
    }

    /* --- hazard floors -----------------------------------------------------
       `hurt <dps>` makes a floor damage whatever stands on it. Two properties
       matter and neither is visible from inside the game until a player is
       standing in lava wondering why nothing is happening:

       A sector with no `hurt` must read as exactly zero, or every floor in
       every existing level becomes lethal the moment the field is added.

       And the LAST-WINS rule has to apply here as it does to floor height,
       because that is what allows a safe platform in the middle of a lava
       pit -- which is the only thing that makes a lava room playable rather
       than merely a wall.

       Built by hand rather than read from the shipped level, for the reason
       the fixture note at the top of this file gives: a test that asserts this
       week's map goes red the moment somebody edits it.

       `hurt <dps>`는 바닥이 그 위에 선 대상에게 피해를 주게 합니다. 두 가지 성질이
       중요하며, 둘 다 플레이어가 용암 위에 서서 왜 아무 일도 없는지 의아해하기
       전까지는 게임 안에서 보이지 않습니다.

       `hurt`가 없는 섹터는 정확히 0으로 읽혀야 합니다. 그렇지 않으면 이 필드가 추가되는
       순간 기존 모든 레벨의 모든 바닥이 치명적이 됩니다.

       그리고 바닥 높이와 마찬가지로 마지막 선언 우선 규칙이 적용되어야 합니다. 그것이
       용암 구덩이 한가운데의 안전한 발판을 가능하게 하며, 그것만이 용암 방을 단순한 벽이
       아니라 플레이 가능한 곳으로 만듭니다.

       이 파일 상단의 픽스처 참고 사항이 밝히는 이유로, 배포되는 레벨을 읽지 않고 손으로
       만듭니다. 이번 주의 맵을 단언하는 테스트는 누군가 그것을 편집하는 순간
       빨간불이 됩니다. */
    {
        Level h;
        Level zero = {0};
        h = zero;

        /* A big safe room. */
        Sector *room = &h.sectors[h.n_sectors++];
        short rp[8] = { -1000,-1000, 1000,-1000, 1000,1000, -1000,1000 };
        for (int i = 0; i < 8; i++) room->pts[i] = rp[i];
        room->n = 4; room->floor = 0; room->ceil = 600; room->hurt = 0;

        /* A lava pool inside it. */
        Sector *lava = &h.sectors[h.n_sectors++];
        short lp[8] = { -500,-500, 500,-500, 500,500, -500,500 };
        for (int i = 0; i < 8; i++) lava->pts[i] = lp[i];
        lava->n = 4; lava->floor = -40; lava->ceil = 600; lava->hurt = 20;

        /* And a safe platform in the middle of the lava, declared last. */
        Sector *isle = &h.sectors[h.n_sectors++];
        short ip[8] = { -150,-150, 150,-150, 150,150, -150,150 };
        for (int i = 0; i < 8; i++) isle->pts[i] = ip[i];
        isle->n = 4; isle->floor = 20; isle->ceil = 600; isle->hurt = 0;

        for (int i = 0; i < h.n_sectors; i++) level_bounds(&h.sectors[i]);
        level_grid_build(&h);

        okf(level_hazard_at(&h, 0.0f, 0.0f) == 0,
            "the safe island in the lava hurts nothing",
            (float)level_hazard_at(&h, 0.0f, 0.0f), 0.0f);

        okf(level_hazard_at(&h, 3.5f, 0.0f) == 20,
            "the lava around it does",
            (float)level_hazard_at(&h, 3.5f, 0.0f), 20.0f);

        okf(level_hazard_at(&h, 8.0f, 8.0f) == 0,
            "and the room outside the lava does not",
            (float)level_hazard_at(&h, 8.0f, 8.0f), 0.0f);

        okf(level_hazard_at(&h, 500.0f, 500.0f) == 0,
            "a point outside the map is not a hazard either",
            (float)level_hazard_at(&h, 500.0f, 500.0f), 0.0f);

        /* A level parsed with no `hurt` anywhere must be entirely safe. This
           is the check that would catch the field being left uninitialised by
           the sector parser, which would make every floor hazardous by
           whatever happened to be on the stack.
           `hurt`가 전혀 없는 레벨은 완전히 안전해야 합니다. 섹터 파서가 이 필드를
           초기화하지 않고 두는 경우를 잡아내는 검사이며, 그렇게 되면 스택에 남아 있던
           값에 따라 모든 바닥이 위험해집니다. */
        Level plain;
        if (level_load("arena", &plain)) {
            int any = 0;
            for (int i = 0; i < plain.n_sectors; i++)
                if (plain.sectors[i].hurt != 0) any = 1;
            ok(!any, "a level that authors no hazard has none");
        }
    }

    mb_free(&b);
    printf(fails ? "\n%d FAILURE(S)\n" : "\nall level checks passed\n", fails);
    return fails != 0;
}
