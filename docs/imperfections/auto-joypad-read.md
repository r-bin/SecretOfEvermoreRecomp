# The Automatic Joypad Read's Busy Window

**Status:** fixed in our frame driver (`src/soe_frame_driver.c`, `auto_joypad_begin`), 2026-10-30.

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
