# CGRAM Writes During Rendering

**Status:** open, not fixed. Costs replay 05's CGRAM from key 2992 to 3065. WRAM, VRAM, OAM and clocks are not affected.

## What MesenCE does
`SnesPpu::Write` `$2122`: outside vblank, forced blank and H-blank (`CanAccessCgram`), a write goes to the colour the PPU is
reading at that moment (`InternalCgramAddress`), not to the CGRAM address.

## What snesrecomp does
Every write goes to the CGRAM address.

## Effect
At key 2991 a fade's DMA of zeros runs into visible lines 25-28, brightness 1. MesenCE's sampled writes hit colour 0 or land in
H-blank; ours clear all of it. 276 bytes differ at 2992, on a screen that is almost black, until the game writes the palette
again at 3066.

## What a fix needs
The colour the PPU reads, per pixel, which a per-line renderer does not have, and a hook on `$2122` writes (only VRAM has one).
Ignoring the write in the window would be close but not exact.
