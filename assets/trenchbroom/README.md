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

Then in TrenchBroom: **New Map…** → pick **SFPS** → set its **game path** to
this repository's `assets` directory (`E:\GamePJ\144MB\assets`) → format
**Valve**.

`File → Open…` on `assets\maps\atrium.map` from there.

Copying rather than symlinking, because TrenchBroom overwrites everything under
its own install folder on update and a link into a working tree is a file the
editor can rewrite without anyone meaning it to. The cost is remembering to copy
again after editing the config, which is what this README is for.

## Seeing your edits

Build the tools once (`.\build.ps1 -Tools`), then:

```
.\build\mapview.exe atrium
```

Leave it open beside the editor. It watches `assets\maps\atrium.map` and
rebuilds when TrenchBroom saves, so the loop is save → look, with no compile in
between. A save caught half-written leaves the last good map on screen rather
than blanking it.

`mapview` flies; it does not collide. Brush collision does not exist yet.

## What works today, and what does not

| | |
|---|---|
| Brushes, planes, face polygons | yes |
| Valve 220 per-face UV axes | yes — this is the reason for the whole format change |
| Standard-format faces | read correctly, but the config will not let you save them |
| `worldspawn`, `info_player_start`, `func_door` | parsed; `func_door` does not move yet |
| Monsters, pickups, lights | not read from `.map` at all yet |
| Collision against brushes | not yet |
| Static light baked into brushes | not yet — `mapview` shows ambient plus the shader's key light, so it is flatter than the game will be |

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
