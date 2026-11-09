# Widescreen progress

Where the work stands. Plan and settings: [README](README.md); research per topic: the other
files here; what the MMX recomp and SoE-Recomp taught us: [other-recomps](other-recomps.md).

## Steps

| # | Step | State | Where |
|---|---|---|---|
| 0 | Research, tags, plugin docs | done 2026-11-09 | this folder; everscript-vscode `docs/entities/off-screen-behaviour.md`, tag `widescreen` |
| 1 | **First approach: field backgrounds** | working, verified headless (below) | `src/soe_widescreen.c` |
| 2 | Gate: menu pages, cutscenes, "player in control" | open | [menus-and-gates](menus-and-gates.md), [cutscenes](cutscenes.md) |
| 3 | HUD placement (vanilla / edges / safe area) | open | [hud-and-adaptive-ui](hud-and-adaptive-ui.md) |
| 4 | Edges option (clip / mirror / extend), small rooms | open (clip to the camera limits is in step 1) | [backgrounds-and-parallax](backgrounds-and-parallax.md), [camera-and-limits](camera-and-limits.md) |
| 5 | Fog of war | open | [fog-of-war](fog-of-war.md) |
| 6 | Hybrid ghosts | open | [modes](modes.md) |
| 7 | Settings as a mod package, pixel aspect, adaptive | open (env variables in step 1) | [README](README.md#settings) |
| 8 | Floating menu window, Mode 7 | open | [menus-and-gates](menus-and-gates.md), [mode7](mode7.md) |
| 9 | Experiments E1..E5 | after the bound C of each consumer | [modes](modes.md), [bindings](bindings.md) |

## Step 1: what the first approach does

Hooks only (`SnesDesktopHostGame.prepare_frame`, `begin_sim_frame`; `native_widescreen = 1`), no
framework change, no guest write.

* **On/off:** config `Widescreen = 1` (the launcher shows the switch). Off, the frame stays 256 px
  and none of the code runs.
* **Width:** `SOE_WS_ASPECT` = `16:9` (default, 342 px), `16:10` (308), `21:9` (448), `adaptive`
  (follows the window); `SNESRECOMP_WS_EXTRA=<px>` pins the margin per side (tests).
* **Gate:** field frames only (BG mode 1, 16 × 16 tiles on BG1 and BG2, camera mode `$241F` = 0);
  everything else is pillarboxed: menu pages, Mode 7, the 9 rooms with other camera modes.
* **Edges:** each margin stops at the active camera-limit box `$23E9..$23EF` (room `0x51` shows one
  hut). Kill switch `SOE_WS_EDGES=0`.
* **Prefill:** BG1 and BG2 margin cells from the room grid in bank `$7F` through
  `WsShadowForceTile` (not `Prefill`: the room set-up's clearing DMA is kept as history that
  Prefill never replaces; `../bugs-and-todos.md`). Shadow reset on room change
  (`$0ADB`). Kill switch `SOE_WS_PREFILL=0`. `SOE_WS_CHECK=1` compares the rule with the game's
  own tilemap words for the cells on screen; `SOE_WS_DEBUG=1` prints the gate and the
  framework's margin statistics every 60 frames.
* **Sprites:** unchanged. The game never places one at X ≥ 256, so enemies appear at the old
  edge on the right; on the left, sprites partly off screen show in the margin. Fixing that
  asymmetry is step 3/6 work (clip OBJ to the 256 px centre or widen on purpose).

## Learnings applied from other recomps

* Margins are clipped to the camera-limit box, not the room width (SoE-Recomp clamps to the room
  and would show the neighbouring hut).
* Room change is detected by the room id, not by the room width (SoE-Recomp's width check
  misses rooms of equal width).
* Nothing is widened that changes the game; enemies keep vanilla timing (MMX had to write a
  policy per object once it widened spawning).
* Pillarbox by default for anything not proven to be a field frame (MMX pillarboxes menus).

## Verification log

| Date | Check | Result |
|---|---|---|
| 2026-11-09 | Guest state with widescreen on vs off: headless from the user's save, hut `0x51` → village `0x25`, walking left and right; WRAM, VRAM, CGRAM and OAM at 7 dumps | identical (presentation only) |
| 2026-11-09 | Tile-word rule: grid word → `$7F:grid+0` (BG1) / `+2` (BG2) against VRAM for the on-screen cells of a settled village frame | 224 / 224 per layer; transition frames differ (the game is mid-stream) |
| 2026-11-09 | Picture, village `0x25` at 16:9 (342 px), frames 2100–2490 | margins drawn edge to edge (columns 0–341), seamless with the centre; a neighbouring hut and its door in the left margin |
| 2026-11-09 | Picture, hut `0x51` (limit box 256 px wide) | pillarboxed; with `SOE_WS_EDGES=0` the neighbouring huts show at both edges, as predicted |
| 2026-11-09 | `ctest` in the shared checkout | replays 01/02 fail at key 420 **with and without** the widescreen hooks, and the shared `build/` without widescreen code fails the same way: another session's uncommitted seeds and bindings. Clean check on the last commit: see the next row |
| 2026-11-09 | `ctest` on the last commit plus only this change (isolated worktree, fresh regen: 28 banks) | 6 / 6 pass, replays 01 and 02 included |
