# Starting From a MesenCE Save State

**Status:** works. Replay 05 is identical to MesenCE to its end from a state taken mid-game (CGRAM: below).

## What it is
`SOE_COMPARE_MESEN_STATE=<file>` starts the game from a MesenCE state's field stream (everscript-vscode
`tools/recomp/mesen-state.mts` writes it from a `.mmo` or `.mss`; a compare run does it by itself). It does not depend on the
compare harness: a normal launch with the variable set starts there too. Saving a recomp state from such a run is
not tested yet.

`src/soe_mesen_state.c` fills in the CPU, WRAM, SRAM, VRAM, CGRAM, OAM, PPU registers (through `ppu_write`), DMA channels,
internal registers, the SPC700, its timers and the per-clock S-DSP, whose state matches MesenCE's field by field.

## Why it can be exact
- MesenCE saves between frames: at an instruction boundary at line 225, where this driver's host frame starts.
- Clocks stay absolute. MesenCE's master clock is on the driver's field grid (`field_end`, difference 0), and `spc.cycle`
  is on the APU's half-cycle timeline (difference 1).

## Traps found
- The state's next instruction runs before the NMI: the driver runs it as one step, then takes the NMI as usual.
- The first NMI comes in the first host frame, before the compare hook had read its settings: it went uncounted.
- The beam starts at H=2, past where snesrecomp runs the automatic joypad read: the handler's `$4016` read took the other
  branch at `$80:848D`. The load runs that read.
- MesenCE's SPC700 steps through an opcode, ours runs it at the first clock: the opcode in progress runs at load
  and its remaining cycles count down. `spc.opStep` counts from 0: on the one state so far (step 3 of `$F7`), every value but
  step + 1 parted by key 108. An opcode that had already written when the state was taken would write twice; none has
  been met. Between opcodes (step 0, 04's state) the next one starts a cycle later.

## Open
- Replay 05, keys 2992-3065: CGRAM differs ([cgram-writes-during-rendering.md](cgram-writes-during-rendering.md)).
- MesenCE's PPU1/PPU2 open bus and mid-line PPU state are not taken over: nothing has needed them at line 225.
