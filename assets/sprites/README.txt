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
build time, so the game contains no image decoder. Each 128x96 weapon frame
costs about 1.2KB; see the size report that build.ps1 prints.
