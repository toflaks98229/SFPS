/* doortest -- drive doors through their whole cycle with no window.
 *
 * A door is a sector that moves, so the thing worth checking is that the
 * MOVEMENT reaches the collision routines: it is not enough that the door's
 * `t` counted up, the sector it names has to actually be somewhere else
 * afterwards. Every assertion below reads the level rather than the door's
 * internal state, because the level is what the player collides with.
 *
 * The failures this is written against:
 *
 *   - a door that animates its own counter and never writes the sector, so it
 *     looks open and is solid
 *   - a slid door whose bounding box stayed behind, so it blocks where it no
 *     longer is
 *   - a keyed door that opens anyway
 *   - a door that closes on the player standing in it
 *   - a switch that opens every door instead of the one it names
 *
 * 문은 움직이는 섹터이므로, 검사할 가치가 있는 것은 그 *움직임*이 충돌 루틴에 도달하는가
 * 입니다. 문의 `t`가 증가한 것만으로는 부족하고, 문이 지목한 섹터가 실제로 다른 곳에 있어야
 * 합니다. 아래의 모든 단언은 문의 내부 상태가 아니라 레벨을 읽습니다. 플레이어가 충돌하는
 * 대상이 레벨이기 때문입니다.
 */

#include <stdio.h>
#include <math.h>
#include "door.h"
#include "player.h"

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void okf(int cond, const char *what, float got, float want) {
    printf("  %-58s %8.2f / %8.2f  %s\n", what, got, want, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static Level L;

/* A room, plus a door sector sitting inside it at `dx` on the x axis. The room
   comes first so the door is declared LAST and therefore wins the
   last-declared rule where the two overlap -- which is what makes the door the
   thing you collide with rather than the floor under it.
   방 다음에 문 섹터를 둡니다. 문이 *마지막에* 선언되어, 겹치는 곳에서 마지막 선언 우선
   규칙에 따라 이깁니다. 그것이 문을 그 아래 바닥이 아니라 충돌 대상으로 만듭니다. */
static void build(int axis, int amount, int tag, int key) {
    Level zero = {0};
    L = zero;

    Sector *room = &L.sectors[L.n_sectors++];
    short rp[8] = { -2000, -2000,  2000, -2000,  2000, 2000,  -2000, 2000 };
    for (int i = 0; i < 8; i++) room->pts[i] = rp[i];
    room->n = 4; room->floor = 0; room->ceil = 600;
    level_bounds(room);

    Sector *d = &L.sectors[L.n_sectors++];
    short dp[8] = { -200, -200,  200, -200,  200, 200,  -200, 200 };
    for (int i = 0; i < 8; i++) d->pts[i] = dp[i];
    d->n = 4;
    /* Closed: ceiling at the floor, so the sector is solid. */
    d->floor = 0;
    d->ceil  = (axis == DOOR_UP) ? 0 : 300;
    level_bounds(d);

    DoorDef *def = &L.doors[L.n_doors++];
    def->sector = 1;
    def->axis   = (short)axis;
    def->amount = (short)amount;
    def->speed  = 400;
    def->tag    = (short)tag;
    def->key    = (short)key;

    level_grid_build(&L);
    door_reset(&L);
}

/* Step the world for `secs`, with the player standing at (x,z) IN METRES.
 *
 * The unit matters and cost this test its first run: the level's points are
 * file units (centimetres) and everything a player position touches is metres,
 * so a door outline written as +/-200 stands at +/-2 m. The first draft stood
 * the player at 300 -- three hundred metres away -- and every touch-triggered
 * assertion failed against a door that was working correctly.
 *
 * 단위가 중요하며 이 테스트의 첫 실행이 그 대가를 치렀습니다. 레벨의 점은 파일 단위
 * (센티미터)이고 플레이어 위치가 닿는 모든 것은 미터이므로, +/-200으로 기록된 문 외곽선은
 * +/-2m에 있습니다. 초안은 플레이어를 300, 즉 300미터 밖에 세웠고, 정상 동작하는 문을
 * 상대로 접촉 관련 단언이 전부 실패했습니다. */
static void run(float secs, float x, float z, int keys) {
    const float DT = 1.0f / 60.0f;
    for (float t = 0; t < secs; t += DT)
        door_update(&L, v3f(x, PLAYER_EYE, z), keys, DT);
}

int main(void) {
    printf("doortest\n\n");

    /* --- a vertical door lifts, and the LEVEL says so -------------------
       The ceiling has to actually be higher afterwards, because that is what
       open_at reads. A door that only moved its own counter would pass a test
       written against door_openness and still be a wall. */
    {
        build(DOOR_UP, 400, 0, KEY_NONE);
        ok(L.sectors[1].ceil == 0, "closed, the door sector is solid");

        run(0.2f, 0.0f, 9.0f, KEY_NONE);        /* 9 m away */
        ok(L.sectors[1].ceil == 0, "and stays shut with nobody near it");

        run(3.0f, 0.0f, 2.6f, KEY_NONE);        /* 0.6 m from its edge */
        okf(L.sectors[1].ceil >= 380, "touching it raises the ceiling",
            (float)L.sectors[1].ceil, 400.0f);
        okf(door_openness(0) >= 0.99f, "and the door reports itself open",
            door_openness(0), 1.0f);
    }

    /* --- and closes again once you leave -------------------------------- */
    {
        run(DOOR_OPEN_TIME + 3.0f, 0.0f, 18.0f, KEY_NONE);
        okf(L.sectors[1].ceil <= 20, "walking away lets it close",
            (float)L.sectors[1].ceil, 0.0f);
    }

    /* --- it will not close on somebody standing in it --------------------
       Being crushed by a door you were standing in is a death with no lesson
       in it. Standing in the doorway holds it open indefinitely. */
    {
        build(DOOR_UP, 400, 0, KEY_NONE);
        run(3.0f, 0.0f, 2.6f, KEY_NONE);             /* open it */
        run(DOOR_OPEN_TIME + 4.0f, 0.0f, 0.0f, KEY_NONE);  /* stand IN it */
        okf(L.sectors[1].ceil >= 380, "a door held open by somebody in it stays open",
            (float)L.sectors[1].ceil, 400.0f);
    }

    /* --- a sliding door moves its outline AND its bounding box -----------
       point_in_sector rejects against the box before it walks the edges, so a
       slid outline whose box stayed put is solid where it no longer is. The
       box is the assertion that catches that. */
    {
        build(DOOR_X, 500, 0, KEY_NONE);
        short x0 = L.sectors[1].pts[0], bx0 = L.sectors[1].min_x;

        run(4.0f, 0.0f, 2.6f, KEY_NONE);

        okf(L.sectors[1].pts[0] >= x0 + 480, "a sliding door moves its points",
            (float)L.sectors[1].pts[0], (float)(x0 + 500));
        okf(L.sectors[1].min_x >= bx0 + 480,
            "and its bounding box moves with them",
            (float)L.sectors[1].min_x, (float)(bx0 + 500));

        /* The place it used to stand is now open, and the place it moved to is
           not. This is the property the whole design exists for: collision
           follows the door without knowing what a door is. */
        int was = level_sector_at(&L, 0.0f, 0.0f);
        ok(was == 0, "where it used to be is now the room, not the door");
    }

    /* --- a tagged door ignores a touch and waits for its switch ---------- */
    {
        build(DOOR_X, 500, 7, KEY_NONE);
        run(3.0f, 0.0f, 2.6f, KEY_NONE);
        okf(door_openness(0) <= 0.01f,
            "a tagged door does not open just because you touched it",
            door_openness(0), 0.0f);

        /* Give it a switch far from the door, and stand on that instead. */
        Entity *e = &L.ents[L.n_ents++];
        e->kind[0]='s';e->kind[1]='w';e->kind[2]='i';e->kind[3]='t';
        e->kind[4]='c';e->kind[5]='h';e->kind[6]='7';e->kind[7]=0;
        e->x = 1500; e->z = 1500;

        run(4.0f, 15.0f, 15.0f, KEY_NONE);
        okf(door_openness(0) >= 0.99f, "and opens when its own switch is touched",
            door_openness(0), 1.0f);
    }

    /* --- a switch opens only the door that names it ---------------------- */
    {
        build(DOOR_X, 500, 7, KEY_NONE);
        /* A second door on another tag, sharing the level. */
        Sector *d2 = &L.sectors[L.n_sectors++];
        short p2[8] = { 900, -200,  1300, -200,  1300, 200,  900, 200 };
        for (int i = 0; i < 8; i++) d2->pts[i] = p2[i];
        d2->n = 4; d2->floor = 0; d2->ceil = 300;
        level_bounds(d2);
        DoorDef *def2 = &L.doors[L.n_doors++];
        def2->sector = 2; def2->axis = DOOR_X; def2->amount = 500;
        def2->speed = 400; def2->tag = 9; def2->key = KEY_NONE;

        Entity *e = &L.ents[L.n_ents++];
        e->kind[0]='s';e->kind[1]='w';e->kind[2]='i';e->kind[3]='t';
        e->kind[4]='c';e->kind[5]='h';e->kind[6]='7';e->kind[7]=0;
        e->x = 1500; e->z = 1500;

        level_grid_build(&L);
        door_reset(&L);
        run(4.0f, 15.0f, 15.0f, KEY_NONE);

        okf(door_openness(0) >= 0.99f, "switch 7 opens the door tagged 7",
            door_openness(0), 1.0f);
        okf(door_openness(1) <= 0.01f, "and leaves the one tagged 9 alone",
            door_openness(1), 0.0f);
    }

    /* --- a keyed door refuses, and says which key ------------------------ */
    {
        build(DOOR_UP, 400, 0, KEY_RED);
        run(2.0f, 0.0f, 2.6f, KEY_NONE);
        okf(door_openness(0) <= 0.01f, "a red door refuses without the red key",
            door_openness(0), 0.0f);
        ok(door_refused() == KEY_RED, "and reports which key it wanted");

        /* The wrong key is no better than none. */
        run(2.0f, 0.0f, 2.6f, KEY_BLUE);
        okf(door_openness(0) <= 0.01f, "and the blue key does not open it",
            door_openness(0), 0.0f);

        run(3.0f, 0.0f, 2.6f, KEY_RED);
        okf(door_openness(0) >= 0.99f, "the red key opens it",
            door_openness(0), 1.0f);
        ok(door_refused() == KEY_NONE, "and nothing is refused once it is open");
    }

    /* --- a door with no def, and an out-of-range sector, are survivable --- */
    {
        Level zero = {0};
        L = zero;
        DoorDef *d = &L.doors[L.n_doors++];
        d->sector = 42;          /* no such sector */
        d->axis = DOOR_UP; d->amount = 400; d->speed = 400;
        door_reset(&L);
        run(1.0f, 0.0f, 0.0f, KEY_NONE);
        ok(1, "a door naming a sector that does not exist is ignored");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall door checks passed\n", fails);
    return fails != 0;
}
