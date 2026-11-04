# Secret of Evermore Recomp

A native recompilation of **Secret of Evermore** (USA), built on
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).

The seeds come from [everscript-vscode](https://github.com/r-bin/everscript-vscode), which
records the running game (CDL) and turns what it learns into snesrecomp input; the glue, the
docs and the debugging tools live here.



> **You must legally own a copy of the game.** No ROM data is distributed with
> this project, in the repository or in any release. The recompiled C is
> generated locally from your own copy and is never committed.

## Status

Five replays, about 17.5 minutes of distinct play from power-on into Prehistoria, run identically to MesenCE at every
frame: all memory, and the picture but for fade rounding. 99.9 % of what they execute runs natively, and three routines
run as reconstructed C. The whole game has not been proven, and no release exists yet. Per replay:
[docs/progress.md](docs/progress.md); the bug history: [docs/bugs-and-todos.md](docs/bugs-and-todos.md).

These results need four snesrecomp branches that are not upstream yet
([docs/upstream-access-timing.md](docs/upstream-access-timing.md)), and the pinned commit is local for now.

## ROM identity

| | |
|---|---|
| File | `rom.sfc` |
| Publisher | — |
| Developer | — |
| Year | — |
| Mapping | hirom |
| Region | USA (North America) |
| Coprocessor | none |
| CRC32 | `a5c0045e` |
| SHA-256 | `17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db` |

`tools/regen.sh` refuses to run against anything else, so a mismatched
revision fails immediately instead of producing subtly wrong output.

## Build

```sh
git submodule update --init --recursive
bash tools/regen.sh --rom /path/to/rom.sfc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The ROM does not have to live in the repository — keeping it on your own
drive is the better habit, and `SNESRECOMP_ROM` sets the path once for a
shell. Without either, `tools/regen.sh` finds `rom.sfc` at the repo root,
then the path the setup wizard recorded in `rom.cfg`; `.gitignore` blocks
both from ever being committed.

`tools/regen.sh` verifies the ROM, generates `src/gen/*.c`, and re-syncs
`recomp/funcs.h`. Re-run it whenever you change anything under `recomp/`.
Our frame driver (`src/soe_frame_driver.c`) and the
glue (`src/game_rtl.c`) hold everything this game needs ([docs/game-rtl.md](docs/game-rtl.md)).

New seeds (a bigger CDL, a new exclusion in `recomp/seed-exclusions.txt`) come from
everscript-vscode: `npm run recomp` in a sibling checkout writes `recomp/bank*.cfg` here
([docs/setup.md](docs/setup.md) §4).

## Run

```sh
./build/SecretOfEvermoreSNESRecomp                     # launcher picks the ROM
./build/SecretOfEvermoreSNESRecomp /path/to/rom.sfc # or name it and skip the launcher
```

With no ROM on the command line the build opens the recomp-ui launcher: a ROM
picker with a verification badge, plus display / audio / input settings. Your
choice is cached in `rom.cfg` beside the executable, so the next launch opens
on it and "Skip launcher on boot" makes it immediate. If the launcher is not
built in (`--no-recomp-ui`), the host falls back to a native file picker.

Either way the ROM is checked against the digests above before anything boots
— a dump that could not have produced this build is refused at the door rather
than mis-executing ten frames in.

## Layout

| Path | What lives there |
|---|---|
| `recomp/` | Analysis input: `bank*.cfg` (seeds), `seed-exclusions.txt`, `symbols.toml`, generated `funcs.h` |
| `rom_identity.txt` | ROM digests — read by the build, `tools/regen.sh` and CI |
| `src/` | Host code: `game_rtl.c` (our glue), `main.c` |
| `src/gen/` | Generated C. Never committed — regenerate locally |
| `snesrecomp/` | Framework submodule (owns `lib/recomp-net`, `lib/retcomm-rbengine`) |
| `tools/` | `regen.sh` — the ROM → C pipeline; `bisect.js`, `verify-intro.js`, `check-obj-tiles.js` |
| `scripts/` | `package_release.sh` — player-facing zip |
| `framework_pins.txt` | Exact framework commits this project was scaffolded against |
| `docs/` | Status, setup, glue, debugging, headless exploration, bugs and roadmap, research |

## Porting from here

The scaffold stops where the game-specific work starts. In rough order:

1. **Make it boot.** `src/game_rtl.c` holds the frame driver. It starts on
   the framework's beam-aligned driver (`snesrecomp/runner/src/beam_frame_driver.h`):
   one frame per PPU field, interrupts taken where the beam latches them,
   the field rasterized from the raster journal. A title whose main loop it
   does not fit replaces those calls with its own driver.
2. **Name things.** Add entries to `recomp/symbols.toml` as you identify
   routines, then re-run `tools/regen.sh`. Set `emit = true` to promote one
   into ahead-of-time analysis; leave it false to keep it interpreted.
   Regen synchronizes the marked blocks in each bank cfg, creating missing
   configs. Only proven variants become AOT code. See `recomp/README.md`.
3. **Resolve dispatch misses.** After every run, deal with unresolved
   indirect targets before anything else — they are the reason a port
   diverges, and they are cheap to fix early.
4. **Never synthesise a result** to get past uncovered code, and never edit
   `src/gen/` by hand. Fix the config or the framework and regenerate.

## Multiplayer

Single player.

## License

This project's own source is under the license in `LICENSE`. The framework
carries its own terms — see `snesrecomp/LICENSE` and
`snesrecomp/THIRD_PARTY_ATTRIBUTION.md`. Neither covers the game data, which
is not distributed here.
