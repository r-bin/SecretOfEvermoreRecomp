# Mode 7 (Windwalker flight)

Tag `widescreen.mode7`. Marks: [README](README.md).

## Framework (V)

`PpuDrawBackground_mode7` (`snesrecomp/runner/src/snes/ppu.c:2152`) evaluates the affine transform
for every span of the window list; without a game window the span is `PpuWindows_Clear`'s
[−extraLeft, 256 + extraRight) (`ppu.c:964`). `xpos`/`ypos` are computed from the screen x (`rx`),
so negative and ≥ 256 columns continue the same per-line matrix: **true wide Mode 7 already works**
in the software renderer. The HD Mode 7 renderer (`snesrecomp/docs/HD_MODE7.md`) is tested with
"negative margins" too (U for our use). Outside the 1024 × 1024 field the PPU's large-field /
char-fill rule applies as on hardware (`m7largeField`, `m7charFill`).

## Game side (U)

* Per-line matrices come from HDMA tables (perspective), so they are per scanline and need no
  horizontal extension. The sky/horizon gradient is HDMA per line too (`$80:D7B0`, U).
* The flight camera is the same `$59`/`$5B`-based pipeline with the altitude bias (header flag
  bit 14, [camera-and-limits](camera-and-limits.md)).
* Sprites: aircraft, clouds, cloud shadows and the cockpit compass (`$8E:B33D`, `$8E:B???`
  `mode7_flight_sprite_render`, `$80:C881`, `$80:D23A`) use the same OAM path and are clipped at
  256 ([sprites-and-cull](sprites-and-cull.md)); the cockpit/compass HUD is screen-fixed.
* Gate: `$0E70` (flight state) or the entry of `script_op_99_mode7_scene` (`$8C:E66F`).

## Options

| Option | What | Risk |
|---|---|---|
| 4:3 (default until checked) | pillarbox during flight | none |
| Wide | margins from the continued affine transform; flight sprites clipped at 256; cockpit HUD moved like the field HUD | sprites (clouds) cut at the 4:3 seam; check the horizon line and the sky band |
| Stretch | presenter scales the 256 image | distorts |

Check: frame dumps of take-off, cruise and landing at 16:9; look at the horizon, the sky gradient
and where clouds stop.
