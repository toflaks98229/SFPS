/**
 * @file save.c
 * @brief Reads and writes the two-line file described in save.h.
 *
 * ENGLISH
 * -------
 * The same grammar every other authored file in this project uses -- a keyword
 * and its value, whitespace and `#` comments skipped by txt.h, unknown keywords
 * passed over rather than rejected. loot.c states the reason for that last rule
 * and it applies harder here: a save written by a build with a third unlock has
 * to still tell THIS build what its best wave was.
 *
 * WHY NOT A BINARY BLOB. It is eleven bytes of payload; a struct written with
 * fwrite would be smaller by nothing measurable and would trade a file a player
 * can read for a file whose layout depends on the compiler. The one thing this
 * format costs -- a parser -- is thirty lines of txt.h calls that already exist.
 *
 * 한국어
 * ------
 * @brief save.h가 기술하는 두 줄짜리 파일을 읽고 씁니다.
 *
 * 이 프로젝트의 다른 모든 제작 파일이 쓰는 것과 같은 문법입니다. 키워드와 그 값이며, 공백과
 * `#` 주석은 txt.h가 건너뛰고, 알 수 없는 키워드는 거부하지 않고 지나칩니다. 마지막 규칙의
 * 이유는 loot.c가 진술하고 있으며 이곳에서 더 강하게 적용됩니다. 세 번째 해금을 가진 빌드가
 * 쓴 저장도 *이* 빌드에게 최고 웨이브가 얼마였는지는 여전히 말해 주어야 합니다.
 *
 * *바이너리 덩어리가 아닌 이유.* 페이로드가 열한 바이트입니다. fwrite로 쓴 구조체는 잴 수
 * 없을 만큼만 작으면서, 플레이어가 읽을 수 있는 파일을 배치가 컴파일러에 달린 파일과
 * 맞바꿉니다. 이 형식이 치르는 유일한 비용인 파서는 이미 존재하는 txt.h 호출 서른 줄입니다.
 */

#include "save.h"

#include <stdio.h>    /* fopen/fgets/fprintf: this is the module that writes */
#include "plat.h"     /* where a save may live, and where the exe does */
#include "diag.h"
#include "txt.h"

/* --- File-local macros / 파일 지역 매크로 --- */

/* Room for a directory and the file name after it. data.c's PATH_CAP, and for
   its reason: the number no longer means "Windows' limit", it means what this
   program is willing to spend on a path. Spelled again rather than shared
   because data.c's copy only exists in a HOT_RELOAD build and this one has to
   exist in the shipped one.
   디렉토리와 그 뒤의 파일 이름을 담을 공간입니다. data.c의 PATH_CAP이며 이유도 같습니다. 이
   값은 더 이상 Windows의 한계를 뜻하지 않고 이 프로그램이 경로에 쓰기로 한 분량을 뜻합니다.
   공유하지 않고 다시 적은 이유는, data.c의 것이 HOT_RELOAD 빌드에만 존재하는 반면 이것은
   배포 빌드에 존재해야 하기 때문입니다. */
#define SAVE_PATH_CAP 512

/* The longest line the writer can produce, and it is not close: a keyword of
   six characters, a space, and ten digits for the widest `int`.
   기록기가 만들 수 있는 가장 긴 줄이며 여유가 큽니다. 여섯 글자 키워드, 공백, 그리고 가장 넓은
   `int`를 위한 열 자리입니다. */
#define SAVE_LINE_CAP 64

/* --- Static variable definitions / 정적 변수 정의 --- */

/** @brief Full path to ::SAVE_FILE, built once by ::save_init_in. / ::SAVE_FILE의 전체 경로. ::save_init_in이 한 번 만듭니다. */
static char     g_path[SAVE_PATH_CAP];

/** @brief Every unlock bit read or set. Opaque here; see save.h. / 읽거나 설정된 모든 해금 비트. 이곳에서는 불투명합니다. save.h를 참조하십시오. */
static unsigned g_unlocks;

/** @brief The best wave any recorded run reached. / 기록된 어떤 플레이든 도달한 최고 웨이브. */
static int      g_best_wave;

/* --- Static function prototypes / 정적 함수 프로토타입 --- */

static void resolve(const char *dir);
static void parse(const char *text);
static int  read_file(char *out, int cap);

/* --- Static function definitions / 정적 함수 정의 --- */

/**
 * @brief Builds ::g_path from a directory, or from the host's answer.
 *
 * ENGLISH: THE FALLBACK CHAIN, stated once and here. plat.h refuses to hold it
 * -- a platform function that quietly answered a different question than the
 * one asked would be the hard half to notice -- so the policy lives beside the
 * module whose behaviour it describes.
 *
 * 한국어: *폴백 사슬*이며, 한 번, 이곳에서 진술됩니다. plat.h는 그것을 갖기를 거절합니다.
 * 물은 것과 다른 질문에 조용히 답하는 플랫폼 함수는 알아채기 어려운 쪽이기 때문입니다. 그래서
 * 정책은 그것이 기술하는 동작을 가진 모듈 곁에 삽니다.
 */
static void resolve(const char *dir) {
    char base[SAVE_PATH_CAP];
    int n = 0;

    if (dir) {
        n = txt_copy(base, (int)sizeof(base), dir, -1);
    } else {
        n = plat_save_dir(base, (int)sizeof(base));
        /* The tree the game was built in. A developer running out of build\
           gets a save beside the assets rather than none, and on a machine
           where %APPDATA% is not there this is the honest second choice.
           게임이 빌드된 트리입니다. build\에서 실행하는 개발자는 저장이 없는 대신 에셋 옆에
           저장을 갖게 되며, %APPDATA%가 없는 기계에서 이것이 정직한 두 번째 선택입니다. */
        if (!n) n = plat_exe_dir(base, (int)sizeof(base));
    }

    /* Neither answered, so the name alone -- which resolves against the working
       directory. A worse answer, not a wrong one, and the same bargain
       ::plat_exe_dir's own note strikes about returning nothing.
       어느 쪽도 답하지 않았으므로 이름만 씁니다. 작업 디렉토리를 기준으로 해석됩니다. 틀린
       답이 아니라 더 나쁜 답이며, 아무것도 반환하지 않는 것에 대해 ::plat_exe_dir 자신의 참고
       사항이 맺는 것과 같은 거래입니다. */
    if (n <= 0) { base[0] = 0; n = 0; }

    int w = txt_copy(g_path, (int)sizeof(g_path), base, n);
    txt_copy(g_path + w, (int)sizeof(g_path) - w, SAVE_FILE, -1);
}

/**
 * @brief Reads the whole file into `out`, or leaves it empty.
 * @return Non-zero when a file was opened and something was read.
 *
 * 한국어
 * ------
 * @brief 파일 전체를 `out`에 읽거나, 비운 채로 둡니다.
 */
static int read_file(char *out, int cap) {
    if (cap > 0) out[0] = 0;
    if (!g_path[0] || cap < 2) return 0;

    FILE *f = fopen(g_path, "rb");
    if (!f) return 0;

    size_t n = fread(out, 1, (size_t)cap - 1, f);
    fclose(f);
    out[n] = 0;
    return n > 0;
}

/**
 * @brief Applies a save file's keywords, skipping what it does not know.
 *
 * ENGLISH: `v` is read and dropped on the floor. See ::SAVE_VERSION for why it
 * is written at all when nothing reads it yet.
 *
 * 한국어: `v`는 읽어서 버립니다. 아직 아무도 읽지 않는데도 왜 쓰는지는 ::SAVE_VERSION을
 * 참조하십시오.
 */
static void parse(const char *text) {
    const char *p = text;

    for (;;) {
        int len;
        const char *t = txt_token(p, &len);
        if (!t) break;
        p = t + len;

        int ok = 1, v = 0;

        if (txt_is(t, len, "unlock")) {
            p = txt_read_int(p, &v, &ok);
            /* Through an int and then widened, because that is what the writer
               produced: a negative here is a corrupt file rather than a large
               mask, and taking it as one would set every bit including the ones
               that do not exist yet.
               int을 거쳐 넓힙니다. 기록기가 만든 것이 그것이기 때문입니다. 이곳의 음수는 큰
               마스크가 아니라 손상된 파일이며, 그것을 마스크로 받아들이면 아직 존재하지 않는
               것을 포함해 모든 비트가 켜집니다. */
            if (ok && v > 0) g_unlocks = (unsigned)v;
        } else if (txt_is(t, len, "wave")) {
            p = txt_read_int(p, &v, &ok);
            if (ok && v > 0) g_best_wave = v;
        } else if (txt_is(t, len, "v")) {
            p = txt_read_int(p, &v, &ok);
        }
        /* Anything else: skipped, not refused. loot.c's rule. */
    }
}

/* --- Public function definitions / 공개 함수 정의 --- */

void save_init(void) { save_init_in(0); }

void save_init_in(const char *dir) {
    save_forget();
    resolve(dir);

    /* Sized for a file this module could have written, plus room for one a
       later build might. A save longer than this is not a save this build
       wrote, and reading the first half of it is exactly as good as reading
       none: the keywords it understands are all at the front.
       이 모듈이 썼을 법한 파일의 크기에, 이후의 빌드가 쓸 수도 있는 것을 위한 여유를 더한
       값입니다. 이보다 긴 저장은 이 빌드가 쓴 저장이 아니며, 그것의 앞 절반을 읽는 것은 전혀
       읽지 않는 것과 정확히 같은 값어치입니다. 이해하는 키워드는 전부 앞쪽에 있습니다. */
    char text[SAVE_LINE_CAP * 8];
    if (read_file(text, (int)sizeof(text))) parse(text);
}

void save_forget(void) {
    g_unlocks   = 0;
    g_best_wave = 0;
}

int save_path(char *out, int cap) {
    return txt_copy(out, cap, g_path, -1);
}

unsigned save_unlocks(void)          { return g_unlocks; }
int      save_unlocked(unsigned bits) { return (g_unlocks & bits) == bits; }
int      save_best_wave(void)        { return g_best_wave; }

int save_unlock(unsigned bits) {
    unsigned was = g_unlocks;
    g_unlocks |= bits;
    if (g_unlocks == was) return 0;
    save_write();
    return 1;
}

int save_note_wave(int wave) {
    if (wave <= 0 || wave <= g_best_wave) return 0;
    g_best_wave = wave;
    save_write();
    return 1;
}

int save_write(void) {
    if (!g_path[0]) { DIAG(DIAG_SAVE_IO); return 0; }

    /* "wb" rather than "w", so the file has the same bytes on every host this
       is ever built for. A save written with CRLF here and read by a POSIX
       build would still parse -- txt_skip eats '\r' -- but a file whose
       contents depend on which platform wrote it is a file two builds disagree
       about, and this one is meant to be readable by a person as well.
       "w"가 아니라 "wb"이므로, 이 파일은 앞으로 빌드될 모든 호스트에서 같은 바이트를 갖습니다.
       이곳에서 CRLF로 쓰인 저장을 POSIX 빌드가 읽어도 파싱은 됩니다. txt_skip이 '\r'을 먹기
       때문입니다. 그러나 내용이 어느 플랫폼이 썼는지에 달린 파일은 두 빌드가 서로 다르게 보는
       파일이며, 이것은 사람도 읽을 수 있어야 하는 파일입니다. */
    FILE *f = fopen(g_path, "wb");
    if (!f) { DIAG(DIAG_SAVE_IO); return 0; }

    /* A header line, because the first person to find this file will not have
       read save.h. It is a comment, so reading it back costs nothing.
       머리글 한 줄입니다. 이 파일을 처음 발견하는 사람은 save.h를 읽지 않았을 것이기
       때문입니다. 주석이므로 다시 읽는 비용은 0입니다. */
    int n = fprintf(f, "# SFPS -- progress. Delete this file to start over.\n"
                       "v %d\nunlock %u\nwave %d\n",
                    SAVE_VERSION, g_unlocks, g_best_wave);

    /* Both checked. fclose is where a buffered write actually reaches the disk,
       so a full volume reports here and nowhere earlier -- ignoring it is how a
       save that never landed returns success.
       둘 다 검사합니다. 버퍼링된 쓰기가 실제로 디스크에 닿는 곳이 fclose이므로, 가득 찬
       볼륨은 그보다 앞이 아니라 이곳에서 보고됩니다. 그것을 무시하는 것이, 도달한 적 없는
       저장이 성공을 반환하는 방식입니다. */
    int shut = fclose(f);
    if (n <= 0 || shut != 0) { DIAG(DIAG_SAVE_IO); return 0; }
    return 1;
}
