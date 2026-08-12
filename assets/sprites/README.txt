Hand-drawn sprites. Drop a PNG here and rebuild.

NAMING decides where a drawing lands. The name is "<subject><frame>":

    imp0.png     monster "imp", frame 0
    brute2.png   monster "brute", frame 2
    gun0.png     the weapon, frame 0

Monsters:  imp  brute  hound  caster
Frames:    0 walk-A   1 walk-B   2 attack   3 hurt   4 dead

Weapon:    gun
Frames:    0 idle   1 firing   2 pump back   3 pump returning

A name that matches nothing is ignored rather than painted over whichever
subject happens to be first, so a work-in-progress file parked here is
harmless.


THE WEAPON REPLACES THE 3D MODEL; A MONSTER DRAWING COMPOSITES OVER ITS
GENERATED ONE. That difference is deliberate. A half-drawn bestiary should
still show creatures, so a drawing is painted on top of the SDF version and a
monster with no art keeps the generated one. A gun drawn over the extruded 3D
gun would be two guns, so the moment gun0.png exists the model stops being
drawn -- and deleting the file brings it straight back. Nothing else in the
project changes either way.


A WEAPON DRAWING IS THE VIEWMODEL ONLY. The item lying on the floor is NOT
this sprite -- it is a generated icon, and that is a decision rather than a
gap waiting to be filled. A viewmodel is drawn to be seen from one angle,
filling the bottom of the screen; on the floor across a room it is a small
dark smear, because the detail that sells it up close is the first thing the
art resolution throws away. The floor icons were designed for that distance
instead, so colour says which weapon and the shard-vs-box shape says whether
it is the weapon or its ammunition. See pickup_pixel in src/sprite.c.


THE WEAPON FACES FORWARD, NOT SIDEWAYS. You are looking down your own sights,
so the weapon points AWAY into the screen: the barrel recedes to a muzzle near
the top-centre of the cell, the receiver and stock are below it, and your hands
are at the bottom edge. A side profile is what a weapon looks like in a shop
display, not what a held one looks like.

The cell is 128x96 and the drawing is centred horizontally and sits on the
BOTTOM, so leave the top of the cell empty and let the grip run off the bottom
edge -- that is what makes it read as being held rather than as floating.


THE MUZZLE IS ONE MAGENTA PIXEL (255, 0, 255). Put it where the flash and the
tracers should come from -- on a forward-facing weapon that is the far end of
the barrel, near the top-centre. It is a marker, never a colour: it is recorded
and then left transparent, so it never appears in the game.

Mark it on every frame if the barrel moves between them. A frame with no marker
falls back to frame 0's, which is right for a weapon whose barrel holds still.
A weapon with no marker anywhere simply draws no muzzle flash, rather than
putting one in a guessed place.

This exists because the alternative is a constant in weapon.c that somebody
edits to match the art, and this project already knows what that costs:
placing the shotgun that way failed three times in a row, which is why
modeledit puts a draggable muzzle on the 3D model. The marker is the same idea
for a drawing -- redraw the gun and the flash follows it.


USING FREEDOOM ART. Freedoom (https://freedoom.github.io/) is BSD-3 licensed
and is the intended source for this project's art; its sprites live in the
repository's sprites/ directory as individual PNGs. Their names are Doom's
convention -- a four-letter subject, a frame LETTER, and a rotation DIGIT:

    POSSA1     zombieman, frame A, rotation 1 = seen from the front
    POSSA2A8   one file serving two rotations, mirrored

Take rotation 1 only. This engine draws monsters as billboards that always
face the player, so the other seven views are never visible, and a set that
included them would spend its budget on frames the game cannot reach.

    ours     Freedoom      why
    imp      POSS          the baseline humanoid
    hound    SARG          low, fast, all mouth
    brute    BOSS / BOS2   the big one
    caster   HEAD          floats, attacks at range

Frame letters do not map to our frame numbers by position -- A and B are a walk
cycle, but which letter is the attack differs per monster, so open the sheet
and look. Rename to <subject><0-4> as above.

Two conversion steps are not optional. These were drawn for a 320x200 screen
with non-square pixels, so scale to about 32x32 and expect to repair the
result by hand; and they carry Doom's own 256-colour palette, which this
project re-quantises to its shared 16 automatically -- so check the baked
output rather than the PNG, because sixteen entries shared across every sprite
is a much harder constraint than the one the art was drawn for.

The moment a Freedoom-derived PNG lands here, the build begins requiring the
attribution notice in src/scene.c; see docs/LICENSE-Freedoom.txt and the
Credits section of README.md. That is a licence obligation, not a style rule.


SIZE is 32x32 by convention for monsters, but anything up to the 64x96 cell
works. Weapons use the 128x96 cell. A drawing is centred horizontally and sits
on the BOTTOM of its cell.

TRANSPARENCY is alpha < 128.

COLOUR is quantised to a shared 16-entry palette built from every sprite in
this directory, in filename order. Draw in whatever colours you like -- an
image with more than sixteen is snapped to the nearest entries. Sharing one
palette is what makes a set of sprites look like they belong to the same
game, and it is why the files are read in sorted order: a set that reordered
itself between builds would produce a different palette every time.

The PNG is never shipped. bake.ps1 converts it to palette-indexed text at
build time, so the game contains no image decoder. A 128x96 weapon frame
measures at about 1.3KB -- flat-shaded placeholder art, so busier drawings run
dearer, since the encoding pays per run of same-coloured pixels. See the size
report that build.ps1 prints.
