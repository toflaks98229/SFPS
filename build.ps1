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

param(
    [switch]$Debug,
    [switch]$Run,
    [switch]$Tools,   # also build tools\*.c
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
        'audio.c'       = 'NOT YET SPLIT -- the waveOut device and mixer thread are still mixed in with the portable mixing policy'
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
$toolLibs = $toolLibNames | ForEach-Object { Join-Path $root "tools\$_.c" }
foreach ($lib in $toolLibs) {
    if (-not (Test-Path $lib)) { throw "Tool library missing: $lib" }
}

if ($Debug) {
    # DEBUG_HUD adds a live player/weapon state readout in the title bar.
    # HOT_RELOAD reads assets\*.txt at runtime and watches them, so editing a
    # silhouette or a material updates the running game. Both are compiled out
    # entirely in release, so they cost nothing in the shipped exe.
    $flags = @('-std=c11','-O0','-g','-Wall','-Wextra','-mconsole',
               '-DDEBUG_HUD','-DHOT_RELOAD')
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
    & $gcc '-std=c11' '-O1' '-g' '-Wall' '-Wextra' '-mconsole' @hot `
           @extraDefines `
           '-I' (Join-Path $root 'src') `
           $src @shared -o $script:lastToolExe `
           '-lopengl32' '-lgdi32' '-luser32' '-lwinmm' '-lm' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Tool build failed (exit $LASTEXITCODE)" }
    Write-Host "  -> $script:lastToolExe" -ForegroundColor DarkGray
}

# Variants: the same test source built again with a capacity forced small
# enough that its overflow path actually executes. Keyed by tool name so the
# -Tools sweep picks them up automatically.
$toolVariants = @{
    'textest' = @{ Defines = @('-DMAX_CACHED=4'); Suffix = '_tinycache' }

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
    'leveltest' = @{ Defines = @('-DLIGHT_CACHE_SLOTS=256'); Suffix = '_tinylcache' }

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
    'tracetest' = @{ Defines = @(); Suffix = '_baked'; NoHotReload = $true }
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
        $v = $toolVariants[$Tool]
        Invoke-ToolBuild $Tool $v.Defines $v.Suffix ([bool]$v.NoHotReload)
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
    # -UpdateReadme, so the figure quoted in README.md is the one this build
    # just measured. Only on a release build: a -Debug binary carries symbols
    # and is not what the budget describes, and writing its size into the
    # README would put a number there that no shipped build ever had.
    # -UpdateReadme를 주어 README.md에 인용된 수치가 이번 빌드가 방금 측정한 값이 되게
    # 합니다. 릴리스 빌드에서만입니다. -Debug 바이너리는 심볼을 포함하며 예산이 설명하는
    # 대상이 아니므로, 그 크기를 README에 쓰면 어떤 출하 빌드도 가진 적 없는 숫자를 그곳에
    # 두게 됩니다.
    & (Join-Path $root 'size.ps1') -UpdateReadme
}

if ($Tools) {
    Get-ChildItem (Join-Path $root 'tools') -Filter *.c |
      Where-Object { $toolLibNames -notcontains $_.BaseName } |
      ForEach-Object {
        $toolName = $_.BaseName
        Invoke-ToolBuild $toolName

        # Some tests need a second binary with a constant forced to a value the
        # real build never reaches, so the branch that only runs on overflow is
        # exercised too. See $toolVariants.
        if ($toolVariants.ContainsKey($toolName)) {
            $v = $toolVariants[$toolName]
            Invoke-ToolBuild $toolName $v.Defines $v.Suffix ([bool]$v.NoHotReload)
        }
    }
}

if ($Run) {
    Write-Host "`nLaunching..." -ForegroundColor Cyan
    Start-Process -FilePath $exe -WorkingDirectory $outDir
}
