---
name: recomp-debugging
description: Use when the recompiled game misbehaves or must be compared with the original: headless repro, reference emulators, deciding whether C, seeds, glue or snesrecomp is at fault.
applyTo: "**"
---

# Skill: reproduce and fix recomp bugs

Overview of the build itself: `recomp` skill. Worked examples of every step below:
`docs/bugs-and-todos.md` §2 (each solved bug ends with its lesson). Driving the game:
`docs/headless-exploration.md`.

## 1. The loop

1. **Reproduce** in a headless script from a save state → deterministic.
2. **Signal**: one cheap check that says "broken" (a VRAM tile hash, a WRAM invariant, a room id).
3. **Reference**: does the same script misbehave on MesenCE? No → it is the recomp. snes9x may point
   the way but never decides (`docs/reference-emulators.md`).
4. **Narrow** to the first bad frame without changing timing.
5. **Classify** (§4): our compiled C, our seeds/directives, our glue, or the snesrecomp runtime.
6. **Fix** (§5), rebuild, rerun the script, then a longer run (all fights of a room).
7. **Record**: bug + lesson in `bugs-and-todos.md`, addresses in knowledge entries, the script
   as a regression scene.

## 2. Reproduce

| Source | How |
|---|---|
| A player's state | Ask for `saves/saveN.sav` (in-game: **Shift+F1..F10 save, F1..F10 load**; F1 = slot 0). Only states from 2026-10-12 on carry the CPU and load. |
| The player's in-game save | `saves/save.srm`; continue from the title (inputs in headless-exploration.md §1). |
| Any room / enemy | optional, everscript-vscode: `node tools/recomp/room-nav.js <rom> <room> --script` → teleport + walk lines. |
| A state from a script | `Autosave = 1` in `config.ini`, end the script with `quit` → `saves/save0.sav`. |

Run on a **copy** of `build/` in a scratch folder (the user's saves and config live in the
original): `SNESRECOMP_DUMP_DIR=<dir> ./SecretOfEvermoreSNESRecomp --no-launcher --script s.txt <rom>`.
`dump <tag>` writes wram/vram/cgram/oam/regs/ppuw/dma/apu + `fb.bmp`; `sips -s format png` to view.

## 3. Reference emulators

Trust: MesenCE is the reference for WRAM; ares for precision, by hand; the snes9x rows are probes,
never the reference (`docs/reference-emulators.md`).

| Emulator | Where | Use it for | How |
|---|---|---|---|
| **snesrecomp, native** | `build/SecretOfEvermoreSNESRecomp` | the subject | script + dumps |
| **snesrecomp, compiled bodies off** | same binary, `SNESRECOMP_LLE_BOUNCE=0` | "is our generated C wrong?" — must be bit-exact with the run above | env var |
| **snesrecomp, chosen entries interpreted** | `SNESRECOMP_LLE_INTERP_TARGET_FILE=<file>` (one PC24 per line, plain hex without `$`, all of the `0x/8x/Cx` mirrors) | bisecting compiled entries; a list of roots alone is **not** interpreter-only | env var |
| **snesrecomp, zero seeds** | regen + build with `recomp/bank8x/9x.cfg` moved aside (debugging.md §3.2) | framework vs seeds | rebuild |
| **snes9x 2005, headless** (optional) | everscript-vscode's `tools/recomp/s9x-run.js` | same script on a reference; ~10,000 fps | `node tools/recomp/s9x-run.js <rom> <save.srm> s.txt <dir>` there; dumps in the recomp layout |
| **snes9x 2005, breakpoints** (optional) | everscript-vscode's debugger core in node: `Module.addWriteBreakpoint(addr)`, `onBreakpointHit` | who writes an address on the reference | `src/core/emulator/core/snes9x2005-wasm/DEBUGGER.md` |
| **Extension emulator panel** (optional) | VS Code, everscript-vscode's emulator | play, CDL recording, room overlays | interactive |
| **MesenCE 2.2.1** (the reference) | `/Applications/Mesen.app` or `$MESEN` | lockstep comparison (`tools/compare`), trace logger, PPU/sprite viewers | `compare.mts mesen recomp` (docs/testing.md); interactive with everscript-vscode's `labels.mlb` + `rom.cdl` |
| **ares** (precision, by hand) | ares v148 | a disputed frame | GUI debugger only |
| **Snes9x (desktop)** | any build | quick visual check of the original | manual |
| snesref | `snesrecomp/tools/snesref/` (not built; needs SDL2 + a libretro SNES core) | per-frame WRAM diff trace in the debug-server format | build per its README |
| COSIM build | `snesrecomp/SNES_COSIM.md`, `cosim/` (not tried) | full-state first-divergence; also enables `SNESRECOMP_WRITE_WATCH` | separate build |

**Comparing across emulators:** MesenCE and the recomp run clock for clock, so `tools/compare` compares them at every NMI
from power-on and any difference is real. Every other emulator drifts: against those, compare **invariants** (lists
consistent, HP, room id) or state at **game events** (room entered, fight N over), never at equal frame numbers.

## 4. Classify

| Question | Test | Seen |
|---|---|---|
| Real art, not a glitch? | the ROM's own frames: `soe://rom/assets/characters/<id>/animations/<anim>/frames/` | Boy's hit-pose hair streaks |
| Frozen though it draws? | `$7E0B19` (field frames) must advance with `$7E0100` | the save-state fix |
| Recomp only? | same input on MesenCE (`compare.mts`) | portrait corruption |
| Our compiled C? | `SNESRECOMP_LLE_BOUNCE=0` removes it → yes; bisect the compiled entries of `src/gen/dispatch_v2.c` (rows without all-`NULL` variants) through the target file | `$80:9033`  |
| A seed directive? | `node tools/bisect.js` (debugging.md §6) | `$8F:9F46` dispatch (bugs-and-todos §2) |
| Renderer? | press `r` in game / `g_config.new_renderer` | mode-4 name screen |
| Host / save path? | load at frame 0 and mid-game, check the game advances | `g_cpu` not in snapshots |
| Runtime timing? | `compare.mts mesen recomp`, the bridge switches (`SNESRECOMP_NO_POLLGATE`, `_NO_VBLANK_FF`, `_NO_D9FF`, `_INTERP_STEP_CAP`) | drift from counter ~315 |

Narrowing: coarse → per action → per frame, replacing `wait N` by `wait`/`dump` pairs that add
up to N (`dump` takes no frame). Then read the bad frame: `ppuw.tsv` (vblank VRAM addresses),
`oam.bin` (which sprite uses which tile), WRAM queues and the previous frame's diff.
`SNESRECOMP_INTERP_PCTRACE=1` traces interpreted instructions but changes the run.

## 5. Fix

| Cause | Fix | Where |
|---|---|---|
| A compiled routine misbehaves | `func <pc24> <reason>` → the export emits `force_lle` (interpreted for every caller); rerun everscript-vscode's `npm run recomp` | `recomp/seed-exclusions.txt` |
| An indirect-jump directive is wrong | `dispatch <pc24> <reason>` | same file |
| Host behaviour (frame, save state, renderer) | our glue; the `after_config` hook; our frame driver | `src/game_rtl.c`, `src/main.c`, `src/soe_frame_driver.c` |
| snesrecomp itself | work around in glue, write it up under "Upstream" in bugs-and-todos.md §3 | never edit `snesrecomp/` by hand |

Verify an exclusion took: the address has only `NULL` variants in `dispatch_v2.c`.

## 6. Where to find what

| What | Where |
|---|---|
| Built game, saves, config | `build/` (`saves/`, `config.ini`, `keybinds.ini`) |
| Seeds, generated C | `recomp/bank*.cfg` (committed); `src/gen/` (never commit; `dispatch_v2.c` = compiled entries); runner sources `snesrecomp/runner/src/` |
| Seed export report, import | everscript-vscode's `out/recomp/REPORT.md`; `tools/import.mts --from <checkout>` here (`imported/annotations.tsv`) |
| Disassembly with our names and comments | everscript-vscode's `out/recomp/asar/banks/bank_XX.asm` (bus `$8x`/`$Cx` → file `bank_Cx`), `functions.json` |
| MesenCE files | everscript-vscode's `out/recomp/asar/mesen/` |
| Our glue / hooks | `src/game_rtl.c`, `src/main.c` (`after_config`), `src/soe_frame_driver.c` |
| Seed generation, exclusions | everscript-vscode's `src/platform/cdl/recomp-seeds.ts`; `recomp/seed-exclusions.txt` here |
| Helpers | `tools/`: `compare/` (and its `probes/`), `check-obj-tiles.js`, `bisect.js`; optional in everscript-vscode: `tools/recomp/room-nav.js`, `s9x-run.js` |
| Clean ROM | yours, outside the repo (`SNESRECOMP_ROM`; SHA-256 in `rom_identity.txt`) |
| Game facts | everscript-vscode's `src/games/evermore/localizations/knowledge/{ram,rom}/`, MCP `soe://` (`rom/assets/maps/<room>/scripts/`, `characters/`) |
| Runner switches | `grep -rhoE 'getenv\("SNESRECOMP_[A-Z0-9_]+' snesrecomp/runner/src` |
| Script commands | `snesrecomp/runner/src/desktop/host_main.c` (`LoadScript`); docs: debugging.md §5.1 |

## 7. Traps

1. A file of func roots in the target file is not an interpreter-only run (cost one wrong diagnosis).
2. Excluding a root does not stop snesrecomp compiling the routine as a call target: `force_lle`.
3. A picture is not a running game: check `$7E0B19`.
4. Changing a `wait` while narrowing changes the run; keep the frame total.
5. Writing coordinates during a fade-in is ignored; a teleport into the canopy hides the Boy.
6. States from before 2026-10-12 cannot be restored; F1 *loads*.
7. The interpreter reaches registers and APU ports at an instruction's *start* clock (it adds the clocks after
   running it); MesenCE samples 2 clocks into the access cycle (+20 for an absolute `LDA`/`BIT`/`CMP`).
8. MesenCE SPC callbacks need `emu.cpuType.spc, emu.memType.spcMemory`, and their `masterClock` is frozen (the SPC
   runs in catch-up batches): time them with `spc.cycle / spc.clockRatio`.
9. A timing hunt against MesenCE: method, probes and traps in `docs/research/mesence-parity-lessons.md` (MesenCE's `.srm`
   per ROM name, callbacks per bank mirror, the script engine's own frames, env switches the glue overrides).
10. Exclusions and `force_lle` lines are dated workarounds: after a framework or driver change where the tiers
    meet, rebuild without each and A/B native vs interpreted (`native-vs-interp.mts`); 8 of 9 were obsolete in 2026-11.
11. Moving long-interpreted code to compiled code can surface the compiled tier's leftovers from older timing models
    (the `$4212` polling tick moved the beam 64 clocks per read): bisect with `clock-trace.mts`, then a beam print per
    instruction from `g_interp_bridge_pc_hook`.
