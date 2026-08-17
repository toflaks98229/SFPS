# SFPS

A 3D FPS that fits on a 1.44MB floppy disk — **1,474,560 bytes**, single `.exe`,
no asset files.

Inspired by [QUOD](https://daivuk.itch.io/quod), which did the same thing in
64KB. Our budget is 22× that, so the extreme demoscene tricks are optional; the
discipline is not.

## Status

A small but complete FPS loop, start to finish. Win32 window → OpenGL 3.3 core
→ shaders → procedural textures and sprites → level geometry from sectors or
from TrenchBroom brushes, mixed freely in one episode →
FPS camera with real momentum, a DOOM-Eternal-style meat hook and recoil
jumping → Quake-style shotgun with ammo → four monster types, three melee and
one that shoots → health, pickups, and level transitions that end in a win
screen → a pixelised, luminance-dithered presentation over the whole thing.
Models, materials, sounds and levels are all authored as text and hot-reload
into the running game.

```
387,072 / 1,474,560 bytes   (26.25% used)
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

All of that lives in `audio_win32.c`, separately from `audio.c` — which decides
what a sound *is* and has no `windows.h` in it. See "Where Windows stops" for
why the traffic between them needed a header of its own.

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

### Asking the level a question

Everything above builds the level. `level_trace` is how the game *queries* it —
hitscans, the grapple, monster sight, and the shadow ray behind every baked
vertex all go through it.

It used to sample. Walk the ray in 5 cm steps, ask "is this point inside a
sector and between its floor and ceiling", stop at the first no, then bisect the
last interval ten times to find the surface. Nothing about that is wrong, and
for a long time nothing about it was slow enough to look at. What it was, was
**uninformed**: a 40 m ray took 800 samples to discover a handful of crossings.

The insight is one sentence. `open_at` asks two things — does a sector cover
`(x,z)`, and is `y` between that sector's floor and ceiling — and **neither
answer can change except where the ray crosses an outline in plan or a
floor/ceiling in height.** Between two such crossings the answer is constant,
whatever the sectors are doing, overlapping and last-wins included. So find the
crossings, sort them, and sample once inside each piece. That is not an
approximation of the answer; it is the answer.

| `level_trace`, 40 m | before | after | |
| --- | --- | --- | --- |
| `arena` | 10.76 µs | 0.98 µs | **11.0×** |
| `vault` | 5.03 µs | 0.58 µs | **8.7×** |
| `dm03` (converted Doom map) | 16.65 µs | 6.01 µs | **2.8×** |

`levelbench` reports what that is as a share of a frame rather than in
microseconds, because a microbenchmark is not a decision: at full monster load
`arena` went from **7.3% of a 60 fps frame to 0.7%**, `dm03` from 11.3% to 4.1%.
`level_blocked` shares the same walker, so the light bake came with it — a cold
build of `arena` went 0.51 ms → 0.15 ms.

**It is also more correct, and that is not a claim worth making from reading
the code.** `tracediff` fires 60,000 rays per level from points inside it, runs
the old algorithm — reimplemented from the public API, so it is a second opinion
and not the same code asked twice — beside the new one, and adjudicates every
disagreement with a 1 mm march over the same predicate. Across the three levels:
**180,000 rays, 91 answers that differ, and the new one is right in all 91.**
The old sampler had been stepping over geometry, by up to 31 m on `dm03`, where
a ray cleared a thin solid and ran on into the next room. "No wall is thinner
than 5 cm" had been a constraint on level authors that nobody had written down.

Two things that only surfaced under that test, and neither is obvious:

*Sampling at a fixed fraction of an interval whose ends are geometry puts the
samples exactly where the sector model's hairline cracks are.* Two sectors that
share an edge are two independent polygons that happen to have equal
coordinates, and `point_in_sector`'s crossing test is half-open — so along the
seam there are points inside neither. Uniform 5 cm sampling landed there only by
luck. Hence two samples per piece that have to agree, and a 1 mm floor on how
narrow a piece is worth sampling at all: bounded below by the format's 1 cm
coordinates and above by float noise on a 40 m ray, so the number is a choice
rather than a knob.

*The exact crossing lies on the wall.* `level_trace` decides floor-versus-wall
by moving only the height and asking again — and asking that **at** the wall
makes the plan position a coin toss, which turned every wall normal vertical.
`leveltest`'s "wall normal is horizontal" caught it. The walker now reports the
crossing for the distance and the last point it knew to be open for that
question; they were the same value while the answer was approximate, and stopped
being the same value when it became exact.

The old sampler survives as the fallback for a ray that meets more crossings
than the table holds, counted by `DIAG_TRACE_EVENTS`. Slower, not wrong, and
better than answering from a partial list.

## Recording a run

```powershell
.\build\game.exe -record bug.dem     # play; the input stream is written on exit
.\build\game.exe -play bug.dem       # watch it happen again
```

A demo holds a level name and a list of intents — one line per frame, four
integers on it — and **no world state at all**. Replaying it recomputes
everything. That is the whole reason it is worth having: a demo that disagrees
with the game has found a bug, and a save can never disagree with anything.

It works because `world_step` is a function of `(World, Input, aspect, dt)` and
of nothing else. Every random state it consumes — the weapon's spread, the
monsters' fight rolls, the particles, the lava smoke — is a field inside the
`World` seeded from a constant. There is no `rand()`, no clock read and no
file-scope state on the simulation path.

**That was not true recently, and the work that made it true was not done for
this.** The monsters, items, projectiles, particles and bullet holes were
file-scope arrays in five modules; the doors were a sixth; weapon switching, the
grapple's release and the death screen's grace period lived inside a Win32
message handler. Each was a piece of the run somewhere a second `World` could
not reach and a recording could not describe. Moving them is what left `World`
holding all of the state and `Input` carrying all of the intent — and record and
replay then cost one module and one test.

`demotest` is that test, and it is worth being precise about what it proves. It
does not compare a demo against a stored expectation; that would only say the
format round-trips. It drives **two** worlds in lockstep from one loop — the
live one from the raw `Input`, the replay one from whatever survived the round
trip through the recording — and compares them field by field **every frame**,
on exact equality rather than a tolerance. Thirty seconds of pseudo-random
input: walking into walls, firing at nothing, throwing the hook at the ceiling,
opening the menu mid-jump.

It earned its keep on the first run. The format stored the viewport aspect as
thousandths, which is lossy — 1280/720 quantises to 1.778 and the recording was
made at 1.7777778. The muzzle solve reads the aspect, so shots landed a hair
elsewhere and the two runs had visibly diverged within a second. A `Demo` stores
the viewport's **width and height** now and divides them again on the way out,
which is exact by construction. Comparing per frame is what made that a
one-line diagnosis instead of "the demo doesn't work".

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

The generated icons answered what the floor actually asks — *what is that, and
do I want it* — from across a room, because they were designed for that
distance instead of borrowed from another one.

**Doom's pickups are now what the atlas shows, and that does not overturn the
paragraph above — it satisfies it.** The argument was against *viewmodel* art
on the floor, and `MEDI`, `SHEL` and `SHOT` are not viewmodels: they are
separate drawings id made to be recognised from exactly that distance. The
objection was never "imported art is worse", it was "art drawn for one distance
does not work at another", and these were drawn for this one. Thirteen of them
cost 3,571 bytes.

The prefix is load-bearing. A drawing is named `item` + *the exact name a level
uses to place the thing* — `itemhealth`, `itemshotgunammo`, `itemredkey` — so
one resolver in `pickup.c` serves the level loader and the sprite decoder
alike. Without the prefix, `shotgun0` would be both the shotgun's viewmodel and
the shotgun lying on the floor, and one would silently become the other.

`pickup_pixel` in [src/sprite.c](src/sprite.c) still generates an icon for any
kind nobody drew, so the graceful path is unchanged: a half-imported set shows
every item.

**One scale across every floor item**, not one per item. The cell is drawn as a
fixed square in world space, so fitting each item to its own cell would make a
box of shells exactly as large as a rocket launcher — and Doom drew them at 14
and 59 units precisely because they are not the same size. The cost is that the
smallest items are small, which is what they are; if they want to be findable
rather than faithful, the fix is a per-kind billboard size, not a per-item
scale that flattens them all to the same one.

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
campaign is `arena → atrium → vault`, and **vault has no `next`**, which is
what makes it the end of the game (see below).

The chain is not one file's list. `arena` and `vault` are sector levels in
`assets/levels.txt`; `atrium` is `assets/maps/atrium.map`, authored in
TrenchBroom, and it is on the chain because `arena` names it and its own
`worldspawn` names `vault`. `level_load` looks for a `.map` before it looks in
`levels.txt`, so a name resolves to whichever exists and the two authoring
routes mix freely inside one episode — see
[assets/trenchbroom/README.md](assets/trenchbroom/README.md).

`build\leveltrans.exe` walks the whole chain from `WORLD_START_LEVEL` and
asserts what has to be true of any campaign: every hop loads, has geometry, has
an exit, keeps that exit clear of its spawn, and the chain ends rather than
loops. It used to name `arena` and `vault` outright and went red the day a
level was inserted between them — a test that memorises the campaign instead of
checking it is one nobody can edit around.

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
! DROPPED vtx=177@0 traceev=12@430..1187 | SFPS 60fps | assets: LIVE assets\ | …
```

Nothing changes about the truncation itself. The game still degrades
gracefully; it just stops doing so in silence.

**The `@` is the second axis, and it is what makes the first one usable.** A
count on its own cannot separate two faults that want opposite fixes.
`vtx=4000` is either one level that does not fit — four thousand drops in a
single frame while it loads — or a mesh that leaks a handful every frame for
twenty minutes. Identical number, and the first is a content problem while the
second is a bug. So each entry carries the frame it first fired on, and the
frame it last fired on when those differ. Above, `vtx=177@0` is a burst during
load and is over; `traceev=12@430..1187` started seven seconds in and is still
going. The two shapes are distinguishable before a single digit is compared.

The clock is advanced in `world_step` rather than in the render loop, which
costs nothing and buys two things: the number means *simulation frame*, the
unit a demo replay and a golden digest already reason in, and every headless
tool that drives a world gets real frame numbers without knowing `diag`
exists. It ticks *before* the frozen test on purpose — a paused world still
draws and can still overflow something, and a clock that stopped with the
simulation would stamp those reports with the frame the pause began on.
`diagtest` steps a frozen title-screen world and asserts the clock moved,
which is the case that fails if anyone ever gates the tick on `!frozen`.

A counter that has never fired reports `-1`, not `0`. Frame 0 is a real frame —
the one levels load on, where most reports come from — so "never happened" had
to be given a value it could not be confused with.

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
which includes `gl.h`, which at the time included `windows.h` and the whole
OpenGL API — see the section below for how `gl.h` stopped doing that.
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

## Where Windows stops, and how that is checked

The section above kept `windows.h` out of the *simulation*. It was still in
the **renderer** — and in the asset loader, the mesh builder, the texture
cache and the font atlas — and nobody had said so, because nobody had asked
the question in a way a compiler could answer.

`.\build.ps1 -Portable` asks it. `tools/nowin/windows.h` is a file containing
nothing but `#error`, it goes *first* on the include path, and every `.c` file
in `src` is compiled against it. Anything that reaches `windows.h` by any
route stops there and is named.

**The transitive route is the entire point.** Grepping for
`#include <windows.h>` found three files. The compiler found seventeen — and
the fourteen it added were the ones receiving it silently through `gl.h`,
which is exactly how a codebase stops being portable without anyone deciding
to.

| | before | after |
|---|---|---|
| portable | 13 | **28** |
| Windows-only | 17 | **4** |

(30 files became 32: `plat_win32.c` and `audio_win32.c` are new, and both are
Windows-only on purpose — the point of them is to be the place the Win32 went.)

The four are declared, in a table in `build.ps1` that says *why* each one is:
`main.c` (window, input, frame loop), `gl.c` (WGL context creation, declared
in `wgl.h`), `plat_win32.c` (the Win32 side of `plat.h`), and `audio_win32.c`
(`waveOut`, the mixer thread and the lock, declared in `audio_dev.h`). Three
of the four have a header naming exactly what they owe the rest of `src`,
which is what keeps the list short enough to be worth reading.

**The list is checked in both directions.** A file that is not on it and
fails is a regression. A file that *is* on it and passes is a stale list —
somebody finished the work and left the entry behind, and the next reader
would believe there is more left to port than there is. Both fail the build,
for the same reason the `_Static_assert`s elsewhere object to a table that has
drifted from the enum beside it. Both were verified by causing them and
watching the check fire.

Four changes moved the fourteen:

- **`gl.h` no longer includes `windows.h`.** mingw's `<GL/gl.h>` includes it
  itself, and only to obtain `APIENTRY` and `WINGDIAPI` — guarded on both
  already being defined. Defining them first is a five-line change with an
  eleven-file blast radius. The WGL context creation that genuinely needs
  Win32 moved to `wgl.h`, which two files include instead of eleven.
- **`HeapAlloc(GetProcessHeap(), 0, n)` became `malloc(n)`** in seven files.
  `msvcrt.dll` was already imported, so this is the same allocation through a
  standard name. The two sites using `HEAP_ZERO_MEMORY` became `calloc`.
- **`wsprintfA` became `txt_append_int`/`txt_append_str`** in `scene.c`. The
  project already had those, `txt.h` already noted that pulling in `wsprintfA`
  drags `windows.h`, and the HUD was the one place still doing it.
- **`plat.h`** now holds the three things a non-platform file genuinely cannot
  do without a host: tell the user the driver refused (`plat_fatal`, from the
  renderer's shader-compile failure), find where the executable lives
  (`plat_exe_dir`), and ask whether a file has changed (`plat_file_stamp`).
  Each was one Win32 call in the middle of a file with no other reason to know
  what OS it was on, and each alone was enough to pin its whole translation
  unit to Windows.

`plat_file_stamp` deliberately returns *an opaque token*, not a modification
time. Callers only ever compare two for equality, and saying that in the type
lets each host answer as precisely as it can — Win32 hands back the
100-nanosecond `FILETIME` it already has, where a POSIX version must fold
`st_mtim.tv_nsec` and `st_size` in beside `st_mtime`, because `st_mtime` alone
counts whole seconds and would miss a second save inside the same one. An
editor that formats on save does exactly that, and the symptom would look like
the hot reload being broken.

**Splitting `audio.c` was not a move.** `audio.c` decides what a sound *is* —
layers, envelopes, which of twelve voices gets evicted, how gain falls off
with distance. `audio_win32.c` owns what a machine plays it on. The traffic
crosses both ways, which is why `audio_dev.h` exists rather than one side
including the other: the device calls `audio_mix` when a buffer comes back
empty, and the policy calls `audio_dev_lock` because the voice table it writes
is the one the mixer is reading.

The subtle part was `g_ready` — a flag gating the critical section's *own
validity*, since `audio_shutdown` deletes the lock from the game thread while
the mixer may still be running. Four call sites in the policy half each spelled
out the same "is there a device" test by hand before daring to enter the lock.
It is folded into `audio_dev_lock` now, which returns 0 when there is no device
and nothing to take — so `audio.c` never sees the flag, never sees
`__atomic_load_n`, and has no cross-thread state left in a file with no threads
of its own. The gate and the lock it gates were one thing described as two.

There genuinely is no device sometimes: `sndtest` renders sounds offline and
never calls `audio_init`, and a machine with no sound card reaches the same
path. Entering a critical section that was never initialised is not a missed
optimisation there, it is the bug.

**What this is not.** It is not a Linux build — there is no CI here to run one
on and no toolchain here to try it with, so the claim is the narrow one the
check actually supports: these 27 translation units do not reach `windows.h`.
And a Linux CI would not run all 28 suites anyway. Four of them —
`posttest`, `scenetest`, `textest`, `textest_tinycache` — exist to test real
GL output through a real context, and they would want Xvfb and Mesa rather
than portability. `audiorace` opens a real sound device on purpose. Those are
not portability failures; they are tests of things a headless runner does not
have.

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
| [src/audio.h](src/audio.h) / [src/audio.c](src/audio.c) | what a sound IS: the oscillator synth, layers, envelopes, voice eviction, distance falloff — no windows.h |
| [src/audio_dev.h](src/audio_dev.h) / [src/audio_win32.c](src/audio_win32.c) | what plays it: waveOut, four buffers, the mixer thread, and the lock the voice table needs |
| [src/txt.h](src/txt.h) | the tokenizer every asset language shares |
| [src/gl.h](src/gl.h) | the GL entry points, and no windows.h — see "Where Windows stops" |
| [src/wgl.h](src/wgl.h) / [src/gl.c](src/gl.c) | WGL bootstrap and 3.3 core context: the Win32 half, split out |
| [src/plat.h](src/plat.h) / [src/plat_win32.c](src/plat_win32.c) | everything src/ needs from the host, and the whole list is three functions |
| [src/m.h](src/m.h) | vec3 + column-major mat4, all `static inline` |
| [src/render.h](src/render.h) / [src/render.c](src/render.c) | `Box`/`Vtx`, geometry builder, extrusion + ear clipping, GPU meshes, the one shader |
| [src/tex.h](src/tex.h) / [src/tex.c](src/tex.c) | material recipe interpreter |
| [src/model.h](src/model.h) / [src/model.c](src/model.c) | model text parser → extruded geometry |
| [src/data.h](src/data.h) / [src/data.c](src/data.c) | baked text in release, watched files in dev |
| [src/player.h](src/player.h) / [src/player.c](src/player.c) | movement, momentum, collision — no GL |
| [src/weapon.h](src/weapon.h) / [src/weapon.c](src/weapon.c) | hitscan, recoil, recoil-jump kick, view model, flash, HUD |
| [src/decal.h](src/decal.h) / [src/decal.c](src/decal.c) | the marks a shot leaves: bullet holes, blood, sparks, tracers — spawning and ageing are GL-free |
| [src/hook.c](src/hook.c) | the grapple: a projectile claw, the winch pull, the launch — no GL |
| [src/enemy.h](src/enemy.h) / [src/enemy.c](src/enemy.c) | monster types, AI, spawning, collision, hitscan — no GL |
| [src/pickup.h](src/pickup.h) / [src/pickup.c](src/pickup.c) | ammo/health pickups: spawn and collection — no GL |
| [src/sprite.h](src/sprite.h) / [src/sprite.c](src/sprite.c) | procedural monster + pickup sprite atlases (SDF → RGBA silhouette) |
| [src/run.h](src/run.h) / [src/run.c](src/run.c) | the state one playthrough owns, and the single call that resets it |
| [src/world.h](src/world.h) / [src/world.c](src/world.c) | level + player + weapon + run in one struct, and the order one frame advances them — no GL, no Win32, no menu |
| [src/main.c](src/main.c) | window, GL context, input, graphics settings, the draw passes |
| [src/level.h](src/level.h) / [src/level.c](src/level.c) | both level models — sector polygons and Valve 220 brushes — behind one loader, plus collision, the light bake and exact ray queries |
| [src/brush.h](src/brush.h) / [src/brush.c](src/brush.c) | Valve 220 brush planes: clipping to faces, point tests, moving a solid with its texture |
| [src/door.h](src/door.h) / [src/door.c](src/door.c) | doors as a group of surfaces that travels, sector or brush alike — state lives in `Level` |
| [src/scene.h](src/scene.h) / [src/scene.c](src/scene.c) | the draw order for one frame, and everything it owns — takes the `World` `const` |
| [src/weaponview.h](src/weaponview.h) / [src/weaponview.c](src/weaponview.c) | the drawn gun: bob/sway pose, muzzle flash — the half of `weapon` that needs a context |
| [src/proj.h](src/proj.h) / [src/proj.c](src/proj.c) | grenades and bolts: flight, bounce, blast — no GL |
| [src/fx.h](src/fx.h) / [src/fx.c](src/fx.c) | particle effects, authored as text in `effects.txt` |
| [src/menu.h](src/menu.h) / [src/menu.c](src/menu.c) | the ESC menu's rows and what each one does |
| [src/demo.h](src/demo.h) / [src/demo.c](src/demo.c) | a recorded run: a level name and a list of intents |
| [src/font.h](src/font.h) / [src/font.c](src/font.c) | the 5x7 glyph atlas, built into a texture at start-up |
| [src/inflate.h](src/inflate.h) / [src/inflate.c](src/inflate.c) | the deflate decoder the baked assets need — the only cost of compressing them |
| [src/pools.h](src/pools.h) | the entity pools `World` owns, in one struct so a caller cannot hold half a game |
| [src/gen_assets.h](src/gen_assets.h) | generated by `bake.ps1` — every asset, deflated. Not edited by hand |
| [tools/modelview.c](tools/modelview.c) | model viewer and view-model pose tuner |
| [tools/enemytest.c](tools/enemytest.c) | headless monster AI checks |
| [tools/pickuptest.c](tools/pickuptest.c) | headless pickup collection checks |
| [tools/leveltrans.c](tools/leveltrans.c) | headless level-transition data checks |
| [src/post.h](src/post.h) / [src/post.c](src/post.c) | offscreen target, pixelisation, luminance-driven dither |
| [src/diag.h](src/diag.h) / [src/diag.c](src/diag.c) | capacity-overflow counters — dev builds only, 0 bytes in release |
| [tools/hooktest.c](tools/hooktest.c) | headless grapple + momentum checks |
| [tools/diagtest.c](tools/diagtest.c) | headless diagnostics checks |
| [tools/runtest.c](tools/runtest.c) | headless restart / run-state checks |
| [tools/steptest.c](tools/steptest.c) | headless **whole-frame** checks: update order, what a freeze stops, the rebuild handshake |
| [tools/decaltest.c](tools/decaltest.c) | headless decal checks: placement, the two lifetimes, the ring wrapping |
| [tools/ui.h](tools/ui.h) / [tools/ui.c](tools/ui.c) | the editors' immediate-mode widget layer — only `ui_end` touches GL |
| [tools/uitest.c](tools/uitest.c) | headless widget checks: drags, fields, click handshake |
| [tools/sprtest.c](tools/sprtest.c) | headless sprite codec checks: both opcodes, the alphabet contract, the muzzle marker |
| [tools/audiorace.c](tools/audiorace.c) | audio threading contract under contention |
| [tools/posttest.c](tools/posttest.c) | FBO + dither shader, on a real GL context |
| [tools/sprdump.c](tools/sprdump.c) | dump the sprite atlas to a PPM (dev builds only) |
| [tools/movetest.c](tools/movetest.c) | headless movement and collision, including 4000 randomised frames that must never end inside a box |
| [tools/leveltest.c](tools/leveltest.c) | headless level checks: the light bake, the cache, geometry fingerprints |
| [tools/maptest.c](tools/maptest.c) | headless `.map` checks: brush clipping, doors, triggers, UVs that travel |
| [tools/doortest.c](tools/doortest.c) | headless door checks, including the texture that has to move with the surface |
| [tools/tracetest.c](tools/tracetest.c) | headless ray queries — the event walk that replaced the fixed-step march |
| [tools/demotest.c](tools/demotest.c) | record/replay lockstep, the text round-trip, and the **golden**: 22 pinned fields after 30 seconds |
| [tools/scenetest.c](tools/scenetest.c) | the draw order, on a real GL context |
| [tools/textest.c](tools/textest.c) | the material recipe interpreter, on a real GL context |
| [tools/weapontest.c](tools/weapontest.c) | headless weapon rules: spread, recoil, ammo, reload |
| [tools/fxtest.c](tools/fxtest.c) | headless particle checks |
| [tools/menutest.c](tools/menutest.c) | headless menu checks: which row does what |
| [tools/sndtest.c](tools/sndtest.c) | the synth offline — no device, no thread |
| [tools/mapview.c](tools/mapview.c) | fly through a `.map` with the game's own geometry |
| [tools/dithershot.c](tools/dithershot.c) | render a level to a PNG, with `-door`/`-yaw` to pose it |
| [tools/levelbench.c](tools/levelbench.c) | time triangulation, the load build, and a frame during door travel |
| [tools/modeledit.c](tools/modeledit.c) | drag silhouette points, per-part thickness, muzzle, lathe |
| [tools/nowin/windows.h](tools/nowin/windows.h) | not a header — an `#error` that `build.ps1 -Portable` puts first on the include path |
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

## Credits and licences

**Artwork from the [Freedoom](https://freedoom.github.io/) project**, used under
the 3-clause BSD licence. The full text is in
[docs/LICENSE-Freedoom.txt](docs/LICENSE-Freedoom.txt), reproduced verbatim
because a tidied licence is not the licence that was granted.

> Copyright © 2001-2024 Contributors to the Freedoom project. All rights
> reserved. Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the conditions of the 3-clause BSD
> licence are met. Neither the name of the Freedoom project nor the names of its
> contributors may be used to endorse or promote products derived from this
> software without specific prior written permission.

**The notice ships inside the binary, and that is not decoration.** The licence
requires it to accompany *binary* distributions, and this game is one executable
with nothing beside it — so the only thing it can accompany is the game itself.
It is on the `CREDITS` screen in the ESC menu, where a player can actually read
it; a notice sitting in `.rdata` that nothing displays is a weaker claim.

**The build asserts it rather than trusting anyone to remember.** Deleting the
notice breaks nothing: the game compiles, runs and looks identical, and the only
difference is that shipping it is no longer permitted. `bake.ps1` therefore
fails the build when `assets/sprites/` holds artwork and the notice in
`src/scene.c` does not match:

```
Freedoom artwork is present in assets\sprites\ but the attribution notice in
src/scene.c is missing the line: 'Artwork from the Freedoom project.'
```

Keyed on artwork actually being present, so a checkout with none — which is how
this ships today — is under no obligation and pays nothing. Verified by removing
the line and watching the build stop.

### Why Freedoom and not the alternatives

The trap worth stating: **an engine's licence is not its assets' licence.** Doom
and Quake both released their *source* under the GPL and both keep their game
data proprietary, so "Doom is open source" does not make its sprites usable.

| | licence | verdict |
|---|---|---|
| **Freedoom** | BSD-3, single, at the repo root | **chosen** — 25 years, prior commercial use, and 2D sprites, which is the format this game is in |
| LibreQuake | BSD-3 art *plus* GPL-2 game code, in `docs/` | legitimate, but mixed, younger, and mostly 3D models |
| OpenArena, Xonotic | GPL for code *and* assets | copyleft reaches the whole game |
| Doom, Quake data | proprietary | not usable at any price |

### Turning a Freedoom sprite into one of ours

Done, and reproducible: `assets/sprites/import-freedoom.py` rebuilds all 24
frames from Freedoom's own lumps. The PNGs it writes are committed, so the
build needs neither Python nor a network; the script is kept because those
images are the *result* of a conversion and it is the recipe.

| ours | Freedoom | frames taken |
|---|---|---|
| `imp` | `POSS` | A, C, F, G, L |
| `brute` | `BOSS` | A, C, G, H, O |
| `hound` | `SARG` | A, C, F, H, N |
| `caster` | `HEAD` | A, B, D, F, L |
| `gun` | `SHTG` | B, C, D, C |

Four things the conversion has to get right, and three of them are invisible
until they are wrong.

**The offsets are the part people get wrong.** Doom crops each sprite to its
own ink and records the creature's origin separately, in `buildcfg.txt` at the
repository root. Centring the images instead looks correct until the firing
frame — which is narrow because the *arm left the box*, not because the body
moved. `POSSF1` is 4.5px off its neighbours, over a tenth of its width, and
the zombie appears to flinch sideways every time it shoots. Some PNGs also
carry a `grAb` chunk but only some, because that chunk is a staging area for
`buildcfg.txt` rather than the record. X comes from the offsets; Y does not,
since its drift is a uniform +4 on every frame — a constant shift, not jitter.

**Doom's pixels are 1.2× taller than wide**, because 320×200 was displayed at
4:3. Drawn square, every creature is squat.

**One scale per subject, set by the living frames, filling the cell height.**
The cell maps to the collision height in metres, so a sprite that underfills
it is a creature you can shoot over the head of and still hit. The corpse is
exempt — Doom's death frames sprawl wider than the cell (the brute's is 90px
against 64) and at the body's scale a third would be cut off.

**Rotation 1 only.** Monsters are billboards that always face the player, so
the other seven views are unreachable. Death frames are rotation 0 because a
corpse looks the same from every angle, which is also how the frame list tells
you which letters are deaths.

The set costs **66KB, about 4.5% of the floppy** — a shaded 64×96 creature
frame is ~2,600 bytes and its corpse ~1,100. See
[Hand-drawn art](#hand-drawn-art-and-the-weapon-that-replaces-its-model).

### The viewmodel animates off a table, not a chain of ifs

Real art made a real animation possible, and then showed why the old shape
could not hold one. The atlas cells used to be named for *moments* — `IDLE`,
`FIRE`, `PUMP0`, `PUMP1` — which works only while every moment needs a drawing
of its own. A pump does not: it passes through the **same pose** going out and
coming back. Doom's shotgun cycles `B → C → D → C → B`, and `C` is in there
twice. Naming slots after moments meant storing that pose twice, so `gun1` and
`gun3` were byte-identical — 3.3KB of atlas spent saying the same thing again.

So the cells are poses now, and *when* is a table:

```c
static const struct { unsigned char frame; float upto; } PUMP_CYCLE[] = {
    { WPN_RAISED, 0.28f },   /* the shot: kicked up, where the flash lives */
    { WPN_OPEN,   0.52f },   /* pump snapped back */
    { WPN_RAISED, 0.80f },   /* and forward again -- the same drawing */
    { WPN_REST,   1.00f },   /* settled, before idle takes over */
};
```

A pose repeats by being named twice in a table rather than stored twice in a
texture, and a longer or lumpier cycle over the same three drawings is a row,
not new art. The rows are **fractions of the pump**, not seconds, so retuning
`FIRE_INTERVAL` changes the animation's speed and not its shape — a table in
seconds would quietly run off the end of a faster pump and freeze on its last
row. And `PUMP_TIME` is now a constant rather than `FIRE_INTERVAL * 0.55f`
written out in both the timer and the picker, which is two copies of one number
free to disagree after either is edited.

Still driven by the weapon's own timers rather than an animation clock of its
own, for the reason the monsters read their frames from `EState`: a second
clock has to be advanced in step with firing, and when it drifts the gun's
picture stops matching what the gun is doing.

**Nothing on screen asserts an animation.** One that drifts does not crash; it
just stops being right, which is how it survives. `weapontest` samples the
whole pump through a `HOT_RELOAD` accessor and reads the poses back in order,
asserting the sequence is exactly raised → open → raised → rest *and nothing
else* — a single sample cannot see a row inserted, dropped or reordered.
Boundaries are deliberately not asserted: where the pump snaps back is a feel
decision that should move without breaking a test. Verified by dropping the
return pose and watching it report `got 3 poses: 1 2 0  wanted 4: 1 2 1 0`.

### Where a viewmodel goes is the art's decision, not the cell's

The imported shotgun sat dead centre looking like it belonged to a different
game, and it did, because the cell was a box the art got centred in. **Doom
does not centre its weapons.** It stores a per-frame offset and draws the
sprite at it, and those offsets are the artist placing the weapon:

| weapon | Doom screen x (of 320) | |
|---|---|---|
| shotgun | 44…166 | left of centre |
| launcher | 107…213 | centred |
| chaingun | 107…213 | centred, lower |
| chainsaw | 150…408 | right, and off the edge on purpose |

Every one bottoms out at y=200, the screen's bottom edge — which is why "sits
on the floor of its cell" looked nearly right and the horizontal never did.

So **the cell is a window on Doom's screen**: 320 units wide, 144 rows of its
3D view, and a frame's place in the cell *is* its place on that screen.

**Which view, though — and that is not the obvious one.** Doom's screen is
320×200, but its 3D *view* is 168 rows: the status bar takes the bottom 32, and
that is the framing the game shipped with. The weapon is still drawn against
the full 200 — `BASEYCENTER` is a fixed 100 whatever the view height is — so
the bar does not merely hide the bottom of the gun, it **moves** the gun and
changes its size against what you can see.

Matching the 200-row fullscreen view put the shotgun at 35.8%…99.8% of the
screen: bottom balanced exactly on the edge, nothing cut off, which reads as a
gun perched too high and too small. Doom as shipped puts it at 33.0%…109.2% —
a fifth larger, planted past the bottom edge. Measured in the running game, the
barrel tip moved from 40.1% to 38.3% down the frame.

Worth being precise about what this is *not*: it is not a resolution
difference. The whole mapping is in fractions of viewport height, so 320×200
against 1280×720 never enters — a bigger window scales everything identically.

The quad follows from the same geometry: `(4/3)·200/168·height` wide however
wide the window is, because 320×200 was displayed at 4:3 and our height covers
168 of those rows, so the weapon layer letterboxes into a widescreen viewport
rather than stretching across it.

Nothing in the sprite format changed to allow it. `o <x> <y>` already placed a
drawing in its cell; the importer computes it from the offsets instead of from
a centring rule. And it costs nothing, because bake crops to the ink: a cell
four times the area stores the same pixels, and only the atlas texture grows —
RAM, not floppy. 192×104 keeps the cell's pixel aspect equal to its screen
aspect, so Doom's 1.2 non-square-pixel correction falls out of the geometry
instead of being applied by hand.

`WPN_FRAMES` stopped being a list of moments and became a **cap**. Doom gives
the chaingun two drawings, the launcher two, the shotgun three and the chainsaw
four; a shared list of moments would pad the short ones with duplicates, which
is the thing the cycle table exists to avoid. Every weapon animates off the
timer that was already there, for a **share** of its own cooldown — they span
0.085s to 0.85s, so one fixed length would leave the rapid weapon mid-swing
when it was ready to fire again. The shotgun's rack became a table column,
because once every weapon set the timer, every weapon racked a shotgun.

### Sound: Freedoom's recordings, at four bits a sample

Sound was the last thing here still entirely synthesised, and importing real
audio is a genuine departure from *keep the recipe, not the result*. So it pays
its way:

| | bytes | of the floppy |
|---|---|---|
| 8-bit raw at 11025 Hz, as text | 253,533 | 17% |
| **4-bit IMA ADPCM** | **84,630** | 5.7% |

Half the data for a decoder that costs about forty lines. And 11025 Hz is not a
compromise — it is Doom's own rate and exactly a quarter of the mixer's 44100,
so playback holds each source sample for four output samples with no resampler
and no accumulating phase error. The nibbles pack three per two characters in
the sprite codec's alphabet: 12 bits into 12, no waste.

**A sound is a recipe or a sample**, whichever exists for its name, and both
kinds live in one library. The bake emits samples *after* the recipes and
`s <name>` became select-or-create, so a sample attaches to the sound the
recipe already made — and deleting a WAV brings the recipe straight back. Three
sounds have no sample and never will: Doom has no pump-action rack, no grapple
and no reel, so `pump`, `hook` and `hreel` stay synthesised. That is the reason
to keep both kinds rather than have one replace the other.

Seventeen lumps serve twenty-one names — `impact`, `ehit`, `hbite` and `hbiteb`
are all the same punch, encoded once. Resampling averages pairs rather than
dropping every other sample, because dropping is a brick-wall decimation that
folds everything above 5.5kHz back into the audible band and turns a shotgun
into a hiss.

Measured against the source WAVs by decoding the shipped header with a third
implementation: **RMS error 0.3%–5.2%** of full scale, with the peaks being
mid-sound transient smear rather than the codec's start-up ramp — the first 32
samples carry only 0.1%–2.4% of the total error. That is what 4-bit ADPCM costs
on percussive content.

**The round-trip guard paid for itself on its first run.** `$codes` was a
`byte[]`, and `-shl` keeps the type of its *left* operand, so a byte shifted up
by 8 is 0 and every packed pair lost its first nibble. That is the **third**
time this project has been bitten by that rule, after the sprite palette and
the sprite packer — so the array's type is the fix, not a cast at the shift.
The IMA tables are `$AdpcmStep` and `$AdpcmNext` for a related reason:
PowerShell variable names are case-insensitive, and `$step = $STEP[$i]`
overwrites the table with its own first lookup.

### Distance, and the two models that were already there

Everything positional played at whatever volume its caller felt like, so a door
two rooms away opened at your feet. Fixing that turned up more than the missing
attenuation.

**There were already two distance models, and one was invisible.** `enemy.c`
had carried its own falloff — `base * 12/(12+d)` — since before anything else
had one. Fine while it was the only one; wrong the moment audio grew a second,
because a monster and a door at the same distance became different volumes for
no reason anyone could point at. The enemy curve also never reached zero, so a
growl four rooms away was still a quarter as loud as one in your face.

The surviving curve is **full inside 5m, silent at 34m, straight line
between** — Doom's model, not an inverse square. Inverse square is what physics
does and it is the wrong choice here: it falls off so fast that a monster two
rooms away is inaudible while one across the room is deafening, and the band
where a sound is *quiet but informative* is where all the play happens.

Distance is taken **once**, when the sound starts. A one-shot that tracked its
emitter would need the mixer to read the world every buffer, and the threading
contract in `audio.c` exists precisely to keep game state off that thread. It
also means a sound cannot swell because the corpse that made it slid past.
Nothing past 34m is queued at all — voices are the scarce thing in a firefight
(twelve, evicting the *oldest*), so a distant shot that rounds to silence could
otherwise cut off a near one.

**Three sounds had never once been audible**, and the same one-line rule was
wrong in two places. `audio_play` rejected a sound with `!s->n` — right while
every sound was a recipe, wrong the moment some were samples, because a sample
has no layers. The door, the switch and the keycard were silent from the day
they were imported. And `audio_render` had its *own copy* of that test, so
fixing playback left the render path — the one the tests drive — still
reporting silence. The test written to catch the first bug found the second.

`sndtest` asks `audio_gain_at`, which is the function `audio_play_at` itself
calls. A test that reimplemented the curve would pass while the game applied a
different one, which is the exact failure it exists to catch.

Left non-positional on purpose: the shot, the rack, the dry click, the grapple,
taking damage, dying, and picking things up. Those all happen *at* the player,
and attenuating them would be measuring the distance from the listener to
itself.

### Two builds that sounded different

Recipes hot-reload from `assets/sounds.txt`. Samples cannot — they are ADPCM
the bake produces from WAVs, and the file has none. So a dev build read only
the file and heard the *recipes* while the shipped build played the *samples*:
`sndtest` rendered `shot` at 319ms where the game played 986.

That is worse than a bug in either build, because it means the test was
exercising something the player never gets. `parse_sounds` makes two passes
now, the second over the baked text with layers disabled, so both builds carry
the same audio.

The licence guard had the same shape of gap: it watched `assets/sprites/` only,
and the notice is owed for the audio on identical terms. A check written
against the example rather than against the rule goes quiet exactly when the
example stops being the whole of it.

`sndtest` reads bake's alphabet and compares it against `audio.c`'s, as
`sprtest` already does for sprites — the contract spans PowerShell and C, no
compiler can see it, and a divergence makes every sampled sound decode to
noise, which sounds like a bad recording rather than a bug. Verified by
changing one character and watching it report `1 of 64 characters decode to the
wrong value (first at 62, '+')`.

### Which frame is the idle comes from Doom's state table, not from the art

Two weapons were animating with the wrong frames, and both shipped, because I
picked the idle by looking at the drawings.

**The shotgun idled on its first pump frame.** `SHTGA0` is its real idle
(`S_SGUN`, `A_WeaponReady`), but it looks like little more than the end of a
barrel — at rest the gun is mostly below the screen edge — so I had dropped it
as unusable back when the cell was a tight box. `SHTGB0` stood in for "rest",
and `SHTGB0` is the first frame of the reload. The pump was also only playing
its middle: Doom's is `A B C D C B A`, out *and* back through two of the same
drawings, and only `B C D C B` was reachable.

**The chainsaw had its idle and its cut swapped.** `SAWG` C and D are
`S_SAW`/`S_SAWB`, the pair `A_WeaponReady` alternates between; A and B are
`S_SAW1`/`S_SAW2`, the pair `A_Saw` alternates between. C and D are the *wider*
drawings, which reads as a lunge and is exactly the wrong conclusion.

Neither failed to compile, neither crashed, and neither is visible in a
screenshot unless you already know what to look for. So the cycles are now
transcribed from `info.c` — frame and duration in tics, converted to fractions
of the recovery — and `weapontest` walks each weapon's whole recovery and reads
the poses back in order, against the sequence the state table specifies.

**And the idle is a cycle too.** `A_WeaponReady` shows one frame for three of
these weapons and alternates two for the chainsaw, because a saw you are
holding revs. Modelling "at rest" as a single drawing cannot express that, so
rest is a cycle and three of the four simply have one row. It runs off a
free-running `anim_clock` rather than `bob_phase`, because `bob_phase` stops
when the player does and a saw does not.

One test bug worth recording, since it hid two of the four: the sampling loop
ran to `i <= 400`, where the timer is exactly zero — which is not "the end of
the animation" but "not animating", so it picked up the idle frame as a phantom
extra pose. The shotgun and grenade passed anyway, because their idle happens
to equal their cycle's last pose.

### A 411KB stowaway, and why the graceful path hid it

The importer's `--preview` contact sheet landed in `assets/sprites/` and was
baked as a sprite: **411KB of a 1.44MB budget**, carried in `.rdata`, never
drawn once.

`sprite.c` ignores a name matching no monster and no weapon — documented,
deliberate, and it sounds like enough. It is not, because ignoring it happens
at *decode* time, long after the bytes are quantised, encoded and committed to
the binary. Graceful degradation at the wrong end of the pipeline is just a
silent cost.

Three changes, because one would have been the fix and not the lesson: bake
skips `_`-prefixed names, the importer writes the preview outside the scanned
directory, and the size report prints a **total**. Every line of that table was
individually unremarkable; only the sum said a third of the floppy.

### What the import cost the codec

Real art broke three things that placeholder art had been hiding, which is
the argument for importing early rather than late.

**The palette was never chosen, only collected.** Sixteen entries filled
first-come in filename order, then everything else snapped to the nearest.
Fine for four flat guns; on the first real import it produced four greens,
eleven near-identical greys and black, because `brute0.png` sorts first. The
pink creature, the gold one and the shotgun all became grey. It is median cut
now, and **per subject** rather than per set: sixteen colours across five
creatures is three each, and the reason to share a palette — making a set look
like one game — is already paid for by art that comes from one game. The
decoder needed no change, since it reads `pal` as a directive in a single
forward pass and replaces the current palette wherever it appears.

**The packed encoder had never once produced a correct sprite.** PowerShell
variable names are case-insensitive, so `$a = $idx[$i]` overwrote `$A`, the
alphabet the next line indexes; `$A` became an integer, indexing an integer
returns the integer, and every pixel encoded as the character `0`. It went
unseen because packing had never been *chosen* — all the placeholder art was
flat, RLE won every time, and the first drawing dense enough to pack was the
first to be corrupted. `sprtest` could not have caught it: it tests the C
decoder against hand-written text, which proves the decoder reads the format
and says nothing about whether the encoder writes it. bake.ps1 now decodes
each sprite it just encoded and fails the build on any mismatch.

**A drawing composited over the generated creature instead of replacing it.**
Sensible for a half-drawn bestiary, wrong for any drawing narrower than the
SDF version underneath — the generated creature showed around the edges as a
halo, a green shape standing behind the hound and a horn over the caster. The
clear is per *cell*, so the graceful path survives: a frame nobody drew is
never reached and keeps its generated creature.

And one thing that was simply left on the table: **the cells were mostly empty
margin.** RLE barely cares — a run of transparency is two characters however
long — but packing pays per three pixels whether they are picture or nothing.
Cropping to the ink and recording the offset is 13% off the set, 34% on the
frames that pack, with the decoded atlas bit-identical to before.


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
- [x] Audio: layered oscillator synth and recipes as text, with the `waveOut` device split off behind `audio_dev.h`
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
- [x] Doors, switches, trigger volumes and keycards
- [x] **TrenchBroom `.map` levels** — Valve 220 brushes read with no converter,
      collided against, lit and moved; a brush level and a sector level sit next
      to each other in one episode and nothing downstream can tell
- [x] Exact level queries — `level_trace` finds the crossings instead of
      sampling past them, verified differentially against the sampler it
      replaced
- [ ] A vertical arena built around the hook — floating platforms, chasms, an
      anchor point you cannot reach on foot
- [x] **Input record and replay** — `game.exe -record foo.dem` / `-play foo.dem`
- [x] **A portable core** — 28 of 32 translation units compile with no `windows.h`,
      and that is checked by a compiler rather than believed: `build.ps1 -Portable`
      poisons the header and names anything that reaches it. The other four are
      declared platform files with a reason written beside each
- [x] **A golden test over a recorded run** — `demotest` replays 30 seconds and
      compares 22 pinned fields exactly, so a change that alters an outcome no
      rule asserts still has to be looked at on purpose
- [ ] Final packing pass (`kkrunchy` / UPX) if the budget ever gets tight
