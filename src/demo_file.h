/**
 * @file demo_file.h
 * @brief A recording as a FILE: the command line that names one, and the bytes.
 *
 * ENGLISH
 * -------
 * demo.c owns what a recording MEANS -- which frame comes next, what happens
 * when one runs out, how an ::Input becomes ten bits and three numbers. This
 * owns the other half: the flag that names a path, reading that path into
 * ::demo_read, and writing ::demo_write back out at exit.
 *
 * That division is not new; it is the one main.c's own comment already claimed
 * -- the rules are demo.c's and the file is the platform's. What is new is that
 * the claim is a file boundary rather than a paragraph, so main.c is left
 * holding a window, a clock and a message pump, which is all its header comment
 * ever said it was.
 *
 * PORTABLE, DELIBERATELY. The obvious reading is that touching a disk makes
 * this a platform file, and the previous implementation agreed: it opened
 * recordings with CreateFileA and sized its path buffer with MAX_PATH. Neither
 * was necessary. data.c reached the same fork earlier and answered it -- see
 * the note on its own include of <stdio.h> -- because reading a file is
 * standard C and only LOCATING one is not. So this uses stdio, sizes its path
 * with ::DEMO_PATH_CAP, and stays on the portable side of the line that
 * build.ps1 -Portable draws. The declared platform list is still four.
 *
 * @note REPORTS A FAILURE RATHER THAN SHOWING ONE. A recording that will not
 *       load is not fatal -- the game starts normally, because a demo is not a
 *       reason to refuse to run -- but saying so to the user means a message
 *       box, and that is a window's job. Same division ::menu_take_action and
 *       ::door_update make: the answer comes back as a return value and the
 *       caller decides what to do about it.
 *
 * 한국어
 * ------
 * @brief *파일*로서의 기록. 그것을 지목하는 명령줄과, 바이트입니다.
 *
 * demo.c는 기록이 무엇을 *뜻하는지*를 소유합니다. 다음 프레임이 무엇인지, 기록이 다 떨어지면
 * 어떻게 되는지, ::Input이 어떻게 10비트와 숫자 셋이 되는지입니다. 이 파일은 나머지 절반을
 * 소유합니다. 경로를 지목하는 플래그, 그 경로를 읽어 ::demo_read에 건네는 일, 그리고 종료
 * 시점에 ::demo_write를 다시 내보내는 일입니다.
 *
 * 이 구분은 새롭지 않습니다. main.c 자신의 주석이 이미 주장하던 것입니다. 규칙은 demo.c의
 * 것이고 파일은 플랫폼의 것입니다. 새로운 것은 그 주장이 이제 문단이 아니라 *파일 경계*라는
 * 점이며, 그래서 main.c에는 창과 시계와 메시지 펌프만 남습니다. 그 헤더 주석이 처음부터
 * 자신이라고 말해 온 것이 그것입니다.
 *
 * *의도적으로 이식 가능합니다.* 디스크를 건드리니 플랫폼 파일이라는 것이 자연스러운 독해이고
 * 이전 구현도 그에 동의했습니다. CreateFileA로 기록을 열고 MAX_PATH로 경로 버퍼 크기를
 * 잡았습니다. 둘 다 필요하지 않았습니다. data.c가 같은 갈림길에 먼저 도달해 답을 냈습니다.
 * 그 파일 자신의 <stdio.h> 포함에 붙은 주석을 보십시오. 파일을 *읽는* 것은 표준 C이고, 파일이
 * *어디 있는지 찾는* 것만이 아니기 때문입니다. 그래서 이 파일은 stdio를 쓰고 경로를
 * ::DEMO_PATH_CAP으로 잡으며 build.ps1 -Portable이 긋는 경계에서 이식 가능한 쪽에 남습니다.
 * 선언된 플랫폼 파일 목록은 여전히 넷입니다.
 *
 * @note *실패를 보여 주지 않고 보고합니다.* 불러오지 못하는 기록은 치명적이지 않습니다.
 *       게임은 평소대로 시작하며, 데모는 실행을 거부할 이유가 아니기 때문입니다. 그러나 그것을
 *       사용자에게 *말하는* 것은 메시지 박스를 뜻하고, 그것은 창의 일입니다.
 *       ::menu_take_action과 ::door_update가 하는 것과 같은 구분입니다. 답은 반환값으로
 *       돌아오고 무엇을 할지는 호출자가 정합니다.
 */
#ifndef DEMO_FILE_H
#define DEMO_FILE_H

#include "demo.h"   /* DemoDrive, and World through it */

/**
 * @brief Capacity of ::DemoFile::path, bytes.
 *
 * ENGLISH: Stands in for Win32's MAX_PATH, which is the same reason data.c
 * defines a PATH_CAP of its own: that one macro was the whole of what this code
 * needed windows.h for, and needing it for one number is how a translation unit
 * becomes Windows-only without anybody deciding it should be.
 *
 * 한국어: Win32의 MAX_PATH를 대신합니다. data.c가 자체 PATH_CAP을 정의하는 것과 같은
 * 이유입니다. 이 코드가 windows.h를 필요로 하던 이유의 전부가 그 매크로 하나였으며, 숫자
 * 하나 때문에 그것이 필요하다는 것은 아무도 그렇게 정하지 않았는데 번역 단위가 Windows
 * 전용이 되는 방식입니다.
 */
#define DEMO_PATH_CAP 512

/**
 * @struct DemoFile
 * @brief A recording, and where on disk it came from or is going.
 *
 * ENGLISH
 * -------
 * One struct rather than a ::DemoDrive beside a loose path buffer, for the
 * reason ::RunState and ::EdgeLatch are each one: the two are only ever useful
 * together -- a mode with no path records into nothing, and a path with no mode
 * is never opened -- and a caller that can only be handed both cannot be handed
 * half.
 *
 * @note ::DemoFile::drive is what ::demo_take and ::demo_put want, and they are
 *       called from inside the frame loop rather than from here. That is
 *       deliberate: per-frame is demo.c's, and this file is only the two ends.
 *
 * 한국어
 * ------
 * @brief 기록, 그리고 그것이 디스크의 어디에서 왔거나 어디로 가는지.
 *
 * ::DemoDrive와 헐거운 경로 버퍼를 나란히 두지 않고 하나의 구조체로 만든 것은 ::RunState와
 * ::EdgeLatch가 각각 하나인 이유와 같습니다. 둘은 오직 함께여야 쓸모가 있으며(경로 없는
 * 모드는 아무 데도 기록하지 않고, 모드 없는 경로는 결코 열리지 않습니다) 둘 다 함께만
 * 건네받을 수 있는 호출자는 그중 절반을 건네받을 수 없습니다.
 *
 * @note ::DemoFile::drive는 ::demo_take와 ::demo_put이 원하는 것이며, 그 둘은 이곳이 아니라
 *       프레임 루프 안에서 호출됩니다. 의도적입니다. 프레임 단위는 demo.c의 것이고, 이 파일은
 *       양쪽 끝일 뿐입니다.
 */
typedef struct {
    DemoDrive drive;                /**< The recording and where playback has got to. / 기록과 재생 진행 위치. */
    char      path[DEMO_PATH_CAP];  /**< What the command line named. Empty when neither flag was given. / 명령줄이 지목한 것. 두 플래그가 모두 없으면 비어 있습니다. */
} DemoFile;

/**
 * @brief Pulls -record <file> or -play <file> off the command line.
 *
 * ENGLISH
 * -------
 * @param[out] df  Filled in; ::DemoFile::drive keeps mode ::DEMO_OFF unless a
 *                 flag arrived with a path after it.
 * @param[in]  cmd The raw command line, as WinMain receives it.
 *
 * @note Hand-parsed rather than through CommandLineToArgvW, which lives in
 *       shell32 and would put a DLL on the import table for two flags. The
 *       grammar is one flag and one path, and quotes are honoured because a
 *       path with a space in it is the normal case on Windows.
 * @note A flag with no path is ignored rather than treated as a request to
 *       record into an empty name, so the game starts normally on a typo.
 *
 * 한국어
 * ------
 * @brief 명령줄에서 -record <파일> 또는 -play <파일>을 꺼냅니다.
 * @param[out] df  채워집니다. 뒤에 경로를 동반한 플래그가 도착하지 않으면
 *                 ::DemoFile::drive의 모드는 ::DEMO_OFF로 남습니다.
 * @param[in]  cmd WinMain이 받는 그대로의 명령줄.
 *
 * @note CommandLineToArgvW를 쓰지 않고 직접 파싱합니다. 그것은 shell32에 있으며 플래그 두 개를
 *       위해 임포트 테이블에 DLL을 올리게 됩니다. 문법은 플래그 하나와 경로 하나이며, 공백이 든
 *       경로가 Windows에서는 평범한 경우이므로 따옴표를 존중합니다.
 * @note 경로 없는 플래그는 빈 이름에 기록하라는 요청으로 다루지 않고 무시하므로, 오타에도
 *       게임이 평소대로 시작합니다.
 */
void demo_file_parse_cmdline(DemoFile *df, const char *cmd);

/**
 * @brief Loads a recording to play, or begins one, and says where to start.
 *
 * ENGLISH
 * -------
 * @param[in,out] df The drive and its path.
 * @param[in,out] w  World whose ::World::cur_level a playback overrides.
 * @return 1 when there is nothing to report. 0 when a -play was asked for and
 *         the file could not be read -- in which case the mode has already been
 *         set back to ::DEMO_OFF and the caller need only say so.
 *
 * @note A recording carries a level name and nothing else, so playback enters
 *       it exactly as a new game does. Carrying world state is precisely what
 *       would make a demo unable to disagree with the game. See demo.h.
 *
 * 한국어
 * ------
 * @brief 재생할 기록을 불러오거나 새 기록을 시작하고, 어디서 시작할지 알려 줍니다.
 * @param[in,out] df 드라이브와 그 경로.
 * @param[in,out] w  재생이 ::World::cur_level을 덮어쓸 월드.
 * @return 보고할 것이 없으면 1. -play를 요청받았으나 파일을 읽지 못했으면 0이며, 그 경우
 *         모드는 이미 ::DEMO_OFF로 되돌려져 있으므로 호출자는 그 사실을 말하기만 하면 됩니다.
 *
 * @note 기록은 레벨 이름만 나르므로 재생은 새 게임과 정확히 같은 방식으로 진입합니다. 월드
 *       상태를 나르는 것이야말로 데모가 게임과 어긋날 수 없게 만드는 바로 그것입니다.
 *       demo.h를 참조하십시오.
 */
int demo_file_open(DemoFile *df, World *w);

/**
 * @brief Writes the recording out, if one was being made.
 *
 * ENGLISH
 * -------
 * @param[in] df The drive and its path.
 * @return 1 when there is nothing to report, including when nothing was being
 *         recorded. 0 when a recording existed and could not be written.
 *
 * @note On the way out rather than as it goes: a recording is one file, and
 *       appending every frame would put a disk write in the frame loop to save
 *       a crash that would have taken the world state with it anyway.
 *
 * 한국어
 * ------
 * @brief 기록 중이었다면 그것을 기록해 내보냅니다.
 * @param[in] df 드라이브와 그 경로.
 * @return 보고할 것이 없으면 1이며, 아무것도 기록 중이 아니었던 경우도 포함합니다. 기록이
 *         있었으나 쓰지 못했으면 0.
 *
 * @note 진행하며 쓰지 않고 나갈 때 씁니다. 기록은 파일 하나이고 매 프레임 덧붙이면 프레임
 *       루프에 디스크 쓰기를 두게 되는데, 그것이 구해 낼 크래시는 어차피 월드 상태를 함께
 *       가져갑니다.
 */
int demo_file_close(const DemoFile *df);

#endif /* DEMO_FILE_H */
