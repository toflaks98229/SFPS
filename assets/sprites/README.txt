Hand-drawn sprites. Drop a PNG here and rebuild.

NAMING decides where a drawing lands. The name is "<subject><frame>":

    caster0.png       monster "caster", frame 0
    brute2.png        monster "brute", frame 2
    shotgun0.png      the weapon "shotgun", pose 0

Monsters:  water_spirit  brute  caster
Frames:    0 walk-A   1 walk-B   2 attack   3 hurt   4 dead

The list is the bestiary and nothing else. It used to carry `hound` and
`wraith`; both rows left enemy.c, so a `hound0.png` parked here now matches no
subject and is ignored -- which is the same thing that happens to a typo, and
is why deleting the drawings was the tidy-up rather than the fix.

A NAME THAT ENDS IN A LETTER MEANS EVERY FRAME.

    brute2.png   one frame of the brute
    brute.png    all five of them

That is what a half-finished creature needs. A new monster arrives as ONE
drawing -- somebody draws it standing before they draw it walking, attacking
and dying -- and without this rule the only way to see it in the game was to
copy the same file five times under five names. Five identical pictures in the
tree, five copies in the binary, and five files to delete one at a time as the
real frames arrive.

The override falls out of the sort: bake.ps1 emits drawings in name order and
`.` sorts before `0`, so `brute` is always decoded before `brute0`. A
subject-wide drawing lays down every frame; each numbered one painted after it
replaces exactly its own. Adding `brute4.png` later is dropping in a file.

`water_spirit`, `brute` and `caster` are each one drawing today, which is why
they have no digits. Their death frames are not drawn yet -- frame 4 is
currently the same picture as the rest, and a `<name>4.png` will take it.

That is the WHOLE monster set. `hound0.png`..`hound4.png` sat here after the
redraw as the last five imported frames, drawings of a creature the bestiary no
longer had a row for, and they are gone.

Weapons:   shotgun  grenade  rapid  axe   -- named for weapon.c's WEAPONS table
Poses:     shotgun  0 idle   1 pump-A  2 pump-B  3 pump-C   (SHTG A B C D)
           grenade  0 idle   1 firing                       (MISG A B)
           rapid    0 idle   1 spun                         (CHGG A B)
           axe      0 rev-A  1 rev-B   2 cut-A   3 cut-B    (SAWG C D A B)

Those are POSES, not moments, and the counts differ because Doom drew each
weapon with the frames that weapon needed. Which pose shows WHEN is the cycle
table beside each weapon in weapon.c, so the animation can gain steps without
the atlas gaining cells -- the shotgun's pump walks A B C D C B A out of four
drawings, passing back through two of them.

WHICH FRAME IS THE IDLE COMES FROM DOOM'S STATE TABLE, NOT FROM THE ART, and
guessing from the drawings got it wrong twice:

  SHTGA0 is the shotgun's idle (S_SGUN, A_WeaponReady). It looks like little
  more than the end of a barrel, because at rest the gun is mostly below the
  screen edge, and it was dropped as unusable -- which put the first PUMP
  frame on screen as the resting pose.

  SAWG C and D are the chainsaw's idle (S_SAW/S_SAWB) and A and B are its cut
  (S_SAW1/S_SAW2). C and D are the WIDER drawings, which reads as a lunge and
  is not, so idle and attack ended up swapped.

The axe is also the one weapon whose idle is itself an animation: A_WeaponReady
alternates its two frames, because a saw you are holding revs. weapon.c gives
every weapon an idle cycle for that reason; three of them just have one row.

A WEAPON CELL IS A WINDOW ON DOOM'S SCREEN. It spans Doom's full 320-unit
width and 144 rows of its 3D view, so where a drawing sits in the cell is
where it sits on screen.

Which view matters: Doom's screen is 320x200 but its 3D VIEW is 168 rows,
because the status bar takes the bottom 32, and that is the framing the game
shipped with. The weapon is still drawn against the full 200 -- BASEYCENTER is
a fixed 100 whatever the view height is -- so the bar does not merely hide the
bottom of the gun, it MOVES the gun and changes its size against what you can
see. Matching the 200-row fullscreen view instead left the shotgun a fifth too
small with its bottom balanced on the screen edge. That is not a detail -- Doom places each weapon by a per-frame
offset, and those offsets are the artist's decision: the shotgun rests left of
centre, the chaingun sits lower and centred, and the chainsaw's cutting frames
run off the right edge on purpose. Centring each drawing in a tight cell puts
four different weapons in the same place, which is what this did until the
imported shotgun turned up dead centre looking wrong.

Pixels outside the cell are dropped rather than pulled back in, because Doom
clips at the screen edge and the chainsaw is drawn expecting it.

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


A WEAPON DRAWING IS THE VIEWMODEL ONLY, and the floor item is a separate
drawing with its own name. That has not changed; what it points at has.

The floor items used to be generated icons, kept deliberately rather than as a
gap, because the obvious move -- reusing the weapon's viewmodel art -- is
worse: a viewmodel is drawn to be read at arm's length filling the bottom of
the screen, and across a room it is a small dark smear, since the detail that
sells it up close is the first thing the art resolution throws away.

That reasoning was about VIEWMODEL art, and it still holds. Doom's pickups are
not viewmodels. MEDI, SHEL and SHOT are separate drawings made to be
recognised from exactly the distance the generated icons were designed for, so
importing them clears the objection instead of ignoring it. They are named
with an `item` prefix and are what the atlas shows now; pickup_pixel in
src/sprite.c still generates an icon for any kind nobody drew.

Floor items:

    itemhealth        itemredkey    itembluekey   itemyellowkey
    itemammo          itemshotgun   itemgrenade   itemrapid     itemaxe
    itemshotgunammo   itemgrenadeammo   itemrapidammo   itemaxeammo

After the `item` prefix comes the exact name a level uses to place the thing,
so one resolver in pickup.c serves both and there is no second table to drift.
The prefix is load-bearing rather than tidy: without it `shotgun0` would be
both the shotgun's viewmodel and the shotgun lying on the floor.

ONE SCALE ACROSS EVERY FLOOR ITEM, not one per item. The cell is drawn as a
fixed square in world space, so fitting each item to its own cell would make a
box of shells exactly as large as a rocket launcher -- and Doom drew them at 14
and 59 units precisely because they are not the same size. The cost is that
the smallest items are small, which is what they are.


THE WEAPON FACES FORWARD, NOT SIDEWAYS. You are looking down your own sights,
so the weapon points AWAY into the screen: the barrel recedes to a muzzle, the
receiver and stock are below it, and your hands are at the bottom edge. A side
profile is what a weapon looks like in a shop display, not what a held one
looks like.

Let the grip run off the bottom of the cell -- that is what makes it read as
being held rather than as floating. WHERE in the cell is not a centring rule
but a screen position; see the window paragraph above.


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

    ours      Freedoom   why
    imp       POSS       the baseline humanoid
    brute     BOSS       the big one
    caster    HEAD       floats, attacks at range
    shotgun   SHTG       the pump shotgun viewmodel
    grenade   MISG       the launcher: it arcs, so a launcher reads right
    rapid     CHGG       the chaingun, for the fast projectile weapon
    axe       SAWG       the chainsaw: a held melee tool rather than a fist

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
works, and a monster drawing is centred horizontally and sits on the BOTTOM of
its cell. Weapons use a 192x104 cell and are NOT centred -- it is a window on
Doom's screen and the drawing goes where it goes.

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
