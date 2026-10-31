# Secret of Evermore Static Recompilation (snesrecomp)

A static recompiler pipeline and runtime environment that turns Secret of Evermore (SNES)
into a native, standalone, portable C desktop executable using
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

---

## 1. Status

- **Status (2026-10-30): playable in the field.** Boots, intro, attract demo, title, continue
  from the SRAM save, Prehistoria village and South jungle with combat, verified headless against
  snes9x (no sprite or HUD corruption over 21 fights). Save states (F1) work.
- **Seeds:** a CDL covering 90.3 % of the ROM (a full playthrough), written into `recomp/` by
  everscript-vscode's export. On the pinned framework: 3916 roots, 4871 native variants, 759
  interpreted. Routines that miscompile stay interpreted (`recomp/seed-exclusions.txt`).
- **Lockstep validation:** WRAM matches snes9x through NMI counter ~300 (timing drift after, see
  bugs-and-todos.md §1.1). snes9x is not the reference; MesenCE is ([reference-emulators.md](reference-emulators.md)).

---

## 2. Documentation Map

| Guide | Contents |
|---|---|
| **[setup.md](setup.md)** | **Setup & Build Pipeline**<br>Prerequisites, required assets (ROM, CDL, knowledge), environment variables, the build, and how the seeds come from everscript-vscode. |
| **[game-rtl.md](game-rtl.md)** | **Game Runtime Layer (Game RTL / Glue Code)**<br>Why "Game RTL" is the proper architectural term, how `game_rtl.c` drives frames and interrupts, host configuration hooks (`SoeAfterConfig`), and licensing isolation. |
| **[debugging.md](debugging.md)** | **Debugging, Emulators & Comparison**<br>Available emulators (snes9x2005-wasm, Mesen2, desktop host), execution modes (native vs interpreter, renderer modes), WRAM lockstep diffing, headless script automation, and seed bisection. |
| **[headless-exploration.md](headless-exploration.md)** | **Headless Exploration**<br>Into the game from SRAM, save states without a UI, reading rooms (`room-nav.js`), teleporting onto step-on triggers, fighting, narrowing a glitch to its frame, snes9x comparison. |
| **[bugs-and-todos.md](bugs-and-todos.md)** | **Open Bugs, Findings & Roadmap**<br>Open issues (timing drift), resolved case studies with their lessons (BG mode 4, stack-resident dispatch, sprite allocator, save states, HUD portrait / force_lle), upstream bug reports, next milestones. |
| **[mesence-parity-lessons.md](mesence-parity-lessons.md)** | **What the MesenCE parity work taught us**<br>Results, why the framework changes were needed (A/B per switch), snesrecomp facts, the method and its traps. |
| **[upstream-access-timing.md](upstream-access-timing.md)** | **Upstream Proposal: Access-Exact Timing**<br>The one game-neutral snesrecomp change that would close every open MesenCE difference: each bus access at its own clock, catch-up sync at shared registers, SPC700 port accesses on their cycle, events inside DMA. |
| **[snesmod-addons.md](snesmod-addons.md)** | **.snesmod Packages, Addons & Config**<br>ZIP package architecture, TOML manifests, native trusted plugins in C/C++, options (bool, choice, int), and ideal Evermore addons (2-player co-op, bugfixes, MSU-1 audio, QoL). |
| **[enhancements.md](enhancements.md)** | **Widescreen, Localization & Overlays**<br>Hybrid 16:9/21:9 implementation, Evermore widescreen invariants (scroll phase, room prefill, HUD pinning), declarative TOML translations, in-game map/hitbox overlays, and native randomizer plugins. |
| **[reference-emulators.md](reference-emulators.md)** | **Which Emulator for What**<br>MesenCE is the trusted, scriptable reference for WRAM comparison; ares the precision reference for probing by hand; snes9x a fast probe, never accurate. How to run each. |
| **[cdl-and-seeds.md](cdl-and-seeds.md)** | **The CDL and the Seeds**<br>How good the recording is (90 % of the ROM), why it is enough for the recomp, and why only the seeds are committed. |
| **[lessons-from-mmx-and-lufia2.md](lessons-from-mmx-and-lufia2.md)** | **Lessons from Other Ports**<br>What Mega Man X Recompiled (LLE-first contract, tripwires, validation ladder, replays, coverage capture) and Lufia II Recompiled (proven decomp bindings) do for accuracy, and a prioritised list for our recomp. |
| **[experimental-renderer.md](experimental-renderer.md)** | **Experimental Graphics & Lighting**<br>Dynamic 2D lighting with 2D shadow casting from collision words, HD Mode 7 flight supersampling, sub-frame motion interpolation, and CIE color-science CRT simulation. |

---

## 3. Quick Start

### Build and play
```sh
git submodule update --init --recursive
SNESRECOMP_ROM="/path/to/Secret of Evermore (U) [!].smc" bash tools/regen.sh   # ROM -> src/gen/ (~2-3 min)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/SecretOfEvermoreSNESRecomp "/path/to/Secret of Evermore (U) [!].smc"
```
*(Player data in `build/saves/`, `build/config.ini` and `build/keybinds.ini` survive rebuilds.)*

### New seeds from a new CDL
In an everscript-vscode checkout next to this one (it finds `../SecretOfEvermoreRecomp`; else
`--repo` or `SOE_RECOMP`):
```sh
npm run recomp               # export, byte-identical Asar rebuild, seeds into recomp/, regen + build here
npm run recomp -- --no-build # only write recomp/bank*.cfg (reads recomp/seed-exclusions.txt)
```
Commit the changed `recomp/bank*.cfg` here.

### Lockstep against snes9x
From everscript-vscode: `npm run compare -- recomp=` runs this repository's `build/` game.

---

## 4. Pipeline Architecture

```text
 everscript-vscode (npm run recomp)            this repository
 ┌────────────────────────────────────┐        ┌────────────────────────────────────┐
 │ clean ROM + CDL library + names    │        │ recomp/bank*.cfg   seeds (committed)│
 │ -> Asar disassembly, byte-identical│ seeds  │ recomp/seed-exclusions.txt         │
 │    rebuild, Mesen2 labels          │ ─────▶ │ src/game_rtl.c     our glue        │
 │ -> recomp seeds (func roots, exit  │ ◀───── │ tools/regen.sh     ROM -> src/gen/ │
 │    widths, dispatch, data regions) │ exclu- │ CMake              -> build/       │
 └────────────────────────────────────┘ sions  └────────────────────────────────────┘
```
