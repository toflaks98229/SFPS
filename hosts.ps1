# SFPS -- which src\*.c belong to ONE host. Dot-sourced by build.ps1 and
# build_web.ps1.
#
# A FILE OF ITS OWN because each script has to step over the OTHER host's
# files, so both scripts need both lists. They used to live in build.ps1 alone,
# with build_web.ps1 scraping one out of it by regex and writing the other out
# again by hand -- two copies of the list a new Win32 file has to join, and
# missing the second copy fails only under emcc, with windows.h errors that
# look like a toolchain problem.
#
# 한 호스트에만 속하는 src\*.c입니다. build.ps1과 build_web.ps1이 dot-source합니다.
#
# *별도 파일인 이유*는 각 스크립트가 *다른* 호스트의 파일을 넘어가야 하므로 두 스크립트 모두
# 두 목록이 필요하기 때문입니다. 이전에는 build.ps1에만 있었고, build_web.ps1이 하나는 정규식으로
# 긁어 오고 다른 하나는 손으로 다시 적었습니다. 새 Win32 파일이 들어가야 할 목록의 사본이 둘이었고,
# 두 번째 사본을 빠뜨리면 emcc 아래에서만, 툴체인 문제처럼 보이는 windows.h 오류로 실패했습니다.
#
# A file on neither list is compiled by both, and one of them is there for
# that reason alone: host.c holds the frame-loop lines main.c and main_web.c
# used to write out twice. See src\host.h.
#
# 어느 목록에도 없는 파일은 둘 다 컴파일하며, 그중 하나는 오직 그 이유로 그곳에 있습니다.
# host.c는 main.c와 main_web.c가 두 번 적던 프레임 루프의 줄들을 담습니다. src\host.h를
# 참조하십시오.

# The web host's platform files. They live in src/ because that is where a
# platform file lives -- plat_win32.c does -- but they are compiled by emcc and
# nothing else, so every path in build.ps1 that walks src\*.c has to step over
# them.
#
# NAMED ONCE, because there are two such paths and they had drifted apart the
# moment there were. The release build links them into game.exe and fails on a
# missing emscripten.h; -Portable compiles them against a poisoned windows.h and
# reports a regression for a header neither of them mentions. Both failures are
# the same mistake and neither is about portability.
#
# A file added here is invisible to the desktop build. That is the point, and it
# is also the risk: nothing on that side will ever tell you it is broken. What
# checks them is build_web.ps1 and emcc.
#
# 웹 호스트의 플랫폼 파일들입니다. src/에 있는 이유는 플랫폼 파일이 사는 곳이 그곳이기
# 때문이며 plat_win32.c가 그러합니다. 다만 이것들을 컴파일하는 것은 emcc뿐이므로, build.ps1에서
# src\*.c를 훑는 모든 경로가 이것들을 넘어가야 합니다.
#
# *한 번만 이름 붙입니다.* 그런 경로가 둘이고, 둘이 생긴 순간 이미 서로 어긋나 있었기
# 때문입니다. 릴리스 빌드는 이것들을 game.exe에 링크하려다 없는 emscripten.h에서 실패하고,
# -Portable은 독을 탄 windows.h에 대고 컴파일해서는 둘 다 언급한 적 없는 헤더 때문에 후퇴를
# 보고합니다. 두 실패는 같은 실수이며 어느 쪽도 이식성에 관한 것이 아닙니다.
#
# 이곳에 추가된 파일은 데스크톱 빌드에 보이지 않습니다. 그것이 요점이고 동시에 위험입니다. 그쪽의
# 무엇도 그것이 깨졌다고 말해 주지 않습니다. 그것들을 검사하는 것은 build_web.ps1과 emcc입니다.
$webOnly = @('plat_web.c', 'gl_web.c', 'audio_web.c', 'main_web.c')

# The Win32 host's files: built into game.exe and never by emcc, and the only
# files ALLOWED to reach windows.h. build.ps1 -Portable holds that line in both
# directions; the value is the reason each file is on it.
#
# Win32 호스트의 파일들입니다. game.exe에 빌드되고 emcc로는 결코 빌드되지 않으며, windows.h에
# 닿는 것이 *허용된* 유일한 파일들입니다. build.ps1 -Portable이 그 선을 양방향으로 지키며, 값은
# 각 파일이 목록에 있는 이유입니다.
$win32Only = @{
    'main.c'        = 'the platform layer: window, input, frame loop, WinMM start-up'
    'gl.c'          = 'WGL context creation'
    'plat_win32.c'  = 'the Win32 side of plat.h, and meant to be the only one'
    'audio_win32.c' = 'waveOut, the mixer thread, and the lock the voice table needs'
}
