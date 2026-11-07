# The Automatic Joypad Read's Busy Window

**Status:** fixed in our frame driver (`src/soe_frame_driver.c`, `auto_joypad_begin`), 2026-10-30; the rule
corrected with PR 1 (2026-10-31, below).

## What hardware does (MesenCE)
`InternalRegisters::SetAutoJoypadReadClock` runs when a field ends (V=225). The read starts at the first multiple
of 256 master clocks at least 130 clocks later, and `$4212` bit 0 stays set for 4224 clocks from there.
`ProcessAutoJoypad` evaluates the bit at the read's clock. That clock is 2 clocks into the access cycle, which for
`BIT $4212` (absolute, 3 fetches at 6) is 20 clocks after the instruction starts.

## What the recomp did
snesrecomp's beam sets a 4224-clock timer at V=225 H=0 (`snes.c`, `snes_advance_beam`). That is 130–385 clocks
earlier than MesenCE, depending on the master clock modulo 256. The interpreter also reads a register at the
instruction's start clock, because it adds the instruction's clocks after running it. The game's NMI handler waits
for the read to finish at `$80:8473` (`LDA #1 / BIT $4212 / BNE`), 42 clocks a pass, so the wait ended up to 9
passes early. In fields where the field-length alignment had moved H off 0, the timer was not set at all.

## Fix
Once the beam has crossed V=225, the driver sets `g_snes->autoJoyTimer` so that it runs out at MesenCE's end,
minus the 20-clock read lead. The NMI path syncs the beam first, so the timer is right before the handler runs.
Simplification: from V=225 to the 256-clock start point the bit already reads as set. The game only polls it
later in the handler.

## Evidence
MesenCE `$4212` read callbacks in frames 200 and 245 of `01-no-inputs-4m.evsmv`: bit 0 reads 1 up to 34 clocks
before the computed end and 0 at 8 clocks after it. The one-frame trace diff (MesenCE exec callbacks against
`SNESRECOMP_CYC_WATCH`) matched the handler instruction for instruction in both frames after the fix.

## Correction (2026-10-31, with PR 1)
The rule above was measured on two frames and is wrong in its parts; they cancelled out in most frames. MesenCE's
`internalRegisters.autoReadClockStart`, read at every NMI from 2 to 330 of `01-no-inputs-4m.evsmv`:
- every start is 128 past a multiple of 256 (not on the multiple), at least 3 clocks after MesenCE's line 225 began;
- `$4212` bit 0 reads busy for 4352 clocks (17 × 256) from there, as the interpreter samples it (start clock, 20-clock
  lead), not 4224;
- MesenCE's line 225 starts at `field_end(k)`. In the fields whose short line our driver takes at V=225, the beam's
  line 225 starts 4 clocks earlier (short-scanline.md), and that parity decided the borderline frames.

The old rule (multiple of 256, ≥ 130 clocks, 4224) ended the window at the same clock whenever the 128-clock grid
offset went one way, and 256 clocks early when it went the other: frame 108, where the NMI handler's wait at
`$80:8473` left one pass early. With the corrected rule every NMI from 2 to 319 has MesenCE's clock and interrupted
address (all interpreted).

## A frame that starts past line 0 (2026-11-04)
A DMA that runs through all of vblank starts the next host frame on line 29 (NMI 3065 of replay 05). The driver counted back to
line 225 only from lines above it, took line 29 for line 225 and held the bit about 90,000 clocks too long. It now counts across the wrap.

