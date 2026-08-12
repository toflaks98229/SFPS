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


THE ART HERE IS IMPORTED, NOT DRAWN, and import-freedoom.py is how. Run it to
rebuild every PNG in this directory from Freedoom's own lumps:

    python import-freedoom.py --preview

It needs Python 3 and Pillow; the BUILD needs neither, because the PNGs it
writes are committed. It is kept for the same reason the .obj files are kept
rather than only their baked form: these images are the result of a
conversion, and the script is the recipe. The frame choices, the anchor rule,
the aspect correction and the scale rule are each a decision with a reason,
and its header is where those reasons are written down. Changing a frame means
editing SUBJECTS there and re-running, not editing a PNG by hand.

Freedoom names sprites the way Doom does -- a four-letter subject, a frame
LETTER, a rotation DIGIT:

    POSSA1     zombieman, frame A, rotation 1 = seen from the front
    POSSA2A8   one file serving two rotations, mirrored

Rotation 1 only. This engine billboards monsters towards the player, so the
other seven views are unreachable, and a set that included them would spend
its budget on frames the game cannot show. Death frames carry rotation 0
because a corpse looks the same from every angle -- which is also how the
frame list tells you which letters are deaths.

    ours     Freedoom   why
    imp      POSS       the baseline humanoid
    hound    SARG       the fast melee charger
    brute    BOSS       the big one
    caster   HEAD       floats, attacks at range
    gun      SHTG       the pump shotgun viewmodel

Frame letters do not map to our numbers by position: A and B are a walk cycle,
but which letter is the attack differs per monster, so they were read off the
sheet rather than assumed.

THE OFFSETS ARE THE PART PEOPLE GET WRONG. Doom crops each sprite to its own
ink and records the creature's origin separately, in buildcfg.txt at the root
of the Freedoom repository. Centring the images instead looks fine until the
firing frame, which is narrow because the arm left the box rather than because
the body moved -- POSSF1 is 4.5px off, and the zombie appears to flinch
sideways every time it shoots. Some PNGs also carry a grAb chunk, but only
some: that chunk is a staging area for buildcfg.txt, not the record.

The moment a Freedoom-derived PNG lands here, the build begins requiring the
attribution notice in src/scene.c to match docs/LICENSE-Freedoom.txt word for
word. That is a licence obligation, not a style rule; see the Credits section
of README.md.


SIZE is 32x32 by convention for monsters, but anything up to the 64x96 cell
works. Weapons use the 128x96 cell. A drawing is centred horizontally and sits
on the BOTTOM of its cell.

TRANSPARENCY is alpha < 128.

COLOUR is quantised to a 16-entry palette PER SUBJECT -- imp0..imp4 share one,
brute0..brute4 another. The subject is the filename with its trailing frame
number removed, so naming is all that groups them. Draw in whatever colours
you like; anything past sixteen is snapped to the nearest entry, chosen by
median cut over every frame of that subject at once, so a colour that covers a
lot of pixels keeps its own slot.

It used to be ONE palette for the whole directory, filled first-come: walk the
pixels in filename order and take each new colour until sixteen were gone.
That survived flat placeholder art and nothing else. The first real import got
four greens, eleven near-identical greys and black, because brute0.png sorts
first -- the pink creature, the gold one and the shotgun all snapped to grey.
Sixteen colours across five different creatures is three each, which no amount
of clever selection rescues, and the argument for sharing (a common palette
makes a set look like one game) is already paid for here: the art comes from
one game and arrives sharing Doom's palette.

Files are still read in sorted order, so the baked output is stable: a set
that reordered itself between builds would repalette everything every time.

The PNG is never shipped. bake.ps1 converts it to palette-indexed text at
build time, so the game contains no image decoder. It also crops each drawing
to its ink and records where the crop came from, because the packed encoding
pays for empty margin at the same rate as picture: 13% off the imported set,
34% on the frames dense enough to pack, and not one pixel moved.

Cost runs with how busy the drawing is, not its size, because both encodings
are paid per run of same-coloured pixels. The imported Freedoom set measures
2,600 bytes for a shaded 64x96 creature frame and 1,100 for its corpse; the
whole set of 24 is 66KB, about 4.5% of the floppy. See the size report that
build.ps1 prints.
