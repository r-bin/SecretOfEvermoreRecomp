# Backgrounds: margin prefill, streaming, parallax clipping, BG3

Tags `widescreen.streaming`, `widescreen.parallax`, `widescreen.scroll`, `widescreen.window`.
Marks: [README](README.md).

## How the game fills the tilemaps (V)

BG1 (canopy, in front) and BG2 (terrain) are 32 × 32 maps of 16 × 16 tiles: one tilemap word per
room cell, a 512 px ring. The field set-up `$8E:E08B` writes BGMODE `$39` (mode 1, BG3 priority, 16 × 16 tiles on BG1 and BG2) and BG1SC `$00`, BG2SC `$04`, BG3SC `$08` (V, values read 2026-11-09).

* The room grid `$7F:0000` holds one word per cell for the whole room (width `$08EE`); the word is
  an offset into the cell definitions: `$7F:0000 + w` = BG1 tile word, `+2` = BG2 tile word, `+4` =
  collision word.
* Full upload (`$90:9379` BG1, `$90:93AA` BG2; on snaps): clears the whole map by a fixed-source DMA
  of the word at `$7F:8000`, then writes `$0FAC` × 15 cells from the grid, wrapping at 32 columns.
* Per frame (`$90:98FA`, named `hud_compass_needle_update`): the window is left = HOFS >> 4, top =
  (VOFS + 1) >> 4 (`$0F90`/`$0F8C` BG1, `$0F98`/`$0F94` BG2). When it moves by a cell, one row
  (`$90:999C`, `$0FAC` cells) and one column (`$90:99FB`, 15 cells) are built into ring-ordered
  buffers (`$097A`/`$09BA` rows, `$08FA`/`$093A` columns, 32 words each) and queued for VRAM DMA
  (`$013D`, VMAIN `$80` rows / `$81` columns, BG2 at VRAM `+$0400`).
* `$0FAC` = 17 cells (256 px + 1), set by `$90:8E90`; 28 or 32 in rooms whose HDMA effect needs it
  (`$90:9BFF`, `$90:9C33`).

So after a room load the 15 ring columns outside the window are blank, and after scrolling they
hold stale content. BG1 and BG2 hold different stale content. That is what a naive wide render
shows: canopy with hard edges, terrain without canopy, black bands — not a real difference of
extent in mode-0 rooms, where both layers come from the same grid with the same scroll.

## Prefill (default)

Renderer-side, read-only, per layer: `WsShadowSetWorld(layer, $59, $5B)`, `WsShadowSetScroll` with
the PPU scroll, then `WsShadowPrefillTile(layer, wx, wy, entry)` for every margin cell, with entry
read from `$7F:0000` (+0 for BG1, +2 for BG2) (`snes/ws_shadow.h`; it supports 16 × 16 tiles and
32-wide streamed maps). Cells the game later writes to VRAM win automatically. Raising `$0FAC`
instead would also work but writes guest state (not allowed in the default mode).

## Edges: the parallax/foreground clipping option

**Definition (the user's):** with the option on, every foreground layer (BG1 canopy, and any layer
whose scroll differs from the terrain) is clipped to the **bounding box of the background**: the
terrain BG2 inside the active camera-limit box ([camera-and-limits](camera-and-limits.md)). Margin
pixels of BG1 outside that box are transparent, BG2 outside it is backdrop (black). Implementation:
prefill blank (`WsShadowSetBlankTile`) for cells outside the box, or a per-layer world band.

Alternatives (setting values): **off** (draw whatever the ring holds); **mirror** about the box
edges (`PpuSetWidescreenLayerWorldMirrorBand(ppu, layer, 0, 224, left, right)`, `snes/ppu.h:932`;
HOFS is the world X, so left/right are world pixels); **extend** the edge column
(`WsShadowExtendEdges`); **clip to the parallax layer's own authored area** (below).

Why the foreground overhangs in the screenshots: past the terrain's right edge the grid lookup
continues into the next row's cells (index = row·width + column), so whatever lies there is drawn.
The clip removes it.

## Parallax rooms (V code, room counts from headers)

| Camera mode | Entry | BG1 | BG2 | Rooms |
|---|---|---|---|---|
| 0 | `$90:9A98` | camera | camera | 118 |
| 1 | window bounds A | parallax | camera | 1 (`0x4B`) |
| 2 | heat shimmer `$90:9D45` | camera + HDMA sine | camera + HDMA sine | 6 (`0x22 0x31 0x38 0x41 0x5B 0x6A`) |
| 3 | window bounds B | camera | parallax | 0 |
| 4, 5 | lighting, torch flicker | not read | | 1 each (`0x1D`, `0x52`) |

Parallax position (`$90:9BA7`, V):
`x = ((($59 − $2417) · 16 + $0F38) · $22FA) >> 8 + $241B`, likewise y with `$5B`, `$2419`, `$22FB`,
`$241D`, − 1; ratio `$22FA` in 1/16 (`$10` = 1:1). The streamer streams BG1 from its own scroll, so
the parallax layer samples the same grid at parallax coordinates. Its authored area is what vanilla
can show: [x(camera min), x(camera max) + 256). Scripts that change the mode or the ratio at run
time were not traced (U): re-evaluate every frame.

Mode 2 rooms: the HDMA wave shifts lines; the margins get the same per-line scroll because they use
the PPU's per-line values (P1).

## Window and colour-math effects (U)

The NMI copies the window shadows `$0122..$0124` to `$2123..$2125`; which effects program window
positions by HDMA was not traced. If a window limits an effect, extend it into the margins with
`PpuSetWidescreenWindowExpansion` (`ppu.h:858`). Per-room check with the visual tests.

## BG3 (U)

BG3 carries the HUD text and dialogue windows (`$7F:C800`): [hud-and-adaptive-ui](hud-and-adaptive-ui.md).
`ppu_reset_bg3_scroll_and_shadows` (`$80:BE09`) zeroes its scroll. No BG3 weather layer was found
(weather is sprites: `$91:BDC4`, U). Default: BG3 clamped to the 256 columns except the HUD split.
