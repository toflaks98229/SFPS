/**
 * @file plat.h
 * @brief Everything src/ needs from the machine underneath it. The whole list.
 *
 * ENGLISH
 * -------
 * This header exists to be SHORT, and to be the only place a reader has to
 * look to answer "what would porting this cost". Four functions today. The
 * implementation for one host is one file; adding a host is writing that file
 * and nothing else in src/.
 *
 * WHAT DOES NOT BELONG HERE. Not the window, not the input, not the GL
 * context, not the frame loop -- main.c owns all of that and is itself the
 * platform layer for the game. wgl.h is the same idea for creating a context.
 * plat.h is only for things a NON-platform file genuinely cannot do without:
 * a renderer that must tell the user the driver refused, an asset loader that
 * must find where the executable lives, and know whether it has changed since,
 * and a save that must find the one directory a host lets a game write to.
 * The first three used to be Win32 calls sitting in the middle of files that
 * had no other reason to know what OS they were on, and each one alone was
 * enough to pin its whole translation unit to Windows. The fourth was written
 * here from the start rather than inside save.c, for that reason stated ahead
 * of time instead of after.
 *
 * The test that keeps this honest is build.ps1 -Portable, which compiles every .c
 * file in src against a windows.h that is nothing but an #error. Whatever that
 * reports as portable really is; whatever it does not is either listed here as
 * a deliberate platform file, or is work left to do. It is checked rather than
 * claimed because "we kept the core clean" is exactly the kind of statement
 * that is true when written and false a year later.
 *
 * 한국어
 * ------
 * 이 헤더는 *짧기* 위해 존재하며, "이식하면 얼마나 드는가"에 답하려는 사람이 볼 곳을
 * 한 군데로 만들기 위해 존재합니다. 오늘 기준 함수 네 개입니다. 한 호스트에 대한 구현은
 * 파일 하나이며, 호스트를 추가하는 일은 그 파일을 쓰는 것이고 src/의 나머지는 건드리지
 * 않습니다.
 *
 * 여기에 속하지 *않는* 것: 창도, 입력도, GL 컨텍스트도, 프레임 루프도 아닙니다. main.c가
 * 그 전부를 소유하며 게임에 대한 플랫폼 계층 자체입니다. wgl.h는 컨텍스트 생성에 대한 같은
 * 발상입니다. plat.h는 오직 *플랫폼이 아닌* 파일이 정말로 없이는 못 하는 것만을 위한
 * 것입니다. 드라이버가 거부했음을 사용자에게 알려야 하는 렌더러, 실행 파일이 어디 있는지
 * 찾고 그것이 그 뒤로 바뀌었는지 알아야 하는 에셋 로더, 그리고 호스트가 게임에게 쓰기를
 * 허락한 유일한 디렉토리를 찾아야 하는 저장 모듈. 앞의 셋은 자신이 어떤 OS 위에 있는지 알
 * 다른 이유가 없는 파일 한가운데 놓인 Win32 호출이었고, 각각 하나만으로도 번역 단위 전체를
 * Windows에 묶어 두기에 충분했습니다. 넷째는 save.c 안이 아니라 처음부터 이곳에 썼습니다.
 * 같은 이유를 사후가 아니라 사전에 적용한 것입니다.
 *
 * 이를 정직하게 유지하는 검사는 build.ps1 -Portable이며, src의 모든 .c 파일을 #error 하나뿐인
 * windows.h에 대고 컴파일합니다. 그것이 이식 가능하다고 보고하는 것은 실제로 그러하며,
 * 그렇지 않은 것은 여기에 의도적인 플랫폼 파일로 적혀 있거나 아직 남은 작업입니다. 주장이
 * 아니라 검사인 이유는, "핵심은 깨끗하게 유지했다"가 바로 쓸 때는 참이고 일 년 뒤에는
 * 거짓인 종류의 문장이기 때문입니다.
 *
 * @note THE PLATFORM FILES, and why each is one -- the whole list is still four:
 *       plat_win32.c (this header), main.c (window, input, frame loop), gl.c
 *       (WGL context creation, declared in wgl.h), and audio_win32.c (waveOut,
 *       the mixer thread, and the lock, declared in audio_dev.h). Three of the
 *       four have a header of their own naming exactly what they owe the rest
 *       of src, which is what makes the list short enough to be worth reading.
 * @note 플랫폼 파일들과 각각의 이유. 전체 목록은 넷입니다. plat_win32.c (이 헤더), main.c
 *       (창, 입력, 프레임 루프), gl.c (WGL 컨텍스트 생성, wgl.h가 선언), audio_win32.c
 *       (waveOut, 믹서 스레드, 그리고 락. audio_dev.h가 선언). 넷 중 셋이 src의 나머지에
 *       무엇을 빚지고 있는지를 정확히 이름 붙인 자기 헤더를 가지며, 그것이 이 목록을 읽을
 *       가치가 있을 만큼 짧게 유지하는 것입니다.
 */
#ifndef PLAT_H
#define PLAT_H

/**
 * @brief Reports an unrecoverable failure to the user and ends the process.
 *
 * ENGLISH
 * -------
 * For the small class of failure that is not an overflow and not recoverable:
 * the GPU rejected a shader that has always compiled, the driver is not what
 * it claimed to be. Nothing sensible follows, the program has to stop, and the
 * user has to be told something other than the window vanishing.
 *
 * @param[in] title  Short label for what failed, e.g. "shader compile".
 * @param[in] detail The machine's own account of it -- a driver log, usually.
 *                   May be empty but must not be NULL.
 * @note Does not return. Callers need no `return` after it, and writing one is
 *       a hint the caller has misread this.
 * @warning NOT for anything the game can carry on without. A dropped vertex, a
 *          full sound slot, a level that will not fit are ::DIAG, and the
 *          difference is whether the next frame still means anything.
 *
 * 한국어
 * ------
 * @brief 복구 불가능한 실패를 사용자에게 알리고 프로세스를 종료합니다.
 *
 * 초과도 아니고 복구 가능하지도 않은 작은 부류의 실패를 위한 것입니다. GPU가 늘 컴파일되던
 * 셰이더를 거부했다든가, 드라이버가 자신이 주장한 것과 다르다든가. 그 뒤에 이어질 합당한
 * 일이 없고, 프로그램은 멈춰야 하며, 사용자는 창이 사라지는 것 말고 다른 무언가를 들어야
 * 합니다.
 *
 * @param[in] title  실패한 대상을 나타내는 짧은 라벨. 예: "shader compile".
 * @param[in] detail 기계 자신의 설명. 보통 드라이버 로그입니다. 비어 있어도 되지만 NULL이어서는
 *                   안 됩니다.
 * @note 반환하지 않습니다. 호출자는 뒤에 `return`을 둘 필요가 없으며, 두었다면 잘못 읽은
 *       것입니다.
 * @warning 게임이 없이도 계속할 수 있는 것에는 쓰지 *마십시오*. 버려진 정점 하나, 가득 찬
 *          사운드 슬롯, 들어가지 않는 레벨은 ::DIAG의 몫이며, 차이는 다음 프레임이 여전히
 *          의미를 갖는가입니다.
 */
void plat_fatal(const char *title, const char *detail);

/**
 * @brief Writes the directory holding the executable's parent directory.
 *
 * ENGLISH
 * -------
 * Assets live beside the source tree but the executable is built into build\,
 * so a path has to be resolved against the exe's PARENT rather than against
 * the working directory -- which depends on how the game was launched and is
 * therefore not something to build a file path on.
 *
 * @param[out] out Destination, always null-terminated on return.
 * @param[in]  cap Capacity of `out` in bytes. Must be at least 1.
 * @return The number of characters written, not counting the terminator.
 * @note Ends with the platform's separator, so a relative path can be appended
 *       directly with no separator logic at the call site.
 * @note Returns 0 and writes an empty string if the location cannot be
 *       determined, which leaves the caller resolving against the working
 *       directory. That is a worse answer, not a wrong one, and it is better
 *       than refusing to load anything.
 *
 * 한국어
 * ------
 * @brief 실행 파일의 상위 디렉토리 경로를 씁니다.
 *
 * 에셋은 소스 트리 옆에 있지만 실행 파일은 build\에 빌드되므로, 경로는 작업 디렉토리가
 * 아니라 exe의 *부모*를 기준으로 확인해야 합니다. 작업 디렉토리는 게임이 어떻게 실행되었는지에
 * 따라 달라지며, 따라서 파일 경로를 세울 기반이 못 됩니다.
 *
 * @param[out] out 대상 버퍼. 반환 시 항상 널로 종료됩니다.
 * @param[in]  cap `out`의 용량 (바이트). 최소 1 이상이어야 합니다.
 * @return 종료 문자를 제외하고 기록된 문자 수.
 * @note 플랫폼의 구분자로 끝나므로, 호출 지점에서 구분자를 따지지 않고 상대 경로를 바로
 *       이어 붙일 수 있습니다.
 * @note 위치를 알아낼 수 없으면 0을 반환하고 빈 문자열을 씁니다. 그러면 호출자는 작업
 *       디렉토리를 기준으로 해석하게 됩니다. 그것은 틀린 답이 아니라 더 나쁜 답이며, 아무것도
 *       로드하지 않는 것보다는 낫습니다.
 */
int plat_exe_dir(char *out, int cap);

/**
 * @brief Writes the directory this host lets a game keep a save in, creating it.
 *
 * ENGLISH
 * -------
 * NOT beside the executable, and that is the whole reason this is a separate
 * function from ::plat_exe_dir. A game folder is frequently somewhere a user
 * cannot write -- under Program Files, on a read-only share, on the floppy this
 * project is named after -- and a save that silently fails to persist is worse
 * than one that never claimed to. Every host has an answer to "where does an
 * application put a small file that belongs to this user"; only the host knows
 * what it is.
 *
 * @param[out] out Destination, always null-terminated on return.
 * @param[in]  cap Capacity of `out` in bytes. Must be at least 1.
 * @return The number of characters written, not counting the terminator, or 0
 *         when no such directory could be found OR created.
 * @note Ends with the platform's separator, exactly as ::plat_exe_dir does, so
 *       a file name can be appended with no separator logic at the call site.
 * @note CREATES the directory as well as naming it. Splitting those into two
 *       calls would put "and then make it" in every caller, and there is one
 *       caller; a host that has no directories to create simply returns the
 *       name.
 * @note Returns 0 rather than falling back to ::plat_exe_dir itself. The
 *       fallback is a policy about saves and belongs to save.c, which is where
 *       it can be stated once and read by somebody wondering where their file
 *       went. A platform function that quietly answered a different question
 *       than the one asked would be the harder half of that to notice.
 *
 * 한국어
 * ------
 * @brief 이 호스트가 게임에게 저장을 허락하는 디렉토리를 쓰고, 그것을 만듭니다.
 *
 * *실행 파일 옆이 아니며*, 그것이 이 함수가 ::plat_exe_dir과 별개인 이유 전부입니다. 게임
 * 폴더는 사용자가 쓸 수 없는 곳인 경우가 흔합니다. Program Files 아래, 읽기 전용 공유,
 * 그리고 이 프로젝트가 이름을 딴 플로피 위. 그리고 조용히 남지 않는 저장은 애초에 남는다고
 * 말한 적 없는 저장보다 나쁩니다. 모든 호스트는 "이 사용자에게 속한 작은 파일을 애플리케이션이
 * 어디에 두는가"에 대한 답을 가지고 있으며, 그것이 무엇인지는 호스트만 압니다.
 *
 * @param[out] out 대상 버퍼. 반환 시 항상 널로 종료됩니다.
 * @param[in]  cap `out`의 용량 (바이트). 최소 1 이상이어야 합니다.
 * @return 종료 문자를 제외하고 기록된 문자 수. 그런 디렉토리를 찾을 수도 *만들 수도* 없으면 0.
 * @note ::plat_exe_dir과 정확히 같이 플랫폼의 구분자로 끝나므로, 호출 지점에서 구분자를
 *       따지지 않고 파일 이름을 바로 이어 붙일 수 있습니다.
 * @note 이름을 알려 줄 뿐 아니라 디렉토리를 *만듭니다*. 둘을 나누면 "그리고 그것을 만든다"가
 *       모든 호출자에 들어가는데, 호출자는 하나입니다. 만들 디렉토리가 없는 호스트는 그냥
 *       이름을 반환하면 됩니다.
 * @note ::plat_exe_dir로 스스로 되돌아가지 않고 0을 반환합니다. 그 폴백은 저장에 대한
 *       *정책*이며 save.c의 것입니다. 그곳에서 한 번 진술되고, 자기 파일이 어디로 갔는지
 *       궁금한 사람이 읽을 수 있습니다. 물은 것과 다른 질문에 조용히 답하는 플랫폼 함수는 그중
 *       알아채기 어려운 쪽입니다.
 */
int plat_save_dir(char *out, int cap);

/**
 * @brief An opaque token that changes when a file changes.
 *
 * ENGLISH
 * -------
 * Deliberately NOT "the modification time". Callers only ever compare two of
 * these for equality -- nothing in this project wants to know what time it is,
 * only whether the file on disk is still the one already read. Saying so in
 * the type frees each host to answer as precisely as it can: Win32 returns the
 * 100-nanosecond timestamp it already has, while a POSIX version would fold
 * `st_mtim.tv_nsec` and `st_size` in beside `st_mtime`, because `st_mtime`
 * alone counts in whole seconds and would miss a second save inside the same
 * one. An editor that formats on save does exactly that, and the failure looks
 * like the hot reload being broken.
 *
 * @param[in] path File to examine.
 * @return A token that differs whenever the file's contents may have changed,
 *         or 0 if the file could not be examined at all.
 * @note 0 means "no answer", and a caller must treat it as "do not reload"
 *       rather than as a token -- a file being briefly unopenable is normal
 *       while an editor is writing it.
 *
 * 한국어
 * ------
 * @brief 파일이 변경되면 달라지는 불투명한 토큰입니다.
 *
 * 의도적으로 "수정 시각"이 *아닙니다*. 호출자는 오직 두 개를 같은지 비교할 뿐이며, 이
 * 프로젝트의 어떤 것도 지금이 몇 시인지 알고 싶어 하지 않고 디스크의 파일이 이미 읽은 그
 * 파일인지만 알고자 합니다. 그것을 타입에 명시하면 각 호스트가 가능한 한 정밀하게 답할
 * 자유를 얻습니다. Win32는 이미 가지고 있는 100나노초 타임스탬프를 반환하고, POSIX 판이라면
 * `st_mtime` 옆에 `st_mtim.tv_nsec`과 `st_size`를 함께 접어 넣을 것입니다. `st_mtime`만으로는
 * 초 단위로만 세므로 같은 1초 안의 두 번째 저장을 놓치기 때문입니다. 저장 시 포맷하는
 * 에디터가 바로 그렇게 하며, 그 실패는 핫 리로드가 고장 난 것처럼 보입니다.
 *
 * @param[in] path 검사할 파일.
 * @return 파일 내용이 바뀌었을 수 있을 때마다 달라지는 토큰. 파일을 아예 검사할 수 없으면 0.
 * @note 0은 "답 없음"이며, 호출자는 이를 토큰이 아니라 "다시 읽지 말 것"으로 다뤄야 합니다.
 *       에디터가 파일을 쓰는 동안 잠시 열 수 없는 것은 정상입니다.
 */
unsigned long long plat_file_stamp(const char *path);

#endif /* PLAT_H */
