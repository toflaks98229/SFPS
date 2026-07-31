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

    mb_free(&b);
    printf(fails ? "\n%d FAILURE(S)\n" : "\nall level checks passed\n", fails);
    return fails != 0;
}
