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
    @{ Name = 'ASSET_EFFECTS'; File = 'assets\effects.txt'  }
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

# PNG -> palette-indexed RLE text.
#
# Aseprite is the authoring tool, and its output is not what should be shipped
# for the same reason Blender's is not: a 32x32 sprite is 4096 bytes of raw
# RGBA, and shipping the PNG instead would mean carrying a PNG decoder --
# roughly 15KB of inflate and filter reconstruction -- to save 3KB of pixels.
# Converting here means the game contains no image decoder at all.
#
# The format is a stream of directives, read in one forward pass:
#
#   pal <n> <rrggbb> ...     palette for the sprites that FOLLOW it
#   s <name> <w> <h>         begin a sprite, w*h being its own size
#   o <x> <y>                where it sits in its cell  (optional)
#   m <x> <y>                muzzle, relative to the drawing  (optional)
#   r <char pairs>           RLE: one char run (1..63), one char index
#   p <char pairs>           packed: three 4-bit indices per two chars
#
# `pal` appearing more than once is not an error and is how each subject gets
# its own sixteen colours: the decoder replaces the current palette wherever it
# meets one. Without `o`, a drawing is centred in its cell and sits on the
# floor, which is what everything did before cropping existed.
#
# Both data opcodes use a 64-character printable alphabet, so every byte of
# .rdata carries six bits of picture instead of a hex digit's four.
#
# Two encodings because neither wins everywhere. RLE exploits the horizontal
# runs that separate pixel art from a photograph, and on flat art it is a
# large win -- a measured 32x32 went from 4096 bytes raw to 227. But it costs
# two characters per RUN, so heavy dithering, which breaks runs down to one or
# two pixels, makes it worse than storing the indices directly: the same size
# sprite drawn with dithering came out 1048 as RLE and 1024 flat. Both are
# encoded and the smaller is kept, per sprite.
#
# ONE PALETTE PER SUBJECT, CHOSEN RATHER THAN COLLECTED.
#
# Both halves of that replaced something that only worked by accident.
#
# The palette used to be filled first-come: walk the pixels in filename order,
# take each new colour until sixteen were spent, snap everything after that to
# the nearest. On four flat placeholder guns it was fine. On real art it is
# not a quantiser at all -- it is "whatever brute0.png happened to contain",
# and the first import produced four greens, eleven near-identical greys and
# black. The pink creature, the gold one and the shotgun all snapped to grey,
# because nothing red or gold ever got a slot. Median cut instead: split the
# colour cloud on its widest axis at the median until there are as many boxes
# as slots, and average each box. Frequency decides what gets resolution, so
# an unusual colour that covers a lot of pixels keeps its entry.
#
# And the palette is per SUBJECT rather than per set. Sixteen colours across a
# green soldier, silver armour, pink flesh, a gold floater and a grey gun is
# three colours each, which no amount of clever selection rescues. The
# original argument for sharing was that a common palette makes a set look
# like one game -- true, and it is why this held while the art was
# placeholders, but the art now comes FROM one game and arrives sharing Doom's
# palette already. Paying for coherence that the source material provides for
# free costs about three quarters of the colour resolution.
#
# The decoder needed nothing for this: it reads `pal` as a directive in a
# single forward pass and replaces the current palette wherever it appears, so
# a palette line per group is a bake-side change alone.
#
# Aseprite can export an indexed PNG, but this does not require it -- any RGB
# image is quantised to the nearest entry, so a sprite drawn without thinking
# about the palette still lands in one.

# Read a PNG once into a flat array of packed RGB, -1 where transparent.
# Separated from encoding because the palette cannot be chosen until every
# sprite in the group has been seen, and reading each file twice would double
# the slowest part of the bake.
function Get-SpritePixels([string]$path) {
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap $path
    try {
        $w = $bmp.Width; $h = $bmp.Height
        $px = New-Object int[] ($w * $h)
        $muzX = -1; $muzY = -1
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $c = $bmp.GetPixel($x, $y)
                if ($c.A -ge 128 -and $c.R -gt 240 -and $c.G -lt 16 -and $c.B -gt 240) {
                    $muzX = $x; $muzY = $y
                    $px[$y * $w + $x] = -1
                } elseif ($c.A -lt 128) {
                    $px[$y * $w + $x] = -1
                } else {
                    # [int] casts are load-bearing: -shl keeps the type of its
                    # LEFT operand, and Color.R is a Byte, so [byte]200 -shl 16
                    # is 0 rather than 13107200. Without these every colour
                    # packs down to its blue channel alone, and the palette
                    # comes out as a ramp of pure blues.
                    # [int] 캐스팅은 반드시 필요합니다. -shl은 *왼쪽* 피연산자의 타입을
                    # 유지하는데 Color.R은 Byte이므로 [byte]200 -shl 16은 13107200이
                    # 아니라 0입니다. 이것이 없으면 모든 색이 파랑 채널만 남고, 팔레트는
                    # 순수한 파랑의 계조로 나옵니다.
                    $px[$y * $w + $x] = (([int]$c.R) -shl 16) -bor
                                        (([int]$c.G) -shl 8)  -bor ([int]$c.B)
                }
            }
        }
        return [pscustomobject]@{ W = $w; H = $h; Px = $px; MuzX = $muzX; MuzY = $muzY }
    } finally {
        $bmp.Dispose()
    }
}

# Median cut over every opaque pixel of a group, to $slots colours.
function Select-Palette($frames, [int]$slots) {
    $hist = @{}
    foreach ($f in $frames) {
        foreach ($v in $f.Px) {
            if ($v -ge 0) { $hist[$v] = 1 + $hist[$v] }
        }
    }
    if ($hist.Count -eq 0) { return @() }

    # One box holding every distinct colour, then split until we run out of
    # slots or of boxes worth splitting.
    $boxes = @(, @($hist.Keys))
    while ($boxes.Count -lt $slots) {
        # Split the box with the widest spread in any single channel: that is
        # where two colours are being forced to share one entry.
        $bi = -1; $bchan = 0; $bspread = 0
        for ($i = 0; $i -lt $boxes.Count; $i++) {
            if ($boxes[$i].Count -lt 2) { continue }
            for ($k = 0; $k -lt 3; $k++) {
                $sh = 16 - 8 * $k
                $lo = 255; $hi = 0
                foreach ($v in $boxes[$i]) {
                    $c = ($v -shr $sh) -band 255
                    if ($c -lt $lo) { $lo = $c }
                    if ($c -gt $hi) { $hi = $c }
                }
                if (($hi - $lo) -gt $bspread) { $bspread = $hi - $lo; $bi = $i; $bchan = $k }
            }
        }
        if ($bi -lt 0) { break }

        $sh = 16 - 8 * $bchan
        $sorted = @($boxes[$bi] | Sort-Object { ($_ -shr $sh) -band 255 })
        # Split at the median PIXEL, not the median colour, so a hundred
        # near-identical shades of one colour do not outvote a region that
        # actually covers the sprite.
        $total = 0
        foreach ($v in $sorted) { $total += $hist[$v] }
        $half = [int]($total / 2)
        $acc = 0; $cut = 1
        for ($i = 0; $i -lt $sorted.Count - 1; $i++) {
            $acc += $hist[$sorted[$i]]
            if ($acc -ge $half) { $cut = $i + 1; break }
            $cut = $i + 2
        }
        $new = @()
        for ($i = 0; $i -lt $boxes.Count; $i++) {
            if ($i -eq $bi) { $new += , @($sorted[0..($cut - 1)]); $new += , @($sorted[$cut..($sorted.Count - 1)]) }
            else { $new += , $boxes[$i] }
        }
        $boxes = $new
    }

    # Each entry is its box's pixel-weighted average, so it lands where the
    # pixels are rather than in the middle of the box's extremes.
    $out = @()
    foreach ($b in $boxes) {
        $n = 0; $r = 0.0; $g = 0.0; $bl = 0.0
        foreach ($v in $b) {
            $c = $hist[$v]; $n += $c
            $r  += (($v -shr 16) -band 255) * $c
            $g  += (($v -shr 8)  -band 255) * $c
            $bl += ( $v          -band 255) * $c
        }
        if ($n -eq 0) { continue }
        $out += ('{0:x2}{1:x2}{2:x2}' -f [int][math]::Round($r / $n),
                                         [int][math]::Round($g / $n),
                                         [int][math]::Round($bl / $n))
    }
    return $out
}

function ConvertFrom-Png([string]$path, [string]$name, [System.Collections.ArrayList]$pal) {
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap $path
    try {
        $w = $bmp.Width; $h = $bmp.Height

        # Index every pixel against the shared palette, growing it as new
        # colours appear and snapping to the nearest entry once it is full.
        $idx = New-Object int[] ($w * $h)
        $muzX = -1; $muzY = -1
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $c = $bmp.GetPixel($x, $y)

                # MAGENTA IS A MARKER, NOT A COLOUR: it says "the muzzle is
                # here" and is never drawn.
                #
                # A weapon sprite has to say where its flash and tracers come
                # from, and the alternative is a constant in weapon.c that
                # somebody edits to match the art. This project already learned
                # what that costs: placing the shotgun by editing constants,
                # rebuilding and squinting at screenshots failed three times in
                # a row, which is why modeledit puts a draggable muzzle on the
                # 3D model. A marker pixel is the same idea for a drawing --
                # redraw the gun and the flash follows it, because the muzzle IS
                # part of the drawing.
                #
                # FF00FF because no one paints with it by accident, and it is
                # the convention half of games have used for exactly this.
                #
                # 마젠타는 색이 아니라 *표식*입니다. "총구가 여기 있다"는 뜻이며 결코
                # 그려지지 않습니다. 무기 스프라이트는 화염과 예광탄이 어디서 나오는지
                # 말해야 하는데, 대안은 누군가 아트에 맞춰 수정하는 weapon.c의 상수입니다.
                # 이 프로젝트는 그 대가를 이미 배웠습니다. 상수를 고치고 다시 빌드해
                # 스크린샷을 들여다보는 방식으로 샷건을 배치하려다 세 번 연속 실패했고,
                # 그래서 modeledit이 3D 모델에 끌 수 있는 총구를 둡니다. 표식 픽셀은
                # 그림에 대해 같은 발상입니다. 총을 다시 그리면 화염이 따라옵니다. 총구가
                # 그림의 *일부*이기 때문입니다.
                if ($c.A -ge 128 -and $c.R -gt 240 -and $c.G -lt 16 -and $c.B -gt 240) {
                    $muzX = $x; $muzY = $y
                    $idx[$y * $w + $x] = 0
                    continue
                }

                # Fully transparent pixels are all the same pixel regardless
                # of what RGB Aseprite left under them, and they must share
                # index 0 so the decoder can treat it as "draw nothing".
                if ($c.A -lt 128) { $idx[$y * $w + $x] = 0; continue }

                $key = '{0:x2}{1:x2}{2:x2}' -f $c.R, $c.G, $c.B
                $at  = $pal.IndexOf($key)
                if ($at -lt 0) {
                    if ($pal.Count -lt 16) {
                        $at = $pal.Add($key)
                    } else {
                        # Full: snap to the nearest entry. Squared distance in
                        # RGB is not perceptually ideal, but at 16 colours the
                        # entries are far apart and a fancier metric would pick
                        # the same one.
                        $best = 1; $bestD = [int]::MaxValue
                        for ($p = 1; $p -lt $pal.Count; $p++) {
                            $pr = [Convert]::ToInt32($pal[$p].Substring(0,2),16)
                            $pg = [Convert]::ToInt32($pal[$p].Substring(2,2),16)
                            $pb = [Convert]::ToInt32($pal[$p].Substring(4,2),16)
                            $d  = ($pr-$c.R)*($pr-$c.R) + ($pg-$c.G)*($pg-$c.G) + ($pb-$c.B)*($pb-$c.B)
                            if ($d -lt $bestD) { $bestD = $d; $best = $p }
                        }
                        $at = $best
                    }
                }
                $idx[$y * $w + $x] = $at
            }
        }

        # CROP TO THE INK BEFORE ENCODING ANYTHING.
        #
        # A drawing is placed in a cell whose size is set by the largest frame,
        # so most frames carry a margin of nothing. RLE barely notices -- a run
        # of transparency is two characters however long it is -- but the
        # packed opcode spends two characters per three pixels whether they are
        # picture or margin, and packing is what dense art chooses. Measured
        # over the imported set: 13% overall, 34% on the frames that pack, 0%
        # on the ones that already RLE. The margins are also partly an artefact
        # of upscaling a 41x57 drawing to fill a 64x96 cell, which is storing a
        # result this project would rather not store at all.
        #
        # The offset is emitted as its own line so the default -- centre it,
        # sit it on the floor -- still applies to anything without one.
        #
        # 무엇이든 인코딩하기 전에 잉크에 맞춰 잘라 냅니다. 그림은 가장 큰 프레임이 크기를
        # 정한 셀에 놓이므로 대부분의 프레임은 빈 여백을 함께 나릅니다. RLE는 이를 거의
        # 개의치 않지만(투명한 런은 길이와 무관하게 두 글자입니다) packed opcode는 그것이
        # 그림이든 여백이든 픽셀 셋마다 두 글자를 씁니다. 그리고 조밀한 아트가 고르는 것이
        # 바로 패킹입니다. 이식한 세트에서 측정한 값은 전체 13%, 패킹하는 프레임에서 34%,
        # 이미 RLE인 프레임에서 0%입니다.
        $ix0 = $w; $iy0 = $h; $ix1 = -1; $iy1 = -1
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                if ($idx[$y * $w + $x] -ne 0) {
                    if ($x -lt $ix0) { $ix0 = $x }
                    if ($x -gt $ix1) { $ix1 = $x }
                    if ($y -lt $iy0) { $iy0 = $y }
                    if ($y -gt $iy1) { $iy1 = $y }
                }
            }
        }
        if ($ix1 -lt 0) { $ix0 = 0; $iy0 = 0; $ix1 = 0; $iy1 = 0 }

        $cellW = $w; $cellH = $h
        $iw = $ix1 - $ix0 + 1; $ih = $iy1 - $iy0 + 1
        if ($iw -ne $w -or $ih -ne $h) {
            $crop = New-Object int[] ($iw * $ih)
            for ($y = 0; $y -lt $ih; $y++) {
                for ($x = 0; $x -lt $iw; $x++) {
                    $crop[$y * $iw + $x] = $idx[($y + $iy0) * $w + ($x + $ix0)]
                }
            }
            $idx = $crop
            # The muzzle was recorded in cell coordinates; move it with the
            # pixels or the flash stays where the margin used to be.
            if ($muzX -ge 0) { $muzX -= $ix0; $muzY -= $iy0 }
            $w = $iw; $h = $ih
        }

        # Two encodings, and the smaller one wins per sprite.
        #
        # RLE is the obvious choice for pixel art and is usually right, but it
        # is not always: it stores two characters per RUN, so it only beats a
        # packed index stream when runs average longer than three pixels. Heavy
        # dithering -- which is a legitimate pixel-art technique, and one this
        # game's own renderer leans on -- breaks runs down to 1-2 px and makes
        # RLE actively worse than storing the indices directly. Measured on
        # two test sprites: a flat one averaged 15.3 px per run, a dithered
        # one 2.1.
        #
        # Rather than forcing a choice on the artist, encode both and keep the
        # shorter. The decoder reads whichever opcode it finds, so a sprite
        # that changes character between edits changes encoding with it and
        # nobody has to think about which to use.
        #
        # BOTH now spend a full printable character rather than a hex digit.
        # The first version used hex, which throws away half of every byte: a
        # character carries six usable bits and a hex digit uses four. Worse,
        # spending one digit on a run length capped a run at 15 pixels, so on
        # flat-shaded art it was the CAP breaking the runs up rather than the
        # picture. Measured on viewmodel-sized gun art, lifting the cap to 63
        # and packing the indices was 2.2x smaller on flat art and 1.2x on
        # dithered -- the same drawing, the same palette, purely the encoding:
        #
        #     128x96 flat     2,098 -> 946 bytes
        #     160x120 flat    3,224 -> 1,298
        #     128x96 dithered 6,272 -> 5,332
        #
        # 64 characters that need no escaping inside a C string literal: no
        # backslash, no double quote, and no '?' (which can start a trigraph).
        $A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-'

        # Run-length: one character for the run, one for the palette index.
        $rle = New-Object System.Text.StringBuilder
        $i = 0
        while ($i -lt $idx.Length) {
            $j = $i
            while ($j -lt $idx.Length -and $idx[$j] -eq $idx[$i] -and ($j - $i) -lt 63) { $j++ }
            [void]$rle.Append($A[$j - $i]).Append($A[$idx[$i]])
            $i = $j
        }

        # Packed: three 4-bit indices (12 bits) in two 6-bit characters, so 1.5
        # pixels per byte where a hex digit managed 1. This is what wins when
        # the art is noisy enough that runs stop paying for themselves.
        # $p0/$p1/$p2 rather than $a/$b/$c, and that is not a style choice.
        # PowerShell variable names are CASE-INSENSITIVE, so `$a = $idx[$i]`
        # overwrites $A -- the alphabet this loop is about to index. $A then
        # holds an integer, indexing an integer returns the integer, and every
        # pixel encodes as the character '0'. It went unseen because a packed
        # sprite had never actually been produced: all the placeholder art was
        # flat, RLE won every time, and the first drawing dense enough to
        # choose packing was the first to be corrupted by it.
        # $a/$b/$c가 아니라 $p0/$p1/$p2인 것은 취향의 문제가 아닙니다. PowerShell의
        # 변수명은 *대소문자를 구분하지 않으므로* `$a = $idx[$i]`는 이 루프가 곧
        # 인덱싱할 알파벳인 $A를 덮어씁니다. 그러면 $A는 정수가 되고, 정수를 인덱싱하면
        # 그 정수가 돌아오며, 모든 픽셀이 문자 '0'으로 인코딩됩니다. 드러나지 않았던
        # 이유는 packed 스프라이트가 실제로 만들어진 적이 없었기 때문입니다. 플레이스홀더
        # 아트가 전부 평면이라 매번 RLE가 이겼고, 패킹을 고를 만큼 조밀한 첫 그림이
        # 곧 이 버그에 당한 첫 그림이었습니다.
        $packed = New-Object System.Text.StringBuilder
        for ($i = 0; $i -lt $idx.Length; $i += 3) {
            $p0 = $idx[$i]
            $p1 = if ($i + 1 -lt $idx.Length) { $idx[$i + 1] } else { 0 }
            $p2 = if ($i + 2 -lt $idx.Length) { $idx[$i + 2] } else { 0 }
            $v = ($p0 -shl 8) -bor ($p1 -shl 4) -bor $p2
            [void]$packed.Append($A[$v -shr 6]).Append($A[$v -band 63])
        }

        if ($rle.Length -le $packed.Length) {
            $op = 'r'; $data = $rle.ToString()
        } else {
            $op = 'p'; $data = $packed.ToString()
        }

        # DECODE WHAT WAS JUST ENCODED AND COMPARE. sprtest exercises the C
        # decoder against hand-written text, which proves the decoder reads the
        # format -- it cannot prove this writes it. The two halves live in
        # different languages and only meet in the built game, so nothing was
        # checking the one direction that matters, and the $a/$A collision
        # above rode along for as long as no sprite chose packing. A round trip
        # here costs one pass over the indices and fails the build instead.
        # 방금 인코딩한 것을 되돌려 디코딩해 비교합니다. sprtest는 손으로 쓴 텍스트로 C
        # 디코더를 검사하므로 디코더가 포맷을 *읽는다*는 것은 증명하지만, 이쪽이 포맷을
        # *쓴다*는 것은 증명할 수 없습니다. 두 절반은 서로 다른 언어에 살고 빌드된
        # 게임에서만 만나므로 정작 중요한 방향을 아무도 검사하지 않았고, 위의 $a/$A
        # 충돌은 어떤 스프라이트도 패킹을 고르지 않는 동안 계속 묻어갔습니다. 여기서의
        # 왕복 검사는 인덱스를 한 번 훑는 비용으로 대신 빌드를 실패시킵니다.
        $back = New-Object int[] $idx.Length
        $n = 0
        if ($op -eq 'r') {
            for ($k = 0; $k -lt $data.Length - 1; $k += 2) {
                $run = $A.IndexOf($data[$k]); $val = $A.IndexOf($data[$k + 1])
                for ($q = 0; $q -lt $run -and $n -lt $back.Length; $q++) { $back[$n++] = $val }
            }
        } else {
            for ($k = 0; $k -lt $data.Length - 1; $k += 2) {
                $v = $A.IndexOf($data[$k]) * 64 + $A.IndexOf($data[$k + 1])
                foreach ($sh2 in 8, 4, 0) {
                    if ($n -lt $back.Length) { $back[$n++] = ($v -shr $sh2) -band 15 }
                }
            }
        }
        if ($n -ne $idx.Length) {
            throw ("$name : '$op' encoding produced $n pixels for a $w x $h sprite " +
                   "($($idx.Length) expected). bake.ps1 and sprite.c disagree about the format.")
        }
        for ($k = 0; $k -lt $idx.Length; $k++) {
            if ($back[$k] -ne $idx[$k]) {
                throw ("$name : '$op' encoding does not round-trip. Pixel $k " +
                       "($($k % $w),$([int]($k / $w))) went in as index $($idx[$k]) and " +
                       "came back as $($back[$k]). bake.ps1 is writing something " +
                       "sprite.c will not read back as the same picture.")
            }
        }

        # The muzzle marker, when the drawing carried one. Emitted BEFORE the
        # data line so the decoder has it while the sprite is still the current
        # one -- the parser is a single forward pass and never looks back.
        # 그림에 표식이 있었다면 총구를 기록합니다. 디코더가 해당 스프라이트를 처리하는
        # 동안 값을 갖도록 데이터 줄보다 *앞에* 놓습니다. 파서는 단방향 1회 통과이며
        # 되돌아보지 않습니다.
        $muz = if ($muzX -ge 0) { "m $muzX $muzY`n" } else { '' }

        # Where the cropped drawing goes in its cell. Omitted when the drawing
        # still fills the cell, so the decoder's default placement -- centred,
        # sitting on the floor -- keeps serving everything that never needed
        # an offset, and an older sprite text stays readable.
        # 잘라 낸 그림이 셀의 어디에 놓이는지입니다. 그림이 여전히 셀을 가득 채우면
        # 생략하므로, 오프셋이 필요 없던 모든 것에는 디코더의 기본 배치가 그대로
        # 적용되고 이전의 스프라이트 텍스트도 계속 읽힙니다.
        $orig = if ($w -ne $cellW -or $h -ne $cellH) { "o $ix0 $iy0`n" } else { '' }

        return [pscustomobject]@{
            Text = "s $name $w $h`n$orig$muz$op $data`n"
            W    = $cellW
            H    = $cellH
            Enc  = $op
        }
    } finally {
        $bmp.Dispose()
    }
}

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
$freedoomAssets = @()
foreach ($d in @('assets\sprites', 'assets\sounds')) {
    $dir = Join-Path $root $d
    if (Test-Path $dir) {
        $freedoomAssets += @(Get-ChildItem $dir -Include *.png, *.wav -File `
                             -ErrorAction SilentlyContinue |
                             Where-Object { $_.Name -notlike '_*' })
    }
}
if ($freedoomAssets.Count -gt 0) {
        $licPath = Join-Path $root 'docs\LICENSE-Freedoom.txt'
        if (-not (Test-Path $licPath)) {
            throw ("Freedoom assets are present but docs/LICENSE-Freedoom.txt is " +
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
            throw ("Freedoom artwork is present in assets\sprites\ but the NOTICE " +
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
            throw ("docs/LICENSE-Freedoom.txt no longer contains the BSD text this " +
                   "build checks the in-game notice against. Restore it from " +
                   "https://github.com/freedoom/freedoom/blob/master/COPYING.adoc")
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
            throw ("Freedoom artwork is present in assets\sprites\ but the NOTICE " +
                   "table in src/scene.c is not the licence verbatim: $detail. " +
                   "The BSD licence requires this text to be reproduced with the " +
                   "binary, and this game IS the binary -- see docs/LICENSE-Freedoom.txt.")
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
$spriteText = ''
$spriteDir  = Join-Path $root 'assets\sprites'

if (Test-Path $spriteDir) {
    $groups = [ordered]@{}
    # A LEADING UNDERSCORE MEANS "NOT A SPRITE", and it is enforced here rather
    # than left to the decoder. sprite.c already ignores a name that matches no
    # monster and no weapon, which sounds like enough and is not: ignoring it
    # happens at DECODE time, so the drawing is still quantised, encoded and
    # carried in .rdata for the life of the binary. The importer's --preview
    # contact sheet landed here once and cost 411KB of a 1.44MB budget while
    # never being drawn a single time.
    #
    # 앞의 밑줄은 "스프라이트가 아님"을 뜻하며, 디코더에 맡기지 않고 이곳에서 강제합니다.
    # sprite.c는 이미 어떤 몬스터에도 무기에도 해당하지 않는 이름을 무시하는데, 그것으로
    # 충분해 보이지만 아닙니다. 무시는 *디코드* 시점에 일어나므로 그림은 여전히
    # 양자화되고 인코딩되어 바이너리가 사는 내내 .rdata에 실려 다닙니다. 임포터의
    # --preview 대조 시트가 한 번 이곳에 떨어져, 단 한 번도 그려지지 않으면서 1.44MB
    # 예산 중 411KB를 차지했습니다.
    foreach ($png in (Get-ChildItem $spriteDir -Filter *.png |
                      Where-Object { $_.Name -notlike '_*' } | Sort-Object Name)) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($png.Name)
        $subject = $name -replace '\d+$', ''
        if (-not $subject) { $subject = $name }
        if (-not $groups.Contains($subject)) { $groups[$subject] = @() }
        $groups[$subject] += , [pscustomobject]@{ Png = $png; Name = $name }
    }

    foreach ($subject in $groups.Keys) {
        $members = $groups[$subject]

        # Index 0 is reserved for transparent and is never drawn, which is why
        # the colour stored for it is arbitrary -- so median cut gets 15.
        $frames = @()
        foreach ($m in $members) { $frames += , (Get-SpritePixels $m.Png.FullName) }

        $palette = New-Object System.Collections.ArrayList
        [void]$palette.Add('000000')
        foreach ($c in (Select-Palette $frames 15)) { [void]$palette.Add($c) }
        # A group with fewer than 15 distinct colours leaves the palette short.
        # Pad it: the encoder snaps to the NEAREST entry over the whole array,
        # and an entry that was never filled would be black and would pull dark
        # pixels away from the colour median cut actually chose for them.
        while ($palette.Count -lt 16) { [void]$palette.Add($palette[$palette.Count - 1]) }

        $spriteText += "pal $($palette.Count) $($palette -join ' ')`n"

        foreach ($m in $members) {
            $conv = ConvertFrom-Png $m.Png.FullName $m.Name $palette
            $spriteText += $conv.Text
            $report += [pscustomobject]@{
                Asset  = "sprites\$($m.Png.Name)"
                Source = $m.Png.Length
                Baked  = $conv.Text.Length
                Saved  = "$([math]::Round((1 - $conv.Text.Length / ($conv.W * $conv.H * 4)) * 100))% vs raw ($($conv.W)x$($conv.H) $($conv.Enc))"
            }
        }
    }
}

$escSpr = $spriteText.Replace('\', '\\').Replace('"', '\"').
                      Replace("`r", '').Replace("`n", ' ')
[void]$sb.AppendLine('static const char ASSET_SPRITES[] =')
if ($escSpr.Length -eq 0) {
    [void]$sb.AppendLine('    ""')
} else {
    for ($i = 0; $i -lt $escSpr.Length; $i += 76) {
        $len = [Math]::Min(76, $escSpr.Length - $i)
        [void]$sb.AppendLine("    `"$($escSpr.Substring($i, $len))`"")
    }
}
[void]$sb.AppendLine('    ;')
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
        $payload = $sbp.ToString().Replace('\"','"').Replace('\','')
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
