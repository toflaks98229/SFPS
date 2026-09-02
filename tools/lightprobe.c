/* lightprobe -- the sun a level declares actually reaches the level.
 *
 * TWO THINGS IN ONE FILE, and only the second is a claim. Most of it measures:
 * how big a face is, what subdividing would cost, what the bake put on each
 * vertex, and which of the three refusals accounts for a lamp lighting nothing.
 * Those are numbers to read rather than assertions to hold, and reading them is
 * what found the defect below. The bake is per VERTEX (see level.c's
 * bake_light), so the resolution of the light is the resolution of the
 * geometry, and the geometry is most of what these numbers describe.
 *
 * WHAT THESE NUMBERS ARGUED FOR, AND WON. The measurement below -- 93.3% of
 * vertex-lamp pairs rejected on distance, 0.5% lighting anything -- is the
 * evidence that took the point lamps OUT of the bake and into the shader's
 * per-fragment loop (scene.c's LIGHT_LAMP_POWER). So the lamp half of this
 * file no longer describes what happens at load; it describes what WOULD
 * happen if a lamp were baked, which is exactly the question to ask before
 * anybody proposes baking one again. The face-size and sun halves are
 * unchanged and still measure the shipping path.
 *
 * NO SHIPPED MAP DECLARES A SUN ANY MORE, so the claim below runs against one
 * this file puts there: `lqdm1`'s own `_sunlight 120` / `_sunlight2 50` /
 * `_sun_mangle "136 -73 0"`, which is what its worldspawn carried until the
 * sky lighting was taken out of it. That makes this a FIXTURE rather than a
 * measurement of what ships. It is kept because the code it exercises is kept:
 * ::level_sun_reaches and the walk beneath it still compile, still run for any
 * level that declares a sun, and would still be wrong in the way recorded
 * below if nothing held them to it.
 *
 * WHAT IT CHECKS, AND WHAT THAT CAUGHT. A level lit by "_sunlight" needs its
 * rays to pass THROUGH the sky, because this engine has no sky pass: a sky face
 * is drawn and collided with as the solid it is, so every ray toward the sun
 * hits one. level.c walks past it. The first version of that walk stepped two
 * centimetres past the FACE it hit rather than out of the BRUSH -- and a skybox
 * wall is metres thick. Four passes advanced eight centimetres and the ray never
 * left the wall: of the 12,504 vertices that face the sun, passing sky lifted
 * the sunlit ones from 174 to 209, where the walk as it ships reaches 3,201.
 * A fix shaped exactly like a fix, doing almost nothing. Nothing else in this
 * suite looks at where light lands, so nothing else could have said so.
 *
 * 한 파일에 두 가지가 있고, 주장하는 것은 두 번째뿐입니다. 대부분은 측정이며 그 측정이
 * 아래의 결함을 찾아냈습니다. 검사하는 것은 하나입니다. "_sunlight"를 선언한 레벨에서
 * 태양이 실제로 레벨에 닿는가. 이 엔진에는 하늘 패스가 없어 하늘 면도 솔리드이므로
 * 태양을 향한 모든 광선이 그것에 부딪히며, level.c가 그것을 지나갑니다. 그 걸음의 첫
 * 판은 부딪힌 면에서 2cm만 나아갔습니다. 스카이박스 벽은 몇 미터 두께입니다. 광선은
 * 벽을 빠져나가지 못했고, 태양을 향한 12,504개 중 햇빛을 받는 정점은 174개에서 209개가
 * 되었을 뿐입니다. 출하되는 걸음은 3,201개에 닿습니다. 고침처럼 생겼으나 거의 아무것도
 * 고치지 않은 것입니다. 이 스위트에서 빛이 어디에 닿는지 보는 것은
 * 이것뿐이므로, 다른 무엇도 그것을 말해줄 수 없었습니다.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "world.h"
#include "level.h"
#include "brush.h"
#include "render.h"   /* MeshBuf, Vtx -- what the bake writes into */

static World W;
static int fails;

static void okf(int cond, const char *what, float got, float want) {
    printf("  %-58s %8.2f / %8.2f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static float poly_area(const v3 *p, int n) {
    if (n < 3) return 0.0f;
    v3 acc = v3f(0, 0, 0);
    for (int i = 1; i + 1 < n; i++) {
        v3 a = v3sub(p[i], p[0]), b = v3sub(p[i + 1], p[0]);
        acc = v3add(acc, v3cross(a, b));
    }
    return 0.5f * v3len(acc);
}

int main(void) {
    world_init(&W);
    W.run.title = 0;
    if (!world_load_level(&W, WORLD_BOSS_ARENA, WORLD_ENTER_NEW)) {
        printf("%s did not load\n", WORLD_BOSS_ARENA);
        return 1;
    }
    const BrushMap *m = W.level.brushes;
    if (!m) { printf("not a brush level\n"); return 1; }

    /* Face sizes, in engine units. BRUSH_UNIT converts map units to metres, so
       an edge in metres is what a player's sense of scale is in. */
    double total_area = 0.0;
    float  biggest = 0.0f, longest_edge = 0.0f;
    int    n_faces = 0, over_4m = 0, over_8m = 0;
    int    hist[6] = {0};          /* <1m2, <4, <16, <64, <256, more */

    for (int bi = 0; bi < m->n_brushes; bi++) {
        const Brush *b = &m->brushes[bi];
        for (int fi = 0; fi < b->n_faces; fi++) {
            v3 poly[BR_MAX_POLY];
            int n = brush_face_poly(m, bi, fi, poly, BR_MAX_POLY);
            if (n < 3) continue;

            float a = poly_area(poly, n);      /* square metres */
            n_faces++;
            total_area += a;
            if (a > biggest) biggest = a;
            if (a > 16.0f) over_4m++;          /* bigger than a 4m x 4m wall */
            if (a > 64.0f) over_8m++;

            for (int i = 0; i < n; i++) {
                float e = v3len(v3sub(poly[(i + 1) % n], poly[i]));
                if (e > longest_edge) longest_edge = e;
            }

            int k = a < 1 ? 0 : a < 4 ? 1 : a < 16 ? 2 : a < 64 ? 3 : a < 256 ? 4 : 5;
            hist[k]++;
        }
    }

    printf("%s: %d brushes, %d faces with area\n\n",
           WORLD_BOSS_ARENA, m->n_brushes, n_faces);
    printf("  total surface     %10.1f m2\n", total_area);
    printf("  largest face      %10.1f m2\n", (double)biggest);
    printf("  longest edge      %10.1f m\n", (double)longest_edge);
    printf("  faces over 4x4m   %10d  (%.0f%%)\n", over_4m,
           100.0 * over_4m / (n_faces ? n_faces : 1));
    printf("  faces over 8x8m   %10d  (%.0f%%)\n\n", over_8m,
           100.0 * over_8m / (n_faces ? n_faces : 1));

    static const char *BAND[] = { "<1", "1-4", "4-16", "16-64", "64-256", ">256" };
    printf("  face area, m2:\n");
    for (int i = 0; i < 6; i++)
        printf("    %-8s %6d\n", BAND[i], hist[i]);

    /* WHERE THE ART SAYS A LAMP IS.

       No shipped map has a `light` entity -- the importer drops the classname,
       so a converted map arrives with its author's lighting design deleted.
       What survives the conversion is the TEXTURE: Psychofuge draws its lamps
       with `med_tmpl_lit3`, and a face wearing it is the author pointing at a
       spot and saying a light hangs here. That is the argument brush_is_lava
       makes as well -- Quake put the fact in the surface rather than in an
       entity, and a converter that reads entities brings it across as paint.

       PRINTED RATHER THAN PLACED. These are candidate origins to paste into the
       .map as `light_day`, not lamps this tool creates: the engine reads
       entities and knows nothing about lamp textures, which keeps the rule out
       of the shipping binary and keeps the placement editable in TrenchBroom.
       The count is the reason it is worth printing at all -- twenty candidates
       against ::LVL_LAMP_MAX's three is the ratio the README's thirty-two-
       against-eight failure was made of, so whoever pastes these has to choose.

       *예술이 등이 있다고 말하는 곳입니다.* 출하되는 어떤 맵에도 `light` 엔티티가 없습니다.
       임포터가 그 classname을 버리므로, 변환된 맵은 제작자의 조명 설계가 지워진 채 도착합니다.
       변환에서 살아남는 것은 *텍스처*입니다. 그것을 입은 면은 제작자가 한 지점을 가리키며
       여기에 등이 걸린다고 말하는 것입니다. brush_is_lava가 펴는 것과 같은 논증입니다.
       *놓는 것이 아니라 출력합니다.* .map에 `light_day`로 붙여 넣을 후보 원점이며, 이 도구가
       만드는 등이 아닙니다. 엔진은 엔티티를 읽고 램프 텍스처를 모르며, 그래야 규칙이 출하
       바이너리 밖에 남고 배치가 TrenchBroom에서 편집 가능한 채로 남습니다. 개수가 이것을
       출력할 값어치의 이유입니다. 후보 스물 대 ::LVL_LAMP_MAX의 셋은 README의 실패가
       만들어진 그 비율이므로, 붙여 넣는 쪽이 골라야 합니다. */
    {
        static const char LAMPTEX[] = "med_tmpl_lit3";
        /* Out of the wall by half a metre, so the lamp is in the room rather
           than inside the solid it is drawn on. A light at the surface itself
           lights the face it sits on and little else.
           벽에서 0.5m 밖으로. 등이 그려진 고체 안이 아니라 방 안에 있도록 합니다. 표면
           자체에 놓인 빛은 자기가 앉은 면을 밝히고 그 밖은 거의 밝히지 못합니다. */
        const float STANDOFF = 0.5f;
        int n_lamp = 0;

        printf("\n  %s faces, as light_day origins in map units:\n", LAMPTEX);
        for (int bi = 0; bi < m->n_brushes; bi++) {
            const Brush *b = &m->brushes[bi];
            for (int fi = 0; fi < b->n_faces; fi++) {
                const BrushFace *f = &m->faces[b->first_face + fi];
                if (strcmp(f->tex, LAMPTEX) != 0) continue;

                v3 poly[BR_MAX_POLY];
                int n = brush_face_poly(m, bi, fi, poly, BR_MAX_POLY);
                if (n < 3) continue;

                v3 c = v3f(0.0f, 0.0f, 0.0f);
                for (int i = 0; i < n; i++) c = v3add(c, poly[i]);
                c = v3scale(c, 1.0f / (float)n);
                c = v3add(c, v3scale(f->normal, STANDOFF));

                /* Engine metres back to map units. brush.c's map_pos is
                   (x, z, -y) scaled by ::BRUSH_UNIT and this is its inverse,
                   written out rather than shared because a reader checking one
                   against the other is the only thing keeping them in step.
                   엔진 미터를 맵 단위로 되돌립니다. brush.c의 map_pos의 역이며, 공유하는
                   대신 적어 두는 이유는 둘을 맞대어 보는 독자만이 둘의 발을 맞추기
                   때문입니다. */
                printf("    \"origin\" \"%.0f %.0f %.0f\"   %5.1f m2\n",
                       (double)( c.x / BRUSH_UNIT),
                       (double)(-c.z / BRUSH_UNIT),
                       (double)( c.y / BRUSH_UNIT),
                       (double)poly_area(poly, n));
                n_lamp++;
            }
        }
        printf("    %d lamp face(s), %d lit at once (LVL_LAMP_MAX)\n",
               n_lamp, LVL_LAMP_MAX);
    }

    /* WHAT A PATCH SIZE WOULD COST. A face cut into patches of side L needs
       about area/L^2 quads, and a quad is 6 vertices the way brush_geometry
       fans them. Quake used 16 map units, which is BRUSH_UNIT * 16 = 0.5m. */
    printf("\n  vertices if faces were cut into patches of side L:\n");
    printf("    %-8s %12s %12s\n", "L", "vertices", "of 49152");
    static const float L[] = { 8.0f, 4.0f, 2.0f, 1.0f, 0.5f };
    for (int li = 0; li < 5; li++) {
        double verts = 0.0;
        for (int bi = 0; bi < m->n_brushes; bi++) {
            const Brush *b = &m->brushes[bi];
            for (int fi = 0; fi < b->n_faces; fi++) {
                v3 poly[BR_MAX_POLY];
                int n = brush_face_poly(m, bi, fi, poly, BR_MAX_POLY);
                if (n < 3) continue;
                float a = poly_area(poly, n);
                double patches = ceil(a / (L[li] * L[li]));
                if (patches < 1.0) patches = 1.0;
                verts += patches * 6.0;
            }
        }
        printf("    %-8.2fm %12.0f %11.1fx\n", (double)L[li], verts, verts / 49152.0);
    }

    /* ADAPTIVE: only a face a light can actually reach needs resolution.
       Everything outside every radius is uniformly unlit, and subdividing it
       buys vertices and no light. */
    printf("\n  lights in this level: %d\n", W.level.n_lights);
    printf("  vertices if only faces a light REACHES are cut:\n");
    printf("    %-8s %12s %12s\n", "L", "vertices", "of 49152");
    for (int li = 0; li < 5; li++) {
        double verts = 0.0;
        int touched = 0;
        for (int bi = 0; bi < m->n_brushes; bi++) {
            const Brush *b = &m->brushes[bi];
            for (int fi = 0; fi < b->n_faces; fi++) {
                v3 poly[BR_MAX_POLY];
                int n = brush_face_poly(m, bi, fi, poly, BR_MAX_POLY);
                if (n < 3) continue;
                float a = poly_area(poly, n);

                int lit = 0;
                for (int k = 0; k < W.level.n_lights && !lit; k++) {
                    const Light *L2 = &W.level.lights[k];
                    v3 lp = v3f(L2->x * 0.01f, L2->y * 0.01f, L2->z * 0.01f);
                    float r = L2->radius * 0.01f;
                    for (int v = 0; v < n; v++)
                        if (v3len(v3sub(poly[v], lp)) < r) { lit = 1; break; }
                    if (!lit) {
                        v3 c = v3f(0,0,0);
                        for (int v = 0; v < n; v++) c = v3add(c, poly[v]);
                        c = v3scale(c, 1.0f / n);
                        if (v3len(v3sub(c, lp)) < r) lit = 1;
                    }
                }

                double patches = 1.0;
                if (lit) {
                    touched++;
                    patches = ceil(a / (L[li] * L[li]));
                    if (patches < 1.0) patches = 1.0;
                }
                verts += patches * 6.0;
            }
        }
        printf("    %-8.2fm %12.0f %11.1fx   (%d faces a light reaches)\n",
               (double)L[li], verts, verts / 49152.0, touched);
    }

    /* WHAT THE BAKE ACTUALLY PUT ON THE VERTICES. The geometry above bounds
       where light CAN vary; this is whether it does. Since the lamps left,
       everything counted here is the sun and the sky dome. */
    {
        static MeshBuf mb;
        static int made;
        if (!made) { mb_init(&mb, 200000); made = 1; }
        mb_reset(&mb);
        level_geometry(&mb, &W.level, 0, 0);

        int zero = 0, n = mb.count;
        float lo = 1e9f, hi = -1e9f, sum = 0.0f;
        for (int i = 0; i < n; i++) {
            const Vtx *v = &mb.v[i];
            float l = (v->lr + v->lg + v->lb) / 3.0f;
            if (l < 0.002f) zero++;
            if (l < lo) lo = l;
            if (l > hi) hi = l;
            sum += l;
        }
        printf("\n  baked vertex light over %d vertices:\n", n);
        printf("    with NO baked light at all : %d  (%.0f%%)\n",
               zero, 100.0 * zero / (n ? n : 1));
        printf("    range %.3f .. %.3f, mean %.3f\n",
               (double)lo, (double)hi, (double)(sum / (n ? n : 1)));
        printf("    (ambient alone is %.2f, so a vertex at 0 is lit by the key\n", 0.45);
        printf("     light and nothing else -- flat across its whole face,\n");
        printf("      which is every vertex in every shipped level now)\n");
    }

    /* WHICH OF THE THREE REFUSALS DOES IT -- range, facing, shadow -- asked
       of every vertex against every lamp. The bake no longer runs this loop;
       this file still does, because the answer is what says whether a lamp is
       worth a bake at all, and it is the number that moved them out of one.
       Reproduced here rather than instrumented in level.c for the reason it
       always was: a counter in the bake is a cost the game pays forever to
       answer a question asked once. */
    {
        const Level *L2 = &W.level;
        long far_ = 0, away = 0, shadow = 0, lit2 = 0;
        float rmin = 1e9f, rmax = -1e9f;
        for (int k = 0; k < L2->n_lights; k++) {
            float r = L2->lights[k].radius * 0.01f;
            if (r < rmin) rmin = r;
            if (r > rmax) rmax = r;
        }
        static MeshBuf mb2;
        static int made2;
        if (!made2) { mb_init(&mb2, 200000); made2 = 1; }
        mb_reset(&mb2);
        level_geometry(&mb2, L2, 0, 0);

        for (int i = 0; i < mb2.count; i++) {
            const Vtx *v = &mb2.v[i];
            v3 p = v3f(v->px, v->py, v->pz), nn = v3f(v->nx, v->ny, v->nz);
            v3 from = v3add(p, v3scale(nn, 0.05f));
            for (int k = 0; k < L2->n_lights; k++) {
                const Light *lt = &L2->lights[k];
                v3 lp = v3f(lt->x * 0.01f, lt->y * 0.01f, lt->z * 0.01f);
                v3 d = v3sub(lp, from);
                float dist = v3len(d), rad = lt->radius * 0.01f;
                if (rad <= 0.0f || dist > rad) { far_++; continue; }
                v3 dir = v3scale(d, 1.0f / (dist > 0.001f ? dist : 0.001f));
                if (v3dot(nn, dir) <= 0.0f) { away++; continue; }
                if (level_blocked(L2, from, dir, dist)) { shadow++; continue; }
                lit2++;
            }
        }
        long tot = far_ + away + shadow + lit2;
        printf("\n  every vertex against every one of the %d lights (%ld pairs):\n",
               L2->n_lights, tot);
        /* A level with no lamps leaves rmin/rmax at their sentinels, and
           printing 1000000000.0 .. -1000000000.0 as a radius range reads as a
           bug in the probe rather than as an empty set. Every shipped level is
           that level now.
           등이 없는 레벨은 rmin/rmax를 초기 감시값 그대로 남기며, 반경 범위로
           1000000000.0 .. -1000000000.0을 찍는 것은 빈 집합이 아니라 프로브의 결함으로
           읽힙니다. 이제 출하되는 모든 레벨이 그런 레벨입니다. */
        if (L2->n_lights < 1) {
            printf("    (the level declares none, so there are no pairs)\n");
        } else {
        printf("    out of range   %9ld  (%.1f%%)   radii %.1f .. %.1f m\n",
               far_, 100.0 * far_ / (tot ? tot : 1), (double)rmin, (double)rmax);
        printf("    facing away    %9ld  (%.1f%%)\n", away, 100.0 * away / (tot ? tot : 1));
        printf("    shadowed       %9ld  (%.1f%%)\n", shadow, 100.0 * shadow / (tot ? tot : 1));
        printf("    LIT            %9ld  (%.1f%%)\n", lit2, 100.0 * lit2 / (tot ? tot : 1));
        }
    }

    /* THE ONE CLAIM THIS FILE MAKES. Everything above is a report.
       이 파일이 하는 유일한 주장입니다. 위의 모든 것은 보고입니다. */
    {
        Level *l = &W.level;

        /* THE SUN THIS ARENA USED TO DECLARE, PUT BACK FOR THE CHECK. Its
           worldspawn carried these three keys until the sky lighting came out
           of it, and no map declares a sun now -- so the branch below would
           take the "nothing to reach" path on every run and this file would
           report `ok` while asserting nothing at all. That is a worse state
           than a red test: a suite that passes because its subject is missing
           says the subject is fine.

           Written here rather than restored to the .map because the map was
           changed on purpose. The numbers are `lqdm1`'s own, so what the walk
           is measured against is the case it was written for.

           *이 아레나가 선언하던 태양을 검사를 위해 되돌려 놓습니다.* 이 맵의 worldspawn은
           하늘 조명이 빠지기 전까지 이 세 키를 지니고 있었고, 이제 어떤 맵도 태양을 선언하지
           않습니다. 그래서 아래의 분기는 매 실행마다 "닿을 것이 없음" 경로를 타고, 이 파일은
           아무것도 단언하지 않으면서 `ok`를 보고하게 됩니다. 그것은 빨간 테스트보다 나쁜
           상태입니다. 대상이 없어서 통과하는 스위트는 대상이 멀쩡하다고 말합니다.

           .map에 되돌리지 않고 이곳에 적는 이유는 맵이 의도적으로 바뀌었기 때문입니다. 수치는
           `lqdm1` 자신의 것이므로, 걸음이 측정되는 대상은 그것이 쓰이게 된 바로 그 경우입니다. */
        if (l->sun_power <= 0 && l->sky_power <= 0) {
            printf("\n  the map declares no sun; putting its old one back for"
                   " the check below\n");
            /* _sun_mangle "136 -73 0" through brush_sun_of's own conversion:
               yaw 136 deg, pitch -73 deg, Quake axes to this engine's, negated
               because the file names the direction light TRAVELS.
               brush_sun_of 자신의 변환을 거친 _sun_mangle "136 -73 0"입니다. yaw 136도,
               pitch -73도, Quake 축에서 이 엔진의 축으로, 그리고 파일이 빛이 *진행하는*
               방향을 적으므로 부호를 뒤집습니다. */
            float yaw = 136.0f * 0.01745329f, pitch = -73.0f * 0.01745329f;
            float cp = cosf(pitch);
            float qx = cosf(yaw) * cp, qy = sinf(yaw) * cp, qz = sinf(pitch);
            float ex = qx, ey = qz, ez = -qy;
            float len = sqrtf(ex * ex + ey * ey + ez * ez);
            l->sun[0] = -ex / len;
            l->sun[1] = -ey / len;
            l->sun[2] = -ez / len;
            l->sun_power = 120;
            l->sky_power = 50;
            level_light_cache_reset();
        }

        printf("\n  sun: power %d  sky %d  dir (%.3f %.3f %.3f)\n",
               l->sun_power, l->sky_power,
               (double)l->sun[0], (double)l->sun[1], (double)l->sun[2]);

        if (l->sun_power <= 0) {
            printf("    no sun declared -- nothing to reach\n");
        } else {
            static MeshBuf mb3;
            static int made3;
            if (!made3) { mb_init(&mb3, 200000); made3 = 1; }
            mb_reset(&mb3);
            level_geometry(&mb3, l, 0, 0);

            v3 sd = v3f(l->sun[0], l->sun[1], l->sun[2]);
            long away = 0, blocked = 0, lit = 0;


            for (int i = 0; i < mb3.count; i++) {
                const Vtx *v = &mb3.v[i];
                v3 nn = v3f(v->nx, v->ny, v->nz);
                if (v3dot(nn, sd) <= 0.0f) { away++; continue; }
                v3 from = v3add(v3f(v->px, v->py, v->pz), v3scale(nn, 0.05f));
                if (level_sun_reaches(l, from)) lit++; else blocked++;
            }

            long facing = blocked + lit, tot = away + facing;
            printf("    facing away %ld (%.0f%%)  BLOCKED %ld (%.0f%%)  sunlit %ld (%.0f%%)\n",
                   away, 100.0 * away / (tot ? tot : 1),
                   blocked, 100.0 * blocked / (tot ? tot : 1),
                   lit, 100.0 * lit / (tot ? tot : 1));
            printf("\n");

            /* ONE NUMBER, AND WHY IT IS A RATIO RATHER THAN A LANDMARK.
               The first version of this test also asserted that the highest
               upward-facing vertex in the level is sunlit, on the reasoning
               that nothing is above it so nothing can shadow it. That reasoning
               is right and the check is useless: that vertex is ABOVE the
               skybox, its ray hits nothing on the way out, and it stays lit
               however badly the walk through sky is broken. Both mutations
               below left it green. A landmark chosen for being unobstructed
               cannot test the code that handles obstruction.
               So: what share of the surfaces that face the sun does the sun
               reach. Measured on this arena, the walk as it ships says 25.6%.
               Stepping past the face instead of the brush says 1.67%. Treating
               sky as opaque, which is where this started, says 1.39%. The bar
               is 5% -- an eighth of the working value, three times the broken
               ones, sitting in a gap fifteen times wide rather than on a hair.
               *하나의 숫자, 그리고 그것이 지형지물이 아니라 비율인 이유.* 이 테스트의 첫 판은
               레벨에서 가장 높은 위를 향한 정점이 햇빛을 받는지도 주장했습니다. 그 위에는
               아무것도 없으니 그림자도 없다는 논리였습니다. 논리는 옳고 검사는 쓸모없습니다.
               그 정점은 스카이박스 *위*에 있어 광선이 아무것도 만나지 않으며, 하늘을 지나는
               걸음이 아무리 망가져도 계속 밝습니다. 아래의 두 변이 모두 그것을 통과시켰습니다.
               가려지지 않았다는 이유로 고른 지형지물은 가림을 다루는 코드를 시험할 수 없습니다.
               그래서 태양을 향한 표면 중 태양이 실제로 닿는 비율입니다. 출하되는 걸음은 25.6%,
               브러시가 아니라 면을 지나가면 1.67%, 하늘을 불투명하게 두면 1.39%입니다.
               기준은 5%이며 15배 넓은 틈 안에 있습니다. */
            okf(facing > 0 && lit * 20 >= facing,
                "the sun reaches a twentieth of what faces it, at least",
                100.0f * lit / (float)(facing ? facing : 1), 5.0f);
        }
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nthe sun arrives\n", fails);
    return fails != 0;
}
