# Debugging the Recomp: Emulators, Modes, and Comparison

How to systematically debug visual glitches, state divergence, and logic errors in the
Secret of Evermore static recompilation (`build/SecretOfEvermoreSNESRecomp`).

---

## 1. The 5-Step Debugging Methodology

When an anomaly occurs in the recompiled game:

```text
 1. Reproduce Headless  ──►  Run via scripted inputs (eliminate human timing jitter)
           │
           ▼
 2. Reference Run       ──►  Play the same input on MesenCE (tools/compare)
           │
           ▼
 3. Native vs Interp    ──►  Rebuild without seeds; run purely in snesrecomp interpreter
           │
           ├─► Bug still present  ──► Framework / PPU / APU / Game RTL bug
           │
           └─► Bug disappears     ──► Our CDL seeds / generated C caused the issue
                     │
                     ▼
 4. Subsystem Dumps     ──►  Diff VRAM, OAM, CGRAM, PPU registers, PPU-write logs
                     │
                     ▼
 5. Seed Bisection      ──►  Bisect bank configs and directive lines to isolate the exact fault
```

---

## 2. Emulators We Have Access To

Which one we trust for what, and the rules for comparing: [reference-emulators.md](reference-emulators.md).
**Only MesenCE is the reference for WRAM comparisons; snes9x is never treated as accurate.**

### 2.1 snes9x 2005 (optional, in everscript-vscode)
- **Where:** everscript-vscode's embedded core (`src/core/emulator/core/snes9x2005-wasm-vanilla/` there), headless in node.
- **Use:** fast (about 10,000 frames/s), write breakpoints: finding where to look and who writes an address.
- **Limitation:** not cycle-accurate; drifts in loops with NMI off. **Not trusted:** a probe, never the reference.

### 2.2 MesenCE (the Reference: WRAM Comparison & Interactive Debugger)
- **Version:** the 2.2.1 release ([testing.md](testing.md), "The reference").
- **Labels (optional):** everscript-vscode's export writes `rom.cdl` and `labels.mlb` (our function and WRAM names) to its
  `out/recomp/asar/mesen/`, for MesenCE's debugger.
- **Capabilities:**
  - Cycle-accurate PPU and APU emulation.
  - Interactive trace logger, event viewer, and PPU tile/sprite viewer.
  - Headless: `Mesen --testRunner <script.lua> <rom>` with Lua memory access ([reference-emulators.md](reference-emulators.md)).
  - The reference the recomp's WRAM must match; ares (by hand) settles the rare dispute.

### 2.3 snesrecomp Desktop Host (The Recompiled Executable)
- **Path:** `build/SecretOfEvermoreSNESRecomp`
- **Interactive Controls:**
  - `Shift+F1`...`F10`: Save state to slot.
  - `F1`...`F10`: Load state from slot.
  - `Tab`: Fast-forward / Turbo.
  - `r`: Toggle between legacy and optimized PPU renderers at runtime.
  - `Ctrl+R`: Soft reset.
- **Headless Script Interface:**
  Pass `--no-launcher --script <script.txt> <rom_path>` to execute automated batch runs without a UI window.

---

## 3. Modes We Can Run snesrecomp In

snesrecomp can be configured to run in multiple diagnostic modes to isolate faults:

### 3.1 Native Recompiled Mode (Full Seeds)
- Generated with `tools/regen.sh --cfg-roots --rom <rom>`.
- Translates all verified CDL roots into native C functions (`src/gen/*.c`).
- Unresolved branches and indirect dispatches fall back to `interp_tier_dispatch_tail`.
- **Target state:** Maximum execution speed (~400+ FPS), full C call graph.

### 3.2 Interpreter-Only / LLE Fallback Mode (Zero Seeds)
- Remove `bank8x/9x.cfg` and run `tools/regen.sh` without `--cfg-roots`.
- Everything executes through snesrecomp's internal 65816 interpreter.
- **Diagnostic Power:**
  - **If a bug persists here:** It is a framework bug (PPU rasterizer, APU synchronization, interrupt timing, or Game RTL glue). It cannot be caused by our CDL seeds or generated C.
  - **If the bug vanishes here:** The framework is innocent; a specific `func` or `indirect_dispatch` directive generated incorrect native C.

### 3.3 Renderer Modes
- **Optimized PPU (`g_config.new_renderer = true`):** Multithreaded scanline renderer supporting BG Modes 0–3. Fast, but crashes/glitches on Mode 4 (character-name screen).
- **Reference PPU (`g_config.new_renderer = false`):** Legacy renderer (`ppu_legacy.c`). Implements all SNES BG modes (0–7) accurately. Forced by our `SoeAfterConfig` hook.

---

## 4. What and How to Compare

### 4.1 Lockstep Comparison Against MesenCE (`tools/compare`)

MesenCE and the recomp play the same replay from power-on; at every NMI both hash WRAM, VRAM, CGRAM and OAM (and with
`--full` every RAM, the picture and the S-DSP state). The first NMI that differs, and its bytes with their WRAM names, are
the report ([testing.md](testing.md) has the commands and what is compared):

```sh
node tools/compare/compare.mts mesen recomp /path/to/rom.sfc \
  --replay tools/compare/replays/01-no-inputs-1-loop.mmo --regions wram,vram,cgram,oam --verbose
```

- **Aligned on the NMI count, not on host frames.** The key is the number of NMIs since the reset code first writes 0 to
  `$7E0100`; the attract demo zeroes `$7E0100` every time it loops, so a drop counts as a restart from 0. Both sides
  sample as each NMI is taken, an instruction boundary on both ([imperfections/sampling-point.md](imperfections/sampling-point.md)).
- **Nothing is copied or masked.** The game seeds its RNG at reset from the PPU's H/V counters, so a run that is a clock
  off parts within the first frames: a difference anywhere is a real difference.
- **Where clocks part:** `SOE_COMPARE_NMI_LOG` adds each NMI's master clock and interrupted address; the probes in
  `tools/compare/README.md` go down to single instructions.

Known differences from MesenCE and how they were measured: [imperfections/README.md](imperfections/README.md). Every
option: `tools/compare/README.md`; reports go to `out/compare/`.

---

## 5. Headless Automation & Dump Analysis

### 5.1 Input Script Commands
snesrecomp script files (`--script <file>`) support:

```text
turbo on                          # Run at maximum possible speed
until16 100 == 13B                # Block until WRAM $7E0100 equals hex 0x13B
poke 133 1A2B3C4D                 # Overwrite WRAM at 0x7E0133
wait 120                          # Advance exactly 120 frames
dump tag_name                     # Write comprehensive hardware dumps
quit                              # Terminate process cleanly
press b 4                         # Hold B for 4 frames (+1 idle frame); combine with +: down+b
pokefor 4ea3 a002 3               # Write WRAM for 3 frames (little-endian bytes: 0x02A0)
forcepoke 4eb3 1e00               # Write WRAM every frame from now on (HP lock)
loadstate 0                       # Load save-state slot 0 (saves/save0.sav)
```

Getting into the game, making save states headless, reading the room data, teleporting onto
step-on triggers, fighting and narrowing a glitch down to its frame:
**[headless-exploration.md](headless-exploration.md)**.

### 5.2 Analyzing Dump Artifacts
When `dump <tag>` is executed with `SNESRECOMP_DUMP_DIR=/path/to/dumps`, six files are emitted:

| Dump File | Contents | What to Look For |
|---|---|---|
| `<tag>.wram.bin` | 131,072 bytes (128 KB WRAM) | Binary diff against reference WRAM to find logic corruption |
| `<tag>.vram.bin` | 65,536 bytes (64 KB VRAM) | Verify whether graphics and tilemaps were uploaded correctly |
| `<tag>.cgram.bin` | 512 bytes (CGRAM palettes) | Check for palette corruption or black screen fades |
| `<tag>.oam.bin` | 544 bytes (OAM sprite table) | Inspect sprite X/Y coordinates, tile IDs, and priority bits |
| `<tag>.regs.json` | PPU register state | Check BG mode (`$2105`), screen display (`$2100`), OBSEL (`$2101`) |
| `<tag>.ppuw.tsv` | Complete register write trace | Full log of every PPU write with exact frame and scanline number |

### 5.3 Same Script on snes9x (everscript-vscode's `tools/recomp/s9x-run.js`)
`node tools/recomp/s9x-run.js <rom> <save.srm | -> <script> <outdir>` runs the same commands on
the extension's snes9x core (about 10,000 frames per second) and writes `<tag>.wram.bin`,
`<tag>.vram.bin` and `<tag>.fb.bmp` in the recomp's layout. Start it from SRAM with the title
sequence above instead of `loadstate`. Hash a VRAM or WRAM range in both dump folders to see
whether a glitch exists on the reference at all.

### 5.4 Forcing Functions into the Interpreter at Run Time
`SNESRECOMP_LLE_INTERP_TARGET_FILE=<file>` (one 24-bit PC per line, plain hex such as `8CCFFD`: the
parser is `strtoul`, so a `$8CCFFD` line is skipped without a warning) makes the bridge run
those entries in the interpreter instead of their generated C, with no rebuild. It only
covers the entries listed, so a file of `func` roots is **not** an interpreter-only run: compiled
variants and rediscovered callees still run natively. For a real differential:

1. `SNESRECOMP_LLE_BOUNCE=0`: interpreted code stops bouncing into compiled bodies. snesrecomp
   promises bounced and interpreted runs are guest-state bit-exact, so if this makes a glitch
   vanish, a compiled body is wrong.
2. List every compiled entry: the `g_dispatch_table` rows of `src/gen/dispatch_v2.c`
   whose variant pointers are not all `NULL` (in the `$0x`, `$8x` and `$Cx` mirrors), and bisect that
   list through the target file against a deterministic script and a WRAM invariant.
3. Keep the culprit off the native tier for good with `force_lle <pc24>`: add a `func <pc24> <reason>`
   line to `recomp/seed-exclusions.txt` and rerun the seed export, which emits it.

---

## 6. Seed Bisection Technique (`tools/bisect.js`)

When a bug reproduces in Native mode but vanishes in Interpreter mode, use automated bisection:

### Phase 1: Bank-Level Bisection
1. Clear all `bank8x/9x.cfg` files.
2. Restore one bank configuration at a time (e.g. only `bank80.cfg`, then only `bank8F.cfg`, then `bank90.cfg`).
3. Run `regen.sh` and build.
4. Run the automated headless test.
5. Identify which bank triggers the failure.

### Phase 2: Directive-Type Bisection
Within the guilty bank:
1. Strip all `indirect_dispatch` lines, testing with `func` lines only.
2. If the bug vanishes, the issue is an incomplete jump table or pointer dispatch.
3. If the bug persists, the issue is an incorrect function boundary or $M/X$ width.

### Phase 3: Binary Search on Seed Lines
1. Split the remaining directives in half.
2. Regenerate, compile, and execute the automated check.
3. Halve the suspect group until the single line causing the regression is isolated.

*(This exact methodology successfully resolved the "Press START" title screen corruption in under 15 minutes).*
