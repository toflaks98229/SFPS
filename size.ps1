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

param([switch]$Detail, [int]$Top = 25)

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
