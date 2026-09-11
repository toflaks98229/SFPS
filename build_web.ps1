# SFPS -- the web build.
#
# WHY THIS IS A SEPARATE SCRIPT rather than a switch on build.ps1. The two
# builds share their source list and nothing else: a different compiler, a
# different C dialect, a different set of platform files, a different notion of
# what "the output" even is (one file against three). Folding them together
# would put an -if around every line of build.ps1 and leave neither readable.
# What IS shared -- which files are the other host's -- lives in build.ps1 as
# $webOnly and is read from here, because two lists is how they drift.
#
# 왜 build.ps1의 스위치가 아니라 별도 스크립트인가. 두 빌드는 소스 목록만 공유하고 그 외에는
# 아무것도 공유하지 않습니다. 컴파일러가 다르고, C 방언이 다르고, 플랫폼 파일 집합이 다르고,
# "출력"이 무엇인지조차 다릅니다(하나 대 셋). 둘을 접어 넣으면 build.ps1의 모든 줄에 if가 붙고
# 어느 쪽도 읽을 수 없게 됩니다. *공유되는 것*, 즉 어떤 파일이 다른 호스트의 것인가는
# build.ps1에 $webOnly로 있고 이곳에서 읽어 옵니다. 목록이 둘이면 어긋나기 때문입니다.

param(
    [switch]$Debug,   # -O0 -g, assertions on, readable stack traces
    [switch]$Serve    # start a local server and print the URL
)

$ErrorActionPreference = 'Stop'
$root   = $PSScriptRoot
$outDir = Join-Path $root 'build\web'

# --- the toolchain ----------------------------------------------------------
#
# tools\emsdk is where emsdk install puts it, and .gitignore keeps it out for
# the same three reasons tools\w64devkit is out. A clone without it builds
# game.exe exactly as before and is told what is missing here rather than
# failing inside a compiler.
#
# tools\emsdk는 emsdk install이 그것을 두는 곳이며, .gitignore가 tools\w64devkit과 같은 세
# 이유로 제외합니다. 그것이 없는 클론은 game.exe를 예전과 똑같이 빌드하고, 컴파일러 안에서
# 실패하는 대신 이곳에서 무엇이 없는지를 듣습니다.
$emcc = Join-Path $root 'tools\emsdk\upstream\emscripten\emcc.exe'
if (-not (Test-Path $emcc)) {
    throw ("No Emscripten at $emcc.`n" +
           "  git clone https://github.com/emscripten-core/emsdk tools\emsdk`n" +
           "  tools\emsdk\emsdk.bat install latest`n" +
           "  tools\emsdk\emsdk.bat activate latest")
}

# --- which files are the other host's ---------------------------------------
#
# Read out of build.ps1 rather than repeated. The regex is narrow on purpose: if
# somebody rewrites that line into a different shape this throws instead of
# silently compiling the Win32 files into a wasm, which fails a hundred lines
# later with windows.h errors that look like a toolchain problem.
#
# 반복하지 않고 build.ps1에서 읽어 옵니다. 정규식이 좁은 것은 의도적입니다. 누군가 그 줄을 다른
# 형태로 고쳐 쓰면 이곳은 조용히 Win32 파일을 wasm에 컴파일해 넣는 대신 예외를 던집니다. 그러지
# 않으면 백 줄 뒤에 툴체인 문제처럼 보이는 windows.h 오류로 실패합니다.
$buildPs1 = Get-Content (Join-Path $root 'build.ps1') -Raw
if ($buildPs1 -notmatch '(?m)^\$webOnly\s*=\s*@\(([^)]*)\)') {
    throw 'build.ps1 no longer defines $webOnly in the shape this script reads.'
}
$webOnly = $matches[1] -split ',' | ForEach-Object { $_.Trim().Trim("'") } |
           Where-Object { $_ }

# The Win32 half, named here because this is the script that has to exclude it.
# Kept as the complement of $webOnly rather than as a second hand-written list:
# these four are what build.ps1's $platform hash already names, and asking that
# hash would mean parsing a multi-line literal for no gain.
# Win32 절반이며, 그것을 제외해야 하는 스크립트가 이곳이므로 이곳에서 이름 붙입니다.
$win32Only = @('main.c', 'gl.c', 'plat_win32.c', 'audio_win32.c')

$sources = Get-ChildItem (Join-Path $root 'src') -Filter *.c |
           Where-Object { $win32Only -notcontains $_.Name } |
           ForEach-Object { $_.FullName }

Write-Host ("Compiling {0} file(s) for the web..." -f $sources.Count) -ForegroundColor Cyan
foreach ($n in $webOnly) { Write-Host ("  platform: {0}" -f $n) -ForegroundColor DarkGray }

# assets\*.txt are the source of truth; bake them into src\gen_assets.h first,
# exactly as build.ps1 does. The web build has no files beside it either.
# 에셋은 assets\*.txt가 진실의 원천이며, build.ps1과 똑같이 먼저 src\gen_assets.h로 굽습니다.
# 웹 빌드도 옆에 파일이 없기는 마찬가지입니다.
& (Join-Path $root 'bake.ps1')

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory $outDir -Force | Out-Null }

# --- flags ------------------------------------------------------------------
#
# -std=gnu11 AND NOT c11, which is the one dialect difference between the two
# hosts and it is forced: EM_ASM refuses to expand outside a GNU mode, and
# plat_web.c, audio_web.c and main_web.c all need it to reach the browser at
# all. gnu11 is c11 plus extensions, so nothing this project writes changes
# meaning -- and the desktop build stays c11, which is the stricter of the two
# and the one the 41 suites run, so an extension that crept into a portable
# file fails there rather than shipping.
#
# *c11이 아니라 gnu11이며*, 두 호스트 사이의 유일한 방언 차이이고 강제된 것입니다. EM_ASM은
# GNU 모드 밖에서 확장을 거부하며, plat_web.c와 audio_web.c와 main_web.c가 브라우저에 닿으려면
# 전부 그것이 필요합니다. gnu11은 c11에 확장을 더한 것이므로 이 프로젝트가 쓰는 어떤 것도 의미가
# 달라지지 않습니다. 그리고 데스크톱 빌드는 c11로 남으며, 그쪽이 더 엄격하고 41개 스위트가 도는
# 쪽이므로, 이식 가능한 파일에 끼어든 확장은 배포되는 대신 그곳에서 실패합니다.
$flags = @('-std=gnu11', '-Wall', '-Wextra', '-I', (Join-Path $root 'src'))

if ($Debug) {
    $flags += @('-O0', '-g', '-sASSERTIONS=2', '-sSAFE_HEAP=1', '-DDEBUG_HUD')
} else {
    $flags += @('-Os', '-flto', '--closure', '1', '-sASSERTIONS=0')
}

$link = @(
    # WEBGL 2 ONLY, both ends. MIN stops the runtime from quietly handing back a
    # WebGL 1 context that would die at the first shader; MAX stops it compiling
    # in the WebGL 1 path nobody will take. See webgl.h.
    # *양쪽 끝 모두 WebGL 2입니다.* MIN은 런타임이 첫 셰이더에서 죽을 WebGL 1 컨텍스트를 조용히
    # 돌려주는 것을 막고, MAX는 아무도 가지 않을 WebGL 1 경로를 컴파일해 넣는 것을 막습니다.
    '-sMIN_WEBGL_VERSION=2', '-sMAX_WEBGL_VERSION=2',

    # 8,214,512 bytes of .bss on the desktop build, plus the heap. 32MB with no
    # growth: growth costs a check on every access and this game's footprint is
    # known at link time rather than discovered at run time.
    # 데스크톱 빌드의 .bss가 8,214,512바이트이고 그 위에 힙이 얹힙니다. 증가 없이 32MB입니다.
    # 증가는 모든 접근에 검사 비용을 물리며, 이 게임의 사용량은 실행 중에 발견되는 것이 아니라
    # 링크 시점에 알려져 있습니다.
    '-sINITIAL_MEMORY=33554432', '-sALLOW_MEMORY_GROWTH=0',

    # world_progress_for_stage's frame is 33,424 bytes, measured with
    # -Wstack-usage. Emscripten's default stack is 64KB and that is close enough
    # to be a bad place to stand: an overflow here does not fault, it quietly
    # writes over whatever is below.
    # world_progress_for_stage의 프레임이 -Wstack-usage로 재어 33,424바이트입니다.
    # Emscripten의 기본 스택은 64KB이며 서 있기에 나쁜 만큼 가깝습니다. 이곳의 넘침은 오류를
    # 내지 않고 아래에 있는 것을 조용히 덮어씁니다.
    '-sSTACK_SIZE=1048576',

    # save.c writes with fopen and plat_web.c mounts IDBFS under it.
    # save.c는 fopen으로 쓰고 plat_web.c가 그 아래에 IDBFS를 마운트합니다.
    '-lidbfs.js',

    # No node, no worker, no shell. Only the web path is reachable and the rest
    # is glue nobody downloads this to run.
    # node도 worker도 shell도 없습니다. 도달 가능한 것은 web 경로뿐이고 나머지는 아무도 이것을
    # 내려받아 실행하지 않을 접착제입니다.
    '-sENVIRONMENT=web',

    '--shell-file', (Join-Path $root 'web\shell.html')
)

$out = Join-Path $outDir 'index.html'
& $emcc @flags @sources @link -o $out
if ($LASTEXITCODE -ne 0) { throw "emcc failed (exit $LASTEXITCODE)" }

# --- what it weighs ---------------------------------------------------------
#
# THE NUMBER THAT MATTERS IS WHAT A VISITOR DOWNLOADS, not what is on disk, and
# on static hosting that is the gzip. Measured rather than guessed, because
# docs/WEBGL_PROPOSAL.md §3 spent a long time as an estimate and this script is
# what ends that.
#
# NOT COMPARED TO 1,474,560. The floppy is the .exe's budget and the proposal
# says plainly that a web build has no reason to inherit it. Printed beside it
# because it is interesting, not because it is a limit.
#
# *중요한 수는 방문자가 내려받는 양*이며 디스크에 있는 양이 아닙니다. 그리고 정적 호스팅에서
# 그것은 gzip입니다. 추측이 아니라 측정합니다. docs/WEBGL_PROPOSAL.md의 §3이 오랫동안 추정으로
# 있었고 그것을 끝내는 것이 이 스크립트이기 때문입니다.
function Get-GzipSize([string]$path) {
    $raw = [System.IO.File]::ReadAllBytes($path)
    $ms  = New-Object System.IO.MemoryStream
    $gz  = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionLevel]::Optimal)
    $gz.Write($raw, 0, $raw.Length)
    $gz.Close()
    return $ms.ToArray().Length
}

Write-Host "`n  === WHAT A VISITOR DOWNLOADS ===`n" -ForegroundColor Cyan
$totalRaw = 0; $totalGz = 0
foreach ($f in (Get-ChildItem $outDir -File | Sort-Object Name)) {
    $gz = Get-GzipSize $f.FullName
    $totalRaw += $f.Length; $totalGz += $gz
    Write-Host ("  {0,-14} {1,12:N0}  ->{2,12:N0} gzip" -f $f.Name, $f.Length, $gz)
}
Write-Host ("  {0,-14} {1,12:N0}  ->{2,12:N0} gzip" -f '', $totalRaw, $totalGz) -ForegroundColor White
Write-Host ("`n  the floppy, for scale: {0:N0} bytes (the .exe's budget, not this one)" -f 1474560) -ForegroundColor DarkGray

if ($Serve) {
    Write-Host "`n  http://localhost:8000/  (Ctrl+C to stop)" -ForegroundColor Green
    Push-Location $outDir
    try { & (Join-Path $root 'tools\emsdk\upstream\emscripten\emrun.bat') --no_browser --port 8000 . }
    finally { Pop-Location }
}
