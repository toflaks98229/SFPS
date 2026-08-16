/* leveltrans -- the level-transition data, checked headless.
 *
 * The transition itself (rebuild geometry, respawn, carry stats) lives in the
 * frame loop and needs a window, but everything it *depends on* is data: does
 * `next` parse, is the exit where the entity is, does the target level load.
 * Those are what break silently, so those are what get asserted here.
 *
 * WHAT THIS FILE STOPPED DOING, and why. It used to name the campaign: load
 * "arena", assert its `next` spells "vault", assert vault is terminal. Then a
 * TrenchBroom level was put between them and all three went red -- not because
 * anything was broken, but because the test had memorised an answer instead of
 * checking a property. steptest.c's own header warns about exactly this: a map
 * is a thing somebody edits, and a test that names its contents goes red on
 * every edit.
 *
 * So it walks the chain now, from ::WORLD_START_LEVEL to whatever ends it, and
 * asserts at every hop the things that must be true of ANY campaign. Adding a
 * level, reordering two, or replacing a sector level with a .map changes what
 * this prints and not whether it passes. What still fails it is a typo'd
 * `next`, an exit on a spawn point, a level with no geometry, and a chain that
 * loops -- which is the list it was always trying to be.
 *
 * 한국어
 * ------
 * 이 파일이 그만둔 일과 그 이유. 이전에는 캠페인을 이름으로 지목했습니다. "arena"를 로드하고,
 * 그 `next`가 "vault"인지 단언하고, vault가 종착인지 단언했습니다. 그러다 TrenchBroom 레벨
 * 하나가 그 사이에 들어가자 셋 다 실패했습니다. 무언가 망가져서가 아니라, 그 테스트가 성질을
 * 검사하는 대신 답을 외우고 있었기 때문입니다. steptest.c의 헤더가 바로 이것을 경고합니다.
 * 맵은 누군가 편집하는 대상이고, 그 내용을 이름으로 지목하는 테스트는 편집할 때마다
 * 빨간불이 됩니다.
 *
 * 그래서 이제는 ::WORLD_START_LEVEL에서 시작해 끝나는 곳까지 체인을 걸으며, 매 구간마다
 * *어떤* 캠페인에든 참이어야 하는 것들을 단언합니다. 레벨을 추가하거나, 둘의 순서를 바꾸거나,
 * 섹터 레벨을 .map으로 교체하는 것은 이 파일이 무엇을 출력하는지를 바꿀 뿐 통과 여부를 바꾸지
 * 않습니다. 여전히 실패시키는 것은 오타 난 `next`, 스폰 지점 위의 출구, 지오메트리가 없는
 * 레벨, 그리고 순환하는 체인입니다. 원래 되려고 했던 목록이 그것입니다.
 */

#include <stdio.h>
#include <math.h>
#include "level.h"
#include "world.h"   /* WORLD_START_LEVEL, WORLD_STAGE_MAX_HOPS: the chain's ends */
#include "txt.h"     /* txt_copy -- the chain is walked by name */

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
 * @note world.h is included above and is still on the simulation side: it
 *       reaches level.h, player.h, weapon.h and run.h, none of which may touch
 *       GL either. If this guard ever fires after a world.h edit, the note in
 *       world.h's header is the one that was broken.
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
 * 빌드 오류로 드러나게 됩니다.
 *
 * @note 위에서 world.h도 포함하며 그것 역시 시뮬레이션 쪽입니다. level.h, player.h,
 *       weapon.h, run.h에 닿으며 그중 무엇도 GL을 건드려서는 안 됩니다. world.h를 수정한 뒤
 *       이 검사가 발동한다면 깨진 것은 world.h 헤더의 그 참고 사항입니다. */
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

/* Has this level any geometry at all?
 *
 * BOTH MODELS, because the answer used to be `n_sectors > 0` and that is the
 * sector model's answer alone. A brush level keeps its solids in ::Level::brushes
 * and has no sectors whatever -- so the moment a .map entered the chain, "and
 * has geometry" failed on a level that is nothing but geometry.
 *
 * 두 모델 모두입니다. 이전의 답은 `n_sectors > 0`이었고 그것은 섹터 모델만의 답입니다.
 * 브러시 레벨은 고체를 ::Level::brushes에 두며 섹터가 전혀 없습니다. 그래서 .map이 체인에
 * 들어온 순간, 지오메트리 그 자체인 레벨에서 "지오메트리가 있다"가 실패했습니다. */
static int has_geometry(const Level *l) {
    return l->n_sectors > 0 || l->brushes != 0;
}

int main(void) {
    printf("leveltrans\n\n");

    char at[64];
    txt_copy(at, sizeof(at), WORLD_START_LEVEL, -1);

    /* Two levels live at once here for the same reason world.c keeps two: the
       one being checked and the one it names. Released at the end of each hop
       so the brush pool -- which has room for exactly two -- is never asked for
       a third. See ::Level::brush_key.
       이곳에서 레벨 둘이 동시에 살아 있는 이유는 world.c가 둘을 두는 이유와 같습니다. 검사
       중인 레벨과 그것이 지목하는 레벨입니다. 구간마다 끝에서 반납하므로, 정확히 둘을 담는
       브러시 풀이 셋을 요구받는 일이 없습니다. ::Level::brush_key를 참조하십시오. */
    static Level cur, nxt;

    int hops = 0, terminal = 0;

    for (; hops < WORLD_STAGE_MAX_HOPS; hops++) {
        char what[128];

        snprintf(what, sizeof(what), "[%d] '%s' loads", hops, at);
        if (!level_load(at, &cur)) { ok(0, what); break; }
        ok(1, what);

        snprintf(what, sizeof(what), "[%d] '%s' has geometry", hops, at);
        ok(has_geometry(&cur), what);

        /* An exit on the spawn point transitions you the instant you arrive,
           which makes the level unplayable and looks like the previous level
           refusing to end.
           스폰 지점 위의 출구는 도착하는 즉시 전환시키므로 레벨을 플레이 불가능하게 만들며,
           이전 레벨이 끝나기를 거부하는 것처럼 보입니다. */
        {
            float sx = cur.start[0] * 0.01f, sz = cur.start[1] * 0.01f;
            snprintf(what, sizeof(what), "[%d] its exit is clear of its start", hops);
            ok(!level_exit_at(&cur, sx, sz), what);
        }

        /* Every level in the chain needs a way out, terminal or not: the last
           one's exit is what shows the win screen.
           체인의 모든 레벨에는 나가는 길이 필요합니다. 종착이든 아니든 마찬가지이며,
           마지막 레벨의 출구가 승리 화면을 띄우는 것입니다. */
        float ex = 0, ez = 0;
        int has_exit = exit_pos(&cur, &ex, &ez);
        snprintf(what, sizeof(what), "[%d] it has an exit entity", hops);
        ok(has_exit, what);
        if (has_exit) {
            snprintf(what, sizeof(what), "[%d] standing on it reads as reached", hops);
            ok(level_exit_at(&cur, ex, ez), what);

            snprintf(what, sizeof(what), "[%d] and 3 m away does not", hops);
            ok(!level_exit_at(&cur, ex + 3.0f, ez + 3.0f), what);
        }

        if (!cur.next[0]) {
            /* The end of the game. main.c decides win-vs-transition on this
               being empty, which needs no window to check.
               게임의 끝입니다. main.c는 이것이 비어 있는지로 승리와 전환을 구분하며, 그
               판단에는 창이 필요 없습니다. */
            printf("      '%s' is terminal -- its exit is the win screen\n", at);
            terminal = 1;
            break;
        }

        /* The target must exist, or the game loads a void. This is the check
           that catches a typo'd `next`.
           대상이 존재해야 하며, 그렇지 않으면 게임이 빈 공간을 로드합니다. 오타 난 `next`를
           잡는 검사가 이것입니다. */
        snprintf(what, sizeof(what), "[%d] the '%s' it names loads", hops, cur.next);
        int found = level_load(cur.next, &nxt);
        ok(found, what);
        if (!found) break;

        txt_copy(at, sizeof(at), cur.next, -1);

        level_release(&cur);
        level_release(&nxt);
    }

    ok(terminal, "the chain reaches an end rather than looping");
    printf("      %d level(s) walked\n", hops + 1);

    level_release(&cur);
    level_release(&nxt);

    printf(fails ? "\n%d FAILURE(S)\n" : "\nall transition checks passed\n", fails);
    return fails != 0;
}
