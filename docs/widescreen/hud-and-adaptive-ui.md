# HUD and adaptive widescreen UI

Tag `widescreen.hud` (the field HUD alone: tag `hud`). Marks: [README](README.md).

**Done for the field HUD:** the adaptive-HUD mod ([../mods/hud.md](../mods/hud.md)) does the
*Edges* row below, plus a top position and hiding, through the PPU's host-overlay capture rather
than the OAM shifter or BG3 split. Measured layout: everscript-vscode `docs/disassembly/ui/field-hud.md`.

## How Evermore draws each element

| Element | Drawn by | Mark |
|---|---|---|
| Portraits | two 16×16 sprites, OBJ tile `$080` (Boy, palette 6) at (8, 201) and `$082` (Dog, palette 7) at (232, 201), in two adjacent OAM slots that move with the menus (0/1 in the field, up to 11/12 under a ring menu); records set by `$8F:810E`. The positions the `$90:8CEE`/`$90:8D2D` entries give, (224, 16) and (96, 162), match no sprite measured | V (measured 2026-11-10) |
| Boxes: HP numbers, charge gauge, frame | BG3 map rows 24..27 (lines 193..223), columns 3..12 Boy and 19..28 Dog, palette 2, priority; glyphs `$3E0..$3FF`. Written to VRAM from the widgets `$051D`/`$0561` (`+$06` is the gauge glyphs' VRAM address `$1F00`/`$1F20`, not a tilemap address); `$7F:C800` holds blanks at those rows | V (measured 2026-11-10) |
| Dialogue windows, choice boxes, shop lists | BG3 from the shadow tilemap `$7F:C800`, OAM window borders (`$80:D22D`) | U |
| Ring menu | sprites on an ellipse around the calling actor `$05A7`, angle `$061B` | U (`docs/disassembly/ui/ring-menu-sprites.md` in everscript-vscode) |
| Damage popups | world sprites (world − `$59`/`$5B`, `$8C:888F`) | V |
| Target / alchemy cursors | sprites on the target (`$8D:B1B5`) | U |
| Full-screen pages | BG tilemaps at scroll 0, drawn into the room's VRAM | floating window over a snapshot, or pillarbox: [menus-and-gates](menus-and-gates.md#floating-menu-window) |

## UI placement setting

| Option | Boy group | Dog group | Dialogue | Ring menu, popups, cursors |
|---|---|---|---|---|
| **Vanilla** | 4:3 position | 4:3 position | centred | unchanged |
| **Edges** | moved to the left screen edge | moved to the right screen edge | centred | unchanged (world-relative) |
| **Safe area** | anchored to a safe-area rectangle inset by N px | same | centred | unchanged |
| **Custom** | X offset per group | per group | centred, or offset | unchanged |

## How, renderer-side (no game state)

* HUD sprites: `PpuSetWsHudOamBand` + `PpuSetWsHudOamShiftRange` (`snes/ppu.h` ~794–816) shift
  edge-hugging OAM entries by the margin. They expect fixed OAM slots; Evermore's portraits are
  always adjacent but not in fixed slots (measured), so the slots are found per frame by tile.
  The adaptive-HUD mod uses the overlay capture instead, which also moves them vertically.
* BG3 text: `PpuSetWidescreenHudSplit(height, left_end, right_start)` (`ppu.h:781`) moves the left
  and right chunks of the top `height` lines to the edges; for a HUD at the bottom, anchor/elastic
  bands per scanline range (`PpuSetWidescreenLayerElasticSplitBandSlot`, `ppu.h:1013`). Measure the
  column ranges with `SNESRECOMP_LAYER_MASK=4` (BG3 only) first.
* `PpuSetWidescreenHudAlwaysVisible` (`ppu.h` ~788) keeps HUD pixels inside pillarboxed margins
  (small rooms, camera-limit boxes).

## What breaks

* The ring menu must follow the Boy: never move it with the screen edges.
* Dialogue windows stay centred; moving them gains nothing.
* Popups and cursors are world objects.
* A HUD element that is hidden by a window or brightness fade must stay hidden at its new place:
  brightness is global ([fades](fades.md)); windows need checking.

## Interactions

* Fog of war: HUD is drawn after the fog and never fogged ([fog-of-war](fog-of-war.md)).
* Enemy modes: the UI setting is presentation in every mode; ghosts are drawn under the HUD.
* Menu pages: pillarboxed, the HUD setting does not apply ([menus-and-gates](menus-and-gates.md)).
