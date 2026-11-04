# Setting Up the Recompilation Pipeline

This guide covers what the build needs, the toolchain, and the two halves of the pipeline: the
seeds, which everscript-vscode derives from the recorded game, and the build here.

---

## 1. What Information We Need

The recompilation pipeline does not generate code blindly from raw bytes; it synthesizes
runtime observations with verified structural knowledge about Secret of Evermore. Sections 1.2 to
1.4 live in [everscript-vscode](https://github.com/r-bin/everscript-vscode) (paths below are
there); what reaches this repository is `recomp/bank*.cfg`.

### 1.1 The Clean ROM
- **Target:** *Secret of Evermore (USA)*, 3,145,728 bytes (3 MB), HiROM layout.
- **SHA-1:** `79e7738630fff5699217ef58ecc421bc8fcbcd89`
- **Role:** Source of all game machine code and binary assets. snesrecomp analyzes these exact bytes,
  and the Asar rebuild step must match this checksum byte-for-byte.
- **Location:** anywhere on your drive: `tools/regen.sh --rom <path>` or `SNESRECOMP_ROM`; never in the repository.

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
- **`src/games/evermore/localizations/functions.ts`:** Hand-curated, verified function names and signatures.
- **`src/games/evermore/localizations/tables.ts`:** Catalogs jump tables and data lookup tables with entry counts.
- **`src/games/evermore/localizations/data/wram-map.md`, `knowledge/wram/*.md`:** Curated WRAM variable and structure documentation.
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

| Component | Required Version | Purpose |
|---|---|---|
| **snesrecomp** | pinned submodule (`framework_pins.txt`) | 65816 static recompiler and desktop runner |
| **Python** | $\ge 3.9$ | `tools/regen.sh` |
| **Rust / cargo** | stable | snesrecomp's native analyzer (built on the first regen) |
| **CMake** | $\ge 3.20$ | configures the host C project |
| **C compiler** | C11 | AppleClang (macOS), GCC (Linux), MSVC (Windows) |
| **Node.js** | $\ge 20$ | `tools/*.js` (bisection, intro check, OBJ tile check) |
| everscript-vscode | sibling checkout | only to make new seeds: its `npm run recomp` (Asar for the byte-identical rebuild) |

```sh
git submodule update --init --recursive
```
*(The first regen builds snesrecomp's analyzer with cargo, about 30 seconds.)*

---

## 3. Building

```sh
SNESRECOMP_ROM="/path/to/rom.sfc" bash tools/regen.sh     # verify the ROM, generate src/gen/, re-sync recomp/funcs.h
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

| Step | What happens |
|---|---|
| `tools/regen.sh` | checks the ROM against `rom_identity.txt`, seeds the analysis from every `func` in `recomp/*.cfg` (`--cfg-roots`, on by default here), lifts the reachable 65816 code to C in `src/gen/` (never committed) |
| CMake build | `build/SecretOfEvermoreSNESRecomp`, with `assets/` and `mods/` beside it; `saves/`, `config.ini`, `keybinds.ini` are yours and survive rebuilds |

---

## 4. New Seeds (`npm run recomp` in everscript-vscode)

The seeds change when the CDL grows or an exclusion is added. In everscript-vscode, with this
repository as a sibling (`../SecretOfEvermoreRecomp`; else `--repo` or `SOE_RECOMP`):

```sh
npm run recomp                 # everything below, then regen + build here
npm run recomp -- --no-build   # stop after writing recomp/bank*.cfg
npm run recomp -- --no-recomp  # only the Asar export and rebuild (~2 s)
```

1. **Inputs:** the clean ROM, the CDL library, curated names and RAM entries, ROM asset regions,
   and this repository's `recomp/seed-exclusions.txt`.
2. **Asar export** (`out/recomp/asar/` there): `banks/*.asm` with names and comments, `ram.asm`,
   `structs.h`, `functions.json`, Mesen2 `labels.mlb` + `rom.cdl`, and the seeds.
3. **Byte-identical rebuild:** Asar assembles the export; one differing byte stops the run.
4. **Seeds into this repository:** `recomp/bank8x..9x.cfg` replaced; `recomp/bank00.cfg` keeps its
   own text and gets one `data_region` block between the `everscript data regions` markers.
5. **Regen + build** here (skipped with `--no-build`), then `out/recomp/REPORT.md` there:
   coverage, names, native vs interpreted variants, the next steps.

### `recomp/seed-exclusions.txt`
One line per directive the export must not emit, each a bug in [bugs-and-todos.md](bugs-and-todos.md):
`func <pc24> <reason>` (no root, plus `force_lle`: interpreted for every caller) or
`dispatch <pc24> <reason>` (no `indirect_dispatch` at that site). An address covers all its HiROM
mirrors.

---

## 5. Common Setup Issues & Troubleshooting

| Symptom | Cause | Solution |
|---|---|---|
| `cargo: command not found` | Rust not on `PATH` | `export PATH="$HOME/.cargo/bin:$PATH"` |
| `snesrecomp submodule missing` | submodules not checked out | `git submodule update --init --recursive` |
| `ROM differs` during the Asar rebuild | the export or the CDL library is broken | see everscript-vscode's `out/recomp/asar.log` |
| Character-name screen renders garbled | `NewRenderer` active instead of the legacy PPU | check the `SoeAfterConfig` hook in `src/main.c` ([game-rtl.md](game-rtl.md)) |
