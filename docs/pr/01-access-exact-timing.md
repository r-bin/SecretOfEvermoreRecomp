# 1. Access-exact timing: PR #241

**Status:** merged 2026-11-09, https://github.com/RetroPortingToolKit/snesrecomp/pull/241: `mstan` squashed it as `c1f84f61`
("Preserve access-exact audio and DMA timing on current runtime", merge `7925230a`), the same code as our commits. Our copy is
gone from the fork's `develop` and from `framework/snesrecomp.patch`; replay 01 is still in sync on main `e9c20732`
([README.md](README.md#what-was-tested)). What is left of the list below (steps 3 and 4) is a follow-up PR.

Commits (same patches on the PR and on our pin): `a9df3563`, `f192b16f`, `f032712d`, `37157d4a`, `a395f9ed`, `fb289e33`, `1b61ec4d`,
plus `mstan`'s repairs on the PR.

## To do before it can merge

1. Publish what the recipe names (tracker in [README.md](README.md)), then post the comment below.
2. Merge main. One conflict, `apu_runToGuestCycle` in `apu.c`: #258 made the same drain fix as `3856deb1`. Keep the PR's side.
3. Take out everything under [Not proposed](README.md#not-proposed) marked #241. `mstan`'s snapshot work serializes the DSP pipeline, so
   check it before removing the per-sample DSP fields.
4. Replace the fitted DMA constant (below). Remove the two comments naming the game (`common_rtl.c`, `snes.c`).
5. Run replay 01 on the result. The removed parts were each tested off alone, never all together, and this branch itself has not run on
   main: only the fork's `develop` has.

## PR title

Access-exact APU port timing, per-clock S-DSP, hardware DMA alignment

## PR text

Replaces the author's part of the body. `mstan`'s sections stay as they are.

```markdown
My goal was a generic change that lets a game sync with a cycle-accurate emulator. Secret of Evermore parts from MesenCE 2.2.1 at
NMI 319, because the SPC700 handles all of an opcode's bus accesses on its first cycle and the CPU and DMA are charged per block,
so `$2140-$2143` handshakes, timer reads and `$420B` drift from hardware.

What changes:
- SPC700 port, timer and `$F3` accesses happen on their own cycle.
- CPU port accesses sync the SPC to their exact clock, on a master-clock timeline (opt-in: `RtlApuUseMasterClock`).
- The S-DSP runs per clock.
- General DMA follows hardware's start and end alignment, and waits behind the line-0 HDMA init event.
- The H/V latch takes the beam at the access's own clock and leaves the flip-flops alone; `$2137` reads open bus.

Every part is required: each one turned off alone makes the attract replay part from MesenCE (table in the comment below).

Result, with the later PRs of this series, on current main: WRAM, VRAM, CGRAM, OAM and SRAM identical to MesenCE at every NMI of
two replays from power-on (32,348 NMIs), interpreted and compiled, with nothing copied in or masked. With this PR alone,
interpreted: identical through NMI 5997 of the attract replay; the next PR covers the rest.

Limits and doubts:
- `RTL_MASTER_PER_SEC` is the NTSC master clock, PAL is not handled. DMA's end alignment assumes 6-clock code.
- The result needs a frame driver with the interrupt rules in the recipe; ours is game glue today and could move here in a follow-up.
  Until then, no other game gets the full result.
- `RtlApuUseMasterClock`, `RtlSetCpuAccessClockHook` and `g_snes_dma_charged` have no caller in snesrecomp yet.
- Sync is measured on one game. For other games there is the four-game regression run below, which shows nothing broke, not that they
  gain.
```

## Comment to post

Answers `mstan`'s request and `pogoflow`'s two questions. Fill the heads in after pushing.

```markdown
Sorry for the wait. Here is the recipe; ROMs and generated C stay out of it.

- **ROM:** Secret of Evermore (USA), headerless, SHA-256 `17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db`.
- **Consumer:** https://github.com/r-bin/SecretOfEvermoreRecomp at `<head>`. It builds and tests on its own; this PR and the
  later ones are its `framework/snesrecomp.patch` on main `816bc5ac`, applied by `tools/regen.sh`.
- **MesenCE:** the 2.2.1 release (nesdev-org/MesenCE tag `2.2.1`, `20ba206c`), default settings, run headless with
  `--testrunner --snes.ramPowerOnState=AllZeros --snes.disableFrameSkipping=true`.
- **Input:** `tools/compare/replays/01-no-inputs-1-loop.mmo`, a MesenCE 2.2.1 movie from power-on with no button pressed
  (8,592 NMIs); `02-new-game-to-flowers.mmo` plays a new game (23,756 NMIs). Input and settings only.
- **Comparison:** WRAM, VRAM, CGRAM and OAM hashed at every NMI on both sides from power-on. Nothing is copied from MesenCE into
  the recomp and no byte is masked.
- **Commands and expected output:** `docs/testing.md` and `docs/pr/README.md` (section Reproduce) in the consumer repository.

Each part turned off alone, first NMI where WRAM parts from MesenCE (attract replay / new game; native and interpreted alike):

| Off | Parts at |
|---|---|
| empty `$420B` is free, DMA end alignment, latch position, `$2137` open bus | 1 / 1 |
| master-clock timeline, port hold, port drain, SPC reset delay | 2 / 2 |
| SPC access cycles | 11 / 11 |
| DMA alignment | 26 / 26 |
| hardware cycle map, queue order, write lead | 320 / 320 |
| host access clock | 362 / 362 |
| per-clock S-DSP, timer phase | 1492 / 578 |
| timer access cycle | 2464 / 578 |
| `$F3` access cycle | 2757 / 4372 |
| `g_snes_dma_charged` | 3876 / 3221 |
| line-0 HDMA wait | 4978 / in sync |
| H/V flip-flops | 5998 / in sync |

@pogoflow on your two points: the overshoot is gone with #258 and `3856deb1`. The input delay is here too: a CPU write at half-cycle
`h` is seen by SPC accesses from `h + 2` on (`rtl_apu_write_target`).

I'm removing the parts no replay needs and rebasing on main.
```

## Required

Each change turned off alone, on the committed replays from power-on, measured 2026-11-07: the first NMI where WRAM parts from
MesenCE. Native and interpreted part at the same NMI in every row.

| Change | Switch | Replay 01 | Replay 02 |
|---|---|---|---|
| Master-clock APU timeline | `SNESRECOMP_APU_MASTER_CLOCK=0` | 2 | 2 |
| SPC port accesses on their own cycle | `SNESRECOMP_SPC_ACCESS_CYCLES=0` | 11 | 11 |
| — access numbers mapped to hardware cycles | `ABL_HW_CYCLE` | 320 | 320 |
| — hold the opcode that would touch a port too early | `ABL_HOLD` | 2 | 2 |
| — a port read takes the writes due by its cycle | `ABL_DRAIN_TO` | 2 | 2 |
| — port queue stays in time order | `ABL_QUEUE_ORDER` | 320 | 320 |
| — SPC starts 3 cycles after reset | `ABL_RESET3` | 2 | 2 |
| A write lands at the end of its bus cycle | `ABL_WRITE_LEAD` | 320 | 320 |
| General DMA alignment | `SNESRECOMP_DMA_ALIGN=0` | 26 | 26 |
| — end alignment | `SNESRECOMP_DMA_END_SPEED=0` | 1 | 1 |
| Host access clock (`RtlSetCpuAccessClockHook`) | `ABL_ACCESS_HOOK` | 362 | 362 |
| S-DSP per clock | `SNESRECOMP_DSP_PER_CLOCK=0` | 1492 | 578 |
| SPC timer phase | `SNESRECOMP_SPC_TIMER_PHASE=0` | 1492 | 578 |
| Timer accesses on their cycle | `ABL_TIMERS_TO` | 2464 | 578 |
| `$F3` accesses on their cycle | `ABL_DSP_TO` | 2757 | 4372 |
| `g_snes_dma_charged` | `ABL_DMA_CHARGED` | 3876 | 3221 |
| DMA waits behind the line-0 HDMA init | `ABL_HDMA_INIT` | 4978 | in sync |
| Latch leaves the H/V flip-flops alone | `ABL_HV_TOGGLE` | 5998 | in sync |
| Empty `$420B` costs nothing | `ABL_DMA_ZERO` | 1 | 1 |
| Latch position at the access's clock | `ABL_HV_HOOK` | 1 | 1 |
| `$2137` reads open bus | `ABL_OPENBUS2137` | 1 | 1 |

Replay 01 needs every row. The parts at NMI 1 are the reset code's RNG seed, read from the H/V latch: it is compared from
power-on now, so a change there shows at once.

An access's own port lead stays too, as the exception in [README.md](README.md#not-proposed). Everything else in the PR's commits goes.

## Not game specific?

All of it is hardware behaviour, but four things are not clean yet:

- **Fitted constant: `dma_lead = 30`.** That is a FastROM `STA abs`. Fix: `interp_bridge_access_lead(0x420b, 20) + 10`, the write's real
  clock plus the rest of its bus cycle and the next fetch. Tested (`ABL_DMA_LEAD_DYN`): identical on both committed replays, both modes.
- **`dma_end_speed = 6`** assumes the next opcode comes from FastROM. A SlowROM game needs 8. Not fixed, not testable with this game.
- **NTSC only.** `RTL_MASTER_PER_SEC` and the SPC rate have no PAL case.
- **No caller inside snesrecomp.** `RtlApuUseMasterClock`, `RtlSetCpuAccessClockHook` and `g_snes_dma_charged` are used only by our
  glue, and upstream's AGENTS.md warns against unused abstractions. They need a line in the PR, or the frame driver rules moved in.

## Good for future games?

Yes, for any game that handshakes with its sound driver in tight loops: `pogoflow` reports needing nearly the same fixes for Secret of
Mana. SPC access cycles, DMA alignment and the per-clock DSP are on by default, so they reach every game; `mstan` ran MMX, ALttP, SMW
and Super Metroid on the PR with no regression. The master-clock timeline is opt-in.

## Reproduce

Replay 01 reproduces all of it. Setup and the command: [README.md](README.md#reproduce). Then each row's switch on the same
command; rows named `ABL_*` need [ablation-gates.patch](ablation-gates.patch). All rows at once: `tools/compare/probes/ablate.mts`
(README.md, "All of it at once").
