/* savetest -- the file that outlives a run, driven with no window.
 *
 * WHAT IS WORTH CHECKING HERE is not "does fopen work". It is the handful of
 * rules that are invisible from inside the running game until they are wrong
 * for a player who is no longer looking: that an unlock survives a restart of
 * the process, that a worse wave does not overwrite a better one, that a
 * missing file is an empty save rather than a refusal, and that a file written
 * by a build with more unlocks than this one still tells this one what it can
 * read. Every one of those is a bug you find a week later with no way back.
 *
 * IT WRITES SOMEWHERE THAT IS NOT THE PLAYER'S. save_init_in exists for this
 * and for nothing else -- a test that used the resolved location would edit
 * the save of whoever ran it, and a test that could not write at all would be
 * asserting about this module with its one side effect removed. The directory
 * comes from plat_exe_dir rather than from the working directory, because a
 * test that only passes when launched from the right folder is a test that
 * fails in CI for a reason that has nothing to do with the code.
 *
 * 이곳에서 검사할 가치가 있는 것은 "fopen이 동작하는가"가 아닙니다. 더 이상 보고 있지 않은
 * 플레이어에게 잘못되기 전까지 실행 중인 게임 안에서 보이지 않는 몇 가지 규칙입니다. 해금이
 * 프로세스 재시작을 견디는가, 더 나쁜 웨이브가 더 좋은 것을 덮어쓰지 않는가, 없는 파일이 거절이
 * 아니라 빈 저장인가, 그리고 이 빌드보다 해금이 많은 빌드가 쓴 파일도 이 빌드에게 읽을 수 있는
 * 것은 말해 주는가. 그 하나하나가 일주일 뒤에 되돌릴 방법 없이 발견되는 결함입니다.
 */

#include <stdio.h>
#include "save.h"
#include "menu.h"   /* MENU_UNLOCK_ENDLESS -- the vocabulary save.c stores and does not own */
#include "plat.h"
#include "txt.h"
#include "diag.h"

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

/* build\, beside the binary that is running. Resolved rather than assumed for
   the reason above, and `build` rather than a temp directory because the tools
   are built into it -- it exists by the time this runs, which a directory this
   test would have to create does not.
   실행 중인 바이너리 옆의 build\입니다. 위의 이유로 가정하지 않고 확인하며, 임시 디렉토리가
   아니라 `build`인 이유는 도구들이 그곳에 빌드되기 때문입니다. 이 테스트가 도는 시점에 이미
   존재하며, 이 테스트가 만들어야 할 디렉토리는 그렇지 않습니다. */
static const char *scratch_dir(void) {
    static char dir[512];
    int n = plat_exe_dir(dir, (int)sizeof(dir));
    txt_copy(dir + n, (int)sizeof(dir) - n, "build\\", -1);
    return dir;
}

/* Puts a literal file where the next save_init_in will read it. The only way to
   check what this module does with input it did not write -- a save from a
   later build, a hand-edited one, a truncated one.
   다음 save_init_in이 읽을 자리에 리터럴 파일을 놓습니다. 이 모듈이 자기가 쓰지 않은 입력을
   어떻게 다루는지 검사할 유일한 방법입니다. 이후 빌드의 저장, 손으로 고친 저장, 잘린 저장. */
static int put_file(const char *body) {
    char path[512];
    int n = txt_copy(path, (int)sizeof(path), scratch_dir(), -1);
    txt_copy(path + n, (int)sizeof(path) - n, SAVE_FILE, -1);

    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs(body, f);
    return fclose(f) == 0;
}

int main(void) {
    printf("savetest\n\n");

    const char *dir = scratch_dir();
    printf("  writing under %s\n\n", dir);

    /* --- a save that is not there ---------------------------------------
       The first launch, and the case a player has exactly once. It must be an
       EMPTY save rather than a failure: refusing to start because there is no
       progress yet would be the game demanding its own history.
       없는 저장이며, 플레이어가 정확히 한 번 겪는 경우인 첫 실행입니다. 거절이 아니라 *빈
       저장*이어야 합니다. 아직 진행이 없다는 이유로 시작을 거부하는 것은 게임이 자기 이력을
       요구하는 일입니다. */
    printf("  -- a first launch --\n");
    ok(put_file(""), "the scratch directory can be written to at all");
    save_init_in(dir);
    save_forget();
    ok(save_unlocks() == 0,      "a fresh save has unlocked nothing");
    oki(save_best_wave() == 0,   "and has no best wave", save_best_wave(), 0);
    ok(!save_unlocked(MENU_UNLOCK_ENDLESS), "so ENDLESS is locked");

    /* A mask of zero is trivially satisfied, which is what every row with no
       requirement asks and what stops menu.c needing a special case for one.
       마스크 0은 자명하게 충족되며, 요구 조건이 없는 모든 행이 묻는 것이자 menu.c가 그것을 위해
       특수한 경우를 둘 필요가 없게 하는 것입니다. */
    ok(save_unlocked(0), "a row that requires nothing is not locked by an empty save");

    /* --- unlocking ------------------------------------------------------- */
    printf("\n  -- unlocking --\n");
    ok(save_unlock(MENU_UNLOCK_ENDLESS) == 1, "the first unlock reports that it was new");
    ok(save_unlocked(MENU_UNLOCK_ENDLESS),    "and it took");
    ok(save_unlock(MENU_UNLOCK_ENDLESS) == 0,
       "the second reports nothing -- so the frame loop may raise it every frame");

    /* --- the best wave --------------------------------------------------- */
    printf("\n  -- the best wave --\n");
    ok(save_note_wave(7) == 1, "a first wave is a new best");
    oki(save_best_wave() == 7, "and is what is kept", save_best_wave(), 7);
    ok(save_note_wave(4) == 0, "a worse wave is refused");
    oki(save_best_wave() == 7, "and does not overwrite the better one",
        save_best_wave(), 7);
    ok(save_note_wave(7) == 0, "equalling the best is not beating it");
    ok(save_note_wave(9) == 1, "a better one is");
    oki(save_best_wave() == 9, "and replaces it", save_best_wave(), 9);

    /* Every level without spawners ends with wave_best at 0, and main.c hands
       that over every frame. Recording it would make the file learn something
       from a corridor.
       스포너가 없는 모든 레벨은 wave_best가 0인 채로 끝나고, main.c는 그것을 매 프레임
       건넵니다. 그것을 기록하면 파일이 복도에서 무언가를 배우게 됩니다. */
    ok(save_note_wave(0) == 0,  "wave 0 is not a result and is not recorded");
    ok(save_note_wave(-3) == 0, "and neither is a negative one");
    oki(save_best_wave() == 9,  "the best survives both", save_best_wave(), 9);

    /* --- across a process ------------------------------------------------
       THE ONE CLAIM THIS MODULE EXISTS FOR. Everything above is arithmetic on
       two ints; this is the line where the ints are on a disk. save_forget
       empties the module without touching the file, so a re-init is the same
       thing a new process does.
       *이 모듈이 존재하는 유일한 주장입니다.* 위의 모든 것은 정수 둘에 대한 산술이고, 이것이 그
       정수들이 디스크에 있는 줄입니다. save_forget은 파일을 건드리지 않고 모듈을 비우므로,
       다시 초기화하는 것은 새 프로세스가 하는 것과 같은 일입니다. */
    printf("\n  -- across a restart --\n");
    save_forget();
    ok(save_unlocks() == 0 && save_best_wave() == 0, "forgetting empties the module");

    save_init_in(dir);
    ok(save_unlocked(MENU_UNLOCK_ENDLESS), "the unlock came back off the disk");
    oki(save_best_wave() == 9, "and so did the best wave", save_best_wave(), 9);

    /* --- a file this build did not write --------------------------------- */
    printf("\n  -- a file from somewhere else --\n");

    /* A later build's save: a third unlock bit and a keyword this one has never
       heard of. It must still be read for what it does understand -- loot.c's
       rule, and the reason a format that refuses what it cannot parse cannot be
       extended without breaking every older build.
       이후 빌드의 저장입니다. 세 번째 해금 비트와 이 빌드가 들어 본 적 없는 키워드입니다. 이해할
       수 있는 것에 대해서는 여전히 읽혀야 합니다. loot.c의 규칙이며, 파싱하지 못하는 것을
       거부하는 형식이 오래된 빌드를 깨뜨리지 않고는 확장될 수 없는 이유입니다. */
    ok(put_file("# from a later build\nv 9\nunlock 7\nwave 41\nmedals 3\n"),
       "a save from a later build can be planted");
    save_init_in(dir);
    oki(save_best_wave() == 41, "its wave is read", save_best_wave(), 41);
    ok(save_unlocked(MENU_UNLOCK_ENDLESS),
       "and a bit this build knows, out of a mask with bits it does not");
    ok(save_unlocks() == 7u, "the unknown bits are kept rather than dropped");

    /* Comments and blank lines are the parser's, not this file's -- but a save
       a player has hand-edited is the shape this has to survive, and the header
       save_write itself emits is a comment.
       주석과 빈 줄은 이 파일이 아니라 파서의 것입니다. 다만 플레이어가 손으로 고친 저장이 이것이
       견뎌야 할 형태이고, save_write 자신이 내보내는 머리글이 주석입니다. */
    ok(put_file("   \n\n# nothing but comments\n#wave 900\n"),
       "a save with no data can be planted");
    save_init_in(dir);
    oki(save_best_wave() == 0, "a commented-out field is not a field",
        save_best_wave(), 0);

    /* A number in a file is not a number this module put there. Negative is the
       one that matters: taken as a mask it would set every bit including the
       ones that do not exist yet, which unlocks a row nothing has been earned
       for.
       파일 속의 숫자는 이 모듈이 넣은 숫자가 아닙니다. 중요한 것은 음수입니다. 마스크로 받아들이면
       아직 존재하지 않는 것을 포함해 모든 비트가 켜지고, 아무것도 얻지 않은 행이 해금됩니다. */
    ok(put_file("unlock -1\nwave -5\n"), "a corrupt save can be planted");
    save_init_in(dir);
    ok(save_unlocks() == 0,    "a negative unlock mask is refused, not widened");
    oki(save_best_wave() == 0, "and so is a negative wave", save_best_wave(), 0);

    /* --- where it lives --------------------------------------------------- */
    printf("\n  -- the path --\n");
    {
        char path[512];
        int n = save_path(path, (int)sizeof(path));
        ok(n > 0, "the save names a path");
        printf("      %s\n", path);

        /* The name is save.h's so that a person hunting for their file and a
           test writing one read the same string.
           이름은 save.h의 것입니다. 자기 파일을 찾는 사람과 그것을 쓰는 테스트가 같은 문자열을
           읽도록 하기 위함입니다. */
        int len = 0; while (path[len]) len++;
        int fl  = 0; while (SAVE_FILE[fl]) fl++;
        ok(len > fl && txt_eq(path + len - fl, SAVE_FILE),
           "and it ends with SAVE_FILE rather than a name spelled twice");
    }

    /* --- a directory that is not there ----------------------------------
       A write that cannot land must SAY SO, and must say so through the one
       channel this project uses for a failure the next frame can survive. An
       unlock that silently did not persist is exactly the bug DIAG_SAVE_IO
       exists to make findable.
       착지할 수 없는 쓰기는 *그렇다고 말해야* 하며, 다음 프레임이 견딜 수 있는 실패에 대해 이
       프로젝트가 쓰는 그 한 경로로 말해야 합니다. 조용히 남지 않은 해금이야말로 DIAG_SAVE_IO가
       찾을 수 있게 만들려고 존재하는 결함입니다. */
    printf("\n  -- a save that cannot land --\n");
    {
        int before = diag_count(DIAG_SAVE_IO);
        save_init_in("this\\directory\\does\\not\\exist\\");
        ok(save_best_wave() == 0, "an unreadable location reads as an empty save");
        ok(!save_write(),         "and a write there fails rather than claiming to work");
        oki(diag_count(DIAG_SAVE_IO) == before + 1,
            "and it is counted, so a lost unlock is findable",
            diag_count(DIAG_SAVE_IO), before + 1);
    }

    /* Left as this build would have written it, so the next run of the suite
       starts from a file rather than from whatever the last assertion planted.
       이 빌드가 썼을 모습으로 남겨 둡니다. 그래야 다음 스위트 실행이 마지막 단언이 심어 둔
       무언가가 아니라 파일에서 시작합니다. */
    save_init_in(dir);
    save_forget();
    save_write();

    printf("\n%s\n", fails ? "FAILURES" : "all ok");
    return fails ? 1 : 0;
}
