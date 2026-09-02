/* movetest -- step the player simulation against sector geometry.
 *
 * Movement bugs are impossible to pin down by walking around and trivial to
 * see by dropping a simulated player onto geometry and printing where they
 * end up.
 *
 * The rules are checked against a level built here, not against `arena`.
 * `arena` is a map somebody edits, and every edit used to break assertions
 * that named its coordinates -- which teaches you to ignore the suite rather
 * than read it. The shipped level still gets a smoke test at the end, but one
 * that asks only what must be true of any level.
 */

#include <stdio.h>
#include <math.h>
#include "player.h"
#include "level.h"

#define DT (1.0f / 60.0f)

static int fails;
static Level L;

static void check(int ok, const char *what, float got, float want) {
    printf("  %-46s got %8.3f  want %8.3f  %s\n",
           what, got, want, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

static void run(Player *p, v3 wish, float speed, int jump, int frames) {
    for (int i = 0; i < frames; i++)
        player_move(p, &L, 0, 0, wish, speed, jump, DT);
}

static void run_past(Player *p, const Blocker *b, int n, v3 wish, float speed,
                     int frames) {
    for (int i = 0; i < frames; i++)
        player_move(p, &L, b, n, wish, speed, 0, DT);
}

static float flat_gap(v3 a, v3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

/* Appends an axis-aligned sector, in centimetres. */
static void box(Level *l, short x0, short z0, short x1, short z1,
                short floor, short ceil) {
    Sector *s = &l->sectors[l->n_sectors++];
    short p[8] = { x0,z0,  x1,z0,  x1,z1,  x0,z1 };
    for (int i = 0; i < 8; i++) s->pts[i] = p[i];
    s->n = 4;
    s->floor = floor;
    s->ceil  = ceil;
}

/* The fixture:  a 24m room at height 0, with
     - a 0.45m platform to the west, low enough to step onto
     - a 2.60m ledge to the north, far too high
   Heights are chosen either side of PLAYER_STEP so the two cases cannot
   accidentally swap places if that constant is retuned. */
static void build(void) {
    Level zero = {0};
    L = zero;
    box(&L, -1200, -1200,  1200,  1200,    0, 600);   /* room     */
    box(&L,  -800,  -600,  -400,  -200,   45, 600);   /* platform */
    box(&L,  -200,   600,   600,  1000,  260, 600);   /* ledge    */
    L.start[0] = 0; L.start[1] = 0; L.start[2] = 0;
}

int main(void) {
    printf("movetest\n\n");
    build();

    const float FLOOR = 0.0f, PLAT = 0.45f, LEDGE = 2.60f;

    check(PLAT < PLAYER_STEP && LEDGE > PLAYER_STEP,
          "fixture: one step in reach, one out of it", PLAYER_STEP, PLAYER_STEP);

    /* --- spawn puts you on the floor, not through it --- */
    {
        Player p = {0};
        player_spawn(&p, &L);
        check(fabsf(p.pos.y - (FLOOR + PLAYER_EYE)) < 0.05f,
              "spawn: standing on the room floor", p.pos.y, FLOOR + PLAYER_EYE);
    }

    /* --- the reported bug: dropping onto a platform must not shove you --- */
    {
        Player p = {0};
        p.pos = v3f(-6.0f, PLAT + PLAYER_EYE + 1.5f, -4.0f);   /* above it */
        float x0 = p.pos.x, z0 = p.pos.z;
        run(&p, v3f(0, 0, 0), 0.0f, 0, 120);                   /* fall, no input */

        float drift = sqrtf((p.pos.x-x0)*(p.pos.x-x0) + (p.pos.z-z0)*(p.pos.z-z0));
        check(drift < 0.001f, "land on platform: no sideways shove", drift, 0.0f);
        check(fabsf(p.pos.y - (PLAT + PLAYER_EYE)) < 0.02f,
              "land on platform: standing on its top", p.pos.y, PLAT + PLAYER_EYE);
        check(p.grounded == 1, "land on platform: grounded", (float)p.grounded, 1.0f);
    }

    /* --- a step within reach is walked up ---
       The height *reached*, not the height at some chosen frame: the platform
       is only 4m deep, so a walk long enough to be sure of arriving is also
       long enough to cross it and step back down the far side. Getting that
       wrong once already cost a false failure. */
    {
        Player p = {0};
        p.pos = v3f(-6.0f, PLAYER_EYE, -8.0f);     /* room floor, south of it */
        float top = p.pos.y;
        for (int i = 0; i < 120; i++) {
            run(&p, v3f(0, 0, 1), PLAYER_WALK, 0, 1);
            if (p.pos.y > top) top = p.pos.y;
        }
        check(fabsf(top - (PLAT + PLAYER_EYE)) < 0.05f,
              "low step: walked up onto it", top, PLAT + PLAYER_EYE);
    }

    /* --- a wall taller than step height stops you --- */
    {
        Player p = {0};
        p.pos = v3f(2.0f, PLAYER_EYE, 2.0f);       /* room, south of the ledge */
        run(&p, v3f(0, 0, 1), PLAYER_WALK, 0, 120);
        check(p.pos.y < PLAYER_EYE + 0.02f,
              "tall ledge: not climbed", p.pos.y, PLAYER_EYE);
        check(p.pos.z < 6.0f - PLAYER_RADIUS + 0.05f,
              "tall ledge: stopped outside it", p.pos.z, 6.0f - PLAYER_RADIUS);
    }

    /* --- you cannot walk out of the map --- */
    {
        Player p = {0};
        player_spawn(&p, &L);
        run(&p, v3f(0, 0, 1), PLAYER_WALK, 0, 240);   /* walk hard at a wall */
        float f, c;
        check(level_ground(&L, p.pos.x, p.pos.z, p.pos.y - PLAYER_EYE, 1e9f, &f, &c) != 0,
              "walk into a wall: still inside the map", 1.0f, 1.0f);
    }

    /* --- a body in the way is a body you stop against ---------------------
     *
     * ::Blocker AND NOT A MONSTER, which is the point of the type and the
     * reason this check lives in movetest at all: the rule is about a cylinder
     * somebody is standing in, and nothing in this file links enemy.c. What
     * fills the array from the bestiary is world.c's, and steptest is where
     * that half is checked -- a synthetic cylinder here would pass forever if
     * the join stopped filling it, and the whole-world check would pass on a
     * rule that only ever refused monsters.
     *
     * *몬스터가 아니라 ::Blocker이며*, 그것이 이 타입의 요점이자 이 검사가 movetest에 있는
     * 이유입니다. 규칙은 누군가 서 있는 원기둥에 대한 것이고, 이 파일의 무엇도 enemy.c를
     * 링크하지 않습니다. 도감으로 그 배열을 채우는 것은 world.c의 몫이며 그 절반은
     * steptest에서 확인합니다. 이곳의 합성 원기둥은 이음매가 채우기를 그만두어도 영원히
     * 통과할 것이고, 세계 전체 검사는 몬스터만 거절하는 규칙에 대해서도 통과할 것입니다. */
    {
        const float R = 0.80f;                  /* about a brute */
        Blocker b = { v3f(0.0f, FLOOR, -4.0f), R, 2.35f };
        float touch = PLAYER_RADIUS + R;

        /* Head on: walk at it from three metres and stop against it. */
        Player p = {0};
        p.pos = v3f(0.0f, FLOOR + PLAYER_EYE, -1.0f);
        run_past(&p, &b, 1, v3f(0, 0, -1), PLAYER_WALK, 60);
        float gap = flat_gap(p.pos, b.pos);
        check(gap >= touch - 0.02f, "walked at a body: stopped against it",
              gap, touch);
        check(gap < touch + 0.25f, "  and got all the way to it", gap, touch);
        /* ON THE NEAR SIDE OF IT. A distance alone cannot tell "stopped
           against it" from "went clean through and kept walking" -- both leave
           the player one touching-distance away, and the second is the bug.
           *그것의 가까운 쪽에서입니다.* 거리만으로는 "그것에 막혀 섰다"와 "통과해서 계속
           걸었다"를 구별할 수 없습니다. 둘 다 플레이어를 닿는 거리만큼 떨어뜨려 놓고,
           두 번째가 버그입니다. */
        check(p.pos.z > b.pos.z, "  on the near side of it, not past it",
              p.pos.z, b.pos.z);

        /* AND THE STOP IS NOT A WALL. Aimed a little off centre the player
           slides round rather than sticking, which is what per-axis rollback
           buys and what a whole-move rollback would lose.
           *그리고 그 멈춤은 벽이 아닙니다.* 중심에서 조금 빗겨 겨누면 플레이어는 들러붙지
           않고 돌아서 미끄러집니다. 축별 되돌림이 사 주는 것이고, 이동 전체를 되돌리면
           잃는 것입니다. */
        Player q = {0};
        q.pos = v3f(0.30f, FLOOR + PLAYER_EYE, -1.0f);
        run_past(&q, &b, 1, v3norm(v3f(0.15f, 0, -1.0f)), PLAYER_WALK, 60);
        check(q.pos.x > 0.30f + touch * 0.5f,
              "  and a glancing approach slides round it", q.pos.x, touch);

        /* Standing inside one -- which happens, because nothing stops a
           monster walking into the player -- must not pin the player there. */
        Player r = {0};
        r.pos = v3f(b.pos.x, FLOOR + PLAYER_EYE, b.pos.z);
        run_past(&r, &b, 1, v3f(0, 0, 1), PLAYER_WALK, 60);
        float out = flat_gap(r.pos, b.pos);
        check(out >= touch, "started inside a body: walked out of it", out, touch);

        /* And one over the player's head is scenery. */
        Blocker air = { v3f(0.0f, FLOOR + PLAYER_EYE + 0.5f, -4.0f), R, 1.9f };
        Player s = {0};
        s.pos = v3f(0.0f, FLOOR + PLAYER_EYE, -1.0f);
        run_past(&s, &air, 1, v3f(0, 0, -1), PLAYER_WALK, 60);
        check(s.pos.z < air.pos.z - 1.0f, "walked under a hovering one",
              s.pos.z, air.pos.z);
    }

    /* --- the shipped level, asked only what any level must satisfy --- */
    if (!level_load("lqdm4", &L)) {
        printf("\n  no level 'arena' to smoke test\n");
    } else {
        printf("\n  smoke testing '%s' (%d sectors)\n", L.name, L.n_sectors);
        Player p = {0};
        player_spawn(&p, &L);
        unsigned rng = 987654321u;
        int escaped = 0, sunk = 0;
        for (int i = 0; i < 4000; i++) {
            rng = rng * 1664525u + 1013904223u;
            float a = (rng >> 8) * (6.2831853f / 16777216.0f);
            run(&p, v3f(cosf(a), 0, sinf(a)), PLAYER_WALK * 1.8f,
                (rng & 0x400000) != 0, 1);

            float f, c;
            if (!level_ground(&L, p.pos.x, p.pos.z, p.pos.y - PLAYER_EYE, 1e9f, &f, &c))
                escaped = 1;
            else if (p.pos.y - PLAYER_EYE < f - 0.05f)
                sunk = 1;
        }
        check(!escaped, "4000 random frames: never left the map", (float)escaped, 0.0f);
        check(!sunk,    "4000 random frames: never sank through a floor",
              (float)sunk, 0.0f);
    }

    /* --- the pull-up: what a jump can get onto ---------------------------
     *
     * ::can_stand REFUSES A HORIZONTAL MOVE onto anything more than
     * ::PLAYER_STEP above the feet, airborne or not, so mounting a shelf meant
     * already being above it at the moment forward was pressed. Measured before
     * the pull-up existed: a jump held into a wall topped out at 1.75m and
     * failed at 2.00m -- and at 2.00m the player did not merely fail to mount,
     * they ended up pushed back off the face. The hook's arrival launch reached
     * 3.00m over the same fixture, which is why this only bit when the hook was
     * not involved. You could be carried up and not climb.
     *
     * THREE CASES, AND THE TWO NEGATIVES ARE THE DESIGN. A rule that only ever
     * says yes is not a rule: what makes this a pull-up rather than flight is
     * that a wall whose top is out of reach stays a wall, and that it takes a
     * jump to begin -- otherwise it is a way to walk up anything.
     *
     * *::can_stand는 공중이든 아니든 발보다 ::PLAYER_STEP 넘게 높은 것으로의 수평 이동을
     * 거절하므로*, 선반에 올라선다는 것은 앞으로 누르는 순간 이미 그 위에 있다는 뜻이었습니다.
     * 끌어올림이 생기기 전 측정: 벽을 향해 누른 점프는 1.75m가 한계이고 2.00m에서 실패했으며,
     * 2.00m에서는 올라서지 못하는 정도가 아니라 벽면에서 뒤로 밀려났습니다. 같은 픽스처에서
     * 훅의 도달 도약은 3.00m에 닿았고, 그래서 이것이 훅이 관여하지 않을 때만 물었습니다.
     * 실려 올라갈 수는 있어도 기어오를 수는 없었습니다.
     * *경우가 셋이고 부정 둘이 곧 설계입니다.* 언제나 예라고만 하는 규칙은 규칙이 아닙니다.
     * 이것을 비행이 아니라 끌어올림으로 만드는 것은, 손 닿는 곳에 꼭대기가 없는 벽은 벽으로
     * 남는다는 것과, 시작하려면 점프가 필요하다는 것입니다. 아니라면 무엇이든 걸어 오르는
     * 방법입니다. */
    {
        struct { float shelf; int jump; int want_on; const char *what; } CASE[] = {
            { 2.50f, 1, 1, "a jump pulls up onto a shelf a jump could not reach" },
            { 3.50f, 1, 0, "and a wall whose top is out of reach stays a wall" },
            { 1.20f, 0, 0, "and standing against one climbs nothing" },
        };

        for (int k = 0; k < 3; k++) {
            Level z = {0};
            L = z;
            box(&L, -2000, -2000, 2000, 2000, 0, 3000);
            box(&L,     0, -2000, 2000, 2000, (short)(CASE[k].shelf * 100), 3000);

            Player p = {0};
            p.pos = v3f(-2.0f, PLAYER_EYE, 0.0f);
            p.grounded = 1;
            for (int i = 0; i < 180; i++)
                player_move(&p, &L, 0, 0, v3f(1, 0, 0), PLAYER_WALK,
                            CASE[k].jump && i == 0, DT);

            float feet = p.pos.y - PLAYER_EYE;
            int on = feet > CASE[k].shelf - 0.1f;
            printf("      %.2fm shelf, %s -> feet at %.2f, %s\n",
                   (double)CASE[k].shelf, CASE[k].jump ? "jumped" : "walked",
                   (double)feet, on ? "on it" : "below it");
            check(on == CASE[k].want_on, CASE[k].what,
                  (float)on, (float)CASE[k].want_on);
        }
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall movement checks passed\n", fails);
    return fails != 0;
}
