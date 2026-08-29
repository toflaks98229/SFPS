# SFPS - build script
#
# Size discipline lives here. The flags below are the difference between a
# ~250KB binary and a ~40KB one, so treat them as part of the design:
#   -Os                    optimise for size, not speed
#   -ffunction-sections    put each function/object in its own section...
#   -Wl,--gc-sections      ...so the linker can drop everything unreferenced
#   -fno-ident             no "GCC: (GNU) 16.1.0" string in the binary
#   -fno-asynchronous-unwind-tables  no .eh_frame (we never throw)
#   -s                     strip all symbols
#   -mwindows              WinMain entry, no console window
#
# Usage:  .\build.ps1            release build
#         .\build.ps1 -Debug     -O0 -g, console window, symbols kept
#         .\build.ps1 -Run       build then launch
#         .\build.ps1 -Test      build the tools and run every self-checking one

param(
    [switch]$Debug,
    [switch]$Run,
    [switch]$Tools,   # also build tools\*.c
    [switch]$Test,    # build tools\*.c and RUN the self-checking ones; fail on any
    [string]$Tool,    # build and launch just this tool, e.g. -Tool modelview
    [switch]$Portable # check which src\*.c still need windows.h, and nothing else
)

$ErrorActionPreference = 'Stop'

$root    = $PSScriptRoot
$devkit  = Join-Path $root 'tools\w64devkit'
$gcc     = Join-Path $devkit 'bin\gcc.exe'
$outDir  = Join-Path $root 'build'

# Two clearly separate outputs, because they behave differently. A single
# game.exe whose behaviour depended on which build ran last was a trap: edits
# under assets\ show up instantly in game_dev.exe and never in game.exe, which
# only ever reads the copy baked in at compile time.
$exe = Join-Path $outDir $(if ($Debug) { 'game_dev.exe' } else { 'game.exe' })

if (-not (Test-Path $gcc)) {
    throw "Toolchain missing at $gcc. Re-extract w64devkit into tools\."
}

# gcc shells out to `as` and `ld` by name, so the devkit's bin must be on PATH.
# Scoped to this process only -- nothing about the machine is modified.
$devkitBin = Join-Path $devkit 'bin'
if ($env:PATH -notlike "*$devkitBin*") { $env:PATH = "$devkitBin;$env:PATH" }

# ---------------------------------------------------------------- -Portable --
#
# Which translation units still need windows.h, checked by a compiler rather
# than believed. tools\nowin\windows.h is an #error and goes FIRST on the
# include path, so a file that reaches windows.h by any route -- its own
# include, or one four headers deep -- fails to compile and is named here.
#
# The transitive route is the point. Grep finds the files that SAY they include
# windows.h; it does not find the eleven that used to receive it silently
# through gl.h, which is exactly how a codebase stops being portable without
# anyone deciding to.
#
# $platform below is the list of files that are ALLOWED to be Windows-only,
# and it is checked in BOTH directions. A file not on the list that fails is a
# regression. A file ON the list that now passes is a stale list -- somebody
# did the work and left the entry behind, and the next reader would believe
# there is more to port than there is. Both are failures here, for the same
# reason the _Static_asserts elsewhere in this project object to a table that
# has quietly drifted from the enum beside it.
#
# 어떤 번역 단위가 여전히 windows.h를 필요로 하는지, 믿는 대신 컴파일러로 검사합니다.
# tools\nowin\windows.h는 #error이며 include 경로 *맨 앞*에 놓이므로, 어떤 경로로든
# windows.h에 닿는 파일은 컴파일에 실패하고 이곳에 이름이 불립니다.
#
# 요점은 전이적 경로입니다. grep은 windows.h를 포함한다고 *말한* 파일을 찾지만, gl.h를 통해
# 조용히 받아 오던 열한 개는 찾지 못합니다. 코드베이스가 아무도 그렇게 정하지 않았는데도
# 이식성을 잃는 방식이 정확히 그것입니다.
#
# 아래 $platform은 Windows 전용이어도 되는 파일 목록이며 *양방향*으로 검사됩니다. 목록에
# 없는 파일이 실패하면 후퇴입니다. 목록에 *있는* 파일이 이제 통과하면 낡은 목록입니다.
# 누군가 작업을 끝내고 항목을 남겨 둔 것이며, 다음 읽는 사람은 남은 이식 작업이 실제보다
# 많다고 믿게 됩니다. 둘 다 이곳에서는 실패이며, 이 프로젝트의 다른 곳에 있는
# _Static_assert가 옆의 열거형에서 조용히 멀어진 표에 반대하는 것과 같은 이유입니다.
if ($Portable) {
    $platform = @{
        'main.c'        = 'the platform layer: window, input, frame loop, WinMM start-up'
        'gl.c'          = 'WGL context creation'
        'plat_win32.c'  = 'the Win32 side of plat.h, and meant to be the only one'
        'audio_win32.c' = 'waveOut, the mixer thread, and the lock the voice table needs'
    }

    $poison = Join-Path $root 'tools\nowin'
    $bad = @(); $stale = @(); $ok = 0

    # A failing compile is this check's NORMAL result for a platform file, so
    # the script-wide 'Stop' has to stand down here or the first declared
    # platform file aborts the run. $LASTEXITCODE rather than $? for the same
    # reason: in Windows PowerShell a native command that writes to stderr sets
    # $? to false even when it exited 0, which would report every file that
    # merely warned as unportable.
    # 컴파일 실패는 플랫폼 파일에 대해 이 검사의 *정상* 결과이므로, 스크립트 전역의 'Stop'이
    # 이곳에서는 물러나야 합니다. 그러지 않으면 선언된 첫 플랫폼 파일에서 실행이 중단됩니다.
    # $? 대신 $LASTEXITCODE인 것도 같은 이유입니다. Windows PowerShell에서 네이티브 명령이
    # stderr에 쓰면 0으로 종료해도 $?가 false가 되며, 경고만 낸 파일까지 이식 불가로 보고하게
    # 됩니다.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'

    Write-Host "`nPortability check (windows.h poisoned)" -ForegroundColor Cyan
    foreach ($f in (Get-ChildItem (Join-Path $root 'src') -Filter *.c | Sort-Object Name)) {
        # -DHOT_RELOAD because that path holds the file I/O, and it is the half
        # that would otherwise go unchecked -- the release build simply has no
        # file reading in it to be unportable.
        # HOT_RELOAD를 정의하는 이유는 그 경로가 파일 I/O를 담고 있고, 그러지 않으면 검사되지
        # 않을 절반이기 때문입니다. 릴리스 빌드에는 이식성을 잃을 파일 읽기 자체가 없습니다.
        & $gcc '-std=c11' '-fsyntax-only' '-DHOT_RELOAD' '-I' $poison '-I' (Join-Path $root 'src') $f.FullName 2>$null | Out-Null
        $pure = ($LASTEXITCODE -eq 0)

        if ($platform.ContainsKey($f.Name)) {
            if ($pure) {
                $stale += $f.Name
            } else {
                Write-Host ("  {0,-16} platform  ({1})" -f $f.Name, $platform[$f.Name]) -ForegroundColor DarkGray
            }
        } elseif ($pure) {
            $ok++
        } else {
            $bad += $f.Name
        }
    }

    $ErrorActionPreference = $prevEAP
    Write-Host ("`n  {0} portable, {1} declared platform" -f $ok, $platform.Count)

    foreach ($f in $bad) {
        Write-Host ("  REGRESSION  {0} reaches windows.h and is not a declared platform file" -f $f) -ForegroundColor Red
    }
    foreach ($f in $stale) {
        Write-Host ("  STALE LIST  {0} no longer needs windows.h -- remove it from `$platform" -f $f) -ForegroundColor Yellow
    }

    if ($bad.Count -or $stale.Count) {
        throw "Portability check failed: $($bad.Count) regression(s), $($stale.Count) stale entry(ies)"
    }
    Write-Host "  the line holds" -ForegroundColor Green

    # Nothing else to do. The check needs a compiler and the sources, not a
    # bake, a link or a size report, and making it wait for those is how a
    # check stops being run.
    # 더 할 일이 없습니다. 이 검사에 필요한 것은 컴파일러와 소스이지 베이크나 링크나 크기
    # 보고가 아니며, 그것들을 기다리게 만드는 것이 검사가 실행되지 않게 되는 방식입니다.
    exit 0
}
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory $outDir | Out-Null }

# assets\*.txt are the source of truth; bake them into src\gen_assets.h first.
& (Join-Path $root 'bake.ps1')

$sources = Get-ChildItem (Join-Path $root 'src') -Filter *.c | ForEach-Object { $_.FullName }

# Tool-side shared libraries: tools\*.c with no entry point of their own. These
# are linked INTO tools rather than built AS tools, so the -Tools sweep has to
# skip them or it tries to link a binary with no WinMain and fails with an
# error that names the linker rather than the mistake.
#
# Listed by name rather than detected by grepping for WinMain: a build script
# that guesses at the contents of source files is a rule with no way to state
# itself, and adding a library here is one line at the moment you create it.
$toolLibNames = @('ui')

# --- undefined behaviour, made loud in the builds that can afford it -------
#
# TRAP MODE, and not by preference. w64devkit ships no libubsan and no libasan
# -- `-fsanitize=undefined` on its own fails at the link with `cannot find
# -lubsan`, and so does the address sanitizer. What DOES work is
# -fsanitize-undefined-trap-on-error, which emits a `ud2` at each check instead
# of a call into a runtime that is not there. No library, no diagnostic text:
# the process dies at the instruction that was about to do the undefined thing,
# and a debugger names the line.
#
# A crash with no message is a poor error report, and it is still far better
# than what it replaces. This project's number readers accumulated into signed
# ints with nothing bounding the digit count, so `9999999999` in a hand-edited
# level file was signed overflow -- which at -Os is not a wrong answer but a
# licence for the optimiser to assume the loop it sits in cannot run that far.
# Verified rather than assumed: before the fix, txt_to_int on twenty digits
# trapped here.
#
# NOT in the release build. The checks cost size, and the 1.44MB budget is the
# one thing this project cannot trade for anything.
#
# 트랩 모드이며 취향이 아닙니다. w64devkit에는 libubsan도 libasan도 없습니다.
# `-fsanitize=undefined`만 주면 `cannot find -lubsan`으로 링크에 실패하며 주소 새니타이저도
# 마찬가지입니다. 동작하는 것은 -fsanitize-undefined-trap-on-error이며, 검사마다 존재하지
# 않는 런타임 호출 대신 `ud2`를 냅니다. 라이브러리도 진단 문구도 없습니다. 프로세스는
# 정의되지 않은 일을 하려던 바로 그 명령에서 죽고, 디버거가 줄 번호를 알려 줍니다.
#
# 메시지 없는 크래시는 나쁜 오류 보고이며, 그럼에도 그것이 대체하는 것보다는 훨씬 낫습니다.
# 이 프로젝트의 숫자 판독기들은 자릿수를 제한하는 것 없이 부호 있는 int에 누적했으므로, 손으로
# 고친 레벨 파일의 `9999999999`는 부호 있는 오버플로였습니다. -Os에서 그것은 틀린 답이
# 아니라, 그 루프가 그렇게까지 돌 수 없다고 최적화기가 가정해도 된다는 허가입니다.
# 가정하지 않고 확인했습니다. 수정 전에는 스무 자리에 대한 txt_to_int가 이곳에서
# 트랩했습니다.
#
# 릴리스 빌드에는 넣지 않습니다. 검사는 크기를 소모하며, 1.44MB 예산은 이 프로젝트가 무엇과도
# 바꿀 수 없는 유일한 것입니다.
$ubsan = @('-fsanitize=undefined', '-fsanitize-undefined-trap-on-error')
$toolLibs = $toolLibNames | ForEach-Object { Join-Path $root "tools\$_.c" }
foreach ($lib in $toolLibs) {
    if (-not (Test-Path $lib)) { throw "Tool library missing: $lib" }
}

if ($Debug) {
    # DEBUG_HUD adds a live player/weapon state readout in the title bar.
    # HOT_RELOAD reads assets\*.txt at runtime and watches them, so editing a
    # silhouette or a material updates the running game. Both are compiled out
    # entirely in release, so they cost nothing in the shipped exe.
    #
    # WORLD_START_LEVEL USED TO BE HERE and is not any more. It sent the dev
    # build straight into the boss arena because there was no other way in --
    # the menu that lets a player choose story or endless was unwritten, and a
    # fight nothing can reach is a fight nobody can playtest. MENU_TITLE landed,
    # so the mode is chosen there and world_begin loads the arena, which is
    # where "which room is which mode" belongs.
    #
    # The note stays because a scaffold that is removed silently is a scaffold
    # somebody puts back: the next person to want the arena on boot should know
    # that the way in is the title screen and not a -D.
    #
    # WORLD_START_LEVEL이 이곳에 있었고 이제는 없습니다. 다른 입구가 없었기에 개발 빌드를 곧장
    # 보스 아레나로 보냈습니다. 플레이어가 스토리와 무한을 고르는 메뉴가 쓰이지 않았고, 아무것도
    # 도달할 수 없는 전투는 아무도 플레이테스트할 수 없는 전투였습니다. MENU_TITLE이 들어왔으므로
    # 모드는 그곳에서 정해지고 아레나는 world_begin이 로드합니다. "어느 방이 어느 모드인가"가
    # 있어야 할 곳이 그곳입니다.
    #
    # 이 note가 남는 이유는, 조용히 제거된 발판은 누군가 다시 세우는 발판이기 때문입니다. 다음에
    # 부팅 시 아레나를 원하는 사람은 입구가 -D가 아니라 타이틀 화면임을 알아야 합니다.
    $flags = @('-std=c11','-O0','-g','-Wall','-Wextra','-mconsole',
               '-DDEBUG_HUD','-DHOT_RELOAD') + $ubsan
} else {
    # Deliberately NOT -fdata-sections. On this target it stops zero-filled
    # statics from reaching .bss: every one of them gets its own .data$name
    # section and is written to disk as a run of zeros. Measured on this
    # project, the data that --gc-sections reclaims is worth far less than
    # what that costs -- 55,296 bytes with it, 51,200 without.
    # -ffunction-sections stays; reclaiming dead *code* is the real win.
    $flags = @(
        '-std=c11','-Os','-Wall','-Wextra',
        '-ffunction-sections',
        '-fno-ident','-fno-asynchronous-unwind-tables','-fno-unwind-tables',
        '-fomit-frame-pointer','-fno-stack-protector',
        '-mwindows','-s'
    )
}

# -lm last: mingw resolves left-to-right, and m.h pulls in sinf/cosf/tanf.
# -Map writes a linker map; with -ffunction-sections every function lands in
# its own section, so the map attributes bytes to individual symbols. That
# turns "the binary grew 3KB" into "mb_lathe costs 900 bytes" -- see
# .\size.ps1 -Detail.
$mapFile = Join-Path $outDir 'game.map'
$libs = @('-Wl,--gc-sections', "-Wl,-Map=$mapFile",
          '-lopengl32','-lgdi32','-luser32','-lwinmm','-lm')

# Tools link the game's own modules so their preview matches the real thing.
# They never ship, so they are built for debuggability, not size, and their
# size is deliberately absent from the budget report.
# $extraDefines builds a SECOND binary from the same source with different
# constants. It exists for capacity limits: a cache sized to hold everything the
# project defines means its overflow branch never runs, so the code that only
# executes when something has gone wrong is the code that goes untested. Passing
# a small -DMAX_CACHED forces it. $suffix keeps the variant beside the normal
# build rather than overwriting it.
#
# $noHotReload drops HOT_RELOAD, which is the one thing this function otherwise
# forces. See the note at the call to gcc for why it is normally mandatory and
# why exactly one variant is allowed to go without it.
function Invoke-ToolBuild([string]$name, [string[]]$extraDefines = @(),
                          [string]$suffix = '', [bool]$noHotReload = $false) {
    $src = Join-Path $root "tools\$name.c"
    if (-not (Test-Path $src)) { throw "No tool source at $src" }
    $script:lastToolExe = Join-Path $outDir "$name$suffix.exe"

    # main.c holds WinMain for the game; a tool brings its own.
    $shared = $sources | Where-Object { (Split-Path $_ -Leaf) -ne 'main.c' }

    # ...plus the tool-side shared library. $toolLibs are tools\*.c that hold no
    # entry point and exist to be linked into the tools that want them -- the
    # same relationship src\*.c has to the game. They are compiled into every
    # tool rather than tracked per tool: nothing here is size-constrained, and a
    # per-tool dependency list is a second place for the truth to live.
    $shared += $toolLibs | Where-Object { $_ -ne $src }

    $label = if ($suffix) { "$name$suffix" } else { $name }
    Write-Host "Compiling tool $label..." -ForegroundColor Cyan
    # HOT_RELOAD is mandatory for tools, not optional: without it data_text()
    # returns the copy baked into gen_assets.h, so an editor would edit a
    # stale snapshot and its save would silently overwrite whatever the user
    # had actually written in assets\.
    #
    # THE ONE EXCEPTION IS A READ-ONLY TEST OF THE SHIPPED PATH. That reason
    # above is about WRITING: an editor must not save over a snapshot. A test
    # that only reads has nothing to overwrite, and dropping HOT_RELOAD is the
    # only way to exercise what game.exe actually does -- data_map reading a
    # .map out of the baked blob instead of off disk. Every tool being a
    # hot-reload build meant the blob scanner, which is the code that ships,
    # was never run by anything that could assert on it.
    #
    # HOT_RELOAD가 도구에 필수인 이유는 *쓰기*에 관한 것입니다. 에디터가 낡은 스냅숏 위에
    # 저장해서는 안 됩니다. 읽기만 하는 테스트에는 덮어쓸 것이 없고, HOT_RELOAD를 빼는 것이
    # game.exe가 실제로 하는 일(data_map이 디스크가 아니라 구워 넣은 블롭에서 .map을 읽는
    # 것)을 실행해 볼 유일한 방법입니다. 모든 도구가 핫 리로드 빌드라는 것은, 정작 출하되는
    # 코드인 블롭 판독기가 그것에 대해 단언할 수 있는 무엇에 의해서도 실행되지 않았다는
    # 뜻이었습니다.
    #
    # Anything gcc prints to stdout would otherwise become part of this
    # function's return value, so the result is published through a script
    # variable instead of returned.
    # -I src so a tool may include "level.h" as well as "../src/level.h".
    # [string[]] and not a bare `if` expression. PowerShell unrolls a
    # single-element array to a scalar, and splatting a scalar STRING iterates
    # its characters -- gcc received a lone `-` and reported "input is from
    # standard input", which names neither the flag nor this line.
    # 단순 `if` 식이 아니라 [string[]]입니다. PowerShell은 원소가 하나인 배열을 스칼라로
    # 풀어 버리고, 스칼라 *문자열*을 스플랫하면 문자 단위로 순회합니다. gcc는 홀로 남은 `-`를
    # 받아 "입력이 표준 입력에서 온다"고 보고했는데, 그 말은 플래그도 이 줄도 지목하지 않습니다.
    [string[]]$hot = @()
    if (-not $noHotReload) { $hot = @('-DHOT_RELOAD') }
    # $ubsan here as well as in -Debug, and this is the half that matters more:
    # the tools are what -Test runs, so a check that trips turns a passing suite
    # into a named crash instead of a wrong answer nobody looks at.
    # -Debug뿐 아니라 이곳에도 $ubsan을 넣으며, 더 중요한 절반이 이쪽입니다. -Test가
    # 실행하는 것이 도구들이므로, 걸린 검사는 통과하는 스위트를 아무도 보지 않는 틀린 답이
    # 아니라 이름이 붙은 크래시로 바꿉니다.
    & $gcc '-std=c11' '-O1' '-g' '-Wall' '-Wextra' '-mconsole' @hot `
           @ubsan @extraDefines `
           '-I' (Join-Path $root 'src') `
           $src @shared -o $script:lastToolExe `
           '-lopengl32' '-lgdi32' '-luser32' '-lwinmm' '-lm' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Tool build failed (exit $LASTEXITCODE)" }
    Write-Host "  -> $script:lastToolExe" -ForegroundColor DarkGray
}

# Variants: the same test source built again with a capacity forced small
# enough that its overflow path actually executes. Keyed by tool name so the
# -Tools sweep picks them up automatically.
#
# The value is a LIST, because one source can have more than one branch that
# only a forced constant reaches and they do not have to be reachable at the
# same time -- leveltest has two, and a single small-caps binary would be
# asserting about a light cache in a level cut down to eight sectors.
#
# 값이 *목록*인 이유는, 한 소스가 강제된 상수로만 도달하는 분기를 여럿 가질 수 있고 그것들이
# 동시에 도달 가능할 필요는 없기 때문입니다. leveltest에 둘이 있으며, 작은 상한 하나로 합친
# 바이너리는 여덟 개 섹터로 잘려 나간 레벨에서 라이트 캐시에 대해 단언하게 됩니다.
$toolVariants = @{
    'textest' = @(
        @{ Defines = @('-DMAX_CACHED=4'); Suffix = '_tinycache' }
    )

    # The baked-light cache, forced smaller than the level that fills it needs.
    # That level is arena, not the much larger dm03: the cache only ever holds
    # vertices a lamp reached, and dm03 has no lamps at all. arena wants 353
    # keys, so 256 slots makes it overflow partway through and the "table full:
    # trace anyway, store nothing, count it" path runs instead of never.
    # leveltest asserts the opposite in each binary -- that it overflowed here,
    # and that it did not in the normal one.
    # 베이크 조명 캐시를, 그것을 채우는 레벨이 필요로 하는 것보다 작게 강제합니다. 그 레벨은
    # 훨씬 큰 dm03이 아니라 arena입니다. 캐시가 담는 것은 등이 닿은 정점뿐인데 dm03에는 등이
    # 하나도 없기 때문입니다. arena는 키 353개를 원하므로 256 슬롯이면 도중에 넘치고,
    # "테이블이 가득 참: 그래도 판정하고, 저장하지 않고, 센다" 경로가 결코 실행되지 않는
    # 대신 실행됩니다.
    # The level loader's own two caps, forced under what the shipped levels
    # need. dm03 is 59 sectors, so 32 truncates it while leaving arena's 9 and
    # vault's 3 intact -- both directions in one binary. dm03's longest outline
    # is 38, so 8 truncates that too and leaves the 4-point fixtures alone.
    #
    # 32 rather than something smaller because leveltest builds its own levels
    # too: many_lamp_checks lays out one room per lamp and wants 16 sectors, and
    # the worst-case rebuild grid fills the cap by construction. A cap under
    # WANT_LAMPS is now a compile error naming the fixture rather than a
    # segfault inside it. Until DIAG_SECTOR_CAP and
    # DIAG_POINT_CAP were added, those two refusals were the only ones in the
    # text loader that dropped their surplus without saying so -- and a counter
    # is only worth having if some binary reaches the branch that raises it.
    # leveltest's cap_checks asserts the opposite in each binary, the same way
    # overflow_checks does for the light cache.
    #
    # 레벨 로더 자신의 두 상한이며, 출하 레벨이 필요로 하는 것보다 낮게 강제합니다. arena는
    # dm03은 섹터 59개이므로 32면 그것이 잘리고 arena의 9개와 vault의 3개는 온전히 남습니다.
    # 한 바이너리에서 양쪽 방향을 모두 봅니다. dm03의 가장 긴 외곽선은 38이므로 8이면 그것도
    # 잘리고 4점짜리 픽스처들은 건드리지 않습니다.
    #
    # 더 작은 값이 아니라 32인 이유는 leveltest가 자기 레벨도 만들기 때문입니다.
    # many_lamp_checks는 등마다 방 하나를 놓아 섹터 16개를 원하고, 최악 재생성 격자는 구성상
    # 상한을 채웁니다. WANT_LAMPS보다 낮은 상한은 이제 그 안에서의 segfault가 아니라 픽스처를
    # 지목하는 컴파일 오류입니다. DIAG_SECTOR_CAP과 DIAG_POINT_CAP이 추가되기 전까지 그 두 거절은 텍스트 로더에서
    # 초과분을 말없이 버리는 유일한 것이었으며, 카운터는 어떤 바이너리가 그것을 올리는 분기에
    # 도달할 때에만 가질 가치가 있습니다. leveltest의 cap_checks가 각 바이너리에서 서로 반대되는
    # 것을 단언하며, overflow_checks가 라이트 캐시에 대해 하는 것과 같습니다.
    'leveltest' = @(
        @{ Defines = @('-DLIGHT_CACHE_SLOTS=256'); Suffix = '_tinylcache' }
        @{ Defines = @('-DLVL_MAX_SECTORS=32', '-DLVL_MAX_PTS=8'); Suffix = '_tinycaps' }
    )

    # THE SHIPPED DATA PATH, which no other binary here takes. Every tool is a
    # HOT_RELOAD build, so data_map reads assets\maps\<name>.map off disk and
    # the blob scanner that game.exe actually runs goes unexecuted. Dropping
    # HOT_RELOAD makes this variant read the map out of gen_assets.h exactly as
    # the shipped binary does.
    #
    # Run it from a directory with no assets\ beside it and it still passes,
    # which is the whole claim: the levels are IN the executable, and a floppy
    # carries one file.
    #
    # 출하되는 데이터 경로이며 이곳의 다른 어떤 바이너리도 그 길을 가지 않습니다. 모든 도구가
    # HOT_RELOAD 빌드이므로 data_map은 assets\maps\<name>.map을 디스크에서 읽고, 정작
    # game.exe가 실행하는 블롭 판독기는 실행되지 않습니다. HOT_RELOAD를 빼면 이 변형이 출하
    # 바이너리와 똑같이 gen_assets.h에서 맵을 읽습니다.
    #
    # 옆에 assets\가 없는 디렉터리에서 실행해도 통과하며, 그것이 주장의 전부입니다. 레벨은
    # 실행 파일 *안에* 있고, 플로피는 파일 하나를 나릅니다.
    # audio_shutdown gives the mixer 500ms and then, until it was fixed, carried
    # on regardless -- closing the device the mixer was writing to and deleting
    # the critical section it was inside. Missing that deadline needs a driver
    # to block, so no machine reaches the branch on purpose. AUDIO_MIXER_STALL_MS
    # holds the mixer inside one pass for longer than the deadline and the branch
    # runs. audiorace's own comment is careful about what that does and does not
    # prove: it exercises the path and pins the bounded return, and the pre-fix
    # code passes it too, because what that code freed was re-gated elsewhere.
    #
    # audio_shutdown은 믹서에 500ms를 주고, 고쳐지기 전까지는 그 뒤로도 개의치 않고
    # 진행했습니다. 믹서가 쓰고 있는 장치를 닫고 믹서가 들어가 있는 임계 영역을 삭제했습니다.
    # 그 기한을 놓치려면 드라이버가 막혀야 하므로 어떤 기계도 일부러 그 갈래에 도달하지
    # 않습니다. AUDIO_MIXER_STALL_MS가 믹서를 기한보다 오래 한 패스 안에 붙들면 그 갈래가
    # 실행됩니다. 그것이 무엇을 증명하고 무엇을 증명하지 못하는지에 대해서는 audiorace 자신의
    # 주석이 신중하게 밝힙니다. 경로를 실행하고 유계 반환을 고정하며, 수정 전 코드도 이것을
    # 통과합니다. 그 코드가 해제하던 것이 다른 곳에서 다시 막혀 있었기 때문입니다.
    'audiorace' = @(
        @{ Defines = @('-DAUDIO_MIXER_STALL_MS=2000'); Suffix = '_stuckmixer' }
    )

    # TRACETEST_BAKED WAS HERE AND IS RETIRED. It built tracetest without
    # HOT_RELOAD so the same assertions ran against the baked blob instead of
    # the files on disk -- "a read-only test of the shipped path", which is
    # what Invoke-ToolBuild's own comment calls the one exception to the
    # tools-are-authoring-builds rule.
    #
    # ITS SUBJECT STOPPED BEING SHIPPED. tracetest asserts against
    # `assets\maps\atrium.map`, which is a FIXTURE and not a level: the game
    # cannot enter it, and bake.ps1's $mapsNotBaked now keeps it out of the
    # binary so that one map ships. A fixture that is deliberately not in the
    # blob cannot be read through the blob, and the variant did not fail
    # because something broke -- it failed because it was asking for a file
    # this project had decided not to carry.
    #
    # WHAT IT PROTECTED IS STILL PROTECTED, and on better ground. The property
    # was "the blob reproduces the file"; maptest's test_bake_matches compares
    # `data_map` against `data_map_baked` plane for plane, texture for texture,
    # UV for UV and key for key -- and it does it on `lqdm1`, the map that
    # actually ships, which the variant never touched. Everything downstream of
    # that (level_load, level_ground, the traces) takes a `const char *` and
    # cannot tell where the bytes came from once they are proven identical.
    #
    # Recorded rather than deleted quietly: a variant that vanishes from this
    # table is a variant nobody can tell was ever considered.
    #
    # *tracetest_baked가 이곳에 있었고 은퇴했습니다.* HOT_RELOAD 없이 tracetest를 빌드하여 같은
    # 단언들이 디스크의 파일이 아니라 구워진 블롭에 대해 실행되게 했습니다.
    #
    # *그 대상이 출하되기를 그만두었습니다.* tracetest는 `assets\maps\atrium.map`에 대해
    # 단언하며, 그것은 레벨이 아니라 *픽스처*입니다. 게임은 그곳에 들어갈 수 없고, bake.ps1의
    # $mapsNotBaked가 맵 하나만 출하되도록 그것을 바이너리 밖에 둡니다. 의도적으로 블롭에 없는
    # 픽스처는 블롭을 통해 읽힐 수 없으며, 이 변형은 무언가 고장 나서 실패한 것이 아니라 이
    # 프로젝트가 나르지 않기로 정한 파일을 요구했기에 실패했습니다.
    #
    # *그것이 지키던 것은 여전히 지켜지며*, 더 나은 근거 위에서입니다. 그 성질은 "블롭이 파일을
    # 재현한다"였고, maptest의 test_bake_matches가 `data_map`과 `data_map_baked`를 평면 대 평면,
    # 텍스처 대 텍스처, UV 대 UV, 키 대 키로 비교합니다. 그것도 이 변형이 한 번도 건드리지 않은,
    # *실제로 출하되는* 맵인 `lqdm1`에 대해서 합니다.
}

if ($Tool) {
    Invoke-ToolBuild $Tool

    # The variant is rebuilt here too, not only in the -Tools sweep. A variant
    # that is only refreshed by the full sweep goes stale the moment somebody
    # iterates on the source with -Tool, and a stale test binary does not
    # announce itself -- it reports on code that is no longer there. That cost
    # a mutation check its result once: a deliberate fault was introduced, the
    # variant was not rebuilt, and the old binary passed.
    # 변형 바이너리도 -Tools 스윕에서만이 아니라 이곳에서 다시 만듭니다. 전체 스윕으로만
    # 갱신되는 변형은 누군가 -Tool로 소스를 반복 수정하는 순간 낡아 버리며, 낡은 테스트
    # 바이너리는 스스로를 알리지 않습니다. 더 이상 존재하지 않는 코드에 대해 보고합니다.
    # 이것이 변형 검증 하나의 결과를 망쳤습니다. 일부러 결함을 넣었는데 변형이 다시 만들어지지
    # 않아 옛 바이너리가 통과했습니다.
    # Held before the variant build overwrites it: what -Tool launches is the
    # tool that was asked for, not the last thing that happened to compile.
    # 변형 빌드가 덮어쓰기 전에 붙잡아 둡니다. -Tool이 실행하는 것은 요청받은 도구이지 마침
    # 마지막으로 컴파일된 것이 아닙니다.
    $wanted = $script:lastToolExe

    if ($toolVariants.ContainsKey($Tool)) {
        # @() around the lookup: a one-element list comes back unwrapped in
        # Windows PowerShell, and foreach over a scalar hashtable would iterate
        # its ENTRIES rather than the variant.
        # 조회를 @()로 감쌉니다. Windows PowerShell에서는 원소 하나짜리 목록이 풀린 채
        # 돌아오며, 스칼라 해시테이블에 대한 foreach는 변형이 아니라 그 *항목들*을
        # 순회하게 됩니다.
        foreach ($v in @($toolVariants[$Tool])) {
            Invoke-ToolBuild $Tool $v.Defines $v.Suffix ([bool]$v.NoHotReload)
        }
    }

    Write-Host "`nLaunching $Tool..." -ForegroundColor Cyan
    & $wanted
    return
}

# --- GLSL reserved words, checked before gcc ever runs ----------------------
#
# The shaders are C string literals, so the C compiler sees only text and the
# GLSL compiler sees them at RUN time, on whatever driver the player has. That
# gap has already cost once: `float patch` in pRust is a reserved word --
# tessellation, GLSL 4.0 and up -- and a fragment shader that never tessellates
# still may not use it as a variable. NVIDIA allows it in a #version 330
# shader; Intel, AMD and Mesa do not. So it built and ran here and failed on
# somebody else's machine with
#
#     ERROR: 1:65: error(#132) Syntax error: "patch" parse error
#
# which is the worst shape a bug can have: nothing local reproduces it, and the
# person who can reproduce it cannot fix it.
#
# C COMMENTS ARE STRIPPED FIRST, and a line-by-line regex cannot do that. The
# first version of this check reported its own explanatory comment, because the
# comment contains the word `patch` between backticks and a regex looking for
# quote pairs found some. A comment discussing the bug is not the bug.
#
# 셰이더는 C 문자열 리터럴이므로 C 컴파일러는 텍스트만 보고, GLSL 컴파일러는 플레이어가
# 가진 드라이버에서 *실행 시점에* 봅니다. 이 틈은 이미 한 번 대가를 치렀습니다. 국소적
# 으로 재현되지 않으며, 재현할 수 있는 사람은 고칠 수 없습니다. C 주석을 먼저 벗겨
# 내야 하고 줄 단위 정규식으로는 그럴 수 없습니다. 이 검사의 첫 판본은 자기 설명 주석을
# 신고했습니다.
$glslReserved = @(
    'active','asm','attribute','cast','class','common','enum','extern',
    'external','filter','fixed','goto','half','hvec2','hvec3','hvec4','fvec2',
    'fvec3','fvec4','inline','input','interface','long','namespace','noinline',
    'output','partition','patch','public','resource','sample','short','sizeof',
    'static','subroutine','superp','template','this','typedef','union',
    'unsigned','using','varying','volatile'
)

# Walk the file once, tracking whether we are in a C comment or a string, and
# collect only what is inside string literals -- that and nothing else is
# shader source.
function Get-ShaderText([string]$text) {
    $out = New-Object System.Collections.ArrayList
    $line = 1
    $i = 0
    $n = $text.Length
    while ($i -lt $n) {
        $c = $text[$i]
        if ($c -eq "`n") { $line++; $i++; continue }
        if ($c -eq '/' -and $i + 1 -lt $n -and $text[$i + 1] -eq '*') {
            $i += 2
            while ($i + 1 -lt $n -and -not ($text[$i] -eq '*' -and $text[$i + 1] -eq '/')) {
                if ($text[$i] -eq "`n") { $line++ }
                $i++
            }
            $i += 2
            continue
        }
        if ($c -eq '/' -and $i + 1 -lt $n -and $text[$i + 1] -eq '/') {
            while ($i -lt $n -and $text[$i] -ne "`n") { $i++ }
            continue
        }
        if ($c -eq '"') {
            $i++
            $sb = New-Object System.Text.StringBuilder
            while ($i -lt $n -and $text[$i] -ne '"') {
                if ($text[$i] -eq '\' -and $i + 1 -lt $n) { $i += 2; continue }
                [void]$sb.Append($text[$i]); $i++
            }
            $i++
            $body = $sb.ToString()
            # a GLSL // comment inside the literal is prose too
            $slash = $body.IndexOf('//')
            if ($slash -ge 0) { $body = $body.Substring(0, $slash) }
            [void]$out.Add([pscustomobject]@{ Line = $line; Code = $body })
            continue
        }
        $i++
    }
    return $out
}

$glslBad = @()
foreach ($shaderFile in @('src\render.c', 'src\post.c', 'src\tex.c')) {
    $path = Join-Path $root $shaderFile
    if (-not (Test-Path $path)) { continue }
    foreach ($frag in (Get-ShaderText (Get-Content $path -Raw))) {
        foreach ($w in $glslReserved) {
            if ($frag.Code -match ('(?<![A-Za-z0-9_])' + $w + '(?![A-Za-z0-9_])')) {
                $glslBad += ("  {0}:{1}  '{2}' is reserved: {3}" -f
                             $shaderFile, $frag.Line, $w, $frag.Code.Trim())
            }
        }
    }
}
if ($glslBad.Count -gt 0) {
    throw ("A shader uses a GLSL reserved word as an identifier. That compiles " +
           "on some drivers and fails on others, so it cannot be left to " +
           "whoever happens to have the stricter one:`n" + ($glslBad -join "`n"))
}

# Compile to named object files first, then link. Compiling and linking in one
# gcc call makes gcc use temporary object names (ccQKUxGq.o), which turns the
# linker map into noise -- the per-symbol report needs to know that a symbol
# came from render.c. The project is small enough that rebuilding every object
# every time costs about a second and avoids stale-flag bugs.
$objDir = Join-Path $outDir 'obj'
if (Test-Path $objDir) { Remove-Item $objDir -Recurse -Force }
New-Item -ItemType Directory $objDir | Out-Null

Write-Host "Compiling $($sources.Count) file(s)..." -ForegroundColor Cyan
$objs = @()
foreach ($src in $sources) {
    # Split-Path -LeafBase is PowerShell 6+; this runs on Windows PowerShell 5.1.
    $obj = Join-Path $objDir ([System.IO.Path]::GetFileNameWithoutExtension($src) + '.o')
    & $gcc @flags '-c' $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $src" }
    $objs += $obj
}

& $gcc @flags @objs -o $exe @libs
if ($LASTEXITCODE -ne 0) { throw "Link failed (exit $LASTEXITCODE)" }

if ($Debug) {
    # The budget only ever describes the shipped binary, so a dev build does
    # not print it -- seeing "14% used" for a 200KB build with symbols in it
    # was actively misleading.
    Write-Host ("  -> {0}  (hot reload: edits under assets\ appear live)" -f $exe) `
               -ForegroundColor Green
} else {
    # -UpdateDocs, so every figure quoted in the tree is the one this build just
    # measured -- README.md and docs\REFACTORING.md today; size.ps1 owns the
    # list. Only on a release build: a -Debug binary carries symbols and is not
    # what the budget describes, and writing its size into a document would put
    # a number there that no shipped build ever had.
    # -UpdateDocs를 주어 트리에 인용된 모든 수치가 이번 빌드가 방금 측정한 값이 되게
    # 합니다. 오늘 기준 README.md와 docs\REFACTORING.md이며, 목록은 size.ps1이 소유합니다.
    # 릴리스 빌드에서만입니다. -Debug 바이너리는 심볼을 포함하며 예산이 설명하는 대상이
    # 아니므로, 그 크기를 문서에 쓰면 어떤 출하 빌드도 가진 적 없는 숫자를 그곳에 두게
    # 됩니다.
    & (Join-Path $root 'size.ps1') -UpdateDocs
}

# ------------------------------------------------------------------ -Test --
#
# WHICH tools\*.c are not tests. Everything else is built AND RUN, and a
# non-zero exit fails the build.
#
# The list names the EXCEPTIONS rather than the tests, because the two go stale
# in opposite directions and only one of them matters. A new test that nobody
# adds to a list is a test that never runs, and it announces nothing -- which is
# the failure this whole switch exists to end. A new interactive tool that
# nobody excludes hangs the run on the first go and gets fixed in a minute.
#
# Listed by name for the reason $toolLibNames gives: a build script that guesses
# at the contents of source files is a rule with no way to state itself. The
# reason strings are the statement, and the check below refuses a stale entry.
#
# 어떤 tools\*.c가 테스트가 아닌지입니다. 그 밖의 전부는 빌드되고 *실행되며*, 0이 아닌 종료
# 코드는 빌드를 실패시킵니다.
#
# 목록이 테스트가 아니라 *예외*를 적는 이유는, 둘이 서로 반대 방향으로 낡고 그중 하나만
# 중요하기 때문입니다. 아무도 목록에 넣지 않은 새 테스트는 결코 실행되지 않는 테스트이며
# 아무것도 알리지 않습니다. 이 스위치가 끝내려고 존재하는 실패가 바로 그것입니다. 아무도
# 제외하지 않은 새 대화형 도구는 첫 실행에서 멈추고 1분 만에 고쳐집니다.
#
# 이름으로 적는 이유는 $toolLibNames가 밝히는 것과 같습니다. 소스 파일의 내용을 추측하는
# 빌드 스크립트는 스스로를 진술할 방법이 없는 규칙입니다. 이유 문자열이 그 진술이며, 아래의
# 검사가 낡은 항목을 거부합니다.
$notTests = @{
    'mapedit'    = 'draws levels with the mouse; a window and a person'
    'modeledit'  = 'draws weapon outlines with the mouse; likewise'
    'mapview'    = 'walks through a .map to look at it'
    'modelview'  = 'looks at a model and tunes where the view model sits'
    'dithershot' = 'renders a level to a PNG so the look can be judged by eye'
    'levelbench' = 'measures what the spatial queries cost; reports, asserts nothing'
    'soaktest'   = 'runs the frame loop for twenty minutes and prints what it cost'
    'sprdump'    = 'writes the sprite atlas to a PPM for eyeballing'
    'matdump'    = 'renders the procedural recipes to PNG for the map editor'
}

if ($Tools -or $Test) {
    Get-ChildItem (Join-Path $root 'tools') -Filter *.c |
      Where-Object { $toolLibNames -notcontains $_.BaseName } |
      ForEach-Object {
        $toolName = $_.BaseName
        Invoke-ToolBuild $toolName

        # Some tests need a second binary with a constant forced to a value the
        # real build never reaches, so the branch that only runs on overflow is
        # exercised too. See $toolVariants.
        if ($toolVariants.ContainsKey($toolName)) {
            foreach ($v in @($toolVariants[$toolName])) {
                Invoke-ToolBuild $toolName $v.Defines $v.Suffix ([bool]$v.NoHotReload)
            }
        }
    }
}

if ($Test) {
    # A stale exception is a tool somebody renamed or deleted, and the entry it
    # left behind now excludes nothing while reading as though it excludes
    # something. Same argument as -Portable's $stale, and the same verdict.
    # 낡은 예외는 누군가 이름을 바꾸거나 지운 도구이며, 남겨진 항목은 이제 아무것도 제외하지
    # 않으면서 무언가를 제외하는 것처럼 읽힙니다. -Portable의 $stale과 같은 논거이고 같은
    # 판정입니다.
    $sourceNames = Get-ChildItem (Join-Path $root 'tools') -Filter *.c |
                   ForEach-Object { $_.BaseName }
    $staleSkips = $notTests.Keys | Where-Object { $sourceNames -notcontains $_ }
    if ($staleSkips) {
        throw ("These are excluded from -Test but no longer exist: " +
               ($staleSkips -join ', '))
    }

    # The variants too. leveltest_tinycaps is the ONLY binary that reaches the
    # sector and point cap reports, so a run that skipped it would be a run that
    # never executed the branch those counters exist for.
    # 변형도 함께입니다. leveltest_tinycaps는 섹터와 점 상한 보고에 도달하는 유일한
    # 바이너리이므로, 그것을 건너뛴 실행은 그 카운터들이 존재하는 이유인 분기를 한 번도
    # 실행하지 않은 실행입니다.
    $testExes = @()
    foreach ($name in ($sourceNames | Sort-Object)) {
        if ($toolLibNames -contains $name) { continue }
        if ($notTests.ContainsKey($name))  { continue }
        $testExes += $name
        if ($toolVariants.ContainsKey($name)) {
            foreach ($v in @($toolVariants[$name])) { $testExes += "$name$($v.Suffix)" }
        }
    }

    Write-Host "`nRunning $($testExes.Count) self-checking binaries" -ForegroundColor Cyan
    foreach ($skip in ($notTests.Keys | Sort-Object)) {
        Write-Host ("  {0,-16} not run  ({1})" -f $skip, $notTests[$skip]) -ForegroundColor DarkGray
    }
    Write-Host ""

    # FROM build\, and it is not a preference. plat_exe_dir walks back from the
    # executable's own path -- drop the file, drop the separator, drop build\ --
    # so a binary run from anywhere else resolves assets\ against the wrong
    # parent, finds nothing, and silently falls back to the baked copy. maptest
    # compares the file against the baked copy, so it would be comparing the
    # baked copy with itself and reporting a failure that is really the runner's.
    # build\에서 실행하며 이는 취향이 아닙니다. plat_exe_dir는 실행 파일 자신의 경로에서
    # 거슬러 올라갑니다(파일, 구분자, build\를 차례로 버림). 따라서 다른 곳에서 실행된
    # 바이너리는 엉뚱한 부모를 기준으로 assets\를 찾고, 찾지 못하면 조용히 구워 넣은 사본으로
    # 되돌아갑니다. maptest는 파일을 구워 넣은 사본과 비교하므로, 구워 넣은 사본을 자기
    # 자신과 비교하면서 실제로는 실행기의 것인 실패를 보고하게 됩니다.
    $logDir = Join-Path $outDir 'testlog'
    if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Force $logDir | Out-Null }

    $failedTests = @()
    foreach ($name in $testExes) {
        $exePath = Join-Path $outDir "$name.exe"
        if (-not (Test-Path $exePath)) { throw "-Test wants $exePath and the sweep did not build it" }

        $log = Join-Path $logDir "$name.log"
        $proc = Start-Process -FilePath $exePath -WorkingDirectory $outDir `
                              -NoNewWindow -PassThru `
                              -RedirectStandardOutput $log `
                              -RedirectStandardError (Join-Path $logDir "$name.err")

        # Touching .Handle is not superstition and not a no-op. Start-Process
        # -PassThru hands back a Process object that never opened a handle of
        # its own, and once the process exits Windows has nothing left to ask:
        # .ExitCode comes back EMPTY, `$proc.ExitCode -eq 0` is false, and every
        # test in the run is reported as having failed while its log says it
        # passed. Reading .Handle here makes the object hold one, so the exit
        # code survives the exit. This is the first thing this switch got wrong.
        # .Handle을 건드리는 것은 미신도 아니고 아무 일도 하지 않는 것도 아닙니다.
        # Start-Process -PassThru가 돌려주는 Process 객체는 자기 핸들을 연 적이 없으며,
        # 프로세스가 종료되고 나면 Windows에 더 물어볼 것이 남지 않습니다. .ExitCode가 *빈
        # 값*으로 돌아오고, `$proc.ExitCode -eq 0`이 거짓이 되며, 로그에는 통과라고 적혀
        # 있는데 실행 중인 모든 테스트가 실패로 보고됩니다. 이곳에서 .Handle을 읽으면 객체가
        # 핸들을 보유하게 되어 종료 코드가 종료 이후까지 살아남습니다. 이 스위치가 처음
        # 저지른 잘못이 그것입니다.
        $null = $proc.Handle

        # A bounded wait, because the cost of getting the exception list wrong is
        # otherwise a build that never returns. Two minutes is far past the
        # slowest of these (tracetest, a few seconds) and far short of forever.
        # 제한된 대기입니다. 그러지 않으면 예외 목록을 잘못 적었을 때의 대가가 결코 돌아오지
        # 않는 빌드이기 때문입니다. 2분은 이 중 가장 느린 것(tracetest, 몇 초)보다 훨씬 길고
        # 영원보다는 훨씬 짧습니다.
        if (-not $proc.WaitForExit(120000)) {
            $proc.Kill()
            $failedTests += "$name (timed out; interactive?)"
            Write-Host ("  {0,-22} TIMEOUT" -f $name) -ForegroundColor Red
            continue
        }

        # The parameterless wait after the timed one, which MSDN asks for: the
        # overload with a timeout returns as soon as the process is gone and
        # does not promise the object's async state has caught up with it.
        # 시간 제한이 있는 대기 뒤의 인자 없는 대기이며, MSDN이 요구하는 것입니다. 시간
        # 제한이 있는 오버로드는 프로세스가 사라지자마자 반환하며, 객체의 비동기 상태가
        # 그것을 따라잡았다고 약속하지 않습니다.
        $proc.WaitForExit()

        $summary = ''
        if (Test-Path $log) {
            $lines = @(Get-Content $log | Where-Object { $_.Trim() })
            if ($lines.Count) { $summary = $lines[-1].Trim() }
        }

        if ($proc.ExitCode -eq 0) {
            Write-Host ("  {0,-22} ok       {1}" -f $name, $summary) -ForegroundColor DarkGray
        } else {
            $failedTests += "$name (exit $($proc.ExitCode))"
            Write-Host ("  {0,-22} FAILED   {1}" -f $name, $summary) -ForegroundColor Red
            Write-Host "    see $log" -ForegroundColor Red
        }
    }

    if ($failedTests) {
        throw ("$($failedTests.Count) of $($testExes.Count) failed: " +
               ($failedTests -join ', '))
    }
    Write-Host "`n  all $($testExes.Count) passed" -ForegroundColor Green
}

if ($Run) {
    Write-Host "`nLaunching..." -ForegroundColor Cyan
    Start-Process -FilePath $exe -WorkingDirectory $outDir
}
