/**
 * @file plat_win32.c
 * @brief The Win32 side of plat.h. Deliberately the only thing in it.
 *
 * ENGLISH
 * -------
 * TO ADD A HOST: write plat_posix.c beside this, implementing the same two
 * functions, and have the build pick one. Nothing else in src/ changes, which
 * is the entire reason these two calls were moved out of render.c and data.c
 * rather than wrapped in an #ifdef where they sat. An #ifdef leaves the
 * platform code in the file; moving it leaves the file.
 *
 * 한국어
 * ------
 * 호스트를 추가하려면: 이 옆에 plat_posix.c를 쓰고 같은 두 함수를 구현한 뒤, 빌드가 하나를
 * 고르게 하십시오. src/의 다른 어떤 것도 바뀌지 않으며, 이 두 호출을 있던 자리에 #ifdef로
 * 감싸는 대신 render.c와 data.c 밖으로 옮긴 이유의 전부가 그것입니다. #ifdef는 플랫폼 코드를
 * 파일 안에 남기고, 옮기는 것은 파일을 떠나게 합니다.
 */
#include "plat.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void plat_fatal(const char *title, const char *detail) {
    /* A dialog, not a printf. The shipped game is built -mwindows and has no
       console, so anything written to stderr goes nowhere and the user sees
       the window disappear without explanation -- the exact failure this
       function exists to prevent. Writing to a stream nobody can read would
       look more portable and defeat the purpose.

       detail is the body and title is the caption: the caption is a category,
       the body is the evidence, and a driver log is what the reader actually
       needs to paste into a search.

       printf가 아니라 대화 상자입니다. 배포되는 게임은 -mwindows로 빌드되어 콘솔이 없으므로
       stderr에 쓴 것은 어디에도 도달하지 않고, 사용자는 설명 없이 창이 사라지는 것을 봅니다.
       그것이 바로 이 함수가 막으려는 실패입니다. 아무도 읽을 수 없는 스트림에 쓰는 것은 더
       이식성 있어 보이면서 목적을 무너뜨립니다.

       본문이 detail이고 제목이 title입니다. 제목은 분류이고 본문은 증거이며, 읽는 사람이
       실제로 검색창에 붙여 넣어야 하는 것은 드라이버 로그이기 때문입니다. */
    MessageBoxA(0, detail, title, MB_ICONERROR);

    /* ExitProcess rather than exit(): there is no reason to run atexit
       handlers or flush anything once the GPU has refused to work, and a
       shutdown path running while the renderer is half-initialised is its own
       way to crash on the way out.
       exit()가 아니라 ExitProcess입니다. GPU가 이미 동작을 거부한 마당에 atexit 핸들러를
       돌리거나 무언가를 플러시할 이유가 없으며, 렌더러가 절반만 초기화된 상태에서 실행되는
       종료 경로는 나가는 길에 죽는 또 다른 방법입니다. */
    ExitProcess(1);
}

int plat_exe_dir(char *out, int cap) {
    if (!out || cap < 1) return 0;
    out[0] = 0;

    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(0, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;

    /* Three walks back, and each one is a separate fact: drop the file name,
       drop the separator that preceded it, then drop the directory the
       executable sits in -- build\. What is left is the tree root the assets
       are beside.
       세 번 거슬러 올라가며 각각이 별개의 사실입니다. 파일 이름을 버리고, 그 앞의 구분자를
       버리고, 실행 파일이 놓인 디렉토리인 build\를 버립니다. 남는 것이 에셋이 나란히 있는
       트리의 루트입니다. */
    while (n > 0 && exe[n - 1] != '\\') n--;
    if (n > 1) n--;
    while (n > 0 && exe[n - 1] != '\\') n--;

    int i = 0;
    while (i < (int)n && i < cap - 1) { out[i] = exe[i]; i++; }
    out[i] = 0;
    return i;
}

unsigned long long plat_file_stamp(const char *path) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return 0;

    /* The two halves of a FILETIME joined into the one number the caller
       compares. 100-nanosecond units, so two saves in the same second are
       still two different tokens.
       FILETIME의 두 절반을 호출자가 비교하는 하나의 수로 합칩니다. 단위가 100나노초이므로
       같은 1초 안의 두 저장도 여전히 서로 다른 토큰입니다. */
    unsigned long long t = ((unsigned long long)fad.ftLastWriteTime.dwHighDateTime << 32)
                         |  (unsigned long long)fad.ftLastWriteTime.dwLowDateTime;

    /* Zero is the caller's "no answer", so a file that somehow stamps as zero
       must not be handed back as one. One tick off is indistinguishable for
       every purpose this serves.
       0은 호출자에게 "답 없음"이므로, 어쩌다 0으로 찍히는 파일을 그대로 돌려주어서는 안
       됩니다. 이 함수가 쓰이는 모든 용도에서 1틱 차이는 구분되지 않습니다. */
    return t ? t : 1;
}
