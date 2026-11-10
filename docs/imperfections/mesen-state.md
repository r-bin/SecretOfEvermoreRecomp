# Starting From a MesenCE Save State

**Status:** works. Replays 05, 06 and 07 are identical to MesenCE to their ends from states taken mid-game.

## What it is
`SOE_COMPARE_MESEN_STATE=<file>` starts the game from a MesenCE state's field stream (everscript-vscode
`tools/compare/mesen-state.mts` writes it from a `.mmo` or `.mss`; a compare run does it by itself). It does not depend on the
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
- Or the NMI is already taken: MesenCE leaves its frame loop later in line 225 when an instruction or DMA ran past the
  NMI point (replay 07: H=80, PC `$00:823A`, the handler's first instruction, the frame pushed). The driver took a second
  NMI on top: `$0100` one behind, RNG and scheduler apart from key 1. Now `start_from_mesen_state` sees the PC at the
  NMI vector's target past H=66 (the NMI point + the interrupt sequence) and the first field runs that handler instead,
  not counted as a key: MesenCE's first key is the next NMI (2026-11-10).
- The first NMI comes in the first host frame, before the compare hook had read its settings: it went uncounted.
- The beam starts at H=2, past where snesrecomp runs the automatic joypad read: the handler's `$4016` read took the other
  branch at `$80:848D`. The load runs that read.
- MesenCE's SPC700 steps through an opcode, ours runs it at the first clock: the opcode in progress runs at load
  and its remaining cycles count down. `spc.opStep` is a phase, not a count: in addressing (`$E4`, replay 06) the fetch and
  `spc.opSubStep` cycles have run, the operation is taken as the last cycle (`$F7`, replay 05), and the next opcode starts
  `used - ran + 1` cycles on. An operation of several cycles has not been met (the load warns on stderr when `opSubStep` shows one), and an opcode that had already written when
  the state was taken would write twice.

## Open
- MesenCE's PPU1/PPU2 open bus and mid-line PPU state are not taken over: nothing has needed them at line 225.
