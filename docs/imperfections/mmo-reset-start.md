# A MesenCE Movie Starts From a Reset of a Running Console

**Status:** solved: `04-newgame-flowers.mmo` is identical to MesenCE from key 120 to its end, 25,923.

## What happens
With `snes.ramPowerOnState Random` MesenCE stores a save state in the movie. Ours sits at the reset vector (master clock
186) but is not a power-on: `ppu.frameCount` is 50, FastROM is on, DMA channels hold game addresses, SPC timer 0
runs, ARAM and the S-DSP's readable registers are random, and WRAM and SRAM hold data.

## What the recomp takes over
WRAM, SRAM and FastROM (`SOE_COMPARE_WRAM`, `_SRAM`, `_HW`), the whole SPC700 and S-DSP through the state loader
(`SOE_COMPARE_MESEN_APU`, `soe_mesen_state_load_apu`), and the controller's shifted-out state (one automatic read).

| Without | Effect |
|---|---|
| the controller's state | `$4016` reads 0 at `$80:848D`, MesenCE's 1: NMI 2 parts, and the NMIs jitter by up to 40 clocks |
| the S-DSP's two register sets apart | MesenCE keeps what `$F3` reads back (random) and what the S-DSP runs on (0) separately. One copy of the random bytes: `ENVX` steps elsewhere, the driver's read at SPC `$09FB` parts at key 412 |
| any S-DSP clock offset instead of the state | offsets from the state's step: to 620 or 776, or to 4370 with the opposite error at key 1130: no single offset is right |
| ARAM, S-DSP registers | the gap from NMI 319 to 320 is 55 fields, not 75 |
| FastROM | NMI 1 is 362 clocks late |

## Trap
The state sits between two SPC700 opcodes (`spc.opStep` 0): the next one starts a cycle later here, or NMI 320 parts by 310,050 clocks.

## Left
`SOE_MMO_FRESH=1` still powers both sides on, for a lockstep run that is not the recording. The RNG is still copied at key 115,
so keys before 120 are not compared. PPU registers and latches of the state are not taken over: nothing has needed them.
