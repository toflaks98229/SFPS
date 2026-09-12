# SFPS on the web — build output only

This branch is **not source**. It holds three files and nothing else, and every
one of them is generated:

| file | what it is |
|---|---|
| `index.html` | the page, from `web/shell.html` through emscripten |
| `index.js` | the loader emscripten writes |
| `index.wasm` | the game, assets baked in |

**The source is on `main` and `feature/webgl-port`.** Do not edit anything here;
edit `web/shell.html` or `src/` and run `build_web.ps1`, which writes
`build/web/`. This branch is that directory, committed.

**Why a branch rather than `docs/` on `main`.** `build/` is in `.gitignore` for
the reason `.gitignore` states — build output is regenerated, not kept — and a
1MB wasm landing in `main`'s history on every rebuild is exactly what that rule
exists to prevent. An orphan branch keeps the weight off the history anybody
reads.

**Why not a GitHub Actions workflow.** That is the better answer and it wants
one thing this branch does not: a workflow committed here could not be tested
before it ran. The three files below were built and opened in a browser first.
If this branch's weight ever becomes a problem, the workflow is the fix.

## What it needs from a host

Only that `.wasm` is served as `application/wasm`, which GitHub Pages does. No
headers, no COOP/COEP, no server configuration — the port does not use threads,
and `docs/WEBGL_PROPOSAL.md` §6 collects that argument.

## Known limits

- **Mouse and keyboard only.** WASD and mouse look; there is no touch control,
  and a device with no precise pointer is told so rather than handed a canvas it
  cannot play.
- **Pointer lock** is how the camera is steered. Where a host refuses it the
  game still runs and still shoots, but cannot be aimed.
