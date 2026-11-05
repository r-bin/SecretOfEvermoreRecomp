# 1. Access-exact timing: PR #241

**Status:** open, https://github.com/RetroPortingToolKit/snesrecomp/pull/241, head `810d438a`. Conflicts with main. Waiting for our
replay recipe (`mstan`, 2026-11-03: "the author's reproducible Secret of Evermore replay recipe remains required").

Commits (same patches on the PR and on our pin): `a9df3563`, `f192b16f`, `f032712d`, `37157d4a`, `a395f9ed`, `fb289e33`, `1b61ec4d`,
plus `mstan`'s repairs on the PR.

## To do before it can merge

1. Publish what the recipe names (tracker in [README.md](README.md)), then post the comment below.
2. Merge main. One conflict, `apu_runToGuestCycle` in `apu.c`: #258 made the same drain fix as `3856deb1`. Keep the PR's side.
3. Take out everything under [Not proposed](README.md#not-proposed) marked #241. `mstan`'s snapshot work serializes the DSP pipeline, so
   check it before removing the per-sample DSP fields.
4. Replace the fitted DMA constant (below). Remove the two comments naming the game (`common_rtl.c`, `snes.c`).

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

Result, with the later PRs of this series, on current main: WRAM, VRAM, CGRAM, OAM and SRAM identical to MesenCE at every frame of
five replays, 71,055 frames, interpreted and compiled. With this PR alone, interpreted: identical through frame 5997 of the attract
replay; the next PR covers the rest.

Limits: `RTL_MASTER_PER_SEC` is the NTSC master clock, PAL is not handled. DMA's end alignment assumes 6-clock code. The result also
needs a frame driver with the interrupt rules in the recipe; ours is game glue today and could move here in a follow-up.
```

## Comment to post

Answers `mstan`'s request and `pogoflow`'s two questions. Fill the heads in after pushing.

```markdown
Sorry for the wait. Here is the recipe; ROMs and generated C stay out of it.

- **ROM:** Secret of Evermore (USA), headerless, SHA-256 `17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db`.
- **Consumer:** https://github.com/r-bin/SecretOfEvermoreRecomp at `<head>`, snesrecomp at `<pr-stack-on-main head>`.
- **Tools:** https://github.com/r-bin/everscript-vscode at `<head>`.
- **MesenCE:** 2.2.1, https://github.com/r-bin/MesenCE, headless with `--testrunner --snes.ramPowerOnState=AllZeros --snes.disableFrameSkipping=true`.
- **Input:** none. Power-on, 14,424 frames, no button pressed. The five-line replay file is in the recipe.
- **Commands and expected output:** `docs/pr/README.md`, section Reproduce, in the consumer repository.

Not compared: frames before 120 (the RNG is copied from MesenCE at 115) and the WRAM bytes `$02-$0F`, `$10E-$116`, `$1FEE`, `$1FF4`.
The two replays that start from a MesenCE state copy nothing and compare every frame.

Each part turned off alone, attract replay, first frame WRAM differs from MesenCE:

| Off | Parts at |
|---|---|
| master-clock timeline | 319 |
| SPC access cycles, DMA alignment | 320 |
| host access clock | 362 |
| per-clock S-DSP, timer phase | 1492 |
| timer access cycle | 2468 |
| `$F3` access cycle | 2757 |
| `g_snes_dma_charged` | 3876 |
| line-0 HDMA wait | 4978 |
| H/V flip-flops, empty `$420B` is free | 5998 |
| latch position, `$2137` open bus | 11987 |

@pogoflow on your two points: the overshoot is gone with #258 and `3856deb1`. The input delay is here too: a CPU write at half-cycle
`h` is seen by SPC accesses from `h + 2` on (`rtl_apu_write_target`).

I'm removing the parts no replay needs and rebasing on main.
```

## Required

Replay 01, on current main. Native and interpreted part at the same frame.

| Change | Switch | Parts at |
|---|---|---|
| Master-clock APU timeline | `SNESRECOMP_APU_MASTER_CLOCK=0` | 319 |
| SPC port accesses on their own cycle | `SNESRECOMP_SPC_ACCESS_CYCLES=0` | 320 |
| — access numbers mapped to hardware cycles | `ABL_HW_CYCLE` | 320 |
| — hold the opcode that would touch a port too early | `ABL_HOLD` | 320 |
| — a port read takes the writes due by its cycle | `ABL_DRAIN_TO` | 320 |
| — port queue stays in time order | `ABL_QUEUE_ORDER` | 320 |
| — SPC starts 3 cycles after reset | `ABL_RESET3` | 320 |
| A write lands at the end of its bus cycle | `ABL_WRITE_LEAD` | 320 |
| General DMA alignment | `SNESRECOMP_DMA_ALIGN=0` | 320 |
| — end alignment | `SNESRECOMP_DMA_END_SPEED=0` | 320 |
| Host access clock (`RtlSetCpuAccessClockHook`) | `ABL_ACCESS_HOOK` | 362 |
| S-DSP per clock | `SNESRECOMP_DSP_PER_CLOCK=0` | 1492 |
| SPC timer phase | `SNESRECOMP_SPC_TIMER_PHASE=0` | 1492 |
| Timer accesses on their cycle | `ABL_TIMERS_TO` | 2468 |
| `$F3` accesses on their cycle | `ABL_DSP_TO` | 2757 |
| `g_snes_dma_charged` | `ABL_DMA_CHARGED` | 3876 |
| DMA waits behind the line-0 HDMA init | `ABL_HDMA_INIT` | 4978 |
| Latch leaves the H/V flip-flops alone | `ABL_HV_TOGGLE` | 5998 |
| Empty `$420B` costs nothing | `ABL_DMA_ZERO` | 5998 (VRAM) |
| Latch position at the access's clock | `ABL_HV_HOOK` | 11987 |
| `$2137` reads open bus | `ABL_OPENBUS2137` | 11987 |

An access's own port lead stays too, as the exception in [README.md](README.md#not-proposed). Everything else in the PR's commits goes.

## Not game specific?

All of it is hardware behaviour, but four things are not clean yet:

- **Fitted constant: `dma_lead = 30`.** That is a FastROM `STA abs`. Fix: `interp_bridge_access_lead(0x420b, 20) + 10`, the write's real
  clock plus the rest of its bus cycle and the next fetch. Tested (`ABL_DMA_LEAD_DYN`): identical on all five replays, both modes.
- **`dma_end_speed = 6`** assumes the next opcode comes from FastROM. A SlowROM game needs 8. Not fixed, not testable with this game.
- **NTSC only.** `RTL_MASTER_PER_SEC` and the SPC rate have no PAL case.
- **No caller inside snesrecomp.** `RtlApuUseMasterClock`, `RtlSetCpuAccessClockHook` and `g_snes_dma_charged` are used only by our
  glue, and upstream's AGENTS.md warns against unused abstractions. They need a line in the PR, or the frame driver rules moved in.

## Good for future games?

Yes, for any game that handshakes with its sound driver in tight loops: `pogoflow` reports needing nearly the same fixes for Secret of
Mana. SPC access cycles, DMA alignment and the per-clock DSP are on by default, so they reach every game; `mstan` ran MMX, ALttP, SMW
and Super Metroid on the PR with no regression. The master-clock timeline is opt-in.

## Reproduce

Replay 01 reproduces all of it. Setup: [README.md](README.md#reproduce), steps 1-6. Then, per row above, the switch in step 7. Rows named
`ABL_*` need [ablation-gates.patch](ablation-gates.patch).

With this PR alone (no PR 2), replay 01 parts at 5998, as its body says: that is `SNESRECOMP_PPU_RENDER_READS=0` on the full stack.
