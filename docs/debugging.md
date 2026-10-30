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
 2. Reference Run       ──►  Execute the same frames in snes9x / Mesen2
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

### 2.1 snes9x2005-wasm (Embedded Node.js Reference)
- **Path:** `src/core/emulator/core/snes9x2005-wasm-vanilla/snes9x_2005.js`
- **Execution:** Runs headless inside Node.js scripts (`tools/compare/`).
- **Capabilities:**
  - Instantly accessible from JavaScript via WebAssembly exports (`_startWithRom`, `_mainLoop`, `_saveState`).
  - Direct WRAM access: 128 KB of WRAM is located at state offset `0x10C14`.
  - Used for automated, frame-by-frame memory lockstep testing.
- **Limitation:** not cycle-accurate; drifts in loops with NMI off. **Not trusted:** a probe for
  finding where to look, never the reference.

### 2.2 MesenCE (the Reference: WRAM Comparison & Interactive Debugger)
- **Integration:** The Asar export automatically writes Mesen2 debug files to everscript-vscode's `out/recomp/asar/mesen/`:
  - `rom.cdl`: Byte-level Code/Data Logger bitfields.
  - `labels.mlb`: Complete symbol map containing all our curated function and WRAM names.
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

### 4.1 Lockstep WRAM Comparison (everscript-vscode's `npm run compare`)
Lockstep runs the clean ROM in a reference and in the recompiled binary side by side. MesenCE is the
reference; snes9x only finds the first differing frame and its writers:

```sh
# in everscript-vscode; runs build/ here
node tools/compare/compare.mts rom.smc --backend mesen,recomp --replay tools/compare/replays/01-no-inputs-4m.evsmv
npm run compare -- recomp=/path/to/rom.smc                # against snes9x: a lead
```

Known differences from MesenCE and how they were measured: [imperfections/README.md](imperfections/README.md).

`tools/compare/README.md` has every option; the report goes to `out/compare/`.

#### Rule 1: Align on the NMI Count, Not on Host Frames
Different emulators spend different numbers of initial cycles clearing memory and setting up
the PPU. Align on the NMIs counted since power-on: the increases of `$7E0100` from the 0 the reset code
writes. `$7E0100` itself is no key: the attract demo zeroes it every time it loops (~6,000 NMIs), so the glue
(`SOE_COMPARE_*`) and the MesenCE script count a drop as a restart from 0, and the glue ends a run itself
(`SOE_COMPARE_QUIT_AT`). Against MesenCE both sides sample as each NMI is taken instead
(`SOE_COMPARE_AT=nmi`, the key is the NMI's number), an instruction boundary on both
([imperfections/sampling-point.md](imperfections/sampling-point.md)); `SOE_COMPARE_NMI_LOG` adds each NMI's
master clock and interrupted address, the quickest way to see where the two clocks part.

#### Rule 2: Synchronize the PRNG State ($7E0133..$7E0139)
The reset routine `$80:8020` seeds Evermore's pseudorandom number generator from the PPU's
horizontal and vertical beam counters. Because different emulators boot with different beam positions,
their RNG seeds immediately diverge.
- `tools/compare` waits for key 115 in the reference (MesenCE or snes9x), reads its 8-byte RNG state
  (`$7E0133..$7E013A`), and `poke`s it into the recompiled game after the same frame.

#### Rule 3: Mask Non-Deterministic Timing Noise
Exclude addresses that naturally fluctuate due to sub-frame interrupt timing:
- Direct Page temporary scratch: `$7E0002..$7E000F`
- PPU scroll register shadows mid-update: `$7E010E..$7E0115`
- Hardware interrupt stack return slots: `$7E1FEE`, `$7E1FF4`

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

---

## 7. Interactive `.evs` Script Debugging in the Recomp

While `snes9x2005-wasm` remains the primary fast-loop environment for F5 debugging inside VS Code,
the recompiled native desktop executable (`snesrecomp`) can also support `.evs` script debugging.

### 7.1 Script VM Opcode Interception
In Secret of Evermore, `.evs` code does not compile into native 65816 assembly; it is bytecode executed by
the in-game interpreter at `$8C:D0A6` (`lda [$82]`):
* `$82..$84`: 24-bit pointer to the current script instruction about to run.
* `$7E`: Running slot pointer (one of 20 slots at `$7E28FC`, each 0x4F bytes).

In the recompilation host (`src/game_rtl.c`), a native C hook at `$8C:D0A6` can intercept
every bytecode fetch:
> **Reconstructed C Decompilation**:
> Source moved to [`reconstructed-c/80/808020_debugging.c`](https://github.com/r-bin/everscript-vscode/blob/develop/reconstructed-c/80/808020_debugging.c).
> [View in VFS: soe://rom/code/808020](soe://rom/code/808020?c=1&asm=0)


### 7.2 Native Trusted Plugin vs. `.snesmod`
Because `.snesmod` archives follow a strict data-only security model (no arbitrary shared libraries or executable code),
a `.snesmod` alone cannot open network sockets or control process execution threads:
1. **The Engine (Native Trusted Plugin):** Written in C/C++ in `src/`, statically linked into the executable. Runs a local TCP server listening for Debug Adapter Protocol (DAP) messages and synchronizes breakpoints across threads.
2. **The Front-End (`soe-evs-debugger.snesmod`):** Exposes options in the launcher UI to toggle the debug server, select the TCP listening port, and choose whether to halt execution on game boot.

### 7.3 Workflow Comparison: Recomp vs. snes9x Webview

| Characteristic | `snes9x2005-wasm` Webview | `snesrecomp` Desktop Host |
|---|---|---|
| **Best For** | 5-second edit-compile-step iteration loop | Playtesting in 16:9 60fps with active `.snesmod` addons |
| **Launch Setup** | Zero setup; runs entirely inside VS Code process | Requires building native desktop binary |
| **Hot Reload** | Instant in-memory ROM re-patching | In-memory patch reloading or restarted executable |
| **Transport** | Webview `postMessage` | Local TCP socket (e.g. `localhost:54321`) |

