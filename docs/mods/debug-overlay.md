# Debug overlay

Draws the room's collision, special cells, triggers, map objects and characters over the running game,
in the colours and shapes of everscript-vscode's map editor (Rooms tab). Package `soe.debug.overlay`
1.0.0, feature `overlay`, plugin `soe.debug.overlay`, off by default. Presentation only.

## Features and options

One boolean per layer, all on by default: Collision, Special cells, Triggers, Map objects, Characters,
Labels.

| Layer | Drawn as (editor's source) |
|---|---|
| Collision | 3-px contour per level in its colour, blue 0, red 1, green 2, violet 3; the walls of the Boy's level tinted red (`overlay-shapes.ts drawContours`, `overlay-features.ts PLANE_COLORS`) |
| Special cells | drift / stairs cyan, see-through violet, character gates 3/5/7 white frame (`collision-overlay.ts` passes 3..6) |
| Triggers | step-on magenta, B yellow, 1-px edge + 1/3 fill, hex script id top-left (pass 9) |
| Map objects | every state of every Section 3 object, blue, object number bottom-left (pass 7) |
| Characters | body box (2r × r, r = character record `+$0D`): blue party and friends, red the rest, dashed asleep (`svg-spawns.js` colours) |
| Labels | the editor's 3×5 font with a 1-px shadow (`font.ts`) |

## How it works

`SoeDebugEndFrame` (`src/soe_debug_overlay.c`) runs in `end_sim_frame` after the widescreen pass and
before the HUD mod draws its HUD back; it blends into the finished field. Field frames only (BGMODE `$39`,
maps `$00`/`$04`, a room loaded, display on). Everything is read each frame: collision from the room grid
and stamp dictionary in `$7F` (stamp word +4), triggers through `$1062`/`$1064` and `$1067`/`$1069`, objects
from the ROM blob (cached per room), characters from the entity lists (drawn a frame late, as the
picture shows them). The facts and their evidence are in the file's head comment and in everscript-vscode's
tags `map.collision`, `map.triggers`, `map.objects`, `map.characters`.

Switches: `SOE_DEBUG_OVERLAY=0|1`, `SOE_DEBUG_LAYERS=collision,specials,triggers,objects,characters,labels`.
In-game chords: `Start + Select` toggles overlay on/off; `Start + L` cycles layer presets (All, Collision, Specials, Triggers, Objects, Characters) with on-screen OSD toasts.

## MesenCE counterpart

None: presentation only, no guest state to compare.

## Checks

| Date | What | Result |
|---|---|---|
| 2026-11-10 | Headless from a save, room `0x51`, two views, 4:3 and 64-px widescreen; overlay on and off | contours on the walls and the fire pit, B-triggers on the pots with their ids, step-on at the exit, objects 5..7, the party and the NPC as friendly bodies; WRAM, VRAM, CGRAM, OAM identical on and off |
| 2026-11-10 | `ctest`, `replay_0x_widescreen` with widescreen, adaptive HUD and debug overlay on | 8 of 8 pass |
| 2026-11-10 | In-game hotkeys and margin clipping | `Start + Select` toggles overlay, `Start + L` cycles presets; widescreen margins clipped via `SoeWsMargins` |
| 2026-11-10 | Active map object state detection | `$7E:10CE,X` singles out active state with solid border; other states drawn dashed |

## Missing

- **Not drawn yet** (the editor has them): cuttable grass, elevation changes (orange), drift arrows,
  dashed "covered by the foreground" boundaries, step-on (bit 14) and interact (bit 15) cells,
  enemy spawn records and the room's enter-script placements, projectiles, attack and hurt boxes.
- **Wall tint** follows the Boy's level, not the room's main level as in the editor; the B-trigger test
  point (moved toward the facing, `$8FAC84`) is not shown.
- **Rooms checked**: only `0x51`. Rooms with camera modes 1/3 (parallax) or heat shimmer, and rooms
  over 1024 px wide, are untested (the camera takes the shadows' high bits, as widescreen does).
