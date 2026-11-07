# Secret of Evermore Recomp

A native recompilation of **Secret of Evermore** (SNES, USA), built on
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

> **You must own the game.** No ROM data is distributed with this project, in the repository or in any release. The C code
> is generated on your machine from your own copy and is never committed.

## Status

Two replays, about 9 minutes of play from power-on to Prehistoria, run identically to MesenCE 2.2.1: WRAM, VRAM, CGRAM
and OAM are equal at every NMI from power-on, with nothing copied in or masked. Almost all of what they execute runs as
compiled code, and 14 routines run as reconstructed C. The whole game has not been proven, and there is no release yet.
Per replay and memory region: [docs/progress.md](docs/progress.md).

The results need changes to snesrecomp that are not upstream yet. They are one file, `framework/snesrecomp.patch`, which
`tools/regen.sh` applies, and they are on their way upstream as pull requests ([docs/pr/README.md](docs/pr/README.md)).

## Build

Needs Python 3.9+, Rust (cargo), CMake 3.20+, a C11 compiler, and node 23.6+ for the tests.

```sh
git clone --recursive https://github.com/r-bin/SecretOfEvermoreRecomp && cd SecretOfEvermoreRecomp
bash tools/regen.sh --rom /path/to/rom.sfc      # verifies the ROM, applies the framework patch, generates src/gen/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`tools/regen.sh` refuses any ROM but the one below. Instead of `--rom` you can set `SNESRECOMP_ROM`. Re-run it after
anything under `recomp/` changes. Details and troubleshooting: [docs/setup.md](docs/setup.md).

| ROM | |
|---|---|
| Title | Secret of Evermore (USA), headerless, HiROM, 3 MB |
| CRC32 | `a5c0045e` |
| SHA-256 | `17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db` |

## Test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSOE_TEST_ROM=/path/to/rom.sfc
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The replay tests check the recomp against stored MesenCE runs, so MesenCE is not needed. To compare against MesenCE
itself, and for what each test proves: [docs/testing.md](docs/testing.md).

## Run

```sh
./build/SecretOfEvermoreSNESRecomp                    # the launcher picks the ROM
./build/SecretOfEvermoreSNESRecomp /path/to/rom.sfc   # or name it
```

The launcher checks the ROM against the digests above before anything boots. Saves, `config.ini` and `keybinds.ini` live
beside the executable and survive rebuilds.

## Layout

| Path | What |
|---|---|
| `src/` | Our glue: `game_rtl.c` (hooks, save states, compare harness), `soe_frame_driver.c` (frame and interrupt timing), `soe_bindings.c` (routines run as C), `main.c` |
| `src/bound-c/` | Reconstructed C of the bound routines, imported from [everscript-vscode](https://github.com/r-bin/everscript-vscode) |
| `src/gen/` | Generated C, never committed |
| `recomp/` | Analysis input: `bank*.cfg` (seeds), `seed-exclusions.txt`, `symbols.toml` |
| `framework/snesrecomp.patch` | Our snesrecomp changes, applied by `tools/regen.sh` ([docs/framework.md](docs/framework.md)) |
| `snesrecomp/`, `recomp-ui/` | Submodules at the commits in `framework_pins.txt` |
| `tools/` | `regen.sh` (ROM to C), `compare/` (MesenCE against the recomp), `import.mts` (new seeds and C), debugging helpers |
| `tests/` | ctest scripts and the stored MesenCE runs |
| `docs/` | Status, setup, testing, debugging, the framework PRs: [docs/README.md](docs/README.md) |

The seeds and the bound C come from everscript-vscode, which records the running game. Build and test need nothing but
this repository; only new seeds need an everscript-vscode checkout ([docs/setup.md](docs/setup.md) §4).

## License

This project's own source is under PolyForm Noncommercial 1.0.0 (`LICENSE`). The scaffold and `src/soe_frame_driver.c`
derive from snesrecomp under the same terms. snesrecomp, and `framework/snesrecomp.patch` as changes to it, carry
snesrecomp's terms: `snesrecomp/LICENSE`, `snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Neither covers the game, which is not
distributed here.
