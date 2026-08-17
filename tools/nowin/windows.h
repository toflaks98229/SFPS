/* Not a header. A tripwire.
 *
 * ENGLISH
 * -------
 * build.ps1 -Portable puts this directory FIRST on the include path and then
 * compiles every src/*.c. Any translation unit that reaches windows.h -- its
 * own include, or one four headers deep that nobody remembered -- stops here
 * with a message instead of quietly compiling and being Windows-only forever.
 *
 * The transitive case is the whole reason this exists. Grepping for
 * `#include <windows.h>` finds the files that say so; it does not find the
 * eleven that used to get it through gl.h without a word. A compiler does.
 *
 * This file is never part of a real build. It is only ever reached through
 * -I, and the moment it is reached the answer is already no.
 *
 * 한국어
 * ------
 * 헤더가 아니라 경보선입니다.
 *
 * build.ps1 -Portable은 이 디렉토리를 include 경로 *맨 앞*에 두고 모든 src/*.c를
 * 컴파일합니다. windows.h에 닿는 번역 단위는 -- 자신의 include든, 아무도 기억하지 못하는
 * 네 단계 아래의 헤더를 통해서든 -- 조용히 컴파일되어 영원히 Windows 전용으로 남는 대신
 * 이곳에서 메시지와 함께 멈춥니다.
 *
 * 이 파일이 존재하는 이유의 전부가 그 *전이적* 경우입니다. `#include <windows.h>`를
 * grep하면 그렇게 적어 둔 파일은 찾지만, 아무 말 없이 gl.h를 통해 받아 오던 열한 개는
 * 찾지 못합니다. 컴파일러는 찾습니다.
 *
 * 이 파일은 실제 빌드의 일부가 된 적이 없습니다. 오직 -I를 통해서만 도달하며, 도달한
 * 순간 답은 이미 아니오입니다.
 */
#error "windows.h reached a translation unit that is supposed to be portable -- see tools/nowin/windows.h"
