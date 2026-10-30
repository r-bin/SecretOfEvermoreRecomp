# SPC700 Clock Rate

**Status:** corrected in our frame driver (`src/soe_frame_driver.c`, `slow_apu_to_reference`), 2026-10-30.

## The difference
snesrecomp maps 357,368 master clocks to 17,088 SPC cycles (`RTL_APU_CYCLES_PER_FRAME`; 1.02528 MHz at
exactly 60 Hz). MesenCE runs the SPC700 at 32,040 Hz × 32 = 1,025,280 cycles per 21,477,270 master clocks
(`Spc.cpp`: `_clockRatio = 32040 * 64 / masterClockRate` in half-cycles). The recomp's SPC700 ran 0.16 % fast.

## Fix
After every CPU slice (at most a scanline) the APU's guest-time anchor (`apu->portGuestAnchor`) moves
forward by the accumulated difference, so its target cycle follows MesenCE's rate.

## Measured
The boot upload alone did not change much (NMI 1: 840 → 798 clocks early): its per-byte rate already
matched (see [apu-port-handshake.md](apu-port-handshake.md)). The correction matters over minutes of music.
