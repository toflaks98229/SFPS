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
jumping → Quake-style shotgun with ammo → seven monster types: five that come
at you, one that never moves and one that never acts → a boss fight where the
room is the puzzle — the maw is untouchable while its wards stand, and shooting
a ward is what fills the room → health, pickups, and level transitions that end
in a win screen → a pixelised, luminance-dithered presentation over the whole thing.
Models, materials, sounds and levels are all authored as text and hot-reload
into the running game.

```
1,054,208 / 1,474,560 bytes   (71.49% used)
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

### A surface that moves

`flow n` makes a material drift and rock at `n/100`. The lava says it:

```
t star_lava3
image star_lava3 2
flow 70
```

One world-space wave field drives **both** halves, because they have to agree —
the texture drifts where the surface tilts, so a crest is a place the crust is
being carried rather than a place it happens to be brighter. It is keyed on
world position rather than UV, so a sea assembled from several brushes swells as
one sea instead of per-face.

**Nothing is displaced, and that is deliberate.** Moving the vertices is the
obvious way to make a liquid slosh and it is wrong here: a lava brush *is* the
floor, collision is its flat top, and `LVL_HAZARD_UNDERFOOT` is five
centimetres — so any displacement honest enough not to lie about where the
surface is would be too small to see from standing height. What this renderer
has instead is `LIGHT_BANDS`: five levels, so a normal rocked a couple of
degrees walks the shading across a band edge and back. **The moving band edge is
the swell**, and it reads across a whole sea from the far side of the room.

It also breaks up the texture's tiling grid, which was not the point but is most
of what a before/after shows.

The renderer has no list of which materials are lava — a liquid is a material
that *says* it is one, so a slime or a river costs one word rather than an entry
in C. `flow` is read on both the procedural and the textured path, which the
gloss channel is not.

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

The pool is shared across every effect (`FX_MAX_PARTICLES`, 2048). A flood
overwrites the oldest particles rather than refusing the newest — the burst
that just spawned is the one being looked at — and reports `DIAG_FX_CAP` so
the truncation is visible rather than merely silent.

**The number that decides that cap is the blast**, at 297 particles for one
grenade — 213 spawned at the moment it goes off, and 84 more arriving behind
the trails over the second and a half after. It was 256 when three effects
existed, 640 when the blast was four layers, and 1536 when it was nine; the
note beside the constant carries the current figure, because a capacity
argument quoting a stale number is one nobody can check.

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

### An explosion is eleven layers, a light and a jolt

A grenade going off used to be six particle effects and a sound, and what was
missing was not artistic. **Every one of those six is additive geometry drawn in
front of the world.** None of them brightens the wall behind the blast, the
monster standing against it, or the floor under the player's feet.

`scene.c` offers every projectile in flight to the shader as a point light, and
`detonate` clears `active`. So the loudest event in this game was the one frame
in which the room got **darker** — the grenade had been lighting it right up
until it stopped existing, and then six layers of fire were drawn over geometry
that had fallen back to ambient.

`Flash` in [src/proj.h](src/proj.h) is the record that fixes it: *something went
off here, this big, this recently.* Three readers each ask it a different
question.

| the question | who asks it |
|---|---|
| how bright is the room | `scene_lights`, which hands it to the shader |
| how hard is the camera moving | `world_step`, which shakes by distance from it |
| how long ago was it | both, through `proj_flash_fade` — one curve, so the light and the jolt cannot disagree about when the explosion was over |

**It is not a particle**, and `fx.c` would hold one happily. It could answer
none of the three: a particle is written to be drawn and nothing may ask where
it is, the pool evicts the oldest without asking whether something is still
reading it, and a light has to last exactly as long as it is bright where a
burst is authored to last as long as it is still expanding. The blast smoke
lives 900ms; the light that made it is over in a third of that.

**The three reaches are three different numbers, deliberately.**

| | multiple of the blast radius | at the grenade's 4.2m |
|---|---|---|
| damage | 1.0 | 4.2m |
| light — `LIGHT_BLAST_REACH` | 1.7 | 7.1m |
| shake — `WORLD_SHAKE_BLAST_REACH` | 2.5 | 10.5m |

A blast you cannot be hurt by can still light you, and one too far away to light
you can still be felt through the floor.

**And the thrower is inside the first of those reaches like anyone else.** A
grenade takes `PROJ_SELF_DAMAGE` — Quake's half, from `T_RadiusDamage`'s
`if (head == attacker) points = points * 0.5` — off the same linear falloff,
measured against the **damage** radius rather than the shake's wider one. A
splash weapon that cannot hurt the thrower is not a splash weapon, it is a
hitscan with a delay: the arc, the fuse and the radius are all questions about
*where*, and none of them is a question while the answer is free.

It arrives through the same `player_take` as a monster's blow, so `PW_AEGIS`
cuts it and the red wash, the shake and the sound all happen — an artifact that
protected against monsters and not against your own grenade would be a rule
nobody could state. The quad multiplies it too, which is also Quake.

The axe's slam does **not** self-damage: it is 70 points in 5.5m centred on
your own feet by design, and a ground pound that hurt the grounder is a weapon
with a cost nobody chose. A monster's bolt bursting does not either — the
player has already been charged for it by `enemy_update`, and billing it here
as well would collect twice for one hit. Collapsing them onto one number means
picking which two to get wrong. **The dome is the exception that proves the
rule:** `blastdome` is scaled to the damage radius *exactly*, because it is a
claim with a gameplay number in it and a claim drawn at the wrong distance is a
lie the player will believe. Light makes no such claim, so it is allowed to
reach past where the damage stops.

The light also runs **over 1.0**, further over than anything else in the file.
`lum` clamps before it bands, so power past 1 saturates, and the shader's `tint`
mix weights by `clamp(e, 0, 1)` — at `e >= 1` the wall takes the light's colour
outright. A shotgun in your hands is worth 1.15 of that; a charge going off has
to blow the room out for two frames and then not be there, which is a curve
rather than a level. `proj_flash_fade` is the curve, and it is **squared**: an
explosion arrives at full and is most of the way gone before the eye finishes
registering it, where a light walking evenly down to nothing reads as a lamp
being dimmed.

Five particle layers joined the six:

| layer | what it is for |
|---|---|
| `blastflash` | the over-exposed first five frames. The one effect in the file that *wants* the saturation `spawn` exists to prevent — five metre-wide additive quads at full alpha, composited to a hole burnt in the frame |
| `blastwave` | the dome's equator, drawn on the floor it went off against. `disc 1` with `face normal`, scaled by the caller, because the floor is where a distance can actually be paced out — a hemisphere hanging in space cannot be |
| `blastember` | what is still burning a second later. Everything else is over inside 300ms except the smoke, and smoke on its own says the fire went *out* |
| `blastring` | the same rim as `blastwave`, lit. The dust is alpha-blended so it can occlude the floor, which is right for what a shockwave does to grit and wrong for what it does to the eye — in a dark room a pale smudge is not a radius anybody can read. Same position, normal and scale; the dust runs out in 300ms and the light takes 420, so the pale edge arrives first and the bright one stays after it |
| `blastfire` | the second the blast spends as a cloud. Everything above it is additive and can only brighten, so the explosion had two states and nothing between them: white-hot, then grey smoke climbing out of an empty floor. This one is alpha, which is what lets it go *dark*, and its ramp carries it from flame to soot |

**And the axe's slam finally admits it is one.** `wp_axe_land` calls
`proj_blast` — it makes a crater by every measure the code has — and what it
drew was `boltburst`, the *monster* bolt's flash, which cools into that bolt's
blue. The player's own axe coming down was painted in the colour `scene.c`'s
palette reserves for something shooting at them, which is the same fault
`detonate` carried until it was found. It now takes the ground wave at
`AXE_SLAM_RADIUS`, the warm burst, and a flash at `AXE_SLAM_FLASH` — a third of
a charge's. The reach is **larger** than a grenade's (5.5m against 4.2) and the
light is smaller, which is why the two cannot be derived from one another: a
mass of metal hitting stone is not a charge going off, and anything taking
brightness from reach would light the room harder for the one with no fire in
it.

### What a projectile leaves behind it, and where it lands

Two passes, both worked from a screen recording and from what other engines do,
and both of them found the same shape of hole: **an event with no before and no
after.** A grenade left the muzzle and arrived at the wall with nothing drawn
between those two moments. A bolt struck a wall and threw one effect, which was
the shotgun's.

Two keywords in `effects.txt` came out of it, and both are data rather than C:

| keyword | what it does |
|---|---|
| `stretch <ms>` | draws the quad over that many milliseconds of the particle's **own travel**, along its velocity — a streak instead of a square, and one that *shortens* as `drag` takes the speed away, which is what reads as a spark landing rather than merely fading. DarkPlaces calls the same parameter `stretchfactor` |
| `trail <name>` / `trailms <ms>` | the effect a particle leaves **behind** it as it flies. A bright dot arcing across a dark room is a dot moving; the same dot with half a metre of smoke behind it was *hurled* out of something. The chain is one link deep by construction — a wake never has a wake — so a definition may safely name one that names it back |

**The trail is what makes an arc legible.** A thrown charge is read from its
path: you learn where a grenade is going by seeing where it has been, and a dot
has been nowhere. `fusespark` and `fusetrail` are emitted by `proj.c` on a fixed
`PROJ_TRAIL_INTERVAL` rather than per frame, for the reason `SHOT_TRAIL_INTERVAL`
already existed on the monsters' side: a rate that follows the frame rate makes
the same trail denser on a faster machine and lets one round in the air empty a
pool shared with every other effect in the level.

**A landing is four layers, and none of them is invented here.** Quake II's
`TE_BLASTER` is particles plus a light; ioquake3's `CG_MissileHitWall` is a
mark, a sprite, a light and a sound, and it picks `energyMarkShader` over
`bulletMarkShader` because a bolt *burns* where a bullet chips; Xonotic's
`electro_impact` is a decal carrying the light, a smoke puff pushed along the
surface, and a short bright core, with the sparks split off into
`electro_ballexplode` and drawn stretched. Ours is `zapflash`, `zapburst`,
`scorch` and `smokepuff` — the last of which the shotgun already wrote, because
dust off a wall is dust off a wall.

| what changed | before | after |
|---|---|---|
| the rapid's impact | `spark` — the **shotgun's** warm orange, on a green plasma weapon | a white-green core, stretched green sparks, a burn, a puff, and a light in the bolt's own hue |
| a monster bolt's impact | two effects, identical whether it hit a wall or the player | the wall gets the burn and the puff; a hit on the player gets neither, because a scorch hanging where a body was reads as having *missed* |
| both | the room went **dark** on the frame of impact | `FLASH_BOLT` / `FLASH_SHOT`, at `LIGHT_HIT_REACH` and `LIGHT_HIT_POWER` — deliberately under 1.0, because the rapid lands one every 85ms and a saturating light at that rate is a strobe |

`Flash` had to learn **what made it** to do that. It carries a `kind` and not a
colour: every hue in this game is in one table in `scene.c`, under a note saying
warm is yours and cold is theirs, and a `float col[3]` on the record would be a
second place a light's colour can be decided. `flash_look` is where the three
kinds turn back into one.

**Three bugs fell out of the two passes**, all of the same shape — a rule stated
in one place that a second place never heard about:

- `blend 1` in `blastdome`, `blastcore` and `sawgrind`. The parser takes the
  *word* `add`; `1` is not that word, so it fell through to `alpha` and the two
  brightest layers of every explosion were drawn as quads that **occluded** the
  room instead of lighting it. Unknown tokens are skipped by design — that is
  what lets an old build read a new file — and the cost is that a mistyped value
  cannot be told from one that was never written.
- `scene_draw_proj` drew the player's plasma bolt in a literal pale **blue**
  while `LIGHT_COL_BOLT` lit the wall behind it acid **green**. That is exactly
  the failure `LIGHT_COL_SHOT`'s own note describes for the monsters' side, and
  the note under `scene_draw_shots` records the same line being fixed *there*.
  This was the one call site that never got the message.
- `detonate`'s bolt branch would have drawn `blood` on a monster that
  `enemy_hurt` had already bled a line earlier. It does not; there is a comment
  saying why, because an omission that looks like one gets *fixed* by the next
  reader.

## Loot

Who drops what, how often, what a cleared wave pays, where it lands and how
brightly it announces itself are all integers in
[assets/loot.txt](assets/loot.txt). None of them appear in code:

```
d brute                # a monster's drop table
  chance 62            # percent of kills that leave anything at all
  item held   3        # weights, not counts -- a kill drops exactly one thing
  item health 2

r                      # what a cleared wave pays
  give health 2        # counts, this time: a wave pays a fixed purse
  give held   3
  at    altar          # altar | player | centre
  out   340            # cm/s outward, and
  up    420            # cm/s upward, for the ring it is thrown along
  altar 6000           # ms the drop point burns as a shrine

m                      # the specks every floor item gives off
  rate   340           # ms between one mote and the next, once settled
  hurry  80            # ms between them while it is still announcing itself
  hold   1200          # ms that hurry lasts, from the moment it arrives
  range  2200          # cm past which it gives off nothing at all
```

`held` is the one name no level can place: **ammo for a weapon the player is
actually holding**, round-robin over the ones they own. A box for a gun you
have not found is the one thing `pickup.c` refuses to collect, so it would lie
on the floor for the rest of the run looking like something you had missed.

**Two rolls per kill, not one.** `chance` decides whether anything is left
behind and the weights decide what. Folding them into a single weighted table
with an implicit "nothing" entry would mean that making a monster drop medkits
more often also made it drop *everything* more often, and an author retuning
one number should be retuning one thing.

Both rolls are spent whichever way the first one lands, so `EnemyPool::rng`
advances by exactly two per kill and **a recorded demo still replays after a
rate is changed**. A `chance` that short-circuited the second roll would make
the drop table an input to the AI.

### Where a reward lands

`at` is three different games:

| | |
|---|---|
| `player` | at your own feet, which is where you are looking when the room goes quiet. Diablo's drop, and what this used to be unconditionally |
| `altar` | at an `info_altar` the map placed. The reward becomes a **place you go to** |
| `centre` | the average of the arena's spawners, for a map that wants the middle of the room without marking it |

A reward thrown at your feet is collected without a decision — you are already
standing on it. One that lands on a shrine across the room *is* a decision: go
now and be caught reloading in the open, or hold position and start the next
wave without it. `WORLD_WAVE_BREAK` is six seconds precisely so there is time
to make it.

A map that places no `info_altar` falls back to the player's feet rather than
dropping the purse at the origin, which is what makes `at altar` safe to leave
switched on for every level. That fallback is also invisible in play — the
reward still arrives, still bounces, still gets collected, and the only thing
missing is the shrine nobody knew to look for — so `wavetest` asks the
campaign directly: every level with spawners must have an altar, standing on
floor, somewhere other than the start.

### The shrine, and why it is three effects

An altar is `altarring`, `altarcore` and `altarmote` in
[assets/effects.txt](assets/effects.txt), for the same reason the blast is eleven
layers: each answers a different question. **Where** (the ring, running out
across the floor at the instant the wave ends — motion at the edge of vision is
what turns the head), **what** (the core, a warm column that says shrine and not
explosion), and **still here** (the motes, paced by `world.c` for as long as the
shrine burns, because the ring and the core are over in half a second and the
player has six).

The ring uses `disc 1`, which is `dome`'s equator: one speed around the circle
perpendicular to the spawn normal. With `face normal` the quads lie in that same
plane, so a disc spawned against a floor is a ring of light travelling out
across it. The dome cannot do this — half its particles leave the ground.

Warm gold throughout, and deliberately not the blue every other bright thing in
this game is. A bolt is blue, the hook is blue, a caster's burst cools into blue
— all of them things that are about to hurt. The one thing in the room that is a
*reward* should not be drawn in the colour of the things that are not.

### What a floor item gives off

A floor item is an alpha-tested billboard standing in a room lit to about 0.4,
and the dither pass quantises what survives. At the far end of a hall a medkit
was four dark pixels: the player did not decide not to fetch it, **they never
saw it**.

So every item gives off small specks that rise off it — `itemmote`, one per
spawn, paced by `pickup.c` the same way `world.c` paces the lava smoke.

**This replaced a halo and a light, and the replacement is smaller on purpose.**
The first attempt drew an additive halo behind the sprite and lit the floor
under a freshly landed item. Both worked, and both were the wrong kind of
working: a glowing disc standing in the air is a **lamp**, and this game already
spends that language on bolts, the hook and muzzle flash — on the things that
are about to hurt you. An item that glows the same way asks to be read as one of
them, and does it while parked, forever, which none of them do.

Motes say the same thing with none of that. They are small, so nothing reads as
a light source. They **move**, which is what actually catches the eye at
distance: a static glow competes with every lit surface in the room, and
drifting specks compete with nothing. And they leave the item's own drawing
completely untouched, so what says *which* item this is is never sitting inside
what says *where* it is.

Every number in `loot.txt`'s `m` block is a **time**, not a size:

| | |
|---|---|
| `rate` | ms between one mote and the next, once the item has settled |
| `hurry` | ms between them while it is still announcing itself |
| `hold` | ms `hurry` lasts, from the moment it arrives |
| `range` | cm past which an item gives off nothing at all |

That is the point of the struct rather than an accident of it: `LootMote` cannot
express a size or a brightness, so it cannot drift back into being a lamp.

`hurry` against `rate` is what answers "something just arrived". A steady trickle
says where an item *is*, equally whether it has been there a minute or landed
this frame — so the arrival, the one moment the player has to be told about,
reads as nothing at all. Four times the rate for `hold` milliseconds and then
settling costs one float per item.

**`range` is a budget, not a look.** Every effect shares one particle pool, and
`PICKUP_MAX` items each on a timer would spend it on specks nobody can see
before a caster's bolt could lay a trail. An item past that distance keeps its
timer running and simply does not spawn — pausing it instead would hand over the
whole backlog the instant the player walked close enough, and holding it at zero
would make every item in the room emit on the same frame, which reads as the
room blinking rather than as twelve items.

Each item's timer is staggered at birth from its **slot index**, not from a
generator: this runs in the same world a recorded demo replays, and a random
start would put every particle in the level one step out of phase with the
recording for no benefit at all.

The bolts a caster throws still got the opposite treatment, and deliberately —
they gained a third tier outside the halo and the core, twice as wide and a
fifth as opaque, breathing off the bolt's own clock. A bolt *should* read as a
lamp. It is about to hurt you.

### Retuning it

`loot.txt` hot-reloads, and it is the one asset in this project that is edited
*while playing*: tuning a drop rate means killing a monster, deciding it paid
too well, changing a number and killing another. `loot_reload` clears a flag
rather than re-parsing, so the cost lands on the next kill that asks a question
rather than on the save.

`build\loottest.exe` checks the parse and the arithmetic headlessly — that
40% of a thousand evenly spread rolls drop something, that weight 3 against 1
lands mostly on the 3, that a roll landing exactly on the edge of the die still
drops rather than reading as nothing, and that a name the file got wrong
produces no entry rather than entry zero, which is `PK_AMMO` and would have
every monster in the level quietly dropping shells.

What it does **not** assert is the shipped numbers. `chance 26` for a water spirit is a
design decision somebody will move next week, and a test that names it goes red
on that edit while proving nothing.

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

### The map caps are RAM budgets, and they were guesses until they were measured

`brush.h` says it plainly at the top of its capacity block: *"All of this lives
in `.bss`, which the floppy budget does not count. What it costs is RAM and
nothing else."* So the caps are not a size decision — and for years nothing
checked them against a map, because every map the project shipped was
hand-authored and small enough that no cap was close.

Importing somebody else's arena ended that. [tools/mapcap.c](tools/mapcap.c)
loads every map and reports what each one asks of every cap. Built with the caps
opened wide, so the numbers are what the maps *want* rather than what they were
allowed:

| map | brushes | faces | ents | verts | **runs** | load |
|---|---|---|---|---|---|---|
| `glasstower` | 7 | 42 | 25 | 252 | 4 | 0 ms |
| `lqdm13` | 244 | 1,555 | 83 | 8,946 | 6 | 2 ms |
| `lqdm11` | 425 | 2,564 | 90 | 15,411 | 4 | 3 ms |
| `lqdm2` | 607 | 3,677 | 104 | 20,211 | 17 | 5 ms |
| **`lqdm1`** | **737** | **4,340** | **83** | **24,957** | **24** | **5 ms** |
| `lqdm4` | 1,180 | 7,166 | 91 | 42,267 | 20 | 12 ms |
| `lqdm3` | 2,191 | 13,124 | 111 | 71,142 | 53 | 20 ms |

**Nothing in that table is a ceiling.** A 2,191-brush map bakes its lighting and
builds its geometry in 19 ms, costs 1.9 MB of face pool, and asks the renderer
for 53 draw calls.

**The last column used to be the ceiling, and that is why it was remeasured.**
`brush_geometry` emitted faces in brush order, so a run ended every time the
material changed and the count grew *faster* than the brush count: `lqdm4`
wanted 3,001 runs, `lqdm3` more than the 4,096-entry probe could hold, and both
shipped arenas were already past the cap of 192. Emitting one material at a time
instead makes a run the number of *distinct* materials rather than the number of
changes. The same six maps now want 4 to 53. The cap that decided how big a map
could be is now the cap furthest from being reached.

Four things the measurement found:

1. **Both imported arenas were already over `LVL_MAX_RANGES`.** 262 and 338
   against a cap of 192, so 428 and 734 runs were *merged* — and `brush.c`
   merges rather than drops on purpose, because "the surplus draws with the
   wrong texture, which is visible; dropping it would delete the wall, which is
   not". They shipped with surfaces drawing the wrong material.
2. **The cap's formula was a sector level's answer.** `LVL_MAX_SECTORS * 3` is
   exact for a loader where a sector has a floor, a ceiling and walls. A brush
   level has no sectors, and nothing in the header bounded it.
3. **`LEVEL_BUF_VERTS` was 94% consumed** by a shipped map — 973 vertices from
   dropping walls, silently, because `mb_vtx` does not grow.
4. **The run count was a property of the emitter, not of the maps.** Three of
   the six were "too expensive to ship" against a number that a reordering
   removed. Measuring a cap says what the content wants; it does not say the
   code was right to want it.

The caps were rewritten from the shipped content with roughly 3× headroom, never
from the largest map that could be made to fit — *a cap chosen to admit one
particular map means nothing the day that map is replaced.*

| | was | worst shipped | now | cost |
|---|---|---|---|---|
| `BR_MAX_BRUSHES` | 512 | 425 → 807 | **2048** | +64 KB `.bss` |
| `BR_MAX_TOTAL_FACES` | 4096 | 2,564 → 4,746 | **16384** | +1.7 MB `.bss` |
| `BR_MAX_ENTS` | 96 | 90 | **192** | +217 KB `.bss` |
| `LVL_MAX_RANGES` | 192 | **338 (over)** → 9 | **1056** | +51 KB |
| `LEVEL_BUF_VERTS` | 16384 | 15,411 | **49152** | +1.4 MB heap |

`LVL_MAX_RANGES` is the one that is no longer a sample. After the reordering the
worst shipped map wants **9** runs, so a headroom multiple would have been
meaningless in either direction. It is derived instead:
`(2 * LVL_MAX_DOORS + 1) * 32` — at most sixteen doors cut the brush list into
thirty-three stretches, `brush_geometry` cannot group across the calls that
build them, and `mapedit` offers a palette of 32 materials. That is every
stretch of a level using every material in the editor. It is sized so that no
map the other caps admit can reach it, rather than to fit the maps that exist.

**The binary did not change size: 566,272 bytes before and after.** `.bss` went
2.6 MB → 3.5 MB and the heap buffer 0.7 MB → 2.1 MB. That is the whole argument
for measuring these rather than economising on them.

**The hard ceiling is 32,767 and it is not memory.** `Brush::first_face` and
`BrushEnt::first_brush` are `short`, so an index past that wraps negative and a
brush points at somebody else's faces — silently, because nothing validates an
index that is merely wrong. Two `_Static_assert`s now say so, and both were
verified by tripping them.

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

**Three types, and a new one is a table row plus a `_pixel` function** — no new
code path. The atlas is a grid, one row per type, one column per frame:

| | role | reads as |
|---|---|---|
| **water_spirit** | the baseline — holds mid range and hoses a stream of five to ten small bolts at where you *were* | a small pale drifting shape |
| **brute** | a wall of health that hits like a truck, and the straightest line in the bestiary | broad grey-green hulk, tusks, back spikes |

**Everything moves at roughly half your walking speed, and none of it comes at
you in a straight line.** Those are one change: `PLAYER_WALK` is 10.8 m/s and
the bestiary used to be 1.9–3.0, so *nothing in the game could reach a player
who kept walking* — not a balance choice, an arithmetic fact nobody had put the
two numbers beside each other to notice. They are 5.6–7.0 now (0.52–0.65×).

Speed alone would have made them **easier**: a straight line gets simpler to
avoid as it gets faster, because it arrives sooner without arriving anywhere
new. `MonType::weave` rotates the approach vector toward the side the monster is
committed to — the *same* committed side its close-range strafe uses, so a weave
and a strafe are one decision seen at two ranges. A brute crosses nineteen
metres in 3.55 s, 1.80 m off the line, turning three times on the way.

`MON_SLIDE_HOLD` is a **distance divided by speed**, not a time. It was 1.1 s,
which was a two-metre leg at the old speeds and a seven-metre one at the new:
the tuned quantity was the *shape* of the zig-zag, and a shape held in seconds
stretches every time something walks faster.

**A monster's attack is a slot, and a ranged one answers contact.**
`MonType` used to carry one of each attack column, so a creature could not both
bite and shoot. `MON_ODDS_ALSO_MELEE` is the proof that was a limit rather than
a design: it is Quake's rule that something which can also bite shoots less
while closing, it has been in `enemy.h` with its reasoning since the range bands
arrived, and nothing had ever been able to read it.

`MonAttack` is three slots per kind, each with its own band. **Slot 0 is also
where the archetype stands** -- `chase_caster` keeps its distance around slot 0's
`max` and `chase_brawler` closes to it -- because `MonType::attack` used to
answer both "where do I want to be" and "how far can I reach", and could only
give them the same answer.

**What a caster used to be worth was "walk at it."** The retreat has no floor, so
a player who kept touching one walked it into a wall and killed it at leisure.
Now the retreat asks before it backs away, and the swing is the only thing the
table offers that close: the bolt's band starts at 7.2 m, where the retreat ends,
so nothing makes a caster stand and shoot at point-blank. The gap between 2.2 m
and 7.2 m is not an oversight -- it is where the caster kites, and the "no hole
in the bands" rule that would have forbidden it was removed for saying otherwise.

**No slot carries art.** The atlas is a fixed grid on a floppy; every attack a
kind has is drawn with the one `<name>_attack` frame it already owns. What
separates a swing from a bolt is that the swing *travels* -- `LUNGE_PIXELS`
along the line to the viewer, back through the wind-up, out at the instant
`release_swing` fires, home again over the follow-through. `scenetest` compares
two moments *inside the same pose window*: the same picture, two places, with
the bolt's pose as the control.

**An arm reaches in three dimensions**, and until a flyer could swing nothing
had to say so. Every band in the table is a floor plan, which is right for a
bolt and wrong for a reach: a caster hovering five metres up is 1.5 m away on
the plan and cannot touch you.

**And the refactor's first cut moved the whole game.** `pick_attack` rolled for
the weighted choice unconditionally, and `EnemyPool::rng` is one stream shared by
the weave, the attack rest and the spawners -- so a brute, which used to draw
*zero* times to decide it was in reach, was drawing once. Two brutes converging
came to 2.397 m instead of 1.634 m and the demo golden went red. A choice among
one is not a choice and must not cost a draw; with the single candidate returned
before the roll, both numbers came back exactly.

**A swing could not miss, and the turn rate was decorative because of it.**
`change_yaw` was put in so a player could get behind something — its own note
says "nothing could ever get behind anything, so strafing won no angle and the
whole of Quake's manoeuvring had nothing to bite on." That fixed the *looking*.
The hitting stayed `release_swing` testing a distance and nothing else, so a
player who got behind a brute during its half-second wind-up was hit anyway.

`MON_SWING_CONE` is 60°, and **it is not Quake's** — `ai_melee` is four lines
and checks `vlen(delta) > 60`, a distance with no angle. Quake gets away with it
because its monsters keep turning through the attack, and so do these, and it
*still* left a brute swinging at your back. The gate is argued from
`change_yaw`'s own note rather than from id's source.

**And the cone is drawn.** `clawarc` marks exactly that wedge at exactly the
slot's reach, the moment the swing lands — whether it connected or not, because
a swing that missed is the one whose shape the player most needs to see. It is a
*mark*, not a telegraph: the wind-up already warns, and a cone painted on the
floor beforehand would be the monster doing the player's job.

The `arc` placement op exists for it: `spawn` stops being a scatter and becomes
a ring segment — particles land **at** the radius rather than within it, because
a reach has an edge and a cloud does not. `fx_spawn_arc` takes the radius and
the angle from the *caller*, since a reach is a column in `enemy.c`'s attack
table and writing it into `effects.txt` too would be one number in two files.
`fxtest` checks it as geometry: every particle at the radius asked for, none
outside the wedge, and the wedge spanned rather than collapsed to a line.

**And they take up room, which for a long time they did not.**
`MonType::radius` had two readers — what a monster is to shoot at, and what it
is to the level's geometry — and no third. Four converging on the player arrived
as one sprite with four healths, because nothing anywhere asked whether the
space was taken.

It is two rules, and each is wrong without the other:

- **A step into another monster is refused**, and it slides along one axis
  first, exactly as a step into a wall does. `move_toward` is the one place
  every kind of movement passes through — chase, strafe, weave, retreat — so
  the rule is stated once.
- **An overlap already standing is pushed apart**, `MON_PUSH_RATE` per second
  of it. Refusal alone cannot fix that state: every direction out of an overlap
  is still an overlap, so refusing every step *welds the pair together for the
  rest of the level*. And monsters arrive inside each other for reasons that
  never involved walking — a spawner delivers its whole group at one point, the
  maw summons at one point.

The push is proportional to the overlap, so the approach is exponential:
**nine tenths of a full stack is gone in 0.18 s** and the last two centimetres
take 1.7 s, because near contact the push is tiny while the chase keeps nudging
them together. The fast number is what a player sees. Each half is capped at
half the overlap, which is what makes it stable without a damping term — a push
that can overshoot is a push that can oscillate.

**Cylinders, not spheres**, so a caster crossing the room six metres up and a
brute walking under it do not shove each other. The vertical test is the exact
span overlap rather than a margin, because heights run from the ward's 1.1 m to
the maw's 3.6 m. Corpses are not in the way, for the reason `enemy_hitscan`
already ignores them. `MON_ANCHORED` gets all of this for free in the right
direction: the maw and the wards are pushed by nothing and block everything,
because `move_toward` already refuses their steps.

**Spawning is deliberately not part of it.** Refusing to place a monster on an
occupied spot is one line, and `enemy.c` has the note explaining why it would be
a mistake — *a refusal that costs nothing never ends*: a spawner that could not
deliver kept what it owed, `enemy_wave_done` never came true, and the run
stopped on that wave for good. The push resolves a stacked delivery in a fifth
of a second and cannot stall anything.

**And the player stops against them too — both ways, after a correction.**
`move_axis` refuses a step into an occupied cylinder exactly as it refuses a step
into a wall — per axis, so brushing a monster on the diagonal slides round it
rather than sticking flat.

It shipped one-way, and the note defending that was wrong. It argued a brawler
must close to arm's length, so a monster the player's own cylinder could hold at
bay would be one the player could never be hit by. The arithmetic says
otherwise: a brute reaches 2.3 m and the bodies touch at 1.16 m; a caster's
swing reaches 2.2 m against 0.90 m. **Every melee attack reaches about twice as
far as contact**, so stopping a monster at contact costs it nothing it needs.

Nothing had a *reason* to walk into the player while a brawler stopped at its
band, which is further out than contact — so the fault was invisible until the
swing began to close. `steptest` then measured it: holding forward at a brute
put the player 0.18 m from its centre, well inside a body 0.81 m wide. It is
1.156 m now, which is touching exactly. Both halves keep the same rule: you may
not walk *into* someone, and if you are already inside them you are walking out
rather than being held there.

**player.c does not know what a monster is.** It takes `Blocker`, a cylinder
with a radius and a height, and world.c fills the array from the bestiary each
frame. The alternative was player.c including enemy.h while enemy.c already
includes player.h for `PLAYER_RADIUS` — two modules neither of which could be
read without the other, which is the coupling `m_hash` was moved into `m.h` to
undo. Joining two modules is what world.c *is*.

That split is also what makes the pair of tests mean something. `movetest`
drives `player_move` with a `Blocker` it invented: it proves the rule and would
pass for ever if world.c stopped filling the array. `steptest` never says the
word `Blocker` — it puts a real brute in the room, holds forward, and measures
the closest approach through `world_step`. Cutting the join drops that from
1.220 m to **0.000 m**: the player walks through the brute, and only the second
test says so.
| **caster** | ranged, and *off the floor* — never closes, shoots across the room | violet robe, no legs, cold cyan eyes |

**It used to be five, and the two that went were the two that were adjectives.**
A `hound` was a water spirit with the numbers pushed the other way — faster,
frailer, lower — and a `wraith` was the caster plus `MON_FLIES` and four points
of health. Neither added a question the player had to answer differently, so the
flag moved down onto the caster and both rows went. What is left is one baseline,
one wall, and one thing you cannot walk up to. Every retired name still resolves
(`enemy.c`'s `MON_LEGACY`) so a level that places one still fills.

The stats live in one table in [src/enemy.c](src/enemy.c) (`MonType`), so tuning
a monster is editing a row, and [tools/enemytest.c](tools/enemytest.c) asserts
the *roles* hold — the brute really is tougher and slower, the caster really is
frailer and further away and *up* — so a careless edit that flattens them gets
caught. Each is also visibly distinct in silhouette; `tools/sprdump.c` writes
the whole atlas to a PPM so the art can be eyeballed without launching the
game.

### The ranged type

**`MonType::behaviour` is what makes a monster ranged, and `shot_speed` is how
fast its bolt goes.** Those were one fact for a long time — `shot_speed > 0`
*meant* "is this a caster", in three separate places — and splitting them is
what `MonBehaviour` exists for: an archetype is a column, a number is a number.
`types_check` holds the two in agreement (`AI_CASTER` ⟺ `shot_speed > 0`) and
raises `DIAG_MON_TABLE` if a row ever disagrees.

Two rows are ranged, not one: the caster and the water spirit both throw bolts,
and `enemytest` asserts both. What makes the caster the *only* one you cannot
walk up to is `MON_FLIES`, which is a different column again.

> One reading of `shot_speed`-as-archetype outlived that split, in
> `check_attack`, and it is the first thing
> [docs/MONSTER_PATTERN_PROPOSAL.md](docs/MONSTER_PATTERN_PROPOSAL.md) removes —
> a melee type given a bolt to throw would slip straight through it.

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
  caster telegraphs longer than a water spirit for exactly this reason.

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

Monsters spawn at the level's entities — `water_spirit`, `brute`, `caster` (and
the retired `imp`, `hound`, `wraith`, `spawn`, each of which resolves to
whatever replaced it) — so you place them in `mapedit` with the entity tool, no
code change to build an encounter. A `.map` carries a marker's height and a
`levels.txt` sector level does not, which matters for exactly one kind: the
caster flies, so it holds the Z its marker names.

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

### The three artifacts

`item_quad`, `item_shadow` and `item_aegis` are the exception to *collect only
if it helps* — an artifact is always taken, because what it gives is time and
there is no "full" to check against.

**The `item_` prefix is load-bearing.** `level.c` takes a classname apart by
alias or by a `monster_`/`item_` prefix and ignores what is neither, so a bare
`quad` parses to no kind and is dropped at load — which is what these three
did for one commit. The prefix is why the engine sees the pickup name.

Each is thirty seconds
(`PLAYER_POWER_TIME` in [src/player.h](src/player.h)), and picking up a second
one **restarts** the clock rather than adding to it: adding would turn a room
with two artifacts into a room with one that lasts twice as long, which is not
what an author placing two of them meant.

| | what it does | who reads the clock |
|---|---|---|
| **quad** | damage dealt ×4 | `Weapon::damage_mul` |
| **shadow** | monsters cannot see you | `EnemyPool::blinded` |
| **aegis** | damage taken cut to 30% | `aegis_pct` in [src/world.c](src/world.c) |

All three are one enum for one reason: **each is a clock and a thing that reads
it**. `step_powers` counts them down once a frame and sets the two knobs; there
is no dispatch and no table of function pointers, because three effects that
share nothing but a timer are three `if`s in three modules. `weapon.c` is handed
a number to multiply by and `enemy.c` a bit saying whether it can see anything —
neither learns what a `Player` is.

`aegis` is an adaptation. It comes from Quake's red armour, which is a second
damage *pool*, and this game has no second pool — so the item becomes what a
suit of armour **does** for a while instead of becoming a bar. Quake absorbs
80% and this leaves 30%, deliberately weaker: red armour also had 200 points
that ran out, and a flat 80% for thirty seconds with nothing to deplete is
closer to invulnerability than to armour.

**Nothing counts them down.** The name joins the keycard column in its own
colour, and the whole screen takes a wash — Quake's answer, and Quake's
numbers: blue at 30/255 for the quad and grey at 100/255 for the ring are
`V_CalcPowerupCshift`'s own, and the aegis takes the **biosuit's** green at
20/255 rather than the pentagram's yellow, because the suit is the item that
cuts damage taken from the world and the pentagram is invulnerability.

There was a countdown, and it was wrong twice: it sat a row of 1.15 above the
health, which at `HUD_TEXT_SIZE` is *on* it (the keys are at 7.4, the weapons
at 9.0), and a digit has to be looked at. A wash is read without looking, and
what tells you the time is left is that it is still there.

They have their own sound (`part` in [assets/sounds.txt](assets/sounds.txt))
rather than the ammo box's — a thirty-second window should not be announced
like a shell pickup.

## Level transitions

A level names a `next` and drops an `exit` entity; walk onto the exit and the
game loads `next`, **carrying your health and ammo across** — the exit is a
reward you arrive at, the way a Doom episode runs, not a reset. The shipped
campaign is `arena → vault`, and **vault has no `next`**, which is what makes
it the end of the game (see below).

**Nothing in the shipped game walks that chain any more**, and `world.h` says so
where it names the two halves that came apart: `WORLD_START_LEVEL` is where a
fresh world starts and `WORLD_CHAIN_ROOT` is where the `next` chain begins, and
since the menu sends both STORY and ENDLESS straight to the arena, the campaign
levels "still load by name — nothing has been deleted — but nothing reaches them
by walking forward". They are kept as the progression machinery's test fixtures,
and `WORLD_CHAIN_ROOT` exists so that retiring the campaign outright is one edit
rather than three test files.

It used to be `arena → atrium → vault`, with `atrium` an
`assets/maps/atrium.map` sitting in the middle to demonstrate that a TrenchBroom
brush level and a `levels.txt` sector level mix freely in one episode —
`level_load` looks for a `.map` before it looks in `levels.txt`, so a name
resolves to whichever exists. The demonstration cost a shipped map, and the map
went when the game stopped shipping levels it could not reach. The mechanism is
unchanged: writing a brush level's name into a `next` still puts it on the
chain. See [assets/trenchbroom/README.md](assets/trenchbroom/README.md).

**And a third route: somebody else's editor.** `brush_parse` reads Valve 220 and
Standard `.map` text, which is the format TrenchBroom writes and the format the
Quake mapping world has been publishing in for thirty years. The arena the game
is fought in — `lqdm4`, LibreQuake's **Psychofuge** — is a converted
LibreQuake deathmatch map, produced by
[assets/maps/import-librequake.py](assets/maps/import-librequake.py) — no
compiler in the path, and no `levels.txt` entry needed because the file *is* the
level. What the crossing costs is written in that script: armour becomes health,
eight Quake weapons become four of ours, powerups have nowhere to go, movers
this engine has no counterpart for arrive frozen, and a room built for other
players gains the spawners, the shrine, the maw and the ward slots that make it
an arena here.

**Teleporters used to be on that list and are not any more.** A `TeleportDef` is
a `TriggerDef` with a place instead of a tag — the same non-solid volume, the
same `brush_point_in` test — so Quake's `trigger_teleport` and the
`info_teleport_destination` it names both cross, and Psychofuge's two routes
cross with them. `level.c` resolves the pair at parse time, so the runtime never
looks a name up: what reaches the level is a volume and the coordinates it sends
you to. A teleporter whose destination did not resolve is not stored at all —
half a mechanism is worse than none, and the world origin is usually solid rock.

Stepping in keeps your speed and turns it with you, so running in is running
out; the view is yanked to the destination's `angle`, which is the one place in
this game the camera moves without the mouse. At most one hop per frame, because
two teleporters pointing at each other is a legal thing to draw and an infinite
loop for anything that keeps testing.


> **The arena is `lqdm4` ("Psychofuge") now, and most of what follows measured
> `lqdm1` ("Solstice").** Everything below about how a LibreQuake map crosses —
> the texture import, the sun, the tiling divisor, the caps it pushed on — is
> the account of the first map that made the crossing, and it is kept because
> the *mechanism* is what it describes and the mechanism did not change. The
> numbers in it are Solstice's.
>
> What changed is the room. Solstice was picked for being the map LibreQuake
> tells a first-time host to start on, which is a good rule for choosing a
> *first* arena and the wrong one for choosing this game's *only* one: a winter
> castle and a maw in the wall do not belong to each other. Psychofuge is "an
> ancient lava fortress" with a lava-filled underbelly, and it is **1,149
> brushes over 1,616 × 1,200** against Solstice's 807 over 2,614 × 2,016 —
> denser and *smaller*, which is the direction a wave shooter wants. A big room
> buys a deathmatch somewhere to run; it costs this game a fight you can walk
> away from.
>
> It also brought a sun back. Solstice's `_sunlight` had been stripped when the
> lamps stopped lighting anything, so `bake_light` ran on nothing; Psychofuge
> declares 90 and 11% of its surfaces are lit by it.

**And then the walls came too.** The import above kept LibreQuake's geometry and
threw away its surface: every face was looked up in a table and given one of
*this* project's materials — `med_csl_brk14b` became `wall_stone`, four
`met_brn_*` collapsed into `wall_metal` and `wall_track`, four woods into one
`pwood`. Twenty-three names became eleven, and the result was LibreQuake's room
wearing somebody else's walls.

`assets/sprites/import-librequake-textures.py` inverts it. It reads the wads a
map's `worldspawn` names, fetches exactly the textures its faces use, and writes
them as the only thing `src/png.c` will read — 8-bit RGBA, non-interlaced,
never wider than the 128-pixel cell `sprite.c` places a wall drawing into, and
**never upscaled**: LibreQuake authored at 16, 64 and 128, and enlarging a 64
costs four times the bytes to invent detail that is not there. The project's own
materials were not removed to make room; they are still in `textures.txt` and
still used by the weapons, the fixture map and everything procedural. A second
set arrived beside the first.

The licence is on firmer ground here than the maps were. LibreQuake's own note
enumerates the BSD-3 side as *"models, textures and sounds"* — so the textures
are permissive **by enumeration**, where the maps are permissive only by residue.

| | |
|---|---|
| textures imported | 23, from 6 wads |
| faces resolving to real art | **4,314 / 4,314** (was 6 / 4,746 the day the map landed) |
| draw calls | 13 → **24** — one run per material, and there are 23 now |
| binary | 595,456 → **953,344** of 1,474,560 (64.7%, 521 KB free) |

**It cost 358 KB and uncovered a bug that had been shipping.** `sprite.c` centres
a drawing in its cell and clears the rest, which is right for a creature and
wrong for a wall — a wall's whole job is to repeat. `wall_meat`, the one 64×64
surface this project shipped, was **75.0% pure black**: exactly
(128²−64²)/128², its cleared border tiled along with the art. Nothing pointed at
it because no shipped map used that material; an imported map naming a 64×64
surface wears it on every face, and fourteen of the twenty-three are 64 or 16.
Walls tile into their cell now, and `tools/texprobe.c` pins it.

**And `wall_meat` has since been deleted, for the reason it was invisible.** No
map ever placed it — nor `black`, the only 16×16 drawing, which came across with
the LibreQuake set and is named on no face in Solstice. Both were in the
material library and on nothing you could walk up to. They went together, 732
bytes of PNG between them, and the placement path they were the only witnesses
to is now walked by six 64×64 surfaces that shipped maps actually use.

What was *not* deleted is everything else Solstice does not use. `wall_brick`,
`wall_stone`, `wall_rough`, `wall_metal`, `wall_marble`, `wall_track` and
`wall_door` are the surfaces `arena`, `vault`, `dm03` and the `atrium` fixture
are made of, and `door_red` / `door_blue` / `door_yellow` are picked at runtime
by `level.c` from a door's key colour, so no map names them at all. "Unused by
`lqdm1`" and "unused" are different sets, and only the second one is safe to
delete.

That test is worth its own note, because its first version was wrong. It measured
how much of the material came out **black**, which is what the bug looks like —
and it failed on `black` (a texture that is black) and `sky5_blu` (a night sky
that is 22% dark). Darkness is content. A cleared cell is not dark, it is
*unwritten*, and the two are the same colour only by coincidence. It asks
`sprite_wall` for the cell and counts pixels with alpha 0 instead, which says
nothing at all about how dark the art is.

**A wave arena is not yet a boss arena, and that gap used to be documented
rather than closed.** This section previously said of the imported maps that
"neither carries `info_ward_*` markers, so a maw raised in one would have
nothing to hide behind" — true, and harmless while the boss was fought in
hand-authored `glasstower`. It stops being harmless the moment an imported map
*becomes* `WORLD_BOSS_ARENA`, and the failure would not have looked like a bug:
`enemy_ward_place` with no candidates raises nothing, leaves the maw open from
its first frame, and plays as "this fight is oddly easy".

So the importer places them, on the same argument the spawners are placed by —
a deathmatch start or an item pickup is a point the map's author put something
at and therefore *checked*. It takes the starts the furniture did not use,
falls back to item positions, splits the pool at its own median height, and
picks 8 + 8 by farthest-point sampling so that `enemy_ward_place`'s "somewhere
the last cycle did not use" means somewhere across the room rather than a step
away. The high half summon flyers: `info_ward_air` selects a *summon table*,
not a floating position — `enemy.h` is explicit that the difference is "off
what walks out rather than off the ward" — which is why this needs no geometry
query it could not honestly answer. Solstice gets 16 candidates, the same
number `glasstower` was hand-authored with.

### The arena was lit by a sun the import left behind

> **This section is history now.** Solstice's `_sunlight`, `_sunlight2` and
> `_sun_mangle` were taken back out of its worldspawn along with its lamps, so
> nothing below describes what the game currently draws — no shipped map
> declares a sun, `bake_light` returns on its first line for every level, and
> every vertex carries zero baked light. The parse, the bake and the sky walk
> are all still there and still correct; nothing feeds them. Read on for what
> the numbers were and why the walk is shaped the way it is, and see the note
> at the end of this section for where the lighting went.

Solstice looked wrong after the textures came across, and not in the way the
albedo work had fixed: light did not fall in pools, it bled along one side of a
mesh and stopped dead at a seam, and every face was one flat tone. That is not a
shading bug. **It is a room with almost no light in it.**

`tools/lightprobe.c` measured the bake rather than guessing at it:

| | |
|---|---|
| vertices with **no** baked light | **22,587 of 24,957 — 91%** |
| mean baked light | 0.016, against an ambient of 0.32 |
| vertex-light pairs rejected on distance | **93.3%** of 798,624 |
| …rejected by shadow | 2.7% |

The lamps are not the problem — they are 9 to 14 m and the room is
**82 × 63 × 43 m**. They are accents. The map's worldspawn says what actually
lights it:

```
"_sun_mangle" "136  -73 0"     a directional sun
"_sunlight"   "120"
"_sunlight2"  "50"             sky-dome ambient
```

**Solstice is lit by a sun and a sky, and the import took neither.** The engine
had no idea either existed, so the whole room fell back on the shader's fixed
key direction — one constant vector, so `dot(n, key)` cannot vary across a face,
so a wall is one value corner to corner and its neighbour is another. Exactly
the reported symptom.

`level.c` reads all three now and **bakes the sun with a shadow trace**, which is
the half that matters: a directional term with no trace is just `dot(n, dir)`,
constant across a face and no better than the key it replaces. What makes light
read as *shaped* is that part of a wall is occluded and part is not. The sky
dome gets one ray straight up — is this surface under open sky or under a roof —
weighted `0.5 + 0.5·n.y` for the hemisphere a surface can see.

**Then the sun was blocked by the sky.** This engine has no sky pass, so a
`sky5_blu` face is drawn and collided with as the solid it physically is — and
every ray toward the sun hit it first. Quake's compiler answers this by treating
a ray that reaches sky as a ray that reached the sun; `light_blocked` does the
same from the other side, stepping past a sky hit and tracing on.

That took two goes, and the first was wrong in a way worth recording: it stepped
**2 cm past the face it hit**, which leaves the ray still inside a skybox wall
metres thick. Four passes advanced eight centimetres and the ray never came out
— sunlit vertices went from 174 to 209 of 12,504, *the shape of a fix that is
not fixing anything*. It steps past the **whole brush** now, by a slab test
against the bounding box that was already computed.

| | before | after |
|---|---|---|
| vertices with no baked light | 91% | **73%** |
| mean baked light | 0.016 | **0.065** |
| sunlit vertices | 174 | **3,201** |
| load | 5 ms | 6 ms |

Purely additive at the time: a level that declared no `_sunlight` got zero and
baked exactly as before, which is every hand-authored level in this project.
That stopped being true when the lamps left the bake — see below — and such a
level now bakes nothing at all, because the sun is the only thing left in it.

`tools/lightprobe.c` measured all of this, and measuring is all it did — which
made it a file `build.ps1 -Test` ran, and passed, without it ever being able to
fail. It makes one claim now: **of the surfaces that face the sun, the sun
reaches at least a twentieth.** Both broken walks were re-applied to check the
claim bites:

| walk | share of sun-facing surfaces the sun reaches |
|---|---|
| as it ships | **25.6%** |
| stepping past the face, not the brush | 1.67% |
| sky treated as opaque | 1.39% |

The first version of that test also asserted the highest upward-facing vertex in
the level is sunlit — nothing is above it, so nothing can shadow it. The logic
holds and the check is worthless: that vertex is *above* the skybox, its ray
leaves without touching anything, and it stayed green under both mutations. **A
landmark picked for being unobstructed cannot test the code that handles
obstruction.** It was deleted rather than kept as reassurance.

The probe also replicated the sky walk so it could ask its question, which is a
test that agrees with itself — the copy would have carried the same
two-centimetre step, passed, and said nothing. `level_sun_reaches()` is public
now for exactly one caller, and the copy is gone.

**The same measurement said the lamps should not be baked either, and it took
a second look to hear it.** The table above reads as a case for the sun, and it
is; it is also a verdict on the lamps, and the verdict was left on the page:
**93.3% of vertex-lamp pairs rejected on distance, 0.5% lighting anything.**
Those are not the numbers of lamps that are too dim. They are the numbers of
lamps that are **being asked the wrong question** — because the bake samples
light at VERTICES, and `lqdm1` is a brush level whose single faces are metres
across.

A lamp reaching nine metres in a room 82 across lands on a handful of corners
and is then *interpolated* over everything between them. What that looks like
is light that bleeds along one side of a wall and stops dead at a seam — which
is the same sentence this section opens with, describing the symptom the sun
was meant to fix. **The sun fixed the room. It could not fix the lamps, because
the lamps were broken by the sampling rather than by the dark.**

So the lamps left `bake_light` and went to the shader's point-light loop —
`scene_lights` offers them beside the muzzle flash and the grenades, per
fragment, with the same `(1 - d/r)²` falloff and the same banding. Nothing
about the light model changed; only where it is sampled.

**The sun did not go with them, and the reason is the shape of the two terms.**
The shader's loop takes a position, a radius and a falloff. A sun has none of
the three, and what makes a directional light read as shaped is the *trace* —
the ray that says whether this point can see the sky. A fragment shader here
cannot cast one. The lamps could leave because their falloff already ends; the
sun cannot, because for it the shadow **is** the light. `bake_light` is a sun
bake now, and its guard moved from `n_lights < 1` to `sun_power <= 0 &&
sky_power <= 0` with it.

Two things are paid for it, both written down rather than discovered later:

- **A lamp no longer casts a shadow.** The bake traced one per lamp per vertex;
  the loop cannot. Its radius is the only thing that stops it, which is why a
  lamp's reach is authored rather than physical.
- **Eight at a time.** `LVL_MAX_LIGHTS` is 64 and `RD_MAX_LIGHTS` is 8, so the
  nearest eight win, chosen per frame — the rule the grenades already lived
  under. On Solstice's thirty-two accents the ones that lose are the ones whose
  reach ended long before the eye.

`LIGHT_LAMP_POWER` is **0.45**, against the bolt's 0.55 and the muzzle flash's
0.85, scaled by each lamp's own `power`. Lower than an event on purpose: a
grenade lights one room for a second and leaves, a lamp is on for the whole
level, eight of them can overlap, and `lum` clamps at 1.0 before it bands. It
is also a *bigger* light than it was — the bake missed most of what it aimed at
and the fragment loop misses none of it — so keeping the old effective power of
1.0 would have made every lamp roughly twice the light the author placed.

**A light's colour is its identity, and the shader has to protect it.** `proj.h`
calls the colour table *"a legend, not decoration"* — a grenade throws orange, a
monster's bolt green, each caster its own row — and the player is meant to read
what is coming before they can see it. Two things were erasing that:

- **The sun had the last word on hue.** The baked sunlight was mixed into `tint`
  *after* the dynamic lights, at a weight that reaches 1 on a sunlit surface, so
  in daylight every projectile lit the room the colour of the sky. The sun is the
  background and a dynamic light is an event, so the sun's hue goes down first
  now and the lights mix over it. `lum` is untouched — the sun still lands after
  the quantiser and still saturates, so this is not a second exemption from the
  banding, only a change of who owns the hue.
- **The weight was brightness, not share.** A light's brightness falls off with
  distance; its colour does not. Mixing by the raw contribution collapsed the
  hue onto the hot centre. It is mixed by the light's *share* of everything
  landing there now, so the pool has a coloured area instead of a coloured dot.

`HUE_GAIN` is **4**, deliberately an exaggeration of that share. `LIGHT_BANDS` is
why: luminance is five levels, so a light that is merely *brighter* has almost
nowhere to say so and none at all where the sun has saturated the surface. Hue is
the one channel the quantiser leaves intact, so the event is given more of it
than the photons earn. Past about 6 the pool stops having an edge, and the
falloff is most of what says *where* the thing is.

Measured rather than judged: a green light and an orange one at the same point in
`lqdm4` differed by **4.6 parts in 255 before and 12.4 after**. `scenetest` holds
the floor — it puts a grenade in the air and asks whether the room got warmer
*the way the colour says*, scored against the other two channels so a light that
only brightened counts zero.

**The check that guarded this inverted rather than went away.** `scenetest`
asserted that a level's lamps occupy *no* dynamic slot, because a lamp applied
in both places is applied twice and *"a room lit twice does not look broken, it
looks bright"*. That failure is still real and still invisible; only its
direction changed, so the check now asserts the lamps **are** in the slots and
`leveltest` asserts that **none of them reaches a vertex**. Between them the
double-apply cannot come back from either side.

**One thing a count could not see: the events keeping their slots.** Thirty-two
lamps against eight means lamps offered on equal terms would hold all of them
and a grenade thrown twenty metres would light nothing, so `light_offer` took a
`keep` — a reserved prefix the lamps were offered against. It is gone with
them: a reservation with nobody on either side of it is a rule that cannot be
got wrong, and a rule that cannot be got wrong is the kind that quietly stops
meaning anything.

**And `dithershot` went dark for a release, which nothing would have said.**
The tool exists so the look can be judged rather than argued about, and it got
the lamps for free while they lived in the vertices: it called `level_geometry`
and the light came back inside them. A per-frame upload is something a caller
has to *make*, and a tool that makes none photographs the level with its lights
off — then the dark gets blamed on a material. It calls `rd_lights(0, 0, 0)`
now, explicitly, and that is not the same as making no call: the uniform holds
whatever the last caller left in it, and a shot lit by a leftover is a shot of a
frame the game never draws. What it photographs is the game at rest, which is
what the game at rest is.

**And then the lamps were switched off in the shader too, and the room is lit
by what is being fired in it.** The per-fragment pools were round and the
per-vertex ones were not, and it still did not come right on the map that
started this: eight slots against a level that declares thirty-two means a big
room **re-lights itself as the player walks through it**, and nothing in the
loop casts a shadow, so a lamp behind a wall lights the far side of it and then
hands its slot to another lamp two steps later. Two arrangements, both wrong in
different ways, is a sign the thing being placed is wrong rather than its
placement.

So `::Level::lights` was parsed, stored, and **read by nothing** — and then the
lamps came out of the maps as well. Solstice's thirty-two `light` entities went
from `lqdm1.map`, arena's four `light` lines went from `levels.txt`, and for two
revisions no shipped level declared one. The parser kept the word, because a
format that silently drops a word it can read is worse than one that reads a
word nothing uses. What lit a room instead was two things:

**The floor came up.** `AMBIENT` was 0.32, chosen when the lamps did the work
and the floor only had to be dark rather than black. It is **0.45**, and the
key light's share came down to `1.0 - AMBIENT` so the two still sum to one —
the constraint the first attempt at this block got wrong, topping out at 0.80
and dimming every surface a light did not reach. It costs tonal range and the
comment says so: `lum` spans 2.2 of the five bands instead of 2.7.

**And every moving light got brighter, because there is nothing else.** A flash
worth 0.85 against a corridor already at 0.6 is a flash; the same flash against
ambient and nothing else has to carry the whole moment. The muzzle is **1.15** —
over 1.0 deliberately, since `lum` clamps before it bands and a shotgun going
off in your hands *should* blow out. Monster bolts are 1.00, the grenade 0.95,
the shrine 0.80.

### The lamps came back, and what changed was the count

Nothing above is retracted. The two failures were real, the measurements that
found them stand, and the mechanism is the same one that failed: the same
`(1 - d/r)²` in the same fragment loop, competing for the same eight slots,
casting the same absence of shadow. **`LVL_LAMP_MAX` is 3.**

That number is the whole change. Eight slots against thirty-two candidates is
what made a room re-light itself — the nearest eight kept changing while the
player walked — and three cannot churn the way thirty-two did, because a lamp
leaves the set when the player has walked past it, which is when its own falloff
had taken it out anyway. They are offered **last**, after the muzzle flash and
the grenades have taken theirs, so a lamp can no longer crowd out an event.

**A bare `light` is still dark, and that is not a leftover.** It is the classname
the importer writes for every Quake lamp it converts, so a re-imported map brings
its thirty-two back as the inert things they were and no shipped level moves. A
`light_*` preset lights — `light_day` is the one in the FGD — and the underscore
is required rather than a bare prefix, because a classname turning into a lamp on
its first syllable (`lightning_bolt`) is the kind of surprise found in a dark
room six months later. A `light` line in `levels.txt` lights too, by the same
rule read the other way: nothing generates that line, a person types it.

**The shadow problem is not solved.** What is solved is the count. Three lamps
placed by hand in a room somebody chose is a case small enough to place around;
thirty-two arriving with a conversion is not.

**Where they went is what the map already said.** No converted map has a `light`
entity — the importer drops the classname, so Psychofuge arrived with its
author's lighting design deleted. What survived the conversion is the *texture*:
the arena draws its lamps with `med_tmpl_lit3`, and a face wearing it is the
author pointing at a spot. `lightprobe` prints those faces as candidate origins
(21 of them, 17 fixtures once the multi-face ones are merged), the same argument
`brush_is_lava` makes — Quake put the fact in the surface rather than in an
entity.

**Eight were placed, and the number is a measurement rather than a taste.** At
the shipped reach of 400 units, all seventeen put **seven** lamps within reach of
some of the places the map stands a player, and 44% of those places exceed the
cap. The eight leave **no marker with more than three** — so the nearest-three
sort never has a fourth to choose between and cannot churn at all. `scenetest`
asserts that of the brush arena and `leveltest` asserts it of the text one; the
cap makes crowding survivable, and not crowding is what makes it a non-event.

**Colour stopped being flavour and became the legend.** When lamps lit the
room, a light in the air was one source among many and its hue was decoration.
It is the lighting now, so the colour a wall goes is the game saying *what just
happened to it* — from behind, in the dark, before the sound arrives:

| | |
|---|---|
| muzzle flash | warm white — burnt powder |
| grenade | hot orange |
| plasma bolt | acid green |
| shrine | gold, and nothing that hurts may use it |
| monster bolt | **one row per creature** |

Warm is yours, cool is theirs, and the maw is the single deliberate exception —
the only monster attack painted warm, because it is the only one that must not
be filed with the rest at a glance. The player's two projectiles are told apart
by the field that already tells them apart: a grenade is the round with gravity
on it, which is the same test `fire_projectile` makes to decide whether what
leaves the barrel arcs. The bolt is the dimmer of the two at 0.70, and that is
about *how many* — the launcher holds six and `rapid` holds two hundred at
0.085s a shot, so a burst lays a line of them across a room and at the
grenade's power that line saturates `lum` along its whole length.

`Shot` gained one field for this. A bolt in the air has no other link back to
the monster that made it — `shot_fire` copies a position, a velocity and a
damage number and the caster walks away — so without it `scene.c` would have to
guess a creature from a damage value, which two of them share. Nothing in
`enemy.c` branches on it; the renderer is the only reader.

**The two readers of that table are the failure mode.** `scene_lights` uses the
row for the light the bolt throws on the wall and `scene_draw_shots` for the
glow the bolt is drawn as, and they are separate functions that are not obliged
to agree — which is exactly the drift the table's own note warns about, from
the other side: *a wall lit violet with a blue bolt in front of it*. So the
tier table in `scene_draw_shots` stopped naming colours and started naming what
to **do** to a hue — how bright this layer burns, how far it washes toward the
core's heat — and the numbers reproduce the old blue almost exactly at the
caster's row. Nothing about how a bolt looks changed except that it can now be
a different colour. The check for it changes `Shot::type` and nothing else,
caster against maw because those two rows are furthest apart; neighbouring hues
could come out equal after the resolve pass quantises and prove nothing.

**And the check that guards all this has pointed both ways and then lost its
subject.** It began as *the lamps occupy no dynamic slot* (they were baked, and
being in both places applies each twice), became *the lamps occupy their slots*
(the bake sampled them at the corners), and is neither now. With the lamps
deleted from the levels, a check that only read the shipped maps would pass for
the wrong reason — zero in, zero out, and it would go on passing if somebody
wired `Level::lights` straight back into `scene_lights`. So `scenetest` **puts
the lamps in itself**: eight of them, on top of the camera, reaching twenty
metres, placed where they would take every slot and light every wall in sight
if anything read them at all. The count stays at zero and the frame does not
change by a pixel. The other half — that they do not reach a *vertex* either —
is `leveltest`'s and `tracetest`'s, because `scenetest` has no vertices to look
at. Each half is separately invisible: a room lit twice looks bright, a room lit
once looks fine, and only the pair says which arrangement is in force.

**What went uncovered, said out loud.** `leveltest` used to prove a parser
property off arena's four lamps — a `light` line is eight integers, the reader
has to consume exactly those eight, and a miscount leaves it mid-line so every
declaration after it reads as garbage, silently. With no lamp in any level file
that check has no input, so it now asserts the fact that replaced it: **every
shipped level declares zero**, which is worth pinning because "no lamp lights
anything" is a property of the data as well as of the engine. The eight-integer
parse itself is unexercised. `tracetest` still covers the other half — Quake
`light` entities out of a `.map`, on the `atrium` fixture, which keeps its three
because it is a fixture the game cannot enter and deleting them would delete the
last coverage of the importer for no gain. If lamps ever come back, a fixture
for the text line is the first thing to write back.

**And then the sky went too, which is where this ends.** The lamps were
switched off, then deleted from the maps, and the last thing lighting Solstice
was the sun this section is about. Its three worldspawn keys came out with
them, so **`bake_light` now returns on its first line for every level the game
loads** and `Vtx::lr/lg/lb` is zero on every vertex in the project. Measured
after: 24,957 of 24,957 vertices carry no baked light.

What is left lighting a room is the shader's `AMBIENT` and whatever is in the
air. That is the whole model now — a flat floor of illumination, and moments
that are brighter because something was fired.

**None of the machinery was deleted, and that is a decision worth naming.**
`brush_sun_of` still reads all three keys, `bake_light` still traces, the sky
walk still steps past a whole brush rather than a face, and the 8,192-slot
light cache still stands ready to hold what the bake produces. All of it is
reachable and none of it is reached: a `.map` with a `_sunlight` key would
still light correctly. What that costs while nothing declares one is 327,680
bytes of `.bss` for the cache and twelve bytes a vertex for the light the bake
would have written.

`tools/lightprobe.c` had to be told about this. Its one claim — *of the
surfaces that face the sun, the sun reaches at least a twentieth* — took the
"no sun declared, nothing to reach" branch the moment the keys came out, and
would have gone on reporting `ok` while asserting nothing. **A suite that
passes because its subject is missing says the subject is fine.** So it puts
Solstice's own sun back in memory before it measures, and says in its header
that it is now a fixture rather than a measurement of what ships. It still
reads 25.1% against a bar of 5%, and the two broken walks it was written
against still read 1.67% and 1.39%.

**And every wall in it was tiled at twice the rate the author drew it.**
Reported from the editor: the tiles in game repeat about twice as often as
TrenchBroom shows them. Two numbers decide that, and they were being chosen by
two different rules that each looked right on its own.

`BRUSH_TEXELS` is what one material spans, in the texels a Valve 220 face
measures its offsets and scales in — every UV in a brush level is a map
coordinate divided by it. It was **128**, on the reasoning that TrenchBroom
shows the hand-drawn wall art at 128, so 128 is the tile an author fits a face
to. The first half of that is true and the second does not follow, **because a
material is not one copy of its drawing.**

Two things stack before the art reaches the screen:

- `sprite.c` tiles a drawing that is *smaller* than its cell, so one 128-wide
  cell always holds exactly 128 texels of the source's own grid — one copy of a
  128 drawing, four of a 64, sixty-four of a 16.
- `image <name> <n>` in `textures.txt` then repeats that **cell** `n` times
  across the 256-wide material.

So a material spans `128 · n` source texels, and that is what a UV of 1.0
crosses. The counts were being picked as *"256 divided by the source's side"* —
the count that fills a material, not the count that matches an editor — which
counts the source's size a second time after `sprite.c` already accounted for
it. Against a divisor of 128 that put every 128 surface at **2×** the editor's
rate, every 64 surface at **4×**, and `black` at **16×**. On Solstice: 2,721
faces at 2× and 1,593 at 4×.

The fix is one rule instead of two. Every wall material tiles its cell
`TEX_SIZE / SPR_WALL` = **2** times — the count that puts the art in the
material at its native density — and `BRUSH_TEXELS` is `SPR_WALL · 2` = **256**,
which is simply *how many texels one material spans*.

**Neither half could have caught it alone**, and that decided where the check
went. A static assert in `brush.h` cannot read a text asset, and the material
library cannot see the divisor — and the failure is invisible from both sides:
a wall tiled at twice its intended rate is still a wall, and it reads as an
authoring mistake in the map rather than as a constant disagreeing with a
number in a `.txt`. `tools/texprobe.c` is the one place that can hold both, so
it walks the material library, requires every `image` count to be
`TEX_SIZE / SPR_WALL`, and requires `BRUSH_TEXELS` to equal `SPR_WALL` times
that. Both halves were mutation-tested: putting the divisor back to 128 and one
count back to 4 turns it red on each independently.

**And two gates that could never open.** `lqdm1`'s doors are Quake `func_door`s
with `angle 90` and `angle 270` — sideways, which is what the engine already
does with them — and `targetname gate1`. The only thing that fired that name
was a `trigger_once`, which the importer drops because this engine has no
counterpart for it. `level.c` reads `targetname` into `DoorDef::tag`, and
`door.c` branches on it:

```c
if (d->tag > 0) asked = tagged;
else            asked = dist_to_outline(st, ...) <= DOOR_TOUCH_DIST;
```

So both gates arrived **tagged, waiting on a switch the conversion had already
deleted**, and nothing a player could do would move them. The untagged branch is
the behaviour that was wanted anyway — it opens within `DOOR_TOUCH_DIST` and
re-arms `DOOR_OPEN_TIME` every frame the player is near, so walking away lets it
close by itself. **A name nothing surviving can call is a promise the crossing
broke**, so the importer frees it; a door still driven by a switch that *did*
cross keeps its tag and keeps needing the switch. `doortest` asks the shipped
arena directly — not where its doors are, which is layout, but whether a player
can open them, which no built fixture can answer.

**The PlayStation look is off.** Both halves: `PSX_SNAP_COARSE` and
`PSX_AFFINE` are 0. The machinery is intact and one edit from returning — what
changed is that the arena did. Both artefacts scale with how much screen a
single polygon covers, and `PSX_AFFINE`'s own note already said where that ends:
*"a brush level has single faces bigger than a PlayStation drew in a whole
room... those faces crease along their diagonal hard enough to read as a broken
renderer."* Solstice's longest edge is **72 metres**. Turned off together
because the call site is explicit that they are halves of one thing — *"the
vertices wobble and the texture between them swims. Turning on either alone
reads as a fault in the renderer rather than as a period"* — so removing the
wobble and leaving the swim would trade one artefact for a worse one.

**And the map arrived with seventy walls that are not there.** Quake mappers use
`clip` to smooth a staircase, round off a doorframe or fence a player away from
a ledge: a brush that is solid and drawn as nothing. `brush.c` honours both
halves — it skips those faces in `brush_geometry` and leaves `Brush::solid` set
— which is a fair trade in the game those maps were built for, where the only
thing you do to a wall is bump into it. **This game has a grapple.** An
invisible wall is a hook point hanging in mid-air, and Solstice had seventy of
them across 406 faces.

The importer drops them, and only them: of 807 brushes, 70 are *entirely* clip
and **zero** mix clip faces with drawn ones, so there is no case where dropping
the brush would delete a wall. `trigger` is deliberately not on the list — a
trigger volume is also invisible and also bounds space, but the engine *reads*
it, so dropping one would delete behaviour rather than an obstacle. 807 brushes
became 737 and 4,746 faces became 4,340.

**Raising the brush cap retired it, and a test that could no longer pass said
so.** `BR_MAX_BRUSHES` 1024 → 2048 looked like a self-contained decision;
`maptest` generates `BR_MAX_BRUSHES + 8` cubes and asserts the parser stops at
the cap having stored `BR_MAX_BRUSHES * 6` faces — which now asked for 12,288
faces out of a pool of 8,192. Six faces is the fewest a closed solid can have,
so **2,048 brushes cannot exist under a pool of 8,192**: the pool fills at ~1,365
brushes, `parse_brush` drops the rest on its `count <= 0` path — which is not a
refusal and reports nothing — and the brush cap sits behind it unreachable,
refusing nothing, forever. The face pool went to 16384, and a `_Static_assert`
now says the relationship instead of a paragraph. Real content agrees with the
six: Solstice is 5.88 faces per brush.

**The same failure was hiding a second one in the fixture.** `maptest` laid its
cubes in a row 16 units apart, which put cube 1,024 at x = 16,384 — exactly
`BRUSH_MAX_COORD`, the half-extent of the world a `.map` may describe. Every
cube past it clipped to nothing and was skipped in silence. It went unnoticed
because the old cap fired on the very cube the row reached the edge of the world
at; raising the cap made the *layout* the binding constraint, and the test then
reported the cap as 1,024 — a fixture measuring its own geometry and calling it
a capacity. It lays out a grid now.

**And a third threat, because the arena had two of the same one — twice over.**
The furniture was hound, caster, hound — two rushers and a shooter, nothing in
the air.
`wavetest` asks the *shipped* arena whether at least one spawner is a flyer's
and then whether anything is off the ground, and both went red the day an
imported map became that arena. The assertions were right and the furniture was
wrong: a room with no flyer's spawner is a room where the flying path is
authored, tested elsewhere, and never entered in play. The boss's air wards
already summon flyers — so the arena knew how to be three-dimensional during a
boss fight and not for the fifteen waves before it. The duplicate rusher was the
thing to spend.

**Which spawner is the flyer's has since moved, and the assertion did not have
to.** `wraith` carried `MON_FLIES` when this was written; that row is gone and
the flag came down onto the caster, so the arena's *ranged* spawner is the air
now — it did not gain a fourth entry, it stopped being a floor spawner. The test
asks the shipped level "is at least one of them a flyer's" and never names a
class, which is the whole reason it survived the bestiary changing under it.
The three are a water spirit, a caster in the air, and a brute.

**Having the entity is not having the behaviour.** With the flyer's spawner
placed, "at least one of them is a flyer's" passed and "and something is off the
ground" still failed — because the importer puts every spawner on a deathmatch
start, and a start is by construction a place a player's *feet* go. `enemy.c` is
explicit that "a flyer keeps the height it was spawned at and never asks the
floor about it", so a flyer made at a start hovers at floor level for its whole
life: a flying monster that never flies. A flyer's spawner is lifted 64 units —
two metres at `BRUSH_UNIT` — which clears a player's head and sits under any
ceiling a deathmatch start has above it, since a start must already have
standing and jump room or the author could not have spawned there.

**And the boss itself, which the importer had no reason to know about until
now.** A wave arena needs spawners; a boss arena also needs something to fight,
and `enemy_boss_summon` raises the maw at the level's `monster_maw` marker.
`bosstest` has asserted "the shipped boss arena places a maw" all along — the
check was already there, pointed at hand-authored `glasstower`, and it is what
would have caught this. The importer places one now, at the spare deathmatch
start **farthest from the altar**: the altar is where the reward lands and so
where every wave ends, the maw is what you cross the room to reach, and putting
them at opposite ends is the difference between an arena and a corridor. In
Solstice that is 1,150 units apart.

**And then the maw arrived too early, which is the same premise failing a third
time.** Story mode had no wave gate at all, deliberately: *"the story arena IS
the boss fight, and a player who has to survive to wave five to meet it is
playing endless mode with a cutscene."* That is exact reasoning about
`glasstower` — seven brushes, a ring of ward slots, nothing else in the room to
be dropped into. Measured on Solstice, the maw stood up **one frame after the
intro cutscene ended**, at wave 1, with four wards already placed and five
monsters already walking: the player met the boss before reaching any of the
twenty-six weapons and pickups the map lays out, in a room they had not seen.

`WORLD_BOSS_STORY_WAVE` is 2 now — one wave, not five. Long enough to pick up a
gun and learn where the walls are; short enough that the story arena is still
the boss fight rather than a survival mode with a cutscene. The sentence
rejecting five waves is still right; it was never about one. `bosstest` used to
be two lines here — a fresh arena has no maw, and two frames later it does —
both true, and the second one *was* the bug. It pins the negative half now, with
the wave held open for a full second so the gate is what is under test rather
than a wave that happened not to clear.

**How a DM map is picked, twice, and why the second answer is different.** All
thirteen LibreQuake deathmatch maps were run through the importer and measured
against the engine's caps rather than guessed at from file size. Against the
caps of the day:

| | brushes /512 | entities /96 | lights /64 | verdict |
|---|---|---|---|---|
| `lqdm13` | 244 | 83 | 13 | fits |
| `lqdm11` | 425 | 90 | 47 | fits |
| `lqdm12` | 428 | **127** | 32 | over on entities and doors |
| `lqdm2` | **607** | **127** | 47 | over |
| the other nine | **807 – 2191** | up to **2704** | up to **2590** | far over |

`BR_MAX_BRUSHES` was the wall, and it was not close for most of them. The
single-player maps are worse (0.5–8 MB of `.map` text each), and the four that
*are* small enough turn out to be unfinished stubs: 24 brushes and three
entities apiece. So `lqdm11` and `lqdm13` were imported, and the honest note at
the time was that the survey was the point, not the two that survived it —
"the smallest file" was how `lqdm13` got picked first, and smallest is not a
quality measure.

**It was worse than not a quality measure. It was anti-correlated with one.**
Measuring the share of faces carrying LibreQuake's `lq_dev.wad` *development*
textures across all thirteen maps gives a clean bimodal split:

| | `lq_dev.wad` faces | |
|---|---|---|
| `lqdm11` | **99.1%** | greybox |
| `lqdm12` | **93.6%** | greybox |
| `lqdm13` | **91.1%** | greybox |
| every other map | 0.0 – 0.2% | art-passed |

The three maps that were small enough to fit are the three that had not been
built yet. A greybox is small *because* it is a greybox. And `lqdm11`
specifically is the map LibreQuake **demoted** out of its `lqdm1` slot in
December 2023 — commit `8ae29a30`, "Detailed lqdm1; switched lqdm1 with
lqdm11" — when the map that is now `lqdm1` was finished and put in its place.
This project imported the loser of that swap.

**And the map arrived with none of its art on screen.** The importer's texture
table maps LibreQuake's WAD names onto this project's materials, and every entry
in it was an `lq_dev.wad` name — because it was built against the two greyboxes,
and a table that covered them completely covered nothing else. Solstice came
through with **6 of its 4,746 drawn faces resolving to a real material. 0.1%.**
The map chosen over the greyboxes *for being art-passed* would have rendered
almost entirely in the unknown-material fallback, and nothing would have said so:
a material `textures.txt` cannot resolve is not a failure the engine reports —
it is a surface, drawn as whatever the fallback happens to be. It would have
looked like a rendering bug, or like the map being bad, and not like a lookup
table with 22 holes in it. Mapped by role, it is 100% now across 11 materials,
and the draw calls went *down* — 24 runs to 13 — because 22 distinct texture
names collapsed into 11 real ones.

**`wall_plain` is not plain**, which is the second thing that table taught. The
first pass sent Solstice's snowfields and sky to it on the strength of the name;
it is a sandstone panel carved with hieroglyphs, and snow rendered in
hieroglyphs is a worse answer than the fallback it replaced. They go to
`wall_marble` — mottled pale grey, which is what a snowfield and an overcast
winter sky have in common. The check that caught it was opening the PNG. A
material chosen off its name is a material nobody looked at.

**So the arena is `lqdm1` now, and the two greyboxes are gone.** Solstice is
807 brushes, fully art-passed, and the map LibreQuake's own
`docs/deathmatch-setup-guide.txt` tells a first-time host to start on. It cost
one cap — `BR_MAX_BRUSHES` 1024 → 2048, +32 KB of `.bss` — because 807 against
1024 is 79%, inside the cap and outside the headroom rule `mapcap` asserts.
Nothing else moved: 4,746 of 8,192 faces and 24,957 of 49,152 vertices were
already inside it.

**Why not `lqdm3` "Hyperborea", which is the pack's actual showpiece.** It is
the newest map, the only DM map LibreQuake ever announced by name, 2,191
brushes, 51 textures and zero dev faces — and it loses on cost and on shape. It
wants **four** caps, not one: brushes → 4096, the face pool → 24576 (16384 fails
the headroom rule at 80%, ≈ +2.4 MB `.bss`), `LEVEL_BUF_VERTS` → 131072, which
is *heap allocated for every level regardless of size* — 2.1 MB becomes 5.6 MB,
paid while loading `spire` and its 612 vertices — and `BR_MAX_FACES` 32 → 36,
because two of its brushes carry 33 faces and `BR_MAX_POLY` must follow it.
Then 51 texture entries instead of 23. And it is **6152 × 5136** against
Solstice's 2614 × 2016: in a wave shooter that is not grandeur, it is kiting
distance, and four wards to hunt across it is a walk rather than a fight. A
defensible upgrade once this pipeline has been proven on a smaller map; not the
cheaper or the safer answer now.

Retiring `lqdm11` and `lqdm13` is not tidying. **Neither was ever entered by the
game** — `WORLD_BOSS_ARENA` pointed at `glasstower` and `WORLD_START_LEVEL` at
`spire` — so they were 47 KB of a 1.44 MB budget spent on two rooms no player
could reach.

**And then the same test was applied to everything else, and the binary went from
four `.map` files to one.** The question "is this reachable" had exactly one right
answer per map and nobody had asked it of the whole directory:

| | was | verdict |
|---|---|---|
| `lqdm1` | — | **the arena, and now the title backdrop too** |
| `spire` | the title backdrop | deleted — a hand-authored arena kept alive to be the thing a menu is drawn over |
| `glasstower` | the boss arena | deleted — superseded; seven brushes, and the ward reference is now the importer |
| `atrium` | on the campaign chain | **kept as a file, dropped from the bake** — see below |

**`atrium` is a fixture, not a level, and that is the difference between the
file and the asset.** `tools/tracetest.c` makes 108 assertions against its
geometry — a balcony at engine (−5, −5) whose top is at 3 m over a floor at 0,
which is *"the pair of heights a sector could not hold"* and the reason brush
levels exist at all — plus its doors, triggers, keys, hazards and entities. No
other map has that combination and `lqdm1` has almost none of it: two doors, no
triggers, no hazards, no keys. Deleting the file would delete the test.

So `bake.ps1` grew a `$mapsNotBaked` list and the file stops being an asset
without stopping being a file. Every tool is an authoring build and reads
`assets\maps\<name>.map` from disk (see `data.c`'s `HOT_RELOAD` half), so the
fixture is exactly as available as it was; the shipped build reads the blob, and
the blob has one map in it.

That cost one test variant, recorded where it used to be declared:
`tracetest_baked` built the same assertions without `HOT_RELOAD` so they ran
against the blob. **A fixture deliberately kept out of the blob cannot be read
through the blob** — it was not failing because something broke. What it
protected was "the blob reproduces the file", and `maptest`'s `test_bake_matches`
checks exactly that, plane for plane, texture for texture, UV for UV and key for
key — on `lqdm1`, the map that actually ships, which the variant never touched.

`WORLD_START_DEFAULT` is `lqdm1` now, which the `WORLD_BOSS_ARENA` note used to
argue against: loading the arena behind the menu "would mean the room the player
is about to be dropped into has already been walked through by nobody, and its
spawners are one `title = 0` away from arming." Both halves have answers.
`step_confirm` is the only thing that clears `RunState::title`, and
`screen_takes_press` returns 0 whenever a menu is open — `MENU_TITLE` always is;
the only path that reaches it is demo playback, which drives recorded input in
the room it was recorded in. And choosing STORY does not reuse the backdrop:
`world_begin` reloads with `WORLD_ENTER_NEW`, a fresh level and a cleared run.
The aesthetic half was about `glasstower` — a greybox tower nobody would want
behind a menu. A finished deathmatch map is a better backdrop than a room kept
alive to be one.

**The redone survey found three silent refusals, and none of them was a cap
being full.** Re-measuring the pack against today's numbers turned up bugs
rather than verdicts, which is the useful kind of result:

- **The importer was measuring against caps that no longer existed.** Its cap
  table was a hand-copied list under a comment reading *"a copy that drifts is a
  copy that reports the wrong cap"* — and it had drifted: it reported every map
  against `BR_MAX_BRUSHES` 512 and `BR_MAX_ENTS` 96 while the engine carried
  1024 and 192, calling maps too big that fitted. It reads the headers now, and
  refuses to run rather than invent a number it cannot find.
- **`_tb_transformation` was being truncated into an unattributable counter.**
  TrenchBroom writes its own bookkeeping into a `.map` — layer, linked-group and
  a 4×4 placement matrix — and a compiler discards it. This engine has no
  compiler, so the matrix reached `brush.c` at 98–136 characters against a
  `BR_VAL` of 64, got cut, and raised `DIAG_MAPENT_CAP`. `lqdm2` and `lqdm4`
  both reported `mapent=2` while sitting at 104 and 91 entities against a cap of
  192 — a refusal named after a cap that was nowhere near full. The two maps
  already shipped carried no linked groups, so it was invisible until a map that
  used them was measured. The importer drops `_tb_*` now.
- **`BR_MAX_FACES` was named and never counted.** The importer's cap table has
  listed faces-per-brush since it was written, and nothing ever compared
  anything to it — so `lqdm3`, which carries two brushes of 33 faces against a
  cap of 32, lost a face from each without a word. A brush missing a face is not
  a smaller brush; it is an open box.

What is worth carrying away is that a third of the original survey was decided
by a draw-call number that a reordering of `brush_geometry` later deleted, and
the rest of it by a file-size heuristic that selected for unfinished work.

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
- **`plat.h`** now holds the four things a non-platform file genuinely cannot
  do without a host: tell the user the driver refused (`plat_fatal`, from the
  renderer's shader-compile failure), find where the executable lives
  (`plat_exe_dir`), find where a save may live (`plat_save_dir`), and ask
  whether a file has changed (`plat_file_stamp`). Each was one Win32 call in
  the middle of a file with no other reason to know what OS it was on, and each
  alone was enough to pin its whole translation unit to Windows.

### What is left inside `main.c`, and what that costs

`main.c` is a declared platform file, so nothing checks it — and the four
`_Static_assert`s, the `-Portable` sweep and the 36 headless tools all stop at
its door. That is the deal, and it has a price that is worth naming, because it
was paid.

**The cursor came up hidden on a screen that had to be clicked.** Cursor
visibility is one expression — visible while a menu, the title, the death
screen or a cutscene is up, or while another window has focus — and it was
evaluated only by the five places that changed one of those. The *earliest* of
the five is `WM_SETFOCUS`, which arrives while `app_start` is still creating
the window: before `world_init`, so `run.title` is still `0`, and before
`menu_open_title`, so no menu is open yet. The one call that ever decided the
pointer decided it from a `World` that did not exist, hid it, and was never
asked again.

It had been that way for as long as there had been a title screen and cost
nothing, because a screen that takes *any* key or *any* click needs no pointer
to aim with. The first screen that had to be clicked **at** was the first one
that could not be used.

The fix is the idiom three other lines in that loop already use — `music_play`,
`audio_set_volume`, `menu_set_unlocked` — *state as a condition, never as an
event*: ask once a frame, from the state, and delete the five edge calls.
`cursor_show` was already idempotent, which is what makes that free.

The lesson is not about cursors. **An invariant maintained by every site that
could break it is an invariant that depends on the order two initialisers
happen to run in**, and no test in this project can see that order — it is
behind `WinMain`. What found it was running the game and clicking.

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
`glDepthMask` or clear the buffer, and `wpview_draw_view` in particular draws the
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
| [assets/loot.txt](assets/loot.txt) | drop rates, the wave reward, where it lands, how it marks itself — **edit this** |
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
| [src/loot.h](src/loot.h) / [src/loot.c](src/loot.c) | who drops what and how often, what a cleared wave pays and where — authored as text in `loot.txt`, no GL |
| [src/menu.h](src/menu.h) / [src/menu.c](src/menu.c) | the ESC menu's rows and what each one does, plus the title screen that chooses a mode |
| [src/save.h](src/save.h) / [src/save.c](src/save.c) | the two facts that outlive a run — what is unlocked, how far it got. The only file the shipped exe writes |
| [src/story.h](src/story.h) / [src/story.c](src/story.c) | the intro, victory and defeat cutscenes, authored as text in `story.txt` — no GL |
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
| [tools/menutest.c](tools/menutest.c) | headless menu checks: which row does what, and which one is locked |
| [tools/savetest.c](tools/savetest.c) | headless save checks: what survives a process, and what a corrupt file cannot do |
| [tools/storytest.c](tools/storytest.c) | headless cutscene checks: the shipped `story.txt`, and every cap counted |
| [assets/maps/import-librequake.py](assets/maps/import-librequake.py) | converts a BSD-3 LibreQuake deathmatch map into a level this engine loads |
| [tools/mapcap.c](tools/mapcap.c) | what a map costs against every cap that could refuse it — including the ones that refuse silently |
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

**The `lqdm4` arena — "Psychofuge" — from
[LibreQuake](https://github.com/lavenderdotpet/LibreQuake)**, under the same
3-clause BSD licence — text in
[docs/LICENSE-LibreQuake.txt](docs/LICENSE-LibreQuake.txt). LibreQuake ships
under two licences and says so itself: its `docs/COPYING` is BSD-3 project-wide,
and `docs/README-IMPORTANT-LICENCE-INFO` carves GPL-2 out for the QuakeC,
`progs.dat` and `pop.lmp`. Nothing on the GPL side is used here.

**The maps are BSD-3 by residue, and this used to be written as if by
enumeration.** The sentence above said "BSD-3 for the maps, models, textures and
sounds" — but the word *maps* appears on neither side of LibreQuake's own list,
which enumerates the permissive half as "models, textures and sounds". Maps are
permissive because everything not carved out falls under the project-wide
`COPYING`, which is a sound chain and a different one. Stated precisely because
the difference is exactly the kind a paraphrase hides: GitHub's own API reports
this repository as `NOASSERTION` / "Other", so a reader who checks the badge
rather than the files will not find the answer this project relies on. What
shores it up further is `docs/CREDITS`, which lists ZungryWare's contributions
as including *Maps* — so the maps are contributions to the project whose
`COPYING` this is. Converted by
[assets/maps/import-librequake.py](assets/maps/import-librequake.py), which is
kept for the reason every importer here is kept: the map is the *result* of a
conversion and the script is the recipe.

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
fails the build when a licensed work is present and the notice in `src/scene.c`
is not that work's licence verbatim:

```
LibreQuake assets are present (assets\maps\) but the NOTICE table in
src/scene.c is not the licence verbatim: after '...IMPLIED WARRANTIES OF
MERCHANTABILITY ' the licence says 'AND' but the notice says 'ARE'.
```

Keyed on the assets actually being present, so a checkout with none is under no
obligation and pays nothing.

**And it did not work.** For as long as this section has claimed it was
"verified by removing the line and watching the build stop", the check had been
finding **zero** of the 52 PNGs it watches — `Get-ChildItem $dir -Include *.png`
matches nothing unless the path ends in `\*`, silently, with no error. The guard
ran, found nothing to be obliged about, and printed that everything was fine.
Adding a second work is what found it: the same code was written again, failed
the same way, and this time somebody mutated the notice to check.

That is the failure mode worth naming. *A guard with false positives gets
switched off by a person; a guard with false negatives switches itself off and
keeps reporting success.* The fix is one `\*`; the lesson is that
"verified by watching it fail" has to be redone whenever the thing it verified
is rewritten — so both mutations are now written down beside the guard, and both
were run.

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

**The same table answered a second question later, and gave a different name.**
When a Quake-format *arena* was wanted, "mostly 3D models" stopped being an
objection — a map is not a model — and "mixed" stopped being one too, because
LibreQuake's split is written down in its own docs and the maps are on the
permissive side of it. So the row that lost for sprites won for a level, on the
identical test. OpenArena and Xonotic lost again and for exactly the reason
recorded above: this game bakes its maps *into* the executable, so a copyleft
map is a copyleft binary. `spirit-quake-maps-gpl`, the other sizeable collection
of Quake `.map` sources on GitHub, is GPL-2 throughout and falls the same way.

Worth stating because it is the point of writing a rule down rather than a
decision: the rule was reusable and the decision would not have been.

### And a third time, when the ask was "the most famous map"

The same rule was put to the hardest version of the question — not "is there a
permissive arena" but "can we have a *famous* one" — and the answer is a clean
negative that is worth recording so nobody runs the search again.

**There is no famous Quake-lineage arena with a permissive `.map` source. Not
one.** The emptiness is structural rather than a failed search: the famous
arenas were built 1996–2000, before anyone attached a licence to a level. They
shipped as a `.bsp` and a readme, and the readme says who made it, not what you
may do with it. Licensing norms arrived with the GitHub generation, by which
time the maps that matter were twenty years old. **Fame and permissive licensing
in this field are close to anti-correlated**, and under this project's own rule
no licence is not a gap to fill with optimism — it is all rights reserved.

What that costs, named plainly, because it is the price of the constraint:

| off the table | why |
|---|---|
| **aerowalk** (Preacher, 1998) — the most-ported competitive map in FPS history, official in Quake Live and Quake Champions | no `.map` source has ever been published anywhere; the 1998 readme contains no licence and no grant |
| **ztndm3 / Blood Run** (ztn, 1997) — an official Quake Champions arena today | same: `.bsp` only, no licence |
| **id's own `dm2`, `dm3`, `dm4`, `dm6`, `e1m1`** — Romero's Oct 2006 `quake_map_source.zip` | a **double bind with no third reading**, below |
| **Arcane Dimensions**, the most acclaimed Quake level design there is | did release source maps — GPL-2-or-later |
| **Quetoo** (38 `.map` sources, the best non-LibreQuake pool in existence) | CC-BY-**SA**-4.0 |
| Xonotic, Nexuiz, OpenArena, Unvanquished, Tremulous, Turtle Arena, Red Eclipse, Warsow | GPL-2 or CC-BY-SA, without a single exception found |
| the `..::LvL` / Quaddicted custom-map catalogue, where the famous high-craft maps actually are | no-commercial / no-derivative readme boilerplate — **stricter** than the GPL already rejected |

**The Romero double bind, because it is a stronger exclusion than "GPL-2".**
The 11 Oct 2006 blog post carried no licence language at all; the GPL was an
amendment days later, the version is stated nowhere by Romero, and the copy
re-uploaded after his site migration lacked `gpl.txt` again — so the artefact
most people hold asserts "id Software © 1996" and grants nothing. Either the
grant is valid, and the maps are copyleft, and a copyleft map baked into this
binary is a copyleft binary; **or it is not valid** — he had been ten years gone
from id, 1996 level design was work-for-hire, and neither id nor ZeniMax nor
Microsoft has ever restated it — **and the maps are unlicensed, which is worse.**
There is no third reading. Corroborated by the best available source:
LibreQuake's own maintainers examined this in their issue #23 and declined to
use them. The project whose maps this game *does* take from looked at the ones
it does not, and reached this verdict first.

**Two permissive labels over non-permissive contents**, recorded because the
shape will recur and a search engine hands you the label, not the licence:

- `jdolan/quetoo-data` — the repository *description*, which is what appears in
  search results, reads "Creative Commons Attribution license". `LICENSE.md` is
  CC-BY-**SA**-4.0. Anyone who greps descriptions rather than licence files gets
  this one wrong.
- `quake-leveldesign-starterkit` — GitHub's badge says CC0-1.0. The CC0 covers
  the kit; the maps inside are Romero's 2006 release. A permissive wrapper
  around GPL contents.

So the ask could not be satisfied as stated, and the honest framing is that
"highest quality **and** most famous" collapses to "highest quality": this is
the best arena that can legally be baked in, not the most famous one, because
the most famous one is not for sale at any price.

### Turning a Freedoom sprite into one of ours

Done, and reproducible: `assets/sprites/import-freedoom.py` rebuilds all 24
frames from Freedoom's own lumps. The PNGs it writes are committed, so the
build needs neither Python nor a network; the script is kept because those
images are the *result* of a conversion and it is the recipe.

| ours | Freedoom | frames taken |
|---|---|---|
| `imp` | `POSS` | A, C, F, G, L |
| `brute` | `BOSS` | A, C, G, H, O |
| `caster` | `HEAD` | A, B, D, F, L |
| `gun` | `SHTG` | B, C, D, C |

The `SARG` row was here too, importing the hound's five frames. That row left
`enemy.c`, so the recipe went with it and the five `hound*.png` it had already
written were deleted — a drawing whose subject matches no monster is ignored
rather than painted over somebody else's, so they were dead weight rather than
a bug. The monster art that ships today is one drawing per creature, not five;
see `assets/sprites/README.txt`.

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
halo, a green shape standing behind the creature and a horn over its head. The
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
- [ ] **Monster patterns, not monster types** — a second attack per kind, a
      readable telegraph, and attacks that move the thing making them. The
      bestiary went five rows to three on the argument that a slower copy of
      something is a stat block rather than a question, so what is left to add
      is what a kind *chooses*, not another kind.
      [docs/MONSTER_PATTERN_PROPOSAL.md](docs/MONSTER_PATTERN_PROPOSAL.md)
- [ ] Eight-view sprites — a monster you can catch facing away from you
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
