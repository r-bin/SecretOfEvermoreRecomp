# A MesenCE Movie Starts From a Reset the Recomp Cannot Fully Copy

**Status:** open. Costs replay `04-newgame-flowers.mmo` its sync at key 621.

## What happens
With `snes.ramPowerOnState Random` MesenCE stores a save state in the movie. Ours sits at the reset vector (master clock
186) but is not a power-on: `ppu.frameCount` is 50, FastROM is on, DMA channels hold game addresses, SPC timer 0
runs, ARAM and the S-DSP registers are random, and WRAM and SRAM hold data.

## What the recomp takes over
WRAM, SRAM, ARAM, S-DSP registers, FastROM, SPC timers, the S-DSP's clock (`SOE_COMPARE_WRAM`, `_SRAM`, `_ARAM`,
`_DSP`, `_HW`; `src/game_rtl.c`, `src/platform/tas/mesen-movie.ts` in everscript-vscode).

| Without | Effect |
|---|---|
| ARAM, S-DSP registers | the gap from NMI 319 to 320 is 55 fields, not 75: everything after it runs 20 fields early |
| FastROM | NMI 1 is 362 clocks late |
| any of the state | the RNG differs from key 50 (the harness copies it at key 115) |

## What is left
PPU latches and registers, `dmaClockCounter` (86), S-DSP internals. NMIs stay up to about 40 clocks apart from NMI 2, and
at key 621 an NMI pushes another return address. Which field causes the jitter is not traced; `dmaClockCounter` is the
first suspect, since the reset code's DMAs come right before NMI 2.

## Ways out
Start recording mid-game instead: such a state is taken over whole ([mesen-state.md](mesen-state.md)). Or:
Record with `snes.ramPowerOnState` set to all zeros and from power-on: no state in the movie, the start is the one
`01` and `02` have. Or, for this movie, `SOE_MMO_FRESH=1`, which is lockstep but not the recording.
