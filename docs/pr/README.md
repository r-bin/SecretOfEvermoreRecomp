# Changes to snesrecomp, on the way upstream

Every change we made to snesrecomp, as the pull requests they should become. One file per PR: its title, its text, what is
required and how to reproduce it. A change stays only if it is required, not game specific and useful to future games; the
rest is in [Not proposed](#not-proposed).

Upstream: https://github.com/RetroPortingToolKit/snesrecomp. Measured 2026-11-07 on main `816bc5ac` plus our stack.

## Tracker

| # | File | PR | State | Depends on | Next step |
|---|---|---|---|---|---|
| 1 | [01-access-exact-timing.md](01-access-exact-timing.md) | [#241](https://github.com/RetroPortingToolKit/snesrecomp/pull/241) | **open**, conflicts with main, waiting for our replay recipe | — | Post the recipe (the comment in its file), resolve the conflict, trim |
| 2 | [02-ppu-reads-during-rendering.md](02-ppu-reads-during-rendering.md) | not opened | draft | 1 | Game-neutral test, then open |
| 3 | [03-bus-exact-compiled-code.md](03-bus-exact-compiled-code.md) | not opened | draft, **blocked** on a decision | 1 | Decide against [#250](https://github.com/RetroPortingToolKit/snesrecomp/pull/250)/[#251](https://github.com/RetroPortingToolKit/snesrecomp/pull/251) |
| 4 | [04-compiled-resume.md](04-compiled-resume.md) | not opened | draft | 3 | Follows 3 |

**Why each one, and the doubts:**

| # | Needed for sync? | Doubts |
|---|---|---|
| 1 | Yes, from NMI 1 | Full result needs our frame driver; NTSC only; hooks without a caller; measured on one game |
| 2 | Yes, from NMI 5998 (replay 01 only) | Checked against one game's reads; one CGRAM case not modelled |
| 3 | Compiled code only, from NMI 320 | **#250/#251 may replace it**, untested |
| 4 | **No.** Only the native share, 16.31 % → 99.70 % | Speed gain not measured; built on 3, so it moves with 3; can be dropped |

State values: draft, open, conflicts, merged, closed. Update this table when a state changes, and the file's Status line with it.

The framework side of all four is `framework/snesrecomp.patch` in this repository: the tree of the local snesrecomp branch
`pr-stack-on-main` against main ([../framework.md](../framework.md)). A reviewer needs only this repository.

## What was tested

The whole stack on current main (`pr-stack-on-main`, `eb2b5e88`: main + #241 + the later commits + the PB fix
([04](04-compiled-resume.md)) + test stubs for PRs 2-4). Against MesenCE 2.2.1 at every NMI from power-on, nothing copied in or
masked, native and all-interpreted alike ([../progress.md](../progress.md)):

| Replay | NMIs | WRAM, VRAM, CGRAM, OAM, SRAM |
|---|---|---|
| `01-no-inputs-1-loop.mmo` | 8,592 | identical |
| `02-new-game-to-flowers.mmo` | 23,756 | identical |

**Required** means: with the change turned off, a replay leaves sync. Each PR file lists the first NMI it parts at, on both
replays. Every change was turned off alone, native and interpreted, on both (`tools/compare/probes/ablate.mts`, 2026-11-07).
Replay 01 needs every required change. 02 needs one thing 01 does not: the `$4212` polling tick with compiled resume off
(PR 4, NMI 19685).

Upstream's tests on `pr-stack-on-main`: `pytest tests/v2 tests/test_snes_cycles.py` **596 passed** (with `lib/` submodules
initialized; without them `test_disconnect_diagnostics` cannot build, on plain main too). The three test stubs that made it so are
one commit per PR: `7359f4f6` (PR 2), `666e9f60` (PR 3), `eb2b5e88` (PR 4). `tests/run_c_tests.sh` passes on macOS as far as plain
main does: both stop at the lobby test, which does not build on macOS, and after it need `pkg-config`; not run on Linux yet.

## Reproduce

Everything is one setup. The ROM stays yours; generated C and ROM bytes stay out of git.

1. **ROM:** Secret of Evermore (USA), headerless, SHA-256
   `17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db`, CRC32 `a5c0045e`.
2. **Reference:** MesenCE 2.2.1, the release (nesdev-org/MesenCE tag `2.2.1`, commit `20ba206c`), as `/Applications/Mesen.app` or
   `$MESEN`. Default settings; the harness runs it headless with `--testrunner --snes.ramPowerOnState=AllZeros
   --snes.disableFrameSkipping=true`.
3. **Sources:** this repository with its submodules. Nothing else.
4. **Build:**
   ```sh
   git clone --recursive https://github.com/r-bin/SecretOfEvermoreRecomp && cd SecretOfEvermoreRecomp
   bash tools/regen.sh --rom /path/to/rom.sfc
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
   ```
5. **Replays** are in the repository: MesenCE movies from power-on, input and settings only.
6. **Compare**, about 2 minutes:
   ```sh
   node tools/compare/compare.mts mesen recomp /path/to/rom.sfc --replay tools/compare/replays/01-no-inputs-1-loop.mmo --regions wram,vram,cgram,oam --verbose
   ```
   Expected: `in sync through counter $0100 = 8592 (≈143.0 s), 8592 checkpoints`.
7. **Turn one change off** with its switch and run the same command. It stops at the NMI its PR file names, for example:
   ```sh
   SNESRECOMP_DMA_ALIGN=0 node tools/compare/compare.mts mesen recomp /path/to/rom.sfc --replay tools/compare/replays/01-no-inputs-1-loop.mmo --regions wram,vram,cgram,oam --verbose
   ```
   Expected: `wram differ at counter $0100 = 26`.

**Switches named `ABL_*`** do not exist upstream. They come from [ablation-gates.patch](ablation-gates.patch), one gate per change
that has no switch of its own; a gate does nothing unless its variable is set, and it is never part of a PR. Build them apart from
your normal build:
```sh
git -C snesrecomp apply ../docs/pr/ablation-gates.patch      # after regen.sh applied the framework patch
cmake -S . -B build-abl -DCMAKE_BUILD_TYPE=Release && cmake --build build-abl -j
git -C snesrecomp apply -R ../docs/pr/ablation-gates.patch   # back to the framework patch alone
```

**All of it at once:** `tools/compare/probes/ablate.mts` runs MesenCE once, keeps its hashes in `out/compare/ablate/`, then runs
each variant alone against them, several at a time. About two minutes per variant on replay 01.
```sh
echo '[{"name":"no DMA alignment","env":{"SNESRECOMP_DMA_ALIGN":"0"}},{"name":"no hold","env":{"ABL_HOLD":"1"},"exe":"build-abl/SecretOfEvermoreSNESRecomp"}]' > variants.json
node tools/compare/probes/ablate.mts tools/compare/replays/01-no-inputs-1-loop.mmo variants.json --rom /path/to/rom.sfc --jobs 6
```
With `"interpreted": true` a variant runs all-interpreted.

**What the comparison holds the recomp to:** every byte of WRAM, VRAM, CGRAM and OAM at every NMI from power-on. Both sides start
with zeroed RAM and empty battery RAM. One accommodation, for a MesenCE bug: in compare runs the recomp's ROM copy has MesenCE's 4th
MB, one page repeated ([../imperfections/mesen-rom-mirror.md](../imperfections/mesen-rom-mirror.md)). The recomp runs on our own frame
driver, `src/soe_frame_driver.c`, not snesrecomp's `beam_frame_driver.c`. See below.

## Not proposed

Tested off, alone, in both modes, on both committed replays (2026-11-07): nothing differed in any region, sound and picture
included (and on the five replays of 2026-11-05 before). They come out
of [#241](01-access-exact-timing.md) and the later commits unless someone brings a test that needs them.

| Change | Switch | From | Why it goes |
|---|---|---|---|
| `$4212` H-blank at the read's clock | `SNESRECOMP_HBLANK_EXACT=0` | #241 | No replay needs it |
| WRIO gates the `$2137` latch | `ABL_WRIO_GATE` | #241 | No replay needs it |
| WRIO reads `$FF` after reset | `ABL_WRIO_POWERON` | #241 | No replay needs it |
| 16-bit port reads sync per byte | `ABL_WORD_PORT` | #241 | No replay needs it |
| DSP key-on delay, per-sample path | `SNESRECOMP_DSP_KON_DELAY=0` | #241 | Unused with the per-clock DSP; without it the run parts at 1492 either way |
| DSP ENDX point, per-sample path | `SNESRECOMP_DSP_ENDX_AT=0` | #241 | Same |
| DSP shared rate counter, per-sample path | `SNESRECOMP_DSP_GLOBAL_COUNTER=0` | #241 | Same |
| Nested interpreter stops at the deadline | `SNESRECOMP_NESTED_DEADLINE=0` | bus-exact | No effect, with or without compiled resume |
| Beam sync when the outermost bridge run returns | `ABL_BEAM_HOST` | compiled interrupts | No effect, with or without compiled resume |
| `SNESRECOMP_TIER_STATS` | — | own commit | Measures, changes nothing. Our Native column uses it, so it stays on our branch |
| Diagnostics and tuning knobs | `SNESRECOMP_PORT_LOG`, `_DMA_LOG`, `_DSP_LOG`, `_SPC_EXEC_LOG`, `_SYNC_WATCH`, `_HV_LOG`, `_PPU_READ_LOG`, `_APU_KR`, `_APU_KW`, `_APU_LEAD`, `_SPC_READ_BIAS`, `_SPC_WRITE_BIAS`, `_DMA_LEAD`, `_DMA_END_SPEED` | #241 | Change nothing by default. `tools/compare/probes/port-handshakes` and `ppu-reads` use some of them |

One exception stays, for a reason replays cannot show: **an access's own lead for port timing** (`ABL_ACCESS_LEAD`). Without it no
replay changes, but ports fall back to a constant 20 clocks, which is right only for an absolute-addressed FastROM access: a
constant fitted to this game.

## Our own code that could follow

`src/soe_frame_driver.c` holds game-neutral hardware rules that snesrecomp's `beam_frame_driver.c` lacks: late NMI, the interrupt
sequence's cost, NMI recognition, line 240's short field, refresh position, the auto-joypad window, CPU start at clock 186
([../upstream-access-timing.md](../research/upstream-access-timing.md) part F). They are not changes to snesrecomp, so they have no file here.
They matter anyway: until they move, nobody but this port gets the full result, and #241's hooks have no caller inside snesrecomp.
