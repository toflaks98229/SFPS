# SFPS

A 3D FPS that fits on a 1.44MB floppy disk — **1,474,560 bytes**, single `.exe`,
no asset files.

Inspired by [QUOD](https://daivuk.itch.io/quod), which did the same thing in
64KB. Our budget is 22× that, so the extreme demoscene tricks are optional; the
discipline is not.

## Status

A small but complete FPS loop, start to finish. Win32 window → OpenGL 3.3 core
→ shaders → procedural textures and sprites → sector-based level geometry →
FPS camera with real momentum, a DOOM-Eternal-style meat hook and recoil
jumping → Quake-style shotgun with ammo → four monster types, three melee and
one that shoots → health, pickups, and level transitions that end in a win
screen → a pixelised, luminance-dithered presentation over the whole thing.
Models, materials, sounds and levels are all authored as text and hot-reload
into the running game.

```
112,128 / 1,474,560 bytes   (7.60% used)
```

## Build

There are **two binaries**, and the difference matters:

| | reads assets from | use it for |
|---|---|---|
| `build\game.exe` | the copy baked in at compile time | shipping — this is what the 1.44MB budget measures |
| `build\game_dev.exe` | `assets\` live, reloading on save | authoring — an edit in `modeledit` appears without a rebuild |

**`game.exe` will not show an asset edit until you rebuild.** That is the point
of it: it is self-contained, with nothing beside it to load. While you are
authoring, run `game_dev.exe` — its title bar reads `assets: LIVE assets\`, and
the shipped build reads `assets: baked (rebuild to update)` in a debug build so
the difference is never silent.

```powershell
.\build.bat          # double-click: builds both, launches game_dev.exe
.\build.ps1          # release -> build\game.exe, prints the size report
.\build.ps1 -Debug   # dev     -> build\game_dev.exe, hot reload + HUD
.\build.ps1 -Run     # build and launch
.\build.ps1 -Tools   # also build tools\*.c
.\build.ps1 -Tool modelview   # build and launch one tool
.\size.ps1           # size report only
.\size.ps1 -Detail   # plus per-symbol bytes from the linker map
```

PowerShell's default execution policy blocks `.\build.ps1`. Use `build.bat`, or
`powershell -ExecutionPolicy Bypass -File .\build.ps1`.

Controls: `WASD` move, mouse look, **left mouse fire**, **right mouse meat
hook** (throw it and let go — the pull, the hit and the launch run
themselves), `Shift` sprint, `Space` jump, `F1` toggle the pixelise/dither
pass, `Esc` quit.

## Authoring models and materials

Everything the game draws is described in [assets/](assets/) as plain text.
Both files document their own grammar at the top.

**Edit a weapon and watch it change in the running game:**

```powershell
.\build.ps1 -Debug -Run          # a HOT_RELOAD build
# edit assets\models.txt, save
# the gun updates in place -- real level, real lighting, real fog
```

A model is a list of parts, each a closed 2D outline plus a thickness, which
the engine extrudes and triangulates:

```
m shotgun
uv 300
th 4                                    # half thickness, 1/100 units
p  -86  9   -34  9   -34  2   -86  2    # barrel: side profile, -z is forward
th 7
p  -36 12   8 12   28 5   44 1  ...     # receiver, thicker
```

Each part carries its own `th`. One thickness for a whole gun cannot serve
both a thin barrel and a chunky receiver — the first attempt was 0.18 across
with a 0.12-tall barrel, so the barrel came out wider than it was tall and the
whole weapon read as a slab.

Outlines may be concave and wound either way; winding is normalised and the
caps are ear-clipped. UVs fall out of the extrusion for free: `u` follows the
perimeter along the skirt, and the flat caps use the silhouette coordinates
directly. Nothing has to be unwrapped.

### Materials

A part names its material, and the model is drawn as one run per material:

```
mat blued
th 4
p  -86 9   50 9   50 0   -86 0     # barrel
mat grip
p   10 -10   28 -10   34 -38   16 -38
```

**Material contrast is what makes a weapon read as a weapon** — far more than
detail inside any single texture. One material across the whole gun looks like
a prop no matter how good that material is. A blued barrel next to a machined
steel receiver next to a checkered walnut grip reads instantly.

Grouping by material means several draw calls instead of one. At ~200 vertices
that costs nothing, and it avoids a texture atlas, which would break here: the
UVs tile several times per unit and would sample across atlas cells.

**Gloss rides in the alpha channel.** The textures were already RGBA with
alpha pinned at 255 and unused, so per-pixel specular costs no second texture,
no second sampler and no per-draw uniform. A recipe with no `gloss` op stays
matte, so brick needed no change and wood looks like wood for free. `check`
also drops gloss inside its cut grooves, so one material shines on the raised
diamonds and not between them — which is most of why real checkering reads.

At this polygon count a highlight that travels along an edge as the gun moves
says "metal" more convincingly than any amount of texture detail, so the view
model shader adds Blinn-Phong specular and a small upward-facing wear term,
both scaled by that gloss channel.

### Procedural shader materials

A material can skip pixels entirely and be **computed per fragment from the
UV** instead. One extra line in the recipe switches paths:

```
t pbrick
base 148 72 54          # the colour the shader tints with
proc 1 120              # shader 1 (brick), 1.2 courses per UV unit
gloss 20                # rides in uPParam.x instead of an alpha channel
```

`base` and `gloss` mean the same thing on both paths, so a material moves
between them by adding or deleting the `proc` line — nothing else changes, and
`tex_mat()` returns the same `Mat` struct either way.

Why bother when the pixel path already exists:

- **No resolution.** A brick wall stays sharp with the player's nose against
  it. The 256×256 recipes visibly blur at that distance, and the fix there is
  more pixels, which is the one thing the budget cannot buy.
- **No memory.** Four uniforms instead of a 256KB RGBA texture plus mipmaps.
- **No generation cost.** `tex_make` runs an op list over 65,536 pixels per
  material at startup; a procedural material does no work until it is drawn.

The shaders live in `FS_PROC` in `src/render.c` — value noise plus fbm, then
`pBrick`, `pTile`, `pPanel`, `pWood`, `pHex`, `pMarble`, `pRust` and `pGrid`
behind a `procColour()` dispatcher. The whole set costs about 4.5KB of
`.rdata`, which is roughly two of the textures it replaces.

The id in `proc` must match `PROC_*` in `src/render.h` and the switch in
`procColour()`. There is no way to share an enum with GLSL, so the three are
kept in step by hand and the comment in each says so.

The pixel path is not obsolete. It wins wherever the surface is *authored*
rather than described — the gun's materials stay on it, because bluing that
rubs through on upward faces and checkering cut at a specific angle are
recipes, not formulas.

### Authored meshes

Extrusion and lathe are the default because they generate their own UVs and
cost a fraction of the bytes. When a shape genuinely needs to be modelled and
unwrapped by hand, drop a `.obj` in [assets/](assets/) and reference it:

```
m crate
uv 100
mesh crate
```

A mesh part obeys the same `at`/`rot` placement as any other part, so a
Blender piece can be bolted onto an extruded body.

**The game never reads `.obj`.** `bake.ps1` converts each one into the same
integer text everything else uses, which keeps a float parser out of the
project entirely and shrinks a vertex from ~28 bytes to ~11:

```
crate.obj      1584 -> 469 bytes   (70% saved, 8v 12t)
```

`assets/crate.obj` is a UV test, not art: each of its six faces is mapped to a
different patch of the texture, so a flipped V axis or a crossed index list is
caught instead of hiding behind a plausible-looking cube. OBJ's V axis runs
bottom-up while a texture uploaded with `glTexImage2D` has its first row at
`v = 0`, so the loader flips it — without that, every authored mapping arrives
upside down and nothing about the render looks obviously wrong.

**Source of truth vs. shipped bytes.** `assets/*.txt` are what you edit;
[bake.ps1](bake.ps1) strips their comments and whitespace into
`src/gen_assets.h`, which the release build embeds. Comment the files as
heavily as you like — on the current library the bake is a ~7x reduction, so
documentation costs the binary nothing:

```
Asset          Source  Baked  Saved
models.txt       1371    173    87%
textures.txt     1799    188    90%
```

## Sound

No samples are stored. A sound is a few integers in
[assets/sounds.txt](assets/sounds.txt) saying which oscillators to run and how
their pitch and volume move; the mixer synthesises the waveform as it plays:

```
s shot
l 3 320 1400   60   0 300  90     # noise crack, falling fast
l 0 200  150   40   2 190  55     # low square body
l 1  90  420  110   0  90  35     # short saw bite up front
```

`l <wave> <ms> <f0> <f1> <attack> <decay> <vol>` — wave is 0 square, 1 saw,
2 sine, 3 noise. Layers play together, because real weapon sounds are never a
single oscillator: a shotgun is a noise crack plus a low body, and dropping
either one makes it a toy. The whole library above bakes to **188 bytes**;
the same three sounds as 16-bit PCM would be about 45KB.

The backend is `waveOut` from winmm — already linked, nothing to add. Four
512-frame buffers give ~46ms of latency, refilled by one mixing thread. A
machine with no output device makes `audio_init()` return 0 and the game runs
silent rather than refusing to start.

**Verifying sound you cannot hear.** `build\sndtest.exe -wav` runs the game's
own synth offline and reports whether each recipe actually produced audio:

```
  shot         319 ms  peak 13415  rms  2855  ok
    pitch (zero-crossings/s, 4 windows):    613   513   313   100
```

The falling zero-crossing rate is the frequency sweep — a flat row means the
sweep never happened, which is the failure a peak-level check would miss.
`-wav` also drops `build\snd_<name>.wav` if you do want to listen.

## Effects

A particle effect is a few integers in
[assets/effects.txt](assets/effects.txt) saying how many particles to throw,
how fast, what colour, and how they change as they die. Code spawns one by
name and never mentions any of those numbers:

```c
fx_spawn("spark", hit_point, surface_normal);
```

```
e spark
  count 6              # particles per spawn
  life 220             # milliseconds each one lives
  size 14 2            # edge length in cm, at birth and at death
  rgb 255 216 128
  alpha 90 0           # percent, at birth and at death
  speed 320 260        # cm/s along the normal, +/- spread
  gravity 400          # cm/s^2 downward
  blend add            # `add` glows, `alpha` can be darker than the wall
  face camera          # `camera` billboards, `normal` lies flat -- a decal
```

Every number is an integer, so the parser needs no float handling — the same
rule the model, material, sound and level languages follow. Unknown keywords
are **skipped rather than rejected**, so a file written against a newer build
still loads in an older one instead of failing wholesale.

### Adding one

1. Add an `e <name>` block to `assets/effects.txt`.
2. Call `fx_spawn("<name>", position, normal)` wherever the event happens.
3. Save. In a dev build the running game re-reads the file — no rebuild.

That is the whole procedure. There is no struct to declare, no ring buffer to
size, no ageing loop and no draw call: `fx.c` owns all five and they are
written once. Before this existed each effect was its own copy of those five
pieces in `weapon.c`, which is why there were exactly three of them.

**The normal is what gives an effect its direction.** For a surface hit pass
the surface normal and particles come off the wall; for something bursting in
open air pass any unit vector and let `spread` scatter them. `spread` widens
the *direction* as well as the speed, so a spread near the speed reads as a
burst and a small one as a jet.

**Pick the blend for what the effect has to do, not for how bright it is.**
`add` can only ever lighten, which is correct for sparks, muzzle flash and
bolts — and wrong for blood, smoke or a scorch mark, none of which can exist
if they cannot be darker than what is behind them. This is the one parameter
that cannot be fixed by tuning the others.

### Where effects are allowed to draw

`fx_draw` belongs in the **world pass**, before `post_end` — particles are part
of the scene and must be pixelised and dithered with it. A dev build asserts
this: drawing on the wrong side of the boundary increments `DIAG_PASS_ORDER`
and shows up in the title bar. See [Making silent truncation visible](#making-silent-truncation-visible).

The pool is shared across every effect (`FX_MAX_PARTICLES`, 256). A flood
overwrites the oldest particles rather than refusing the newest — the burst
that just spawned is the one being looked at — and reports `DIAG_FX_CAP` so
the truncation is visible rather than merely silent.

### Verifying an effect you have not seen yet

`fx_spawn` and `fx_update` touch no GL, so the parse and the simulation are
checked headlessly by `build\fxtest.exe`:

```
  parsed 7 effect definitions

  assets\effects.txt supplies at least one effect            ok
  an unknown name spawns nothing and does not crash               6 /      6  ok
  and every particle retires once its life runs out               0 /      0  ok
  a flood never exceeds the particle pool                       256 /    256  ok
  every effect the game spawns by name exists in the file         7 /      7  ok
```

The last line is the one that catches a rename: an effect the code spawns but
the file no longer defines produces nothing at all, and nothing is exactly
what a missing effect looks like in play.

For the look itself, `build\dithershot.exe <level> <effect>` renders the real
level with the effect firing and writes a PNG — the same tool used to compare
dither settings, since an effect has to be judged through the post pass rather
than beside it.

## Levels

A level is a list of **sectors**: a 2D floor plan polygon plus a floor and
ceiling height, with vertical walls. That buys rooms that are not rectangles,
corridors at angles and real height variation — none of which the axis-aligned
boxes this replaced could express.

```
s
floor 45  ceil 600
mat floor steel  wall steel  ceil brick
p  -800 -600   -400 -600   -400 -200   -800 -200
```

Doom tiles the plane with sectors and defines their edges with linedefs, which
is fiddly to author. Here **sectors may overlap and the last one declared
wins** — a platform inside a room is just a small sector laid on top of the
big one, and no polygon has to be cut.

Last-wins rather than highest-floor-wins because the latter makes a **pit
impossible**: the room's floor always beats the lower one dug into it. That
was caught by `leveltest`, not by playing. With file order deciding, a
platform and a pit are the same operation.

Walls are generated per edge: where another sector lies just outside, only the
*difference* in floor and ceiling height is solid. That is what turns a shared
boundary into a step you walk over instead of a wall.

**An edge is not uniform, so it has to be cut.** Overlapping sectors are the
whole authoring model, and one sector may cover only part of another's edge:
beside it the wall is just the step, and past its end the *same edge* faces the
void and is solid floor to ceiling. So `level_edge_spans()` intersects the edge
with every other sector's outline, and each resulting piece answers the
question for itself.

The first version asked once, at the edge's midpoint, and applied the answer to
the whole edge. That was a real reported bug: *"when two objects overlap, the
overlapping face disappears across its entire X extent, and only the overlap
amount disappears in Y."* Exactly right — the length of the wall was decided by
one sample that happened to land in the other sector, while the height was
computed correctly from the step. In the six-sector test map it had silently
deleted an entire diagonal wall, because one platform crossed it.

Cutting makes the build quadratic in sector count, so `leveltest` times a full
64-sector grid of overlapping squares: **0.8 ms per rebuild**, against a 16 ms
frame. The editor rebuilds on every frame of a drag, so that number is the one
that matters, and it is measured rather than assumed.

**Floors and ceilings have to be cut too, or a pit is invisible.** `sector_at`
says the later sector wins, but the renderer was drawing every sector's floor,
so the room's floor was laid flat across the hole. The pit was all there —
walls, floor, collision — and none of it could be seen. The same applies to two
sectors sharing a ceiling height, which z-fought.

So a cap is triangulated and then every later sector is **subtracted** from it.
The subtraction works triangle against triangle, which keeps every clip convex
and makes a half-plane split enough — no general polygon boolean. Concave
sectors come out right because the same ear clipper cuts them into triangles
first.

Two traps, both of which deleted geometry silently:

- *A vertex exactly on the split line belongs to both halves.* Giving it to one
  left the other with two vertices, which was discarded as degenerate — so any
  sector sharing an edge with a later one lost its whole floor.
- *Test bounding boxes per piece, not per piece list.* Testing the list as a
  whole meant every piece was split by every clip triangle even when nowhere
  near it, tripling the count each time until pieces had to be left uncut,
  which put the floor back over the hole.

**Three things about walls that are easy to get wrong, and all look like
"faces are missing":**

*Winding must agree with the normal.* A triangle whose vertex order disagrees
with the normal it carries is culled from **exactly the side it is lit on** —
it does not look mislit, it looks absent. Every wall in the level was wrong
this way at first. `add_wall` now measures the winding it produced and flips
it if it disagrees, rather than deriving the handedness of the xz plane on
paper, which is where it went wrong.

*Which side the face is on depends on the step direction.* The side of a
platform is seen from outside its polygon; a room's wall and the side of a
pit are seen from inside.

*A pit needs the lower-floor case.* Handling only "our floor is higher" covers
platforms and silently drops every pit wall, because the room a pit is cut
into has no edge there at all.

`leveltest` now checks all of it: every triangle's winding against its normal,
and that floors, ceilings and walls are all present.

### Tests must not name the map

`arena` is a map somebody edits. The first versions of `leveltest` and
`movetest` wrote its numbers into their assertions — "six sectors", "the room
floor is at 0", "the ledge is at z=9" — so the suite went red the moment
anyone moved anything. A test that fails for a legitimate edit teaches you to
ignore it, which is worse than not having it.

Both now avoid that, in the two ways that are available:

- `leveltest` **derives** its expectations from the level it loaded — the first
  sector's centroid, the lowest sector as "the pit", the highest as "the ledge
  out of reach". It asserts the rules, not this week's heights.
- `movetest` **builds its own** three-sector fixture, with the platform and the
  ledge either side of `PLAYER_STEP` so the two cases cannot swap places if
  that constant is retuned. `arena` still gets a smoke test — 4000 random
  frames that must not leave the map or sink through a floor — because that
  asks only what must be true of any level.

## Movement

Two rules in [src/player.c](src/player.c) that are easy to get wrong:

**Vertical resolves before horizontal.** With the other order, the frame in
which you drop onto a platform sees your box still overlapping it, and the
horizontal pass shoves you sideways off the ledge before the vertical pass has
had a chance to stand you on top. That was a real reported bug: *"the platform
pushes the player."*

**One axis moves per call, and it stops at contact.** Resolving by penetration
depth picks whichever side is nearer, which can eject you out the far face of
a box you are barely inside. Moving a single axis makes the contact position
exact, and blocking X while leaving Z free is what produces wall sliding.

Step-up is `PLAYER_EYE / 3` — Quake allows 18 units against a 56-unit player,
and below that proportion small trim geometry starts catching your feet.
Contact positions are placed a 2mm `SKIN` clear of the surface: feet landed
*exactly* on a ledge do not survive the round trip through
`pos.y = top + PLAYER_EYE` and back, and the overlap test then re-fires and
cancels every step-up.

`build\movetest.exe` steps the simulation headlessly and checks all of it,
including 4000 randomised frames that must never end inside a box.

## Tools

Tools link the game's own `render.c`, `tex.c`, `model.c` and `weapon.c`, so a
preview is the real thing rather than a reimplementation that drifts. They
never ship, so their size is irrelevant and absent from the budget report.

**`mapedit`** — draw levels.

```powershell
.\build.ps1 -Tool mapedit
.\build\mapedit.exe arena
```

Top half is the plan view you edit in; bottom half flies through the same
level in 3D with the game's own geometry and materials. Doom's editor and
TrenchBroom both settled on this split for the same reason: a floor plan is
where you lay a map out, and a 3D view is where you find out it feels wrong.

| | |
|---|---|
| `W` `A` `S` `D` | fly **along the view** — look up and go up; `SPACE`/`C` are straight up/down, `SHIFT` faster |
| `RMB` | look (3D) or pan (plan) — `F` returns to the player start |
| wheel | raise the surface under the cursor (3D), or zoom (plan) |
| `-` `=` | selected sector's floor down/up (`SHIFT`: ceiling) |
| drag | move the vertex or sector under the cursor |
| `ctrl`+click | insert a vertex on the nearest edge |
| `N` `CTRL+D` `DEL` | new sector, duplicate, delete — `V` deletes a vertex |
| `TAB` | next sector (`shift` for previous) |
| palette | click or drag a swatch onto a surface — `M` cycles |
| `E` `R` `P` | place entity, cycle its kind, move the player start |
| `[` `]` `G` | grid size, snap on/off |
| `CTRL+Z`/`Y` | undo / redo |
| `CTRL+S` | save |

**The keys assume your left hand is on WASD and your right on the mouse**, the
way every 3D editor and every game already works. The first layout put the
camera on the arrow keys and the floor/ceiling heights on `Q`/`A`/`W`/`S`,
which meant reaching across the keyboard to fly.

Heights moved onto the **wheel**: point at a floor and scroll. No key, no
selecting the sector first, no wondering which of three height slots is about
to move. A run of notches coalesces into a single undo step, because raising a
floor by a metre should not cost twenty presses of `CTRL+Z`.

**The 3D view edits too.** Hovering highlights the surface under the cursor;
dragging a floor or ceiling raises it, dragging a wall slides that edge along
its own normal, and `ctrl`+dragging a wall moves the nearer corner instead.
`M` retextures whatever you last clicked, so you point at a thing rather than
remembering which of three material slots it lives in.

**The palette is the material list, drawn with the materials.** Every recipe in
`assets/textures.txt` gets a swatch rendered by the shader or texture it
actually draws with — a procedural material shows its real pattern, not a
colour chip. Drag a swatch onto a surface in the 3D view and it lands there;
the target highlights while the swatch is in flight. Clicking a swatch instead
applies it to whatever was last picked in 3D, and a dot of colour on the name
marks the material already on that surface.

### The inspector, and the fields that had no UI

The panel used to be read-only text. That was not a presentation problem: a
field with no widget had **no way to be edited at all**, so the level format
had grown past what the editor could reach.

Two things were entirely unreachable, and one of them was worse than
unreachable:

- **`hurt`** — the per-sector hazard rate that makes a lava floor lethal. It
  could only be authored by hand-editing `assets/levels.txt`.
- **Point lights** — all eight fields of them. You typed the numbers into the
  text file and ran the game to find out where the lamp landed.

**And saving deleted both.** `level_load` parses them, `mapedit` held them in
memory correctly, and the serialiser wrote neither back — so opening `arena` and
pressing `CTRL+S` removed all four of its lights, and opening `vault` removed
its lava. Nothing failed and the level still loaded, so the only symptom was a
room gone dark. That is the worst shape a bug can have in an editor: the tool
you reach for to make a small change, charging you everything it did not know
about.

`mapedit -verify` now reads a save back and counts what survived, across every
level the file defines rather than one:

```
mapedit -verify: save/reload round trip

  arena         6 sectors   5 entities   4 lights   ok
  vault         3 sectors   6 entities   0 lights   ok

every level survives a save unchanged
```

Verified by reintroducing the omission and watching it report
`FAIL lights: wrote 0, level has 4` — a writer is exactly the kind of code where
what is missing is invisible from the inside, because every line that *is* there
works.

**Lights are drawn in the plan, in their own colour, at their real radius.** The
reach is the field hardest to guess and the one that decides whether a room is
lit or merely has a bright spot in it, so the ring is the part that matters.
Click one to select it, drag it to move it, and the inspector edits the rest.

**The widget layer is hand-written and headless-testable.** `tools/ui.h` is an
immediate-mode GUI — buttons, drag-or-type number fields, text fields, scrolling
lists, collapsible sections — built on the renderer's existing `RD_FLAT`,
`RD_TEXT` and `RD_SWATCH` modes, so it needed nothing new from `src/`.

Immediate mode was chosen for the reason this project keeps choosing things:
there is no widget tree to fall out of step with the data. `ui_drag_short(&s->floor, …)`
reads and writes the live struct, so the panel cannot show a value the level
does not contain.

**Only `ui_end` touches GL.** Every widget above it is arithmetic over a struct,
which is what makes `build\uitest.exe` possible — it feeds synthetic input,
asserts what the widgets returned, and never opens a window. "It is a GUI"
sounded like a reason not to test it and was not one; the suite caught two real
bugs before any panel existed:

- a release-frame reset that cleared `active` *before* the widgets ran, which
  would have made every button in the editor pressable, highlightable and
  incapable of firing;
- a number field that opened seeded with its current value and **appended** what
  you typed, so clicking a ceiling of `450` and typing `300` gave `450300`,
  which the range then clamped to the tallest room the format allows.

Both are the kind of thing that survives review and gets blamed on the mouse.

Swatches use a fifth shader mode, `RD_SWATCH`, which is the material with no
lighting and no fog on it. Reusing `RD_WORLD` would have shown every swatch
fully fogged, because screen-space vertices are thousands of units from the
eye.

Picking calls `level_edge_spans()` — the same function the geometry builder
uses to decide which parts of an edge are solid. A second copy of that rule
would drift, and the symptom would be clicking a wall and grabbing nothing.

A vertical drag intersects the cursor ray with an upright plane through the
grab point and takes the height of that hit. The first attempt used a
ray-versus-line closest approach and got the setup subtly wrong, so nothing
moved at all; the plane version is three lines and hard to get wrong.

Undo keeps whole-level snapshots rather than a command log. A `Level` is a few
kilobytes and edits happen at human speed, so there is nothing to gain from
replayable commands and a great deal to lose in bugs.

**A new sector inherits from whatever it is dropped into** — ceiling, materials
and floor. The first version hardcoded `floor 0, ceil 300`, so a box dropped
into a room 6m tall got a 3m ceiling; the 3m of solid above it was then built
as walls, and the box came with a pillar standing on top. That was reported as
*"placing a box makes a pillar appear above it."*

Its floor lands one grid step proud of the surface below, so a box is visibly a
box instead of being flush with the ground. Dropped in open space it is a new
room instead, and keeps the floor height it inherits.

`mapedit <level> -print` writes what a save would produce and exits — verified
lossless and idempotent. `mapedit <level> -new <x> <z>` drops a sector at those
centimetre coordinates and prints the result: what a new sector inherits
depends on where it lands, and aiming a cursor at a map coordinate from a test
script is fiddly enough that aiming it wrong looks exactly like the code being
wrong.

**`modeledit`** — draw outlines with the mouse instead of typing numbers.

```powershell
.\build.ps1 -Tool modeledit
.\build\modeledit.exe shotgun
```

Left half is the profile you edit, right half is the extruded result, live.

| | |
|---|---|
| drag | move a point, or the muzzle |
| `ctrl`+click | insert a point on the nearest edge |
| `DEL` / `X` | delete point — `shift+DEL` deletes the part |
| `[` `]` | previous / next part — `N` adds one |
| `-` `=` | part thickness — `PgUp`/`PgDn` for one point's own |
| `T` | per-point thickness (taper) on/off |
| `L` | extrude ↔ lathe |
| `,` `.` | uv scale |
| right-drag / wheel | orbit / zoom the preview |
| `CTRL+S` | save |

The cyan crosshair is the **muzzle** — drag it to move where the flash and
tracers come out. It is part of the model, so redrawing the gun carries the
effects along instead of needing a constant in `weapon.c` edited to match.
The hitscan itself still traces from the eye; only the visible tracer starts
at the muzzle, which is what keeps the crosshair honest about what you hit.

Saving rewrites only that model's block inside `assets/models.txt`, so the
documentation header and any other models survive. With the game running from
a `-Debug` build, the change appears in it immediately.

A full 3D modeller would be the wrong tool: the format *is* a 2D profile plus
a thickness, so anything only expressible in 3D could not be saved. Editing
the profile directly is both simpler and complete.

`modeledit <name> -print` writes what a save would produce to stdout and
exits — the parse/serialise round trip is testable with no window and no
mouse involved. It is verified byte-identical and geometry-identical.

**`modelview`** — look at a model, and tune where the view model sits.

```powershell
.\build.ps1 -Tool modelview
.\build\modelview.exe shotgun offx=175 offy=-90 scale=376 yaw=19480
```

`TAB` switches between an orbit turntable and the exact in-game view-model
transform (via the shared `wp_gun_matrix`). `WASD`/`RF`/`QE`/`TG`/`ZX`/`CV`
nudge the pose, `SPACE` toggles idle sway, `ENTER` prints the pose as a C
initialiser ready to paste into `g_gun_pose`. Command-line values are
integers in thousandths, except `yaw`/`pitch` which are millidegrees.

This tool exists because placing the shotgun by editing constants, rebuilding
and squinting at game screenshots failed three times in a row. One look at the
orbit view showed the cause immediately.

## Debugging

`-Debug` defines `DEBUG_HUD`, which puts live player and weapon state in the
title bar — position, yaw, pitch, recoil. It is `#ifdef`'d out of release, so it
costs nothing in the shipped binary.

Use it. Two rendering "bugs" here turned out to be one real logic bug and one
misread screenshot, and the HUD is what told them apart:

- Shots appeared to land on the floor instead of the crosshair. The HUD showed
  `pitch -38.6°` at spawn — the aim was fine, the *camera* was wrong. See the
  focus-warp note in [src/main.c](src/main.c).
- The muzzle flash appeared to never render. It had been rendering the whole
  time; a ~40px effect is invisible in a downscaled 1280×720 screenshot. Crop
  and zoom before concluding an effect is broken.

## Toolchain

[w64devkit](https://github.com/skeeto/w64devkit) (portable gcc 16 + mingw-w64)
extracted into `tools\`. Nothing is installed, no admin rights, no registry —
delete the folder to fully uninstall. It ships `windows.h`, `GL/gl.h`,
`GL/glext.h`, `libopengl32.a` and `libwinmm.a`, which is the complete dependency
list for this project. `build.ps1` prepends `tools\w64devkit\bin` to `PATH` for
its own process only (gcc shells out to `as` and `ld` by name).

**Unity / Unreal / Godot are not options here.** An empty Unity Windows build is
20MB+; the runtime alone is 15× the entire budget.

## Size rules

Rules learned the hard way, each one measured:

**1. Uninitialised globals are not free on this toolchain.** A
`static unsigned char g_tex[256*256*4]` landed in `.data` — `CONTENTS|LOAD` with
a real file offset — and cost its full 256KB on disk despite being all zeros.
Two such arrays were 92% of the first build. Heap-allocate scratch buffers and
free them once the data is in GL. That single change took the binary from
390,656 to 30,208 bytes.

Check with `objdump -h build\game.exe`: a section with `CONTENTS` and a nonzero
`File off` costs disk. `.bss` shows `ALLOC` only and `File off 00000000` — free.

**2. Store the recipe, not the result.** `make_brick_texture()` in
[src/main.c](src/main.c) is ~40 lines of C that generates what would otherwise
be a 256KB image. Per-brick tint from a hash, per-pixel grain, fake bevel from
edge distance. This is the core QUOD technique and it scales to every material
in the game.

**3. Don't store what can be derived.** Mesh vertices carry position and normal
only — no UVs. The fragment shader projects world position onto the plane picked
by the normal's dominant axis. 24 bytes per vertex instead of 32, and texel
density stays consistent for free.

**4. One source of truth per asset.** `g_boxes[]` is the level: 7 boxes, 24
bytes each. The same array feeds both the render mesh and collision. An `inward`
flag flips winding and normals to turn a box into a room.

**5. Link nothing you don't call.** `-ffunction-sections` plus
`-Wl,--gc-sections`, and the GL loader in [src/gl.h](src/gl.h) resolves only the
~26 entry points actually used — no GLEW, no GLAD.

**6. `-fdata-sections` is a trap on this target — measure, don't assume.** It
looks like a companion to `-ffunction-sections`, but it stops zero-filled
statics from reaching `.bss`: each gets its own `.data$name` section and is
written to disk as a run of zeros. Adding it cost **4,096 bytes** here —
55,296 with, 51,200 without — because the data `--gc-sections` reclaims is
worth much less than the `.bss` it forfeits.

```
                      total    .data    .bss
with    -fdata-sections   55,296    4,512     336
without -fdata-sections   51,200      416   3,920
```

This is what `.\size.ps1 -Detail` is for. The per-section report only said the
binary was 55KB; the per-symbol report named `g_keys` (1,024 bytes), `g_impacts`
(1,344) and `g_tracers` (672) sitting in `.data` full of zeros.

### Where the bytes actually are, and why compression is not the answer yet

Measured on the current 145,408-byte build, so the next person asking "can we
compress this?" has the numbers rather than the intuition:

```
  .text     109,888    76%    code
  .rdata     25,752    18%    shader source, baked assets, string literals
  .idata      4,252     3%    the import table
  .data         672    <1%
  everything else     1,520
  .bss       75,696     0%    zeroed at load -- costs nothing on disk
```

**Compression targets the wrong 18%.** The baked assets — every model, material,
sound, level and effect the game ships — are **6,095 bytes**, and `bake.ps1`
already gets a ~7× reduction on them by stripping comments. Running deflate over
what is left yields 2,180 bytes: a **3.9KB saving, 2.7% of the binary**, against
a decompressor that costs code in `.text`, the section that is actually large.

Whole-file packing looks better on paper and is worse in practice:

```
  game.exe        145,408
  deflate          71,296   (49%)
  lzma             62,764   (43%)
```

A packer would save ~80KB — **5.6% of a budget that is 90% empty.** UPX and
kkrunchy both work by prepending a decompressor stub and unpacking at load, so
the cost is a real one (a stub, a slower start, and antivirus heuristics that
treat self-extracting executables as suspicious) paid against a constraint that
is not binding.

**The rule this project already follows beats all of it.** "Store the recipe,
not the result" is not compression, it is *not generating the bytes in the first
place*: `sprite_atlas` is the single largest symbol in the binary at 8,304 bytes
of code, and it stands in for four monsters × five frames of RGBA art that would
be megabytes. The same trade is why there are no `.wav` files, no `.png` files,
and no level data beyond 1,306 bytes of text. Compressing a recipe saves a
fraction of something already tiny.

So the honest answer is that there is nothing to do here **until `.text` is the
problem**, and at 7.4% of the budget it is not. If it ever becomes one, the
order is: cut generated code first (`sprite_atlas`, `mb_extrude_taper` and
`mb_box` are the three largest symbols), and only then reach for a packer. The
roadmap entry says "if the budget ever gets tight" for exactly this reason.

## Monsters

Doom-style: the world is 3D, but a monster is a flat billboard that turns to
face the camera. A polygon monster would cost far more in model data than a
whole animated sheet costs in code here, and the budget is the whole point.

**The sprite is drawn, not stored.** [src/sprite.c](src/sprite.c) builds the
creatures into one RGBA atlas at startup: each body part — legs, torso, arms,
head, horns — is a signed-distance field, the parts are unioned (a `max` of
their distances), and the part with the largest distance at a pixel is the one
whose colour that pixel takes. The union's boundary is a clean silhouette
straight in the alpha channel, with a darkened rim for free. Five frames come
out of the same code with different pose parameters: two for the walk cycle,
one attack, a flinch, and a corpse. Same trade as the textures and the sounds —
keep the recipe, generate the pixels.

### Hand-drawn art, and the weapon that replaces its model

A PNG dropped in [assets/sprites/](assets/sprites/) is baked to palette-indexed
text at build time, so **the game gains an image format without gaining an image
decoder** — the same trade `.obj` already makes for meshes. Shipping the PNG
would mean carrying ~15KB of inflate and filter reconstruction to save 3KB of
pixels.

**A weapon drawing REPLACES the 3D view model; a monster drawing composites
over its generated one.** The difference is deliberate. A half-drawn bestiary
should still show creatures, so a drawing is painted on top of the SDF version
and a monster with no art keeps the generated one. A gun drawn over the extruded
gun would be two guns — so the moment `gun0.png` exists the model stops being
drawn, and deleting the file brings it straight back. Nothing else changes and
there is no flag to keep in agreement with the directory.

**The weapon faces forward, not sideways.** You are looking down your own
sights, so the barrel recedes to a muzzle near the top-centre of the cell and
your hands are at the bottom edge. A side profile is what a weapon looks like in
a shop display, not what a held one looks like.

**The item on the floor is deliberately NOT this sprite.** Drawing the pickup
with the weapon's own viewmodel art is the obvious move — the art exists, it
costs no new pixels, and "the thing on the floor is the thing you pick up"
sounds right. It was tried and it is worse.

A viewmodel is drawn to be seen from one angle, filling the bottom of the
screen, lit as though it were in your hands. On the floor across a room it is a
small dark smear: the silhouette that reads as a weapon at 400 pixels tall reads
as debris at 40, and the detail that sells it up close is the first thing the
art resolution throws away. Four of them at range are four smudges you have to
walk onto to identify.

The generated icons answer what the floor actually asks — *what is that, and do
I want it* — from across a room, because they were designed for that distance
instead of borrowed from another one. Colour carries which weapon, and the
shard-versus-box silhouette carries whether it is the weapon or its ammunition.
Both survive being small. See `pickup_pixel` in [src/sprite.c](src/sprite.c).

**The muzzle is one magenta pixel**, recorded and then left transparent. The
alternative is a constant in `weapon.c` that somebody edits to match the art,
and this project already knows what that costs — placing the shotgun that way
failed three times in a row, which is why `modeledit` puts a draggable muzzle on
the 3D model. The marker is the same idea for a drawing: redraw the gun and the
flash follows it.

A viewmodel sprite needs its own shader mode, and finding out why was the
interesting part. Reusing `RD_SPRITE` produced a **solid white gun**: its
`uColor.a` is a monster's hit-flash, so passing a reasonable-looking alpha of
1.0 asks for a fully white sprite — and its fog is a function of the distance
from the eye to the vertex, which for screen coordinates is not a distance at
all. `RD_SPRITE2D` is the same hard cutout with neither, because a viewmodel is
part of the *frame* rather than a thing standing in the world.

**Every byte of the encoding carries six bits now, not four.** The first version
stored one hex digit per 4-bit palette index and spent another on a run length,
which capped a run at 15 pixels — so on flat-shaded art it was the *cap*
breaking the runs up rather than the picture. Moving both to a 64-character
alphabet that needs no escaping inside a C string literal:

| 128×96 viewmodel | before | after |
|---|---|---|
| flat-shaded | 2,098 B | **946 B** (2.2×) |
| 160×120 flat | 3,224 B | **1,298 B** (2.5×) |
| dithered | 6,272 B | **5,332 B** (1.2×) |

Both encodings are produced and the shorter kept, per sprite, so an artist never
has to think about which. A four-frame weapon costs about 4KB — 0.3% of the
budget — and the shipped binary is 148,992 bytes with the 3D gun, 153,088 with
the drawing.

**The codec had no test while its format was being changed, and that was a
mistake.** The only thing looking at it was `sprdump`, which writes a PPM for a
human to squint at and asserts nothing. A screenshot proved the new format
decoded *something*; it could not catch a run length off by one or a packed
triple leaking into the next sprite.

`build\sprtest.exe` decodes sprite text written by hand and checks it against
pixels computed the same way — and it found a real bug on its first run. **A
packed pair carries three pixels and the loop emitted one per turn, so the last
pair left two pixels held when the data ended and every packed sprite lost its
final one or two pixels.** That is a corner of an image, on art that is usually
transparent at its edges, which is exactly why the screenshot missed it.

The assertion that could not have been written any other way reads `bake.ps1`:

```
  every encoder character decodes to its own index                0 /      0  ok
```

The encoder holds a 64-character string and the decoder *computes* the index
from the character. Nothing can check that the two describe the same alphabet —
one is PowerShell and the other is C — and a mismatch decodes every drawing in
the game to the wrong palette indices, which looks like the art was drawn wrong.
So the test opens the script and compares. Verified by swapping two characters
in `bake.ps1` and watching it report `2 / 0`.

**Four types, and a new one is a table row plus a `_pixel` function** — no new
code path. The atlas is a grid, one row per type, one column per frame:

| | role | reads as |
|---|---|---|
| **imp** | the baseline — dies to one point-blank blast | tall, thin, red, horns, glowing eyes |
| **brute** | a wall of health that hits like a truck, slow | broad grey-green hulk, tusks, back spikes |
| **hound** | fast and frail, punishes standing still | low green beast, all fanged mouth |
| **caster** | ranged — never closes, shoots across the room | violet robe, no legs, cold cyan eyes |

The stats live in one table in [src/enemy.c](src/enemy.c) (`MonType`), so tuning
a monster is editing a row, and [tools/enemytest.c](tools/enemytest.c) asserts
the *roles* hold — the brute really is tougher and slower, the hound really is
faster and frailer — so a careless edit that flattens them gets caught. Each is
also visibly distinct in silhouette; `tools/sprdump.c` writes the whole atlas
to a PPM so the art can be eyeballed without launching the game.

### The ranged type

**`shot_speed` is the only thing that makes a monster ranged.** Above zero and
it launches a bolt instead of swinging; there is no second "is ranged" flag
that could drift out of agreement with the first, and `enemytest` asserts that
the caster is the only type with one.

Everything else falls out of the same state machine the melee types use:

- **Chase means spacing, not closing.** A ranged type walks in when it is
  beyond its firing range, *backs off* when the player crowds it, and plants
  only in the band between. Standing still while you walk into its face reads
  as broken, so backing off is asserted rather than assumed.
- **It needs line of sight to start, and again to release.** Checking only at
  the start lets a caster whose target ducked mid-wind-up shoot through the
  wall; checking only at release wastes the whole telegraph. It checks both,
  and a caster with no angle closes in to find one instead of standing there.
- **The long wind-up is the fight.** That is the window to break line of sight,
  and the bolt is slow enough to sidestep once seen. `enemytest` asserts the
  caster telegraphs longer than an imp for exactly this reason.

Bolts are owned by [src/enemy.c](src/enemy.c), the way `weapon.c` owns its
tracers — they exist only because a monster fired them. They sweep against
walls and the player over the *whole* step rather than testing the endpoint: at
11 m/s a bolt covers ~18 cm a frame, and a point test tunnels through thin
geometry the moment the frame rate dips.

**A bolt is drawn with no texture at all.** It is several camera-facing quads
rotated against each other and blended additively — the muzzle-flash idiom.
One quad reads as a glowing *square*, which is exactly how it first looked; the
overlap is what makes it round. The rotation step has to divide a **quarter**
turn, not a half: a square maps onto itself every 90°, so two quads 90° apart
are one square drawn twice, which came out as a flat diamond until it was
spread over π/2 instead.

That alpha is a **real silhouette mask** (0 or 255), which is why sprites need
their own shader mode. In the world shader alpha means gloss; `RD_SPRITE`
instead does a hard `discard` below 0.5, so the cutout has crisp edges and no
sorting is needed — the depth test and the discard handle overlap between
monsters without drawing them back-to-front.

**The AI has no GL in it**, on purpose — [src/enemy.c](src/enemy.c) is stepped
without a window by [tools/enemytest.c](tools/enemytest.c), the same way
movement is. It checks that a monster spawned from a level entity notices the
player, closes the distance, lands a melee swing, stays inside the map, and
dies in exactly the expected number of pellets — a chase bug is as invisible
from inside the running game as a movement bug was.

A monster is a five-state machine — idle, chase, attack, hurt, dead. It walks
toward the player on the floor (sliding along walls it grazes, using the same
`level_ground` the player does), stops at melee range to wind up and swing, and
the swing lands the instant the wind-up ends — so **stepping back the moment
the telegraph starts** makes it whiff, which is the whole melee game. Shots
trace against monster cylinders as well as walls (`enemy_hitscan`), so a pellet
stops on flesh instead of passing through into the wall, sprays blood, and a
corpse can no longer be hit. The player has health now, drained by swings,
shown bottom-left and tinted green→red as it falls.

Monsters spawn at the level's entities — `imp`, `brute`, `hound` (and legacy
`spawn`) — so you place them in `mapedit` with the entity tool, no code change
to build an encounter.

## Pickups

The `ammo` and `health` entities the editor already places are live:
[src/pickup.c](src/pickup.c) turns each into a bobbing billboard that tops you
up when you walk over it. The sprites are drawn the same way the monsters are —
a shell box and a red-cross medkit, procedural, in the pickup atlas.

Two rules that are worth a headless test rather than trial and error in the
game ([tools/pickuptest.c](tools/pickuptest.c)):

- **Collect only if it helps.** A medkit at full health is left on the floor to
  come back for, not wasted; an ammo box is ignored with a full belt. So the
  collection is gated on need, and both directions are asserted.
- **Everything is capped.** Health stops at 100, ammo at a belt's worth, and
  the overflow is clamped rather than lost or exceeded.

The shotgun now has **ammo** (`WEAPON_MAX_AMMO` in [src/weapon.h](src/weapon.h)):
one shell per trigger pull, an empty click when dry that spends no cooldown —
so the instant a pickup tops you up the next pull fires. The count sits
bottom-right, red at zero.

Collection runs every frame against the player's feet, so a pickup is taken
*in transit* — you grab it walking through, you do not have to stop on it.

## Level transitions

A level names a `next` and drops an `exit` entity; walk onto the exit and the
game loads `next`, **carrying your health and ammo across** — the exit is a
reward you arrive at, the way a Doom episode runs, not a reset. The shipped
`assets/levels.txt` wires `arena → vault`, and **vault has no `next`**, which
is what makes it the end of the game (see below).

The whole thing is a few lines because the pieces were already there:

- `next <name>` is one directive parsed in [src/level.c](src/level.c), and
  `level_exit_at()` — one entity scan, next to the level data so the game and
  the test agree on where the exit is.
- The transition in the frame loop reuses the same geometry-rebuild and
  respawn the hot-reload path already ran; it just loads a different name and
  keeps `health`/`ammo` across the `player_spawn` that would otherwise reset
  them.

Two traps worth knowing:

- **Name aliasing.** `level_load` clears the destination's `name`/`next` before
  parsing, so passing `g_level.next` (or `g_level.name`) straight back into it
  blanks the search string mid-call. The current level name and the exit
  destination are each copied to their own buffer first.
- **An unknown `next` must be survivable.** A typo or a half-authored map leaves
  `level_load` failing; the game then stays put rather than dropping the player
  into a void. `leveltrans` asserts the named target actually loads and has
  geometry, and that the target's exit is clear of its own start (or you would
  transition the instant you arrive).

There is no editor UI for `next` yet, but it round-trips through a save —
[tools/mapedit.c](tools/mapedit.c)'s serialiser emits it, so editing a level
that has one does not silently break the progression.

### Ending the game

**A level with an empty `next` is terminal:** reaching its exit does not try to
load anywhere, it sets a `won` flag on the run state in
[src/main.c](src/main.c) instead. No
new entity kind, no new level directive — the same `exit` that transitions
between levels ends the game the moment the level it sits in has nowhere left
to send the player.

Winning **freezes the world rather than clearing it**: once `won` is set,
`update()`, `enemy_update()` and `pickup_update()` are all skipped, so the last
frame — monsters mid-stride, the gun mid-sway — holds still under a dimmed
overlay showing `YOU WIN`, the final health and ammo, and `ESC to quit`. That
last frame is deliberate: a cleared screen says "the program stopped"; a frozen
one says "you stopped it."

Two things worth knowing if you add a second ending:

- **The crosshair is hidden but the health/ammo HUD is not.** The crosshair
  implies you can still act; the corner numbers are just a record of how the
  run ended, so hiding one and not the other is not an oversight.
- **Checking `next[0]` is enough — no separate "is this terminal" flag.** A
  level either names somewhere to go or it doesn't; adding a second piece of
  state that has to agree with the first is exactly the kind of thing that
  drifts. `leveltrans` asserts `vault.next[0] == 0` directly for this reason.

### What a restart has to put back

A run can end three ways and restart three ways — the menu's `RESTART` row, a
key on the death screen, and a click on it — and all three have to clear
**exactly** the same state. A death screen that restarted without clearing the
win latch, or a menu restart that left the player dead, is a separate
half-working path rather than a bug in a shared one.

That agreement used to be a list. The restart assigned three fields by name,
and the run owned eleven:

```c
run_reset(&g_run, 0);      /* the whole of it */
```

`RunState` in [src/run.h](src/run.h) holds every one of them and `run_reset`
assigns a zeroed struct, so **a field added to the struct is reset without
anybody editing the reset.** That is the entire point: the two fields the old
list did not name were the title clock and the world clock, plus three lava
timers that were function-local `static`s buried in the frame loop — somewhere
a restart could not see them even in principle.

Nothing visible was wrong. The accumulator clears itself on dry ground and the
rest are sub-second timers, so the bug was real and harmless at the same time,
which is why it survived. It stops being harmless the first time somebody adds
a field that is not a timer.

`title` is the one field not simply zeroed: a restart goes straight back into
the run, because the player has already asked to play. Startup passes 1 and
gets the title screen. One function, one difference, both entry points.

**It is split out of `main.c` so it can be tested.** Everything else here that
holds rules worth checking is reachable from a headless tool; the run's own
state machine was the last piece that was not, because it lived beside
`WinMain` and a tool brings its own entry point. `build\runtest.exe` now checks
it, and the assertion that actually holds the design to its promise compares
raw bytes rather than fields:

```
  two differently-dirtied runs reset to the identical state  ok
```

A field-by-field test only covers the fields somebody remembered to list, and
forgetting one is precisely the failure the struct exists to prevent. Verified
by reintroducing the old field-by-field reset and watching six assertions fail,
rather than assumed.

## Momentum: the grapple hook and recoil jumping

Two systems that only exist because the player finally has real momentum.
Until now, walking set position directly every frame — `wish * speed * dt`,
gone the instant the key is released — and only the vertical axis had a
velocity that persisted between frames (gravity, the jump). That is enough for
a level FPS and nothing more: nothing can push you, and nothing you do can
carry you anywhere.

**[src/player.h](src/player.h) turns `Player.vel_y` into `Player.vel`, a full
3D velocity — but only for forces from *outside* normal walking.** WASD still
sets position directly, unchanged; `vel.x/z` is momentum layered on top,
touched only by `player_impulse()` (a grapple's pull, a shotgun's kick). This
is additive by construction: nothing feeds `vel.x/z` under ordinary walking,
so plain movement is bit-for-bit what it was before, and `movetest` never had
to change.

**All of the tuning for both systems lives in two clearly bannered blocks**
— `MOVEMENT-FEEL TUNING` in [src/weapon.h](src/weapon.h) (hook range, the rope
constraint's stiffness/slack/restitution, reel speed and rope limits, swing
drag, air control, the release boost, the hook's muzzle offset, the tether's
width and tiling, the recoil kick), and
`MOMENTUM TUNING` in [src/player.h](src/player.h) (how fast that momentum
bleeds off afterward). Each banner points at the other, because retuning "how
hard the hook pulls" without also knowing "how fast that pull decays" is
retuning half the feel blind.

Ground drag is hard — `MOMENTUM_DRAG_GROUND = 6.0` — so recoil does not leave
you sliding across the floor in ordinary combat. Air drag is deliberately
close to nothing — `0.06`, down from an original `0.35` — because the entire
point of the hook and recoil jumping is a fast move that keeps going without a
fight: `hooktest` now asserts a kick keeps **over 90%** of its speed a full
half-second later, not just "more than 5%," specifically so a quiet return to
"kicks that politely die" would fail the suite instead of slipping through a
bound loose enough to hide it. Gravity is not part of this dial — it is a real
force, not friction, and still pulls at every moment, including mid-swing,
which is what makes a grapple arc rather than a straight line.

### The hook: a DOOM Eternal Meat Hook

Four beats, and the design is mostly a matter of keeping them distinct:

| | |
|---|---|
| **1. Fire** | the claw leaves the launcher and *flies* — a projectile with travel time, not a raycast |
| **2. Pull** | on a hit, the player is reeled in under their own momentum |
| **3. Impact** | arriving deals damage, so hooking a demon is an attack rather than just travel |
| **4. Launch** | the player bounces off automatically — no button, no timing |

**It replaced a rope-constraint hook, and that was a mechanic swap rather than
a tuning change.** The previous version held a *length* and let gravity turn a
fall into an arc — the Spider-Man 2 / Energy Hook model, documented at length
in the git history of this file. A rope's entire point is that it does **not**
close distance. A Meat Hook's entire point is that it does. So the winch that
was wrong for swinging is exactly right here, and the arc this version
produces comes from the launch at the end instead of from the tether.

The old test suite went with it. "The rope holds its length" and "gravity
becomes an arc" are not weaker versions of the assertions in `hooktest.c`
now — they are assertions about a different mechanic, so they were deleted
rather than adapted.

**The claw is a real projectile.** `wp_hook_fire()` takes no level at all,
because nothing is resolved when it returns; `wp_hook_update()` steps the claw
forward each frame and tests what it hits. That flight is sub-stepped in
`HOOK_FLY_STEP` increments rather than one jump per frame: at 90 m/s a single
60 Hz step is 1.5 m, which is wide enough to pass straight through a demon or
a thin wall. Monsters are tested *before* geometry over the same sub-step, so
a demon standing against a wall is the target rather than the wall behind it —
getting that priority backwards would make the hook feel broken near cover.

**Gravity is cancelled during the pull, but not during the flight.** A long
horizontal hook would otherwise sag into the floor before arriving, which
reads as the hook failing rather than as physics. The flight is deliberately
left unsupported — the claw has not caught anything yet, so there is nothing
to hold you up.

**The launch overwrites velocity rather than adding to it.** On arrival the
player is moving straight at whatever they just hit; keeping that would drive
them into the target they are supposed to be bouncing off. `HOOK_LAUNCH_UP`
clears the target so the next hook has somewhere to go, and
`HOOK_LAUNCH_ALONG` preserves a fraction of the approach so a chain of hooks
keeps its momentum instead of stopping dead above each one.

**A hook that hits a demon tracks it by index, not by position.** Monsters
move, so the pull has to follow, and the impact needs to know who to hit.
Tracking the index is also what makes "the target died mid-pull" detectable at
all: the hook ends with no launch, because there is nothing left to bounce off.

#### What the tests caught

**The pull overshot and orbited its target forever.** A proximity-only arrival
test misses whenever one frame's travel exceeds the arrival radius — at
`HOOK_PULL_MAX` the player covers 0.63 m per frame, and a straight 20 m hook
overshot to 29 m and never got nearer than **2.07 m** against a 1.6 m
threshold. It then oscillated indefinitely, because the pull kept reversing to
chase the target it had just flown past. Arrival is now also "the target is
behind me".

**The first version of that fix ended every hook instantly.** Testing whether
the *whole* velocity pointed away from the target read the tiny upward drift
from gravity cancellation, on the very first pull frame, as a fly-past — hooks
completed in 15 frames having moved nobody. The test now asks only whether the
velocity component **along the hook** has reversed, and only after the player
has built real inbound speed.

**One assertion was simply wrong.** "A horizontal pull barely sags" failed at
2.34 m against a flat 1.5 m bound — but 0.56 m of that is the unsupported
flight, and the rest is the pull arresting the fall it inherited. The code was
right; the number in the test was invented. It now compares against free fall
over the same duration, which is the property that was actually meant.

### Recoil jumping

Every shot kicks the player back along `-aim`, harder in the air than on the
ground (`RECOIL_MOVE_AIR` vs `RECOIL_MOVE_GROUND`, both in weapon.h's tuning
block) — enough on the ground to be felt, not enough to shove you around by
surprise mid-fight. Because the kick is just `-fwd`, whatever vertical
component the aim already has comes along for free: aim down and fire, and you
launch up-and-back — the classic rocket-jump trick, with no separate case for
it anywhere.

The kick lands inside `fire()` in weapon.c, the same place `recoil`/`punch`/
`flash` already get set, by adding directly to a `v3 *player_vel` the caller
passes in — the same pattern the hook uses, so a shot and a grapple pull are
two things doing the same kind of push rather than two different mechanisms
that both happen to move the player.

### What visual testing caught here

**A ground-level pull got stuck 2m into a 26m swing**, and the reason was
correct, boring physics: the arena's start point sits inside a sunken pit, and
its own 70cm lip is taller than `PLAYER_STEP` — the pull was pushing straight
into a real wall the whole time. Jumping crossed it immediately, because a
jump's rising arc temporarily lifts the feet above the ledge, and the existing
step-height check ­(rightly) reads that as no obstacle at all. Not a hook bug;
a reminder that a momentum system inherits every collision quirk of the
ground it runs on.

**A pitch aimed via raw mouse-pixel math span wildly out of control** — the
same overshoot this project's screenshot scripts had already hit once before
with a full 180° turn. Switching the verification to keyboard-only movement
(hold `W`+`Space` to clear the pit, then fire level) sidestepped the
conversion entirely rather than re-deriving it under time pressure.

**The ribbon's UV axes were transposed once**, which is not something a
screenshot catches at a glance — a rope a few pixels wide looks the same
whichever way its bands run until you know to look for the twist. That is
exactly why `hooktest` reads the UVs back out of the vertices directly rather
than trusting a render: a wrong axis fails the assertion instantly, where a
screenshot would need a trained eye and a zoom.

### What the rope constraint's tests caught

**A one-line "take up the slack" clamp silently disabled the entire swing.**
Shortening the rope to the current distance whenever it is shorter looks
obviously equivalent to taking up slack, and is not: a player hanging at
exactly rope length sits a float's-breadth *inside* it, so the clamp fired
every frame and the length tracked the player instead of constraining them.
The taut test downstream then never fired, and the position correction froze
the player mid-air. It needed a margin (`HOOK_ROPE_SLACK`) to tell "you have
moved inward, take up the slack" apart from "you are on the constraint, leave
it alone." A swing that does not swing is exactly the kind of thing that reads
as "the physics need tuning" for an hour before it reads as a bug.

**Snapping the position hard onto the sphere stalled the pendulum too.**
Correcting the full length error in one frame fights the integrator for the
same position and drains the arc; easing most of the way there removes the
drift and leaves the momentum alone. Hence `HOOK_ROPE_STIFFNESS` at 0.5 rather
than a bare assignment.

**Nine failing assertions blamed the physics when the fixture was wrong.**
The first swing tests hung the player at `y=40` in a room whose ceiling is at
30m — outside the geometry, where `level_trace` hits instantly at zero range.
Every "swing" was a 2m stub of rope, and the numbers did not budge across two
rounds of real fixes to the constraint code. The tell was that they did not
budge *at all* — byte-identical output after an edit that provably changed the
code path means the path is not the one running. The room-sized comment on the
fixture in `hooktest.c` is there so the next person does not spend that hour.

## Making silent truncation visible

Several subsystems here have fixed capacities and drop the surplus rather than
failing. A `MeshBuf` stops appending vertices, a mesh parser stops storing
points, a level stops spawning monsters. **That is the right behaviour for a
size-bound game** — refusing to draw is worse than drawing slightly less — but
it is invisible, and invisible truncation has already cost this project real
debugging time. `LVL_MAX_RANGES` was sized for a weapon's handful of parts, a
level reached six materials, and the surplus walls simply stopped being drawn.
It read as a hole in the geometry and took a headless check to find.

**[src/diag.h](src/diag.h) is the counter to that.** A subsystem reports an
overflow, the count accumulates, and the debug HUD shows it — prefixed with
`!` and placed at the *front* of the title bar, because a truncation is the
one thing up there that means something is actually wrong, and the tail of a
long title is the first thing Windows elides:

```
! DROPPED vtx=177 | SFPS 60fps | assets: LIVE assets\ | pos 0,50,1200 cm | …
```

Nothing changes about the truncation itself. The game still degrades
gracefully; it just stops doing so in silence.

**It costs zero bytes in release, and that is verified rather than assumed.**
`DIAG()` expands to `((void)0)` and the whole of `diag.c` compiles away, so
report sites can sit inside per-vertex loops without a second thought. The
linker map confirms it — `diag.o` contributes `0x0` to `.text`, `.data` and
`.bss`, and no `diag_*` symbol appears in the shipped binary, which is
unchanged at 108,032 bytes.

Two report sites needed a small restructure rather than a one-line addition.
`enemy_spawn_level` and `pickup_spawn_level` had the cap in the loop
*condition*, so they stopped iterating once full — the same amount of spawning
either way, but the count of what was skipped was lost, and "the level is
missing monsters" is otherwise indistinguishable from "the level was authored
that way." The cap check moved into the body so the loop still sees every
entity.

`tools/diagtest.c` checks the counters, but the assertion that matters drives
a **real `MeshBuf` past its capacity through the real `mb_vtx`** and asserts
the count moves. A counter nobody increments is worse than no counter at all —
it reads as "no problem" forever — so testing the module in isolation would
have proved nothing about whether it was wired up.

## Keeping the GL stack out of the simulation

`level.h` is the root of the simulation half — `player.h`, `enemy.h`,
`pickup.h` and `weapon.h` all include it. It used to include `render.h`,
which includes `gl.h`, which includes `windows.h` and the whole OpenGL API.
So every headless movement, AI and pickup test depended on the GUI stack it
exists precisely to avoid, and touching `render.h` forced a rebuild of
everything.

The entire dependency existed for **two pointer types**. `MeshBuf` and
`MdlRange` appear in exactly one declaration:

```
int level_geometry(MeshBuf *b, const Level *l, MdlRange *ranges, int max_ranges);
```

A pointer needs only a forward declaration, so that is what `level.h` uses
now — plus `m.h` for `v3`. The two structs were anonymous typedefs
(`typedef struct { … } MeshBuf;`), which C cannot forward-declare, so both
gained a struct tag; existing uses are unchanged.

**The measured effect:**

| Header | Before | After |
|---|---|---|
| `player.h` | ~78,000 preprocessed lines | **929** |
| `enemy.h` | ~78,000 | **1,038** |
| `pickup.h` | ~78,000 | **937** |
| `level.h` | ~78,000 | **910** |

Two `.c` files and four tools now state a dependency they were getting
transitively — `level.c` and `main.c` include `model.h`/`render.h`
explicitly, and the geometry-building tools do the same. That is the fix
working as intended: the dependency did not appear, it became *visible*.

**A build-time guard keeps it that way.** One `#include "render.h"` added to
`level.h` for convenience would restore the whole chain silently, because
everything still compiles. `tools/leveltrans.c` — which includes `level.h`
and nothing else — checks for the sentinels those headers define and fails
the build instead. Verified by reintroducing the violation and watching it
fire, rather than assumed.

## Pixelisation and luminance-driven dithering

The world is drawn into a 320×180 offscreen buffer and blitted to the window
through one full-screen triangle. `F1` toggles it.

**Pixelisation is not a filter.** Rendering small and magnifying with
`GL_NEAREST` *is* the look — no shader is involved, and switching those two
`glTexParameteri` calls to `GL_LINEAR` turns it off while leaving the dither
on.

**But blocky is not the same as pixelised, and the first version only had the
first half.** Rendering small does make the image blocky, yet the rasteriser
still produces each art pixel from a *single* sample at its centre: a wall
edge, a distant railing or a thin sliver either lands on that centre and
appears at full strength or misses it and vanishes. Nothing in between, and
nothing combining a pixel with what surrounds it. Real pixel art averages the
area a pixel covers — that is where an edge's intermediate tones come from.

So the world is rendered at `POST_SUPERSAMPLE` times the art resolution and
each art pixel is resolved as the **mean of its block**, in linear light. An
edge crossing a quarter of the block contributes a quarter of its colour. The
image stays exactly as blocky — the art resolution has not changed — but each
block is now a considered average instead of a lucky sample.

`tools/posttest.c` checks this by painting a checkerboard of alternating
black/white *sub*-texels and looking at what comes out: averaging turns each
block mid grey, point sampling returns pure black and white. Verified in both
directions — at `POST_SUPERSAMPLE 2` the centre is 1594 intermediate pixels to
710 extreme, and at `1` it inverts to 258 against 2046.

**The dither is where the design actually is.** Quantising each channel
against a Bayer threshold gives a correct ordered dither and a flat,
screen-door look: the pattern is the same density everywhere regardless of
what the surface is doing. Driving the threshold by **luminance** instead is
what makes it read as shading —

```
bias = (0.5 - lum) * LUMA_DRIVE
```

— so a dark pixel sits near the bottom of its quantisation step and more of
the matrix clears it (the pattern opens), while a bright pixel sits near the
top (the pattern closes). Dots thin out in light and crowd in shadow, which is
how hand-drawn stipple works and why the *Who's Lila?* / *Obra Dinn* look reads
as drawn rather than as filtered.

### Getting it wrong first, and what the pixels said

The first version looked bad, and measuring the frame said why: sampling a
real screenshot, the **dark sky had four distinct colours** and topped out at
luminance 85 while the lit floor got twelve. Four fixes, in the order they
mattered:

**Two colour spaces, each for what it is correct for.** The obvious reading of
the gamma advice — "dither in linear light" — is *wrong here*, and doing it
made the image markedly worse. That advice assumes many output levels. With
only four, linear space packs almost all its resolution into the top of the
range: four evenly spaced linear steps land at roughly 0%, 62%, 82% and 100%
perceptual brightness, so everything below mid grey collapses onto one step.
Simulated across the ramp, **every source value from 0.05 to 0.50 produced the
same two outputs.** So quantisation happens in *gamma* space, where four steps
are four evenly perceived tones — while the **luminance** driving the bias is
computed in *linear* light, because the Rec. 709 weights are defined against
physical intensity. Two spaces, two jobs.

**Per-channel quantisation breaks the hue.** A near-neutral grey like
`(0.22, 0.23, 0.26)` has its channels either side of the same threshold, so at
some matrix cells it resolves to `(0, 0, 85)` — a pure blue dot in what should
be grey. Across a wall that is coloured confetti. Quantising the *luminance*
once and rescaling the colour to it means one decision per pixel, so the
channels cannot disagree; `SATURATION` mixes back toward the per-channel
result.

**An 8×8 matrix.** Sixty-four thresholds instead of sixteen — four times the
intermediate tones before the tile repeats, still O(1) per pixel with no error
buffer.

**The dither cell is decoupled from the render pixel.** `DITHER_SCALE` samples
the matrix on a coarser grid than the framebuffer, so the render resolution
can rise (the world gets sharper) while the stipple keeps its apparent size.
That is the dial for "less pixelated dithering"; at 1.0 the pattern is as
dense as the pixels and reads as static rather than as a pattern.

**Anisotropic filtering matters more here than in an ordinary renderer.** A
floor at a grazing angle is compressed hugely along the view direction and
barely across it; isotropic mipmapping has to pick one LOD, so it aliases. And
because the dither is luminance-driven, pixel-to-pixel luminance noise becomes
pixel-to-pixel *pattern* noise — the chaotic band across the mid-distance
floor. **The dither can only be as clean as the image it quantises.**

What was deliberately **not** done: no temporal dithering. Animating the matrix
over frames is the standard trick for more apparent levels, and it makes a
still image better and a moving one worse — at this resolution the pattern
crawls, and this game is almost never still.

No second lighting calculation happens in the pass. The luminance comes from
the world shader's existing output, so the dither tracks the fog, the specular
highlights and the muzzle flash for free.

**`LEVELS` is 4, not 2.** A true 1-bit look would throw away the
blued/steel/walnut contrast the gun depends on — and that contrast is
documented above as most of what makes a weapon read as a weapon.

**The UI is deliberately outside the pass.** `post_end()` is placed after the
view model and before the crosshair, so the gun is pixelised with the world it
is lit by, while 5×7 glyphs stay at native resolution. Magnified 4× they are
unreadable, and dithered text is worse.

### Testing a shader the C compiler never sees

The GLSL is a C string, so a syntax error in it is invisible at build time:
`post_init` just returns 0 and the game renders without the effect, which
looks like "the feature didn't work" rather than like a bug with a cause.

`tools/posttest.c` is the only test here that needs a GL context. It creates a
hidden window, completes the FBO, runs a full frame through the pass and
checks `glGetError`. Verified by replacing `dot(...)` with a nonexistent GLSL
function and watching four assertions fail — the same reverting check the
audio work used, and this time the test genuinely catches its target.

## PSX-era rendering: what to add, and what not to

The pixelisation and dithering above already do half of this look. What is
missing is everything that came from the PlayStation's *geometry* pipeline
rather than its framebuffer, and those artefacts are the ones people actually
recognise: the wobbling vertices, the warping textures, the polygons that pop
through each other.

This section is the survey and the plan. It is written before the work so the
order is a decision rather than an accident, because these techniques interact:
vertex snapping and light noise both change what the dither has to quantise.

Two of the three geometry artefacts are **deliberately not implemented** —
affine texture mapping and depth-sort popping. Both are recognisable, both are
cheap to switch on, and both are wrong for a project whose geometry is authored
as whole sectors rather than as PSX-sized triangles. The reasoning is in
[What NOT to do](#what-not-to-do); it belongs there rather than as a footnote,
because "we could have and chose not to" is the part that gets forgotten and
re-litigated.

### What the hardware actually did, and why

Every artefact below is a *consequence* of a specific hardware limit, not a
stylistic choice anyone made. Reproducing the limit reproduces the look;
reproducing the look directly tends to overshoot into parody.

| Artefact | Cause |
|---|---|
| Vertices wobble and jitter | The GTE transformed vertices with 16-bit fixed-point maths and produced integer screen coordinates. There was no subpixel precision, so a vertex snapped to the pixel grid and *changed which pixel* as the camera moved. |
| Textures swim and warp | No perspective-correct interpolation. UVs were interpolated linearly across a triangle in screen space, so a large polygon seen at an angle stretched its texture wrongly — worst on floors and long walls. |
| Polygons cut through each other | No depth buffer in the usual sense. Sorting was per-primitive into an ordering table, so two polygons at similar depth could swap order between frames. |
| Colour banding and dither | 15-bit framebuffer (32 levels per channel) with a hardware dither to hide the steps. |
| Everything is dark and fogged | Limited fill rate meant short draw distances, and fog was how the far plane was hidden. |
| No texture filtering | Point sampling only. Combined with low-resolution textures this is why surfaces look chunky rather than blurry. |

### What this project already has

Two of the six, and both are the framebuffer half:

- **Low resolution and colour quantisation** — `POST_HEIGHT` renders small and
  `LEVELS = 4` in the resolve shader quantises harder than the PSX's 15-bit
  buffer did. See [Pixelisation and luminance-driven dithering](#pixelisation-and-luminance-driven-dithering).
- **Distance fog** — already in the world shader, already hiding the far plane.

Everything in the table above that comes from the geometry pipeline is absent.
`VS_SRC` is four lines and does nothing but transform.

### The plan, in the order it should be done

Ordered by *how much look per unit of risk*, not by how interesting each one is.
Each step is small enough to be judged on its own with `dithershot` before the
next is started.

**1. Vertex snapping — DONE.** The single most recognisable artefact and the
cheapest to add. `rd_snap` sets the grid, `VS_SRC` does the quantisation, and
`PSX_SNAP_COARSE` in main.c is the one number to change when tuning it.

Snap in clip space, not world space. The wobble has to be relative to the
*screen* grid, because that is what the hardware's integer coordinates were;
snapping world positions moves geometry in ways that depend on where the level
was authored rather than on where the camera is.

```glsl
vec4 p = uMVP * vec4(aPos, 1.0);
if (uSnap.x > 0.0 && p.w > 0.0) {      // w <= 0 is behind the eye
  vec2 ndc = p.xy / p.w;
  p.xy = floor(ndc * uSnap + 0.5) / uSnap * p.w;
}
```

The `/p.w` and `*p.w` are not decoration: clip space is pre-divide, so the
snap has to happen in NDC and then be put back. Snapping `p.xy` directly
quantises by an amount that scales with distance, so the far geometry stops
snapping at all — simulated on a 640-wide grid, a point at `w=1` moves by
5.0e-04 either way, but at `w=60` the direct version moves it by 5.2e-06 while
the correct one still moves it the full 5.0e-04. The wobble would fade out
with distance, which is the opposite of the artefact.

The grid comes from `post_size`, added for this. A *coarser* grid than the
render resolution is the dial for "more PSX": matching the buffer exactly is
the honest amount and is almost invisible at 640x360, because the artefact is a
whole-pixel jump and those pixels are half the angular size the PSX's were.
`PSX_SNAP_COARSE` is 2.0, which halves the grid.

Simulating the shader maths directly — a vertex creeping sideways by a quarter
pixel per step, over three pixels of travel:

| | positions the vertex took |
|---|---|
| snap off | 12 (continuous) |
| coarse 1.0 | 3 |
| coarse 2.0 | 1 |

That hold-then-jump is the wobble. Note what it means for measurement: the
artefact is *the absence of movement*, so a still screenshot shows nothing at
all and only the game running shows it.

**Do not snap the view model.** It is drawn in gun space at a fixed distance,
so snapping makes it vibrate constantly in the centre of the screen where the
eye is least forgiving. The mode uniform already separates it.

**2. Light noise on the illumination — DONE.** This is where the current
banded lighting meets the PSX look. The bands from `LIGHT_BANDS` are clean
steps with hard edges; the hardware's were noisy because the dither ran on the
final colour at 15-bit, breaking every boundary up.

The value noise already in `FS_PROC` (`n2`, `fbm`) is the tool. Perturbing the
*illumination* before it bands — rather than adding noise to the final colour —
keeps the noise attached to the surface instead of crawling across the screen
as the camera moves:

```glsl
lum += (n2(vPos.xz * NOISE_SCALE) - 0.5) * NOISE_AMOUNT;
lum = floor(lum * (LIGHT_BANDS-1.0) + 0.5) / (LIGHT_BANDS-1.0);
```

Noise applied *before* the quantisation dissolves the band edge into a
stippled boundary; applied after, it just adds grain on top of clean steps and
reads as film grain rather than as dithered light.

Sampled from `vPos.xz` — world space, so the pattern is attached to the
surface. Screen-space noise crawls as the camera moves, which is the one thing
that reliably reads as post-processing rather than as the room.

**The amplitude is not small.** A band is `1/(LIGHT_BANDS-1)` = 0.25 wide and
`NOISE_AMOUNT` is 0.20, which perturbs by up to 40% of a band. The first
attempt used 0.055 on the reasoning that a tenth of a band would stipple an
edge, and measured against a zero-noise reference frame it changed *nothing* —
almost every surface sits near the middle of its band rather than near an edge,
so a small perturbation moves nothing across a boundary:

| `NOISE_AMOUNT` | pixels changed vs. no noise |
|---|---|
| 0.055 | 0.00% |
| 0.10 | 0.05% |
| 0.20 | 7.2% |
| 0.35 | 16.1% |

Past about half a band the bands stop being bands, so 0.20 is the useful end of
that range rather than the middle of it.

**3. Nearest-neighbour texture sampling.** `tex.c` and `sprite.c` set
`GL_LINEAR` and 8x anisotropic filtering. The PSX had neither.

This one is listed last and marked as **a judgement call rather than a
recommendation**, because the anisotropic filtering is there for a stated
reason: the note in `tex_make` records that grazing-angle floors aliased
badly, that the aliasing fed the luminance-driven dither, and that the result
was a chaotic band across the mid-distance floor. Turning filtering off will
bring that back.

The honest options are to accept the aliasing as part of the look, or to keep
filtering and lose one artefact. Do not decide this from the table above —
decide it from a screenshot of the actual floor.

### What NOT to do

**Do not add affine texture mapping.** `noperspective` on the UV varying is one
keyword and it is the second most recognisable artefact on the list, which is
exactly why it needs a reason rather than a shrug.

The warp is proportional to how much depth changes across a polygon, and this
project's walls are whole sector edges — single large quads, not the small
triangles a PSX-era model was cut into. Measured as the UV error at a quad's
midpoint: a 2m-to-3m wall is off by 0.10, a 2m-to-8m one by 0.30, and a
sector-sized 2m-to-40m floor by 0.45 against a theoretical maximum of 0.5. The
arena's sectors are 35m across, so every floor and every long wall would sit at
the top of that range.

That is not the PSX look, it is a broken one. The hardware warped because its
polygons were small and its artists worked around it; a quad forty metres deep
warps until the texture is unreadable. Making it work would mean subdividing
level geometry into PSX-sized triangles — which is a change to
`level_geometry`, to the vertex budget, and to `MeshBuf` sizing, for one
artefact. The geometry here is authored as sectors on purpose, and that
decision is worth more than the warp.

**Do not add depth sorting artefacts.** Polygons cutting through each other is
a genuine PSX artefact and it is also just a bug that shipped. Reproducing it
means giving up the depth buffer, and everything in this project — the
particles, the sprites, the view model over a cleared depth buffer — assumes
depth works. Sixteen call sites across five files set `GL_DEPTH_TEST`,
`glDepthMask` or clear the buffer, and `wp_draw_view` in particular draws the
gun over a *deliberately cleared* depth buffer so it never clips a wall. The
cost is rewriting all of that, and the gain is an artefact most players
remember as "the graphics were broken".

**Do not add per-vertex Gouraud colour to imitate PSX lighting.** It would mean
a fourth vertex attribute on every vertex in the project, and the banded
per-pixel lighting already produces flatter, more era-appropriate shading than
smooth Gouraud would. The `Vtx` struct is 32 bytes and every mesh is generated
at startup; growing it costs RAM on every model to reproduce something the
current lighting does better.

**Do not lower `POST_HEIGHT` to 240 to "match" the PSX.** The pixel size is
already a deliberate choice with its own note about integer scaling and pixel
creep. Vertex snapping and banded, noisy light will read as PSX at the current
resolution; making the pixels bigger is a separate decision about legibility,
not part of this work.

### How each step gets judged

`dithershot` renders the real level through the real pass and writes a PNG, and
it takes a level name so a comparison is two commands. Each step above should
be looked at before the next is started, because they compound: light noise on
top of snapped vertices is a different picture from either alone, and the
nearest-neighbour question in step 3 can only be answered against whatever the
first two have already done to the floor.

The one that cannot be judged from a still image is the vertex snapping — the
wobble only exists when the camera moves. That needs the game running, and it
is the reason the snap grid is a uniform rather than a compile-time constant.

## Layout

| File | Role |
|---|---|
| [assets/models.txt](assets/models.txt) | weapon and prop silhouettes — **edit this** |
| [assets/textures.txt](assets/textures.txt) | material recipes — **edit this** |
| [assets/sounds.txt](assets/sounds.txt) | sound recipes — **edit this** |
| `assets/*.obj` | authored meshes from Blender |
| [src/mesh.h](src/mesh.h) / [src/mesh.c](src/mesh.c) | integer mesh text → geometry with authored UVs |
| [src/audio.h](src/audio.h) / [src/audio.c](src/audio.c) | waveOut mixer and the oscillator synth |
| [src/txt.h](src/txt.h) | the tokenizer every asset language shares |
| [src/gl.h](src/gl.h) / [src/gl.c](src/gl.c) | WGL bootstrap, 3.3 core context, X-macro function loader |
| [src/m.h](src/m.h) | vec3 + column-major mat4, all `static inline` |
| [src/render.h](src/render.h) / [src/render.c](src/render.c) | `Box`/`Vtx`, geometry builder, extrusion + ear clipping, GPU meshes, the one shader |
| [src/tex.h](src/tex.h) / [src/tex.c](src/tex.c) | material recipe interpreter |
| [src/model.h](src/model.h) / [src/model.c](src/model.c) | model text parser → extruded geometry |
| [src/data.h](src/data.h) / [src/data.c](src/data.c) | baked text in release, watched files in dev |
| [src/player.h](src/player.h) / [src/player.c](src/player.c) | movement, momentum, collision — no GL |
| [src/weapon.h](src/weapon.h) / [src/weapon.c](src/weapon.c) | hitscan, recoil, recoil-jump kick, view model, flash, tracers, decals |
| [src/hook.c](src/hook.c) | the grapple: a projectile claw, the winch pull, the launch — no GL |
| [src/enemy.h](src/enemy.h) / [src/enemy.c](src/enemy.c) | monster types, AI, spawning, collision, hitscan — no GL |
| [src/pickup.h](src/pickup.h) / [src/pickup.c](src/pickup.c) | ammo/health pickups: spawn and collection — no GL |
| [src/sprite.h](src/sprite.h) / [src/sprite.c](src/sprite.c) | procedural monster + pickup sprite atlases (SDF → RGBA silhouette) |
| [src/run.h](src/run.h) / [src/run.c](src/run.c) | the state one playthrough owns, and the single call that resets it |
| [src/main.c](src/main.c) | window, input, level, movement, frame loop |
| [tools/modelview.c](tools/modelview.c) | model viewer and view-model pose tuner |
| [tools/enemytest.c](tools/enemytest.c) | headless monster AI checks |
| [tools/pickuptest.c](tools/pickuptest.c) | headless pickup collection checks |
| [tools/leveltrans.c](tools/leveltrans.c) | headless level-transition data checks |
| [src/post.h](src/post.h) / [src/post.c](src/post.c) | offscreen target, pixelisation, luminance-driven dither |
| [src/diag.h](src/diag.h) / [src/diag.c](src/diag.c) | capacity-overflow counters — dev builds only, 0 bytes in release |
| [tools/hooktest.c](tools/hooktest.c) | headless grapple + momentum checks |
| [tools/diagtest.c](tools/diagtest.c) | headless diagnostics checks |
| [tools/runtest.c](tools/runtest.c) | headless restart / run-state checks |
| [tools/ui.h](tools/ui.h) / [tools/ui.c](tools/ui.c) | the editors' immediate-mode widget layer — only `ui_end` touches GL |
| [tools/uitest.c](tools/uitest.c) | headless widget checks: drags, fields, click handshake |
| [tools/sprtest.c](tools/sprtest.c) | headless sprite codec checks: both opcodes, the alphabet contract, the muzzle marker |
| [tools/audiorace.c](tools/audiorace.c) | audio threading contract under contention |
| [tools/posttest.c](tools/posttest.c) | FBO + dither shader, on a real GL context |
| [tools/sprdump.c](tools/sprdump.c) | dump the sprite atlas to a PPM (dev builds only) |
| [build.ps1](build.ps1) | build with the size flags |
| [bake.ps1](bake.ps1) | assets → `src/gen_assets.h` |
| [size.ps1](size.ps1) | per-section budget report |

## View model notes

Two things that look like polish but are actually correctness:

**Sway must be clamped, and in the right units.** `mouse_dx` is in pixels. A
target of `-mouse_dx * 0.045` turns a routine 30px flick into a 170° gun
rotation. Convert per-pixel, then clamp.

**Sway pivots at the stock, not the gun's origin.** Pivoting at the origin puts
the stock ~0.28m from the camera and the muzzle ~1.12m away, so perspective
makes the *stock* sweep across the screen while the muzzle sits still — exactly
backwards from a shouldered weapon. The pivot constants live in
[src/weapon.c](src/weapon.c).

## Roadmap

- [x] Win32 + GL 3.3 core, zero external libraries
- [x] Procedural texture generator
- [x] Level as boxes → mesh + collision
- [x] FPS camera, gravity, jump
- [x] Weapon: hitscan, recoil, spread, view model with bob/sway, muzzle flash, tracers, bullet holes
- [x] Texture recipe system — integer-only op list, one interpreter for every material
- [x] Per-vertex UVs, and extruded silhouettes that generate them for free
- [x] Text asset pipeline — hot reload in dev, comment-stripping bake for release
- [x] `modelview` — orbit preview and view-model pose tuning
- [x] `modeledit` — drag silhouette points, per-part thickness, muzzle, lathe
- [x] Audio: `waveOut` mixer, layered oscillator synth, recipes as text
- [x] Per-symbol size analysis from the linker map (`size.ps1 -Detail`)
- [x] OBJ import with authored UVs — Blender → `bake.ps1` → integer mesh text
- [x] Enemies: billboard sprites drawn from a procedurally generated atlas,
      chase/attack AI, hitscan hits, player health and a HUD
- [x] Pickups: ammo and health from the `ammo`/`health` entities, with ammo on the shotgun
- [x] Level transitions: an `exit` entity loads the level's `next`, health and ammo carried across
- [ ] `texedit` — edit recipe ops with a live preview
- [ ] Music: a tracker over the same synth
- [x] A ranged monster — the caster, with dodgeable projectiles that cover blocks
- [ ] More monster types, eight-view sprites
- [x] An `exit` that ends the game rather than looping — a win screen
- [x] Real player momentum (`Player.vel`), a grapple hook, and recoil jumping
- [ ] A vertical arena built around the hook — floating platforms, chasms, an
      anchor point you cannot reach on foot
- [ ] Final packing pass (`kkrunchy` / UPX) if the budget ever gets tight
