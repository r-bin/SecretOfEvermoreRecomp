# Upstream Proposal: Access-Exact Timing in snesrecomp

**Status:** prototype of A, C, D1 and part of E on snesrecomp branch `spc-access-cycles` (this repo's branch
`snesrecomp-spc-access-cycles` pins it); owner's experiment for an upstream PR, `develop` keeps the pinned framework
([../AGENTS.md](../AGENTS.md) rule 2). It holds no Evermore facts: every rule below is SNES hardware, so every game on
snesrecomp gains from it. Results and what is left: [Prototype](#prototype-2026-10-31).

## Goal
WRAM identical to MesenCE at every NMI, for the whole run, native build included. Today we have that through NMI
key 319 of `01-no-inputs-4m.evsmv`, then 97.9–99.98 % ([imperfections/README.md](imperfections/README.md)).

## The one principle
> Every bus access happens at its own master clock. Before an access to state that another chip shares, that
> chip is run up to this clock.

snesrecomp keeps time per instruction (SPC700), per compiled block (CPU) and per DMA. That is accurate on average
but not at the moment of an access. Every open imperfection is this one coarseness, seen in another place:

| Open imperfection | Coarse unit today | Part below |
|---|---|---|
| [apu-port-handshake.md](imperfections/apu-port-handshake.md) (the first difference left) | SPC700 instruction: all port accesses on its first cycle | C, D |
| [compiled-cycle-charges.md](imperfections/compiled-cycle-charges.md) | compiled block: one constant, every cycle at code speed | B |
| [dma-overhead.md](imperfections/dma-overhead.md) | whole DMA charged at once, no alignment | E |
| [host-frames-and-fields.md](imperfections/host-frames-and-fields.md) | host frame runs past several V=225 inside a DMA | E |

The fixed ones (late NMI, interrupt cost, refresh, short line 240, auto-joypad window, NMI recognition, SPC
rate, start clock 186) are in our frame driver `src/soe_frame_driver.c`. They are game-neutral and belong
upstream too (part F).

## A. One clock
- One 64-bit master clock from power-on, owned by the runtime; every chip derives its time from it.
- No rebasing per host frame (today `apu_frame_clock.h`: master × 17,088 / 357,368, rebased each frame, private
  to `common_rtl.c`).
- SPC time = `floor(master × 1,025,280 / 21,477,270)`, as MesenCE computes it: one mapping, so rounding never
  depends on where a host frame began.

## B. CPU: charge each access at its region's speed
The interpreter already does this exactly (`cpu_state.c`: all 331 decompressor instructions cost what MesenCE
charges). Compiled code should do the same:
- Fold an instruction into its parts: code fetches (code speed), internal cycles (6 clocks), data accesses
  (speed of the region the addressing mode reaches: direct page and stack are WRAM, 8; long operands are known at
  recompile time; absolute depends on DB, known at recompile time when DB is, otherwise charged at run time).
  `recompiler/snes_cycles.py` already sketches this (`instr_master_cycles`, the "first-cut model" note).
- Keep folding inside a block, so plain code stays as fast as today.
- **Flush before I/O:** before an access to `$2100–$21FF` or `$4000–$43FF`, add the charge owed so far plus the
  access's own offset, so the access sees its exact clock. The access itself goes through C.

## C. Sync at shared state
Before a CPU access to a register another chip owns, run that chip to the access clock, then do the access:
- `$2140–$2143`: run the SPC700 to the access clock (MesenCE: 2 clocks into the access cycle, 20 clocks into an
  absolute `STA`/`CMP $2140`). A CPU write takes effect for the SPC at that clock, not through a queue placed by
  an estimate (`apu_schedulePortWrite`).
- `$4210–$4212`, `$2137`, `$213C–$213F`, `$4218–$421F`: bring the beam (H, V, NMI/IRQ flags, auto-joypad state)
  to the access clock.
This is the classic catch-up scheme. Off shared state nothing changes, so the cost scales with I/O, not code.

## D. SPC700: accesses on their own cycle
`spc_runOpcode` (`runner/src/snes/spc.c`, driven by `apu_cycle` in `apu.c`; `apu_native.inc` the same) runs the
whole instruction on its first cycle. Two ways, in order of cost:
1. **Per-opcode access offsets** (smallest change): a table giving, for each opcode, the cycle of each bus
   access. When an access hits `$F0–$FF` (ports, timers, control), the core runs to that cycle first. Example:
   MesenCE's `MOV $F4,$F4` reads on cycle 3 and writes on cycle 4.
2. **A per-cycle core** (as MesenCE's `Spc::ProcessCycle`): exact for every access, slower.
Either one alone does not help without C: the CPU side must also arrive at its exact clock.

## E. Events stop the CPU, also inside a DMA
- Run a general DMA as a resumable loop: 8 clocks per byte, MesenCE's start alignment (`8 - (clock & 7)`, then 8)
  and end alignment (to a whole CPU cycle), instead of `12 + Σ(8 + 8 × bytes)` at once.
- A scheduler with the next event's clock (V=225, H/V IRQ, auto-joypad start, refresh at its H, field end): the
  CPU, interpreted or compiled, and the DMA loop stop at it. Compiled code needs only a check at block ends and
  at flushes (B), since events are taken between instructions.
- Then a host frame is exactly one field, and replay input lands on the field MesenCE gives it.

## F. Beam driver: the hardware's interrupt rules
Move what our `src/soe_frame_driver.c` does into `beam_frame_driver.c`, unchanged in substance: NMI raised when
it is enabled inside vblank, the 8-cycle interrupt sequence charged, NMI recognised after the first instruction
with a cycle from V=225 + 6 clocks, line 240 1360 clocks every other non-interlaced field, refresh at its fixed
H, auto-joypad busy from MesenCE's point, CPU start at clock 186, SPC rate as in A. Each has its evidence in its
own [imperfections/](imperfections/README.md) file.

## Cost
Not measured. Expected: interpreter unchanged; compiled code adds work only at I/O accesses and block ends; the
SPC gains one table lookup per access (D1) or runs per cycle (D2). D1 first; D2 only if D1 leaves a difference.

## How to prove it
In everscript-vscode, against MesenCE 2.2.1, native and all-interpreted:
```sh
node tools/compare/compare.mts "Secret of Evermore (U) [!].smc" --backend mesen,recomp \
  --replay tools/compare/replays/01-no-inputs-4m.evsmv --frames 3000 --keep-going --regions wram
```
Done when WRAM is identical at every NMI key for all replays in [replay-sync.md](replay-sync.md), and every
`$2140` handshake takes as many polls as on MesenCE (probe `tools/compare/probes/port-handshakes`). Upstream
should also gain a game-neutral test: a small homebrew ROM that polls `$2140`, `$4212` and the H/V counters in
tight loops, checked against MesenCE.

## Out of scope
- Correctness of compiled code (pushed-address returns, [imperfections/README.md](imperfections/README.md) "Open
  risk"): not timing.
- Rendering (Mode 4, [bugs-and-todos.md](bugs-and-todos.md) §3): does not touch WRAM.

## Prototype (2026-10-31)
On snesrecomp branch `spc-access-cycles`; switches in the code, off switches as env vars for A/B runs:

| Part | Where | Switch |
|---|---|---|
| A, C: master-clock APU timeline (half-cycles, no rebase, no relative catch-up, frame end to the exact clock) | `common_rtl.c` | `RtlApuUseMasterClock(on, lead)` (our glue: on, 20); `SNESRECOMP_APU_MASTER_CLOCK` |
| C: host access clock (refresh stalls in front of a port access) | `common_rtl.c` | `RtlSetCpuAccessClockHook` |
| D1: per-access SPC cycles, hold of the straddling opcode, read lookahead, reset 3 cycles, no rebase on small lateness | `spc.c`, `apu.c` | `SNESRECOMP_SPC_ACCESS_CYCLES=0` |
| E (part): MesenCE's DMA start/end rule, empty `$420B` free | `snes.c` | `SNESRECOMP_DMA_ALIGN=0` |
| diagnostics | `common_rtl.c`, `apu.c`, `snes.c` | `SNESRECOMP_PORT_LOG=1`, `SNESRECOMP_DMA_LOG=1` |

Measured on `01-no-inputs-4m.evsmv`, all interpreted, against MesenCE 2.2.1: the SPC's port writes land on
MesenCE's cycles; every NMI 2–319 is exact; the load after NMI 319 matches for 12,748 port accesses. Per-key WRAM
still parts at 320 (native and interpreted; develop too), so the replay does not sync yet. Details and the numbers:
[imperfections/README.md](imperfections/README.md).

Still open, in the order they part the run:
1. MesenCE's SPC catch-up rounding at a CPU read (apu-port-handshake.md, "Left open").
2. B, compiled code billed at code speed: the native build is exact only to NMI 1 (compiled-cycle-charges.md).
3. 16-bit port writes land both bytes at once (port-word-writes.md).
4. Our frame driver takes the short line's 4 clocks at V=225, so the beam is 4 clocks off MesenCE's in lines
   225–240 of every other field (the auto-joypad rule works around it; refresh does not need it, measured).

How it was measured (probes outside the repo, in the session's scratch space; worth adding to everscript-vscode's
`tools/compare/probes/` once kept): MesenCE Lua callbacks on `$2140–$2143` and SPC `$F4–$F7` with `masterClock` and
`spc.cycle` (2 per SPC cycle), against the recomp's `SNESRECOMP_PORT_LOG`; NMI clocks from both sides' NMI logs;
`dmaController.dmaClockCounter`, `memoryManager.hClock` and `internalRegisters.autoReadClockStart` from MesenCE's
state. MesenCE's exec callbacks on every instruction are slow enough that the runner stops a long job: arm them from
an NMI event.
