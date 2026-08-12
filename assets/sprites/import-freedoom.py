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
WPN_CW, WPN_CH = 128, 96
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
# SHTGA0 is deliberately unused. It is Doom's resting shotgun, drawn on the
# assumption that most of the weapon is below the screen edge, so all that is
# in the file is the end of the barrel -- in a cell of our proportions it lands
# as a fragment in the corner. B is the resting gun that is actually all there.
SUBJECTS = {
    'imp':    ['POSSA1', 'POSSC1', 'POSSF1', 'POSSG1', 'POSSL0'],
    'brute':  ['BOSSA1', 'BOSSC1', 'BOSSG1', 'BOSSH1', 'BOSSO0'],
    'hound':  ['SARGA1', 'SARGC1', 'SARGF1', 'SARGH1', 'SARGN0'],
    'caster': ['HEADA1', 'HEADB1', 'HEADD1', 'HEADF1', 'HEADL0'],
    'gun':    ['SHTGB0', 'SHTGC0', 'SHTGD0'],
}


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


def scaled(im, s):
    w = max(1, round(im.width * s))
    h = max(1, round(im.height * s))
    im = im.resize((w, h), Image.NEAREST if s > 1.0 else Image.LANCZOS)
    r, g, b, a = im.split()
    return Image.merge('RGBA', (r, g, b, a.point(lambda v: 255 if v > 110 else 0)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--preview', action='store_true',
                    help='also write _preview.png, every cell outlined')
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
            im = Image.open(p).convert('RGBA')
            src[l] = im.resize((im.width, round(im.height * DOOM_Y)), Image.LANCZOS)

    print('%-7s %-2s %-7s %-10s %-9s %-8s %s'
          % ('subject', 'fr', 'lump', 'corrected', 'in cell', 'at', 'scale'))

    for who, lumps in SUBJECTS.items():
        weapon = (who == 'gun')
        cw, ch = (WPN_CW, WPN_CH) if weapon else (SPR_CW, SPR_CH)
        ims = [src[l] for l in lumps]
        lefts = [-off[l][0] for l in lumps]
        live = list(range(len(ims))) if weapon else [0, 1, 2, 3]
        tall = max(ims[i].height for i in live)

        if weapon:
            # Keep the frames' relative travel: fit the union of all of them,
            # because the pump swinging left IS the animation.
            lo = min(lefts[i] for i in live)
            hi = max(lefts[i] + ims[i].width for i in live)
            s = min(ch / tall, cw / (hi - lo))
            ax = cw * 0.5 - (lo + hi) * 0.5 * s
        else:
            s = min(ch / tall, cw / max(ims[i].width for i in live))
            ax = cw * 0.5

        for i, lump in enumerate(lumps):
            im = src[lump]
            si = s if (weapon or i < 4) else min(s, cw / im.width, ch / im.height)
            im2 = scaled(im, si)
            x = round(ax + lefts[i] * si) if weapon else round(ax - off[lump][0] * si)
            y = ch - im2.height
            # A frame that fits the cell must be inside it. The corpses land a
            # couple of pixels over because the anchor is off-centre in a
            # sprawled body, and losing an arm to keep an anchor honest is the
            # wrong trade: the anchor exists to stop an animation sliding, and
            # a corpse is one frame with nothing to slide against.
            if im2.width <= cw:
                x = max(0, min(x, cw - im2.width))

            cell = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
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
            print('%-7s %-2d %-7s %3dx%-6d %3dx%-5d %3d,%-4d %.3f'
                  % (who, i, lump, im.width, im.height,
                     im2.width, im2.height, x, y, si))

    if args.preview:
        write_preview()


def write_preview():
    from PIL import ImageDraw
    rows = list(SUBJECTS)
    gw = WPN_CW + 8
    sheet = Image.new('RGB', (gw * 5 + 8, (WPN_CH + 22) * len(rows) + 8), (24, 24, 28))
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
    out = os.path.join(HERE, '_preview.png')
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(out)
    print('preview ->', out)


if __name__ == '__main__':
    main()
