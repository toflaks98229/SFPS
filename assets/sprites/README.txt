Hand-drawn sprites. Drop a PNG here and rebuild.

NAMING decides where a drawing lands. The name is "<monster><frame>":

    imp0.png     monster "imp", frame 0
    brute2.png   monster "brute", frame 2

Monsters:  imp  brute  hound  caster
Frames:    0 walk-A   1 walk-B   2 attack   3 hurt   4 dead

A name that matches no monster is ignored rather than painted over whichever
one happens to be first, so a work-in-progress file parked here is harmless.

SIZE is 32x32 by convention, but anything up to the 64x96 cell works. A
drawing is centred horizontally and sits on the BOTTOM of its cell, so the
creature stands on the ground instead of floating.

TRANSPARENCY is alpha < 128. Transparent pixels are left alone, which means
a drawing composites OVER the procedurally generated creature underneath it
rather than punching a hole. A monster with no drawing keeps the generated
one entirely, so the art can be replaced one frame at a time.

COLOUR is quantised to a shared 16-entry palette built from every sprite in
this directory, in filename order. Draw in whatever colours you like -- an
image with more than sixteen is snapped to the nearest entries. Sharing one
palette is what makes a set of sprites look like they belong to the same
game, and it is why the files are read in sorted order: a set that reordered
itself between builds would produce a different palette every time.

The PNG is never shipped. bake.ps1 converts it to palette-indexed text at
build time, so the game contains no image decoder. A 32x32 sprite costs a few
hundred bytes; see the size report that build.ps1 prints.
