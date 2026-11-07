# Setup

What the build needs, how to build, and how new seeds and reconstructed C come in. Build and test need nothing but this
repository and its submodules.

## 1. Prerequisites

| Component | Version | Used by |
|---|---|---|
| Your ROM | Secret of Evermore (USA), headerless, SHA-256 in `rom_identity.txt` | everything; keep it outside the repository |
| Python | 3.9+ | `tools/regen.sh` (snesrecomp's recompiler) |
| Rust (cargo) | stable | snesrecomp's analyzer, built on the first regen (about 30 s) |
| CMake | 3.20+ | the build |
| C compiler | C11 | AppleClang, GCC or MSVC |
| node | 23.6+ | the tests and `tools/compare` (TypeScript run directly) |
| MesenCE 2.2.1 | the release ([testing.md](testing.md)) | only to compare against MesenCE itself |

```sh
git clone --recursive https://github.com/r-bin/SecretOfEvermoreRecomp
```

In an existing clone: `git submodule update --init --recursive`.

## 2. Build

```sh
bash tools/regen.sh --rom /path/to/rom.sfc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

| Step | What happens |
|---|---|
| `tools/regen.sh` | checks the ROM against `rom_identity.txt`, applies `framework/snesrecomp.patch` to the submodule ([framework.md](framework.md)), lifts the code the seeds in `recomp/*.cfg` reach to C in `src/gen/` (never committed; the log must say `28 bank(s) emitted`) |
| CMake | `build/SecretOfEvermoreSNESRecomp`, with `assets/` and `mods/` beside it |

The ROM path: `--rom`, else `SNESRECOMP_ROM`, else `rom.sfc` at the repository root, else the path the launcher saved in
`rom.cfg`. `.gitignore` keeps every one of them out of git. Re-run `tools/regen.sh` after anything under `recomp/` changes.

`saves/`, `config.ini` and `keybinds.ini` beside the executable are the player's and survive rebuilds. Copy `build/` to a
scratch folder before experimenting with it.

## 3. Where the seeds come from

The seeds (`recomp/bank8x..9x.cfg`, the data-region block in `recomp/bank00.cfg`) say where routines start, with which
register widths they are entered and left, where indirect jumps go, and which bytes are data. They are written by
[everscript-vscode](https://github.com/r-bin/everscript-vscode) from a Code/Data Log recorded while the game was played
(90 % of the ROM, [cdl-and-seeds.md](cdl-and-seeds.md)), its curated names, and its asset maps. The reconstructed C in
`src/bound-c/` and the names for the generated C come from there too. This repository holds a copy of each; the CDL
itself is not committed.

## 4. New seeds, bound C or names

Only needed when the recording grows, an exclusion is added, or a reconstructed routine changes. Needs an
everscript-vscode checkout.

```sh
# in everscript-vscode: export into its out/recomp/; writes nothing here
npm run recomp
# here: copy the seeds to recomp/, its reconstructed-c to src/bound-c/, the names to imported/
node tools/import.mts --from /path/to/everscript-vscode      # or EVERSCRIPT=/path/to/everscript-vscode
bash tools/regen.sh
```

Then build, run replay 01 ([testing.md](testing.md)), and commit the changed `recomp/bank*.cfg` and `src/bound-c/`.
`imported/annotations.tsv` is text from your ROM and stays out of git.

The export reads two files from this repository: `recomp/seed-exclusions.txt`, and the last regen's
`src/gen/program_manifest.json` for symbol names. After the first regen of new seeds, export and import once more.

What the import does: `recomp/bank8x..9x.cfg` are replaced; `recomp/bank00.cfg` keeps its own text and gets one
`data_region` block between the `everscript data regions` markers; `src/bound-c/` is rewritten from the list
`SOE_BOUND_C` in `CMakeLists.txt` and every header.

### `recomp/seed-exclusions.txt`

One line per directive the export must not emit, each with its bug in [bugs-and-todos.md](bugs-and-todos.md):
`func <pc24> <reason>` (no root, plus `force_lle`: interpreted for every caller) or `dispatch <pc24> <reason>` (no
`indirect_dispatch` at that site). An address covers all its HiROM mirrors. `hle` lines bind a routine to its
reconstructed C ([game-rtl.md](game-rtl.md) §4b).

## 5. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `cargo: command not found` | Rust not on `PATH` | `export PATH="$HOME/.cargo/bin:$PATH"` |
| `snesrecomp submodule missing` | submodules not checked out | `git submodule update --init --recursive` |
| `apply-framework-patch: snesrecomp is at neither the pinned commit nor the patched state` | the submodule was changed by hand | `git submodule update --init --force snesrecomp`, then `git -C snesrecomp clean -fd` (discards changes in the submodule) |
| `ROM differs` | another release or a headered dump | the USA ROM, headerless; digests in `rom_identity.txt` |
| The character-name screen renders garbled | the new renderer is on | `SoeAfterConfig` turns it off ([game-rtl.md](game-rtl.md) §4) |
