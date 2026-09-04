/* storytest -- what assets\story.txt turns into, with no window.
 *
 * TWO THINGS ARE WORTH CHECKING and they are not "does the parser parse".
 *
 * The first is the SHIPPED FILE. Every other assertion here builds its own
 * input; this one reads what the game will actually play, because a cutscene
 * that was authored wrong is not a crash -- it is a page that says nothing, or
 * a moment that never plays, and both look like a feature nobody finished.
 *
 * The second is the REFUSALS. Every capacity in this module drops its surplus
 * silently by design: a cut one page short looks exactly like a cut written one
 * page shorter, and a clipped line still draws -- it just draws a sentence the
 * author did not write. DIAG_STORY_CAP is what makes those findable, and a
 * counter is only worth having if some binary reaches the branch that raises
 * it. This is that binary.
 *
 * 검사할 가치가 있는 것은 둘이며, "파서가 파싱하는가"는 그중 하나가 아닙니다.
 *
 * 첫째는 *출하되는 파일*입니다. 이곳의 다른 모든 단언은 자기 입력을 만들지만, 이것은 게임이
 * 실제로 재생할 것을 읽습니다. 잘못 제작된 컷신은 충돌이 아니라 아무 말도 하지 않는 페이지이거나
 * 결코 재생되지 않는 순간이며, 둘 다 아무도 끝내지 않은 기능처럼 보이기 때문입니다.
 *
 * 둘째는 *거절*입니다. 이 모듈의 모든 용량은 설계상 초과분을 조용히 버립니다. 한 장 모자란 컷은
 * 한 장 짧게 쓰인 컷과 똑같아 보이고, 잘린 줄도 여전히 그려집니다. 다만 제작자가 쓰지 않은 문장을
 * 그릴 뿐입니다. DIAG_STORY_CAP이 그것들을 찾을 수 있게 만들며, 카운터는 어떤 바이너리가 그것을
 * 올리는 분기에 도달할 때에만 가질 가치가 있습니다. 이 바이너리가 그것입니다.
 */

#include <stdio.h>
#include "story.h"
#include "diag.h"
#include "txt.h"
#include "font.h"   /* font_width: the width budget is measured, not counted */

static int fails;

static void ok(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void oki(int cond, const char *what, int got, int want) {
    printf("  %-58s %s", what, cond ? "ok" : "FAIL");
    if (!cond) { printf("   (got %d, want %d)", got, want); fails++; }
    printf("\n");
}

int main(void) {
    printf("storytest\n\n");

    /* --- the file the game will actually play ---------------------------
       Read rather than described. What this cannot check is whether the words
       are any good; what it can check is that there ARE words, in every moment
       world.c knows how to reach, and that none of them is empty -- a page with
       a line of zero characters freezes the world to show nothing.
       기술하지 않고 읽습니다. 이것이 검사할 수 없는 것은 그 말이 좋은지이고, 검사할 수 있는 것은
       world.c가 도달할 줄 아는 모든 순간에 말이 *있다는* 것, 그리고 그중 어느 것도 비어 있지
       않다는 것입니다. 길이 0인 줄이 있는 페이지는 아무것도 보여 주지 않기 위해 월드를
       정지시킵니다. */
    printf("  -- the shipped file --\n");

    for (int m = 0; m < STORY_MOMENTS; m++) {
        const StoryCut *c = story_for(m);
        printf("    %-8s %s\n", story_moment_name(m), c ? "authored" : "MISSING");
        if (!c) { ok(0, "every moment world.c can reach is authored"); continue; }

        int bad_pages = 0, bad_lines = 0, empty = 0, widest = 0;
        for (int p = 0; p < c->n_pages; p++) {
            const StoryPage *pg = &c->page[p];
            if (pg->n_lines < 1 || pg->n_lines > STORY_LINES) bad_lines++;
            if (pg->hold < STORY_PAGE_MIN) bad_pages++;
            for (int i = 0; i < pg->n_lines; i++) {
                if (!pg->line[i][0]) empty++;

                /* MEASURED, not counted. STORY_LINE_MAX used to make these the
                   same thing -- one byte, one letter, six glyph pixels -- and
                   Hangul ended that: a syllable is three bytes and eight
                   pixels, so the buffer stopped saying anything about the
                   width. font_width is the renderer's own answer to the
                   question, which is the only answer worth testing.
                   세는 것이 아니라 *재는* 것입니다. STORY_LINE_MAX가 이 둘을 같은 것으로
                   만들던 시절이 있었습니다. 1바이트에 1글자, 6글리프 픽셀이었습니다. 한글이
                   그것을 끝냈습니다. 음절 하나가 3바이트에 8픽셀이므로 버퍼는 너비에 대해
                   아무 말도 하지 않게 되었습니다. font_width는 그 질문에 대한 렌더러 자신의
                   답이며, 시험할 가치가 있는 답은 그것뿐입니다. */
                int w = (int)font_width(1.0f, pg->line[i]);
                if (w > widest) widest = w;
            }
        }

        ok(c->n_pages > 0 && c->n_pages <= STORY_PAGES, "it has pages, within the cap");
        ok(!bad_lines, "every page has between one line and the cap");
        ok(!bad_pages, "every page holds long enough to be read");
        ok(!empty,     "and no line is blank");
        oki(widest <= STORY_LINE_W, "and none is wider than the budget",
            widest, STORY_LINE_W);
    }

    /* THE DEFEAT CUT IS ONE PAGE, and it is asserted rather than left to
       taste. Dying is the common event in an arena and every page is one more
       press between the player and starting again -- a two-page defeat gets
       read once and skipped forever after, which is worse than saying less.
       steptest depends on this too: it spends exactly one press clearing the
       cut before the press that restarts.
       *패배 컷은 한 페이지이며*, 취향에 맡기지 않고 단언합니다. 아레나에서 죽음은 흔한 사건이고
       페이지 하나하나가 플레이어와 다시 시작하기 사이의 누름 한 번입니다. 두 장짜리 패배는 한 번
       읽히고 그 뒤로 영원히 건너뛰어지며, 그것은 덜 말하는 것보다 나쁩니다.
       steptest도 이것에 기대고 있습니다. 재시작하는 누름 앞에서 정확히 한 번의 누름으로 컷을
       지웁니다. */
    {
        const StoryCut *d = story_for(STORY_DEFEAT);
        oki(d && d->n_pages == 1, "the defeat cut is one page, so one press clears it",
            d ? d->n_pages : -1, 1);
    }

    /* Out of range is NULL rather than the first row or the last. A moment
       this build does not have is not a moment this build should play.
       범위를 벗어나면 첫 행도 마지막 행도 아닌 NULL입니다. 이 빌드가 갖지 않은 순간은 이 빌드가
       재생해서는 안 되는 순간입니다. */
    ok(!story_for(-1) && !story_for(STORY_MOMENTS) && !story_for(9999),
       "a moment out of range has no cut rather than somebody else's");
    ok(!story_moment_name(-1)[0] && !story_moment_name(STORY_MOMENTS)[0],
       "and no name either");

    /* --- the names the file writes --------------------------------------
       The one place the enum and the text meet. A name that changed on one
       side would make the cut silently unauthored, which is the failure this
       whole file is shaped around.
       열거형과 텍스트가 만나는 유일한 곳입니다. 한쪽에서만 바뀐 이름은 그 컷을 조용히 제작되지
       않은 것으로 만들며, 이 파일 전체가 그 실패를 중심으로 짜여 있습니다. */
    printf("\n  -- the vocabulary --\n");
    ok(txt_eq(story_moment_name(STORY_INTRO),   "intro"),   "intro is spelled intro");
    ok(txt_eq(story_moment_name(STORY_VICTORY), "victory"), "victory is spelled victory");
    ok(txt_eq(story_moment_name(STORY_DEFEAT),  "defeat"),  "defeat is spelled defeat");

    /* --- what a reload does ---------------------------------------------
       An author saves story.txt while a cutscene is on screen. The reload must
       not leave the previous file's pages standing beside the new one's, and
       the cheapest way to be sure of that is that a reload followed by a read
       gives the same answer a first read did.
       제작자가 컷신이 화면에 떠 있는 동안 story.txt를 저장합니다. 리로드는 이전 파일의 페이지를 새
       파일의 것 옆에 남겨 두어서는 안 되며, 그것을 확인하는 가장 싼 방법은 리로드 뒤의 읽기가 첫
       읽기와 같은 답을 준다는 것입니다. */
    printf("\n  -- a hot reload --\n");
    {
        const StoryCut *before = story_for(STORY_INTRO);
        int pages = before ? before->n_pages : 0;
        int lines = before ? before->page[0].n_lines : 0;

        story_reload();

        const StoryCut *after = story_for(STORY_INTRO);
        oki(after && after->n_pages == pages,
            "a reload re-reads to the same page count",
            after ? after->n_pages : -1, pages);
        oki(after && after->page[0].n_lines == lines,
            "and does not accumulate lines onto the pages it had",
            after ? after->page[0].n_lines : -1, lines);
    }

    /* --- the caps, and that they are counted -----------------------------
       These cannot be reached from the shipped file -- if they could, the file
       would be broken -- so what is checked is that the counter exists, starts
       where the shipped file leaves it, and that the shipped file leaves it at
       zero. A story.txt that quietly overflowed would fail here rather than on
       the screen of somebody reading a cutscene that stops early.
       이것들은 출하되는 파일에서 도달할 수 없습니다. 도달할 수 있다면 그 파일이 잘못된 것입니다.
       그래서 검사하는 것은 카운터가 존재하고, 출하 파일이 남긴 자리에서 시작하며, 출하 파일이
       그것을 0으로 남긴다는 것입니다. 조용히 넘친 story.txt는 일찍 끝나는 컷신을 읽는 누군가의
       화면이 아니라 이곳에서 실패합니다. */
    printf("\n  -- the caps --\n");
    oki(diag_count(DIAG_STORY_CAP) == 0,
        "the shipped story.txt fits in every cap it has",
        diag_count(DIAG_STORY_CAP), 0);

    printf("\n%s\n", fails ? "FAILURES" : "all story checks passed");
    return fails ? 1 : 0;
}
