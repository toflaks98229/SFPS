# SFPS - size budget report
#
# The one number that decides whether this project succeeded: 1,474,560 bytes,
# the capacity of a 1.44MB floppy. Everything shipped must fit inside it.
#
# Per-section sizes come from objdump so we can see *where* the bytes went:
#   .text   code
#   .rdata  string literals, const tables, GLSL source
#   .data   initialised globals  (costs file bytes)
#   .bss    zeroed globals       (costs NO file bytes -- big arrays go here)
#
# .\size.ps1 -Detail goes one level deeper, attributing bytes to individual
# functions and objects by parsing the linker map. Sections tell you the
# binary grew; symbols tell you what grew it.

# -UpdateReadme rewrites the figure quoted in README.md instead of only printing
# it. Passed by build.ps1 on a release build, so the number in the README is
# whatever the last shipped binary actually measured.
#
# It was 112,128 bytes and 7.60% for a long time, while the binary was 473,088
# and 32.08% -- four times over. Nobody wrote a wrong number: it was right when
# it was typed, and then Freedoom's sprites and sounds were baked in and nothing
# went back to the paragraph. A figure that has to be copied by hand is a figure
# that is eventually wrong, and this is the ONE number the whole project is
# measured against.
#
# -UpdateReadme는 수치를 출력만 하지 않고 README.md에 적힌 값을 다시 씁니다. 릴리스 빌드에서
# build.ps1이 전달하므로, README의 숫자는 마지막으로 출하된 바이너리가 실제로 측정한 값입니다.
#
# 오랫동안 112,128 바이트에 7.60%였고 그동안 바이너리는 473,088에 32.08%였습니다. 네 배
# 넘게 차이가 났습니다. 누구도 틀린 숫자를 쓰지 않았습니다. 타이핑할 때는 맞았고, 그 뒤
# Freedoom의 스프라이트와 사운드가 베이크되었으며 아무도 그 문단으로 돌아가지 않았습니다.
# 손으로 옮겨 적어야 하는 수치는 언젠가 틀리는 수치이며, 이것은 프로젝트 전체가 평가받는
# *그* 하나의 숫자입니다.
param([switch]$Detail, [int]$Top = 25, [switch]$UpdateReadme)

$ErrorActionPreference = 'Stop'

$root    = $PSScriptRoot
$objdump = Join-Path $root 'tools\w64devkit\bin\objdump.exe'
$exe     = Join-Path $root 'build\game.exe'
$BUDGET  = 1474560

if (-not (Test-Path $exe)) { throw "No build at $exe -- run .\build.ps1 first." }

$size = (Get-Item $exe).Length
$pct  = [math]::Round($size / $BUDGET * 100, 2)
$free = $BUDGET - $size

Write-Host ""
Write-Host "  === 1.44MB FLOPPY BUDGET ===" -ForegroundColor Yellow

if (Test-Path $objdump) {
    Write-Host ""
    Write-Host "  section    bytes    on disk?"
    Write-Host "  ---------------------------"
    & $objdump -h $exe |
        Select-String -Pattern '^\s+\d+\s+(\.\S+)\s+([0-9a-f]+)' |
        ForEach-Object {
            $name = $_.Matches[0].Groups[1].Value
            $len  = [Convert]::ToInt64($_.Matches[0].Groups[2].Value, 16)
            if ($len -gt 0) {
                $onDisk = if ($name -eq '.bss') { 'no (zeroed at load)' } else { 'yes' }
                '  {0,-9} {1,8:N0}    {2}' -f $name, $len, $onDisk
            }
        }
}

if ($Detail) {
    $map = Join-Path $root 'build\game.map'
    if (-not (Test-Path $map)) {
        Write-Host "`n  (no linker map at $map -- rebuild to generate one)" -ForegroundColor DarkGray
    } else {
        # -ffunction-sections gives each function its own section. On PE/COFF
        # the separator is '$', not '.', so the map reads
        #   .text$mb_lathe
        #                 0x...   0x384   build/obj/render.o
        # ld wraps whenever the name is long, so the numbers usually land on
        # the following line rather than the same one.
        $rows = @()
        $lines = Get-Content $map
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $m = [regex]::Match($lines[$i], '^\s+\.(text|rdata|data|bss)\$(\S+)\s*(.*)$')
            if (-not $m.Success) { continue }

            $tail = $m.Groups[3].Value
            if (-not $tail -and $i + 1 -lt $lines.Count) { $tail = $lines[$i + 1] }

            $n = [regex]::Match($tail, '0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)')
            if (-not $n.Success) { continue }

            $len = [Convert]::ToInt64($n.Groups[1].Value, 16)
            if ($len -le 0) { continue }
            $rows += [pscustomobject]@{
                Section = '.' + $m.Groups[1].Value
                Symbol  = $m.Groups[2].Value
                Bytes   = $len
                Object  = Split-Path $n.Groups[2].Value -Leaf
            }
        }

        if ($rows.Count -eq 0) {
            Write-Host "`n  (map has no per-symbol sections)" -ForegroundColor DarkGray
        } else {
            Write-Host "`n  --- bytes by object file ---" -ForegroundColor Yellow
            $rows | Group-Object Object |
                Sort-Object { ($_.Group | Measure-Object Bytes -Sum).Sum } -Descending |
                ForEach-Object {
                    '  {0,-16} {1,8:N0}' -f $_.Name, ($_.Group | Measure-Object Bytes -Sum).Sum
                }

            Write-Host "`n  --- largest $Top symbols ---" -ForegroundColor Yellow
            $rows | Sort-Object Bytes -Descending | Select-Object -First $Top |
                ForEach-Object {
                    '  {0,8:N0}  {1,-7} {2,-14} {3}' -f $_.Bytes, $_.Section, $_.Object, $_.Symbol
                }
        }
    }
}

# 40-cell bar so each block is 2.5% of the floppy.
$filled = [math]::Min(40, [math]::Floor($size / $BUDGET * 40))
$bar    = ('#' * $filled) + ('.' * (40 - $filled))
$colour = if ($pct -lt 70) { 'Green' } elseif ($pct -lt 95) { 'Yellow' } else { 'Red' }

Write-Host ""
Write-Host ("  [{0}]" -f $bar) -ForegroundColor $colour
Write-Host ("  {0:N0} / {1:N0} bytes   ({2}% used, {3:N0} free)" -f $size, $BUDGET, $pct, $free) -ForegroundColor $colour
Write-Host ""

if ($UpdateReadme) {
    $readme = Join-Path $root 'README.md'
    if (Test-Path $readme) {
        $line = '{0:N0} / {1:N0} bytes   ({2}% used)' -f $size, $BUDGET, $pct

        # .NET rather than Get-Content/Set-Content, and this is not a style
        # preference. Windows PowerShell 5.1 reads a BOM-less file as the
        # system ANSI codepage and writes UTF-8 WITH a BOM, so the obvious
        # version of this destroyed every non-ASCII character in the README --
        # the em dashes, the arrows in the feature list, the multiplication
        # sign -- and turned a one-line edit into a 600-line diff. ReadAllText
        # defaults to UTF-8, and UTF8Encoding($false) writes it back without
        # adding a BOM the file never had.
        #
        # Get-Content/Set-Content가 아니라 .NET을 쓰는 것은 취향의 문제가 아닙니다. Windows
        # PowerShell 5.1은 BOM 없는 파일을 시스템 ANSI 코드 페이지로 읽고 UTF-8을 BOM과 함께
        # 씁니다. 그래서 이 코드의 자명한 판본은 README의 모든 비ASCII 문자를 파괴했습니다.
        # 엠 대시, 기능 목록의 화살표, 곱셈 기호가 전부입니다. 그리고 한 줄짜리 편집을 600줄
        # diff로 만들었습니다. ReadAllText는 UTF-8을 기본으로 하고, UTF8Encoding($false)는
        # 파일이 가진 적 없는 BOM을 붙이지 않고 다시 씁니다.
        $utf8NoBom = New-Object System.Text.UTF8Encoding $false
        $text = [IO.File]::ReadAllText($readme)

        # The one line in the file shaped like the budget figure. Anchored on
        # the budget itself rather than on the used bytes, because the used
        # bytes are exactly what changes.
        # `\r?$` because the working copy has CRLF endings and .NET's `$`
        # matches before the \n, not before the \r -- so the anchored pattern
        # found nothing and the script reported the line as missing rather than
        # updating it.
        # `\r?$`인 이유는 작업 사본의 줄 끝이 CRLF이고 .NET의 `$`는 \r이 아니라 \n 앞에서
        # 일치하기 때문입니다. 그래서 고정된 패턴이 아무것도 찾지 못했고, 스크립트는 그 줄을
        # 갱신하는 대신 없다고 보고했습니다.
        $pattern = '(?m)^[\d,]+ / 1,474,560 bytes\s+\([\d.]+% used\)\r?$'
        if ($text -match $pattern) {
            $updated = [regex]::Replace($text, $pattern, $line)
            if ($updated -ne $text) {
                [IO.File]::WriteAllText($readme, $updated, $utf8NoBom)
                Write-Host "  README.md updated: $line" -ForegroundColor DarkGray
            }
        } else {
            Write-Host "  README.md has no budget line to update" -ForegroundColor Yellow
        }
    }
}
