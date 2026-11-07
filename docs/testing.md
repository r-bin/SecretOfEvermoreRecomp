# Testing

How the recomp is tested, and how to reproduce every result against MesenCE yourself. Everything runs from this repository
and its submodules. Your ROM stays yours: pass its path, never commit it.

## What a test proves

The recomp is correct when it does what MesenCE does, clock for clock. A replay is played on both from power-on with the
same input. At every NMI each side hashes WRAM, VRAM, CGRAM and OAM, and the hashes must be equal at every NMI to the end:

- **Every byte, from the first NMI.** Nothing is copied from MesenCE into the recomp, no key is skipped, no byte is masked.
- **Aligned on the NMI count**, not on host frames: the key is the number of NMIs since the reset code first writes 0 to
  the frame counter `$7E0100` (the attract demo zeroes it again when it loops; a drop counts as a restart from 0). Both
  sides sample as the NMI is taken, an instruction boundary on both.
- **Both start from the same machine:** RAM zeroed at power-on, battery RAM empty (each run gets its own empty save folder;
  your saves are never read or written).

One accommodation, for a MesenCE bug, not for the recomp: MesenCE maps the ROM's 4th megabyte as one 4 KB page repeated.
In compare runs the recomp's copy of the ROM gets the same 4th MB (`SOE_COMPARE_MESEN_ROM_MIRROR`), so a read past the end
of the 3 MB ROM sees what MesenCE sees. A normal launch keeps the cartridge mirror. Details:
[imperfections/mesen-rom-mirror.md](imperfections/mesen-rom-mirror.md).

## ctest

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSOE_TEST_ROM=/path/to/rom.sfc
cmake --build build -j
ctest --test-dir build --output-on-failure        # 4-6 minutes on an M-series Mac; replay 02 is most of it
```

| Test | Needs | What it proves |
|---|---|---|
| `compare_units` | node | the compare tool's logic, on synthetic data; the committed replays hold input and settings only |
| `import_units` | node | `tools/import.mts` and `tools/annotate.mts`, on synthetic folders |
| `reset_equals_boot` | ROM | a console reset brings the game back as power-on does |
| `save_state_resume` | ROM | a save state resumes the run |
| `replay_01_recorded` | ROM, node | replay 01 equals the stored MesenCE run at every 60th NMI |
| `replay_02_recorded` | ROM, node | replay 02 equals the stored MesenCE run at every 60th NMI |

Node 23.6 or later (the tools are TypeScript run directly). Without `-DSOE_TEST_ROM` only the first two run.

The `recorded` tests need no MesenCE: `tests/recorded/<replay>.crc` holds MesenCE's CRCs of WRAM, VRAM, CGRAM and OAM every
60 NMIs, and no game data. A failure says at which NMI the recomp parts; for the bytes, run against MesenCE itself (below).
`SOE_HLE_MUTATE=1` corrupts the C bindings' results: a control that the test does fail on a wrong game.

## The replays

Both are MesenCE movies (`.mmo`), recorded on MesenCE 2.2.1 from power-on with RAM zeroed. They hold the input and
MesenCE's settings, nothing from the ROM (`compare_units` checks that every committed movie has no save state and no
battery RAM).

| Replay | NMIs | What it plays |
|---|---|---|
| `tools/compare/replays/01-no-inputs-1-loop.mmo` | 8,592 | Power-on with no button pressed: logo, intro, title, one loop of the attract demo |
| `tools/compare/replays/02-new-game-to-flowers.mmo` | 23,756 | New game: names, the 1965 and 1995 prologue, the crash landing, Prehistoria to the flowers |

Replay 01 is the one every change must pass. A movie with a save state or battery RAM holds memory from a ROM: keep it in
`tools/compare/replays/local/`, which git ignores.

## Against MesenCE

### The reference

**MesenCE 2.2.1**, the release: [nesdev-org/MesenCE](https://github.com/nesdev-org/MesenCE), tag `2.2.1`, commit
`20ba206cef5ba207c21203176d02cb9f43dda9fb`. The binary names its commit (`2.2.1+20ba206c…` in its version string). All
settings at their defaults except the two the harness passes: `--snes.ramPowerOnState=AllZeros` (as the movies were
recorded) and `--snes.disableFrameSkipping=true`. The harness finds MesenCE at `/Applications/Mesen.app` or at `$MESEN`
(the executable) and runs it headless (`--testrunner` with a generated Lua script).

### Reproduce

1. **ROM:** Secret of Evermore (USA), headerless, SHA-256
   `17c864a76d498feb6479eee8e7d6807b951c66225033228622bb66754baab1db` (all digests: `rom_identity.txt`).
2. **Build:**
   ```sh
   git clone --recursive https://github.com/r-bin/SecretOfEvermoreRecomp && cd SecretOfEvermoreRecomp
   bash tools/regen.sh --rom /path/to/rom.sfc        # verifies the ROM, applies framework/snesrecomp.patch, generates src/gen/
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
   ```
3. **Compare** (replay 01 about 2 minutes, replay 02 about 5):
   ```sh
   node tools/compare/compare.mts mesen recomp /path/to/rom.sfc \
     --replay tools/compare/replays/01-no-inputs-1-loop.mmo --regions wram,vram,cgram,oam --verbose
   ```
   Expected: `in sync through counter $0100 = 8592 (≈143.0 s), 8592 checkpoints`. For replay 02:
   `in sync through counter $0100 = 23756 (≈395.3 s), 23756 checkpoints`.

`--verbose` compares every NMI (without it, every 60th). `--interpreted` runs the recomp with every routine interpreted
instead of compiled. A run that parts names the first NMI and region that differ and lists the differing bytes with
their WRAM names; the report goes to `out/compare/`.

### Everything at once

```sh
node tools/compare/full.mts /path/to/rom.sfc       # every replay in replays/ and replays/local/: all RAM, the picture, S-DSP state
```

The results are in [progress.md](progress.md).

### Record a reference again

After a change that changes what MesenCE and the recomp do together (a new MesenCE, a new replay):

```sh
node tools/compare/compare.mts mesen recomp /path/to/rom.sfc --replay tools/compare/replays/01-no-inputs-1-loop.mmo \
  --regions wram,vram,cgram,oam --record tests/recorded/01-no-inputs-1-loop.crc
```

The recording is written only from the MesenCE side; the run still compares the recomp against it.

## The framework's own tests

The patched framework passes upstream's test suites: `pytest tests/v2 tests/test_snes_cycles.py` in `snesrecomp/` (596 passed,
with `lib/` submodules initialized). `tests/run_c_tests.sh` passes as far as upstream main does on macOS: both stop at the
same two steps (the lobby test does not build on macOS; later steps need `pkg-config`). Which framework change each test
covers: [pr/README.md](pr/README.md).
