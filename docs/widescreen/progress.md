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
| 10 | Own field renderer: any width **and height**, full control of what the view shows | open | below |

## Step 1: what the first approach does

Hooks only (`SnesDesktopHostGame.prepare_frame`, `begin_sim_frame`; `native_widescreen = 1`), no
framework change, no guest write.

* **The rule:** vanilla widescreen must leave WRAM exactly as without it; only how the recomp
  draws may change. `ctest` holds it: `replay_01_widescreen` and `replay_02_widescreen` run the
  stored MesenCE runs with widescreen forced on at 21:9 (`SOE_WIDESCREEN=1`,
  `SNESRECOMP_WS_EXTRA=96`), every byte of WRAM, VRAM, CGRAM and OAM at every 60th NMI.
* **On/off:** config `Widescreen = 1` (the launcher shows the switch), or `SOE_WIDESCREEN=1|0`.
  Off, the frame stays 256 px and none of the code runs.
* **Any width:** by default the frame follows the window's shape (resize it and the view
  follows); `SOE_WS_ASPECT=W:H` fixes any aspect (16:9, 21:9, 32:9, 5:4 ...), `SOE_WS_WIDTH=<px>`
  any width, `SNESRECOMP_WS_EXTRA=<px per side>` pins it for tests. The pixel shape comes from
  `DisplayAspect` (4:3 CRT, 8:7, 1:1): 16:9 is 342 px with CRT pixels, 398 px with square ones.
  Limit: the PPU draws at most 800 px (272 per side); wider windows are pillarboxed.
* **Window:** the host opens the window before the first frame picks its width, so
  `window_base_width` gives it the widescreen shape (a 1284 × 720 window at scale 3 for 16:9).
  Without it the window opened 4:3 and the wide frame was letterboxed into it: it "did not look
  wide" (the first user test, 2026-11-09).
* **Scroll phase from the PPU (rule P1):** margins use the scroll the PPU draws this field with
  (its 10-bit registers, high bits from the shadows). The WRAM shadows `$010E..$0114` already hold
  the next frame's camera, so using them put the margins one frame ahead of the centre: a seam
  while scrolling (seen by the user in the village).
* **Black outside:** margin columns the PPU is not asked to draw (room or limit edge, pillarbox)
  are blacked in `end_sim_frame`. Left alone they showed whatever the PPU composed there, e.g.
  a room's per-line colour bands (HDMA colour math) as smeared rows.
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

## Any resolution: where the framework stops

The PPU path draws up to 800 × 224 (+16 lines of overscan); the host clamps a frame to 240 lines.
So any *width* works now, but more *height* (a taller view of the room, a free zoom, 4K at native
pixels) needs our own field renderer, as MMX built one (`mmx_renderer.c`): draw BG1/BG2 from the
room grid in `$7F` and the tiles in VRAM, sprites from OAM (later from the entity slots for
ghosts), into a frame of any size through the host's `draw_frame` hook. Still presentation only:
it reads WRAM, VRAM, CGRAM and OAM and writes nothing. That is also where "full control of the
viewport" lives (fog, ghosts, HUD placement, floating menus become passes of one renderer).

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
| 2026-11-09 | Window shape, any width: compiled; headless `SOE_WS_ASPECT=32:9` gives a 684 px frame (214 px margins), the village margin filled, cut at the room's right edge | works; `replay_0x_widescreen` registered, **not run yet** (long run skipped while another session uses the machine) |
| 2026-11-09 | Seam while walking (village, 16:9): best vertical alignment of the last margin column with the first 4:3 column | old build: 3 lines off at the left border; new: 0 at both borders, lower mismatch (3581 vs 6307, 8870 vs 10334) |
| 2026-11-09 | Guest state, widescreen on (21:9) vs off, new build, 4 dumps | identical |
| 2026-11-09 | Smeared rows past the map (user's screenshot, a room with HDMA colour bands) | blacked by construction; not reproduced headless (no such room on the test route) |
