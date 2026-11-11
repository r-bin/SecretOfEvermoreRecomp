# Where the Field's NMI Is Recognised

**Status:** fixed in snesrecomp by PR 6 ([../pr/06-frame-driver.md](../pr/06-frame-driver.md)): the beam frame driver and `snes/last_cycle.c`, `SNESRECOMP_BEAM_EXACT`, 2026-11-11; our glue no longer does it. Earlier: fixed in our frame driver (`src/soe_frame_driver.c`, `NMI_SEEN_CLOCKS`, `finish_nmi_approach`;
`src/soe_last_cycle.c`), 2026-10-30.

## What hardware does (MesenCE)
- At H=6 of line 225, `InternalRegisters::ProcessIrqCounters` calls `SetNmiFlag(1)` (if NMI is enabled).
- `SnesCpu::ProcessCpuCycle` runs `DetectNmiSignalEdge` at the start of every CPU cycle, so the first cycle starting
  at V=225+6 or later raises `NeedNmi`.
- The NMI is taken after the instruction that cycle belongs to.

So an instruction is followed by the NMI only if its last cycle starts at V=225+6 or later.

## What the recomp did
The framework's driver stops the CPU after the first instruction that ends past V=225 and delivers the NMI there.
That can be one instruction early. In the idle loop `$80:858F` (`CMP $0100`, 34 clocks; `BEQ`, 18) the NMI then
interrupted the other of the two instructions, and the 52-clock loop phase carried over into the next frames.

## Fix
- The CPU runs on until V=225+12, the end of a 6-clock last cycle that starts at +6.
- During that final slice, the bridge's PC hook (`g_interp_bridge_pc_hook`, free in this game) records each
  instruction's address.
- `soe_last_cycle_clocks` works out how long the last instruction's last bus cycle was, from its opcode, operand
  and the registers it left: internal 6; code fetch, data access or stack access at the region's speed.
- If that cycle started before +6 (an 8- or 12-clock access), one more instruction runs.
- Parked waits (WAI, idle parking) still stop at V=225.

## Evidence
Per-NMI clocks and interrupted addresses (`SOE_COMPARE_NMI_LOG` against MesenCE's Lua `nmi` event,
`01-no-inputs-4m.evsmv`, all interpreted):
- Before: only a few NMIs matched MesenCE's clock and PC.
- With the +12 rule: NMIs 41–105 matched exactly.
- With the last-cycle check also on: NMIs 258–320 match exactly, along with shorter runs elsewhere. The remaining
  differences were not traced one by one; the known source is the sound-port handshakes
  ([apu-port-handshake.md](apu-port-handshake.md)).

## DMA in the last instruction (2026-10-31)
MesenCE runs a general DMA started by a `$420B` write in the next instruction's first cycle, and the edge detector
looks after the transfer. snesrecomp charges the transfer to the write. So when the last instruction of the
approach started a DMA, its clocks do not count as a cycle of that instruction (`g_snes_dma_charged`, a running
total in snesrecomp's `snes.c` in PR 1, read before and after it). Found at NMI 3876 of the
attract replay: `STA $420B` at `$8C:B040`, then `REP #$20`; MesenCE takes the NMI after the `REP`.

## Refresh inside the interrupt sequence (2026-10-31)
The 8-cycle interrupt sequence was charged without a look at the refresh points; the handler's refresh accounting
starts after it, so a refresh point inside the sequence was never charged and the CPU stayed 40 clocks early.
`run_interrupt` now adds the stall when a point falls inside it (same NMI 3876: MesenCE's handler starts 40 clocks
later).

## Limits
- The classifier ignores an index or D that the instruction itself changed.
- An HDMA running at the edge (`IrqLock`) is not modelled.
