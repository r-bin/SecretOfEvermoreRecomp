# Roadmap

Open work, in working order: each section needs the one before it, except §6 to §8, which can run at any time. Evidence
for each item: [progress.md](progress.md), [progress-details.md](progress-details.md), [bugs-and-todos.md](bugs-and-todos.md),
[research/great-recomp-gap-analysis.md](research/great-recomp-gap-analysis.md).

Key: 🔴 blocks the next stage · 🟡 open gap · 🟢 later or optional · 👤 needs the owner's decision

## Where it stands (2026-11-07)

| Area | State |
|---|---|
| Accuracy | The committed replays 01 and 02 match MesenCE 2.2.1 in WRAM, VRAM, CGRAM and OAM at every NMI from power-on, native and interpreted, nothing copied or masked |
| Picture and sound | See [progress.md](progress.md): the picture differs during fades; the S-DSP state by a power-on default and a voice's end flag a clock early (on 02 for a stretch, also in ARAM) |
| Tests | `ctest`: two tool tests, reset, save state, and both replays against stored MesenCE runs ([testing.md](testing.md)) |
| Framework | Upstream snesrecomp plus `framework/snesrecomp.patch`; four PRs prepared, one open ([pr/README.md](pr/README.md)) |
| What the proof leaves out | The audio sample stream, and all but about 9 minutes of play |
| Release and player features | No release; save states only |

## 1. Publish

| | Task | Done when |
|---|---|---|
| 🔴👤 | **Settle `LICENSE`.** It is the scaffold's PolyForm Noncommercial with the scaffold author's copyright | The owner has picked the license and copyright line |
| 🔴 | **Check a clean clone** on a machine with no local state: clone, `tools/regen.sh`, build, `ctest` | It passes |
| 🟡 | **Send the framework changes upstream** as separate PRs, smallest first ([pr/README.md](pr/README.md)) | `framework_pins.txt` names upstream commits and the replays still sync |

## 2. Prove what the player sees and hears

`node tools/compare/full.mts` compares every RAM, the picture and the S-DSP's state at every NMI.

| | Task | Done when |
|---|---|---|
| 🟡 | **Fades round differently:** up to 8 of 255 below full brightness ([imperfections/fade-rounding.md](imperfections/fade-rounding.md)) | Fixed upstream, or by the glue where brightness holds for a frame |
| 🟡 | **A brightness write during a frame** reached rows already past (a local replay from a state, key 2992, rows 0-24) | Explained and fixed |
| 🟡 | **ENDX seen a clock early** ([imperfections/dsp-endx-read.md](imperfections/dsp-endx-read.md)): proven at two keys | Every key explained; the clock rule found |
| 🟡 | **Compare the sample stream.** `sound` is state once per NMI, because MesenCE's Lua has no audio access | Samples compared over a replay |
| 🟢 | **BRR offset at power-on:** harmless; one line upstream ([imperfections/dsp-brr-offset-reset.md](imperfections/dsp-brr-offset-reset.md)) | Sent with §9 |
| 🟡 | **Close the DMA-only fixes.** VRAM and CGRAM writes during rendering are handled for DMA alone, and `$213B` bit 7 for DMA reads | CPU-side cases fixed or shown not to occur |
| 🟡 | **Try the new renderer on the picture comparison** once Mode 4 exists upstream (§9) | `SoeAfterConfig`'s override removed, or the reason written down |

## 3. Prove the whole game

| | Task | Done when |
|---|---|---|
| 🔴 | **Milestone checks:** `(address, expected value)` lists for rooms, boss flags and items | Checks are named facts, not magic numbers |
| 🔴 | **Play a full-game recording on the native build, checked by events only** (it does not sync, so never a frame comparison) | Every region, boss and the ending reached in order, recorded in `progress.md` |
| 🟡 | **A person finishes the game** on the native build | Written down with the build and the date |
| 🟡 | **Record more MesenCE movies from power-on**, one per act. Hand-played recordings found bugs the attract demo never reached | One synced replay per act, committed |
| 🟡 | **Re-run the older checks:** `tools/verify-intro.js`, the 21 fights in room `0x38` | Pass, or the tool removed as superseded |
| 🟡 | **Audit the 208 pushed-address returns** in `recomp/bank*.cfg`; `$8F:9817` is suspected | A native-vs-interpreted run reaches each, or each is explained |

## 4. Tests and CI

| | Task | Done when |
|---|---|---|
| 🔴 | **ROM-free CI on every push:** the setup-host build and `compare_units` / `import_units` | Green on GitHub, on Windows, Linux and macOS |
| 🟡 | **Native against interpreted as a standing check** (`tools/compare/probes/native-vs-interp.mts`) after each regeneration | One command with an exit code |
| 🟡 | **Arm snesrecomp's runtime tripwires in headless runs** (M/X claim, DB, stack drift, phantom PC) | A trip fails the run |
| 🟡 | **Event-based regression test** for gameplay outcomes (§3's checks) | In `ctest`, skipped without a ROM |
| 🟢 | **"All mods off equals vanilla"** once mods exist | Replays sync with every mod compiled in and off |

## 5. First release

| | Task | Done when |
|---|---|---|
| 🔴 | **Build and run on Windows and Linux.** `release.yml` targets them; every measurement so far is from a Mac | The replay tests pass on each |
| 🟡 | **Player README:** controls, how to report problems | A stranger can follow it |
| 🟡 | **Tag a setup pack** with `scripts/package_release.sh` and `release.yml` | A public release with no ROM data or generated C |
| 🟡 | **Session replay recording:** in memory, hashes per frame on disk, capped | A player's report replays in `compare.mts` |
| 🟢 | **Other ROM releases** need their own addresses; keep refusing them with a clear message until then | — |

## 6. Native share (goal 100 %)

| | Task | Done when |
|---|---|---|
| 🟡 | **First instruction of bound routines:** calls reaching a bound entry at a deadline. The largest interpreted item | Compiled code exists at those entries |
| 🟡 | **Single steps around the NMI:** about 3 % run interpreted, cause open | Traced inside snesrecomp and explained |
| 🟡 | **74 interpreter-only variants.** The `$8E:9EBE`-`$8E:AD3D` dispatchers need `noreturn_jsr` for `jsr (abs,x)`; `$91:B7FA` and `$91:BB72` dispatch through 16-byte records, which `idx:X` cannot describe | Compiled, or left interpreted with the reason written down |
| 🟢 | **Code the CDL never recorded** (about 10 % of the ROM, assets included) | Seeds from later replays; speed only, not correctness |

## 7. Readable C

| | Task | Done when |
|---|---|---|
| 🟡 | **Bind more routines** with the proof already used: replay 01 stays in sync, the C path runs often, `SOE_HLE_MUTATE=1` is caught | Each new binding has a row in [progress-details.md](progress-details.md), "Bound C" |
| 🟡 | **Next targets:** the LZ decompressors, then the room tilemap loader and collision unpacker | Bound and proven |
| 🟡 | **A build report** listing which routines run as C, compiled or interpreted | Written by each build |

What limits the C share, measured on the old replay 01 (2026-11-04): 664 routines run; 9 hold 50 % of its instructions, 76 hold
90 %, 233 hold 99 %. A binding's C runs only when it fits before the next deadline; a routine that can stop between steps,
as the raycast does, is not held to that for its whole length. A binding needs no hand-written cycle costs: `SOE_AT` marks
in the C are enough (`src/soe_cost.c`). Details: [progress-details.md](progress-details.md), "Bound C".

## 8. Speed

| | Task | Done when |
|---|---|---|
| 🟡 | **Measure on weaker machines.** About half the time is the PPU's per-pixel renderer; only Apple Silicon is measured | Numbers for one low-end PC and a handheld-class device |
| 🟢 | **Optional faster mode** if a target falls short, with the accurate build kept as the default | Only after a measured need |

## 9. Upstream reports for snesrecomp

Written up in [bugs-and-todos.md](bugs-and-todos.md) §3. Each goes out as its own report or PR.

| | Report |
|---|---|
| 🟡 | Access-exact timing, PPU reads during rendering, bus-exact compiled code, compiled resume ([pr/README.md](pr/README.md)) |
| 🟡 | The frame driver's interrupt rules, now in our `src/soe_frame_driver.c` ([pr/README.md](pr/README.md), "Our own code that could follow") |
| 🟡 | BG Mode 4 in `NewRenderer` |
| 🟡 | File save states omit `g_cpu`, the beam driver's resume point and the clocks |
| 🟡 | `RtlReset` does not reset `g_cpu` or call `session_reset` |
| 🟡 | D flag not cleared on an interrupt (`cpu_push_interrupt_frame_at`) |
| 🟡 | An empty `$420B` write is charged as a DMA |
| 🟡 | Stack-frame context lost in `interp_tier_dispatch_tail` |
| 🟡 | Function names choose resume entries: break the tie by entry PC |
| 🟡 | Every HiROM mirror is compiled (233 of 317 files never ran) |
| 🟡 | `getenv` on every interpreter entry (about 1 % of the main thread) |
| 🟡 | Save root cut at 96 bytes without an error |

## 10. Player features

All are mods that leave vanilla intact, and none starts before the mod hook layer exists. Designs:
[research/snesmod-addons.md](research/snesmod-addons.md), [research/enhancements.md](research/enhancements.md).

| | Task | Note |
|---|---|---|
| 🟡 | Mod hook layer | Change a decision where it is used, not a value other routines compare later |
| 🟡 | Rewind and a save-state browser | Save states already work |
| 🟡 | Widescreen | Needs room prefill and HUD pinning; sprites past the original edge are culled by the game |
| 🟡 | Fix for the 5-status-effects bug | Cause known: [everscript-vscode docs/5-status-effects-bug.md](https://github.com/r-bin/everscript-vscode/blob/develop/docs/5-status-effects-bug.md) |
| 🟢 | Quality of life: hotkeys, hitbox overlay, translations | |
| 🟢 | MSU-1 music | Sound port commands are known |
| 🟢 | Co-op, then netplay | Last: they change game state, so §3 comes first |
