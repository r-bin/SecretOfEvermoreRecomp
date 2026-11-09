# HUD and adaptive widescreen UI

Tag `widescreen.hud`. Marks: [README](README.md).

## How Evermore draws each element

| Element | Drawn by | Mark |
|---|---|---|
| Boy/Dog gauges, portraits | animation-script sprites through `direct_sprite_draw_oam` (`$80:9033`) at fixed screen positions: (224, 16) in `$90:8CEE`; (232, 20) when `$05AB` bit 0 and (96, 162) in `$90:8D2D`; portraits set up by `$8F:810E` | V |
| HP numbers, charge text, HUD frame | BG3 tilemap: widgets `$051D`/`$0561` hold a screen column (`$19` for the Boy) and a VRAM tilemap address (`$1F00`); lower HUD frame rows 16..27 of `$7F:C800` | U |
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
  edge-hugging OAM entries by the margin. They expect fixed OAM slots; Evermore's OAM is filled in
  depth order each frame, so whether HUD sprites sit in fixed slots is unknown (U). If not, classify
  per entry by Y band and X edge (the positions above), and publish the set each frame.
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
