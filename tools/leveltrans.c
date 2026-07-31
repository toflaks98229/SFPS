/* leveltrans -- the level-transition data, checked headless.
 *
 * The transition itself (rebuild geometry, respawn, carry stats) lives in the
 * frame loop and needs a window, but everything it *depends on* is data: does
 * `next` parse, is the exit where the entity is, does the target level load.
 * Those are what break silently, so those are what get asserted here.
 */

#include <stdio.h>
#include <math.h>
#include "level.h"

/* Layering guard, enforced on every build rather than trusted to review.
 *
 * ENGLISH
 * -------
 * level.h is the root of the simulation half: player.h, enemy.h, pickup.h and
 * weapon.h all include it. It must NOT drag in the GL stack, or a headless
 * movement/AI test ends up depending on the GUI it exists to avoid, and
 * touching render.h forces a rebuild of everything.
 *
 * That is easy to undo by accident -- one `#include "render.h"` added to
 * level.h for convenience restores the whole chain silently, because
 * everything still compiles. These are the sentinels those headers would
 * define, so the mistake becomes a build error here instead.
 *
 * 한국어
 * ------
 * level.h는 시뮬레이션 영역의 뿌리이며 player.h, enemy.h, pickup.h, weapon.h가 모두
 * 이를 포함합니다. 따라서 GL 스택을 끌어들여서는 안 됩니다. 그렇게 되면 헤드리스
 * 이동/AI 테스트가 정작 피하려던 GUI에 의존하게 되고, render.h를 건드릴 때마다 전체
 * 재빌드가 발생합니다.
 *
 * 이 구조는 실수로 되돌리기 쉽습니다. 편의를 위해 level.h에 `#include "render.h"`를
 * 한 줄 추가하는 것만으로 전체 연쇄가 조용히 복원됩니다. 여전히 컴파일이 되기
 * 때문입니다. 아래는 해당 헤더들이 정의하는 표식이며, 이를 통해 그 실수가 이곳에서
 * 빌드 오류로 드러나게 됩니다. */
#if defined(GL_VERSION_1_1) || defined(__gl_h_) || defined(_WINDOWS_)
#error "level.h has started pulling in GL/Win32 again -- see the note in level.h"
#endif

static int fails;
static void ok(int cond, const char *what) {
    printf("  %-54s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Finds the first exit entity's position, in metres. Returns 0 if none. */
static int exit_pos(const Level *l, float *x, float *z) {
    for (int i = 0; i < l->n_ents; i++) {
        const char *k = l->ents[i].kind;
        if (k[0]=='e'&&k[1]=='x'&&k[2]=='i'&&k[3]=='t'&&k[4]==0) {
            *x = l->ents[i].x * 0.01f; *z = l->ents[i].z * 0.01f;
            return 1;
        }
    }
    return 0;
}

int main(void) {
    printf("leveltrans\n\n");

    Level a;
    ok(level_load("arena", &a), "the arena loads");
    ok(a.next[0] != 0, "and names a next level");
    ok(a.next[0]=='v'&&a.next[1]=='a'&&a.next[2]=='u'&&a.next[3]=='l'&&a.next[4]=='t'&&a.next[5]==0,
       "which is 'vault'");

    float ex = 0, ez = 0;
    ok(exit_pos(&a, &ex, &ez), "the arena has an exit entity");
    ok(level_exit_at(&a, ex, ez), "standing on the exit reads as reaching it");
    ok(!level_exit_at(&a, ex + 3.0f, ez + 3.0f),
       "standing 3 m away does not");

    /* The target the exit names must actually exist and be non-empty, or the
       game loads a void. This is the check that catches a typo'd `next`. */
    Level b;
    ok(level_load(a.next, &b), "the level the arena's exit names loads");
    ok(b.n_sectors > 0, "and has geometry");

    /* No exit should sit on top of a spawn point, or you transition the instant
       you arrive and the level is unplayable. */
    {
        float sx = b.start[0] * 0.01f, sz = b.start[1] * 0.01f;
        ok(!level_exit_at(&b, sx, sz), "the target's exit is clear of its start");
    }

    /* --- the terminal level: an exit with no `next` ends the game --------
       main.c decides win-vs-transition on `g_level.next[0]`, which needs no
       window to check -- it is a property of the level data, same as
       everything else here. What main.c does with that fact (set g_won,
       freeze the frame) is exercised by eye, not headlessly: there is no
       simulated Win32 message pump to drive it through. */
    ok(b.next[0] == 0, "the level the arena leads to has no next of its own");
    {
        float vx = 0, vz = 0;
        ok(exit_pos(&b, &vx, &vz), "and it still has its own exit entity");
        ok(level_exit_at(&b, vx, vz),
           "which reads as reached exactly like a transition exit would");
    }

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall transition checks passed\n", fails);
    return fails != 0;
}
