# H/V Counter Latch (`$2137`, `$213C`, `$213D`)

**Status:** fixed in snesrecomp on branch `spc-access-cycles` (`runner/src/snes/ppu.c` `$2137`, `snes.c`
`snes_latch_position`), 2026-10-31.

## What hardware does (MesenCE)
- A `$2137` read latches the counters only while WRIO (`$4201`) bit 7 is set; WRIO is `$FF` after power-on. A
  write that clears bit 7 latches too.
- `$213C` and `$213D` each return the low byte, then bit 8, on alternate reads. Only a `$213F` read resets the two
  flip-flops; a latch leaves them alone.
- The latched H is a dot count: dots 323 and 327 are 6 master clocks long (`SnesPpu::GetCycle`). It is taken at the
  read's own clock.

## What the recomp did
snesrecomp latched on every `$2137` read, reset both flip-flops at each latch, and took `hPos / 4` at the
instruction's start (24 clocks early for `LDA $2137` in FastROM). WRIO started at 0.

## Effect
The attract replay's entropy mix at `$80:81A0` (`LDA $2137`, `EOR $2138`, `ADC $2137`, `EOR $213C`, `ADC $213D`,
`EOR $2180`, `ADC $2139`, `ADC $213B`, with `BCS` branches on the carry) read the low bytes where MesenCE reads bit 8:
the first difference at NMI 5998, after every NMI through 5997 was exact.

## Fix
The latch is gated by WRIO bit 7, leaves the flip-flops, and takes the beam at the access clock
(`interp_bridge_access_lead`, 2 clocks into the bus cycle) as a dot count. NMIs through 6100 exact afterwards.

## `$2137` reads open bus (2026-10-31)
SLHV drives no data: MesenCE's read returns open bus (`$21` after `LDA $2137`, the operand's high byte); snesrecomp
returned 0. Fixed in `ReadRegOpenBus` (branch).

## Left open: PPU reads during rendering
The same routine reads `$2138` (OAM) and `$213B` (CGRAM) on line 124/125, during active display. MesenCE returns
what the PPU itself is reading at that dot: OAM at the sprite evaluation's index (`GetOamAddress`: the range
phase's index up to H 1020, the time phase's after), CGRAM at the palette address of the pixel it is drawing
(`InternalCgramAddress`; CGRAM is free only from H 1096 to H 88, in vblank and in forced blank). Measured at NMI
5997 of the attract replay: OAM MesenCE `$79`, ours `$08`; CGRAM MesenCE `$00`, ours `$83`. The routine mixes them
into the RNG seed (`$7E0133–013A`), so WRAM parts at key 5998 although every NMI through 6358 lands on MesenCE's
clock. Needs per-dot PPU state in snesrecomp (sprite evaluation and the pixel being drawn), not done.

On line 124 of that field our beam's H stood 8 clocks off MesenCE's at the same master clock (latched dot 317
against MesenCE's ~319). Only bit 8 was read there, so it did not matter; a game that reads the low byte would see it.
