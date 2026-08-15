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
#include "door.h"
#include "player.h"
#include "pools.h"
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

/* --- a door is a group of brushes that moves -----------------------------
 *
 * The sector door moved a Sector, and collision saw it without knowing what a
 * door was because collision already asked sectors where their floors were.
 * The brush door has to keep that property: nothing in brush_trace learns what
 * a door is, and the leaf blocks the doorway when it is there and does not when
 * it is not, because it IS somewhere else.
 *
 * 섹터 문은 Sector를 움직였고, 충돌 판정은 이미 섹터에게 바닥이 어디인지 묻고 있었으므로 문의
 * 정체를 모른 채 그 변화를 보았습니다. 브러시 문도 그 성질을 지켜야 합니다. brush_trace의
 * 무엇도 문이 무엇인지 배우지 않으며, 문짝은 그곳에 있을 때 출입구를 막고 없을 때 막지
 * 않습니다. 실제로 다른 곳에 가 있기 때문입니다. */
static void test_door(void) {
    printf("\na door, which is a group of brushes that moves\n");

    if (!level_load("atrium", &LV) || !LV.brushes) {
        printf("  no atrium.map\n"); fails++; return;
    }
    check(LV.n_doors == 1, "the func_door became a door definition");
    if (LV.n_doors != 1) return;

    const DoorDef *d = &LV.doors[0];
    check(d->sector < 0, "which names brushes rather than a sector");
    check(d->n_brushes == 1, "one brush: the leaf");
    check(d->axis == DOOR_UP, "`angle -1` reads as opening upward");
    check(d->amount > 300 && d->amount < 400,
          "travelling its own height less the lip, in centimetres");
    check(d->speed > 0, "at the speed the entity asked for");

    door_reset(&LV);
    checkf(door_openness(&LV, 0), 0.0f, 0.001f, "and it starts closed");

    /* The doorway is at engine x 0, z -8.25. Closed, the leaf fills it. */
    const v3 THROUGH_FROM = { 0.0f, 1.0f, -6.0f };
    const v3 THROUGH_TO   = { 0.0f, 1.0f, -11.0f };
    v3 dir = v3norm(v3sub(THROUGH_TO, THROUGH_FROM));
    float t; v3 n;
    check(level_trace(&LV, THROUGH_FROM, dir, 6.0f, &t, &n),
          "closed, a shot into the doorway is stopped");

    /* Standing in the trigger volume with the card. atrium's door is tagged
       and locked -- what it takes to open is test_trigger_and_key's subject;
       this one is about what MOVES once it does.
       카드를 들고 트리거 부피 안에 섭니다. atrium의 문은 태그가 있고 잠겨 있습니다. 무엇이
       그것을 열게 하는지는 test_trigger_and_key의 주제이고, 이 시험은 열린 뒤에 무엇이
       *움직이는지*에 관한 것입니다. */
    v3 at_door = v3f(0.0f, 1.7f, -6.5f);
    float y_closed = LV.brushes->brushes[d->first_brush].min.y;
    for (int i = 0; i < 300 && door_openness(&LV, 0) < 0.999f; i++)
        door_update(&LV, at_door, KEY_RED, DT);

    checkf(door_openness(&LV, 0), 1.0f, 0.001f, "standing in its trigger, the door opens");

    float y_open = LV.brushes->brushes[d->first_brush].min.y;
    check(y_open > y_closed + 3.0f, "and the leaf's brush actually moved up");

    check(!level_trace(&LV, THROUGH_FROM, dir, 6.0f, &t, &n),
          "open, the same shot goes through");

    /* And the player can now walk the doorway they could not before. */
    BrushMove mv = mover(v3f(0.0f, BRUSH_SKIN, -6.0f), v3f(0, 0, -5.0f), PLAYER_STEP);
    brush_slide_move(LV.brushes, 0, LV.brushes->n_brushes, &mv, 0.6f);
    check(mv.pos.z < -8.6f, "and walks through the opening");

    /* Closing again puts it back where the file drew it. A door that drifted
       by a fraction each cycle would seal a doorway after enough of them.
       다시 닫히면 파일이 그린 자리로 돌아갑니다. 주기마다 조금씩 밀리는 문은 충분히 많은
       주기 뒤에 출입구를 막아 버립니다. */
    v3 far_away = v3f(0.0f, 1.7f, 6.0f);
    for (int i = 0; i < 900 && door_openness(&LV, 0) > 0.001f; i++)
        door_update(&LV, far_away, KEY_RED, DT);
    checkf(door_openness(&LV, 0), 0.0f, 0.001f, "walking away closes it");
    checkf(LV.brushes->brushes[d->first_brush].min.y, y_closed, 0.01f,
           "and the leaf is back where the .map drew it");

    check(level_trace(&LV, THROUGH_FROM, dir, 6.0f, &t, &n),
          "so the doorway is solid again");
}

/* --- markers, and the thing that keeps making monsters --------------------
 *
 * level.c strips `monster_` and `item_` and hands the rest through, so what
 * enemy.c and pickup.c see is the names they already claimed. The point of
 * asserting it here rather than in maptest is that the whole chain has to hold:
 * the .map's classname, the kind level.c wrote, the type enemy.c looked up, and
 * the floor the monster ended up standing on -- which is the storey its origin
 * was over and not the outside of the roof.
 *
 * level.c는 `monster_`와 `item_`을 떼어 내고 나머지를 그대로 넘기므로, enemy.c와 pickup.c가
 * 보는 것은 그들이 이미 차지한 이름입니다. 이것을 maptest가 아니라 이곳에서 단언하는 이유는
 * 사슬 전체가 성립해야 하기 때문입니다. .map의 classname, level.c가 쓴 종류, enemy.c가 찾은
 * 타입, 그리고 몬스터가 결국 딛고 선 바닥입니다. 그 바닥은 origin이 있던 층이지 지붕의
 * 바깥면이 아닙니다.
 */
static Pools PL;

static void test_entities(void) {
    printf("\nmonsters, items, and a spawner that keeps making them\n");

    if (!level_load("atrium", &LV) || !LV.brushes) {
        printf("  no atrium.map\n"); fails++; return;
    }

    int imps = 0, heals = 0, spawners = 0;
    for (int i = 0; i < LV.n_ents; i++) {
        if (txt_is(LV.ents[i].kind, 3, "imp"))    imps++;
        if (txt_is(LV.ents[i].kind, 6, "health")) heals++;
        if (txt_is(LV.ents[i].kind, 12, "spawner_hound")) spawners++;
    }
    check(imps == 1,     "`monster_imp` became the kind `imp`");
    check(heals == 1,    "`item_health` became the kind `health`");
    check(LV.n_ents >= 4, "and the markers are all there");

    /* The prefix is stripped, not the name mangled: spawner_hound keeps its
       own suffix because enemy.c is what reads it. */
    int found_spawner = 0;
    for (int i = 0; i < LV.n_ents; i++) {
        const char *k = LV.ents[i].kind;
        if (k[0]=='s' && k[1]=='p' && k[2]=='a' && k[3]=='w') { found_spawner = 1;
            check(LV.ents[i].p[0] == 80, "the spawner's `wait 8` arrived as tenths");
            check(LV.ents[i].p[1] == 6,  "and its `count 6`");
            check(LV.ents[i].p[2] == 4,  "and its `maxalive 4`");
        }
    }
    check(found_spawner, "`monster_spawner_hound` came through as a kind");
    (void)spawners;

    /* --- and the modules that own those names pick them up ---------------- */
    Pools zero = {0};
    PL = zero;
    enemy_reset(&PL);
    pickup_reset(&PL);
    enemy_spawn_level(&PL, &LV);
    pickup_spawn_level(&PL, &LV);

    check(enemy_count(&PL) == 1, "enemy.c made the one monster the level drew");
    check(pickup_count(&PL) >= 2, "and pickup.c laid out the items");

    /* ON THE FLOOR, not on the roof. The marker sits at 24 units -- under a
       metre -- and the room's floor is at zero; a search that began a kilometre
       up would have settled it on the outside of the ceiling at six metres.
       지붕이 아니라 *바닥* 위입니다. 표식은 24유닛, 1미터도 안 되는 높이에 있고 방의 바닥은
       0입니다. 1킬로미터 위에서 시작한 탐색이었다면 6미터의 천장 바깥면에 안착시켰을
       것입니다. */
    const Enemy *m = enemy_at(&PL, 0);
    checkf(m->pos.y, 0.0f, 0.05f, "and it is standing on the floor, not the roof");

    /* --- the spawner ------------------------------------------------------ */
    check(PL.enemy.n_spawners == 1, "the spawner was read into the pool");
    if (PL.enemy.n_spawners != 1) return;

    check(PL.enemy.spawner[0].left == 6, "with six left to make");
    checkf(PL.enemy.spawner[0].interval, 8.0f, 0.01f, "every eight seconds");

    /* Nothing on the first frame: the first is due after a full interval. */
    int before = enemy_count(&PL);
    enemy_update(&PL, &LV, v3f(0, 1.7f, 5.0f), DT);
    check(enemy_count(&PL) == before, "and it makes nothing on the first frame");

    /* Eight seconds later, one. */
    for (int i = 0; i < 8 * 60; i++) enemy_update(&PL, &LV, v3f(0, 1.7f, 5.0f), DT);
    check(enemy_count(&PL) == before + 1, "one interval later, one monster");
    check(PL.enemy.spawner[0].left == 5, "and one fewer left to make");

    const Enemy *made = enemy_at(&PL, before);
    checkf(made->pos.y, 3.0f, 0.05f,
           "made on the balcony its origin was over, not on the floor below it");

    /* THE CEILING HOLDS IT, and nothing here is dying: the level drew one imp
       and `maxalive 4` allows three more, after which the spawner has nowhere
       to put anything. Run it far past six intervals and it is still holding.
       This is the assertion that says the ceiling is a ceiling rather than a
       suggestion -- without it an endless spawner fills the pool and raises
       DIAG_ENEMY_CAP every few seconds forever.
       천장이 그것을 붙잡고 있으며 이곳에서는 아무것도 죽지 않습니다. 레벨이 임프 하나를
       그렸고 `maxalive 4`가 셋을 더 허용하며, 그 뒤로 스포너는 아무것도 놓을 자리가
       없습니다. 여섯 주기를 한참 넘겨 돌려도 여전히 붙잡혀 있습니다. 천장이 권고가 아니라
       천장이라고 말하는 단언입니다. 이것이 없으면 무제한 스포너가 풀을 채우고 몇 초마다
       영원히 DIAG_ENEMY_CAP을 올립니다. */
    int peak = 0;
    for (int i = 0; i < 120 * 60; i++) {
        enemy_update(&PL, &LV, v3f(0, 1.7f, 5.0f), DT);
        if (enemy_alive(&PL) > peak) peak = enemy_alive(&PL);
    }
    check(peak <= 4, "never exceeding the ceiling the map set");
    check(enemy_alive(&PL) == 4, "and it is still holding at it");
    check(PL.enemy.spawner[0].left > 0, "with some still owed");

    /* Clear the room and it resumes, which is the other half: the ceiling is a
       queue and not a cancellation. Then it runs out and stops for good.
       방을 비우면 재개되며 그것이 나머지 절반입니다. 천장은 취소가 아니라 대기열입니다.
       그러고 나서 다 소진하고 완전히 멈춥니다. */
    for (int i = 0; i < enemy_count(&PL); i++) enemy_hurt(&PL, i, 9999, v3f(0, 1, 0));
    check(enemy_alive(&PL) == 0, "the room is cleared");

    int owed = PL.enemy.spawner[0].left;
    for (int i = 0; i < 300 * 60 && PL.enemy.spawner[0].left > 0; i++)
        enemy_update(&PL, &LV, v3f(0, 1.7f, 5.0f), DT);
    check(owed > 0 && PL.enemy.spawner[0].left == 0,
          "it resumes and makes the rest");
    check(!PL.enemy.spawner[0].active || PL.enemy.spawner[0].left == 0,
          "and then it is done");

    int settled = enemy_count(&PL);
    for (int i = 0; i < 60 * 60; i++) enemy_update(&PL, &LV, v3f(0, 1.7f, 5.0f), DT);
    check(enemy_count(&PL) == settled, "a minute later it has made nothing more");
}

/* --- a locked door, and a volume that opens it ----------------------------
 *
 * Two things the sector model expressed and the brush model had to grow back.
 * The key is the same idea in both -- a mask the door demands and the player
 * either holds or does not. The trigger is not: a `switch<n>` is a point with a
 * radius round it and a `trigger_multiple` is the space somebody drew, and the
 * second is why a trigger's brushes must not be solid.
 *
 * 섹터 모델이 표현하던 것 중 브러시 모델이 되찾아야 했던 두 가지입니다. 열쇠는 양쪽에서 같은
 * 개념입니다. 문이 요구하는 마스크이고 플레이어는 그것을 지녔거나 지니지 않았습니다. 트리거는
 * 다릅니다. `switch<n>`은 점과 그 둘레의 반경이고 `trigger_multiple`은 누군가 그린 공간이며,
 * 그 두 번째가 트리거의 브러시가 고체여서는 안 되는 이유입니다.
 */
static void test_trigger_and_key(void) {
    printf("\na locked door, and a volume that opens it\n");

    if (!level_load("atrium", &LV) || !LV.brushes) {
        printf("  no atrium.map\n"); fails++; return;
    }
    check(LV.n_doors == 1,    "the door is still there");
    check(LV.n_triggers == 1, "and now a trigger volume beside it");
    if (LV.n_doors != 1 || LV.n_triggers != 1) return;

    const DoorDef *d  = &LV.doors[0];
    const TriggerDef *t = &LV.triggers[0];
    check(d->key == KEY_RED, "`key red` became the red mask");
    check(d->tag > 0,        "and the door carries a tag from its targetname");
    check(t->tag == d->tag,  "which is the number the trigger's target became");

    /* NOT SOLID, which is the whole of why a trigger can be a brush. A sweep
       across the doorway must meet the door and nothing else. */
    for (int k = 0; k < t->n_brushes; k++)
        check(!LV.brushes->brushes[t->first_brush + k].solid,
              "the trigger's brushes are not solid");

    /* The player stands in it. door_update is given the EYE, so the volume has
       to contain that -- a trigger drawn only ankle-deep would never fire. */
    v3 in_volume  = v3f(0.0f, 1.7f, -6.5f);
    v3 out_volume = v3f(0.0f, 1.7f,  4.0f);
    check(brush_point_in(LV.brushes, t->first_brush, t->n_brushes, in_volume),
          "and the player standing in front of the door is inside it");
    check(!brush_point_in(LV.brushes, t->first_brush, t->n_brushes, out_volume),
          "and across the room is not");

    /* Walking into it without the key: refused, and the door does not budge. */
    door_reset(&LV);
    for (int i = 0; i < 120; i++) door_update(&LV, in_volume, KEY_NONE, DT);
    checkf(door_openness(&LV, 0), 0.0f, 0.001f, "with no key it stays shut");
    check(door_refused(&LV) == KEY_RED, "and says which card it wanted");

    /* A TAGGED DOOR IGNORES BEING TOUCHED. Standing against the leaf itself,
       outside the trigger, must do nothing -- otherwise the trigger is
       decoration and every tagged door in the level opens by leaning on it.
       태그가 있는 문은 접촉을 무시합니다. 트리거 바깥에서 문짝 자체에 붙어 서 있는 것은
       아무 일도 하지 말아야 합니다. 그러지 않으면 트리거는 장식이고 레벨의 모든 태그 달린
       문이 기대는 것만으로 열립니다. */
    door_reset(&LV);
    v3 at_leaf = v3f(0.0f, 1.7f, -8.2f);
    check(!brush_point_in(LV.brushes, t->first_brush, t->n_brushes, at_leaf),
          "fixture: the leaf itself is outside the trigger");
    for (int i = 0; i < 120; i++) door_update(&LV, at_leaf, KEY_RED, DT);
    checkf(door_openness(&LV, 0), 0.0f, 0.001f,
           "touching a tagged door does not open it");

    /* In the volume, with the card: it opens. */
    door_reset(&LV);
    for (int i = 0; i < 300 && door_openness(&LV, 0) < 0.999f; i++)
        door_update(&LV, in_volume, KEY_RED, DT);
    checkf(door_openness(&LV, 0), 1.0f, 0.001f, "in the volume with the card, it opens");

    /* And the player can be in there at all, which they could not be if the
       trigger's brushes had stayed solid. */
    BrushMove mv = mover(v3f(0.0f, BRUSH_SKIN, -4.0f), v3f(0, 0, -4.0f), PLAYER_STEP);
    brush_slide_move(LV.brushes, 0, LV.brushes->n_brushes, &mv, 0.8f);
    check(mv.pos.z < -6.5f, "and walks into the trigger rather than off it");

    /* The keycard is in the level for the player to find. */
    Pools zero = {0};
    PL = zero;
    pickup_reset(&PL);
    pickup_spawn_level(&PL, &LV);
    int reds = 0;
    for (int i = 0; i < pickup_count(&PL); i++)
        if (pickup_at(&PL, i)->kind == PK_KEY0 + 0) reds++;
    check(reds == 1, "and the red card is somewhere to be picked up");

    /* --- the exit and the jump pad ---------------------------------------
       Neither needed a dispatch: level_exit_at and level_push_at walk
       Level::ents and have never known which model the level is. All that was
       missing was a classname, and the alias table is where the two of them
       that are not families live.
       둘 다 분기가 필요 없었습니다. level_exit_at과 level_push_at은 Level::ents를 훑으며
       레벨이 어느 모델인지 알았던 적이 없습니다. 빠져 있던 것은 classname뿐이고, 계열이 아닌
       그 둘이 사는 곳이 별칭 표입니다. */
    check(txt_is(LV.next, 5, "vault"), "worldspawn's `next` says where the exit leads");

    check(level_exit_at(&LV, -5.0f, -7.0f), "standing on the exit ends the level");
    check(!level_exit_at(&LV, 0.0f, 5.0f),  "and standing anywhere else does not");

    /* 416 map units per second is the 13 m/s LVL_PUSH_DEFAULT describes. */
    checkf(level_push_at(&LV, 6.0f, 6.0f), 13.0f, 0.1f, "the pad launches at 13 m/s");
    checkf(level_push_at(&LV, 0.0f, 0.0f), 0.0f, 0.001f, "and the floor beside it does not");
}

/* --- the lava, as a volume ------------------------------------------------
 *
 * ENGLISH
 * -------
 * What the sector model could not say. `Sector::hurt` is a property of a floor,
 * so the question had two coordinates and the height was whatever the floor
 * happened to be; a safe dais in a moat had to be a SECOND SECTOR declared over
 * the lava, and a precedence rule had to exist to read that back.
 *
 * The assertions below are the same room asked in three dimensions. Standing at
 * the lip is safe, one step down is not, and the plinth in the middle is safe
 * again -- and no rule resolves the last one, because the feet on the plinth
 * are 0.75m above the lava's top face and the volume simply does not contain
 * them.
 *
 * atrium's basin, in engine units: x 3..7.5, z 3..7.5, the lava filling
 * y -0.5..-0.25 and the plinth's top at 0.5.
 *
 * 한국어
 * ------
 * 섹터 모델이 말할 수 없던 것입니다. `Sector::hurt`는 바닥의 속성이므로 질문에는 좌표가 두
 * 개뿐이었고 높이는 그 바닥이 놓인 곳이었습니다. 해자 속 안전한 단상은 용암 위에 선언된 *두
 * 번째 섹터*여야 했고, 그것을 다시 읽어 내려면 우선순위 규칙이 존재해야 했습니다.
 *
 * 아래의 단언은 같은 방을 3차원으로 묻습니다. 가장자리에 서는 것은 안전하고, 한 걸음 내려서는
 * 것은 그렇지 않으며, 한가운데의 받침대는 다시 안전합니다. 마지막 것을 해석하는 규칙은 없습니다.
 * 받침대 위의 발이 용암 윗면보다 0.75m 위에 있고 부피가 그것을 담고 있지 않을 뿐입니다.
 */
static void test_hazard(void) {
    printf("\nlava that is a volume rather than a floor\n");

    if (!level_load("atrium", &LV) || !LV.brushes) {
        printf("  no atrium.map\n"); fails++; return;
    }

    check(LV.n_hazards == 1, "atrium declares one trigger_hurt");
    if (LV.n_hazards != 1) return;
    const HazardDef *h = &LV.hazards[0];
    checkf((float)h->dps, 12.0f, 0.5f, "and its `dmg` came through as the rate");

    /* NOT A DOOR SWITCH. `trigger_hurt` shares the prefix the trigger scan
       reads, so the thing this guards is the version where it is swept up as a
       nameless trigger_multiple and the lava silently opens doors.
       문 스위치가 아닙니다. `trigger_hurt`는 트리거 주사가 읽는 접두사를 공유하므로, 이것이
       지키는 것은 이름 없는 trigger_multiple로 쓸려 들어가 용암이 조용히 문을 여는 판본입니다. */
    check(LV.n_triggers == 1, "and it did not also become a trigger volume");

    for (int k = 0; k < h->n_brushes; k++)
        check(!LV.brushes->brushes[h->first_brush + k].solid,
              "the lava's brushes are not solid");

    /* The three heights over one point on the plan, which is the whole claim. */
    check(level_hazard_at(&LV, 3.5f, -0.5f, 3.5f) == 12,
          "feet in the basin burn");
    check(level_hazard_at(&LV, 3.5f,  0.0f, 3.5f) == 0,
          "the same x,z at the room's floor height does not");
    check(level_hazard_at(&LV, 3.5f,  1.2f, 3.5f) == 0,
          "and jumping over it is a way across");

    check(level_hazard_at(&LV, 0.0f, 0.0f, 5.0f) == 0,
          "the floor beside the basin is safe");
    check(level_hazard_at(&LV, 5.25f, 0.5f, 5.25f) == 0,
          "and so is the plinth standing in the middle of it");

    /* The dais case with no precedence rule in sight: the plinth's own x,z is
       inside the lava's box, and only the height keeps it safe. */
    check(level_hazard_at(&LV, 5.25f, -0.4f, 5.25f) == 12,
          "though the lava is still there beside the plinth's foot");

    /* AND YOU CAN GET OUT. A hazard you cannot leave is a death animation.
       The basin floor is 0.5m down, inside PLAYER_STEP.
       그리고 나올 수 있습니다. 떠날 수 없는 위험 지형은 죽는 연출입니다. 웅덩이 바닥은
       0.5m 아래이며 PLAYER_STEP 안입니다. */
    float f = 0.0f, c = 0.0f;
    check(level_ground(&LV, 3.5f, 3.5f, -0.5f, PLAYER_STEP, &f, &c),
          "there is a floor under the lava");
    checkf(f, -0.5f, 0.02f, "half a metre down");
    check(0.0f - f <= PLAYER_STEP, "which is a step you can climb back out of");

    /* What the smoke sampler asks: is this bit of surface covered? Probing down
       from the lava's top finds the basin where the lava is open and the plinth
       where it is not, and that comparison is the whole exposure test.
       연기 샘플러가 묻는 것입니다. 이 표면 조각이 덮여 있는가? 용암 윗면에서 아래로
       탐침하면 열린 곳에서는 웅덩이 바닥을, 아닌 곳에서는 받침대를 찾습니다. 그 비교가
       노출 판정의 전부입니다. */
    check(level_ground(&LV, 3.5f, 3.5f, -0.25f, 1e9f, &f, &c) && f < -0.25f,
          "open lava has nothing standing on it");
    check(level_ground(&LV, 5.25f, 5.25f, -0.25f, 1e9f, &f, &c) && f > -0.25f,
          "and the plinth reads as something that does");
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
    test_door();
    test_entities();
    test_trigger_and_key();
    test_hazard();

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall trace checks passed\n", fails);
    return fails != 0;
}
