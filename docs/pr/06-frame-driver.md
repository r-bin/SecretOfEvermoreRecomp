# 6. The beam frame driver's interrupt and field rules

**Status:** planned as five PRs (below), no code on a snesrecomp branch yet. [#241](01-access-exact-timing.md) is merged; its
access-clock hook and master-clock APU timeline have no caller inside snesrecomp until these land.

The code is ours today: `src/soe_frame_driver.c`, which started as a copy of snesrecomp's `runner/src/beam_frame_driver.c`. Since
2026-11-07 it holds only hardware rules. What the PPU and registers lack is in `src/soe_ppu_fixes.c` (below), the compare hooks are
named `soe_compare_*`, and the tier statistics `SOE_BANK_STATS` are tooling.

## Plan: five PRs, each rule where it belongs (2026-11-10)

Our driver patches several rules from outside: it moves the beam's H after the fact (short line), tops up refresh after each
slice, and stops slices 20 clocks before each `$4212` edge. Upstream they belong in `snes.c`, where the beam, refresh and
`$4212` live; only the interrupt rules belong in `beam_frame_driver.c`. Main `e9c20732` already has two pieces: the interrupt
push clears D (`cpu_push_interrupt_frame_at`), and `$4212` bit 6 is read at the access's own clock (`interp_bridge_access_lead`,
#241's `SNESRECOMP_HBLANK_EXACT`).

| PR | Where | Rules | Replaces in `src/soe_frame_driver.c` |
|---|---|---|---|
| 6.1 Beam | `snes.c` | line 240 lasts 1360 clocks every other non-interlaced field; the CPU starts at clock 186 | `align_beam`, `align_field_length`, `mesen_line0`, `RESET_CLOCKS` |
| 6.2 `$4212` at the access | `snes.c` | bit 7 and bit 0 at the read's clock, as bit 6 already is; the auto-joypad window from the first clock 128 mod 256 at least 3 after line 225, 4352 clocks | `edge_*`, `auto_joypad_begin`, both fitted leads |
| 6.3 DRAM refresh | `snes.c` | 40 clocks at H=538 (534 on lines starting at 4 mod 8), also in front of a port access | `refresh_begin/end`, the stall in `access_clock` |
| 6.4 Interrupt sequence | driver | 8 cycles, 60 clocks, a refresh point inside it stalls it | the charge in `run_interrupt` |
| 6.5 NMI timing | driver | recognised after the first instruction with a cycle from V=225 H=6; enabled inside vblank, it fires after the next instruction | `finish_nmi_approach`, `soe_last_cycle.c`, the late-NMI watch |

Order: 6.1 first, since 6.2 and 6.3 count from line 225 and need the beam to be MesenCE's in every field. Each PR is one commit
per rule with an off switch, on by default only with `RtlApuUseMasterClock`.

How each is checked: our glue drops the code the PR replaces and keeps the rest, then replay 01 must stay in sync (native and
interpreted), and replay 02 for 6.2 (it has input). With the switch off, the replay parts at the NMI the rule's imperfection file names.
After 6.5, our glue calls `snes_beam_frame_driver_run_frame` and `src/soe_frame_driver.c` keeps only the compare hooks and starting
from a MesenCE state, through a hook the driver offers.

Each needs a test that does not depend on our game: a tiny CPU program (enable NMI inside vblank, poll `$4212`, a long DMA across
V=225) checked against MesenCE's clocks.

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

**Fitted, to replace before opening:** the 20-clock leads `AUTO_JOY_READ_LEAD` and `VBLANK_EDGE_LEAD` are right for `LDA`/`BIT`
absolute in FastROM only, as `APU_ACCESS_LEAD` was before #241 gave each access its own lead.

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

The rules are. Two things in our file are not and stay with the game: the compare hooks, and starting a run from a MesenCE save
state (`soe_mesen_state.c`).
