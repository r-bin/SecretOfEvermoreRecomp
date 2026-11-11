# `$4212` Read at the Access, and Its Open Bus

**Status:** fixed in snesrecomp by PR 6 ([../pr/06-frame-driver.md](../pr/06-frame-driver.md)): `$4212` bit 7 at the access in `snes.c`, `SNESRECOMP_HVBJOY_EXACT`, 2026-11-11; our glue no longer does it. Earlier: fixed in our frame driver (`src/soe_frame_driver.c`: `edge_step`) and `src/soe_ppu_fixes.c` (`soe_ppu_fixes_nmi_entry`, bits 1-5). snesrecomp itself still
has both differences.

## What MesenCE does
`$4212` is read at the access, 20 clocks into `LDA $4212`. Bit 7 is `scanline >= nmiScanline`, bit 6 H-blank, bit 0 the
automatic read's busy flag, and bits 1-5 are the open bus: `$42`, the address's high byte, so bit 1 reads 1.

## What snesrecomp does
Bit 7 comes from the beam at the instruction's start, bits 1-5 read 0, and its beam sets bit 0 at V=225, before MesenCE's
read starts ([auto-joypad-read.md](auto-joypad-read.md)).

## Effect (replay 05, the flight scene)
- `$80:C2D7` (`LDA $4212 / BMI`, the wait for vblank's end) left one pass later when the read fell in the last 20 clocks
  before line 0.
- `$80:C286` (`LDA $4212 / BPL`, the wait for vblank) is interrupted by the NMI. The NMI frame's P held Z where the read
  had returned 0, and the handler's `PHA` pushed `$81` or `$83` where MesenCE has `$82`. Nothing reads those stack bytes,
  but WRAM differs: key 3147, 3148, 3655.

## Fix
- A slice stops 20 clocks before line 0 and before V=225. The beam goes to the edge and waits there for the CPU
  (`snes_beam_hold`), so a read in those clocks sees the far side. At V=225 the beam takes one more step, so that its
  automatic read runs, and the driver clears the busy bit it set: `auto_joypad_begin` sets it at the NMI.
- Before an NMI's frame is pushed, `soe_ppu_fixes_nmi_entry` sets bit 1 in A and clears Z, when the last instruction was
  `LDA $4212` or its `BPL`/`BMI`. The game tests only bits 0, 6 and 7, so nothing else sees bits 1-5.

## Limits
The lead is that of an absolute read, the form of the waits above and of `$80:8473`. A read in a form with another lead would
still be off by the difference.

## Upstream
In `snes_readReg`, case `0x4212`: bit 7 at the read's clock, as bit 6 already is, and `open_bus & 0x3E`.
