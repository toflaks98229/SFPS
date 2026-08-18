/**
 * @file demo_file.c
 * @brief The two ends of a recording: the command line, and the disk.
 *
 * ENGLISH: Everything here was in main.c, between the window procedure and the
 * frame loop, and it was the part of that file that had grown -- neither a
 * window nor a frame, but the plumbing laid between them. See demo_file.h for
 * why this is portable rather than a fifth platform file.
 *
 * 한국어: 이곳의 모든 것은 main.c의 창 프로시저와 프레임 루프 사이에 있었고, 그 파일에서
 * 실제로 커진 부분이 바로 그것이었습니다. 창도 프레임도 아닌, 그 둘 사이에 놓인 배관입니다.
 * 이것이 다섯 번째 플랫폼 파일이 아니라 이식 가능한 파일인 이유는 demo_file.h를 참조하십시오.
 */

#include "demo_file.h"
#include <stdlib.h>   /* malloc/free: the text buffer, taken and given back per call */
#include <stdio.h>    /* fopen/fread/fwrite: reading a file is standard C. See data.c. */
#include "txt.h"      /* txt_is / txt_copy -- parsing two flags, copying one level name */

/* ------------------------------------------------------------- text buffer */

/**
 * @brief How much text a full recording comes to.
 *
 * ENGLISH
 * -------
 * ::DEMO_MAX_FRAMES lines at 24 bytes, against a measured 16.8 -- a frame that
 * needs every digit of every field is longer than a typical one, and the header
 * is on top.
 *
 * @note Not a resident buffer. It was one, and that was 432KB of `.bss` alive
 *       for the whole process to serve two calls: one at startup and one at
 *       exit. It is heap now, taken and given back inside each. The project's
 *       rule is that a FRAME never allocates, which this keeps -- neither call
 *       is in one.
 *
 * 한국어
 * ------
 * @brief 기록 하나가 가득 찼을 때의 텍스트 크기입니다.
 *
 * ::DEMO_MAX_FRAMES 줄에 줄당 24바이트이며, 실측은 16.8입니다. 모든 필드가 모든 자릿수를 쓰는
 * 프레임은 평범한 프레임보다 길고 그 위에 헤더가 얹힙니다.
 *
 * @note 상주 버퍼가 아닙니다. 이전에는 그러했고, 그것은 호출 두 번(시작 시 하나, 종료 시
 *       하나)을 위해 프로세스 내내 살아 있는 432KB의 `.bss`였습니다. 이제 힙이며 각 호출
 *       안에서 잡고 돌려줍니다. 이 프로젝트의 규칙은 *프레임*이 결코 할당하지 않는다는
 *       것이고 이것은 그 규칙을 지킵니다. 두 호출 중 어느 것도 프레임 안에 있지 않습니다.
 */
#define DEMO_TEXT_BYTES_PER_FRAME 24
#define DEMO_TEXT_HEADER_MAX      256
#define DEMO_TEXT_MAX (DEMO_MAX_FRAMES * DEMO_TEXT_BYTES_PER_FRAME \
                       + DEMO_TEXT_HEADER_MAX)

/* ----------------------------------------------------------------- the disk */

/**
 * @brief Reads the whole of a file into `buf`.
 * @return Bytes read, or 0.
 *
 * @brief 파일 전체를 `buf`로 읽습니다.
 */
static int file_read_all(const char *path, char *buf, int cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    size_t got = fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return (int)got;
}

/**
 * @brief Writes `len` bytes to `path`, replacing whatever was there.
 * @return Non-zero on success.
 *
 * ENGLISH
 * -------
 * @note The `fclose` is CHECKED and folded into the result, unlike the
 *       CloseHandle this replaced. A buffered write reports a full disk when
 *       the buffer is flushed rather than when it is filled, so ignoring the
 *       close is how a recording gets reported as written and is not.
 *
 * 한국어
 * ------
 * @brief `path`에 `len` 바이트를 기록하며 기존 내용을 대체합니다.
 * @return 성공 시 0이 아닌 값.
 *
 * @note 이것이 대체한 CloseHandle과 달리 `fclose`를 *검사하여* 결과에 반영합니다. 버퍼링된
 *       쓰기는 디스크가 가득 찼음을 버퍼를 채울 때가 아니라 *비울 때* 보고하므로, 닫기를
 *       무시하는 것은 기록이 쓰이지 않았는데 쓰였다고 보고되는 방식입니다.
 */
static int file_write_all(const char *path, const char *buf, int len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    size_t put  = fwrite(buf, 1, (size_t)len, f);
    int    shut = (fclose(f) == 0);
    return shut && (int)put == len;
}

/* ------------------------------------------------------------- command line */

void demo_file_parse_cmdline(DemoFile *df, const char *cmd) {
    /* Set before the walk rather than trusting the caller's storage to be
       zeroed: this used to read and write two file-scope statics, which `.bss`
       cleared for free, and a struct handed in from a stack would not be.
       Only these two, not the whole struct -- a ::DemoFile carries a ::Demo and
       clearing that is 144KB of memset to answer a question about two flags.
       순회 전에 설정합니다. 호출자의 저장 공간이 0이라고 믿지 않기 위함입니다. 이전에는 파일
       스코프 static 둘을 읽고 썼으며 `.bss`가 공짜로 지워 주었지만, 스택에서 건네받은
       구조체는 그렇지 않습니다. 구조체 전체가 아니라 이 둘만입니다. ::DemoFile은 ::Demo를
       지니고 있고 그것을 지우는 것은 플래그 두 개에 대한 질문에 답하려고 144KB를 memset하는
       일입니다. */
    df->drive.mode = DEMO_OFF;
    df->path[0]    = 0;

    while (*cmd) {
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        if (!*cmd) break;

        int want = DEMO_OFF;
        if (txt_is(cmd, 7, "-record")) { want = DEMO_RECORD; cmd += 7; }
        else if (txt_is(cmd, 5, "-play")) { want = DEMO_PLAY; cmd += 5; }
        else { while (*cmd && *cmd != ' ' && *cmd != '\t') cmd++; continue; }

        while (*cmd == ' ' || *cmd == '\t') cmd++;

        char quote = 0;
        if (*cmd == '"') { quote = '"'; cmd++; }

        int n = 0;
        while (*cmd && n < DEMO_PATH_CAP - 1 &&
               (quote ? *cmd != quote : (*cmd != ' ' && *cmd != '\t')))
            df->path[n++] = *cmd++;
        df->path[n] = 0;
        if (quote && *cmd == quote) cmd++;

        /* A flag with no path is not a request to record into an empty name.
           Ignored, so the game starts normally rather than failing on a typo.
           경로 없는 플래그는 빈 이름에 기록하라는 요청이 아닙니다. 무시하므로, 오타에
           실패하는 대신 게임이 평소대로 시작합니다. */
        if (n > 0) df->drive.mode = want;
    }
}

/* -------------------------------------------------------------- open, close */

int demo_file_open(DemoFile *df, World *w) {
    if (df->drive.mode == DEMO_PLAY) {
        char *text = malloc(DEMO_TEXT_MAX);
        if (!text) { df->drive.mode = DEMO_OFF; return 1; }

        int len = file_read_all(df->path, text, DEMO_TEXT_MAX);
        int got = len && demo_read(&df->drive.d, text, len);
        free(text);

        if (!got) {
            df->drive.mode = DEMO_OFF;
            return 0;
        }
        /* A recording carries a level name and nothing else, so playback enters
           it exactly as a new game does -- see the load in WinMain. Carrying
           world state is precisely what would make a demo unable to disagree
           with the game. See demo.h.
           기록은 레벨 이름만 나르므로 재생은 새 게임과 정확히 같은 방식으로 진입합니다.
           WinMain의 로드를 참조하십시오. 월드 상태를 나르는 것이야말로 데모가 게임과 어긋날 수
           없게 만드는 바로 그것입니다. demo.h를 참조하십시오. */
        txt_copy(w->cur_level, sizeof(w->cur_level), df->drive.d.level, -1);
        df->drive.frame = 0;

    } else if (df->drive.mode == DEMO_RECORD) {
        demo_begin(&df->drive.d, w->cur_level);
    }
    return 1;
}

int demo_file_close(const DemoFile *df) {
    if (df->drive.mode != DEMO_RECORD || df->drive.d.n <= 0) return 1;

    char *text = malloc(DEMO_TEXT_MAX);
    if (!text) return 0;

    int len = demo_write(&df->drive.d, text, DEMO_TEXT_MAX);
    int put = len && file_write_all(df->path, text, len);
    free(text);

    return put ? 1 : 0;
}
