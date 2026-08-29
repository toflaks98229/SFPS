# SFPS - asset baking
#
# assets/*.txt are the single source of truth. This turns them into C string
# literals in src/gen_assets.h, which the release build embeds so the shipped
# exe never touches the filesystem.
#
# Comments and redundant whitespace are stripped on the way through, which is
# what lets the source files stay heavily documented for hand editing while
# the binary carries only the data. On the current library that is roughly a
# 3x reduction.
#
# build.ps1 runs this automatically; run it directly to inspect the output.

$ErrorActionPreference = 'Stop'

$root   = $PSScriptRoot
$outFile = Join-Path $root 'src\gen_assets.h'

$sets = @(
    @{ Name = 'ASSET_MODELS';  File = 'assets\models.txt'   },
    @{ Name = 'ASSET_RECIPES'; File = 'assets\textures.txt' },
    @{ Name = 'ASSET_SOUNDS';  File = 'assets\sounds.txt'   },
    @{ Name = 'ASSET_LEVELS';  File = 'assets\levels.txt'   },
    @{ Name = 'ASSET_EFFECTS'; File = 'assets\effects.txt'  },
    @{ Name = 'ASSET_MUSIC';   File = 'assets\music\music.txt' },
    @{ Name = 'ASSET_LOOT';    File = 'assets\loot.txt'    },
    @{ Name = 'ASSET_STORY';   File = 'assets\story.txt'   }
)

# Wavefront .obj -> the integer mesh text the game parses.
#
# Blender is the authoring tool, but its output is not what should be shipped:
# "v -0.860000 0.090000 0.050000" is 28 bytes for a vertex that fits in 11 as
# "-860 90 50". Converting here also means the game never needs a float parser,
# which keeps every asset language in this project integer-only.
#
# Quads are triangulated by fanning, and the v/vt indices are re-emitted
# 0-based. Normals in the file are ignored: they are a third of the vertex
# data and the loader recomputes them per face anyway, which is what a
# flat-shaded low-poly look wants.
function ConvertFrom-Obj([string]$path, [string]$name) {
    $pos = New-Object System.Collections.ArrayList
    $uv  = New-Object System.Collections.ArrayList
    $tri = New-Object System.Collections.ArrayList

    foreach ($line in Get-Content $path) {
        $line = $line.Trim()
        if (-not $line -or $line.StartsWith('#')) { continue }
        $tok = $line -split '\s+'

        switch ($tok[0]) {
            'v'  { [void]$pos.Add(@(
                        [int][math]::Round([double]$tok[1] * 1000),
                        [int][math]::Round([double]$tok[2] * 1000),
                        [int][math]::Round([double]$tok[3] * 1000))) }
            'vt' { [void]$uv.Add(@(
                        [int][math]::Round([double]$tok[1] * 1000),
                        [int][math]::Round([double]$tok[2] * 1000))) }
            'f'  {
                # Each corner is v, v/vt, v//vn or v/vt/vn, 1-based.
                $corner = @()
                for ($i = 1; $i -lt $tok.Count; $i++) {
                    $parts = $tok[$i] -split '/'
                    $vi = [int]$parts[0] - 1
                    $ti = if ($parts.Count -gt 1 -and $parts[1]) { [int]$parts[1] - 1 } else { -1 }
                    $corner += ,@($vi, $ti)
                }
                for ($i = 1; $i -lt $corner.Count - 1; $i++) {
                    [void]$tri.Add(@($corner[0], $corner[$i], $corner[$i + 1]))
                }
            }
        }
    }

    if ($pos.Count -eq 0 -or $tri.Count -eq 0) { throw "empty or unreadable obj: $path" }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append("x $name`n")

    [void]$sb.Append('p')
    foreach ($p in $pos) { [void]$sb.Append(" $($p[0]) $($p[1]) $($p[2])") }
    [void]$sb.Append("`n")

    if ($uv.Count) {
        [void]$sb.Append('t')
        foreach ($t in $uv) { [void]$sb.Append(" $($t[0]) $($t[1])") }
        [void]$sb.Append("`n")
    }

    [void]$sb.Append('f')
    foreach ($t in $tri) {
        foreach ($c in $t) { [void]$sb.Append(" $($c[0]) $($c[1])") }
    }
    [void]$sb.Append("`n")

    return [pscustomobject]@{
        Text  = $sb.ToString()
        Verts = $pos.Count
        Tris  = $tri.Count
    }
}

# The parsers treat whitespace and newlines as interchangeable and each opcode
# has a known arity, so collapsing every run of whitespace to one space is
# safe and costs the binary nothing in readability it was going to use.
function ConvertTo-Minified([string]$text) {
    $out = New-Object System.Text.StringBuilder
    foreach ($line in $text -split "`r?`n") {
        $hash = $line.IndexOf('#')
        if ($hash -ge 0) { $line = $line.Substring(0, $hash) }
        $line = $line.Trim()
        if (-not $line) { continue }
        $line = [regex]::Replace($line, '\s+', ' ')
        [void]$out.Append($line).Append(' ')
    }
    return $out.ToString().Trim()
}

# WAV -> 4-bit IMA ADPCM, in the same 6-bit alphabet the sprites use.
#
# Sound was the last thing in this project still entirely synthesised, and
# importing real audio is a genuine departure from "keep the recipe, not the
# result" -- so it pays its way. Raw 8-bit at 11025Hz would be 1.33 characters
# per sample and 253KB of a 1.44MB budget; ADPCM is 4 bits, which is exactly
# two thirds of a character, and 78KB. Half the data for a codec the decoder
# spends about forty lines on.
#
# 11025Hz is not a compromise, it is Doom's own rate, and the mixer runs at
# 44100 -- exactly four times it. So playback steps one source sample every
# four output samples with no resampler and no accumulating phase error.
#
# The nibbles pack three per two characters, which is the sprite codec's `p`
# opcode arithmetic: 12 bits into 12 bits, no waste.
#
# A sound is a RECIPE OR A SAMPLE, whichever exists, and both kinds live in one
# library. `pump`, `hook` and `hreel` have no Doom equivalent -- there is no
# pump-action rack, no grapple and no reel in Doom -- so they keep the
# synthesised layers in assets/sounds.txt, and nothing had to choose between
# the two approaches wholesale.
#
# 사운드는 이 프로젝트에서 마지막까지 전부 합성으로 남아 있던 것이며, 실제 오디오를
# 들여오는 일은 "결과가 아니라 레시피를 보관한다"는 원칙에서 진짜로 벗어나는 일입니다.
# 그래서 값을 치릅니다. 11025Hz 8비트 원본은 샘플당 1.33자, 1.44MB 예산 중 253KB이고,
# ADPCM은 4비트이니 정확히 3분의 2자, 78KB입니다. 디코더 마흔 줄로 데이터가 절반이 됩니다.
#
# 11025Hz는 타협이 아니라 Doom 자신의 레이트이며, 믹서는 그 정확히 네 배인 44100으로
# 돕니다. 따라서 재생은 출력 네 샘플마다 원본 한 샘플을 내보내며 리샘플러도, 누적되는
# 위상 오차도 없습니다.

# The IMA tables. Named $AdpcmStep and $AdpcmNext rather than $STEP and $INDEX
# because PowerShell variable names are CASE-INSENSITIVE, so `$step = $STEP[$i]`
# overwrites the table with its own first lookup -- the identical bug that made
# every packed sprite decode to the character '0' for as long as one existed.
# Twice is a pattern: a lookup table here never shares a name with the local
# that reads it, whatever the case.
# 테이블 이름이 $STEP/$INDEX가 아닌 이유는 PowerShell의 변수명이 *대소문자를 구분하지
# 않기* 때문입니다. `$step = $STEP[$i]`는 테이블을 자기 자신의 첫 조회 결과로 덮어쓰며,
# 이는 패킹된 스프라이트가 존재하는 내내 모두 문자 '0'으로 디코딩되게 만든 바로 그
# 버그입니다. 두 번이면 패턴이므로, 조회 테이블은 그것을 읽는 지역 변수와 대소문자를
# 불문하고 이름을 공유하지 않습니다.
$AdpcmStep = @(
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,
    80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
    494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,
    2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,
    8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767)
$AdpcmNext = @(-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8)

# 8-bit mono PCM out of a RIFF file. Only the shape Doom's lumps come in is
# handled, and anything else is an error rather than a guess -- a stereo or
# 16-bit file silently misread is a sound that plays as static.
function Get-WavSamples([string]$path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    if ($b.Length -lt 44 -or
        [System.Text.Encoding]::ASCII.GetString($b, 0, 4) -ne 'RIFF' -or
        [System.Text.Encoding]::ASCII.GetString($b, 8, 4) -ne 'WAVE') {
        throw "$path is not a RIFF/WAVE file"
    }
    $i = 12
    $channels = 0; $rate = 0; $bits = 0
    $data = $null
    while ($i + 8 -le $b.Length) {
        $id  = [System.Text.Encoding]::ASCII.GetString($b, $i, 4)
        $len = [BitConverter]::ToInt32($b, $i + 4)
        $body = $i + 8
        if ($id -eq 'fmt ') {
            $channels = [BitConverter]::ToInt16($b, $body + 2)
            $rate     = [BitConverter]::ToInt32($b, $body + 4)
            $bits     = [BitConverter]::ToInt16($b, $body + 14)
        } elseif ($id -eq 'data') {
            $data = New-Object byte[] $len
            [Array]::Copy($b, $body, $data, 0, $len)
        }
        $i = $body + $len + ($len -band 1)
    }
    if ($null -eq $data) { throw "$path has no data chunk" }
    if ($channels -ne 1 -or $bits -ne 8) {
        throw ("$path is ${channels}ch/${bits}-bit; this bake only handles the " +
               "8-bit mono Doom lumps that assets/sounds/import-freedoom.py writes")
    }
    return [pscustomobject]@{ Rate = $rate; Data = $data }
}

function ConvertTo-Adpcm([byte[]]$pcm8) {
    $alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-'
    $n = $pcm8.Length
    # int[], not byte[]. -shl keeps the type of its LEFT operand, so a byte
    # code shifted up by 8 is 0 and the packed pair loses its first nibble --
    # the third time this project has been bitten by that rule, after the
    # sprite palette and the sprite packer. The array's type is the fix
    # because a cast at the shift is something to remember.
    # byte[]가 아니라 int[]입니다. -shl은 *왼쪽* 피연산자의 타입을 유지하므로 byte 코드를
    # 8비트 올리면 0이 되고 묶인 쌍은 첫 니블을 잃습니다. 스프라이트 팔레트와 패커에 이어
    # 이 규칙에 당한 세 번째입니다. 시프트 지점의 캐스팅은 기억해야 하는 것이므로 배열의
    # 타입 자체를 고칩니다.
    $codes = New-Object int[] $n

    $pred = 0; $ix = 0
    for ($i = 0; $i -lt $n; $i++) {
        # 8-bit unsigned centres on 128; the codec works in signed 16.
        $target = ($pcm8[$i] - 128) * 256
        $st = $AdpcmStep[$ix]
        $diff = $target - $pred
        $code = 0
        if ($diff -lt 0) { $code = 8; $diff = -$diff }
        $t = $st
        if ($diff -ge $t) { $code = $code -bor 4; $diff -= $t }
        $t = $t -shr 1
        if ($diff -ge $t) { $code = $code -bor 2; $diff -= $t }
        $t = $t -shr 1
        if ($diff -ge $t) { $code = $code -bor 1 }

        # Reconstruct exactly as the decoder will, and track THAT rather than
        # the input: an open-loop encoder drifts away from the decoder's state
        # and the error compounds over a whole second of audio.
        # 입력이 아니라 디코더가 재구성할 값을 따라갑니다. 개루프 인코더는 디코더의 상태
        # 에서 멀어지고 그 오차는 1초짜리 오디오 전체에 걸쳐 누적됩니다.
        $d = $st -shr 3
        if ($code -band 4) { $d += $st }
        if ($code -band 2) { $d += $st -shr 1 }
        if ($code -band 1) { $d += $st -shr 2 }
        if ($code -band 8) { $pred -= $d } else { $pred += $d }
        if ($pred -gt 32767) { $pred = 32767 }
        elseif ($pred -lt -32768) { $pred = -32768 }
        $ix += $AdpcmNext[$code]
        if ($ix -lt 0) { $ix = 0 } elseif ($ix -gt 88) { $ix = 88 }

        $codes[$i] = $code
    }

    # Three nibbles per two characters, the sprite codec's packing.
    $out = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $n; $i += 3) {
        $c0 = $codes[$i]
        $c1 = if ($i + 1 -lt $n) { $codes[$i + 1] } else { 0 }
        $c2 = if ($i + 2 -lt $n) { $codes[$i + 2] } else { 0 }
        $v = ($c0 -shl 8) -bor ($c1 -shl 4) -bor $c2
        [void]$out.Append($alphabet[$v -shr 6]).Append($alphabet[$v -band 63])
    }

    # DECODE WHAT WAS JUST ENCODED. The C decoder is a separate implementation
    # in a separate language and they only meet in the built game, so the one
    # direction nothing else checks is checked here -- the same round trip the
    # sprites get, and for the same reason.
    # 방금 인코딩한 것을 되돌려 디코딩합니다. C 디코더는 다른 언어의 별개 구현이며 둘은
    # 빌드된 게임에서만 만나므로, 다른 무엇도 검사하지 않는 방향을 이곳에서 검사합니다.
    $s2 = $out.ToString()
    $back = New-Object int[] $n
    $k = 0
    for ($i = 0; $i -lt $s2.Length - 1; $i += 2) {
        $v = $alphabet.IndexOf($s2[$i]) * 64 + $alphabet.IndexOf($s2[$i + 1])
        foreach ($sh in 8, 4, 0) {
            if ($k -lt $n) { $back[$k++] = ($v -shr $sh) -band 15 }
        }
    }
    for ($i = 0; $i -lt $n; $i++) {
        if ($back[$i] -ne $codes[$i]) {
            throw ("ADPCM packing does not round-trip: nibble $i went in as " +
                   "$($codes[$i]) and came back as $($back[$i]).")
        }
    }

    return $s2
}


$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('/* GENERATED by bake.ps1 from the assets directory -- do not edit.')
[void]$sb.AppendLine('   Edit the .txt files and rebuild; a HOT_RELOAD build reads them live. */')
[void]$sb.AppendLine('#ifndef GEN_ASSETS_H')
[void]$sb.AppendLine('#define GEN_ASSETS_H')
[void]$sb.AppendLine()

$report = @()

# Sampled sounds, appended to the synthesised library.
#
# Emitted AFTER the recipes so a name that has both ends up with the sample:
# the loader takes `s <name>` as "select or create", and a `w` line attaches
# audio to whichever sound that is. So importing a sample for `shot` overrides
# the shotgun recipe without deleting it, and deleting the WAV brings the
# recipe straight back -- the same bargain the drawn sprites strike with the
# generated creatures.
#
# 합성 라이브러리 뒤에 붙입니다. 그래야 둘 다 가진 이름은 샘플을 갖게 됩니다. 로더는
# `s <name>`을 "선택하거나 생성"으로 취급하고 `w` 줄이 그 사운드에 오디오를 붙입니다.
# 따라서 `shot`의 샘플을 들여와도 샷건 레시피는 지워지지 않고, WAV를 지우면 레시피가
# 곧바로 돌아옵니다. 그려진 스프라이트가 생성된 생물과 맺는 것과 같은 거래입니다.
$soundDir = Join-Path $root 'assets\sounds'
$sampleText = ''
$mapPath = Join-Path $soundDir 'sounds.map'
if ((Test-Path $soundDir) -and (Test-Path $mapPath)) {
    # One lump can serve several of our sounds -- `impact`, `ehit`, `hbite` and
    # `hbiteb` are all the same punch -- so encode each WAV once and let the
    # names share it. Encoding per name would carry the same audio four times.
    # 하나의 럼프가 우리 사운드 여럿을 담당할 수 있으므로 WAV마다 한 번만 인코딩하고
    # 이름들이 그것을 공유합니다. 이름마다 인코딩하면 같은 오디오를 네 번 나릅니다.
    $encoded = @{}
    $sampleCount = @{}
    foreach ($wav in (Get-ChildItem $soundDir -Filter *.wav | Sort-Object Name)) {
        $lump = [System.IO.Path]::GetFileNameWithoutExtension($wav.Name)
        $w = Get-WavSamples $wav.FullName
        if ($w.Rate -ne 11025) {
            throw ("$($wav.Name) is $($w.Rate)Hz. The mixer runs at 44100 and " +
                   "steps one source sample every four, so 11025 is the only rate " +
                   "that needs no resampler -- re-run assets/sounds/import-freedoom.py.")
        }
        $encoded[$lump]     = ConvertTo-Adpcm $w.Data
        $sampleCount[$lump] = $w.Data.Length
    }

    $used = @{}
    foreach ($line in (Get-Content $mapPath)) {
        $line = $line.Trim()
        if (-not $line -or $line.StartsWith('#')) { continue }
        $t = $line -split '\s+'
        if ($t.Count -lt 2) { continue }
        $name = $t[0]; $lump = $t[1]
        if (-not $encoded.ContainsKey($lump)) {
            throw ("sounds.map points '$name' at '$lump', which has no WAV in " +
                   "assets/sounds. Re-run assets/sounds/import-freedoom.py.")
        }
        $sampleText += "s $name`nw $($sampleCount[$lump]) $($encoded[$lump])`n"
        $used[$lump] = $true
    }

    foreach ($lump in $encoded.Keys) {
        if (-not $used.ContainsKey($lump)) {
            Write-Host ("  note: assets/sounds/$lump.wav is not named by " +
                        "sounds.map, so nothing plays it") -ForegroundColor Yellow
        }
    }

    if ($sampleText) {
        $report += [pscustomobject]@{
            Asset  = 'sounds\*.wav'
            Source = ($sampleCount.Values | Measure-Object -Sum).Sum
            Baked  = $sampleText.Length
            Saved  = ("{0} lumps, {1} names, 4-bit ADPCM at 11025Hz" -f
                      $encoded.Count, $used.Count)
        }
    }
}


foreach ($s in $sets) {
    $path = Join-Path $root $s.File
    if (-not (Test-Path $path)) { throw "Missing asset file: $path" }

    $raw  = Get-Content $path -Raw
    $mini = ConvertTo-Minified $raw
    # The recipe's own size, before the samples are appended: they are reported
    # on their own row, and counting them twice would put 84KB into the total
    # that is not there.
    # 샘플을 붙이기 전 레시피 자체의 크기입니다. 샘플은 자기 행에서 보고되며, 두 번 세면
    # 실제로는 없는 84KB가 합계에 들어갑니다.
    $recipeLen = $mini.Length
    if ($s.Name -eq 'ASSET_SOUNDS' -and $sampleText) {
        $mini = $mini + ' ' + (ConvertTo-Minified $sampleText)
    }

    # No escaping table needed beyond these two: the grammar is integers and
    # bare words, so a quote or backslash would be a mistake anyway.
    $esc = $mini.Replace('\', '\\').Replace('"', '\"')

    [void]$sb.AppendLine("static const char $($s.Name)[] =")
    # Wrap so the generated header stays readable in a diff.
    $chunk = 76
    for ($i = 0; $i -lt $esc.Length; $i += $chunk) {
        $len  = [Math]::Min($chunk, $esc.Length - $i)
        $part = $esc.Substring($i, $len)
        [void]$sb.AppendLine("    `"$part`"")
    }
    [void]$sb.AppendLine('    ;')
    [void]$sb.AppendLine()

    $report += [pscustomobject]@{
        Asset = Split-Path $s.File -Leaf
        Source = $raw.Length
        Baked  = $recipeLen
        Saved  = "$([math]::Round((1 - $recipeLen / $raw.Length) * 100))%"
    }
}

# PNG -> the binary, unchanged.
#
# THIS USED TO QUANTISE. Every drawing was cut to fifteen colours per subject
# by median cut and run-length packed into text, and the note that stood here
# justified it like this: shipping the PNG would mean carrying a decoder,
# "roughly 15KB of inflate and filter reconstruction, to save 3KB of pixels."
#
# Both halves of that were wrong, and it took measuring to see it.
#
# The inflate was already here. gen_assets.h deflates every asset and
# src/inflate.c expands them, so DEFLATE -- which is the whole of PNG's
# compression -- was paid for before a single sprite was drawn. What a decoder
# actually adds over that is src/png.c: a chunk walk and five predictors.
#
# And it was not 3KB. Measured across the 53 drawings, in the binary after
# deflate: the quantised form is 95,291 bytes and the PNGs are 212,269. So the
# lossy step was buying 117KB -- 7.9% of a floppy with 71% of it unspent --
# and paying for it with a brute cut from 707 colours to 15.
#
# It also cost the one thing no measurement shows: sprites were the last
# authored asset that could not hot reload, because what the game held was not
# the drawing but a derivative only this script knew how to produce. Now the
# file on disk and the bytes in the binary are the same format.
#
# The stream is one record per drawing, and the length is what separates them
# rather than a delimiter a PNG could contain -- the same shape the .map blob
# uses, and for the same reason:
#
#   s <name> <bytes> <that many bytes of PNG>
#
# 이것은 예전에 양자화했습니다. 모든 그림이 median cut으로 주제당 15색으로 잘려 런렝스로
# 텍스트에 담겼고, 이 자리에 있던 설명은 이렇게 정당화했습니다. PNG를 그대로 실으면 디코더를
# 져야 하는데 "대략 15KB의 inflate와 필터 복원을 들여 3KB의 픽셀을 아끼는 일"이라고.
#
# 그 두 절반이 모두 틀렸고, 알아보는 데 측정이 필요했습니다.
#
# inflate는 이미 이곳에 있었습니다. gen_assets.h가 모든 에셋을 deflate하고 src/inflate.c가
# 펼치므로, PNG 압축의 전부인 DEFLATE는 스프라이트가 한 장 그려지기도 전에 이미 지불되어
# 있었습니다. 디코더가 그 위에 실제로 더하는 것은 src/png.c, 즉 청크 훑기와 예측기 다섯입니다.
#
# 그리고 3KB가 아니었습니다. 53장에 대해 deflate 이후 바이너리에서 재면 양자화된 형태가
# 95,291바이트이고 PNG가 212,269바이트입니다. 즉 손실 단계가 사들이고 있던 것은 117KB이며,
# 그것은 71%가 비어 있는 플로피의 7.9%였고, 그 대가로 707색짜리 브루트를 15색으로 잘랐습니다.
#
# 어떤 측정에도 나타나지 않는 것 하나도 함께 치렀습니다. 스프라이트는 핫 리로드가 되지 않는
# 마지막 저작 에셋이었습니다. 게임이 들고 있던 것이 그림이 아니라 이 스크립트만이 만들 줄
# 아는 파생물이었기 때문입니다. 이제 디스크의 파일과 바이너리의 바이트가 같은 형식입니다.
#
# 스트림은 그림마다 레코드 하나이며, 그것들을 가르는 것은 PNG가 담을 수도 있는 구분자가
# 아니라 *길이*입니다. .map 블롭과 같은 형태이고 이유도 같습니다.


# Every .obj in assets\ becomes one entry in a single mesh library, so the
# game has one text blob to parse rather than a file table to manage.
$meshText = ''
foreach ($obj in (Get-ChildItem (Join-Path $root 'assets') -Filter *.obj -ErrorAction SilentlyContinue)) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($obj.Name)
    $conv = ConvertFrom-Obj $obj.FullName $name
    $meshText += $conv.Text
    $report += [pscustomobject]@{
        Asset  = $obj.Name
        Source = $obj.Length
        Baked  = $conv.Text.Length
        Saved  = "$([math]::Round((1 - $conv.Text.Length / $obj.Length) * 100))% ($($conv.Verts)v $($conv.Tris)t)"
    }
}

# Real newlines would terminate the C string literal. The parser treats
# newlines as ordinary whitespace, so collapsing them costs nothing.
$escMesh = $meshText.Replace('\', '\\').Replace('"', '\"').
                     Replace("`r", '').Replace("`n", ' ')
[void]$sb.AppendLine('static const char ASSET_MESHES[] =')
if ($escMesh.Length -eq 0) {
    [void]$sb.AppendLine('    ""')
} else {
    for ($i = 0; $i -lt $escMesh.Length; $i += 76) {
        $len = [Math]::Min(76, $escMesh.Length - $i)
        [void]$sb.AppendLine("    `"$($escMesh.Substring($i, $len))`"")
    }
}
[void]$sb.AppendLine('    ;')
[void]$sb.AppendLine()
# --- attribution, checked rather than remembered ---------------------------
#
# The artwork this project uses comes from Freedoom, whose 3-clause BSD licence
# requires its notice to accompany BINARY distributions as well as source. SFPS
# ships as one executable with nothing beside it, so the only place the notice
# can accompany anything is inside the game -- it is drawn on the CREDITS screen
# in src/scene.c.
#
# Nothing about the build breaks if that notice is deleted. The game compiles,
# runs and looks identical; the only difference is that shipping it is no longer
# permitted. That is precisely the kind of fault this project refuses to leave
# to memory, so the build asserts it: art present without its notice is a failed
# build rather than a licence violation discovered later.
#
# Keyed on sprites actually being present, so a checkout with no art -- which is
# how this ships today -- is under no obligation and pays nothing.
#
# 이 프로젝트가 사용하는 아트는 Freedoom에서 왔으며, 그 3-clause BSD 라이선스는 소스뿐
# 아니라 *바이너리* 배포에도 고지가 동반될 것을 요구합니다. SFPS는 옆에 아무것도 없는 실행
# 파일 하나로 배포되므로, 고지가 동반될 수 있는 유일한 장소는 게임 안입니다.
#
# 그 고지를 지워도 빌드는 아무 문제 없이 됩니다. 게임은 컴파일되고 실행되며 겉보기도 같고,
# 달라지는 것은 배포가 더 이상 허용되지 않는다는 사실뿐입니다. 바로 이런 종류의 결함을 이
# 프로젝트는 기억에 맡기지 않으므로, 빌드가 이를 단언합니다. 고지 없는 아트는 나중에 발견될
# 라이선스 위반이 아니라 실패한 빌드입니다.
# ANY Freedoom asset triggers this, not just the artwork. The notice is owed
# for the audio on exactly the same terms, and a guard that only watches
# assets\sprites\ would have gone quiet the moment the art was the only thing
# it covered -- which is the failure mode of writing a check against the
# example rather than against the rule.
# 아트뿐 아니라 *어떤* Freedoom 에셋이든 이 검사를 발동시킵니다. 고지는 오디오에 대해서도
# 정확히 같은 조건으로 요구되며, assets\sprites\만 지켜보는 가드는 아트가 그것이 다루는
# 유일한 대상이 아니게 되는 순간 조용해졌을 것입니다. 규칙이 아니라 사례에 대고 검사를
# 쓰는 일의 전형적인 실패입니다.
# THE TABLE, and the loop under it, replace a check written against one work.
# The comment above already names the failure that shape has -- "writing a check
# against the example rather than against the rule" -- and a second work is what
# turns that from a prediction into a diff. Everything inside the loop is the
# Freedoom guard unchanged; only the three things that were Freedoom's are now
# the row's: what makes it apply, which licence backs it, and what to say.
#
# 표와 그 아래의 루프가, 저작물 하나에 대고 쓰인 검사를 대체합니다. 위의 주석이 그 형태의
# 실패를 이미 이름 짓고 있으며("규칙이 아니라 사례에 대고 검사를 쓰는 일"), 두 번째 저작물이
# 그것을 예측에서 diff로 바꿉니다. 루프 안의 모든 것은 그대로의 Freedoom 가드이고, Freedoom의
# 것이었던 셋만이 이제 행의 것입니다. 무엇이 이 검사를 적용시키는가, 어떤 라이선스가 뒤에
# 있는가, 그리고 무엇이라고 말하는가입니다.
$licensedWorks = @(
    @{
        Name       = 'Freedoom'
        Where      = 'assets\sprites\, assets\sounds\'
        Dirs       = @('assets\sprites', 'assets\sounds')
        Include    = @('*.png', '*.wav')
        Licence    = (Join-Path $root 'docs\LICENSE-Freedoom.txt')
        LicenceRel = 'docs/LICENSE-Freedoom.txt'
        Restore    = 'https://github.com/freedoom/freedoom/blob/master/COPYING.adoc'
    }
    # The imported arena. Keyed on the .map being present rather than on its
    # name: a second LibreQuake map would owe the same notice, and a guard that
    # watched for `lqdm13` would go quiet the day somebody imported another one.
    # What marks a file as theirs is the line import-librequake.py writes into
    # it, which is also the line that cannot survive somebody replacing the map
    # with one of their own.
    # 가져온 아레나입니다. 이름이 아니라 .map의 존재를 기준으로 삼습니다. 두 번째 LibreQuake
    # 맵도 같은 고지를 빚지며, `lqdm13`을 지켜보는 가드는 누군가 다른 것을 가져오는 날
    # 조용해집니다. 어떤 파일이 그들의 것인지 표시하는 것은 import-librequake.py가 그 안에 쓰는
    # 줄이고, 그 줄은 누군가 그 맵을 자기 것으로 바꾸면 살아남지 못하는 줄이기도 합니다.
    @{
        Name       = 'LibreQuake'
        Where      = 'assets\maps\'
        Dirs       = @('assets\maps')
        Include    = @('*.map')
        Marker     = 'from LibreQuake'
        Licence    = (Join-Path $root 'docs\LICENSE-LibreQuake.txt')
        LicenceRel = 'docs/LICENSE-LibreQuake.txt'
        Restore    = 'https://github.com/lavenderdotpet/LibreQuake/blob/main/docs/COPYING'
    }
)

foreach ($work in $licensedWorks) {
    $found = @()
    foreach ($d in $work.Dirs) {
        $dir = Join-Path $root $d
        if (-not (Test-Path $dir)) { continue }

        # THE `\*` IS LOAD-BEARING AND ITS ABSENCE MADE THIS GUARD INERT.
        # -Include filters the items a path EXPANDS to, and a bare directory
        # expands to the directory. Without -Recurse or a trailing wildcard it
        # matches nothing at all -- silently, with no error and no warning, so
        # `$found.Count` was 0 and the whole check `continue`d past every file
        # it was written to watch.
        #
        # Measured on this tree at the moment it was found: 52 PNGs under
        # assets\sprites\ and the guard saw none of them. README.md said the
        # check had been "verified by removing the line and watching the build
        # stop", and that had stopped being true. A guard with false positives
        # gets switched off by a person; a guard with false negatives switches
        # itself off and keeps printing that everything is fine.
        #
        # It is checked now rather than believed: the mutation below the table
        # -- shorten either notice, run the bake, watch it throw -- is the test,
        # and it is written down because a guard nobody can see fail is a guard
        # nobody can see pass either.
        #
        # `\*`는 구조적으로 필요하며, 그것의 부재가 이 가드를 무력하게 만들었습니다.
        # -Include는 경로가 *전개된* 항목들을 거르는데, 맨 디렉토리는 그 디렉토리로 전개됩니다.
        # -Recurse도 끝의 와일드카드도 없으면 아무것도 일치하지 않습니다. 조용히, 오류도 경고도
        # 없이 그렇게 되므로 `$found.Count`는 0이었고, 검사 전체가 자신이 지켜보려고 쓰인 모든
        # 파일을 지나쳐 `continue`했습니다.
        #
        # 발견된 시점에 이 트리에서 측정한 값: assets\sprites\ 아래 PNG 52개, 그리고 가드는 그중
        # 하나도 보지 못했습니다. README.md는 이 검사가 "줄을 지우고 빌드가 멈추는 것을 보아
        # 확인했다"고 적고 있었고, 그것은 참이기를 그만둔 상태였습니다. 거짓 양성을 내는 가드는
        # 사람이 끄지만, 거짓 음성을 내는 가드는 스스로 꺼지고 계속 이상 없다고 인쇄합니다.
        #
        # 이제는 믿지 않고 검사합니다. 표 아래의 변이(둘 중 아무 고지나 줄이고 베이크를 돌려
        # 예외가 나는지 보기)가 그 검사이며, 적어 두는 이유는 실패하는 것을 아무도 볼 수 없는
        # 가드는 통과하는 것도 아무도 볼 수 없기 때문입니다.
        $hits = @(Get-ChildItem (Join-Path $dir '*') -Include $work.Include -File `
                  -ErrorAction SilentlyContinue |
                  Where-Object { $_.Name -notlike '_*' })
        # A marker narrows "every file of this type" to "the files that came
        # from this work". Without one, every .map in the tree would be claimed
        # by LibreQuake -- including the three this project authored.
        # 표식이 "이 종류의 모든 파일"을 "이 저작물에서 온 파일"로 좁힙니다. 표식이 없으면
        # 트리의 모든 .map이 LibreQuake의 것으로 주장되며, 이 프로젝트가 직접 만든 셋도
        # 그렇게 됩니다.
        if ($work.Marker) {
            $hits = @($hits | Where-Object {
                (Get-Content $_.FullName -TotalCount 12 -ErrorAction SilentlyContinue) `
                    -join "`n" -match [regex]::Escape($work.Marker)
            })
        }
        $found += $hits
    }
    if ($found.Count -eq 0) { continue }

        $licPath = $work.Licence
        if (-not (Test-Path $licPath)) {
            throw ("$($work.Name) assets are present but $($work.LicenceRel) is " +
                   "missing. The licence text must be kept with the project.")
        }

        # Checking that a few lines are PRESENT is not enough, and the first
        # version of this guard proved it: the notice passed while its warranty
        # disclaimer had been shortened to "ANY WARRANTIES ARE DISCLAIMED",
        # which is a summary of the paragraph the licence requires verbatim. So
        # compare against the licence file itself -- pull the string literals
        # out of the NOTICE table, flatten both to single spaces, and require
        # the whole span from the copyright line to SUCH DAMAGE. to appear
        # word for word. Line breaks and indentation are free to differ,
        # because those are layout; the words are not.
        #
        # 몇 줄이 *존재하는지* 확인하는 것으로는 부족하며, 이 가드의 첫 판본이 그것을
        # 증명했습니다. 보증 부인 조항이 "ANY WARRANTIES ARE DISCLAIMED"로 요약된
        # 상태에서 고지가 검사를 통과했는데, 그 문단이야말로 라이선스가 전문 그대로
        # 실으라고 요구하는 부분입니다. 그래서 라이선스 파일 자체와 대조합니다. 줄바꿈과
        # 들여쓰기는 배치이므로 달라도 되지만, 단어는 그렇지 않습니다.
        # -Encoding UTF8 on both reads is required, not tidiness. Windows
        # PowerShell defaults Get-Content to the ANSI codepage, which turns the
        # licence's © into mojibake -- and a comparison against mojibake fails
        # on the copyright line every time, reporting a violation that is not
        # there. A guard with false positives gets switched off.
        # 두 읽기 모두 -Encoding UTF8이 필요하며 이는 단정함의 문제가 아닙니다. Windows
        # PowerShell의 Get-Content는 ANSI 코드페이지를 기본값으로 삼아 라이선스의 ©를
        # 깨뜨리고, 깨진 문자와의 비교는 매번 저작권 줄에서 실패해 있지도 않은 위반을
        # 보고합니다. 거짓 양성을 내는 가드는 꺼지게 됩니다.
        $sceneSrc = Get-Content (Join-Path $root 'src\scene.c') -Raw -Encoding UTF8

        $block = [regex]::Match($sceneSrc,
                                'static const char \*NOTICE\[\] = \{(.*?)\n\s*\};',
                                'Singleline')
        if (-not $block.Success) {
            throw ("$($work.Name) assets are present ($($work.Where)) but the NOTICE " +
                   "table has gone from src/scene.c. That table is the attribution " +
                   "the BSD licence requires to ship with the binary.")
        }

        $notice = (
            [regex]::Matches($block.Groups[1].Value, '"((?:[^"\\]|\\.)*)"') |
                ForEach-Object { $_.Groups[1].Value.Replace('\"', '"') }
        ) -join ' '

        # Curly quotes and the (c) glyph differ between the licence file and a
        # 5x7 bitmap font that has neither. Everything else must match.
        # [string] casts are load-bearing: String.Replace has a (char,char)
        # overload, and PowerShell binds it on a char first argument -- so
        # replacing © with the two-character '(c)' fails to convert rather
        # than replacing anything.
        # [string] 캐스팅은 반드시 필요합니다. String.Replace에는 (char,char) 오버로드가
        # 있고 PowerShell은 첫 인자가 char이면 그쪽에 바인딩하므로, ©를 두 글자인 '(c)'로
        # 바꾸려 하면 치환이 아니라 변환 실패가 됩니다.
        function Normalise-Licence([string] $t) {
            $t = $t.Replace([string][char]0x201C, '"')
            $t = $t.Replace([string][char]0x201D, '"')
            $t = $t.Replace([string][char]0x00A9, '(c)')
            return ([regex]::Replace($t, '\s+', ' ')).Trim()
        }

        $licSrc = Get-Content $licPath -Raw -Encoding UTF8
        $span = [regex]::Match($licSrc, 'Copyright .*?SUCH DAMAGE\.', 'Singleline')
        if (-not $span.Success) {
            throw ("$($work.LicenceRel) no longer contains the BSD text this " +
                   "build checks the in-game notice against. Restore it from " +
                   "$($work.Restore)")
        }

        $want = Normalise-Licence $span.Value
        $have = Normalise-Licence $notice

        if (-not $have.Contains($want)) {
            # Say WHERE it diverges. "The notice is wrong" sends someone
            # diffing 47 lines by eye; the first differing word does not.
            $w = $want.Split(' '); $h = $have.Split(' ')
            $at = [Array]::IndexOf($h, $w[0])
            $detail = 'the notice does not contain the licence text at all'
            if ($at -ge 0) {
                for ($i = 0; $i -lt $w.Length; $i++) {
                    $got = if (($at + $i) -lt $h.Length) { $h[$at + $i] } else { '<end of notice>' }
                    if ($got -ne $w[$i]) {
                        # The words BEFORE the divergence, so the quoted
                        # context is the part that still matched and the
                        # divergence is what follows it.
                        # 어긋나기 *직전*까지의 단어들입니다. 인용된 맥락은 아직 일치하던
                        # 부분이고, 어긋난 지점은 그 다음에 옵니다.
                        $ctx = ''
                        if ($i -gt 0) {
                            $lo = [Math]::Max(0, $i - 6)
                            $ctx = '...' + (($w[$lo..($i - 1)]) -join ' ') + ' '
                        }
                        $detail = ("after '$ctx' the licence says '" + $w[$i] +
                                   "' but the notice says '" + $got + "'")
                        break
                    }
                }
            }
            throw ("$($work.Name) assets are present ($($work.Where)) but the NOTICE " +
                   "table in src/scene.c is not the licence verbatim: $detail. " +
                   "The BSD licence requires this text to be reproduced with the " +
                   "binary, and this game IS the binary -- see $($work.LicenceRel).")
        }
}

# Every .png under assets\sprites\ becomes one entry in a single sprite
# library. Sorted by name so the baked output is stable: a set that reordered
# itself between builds would produce a different palette and a needless
# recompile of everything that reads it.
#
# Grouped by SUBJECT -- the name with its trailing frame number removed, so
# imp0..imp4 are one group -- because a palette serves an animation, and the
# frames of one creature are the thing that must agree about colour. Anything
# whose name carries no frame number is its own group, which is the harmless
# reading for a one-off.
# --- editor previews are not sprites ----------------------------------------
#
# tools\matdump.c renders the materials the fragment shader computes and writes
# one PNG each into assets\sprites\, because that is the directory TrenchBroom's
# material browser reads and a face's material name IS its file name -- which is
# exactly why the `_` escape below cannot be used for them. A preview called
# `_pwindow` would put `_pwindow` on the face and resolve to nothing in game.
#
# They must not be baked. A procedural material has no pixels the game ever
# reads: tex.c's mat_make calls tex_make only when a recipe has no `proc`, so a
# PNG named after one is a preview and nothing else. Baking it would spend the
# floppy on an image nothing draws -- the contact-sheet mistake below in a new
# costume, and at 256x256 apiece these are 1.9MB of it, larger than the budget.
#
# THE RULE IS BY NAME, NOT BY PREFIX. `p*` would be one character and would
# silently swallow a future sprite that happens to start with p. Asking the
# recipe text which materials carry `proc` is the same question matdump.c asks
# to decide what to write, so the two cannot drift apart -- and if a recipe ever
# stops being procedural, both change together without either being edited.
#
# 에디터 미리보기는 스프라이트가 아닙니다.
#
# tools\matdump.c는 프래그먼트 셰이더가 계산하는 재질을 렌더링해 PNG를 하나씩
# assets\sprites\에 씁니다. 그곳이 TrenchBroom의 재질 브라우저가 읽는 디렉터리이고 면의
# 재질 이름이 곧 *파일 이름*이기 때문이며, 바로 그래서 아래의 `_` 회피책을 쓸 수 없습니다.
# `_pwindow`라는 미리보기는 면에 `_pwindow`를 얹고 게임에서는 아무것도 해석되지 않습니다.
#
# 이들은 구워져서는 안 됩니다. 절차적 재질에는 게임이 읽는 픽셀이 없습니다. tex.c의
# mat_make는 레시피에 `proc`가 없을 때에만 tex_make를 부르므로, 그 이름의 PNG는 미리보기일
# 뿐입니다. 그것을 구우면 아무도 그리지 않는 이미지에 플로피를 쓰게 됩니다. 아래의 대조 시트
# 사고가 옷만 갈아입은 것이며, 256x256짜리 열 장이면 1.9MB로 예산 자체보다 큽니다.
#
# *규칙은 접두사가 아니라 이름으로 판단합니다.* `p*`는 한 글자면 되지만 앞으로 p로 시작하는
# 스프라이트를 조용히 삼킵니다. 어느 재질이 `proc`를 지녔는지 레시피 텍스트에 묻는 것은
# matdump.c가 무엇을 쓸지 정할 때 던지는 질문과 같으므로 둘이 어긋날 수 없습니다. 그리고
# 어떤 레시피가 절차적이기를 그만두면 어느 쪽도 편집되지 않은 채 함께 바뀝니다.
function Get-ProceduralMaterials([string]$recipePath) {
    $names = @{}
    if (-not (Test-Path $recipePath)) { return $names }
    $cur = $null
    foreach ($line in Get-Content $recipePath) {
        $line = ($line -replace '#.*$', '').Trim()
        if (-not $line) { continue }
        $tok = $line -split '\s+'
        if ($tok[0] -eq 't' -and $tok.Count -ge 2) {
            $cur = $tok[1]
        } elseif ($tok[0] -eq 'proc' -and $cur) {
            $names[$cur] = $true
            $cur = $null
        }
    }
    return $names
}
$procMats = Get-ProceduralMaterials (Join-Path $root 'assets\textures.txt')

$spriteBytes = New-Object System.Collections.Generic.List[byte]
$spriteDir   = Join-Path $root 'assets\sprites'
$skipped     = 0

if (Test-Path $spriteDir) {
    # NOT RECURSIVE, and that is load-bearing rather than incidental:
    # assets\sprites\.freedoom-walls holds the source patches import-walls.py
    # fetched, and TrenchBroom's material browser -- which DOES recurse -- has
    # already put one of those on a wall once. Baking them would make the
    # mistake permanent instead of merely visible.
    # 재귀하지 않으며, 그것은 부수적인 것이 아니라 구조적입니다. assets\sprites\.freedoom-walls는
    # import-walls.py가 받아 둔 원본 패치를 담고 있고, *재귀하는* TrenchBroom의 재질 브라우저는
    # 이미 한 번 그중 하나를 벽에 올린 적이 있습니다. 그것들을 구우면 그 실수가 눈에 보이는
    # 데서 그치지 않고 영구해집니다.
    #
    # A LEADING UNDERSCORE STILL MEANS "NOT A SPRITE". sprite.c ignores a name
    # that matches nothing, but ignoring happens at DECODE time -- the drawing
    # would still ride in .rdata for the life of the binary. The importer's
    # --preview contact sheet landed here once and cost 411KB of 1.44MB while
    # never being drawn.
    # 앞의 밑줄은 여전히 "스프라이트가 아님"입니다. sprite.c는 아무것과도 맞지 않는 이름을
    # 무시하지만 무시는 *디코드* 시점에 일어나므로, 그림은 바이너리가 사는 내내 .rdata에
    # 실려 다닙니다. 임포터의 --preview 대조 시트가 한 번 이곳에 떨어져, 한 번도 그려지지
    # 않으면서 1.44MB 중 411KB를 차지했습니다.
    foreach ($png in (Get-ChildItem $spriteDir -Filter *.png |
                      Where-Object { $_.Name -notlike '_*' } | Sort-Object Name)) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($png.Name)
        if ($procMats.ContainsKey($name)) { $skipped++; continue }

        $raw = [IO.File]::ReadAllBytes($png.FullName)

        # The dimensions come out of IHDR rather than out of a Bitmap: this
        # runs once per drawing and only the size report reads the answer, so
        # loading GDI+ for it would be the slowest part of the bake serving the
        # least important line of its output.
        # 크기는 Bitmap이 아니라 IHDR에서 나옵니다. 그림마다 한 번 실행되고 그 답을 읽는 것은
        # 크기 보고뿐이므로, 그것을 위해 GDI+를 띄우면 베이크에서 가장 느린 부분이 출력에서
        # 가장 덜 중요한 줄을 섬기게 됩니다.
        $w = ($raw[16] -shl 24) -bor ($raw[17] -shl 16) -bor ($raw[18] -shl 8) -bor $raw[19]
        $h = ($raw[20] -shl 24) -bor ($raw[21] -shl 16) -bor ($raw[22] -shl 8) -bor $raw[23]

        # THE NAME MUST BE ASCII AND MUST NOT CONTAIN A SPACE, because the
        # header is read with the same tokeniser every other asset uses and a
        # space inside the name would shift the length field one token to the
        # left -- which would then be read as the name, and the record after it
        # would begin in the middle of a picture.
        # 이름은 ASCII여야 하고 공백을 담아서는 안 됩니다. 헤더를 다른 모든 에셋과 같은
        # 토크나이저로 읽는데, 이름 안의 공백은 길이 필드를 토큰 하나만큼 왼쪽으로 밀고, 그러면
        # 그것이 이름으로 읽히며 다음 레코드가 그림 한가운데에서 시작합니다.
        foreach ($ch in $name.ToCharArray()) {
            if ([int]$ch -gt 127 -or $ch -eq ' ') {
                throw ("$($png.Name): a sprite name must be ASCII with no spaces.")
            }
        }

        $spriteBytes.AddRange([Text.Encoding]::ASCII.GetBytes("s $name $($raw.Length) "))
        $spriteBytes.AddRange($raw)
        $spriteBytes.Add([byte]32)

        $report += [pscustomobject]@{
            Asset  = "sprites\$($png.Name)"
            Source = $png.Length
            Baked  = $raw.Length
            Saved  = "$([math]::Round((1 - $raw.Length / ($w * $h * 4)) * 100))% vs raw (${w}x${h} png)"
        }
    }
}


# --- the reference tools\pngtest.c checks the game's decoder against ---------
#
# TWO DECODERS THAT AGREE ARE NOT AGREEING BY ACCIDENT. src\png.c reads these
# files at run time; GDI+ reads them here. They share no code and no author, so
# a filter reconstructed against the wrong neighbour, a row read one byte off or
# a Paeth predictor that picks the second-nearest shows up as a disagreement --
# and every one of those failures otherwise produces an IMAGE, which is a thing
# that never announces it is wrong.
#
# A running sum rather than a hash: what it has to catch is a decoder that is
# mistaken, not one that is hostile, and a sum says which drawing differs
# without needing a second table to say what the right answer was.
#
# LockBits rather than GetPixel, which the palette pass above uses. Half a
# million calls across the fifty-three drawings is most of a minute in
# PowerShell; one locked scan is a few milliseconds, and this file exists to be
# regenerated on every build rather than to be worth waiting for.
#
# 두 디코더가 일치한다면 그것은 우연이 아닙니다. src\png.c가 실행 시점에 이 파일들을 읽고,
# 이곳에서는 GDI+가 읽습니다. 코드도 저자도 공유하지 않으므로, 잘못된 이웃에 대고 복원된
# 필터나 한 바이트 어긋나게 읽힌 행이나 두 번째로 가까운 것을 고르는 Paeth 예측기가 불일치로
# 드러납니다. 그리고 그 실패들은 하나같이 *이미지*를 만들어 내는데, 이미지는 자기가 틀렸다고
# 결코 알리지 않습니다.
#
# 해시가 아니라 단순 합인 이유는, 잡아야 할 것이 적대적인 디코더가 아니라 틀린 디코더이기
# 때문입니다. 합은 어느 그림이 다른지를 말해 주며, 정답이 무엇이었는지 적어 둘 두 번째 표를
# 필요로 하지 않습니다.
$refDir = Join-Path $root 'build'
if (-not (Test-Path $refDir)) { [void](New-Item -ItemType Directory -Path $refDir) }
$refLines = New-Object System.Collections.ArrayList
Add-Type -AssemblyName System.Drawing
foreach ($png in (Get-ChildItem $spriteDir -Filter *.png |
                  Where-Object { $_.Name -notlike '_*' } | Sort-Object Name)) {
    $nm = [System.IO.Path]::GetFileNameWithoutExtension($png.Name)
    if ($procMats.ContainsKey($nm)) { continue }

    $bmp = New-Object System.Drawing.Bitmap $png.FullName
    try {
        $rect = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                              [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
            # Stride can exceed width*4 with padding, so the sum walks rows
            # rather than the whole buffer -- padding bytes are not pixels and
            # the game's decoder never sees any.
            # Stride는 패딩 때문에 width*4보다 클 수 있으므로 버퍼 전체가 아니라 행 단위로
            # 더합니다. 패딩 바이트는 픽셀이 아니며 게임의 디코더는 그것을 보지 못합니다.
            [long]$sum = 0
            for ($y = 0; $y -lt $bmp.Height; $y++) {
                $o = $y * $data.Stride
                for ($x = 0; $x -lt ($bmp.Width * 4); $x++) { $sum += $bytes[$o + $x] }
            }
        } finally { $bmp.UnlockBits($data) }
        [void]$refLines.Add("assets/sprites/$($png.Name) $($bmp.Width) $($bmp.Height) $sum")
    } finally { $bmp.Dispose() }
}
Set-Content (Join-Path $refDir 'png_ref.txt') ($refLines -join "`n") -Encoding ASCII
Write-Host ("  {0} drawing(s) -> build\png_ref.txt for tools\pngtest.c" -f $refLines.Count) -ForegroundColor DarkGray

if ($skipped) {
    Write-Host ("  {0} editor preview(s) in assets\sprites\ skipped -- procedural materials, not baked" -f $skipped) -ForegroundColor DarkGray
}

# --- .map levels -----------------------------------------------------------
#
# assets\maps\*.map, one file per level, exactly as TrenchBroom writes them.
# There is no container format on disk and no converter: the editor's output IS
# the asset, which is the whole argument in src\brush.h. What happens here is
# packaging, not translation -- the bytes are stripped of comments and packed
# with a length in front so the game can find one map inside one blob.
#
# WHY COMMENTS GO AND NEWLINES BECOME SPACES. The blob is emitted as a C string
# literal, and a literal cannot span lines. Every other asset here solves that
# by flattening whitespace, and .map is whitespace-delimited too -- except that
# `//` runs to the end of a line, so flattening FIRST would make the first
# comment swallow the rest of the file. So comments are removed before the
# newlines they depend on are.
#
# The stripper tracks quotes. A value may legitimately contain `//` -- Quake's
# own `wad` key holds paths -- and a line-by-line regex deleting from `//`
# onward would eat the rest of that entity. build.ps1's GLSL check carries the
# same lesson from the same mistake.
#
# assets\maps\*.map이며 레벨당 파일 하나로, TrenchBroom이 쓰는 그대로입니다. 디스크에는
# 컨테이너 형식도 변환기도 없습니다. 에디터의 출력이 곧 에셋이며, 그것이 src\brush.h의
# 논지 전체입니다. 이곳에서 일어나는 일은 번역이 아니라 포장입니다. 주석을 걷어 내고 길이를
# 앞에 붙여, 게임이 하나의 블롭 안에서 맵 하나를 찾을 수 있게 합니다.
#
# 주석을 없애고 줄바꿈을 공백으로 바꾸는 이유: 블롭은 C 문자열 리터럴로 방출되고 리터럴은
# 줄을 넘을 수 없습니다. 이곳의 다른 모든 에셋은 공백을 평탄화해 그것을 해결하며 .map도
# 공백으로 구분됩니다. 다만 `//`가 줄 끝까지 이어지므로, *먼저* 평탄화하면 첫 주석이 파일의
# 나머지를 통째로 삼킵니다. 그래서 줄바꿈이 의존하는 주석을 먼저 제거합니다.
#
# 스트리퍼는 따옴표를 추적합니다. 값에는 `//`가 정당하게 들어갈 수 있고(Quake의 `wad` 키가
# 경로를 담습니다) `//`부터 지우는 줄 단위 정규식은 그 엔티티의 나머지를 먹어 치웁니다.
# build.ps1의 GLSL 검사가 같은 실수에서 얻은 같은 교훈을 담고 있습니다.
function ConvertTo-MapText([string]$text) {
    $out = New-Object Text.StringBuilder
    $i = 0
    $n = $text.Length
    $inQuote = $false
    $lastWasSpace = $false
    while ($i -lt $n) {
        $c = $text[$i]
        if ($inQuote) {
            [void]$out.Append($c)
            if ($c -eq '"') { $inQuote = $false }
            $i++
            continue
        }
        if ($c -eq '"') {
            [void]$out.Append($c); $inQuote = $true; $lastWasSpace = $false; $i++
            continue
        }
        if ($c -eq '/' -and $i + 1 -lt $n -and $text[$i + 1] -eq '/') {
            while ($i -lt $n -and $text[$i] -ne "`n") { $i++ }
            continue
        }
        if ($c -eq ' ' -or $c -eq "`t" -or $c -eq "`r" -or $c -eq "`n") {
            if (-not $lastWasSpace) { [void]$out.Append(' '); $lastWasSpace = $true }
            $i++
            continue
        }
        [void]$out.Append($c); $lastWasSpace = $false; $i++
    }
    return $out.ToString().Trim()
}

$mapDir  = Join-Path $root 'assets\maps'

# --- maps that stay on disk and out of the binary ---------------------------
#
# THE GAME SHIPS ONE MAP. `assets\maps\` held four, and three of them were
# unreachable: `spire` was a room kept alive to be the thing the title menu is
# drawn over, `glasstower` was the boss arena before `lqdm1` replaced it, and
# `atrium` sat on a campaign chain nothing walks any more. Two were deleted.
#
# ATRIUM WAS NOT, AND THIS LIST IS WHY. It is not a level, it is a FIXTURE:
# tools/tracetest.c makes 108 assertions against its geometry -- a balcony at
# engine (-5,-5) whose top is at 3m over a floor at 0, which is "the pair of
# heights a sector could not hold" and the reason brush levels exist at all --
# plus its doors, triggers, keys, hazards and entities. No shipped map has that
# combination and lqdm1 has none of it: two doors, no triggers, no hazards, no
# keys. Deleting the file would delete the test.
#
# So it stays a file and stops being an asset. The authoring build reads
# `assets\maps\<name>.map` from disk (see data.c's HOT_RELOAD half), which is
# the build every tool is, so the fixture is exactly as available as it was.
# The shipped build reads the blob this script writes, and the blob is the
# thing that has one map in it.
#
# @note A name here is a name `data_map` cannot answer in a RELEASE build. That
#       is the point, and it is also the trap: a test that wants a map both
#       from disk and from the bake -- maptest's file-versus-baked comparison
#       is the one -- has to name a map that is baked.
#
# *게임은 맵 하나를 출하합니다.* `assets\maps\`에는 넷이 있었고 그중 셋이 도달 불가능했습니다.
# 둘은 삭제했습니다.
#
# *atrium은 삭제하지 않았고, 이 목록이 그 이유입니다.* 그것은 레벨이 아니라 *픽스처*입니다.
# tools/tracetest.c가 그 지오메트리에 대해 108개의 단언을 합니다. 엔진 좌표 (-5,-5)의 발코니,
# 바닥 0 위 3m의 윗면 -- "섹터가 담을 수 없었던 높이 한 쌍"이며 브러시 레벨이 존재하는 이유
# 자체입니다 -- 그리고 그 문·트리거·열쇠·해저드·엔티티들입니다. 출하되는 어떤 맵도 그 조합을
# 갖지 않으며 lqdm1은 그중 무엇도 갖지 않습니다. 파일을 지우는 것은 그 검사를 지우는 일입니다.
#
# 그래서 파일로 남고 에셋이기를 그만둡니다. 저작 빌드는 `assets\maps\<name>.map`을 디스크에서
# 읽으며(data.c의 HOT_RELOAD 절반) 모든 도구가 그 빌드이므로, 픽스처는 이전과 똑같이 쓸 수
# 있습니다. 출하 빌드는 이 스크립트가 쓰는 블롭을 읽고, 맵 하나가 든 것은 그 블롭입니다.
$mapsNotBaked = @('atrium')

$mapText = New-Object Text.StringBuilder
if (Test-Path $mapDir) {
    foreach ($f in (Get-ChildItem $mapDir -Filter *.map | Sort-Object Name)) {
        if ($mapsNotBaked -contains [IO.Path]::GetFileNameWithoutExtension($f.Name)) {
            continue
        }
        $rawMap = Get-Content $f.FullName -Raw
        $body   = ConvertTo-MapText $rawMap
        $nm     = [IO.Path]::GetFileNameWithoutExtension($f.Name)

        # The record length is a byte count and the reader jumps by it, so a
        # character that is not one byte would shift every map after this one.
        # Thrown rather than replaced: ASCII.GetBytes turns a non-ASCII
        # character into '?' silently, which keeps the length right and makes
        # the CONTENT wrong -- a texture name that no longer matches anything.
        # Comments are already gone by here, so this only ever fires on a name
        # or a value, which is a thing the author can fix.
        # 레코드 길이는 바이트 수이고 판독기는 그만큼 건너뛰므로, 1바이트가 아닌 문자는
        # 이 맵 뒤의 모든 맵을 밀어냅니다. 대체하지 않고 예외를 던집니다.
        # ASCII.GetBytes는 비ASCII 문자를 조용히 '?'로 바꾸는데, 그러면 길이는 맞고
        # *내용*이 틀립니다. 아무것과도 일치하지 않는 텍스처 이름이 됩니다. 이 지점에서
        # 주석은 이미 사라졌으므로, 이것이 발생하는 곳은 이름이나 값뿐이며 제작자가 고칠
        # 수 있는 대상입니다.
        foreach ($ch in $body.ToCharArray()) {
            if ([int]$ch -gt 127) {
                throw ("$($f.Name): non-ASCII character '$ch' outside a comment. " +
                       "Map names, keys, values and texture names must be ASCII.")
            }
        }

        # `m <name> <bytes> <payload>` and a space between records. The length
        # is what separates one map from the next, rather than a delimiter that
        # a map could contain.
        [void]$mapText.Append("m $nm $($body.Length) $body ")

        $report += [pscustomobject]@{
            Asset  = "maps\$($f.Name)"
            Source = $rawMap.Length
            Baked  = $body.Length
            Saved  = "$([math]::Round((1 - $body.Length / $rawMap.Length) * 100))%"
        }
    }
}

# WRAPPING CANNOT SPLIT AN ESCAPE PAIR. Every other asset here wraps at a fixed
# 76 characters and gets away with it because its grammar contains no quote and
# no backslash to escape -- the note beside $sets says exactly that. A .map is
# the first asset where escaping actually happens, and `"classname"` puts a
# `\"` every few characters. A chunk that ended between the backslash and the
# quote would emit a C literal ending in `\"`, which is an unterminated string
# and a compile error hundreds of lines into a generated file.
#
# 줄바꿈이 이스케이프 쌍을 가를 수 없습니다. 이곳의 다른 모든 에셋은 고정된 76자에서
# 줄을 바꾸고도 무사한데, 그 문법에 이스케이프할 따옴표도 역슬래시도 없기 때문입니다.
# $sets 옆의 설명이 바로 그 말을 합니다. .map은 실제로 이스케이프가 일어나는 첫 에셋이며
# `"classname"`이 몇 글자마다 `\"`를 놓습니다. 역슬래시와 따옴표 사이에서 끝나는 조각은
# `\"`로 끝나는 C 리터럴을 만들고, 그것은 종료되지 않은 문자열이자 생성된 파일 수백 줄
# 안쪽에서 터지는 컴파일 오류입니다.
$escMap = $mapText.ToString().Replace('\', '\\').Replace('"', '\"')
[void]$sb.AppendLine('static const char ASSET_MAPS[] =')
if ($escMap.Length -eq 0) {
    [void]$sb.AppendLine('    ""')
} else {
    $i = 0
    while ($i -lt $escMap.Length) {
        $len = [Math]::Min(76, $escMap.Length - $i)
        # An odd run of backslashes at the end means the last one escapes
        # whatever comes next, so the chunk stops one character earlier.
        $bs = 0
        while ($bs -lt $len -and $escMap[$i + $len - 1 - $bs] -eq '\') { $bs++ }
        if ($bs % 2 -eq 1) { $len-- }
        [void]$sb.AppendLine("    `"$($escMap.Substring($i, $len))`"")
        $i += $len
    }
}
[void]$sb.AppendLine('    ;')
[void]$sb.AppendLine()

# ASSET_SPRITES IS BYTES, NOT TEXT, so it is emitted here rather than left to
# Compress-AssetArrays below. That function recovers a C string literal and
# calls ASCII.GetBytes on it, which turns every byte over 127 into '?' -- and a
# PNG is mostly bytes over 127. The deflate is the same call it makes; what is
# skipped is the round trip through a string that cannot hold this.
#
# It is still deflated even though PNG already is. The gain is small and real
# (the record headers and the runs of similar drawings compress), and a second
# container shape in gen_assets.h would cost data.c a second code path to read
# one asset differently from all the others.
#
# ASSET_SPRITES는 텍스트가 아니라 바이트이므로, 아래의 Compress-AssetArrays에 맡기지 않고
# 이곳에서 내보냅니다. 그 함수는 C 문자열 리터럴을 복원해 ASCII.GetBytes를 부르는데, 그것은
# 127을 넘는 모든 바이트를 '?'로 바꿉니다. 그리고 PNG는 대부분 127을 넘는 바이트입니다.
# deflate는 그 함수가 하는 것과 같은 호출이며, 건너뛰는 것은 이것을 담을 수 없는 문자열을
# 거치는 왕복뿐입니다.
#
# PNG가 이미 압축되어 있는데도 여전히 deflate합니다. 이득은 작지만 실재하고(레코드 헤더와
# 비슷한 그림들의 연속이 압축됩니다), gen_assets.h에 두 번째 컨테이너 형태를 두면 data.c가
# 에셋 하나를 나머지 전부와 다르게 읽는 두 번째 경로를 지게 됩니다.
$sprRaw = $spriteBytes.ToArray()
$ms = New-Object IO.MemoryStream
$ds = New-Object IO.Compression.DeflateStream($ms, [IO.Compression.CompressionLevel]::Optimal)
$ds.Write($sprRaw, 0, $sprRaw.Length); $ds.Dispose()
$sprLz = $ms.ToArray()

[void]$sb.AppendLine("/* ASSET_SPRITES: $($sprRaw.Length) bytes of PNG records, deflated to $($sprLz.Length). */")
[void]$sb.AppendLine("#define ASSET_SPRITES_RAW $($sprRaw.Length)")
[void]$sb.AppendLine('static const unsigned char ASSET_SPRITES_LZ[] = {')
for ($i = 0; $i -lt $sprLz.Length; $i += 20) {
    $n = [Math]::Min(20, $sprLz.Length - $i)
    $row = ($sprLz[$i..($i + $n - 1)] | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
    [void]$sb.AppendLine("    $row,")
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine()


[void]$sb.AppendLine('#endif')

# --- deflate every asset array -----------------------------------------
#
# The arrays above are the readable form: C string literals a diff can show.
# They are also 304KB of mostly-base64 text, and 69% of the shipped exe was
# that .rdata -- ASSET_SPRITES uses 66 distinct characters and ASSET_SOUNDS
# 65, so every character carried under six bits in a byte that holds eight.
#
# Compressing HERE rather than at each emission site keeps this to one place
# and leaves every array above written the way it reads best. The C side
# expands them once at startup; see data.c and inflate.h.
#
# DeflateStream is .NET's, already present in the PowerShell running this
# build, so nothing is added to the toolchain and the whole set costs ~23ms.
#
# 위의 배열들은 읽기 좋은 형태입니다. diff가 보여 줄 수 있는 C 문자열 리터럴입니다. 동시에
# 대부분이 base64인 304KB의 텍스트이며, 배포되는 exe의 69%가 그 .rdata였습니다.
# ASSET_SPRITES는 66개, ASSET_SOUNDS는 65개의 문자만 쓰므로 8비트가 담기는 자리에 6비트
# 미만을 실었습니다. 배출 지점마다가 아니라 *이곳*에서 압축하면 변경점이 한 곳으로 모이고,
# 위의 모든 배열은 가장 읽기 좋은 형태로 남습니다. C 쪽은 시작 시 한 번 펼칩니다.
function Compress-AssetArrays([string]$text) {
    $rx = [regex]'(?s)static const char (ASSET_\w+)\[\] =\s*(.*?)\s*;'
    $out = $text
    $total_raw = 0; $total_lz = 0
    foreach ($m in $rx.Matches($text)) {
        $name = $m.Groups[1].Value
        # Undo the C escaping to recover the bytes the compiler would store.
        $lits = [regex]::Matches($m.Groups[2].Value, '"((?:[^"\\]|\\.)*)"')
        $sbp  = New-Object Text.StringBuilder
        foreach ($l in $lits) { [void]$sbp.Append($l.Groups[1].Value) }
        # One left-to-right pass, not two Replace calls. The pair of Replaces
        # this had before handled `\"` and mangled `\\`: the first left it
        # alone and the second dropped BOTH characters, so an escaped backslash
        # came back as nothing. No asset contained one until .map arrived --
        # Quake's `wad` key holds paths -- and the symptom would have been a
        # blob one byte short of every length recorded in it, which the map
        # reader would have followed into the middle of the next map.
        # 두 번의 Replace가 아니라 왼쪽에서 오른쪽으로 한 번 훑습니다. 이전의 Replace 쌍은
        # `\"`는 처리하고 `\\`는 망가뜨렸습니다. 첫 번째는 건드리지 않고 두 번째가 두 문자를
        # *모두* 지워, 이스케이프된 역슬래시가 아무것도 아닌 것으로 돌아왔습니다. .map이
        # 오기 전까지는 어떤 에셋도 그것을 담지 않았고(Quake의 `wad` 키가 경로를 담습니다)
        # 그 증상은 자신이 기록한 모든 길이보다 1바이트 짧은 블롭이었을 것이며, 맵 판독기는
        # 그것을 따라 다음 맵 한가운데로 들어갔을 것입니다.
        $raw = $sbp.ToString()
        $unesc = New-Object Text.StringBuilder
        $k = 0
        while ($k -lt $raw.Length) {
            if ($raw[$k] -eq '\' -and $k + 1 -lt $raw.Length) {
                [void]$unesc.Append($raw[$k + 1]); $k += 2
            } else {
                [void]$unesc.Append($raw[$k]); $k++
            }
        }
        $payload = $unesc.ToString()
        $bytes = [Text.Encoding]::ASCII.GetBytes($payload)

        $ms = New-Object IO.MemoryStream
        $ds = New-Object IO.Compression.DeflateStream($ms, [IO.Compression.CompressionLevel]::Optimal)
        $ds.Write($bytes, 0, $bytes.Length); $ds.Dispose()
        $lz = $ms.ToArray()

        $total_raw += $bytes.Length; $total_lz += $lz.Length

        $b = New-Object Text.StringBuilder
        [void]$b.AppendLine("/* ${name}: $($bytes.Length) bytes of text, deflated to $($lz.Length). */")
        [void]$b.AppendLine("#define ${name}_RAW $($bytes.Length)")
        [void]$b.AppendLine("static const unsigned char ${name}_LZ[] = {")
        for ($i = 0; $i -lt $lz.Length; $i += 20) {
            $n = [Math]::Min(20, $lz.Length - $i)
            $row = ($lz[$i..($i + $n - 1)] | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
            [void]$b.AppendLine("    $row,")
        }
        [void]$b.AppendLine('};')
        $out = $out.Replace($m.Value, $b.ToString().TrimEnd())
    }
    Write-Host ("  deflated: {0:N0} -> {1:N0} bytes ({2:N0} saved)" -f $total_raw, $total_lz, ($total_raw - $total_lz)) -ForegroundColor DarkGray
    return $out
}

# Only rewrite when the content actually changed, so an unchanged bake does
# not touch the mtime and force a needless recompile.
$new = Compress-AssetArrays $sb.ToString()
$old = if (Test-Path $outFile) { Get-Content $outFile -Raw } else { '' }
if ($new -ne $old) {
    Set-Content -Path $outFile -Value $new -Encoding utf8 -NoNewline
    Write-Host "baked -> src\gen_assets.h" -ForegroundColor DarkGray
}

$report | Format-Table -AutoSize | Out-String | Write-Host

# A TOTAL, because a per-file list hides a stowaway. A contact sheet dropped
# into assets\sprites\ was baked as a sprite and carried 411KB into .rdata
# while never being drawn; every line of the table above was individually
# unremarkable and the sum was a third of the floppy.
# 파일별 목록은 밀항자를 숨기므로 합계를 냅니다. assets\sprites\에 떨어진 대조 시트가
# 스프라이트로 구워져 한 번도 그려지지 않으면서 411KB를 .rdata로 날랐는데, 위 표의 모든
# 줄은 개별로는 평범했고 합계가 플로피의 3분의 1이었습니다.
$bakedTotal = ($report | Measure-Object -Property Baked -Sum).Sum
Write-Host ("  baked total: {0:N0} bytes across {1} assets`n" -f $bakedTotal, $report.Count)
