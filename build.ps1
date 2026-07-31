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
    [string]$Tool     # build and launch just this tool, e.g. -Tool modelview
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
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory $outDir | Out-Null }

# assets\*.txt are the source of truth; bake them into src\gen_assets.h first.
& (Join-Path $root 'bake.ps1')

$sources = Get-ChildItem (Join-Path $root 'src') -Filter *.c | ForEach-Object { $_.FullName }

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
function Invoke-ToolBuild([string]$name) {
    $src = Join-Path $root "tools\$name.c"
    if (-not (Test-Path $src)) { throw "No tool source at $src" }
    $script:lastToolExe = Join-Path $outDir "$name.exe"

    # main.c holds WinMain for the game; a tool brings its own.
    $shared = $sources | Where-Object { (Split-Path $_ -Leaf) -ne 'main.c' }

    Write-Host "Compiling tool $name..." -ForegroundColor Cyan
    # HOT_RELOAD is mandatory for tools, not optional: without it data_text()
    # returns the copy baked into gen_assets.h, so an editor would edit a
    # stale snapshot and its save would silently overwrite whatever the user
    # had actually written in assets\.
    #
    # Anything gcc prints to stdout would otherwise become part of this
    # function's return value, so the result is published through a script
    # variable instead of returned.
    # -I src so a tool may include "level.h" as well as "../src/level.h".
    & $gcc '-std=c11' '-O1' '-g' '-Wall' '-Wextra' '-mconsole' '-DHOT_RELOAD' `
           '-I' (Join-Path $root 'src') `
           $src @shared -o $script:lastToolExe `
           '-lopengl32' '-lgdi32' '-luser32' '-lwinmm' '-lm' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Tool build failed (exit $LASTEXITCODE)" }
    Write-Host "  -> $script:lastToolExe" -ForegroundColor DarkGray
}

if ($Tool) {
    Invoke-ToolBuild $Tool
    Write-Host "`nLaunching $Tool..." -ForegroundColor Cyan
    & $script:lastToolExe
    return
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
    & (Join-Path $root 'size.ps1')
}

if ($Tools) {
    Get-ChildItem (Join-Path $root 'tools') -Filter *.c |
        ForEach-Object { Invoke-ToolBuild $_.BaseName }
}

if ($Run) {
    Write-Host "`nLaunching..." -ForegroundColor Cyan
    Start-Process -FilePath $exe -WorkingDirectory $outDir
}
