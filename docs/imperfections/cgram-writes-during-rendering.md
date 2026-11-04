# CGRAM Writes During Rendering

**Status:** fixed for DMA in our frame driver (`charge_dma`, `cgram_dma_finish`), with colour 0 as the target (Limits).

## What MesenCE does
`SnesPpu::Write` `$2122`: outside vblank, forced blank and H-blank (`CanAccessCgram`: H<88, H>=1096), a write goes to the colour
the PPU is reading at that moment (`InternalCgramAddress`), not to the CGRAM address. The address counts on either way. It also
drops bit 7 of the high byte.

## What snesrecomp does
Every write goes to the CGRAM address, and it keeps bit 7.

## Effect
At key 2991 of replay 05 a fade's DMA of 240 zero colours runs into visible lines 25-28, brightness 1. 349 of its 480 bytes fall
outside the window on MesenCE, all with colour 0 as the PPU's colour. Ours cleared all of them: 276 bytes differed from key 2992 to
3065, until the game wrote the palette again.

## Fix
`snes_set_master_clock_charge_hook` runs before a `$420B` transfer: our hook (the framework's two lines, plus this) keeps CGRAM when
a channel to `$2122` fits the charge. The hook is not told the channels and other channels keep old sizes, so the fit is on the
clocks. When the slice ends, `cgram_dma_finish` walks the bytes: the first lands 24 clocks after the 8-clock alignment, then 8
each, 40 more at a refresh point. Measured: that offset is the only one of 16, 20, 24, 28 that is exact.

Bit 7: the driver clears it from all of CGRAM at every NMI. Rendering and `$213B` already ignore it.

## Limits
- The colour the PPU reads needs the pixel, which a per-line renderer does not have. Colour 0 is what all 349 writes measured
  did, with layers on main and sub screen; a scene with another colour there would differ in that one colour.
- CPU writes to `$2122` during rendering are not covered: none were seen in the keys measured (2985-3070, 3148-3151).
- A write in the same slice after such a DMA would be undone; a DMA this long ends its slice, so none has been met.
