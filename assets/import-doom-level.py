#!/usr/bin/env python3
"""Convert one Freedoom level into this project's level text.

    python import-doom-level.py dm03 > /dev/null   # prints a report
    python import-doom-level.py dm03 --emit        # writes the level text

THIS IS AN AUTHORING TOOL AND THE BUILD DOES NOT RUN IT, exactly as the sprite
and sound importers beside it are not run. What it prints is meant to be pasted
into assets/levels.txt and then EDITED -- see WHY THE OUTPUT IS A DRAFT below.

Freedoom is BSD-3 licensed, which is what makes its levels usable here at all.

id's own Quake maps are not an option either, and the reason is NOT the one
this paragraph gave for years. It said they "released the engine and the game
code, never the maps". That is false: John Romero released the original Quake
.map sources in October 2006, e1m1 through e4m8 and dm1 through dm6, and they
carry the GPL like the engine does. They exist, they are the real thing, and
they are refused for the same reason OpenArena and Xonotic are -- this game
bakes its maps INTO the executable, so a copyleft map is a copyleft binary.

Worth correcting rather than quietly deleting, because the two verdicts are not
interchangeable. "There is nothing to take" invites nobody to look again;
"there is something and its licence is wrong for us" tells the next reader
exactly what would have to change for the answer to change.

THE SECOND HALF OF THIS PARAGRAPH USED TO SAY that Doom's format was the only
one that could be converted, because "Quake maps are CSG brushes compiled to a
BSP tree and share no structure with this engine at all". That was true when it
was written and stopped being true when brush.c landed: this engine reads Valve
220 and Standard .map text directly, which is what TrenchBroom saves. See
assets/maps/import-librequake.py, which converts a BSD-3 Quake map without a
compiler in the path -- and note that it converts the .map SOURCE, not a .bsp.
The sentence outlived what it was reasoning about, which is the one thing this
tree asks a comment not to do.

WHAT DOOM STORES AND WE DO NOT
------------------------------
Doom does not store a sector as a polygon. It stores linedefs, each naming the
sector to its right and to its left, and a sector's shape is whatever loop
those edges happen to form. Two consequences:

  * The outline has to be RECONSTRUCTED by chaining edges, which is most of
    this file.
  * A sector may have SEVERAL loops. A pillar standing in a room is one sector
    with an outer boundary and an inner one, and a Sector here has a single
    outline with no way to say that. Those inner loops are dropped and counted,
    because dropping them silently would leave a pillar you can see and walk
    through.

WHY THE OUTPUT IS A DRAFT
-------------------------
A Freedoom level was designed for a player who moves at Doom's speed and cannot
jump. This game walks at 10.8 m/s and has a grapple and a leaping melee attack,
so a faithful conversion tends to read as wide and flat. The geometry is a
starting point; the vertical routes are not in the source and have to be added.
"""
import struct
import sys
import urllib.request

RAW = 'https://raw.githubusercontent.com/freedoom/freedoom/master/levels/%s.wad'

# Doom units to this project's file units (centimetres).
#
# A Doom player is 56 units tall and ours is 1.8m, which puts a Doom unit at
# about 3.2cm. Rounded to 3 because the number is a judgement either way -- Doom
# never had a consistent real-world scale -- and because a round factor keeps
# the emitted coordinates readable when somebody edits them afterwards.
SCALE = 3

# Doom thing type -> what we place. Anything absent is skipped, which is most
# of the table: decorations, deathmatch starts and difficulty variants have no
# counterpart here and inventing one would fill the level with entities the
# player cannot interpret.
THINGS = {
    2001: 'rapid',       9: 'imp',        3004: 'imp',
    2002: 'rapid',    2003: 'grenade',    2005: 'axe',
    2007: 'rapidammo', 2008: 'ammo',      2010: 'grenadeammo',
    2011: 'health',   2012: 'health',       13: 'redkey',
    3001: 'hound',    3002: 'hound',      3006: 'hound',
    3003: 'brute',      16: 'brute',        68: 'caster',
}
PLAYER_START = 1


def lumps(d):
    _, n, off = struct.unpack('<4sii', d[:12])
    out = {}
    for i in range(n):
        lo, ls, nm = struct.unpack('<ii8s', d[off + i * 16:off + i * 16 + 16])
        out[nm.rstrip(b'\0').decode('latin-1')] = d[lo:lo + ls]
    return out


def load(name):
    return lumps(urllib.request.urlopen(RAW % name, timeout=60).read())


def loops_of(edges):
    """Chain (a,b) edges into closed loops of vertex indices."""
    nxt = {}
    for a, b in edges:
        nxt.setdefault(a, []).append(b)

    used = set()
    out = []
    for a, b in edges:
        if (a, b) in used:
            continue
        used.add((a, b))
        loop = [a, b]
        cur = b
        while cur != a and len(loop) < 4096:
            cand = [c for c in nxt.get(cur, []) if (cur, c) not in used]
            if not cand:
                break
            used.add((cur, cand[0]))
            cur = cand[0]
            loop.append(cur)
        if cur == a and len(loop) >= 4:
            out.append(loop[:-1])
    return out


def area2(pts):
    s = 0
    for i in range(len(pts)):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % len(pts)]
        s += x0 * y1 - x1 * y0
    return s


def convert(name):
    L = load(name)
    verts = [struct.unpack('<hh', L['VERTEXES'][i:i + 4])
             for i in range(0, len(L['VERTEXES']), 4)]
    sides = [struct.unpack('<hh8s8s8sh', L['SIDEDEFS'][i:i + 30])
             for i in range(0, len(L['SIDEDEFS']), 30)]
    lines = [struct.unpack('<hhhhhhh', L['LINEDEFS'][i:i + 14])
             for i in range(0, len(L['LINEDEFS']), 14)]
    secs = [struct.unpack('<hh8s8shhh', L['SECTORS'][i:i + 26])
            for i in range(0, len(L['SECTORS']), 26)]
    things = [struct.unpack('<hhhhh', L['THINGS'][i:i + 10])
              for i in range(0, len(L['THINGS']), 10)]

    edges = {}
    for (v1, v2, flags, spec, tag, right, left) in lines:
        for sd, a, b in ((right, v1, v2), (left, v2, v1)):
            if 0 <= sd < len(sides):
                edges.setdefault(sides[sd][5], []).append((a, b))

    out, dropped, skipped = [], 0, 0
    for si, s in enumerate(secs):
        floor, ceil = s[0], s[1]
        ls = loops_of(edges.get(si, []))
        if not ls:
            skipped += 1
            continue

        # The OUTER loop is the one enclosing the most area. Inner loops are
        # holes this format cannot express, so they are dropped and counted.
        ls.sort(key=lambda lp: abs(area2([verts[v] for v in lp])), reverse=True)
        dropped += len(ls) - 1
        pts = [verts[v] for v in ls[0]]

        # Consistent winding, so every sector agrees with every other about
        # which side of an edge is inside.
        if area2(pts) < 0:
            pts.reverse()

        out.append((si, floor, ceil, pts))

    # The player start, and the entities.
    start = None
    ents = []
    for (tx, ty, ang, typ, flags) in things:
        if typ == PLAYER_START:
            start = (tx * SCALE, ty * SCALE)
        elif typ in THINGS:
            ents.append((THINGS[typ], tx * SCALE, ty * SCALE))

    return dict(name=name, sectors=out, ents=ents, start=start,
                dropped=dropped, skipped=skipped,
                src_sectors=len(secs), src_things=len(things))


def emit(r):
    L = []
    sx, sz = r['start'] if r['start'] else (0, 0)
    L.append('l %s' % r['name'])
    L.append('start %d 1200 %d' % (sx, sz))
    L.append('')
    for (si, floor, ceil, pts) in r['sectors']:
        L.append('s')
        L.append('floor %d  ceil %d' % (floor * SCALE, ceil * SCALE))
        L.append('mat floor ptile  wall wall_brick  ceil ppanel')
        L.append('p')
        row = []
        for (x, y) in pts:
            row.append('%d %d' % (x * SCALE, y * SCALE))
        L.append('   ' + '   '.join(row))
        L.append('')
    for (kind, x, z) in r['ents']:
        L.append('e %-12s %6d %6d' % (kind, x, z))
    return '\n'.join(L)


def main():
    if len(sys.argv) < 2:
        sys.exit('usage: import-doom-level.py <level> [--emit]')
    r = convert(sys.argv[1])

    worst = max((len(p) for _, _, _, p in r['sectors']), default=0)
    sys.stderr.write(
        '%s: %d/%d sectors kept, %d entities from %d things\n'
        '  largest outline %d points, %d inner loops dropped, %d sectors empty\n'
        % (r['name'], len(r['sectors']), r['src_sectors'],
           len(r['ents']), r['src_things'], worst, r['dropped'], r['skipped']))
    if r['start'] is None:
        sys.stderr.write('  NO PLAYER START -- the level has nowhere to begin\n')

    if '--emit' in sys.argv:
        print(emit(r))


main()
