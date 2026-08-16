# Editing SFPS levels in TrenchBroom

`assets/maps/*.map` is the source form of a level. TrenchBroom writes it and
`src/brush.c` reads it, with no converter in between — see the header of
`src/brush.h` for why that is the whole point.

## Install

TrenchBroom reads user game configurations from `%APPDATA%\TrenchBroom\games\`.
Copy the folder beside this file into it:

```powershell
$dst = "$env:APPDATA\TrenchBroom\games"
New-Item -ItemType Directory -Force $dst | Out-Null
Copy-Item -Recurse -Force .\assets\trenchbroom\SFPS $dst
```

Then set the **game path** for SFPS to this repository's `assets` directory.
TrenchBroom stores it in `%APPDATA%\TrenchBroom\Preferences.json` as

```json
"Games/SFPS/Path": "E:/GamePJ/144MB/assets"
```

which you can also set through **Preferences → Games → SFPS**. Without it the
console reports `Could not reload material collections: Path sprites does not
denote a directory` and every face draws untextured.

After that, opening `assets\maps\atrium.map` needs no dialog at all: the file
begins with

```text
// Game: SFPS
// Format: Valve
```

and TrenchBroom reads those to pick the game and the format itself. If you get
a **Select Game** dialog instead, the profile did not load — see below.

Copying rather than symlinking, because TrenchBroom overwrites everything under
its own install folder on update and a link into a working tree is a file the
editor can rewrite without anyone meaning it to. The cost is remembering to copy
again after editing the config, which is what this README is for.

## If SFPS is not in the game list

A game configuration TrenchBroom cannot parse is **dropped in silence**. There
is no error, no log line and no entry in the list — the game simply is not
there, which looks exactly like installing it in the wrong folder.

Two things were wrong with the first version of this profile, and both are worth
knowing because neither announces itself:

**Two tags may not share a name.** `tags.brush` and `tags.brushface` are
separate lists but one namespace. This profile had a brush tag `Trigger`
matching `trigger*` classnames and a face tag `Trigger` matching the `trigger`
material, and the collision threw the whole config away. Quake's own builtin has
brush tags Detail/Trigger/Func and face tags Clip/Skip/Hint/Liquid — no
repeats, and that is not a coincidence. The face tag here is called
`Trigger surface`.

**Keep it ASCII, and start at the brace.** The first version opened with a
block of `//` commentary before the root `{` and carried a few thousand bytes of
non-ASCII in its comments. Every builtin config starts with `{` and every one is
pure ASCII. Comments *inside* the object are fine — Quake's has one — but the
prose belongs here, in a file nobody else has to parse.

To find which of these it is, copy a builtin config from
`<TrenchBroom>\games\Generic\` into `%APPDATA%\TrenchBroom\games\Probe\`, change
its `name` to `"Probe"`, and see whether **Probe** appears. If it does, the
mechanism works and the fault is in this profile's content; bisect by deleting
blocks from it until it appears.

## Seeing your edits

Build the tools once (`.\build.ps1 -Tools`), then:

```text
.\build\mapview.exe atrium
```

Leave it open beside the editor. It watches `assets\maps\atrium.map` and
rebuilds when TrenchBroom saves, so the loop is save → look, with no compile in
between. A save caught half-written leaves the last good map on screen rather
than blanking it.

`mapview` flies rather than walks. That is the viewer's own choice, not a
missing feature — brushes collide in the game. To play the map instead of
looking at it, put it in the campaign (below) and run `game_dev.exe`.

## Getting a map into the build

Two separate things, and only the first is automatic.

**Baking is automatic.** `bake.ps1` sweeps `assets\maps\*.map` on every build,
deflates each one and writes it into `src\gen_assets.h`, so `.\build.ps1` is the
whole of it. `game.exe` then carries the map with nothing beside it to load —
the size report names each map and what it cost:

```text
maps\atrium.map               17961  10654 41%
```

**Reaching it is not.** A baked map that nothing names is in the binary and
unreachable in play. Levels are a chain: each names the next, and the one with
no `next` ends the game on its exit. `level_load` looks for a `.map` **before**
it looks in `assets\levels.txt`, so a name resolves to your brush level if one
exists and to a sector level if one does not — which is what lets a `.map`
replace a sector level by taking its name.

Three ways to be on the chain, in the order you will want them:

| Route | How | Use it when |
| --- | --- | --- |
| Link it in | set some level's `next` to your map's name, and your map's `worldspawn` `next` to whatever follows | the normal case |
| Make it first | change `WORLD_START_LEVEL` in `src\world.h` | a map is the only thing you are testing |
| Take a name | call the file `arena.map` and it wins over `levels.txt`'s `arena` | replacing a sector level in place |

`atrium` is linked in as the second stage: `assets\levels.txt` gives `arena` a
`next atrium`, and atrium's own worldspawn carries `next vault`. Nothing else
had to change.

`build\leveltrans.exe` walks the whole chain from the start level and checks
each hop — that it loads, that it has geometry, that it has an exit, that the
exit is clear of the spawn, and that the chain ends rather than loops. Run it
after editing `next` anywhere; a typo'd name is otherwise a level that loads a
void, and it looks exactly like an exit that does not work.

## What works today, and what does not

| Capability | State |
| --- | --- |
| Brushes, planes, face polygons | yes |
| Valve 220 per-face UV axes | yes — this is the reason for the whole format change |
| Standard-format faces | read correctly, but the config will not let you save them |
| Collision against brushes | yes — `brush_trace` sweeps the player box, and the same call answers hitscans and the hook |
| `worldspawn` (`next`), `info_player_start` | yes |
| `func_door` | yes, and it moves: the leaf slides on `angle`/`speed`, and `key` locks it |
| `trigger_multiple` / `trigger_once` (`target`) | yes — a volume you walk into, which fires the door naming it |
| `trigger_hurt` | yes |
| `light` (`light`, `_color`) | yes, and baked per vertex against the brushes it stands among |
| `monster_*`, `monster_spawner_*`, `item_*` | yes — read by prefix, so a new monster is an FGD line and a sprite |
| `info_exit`, `info_push` | yes |
| Procedural materials previewed in the editor | no — see below |

Everything in that table is exercised headlessly by `build\maptest.exe` and
`build\tracetest.exe`; the ones about entities are also visible in
`build\leveltrans.exe`'s walk.

## Textures are half there

The material browser is pointed at `assets/sprites`, which holds the hand-drawn
wall art as PNG: `wall_brick`, `wall_door`, `wall_marble`, `wall_metal`,
`wall_plain`, `wall_rough`, `wall_stone`, `wall_track`. Those display correctly
and are what `atrium.map` textures its walls and its door with.

The other materials — `ptile`, `ppanel`, `pgrid`, `pmarble`, `prust`, `phex`,
`plava` — are **recipes**, not images. `assets/textures.txt` describes them and
`src/tex.c` renders them at load time, so there is no file for the editor to
show and they appear as missing. Type the name onto a face and it will be right
in the game; you just cannot see it in the editor yet.

Fixing that means a tool that runs the recipes headlessly and writes an image
per material into a directory of its own. That is real work and it is not done.

## Scale

`1 unit = 1/32 m`, so **grid 32 is one metre** and grid 64 is two. A player is
58 units tall and 22 across, which is near enough to Quake's 56 that its
dimensions transfer: a 64-unit doorway, a 128-unit corridor and a 16-unit step
are all still the right size here.

Set the **default face scale to 0.5**. At scale 1.0 a 128px texture spans 4m; at
0.5 it spans 2m, which is the texel density the rest of the game already uses.

`src/brush.h`'s `BRUSH_UNIT` records the argument for 1/32 over the
alternatives, and is the one number to change if this ever needs revisiting.

## Limits, and how you find out you hit one

Nothing here fails on a map that is too big — the surplus is **dropped**, which
is the right behaviour for a size-bound game and an invisible one for an author.
So each limit has a counter, and a dev build prints them in the window title
with a `!` in front:

```text
! DROPPED brush ent | SFPS 60fps | ...
```

| | limit | counter |
| --- | --- | --- |
| Brushes per map | 512 | `brush` |
| Faces, all brushes | 4096 | `brush` |
| Faces on one brush | 32 | `brush` |
| Entities in the `.map` | 96 | `mapent` |
| Entities the level keeps | 64 | `ent` |
| Lights | 64 | `light` |
| Doors | 16 | `door` |
| Trigger volumes | 16 | — |
| Hurt volumes | 8 | — |

A brush whose faces do not close a solid is dropped too, and counted as
`unclosed` — that is the one on this list you can cause by dragging a face
rather than by building something large.

One more that is not a count: **the file must be ASCII.** `bake.ps1` throws
rather than baking a map with a non-ASCII character in a name, key, value or
texture name — silently replacing it with `?` would keep the byte length right
and make the content wrong, which shifts every map baked after it. Comments are
stripped before the check, so this only ever fires on something you can fix.
