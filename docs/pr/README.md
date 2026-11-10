# Changes to snesrecomp, on the way upstream

Every change we made to snesrecomp, as the pull requests they should become. One file per PR: its title, its text, what is
required and how to reproduce it. A change stays only if it is required, not game specific and useful to future games; the
rest is in [Not proposed](#not-proposed).

Upstream: https://github.com/RetroPortingToolKit/snesrecomp. Measured 2026-11-07 on main `816bc5ac` plus our stack; since 2026-11-10 the pin is main `e9c20732`, which has #241 (replay 01 checked
again, below).

## Tracker

| # | File | PR | State | Depends on | Next step |
|---|---|---|---|---|---|
| 1 | [01-access-exact-timing.md](01-access-exact-timing.md) | [#241](https://github.com/RetroPortingToolKit/snesrecomp/pull/241) | **merged** 2026-11-09, squashed as `c1f84f61` | — | Trim the [Not proposed](#not-proposed) parts in a follow-up PR |
| 2 | [02-ppu-reads-during-rendering.md](02-ppu-reads-during-rendering.md) | [#278](https://github.com/RetroPortingToolKit/snesrecomp/pull/278) | **open** 2026-11-10, fork branch `ppu-render-reads-main` | 1 (merged) | Game-neutral test in `tests/ppu/` |
| 3 | [03-bus-exact-compiled-code.md](03-bus-exact-compiled-code.md) | not opened | draft, **blocked** on a decision | 1 | Decide against [#250](https://github.com/RetroPortingToolKit/snesrecomp/pull/250)/[#251](https://github.com/RetroPortingToolKit/snesrecomp/pull/251) |
| 4 | [04-compiled-resume.md](04-compiled-resume.md) | not opened | draft | 3 | Follows 3 |
| 5 | [05-brightness-rule.md](05-brightness-rule.md) | not opened | branch `brightness-rule-main` pushed | — | Ask upstream whether a parity switch is welcome |
| 6 | [06-frame-driver.md](06-frame-driver.md) | not opened | draft, no branch yet | 1 | Port `src/soe_frame_driver.c` (#241 is merged) |
| 7 | [07-hd-mode7-apple-gl.md](07-hd-mode7-apple-gl.md) | [#279](https://github.com/RetroPortingToolKit/snesrecomp/pull/279) | **open** 2026-11-10, fork branch `hd-mode7-no-fp64-main` | — | A non-Mac GL4 run (asked in the PR) |

**Why each one, and the doubts:**

| # | Needed for sync? | Doubts |
|---|---|---|
| 1 | Yes, from NMI 1 | Full result needs our frame driver; NTSC only; hooks without a caller; measured on one game |
| 2 | Yes, from NMI 5998 (replay 01 only) | Checked against one game's reads; one CGRAM case not modelled |
| 3 | Compiled code only, from NMI 320 | **#250/#251 may replace it**, untested |
| 4 | **No.** Only the native share, 16.31 % → 99.70 % | Speed gain not measured; built on 3, so it moves with 3; can be dropped |
| 5 | **No.** The picture only: 289 (01) and 948 (02) NMIs differ without it, none with it | A switch for parity with one emulator; upstream chose bsnes's rule on purpose |
| 6 | Yes, from NMI 5 (the late NMI); today supplied by our glue | Large; the fitted 20-clock leads must go first; no game-neutral test yet |
| 7 | **No.** HD Mode 7 on macOS only (black without it) | Single precision is not bit-exact at texel edges (0.03 % of samples); checked on one Mac |

State values: draft, open, conflicts, merged, closed. Update this table when a state changes, and the file's Status line with it.

The framework side of all of them is `framework/snesrecomp.patch` in this repository: the tree of `develop` in our fork
https://github.com/r-bin/snesrecomp against main ([../framework.md](../framework.md)). A reviewer needs only this repository.
The fork also holds one branch per PR: `spc-access-cycles` (1), `ppu-render-reads` (2), `aot-bus-exact` (3), `aot-resume` (4),
`brightness-rule` (5); PR 2 is opened from `ppu-render-reads-main` (main `e9c20732` + `d6e7b60d` + stub `e4001e81`). Each older branch's tip is where its PR is in the stack, so it includes the PRs before it.

## What was tested

The whole stack on current main (the fork's `develop`, `d56d8da9`: main + #241 + the later commits + the PB fix
([04](04-compiled-resume.md)) + test stubs for PRs 2-4; `53406604` adds PR 5, off by default, so the memory results stand). Against MesenCE 2.2.1 at every NMI from power-on, nothing copied in or
masked, native and all-interpreted alike ([../progress.md](../progress.md)):

| Replay | NMIs | WRAM, VRAM, CGRAM, OAM, SRAM |
|---|---|---|
| `01-no-inputs-1-loop.mmo` | 8,592 | identical |
| `02-new-game-to-flowers.mmo` | 23,756 | identical |

**Required** means: with the change turned off, a replay leaves sync. Each PR file lists the first NMI it parts at, on both
replays. Every change was turned off alone, native and interpreted, on both (`tools/compare/probes/ablate.mts`, 2026-11-07).
Replay 01 needs every required change. 02 needs one thing 01 does not: the `$4212` polling tick with compiled resume off
(PR 4, NMI 19685).

Upstream's tests on the fork's `develop`: `pytest tests/v2 tests/test_snes_cycles.py` **596 passed** (with `lib/` submodules
initialized; without them `test_disconnect_diagnostics` cannot build, on plain main too). The three test stubs that made it so are
one commit per PR: `4c10057c` (PR 2), `87961ea1` (PR 3), `d56d8da9` (PR 4); after the 2026-11-10 rebase `cbddf51e`, `0fb786df`, `ac7c172b`. `tests/run_c_tests.sh` passes on macOS as far as plain
main does: both stop at the lobby test, which does not build on macOS, and after it need `pkg-config`; not run on Linux yet.

**After #241 merged (2026-11-10):** upstream squashed it as `c1f84f61` (merged in `7925230a`), with the same code as ours. The
fork's `develop` was rebased onto main `e9c20732` without our copy of #241: the 11 later commits only, conflicts with #250/#251's
region-paced emission resolved by keeping both (both opt-in). `framework/snesrecomp.patch` is now that stack alone. Replay 01,
native, against MesenCE 2.2.1: `in sync through counter $0100 = 8592`, 8592 checkpoints; `replay_01_recorded` passes.
Not re-run since: replay 02, interpreted mode, upstream's pytest.

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
sequence's cost, NMI recognition, line 240's short field, refresh position, the auto-joypad window, CPU start at clock 186. Since
2026-11-07 it holds nothing else: [06-frame-driver.md](06-frame-driver.md) lists each rule with its evidence and what must change
before it can go upstream. Until it does, nobody but this port gets the full result, and #241's hooks have no caller inside snesrecomp.

`src/soe_ppu_fixes.c` changes what the framework's PPU and registers do from outside, for DMA only (open bus, 15-bit CGRAM, writes
during rendering). Each belongs in `ppu.c` or `snes.c` as its own change, listed in the same file.
