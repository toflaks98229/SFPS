/**
 * @file plat_win32.c
 * @brief The Win32 side of plat.h. Deliberately the only thing in it.
 *
 * ENGLISH
 * -------
 * TO ADD A HOST: write plat_posix.c beside this, implementing the same four
 * functions, and have the build pick one. Nothing else in src/ changes, which
 * is the entire reason these calls were moved out of render.c and data.c
 * rather than wrapped in an #ifdef where they sat. An #ifdef leaves the
 * platform code in the file; moving it leaves the file.
 *
 * 한국어
 * ------
 * 호스트를 추가하려면: 이 옆에 plat_posix.c를 쓰고 같은 네 함수를 구현한 뒤, 빌드가 하나를
 * 고르게 하십시오. src/의 다른 어떤 것도 바뀌지 않으며, 이 호출들을 있던 자리에 #ifdef로
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

int plat_save_dir(char *out, int cap) {
    if (!out || cap < 1) return 0;
    out[0] = 0;

    /* %APPDATA%, read from the environment rather than through
       SHGetFolderPathA. The shell function is the documented answer and it
       costs shell32 in the import table for one string this process is already
       handed at launch -- every Windows since NT has put the same path in the
       environment, and the shipped binary's whole discipline is not paying for
       a library to be told something it was already told.
       %APPDATA%이며, SHGetFolderPathA가 아니라 환경 변수에서 읽습니다. 셸 함수가 문서화된
       답이지만, 이 프로세스가 시작하면서 이미 건네받은 문자열 하나를 위해 임포트 테이블에
       shell32를 치릅니다. NT 이후의 모든 Windows가 같은 경로를 환경에 넣어 주며, 배포되는
       바이너리의 규율 전체가 이미 들은 것을 다시 듣기 위해 라이브러리를 사지 않는 것입니다. */
    char base[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("APPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;

    /* A trailing separator would double when the folder name is appended.
       Roaming\ has never been handed back with one, and a path with `\\` in the
       middle still opens -- so this is here for the day that stops being true
       rather than for a failure anybody has seen.
       끝에 구분자가 있으면 폴더 이름을 붙일 때 두 개가 됩니다. Roaming은 구분자를 달고 온 적이
       없고 가운데 `\\`가 있는 경로도 여전히 열리므로, 이것은 누군가 본 실패가 아니라 그것이
       참이기를 그만두는 날을 위해 있습니다. */
    while (n > 0 && (base[n - 1] == '\\' || base[n - 1] == '/')) n--;

    char dir[MAX_PATH];
    DWORD i = 0;
    while (i < n && i < MAX_PATH - 1) { dir[i] = base[i]; i++; }

    static const char SUB[] = "\\SFPS\\";
    for (int k = 0; SUB[k] && i < MAX_PATH - 1; k++) dir[i++] = SUB[k];
    dir[i] = 0;

    /* Created here, and ALREADY_EXISTS is the normal result rather than a
       failure -- it is what every launch after the first reports. Any other
       error means the directory is not there and will not be, so the caller is
       told nothing was found rather than being handed a path it cannot use.
       이곳에서 만들며, ALREADY_EXISTS는 실패가 아니라 정상 결과입니다. 첫 실행 이후의 모든
       실행이 보고하는 값입니다. 그 밖의 오류는 디렉토리가 없고 앞으로도 없으리라는 뜻이므로,
       호출자는 쓸 수 없는 경로를 건네받는 대신 아무것도 찾지 못했다고 듣습니다. */
    if (!CreateDirectoryA(dir, 0) && GetLastError() != ERROR_ALREADY_EXISTS)
        return 0;

    int w = 0;
    while (w < (int)i && w < cap - 1) { out[w] = dir[w]; w++; }
    out[w] = 0;

    /* A truncated path names a different directory, and creating the file
       there would scatter saves rather than lose one. Emptied as well as
       refused, so a caller that ignores the return still cannot build a path
       out of half a directory -- ::plat_exe_dir's own bargain.
       잘린 경로는 다른 디렉토리를 가리키며, 그곳에 파일을 만드는 것은 저장을 잃는 것이 아니라
       흩뿌리는 일입니다. 거절하면서 비우기도 하므로, 반환값을 무시하는 호출자도 절반짜리
       디렉토리로 경로를 만들 수 없습니다. ::plat_exe_dir 자신의 약속입니다. */
    if (w != (int)i) { out[0] = 0; return 0; }
    return w;
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
