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

# -UpdateDocs rewrites the figure everywhere a document quotes it, instead of
# only printing it. Passed by build.ps1 on a release build, so every number in
# the tree is whatever the last shipped binary actually measured.
#
# TWO DOCUMENTS TODAY, and the list is here rather than spread through the
# script: README.md's budget line, and the header of docs\REFACTORING.md. A
# third is a fourth argument to Update-BudgetLine and nothing else.
#
# It was 112,128 bytes and 7.60% for a long time, while the binary was 473,088
# and 32.08% -- four times over. Nobody wrote a wrong number: it was right when
# it was typed, and then Freedoom's sprites and sounds were baked in and nothing
# went back to the paragraph. A figure that has to be copied by hand is a figure
# that is eventually wrong, and this is the ONE number the whole project is
# measured against.
#
# THE SAME THING HAPPENED AGAIN, in the file that was supposed to have learned
# from it. docs\REFACTORING.md read 382,976 while the binary was 384,000,
# because this switch was named after README.md and only ever knew about
# README.md. Automating one quotation of a number does not make the number
# automatic; it makes the un-automated copies harder to notice, because the
# first place a reader checks is now always right.
#
# -UpdateDocs는 수치를 출력만 하지 않고, 문서가 그것을 인용하는 모든 곳에 다시 씁니다.
# 릴리스 빌드에서 build.ps1이 전달하므로, 트리 안의 모든 숫자는 마지막으로 출하된 바이너리가
# 실제로 측정한 값입니다.
#
# 오늘 기준 문서는 둘이며, 그 목록은 스크립트 곳곳이 아니라 이곳에 있습니다. README.md의
# 예산 줄과 docs\REFACTORING.md의 머리말입니다. 셋째는 Update-BudgetLine 호출 하나이고 그
# 밖에는 아무것도 아닙니다.
#
# 오랫동안 112,128 바이트에 7.60%였고 그동안 바이너리는 473,088에 32.08%였습니다. 네 배
# 넘게 차이가 났습니다. 누구도 틀린 숫자를 쓰지 않았습니다. 타이핑할 때는 맞았고, 그 뒤
# Freedoom의 스프라이트와 사운드가 베이크되었으며 아무도 그 문단으로 돌아가지 않았습니다.
# 손으로 옮겨 적어야 하는 수치는 언젠가 틀리는 수치이며, 이것은 프로젝트 전체가 평가받는
# *그* 하나의 숫자입니다.
#
# 그리고 같은 일이 다시 일어났습니다. 그것으로부터 배웠어야 할 바로 그 파일에서입니다.
# 바이너리가 384,000일 때 docs\REFACTORING.md는 382,976을 말하고 있었습니다. 이 스위치가
# README.md의 이름을 따랐고 README.md만을 알고 있었기 때문입니다. 어떤 숫자의 인용 하나를
# 자동화한다고 그 숫자가 자동이 되지는 않습니다. 자동화되지 않은 사본을 *더* 알아채기 어렵게
# 만들 뿐입니다. 이제 독자가 가장 먼저 확인하는 곳은 언제나 옳기 때문입니다.
param(
    [switch] $Detail,
    [int]    $Top = 25,

    # NAMED AFTER WHAT IT DOES, which stopped being "the README" the moment a
    # second document quoted the figure. The old spelling is kept as an alias
    # rather than dropped, because a switch name is an interface: a stale call
    # in a script this repo does not contain would fail with "parameter cannot
    # be found", which is a worse answer than working.
    #
    # 하는 일을 따라 이름 붙였습니다. 두 번째 문서가 이 수치를 인용한 순간 그것은 더 이상
    # "README"가 아니게 되었습니다. 옛 표기를 버리지 않고 별칭으로 남기는 이유는 스위치
    # 이름이 인터페이스이기 때문입니다. 이 저장소에 없는 스크립트의 낡은 호출은 "매개
    # 변수를 찾을 수 없음"으로 실패하며, 그것은 동작하는 것보다 나쁜 답입니다.
    [Alias('UpdateReadme')]
    [switch] $UpdateDocs
)

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

# ONE FUNCTION, CALLED PER DOCUMENT, and it is a function because there is now
# more than one document. It was a single inline block for README.md, and the
# obvious way to add a second file was to paste it -- which is the shape that
# produced the problem this whole switch exists to solve. A figure copied by
# hand goes stale; so does an update path copied by hand, and the second copy
# is the one nobody remembers to fix.
#
# 문서마다 호출하는 함수 하나이며, 함수인 이유는 이제 문서가 하나가 아니기 때문입니다.
# README.md만을 위한 인라인 블록이었고 두 번째 파일을 추가하는 자명한 방법은 그것을 복사해
# 붙이는 것인데, 그 형태가 바로 이 스위치가 존재하는 이유인 문제를 만들어 냅니다. 손으로
# 옮겨 적은 수치는 낡습니다. 손으로 복사한 갱신 경로도 낡으며, 아무도 고칠 것을 기억하지
# 못하는 쪽은 두 번째 사본입니다.
function Update-BudgetLine {
    param(
        [string]      $Path,     # File to rewrite.
        [string]      $Label,    # What to call it when reporting.
        [string]      $Pattern,  # Matches the line's CONTENT, without its ending.
        [scriptblock] $Build     # Takes the Match, returns the replacement line.
    )

    if (-not (Test-Path $Path)) {
        Write-Host "  $Label not found" -ForegroundColor Yellow
        return
    }

    # .NET rather than Get-Content/Set-Content, and this is not a style
    # preference. Windows PowerShell 5.1 reads a BOM-less file as the
    # system ANSI codepage and writes UTF-8 WITH a BOM, so the obvious
    # version of this destroyed every non-ASCII character in the README --
    # the em dashes, the arrows in the feature list, the multiplication
    # sign -- and turned a one-line edit into a 600-line diff. ReadAllText
    # defaults to UTF-8, and UTF8Encoding($false) writes it back without
    # adding a BOM the file never had.
    #
    # THE STAKES ARE HIGHER FOR THE SECOND FILE. README.md is mostly ASCII and
    # lost a few punctuation marks; docs\REFACTORING.md is Korean throughout and
    # the same mistake would destroy the entire document.
    #
    # Get-Content/Set-Content가 아니라 .NET을 쓰는 것은 취향의 문제가 아닙니다. Windows
    # PowerShell 5.1은 BOM 없는 파일을 시스템 ANSI 코드 페이지로 읽고 UTF-8을 BOM과 함께
    # 씁니다. 그래서 이 코드의 자명한 판본은 README의 모든 비ASCII 문자를 파괴했습니다.
    # 엠 대시, 기능 목록의 화살표, 곱셈 기호가 전부입니다. 그리고 한 줄짜리 편집을 600줄
    # diff로 만들었습니다. ReadAllText는 UTF-8을 기본으로 하고, UTF8Encoding($false)는
    # 파일이 가진 적 없는 BOM을 붙이지 않고 다시 씁니다.
    #
    # 두 번째 파일에서는 대가가 더 큽니다. README.md는 대부분 ASCII라 문장 부호 몇 개를
    # 잃었지만, docs\REFACTORING.md는 전체가 한국어이며 같은 실수는 문서 전체를 파괴합니다.
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    $text = [IO.File]::ReadAllText($Path)

    $m = [regex]::Match($text, $Pattern)
    if (-not $m.Success) {
        # SAID OUT LOUD RATHER THAN PASSED OVER. A document whose line has been
        # reformatted stops being updated, and it stops silently -- which is
        # exactly how README.md came to read 112,128 while the binary was
        # 473,088. The warning is the only thing standing between a reformat and
        # a figure that is wrong for a year.
        # 넘어가지 않고 소리 내어 말합니다. 줄의 서식이 바뀐 문서는 갱신을 멈추며, 조용히
        # 멈춥니다. 바이너리가 473,088일 때 README.md가 112,128을 말하고 있던 경위가 정확히
        # 그것입니다. 이 경고가 서식 변경과 1년 동안 틀린 수치 사이에 선 유일한 것입니다.
        Write-Host "  $Label has no budget line to update" -ForegroundColor Yellow
        return
    }

    # Splice by index rather than [regex]::Replace, because the replacement is
    # built text and Replace reads `$` in it as a group reference. None of
    # today's lines contain one; the next document's might, and the failure
    # would be a mangled line rather than an error.
    # [regex]::Replace가 아니라 인덱스로 잘라 붙입니다. 대체 문자열은 만들어진 텍스트인데
    # Replace는 그 안의 `$`를 그룹 참조로 읽습니다. 오늘의 줄에는 하나도 없지만 다음 문서의
    # 줄에는 있을 수 있으며, 그 실패는 오류가 아니라 뭉개진 줄로 나타납니다.
    $line = & $Build $m
    $updated = $text.Remove($m.Index, $m.Length).Insert($m.Index, $line)

    if ($updated -ne $text) {
        [IO.File]::WriteAllText($Path, $updated, $utf8NoBom)
        Write-Host "  $Label updated: $line" -ForegroundColor DarkGray
    }
}

if ($UpdateDocs) {
    # Every pattern here matches the line's CONTENT and stops before the line
    # ending, with `(?=\r?$)` rather than `\r?$`. The lookahead keeps the
    # ending out of the match, so the splice above cannot eat a \r and silently
    # convert one line of the file to LF while every other line stays CRLF.
    # `\r?` at all because .NET's `$` matches before the \n and not before the
    # \r -- without it the anchored pattern finds nothing on a CRLF working
    # copy, and the script reports the line as missing rather than updating it.
    #
    # 이곳의 모든 패턴은 줄의 *내용*에 일치하며 줄 끝 앞에서 멈춥니다. `\r?$`가 아니라
    # `(?=\r?$)`입니다. 전방 탐색이 줄 끝을 일치에서 제외하므로, 위의 잘라 붙이기가 \r을
    # 먹어 파일의 한 줄만 조용히 LF로 바꾸는 일이 없습니다. 애초에 `\r?`가 있는 이유는 .NET의
    # `$`가 \r이 아니라 \n 앞에서 일치하기 때문입니다. 그것이 없으면 CRLF 작업 사본에서
    # 고정된 패턴이 아무것도 찾지 못하고, 스크립트는 그 줄을 갱신하는 대신 없다고 보고합니다.

    # Anchored on the budget itself rather than on the used bytes, because the
    # used bytes are exactly what changes.
    # 사용 바이트가 아니라 예산 자체에 고정합니다. 바뀌는 것이 바로 사용 바이트이기 때문입니다.
    Update-BudgetLine `
        -Path    (Join-Path $root 'README.md') `
        -Label   'README.md' `
        -Pattern '(?m)^[\d,]+ / 1,474,560 bytes\s+\([\d.]+% used\)(?=\r?$)' `
        -Build   { param($m) '{0:N0} / {1:N0} bytes   ({2}% used)' -f $size, $BUDGET, $pct }

    # THREE FIGURES ON THIS LINE, not one, and a fourth that must NOT move:
    # `계획 시점 473,088` is where the plan started and is history rather than
    # measurement. It is captured and put back rather than rewritten, so the
    # one number on the line that is not about today survives every build.
    #
    # This file drifted for real. It read 382,976 while the shipped binary was
    # 384,000, because -UpdateReadme is named after the only file it used to
    # know about and nothing told the plan its own header had gone stale.
    #
    # 이 줄에는 수치가 셋이며, 움직여서는 *안 되는* 넷째가 있습니다. `계획 시점 473,088`은
    # 계획이 시작된 지점이며 측정이 아니라 역사입니다. 다시 쓰지 않고 붙잡아 두었다가 그대로
    # 되돌려 놓으므로, 이 줄에서 오늘에 관한 것이 아닌 유일한 숫자가 모든 빌드를 넘어
    # 살아남습니다.
    #
    # 이 파일은 실제로 낡았습니다. 출하 바이너리가 384,000일 때 382,976을 말하고 있었으며,
    # -UpdateReadme가 자신이 알던 유일한 파일의 이름을 따랐고 계획서에게 그 머리말이 낡았다고
    # 말해 주는 것이 없었기 때문입니다.
    # THE PATTERN IS PURE ASCII, and that is a hard requirement rather than a
    # preference. Windows PowerShell 5.1 reads a BOM-less .ps1 as the system ANSI
    # codepage, so every non-ASCII character in THIS FILE is already mangled by
    # the time the parser sees it. The Korean comments survive that because a
    # mangled comment still parses; a Korean string literal used as a regex does
    # not -- the first attempt here died with "Nested quantifier ?" on a pattern
    # that had become `諛붿씠?덈━`. The same class of bug the read/write code
    # above guards against, one level up: there it was the DATA, here it is the
    # SCRIPT.
    #
    # So the line's Korean words are captured out of the file and put straight
    # back. The script never spells one. That also means rewording the line --
    # 바이트 to bytes, 사용 to used -- keeps working without touching this.
    #
    # 패턴이 순수 ASCII인 것은 취향이 아니라 강제 조건입니다. Windows PowerShell 5.1은 BOM 없는
    # .ps1을 시스템 ANSI 코드 페이지로 읽으므로, *이 파일* 안의 모든 비ASCII 문자는 파서가 보는
    # 시점에 이미 뭉개져 있습니다. 한국어 주석이 살아남는 것은 뭉개진 주석도 파싱되기
    # 때문이며, 정규식으로 쓰이는 한국어 문자열 리터럴은 그렇지 않습니다. 이곳의 첫 시도는
    # `諛붿씠?덈━`가 되어 버린 패턴에서 "Nested quantifier ?"로 죽었습니다. 위의 읽기·쓰기
    # 코드가 막는 것과 같은 부류의 결함이 한 층 위에서 일어난 것입니다. 그곳에서는 *데이터*였고
    # 이곳에서는 *스크립트*입니다.
    #
    # 그래서 줄의 한국어 낱말은 파일에서 붙잡아 두었다가 그대로 되돌려 놓습니다. 스크립트는
    # 그중 어느 것도 적지 않습니다. 덕분에 줄의 표현을 바꾸어도(바이트를 bytes로, 사용을
    # used로) 이곳을 건드리지 않고 계속 동작합니다.
    #
    # FOUR FIGURES ON THIS LINE and a fifth that must NOT move: the plan's
    # starting size is history rather than measurement. It sits in group 1 and
    # is echoed back untouched, so the one number on the line that is not about
    # today survives every build.
    #
    # 이 줄에는 수치가 넷이며 움직여서는 *안 되는* 다섯째가 있습니다. 계획의 시작 크기는
    # 측정이 아니라 역사입니다. 그것은 그룹 1에 있고 손대지 않은 채 되돌려지므로, 이 줄에서
    # 오늘에 관한 것이 아닌 유일한 숫자가 모든 빌드를 넘어 살아남습니다.
    Update-BudgetLine `
        -Path    (Join-Path $root 'docs\REFACTORING.md') `
        -Label   'docs\REFACTORING.md' `
        -Pattern '(?m)^(.*?)\*\*[\d,]+\*\* / 1,474,560 (\S+) \([\d.]+% (\S+), \*\*[\d,]+ (\S+) (\S+)\*\*\)(?=\r?$)' `
        -Build   {
            param($m)
            '{0}**{1:N0}** / {2:N0} {3} ({4}% {5}, **{6:N0} {7} {8}**)' -f `
                $m.Groups[1].Value, $size, $BUDGET, $m.Groups[2].Value,
                $pct, $m.Groups[3].Value, $free, $m.Groups[4].Value, $m.Groups[5].Value
        }
}
