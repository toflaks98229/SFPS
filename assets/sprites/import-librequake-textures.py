#!/usr/bin/env python3
"""Fetch the wall textures a LibreQuake map uses and make them ours.

    python import-librequake-textures.py lqdm1                 # report only
    python import-librequake-textures.py lqdm1 --emit          # write the PNGs
    python import-librequake-textures.py lqdm1 --from x.map --emit

WHY THIS EXISTS, AND WHY IT IS THE SECOND HALF OF AN IMPORT.
assets/maps/import-librequake.py brought `lqdm1` across and, for every face,
looked its texture name up in a table and wrote one of THIS project's materials
in its place: `med_csl_brk14b` became `wall_stone`, the four `met_brn_*` became
`wall_metal` and `wall_track`, four woods collapsed into one `pwood`. That is a
conversion which keeps the geometry and throws away the surface -- the map is
LibreQuake's room wearing somebody else's walls, and 22 names became 11.

This inverts it. The textures LibreQuake authored for that room come across with
it, and the project's own materials stay where they were: still in
assets/textures.txt, still used by the weapons, the fixture map and everything
procedural. Nothing is taken away; a second set arrives beside it.

WHAT MAKES THIS LEGITIMATE. LibreQuake's docs/README-IMPORTANT-LICENCE-INFO
names the BSD-3 side as "models, textures and sounds" -- so unlike the maps,
which are permissive by residue (see import-librequake.py), the TEXTURES are
permissive by ENUMERATION. This is the better-evidenced half of that project.
docs/LICENSE-LibreQuake.txt is the notice, and it ships inside the binary.

WHAT THE ENGINE REQUIRES, all three of which this script enforces:

  * 8-bit RGBA, non-interlaced. src/png.c reads colour type 6 and refuses every
    other combination -- deliberately, because reading one wrong "would produce
    something that looks like damaged art rather than like a refusal". Sources
    arrive as RGB, RGBA and palette; they leave as one thing.
  * At most SPR_WALL (128) on a side. sprite.c places a wall drawing into a
    128x128 cell and a bigger one would be cropped. Never UPSCALED, though: a
    64x64 source stays 64x64 and tiles four times into the cell, which costs a
    quarter of the bytes and loses nothing, because upscaling invents no detail
    and PNG charges for the gradients it invents.
  * A NAME THAT IS A FILENAME. A face's material name IS its file name, and
    Quake writes liquids with a leading `*` which Windows will not accept in
    one. LibreQuake's own repository spells those files `star_*`, so that is
    what they are called here -- and import-librequake.py renames the faces to
    match, because the two names have to be the same name.

이것이 존재하는 이유이며, 왜 가져오기의 나머지 절반인가.
assets/maps/import-librequake.py는 `lqdm1`을 가져오면서 모든 면에 대해 텍스처 이름을 표에서
찾아 그 자리에 *이 프로젝트의* 재질을 적었습니다. `med_csl_brk14b`는 `wall_stone`이 되었고,
`met_brn_*` 넷은 `wall_metal`과 `wall_track`이 되었으며, 목재 넷이 `pwood` 하나로 합쳐졌습니다.
그것은 지오메트리를 지키고 표면을 버리는 변환입니다. 그 맵은 남의 벽을 입은 LibreQuake의
방이고, 이름 22개가 11개가 되었습니다.

이 스크립트가 그것을 뒤집습니다. LibreQuake가 그 방을 위해 만든 텍스처가 방과 함께 건너오고,
이 프로젝트 자신의 재질은 있던 자리에 그대로 남습니다. assets/textures.txt에 여전히 있고,
무기와 픽스처 맵과 모든 절차적 표면이 여전히 씁니다. 빼앗기는 것은 없습니다. 두 번째 집합이
그 옆에 도착할 뿐입니다.

*무엇이 이것을 정당하게 만드는가.* LibreQuake의 docs/README-IMPORTANT-LICENCE-INFO는 BSD-3
쪽을 "models, textures and sounds"로 명시합니다. 그러므로 잔여에 의해 허용적인 맵과 달리
*텍스처는 열거에 의해* 허용적입니다. 그 프로젝트에서 증거가 더 좋은 쪽입니다.
"""
import argparse
import io
import os
import re
import sys
import urllib.request

try:
    from PIL import Image
except ImportError:
    print('This needs Pillow: python -m pip install pillow')
    raise SystemExit(1)

RAW = 'https://raw.githubusercontent.com/lavenderdotpet/LibreQuake/main/texture-wads/'
API = 'https://api.github.com/repos/lavenderdotpet/LibreQuake/contents/texture-wads/'
MAP = ('https://raw.githubusercontent.com/lavenderdotpet/LibreQuake/main/'
       'lq1/maps/src/dm/%s.map')

# The cell a wall drawing goes into. src/sprite.h's SPR_WALL, and read from
# there rather than typed here for the reason import-librequake.py reads its
# caps from the headers: a copy of a number in another file is a copy that
# drifts, and this one drifting means every imported texture is silently
# cropped.
# 벽 그림이 들어가는 셀입니다. src/sprite.h의 SPR_WALL이며, import-librequake.py가 상한을
# 헤더에서 읽는 것과 같은 이유로 이곳에 적지 않고 그곳에서 읽습니다.
# The spellings `lqdm4` gets wrong, and what its own wad calls them.
#
# assets/maps/import-librequake.py's TEXTURES table carries the same two
# entries, because the two scripts each need half of one fact: that one
# substitutes the name INSIDE the converted map, so the engine asks for the
# corrected material; this one substitutes it BEFORE the lookup, so the file
# arrives under the name the engine will ask for. Correcting it in only one of
# them leaves either a face with no material or a material no face names.
#
# Applied to `n` itself rather than to the lookup key, because the name is also
# the OUTPUT filename -- fixing only the key would fetch the right pixels and
# write them to `med_cslbrk18_tb.png`, which nothing would ever open.
#
# `lqdm4`가 틀리게 적은 철자와, 그 맵 자신의 wad가 그것을 부르는 이름입니다.
#
# assets/maps/import-librequake.py의 TEXTURES 표가 같은 두 항목을 갖고 있습니다. 두
# 스크립트가 하나의 사실을 절반씩 필요로 하기 때문입니다. 그쪽은 변환된 맵 *안에서* 이름을
# 치환하여 엔진이 고쳐진 재질을 요구하게 하고, 이쪽은 조회 *전에* 치환하여 파일이 엔진이
# 요구할 이름으로 도착하게 합니다. 둘 중 하나에서만 고치면, 재질 없는 면이 남거나 아무 면도
# 부르지 않는 재질이 남습니다.
#
# 조회 키가 아니라 `n` 자체에 적용하는 이유는, 이름이 곧 *출력 파일명*이기 때문입니다. 키만
# 고치면 올바른 픽셀을 가져와 `med_cslbrk18_tb.png`로 쓰게 되고, 그것은 아무도 열지 않습니다.
MEANT = {
    'med_cslbrk18_tb': 'med_csl_brk18_tb',
    'med_cslbrk18_tc': 'med_csl_brk18_tc',
}


def resolve(n, have):
    """The wad key for a face's name, or None.

    ONE CHAIN, TWO CALLERS, and it is one function because it was two. The
    emit loop grew `_fbr` and the entries loop did not, so `*lava3` fetched
    its pixels and never got a row in textures.txt -- six faces of lava and
    twenty-one of temple lamp drawing the fallback material, with the PNG
    sitting right there beside them. A resolution rule that only half the
    script knows is a rule that writes files nothing can reach.

    *하나의 사슬, 두 호출자이며*, 하나의 함수인 이유는 그것이 둘이었기 때문입니다. 방출
    반복문은 `_fbr`을 얻었고 항목 반복문은 얻지 못해서, `*lava3`은 자기 픽셀을 가져오고도
    textures.txt에 행을 얻지 못했습니다. 용암 여섯 면과 신전 램프 스물한 면이 대체 재질로
    그려지는데 PNG는 바로 그 곁에 있었습니다. 스크립트의 절반만 아는 해석 규칙은 아무도
    닿을 수 없는 파일을 쓰는 규칙입니다.
    """
    k = n.lower()
    if k in have:
        return k
    k = legal_name(n).lower()
    if k in have:
        return k
    if (k + '_fbr') in have:
        return k + '_fbr'
    if MEANT.get(k) in have:
        return MEANT[k]
    return None


def wall_cell():
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, '..', '..', 'src', 'sprite.h')
    with open(path, encoding='utf-8') as f:
        m = re.search(r'^#define\s+SPR_WALL\s+(\d+)', f.read(), re.M)
    if not m:
        raise SystemExit('import-librequake-textures: cannot read SPR_WALL from '
                         'src/sprite.h. Fix this rather than typing the number: '
                         'a cropped texture does not announce itself.')
    return int(m.group(1))


# How many cells a wall material spans, and it is ONE NUMBER FOR EVERY MATERIAL.
#
# This used to be `(2 * SPR_WALL) // source_side` -- the count that FILLS a
# material, which is 2 for a 128 drawing, 4 for a 64 and 16 for a 16. That was
# wrong in a way nothing on this side could see. BRUSH_TEXELS is a single
# divisor every brush face uses, so a material tiling its cell a different
# number of times is a material at a different SCALE from its neighbours on the
# same wall. The engine's own table was normalised to one count when
# BRUSH_TEXELS went from 128 to 256; this script kept emitting the old formula,
# and importing a map with any 64-pixel source would have written
# `image med_tmpl_trim1 4` into a table whose other thirty-three rows say 2.
#
# tools/texprobe.c is the check that catches it, and it catches it in the
# ENGINE rather than here -- so a stale importer fails a test run instead of
# shipping one wall at twice its neighbours' scale, which reads as an authoring
# mistake in the map and not as a number in a .py file.
#
# 벽 재질이 걸치는 셀 수이며, *모든 재질에 대해 하나의 수*입니다.
#
# 예전에는 `(2 * SPR_WALL) // 소스 변`이었습니다. 재질을 *채우는* 수이며 128 그림에 2,
# 64에 4, 16에 16입니다. 이쪽에서는 보이지 않는 방식으로 틀렸습니다. BRUSH_TEXELS는 모든
# 브러시 면이 쓰는 단일 제수이므로, 자기 셀을 다른 횟수로 반복하는 재질은 같은 벽의 이웃과
# *배율*이 다른 재질입니다. BRUSH_TEXELS가 128에서 256이 될 때 엔진의 표는 하나의 카운트로
# 정규화되었는데, 이 스크립트는 옛 공식을 계속 내보냈습니다. 64픽셀 소스를 가진 맵을
# 가져왔다면 나머지 서른세 행이 2라고 적힌 표에 `image med_tmpl_trim1 4`를 써 넣었을
# 것입니다.
#
# 그것을 잡는 검사는 tools/texprobe.c이며, 이곳이 아니라 *엔진에서* 잡습니다. 그래서 낡은
# 임포터는 이웃의 두 배 배율인 벽을 출하하는 대신 테스트 실행을 실패시킵니다. 그 벽은 .py
# 파일의 숫자가 아니라 맵의 저작 실수처럼 보입니다.
def tiles_per_material():
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, '..', '..', 'src', 'brush.h')
    with open(path, encoding='utf-8') as f:
        m = re.search(r'^#define\s+BRUSH_TEXELS\s+([0-9.]+)', f.read(), re.M)
    if not m:
        raise SystemExit('import-librequake-textures: cannot read BRUSH_TEXELS '
                         'from src/brush.h. Fix this rather than typing the '
                         'number: a wall at the wrong scale looks like a map '
                         'authoring mistake, not like a stale constant.')
    n = int(float(m.group(1))) // wall_cell()
    if n < 1:
        raise SystemExit('import-librequake-textures: BRUSH_TEXELS is smaller '
                         'than SPR_WALL, so a material cannot hold one cell.')
    return n


# Names brush.c never draws, so nothing needs an image for them.
# brush.c가 결코 그리지 않는 이름이므로 이미지가 필요 없습니다.
NODRAW = {'clip', 'skip', 'trigger', 'hint', 'skip_'}


def legal_name(tex):
    """The material name a face may carry AND a file may be called.

    Quake marks a liquid with a leading `*`, which is not a filename character
    on Windows, and LibreQuake's own repository already writes those files
    `star_water0.png`. Following their spelling rather than inventing one keeps
    the two ends of this import saying the same word.
    Quake는 액체를 앞의 `*`로 표시하며 그것은 Windows에서 파일명 문자가 아닙니다.
    LibreQuake 자신의 저장소가 이미 그 파일들을 `star_water0.png`로 적으므로, 새로 지어내지
    않고 그 철자를 따르는 것이 이 가져오기의 양 끝이 같은 낱말을 말하게 합니다.
    """
    return ('star_' + tex[1:]) if tex.startswith('*') else tex


def faces_of(text):
    """Every texture name the map's faces carry, in first-seen order."""
    seen, out = set(), []
    for line in text.split('\n'):
        line = line.strip()
        if not line.startswith('('):
            continue
        parts = line.split(')')
        if len(parts) < 4:
            continue
        tok = parts[3].split()
        if not tok:
            continue
        n = tok[0]
        if n.lower() in NODRAW or n in seen:
            continue
        seen.add(n)
        out.append(n)
    return out


def index_wads(wads):
    """name -> (wad, download url), over the wads the map asked for."""
    have = {}
    import json
    for w in wads:
        try:
            entries = json.load(urllib.request.urlopen(API + w, timeout=30))
        except Exception as ex:
            print('  ! %s: %s' % (w, ex))
            continue
        for e in entries:
            if e['name'].lower().endswith('.png'):
                have.setdefault(e['name'][:-4].lower(), (w, e['download_url']))
    return have


def wads_of(text):
    m = re.search(r'"wad"\s+"([^"]*)"', text)
    if not m:
        return []
    return [w.strip().split('/')[-1].rsplit('.', 1)[0]
            for w in m.group(1).split(';') if w.strip()]


# --- albedo, and why a Quake texture is too dark to be one here -------------
#
# THE WORLD PATH MULTIPLIES AND NEVER ADDS. Every lighting term in render.c's
# world branch folds into one scalar: AMBIENT + 0.68*key (render.c:1622-1623),
# then the point lights (`lum+=e`), then the baked vertex light (`lum+=bl`),
# then noise -- and the only thing that reaches the colour is
# `c*=lum*tint` (render.c:1805). `c` is written exactly twice in that whole
# branch: once from the texture and once by that multiply.
#
# SO THE ALBEDO IS THE LIGHTING'S DYNAMIC RANGE. `lum` runs AMBIENT..1.0, a
# swing of 0.68, and what a player sees of that swing is albedo x 0.68:
#
#     surface                          albedo   lit-to-unlit swing on screen
#     wall_stone (what these faces      0.495            0.337
#       wore before the import)
#     this project's walls, mean        0.356            0.242
#     med_csl_brk14b, raw               0.126            0.086   <- 43% of faces
#     met_brn_flat, raw                 0.079            0.054
#
# On the wall that is nearly half this room, the entire difference between
# standing in a lamp and standing in the dark was 0.086 of the output range --
# a third of what the same lighting gives on this project's own walls. That is
# the reported defect: a lamp that does not read as a pool with a shape, and a
# face that takes the light of its neighbour without a boundary between them.
# The lighting was never changed. Its amplitude was.
#
# QUAKE TEXTURES ARE DARK ON PURPOSE, for the engine they were drawn for: a
# lightmap MULTIPLIES them and overbright can take the result past 1.0, so the
# art carries the low end and the lighting supplies the range. Here `lum` never
# exceeds 1.0, so the same art arrives with a third of the range the shader was
# built around. The correction belongs at the crossing, next to "armour becomes
# health" -- it is the same kind of statement about what a different engine
# assumed.
#
# ONE GAMMA FOR THE WHOLE SET, not a per-texture normalisation to a target.
# Flattening every surface to one mean would destroy the contrast BETWEEN
# materials, and render.c relies on that by design: it quantises illumination
# before the material is applied "so a dark material and a bright one in the
# same band still differ -- which is what keeps the level readable rather than
# posterised into flat shapes." A single exponent is monotonic, so the author's
# ordering survives intact: snow stays the brightest thing in the room and
# brown metal the darkest.
#
# 0.4739 is the exponent that lifts the mean of the 23 textures lqdm1 uses from
# 55.2 to 90.9 -- this project's own wall set's mean. It is not a taste setting;
# it is one measurement matched to another.
#
# *월드 경로는 곱하기만 하고 결코 더하지 않습니다.* render.c 월드 갈래의 모든 조명 항이 스칼라
# 하나로 접히고(주변광 + 주광, 점광원, 구운 정점광, 잡음), 색에 닿는 것은 `c*=lum*tint`
# 하나뿐입니다. 그 갈래 전체에서 `c`는 정확히 두 번 쓰입니다. 텍스처에서 한 번, 그 곱하기로 한
# 번입니다.
#
# *그러므로 알베도가 곧 조명의 동적 범위입니다.* `lum`은 AMBIENT..1.0으로 0.68만큼 움직이고,
# 플레이어가 보는 것은 알베도 x 0.68입니다. 이 방의 절반에 가까운 벽에서, 등 아래에 서 있는
# 것과 어둠 속에 서 있는 것의 차이 전부가 출력 범위의 0.086이었습니다. 이 프로젝트 자신의
# 벽에서 같은 조명이 주는 것의 3분의 1입니다. 그것이 보고된 결함입니다. 조명은 바뀐 적이
# 없습니다. 그 진폭이 바뀌었습니다.
#
# *Quake 텍스처는 일부러 어둡습니다.* 그것이 그려진 엔진에서는 라이트맵이 곱하고 오버브라이트가
# 결과를 1.0 너머로 보내므로, 아트가 낮은 쪽을 맡고 조명이 범위를 공급합니다. 이곳의 `lum`은
# 1.0을 넘지 않습니다.
#
# *집합 전체에 감마 하나*이며, 텍스처마다 목표값으로 정규화하지 않습니다. 단일 지수는 단조이므로
# 제작자가 고른 재질 사이의 순서가 그대로 살아남습니다.
ALBEDO_GAMMA = 0.4739

ALBEDO_LUT = [min(255, int(255.0 * ((i / 255.0) ** ALBEDO_GAMMA) + 0.5))
              for i in range(256)]


def convert(raw, cell):
    """One source PNG as the engine's only accepted form.

    RGBA because src/png.c reads colour type 6 and nothing else. The alpha is
    flattened to opaque on purpose: tex.c's fill_from_image ends with `d[3] = 0`
    and says why -- "alpha is gloss here, not transparency ... the drawing's own
    alpha is deliberately dropped" -- so a varying alpha channel is bytes spent
    on a value the engine overwrites before it ever reaches a pixel.
    src/png.c가 색 종류 6만 읽으므로 RGBA입니다. 알파를 불투명으로 평탄화하는 것은
    의도적입니다. tex.c의 fill_from_image가 `d[3] = 0`으로 끝나며 이유를 밝힙니다. 알파는
    투명도가 아니라 광택이고 그림 자신의 알파는 일부러 버려집니다. 그러므로 변화하는 알파
    채널은 엔진이 픽셀에 닿기도 전에 덮어쓰는 값에 쓰는 바이트입니다.
    """
    im = Image.open(io.BytesIO(raw)).convert('RGB')

    # Square, and never bigger than the cell. Never SMALLER than it was,
    # either: sprite.c tiles an undersized wall into its cell now, so a 64x64
    # source is four copies at a quarter of the bytes rather than an upscale
    # that invents gradients PNG then charges for.
    # 정사각이며 셀보다 크지 않습니다. 원래보다 *작지도* 않습니다. sprite.c가 이제 작은
    # 벽을 셀 안으로 타일링하므로, 64x64 원본은 4분의 1 바이트로 사본 넷이 되며,
    # PNG가 값을 물리는 그러데이션을 지어내는 확대가 아닙니다.
    side = min(min(im.size), cell)
    if im.size != (side, side):
        im = im.resize((side, side), Image.LANCZOS)

    im = im.point(ALBEDO_LUT * 3)

    r, g, b = im.split()
    return Image.merge('RGBA', (r, g, b, Image.new('L', im.size, 255)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('--from', dest='src', default=None)
    ap.add_argument('--emit', action='store_true')
    a = ap.parse_args()

    where = a.src or (MAP % a.name)
    if a.src:
        text = open(a.src, encoding='utf-8', errors='replace').read()
    else:
        text = urllib.request.urlopen(where).read().decode('utf-8', 'replace')
    print('read %s' % where)

    cell = wall_cell()
    wads = wads_of(text)
    names = faces_of(text)
    print('  wads:  %s' % ', '.join(wads))
    print('  faces name %d drawable texture(s), cell is %dx%d' % (len(names), cell, cell))

    have = index_wads(wads)
    here = os.path.dirname(os.path.abspath(__file__))

    total, missing, sides = 0, [], {}
    print()
    for n in sorted(names):
        # Both spellings, because a liquid's face says `*water0` and the file
        # that holds it is `star_water0.png`. Looking up only what the face
        # says reported the one liquid in this map as missing.
        # 두 철자 모두입니다. 액체의 면은 `*water0`이라고 말하고 그것을 담은 파일은
        # `star_water0.png`이기 때문입니다. 면이 말하는 것만 조회하면 이 맵의 유일한
        # 액체가 없는 것으로 보고되었습니다.
        # AND THE FULLBRIGHT SPELLING, which is the third one. LibreQuake
        # keeps the glowing variant of a liquid or a lamp under a `_fbr`
        # suffix -- `star_lava3_fbr.png` holds the face a map calls `*lava3` --
        # while the still ones (`star_water0.png`) carry no suffix at all. So
        # the suffix cannot be added to every lookup and cannot be left out of
        # them either; it is a fallback after both plain spellings fail.
        # This is not cosmetic on a map like `lqdm4`, whose whole lower half is
        # lava: without it the fortress's underbelly draws in the fallback
        # material and the level loses the thing it is named for.
        # *그리고 풀브라이트 철자이며, 그것이 세 번째입니다.* LibreQuake는 액체나 램프의
        # 빛나는 변형을 `_fbr` 접미사로 둡니다. 맵이 `*lava3`이라 부르는 면을 담은 것은
        # `star_lava3_fbr.png`입니다. 반면 잔잔한 것들(`star_water0.png`)에는 접미사가
        # 아예 없습니다. 그래서 이 접미사는 모든 조회에 붙일 수도, 빼 둘 수도 없습니다.
        # 두 평범한 철자가 모두 실패한 뒤의 대비책입니다.
        # `lqdm4`처럼 아래쪽 절반이 통째로 용암인 맵에서 이것은 겉치레가 아닙니다. 이것이
        # 없으면 요새의 아랫배가 대체 재질로 그려지고, 레벨은 자기 이름의 유래를 잃습니다.
        key = resolve(n, have)
        if key is None:
            missing.append(n)
            print('  %-18s NOT IN THE WADS THIS MAP NAMES' % n)
            continue
        wad, url = have[key]
        raw = urllib.request.urlopen(url, timeout=60).read()
        im = convert(raw, cell)
        buf = io.BytesIO()
        im.save(buf, 'PNG', optimize=True)
        data = buf.getvalue()
        total += len(data)
        sides[n] = im.size[0]
        out = legal_name(n) + '.png'
        print('  %-18s %-13s %3dx%-3d %6d B -> %s'
              % (n, wad, im.size[0], im.size[1], len(data), out))
        if a.emit:
            with open(os.path.join(here, out), 'wb') as f:
                f.write(data)

    print('\n  %d texture(s), %d bytes' % (len(names) - len(missing), total))
    if missing:
        print('  %d MISSING -- those faces would draw as the fallback' % len(missing))

    # The recipes, which are the other half of a material existing. Printed
    # rather than written: assets/textures.txt is authored, its ordering carries
    # meaning, and a script that edits it in place is a script that will one day
    # reorder somebody's file.
    # 레시피이며, 재질이 존재하기 위한 나머지 절반입니다. 쓰지 않고 출력합니다.
    # assets/textures.txt는 사람이 쓴 파일이고 순서에 뜻이 있으며, 그것을 제자리에서 고치는
    # 스크립트는 언젠가 남의 파일을 재배열하는 스크립트입니다.
    tiles = tiles_per_material()
    print('\n  add to assets/textures.txt (tiles = %d for every material):' % tiles)
    for n in sorted(names):
        if resolve(n, have) is None:
            continue
        # A LIQUID SAYS SO IN ITS RECIPE, so the line has to come out here
        # rather than be added by hand afterwards -- these rows are pasted
        # into textures.txt by a person, and a `flow` that only exists in the
        # file is a `flow` the next import silently drops. The same rule the
        # lamps taught: a recipe that stops reproducing its own output has
        # stopped being a recipe.
        # 액체는 자기 레시피에서 그렇다고 말하므로, 그 줄은 나중에 손으로 더하는 것이 아니라
        # 이곳에서 나와야 합니다. 이 행들은 사람이 textures.txt에 붙여 넣으며, 파일에만 있는
        # `flow`는 다음 가져오기가 조용히 떨어뜨리는 `flow`입니다. 램프가 가르친 것과 같은
        # 규칙입니다. 자기 출력을 재현하지 못하게 된 레시피는 레시피이기를 그만둔 것입니다.
        flow = ('\nflow 70' if legal_name(n).startswith('star_lava')
                else '')
        print('t %s\nimage %s %d%s\n'
              % (legal_name(n), legal_name(n), tiles, flow))

    if not a.emit:
        print('(nothing written -- pass --emit)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
