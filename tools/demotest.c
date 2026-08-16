/* demotest -- a recorded run replays into the same run, frame for frame.
 *
 * This is the check the whole feature exists for, and it is worth being precise
 * about what it proves. It does NOT compare a demo against a stored expectation
 * -- that would only say the recording round-trips. It drives one World with an
 * input stream, records that stream, then drives a SECOND World from the
 * recording alone and compares the two worlds field by field. If world_step
 * consulted anything the recording does not carry -- a clock, a global, a
 * random source seeded from outside -- the two would drift, and drift is what
 * this measures.
 *
 * The input stream is pseudo-random rather than scripted, because a scripted
 * one tests the paths somebody thought of. Random input walks into walls, fires
 * at nothing, throws the hook at the ceiling and opens the menu mid-jump, and
 * every one of those is a state the recording has to describe exactly.
 *
 * 한국어
 * ------
 * 이 기능 전체가 존재하는 이유인 검사이며, 그것이 무엇을 증명하는지 정확히 말할 가치가
 * 있습니다. 데모를 저장된 기대값과 비교하지 *않습니다*. 그것은 기록이 왕복한다는 것만 말해 줄
 * 뿐입니다. 하나의 World를 입력 스트림으로 구동하고 그 스트림을 기록한 뒤, *두 번째* World를
 * 기록만으로 구동하여 둘을 필드 단위로 비교합니다. world_step이 기록이 나르지 않는 무언가에
 * 의존한다면(시계, 전역, 바깥에서 시드된 난수원) 둘은 갈라지며, 이 도구가 재는 것이 그
 * 갈라짐입니다.
 *
 * 입력 스트림은 각본이 아니라 유사난수입니다. 각본은 누군가 떠올린 경로만 검사하기 때문입니다.
 * 무작위 입력은 벽으로 걸어 들어가고, 허공에 쏘고, 천장에 훅을 던지고, 점프 도중에 메뉴를
 * 엽니다. 그 하나하나가 기록이 정확히 서술해야 하는 상태입니다.
 */

#include <stdio.h>
#include <math.h>

#include "world.h"
#include "demo.h"
#include "door.h"
#include "pickup.h"
#include "diag.h"

#define DT_US  16667
#define FRAMES 1800          /* thirty seconds */

/* A viewport rather than an aspect, because that is what a ::Demo stores and
   what the game divides. 1600x1000 is 1.6 exactly; the point is not the value
   but that both sides reach it the same way from the same two integers.
   종횡비가 아니라 뷰포트입니다. ::Demo가 저장하는 것이자 게임이 나누는 것이기 때문입니다.
   1600x1000은 정확히 1.6이며, 요점은 값이 아니라 양쪽이 같은 두 정수로부터 같은 방식으로
   그 값에 도달한다는 것입니다. */
#define VW 1600
#define VH 1000

static int fails;
static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* --------------------------------------------------------------- fixtures */

static void box(Level *l, short x0, short z0, short x1, short z1,
                short floor, short ceil) {
    Sector *s = &l->sectors[l->n_sectors++];
    short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4;
    s->floor = floor;
    s->ceil  = ceil;
    level_bounds(s);
}

/* A world a random walk can do interesting things in: a room, a step to fall
   off, and monsters to shoot at.
   무작위 걸음이 흥미로운 일을 할 수 있는 월드입니다. 방 하나, 떨어질 단차 하나, 그리고 쏠
   몬스터입니다. */
static void fixture(World *w) {
    world_init(w);
    w->run.title = 0;

    box(&w->level, -2000, -2000, 2000, 2000,   0, 3000);
    box(&w->level,   400,   400, 1200, 1200, 150, 3000);

    Entity *e;
    e = &w->level.ents[w->level.n_ents++];
    e->kind[0]='i'; e->kind[1]='m'; e->kind[2]='p'; e->kind[3]=0;
    e->x = 900; e->z = -900;
    e = &w->level.ents[w->level.n_ents++];
    e->kind[0]='a'; e->kind[1]='m'; e->kind[2]='m'; e->kind[3]='o'; e->kind[4]=0;
    e->x = -600; e->z = 300;

    level_grid_build(&w->level);

    w->player.pos      = v3f(0.0f, PLAYER_EYE, 0.0f);
    w->player.grounded = 1;
    w->player.health   = PLAYER_MAX_HP;

    enemy_spawn_level(&w->pools, &w->level);
    pickup_spawn_level(&w->pools, &w->level);
    door_reset(&w->level);
}

/* ----------------------------------------------------------------- inputs */

static unsigned rng = 0x51ed2701u;
static unsigned nxt(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

/* Held state changes in RUNS rather than per frame, because a player holds a
   key for a while. Per-frame randomness would average out to standing still and
   never build the momentum the hook and the recoil jump need.
   유지 상태는 프레임마다가 아니라 *구간* 단위로 바뀝니다. 플레이어는 키를 한동안 누르고 있기
   때문입니다. 프레임마다 무작위로 정하면 평균적으로 제자리에 서 있게 되어, 훅과 반동 점프가
   필요로 하는 운동량이 결코 쌓이지 않습니다. */
static void gen_input(Input *in, int frame) {
    static Input held;
    if (frame % 12 == 0) {
        unsigned r = nxt();
        held.forward = (r >> 0) & 1;
        held.back    = (r >> 1) & 1;
        held.left    = (r >> 2) & 1;
        held.right   = (r >> 3) & 1;
        held.jump    = (r >> 4) & 1;
        held.fire    = (r >> 5) & 1;
        held.hook    = (r >> 6) & 1;
    }
    *in = held;

    /* Pixels, and integral, exactly as the window hands them over. */
    in->look_dx = (float)((int)(nxt() % 41) - 20);
    in->look_dy = (float)((int)(nxt() % 21) - 10);

    /* The edges, rarely, so they land in the middle of other things. */
    if (nxt() % 97 == 0) in->want_weapon = (int)(nxt() % WP_TYPES) + 1;
    if (nxt() % 211 == 0) in->let_go     = 1;
    if (nxt() % 401 == 0) in->confirm    = 1;
    in->paused = (nxt() % 331 == 0);
}

/* ------------------------------------------------------------- comparison */

static int nfail;
static void same_f(float a, float b, const char *what, int frame) {
    if (a == b) return;
    if (nfail++ < 6)
        printf("      frame %d: %s  %.9g vs %.9g\n", frame, what, a, b);
}
static void same_i(int a, int b, const char *what, int frame) {
    if (a == b) return;
    if (nfail++ < 6)
        printf("      frame %d: %s  %d vs %d\n", frame, what, a, b);
}

/* EXACT equality, not a tolerance. Two runs of the same arithmetic on the same
   inputs produce the same bits; anything else means one of them consulted
   something the other did not, and a tolerance would hide exactly that.
   허용 오차가 아니라 *정확한* 일치입니다. 같은 입력에 대한 같은 연산의 두 실행은 같은 비트를
   만듭니다. 그 외의 결과는 한쪽이 다른 쪽이 보지 않은 무언가를 참조했다는 뜻이며, 허용 오차는
   바로 그것을 가려 줍니다. */
static void compare(const World *a, const World *b, int frame) {
    same_f(a->player.pos.x, b->player.pos.x, "player.pos.x", frame);
    same_f(a->player.pos.y, b->player.pos.y, "player.pos.y", frame);
    same_f(a->player.pos.z, b->player.pos.z, "player.pos.z", frame);
    same_f(a->player.vel.x, b->player.vel.x, "player.vel.x", frame);
    same_f(a->player.vel.y, b->player.vel.y, "player.vel.y", frame);
    same_f(a->player.vel.z, b->player.vel.z, "player.vel.z", frame);
    same_i(a->player.health, b->player.health, "player.health", frame);
    same_i(a->player.keys,   b->player.keys,   "player.keys",   frame);
    same_i(a->player.grounded, b->player.grounded, "player.grounded", frame);

    same_f(a->yaw,   b->yaw,   "yaw",   frame);
    same_f(a->pitch, b->pitch, "pitch", frame);

    same_i(a->weapon.cur, b->weapon.cur, "weapon.cur", frame);
    same_i((int)a->weapon.rng, (int)b->weapon.rng, "weapon.rng", frame);
    same_f(a->weapon.cooldown, b->weapon.cooldown, "weapon.cooldown", frame);
    same_i(a->weapon.hook_state, b->weapon.hook_state, "weapon.hook_state", frame);
    for (int i = 0; i < WP_TYPES; i++) {
        same_i(a->weapon.ammo[i],  b->weapon.ammo[i],  "weapon.ammo",  frame);
        same_i(a->weapon.owned[i], b->weapon.owned[i], "weapon.owned", frame);
    }

    same_i(a->run.won,   b->run.won,   "run.won",   frame);
    same_i(a->run.dead,  b->run.dead,  "run.dead",  frame);
    same_i(a->run.title, b->run.title, "run.title", frame);
    same_i(a->run.between, b->run.between, "run.between", frame);
    same_f(a->run.world_time, b->run.world_time, "run.world_time", frame);
    same_i((int)a->run.smoke_rng, (int)b->run.smoke_rng, "run.smoke_rng", frame);

    same_i((int)a->pools.enemy.rng, (int)b->pools.enemy.rng, "enemy.rng", frame);
    same_i((int)a->pools.fx.rng,    (int)b->pools.fx.rng,    "fx.rng",    frame);
    same_i(a->pools.enemy.count, b->pools.enemy.count, "enemy.count", frame);

    for (int i = 0; i < a->pools.enemy.count && i < b->pools.enemy.count; i++) {
        const Enemy *x = &a->pools.enemy.m[i], *y = &b->pools.enemy.m[i];
        same_i(x->active, y->active, "enemy.active", frame);
        same_i(x->health, y->health, "enemy.health", frame);
        same_i(x->state,  y->state,  "enemy.state",  frame);
        same_f(x->pos.x,  y->pos.x,  "enemy.pos.x",  frame);
        same_f(x->pos.z,  y->pos.z,  "enemy.pos.z",  frame);
        same_f(x->yaw,    y->yaw,    "enemy.yaw",    frame);
    }

    int pa = 0, pb = 0;
    for (int i = 0; i < PROJ_MAX; i++) {
        if (a->pools.proj.p[i].active) pa++;
        if (b->pools.proj.p[i].active) pb++;
    }
    same_i(pa, pb, "projectiles in flight", frame);
}

/* ------------------------------------------------------------------- main */

static Demo    g_rec, g_back;
static World   g_live, g_play;
static char    g_text[1 << 20];

int main(void) {
    printf("demotest\n\n");

    /* --- one loop, two worlds, in lockstep -------------------------------
       The first draft recorded the whole run and then replayed it, comparing
       the FINISHED live world against the replay at frame i. That fails at
       frame 0 and says nothing about the format. Stepping both here means a
       divergence is reported on the frame it started, which for a simulation
       that carries its state forward is the only moment worth being told about.

       The live world gets the raw Input. The replay world gets whatever
       survived the round trip through the recording, on the same frame. A field
       the format loses is a difference immediately rather than a drift twenty
       seconds later -- which is how the quantised aspect was caught.

       루프 하나, 월드 둘, 보조를 맞춰서. 초안은 플레이 전체를 기록한 뒤 재생하면서 *끝난*
       라이브 월드를 프레임 i의 재생과 비교했습니다. 그것은 프레임 0에서 실패하며 형식에 대해
       아무것도 말해 주지 않습니다. 이곳에서 둘을 함께 진행시키면 갈라짐이 시작된 프레임에서
       보고되며, 상태를 앞으로 나르는 시뮬레이션에서 그것이 들을 가치가 있는 유일한 시점입니다.

       라이브 월드는 원본 Input을 받습니다. 재생 월드는 같은 프레임에, 기록을 왕복하고 살아남은
       것을 받습니다. 형식이 잃어버리는 필드는 20초 뒤의 어긋남이 아니라 즉시 차이로 드러나며,
       양자화된 종횡비가 그렇게 잡혔습니다. */
    printf("recording and replaying, in lockstep\n");
    fixture(&g_live);
    fixture(&g_play);
    demo_begin(&g_rec, "fixture");

    const float ASPECT = (float)VW / (float)VH;
    int played = 0;

    for (int i = 0; i < FRAMES; i++) {
        Input in;
        gen_input(&in, i);
        float dt = DT_US * 0.000001f;

        demo_record(&g_rec, &in, VW, VH, dt);
        world_step(&g_live, &in, ASPECT, dt);

        Input back;
        float back_aspect = 0.0f, back_dt = 0.0f;
        if (!demo_replay(&g_rec, i, &back, &back_aspect, &back_dt)) break;
        world_step(&g_play, &back, back_aspect, back_dt);
        played++;

        compare(&g_live, &g_play, i);
        if (nfail) break;
    }

    ok(g_rec.n == FRAMES, "every frame was recorded");
    ok(played == FRAMES, "and played back to the end");

    /* A run that did nothing would pass every check here for the wrong reason,
       so the walk has to have gone somewhere first.
       아무것도 하지 않은 플레이는 이곳의 모든 검사를 잘못된 이유로 통과시키므로, 걸음이 먼저
       어딘가로 갔어야 합니다. */
    float travelled = sqrtf(g_live.player.pos.x * g_live.player.pos.x +
                            g_live.player.pos.z * g_live.player.pos.z);
    ok(travelled > 1.0f, "the random walk actually went somewhere");
    printf("  %-58s %.2f,%.2f,%.2f\n", "(it ended with the player at)",
           g_live.player.pos.x, g_live.player.pos.y, g_live.player.pos.z);

    ok(nfail == 0, "and the replay matched the live run exactly, every frame");

    /* --- the text form round-trips --------------------------------------- */
    printf("\nas text\n");
    int len = demo_write(&g_rec, g_text, sizeof(g_text));
    ok(len > 0, "the recording fits in the buffer it was given");
    printf("  %-58s %d bytes for %d frames\n", "(size)", len, g_rec.n);

    ok(demo_read(&g_back, g_text, len), "and reads back");
    ok(g_back.n == g_rec.n, "with every frame present");
    ok(g_back.vw == g_rec.vw && g_back.vh == g_rec.vh,
       "and the viewport it was made at");

    int same = 1;
    for (int i = 0; i < g_rec.n; i++) {
        const DemoFrame *x = &g_rec.f[i], *y = &g_back.f[i];
        if (x->dt_us != y->dt_us || x->look_dx != y->look_dx ||
            x->look_dy != y->look_dy || x->bits != y->bits) { same = 0; break; }
    }
    ok(same, "and every frame identical to the one written");

    /* A truncated buffer must be refused rather than half-written: a demo that
       stops mid-file replays into a different run.
       잘린 버퍼는 절반만 쓰이는 대신 거절되어야 합니다. 파일 중간에서 끊긴 데모는 다른
       플레이로 재생됩니다. */
    ok(demo_write(&g_rec, g_text, 64) == 0,
       "a buffer too small is refused rather than truncated");

    /* --- a file this build does not understand is refused ---------------- */
    static const char WRONG_VER[] = "demo 99\nlevel arena\nf 16667 0 0 0\n";
    static const char NO_TAG[]    = "f 16667 0 0 0\n";
    ok(!demo_read(&g_back, WRONG_VER, (int)sizeof(WRONG_VER) - 1),
       "a recording from another format version is refused");
    ok(!demo_read(&g_back, NO_TAG, (int)sizeof(NO_TAG) - 1),
       "and so is text with no tag at all");

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall demo checks passed\n", fails);
    return fails != 0;
}
