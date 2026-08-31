#!/usr/bin/env python3
"""Fetch Freedoom sprites and convert them into this project's cells.

    python import-freedoom.py            # write the PNGs next to this file
    python import-freedoom.py --preview  # also write a contact sheet

THIS IS AN AUTHORING TOOL AND THE BUILD DOES NOT RUN IT. The PNGs it produces
are committed, so a normal build needs nothing from here -- the same standing
Aseprite has in assets/sprites/README.txt. It is kept because the PNGs are the
RESULT of a conversion, and this file is the recipe: the frame choices, the
anchor rule, the aspect correction and the scale rule are all decisions with
reasons, and without them written down as code the next import re-derives them
by guessing. It needs Python 3 and Pillow, neither of which the build does.

Freedoom is BSD-3 licensed. Running this puts its artwork in the tree, and
bake.ps1 then requires the attribution notice in src/scene.c to match
docs/LICENSE-Freedoom.txt word for word. See the Credits section of README.md.

WHAT THE CONVERSION HAS TO GET RIGHT
------------------------------------
ANCHOR. Doom crops every sprite to its own ink and stores the creature's
origin separately, so the firing frame is narrow because the arm left the box,
not because the body moved. Centring such a frame shoves the body sideways --
measured at -4.5px on POSSF1, over a tenth of its width, which reads as the
zombie flinching left every time it shoots. The offsets live in Freedoom's
buildcfg.txt. (Some PNGs also carry a grAb chunk, but only some: that chunk is
a staging area for buildcfg.txt, not the record.) X comes from there. Y does
not -- its drift is a uniform +4 on every frame, a constant shift rather than
jitter, and using the image bottom keeps the four pixels Doom would have sunk
below the floor.

ASPECT. Doom's 320x200 was displayed at 4:3, so its pixels are 1.2x taller
than wide. Drawn square, every creature is squat.

SCALE. One scale per subject, never per frame -- a per-frame fit makes the
creature breathe. The living frames set it and they fill the cell height,
because the cell maps to the collision height in metres and a sprite that
underfills it is a creature you can shoot over the head of and still hit. The
corpse is exempt: Doom's death frames sprawl wider than the cell (the brute's
is 90px against a 64px cell) and at the body's scale a third of it would be
cut off. Scaled to fit, it is still wider than the standing figure, so it
reads as a body on the floor rather than as a smaller creature.

RESAMPLING. LANCZOS down, NEAREST up. Enlarging this art with a smooth filter
invents intermediate colours, and a 16-entry palette then spends entries
resolving a blur that was never in the drawing.

ALPHA. Hard-thresholded after resampling. The renderer alpha-tests, so a
half-transparent edge pixel is a lit pixel and a resampled silhouette grows a
bright fringe without this.
"""
import argparse
import os
import re
import sys
import urllib.request

try:
    from PIL import Image
except ImportError:
    sys.exit('needs Pillow:  python -m pip install pillow')

RAW = 'https://raw.githubusercontent.com/freedoom/freedoom/master/'
HERE = os.path.dirname(os.path.abspath(__file__))

SPR_CW, SPR_CH = 64, 96      # keep in step with sprite.h
PK_CW, PK_CH = 48, 48
WPN_CW, WPN_CH = 192, 104
WPN_DOOM_W, WPN_DOOM_FULL, WPN_DOOM_VIEW, WPN_DOOM_TOP = 320, 200, 168, 24
WEAPONTOP = 32               # psp->sy for a weapon that is up and ready
BASEYCENTER = 100            # R_DrawPSprite's fixed reference, not the view centre
DOOM_Y = 1.2
MAGENTA = (255, 0, 255, 255)

# ours -> Freedoom lump, in our frame order.
#   monsters: 0 walk-A  1 walk-B  2 attack  3 hurt  4 dead
#   weapon:   0 rest    1 raised  2 pump open      (POSES, not moments)
#
# The weapon list is three drawings, not four moments. Doom's pump passes
# through the same pose going out and coming back, so a fourth cell would hold
# a byte-identical copy of the second -- which is what it did until weapon.c
# grew a PUMP_CYCLE table that can name a pose twice for free.
#
# Rotation 1 is the front view and the only one worth taking: this engine
# billboards monsters towards the player, so the other seven are unreachable.
# Death frames carry rotation 0 because a corpse looks the same from every
# angle -- which is also how the frame list tells you which letters are deaths.
#
# The frame LETTERS do not map to our numbers by position. A and B are a walk
# cycle, but which letter is the attack differs per monster, so these were read
# off the sheet rather than assumed. A and C rather than A and B for the walk:
# they are the two extremes of Doom's four-frame cycle, and a two-frame cycle
# wants the extremes.
#
# WHICH FRAME IS THE IDLE COMES FROM DOOM'S STATE TABLE, NOT FROM THE ART.
# Reading the drawings and picking the one that looks like a resting weapon
# gets it wrong twice over, and both were shipped before this comment existed:
#
#   SHTGA0 was dropped as "just the end of a barrel" -- it IS the shotgun's
#   idle (S_SGUN, A_WeaponReady), drawn on the assumption that the rest of the
#   gun is below the screen edge. Standing SHTGB0 in for it put the FIRST PUMP
#   FRAME on screen as the resting pose.
#
#   SAWG C and D are the chainsaw's idle (S_SAW/S_SAWB), and A and B are the
#   cut (S_SAW1/S_SAW2). They are the wider drawings, which looks backwards
#   next to the narrower A/B pair, and taking them for the attack swapped the
#   saw's rest and swing with each other.
#
# info.c numbers frames 0=A, 1=B, 2=C, 3=D; the order below is idle first,
# then whatever the recovery cycle walks through.
#
# 어느 프레임이 대기 자세인지는 아트가 아니라 Doom의 상태 표에서 옵니다. 그림을 보고
# 쉬고 있어 보이는 것을 고르면 두 번 틀리며, 둘 다 이 주석이 생기기 전에 배포되었습니다.
#
# One row per weapon, named for the row in weapon.c's WEAPONS table, so the
# name is what pairs a drawing with a weapon and adding a weapon adds a row
# rather than an edit. The counts differ on purpose: Doom gives the chaingun
# and the launcher two frames each, and padding the short ones to a common
# length would store a duplicate to fill a slot -- the thing the cycle tables
# exist to avoid.
WEAPON_SUBJECTS = ('shotgun', 'grenade', 'rapid', 'axe')

# Floor items. The `item` prefix is load-bearing: without it a drawing called
# `shotgun0` would be both the shotgun's VIEWMODEL and the shotgun lying on the
# floor. After the prefix comes the exact name a level uses to place the thing,
# so pickup.c's resolver serves both and there is no second table to drift.
#
# These are Doom's own floor sprites, not its viewmodels, and that distinction
# is why importing them is right where using viewmodel art would not have been:
# a viewmodel is drawn to be read at arm's length filling the screen bottom and
# becomes a dark smear on the floor across a room, while MEDI and SHEL were
# drawn to be recognised from exactly that distance.
PICKUP_SUBJECTS = {
    'itemhealth':       'MEDIA0',   # medikit
    'itemammo':         'SHELA0',   # the legacy name for the shotgun's box
    'itemshotgunammo':  'SHELA0',   # shells
    'itemgrenadeammo':  'ROCKA0',   # a rocket, for the weapon that arcs
    'itemrapidammo':    'CLIPA0',   # a bullet clip, for the fast weapon
    'itemaxeammo':      'CELLA0',   # an energy cell, for the slam charges
    'itemshotgun':      'SHOTA0',
    'itemgrenade':      'LAUNA0',
    'itemrapid':        'MGUNA0',
    'itemaxe':          'CSAWA0',
    'itemredkey':       'RKEYA0',
    'itembluekey':      'BKEYA0',
    'itemyellowkey':    'YKEYA0',
}
SUBJECTS = {
    'imp':     ['POSSA1', 'POSSC1', 'POSSF1', 'POSSG1', 'POSSL0'],
    'brute':   ['BOSSA1', 'BOSSC1', 'BOSSG1', 'BOSSH1', 'BOSSO0'],
    'caster':  ['HEADA1', 'HEADB1', 'HEADD1', 'HEADF1', 'HEADL0'],
    # idle, then the pump: A B C D, and the cycle walks A B C D C B A
    'shotgun': ['SHTGA0', 'SHTGB0', 'SHTGC0', 'SHTGD0'],
    # the launcher: A at rest, B for the whole shot
    'grenade': ['MISGA0', 'MISGB0'],
    # the chaingun: A at rest, alternating A/B while the barrel spins
    'rapid':   ['CHGGA0', 'CHGGB0'],
    # the chainsaw, for the axe: a held melee tool rather than a fist, and the
    # only melee viewmodel Doom draws as a weapon you can see. C and D FIRST
    # because they are the idle -- the saw revs at rest -- and A/B are the cut.
    'axe':     ['SAWGC0', 'SAWGD0', 'SAWGA0', 'SAWGB0'],
}
for _k, _v in PICKUP_SUBJECTS.items():
    SUBJECTS[_k] = [_v]


def fetch(url, dst):
    if not os.path.exists(dst):
        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()
        with open(dst, 'wb') as f:
            f.write(data)
    return dst


def load_offsets(path):
    off = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        t = line.split(';')[0].split()
        if len(t) >= 3:
            try:
                off[t[0]] = (int(t[1]), int(t[2]))
            except ValueError:
                pass
    return off


def scaled(im, sx, sy):
    """Resize by separate axes, because the 1.2 correction for Doom's
    non-square pixels is just a different scale on y -- doing it as its own
    resize step would resample the art twice for one transform."""
    w = max(1, round(im.width * sx))
    h = max(1, round(im.height * sy))
    up = (sx > 1.0 or sy > 1.0)
    im = im.resize((w, h), Image.NEAREST if up else Image.LANCZOS)
    r, g, b, a = im.split()
    return Image.merge('RGBA', (r, g, b, a.point(lambda v: 255 if v > 110 else 0)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview', action='store_true',
                    help='also write .freedoom-cache/_preview.png, cells outlined')
    ap.add_argument('--cache', default=os.path.join(HERE, '.freedoom-cache'))
    args = ap.parse_args()

    os.makedirs(args.cache, exist_ok=True)
    cfg = fetch(RAW + 'buildcfg.txt', os.path.join(args.cache, 'buildcfg.txt'))
    off = load_offsets(cfg)

    src = {}
    for lumps in SUBJECTS.values():
        for l in lumps:
            if l in src:
                continue
            if l not in off:
                sys.exit('%s has no offset in buildcfg.txt' % l)
            p = fetch(RAW + 'sprites/%s.png' % l.lower(),
                      os.path.join(args.cache, l.lower() + '.png'))
            src[l] = Image.open(p).convert('RGBA')

    # The weapon cell is Doom's screen, so its scales are fixed by geometry
    # rather than chosen per weapon: one Doom column is WPN_CW/320 of the cell
    # and one Doom row is WPN_CH/144 of it. Their ratio is 1.204, which is the
    # 1.2 correction for Doom's non-square pixels arriving on its own.
    SX = WPN_CW / float(WPN_DOOM_W)
    SY = WPN_CH / float(WPN_DOOM_VIEW - WPN_DOOM_TOP)

    # Set by the largest floor item, so it fits and everything else keeps its
    # size relative to it.
    PICKUP_SCALE = min(
        min(PK_CW / float(src[l].width),
            PK_CH / (src[l].height * DOOM_Y))
        for l in PICKUP_SUBJECTS.values())

    print('%-8s %-2s %-7s %-9s %-9s %-9s %s'
          % ('subject', 'fr', 'lump', 'source', 'in cell', 'at', 'doom screen'))

    for who, lumps in SUBJECTS.items():
        weapon = who in WEAPON_SUBJECTS
        pickup = who in PICKUP_SUBJECTS
        cw, ch = ((WPN_CW, WPN_CH) if weapon
                  else (PK_CW, PK_CH) if pickup else (SPR_CW, SPR_CH))
        ims = [src[l] for l in lumps]

        if pickup:
            # ONE SCALE ACROSS EVERY PICKUP, not one per item. The cell is
            # drawn as a fixed square in world space, so a per-item fit would
            # make a box of shells exactly as large as a rocket launcher --
            # and Doom drew them at 14 and 59 units precisely because they are
            # not the same size. Scaling them together keeps that, at the cost
            # of the smallest items being small, which is what they are.
            # 아이템마다가 아니라 *모든* 아이템에 하나의 배율을 씁니다. 셀은 월드에서
            # 고정된 정사각형으로 그려지므로 아이템별로 맞추면 산탄 상자가 로켓 발사기와
            # 똑같이 커집니다. Doom이 그것들을 14단위와 59단위로 그린 이유가 바로 둘이
            # 같은 크기가 아니기 때문입니다. 함께 배율을 맞추면 그 관계가 유지되며,
            # 대가는 가장 작은 것이 작아진다는 점인데 그것이 원래의 크기입니다.
            s = PICKUP_SCALE
        elif not weapon:
            # A monster fills its cell height; see the header. Doom's vertical
            # correction is folded into the y scale rather than pre-applied.
            live = [0, 1, 2, 3]
            tall = max(ims[i].height * DOOM_Y for i in live)
            s = min(ch / tall, cw / max(ims[i].width for i in live))

        for i, lump in enumerate(lumps):
            im = src[lump]
            xo, yo = off[lump]

            if weapon:
                # Doom's own placement, straight out of R_DrawPSprite with a
                # ready weapon:
                #     texturemid = BASEYCENTER - (sy - topoffset)
                #     top        = centery - texturemid
                # centery is half the 3D VIEW while BASEYCENTER is a fixed 100,
                # so the status bar does not merely hide the bottom of the gun:
                # it moves the gun. Using the 200-row fullscreen centre instead
                # sat the shotgun 16 rows too high and a fifth too small.
                # 상태 표시줄은 총의 아래쪽을 가리기만 하는 것이 아니라 총을 *옮깁니다*.
                # 200행 전체 화면의 중심을 쓰면 샷건이 16행 높고 5분의 1 작아집니다.
                top = WPN_DOOM_VIEW / 2.0 - BASEYCENTER + WEAPONTOP - yo
                im2 = scaled(im, SX, SY)
                x = round(-xo * SX)
                y = round((top - WPN_DOOM_TOP) * SY)
                doom = 'x %3d y %3.0f' % (-xo, top)
            elif pickup:
                im2 = scaled(im, s, s * DOOM_Y)
                x = max(0, min(round(cw * 0.5 - xo * s), cw - im2.width))
                y = ch - im2.height
                doom = '-'
            else:
                si = s if i < 4 else min(s, cw / im.width,
                                         ch / (im.height * DOOM_Y))
                im2 = scaled(im, si, si * DOOM_Y)
                x = round(cw * 0.5 - xo * si)
                y = ch - im2.height
                # A frame that fits the cell must be inside it. The corpses
                # land a couple of pixels over because the anchor is off-centre
                # in a sprawled body, and losing an arm to keep an anchor
                # honest is the wrong trade: the anchor exists to stop an
                # animation sliding, and a corpse is one frame with nothing to
                # slide against.
                if im2.width <= cw:
                    x = max(0, min(x, cw - im2.width))
                doom = '-'

            cell = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
            # Out-of-cell pixels are dropped rather than clamped back in.
            # Doom clips the chainsaw's cutting frames at the right edge of
            # the screen and they are drawn expecting it; pulling them back
            # would move a weapon its artist deliberately pushed off-frame.
            cell.paste(im2, (x, y), im2)

            if weapon:
                # The muzzle is the ink at the top of the barrel, derived
                # rather than typed in, so redrawing the gun moves the flash
                # with it. bake.ps1 reads the magenta pixel and leaves it
                # transparent; it is a marker and is never drawn.
                a = cell.split()[3].load()
                top = next((yy for yy in range(ch)
                            for xx in range(cw) if a[xx, yy] > 127), None)
                if top is not None:
                    cols = [xx for xx in range(cw) if a[xx, top] > 127]
                    cell.putpixel(((cols[0] + cols[-1]) // 2, top), MAGENTA)

            cell.save(os.path.join(HERE, '%s%d.png' % (who, i)))
            print('%-8s %-2d %-7s %3dx%-5d %3dx%-5d %3d,%-5d %s'
                  % (who, i, lump, im.width, im.height,
                     im2.width, im2.height, x, y, doom))

    if args.preview:
        write_preview()


def write_preview():
    from PIL import ImageDraw
    rows = list(SUBJECTS)
    gw = WPN_CW + 8
    cols = max(len(v) for v in SUBJECTS.values())
    sheet = Image.new('RGB', (gw * cols + 8, (WPN_CH + 22) * len(rows) + 8),
                      (24, 24, 28))
    d = ImageDraw.Draw(sheet)
    for r, who in enumerate(rows):
        for c in range(len(SUBJECTS[who])):
            im = Image.open(os.path.join(HERE, '%s%d.png' % (who, c))).convert('RGBA')
            x, y = 4 + c * gw, 4 + r * (WPN_CH + 22) + 18
            for by in range(0, im.height, 8):
                for bx in range(0, im.width, 8):
                    if (bx // 8 + by // 8) % 2:
                        d.rectangle([x + bx, y + by, x + bx + 7, y + by + 7],
                                    fill=(46, 46, 52))
            sheet.paste(im, (x, y), im)
            d.rectangle([x, y, x + im.width - 1, y + im.height - 1],
                        outline=(120, 120, 135))
            d.text((x + 2, y - 13), '%s%d' % (who, c), fill=(235, 225, 165))
    # Written OUTSIDE the directory bake.ps1 scans. It landed inside once and
    # was baked as a sprite: 411KB carried in the binary for a contact sheet
    # nothing draws. bake.ps1 now skips '_'-prefixed names too, but the cheaper
    # guard is not putting it there.
    # bake.ps1이 훑는 디렉터리 *바깥*에 씁니다. 한 번 안쪽에 떨어져 스프라이트로
    # 구워졌고, 아무것도 그리지 않는 대조 시트를 위해 411KB가 바이너리에 실렸습니다.
    out = os.path.join(HERE, '.freedoom-cache', '_preview.png')
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(out)
    print('preview ->', out)


if __name__ == '__main__':
    main()
