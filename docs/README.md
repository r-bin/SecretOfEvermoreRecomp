# Secret of Evermore Static Recompilation (snesrecomp)

A static recompiler pipeline and runtime environment that turns Secret of Evermore (SNES)
into a native, standalone, portable C desktop executable using
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

---

## 1. Status

- **Status (2026-10-12, v0.201.20): playable in the field.** Boots, intro, attract demo, title,
  continue from the SRAM save, Prehistoria village and South jungle with combat, verified
  headless against snes9x (no sprite or HUD corruption over 21 fights). Save states (F1) work.
- **Byte-identical rebuild:** Asar produces a ROM bit-for-bit identical to the vanilla cartridge.
- **Native execution:** 685 of 1079 function variants (400 roots) compile to native C; 394 run
  in the interpreter. Routines that miscompile stay on the interpreter via `force_lle`.
- **Coverage:** the CDL used so far is a ~10-second recording (9.3 % of the ROM, ~42 KB of
  code); most of the game's code has never run in a test. Next: a full-play CDL, then test play.
- **Lockstep validation:** WRAM matches snes9x bit-for-bit through NMI counter 305 (timing drift
  after, see bugs-and-todos.md §1.1).

---

## 2. Documentation Map

| Guide | Contents |
|---|---|
| **[setup.md](setup.md)** | **Setup & Build Pipeline**<br>Prerequisites, required assets (ROM, CDL, knowledge), environment variables, and the end-to-end `npm run recomp` pipeline. |
| **[game-rtl.md](game-rtl.md)** | **Game Runtime Layer (Game RTL / Glue Code)**<br>Why "Game RTL" is the proper architectural term, how `game_rtl.c` drives frames and interrupts, host configuration hooks (`SoeAfterConfig`), and licensing isolation. |
| **[debugging.md](debugging.md)** | **Debugging, Emulators & Comparison**<br>Available emulators (snes9x2005-wasm, Mesen2, desktop host), execution modes (native vs interpreter, renderer modes), WRAM lockstep diffing, headless script automation, and seed bisection. |
| **[headless-exploration.md](headless-exploration.md)** | **Headless Exploration**<br>Into the game from SRAM, save states without a UI, reading rooms (`room-nav.js`), teleporting onto step-on triggers, fighting, narrowing a glitch to its frame, snes9x comparison. |
| **[bugs-and-todos.md](bugs-and-todos.md)** | **Open Bugs, Findings & Roadmap**<br>Open issues (timing drift), resolved case studies with their lessons (BG mode 4, stack-resident dispatch, sprite allocator, save states, HUD portrait / force_lle), upstream bug reports, next milestones. |
| **[snesmod-addons.md](snesmod-addons.md)** | **.snesmod Packages, Addons & Config**<br>ZIP package architecture, TOML manifests, native trusted plugins in C/C++, options (bool, choice, int), and ideal Evermore addons (2-player co-op, bugfixes, MSU-1 audio, QoL). |
| **[enhancements.md](enhancements.md)** | **Widescreen, Localization & Overlays**<br>Hybrid 16:9/21:9 implementation, Evermore widescreen invariants (scroll phase, room prefill, HUD pinning), declarative TOML translations, in-game map/hitbox overlays, and native randomizer plugins. |
| **[experimental-renderer.md](experimental-renderer.md)** | **Experimental Graphics & Lighting**<br>Dynamic 2D lighting with 2D shadow casting from collision words, HD Mode 7 flight supersampling, sub-frame motion interpolation, and CIE color-science CRT simulation. |

---

## 3. Quick Start

### Build the Recompiled Game
```sh
# One command: exports Asar disasm, verifies byte-identical rebuild, scaffolds host, generates C, compiles & packages
npm run recomp

# Re-scaffold the host project cleanly and enforce strict checks
npm run recomp -- --fresh --strict

# Stop after Asar rebuild (fast disasm check, ~2 s)
npm run recomp -- --no-recomp
```

### Play the Game
```sh
# macOS
open "out/recomp/SecretOfEvermore-macos-arm64/play.command"

# Linux
./out/recomp/SecretOfEvermore-linux-x64/play.sh

# Windows
out\recomp\SecretOfEvermore-windows-x64\play.bat
```
*(Player data in `saves/`, `config.ini`, and `keybinds.ini` survive rebuilds).*

### Run Lockstep Verification
```sh
npm run recomp:lockstep -- --rom "/path/to/Secret of Evermore (U) [!].smc"
```

---

## 4. Pipeline Architecture

```text
 ┌──────────────────────────────────────────────┐
 │             Input Information                │
 │  • Clean ROM (SHA-1 79e7738...)              │
 │  • CDL Library (<globalStorage>/cdl-library) │
 │  • Curated Knowledge (functions, tables, RAM)│
 │  • ROM Asset Models (rooms, text, gfx)       │
 └──────────────────────┬───────────────────────┘
                        │
                        ▼
 ┌──────────────────────────────────────────────┐
 │     Step 1: Asar Disassembly & Rebuild       │
 │  • Emits banks/*.asm with symbol names       │
 │  • Emits recomp/cfg/bank*.cfg seed directives│
 │  • Verifies byte-identical ROM hash          │
 └──────────────────────┬───────────────────────┘
                        │
                        ▼
 ┌──────────────────────────────────────────────┐
 │  Step 2: Host Scaffolding & Game RTL Overlay │
 │  • snesrecomp CMake project scaffold         │
 │  • tools/recomp/host/ (Game RTL glue) overlay│
 │  • main.c patched with SoeAfterConfig hook   │
 └──────────────────────┬───────────────────────┘
                        │
                        ▼
 ┌──────────────────────────────────────────────┐
 │        Step 3: snesrecomp Code Gen           │
 │  • tools/regen.sh --cfg-roots                │
 │  • Lifts 65816 machine code to native C      │
 │  • Outputs project/src/gen/*.c               │
 └──────────────────────┬───────────────────────┘
                        │
                        ▼
 ┌──────────────────────────────────────────────┐
 │     Step 4: Compilation & Packaging          │
 │  • CMake Release compilation                 │
 │  • Packaged to out/recomp/SecretOfEvermore-* │
 │  • Generates REPORT.md and report.json       │
 └──────────────────────────────────────────────┘
```

