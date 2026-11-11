# Debug overlay

Draws the room's collision, cuttable grass, special cells, triggers, map objects, characters and their
strikes over the running game,
in the colours and shapes of everscript-vscode's map editor (Rooms tab). Package `soe.debug.overlay`
1.0.0, feature `overlay`, plugin `soe.debug.overlay`, off by default. Presentation only.

## Features and options

One boolean per layer, all on by default; in game the number keys toggle them (key in brackets).
Map layers (collision, grass, specials, objects, triggers) lie on the tiles under the sprites; bodies,
strikes and labels on top.

| Layer | Drawn as (editor's source) |
|---|---|
| Collision (1) | 3-px contour per level in its colour, blue 0, red 1, green 2, violet 3; the walls of the Boy's level tinted red (`overlay-shapes.ts drawContours`, `overlay-features.ts PLANE_COLORS`) |
| Special cells (2) | drift / stairs cyan, see-through violet, character gates 3/5/7 white frame (`collision-overlay.ts` passes 3..6) |
| Triggers (3) | step-on magenta, B yellow, 1-px edge + 1/3 fill, hex script id top-left (pass 9) |
| Map objects (4) | every state of every Section 3 object, blue, object number bottom-left (pass 7) |
| Characters (5) | body box (2r × r, r = character record `+$0D`): blue party and friends, salmon the rest, dashed asleep (`svg-spawns.js` colours); bright red with a double edge while charging (mode `+$16 & $C000`: a blocked move deals contact damage, `$8FB52C`) |
| Labels (6) | our own 5×7 hex digits with a 1-px black outline, drawn last over every layer |
| Cuttable grass (7) | cells whose metatile id is a source of the room's swap table (blob section 4): 28 % green fill and a 5-px contour over the walls (`overlay-shapes.ts drawGrass`, fatter); a cut cell gets another id and drops out |
| Strikes (8) | the box of animation command `0x47` (`$9087BA`, centred at the attacker + `dx, dy`), `rgb(255, 51, 85)`, fading over 12 frames (`sprites-motion.js drawTrail`) |

## How it works

`SoeDebugEndFrame` (`src/soe_debug_overlay.c`) runs in `end_sim_frame` after the widescreen pass and
before the HUD mod draws its HUD back; it blends into the finished field. Field frames only (BGMODE `$39`,
maps `$00`/`$04`, a room loaded, display on). Everything is read each frame: collision from the room grid
and stamp dictionary in `$7F` (stamp word +4), triggers through `$1062`/`$1064` and `$1067`/`$1069`, objects
from the ROM blob (cached per room, with the grass sources), characters from the entity lists (drawn a
frame late, as the picture shows them).

**Under the sprites.** No PPU layer data is used (the HUD mod owns the PPU's one OBJ capture). The plugin
decodes OAM (now and at the previous frame's end), the OBJ tiles in VRAM and CGRAM, and leaves a pixel alone
where the frame shows exactly the colour of a sprite pixel covering it. A sprite hidden behind the tiles or
blended by colour math does not match, so lines run over it.

**Strikes without a hook.** A strike lives for one tick and leaves nothing in WRAM, and a hook on `$9087BA`
would need a new `hle` line and a regen. Instead each frame the plugin replays the commands the entity's
last tick read: from the previous resume pointer (`+$00`, bank `+$02`), or from the restart point `+$03`
when an animation began, through the first command with bit 7, both ways at a branch. A path counts only
when it ends where the entity now stands: resume pointer after the last command or after a hold, or
unchanged with the timer `+$05` one lower. A strike on that path is drawn at the attacker's position at the
end of the frame. The facts and their evidence are in the file's head comment and in everscript-vscode's
tags `map.collision`, `map.triggers`, `map.objects`, `map.characters`.

Switches: `SOE_DEBUG_OVERLAY=0|1`, `SOE_DEBUG_LAYERS=collision,specials,triggers,objects,characters,labels`.
Keys (keyboard, while the mod is on): `1`–`8` toggle collision, specials, triggers, objects, characters,
labels, grass, strikes; `0` the whole overlay. Pad chords: `Start + Select` toggles the overlay, `Start + L`
cycles presets (All, Collision + grass, Specials, Triggers, Objects, Characters + strikes). Each change shows
an OSD toast. Toggles live in the plugin: a save state does not keep them.

## MesenCE counterpart

None: presentation only, no guest state to compare.

## Checks

| Date | What | Result |
|---|---|---|
| 2026-11-10 | Headless from a save, room `0x51`, two views, 4:3 and 64-px widescreen; overlay on and off | contours on the walls and the fire pit, B-triggers on the pots with their ids, step-on at the exit, objects 5..7, the party and the NPC as friendly bodies; WRAM, VRAM, CGRAM, OAM identical on and off |
| 2026-11-10 | `ctest`, `replay_0x_widescreen` with widescreen, adaptive HUD and debug overlay on | 8 of 8 pass |
| 2026-11-10 | In-game hotkeys and margin clipping | `Start + Select` toggles overlay, `Start + L` cycles presets; widescreen margins clipped via `SoeWsMargins` |
| 2026-11-10 | Active map object state detection | `$7E:10CE,X` singles out active state with solid border; other states drawn dashed |
| 2026-11-11 | Headless from the save: room `0x51`, village `0x25`, South Jungle `0x38`, a sword swing facing down | lines hidden where the Dog, the Boy and the HUD bars cover them; 5×7 outlined labels; grass in `0x38` as green contours over the walls; the swing's `47 03 0C 23 16` at `$C7:1496` drawn the frame after its tick (the hold left the resume pointer at `$149D`) and fading; no strike in frames without one. Number keys, contact damage (no charging enemy met) and grass after a cut not checked |

## Missing

- **Not drawn yet** (the editor has them): elevation changes (orange), drift arrows,
  dashed "covered by the foreground" boundaries, step-on (bit 14) and interact (bit 15) cells,
  enemy spawn records and the room's enter-script placements, projectiles' hit boxes, hurt boxes.
- **Strikes** from a tick that ran twice in a frame, or from an animation started and ticked past its
  first frame in the same frame, are missed; contact damage also needs speed `+$2E` ≥ `$0400`, which the
  red edge does not check.
- **Wall tint** follows the Boy's level, not the room's main level as in the editor; the B-trigger test
  point (moved toward the facing, `$8FAC84`) is not shown.
- **Rooms checked**: only `0x51`. Rooms with camera modes 1/3 (parallax) or heat shimmer, and rooms
  over 1024 px wide, are untested (the camera takes the shadows' high bits, as widescreen does).
