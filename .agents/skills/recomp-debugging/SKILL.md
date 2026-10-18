---
name: recomp-debugging
description: Use when the recompiled game misbehaves or must be compared with the original: headless repro, reference emulators, deciding whether C, seeds, glue or snesrecomp is at fault.
applyTo: "tools/recomp/**,docs/**,src/platform/cdl/recomp-*.js"
---

# Skill: reproduce and fix recomp bugs

Overview of the build itself: `recomp` skill. Worked examples of every step below:
`docs/bugs-and-todos.md` §2 (each solved bug ends with its lesson). Driving the game:
`docs/headless-exploration.md`.

## 1. The loop

1. **Reproduce** in a headless script from a save state → deterministic.
2. **Signal**: one cheap check that says "broken" (a VRAM tile hash, a WRAM invariant, a room id).
3. **Reference**: does the same script misbehave on snes9x? No → it is the recomp.
4. **Narrow** to the first bad frame without changing timing.
5. **Classify** (§4): our compiled C, our seeds/directives, our glue, or the snesrecomp runtime.
6. **Fix** (§5), rebuild, rerun the script, then a longer run (all fights of a room).
7. **Record**: bug + lesson in `bugs-and-todos.md`, addresses in knowledge entries, the script
   as a regression scene.

## 2. Reproduce

| Source | How |
|---|---|
| A player's state | Ask for `saves/saveN.sav` (in-game: **Shift+F1..F10 save, F1..F10 load**; F1 = slot 0). Only states from v0.201.18 on carry the CPU and load. |
| The player's in-game save | `saves/save.srm`; continue from the title (inputs in headless-exploration.md §1). |
| Any room / enemy | `node tools/recomp/room-nav.js out/rom.sfc <room> --script` → teleport + walk lines. |
| A state from a script | `Autosave = 1` in `config.ini`, end the script with `quit` → `saves/save0.sav`. |

Run on a **copy** of `out/recomp/SecretOfEvermore-<os>-<arch>/` in the scratchpad (the user's
saves and config live in the original):
`SNESRECOMP_DUMP_DIR=<dir> ./SecretOfEvermore --no-launcher --script s.txt out/rom.sfc`.
`dump <tag>` writes wram/vram/cgram/oam/regs/ppuw/dma/apu + `fb.bmp`; `sips -s format png` to view.

## 3. Reference emulators

| Emulator | Where | Use it for | How |
|---|---|---|---|
| **snesrecomp, native** | `out/recomp/SecretOfEvermore-*/` | the subject | script + dumps |
| **snesrecomp, compiled bodies off** | same binary, `SNESRECOMP_LLE_BOUNCE=0` | "is our generated C wrong?" — must be bit-exact with the run above | env var |
| **snesrecomp, chosen entries interpreted** | `SNESRECOMP_LLE_INTERP_TARGET_FILE=<file>` (one PC24 per line, `$0x/$8x/$Cx` mirrors) | bisecting compiled entries; a list of roots alone is **not** interpreter-only | env var |
| **snesrecomp, zero seeds** | rebuild without `bank8x/9x.cfg` (debugging.md §3.2) | framework vs seeds | rebuild |
| **snes9x 2005, headless** | `tools/recomp/s9x-run.js` (debugger core `src/core/emulator/core/snes9x2005-wasm/`) | same script on a reference; ~10,000 fps | `node tools/recomp/s9x-run.js out/rom.sfc <save.srm> s.txt <dir>`; dumps in the recomp layout |
| **snes9x 2005, lockstep** | `npm run recomp:lockstep` / `scripts/lockstep.sh` | WRAM diff per NMI count from power-on | valid to counter ~305; timing drift after (§1.1 of bugs-and-todos) |
| **snes9x 2005, breakpoints** | debugger core in node: `Module.addWriteBreakpoint(addr)`, `onBreakpointHit` | who writes an address on the reference | `src/core/emulator/core/snes9x2005-wasm/DEBUGGER.md` |
| **Extension emulator panel** | VS Code, Everscript emulator | play, CDL recording, room overlays | interactive |
| **Mesen2** | `/Applications/Mesen.app` | cycle-accurate arbiter, trace logger, PPU/sprite viewers | import `out/recomp/asar/mesen/labels.mlb` + `rom.cdl`; manual |
| **Snes9x (desktop)** | `/Applications/Snes9x.app` | quick visual check of the original | manual |
| snesref | `snesrecomp/tools/snesref/` (not built; needs SDL2 + a libretro SNES core) | per-frame WRAM diff trace in the debug-server format | build per its README |
| COSIM build | `snesrecomp/SNES_COSIM.md`, `cosim/` (not tried) | full-state first-divergence; also enables `SNESRECOMP_WRITE_WATCH` | separate build |

**Comparing across emulators:** their timing differs, so never compare at equal frame numbers
after the first few hundred frames. Compare **invariants** (lists consistent, HP, room id) or
state at **game events** (room entered, fight N over). Same inputs, different timing is normal.

## 4. Classify

| Question | Test | Seen |
|---|---|---|
| Real art, not a glitch? | the ROM's own frames: `soe://rom/assets/characters/<id>/animations/<anim>/frames/` | Boy's hit-pose hair streaks |
| Frozen though it draws? | `$7E0B19` (field frames) must advance with `$7E0100` | v0.201.18 save states |
| Recomp only? | same script on `s9x-run.js` | portrait corruption |
| Our compiled C? | `SNESRECOMP_LLE_BOUNCE=0` removes it → yes; bisect the compiled entries of `src/gen/dispatch_v2.c` (rows without all-`NULL` variants) through the target file | `$80:9033` (v0.201.19) |
| A seed directive? | `node tools/bisect.js` (debugging.md §6) | `$8F:9F46` dispatch (bugs-and-todos §2) |
| Renderer? | press `r` in game / `g_config.new_renderer` | mode-4 name screen |
| Host / save path? | load at frame 0 and mid-game, check the game advances | `g_cpu` not in snapshots |
| Runtime timing? | lockstep, Mesen2, the bridge switches (`SNESRECOMP_NO_POLLGATE`, `_NO_VBLANK_FF`, `_NO_D9FF`, `_INTERP_STEP_CAP`) | drift from counter ~315 |

Narrowing: coarse → per action → per frame, replacing `wait N` by `wait`/`dump` pairs that add
up to N (`dump` takes no frame). Then read the bad frame: `ppuw.tsv` (vblank VRAM addresses),
`oam.bin` (which sprite uses which tile), WRAM queues and the previous frame's diff.
`SNESRECOMP_INTERP_PCTRACE=1` traces interpreted instructions but changes the run.

## 5. Fix

| Cause | Fix | Where |
|---|---|---|
| A compiled routine misbehaves | `FUNC_EXCLUSIONS` entry → emitted as `force_lle` (keeps it interpreted for every caller) | `src/platform/cdl/recomp-analysis.ts` |
| An indirect-jump directive is wrong | `DISPATCH_EXCLUSIONS` | same file |
| Host behaviour (frame, save state, renderer) | our glue `game_rtl.c` hooks; scaffold patches in `host.js` (`hookMain`, `hookBeamDriver`) | `tools/recomp/host/`, `tools/recomp/host.js` |
| snesrecomp itself | work around in glue, write it up under "Upstream" in bugs-and-todos.md §3 | never edit the repository root by hand |

Verify an exclusion took: the address has only `NULL` variants in `dispatch_v2.c`.

## 6. Where to find what

| What | Where |
|---|---|
| Built game, saves, config | `out/recomp/SecretOfEvermore-<os>-<arch>/` (`saves/`, `config.ini`, `keybinds.ini`) |
| Generated host project (never commit) | the repository root; seeds `recomp/bank*.cfg`; C `src/gen/` (`dispatch_v2.c` = compiled entries); runner sources `snesrecomp/runner/src/` |
| Build logs, report | `out/recomp/recomp-host.log`, `out/recomp/REPORT.md` |
| Disassembly with our names and comments | `out/recomp/asar/banks/bank_XX.asm` (bus `$8x`/`$Cx` → file `bank_Cx`), `functions.json` |
| Mesen2 files | `out/recomp/asar/mesen/` |
| Our glue / hooks | `src/game_rtl.c`, `tools/recomp/host.js` |
| Seed generation, exclusions | `src/platform/cdl/recomp-seeds.js`, `recomp-analysis.ts` |
| Helpers | `tools/recomp/`: `room-nav.js`, `check-obj-tiles.js`, `s9x-run.js`, `lockstep.js`, `bisect.js`, `verify-intro.js` |
| Wrappers | `scripts/` (`build-recomp.sh`, `play-recomp.sh`, `lockstep.sh`, `test.sh`) |
| Clean ROM | `out/rom.sfc` (SHA-1 `79e77386…`) |
| Game facts | `src/games/evermore/localizations/knowledge/{ram,rom}/`, MCP `soe://` (`rom/assets/maps/<room>/scripts/`, `characters/`) |
| Runner switches | `grep -rhoE 'getenv\("SNESRECOMP_[A-Z0-9_]+' snesrecomp/runner/src` |
| Script commands | `snesrecomp/runner/src/desktop/host_main.c` (`LoadScript`); docs: debugging.md §5.1 |

## 7. Traps

1. A file of func roots in the target file is not an interpreter-only run (cost one wrong diagnosis).
2. Excluding a root does not stop snesrecomp compiling the routine as a call target: `force_lle`.
3. A picture is not a running game: check `$7E0B19`.
4. Changing a `wait` while narrowing changes the run; keep the frame total.
5. Writing coordinates during a fade-in is ignored; a teleport into the canopy hides the Boy.
6. States from before v0.201.18 cannot be restored; F1 *loads*.
