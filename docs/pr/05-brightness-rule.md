# 5. MesenCE's brightness rule as an option

**Status:** draft, no PR yet. Independent of 1-4: it touches only `ppu_brightness.h`, `ppu.c` and the brightness test.

Commit: `8b525c5c` on `pr-stack-on-main`.

## To do before opening

1. Ask first whether upstream wants a parity switch at all: it changes nothing by default and exists for runs compared against
   MesenCE. If not, keep it in our patch and say so under [Not proposed](README.md#not-proposed).
2. Decide whether the GLSL path in `desktop/hd_gpu.c` should follow it. Ours does not need it: compare runs use the legacy renderer.

## PR title

Brightness: MesenCE's truncating rule as an option for parity runs

## PR text

```markdown
`ppu_brightness_level` rounds in the 5-bit domain, as bsnes does (81d410f2). MesenCE truncates (`SnesPpu::ApplyBrightness`:
`c * l / 15`). A port that checks its picture against MesenCE sees every fade differ by one 5-bit step, which hides any other
difference in those frames.

`g_ppu_brightness_truncate` (off by default) switches every path that uses `ppu_brightness_level` to MesenCE's rule: the optimized
renderer's cache, the compatibility renderer, the Mode 7 line service and the raster jobs. A host sets it before the first frame.
The GLSL composition shader does not follow it.

`ppu_brightness_test` checks both renderers with the switch on.

Result: two Secret of Evermore replays, compared with MesenCE 2.2.1 pixel for pixel at every NMI: 289 of 8,592 and 948 of 23,756
frames differ with the default rule, none with this one.
```

## Required

Not for sync: it changes no memory, only the picture. Required for a picture comparison with MesenCE that can find anything else.

## Not game specific?

Yes. One integer expression, no game value.
