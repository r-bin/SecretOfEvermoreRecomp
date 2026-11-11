# 6. The beam: MesenCE's field and interrupt timing

**Status:** prepared as one PR (2026-11-11): local branch `frame-rules-main` in `../snesrecomp`, five commits on main `80a41241`,
pushed to the fork (`f749659e`), not opened yet. On the fork's stack as branch `pr6-rp` (on `develop-rp`); our glue on that stack: recomp branch
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
| `f749659e` | tests | — | `snes_refresh_charge` at the beam and per CPU time |

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

1. Done 2026-11-11: pushed `frame-rules-main`. Open the PR with the title and text below.
2. Done 2026-11-11: this repository runs on the new stack (the fork's local `develop` = main + every open PR, [README.md](README.md)).

## PR title

Beam: MesenCE's field and interrupt timing (short line, $4212 at the access, DRAM refresh at the beam, NMI recognition)

## PR text

````markdown
## Summary

Five hardware timing rules that let a frame driver match MesenCE clock for clock. Each sits where its hardware lives, has its
own switch, and is on by default only with the master-clock APU timeline (`RtlApuUseMasterClock`, #241). Without that timeline
nothing changes.

```diff
 snes.c
-  every line 1364 clocks, every field 357,368
+  line 240 of every other non-interlaced field: 1360 clocks, no long dots   SNESRECOMP_SHORT_LINE
+    (Snes.fieldParity, toggled at V=0; savestate v12)
   $4212 read
-  bits 7, 0: from the beam's last sync
+  bits 7, 0: at the read's own clock, as bit 6 already is                   SNESRECOMP_HVBJOY_EXACT
 snes_refresh_charge
-  40 clocks per 1364 clocks of CPU time, anywhere
+  40 clocks per refresh point crossed, at H = 538 - (line start & 7)        SNESRECOMP_REFRESH_AT_BEAM
+  an APU port access behind a point sees the stall first
 snes_beam_frame_driver_run_frame
+  if SNESRECOMP_BEAM_EXACT: run_frame_exact (the old path is untouched)
+    CPU starts at master clock 186                                          SNESRECOMP_RESET_CLOCK
+    interrupt sequence: 8 cycles, 60 clocks, a refresh point stalls it
+    field NMI after the first instruction with a cycle from V=225 H=6       (snes/last_cycle.c)
+    NMI enabled inside vblank (RDNMI set): after the next instruction, same field
+    slices end at each refresh point; the beam follows the CPU after each
+    SnesBeamFrameHooks: frame_start, interrupt_entry, nmi_taken, field_nmi, slice_end
```

A host needs no timing code of its own for these: no beam alignment after a field, no walking the beam ahead for `$4212`, no
refresh top-ups. `docs/FRAME_MODEL_HOSTS.md` lists the switches.

### Why

Each rule broke Secret of Evermore in a way a player or a replay sees; each touches code most SNES games contain.

| Rule | What went wrong in Evermore without it | The pattern it affects in any game |
|---|---|---|
| Short line | 2 clocks a field ahead of hardware (774 by field 378); the attract demo seeds its RNG from the H counter after three field-long DMAs and read `$F6` for `$F8`: another demo from NMI 11986 | RNG seeded from `$2137`/`$213C`; anything that counts on the field length over minutes |
| Late NMI | the reset code writes `$4200 = $81` at V=226; the NMI waited a field, so the NMI count was one behind from frame 5 (6 times in the first 4 minutes, at scene loads) | enabling NMI after a forced-blank load, inside vblank |
| NMI recognition | the idle loop `CMP $0100 / BEQ` was interrupted at the other instruction; the 52-clock phase carried into the next fields | every main loop that waits for the NMI |
| Interrupt sequence | every NMI handler ran 55-60 clocks early | every NMI and IRQ handler; where a raster split lands |
| `$4212` at the access | the NMI handler's `BIT $4212 / BNE` auto-read wait ended up to 9 passes early; `LDA $4212 / BMI` left a pass late in the last 20 clocks before line 0 | the vblank and auto-joypad waits in nearly every game |
| Refresh at the beam | the sound upload's `CMP $2140 / BNE` polled one pass early about every 25 handshakes | the standard SPC700 upload handshake |
| Reset clock | the first instruction ran 186 clocks early, so every clock of the boot was off by that | anything timed from power-on |

The cost of not having them: a host that wants hardware timing has to rebuild them outside the framework, as we did (several
hundred lines that moved the beam after the fact), and that is fragile: walking the beam ahead and syncing back quietly moved
the field grid by 2 clocks, which our own alignment hid until this work removed it. Only Evermore is measured; for other games
the right column says where to look, not what we tested.

## Evidence

- **Before:** Secret of Evermore's host carried all five rules itself (moving the beam after each field, walking it 20 clocks
  ahead at vblank edges, topping up refresh). With that code removed and any one switch off, replay 01 (power-on, attract demo)
  parts from MesenCE 2.2.1:
  ```
  SNESRECOMP_SHORT_LINE=0       differs at NMI 60
  SNESRECOMP_RESET_CLOCK=0      differs at NMI 60
  SNESRECOMP_HVBJOY_EXACT=0     differs at NMI 180
  SNESRECOMP_REFRESH_AT_BEAM=0  differs at NMI 60
  ```
  **After:** host timing code removed, all switches at their defaults, WRAM, VRAM, CGRAM and OAM compared at every NMI from
  power-on:
  ```
  replay 01 (attract demo)       in sync, 8,592 of 8,592 NMIs (native and all-interpreted)
  replay 02 (new game, 6.5 min)  in sync, 23,756 of 23,756 NMIs
  ```
  Measured together with #278 (OAM/CGRAM reads during rendering), which replay 01 needs from NMI 5998.
- **Tests, no ROM:**
  ```
  tests/dma/short_line_test.c           field lengths from reset 306,900 / 357,368 / 357,364 ..., interlace (on and off)
  tests/dma/hvbjoy_refresh_test.c       $4212 vblank and auto-read edges at the access, refresh points
  tests/dma/refresh_charge_test.c       one stall per point crossed, none for a teleport (on and off)
  tests/dma/last_cycle_test.c           last bus cycle: internal, I/O, joypad, WRAM, branches, stack
  tests/frame_driver/exact_rules_test.c scripted CPU on the real beam: NMI position, 60-clock sequence, late NMI
  ```
  `pytest tests/v2 tests/test_snes_cycles.py tests/test_beam_events.py tests/test_beam_frame_state.py tests/snapshot_guard`:
  645 passed (macOS). Same speed: 322 frames/s, 94.06 % native (318, 94.05 % before).

## Merge Danger

**Door:** two-way

Every rule has a switch, and the default turns them on only with the master-clock APU timeline. One part does not revert by
switch: savestate v12 adds `Snes.fieldParity`, so a v12 state does not load in an older build (v11 states load here with a
fresh reset's parity).

**Blast Radius:** opt-in

Only hosts that call `RtlApuUseMasterClock`. For them, field lengths, `$4212` and refresh timing change, and the frame driver
takes its exact path. Conflicts with #278 in `snes_beam_at` (a few lines); the resolution is ready, whichever merges second.
Checked on one game and on macOS only.
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
