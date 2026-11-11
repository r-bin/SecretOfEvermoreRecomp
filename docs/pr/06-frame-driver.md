# 6. The beam: MesenCE's field and interrupt timing

**Status:** written as one PR (2026-11-11): local branch `frame-rules-main` in `../snesrecomp`, four commits on main `80a41241`,
not pushed, not opened. On the fork's stack as branch `pr6-rp` (on `develop-rp`); our glue on that stack: recomp branch
`pr6-glue`, not merged into `develop`. [#241](01-access-exact-timing.md) is merged; its access-clock hook and master-clock APU
timeline now have their callers inside snesrecomp.

Our frame driver (`src/soe_frame_driver.c`) used to hold these rules, patched in from outside (it moved the beam after each
field, walked it 20 clocks ahead of the CPU at the vblank edges, topped up the refresh after each slice). Now each rule is where
it belongs, behind its own switch, each on by default only with `RtlApuUseMasterClock`, so no other game changes:

| Commit | Where | Switch | Rule |
|---|---|---|---|
| `842f9b7f` | `snes.c`, driver | `SNESRECOMP_SHORT_LINE`, `SNESRECOMP_RESET_CLOCK` | line 240 of every other non-interlaced field 1360 clocks (`Snes.fieldParity`, savestate v12); the CPU starts at 186 |
| `7cc6e1a8` | `snes.c` | `SNESRECOMP_HVBJOY_EXACT` | `$4212` bits 7 and 0 at the read's clock, as bit 6 |
| `159fbdf1` | runtime, `common_rtl.c` | `SNESRECOMP_REFRESH_AT_BEAM` | DRAM refresh per refresh point crossed (H = 538 - (line start & 7)), in front of APU port accesses too |
| `ee9cb738` | driver, `snes/last_cycle.c` | `SNESRECOMP_BEAM_EXACT` | interrupt sequence 60 clocks; NMI after the first instruction with a cycle from V=225 H=6; late NMI; slices to each refresh point; hooks |

The old driver path stays as it was; the exact path is a separate function (`run_frame_exact`).

## What was checked (2026-11-11)

On the fork's stack (`pr6-rp` = `develop-rp` + these commits) with our glue reduced to the hooks (`pr6-glue`), against MesenCE
2.2.1 at every NMI (`--verbose`):

| Run | Result |
|---|---|
| Replay 01, native | WRAM, VRAM, CGRAM, OAM identical at all 8,592 NMIs |
| Replay 01, all interpreted | identical at all 8,592 NMIs |
| Replay 02, native | identical at all 23,756 NMIs |
| Speed, native share (replay 01, `run-stats.mts`) | 322 frames/s, 94.06 % native; the current build 318 frames/s, 94.05 % |
| ctest | 8/8: reset, save state, both recorded replays, both with widescreen, HUD and overlay (free time) |
| Each switch off alone, replay 01 | parts: `SHORT_LINE` at key 60, `RESET_CLOCK` 60, `HVBJOY_EXACT` 180, `REFRESH_AT_BEAM` 60; `BEAM_EXACT=0` runs the old path, which calls no hooks, so the compare gets no samples |
| Upstream's tests on `frame-rules-main` | `pytest tests/v2 tests/test_snes_cycles.py tests/test_beam_events.py tests/test_beam_frame_state.py tests/snapshot_guard` 643 passed (`test_disconnect_diagnostics` needs the `lib/` submodules); each commit builds and passes its own tests |

The full measurement (`compare.mts --full --picture --sound --fade`) gives what [../progress.md](../progress.md) records, no
more and no less: replay 01 every RAM, ARAM, S-DSP registers and the picture identical at all 8,592 NMIs, the S-DSP state at 325
(the silent boot and five ENDX reads); replay 02 every RAM and the picture identical at all 23,756, ARAM 81, S-DSP registers 77,
S-DSP state 579 NMIs from 18,503 (the open ENDX read). The local state replays 05 and 07 (07 starts inside its NMI handler: the
`field_nmi` hook) stay in sync to their ends, 8,434 and 4,446 NMIs, as on the current build.

New game-neutral tests: `tests/dma/short_line_test.c` (field lengths from reset, the wrap at 1360, IRQ and line distances,
interlace; on and off), `tests/dma/hvbjoy_refresh_test.c` (`$4212` edges at the access, the automatic read's window, refresh
points), `tests/dma/last_cycle_test.c`, `tests/frame_driver/exact_rules_test.c` (a scripted CPU on the real beam: where the NMI
lands, the 60-clock sequence, the late NMI in its field). Run from `tests/test_beam_events.py`.

Found on the way: our glue's edge stepping walked the beam ahead of the CPU, and `snes_sync_master_clock` with an earlier clock
then moved the beam's clock back without moving H: the field grid slipped 2 clocks (fields 338 and 362 of replay 01). Our old
`align_beam` had hidden it. With `$4212` read at the access nothing walks the beam ahead any more.

## To do before opening

1. Push `frame-rules-main` to the fork and open the PR (needs the owner's OK).
2. Done 2026-11-11: this repository runs on the new stack (the fork's local `develop` = main + every open PR, [README.md](README.md)).

## PR title

Beam: MesenCE's field and interrupt timing (short line, $4212 at the access, DRAM refresh at the beam, NMI recognition)

## PR text

````markdown
## Summary

Five timing rules a frame driver needs to match hardware clock for clock, each in the place it belongs and each behind its own
switch. All five are on by default only with the master-clock APU timeline (`RtlApuUseMasterClock`, #241); nothing changes for
any other host.

| Switch | Rule | Before |
|---|---|---|
| `SNESRECOMP_SHORT_LINE` | line 240 of every other non-interlaced field lasts 1360 clocks, no long dots | every field 357,368 clocks: 2 clocks a field ahead |
| `SNESRECOMP_RESET_CLOCK` | the CPU starts at master clock 186, the beam at 0 | both at 0 |
| `SNESRECOMP_HVBJOY_EXACT` | `$4212` bits 7 and 0 at the read's own clock, as bit 6 already is | at the beam's last sync |
| `SNESRECOMP_REFRESH_AT_BEAM` | DRAM refresh: 40 clocks at H = 538 - (line start & 7) of every line, per point crossed; an APU port access behind a point sees the stall | 40 per 1364 clocks of CPU time, anywhere |
| `SNESRECOMP_BEAM_EXACT` | interrupt sequence 8 cycles / 60 clocks; the field's NMI after the first instruction with a cycle from V=225 H=6; an NMI enabled inside vblank fires after the next instruction; slices end at refresh points | sequence free; NMI after the first instruction to end past V=225; a late enable waits a field |

With these, a host needs no beam alignment of its own. `SnesBeamFrameHooks` lets it add its own work (field start, interrupt
entry, NMI taken, a field NMI it takes itself, slice end); `snes_beam_frame_driver_deadlines` gives the running slice's limits.
The driver's old path is unchanged; the rules run in a separate `run_frame_exact`. `Snes.fieldParity` is new guest state:
savestate v12 (v11 files load with a fresh reset's parity). docs/FRAME_MODEL_HOSTS.md lists the switches.

## Evidence

Each rule is MesenCE 2.2.1's (and fullsnes where it describes it). On Secret of Evermore, with the host's own timing code
removed and only these switches: two replays identical to MesenCE in WRAM, VRAM, CGRAM and OAM at every NMI from power-on
(8,592 and 23,756 NMIs), native and all-interpreted. Turning any one switch off parts the first replay within 3 seconds of play.

Tests, none needing a ROM: `tests/dma/short_line_test.c`, `tests/dma/hvbjoy_refresh_test.c`, `tests/dma/last_cycle_test.c`,
`tests/frame_driver/exact_rules_test.c` (a scripted CPU on the real beam), run from `tests/test_beam_events.py`.
````

## The rules

Each is hardware, taken from MesenCE 2.2.1 and fullsnes, with no Evermore address or value. Evidence: one file each in
[../imperfections/](../imperfections/README.md), with the NMI where the replay parted without it.

| Rule | What snesrecomp's driver does now | Evidence |
|---|---|---|
| CPU starts at master clock 186 after reset | starts at 0 | [short-scanline.md](../imperfections/short-scanline.md) |
| Line 240 lasts 1360 clocks every other non-interlaced field | every field 357,368 clocks | [short-scanline.md](../imperfections/short-scanline.md): the attract seed at NMI 11986 |
| DRAM refresh: 40 clocks at H=538 (534 on lines starting at 4 mod 8), also inside the interrupt sequence | 40 per 1364 clocks of CPU time, anywhere | [dram-refresh.md](../imperfections/dram-refresh.md), [refresh-before-access.md](../imperfections/refresh-before-access.md) |
| The interrupt sequence: 8 cycles, 60 master clocks, and it clears D | not charged; D cleared since main `e9c20732` | [interrupt-cost.md](../imperfections/interrupt-cost.md); D: replay 05, NMI 6844 |
| NMI recognised after the first instruction with a cycle from V=225, H=6 | after the first instruction to end past V=225 | [nmi-recognition.md](../imperfections/nmi-recognition.md), NMI 3876 |
| NMI enabled inside vblank fires after the next instruction | never fires that field | [late-nmi.md](../imperfections/late-nmi.md): the NMI count one behind from frame 5 |
| Automatic joypad read: busy from the first clock 128 past a multiple of 256 and 3 after line 225 begins, for 4352 clocks | from V=225, 4224 clocks | [auto-joypad-read.md](../imperfections/auto-joypad-read.md) |
| `$4212` bit 7 as the access sees it, not at the instruction's start | the instruction's start | [vblank-flag-at-access.md](../imperfections/vblank-flag-at-access.md), keys 3147, 3148, 3655 |
| Port accesses to the SPC700 at their own clock, a refresh stall in front of them | (the hook #241 adds) | [apu-port-handshake.md](../imperfections/apu-port-handshake.md) |

The fitted 20-clock leads our driver had (`AUTO_JOY_READ_LEAD`, `VBLANK_EDGE_LEAD`) are gone: `$4212` is read at each access's
own clock (`interp_bridge_access_lead`, as #241 did for the APU ports); 20 is only the fallback for an access with no lead.

## Not in this PR: the PPU and registers

`src/soe_ppu_fixes.c` changes what the framework's PPU and registers do, from outside, for DMA only. Each belongs in `ppu.c` or `snes.c`
as a change of its own, where it could cover CPU accesses as well:

| Rule | Evidence |
|---|---|
| CGRAM words have 15 bits | [cgram-read-open-bus.md](../imperfections/cgram-read-open-bus.md) |
| `$213B` high byte, bit 7 from the PPU2 open bus | [cgram-read-open-bus.md](../imperfections/cgram-read-open-bus.md) |
| `$4212` bits 1-5 are open bus (ours matches the opcode bytes of `LDA $4212`: a stopgap) | [vblank-flag-at-access.md](../imperfections/vblank-flag-at-access.md) |
| CGRAM writes during rendering go to the colour being read (ours writes colour 0, as measured on one replay) | [cgram-writes-during-rendering.md](../imperfections/cgram-writes-during-rendering.md) |
| VRAM writes outside vblank and forced blank are dropped (snesrecomp's own `TODO`) | [vram-writes-during-rendering.md](../imperfections/vram-writes-during-rendering.md) |

## Not game specific?

The rules are. What stays with the game, in `src/soe_frame_driver.c` through the hooks: the compare (input per field, the
start memory, NMI samples, a MesenCE state's start), free time, the PPU fixes and the deadlines our C bindings read.
