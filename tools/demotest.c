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

    /* THE BASELINE MONSTER, NAMED. This said `imp` for as long as `imp` was the
       name of MON_WATER_SPIRIT's row; it is a retired alias now and retired
       aliases follow what REPLACED them, so `imp` would hand this fixture a
       caster -- a different creature with different health that does not stand
       on the floor -- and the golden below would have to be re-blessed for a
       change nobody made to the demo.
       기준 몬스터를 이름으로 적습니다. `imp`가 MON_WATER_SPIRIT 행의 이름이던 동안에는 이곳도
       `imp`라고 적혀 있었습니다. 이제 그것은 은퇴한 별칭이고 은퇴한 별칭은 자기를 *대신한* 것을
       따라가므로, `imp`는 이 픽스처에 캐스터를 건네게 됩니다. 체력이 다르고 바닥에 서지도 않는
       다른 생물이며, 그러면 아래의 골든을 아무도 하지 않은 변경 때문에 다시 승인해야 합니다. */
    Entity *e;
    e = &w->level.ents[w->level.n_ents++];
    {
        const char *kind = "water_spirit";
        int ki = 0;
        while (kind[ki]) { e->kind[ki] = kind[ki]; ki++; }
        e->kind[ki] = 0;
    }
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

/* ------------------------------------------------------------- the golden */

/**
 * @struct Digest
 * @brief What a run comes to, in the few numbers worth pinning.
 *
 * ENGLISH
 * -------
 * THE QUESTION THIS ANSWERS is "did this commit change the simulation" -- which
 * nothing else here can. Every other check in this folder asserts a RULE: a door
 * opens when touched, a trace stops at the first solid, a replay matches its
 * recording. All of them keep passing when a constant is retuned, which is
 * correct: they are about behaviour, not about this week's numbers. A golden is
 * the opposite and is the only thing that notices a change nobody meant.
 *
 * @note Compared EXACTLY, not within a tolerance. The reason is measured rather
 *       than hoped for: the same thirty seconds built at -O0, -O1, -O2, -O3 and
 *       -Os land on the same bits, because this is SSE2 with no fast-math and
 *       no contraction, and every random source in the World is an integer LCG.
 *       Measured the other way too -- nudging PLAYER_GRAVITY from 22.0 to
 *       22.001, a change of five thousandths of a percent, moves pos.z by a
 *       centimetre after thirty seconds and this says which field and by how
 *       much.
 *       A tolerance would only decide how much silent drift is acceptable, and
 *       the answer to that is none -- a simulation that carries its own state
 *       forward turns any drift at all into a different run within seconds.
 * @note A FAILURE HERE IS NOT A BUG. It is a diff. Read what moved, decide
 *       whether you meant it, and if you did, run `demotest -bless` and paste.
 *
 * 한국어
 * ------
 * @brief 한 플레이가 도달한 결과를, 못 박을 가치가 있는 몇 개의 숫자로.
 *
 * 이것이 답하는 질문은 "이 커밋이 시뮬레이션을 바꿨는가"이며, 이 폴더의 다른 무엇도 그것에
 * 답할 수 없습니다. 다른 모든 검사는 *규칙*을 단언합니다. 문은 닿으면 열린다, 판정은 첫 고체에서
 * 멈춘다, 재생은 기록과 일치한다. 그 전부는 상수를 조정해도 계속 통과하며 그것이 옳습니다. 그들이
 * 다루는 것은 동작이지 이번 주의 숫자가 아닙니다. 골든은 그 반대이며, 아무도 의도하지 않은 변화를
 * 알아채는 유일한 것입니다.
 *
 * @note 허용 오차가 아니라 *정확히* 비교합니다. 그 근거는 희망이 아니라 측정입니다. 같은 30초를
 *       -O0, -O1, -O2, -O3, -Os로 빌드해도 같은 비트에 도달합니다. fast-math도 축약도 없는
 *       SSE2이고 World의 모든 난수원이 정수 LCG이기 때문입니다. 반대 방향으로도 측정했습니다.
 *       PLAYER_GRAVITY를 22.0에서 22.001로, 십만분의 오만큼 밀면 30초 뒤 pos.z가 1센티미터
 *       움직이고 이것이 어느 필드가 얼마나 움직였는지 말해 줍니다. 허용 오차는 얼마만큼의 조용한 어긋남을
 *       받아들일지 정하는 것일 뿐이고, 그 답은 "없음"입니다. 자기 상태를 앞으로 나르는
 *       시뮬레이션은 어떤 어긋남이든 몇 초 만에 다른 플레이로 바꿉니다.
 * @note 이곳의 실패는 *버그가 아닙니다*. diff입니다. 무엇이 움직였는지 읽고, 의도한 것인지
 *       판단하고, 의도한 것이라면 `demotest -bless`를 실행해 붙여 넣으십시오.
 */
typedef struct {
    float    px, py, pz;
    float    vx, vy, vz;
    float    yaw, pitch;
    int      health, keys, grounded;
    int      cur, ammo_shotgun;
    unsigned wrng, srng, erng, frng;
    int      enemies_alive, enemy_hp;
    int      proj_live, marks_live;
    float    world_time;
} Digest;

/* A field added above without a value below would compare against a zero
   nobody chose. The size is what ties the two together, the way ::PlayerProgress
   ties its three lists.
   위에 필드를 추가하고 아래에 값을 넣지 않으면 아무도 고르지 않은 0과 비교하게 됩니다. 둘을
   묶는 것은 크기이며, ::PlayerProgress가 자기 세 목록을 묶는 방식과 같습니다. */
_Static_assert(sizeof(Digest) == 22 * 4, "a field added to Digest needs a golden value beside it");

static Digest digest_of(const World *w) {
    Digest d = {0};
    d.px = w->player.pos.x; d.py = w->player.pos.y; d.pz = w->player.pos.z;
    d.vx = w->player.vel.x; d.vy = w->player.vel.y; d.vz = w->player.vel.z;
    d.yaw = w->yaw;         d.pitch = w->pitch;

    d.health   = w->player.health;
    d.keys     = w->player.keys;
    d.grounded = w->player.grounded;

    d.cur           = w->weapon.cur;
    d.ammo_shotgun  = w->weapon.ammo[WP_SHOTGUN];

    d.wrng = w->weapon.rng;
    d.srng = w->run.smoke_rng;
    d.erng = w->pools.enemy.rng;
    d.frng = w->pools.fx.rng;

    for (int i = 0; i < w->pools.enemy.count; i++) {
        if (!w->pools.enemy.m[i].active) continue;
        d.enemies_alive++;
        d.enemy_hp += w->pools.enemy.m[i].health;
    }
    for (int i = 0; i < PROJ_MAX; i++)
        if (w->pools.proj.p[i].active) d.proj_live++;

    d.marks_live = decal_live_marks(&w->pools);
    d.world_time = w->run.world_time;
    return d;
}

static void digest_print(const Digest *d) {
    printf("static const Digest GOLDEN = {\n");
    printf("    /* px py pz */ %.9gf, %.9gf, %.9gf,\n", d->px, d->py, d->pz);
    printf("    /* vx vy vz */ %.9gf, %.9gf, %.9gf,\n", d->vx, d->vy, d->vz);
    printf("    /* yaw pitch */ %.9gf, %.9gf,\n", d->yaw, d->pitch);
    printf("    /* health keys grounded */ %d, %d, %d,\n",
           d->health, d->keys, d->grounded);
    printf("    /* cur ammo */ %d, %d,\n", d->cur, d->ammo_shotgun);
    printf("    /* wrng srng erng frng */ %uu, %uu, %uu, %uu,\n",
           d->wrng, d->srng, d->erng, d->frng);
    printf("    /* enemies hp */ %d, %d,\n", d->enemies_alive, d->enemy_hp);
    printf("    /* proj marks */ %d, %d,\n", d->proj_live, d->marks_live);
    printf("    /* world_time */ %.9gf\n", d->world_time);
    printf("};\n");
}

/* Filled by `demotest -bless`. Every number here was produced by the run below
   and nothing else; none of them was chosen.
   `demotest -bless`가 채웁니다. 이곳의 모든 숫자는 아래의 실행이 만들어 낸 것이며 그 외에는
   없습니다. 어느 것도 사람이 고르지 않았습니다. */
/* RE-BLESSED six times: for `landdust`, for thickening the lava smoke, for
   floor items giving off specks instead of carrying a halo, for the water
   spirit taking the imp's slot, for a bolt gaining a wake and a landing, and
   for the water spirit trading its shotgun for a stream. Worth recording what moved and what did not,
   because the two answer different questions: `frng` changed and NOTHING ELSE
   did -- not the position, not the velocity, not the aim, not the health, not
   the monsters, not world_time. A hard landing now spawns a puff, so the
   PARTICLE rng is one draw further along; the simulation the player is playing
   is bit-for-bit the run it was before.
   That is the shape a presentation change should leave here. A diff that also
   moved `px` would be a presentation change that was not one.
   `landdust` 때문에 한 번 다시 승인했습니다. 무엇이 움직였고 무엇이 움직이지 않았는지 적어 둘
   가치가 있습니다. 둘은 서로 다른 질문에 답하기 때문입니다. `frng`가 바뀌었고 *그 외에는
   아무것도* 바뀌지 않았습니다. 위치도, 속도도, 조준도, 체력도, 몬스터도, world_time도
   아닙니다. 세게 착지하면 이제 먼지가 생기므로 *파티클* 난수가 한 번 더 진행되었을 뿐,
   플레이어가 하는 시뮬레이션은 이전의 그 플레이와 비트 단위로 같습니다.
   연출 변경이 이곳에 남겨야 할 모양이 그것입니다. `px`까지 움직인 차이는 연출 변경이
   아니었던 것입니다.
   The second time moved `srng` and nothing else, for the same reason: the smoke
   emits more often, so its own rng is further along. Both re-blessings have the
   same shape, which is the shape to insist on -- a presentation change that
   moved `px` would not have been one.
   두 번째는 `srng`만 움직였고 이유도 같습니다. 연기가 더 자주 방출되므로 자기 난수가 더
   진행되었습니다. 두 번의 재승인이 같은 모양이며, 그것이 고집해야 할 모양입니다. `px`를
   움직인 연출 변경은 연출 변경이 아니었을 것입니다.

   THE THIRD MOVED `frng` AND NOTHING ELSE, and this one was worth the check
   rather than merely surviving it. Floor items stopped carrying a halo and
   started giving off `itemmote`, which means every pickup in the level now
   spawns particles on a timer -- and the drop tables that arrived alongside it
   roll ::EnemyPool::rng twice per kill. If either had leaked into the
   simulation, `erng` would have moved with `frng` and the run would have
   diverged at the first monster. `erng` is unchanged, so this demo killed
   nothing, and `wrng` is unchanged, so nothing was thrown. The specks are the
   whole diff.
   세 번째는 `frng`만 움직였으며, 이번 것은 그저 통과한 것이 아니라 검사할 가치가 있었습니다.
   바닥 아이템이 헤일로를 버리고 `itemmote`을 내보내기 시작했으므로, 이제 레벨의 모든 아이템이
   타이머에 맞춰 입자를 생성합니다. 그리고 그와 함께 도착한 드롭 표는 처치마다
   ::EnemyPool::rng를 두 번 굴립니다. 둘 중 하나라도 시뮬레이션으로 새어 들어갔다면 `erng`가
   `frng`와 함께 움직였을 것이고 첫 몬스터에서 플레이가 어긋났을 것입니다. `erng`가 그대로이니
   이 데모는 아무것도 죽이지 않았고, `wrng`가 그대로이니 아무것도 던져지지 않았습니다.
   알갱이가 차이의 전부입니다.

   THE FOURTH IS THE FIRST THAT SHOULD HAVE MOVED MORE THAN AN RNG, and that is
   what makes it worth reading beside the other three. The imp was a melee
   creature and the water spirit that replaced it holds mid range and sprays --
   so the monster this demo walks past stopped swinging and started shooting.
   `health` moved from 73 to 80 because four damage a bolt from seven metres is
   not nine damage a swing from arm's length; `erng` moved because a volley
   draws randoms a swing never did; `frng` followed the bolts.
   What did NOT move is the whole point: `px`, `vx`, `yaw`, `pitch` and
   `world_time` are bit-for-bit what they were. The recorded input is the same
   input and it still lands in the same place -- the fight around the player
   changed, the player did not.
   네 번째는 난수 이상이 움직여야 마땅했던 첫 번째이며, 그것이 앞의 셋 곁에서 읽을 값어치를
   만듭니다. 임프는 근접 생물이었고 그 자리를 대신한 물의 정령은 중거리를 유지하며 난사하므로,
   이 데모가 지나치는 몬스터가 휘두르기를 그만두고 쏘기 시작했습니다. `health`가 73에서 80이
   된 것은 7미터에서 발당 4의 피해가 팔 길이에서 한 번에 9의 피해와 같지 않기 때문이고,
   `erng`가 움직인 것은 일제 사격이 휘두르기가 결코 뽑지 않던 난수를 뽑기 때문이며, `frng`는
   볼트를 따라갔습니다.
   *움직이지 않은 것*이 요점의 전부입니다. `px`, `vx`, `yaw`, `pitch`, `world_time`이 비트
   단위로 그대로입니다. 기록된 입력은 같은 입력이고 여전히 같은 자리에 떨어집니다. 플레이어
   주위의 전투가 바뀌었을 뿐 플레이어는 바뀌지 않았습니다.

   THE FIFTH IS THE FOURTH'S OPPOSITE, and reading them together is the point.
   Twelve effects arrived and four of them attach to a bolt in flight or to the
   place it stops: `boltwake` behind it, `zapflash` and `zapburst` where it
   lands, `emberwake` on what is still burning. The monster this demo walks past
   is the same monster shooting the same bolts -- `health` is still 80, `erng`
   and `wrng` have not moved a bit, and the round arrives at the same place on
   the same frame. What changed is that the flight is now DRAWN, so `frng` alone
   ran further.
   That is the fourth entry's diff with the simulation half removed. The fourth
   moved `health` and `erng` because the FIGHT changed; this one moved neither
   because only the PICTURE of it did, and the two sitting next to each other
   are what makes this golden worth keeping.
   다섯 번째는 네 번째의 반대이며, 둘을 나란히 읽는 것이 요점입니다. 효과 열둘이 도착했고 그중
   넷이 날아가는 볼트나 그것이 멈추는 자리에 붙습니다. 뒤로는 `boltwake`, 떨어지는 자리에는
   `zapflash`와 `zapburst`, 아직 타고 있는 것에는 `emberwake`입니다. 이 데모가 지나치는
   몬스터는 같은 볼트를 쏘는 같은 몬스터입니다. `health`는 여전히 80이고 `erng`와 `wrng`는
   조금도 움직이지 않았으며, 탄은 같은 프레임에 같은 자리에 도착합니다. 바뀐 것은 그 비행이
   이제 *그려진다*는 것뿐이므로 `frng`만 더 진행되었습니다.
   이것은 네 번째의 차이에서 시뮬레이션 절반을 덜어 낸 것입니다. 네 번째가 `health`와 `erng`를
   움직인 것은 *전투*가 바뀌었기 때문이고, 이번 것이 둘 다 움직이지 않은 것은 그 *그림*만
   바뀌었기 때문입니다. 그 둘이 나란히 있다는 것이 이 골든을 지킬 값어치를 만듭니다.

   THE SIXTH IS THE FOURTH AGAIN, and that it has the same shape twice is the
   useful part. The water spirit stopped firing five bolts in one frame and
   started firing five to ten over ::MonType::shot_gap, at a third less damage
   each and no longer following the player while it fires. `health` moved 80 to
   73 because the FIGHT changed -- a stream aimed where the player WAS lands
   differently from a cone aimed where they ARE -- and `erng` moved because the
   volley rolls its own length and scatters every bolt after the first, which is
   a draw the shotgun never made. `frng` followed the bolts, as it always does.
   What did NOT move is the list worth reading: `px`, `vx`, `yaw`, `pitch` and
   `world_time` are bit-for-bit what they were, and so are `wrng` and `srng` --
   the player threw nothing and the smoke is the same smoke. `enemies hp` is
   still 40, which is the one to check on THIS change specifically: an earlier
   cut of it left the monster planted 55% longer per volley and the demo's
   recorded fire cut it to 5, and a monster that is easier to kill because its
   attack takes longer is a balance decision nobody made. The cooldown came down
   by what the firing time added, and the row went back to being untouched.
   여섯 번째는 네 번째의 반복이며, 같은 모양이 두 번 나왔다는 것이 쓸모 있는 부분입니다. 물의
   정령이 한 프레임에 다섯 발을 쏘던 것을 그만두고 ::MonType::shot_gap에 걸쳐 5~10발을,
   발당 3분의 1 적은 피해로, 쏘는 동안 플레이어를 따라가지 않으면서 쏘기 시작했습니다.
   `health`가 80에서 73이 된 것은 *전투*가 바뀌었기 때문입니다. 플레이어가 *있던* 곳을 겨눈
   줄기는 *있는* 곳을 겨눈 원뿔과 다르게 도착합니다. `erng`가 움직인 것은 일제 사격이 자기
   길이를 굴리고 첫 발 이후 모든 발을 흩뿌리기 때문이며, 그것은 산탄이 결코 하지 않던
   굴림입니다. `frng`는 늘 그렇듯 볼트를 따라갔습니다.
   *움직이지 않은 것*의 목록이 읽을 값어치가 있습니다. `px`, `vx`, `yaw`, `pitch`,
   `world_time`이 비트 단위로 그대로이고 `wrng`와 `srng`도 그렇습니다. 플레이어는 아무것도
   던지지 않았고 연기는 같은 연기입니다. `enemies hp`가 여전히 40인데, *이번* 변경에서
   특별히 확인해야 할 것이 그것입니다. 앞선 판본은 몬스터를 일제 사격당 55% 더 오래 붙박아
   두었고 데모의 기록된 사격이 그것을 5까지 깎았습니다. 공격이 오래 걸린다는 이유로 죽이기
   쉬워진 몬스터는 아무도 내리지 않은 밸런스 결정입니다. 사격 시간이 더한 만큼 경직을
   내렸고, 그 행은 다시 손대지 않은 상태로 돌아왔습니다. */
/* THE SEVENTH MOVED ONE FIELD AND THAT IS THE WHOLE REPORT. `srng` and
   nothing else: the lava now boils as well as smokes, and the bubbles throw
   their own darts from the same generator the puffs do, so the smoke stream
   advances further per tick. Everything a player would feel is bit-for-bit
   unchanged -- `px`, `health`, `erng`, `enemies hp`, `world_time` -- which is
   the claim this line exists to make. A surface effect that moved `erng`
   would have been a surface effect that changed a fight.
   *일곱 번째는 필드 하나를 움직였고 그것이 보고의 전부입니다.* `srng`뿐입니다. 용암이 이제
   연기를 낼 뿐 아니라 끓으며, 거품은 연기와 같은 생성기에서 자기 다트를 던지므로 연기 흐름이
   틱마다 더 나아갑니다. 플레이어가 느낄 모든 것은 비트 단위로 그대로입니다. `px`, `health`,
   `erng`, `enemies hp`, `world_time`이며, 그것이 이 줄이 존재하는 이유인 주장입니다. `erng`를
   움직인 표면 효과였다면 그것은 전투를 바꾼 표면 효과였을 것입니다. */
/* THE EIGHTH REVERSED THE FIGHT, and the reversal is the report. The
   bestiary went from 1.9-3.0 m/s to 5.6-7.0 against a player who walks at
   10.8, and the approach stopped being a straight line. `health` went 73 to
   100 and `enemies hp` went 40 to -2: the monster used to shoot the player and
   never be hit, and now it is killed without landing anything.
   THAT IS NOT THE MONSTERS GETTING WEAKER. A demo is a RECORDED INPUT and
   recorded input does not adapt: the monster reaches its firing band about
   four and a half seconds earlier than it used to, so it arrives into shots
   that were aimed at where it would have been. Against a player who reacts it
   is the opposite -- steptest still measures 30 damage landed on a passive
   player over five seconds, and enemytest measures a brute crossing nineteen
   metres in 3.55s where the old one needed ten seconds.
   WHAT DID NOT MOVE IS THE PROOF. `px`, `vx`, `yaw`, `pitch`, `world_time`,
   `wrng` and `srng` are bit-for-bit what they were: the recording drives the
   player identically and the player's own frame is untouched. Only the fight
   is different, which is the only thing this change should be able to reach.
   *여덟 번째는 전투를 뒤집었고, 그 뒤집힘이 곧 보고입니다.* 도감이 1.9~3.0 m/s에서
   5.6~7.0으로 갔습니다. 걷기 10.8인 플레이어에 대해서이며, 접근이 직선이기를 그만두었습니다.
   `health`가 73에서 100으로, `enemies hp`가 40에서 -2로 갔습니다. 몬스터는 플레이어를 쏘고
   자신은 맞지 않았었는데, 이제 아무것도 맞히지 못한 채 죽습니다.
   *그것은 몬스터가 약해진 것이 아닙니다.* 데모는 *녹화된 입력*이고 녹화된 입력은 적응하지
   않습니다. 몬스터는 예전보다 4.5초쯤 일찍 사격 사거리에 닿으므로, 그것이 *있었을* 자리를
   겨눈 사격 속으로 들어옵니다. 반응하는 플레이어에게는 반대입니다. steptest는 여전히 수동적인
   플레이어에게 5초 동안 30의 피해가 꽂히는 것을 재고, enemytest는 브루트가 19미터를 3.55초에
   건너는 것을 잽니다. 예전 것은 10초가 필요했습니다.
   *움직이지 않은 것이 증거입니다.* `px`, `vx`, `yaw`, `pitch`, `world_time`, `wrng`, `srng`가
   비트 단위로 그대로입니다. 녹화본이 플레이어를 똑같이 구동하고 플레이어 자신의 프레임은
   손대지 않았습니다. 다른 것은 전투뿐이며, 이 변경이 닿을 수 있어야 하는 유일한 것입니다. */
static const Digest GOLDEN = {
    /* px py pz */ -12.2013168f, 2.84367466f, -14.8857975f,
    /* vx vy vz */ -0.312936455f, 1.9998908f, 0.38904506f,
    /* yaw pitch */ 0.382800102f, 0.534599602f,
    /* health keys grounded */ 100, 0, 0,
    /* cur ammo */ 0, 0,
    /* wrng srng erng frng */ 2972006077u, 3888997821u, 685668312u, 1063135996u,
    /* enemies hp */ 1, -2,
    /* proj marks */ 0, 0,
    /* world_time */ 29.9002438f
};

static int golden_bad;

static void same_exact_f(const char *name, float got, float want) {
    if (got == want) return;
    golden_bad++;
    printf("      %-18s %.9g   golden %.9g\n", name, got, want);
}
static void same_exact_i(const char *name, long got, long want) {
    if (got == want) return;
    golden_bad++;
    printf("      %-18s %ld   golden %ld\n", name, got, want);
}

int main(int argc, char **argv) {
    /* `-bless` prints the digest this build produces, in a form to paste over
       ::GOLDEN. It is the only supported way to change that table: a golden
       somebody typed is a golden that records what they expected rather than
       what the simulation does.
       `-bless`는 이 빌드가 만들어 내는 다이제스트를 ::GOLDEN 위에 붙여 넣을 수 있는 형태로
       출력합니다. 그 표를 바꾸는 유일한 지원 방법입니다. 사람이 타이핑한 골든은 시뮬레이션이
       하는 일이 아니라 그 사람이 기대한 것을 기록한 골든입니다. */
    int bless = (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'b');

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

    /* --- driving a frame from one ----------------------------------------
       These are the rules that lived in the body of WinMain, which is the one
       place in this project no test can reach. Moving them into demo.c is what
       this block exists to check -- not that they are new, but that they are
       now somewhere they can be asked about.
       이것들은 WinMain의 본문에 살던 규칙입니다. 이 프로젝트에서 어떤 테스트도 닿을 수 없는
       유일한 곳입니다. 그것을 demo.c로 옮긴 것이 이 블록이 존재하는 이유이며, 규칙이 새롭다는
       것이 아니라 이제 물어볼 수 있는 곳에 있다는 것이 요점입니다. */
    printf("\ndriving\n");
    {
        static DemoDrive dr;

        /* Three frames is enough to have a beginning, a middle and an end. */
        demo_begin(&dr.d, "fixture");
        for (int i = 0; i < 3; i++) {
            Input in = {0};
            in.forward = 1;
            demo_record(&dr.d, &in, VW, VH, DT_US * 0.000001f);
        }
        dr.mode  = DEMO_PLAY;
        dr.frame = 0;

        int supplied = 0;
        for (int i = 0; i < 10; i++) {
            Input in;
            float a = 0.0f, d = 0.0f;
            if (!demo_take(&dr, &in, &a, &d)) break;
            supplied++;
        }
        ok(supplied == 3, "a playing demo supplies exactly the frames it holds");
        ok(dr.mode == DEMO_OFF, "and switches itself off when it runs out");

        /* THE CONTRACT THAT MATTERS. On a zero return the caller's own aspect
           and dt have to survive, because the caller is about to use them for a
           live frame. A demo_take that wrote to them on its way to saying "not
           mine" would make the live path depend on whether a demo had ever been
           loaded -- which is the kind of coupling that shows up as the game
           running at the wrong speed after a demo ends.
           중요한 계약입니다. 0을 반환할 때 호출자 자신의 종횡비와 dt가 살아남아야 합니다.
           호출자가 곧 그것으로 라이브 프레임을 진행시키기 때문입니다. "내 것이 아니다"라고
           말하러 가는 길에 그것들에 기록하는 demo_take는, 라이브 경로가 데모를 한 번이라도
           로드했는지에 의존하게 만듭니다. 데모가 끝난 뒤 게임이 엉뚱한 속도로 도는 형태로
           드러나는 종류의 결합입니다. */
        {
            Input in;
            float a = 1.25f, d = 0.5f;
            ok(!demo_take(&dr, &in, &a, &d), "a demo that is over supplies nothing");
            ok(a == 1.25f && d == 0.5f,
               "and leaves the caller's own aspect and dt untouched");
        }

        /* Not recording: demo_put has to do nothing rather than the caller
           having to remember to guard it.
           기록 중이 아니면 demo_put은 아무것도 하지 않아야 합니다. 호출자가 감싸는 것을
           기억해야 하는 것이 아니라. */
        {
            int before = dr.d.n;
            Input in = {0};
            demo_put(&dr, &in, VW, VH, DT_US * 0.000001f);
            ok(dr.d.n == before, "demo_put adds nothing when nothing is recording");

            dr.mode = DEMO_RECORD;
            demo_put(&dr, &in, VW, VH, DT_US * 0.000001f);
            ok(dr.d.n == before + 1, "and appends when something is");
        }
    }

    /* --- the golden ------------------------------------------------------
       Everything above asserts a RULE and keeps passing when a constant is
       retuned. This asserts the OUTCOME, which is the only way to notice that a
       change nobody meant reached the simulation. See ::Digest.
       위의 모든 것은 *규칙*을 단언하며 상수를 조정해도 계속 통과합니다. 이것은 *결과*를
       단언하며, 아무도 의도하지 않은 변화가 시뮬레이션에 닿았다는 것을 알아채는 유일한
       방법입니다. ::Digest를 참조하십시오. */
    printf("\nthe golden\n");
    {
        Digest d = digest_of(&g_live);

        if (bless) {
            printf("\n");
            digest_print(&d);
            printf("\n");
            return 0;
        }

        same_exact_f("pos.x",       d.px,    GOLDEN.px);
        same_exact_f("pos.y",       d.py,    GOLDEN.py);
        same_exact_f("pos.z",       d.pz,    GOLDEN.pz);
        same_exact_f("vel.x",       d.vx,    GOLDEN.vx);
        same_exact_f("vel.y",       d.vy,    GOLDEN.vy);
        same_exact_f("vel.z",       d.vz,    GOLDEN.vz);
        same_exact_f("yaw",         d.yaw,   GOLDEN.yaw);
        same_exact_f("pitch",       d.pitch, GOLDEN.pitch);
        same_exact_i("health",      d.health,       GOLDEN.health);
        same_exact_i("keys",        d.keys,         GOLDEN.keys);
        same_exact_i("grounded",    d.grounded,     GOLDEN.grounded);
        same_exact_i("weapon.cur",  d.cur,          GOLDEN.cur);
        same_exact_i("ammo",        d.ammo_shotgun, GOLDEN.ammo_shotgun);
        same_exact_i("weapon.rng",  (long)d.wrng, (long)GOLDEN.wrng);
        same_exact_i("smoke.rng",   (long)d.srng, (long)GOLDEN.srng);
        same_exact_i("enemy.rng",   (long)d.erng, (long)GOLDEN.erng);
        same_exact_i("fx.rng",      (long)d.frng, (long)GOLDEN.frng);
        same_exact_i("enemies",     d.enemies_alive, GOLDEN.enemies_alive);
        same_exact_i("enemy hp",    d.enemy_hp,      GOLDEN.enemy_hp);
        same_exact_i("projectiles", d.proj_live,     GOLDEN.proj_live);
        same_exact_i("marks",       d.marks_live,    GOLDEN.marks_live);
        same_exact_f("world_time",  d.world_time,    GOLDEN.world_time);

        ok(golden_bad == 0,
           "thirty seconds of input lands exactly where it landed before");
        if (golden_bad)
            printf("      -> read the diff above; if you meant it, run with -bless\n");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall demo checks passed\n", fails);
    return fails != 0;
}
