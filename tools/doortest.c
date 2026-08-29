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
#include "world.h"   /* WORLD_BOSS_ARENA -- the shipped arena, named once */
/* level_geometry, to look at the texture coordinates a moving door produces.
   CPU side only: mb_init/mb_free need no GL context and only mesh_upload would.
   움직이는 문이 만들어 내는 텍스처 좌표를 보기 위한 level_geometry입니다. CPU 측뿐이며
   mb_init/mb_free는 GL 컨텍스트가 필요 없고 mesh_upload만이 필요로 합니다. */
#include "render.h"

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
/* The `v` of a WALL vertex standing at world height `y`, or a sentinel.
 *
 * The normal is what separates a wall from a cap: a wall's is horizontal and a
 * floor or ceiling's is not. Without that test the door sector's own ceiling
 * cap sits at exactly the height being asked about and answers first, with a
 * `v` derived from x/z that has nothing to do with the question.
 *
 * 월드 높이 `y`에 서 있는 *벽* 정점의 `v`, 또는 감시값입니다.
 * 벽과 캡을 가르는 것은 법선입니다. 벽의 법선은 수평이고 바닥이나 천장의 법선은 그렇지
 * 않습니다. 그 검사가 없으면 문 섹터 자신의 천장 캡이 바로 그 높이에 있어서 먼저 답하며,
 * 그 `v`는 x/z에서 유도된 것이라 질문과 아무 관계가 없습니다. */
static float wall_v_at(const MeshBuf *b, float y) {
    for (int i = 0; i < b->count; i++) {
        const Vtx *vx = &b->v[i];
        if (fabsf(vx->ny) > 0.01f)      continue;   /* a cap, not a wall */
        if (fabsf(vx->py - y) > 0.002f) continue;
        return vx->v;
    }
    return -1e30f;
}

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
        okf(door_openness(&L, 0) >= 0.99f, "and the door reports itself open",
            door_openness(&L, 0), 1.0f);
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
        okf(door_openness(&L, 0) <= 0.01f,
            "a tagged door does not open just because you touched it",
            door_openness(&L, 0), 0.0f);

        /* Give it a switch far from the door, and stand on that instead. */
        Entity *e = &L.ents[L.n_ents++];
        e->kind[0]='s';e->kind[1]='w';e->kind[2]='i';e->kind[3]='t';
        e->kind[4]='c';e->kind[5]='h';e->kind[6]='7';e->kind[7]=0;
        e->x = 1500; e->z = 1500;

        run(4.0f, 15.0f, 15.0f, KEY_NONE);
        okf(door_openness(&L, 0) >= 0.99f, "and opens when its own switch is touched",
            door_openness(&L, 0), 1.0f);
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

        okf(door_openness(&L, 0) >= 0.99f, "switch 7 opens the door tagged 7",
            door_openness(&L, 0), 1.0f);
        okf(door_openness(&L, 1) <= 0.01f, "and leaves the one tagged 9 alone",
            door_openness(&L, 1), 0.0f);
    }

    /* --- a keyed door refuses, and says which key ------------------------ */
    {
        build(DOOR_UP, 400, 0, KEY_RED);
        run(2.0f, 0.0f, 2.6f, KEY_NONE);
        okf(door_openness(&L, 0) <= 0.01f, "a red door refuses without the red key",
            door_openness(&L, 0), 0.0f);
        ok(door_refused(&L) == KEY_RED, "and reports which key it wanted");

        /* The wrong key is no better than none. */
        run(2.0f, 0.0f, 2.6f, KEY_BLUE);
        okf(door_openness(&L, 0) <= 0.01f, "and the blue key does not open it",
            door_openness(&L, 0), 0.0f);

        run(3.0f, 0.0f, 2.6f, KEY_RED);
        okf(door_openness(&L, 0) >= 0.99f, "the red key opens it",
            door_openness(&L, 0), 1.0f);
        ok(door_refused(&L) == KEY_NONE, "and nothing is refused once it is open");
    }

    /* --- the refusal outlives the frame, and then stops -------------------
       door_refused is true for ONE FRAME, which is right for logic and useless
       for a message: at 60Hz nobody reads it. door_notice_key is the same fact
       held long enough to be printed, and the two ways it can be wrong are
       opposites -- expiring too soon shows the player nothing, never expiring
       leaves a stale line up over a door they have since opened.

       Both are checked here, and the second is the one that would otherwise go
       unnoticed: a notice that never clears looks perfectly correct in every
       screenshot taken while standing at the door.

       door_refused는 *한 프레임* 동안만 참이며, 로직에는 맞지만 메시지로는 쓸모없습니다.
       60Hz에서는 아무도 읽지 못합니다. door_notice_key는 같은 사실을 인쇄할 수 있을 만큼
       오래 붙잡아 둔 것이고, 틀릴 수 있는 두 방향은 서로 반대입니다. 너무 빨리 만료되면
       아무것도 보여 주지 못하고, 만료되지 않으면 이미 열어 버린 문 위에 낡은 문장이
       남습니다. 둘 다 검사하며, 두 번째가 그러지 않으면 놓치는 쪽입니다. 지워지지 않는
       알림은 문 앞에 선 채로 찍은 모든 스크린숏에서 완벽히 정상으로 보입니다. */
    {
        build(DOOR_UP, 400, 0, KEY_RED);
        run(0.5f, 0.0f, 2.6f, KEY_NONE);
        ok(door_notice_key(&L) == KEY_RED,
           "a refusal raises a notice naming the key");

        /* Walked away, so nothing re-arms it. One frame later door_refused has
           already gone quiet while the notice has not.
           멀어졌으므로 아무것도 다시 채우지 않습니다. 한 프레임 뒤 door_refused는 이미
           조용해졌지만 알림은 아직 남아 있습니다. */
        run(1.0f / 60.0f, 0.0f, 40.0f, KEY_NONE);
        ok(door_refused(&L) == KEY_NONE,
           "which survives the frame door_refused itself does not");
        ok(door_notice_key(&L) == KEY_RED, "and still names the key a frame later");

        /* Past its life, still standing well clear. */
        run(DOOR_NOTICE_TIME + 0.2f, 0.0f, 40.0f, KEY_NONE);
        ok(door_notice_key(&L) == KEY_NONE, "and expires once its time is up");
        okf(door_notice_left(&L) == 0.0f, "reporting no time left with it",
            door_notice_left(&L), 0.0f);

        /* Leaning on the door holds it steady rather than letting it blink:
           after far longer than its life, spent entirely against a door that
           keeps refusing, it must still be up.
           문에 계속 붙어 있으면 깜빡이지 않고 유지됩니다. 수명보다 훨씬 오래, 계속 거절하는
           문에 붙어서 보낸 뒤에도 여전히 떠 있어야 합니다. */
        run(DOOR_NOTICE_TIME * 3.0f, 0.0f, 2.6f, KEY_NONE);
        ok(door_notice_key(&L) == KEY_RED,
           "and leaning on a locked door keeps it up rather than blinking");
    }

    /* --- the HUD only draws a keycard row where one means something ------- */
    {
        build(DOOR_UP, 400, 0, KEY_RED);
        ok(door_keys_used(&L) == KEY_RED, "a level's demanded keys are reported");

        build(DOOR_UP, 400, 0, KEY_NONE);
        ok(door_keys_used(&L) == KEY_NONE,
           "and a level with no locked door demands none");
    }

    /* --- every key bit has a name ----------------------------------------
       The _Static_assert in door.c ties the table's LENGTH to KEY_KINDS, which
       catches a missing row and not a blank one. This catches the blank.
       door.c의 _Static_assert는 표의 *길이*를 KEY_KINDS에 묶으며, 빠진 행은 잡지만 빈
       행은 잡지 못합니다. 이것이 빈 행을 잡습니다. */
    {
        int named = 0;
        for (int i = 0; i < KEY_KINDS; i++)
            if (door_key_name(1 << i)[0]) named++;
        ok(named == KEY_KINDS, "every key bit has a printable name");
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

    /* --- the texture rides the door up ------------------------------------
       Reported as: the door goes up, but it looks like it is being erased from
       the bottom rather than rising. The geometry was right and the texture was
       not -- `v` was anchored to world height, so the quad's bottom edge climbed
       with the ceiling while the texture stayed pinned in space.

       THE CLAIM, stated so it can fail: the BOTTOM EDGE of the leaf is the same
       part of the door wherever the door is, so it keeps its texture
       coordinate. Anything else means the texture is sliding against the thing
       it is painted on.

       신고 내용: 문이 올라가는데, 올라가는 것이 아니라 아래에서 지워지는 것처럼 보인다.
       기하는 맞고 텍스처가 틀렸습니다. `v`가 월드 높이에 고정되어 있어, 사각형의 아래
       모서리는 천장을 따라 올라가는데 텍스처는 공간에 박혀 있었습니다.

       실패할 수 있는 형태로 진술한 주장: 문짝의 *아래 모서리*는 문이 어디에 있든 문의 같은
       부분이므로 자기 텍스처 좌표를 유지합니다. 그렇지 않다면 텍스처가 자신이 칠해진 대상
       위에서 미끄러지고 있는 것입니다. */
    printf("\ntexture on a rising door\n");
    {
        Level zero = {0};
        L = zero;

        Sector *room = &L.sectors[L.n_sectors++];
        short rp[8] = { -2000, -2000,  2000, -2000,  2000, 2000,  -2000, 2000 };
        for (int i = 0; i < 8; i++) room->pts[i] = rp[i];
        room->n = 4; room->floor = 0; room->ceil = 600;
        level_bounds(room);

        /* Declared SECOND, so it owns the step between the two -- see
           level_edge_spans. A closed ceiling at 20cm is too low to walk under
           and, more to the point here, is a height nothing else in the fixture
           shares.
           *두 번째로* 선언하므로 둘 사이의 단차를 이 섹터가 소유합니다. level_edge_spans를
           참조하십시오. 20cm의 닫힌 천장은 지나갈 수 없을 만큼 낮으며, 이곳에서 더 중요하게는
           픽스처의 다른 무엇도 공유하지 않는 높이입니다. */
        Sector *d = &L.sectors[L.n_sectors++];
        short dp[8] = { -200, -200,  200, -200,  200, 200,  -200, 200 };
        for (int i = 0; i < 8; i++) d->pts[i] = dp[i];
        d->n = 4; d->floor = 0; d->ceil = 20;
        level_bounds(d);

        DoorDef *def = &L.doors[L.n_doors++];
        def->sector = 1; def->axis = DOOR_UP;
        def->amount = 300; def->speed = 400;

        level_grid_build(&L);
        door_reset(&L);

        MeshBuf b;
        mb_init(&b, 1 << 15);

        level_geometry(&b, &L, 0, 0);
        float v_closed = wall_v_at(&b, 0.20f);
        ok(v_closed > -1e29f, "the closed leaf has a bottom edge to look at");

        /* 0.6 m from the door's edge, the way every other case here stands.
           dist_to_outline measures to the nearest EDGE, so standing at the
           centre of a 4 m outline is 2 m away from it and does not touch --
           which is how the first draft of this case watched a door that never
           moved and passed every texture assertion vacuously.
           다른 모든 케이스가 서는 방식대로 문 모서리에서 0.6m입니다. dist_to_outline은 가장
           가까운 *모서리*까지를 재므로, 4m 외곽선의 한가운데에 서는 것은 그것에서 2m 떨어진
           것이고 접촉이 아닙니다. 이 케이스의 초안이 결코 움직이지 않는 문을 지켜보면서 모든
           텍스처 단언을 공허하게 통과시킨 경위입니다. */
        run(0.35f, 0.0f, 2.6f, KEY_NONE);

        float lifted = door_openness(&L, 0) * 300.0f * 0.01f;   /* metres */
        ok(lifted > 0.05f && door_openness(&L, 0) < 0.999f,
           "the door is part way up, which is where the bug is visible");

        mb_reset(&b);
        level_geometry(&b, &L, 0, 0);
        float v_open = wall_v_at(&b, 0.20f + lifted);
        ok(v_open > -1e29f, "and the leaf's bottom edge has risen with it");

        okf(fabsf(v_open - v_closed) < 0.002f,
            "the leaf keeps its texture coordinate as it rises",
            v_open, v_closed);

        /* --- and it holds for the WHOLE travel, not one instant ------------
           Reported as "the uv does go up, but not in step with the animation",
           which a single sample cannot tell apart from a texture that moves at
           the wrong rate or in jumps. Sampled every few frames across the rest
           of the opening: the bottom edge's coordinate has to be the same
           number at every one of them.
           "uv가 올라가긴 하는데 애니메이션과 맞지 않는다"는 신고였고, 표본 하나로는 그것을
           엉뚱한 속도로 움직이거나 튀는 텍스처와 구별할 수 없습니다. 열리는 나머지 구간에서
           몇 프레임마다 표본을 뽑습니다. 아래 모서리의 좌표는 그 전부에서 같은 숫자여야
           합니다. */
        int  samples = 0, off_step = 0;
        float worst = 0.0f;
        for (int k = 0; k < 24 && door_openness(&L, 0) < 0.999f; k++) {
            run(0.05f, 0.0f, 2.6f, KEY_NONE);

            float up = door_openness(&L, 0) * 300.0f * 0.01f;
            mb_reset(&b);
            level_geometry(&b, &L, 0, 0);

            float vk = wall_v_at(&b, 0.20f + up);
            if (vk < -1e29f) continue;      /* the leaf has closed up entirely */

            samples++;
            float d = fabsf(vk - v_closed);
            if (d > worst) worst = d;
            if (d > 0.002f) off_step++;
        }
        ok(samples >= 5, "the travel was sampled at several points");
        okf(off_step == 0,
            "and the texture is in step at every one of them",
            (float)off_step, 0.0f);
        printf("  %-58s %d sample(s), worst drift %.4f\n",
               "(across the opening)", samples, worst);

        mb_free(&b);
    }


    /* --- the doors the SHIPPED arena actually carries --------------------
     *
     * Every block above builds its own fixture, which is right: a test that
     * spells a shipped map's coordinates goes red on every edit. This one
     * asks nothing about where the doors ARE. It asks whether a player can
     * open them, which is a property of the level's data and not of its
     * layout, and which no fixture can answer.
     *
     * IT EXISTS BECAUSE THE ANSWER WAS NO. lqdm1's two gates are Quake
     * func_doors named `gate1`, and the only thing that fired that name was a
     * trigger_once -- which import-librequake.py drops, because this engine
     * has no counterpart for it. They arrived TAGGED, and door.c is explicit
     * about what that means: "An untagged door opens to a touch on itself; a
     * tagged one opens only" when something fires its tag. So both doors
     * waited on a switch the conversion had already deleted, and nothing a
     * player could do would move them. The importer frees a name nothing
     * surviving targets; this is what says so from the other end.
     *
     * 위의 모든 블록은 자기 픽스처를 만들며 그것이 옳습니다. 배포되는 맵의 좌표를 적는
     * 검사는 편집할 때마다 빨개집니다. 이 블록은 문이 *어디* 있는지는 아무것도 묻지
     * 않습니다. 플레이어가 그것을 열 수 있는지를 묻고, 그것은 배치가 아니라 레벨 데이터의
     * 성질이며, 어떤 픽스처도 답할 수 없는 질문입니다.
     *
     * *답이 아니오였기 때문에 존재합니다.* lqdm1의 두 문은 `gate1`이라는 이름의 Quake
     * func_door이고 그 이름을 쏘던 것은 trigger_once 하나뿐인데, 이 엔진에 대응물이 없어
     * import-librequake.py가 그것을 버립니다. 둘 다 *태그를 단 채* 도착했고, 변환이 이미
     * 지운 스위치를 기다렸으며, 플레이어가 할 수 있는 어떤 일로도 움직이지 않았습니다. */
    {
        static Level A;
        if (!level_load(WORLD_BOSS_ARENA, &A)) {
            ok(0, "the shipped arena loads");
        } else {
            ok(1, "the shipped arena loads");
            ok(A.n_doors > 0, "and it has doors at all");

            int tagged = 0, vertical = 0;
            for (int i = 0; i < A.n_doors; i++) {
                if (A.doors[i].tag > 0) tagged++;
                if (A.doors[i].axis == DOOR_UP || A.doors[i].axis == DOOR_DOWN)
                    vertical++;
            }
            okf(tagged == 0,
                "none of them waits on a switch that did not cross",
                (float)tagged, 0.0f);
            okf(vertical == 0,
                "and they slide sideways, not up",
                (float)vertical, 0.0f);
        }
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall door checks passed\n", fails);
    return fails != 0;
}
