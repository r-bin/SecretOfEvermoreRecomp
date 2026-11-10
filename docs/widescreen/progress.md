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
| 4 | Edges option (clip / mirror / extend), small rooms | open; in step 1: clip to the camera limits, and the foreground (BG1) clipped to them on its own | [backgrounds-and-parallax](backgrounds-and-parallax.md), [camera-and-limits](camera-and-limits.md) |
| 5 | Fog of war | open | [fog-of-war](fog-of-war.md) |
| 6 | **Vanilla extended**: entities drawn in the margins from WRAM; then ghosts for not-yet-active entities | entities done (`src/soe_ws_sprites.c`), never-animated ones in their standing frame; ghosts open | [modes](modes.md) |
| 7 | Settings as a mod package, pixel aspect, adaptive | done: package `soe.enhancement.widescreen`, one feature per mode (each with its own options; the experimental renderer gets a third); more options as their steps land | `mods/preloaded/packages/`, [README](README.md#settings) |
| 8 | Floating menu window, Mode 7 | open | [menus-and-gates](menus-and-gates.md), [mode7](mode7.md) |
| 9 | Experiments E1..E5 | after the bound C of each consumer | [modes](modes.md), [bindings](bindings.md) |
| 10 | **Experimental**: own field renderer, any width **and height**, full control of what the view shows; enemies by E6 (whole room alive, 6 in the 4:3 view) | renderer open; E6 built (launcher *Widescreen (experimental)*: extended drawing + E6), the whole room awake at full speed (free time) | below; [enemies](enemies.md#the-whole-room-alive-six-in-the-43-view-experiment-e6) |

## Step 1: what the first approach does

Hooks only (`SnesDesktopHostGame.prepare_frame`, `begin_sim_frame`; `native_widescreen = 1`), no
framework change, no guest write.

* **The rule:** vanilla widescreen must leave WRAM exactly as without it; only how the recomp
  draws may change. `ctest` holds it: `replay_01_widescreen` and `replay_02_widescreen` run the
  stored MesenCE runs with widescreen forced on at 21:9 (`SOE_WIDESCREEN=1`,
  `SNESRECOMP_WS_EXTRA=96`), every byte of WRAM, VRAM, CGRAM and OAM at every 60th NMI.
* **On/off and settings: the mod package `soe.enhancement.widescreen`** (launcher → Mods →
  Display → Widescreen; `mods/preloaded/packages/soe.enhancement.widescreen/1.0.0/manifest.toml`).
  Default off. Options: **View aspect ratio** (adaptive, 16:10, 16:9, 21:9, 32:9) and **Room
  edges** (camera limits, whole room). The plugin `soe.widescreen` is registered as
  presentation-only (`snes_mod_register_presentation_plugin`): it touches no CPU, WRAM, VRAM,
  OAM, CGRAM, APU or save state, which the widescreen replays check. A reset callback makes the
  mod the only switch: the legacy `Widescreen` line in `config.ini` and the launcher's legacy
  toggle (`widescreen_supported`, now 0) do nothing. The window is sized after the mod
  selection is committed, so it opens wide when the mod is on. Test overrides that win over the
  mod: `SOE_WIDESCREEN=0|1`, `SOE_WS_ASPECT`, `SOE_WS_WIDTH`, `SNESRECOMP_WS_EXTRA`,
  `SOE_WS_EDGES=limits|room|off`.
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
* **Gate:** field frames only: the exact register set the field set-up `$8E:E08B` writes
  (BGMODE `$39`, maps BG1 `$00` / BG2 `$04` / BG3 `$08`, BG12NBA `$22`), camera mode `$241F` = 0
  and a camera-limit box at least 256 px wide. Everything else is pillarboxed: intro and logo
  screens, menu pages, Mode 7, the 9 rooms with other camera modes. (A looser gate, "mode 1 with
  16 × 16 tiles", let the Squaresoft logo through: BGMODE `$79`, both BGs on map `$00`; its
  texture layer showed unmasked in the left margin.) The title screen is drawn over a real
  room, so it widens; whether title and cutscenes should is step 2.
* **Edges:** each margin stops at the active camera-limit box `$23E9..$23EF` (room `0x51` shows one
  hut). Kill switch `SOE_WS_EDGES=0`.
* **Prefill:** BG1 and BG2 margin cells from the room grid in bank `$7F` through
  `WsShadowForceTile` (not `Prefill`: the room set-up's clearing DMA is kept as history that
  Prefill never replaces; `../bugs-and-todos.md`). Shadow reset on room change
  (`$0ADB`). Kill switch `SOE_WS_PREFILL=0`. `SOE_WS_CHECK=1` compares the rule with the game's
  own tilemap words for the cells on screen; `SOE_WS_DEBUG=1` prints the gate and the
  framework's margin statistics every 60 frames.
* **Each layer at its own scroll:** BG1 is not always at the camera. In `0x31` (Podunk 1965,
  camera mode 2) it runs 271 px right and 11 px down of BG2 (frame 3000 of replay 02: camera 723/8,
  BG1 994/19), so the hedges in front come from other grid cells than the ground. The margins are
  filled per layer from that layer's drawn scroll; filled at the camera's cells, BG1's margin kept
  the cleared ring and the hedges stopped at the old edge (user's screenshot).
* **Foreground** (option on both modes): BG1 (canopy, hedges, roofs, the front rim of a hut) is
  cut to the camera-limit box `$23E9..$23EF` moved by BG1's offset from the camera, i.e. to what
  BG1 can show in the game; BG2 goes on. Cells outside the box or the grid get the word the room
  set-up clears both maps with (`$7F:8000`, transparent on BG1). *As far as the terrain* draws BG1
  wherever the grid has it. `SOE_WS_FOREGROUND=limits|terrain`.
  Parallax rooms (camera mode 1, room `0x4B`) are still pillarboxed; there BG1 needs its own scroll
  and authored area ([backgrounds-and-parallax](backgrounds-and-parallax.md)).
* **Modes** (one launcher feature each, *Widescreen (vanilla)* and *Widescreen (vanilla extended)*;
  both claim the plugin `soe.widescreen`, so enabling one turns the other off; both keep WRAM
  identical to no widescreen):
  * **Vanilla:** sprites exactly as the game draws them. The game never places one at X ≥ 256 and
    lets far entities sleep, so characters appear at the old edge.
  * **Vanilla extended** (default): `src/soe_ws_sprites.c` also draws, in the margins only, every
    entity of the awake and asleep lists (`$3DDF`, `$3DE1`), the Boy, the Dog and the projectiles
    (`$6387`), from their current frame pointer (`+$06`/`+$09`), palette slot (`+$0C`), hide flags
    (`+$12`) and the canopy priority of `$8F:C773`. The frame is decoded from the ROM (sprite record →
    chunks → 8×8/16×16 blocks, as `$80:9033` does; everscript-vscode `maps/sprites.ts`) and handed to
    the PPU as extra objects (`PpuSetExtraObjects`): composed like OBJ under every native entry, with
    live CGRAM colours. Drawn from the snapshot of the previous frame, because that is the state the
    drawn field shows. Map objects, opened chests and cut grass are tiles of the room grid and come
    with the margin prefill. Option *Characters in the margins*: Colour, or Greyscale (Rec. 601 luma
    of the live CGRAM colour, so fades still apply), which marks what the game itself does not draw;
    `SOE_WS_LOOK=grey`. Entities the camera has not reached yet wait in the asleep list with sprite
    pointer 0 (they are not hidden: `+$12` is clear), so the villagers popped in at the old edge.
    They are drawn in their standing frame: record `$8E:(+$60)`, stand id `+$32`, animation record
    `$C4:id` at facing `+$22` (`$90:8124`), the script's first set-sprite command; their palette
    slot `+$0C` is already set, so live CGRAM colours apply.
  * **Experimental:** vanilla extended plus E6 ([enemies](enemies.md#e6-as-built)): the room alive around the wide
    view, the whole room awake (its logic in free time: no emulated time), enemies outside the 4:3 view do not aggro
    while it holds 6. Its own plugin `soe.widescreen.alive` (ordinary, it changes the
    game). Step 10's own renderer is still open.
  `SOE_WS_MODE=vanilla|extended` overrides; `SOE_WS_SPRITES_CENTER=1` draws the extras in the centre
  too (under native OAM) and `SOE_WS_SPRITES_SHIFT=<px>` shifts them, to check placement.

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
| 2026-11-09 | Window shape, any width: compiled; headless `SOE_WS_ASPECT=32:9` gives a 684 px frame (214 px margins), the village margin filled, cut at the room's right edge | works; `replay_0x_widescreen` registered (run below) |
| 2026-11-09 | Seam while walking (village, 16:9): best vertical alignment of the last margin column with the first 4:3 column | old build: 3 lines off at the left border; new: 0 at both borders, lower mismatch (3581 vs 6307, 8870 vs 10334) |
| 2026-11-09 | Guest state, widescreen on (21:9) vs off, new build, 4 dumps | identical |
| 2026-11-09 | Smeared rows past the map (user's screenshot, a room with HDMA colour bands) | blacked by construction; not reproduced headless (no such room on the test route) |
| 2026-11-09 | Gate, intro to village (frames 120–2490 every 60) | logo screens pillarboxed (both margins black); intro room, title over its room and the village widened; guest state on vs off identical |
| 2026-11-09 | Mod package, headless with a `state.toml`: no selection / 21:9 + camera limits / adaptive + whole room (hut `0x51`) | 256 px (off) / 448 px / 342 px with the neighbouring hut in the left margin, black past the room's right edge |
| 2026-11-09 | `ctest -R replay` on `86f1dc1` (with the bindings of that commit) | `replay_01_recorded`, `replay_01_widescreen`, `replay_02_recorded`, `replay_02_widescreen` pass: widescreen at 21:9 matches MesenCE byte for byte through all 396 checkpoints of 02. Wall time varies with load (02: 35 s alone, 337 s / 982 s beside a running game; ~55 s CPU each) |
| 2026-11-09 | Village walk from the user's save (left, up, right, down), 16:9, whole room | game advances (`$0B19` 36577 → 37561), on vs off identical at 5 dumps, picture clean. The garbage/freeze seen earlier came from the uncommitted bindings of the time, not widescreen |
| 2026-11-09 | Vanilla extended, village walk: guest state vs widescreen off (5 dumps; vanilla, extended, extended with centre check) | identical |
| 2026-11-09 | Placement: extras drawn in the centre under native OAM vs the vanilla picture, 7 frames | 0 differing pixels; control with a 8 px shift: 361–1083 per frame. Position, frame timing, palette and priority match the game's own sprites |
| 2026-11-09 | 32:9 village frame, vanilla vs extended | extended shows a villager in the left margin that vanilla does not draw |
| 2026-11-09 | Foreground option, hut `0x51`, whole room, 21:9: Camera limits vs As far as the terrain | 2880 pixels differ, all at screen x 0–63, left of the box edge (x 96): the neighbouring hut's front layer is cut, its terrain stays; WRAM identical |
| 2026-11-09 | Never-animated entities, village at 32:9 (14 of 17 listed entities had sprite 0) | a villager and a small creature stand in the left margin; WRAM identical |
| 2026-11-09 | Flower room `0x38` and jungle `0x31` (camera mode 2) through replay 02 | were pillarboxed (gate: camera mode 0 only); now wide, the wave continuous across x 96; replay 02 in sync (396 checkpoints) |
| 2026-11-09 | Mode 7 (replay 02, room `0x46`, frames 20400–20900), user report: black window | software path draws it pillarboxed; the black is the GPU HD Mode 7 path (`Mode7Enabled`) with a widened frame, not reproducible headless (no GL). Mode 7 frames are now 256 wide, identical to widescreen off (dump compared); on screen not yet confirmed |
| 2026-11-09 | Podunk 1965 (`0x31`), user's screenshot: hedges cut at the old right edge | BG1 runs 271 px off the camera; per-layer prefill: hedges continue into both margins; replay 02 in sync with both foreground settings; hut and village frames byte-identical to the build before |
| 2026-11-09 | Flower room `0x38` after landing (user: foliage cut at the right 4:3 edge, a colourful tile top right) | build before `194f06d`: BG1 runs +102/+239 px off the camera there, its margin kept stale ring cells. With per-layer fill (VRAM ring = grid at BG1's scroll, 168 of 168 cells) the foliage continues and no stray tile shows (16:9 and 21:9, vanilla and extended). Ruled out: BG3 (not widened unless a game asks, `PpuWidescreenLayerExtra`), native OAM parked at X ≥ 256 (one sprite in all of replay 02, room `0x03`), windows and colour math (none set) |
| 2026-11-09 | Mode 7 still black on the user's Mac (HD Mode 7 on); live session read with lldb | the game was paused at the start of its fade-in ($0106 = 0, $0B83 = $800F; unpaused it rose to $0E), the PPU drew the scene. Headless, the HD capture of the flight rendered by the CPU path at 2x is a clean picture with either renderer, so the data is fine. Mode 7 fields now run with nothing meant for other modes: the framework's own renderer (the reference renderer is the glue's mode-4 workaround and had come back because a 256-wide frame ends the host's widescreen renderer override), no widescreen settings and no extra sprites. Replays 01 and 02 in sync. Still black on screen; with `g_config.mode7.enabled` cleared in the live session the flight showed: the GPU presenter of HD Mode 7, not widescreen (bugs-and-todos §3 item 12) |
| 2026-11-09 | Test harness | two replay runs of one build at once fail ("cannot publish mod state"): they share `build/mods/preloaded/state.toml`; run them one after another |
| 2026-11-09 | Greyscale, village walk at 32:9 | the margin villager is grey, the centre unchanged; WRAM identical to Colour at 3 dumps. Replays not rerun (no long runs while the C bindings are being worked on) |
| 2026-11-09 | E6: `ctest` replays 01/02, recorded and widescreen, with the `$8F:C40F` hook in and E6 off | 6 / 6 pass |
| 2026-11-09 | E6 on, replay 02 (rooms `0x31`, `0x02`, `0x32`, `0x03`, `0x46`) | runs to the end, same rooms, 5 field frames behind; hook took the E6 path 6002 times |
| 2026-11-09 | E6 on, replay 06 in the flower field `0x38` (cap 6 and 3), and through the launcher's feature (no env) | [enemies](enemies.md#e6-as-built): all awake, cap held, counts consistent; game logic at ~55 % speed (now the *Always* option) |
| 2026-11-09 | E6 adaptive radius (superseded the same day by free time), replay 06 in `0x38` through the launcher feature (isolated copy of the build with its own `state.toml`), 16:9 | 8–14 awake, `$0B19` +1189 per 1200 frames (vanilla +1195) |
| 2026-11-09 | E6 with free time (`$8E:E45C`..`$E4B0`), replay 06 in `0x38` and replay 02, through the feature; `ctest` replays 01/02 with E6 off | 19–21 awake, `$0B19` +1200 per 1200 frames, host 668 fps (675 off); replay 02 same rooms, no runaway; frames clean; ctest passes |
| 2026-11-09 | `ctest` while the user played with *Widescreen (experimental)* selected | replays 01/02 failed at key 420: the tests read the launcher's shared `state.toml`, so E6 was on. `tests/replay_recorded.sh` now sets `SOE_WS_ALIVE=0`; all 4 pass with the selection on, and from an isolated copy with it off |
| 2026-11-09 | User test: villagers turned grey and inactive (the hold at a full 4:3 view) | holding removed; the cap now limits aggro instead (`$8F:D879`, `$D69B`, `$D715`). Replay 06 in `0x38`: 17–20 awake, none asleep, view 2–6, full speed; replay 02: none asleep, no runaway; `ctest` 6/6 |
| 2026-11-09 | User: spawner enemies still pop in | spawner `$8F:DDBB` hooked: the room fills during the fade-in, later spawns only out of sight, density 4 per 4:3 box; synthetic records in `0x38` behave so; `ctest` 6/6 |
| 2026-11-10 | User: too many enemies at once, the pop-in looks odd | vanilla spawner timing back, density gate at `$8F:DE43` (4 spawned enemies per 4:3 box), ghosts for records still to spawn; synthetic records in `0x38`: stops at 4, ghost in the 32:9 margin; `ctest` 6/6 |
| 2026-11-10 | User: no pop-in while the 4:3 view holds 6; spawn on load unless the point's view is full; ghosts fade at the 4:3 edge | load spawning at `$8F:DDBB` (box not full), spawn step `$8F:DE43` gated by box and 4:3 crowd, dithered ghost fade; synthetic records in `0x38` behave so (frame dumps); `ctest` 6/6 |
| 2026-11-10 | User: crowd in the flower field although "spawners" should be ghosts | those were placed enemies (`0x38` has no spawn records): all 7 Mosquitoes chased the Dog from across the room while the 4:3 view held < 6. Now outside chasers count towards the cap (nearest keep their place): 3 chase, 4 let go. Placed enemies whose box is crowded on the load are held asleep as ghosts: 6 Wimpy Flowers poked into one box, 4 taken, 2 held (ghosts in the 32:9 margin); `ctest` units + replay 01 pass |
| 2026-11-10 | User: the screenshot was the east jungle `0x5b` (10 Wimpy Flower and 4 Skelesnail records), reached from a copy of the user's save | with a box of one 4:3 view, 17 of 18 spawned on the load; the box is now every view that could hold the point (256 × 224 either way): 11 spawned, 7 ghosts, 1–2 enemies in the 4:3 view near the desert exits, ghosts in the 32:9 margin; `ctest` units + replay 01 pass |
