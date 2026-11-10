# 6. The beam frame driver's interrupt and field rules

**Status:** draft, no code on a snesrecomp branch yet. Depends on [#241](01-access-exact-timing.md): the access-clock hook and the
master-clock APU timeline it feeds are #241's, and without this PR they have no caller inside snesrecomp.

The code is ours today: `src/soe_frame_driver.c`, which started as a copy of snesrecomp's `runner/src/beam_frame_driver.c`. Since
2026-11-07 it holds only hardware rules. What the PPU and registers lack is in `src/soe_ppu_fixes.c` (below), the compare hooks are
named `soe_compare_*`, and the tier statistics `SOE_BANK_STATS` are tooling.

## To do before opening

1. #241 merged on 2026-11-09. Port the rules below into `beam_frame_driver.c`, one commit per rule, each with an environment
   switch to turn it off (as #241 does), all on by default only with `RtlApuUseMasterClock`.
2. Give the driver a hook for what stays game side: the NMI hook (the compare harness) and the start of a run from a saved state.
3. Replace the fitted constants (below) by the access's own lead, which #241 already supplies for port accesses.
4. Point our glue at the framework's driver, delete `src/soe_frame_driver.c`, and run replay 01 native and interpreted against
   MesenCE. Replay 02 too: it is the one with input.
5. A game-neutral test per rule in `tests/`: a frame driver harness with a tiny CPU program (enable NMI inside vblank, poll `$4212`,
   a long DMA across V=225), checked against the clocks MesenCE gives.

## The rules

Each is hardware, taken from MesenCE 2.2.1 and fullsnes, with no Evermore address or value. Evidence: one file each in
[../imperfections/](../imperfections/README.md), with the NMI where the replay parted without it.

| Rule | What snesrecomp's driver does now | Evidence |
|---|---|---|
| CPU starts at master clock 186 after reset | starts at 0 | [short-scanline.md](../imperfections/short-scanline.md) |
| Line 240 lasts 1360 clocks every other non-interlaced field | every field 357,368 clocks | [short-scanline.md](../imperfections/short-scanline.md): the attract seed at NMI 11986 |
| DRAM refresh: 40 clocks at H=538 (534 on lines starting at 4 mod 8), also inside the interrupt sequence | 40 per 1364 clocks of CPU time, anywhere | [dram-refresh.md](../imperfections/dram-refresh.md), [refresh-before-access.md](../imperfections/refresh-before-access.md) |
| The interrupt sequence: 8 cycles, 60 master clocks, and it clears D | not charged; D kept | [interrupt-cost.md](../imperfections/interrupt-cost.md); D: replay 05, NMI 6844 |
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
