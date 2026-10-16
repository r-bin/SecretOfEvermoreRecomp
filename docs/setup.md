# Setting Up the Recompilation Pipeline

This guide details the complete prerequisites, information requirements, environment configuration,
and step-by-step build pipeline for compiling Secret of Evermore into a native C executable.

---

## 1. What Information We Need

The recompilation pipeline does not generate code blindly from raw bytes; it synthesizes
runtime observations with verified structural knowledge about Secret of Evermore.

### 1.1 The Clean ROM
- **Target:** *Secret of Evermore (USA)*, 3,145,728 bytes (3 MB), HiROM layout.
- **SHA-1:** `79e7738630fff5699217ef58ecc421bc8fcbcd89`
- **Role:** Source of all game machine code and binary assets. snesrecomp analyzes these exact bytes,
  and the Asar rebuild step must match this checksum byte-for-byte.
- **Location:** the Evermore base ROM of the ROM library (`npm run roms -- add <path>`), or `--rom <path>`.

### 1.2 The CDL Library (Code/Data Logger)
- **Source:** Generated during play sessions in the embedded emulator panel or headless test harness.
- **Location:** Merged across sessions in the VS Code extension's global storage:
  - macOS: `~/Library/Application Support/Code/User/globalStorage/rbin.everscript/cdl-library/<sha1>/`
  - Linux: `~/.config/Code/User/globalStorage/rbin.everscript/cdl-library/<sha1>/`
  - Windows: `%APPDATA%\Code\User\globalStorage\rbin.everscript\cdl-library\<sha1>\`
- **What it provides:**
  - **Code vs Data masks:** Byte-level tracking of which offsets executed as opcode bytes vs data bytes.
  - **Processor Register Widths ($M/X$):** The 8-bit vs 16-bit accumulator and index register widths
    observed on function entry and exit.
  - **Data Bank ($DB$) and Direct Page ($D$) registers:** Concrete runtime values observed during execution.
  - **Observed Call Graph:** Callers and callees for every executed routine.
  - **Indirect Jump Targets:** Concrete targets observed at runtime for jump tables (`jmp ($addr,x)`),
    pointer calls (`jmp ($ptr)`), and stack returns (`pei ($dp); rts`).

### 1.3 Curated Game Knowledge
- **`src/games/evermore/localizations/functions.js`:** Hand-curated, verified function names and signatures.
- **`src/games/evermore/localizations/tables.js`:** Catalogs jump tables and data lookup tables with entry counts.
- **`src/games/evermore/localizations/knowledge/ram/*.md`:** Curated WRAM variable and structure documentation.
- **Role:** Transformed into clean, valid C identifiers (e.g. `Random number generator` → `random_number_generator`)
  and Mesen2 labels, replacing cryptic hex offsets with meaningful symbols across all generated C files.

### 1.4 ROM Asset Models
- Asset boundaries defined in `src/games/evermore/rom/` and `src/games/evermore/maps/`:
  - 127 room blobs (`rooms/room_XX.bin`)
  - 3,002 dialogue and menu strings (`str_XXXX`)
  - Map collision grids, tilesets, palettes, and sprite graphics.
- **Role:** Explicitly tagged as data regions in the snesrecomp configuration (`data_region` directives),
  preventing the static analyzer from misinterpreting compressed graphics or dialogue as 65816 machine code.

---

## 2. Toolchain Prerequisites

| Component | Required Version | Purpose | Setup / Resolution |
|---|---|---|---|
| **Node.js** | $\ge 20.0$ | Orchestration scripts (`tools/recomp/*.js`) | System package / `nvm` |
| **Asar** | $\ge 1.90\text{pre}$ | SNES assembler for disassembly verification | `--asar` / `ASAR` / PATH / `../asar/bin/asar` |
| **snesrecomp** | `main` branch | 65816 static recompiler & host project template | Git clone with submodules (see below) |
| **Python** | $\ge 3.9$ | snesrecomp script execution | System Python 3 |
| **Rust / cargo** | Stable | Compiles snesrecomp's native 65816 analyzer | `curl https://sh.rustup.rs -sSf \| sh` |
| **CMake** | $\ge 3.20$ | Configures the host C project | System package |
| **C Compiler** | Modern C11 | Compiles generated C and desktop runner | AppleClang (macOS), GCC (Linux), MSVC (Windows) |

### Setting Up snesrecomp
Clone the snesrecomp repository alongside `everscript-vscode` (or specify via `SNESRECOMP`):
```sh
git clone https://github.com/RetroPortingToolKit/snesrecomp
cd snesrecomp
git submodule update --init --depth 1
```
*(On macOS and Linux, the first run builds snesrecomp's native analyzer using Cargo, taking ~30 seconds).*

---

## 3. Environment Variables & Command Line Flags

| Variable / Flag | Default | Description |
|---|---|---|
| `--rom` | the ROM library's base ROM | Absolute path to the clean Secret of Evermore ROM |
| `SNESRECOMP` / `--snesrecomp` | `../snesrecomp` | Path to the snesrecomp checkout |
| `ASAR` / `--asar` | `asar` on PATH | Path to the Asar assembler binary |
| `SOE_RECOMP_EXE` / `--exe` | Built output binary | Path to recompiled executable (for lockstep tests) |
| `--fresh` | `false` | Deletes the repository root and scaffolds from scratch |
| `--strict` | `false` | Treats warnings and incomplete directives as fatal build errors |
| `--no-build` | `false` | Generates the C source files but skips CMake compilation |
| `--no-recomp` | `false` | Stops after the Asar export and rebuild step (~2 s) |
| `--lib` | Global storage | Override path to the CDL library |

---

## 4. The End-to-End Build Pipeline (`npm run recomp`)

When you invoke:
```sh
npm run recomp
```
`tools/recomp/build.js` executes the following sequence:

### Step 1: Input Gathering & Validation
- Validates the ROM SHA-1 against `79e7738630fff5699217ef58ecc421bc8fcbcd89`.
- Locates the CDL library for this SHA-1 in global storage.
- Loads curated function names from `src/games/evermore/localizations/functions.js` and tables from `tables.js`.

### Step 2: Asar Export
- Generates `out/recomp/asar/`:
  - `main.asm`, `banks/bank_XX.asm` with function labels and caller/access comments.
  - `rooms/room_XX.bin` containing extracted room data.
  - `tables/` containing editable lookup tables.
  - `ram.asm`, `enums.asm`, `structs.asm`, `structs.h`, and `functions.json`.
  - Mesen2 debugging files: `labels.mlb` and `rom.cdl`.
  - snesrecomp seed directives: `recomp/cfg/bankXX.cfg`.

### Step 3: Byte-Identical Rebuild Verification
- Invokes Asar to assemble `main.asm` into `build/out.sfc`.
- Performs a byte-by-byte SHA-1 verification against the clean ROM.
- If a single byte differs, the build halts immediately with `asar.log`.

### Step 4: Host Project Scaffolding
- If the repository root does not exist (or `--fresh` is passed):
  - Calls snesrecomp's `tools/new_project/setup_project.sh`:
    `--players 1 --no-netplay --no-ci --no-fetch-boxart --no-github --no-generate --no-build`
  - Initializes git submodules for the desktop runner and SDL frontend.
  - Configures the project under the repository root (git-ignored).

### Step 5: Game RTL Glue Overlay & Main Hook
- Copies `tools/recomp/host/` onto the repository root, replacing `src/game_rtl.c`.
- Patches the scaffold's generated `src/main.c` to add `.after_config = SoeAfterConfig`.
- Overwrites `project/recomp/bank8x/9x.cfg` with our generated seeds.
- Injects `data_region` directives into `project/recomp/bank00.cfg`.

### Step 6: Code Generation (`regen.sh`)
- Runs `bash tools/regen.sh --cfg-roots --rom <clean_rom>`.
- The snesrecomp analyzer evaluates function graphs, tracks M/X widths, resolves entry points,
  and lifts 65816 instructions into native C functions in `project/src/gen/*.c`.

### Step 7: Host Compilation
- Runs CMake configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`.
- Compiles the project: `cmake --build build -j --config Release`.
- Produces `SecretOfEvermoreSNESRecomp` executable.

### Step 8: Standalone Packaging
- Creates `out/recomp/SecretOfEvermore-<os>-<arch>/`:
  - Copies executable, `assets/`, and `mods/`.
  - Generates launcher scripts: `play.command` (macOS), `play.sh` (Linux), or `play.bat` (Windows).
  - Preserves user save files in `saves/` (`save.srm`, `save<N>.sav`) and configuration files
    (`config.ini`, `keybinds.ini`) across rebuilds.

### Step 9: Report Generation
- Writes `out/recomp/REPORT.md` and `out/recomp/report.json`, detailing coverage percentages,
  named functions, native vs LLE variants, and prioritized next steps.

---

## 5. Common Setup Issues & Troubleshooting

| Symptom | Cause | Solution |
|---|---|---|
| `cargo: command not found` | Rust binary directory not in shell `PATH` | Ensure `export PATH="$HOME/.cargo/bin:$PATH"` is in your profile |
| `scaffold failed (recomp-host.log)` | Missing submodules in snesrecomp repository | Run `git submodule update --init --depth 1` in `snesrecomp/` |
| `ROM differs` during Asar rebuild | Uncommitted modifications or corrupt export | Run `npm run recomp -- --fresh` to force a clean re-export |
| `recomp-host.log: regen failed` | Python 3 or Cargo build issue | Check `out/recomp/recomp-host.log` for compilation stack trace |
| Character-name screen renders garbled | `NewRenderer` active instead of legacy PPU | Verify `SoeAfterConfig` hook is present in `src/main.c` (see [game-rtl.md](game-rtl.md)) |

