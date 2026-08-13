#!/usr/bin/env python3
"""Fetch Freedoom's wall patches and prepare them as this project's surfaces.

    python import-walls.py

THIS IS AN AUTHORING TOOL AND THE BUILD DOES NOT RUN IT, exactly as
import-freedoom.py beside it is not run. It writes PNGs here and those are
committed; bake.ps1 is what turns them into the indexed, run-length data the
game carries.

WHY THESE LAND IN assets/sprites/ RATHER THAN A DIRECTORY OF THEIR OWN
---------------------------------------------------------------------
Because everything that has to happen to them already happens here. bake.ps1
groups this directory by SUBJECT and gives each group its own median-cut
palette, which is exactly what a wall texture wants -- a texture is its own
subject, so each gets sixteen colours chosen for it alone. The licence guard
that requires the Freedoom notice watches this directory. The round-trip guard
that proves the encoder is reversible runs over it. A second directory would
have needed all three again, and the third copy of a rule is where the copies
start disagreeing.

They are named `wall_*` so they cannot collide with the monster and weapon
names sprite.c matches on; anything it does not recognise it ignores.

TEXTURES ARE NOT SPRITES IN ONE RESPECT that matters to the encoder: index 0
is reserved for transparent and never drawn, which costs a wall one of its
sixteen colours for nothing. That is accepted rather than worked around -- a
fifteen-colour wall at 128x128 is what Doom itself looked like, and a second
encoder to save one palette entry would be a second encoder.

Freedoom is BSD-3 licensed. Running this puts its artwork in the tree, and
bake.ps1 then requires the attribution notice in src/scene.c to match
docs/LICENSE-Freedoom.txt word for word.

WHY A CURATED HANDFUL rather than everything: 999 patches is 40MB of PNG, and
this game has 1.05MB of floppy left. The set below is chosen to span Doom's
surface vocabulary -- masonry, stone, metal, tech, marble -- because what makes
a level read as Doom is the RANGE of surfaces in one room, not the fidelity of
any one of them.
"""
import os
import struct
import sys
import urllib.request
import zlib

RAW = 'https://raw.githubusercontent.com/freedoom/freedoom/master/patches/%s.png'
HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, '.freedoom-walls')

# our surface name -> the Freedoom patch it comes from.
#
# The door is one image and not three. Doom ships DOORRED, DOORBLU and DOORYEL
# as separate textures; here the key colour is a TINT applied by the material
# recipe over this one bitmap, which is three surfaces for the bytes of one and
# is the same bargain every other material in textures.txt already strikes.
# The second column is how the patch reaches 128x128, and it is not a detail:
# 'tile' repeats it, 'fit' scales it once.
#
# A WALL PATCH IS A TILE AND A DOOR FACE IS A PICTURE. Doom's wall patches are
# drawn to repeat and their sizes say so -- 8x128 for a track meant to run the
# height of any opening. A door is the opposite: one image of one door, and
# repeating it produces two half-doors side by side, which is exactly what the
# first run of this script produced. The distinction cannot be inferred from
# the size, so it is written down.
#
# 두 번째 열은 패치가 128x128에 이르는 방법이며 사소한 것이 아닙니다. 'tile'은 반복하고
# 'fit'은 한 번 확대합니다. 벽 패치는 *타일*이고 문짝은 *그림*입니다. Doom의 벽 패치는
# 반복하도록 그려졌고 크기가 그것을 말합니다. 문은 반대로, 문 하나의 이미지 하나이며
# 반복하면 반쪽짜리 문 둘이 나란히 놓입니다. 이 스크립트의 첫 실행이 정확히 그것을
# 만들었습니다. 구분은 크기로 유추할 수 없으므로 적어 둡니다.
WALLS = {
    'wall_brick':  ('brick',    'tile'),  # clay masonry, the baseline
    'wall_stone':  ('stwall',   'tile'),  # dressed stone blocks
    'wall_rough':  ('stonew1',  'tile'),  # broken, irregular stone
    'wall_metal':  ('mwall1_1', 'tile'),  # riveted plate
    'wall_plain':  ('bigwall',  'tile'),  # large flat panel, for long runs
    'wall_marble': ('marble1',  'tile'),  # polished, for the places that matter
    'wall_door':   ('door2_1',  'fit'),   # the door face -- one door, not four
    'wall_track':  ('doortrak', 'tile'),  # the channel a door slides into
}


def fetch(patch):
    os.makedirs(CACHE, exist_ok=True)
    p = os.path.join(CACHE, patch + '.png')
    if not os.path.exists(p):
        with urllib.request.urlopen(RAW % patch, timeout=60) as r:
            open(p, 'wb').write(r.read())
    return p


def read_png(path):
    """Minimal PNG reader: 8-bit RGB/RGBA/palette, no interlace.

    Written out rather than imported because this project links no libraries
    and its authoring tools hold the same line -- a tool that needs a pip
    install is a tool the next person cannot run.
    """
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('%s is not a PNG' % path)

    pos, idat, pal, trns = 8, b'', None, None
    w = h = depth = ctype = 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            if body[12] != 0:
                sys.exit('%s is interlaced' % path)
        elif typ == b'PLTE':
            pal = body
        elif typ == b'tRNS':
            trns = body
        elif typ == b'IDAT':
            idat += body
        elif typ == b'IEND':
            break
        pos += 12 + ln

    # SUB-BYTE PALETTE INDICES. Freedoom stores a patch at whatever depth its
    # colour count needs, so `bigwall` is 4-bit: two pixels per byte, high
    # nibble first. Unfiltering has to happen at BYTE level and the unpacking
    # after it, because the filters operate on bytes and know nothing about how
    # many pixels are inside one -- doing it the other way round produces an
    # image that is right along the top edge and drifts into noise below.
    # Freedoom은 색 수에 맞는 깊이로 패치를 저장하므로 `bigwall`은 4비트, 즉 바이트당 두
    # 픽셀이고 상위 니블이 먼저입니다. 언필터링은 *바이트* 단위로, 언패킹은 그 뒤에
    # 일어나야 합니다. 필터는 바이트를 다루며 한 바이트 안에 픽셀이 몇 개인지 알지 못합니다.
    # 순서를 바꾸면 위쪽 가장자리만 맞고 아래로 갈수록 잡음으로 번지는 이미지가 나옵니다.
    if depth not in (1, 2, 4, 8):
        sys.exit('%s is %d-bit; this reader handles 1/2/4/8' % (path, depth))
    if depth != 8 and ctype != 3:
        sys.exit('%s is %d-bit non-palette; only palette is sub-byte here'
                 % (path, depth))

    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = (w * chans * depth + 7) // 8

    # Undo the per-scanline filters. This is the whole of PNG's compression
    # beyond deflate, and getting it wrong produces an image that is plausible
    # at the top and garbage by the bottom -- which is worth saying because
    # that failure looks like a bad download rather than a bad decoder.
    out = bytearray()
    prev = bytearray(stride)
    at = 0
    for _ in range(h):
        f = raw[at]; at += 1
        line = bytearray(raw[at:at + stride]); at += stride
        for i in range(stride):
            a = line[i - chans] if i >= chans else 0
            b = prev[i]
            c = prev[i - chans] if i >= chans else 0
            if   f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out += line
        prev = line

    # Now unpack sub-byte indices, once the filters are done with the bytes.
    if depth != 8:
        per = 8 // depth
        mask = (1 << depth) - 1
        wide = bytearray(w * h)
        row_bytes = stride
        for y in range(h):
            for x in range(w):
                b = out[y * row_bytes + x // per]
                shift = 8 - depth * (x % per + 1)
                wide[y * w + x] = (b >> shift) & mask
        out = wide
        chans = 1

    # To straight RGBA.
    px = bytearray(w * h * 4)
    for i in range(w * h):
        s = out[i * chans:(i + 1) * chans]
        if   ctype == 0: r = g = b = s[0]; a = 255
        elif ctype == 4: r = g = b = s[0]; a = s[1]
        elif ctype == 2: r, g, b = s; a = 255
        elif ctype == 6: r, g, b, a = s
        else:
            idx = s[0]
            r, g, b = pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2]
            a = trns[idx] if (trns and idx < len(trns)) else 255
        px[i * 4:i * 4 + 4] = bytes((r, g, b, a))
    return w, h, px


def write_png(path, w, h, px):
    """8-bit RGBA, one IDAT, filter 0. Not small, and it does not need to be:
    bake.ps1 re-encodes these into the game's own format, so this file is an
    intermediate that only has to be readable."""
    raw = b''.join(b'\x00' + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, b):
        c = t + b
        return struct.pack('>I', len(b)) + c + struct.pack('>I', zlib.crc32(c))

    open(path, 'wb').write(
        b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(raw, 9))
        + chunk(b'IEND', b''))


def fit_to(w, h, px, tw, th):
    """Scale the patch once, nearest-neighbour.

    NEAREST, NOT AVERAGED. Doom's art is chunky on purpose and its palette is
    small; a bilinear enlargement invents colours the palette does not contain,
    and the median cut that follows in bake.ps1 then spends entries on those
    invented in-betweens instead of on the colours the artist chose.
    평균이 아니라 최근접입니다. Doom의 아트는 의도적으로 거칠고 팔레트가 작습니다. 이중선형
    확대는 팔레트에 없는 색을 만들어 내며, 이어지는 bake.ps1의 중앙값 분할이 작가가 고른
    색이 아니라 그 지어낸 중간색에 항목을 쓰게 됩니다.
    """
    out = bytearray(tw * th * 4)
    for y in range(th):
        sy = y * h // th
        for x in range(tw):
            sx = x * w // tw
            s = (sy * w + sx) * 4
            out[(y * tw + x) * 4:(y * tw + x) * 4 + 4] = px[s:s + 4]
    return out


def tile_to(w, h, px, tw, th):
    """Repeat the patch up to tw x th.

    TILED, NOT STRETCHED. Doom's patches are authored to repeat, and their
    sizes say so: 128x128 for a wall, 8x128 for a door track that is meant to
    run the height of any opening. Scaling an 8-wide track to 128 would turn a
    crisp metal channel into a smear, whereas repeating it is what the artist
    drew it for.
    """
    out = bytearray(tw * th * 4)
    for y in range(th):
        sy = y % h
        for x in range(tw):
            sx = x % w
            s = (sy * w + sx) * 4
            out[(y * tw + x) * 4:(y * tw + x) * 4 + 4] = px[s:s + 4]
    return out


# The size every surface lands at. 128 because that is what Doom's wall
# patches are and resampling them would be inventing detail the artist did not
# draw; tex.c's own buffer is 256 and repeats this twice, which is a whole
# number of tiles and therefore seamless.
TARGET = 128


def main():
    print('%-12s %-10s %9s %6s    %s'
          % ('surface', 'patch', 'source', 'how', 'result'))
    for name in sorted(WALLS):
        patch, how = WALLS[name]
        w, h, px = read_png(fetch(patch))
        out = (tile_to if how == 'tile' else fit_to)(w, h, px, TARGET, TARGET)
        dst = os.path.join(HERE, name + '.png')
        write_png(dst, TARGET, TARGET, out)
        print('%-12s %-10s %4dx%-4d %6s -> %s'
              % (name, patch, w, h, how, '%dx%d' % (TARGET, TARGET)))

    print('\n%d surfaces written to %s' % (len(WALLS), HERE))
    print('bake.ps1 turns them into the indexed data the game carries.')


if __name__ == '__main__':
    main()
