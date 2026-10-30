# DRAM Refresh

**Status:** fixed in our frame driver (`src/soe_frame_driver.c`), 2026-10-30.

## What hardware does
Every scanline the CPU (and a running DMA) stalls for 40 master clocks at H=538: 40 of the line's 1364
clocks. MesenCE: a `DramRefresh` event per scanline in `SnesMemoryManager.cpp` (at `538 - (clock & 7)`).

## What snesrecomp does
`snes_refresh_charge` adds 40 clocks per 1364 clocks of CPU time, wherever the CPU happens to be. That is
0.09 % short (40 per 1364 of work instead of per 1324) and puts the stall on other instructions. Over the
boot's three 64 KB DMAs it came to ~1,000 clocks.

## Fix
Our driver ends each CPU slice at the next refresh point (H=538 of the line), works out what the framework
charged for the slice from its refresh state (`snes_refresh_state_get`), and tops it up to one 40-clock stall
per refresh point the slice crossed. A long DMA gets the hardware's count; normally the framework charges
none and the stall lands after the instruction that reached H=538.

## Left
The ±7-clock jitter of MesenCE's refresh position (`538 - (clock & 7)`) is not modelled.
