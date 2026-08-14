/* tracetest -- swept box collision against brush planes.
 *
 * WRITTEN BEFORE THE TRACE IT TESTS, on purpose. The whole test suite stands on
 * the sector model: movetest, steptest, doortest and hooktest all assume a
 * point on the plan has one floor, and every one of them goes out with Sector.
 * Replacing collision with that safety net already gone would be replacing it
 * blind, so this file is the net for the new one and it exists first.
 *
 * WHAT IT ASSERTS is the brush version of what movetest asserts, one layer
 * down. movetest steps a Player and checks where they end up; this checks the
 * primitive underneath -- because the primitive is what step 2 delivers and the
 * Player does not move on brushes until step 3.
 *
 *   standing on a floor      a downward trace stops at its surface
 *   blocked by a wall        a forward trace stops at its face, normal outward
 *   a step in reach          blocked at foot level, clear one step up
 *   a ledge out of reach     blocked at foot level AND one step up
 *   A SLOPE                  a downward trace lands part way up it, with a
 *                            normal that is neither flat nor vertical
 *
 * That last one is the reason for all of this. A sector has one floor height
 * per point on the plan, so the ramp in the fixture below could not be built at
 * all -- there is no sector-model assertion for this test to be the brush
 * version of.
 *
 * 시험하는 트레이스보다 먼저 작성했으며 의도적입니다. 테스트 묶음 전체가 섹터 모델 위에 서
 * 있습니다. movetest, steptest, doortest, hooktest 모두 평면상의 한 점에 바닥이 하나라고
 * 가정하며, 그 전부가 Sector와 함께 사라집니다. 그 안전망이 이미 없어진 상태에서 충돌을
 * 교체하는 것은 눈을 감고 교체하는 일이므로, 이 파일이 새것을 위한 그물이고 먼저 존재합니다.
 *
 * 마지막 항목이 이 모든 것의 이유입니다. 섹터는 평면상의 한 점에 바닥 높이가 하나뿐이므로 아래
 * 픽스처의 경사로는 아예 만들 수 없습니다. 이 시험이 브러시판으로 옮겨 올 섹터 모델 쪽 단언이
 * 존재하지 않습니다.
 */

#include <stdio.h>
#include <math.h>
#include "brush.h"
#include "data.h"
#include "player.h"
#include "render.h"
#include "model.h"
#include "txt.h"

static int fails;

static void check(int ok, const char *what) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static void checkf(float got, float want, float tol, const char *what) {
    int ok = fabsf(got - want) <= tol;
    printf("  %-56s got %9.4f  want %9.4f  %s\n",
           what, got, want, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

/* --- the fixture ---------------------------------------------------------
 *
 * Built here rather than loaded, for the reason movetest gives for building its
 * own room: a test that names coordinates in a level somebody edits is a test
 * that breaks when they edit it, and that teaches you to ignore the suite.
 *
 * Written in MAP UNITS, with the metres in the comment, because that is how an
 * author sees it and because the conversion is one of the things under test.
 * At 1/32 m per unit, 32 units is a metre.
 *
 *    floor   the whole plan, top at 0
 *    wall    a slab from x 4m to 5m, standing across the middle
 *    step    0.50m tall -- inside PLAYER_STEP
 *    ledge   0.75m tall -- outside it
 *    ramp    climbs 0 to 2m as engine z falls from 8m to 4m
 *    clip    a brush textured `clip`: drawn by nothing, solid to everything
 */
static BrushMap M;
static char MAPTEXT[16000];

static int emit_pt(char *o, int cap, int pos, int x, int y, int z) {
    pos = txt_append_str(o, cap, pos, "( ");
    pos = txt_append_int(o, cap, pos, x); pos = txt_append_str(o, cap, pos, " ");
    pos = txt_append_int(o, cap, pos, y); pos = txt_append_str(o, cap, pos, " ");
    pos = txt_append_int(o, cap, pos, z);
    return txt_append_str(o, cap, pos, " ) ");
}

static int emit_face(char *o, int cap, int pos, const char *tex,
                     int ax, int ay, int az, int bx, int by, int bz,
                     int cx, int cy, int cz) {
    pos = emit_pt(o, cap, pos, ax, ay, az);
    pos = emit_pt(o, cap, pos, bx, by, bz);
    pos = emit_pt(o, cap, pos, cx, cy, cz);
    pos = txt_append_str(o, cap, pos, tex);
    return txt_append_str(o, cap, pos,
                          " [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n");
}

/* An axis-aligned box, in map units. The six point triples are the same ones
   maptest derives and checks, so a winding mistake here would have been caught
   there first. */
static int emit_box(char *o, int cap, int pos, const char *tex,
                    int x0, int y0, int z0, int x1, int y1, int z1) {
    pos = txt_append_str(o, cap, pos, "{\n");
    pos = emit_face(o, cap, pos, tex, x0,y0,z1, x0,y1,z1, x1,y1,z1);
    pos = emit_face(o, cap, pos, tex, x0,y0,z0, x1,y0,z0, x1,y1,z0);
    pos = emit_face(o, cap, pos, tex, x0,y1,z1, x0,y0,z1, x0,y0,z0);
    pos = emit_face(o, cap, pos, tex, x1,y0,z1, x1,y1,z1, x1,y1,z0);
    pos = emit_face(o, cap, pos, tex, x1,y0,z0, x0,y0,z0, x0,y0,z1);
    pos = emit_face(o, cap, pos, tex, x0,y1,z0, x1,y1,z0, x1,y1,z1);
    return txt_append_str(o, cap, pos, "}\n");
}

static void build(void) {
    const int cap = (int)sizeof(MAPTEXT);
    int pos = txt_append_str(MAPTEXT, cap, 0,
                             "{\n\"classname\" \"worldspawn\"\n");

    pos = emit_box(MAPTEXT, cap, pos, "t", -512, -512, -32,  512,  512,   0);
    pos = emit_box(MAPTEXT, cap, pos, "t",  128, -128,   0,  160,  128, 192);
    pos = emit_box(MAPTEXT, cap, pos, "t", -256, -128,   0, -128,  128,  16);
    pos = emit_box(MAPTEXT, cap, pos, "t", -512, -128,   0, -384,  128,  24);
    pos = emit_box(MAPTEXT, cap, pos, "clip", 320, 320, 0, 384, 384, 96);

    /* The ramp: five planes, because the low end has no height and a brush
       needs no face where it has no face. The sloped plane passes through
       (x, -256, 0) and (x, -128, 64), so 2z - y - 256 = 0 there. */
    pos = txt_append_str(MAPTEXT, cap, pos, "{\n");
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    384, -128, 64,  384, -256, 0,  256, -256, 0);   /* slope  */
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    256, -256, 0,   384, -256, 0,  384, -128, 0);   /* bottom */
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    256, -128, 64,  256, -256, 64, 256, -256, 0);   /* -x     */
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    384, -256, 64,  384, -128, 64, 384, -128, 0);   /* +x     */
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    256, -128, 0,   384, -128, 0,  384, -128, 64);  /* high end */
    pos = txt_append_str(MAPTEXT, cap, pos, "}\n");

    /* A SECOND RAMP, TOO STEEP TO STAND ON: 4m of rise over 2m, which is 63
       degrees and an up-normal of 0.447 -- below BRUSH_GROUND_NORMAL either
       way it is measured. Without it every slope in the fixture is walkable
       and the constant that decides which are would never be exercised.
       Uphill is -z here as well, so the two ramps differ in steepness and in
       nothing else. */
    pos = txt_append_str(MAPTEXT, cap, pos, "{\n");
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    384, 192, 128,  384, 128, 0,  256, 128, 0);    /* slope  */
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    256, 128, 0,    384, 128, 0,  384, 192, 0);    /* bottom */
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    256, 192, 128,  256, 128, 128, 256, 128, 0);   /* -x     */
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    384, 128, 128,  384, 192, 128, 384, 192, 0);   /* +x     */
    pos = emit_face(MAPTEXT, cap, pos, "t",
                    256, 192, 0,    384, 192, 0,  384, 192, 128);  /* high end */
    pos = txt_append_str(MAPTEXT, cap, pos, "}\n");

    pos = txt_append_str(MAPTEXT, cap, pos, "}\n");
    if (pos >= cap - 1) { printf("  FIXTURE TOO BIG FOR ITS BUFFER\n"); fails++; }
}

/* The player, as the box the trace sweeps. PLAYER_RADIUS and PLAYER_EYE rather
   than numbers, so retuning the player retunes the test with it -- and so the
   clearance these assertions describe is the clearance the game gives.
   The trace point is the FEET: mins.y is 0 and maxs.y is the standing height,
   which is what makes "a downward trace stops at the floor" put the feet on
   the floor rather than the navel. */
#define R PLAYER_RADIUS
static const v3 MINS = { -R, 0.0f, -R };
static const v3 MAXS = {  R, PLAYER_EYE, R };

static BrushTrace tr(v3 from, v3 to) {
    BrushTrace t;
    brush_trace(&M, 0, M.n_brushes, from, to, MINS, MAXS, &t);
    return t;
}

/* A zero-length trace answers "is the box overlapping anything here", which is
   what start_solid reports. No separate entry point for it: two ways to ask one
   question are two answers to keep in agreement. */
static int solid_at(v3 p) { return tr(p, p).start_solid; }

/* --- floors --------------------------------------------------------------- */

static void test_floor(void) {
    printf("\nstanding on a floor\n");

    BrushTrace t = tr(v3f(0, 3.0f, 0), v3f(0, -1.0f, 0));
    check(t.hit, "a downward trace hits");
    checkf(t.end.y, 0.0f, 0.01f, "and stops with the feet on the surface");
    checkf(t.normal.y, 1.0f, 0.001f, "with an upward normal");
    check(!t.start_solid, "having started in open air");
    check(!solid_at(t.end), "and the box is not left inside the floor");

    /* The replacement for level_ground: a downward trace IS the floor query,
       and it answers for the whole box rather than five sampled points. */
    t = tr(v3f(-6.0f, 3.0f, 0), v3f(-6.0f, -1.0f, 0));
    checkf(t.end.y, 0.5f, 0.01f, "over the step, it stops on the step");

    t = tr(v3f(-14.0f, 3.0f, 0), v3f(-14.0f, -1.0f, 0));
    checkf(t.end.y, 0.75f, 0.01f, "over the ledge, it stops on the ledge");

    /* Nothing underneath at all. The trace runs to its end and reports no hit,
       which is the honest answer -- level_ground returned 0 for "outside the
       map" and callers had to know that meant "do not move there". */
    t = tr(v3f(0, 3.0f, 40.0f), v3f(0, -1.0f, 40.0f));
    check(!t.hit, "past the edge of the floor, nothing is hit");
    checkf(t.t, 1.0f, 0.001f, "and the sweep completes");

    /* --- resting, which is the case that never stops happening -------------
       A box CLOSER to the surface than ::BRUSH_SKIN. The entry fraction comes
       out negative there -- `(d0 - BRUSH_SKIN)` is below zero -- and a trace
       that reads negative as "no contact" drops the box straight through the
       world. Clamping it to zero is what makes already-touching mean contact
       now.
       Worth its own assertion because nothing else here reaches that branch:
       every trace above lands the box at exactly a skin's distance, where the
       fraction is exactly zero and both readings agree. Drift, a moving floor
       and a hand-placed spawn all put it inside the skin instead.
       ::BRUSH_SKIN보다 표면에 *더 가까운* 상자입니다. 그곳에서는 진입 비율이 음수로
       나오며(`(d0 - BRUSH_SKIN)`이 0 아래입니다) 음수를 "접촉 없음"으로 읽는 트레이스는
       상자를 세계 밖으로 떨어뜨립니다. 0으로 제한하는 것이 "이미 닿아 있음"을 지금 접촉으로
       만듭니다. 별도의 단언이 필요한 이유는 이곳의 다른 무엇도 그 분기에 닿지 않기
       때문입니다. 위의 모든 트레이스는 상자를 정확히 스킨 거리에 놓고, 그곳에서 비율은 정확히
       0이며 두 해석이 일치합니다. 밀림, 움직이는 바닥, 손으로 놓은 스폰은 모두 그것을 스킨
       *안쪽*에 놓습니다. */
    t = tr(v3f(0, BRUSH_SKIN * 0.4f, 0), v3f(0, -0.5f, 0));
    check(t.hit, "a box nearer than the skin still contacts the floor");
    check(t.end.y >= -0.001f, "and is not dropped through it");

    /* And it stays put. A hundred downward sweeps from wherever the last one
       left it, which is what standing still actually is once gravity is
       pulling every frame. A trace that drifts by a skin per frame walks the
       player into the floor at 30cm a second.
       그리고 그 자리에 머뭅니다. 직전 트레이스가 남긴 자리에서 하강 스윕 100번이며, 중력이
       매 프레임 당기는 상황에서 가만히 서 있다는 것이 실제로 그것입니다. 프레임마다 스킨만큼
       밀리는 트레이스는 플레이어를 초당 30cm로 바닥에 밀어 넣습니다. */
    v3 rest = tr(v3f(0, 3.0f, 0), v3f(0, -1.0f, 0)).end;
    for (int i = 0; i < 100; i++)
        rest = tr(rest, v3f(rest.x, rest.y - 0.05f, rest.z)).end;
    checkf(rest.y, BRUSH_SKIN, 0.002f, "100 sweeps later it has not drifted");
}

/* --- walls ---------------------------------------------------------------- */

static void test_wall(void) {
    printf("\nblocked by a wall\n");

    /* Stopping short of x = 7, which is west of the ramp. Running the full way
       to x = 10 was the first version and it passed for the wrong reason: the
       ramp starts at x = 8, so "blocked" was true whether the wall worked or
       not. A test that cannot fail for the reason it names is not a test.
       x = 7에서 멈추며 이는 램프의 서쪽입니다. 첫 판본은 x = 10까지 갔고 잘못된 이유로
       통과했습니다. 램프가 x = 8에서 시작하므로 벽이 동작하든 아니든 "막힘"이 참이었습니다.
       자기가 지목한 이유로 실패할 수 없는 시험은 시험이 아닙니다. */
    const float REACH = 7.0f;

    /* The wall's west face is at x = 4m. A box of half-width R stops with its
       side against it, so the centre stops at 4 - R. */
    BrushTrace t = tr(v3f(0, 0.01f, 0), v3f(REACH, 0.01f, 0));
    check(t.hit, "a forward trace into it hits");
    checkf(t.end.x, 4.0f - R, 0.02f, "stopping a box-width short of the face");
    checkf(t.normal.x, -1.0f, 0.001f, "with the face's outward normal");
    check(!solid_at(t.end), "and the box is not left inside the wall");

    /* The same trace, off the end of the wall by less than a box-width. The
       CENTRE misses; the box does not. A point trace would walk straight
       through the corner, which is the bug five sampled points existed to
       paper over -- and papering was all they did, because a circle sampled at
       five points still has gaps between them. */
    t = tr(v3f(0, 0.01f, 4.2f), v3f(REACH, 0.01f, 4.2f));
    check(t.hit, "a box whose centre misses the corner is still blocked");
    checkf(t.end.x, 4.0f - R, 0.02f, "at the same face, by its corner");

    t = tr(v3f(0, 0.01f, 4.5f), v3f(REACH, 0.01f, 4.5f));
    check(!t.hit, "and one that clears it by a box-width is not");
    checkf(t.end.x, REACH, 0.001f, "completing the whole sweep");

    /* Above the wall there is nothing to hit. */
    t = tr(v3f(0, 7.0f, 0), v3f(REACH, 7.0f, 0));
    check(!t.hit, "over the top of it, the sweep is clear");
}

/* --- steps ---------------------------------------------------------------- */

static void test_step(void) {
    printf("\na step in reach, and a ledge out of it\n");

    check(0.5f < PLAYER_STEP && 0.75f > PLAYER_STEP,
          "fixture: the step is climbable and the ledge is not");

    /* Walking west at floor level runs into the step. */
    BrushTrace t = tr(v3f(-2.0f, 0.01f, 0), v3f(-10.0f, 0.01f, 0));
    check(t.hit, "at foot level the step blocks");
    checkf(t.end.x, -4.0f + R, 0.02f, "at its east face");

    /* Lifted by one step height, the same move is clear over the step and is
       stopped by the ledge instead. Those two facts are the whole of what a
       step-up needs to know.
       The target is x = -14, PAST the ledge's east face at -12. The first
       version stopped at -10 and reported no hit, which was correct: it never
       reached the ledge. An assertion about a thing the sweep does not reach
       says nothing about that thing.
       목표는 x = -14이며 -12에 있는 턱의 동쪽 면을 *지나갑니다*. 첫 판본은 -10에서 멈추고
       충돌 없음을 보고했는데 그것이 옳았습니다. 턱에 닿은 적이 없었습니다. 스윕이 닿지도
       않는 대상에 대한 단언은 그 대상에 관해 아무것도 말하지 않습니다. */
    t = tr(v3f(-2.0f, PLAYER_STEP, 0), v3f(-14.0f, PLAYER_STEP, 0));
    check(t.hit, "one step up, the move goes further and then stops");
    checkf(t.end.x, -12.0f + R, 0.02f, "at the ledge, not at the step");

    /* And from up there, dropping lands on top of the step. */
    t = tr(v3f(-6.0f, PLAYER_STEP, 0), v3f(-6.0f, -1.0f, 0));
    checkf(t.end.y, 0.5f, 0.01f, "and dropping puts the feet on the step top");
}

/* --- the slope ------------------------------------------------------------ */

static void test_slope(void) {
    printf("\na slope, which no sector could hold\n");

    /* The ramp's surface, as a function of engine z: it climbs 2m as z falls
       from 8 to 4, so the height at any z is 4 - z/2. */
    #define RAMP_AT(z) (4.0f - (z) * 0.5f)

    /* A BOX RESTS ON ITS UPHILL EDGE, not on its centre. The first version of
       this test wanted RAMP_AT(6) = 1.0 and got 1.18, which is not the trace
       being wrong -- it is the box being a box. The uphill side reaches the
       surface first and stops the sweep there, exactly as a real crate on a
       ramp sits high on the slope and rocks on that edge.
       Asserting the centre height instead would have been asserting the answer
       a POINT trace gives, which is the thing being replaced.
       상자는 중심이 아니라 자기 *오르막 쪽 모서리*로 놓입니다. 이 시험의 첫 판본은
       RAMP_AT(6) = 1.0을 기대하고 1.18을 얻었는데, 그것은 트레이스가 틀린 것이 아니라
       상자가 상자인 것입니다. 오르막 쪽이 표면에 먼저 닿아 그곳에서 스윕을 멈춥니다. 경사로
       위의 실제 상자가 비탈 위쪽에 걸터앉아 그 모서리로 흔들리는 것과 똑같습니다.
       중심 높이를 단언했다면 그것은 *점* 트레이스가 주는 답을 단언하는 것이었고, 그것이
       바로 교체 대상입니다. */
    BrushTrace t = tr(v3f(10.0f, 3.0f, 6.0f), v3f(10.0f, -1.0f, 6.0f));
    check(t.hit, "a downward trace hits the ramp");
    checkf(t.end.y, RAMP_AT(6.0f - R), 0.02f, "resting on its uphill edge");
    check(t.end.y > RAMP_AT(6.0f) + 0.1f,
          "which is measurably higher than the centre's own height");

    /* Its normal leans: 2/sqrt(5) up and 1/sqrt(5) along +z. A flat floor and a
       vertical wall are both easy to get right by accident; this is not. */
    checkf(t.normal.y, 0.894427f, 0.002f, "with a normal that leans (y)");
    checkf(t.normal.z, 0.447214f, 0.002f, "and leans the right way (z)");
    check(!solid_at(t.end), "and the box is not left inside the ramp");

    /* Two more points along it, to show the height actually tracks position
       rather than being one value the whole way. */
    t = tr(v3f(10.0f, 3.0f, 7.0f), v3f(10.0f, -1.0f, 7.0f));
    checkf(t.end.y, RAMP_AT(7.0f - R), 0.02f, "lower down the ramp, a lower floor");
    t = tr(v3f(10.0f, 3.0f, 5.0f), v3f(10.0f, -1.0f, 5.0f));
    checkf(t.end.y, RAMP_AT(5.0f - R), 0.02f, "higher up the ramp, a higher floor");
}

/* --- the awkward cases ---------------------------------------------------- */

static void test_edges(void) {
    printf("\nstarting inside, moving nowhere, and moving too far\n");

    /* Inside the wall. Reported rather than resolved: a trace that quietly
       pretended an embedded box was in open air would let a door close through
       the player and leave them outside the level. */
    BrushTrace t = tr(v3f(4.5f, 1.0f, 0), v3f(9.0f, 1.0f, 0));
    check(t.start_solid, "a trace beginning inside a brush says so");
    checkf(t.t, 0.0f, 0.001f, "and does not travel");

    /* A move of zero length is not a hit. */
    t = tr(v3f(0, 1.0f, 0), v3f(0, 1.0f, 0));
    check(!t.hit && !t.start_solid, "a zero-length trace in open air is clear");
    checkf(t.t, 1.0f, 0.001f, "and completes");

    /* NO TUNNELLING. A sweep is a sweep, not a series of samples: crossing the
       1m-thick wall in one 40m step must stop at it, exactly as ten 4m steps
       would. ::level_trace sampled every 5cm and this is what replaces that.
       Starting at x = 0 rather than x = -20: from there the first thing in the
       way is the LEDGE, not the wall, and the first version of this asserted
       the wall's face while measuring the ledge's. Both numbers were right;
       the test was pointing at the wrong one.
       x = -20이 아니라 x = 0에서 시작합니다. 그곳에서 앞을 막는 첫 번째 것은 벽이 아니라
       *턱*이며, 이 시험의 첫 판본은 턱의 면을 재면서 벽의 면을 단언했습니다. 두 숫자 모두
       옳았고, 시험이 엉뚱한 쪽을 가리키고 있었습니다. */
    BrushTrace one = tr(v3f(0, 0.01f, 0), v3f(40.0f, 0.01f, 0));
    check(one.hit, "a 40m step across a 1m wall still hits it");
    checkf(one.end.x, 4.0f - R, 0.02f, "at the same face a short step finds");

    v3 p = v3f(0, 0.01f, 0);
    for (int i = 0; i < 10; i++) {
        BrushTrace s = tr(p, v3f(p.x + 4.0f, p.y, p.z));
        p = s.end;
        if (s.hit) break;
    }
    checkf(p.x, one.end.x, 0.02f, "and ten short steps end in the same place");

    /* A `clip` brush draws nothing and stops everything. Same list
       brush_tex_nodraw answers from, so the two cannot disagree. */
    check(brush_tex_nodraw("clip"), "clip is a nodraw texture");
    t = tr(v3f(11.0f, 0.01f, -11.0f), v3f(11.0f, 0.01f, -1.0f));
    check(t.hit, "and a clip brush is still solid to a trace");
}

/* --- moving, rather than just measuring ----------------------------------
 *
 * brush_trace answers "what is in the way". brush_slide_move decides what to do
 * about it, and those are different questions with different right answers: a
 * trace that stops at a wall is correct, and a mover that stops dead at a wall
 * you walked into at a glancing angle is not.
 *
 * This is the layer player.c's move_axis occupies today. move_axis produces
 * sliding by moving x and z in separate calls, which works for a wall square to
 * an axis and does not for anything else -- a diagonal wall stops one axis
 * fully and lets the other through, so you slide along the wrong direction at
 * the wrong speed. Clipping the velocity into the plane that was actually hit
 * is the thing that generalises, and it is also what makes a ramp walkable.
 *
 * brush_trace는 "무엇이 앞을 막는가"에 답합니다. brush_slide_move는 그것에 대해 무엇을 할지
 * 정하며, 둘은 정답이 다른 서로 다른 질문입니다. 벽에서 멈추는 트레이스는 옳지만, 비스듬히
 * 스치듯 부딪힌 벽에서 죽은 듯 멈추는 이동은 옳지 않습니다.
 *
 * 이것은 오늘 player.c의 move_axis가 차지한 층입니다. move_axis는 x와 z를 별도 호출로
 * 움직여 미끄러짐을 만드는데, 축에 반듯한 벽에서는 동작하고 그 밖의 무엇에서도 동작하지
 * 않습니다. 대각선 벽은 한 축을 완전히 막고 다른 축을 통과시키므로, 엉뚱한 방향으로 엉뚱한
 * 속도로 미끄러집니다. 실제로 부딪힌 평면에 속도를 투영하는 것이 일반화되는 방법이며, 그것이
 * 경사로를 걸을 수 있게 만드는 것이기도 합니다.
 */

static BrushMove mover(v3 pos, v3 vel, float step) {
    BrushMove mv;
    mv.pos = pos;
    mv.vel = vel;
    mv.mins = MINS;
    mv.maxs = MAXS;
    mv.step_height = step;
    mv.grounded = 0;
    mv.ground_normal = v3f(0, 0, 0);
    mv.blocked = 0;
    return mv;
}

static void slide(BrushMove *mv, float dt) {
    brush_slide_move(&M, 0, M.n_brushes, mv, dt);
}

/* One frame of the game's own loop: gravity, then the move. The order is
   player.c's and the constant is player.h's, so what these tests describe is
   what the player will do rather than what a test harness does. */
#define DT (1.0f / 60.0f)
static void frames(BrushMove *mv, v3 wish, int n) {
    for (int i = 0; i < n; i++) {
        mv->vel.y -= PLAYER_GRAVITY * DT;
        mv->vel.x = wish.x;
        mv->vel.z = wish.z;
        slide(mv, DT);
    }
}

static void test_slide(void) {
    printf("\nsliding, rather than stopping dead\n");

    /* Nothing in the way: the whole move happens. */
    BrushMove mv = mover(v3f(0, BRUSH_SKIN, 0), v3f(5.0f, 0, 0), PLAYER_STEP);
    slide(&mv, 0.1f);
    checkf(mv.pos.x, 0.5f, 0.01f, "an unobstructed move travels its full length");
    check(mv.grounded, "and stays grounded on the floor");

    /* Straight into the wall. Stops at its face and the velocity into it is
       gone -- a mover that kept it would push into the wall every frame and
       accumulate a speed that comes out all at once when the wall ends. */
    mv = mover(v3f(0, BRUSH_SKIN, 0), v3f(50.0f, 0, 0), PLAYER_STEP);
    slide(&mv, 0.2f);
    checkf(mv.pos.x, 4.0f - R, 0.02f, "head-on into a wall stops at its face");
    /* NOT exactly zero, and it should not be. The overclip leaves the velocity
       leaning a hair AWAY from the surface rather than perfectly parallel to
       it, which is what stops the next bump re-contacting the same plane. What
       matters is the sign: nothing is still heading into the wall.
       정확히 0이 아니며 그래야 합니다. 오버클립은 속도를 표면과 완벽히 평행이 아니라
       표면에서 아주 살짝 *멀어지는* 쪽으로 남기고, 그것이 다음 충돌에서 같은 평면에 다시
       닿는 것을 막습니다. 중요한 것은 부호입니다. 무엇도 여전히 벽을 향하고 있지 않습니다. */
    check(mv.vel.x <= 0.0f && mv.vel.x > -0.2f,
          "with the velocity into it removed, leaning very slightly out");
    check(!solid_at(mv.pos), "and the box is not inside the wall");

    /* At an angle. The component along the wall survives, so the box keeps
       moving -- this is the case move_axis gets wrong for anything that is not
       square to an axis. */
    mv = mover(v3f(0, BRUSH_SKIN, 0), v3f(10.0f, 0, 3.0f), PLAYER_STEP);
    slide(&mv, 1.0f);
    checkf(mv.pos.x, 4.0f - R, 0.02f, "a glancing move still stops at the face");
    check(mv.pos.z > 2.5f, "but carries on along the wall");
    checkf(mv.vel.z, 3.0f, 0.01f, "keeping the velocity that was parallel to it");
    check(!solid_at(mv.pos), "and is not inside the wall");

    /* Wall and floor at once: two planes in one move. Clipping against only the
       last one hit would either drive the box into the floor or stand it up off
       the wall. */
    mv = mover(v3f(0, 1.0f, 0), v3f(10.0f, -5.0f, 0), PLAYER_STEP);
    slide(&mv, 1.0f);
    checkf(mv.pos.x, 4.0f - R, 0.02f, "into a wall while falling: against the wall");
    checkf(mv.pos.y, BRUSH_SKIN, 0.02f, "and resting on the floor");
    check(!solid_at(mv.pos), "and inside neither");
}

static void test_stepping(void) {
    printf("\nclimbing what can be climbed\n");

    /* The 0.50m step, which is inside PLAYER_STEP. Walking west onto it. */
    BrushMove mv = mover(v3f(-2.0f, BRUSH_SKIN, 0), v3f(-5.0f, 0, 0), PLAYER_STEP);
    slide(&mv, 1.0f);
    checkf(mv.pos.y, 0.5f + BRUSH_SKIN, 0.02f, "a step in reach is climbed");
    check(mv.pos.x < -4.0f, "and the move carries on over it");
    check(mv.grounded, "landing grounded on its top");

    /* The 0.75m ledge, which is not. */
    mv = mover(v3f(-10.0f, BRUSH_SKIN, 0), v3f(-5.0f, 0, 0), PLAYER_STEP);
    slide(&mv, 1.0f);
    checkf(mv.pos.x, -12.0f + R, 0.02f, "a ledge out of reach stops the move");
    check(mv.pos.y < 0.1f, "and is not climbed");

    /* NO STEPPING IN MID-AIR. Held 0.40m off the floor -- lower than the ledge
       top, and low enough that a step of PLAYER_STEP would clear it. Nothing is
       underfoot, so nothing is stepped onto: climbing from the air is how a
       player walks up a wall one frame at a time.
       공중에서는 계단을 오르지 않습니다. 바닥에서 0.40m 띄웠고, 턱 윗면보다 낮으며
       PLAYER_STEP만큼의 계단이면 넘길 수 있는 높이입니다. 발밑에 아무것도 없으므로 아무것도
       밟지 않습니다. 공중에서 오르는 것이 플레이어가 프레임마다 한 칸씩 벽을 걸어 올라가는
       방법입니다. */
    mv = mover(v3f(-10.0f, 0.40f, 0), v3f(-5.0f, 0, 0), PLAYER_STEP);
    slide(&mv, 0.5f);   /* 2.5m, which is far enough to reach the ledge at 1.65 */
    check(!mv.grounded, "in mid-air the box is not grounded");
    checkf(mv.pos.x, -12.0f + R, 0.02f, "and the ledge stops it rather than lifting it");
    check(mv.pos.y > 0.39f, "having neither climbed nor fallen");

    /* step_height 0 turns it off entirely, which is what a caller does when it
       does not want the box climbing anything at all. */
    mv = mover(v3f(-2.0f, BRUSH_SKIN, 0), v3f(-5.0f, 0, 0), 0.0f);
    slide(&mv, 1.0f);
    checkf(mv.pos.x, -4.0f + R, 0.02f, "with stepping off, even a low step blocks");
}

static void test_walking_slopes(void) {
    printf("\nwalking up a ramp, and sliding off one that is too steep\n");

    /* Uphill is -z. Start on the floor south of the ramp and walk into it.
       This is the payoff: a sector level could not hold the ramp, and a mover
       that only clipped horizontally would climb it in a series of jolts. */
    /* Forty frames at 4m/s is about 2.7m, which starts on the floor at z = 9,
       crosses onto the ramp at z = 8 and stops well short of its top at z = 4.
       The first version ran ninety and walked off the far end, where the box
       hangs on the top edge -- every assertion here then described a box that
       was no longer on a slope.
       40프레임 × 4m/s는 약 2.7m이며, z = 9의 바닥에서 시작해 z = 8에서 경사로에 올라서고
       z = 4의 꼭대기에는 한참 못 미쳐 멈춥니다. 첫 판본은 90프레임을 돌려 반대편 끝을 걸어
       나갔고, 그곳에서 상자는 윗모서리에 걸칩니다. 그러면 이곳의 모든 단언이 더 이상 경사면
       위에 있지 않은 상자를 기술하게 됩니다. */
    BrushMove mv = mover(v3f(10.0f, BRUSH_SKIN, 9.0f), v3f(0, 0, 0), PLAYER_STEP);
    float worst_drop = 0.0f, prev = mv.pos.y;
    int airborne = 0;
    for (int i = 0; i < 40; i++) {
        frames(&mv, v3f(0, 0, -4.0f), 1);
        if (prev - mv.pos.y > worst_drop) worst_drop = prev - mv.pos.y;
        prev = mv.pos.y;
        if (!mv.grounded) airborne = 1;
    }
    check(mv.pos.z < 7.0f, "the box is well onto the ramp");
    check(mv.pos.y > 0.5f, "and has climbed");
    checkf(mv.pos.y, 4.0f - (mv.pos.z - R) * 0.5f, 0.03f,
           "resting on the surface at the height it reached");
    check(!airborne, "grounded on every frame of the climb");
    check(worst_drop < 0.02f, "climbing smoothly, with no frame that dropped");
    check(!solid_at(mv.pos), "and never inside the ramp");

    /* The 63-degree ramp. Its up-normal is 0.447, under BRUSH_GROUND_NORMAL, so
       it is a surface you are ON and not a surface you STAND on. Dropped onto
       it from clear air with no input, gravity should carry the box down it.
       Starting ABOVE it rather than at the surface height. The first version
       started at y = 3.0, which is the surface height at the box's CENTRE --
       and on a slope this steep the uphill edge is 0.7m higher, so the box
       began embedded and every assertion after it measured a move that never
       happened.
       표면 위가 아니라 그보다 *높은* 곳에서 시작합니다. 첫 판본은 y = 3.0에서 시작했는데
       그것은 상자 *중심*에서의 표면 높이입니다. 이만큼 가파른 경사에서는 오르막 쪽 모서리가
       0.7m 더 높으므로 상자는 박힌 채로 시작했고, 그 뒤의 모든 단언이 일어난 적 없는 이동을
       재고 있었습니다. */
    mv = mover(v3f(10.0f, 5.0f, -5.5f), v3f(0, 0, 0), PLAYER_STEP);
    frames(&mv, v3f(0, 0, 0), 20);
    check(!solid_at(mv.pos), "it lands on the steep face without embedding");
    check(!mv.grounded, "on a 63-degree face the box is not grounded");
    check(mv.ground_normal.y < BRUSH_GROUND_NORMAL,
          "because what is underneath is too steep to stand on");

    float z0 = mv.pos.z, y0 = mv.pos.y;
    frames(&mv, v3f(0, 0, 0), 60);
    check(mv.pos.z > z0 + 0.2f, "and it slides down rather than sticking");
    check(mv.pos.y < y0, "losing height as it goes");
    check(!solid_at(mv.pos), "and stays out of the solid");

    /* Reaching the floor at the bottom, it stops being a slope problem. */
    frames(&mv, v3f(0, 0, 0), 120);
    check(mv.grounded, "and once off it, the floor holds it");
    checkf(mv.pos.y, BRUSH_SKIN, 0.02f, "at floor level");
}

static void test_settling(void) {
    printf("\nstanding still, which is what happens most of the time\n");

    /* Six hundred frames -- ten seconds -- of gravity and no input. A mover
       that drifts a skin per frame walks the player into the floor at 30cm a
       second, and a mover that bounces makes the camera jitter forever. */
    BrushMove mv = mover(v3f(0, 2.0f, 0), v3f(0, 0, 0), PLAYER_STEP);
    frames(&mv, v3f(0, 0, 0), 600);
    checkf(mv.pos.y, BRUSH_SKIN, 0.003f, "ten seconds later, still on the floor");
    check(mv.grounded, "and still grounded");
    checkf(mv.vel.y, 0.0f, 0.5f, "with the fall cancelled rather than accumulating");
    check(!solid_at(mv.pos), "and not sunk into it");

    /* Walking off the step and onto the floor below: falls, lands, stays. */
    mv = mover(v3f(-6.0f, 0.5f + BRUSH_SKIN, 0), v3f(0, 0, 0), PLAYER_STEP);
    frames(&mv, v3f(6.0f, 0, 0), 60);
    check(mv.pos.x > -4.0f, "walking off the step, the box leaves it");
    checkf(mv.pos.y, BRUSH_SKIN, 0.02f, "and settles on the floor below");
    check(mv.grounded, "grounded again");
}

/* --- against the shipped level -------------------------------------------- */

static void test_atrium(void) {
    printf("\nthe shipped level, asked only what any level must satisfy\n");

    int len = 0;
    const char *text = data_map("atrium", &len);
    if (!text) { printf("  no atrium.map to smoke test\n"); return; }

    static BrushMap A;
    if (!brush_parse(text, len, &A)) { printf("  atrium.map did not parse\n"); fails++; return; }

    /* Drop onto the floor from the player start and stay out of the solid. */
    BrushTrace t;
    brush_trace(&A, 0, A.n_brushes, v3f(0, 3.0f, 5.0f), v3f(0, -1.0f, 5.0f),
                MINS, MAXS, &t);
    check(t.hit, "there is a floor under the player start");
    checkf(t.end.y, 0.0f, 0.02f, "and it is at zero");

    /* Walk the room in every direction from the middle and never leave it. A
       trace that ends outside the walls is a wall you can walk through. */
    int escaped = 0, embedded = 0;
    for (int i = 0; i < 64; i++) {
        float a = (float)i * (6.2831853f / 64.0f);
        v3 from = v3f(0, 0.01f, 5.0f);
        v3 to   = v3f(from.x + cosf(a) * 40.0f, from.y, from.z + sinf(a) * 40.0f);
        brush_trace(&A, 0, A.n_brushes, from, to, MINS, MAXS, &t);
        if (fabsf(t.end.x) > 8.5f || fabsf(t.end.z) > 8.5f) escaped = 1;

        BrushTrace z;
        brush_trace(&A, 0, A.n_brushes, t.end, t.end, MINS, MAXS, &z);
        if (z.start_solid) embedded = 1;
    }
    check(!escaped,  "64 traces outward: none left the room");
    check(!embedded, "64 traces outward: none ended inside a wall");

    /* THE PAIR OF HEIGHTS A SECTOR COULD NOT HOLD. The balcony spans engine
       x -8..-2 and z -8..-2, with its top at 3m and its underside at 2.5m, and
       the room's floor runs underneath it at 0. One point on the plan, two
       surfaces to stand on, and the answer depends on where you already are --
       which is the question ::level_ground had no way to be asked.
       The sample point is (-5, -5). The first version used z = +5, which is not
       under the balcony at all: it reported the floor at zero, correctly, and
       proved nothing.
       섹터가 담을 수 없던 높이 한 쌍입니다. 발코니는 엔진 x -8..-2, z -8..-2를 차지하고
       윗면이 3m, 아랫면이 2.5m이며, 방의 바닥이 그 아래 0에서 지나갑니다. 평면상의 한 점,
       설 수 있는 두 표면, 그리고 답은 이미 어디에 있는지에 달려 있습니다. ::level_ground가
       질문받을 방법이 없던 것이 그것입니다. 표본 지점은 (-5, -5)입니다. 첫 판본은 z = +5를
       썼는데 그곳은 발코니 아래가 전혀 아닙니다. 바닥이 0이라고 올바르게 보고했고 아무것도
       증명하지 못했습니다. */
    const float BX = -5.0f, BZ = -5.0f;

    /* From underneath: feet at 0.5m, so the box's head at 2.2m clears the
       balcony's underside at 2.5m and the sweep sees only the floor. */
    brush_trace(&A, 0, A.n_brushes, v3f(BX, 0.5f, BZ), v3f(BX, -1.0f, BZ),
                MINS, MAXS, &t);
    checkf(t.end.y, 0.0f, 0.02f, "under the balcony, the floor is at zero");

    /* From above: feet at 4m, head at 5.7m under the 6m ceiling. */
    brush_trace(&A, 0, A.n_brushes, v3f(BX, 4.0f, BZ), v3f(BX, 2.6f, BZ),
                MINS, MAXS, &t);
    checkf(t.end.y, 3.0f, 0.02f, "over it, the balcony is at three metres");

    /* And standing under it, the ceiling overhead is the balcony rather than
       the room's -- an upward trace finds 2.5m, not 6m. */
    brush_trace(&A, 0, A.n_brushes, v3f(BX, 0.5f, BZ), v3f(BX, 4.0f, BZ),
                MINS, MAXS, &t);
    check(t.hit, "and from under it, the head meets something");
    checkf(t.end.y + PLAYER_EYE, 2.5f, 0.02f, "which is its underside, not the room's ceiling");
}

/* --- the point of all of it -----------------------------------------------
 *
 * A .map used as-is, by the game's own code, with no converter anywhere. Not
 * brush_trace and not brush_slide_move -- level_load, level_ground and
 * player_move, the functions the running game calls, given a level that came
 * out of TrenchBroom.
 *
 * Nothing in player.c changed to make this work. It holds a `const Level *`,
 * asks level_ground where the floor is, and cannot tell that the answer came
 * from a plane sweep instead of a sector lookup. That is what Level::brushes
 * bought and it is the whole reason for the branch rather than a rewrite.
 *
 * 변환기 없이, 게임 자신의 코드로, .map을 그대로 쓰는 것입니다. brush_trace도
 * brush_slide_move도 아니라 level_load, level_ground, player_move입니다. 실행 중인 게임이
 * 호출하는 함수들에게 TrenchBroom에서 나온 레벨을 건네는 것입니다.
 *
 * 이것을 위해 player.c는 아무것도 바뀌지 않았습니다. `const Level *`를 들고, level_ground에
 * 바닥이 어디인지 묻고, 그 답이 섹터 조회가 아니라 평면 스윕에서 왔다는 것을 구별할 수
 * 없습니다. Level::brushes가 사 온 것이 그것이며, 다시 쓰는 대신 분기한 이유 전부입니다.
 */
static Level LV;

static void test_level_on_map(void) {
    printf("\na .map used as-is, by the game's own player\n");

    check(level_load("atrium", &LV) != 0, "level_load finds atrium");
    check(LV.brushes != 0, "and it comes back brush-backed");
    check(LV.n_sectors == 0, "with no sectors at all");

    /* arena is still sectors, through the same call, on the same build. The
       branch is per level and not per build, which is what lets the two models
       coexist while levels are moved over one at a time. */
    static Level SEC;
    if (level_load("arena", &SEC)) {
        check(SEC.brushes == 0, "arena still loads as sectors");
        check(SEC.n_sectors > 0, "and still has them");
    }

    /* info_player_start became Level::start, in the centimetres and
       millidegrees Level already speaks. */
    checkf(LV.start[0] * 0.01f, 0.0f, 0.05f, "start x came from the entity");
    checkf(LV.start[1] * 0.01f, 5.0f, 0.05f, "start z came from the entity");

    /* level_ground -- the 2D query -- answered by a pair of sweeps. */
    float f = 0, c = 0;
    check(level_ground(&LV, 0.0f, 5.0f, 1.0f, 1e9f, &f, &c),
          "level_ground answers on a brush level");
    checkf(f, 0.0f, 0.03f, "floor at zero");
    checkf(c, 6.0f, 0.05f, "ceiling at six metres");

    /* And on the ramp, where the sector version had no answer to give: the
       floor at one point on the plan is not the floor at the next. */
    check(level_ground(&LV, -4.0f, 1.0f, 4.0f, 1e9f, &f, &c), "and over the ramp");
    checkf(f, 1.5f, 0.05f, "where the floor is half way up it");
    check(level_ground(&LV, -4.0f, 3.0f, 4.0f, 1e9f, &f, &c), "and further down it");
    checkf(f, 0.5f, 0.05f, "where the floor is lower");

    /* --- player.c, unchanged, on brushes ------------------------------------
       The same smoke test movetest runs against arena, asking only what must be
       true of any level: you start on the floor, and however you thrash you
       neither leave the map nor sink through it. */
    Player p = {0};
    player_spawn(&p, &LV);
    checkf(p.pos.y, PLAYER_EYE, 0.05f, "player_spawn stands on the floor");

    unsigned rng = 987654321u;
    int escaped = 0, sunk = 0;
    float highest = p.pos.y;
    for (int i = 0; i < 3000; i++) {
        rng = rng * 1664525u + 1013904223u;
        float a = (rng >> 8) * (6.2831853f / 16777216.0f);
        player_move(&p, &LV, v3f(cosf(a), 0, sinf(a)), PLAYER_WALK,
                    (rng & 0x400000) != 0, DT);

        if (p.pos.y > highest) highest = p.pos.y;
        if (!level_ground(&LV, p.pos.x, p.pos.z, p.pos.y - PLAYER_EYE, 1e9f, &f, &c))
            escaped = 1;
        else if (p.pos.y - PLAYER_EYE < f - 0.05f)
            sunk = 1;
    }
    check(!escaped, "3000 random frames: never left the map");
    check(!sunk,    "3000 random frames: never sank through a floor");
    check(highest > 1.0f + PLAYER_EYE,
          "and got up onto the ramp or the balcony at some point");

    /* --- the lamps, baked into the vertices ---------------------------------
       Quake's `light` entities became Level::lights, and level_geometry ran the
       SAME bake the sector path runs -- ::bake_light reads Level::lights and
       shadows with ::level_blocked, and both already answered for either model.
       So this asserts two things at once: that the entities were read, and that
       the shared bake reached brush geometry.
       Quake의 `light` 엔티티가 Level::lights가 되었고, level_geometry는 섹터 경로가 돌리는
       것과 *같은* 베이크를 돌렸습니다. ::bake_light는 Level::lights를 읽고
       ::level_blocked로 그림자를 지우며, 그 둘은 이미 어느 모델에 대해서든 답했습니다.
       따라서 이것은 두 가지를 한 번에 단언합니다. 엔티티가 읽혔다는 것과, 공유된 베이크가
       브러시 지오메트리에 닿았다는 것입니다. */
    check(LV.n_lights == 3, "the map's three light entities were read");
    check(LV.lights[0].radius > 1000 && LV.lights[0].radius < 1300,
          "with `light 400` read as reach in centimetres");
    check(LV.lights[0].r == 255 && LV.lights[0].g < 255 && LV.lights[0].b < LV.lights[0].g,
          "and a 0..1 _color scaled to bytes, warm as written");

    static MeshBuf LB;
    static MdlRange LR[LVL_MAX_RANGES];
    mb_init(&LB, 200000);
    level_light_cache_reset();
    int nr = level_geometry(&LB, &LV, LR, LVL_MAX_RANGES);
    check(nr > 0 && LB.count > 0, "level_geometry builds the brush level");

    int lit = 0;
    float brightest = 0.0f;
    for (int i = 0; i < LB.count; i++) {
        float s = LB.v[i].lr + LB.v[i].lg + LB.v[i].lb;
        if (s > 0.001f) lit++;
        if (s > brightest) brightest = s;
    }
    check(lit > 0, "and the bake reached its vertices");
    check(brightest > 0.1f, "with somewhere actually bright");

    /* NOT EVERYTHING, which is the half that says the shadows work. A bake that
       ignored ::level_blocked would light every vertex it could reach, including
       the ones facing away from every lamp and the ones behind a wall.
       전부는 아니며, 그 절반이 그림자가 동작한다고 말합니다. ::level_blocked를 무시한
       베이크는 닿을 수 있는 모든 정점을 밝히며, 그중에는 모든 등을 등지고 있는 정점과 벽
       뒤의 정점도 포함됩니다. */
    check(lit < LB.count, "and did not light every vertex in the level");
    mb_free(&LB);
}

int main(void) {
    printf("tracetest\n");
    build();

    if (!brush_parse(MAPTEXT, -1, &M)) {
        printf("\n  THE FIXTURE DID NOT PARSE\n");
        return 1;
    }
    printf("  fixture: %d brushes, %d faces\n", M.n_brushes, M.n_faces);

    test_floor();
    test_wall();
    test_step();
    test_slope();
    test_edges();
    test_slide();
    test_stepping();
    test_walking_slopes();
    test_settling();
    test_atrium();
    test_level_on_map();

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall trace checks passed\n", fails);
    return fails != 0;
}
