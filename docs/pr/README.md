# Changes to snesrecomp, on the way upstream

Every change we made to snesrecomp, as the pull requests they should become. One file per PR: its title, its text, what is
required and how to reproduce it. A change stays only if it is required, not game specific and useful to future games; the
rest is in [Not proposed](#not-proposed).

Upstream: https://github.com/RetroPortingToolKit/snesrecomp. Measured 2026-11-05 on main `816bc5ac`.

## Tracker

| # | File | PR | State | Depends on | Next step |
|---|---|---|---|---|---|
| 1 | [01-access-exact-timing.md](01-access-exact-timing.md) | [#241](https://github.com/RetroPortingToolKit/snesrecomp/pull/241) | **open**, conflicts with main, waiting for our replay recipe | — | Publish the recipe, resolve the conflict, trim |
| 2 | [02-ppu-reads-during-rendering.md](02-ppu-reads-during-rendering.md) | not opened | draft | 1 | Game-neutral test, then open |
| 3 | [03-bus-exact-compiled-code.md](03-bus-exact-compiled-code.md) | not opened | draft, **blocked** on a decision | 1 | Decide against [#250](https://github.com/RetroPortingToolKit/snesrecomp/pull/250)/[#251](https://github.com/RetroPortingToolKit/snesrecomp/pull/251) |
| 4 | [04-compiled-resume.md](04-compiled-resume.md) | not opened | draft | 3 | Follows 3 |

State values: draft, open, conflicts, merged, closed. Update this table when a state changes, and the file's Status line with it.

**Blocking all of them:** the recipe needs public commits. `r-bin/SecretOfEvermoreRecomp` has no branches on GitHub, and
`pr-stack-on-main` is a local branch of the snesrecomp submodule. `mstan` can follow nothing below until both are pushed.

## What was tested

The whole stack was rebuilt on current main (local snesrecomp branch `pr-stack-on-main`, `73d951bc`: main + #241 + the later
commits + one fix, [04](04-compiled-resume.md)). Against MesenCE 2.2.1 at every frame, native and all-interpreted:

| Replay | Frames compared | WRAM, VRAM, CGRAM, OAM, SRAM | Picture frames differing (our pin → main) |
|---|---|---|---|
| `01-no-inputs-4m` | 14,305 | identical | 638 → 514 |
| `02-newgame-boy-available` | 17,829 | identical | 501 → 403 |
| `04-newgame-flowers.mmo` | 25,804 | identical | 1,708 → 1,610 |
| `05-flowers-from-state.mmo` | 8,434 | identical | 1,446 → 1,446 |
| `06-flowers-raptors.mmo` | 4,683 | identical | 112 → 112 |

ARAM, S-DSP registers and S-DSP state are as in [../progress.md](../progress.md). The picture gain is upstream's #257.

**Required** means: with the change turned off, replay 01 leaves sync, native or interpreted. Each PR file lists the first frame
it parts at. Every change was turned off alone, in both modes, on current main; the results on our old pin were the same.
Anything replay 01 did not need was tried on 02, 04, 05 and 06 too. None of those replays needed anything 01 did not, so 01
reproduces every required change.

Upstream's tests on `pr-stack-on-main` (`pytest tests/v2 tests/test_snes_cycles.py`): 592 passed, 4 failed. Three fail to link
our new symbols and need stubs (PR 3 and 4 name them); `test_disconnect_diagnostics` also fails on plain main in a checkout
without `lib/` submodules. `tests/run_c_tests.sh` was not run on this branch.

## Reproduce

Everything is one setup. The ROM stays yours; generated C and ROM bytes stay out of git.

1. **ROM:** Secret of Evermore (USA), headerless, SHA-256
   `17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db`, CRC32 `a5c0045e`.
2. **Reference:** MesenCE 2.2.1 from https://github.com/r-bin/MesenCE as `/Applications/Mesen.app` or `$MESEN`. Our binary is dated
   2026-06-05; its source commit was not recorded. Run headless: `--testrunner`, `--snes.ramPowerOnState=AllZeros`,
   `--snes.disableFrameSkipping=true`.
3. **Sources, side by side:** `SecretOfEvermoreRecomp` (the game's glue) and
   [`everscript-vscode`](https://github.com/r-bin/everscript-vscode) (`npm ci`, then `npm run roms` to add the ROM).
4. **Build:**
   ```sh
   cd SecretOfEvermoreRecomp
   git -C snesrecomp checkout pr-stack-on-main
   bash tools/regen.sh --rom /path/to/rom.sfc
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
   ```
5. **Replay 01** needs no recording: it is power-on and 14,424 frames with no button pressed. This file gives the same result as
   the stored one (checked, in sync through 14424):
   ```sh
   cat > 01-no-inputs-4m.evsmv <<'E'
   # Everscript movie 3
   # rom.sha256: 17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db
   # start: power-on, empty SRAM
   F|................*14424
   # frames: 14424
   E
   ```
6. **Compare**, from `everscript-vscode`, about 4 minutes:
   ```sh
   npm run compare -- mesen recomp --replay 01-no-inputs-4m.evsmv --regions wram,vram,cgram,oam --verbose
   ```
   Expected: `in sync through counter $0100 = 14424, 14305 checkpoints`.
7. **Turn one change off** with its switch and run the same command. It stops at the frame its PR file names, for example:
   ```sh
   SNESRECOMP_APU_MASTER_CLOCK=0 npm run compare -- mesen recomp --replay 01-no-inputs-4m.evsmv --regions wram,vram,cgram,oam --verbose
   ```
   Expected: `wram differ at counter $0100 = 319`.

**Switches named `ABL_*`** do not exist upstream. They come from [ablation-gates.patch](ablation-gates.patch), one line per change
that has no switch of its own; never part of a PR:
```sh
git -C snesrecomp apply ../docs/pr/ablation-gates.patch   # then rebuild
```

**All of it at once:** `tools/compare/probes/ablate.mts` in everscript-vscode runs MesenCE once, keeps its hashes in
`out/compare/ablate/`, then runs each variant alone against them, several at a time. About a minute per variant.
```sh
echo '[{"name":"no DMA alignment","env":{"SNESRECOMP_DMA_ALIGN":"0"}}]' > variants.json
node tools/compare/probes/ablate.mts tools/compare/replays/01-no-inputs-4m.evsmv variants.json --jobs 6
```
With `"interpreted": true` a variant runs all-interpreted.

**What the comparison leaves out**, so nobody finds it by surprise:
- Frames before 120 are not compared on 01, 02 and 04: the RNG bytes `$0133-$013A` are copied from MesenCE at frame 115.
- These WRAM bytes are ignored: `$02-$0F`, `$10E-$116`, `$1FEE`, `$1FF4` (`CROSS_NOISE` in `tools/compare/game.mts`).
- 05 and 06 start from a MesenCE state: nothing is copied, every frame counts.
- The recomp runs on our own frame driver, `src/soe_frame_driver.c`, not snesrecomp's `beam_frame_driver.c`. See below.

## Not proposed

Tested off, alone, in both modes, on all five replays: nothing differed in any region, sound and picture included. They come out
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
| Diagnostics and tuning knobs | `SNESRECOMP_PORT_LOG`, `_DMA_LOG`, `_DSP_LOG`, `_SPC_EXEC_LOG`, `_SYNC_WATCH`, `_HV_LOG`, `_PPU_READ_LOG`, `_APU_KR`, `_APU_KW`, `_APU_LEAD`, `_SPC_READ_BIAS`, `_SPC_WRITE_BIAS`, `_DMA_LEAD`, `_DMA_END_SPEED` | #241 | Change nothing by default. `tools/compare/probes/port-handshakes` and `ppu-reads` in everscript-vscode use some of them |

One exception stays, for a reason replays cannot show: **an access's own lead for port timing** (`ABL_ACCESS_LEAD`). Without it no
replay changes, but ports fall back to a constant 20 clocks, which is right only for an absolute-addressed FastROM access: a
constant fitted to this game.

## Our own code that could follow

`src/soe_frame_driver.c` holds game-neutral hardware rules that snesrecomp's `beam_frame_driver.c` lacks: late NMI, the interrupt
sequence's cost, NMI recognition, line 240's short field, refresh position, the auto-joypad window, CPU start at clock 186
([../upstream-access-timing.md](../upstream-access-timing.md) part F). They are not changes to snesrecomp, so they have no file here.
They matter anyway: until they move, nobody but this port gets the full result, and #241's hooks have no caller inside snesrecomp.
