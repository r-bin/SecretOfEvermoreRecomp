# DMA Start and End Overhead

**Status:** fixed in snesrecomp on branch `spc-access-cycles` (`runner/src/snes/snes.c`, `$420B`;
`SNESRECOMP_DMA_ALIGN=0` restores the old charge). Open on `develop` (the framework's flat charge).

## What hardware does (MesenCE)
Measured on all 11 general DMAs of the attract run's first 330 NMIs (`01-no-inputs-4m.evsmv`): MesenCE's
`dmaController.dmaClockCounter` after each transfer, the clocks between the instructions around it, and the H
position (refresh stalls at H=538 taken out):
- a write of 0 to `$420B` starts nothing and costs nothing;
- the transfer waits from the next instruction's opcode fetch (30 clocks after a FastROM `STA $420B` starts) to the
  next 8-clock boundary: `s = 8 − (x & 7)`, 1–8 clocks;
- then 8 clocks, then `8 + 8 × bytes` per channel: counter `n = s + 8 + Σ(8 + 8 × bytes)`;
- then it waits to the next CPU cycle: `6 − n % 6` clocks (1–6), in FastROM code.

All 11 fit (overhead 10, 12, 14, 16 by `x & 7`; end waits 2, 4, 6).

## What the recomp does
snesrecomp charges `12 + Σ(8 + 8 × bytes)` for every `$420B` write, a write of 0 included. The empty write in the
reset code (before the first WRAM clear) made the CPU 18 clocks late on MesenCE from there; the flat charge came out
2–8 clocks short per DMA. With both fixed, the CPU reaches the first DMA and every NMI through 108 on MesenCE's
clock (then auto-joypad-read.md).

## Limits
The clock is taken as the instruction's start plus 30 (`SNESRECOMP_DMA_LEAD`), right for `STA abs` in FastROM code
run by the interpreter; compiled code reports its block clock (compiled-cycle-charges.md). The end wait assumes
6-clock code (`SNESRECOMP_DMA_END_SPEED`).
