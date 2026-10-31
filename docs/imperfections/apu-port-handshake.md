# APU Port Handshake Timing

**Status:** open on `develop`, in snesrecomp's SPC700 model (`runner/src/snes/apu.c`, `spc.c`); it cannot be fixed
in our files with the means tried (below). Mostly fixed in snesrecomp on branch `spc-access-cycles` (below).

## What hardware does (MesenCE)
- The S-SMP runs cycle by cycle (`Spc::ProcessCycle`).
- A port read or write happens on its own cycle. Measured in the sound driver's echo `MOV $F4,$F4` at SPC `$001E`:
  the read is 3 SPC cycles in, the write 4 cycles in.
- A CPU access to `$2140–$2143` first runs the SPC up to the CPU's master clock (`Spc::Run`).
- That clock is 2 clocks into the CPU's access cycle: 20 clocks into an absolute `CMP`/`STA $2140`.
- The SPC's time is `floor(master × 1,025,280 / 21,477,270)`.

## What the recomp does
- **SPC:** `apu_cycle` runs a whole SPC instruction, every port read and write in it, on its first cycle
  (`spc_runOpcode`; `apu_native.inc` does the same).
- **CPU writes:** queued at the SPC cycle that the framework's guest clock assigns to the write (`apu_schedulePortWrite`).
- **CPU reads:** run the SPC up to that clock. The guest clock is the master clock × 17,088 / 357,368, rebased each
  host frame (`apu_frame_clock.h`), and private to `common_rtl.c`.
- **The interpreter** reaches the ports at an instruction's start clock.

## What our glue does
- The anchor moves for the clock rate ([spc-clock-rate.md](spc-clock-rate.md)).
- Since 2026-10-30 the anchor also starts 20 master clocks back (`APU_ACCESS_LEAD`), for the access-cycle
  offset. That brought NMI 1 (after the boot upload) from 530 to 152 clocks early.

## Evidence
Every CPU write to `$2140` up to NMI 320 of `01-no-inputs-4m.evsmv`, and the number of `$2140` reads before the
next write:
- 18,672 handshakes on both sides, the same ones in the same order.
- 45 % take the same number of polls; the rest take one poll pass (42 clocks) more or fewer.
- The off-by-one goes both ways and alternates. That is quantisation, not a constant offset.

The first WRAM difference, at NMI key 320, starts in such a handshake: the block-start wait `$8C:817C`
(`CMP $2140 / BNE`) in the load after NMI 319 needs one pass more in the recomp. Sources: MesenCE `$2140` and SPC
`$F4` callbacks against `SNESRECOMP_CYC_WATCH` (all interpreted).

## Tried, without gain
- Shifting the anchor by 0–105 master clocks: 43–47 % of handshakes match at every setting.
- Queuing CPU writes 0–3 SPC cycles after the time CPU reads see, through a port observer
  (`RtlAddApuPortObserver`): no setting does better. The matched share drops to 15 % at 3 cycles.

## Fix (upstream)
- Port accesses on their own SPC cycle: a per-cycle SPC core, or at least per-instruction offsets for the
  `$F4–$F7` accesses.
- Optionally, MesenCE's guest-time rounding (one mapping from power-on, not rebased per host frame).

## Fix on branch `spc-access-cycles` (2026-10-31)
In snesrecomp (`spc.c`, `apu.c`, `common_rtl.c`), with `RtlApuUseMasterClock(true, 20)` in our glue:
- each SPC bus access gets its own cycle: the core numbers its accesses, corrected where its order is not the
  S-SMP's (`dp,dp` operations read the source before fetching the destination operand; word reads idle a cycle
  between the bytes);
- a CPU sync leaves the one SPC opcode that would touch `$F4–$F7` after the CPU's clock unstarted (a dry run of
  that opcode finds out), and a port read first takes the CPU writes due by its own cycle;
- the SPC's time is the master clock since power-on in MesenCE's half-cycles (`× 2,050,560 / 21,477,270`, no
  per-frame rebase, no relative catch-up): an SPC access sees a CPU write from 2 half-cycles on, a CPU read sees SPC
  writes up to its own half-cycle;
- the SPC starts 3 cycles after reset, not 7: the IPL's first `$F4` access lands on SPC cycle 2400, as on MesenCE;
- the CPU's access clock waits out a refresh stall that falls before it (refresh-before-access.md).

Measured (all interpreted, `01-no-inputs-4m.evsmv`): every SPC write to a port lands on MesenCE's SPC cycle through
the 5,000th; 212 of the boot upload's 214 handshakes and every port access of the load after NMI 319 through the
12,748th match MesenCE's. WRAM still parts at key 320: the load runs about 18,000 handshakes before NMI 320.

## Left open
At the 12,748th access both sides run the same `CMP $2140` at the same H (516) with the refresh inside it, and the
SPC writes the echo on the same half-cycle; MesenCE sees it in one such case and not in another. The two differ only
in the fraction of the master-to-SPC conversion at the read (x = …210.95 sees a write 1.05 half-cycles later,
x = …707.06 misses one 0.94 later). MesenCE's catch-up rounding at that point is not modelled yet.
