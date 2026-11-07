# Testing

Everything here runs from this repository alone. Your ROM stays yours: pass its path, never commit it.

## ctest

`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSOE_TEST_ROM=/path/to/rom.sfc`, build, then
`ctest --test-dir build --output-on-failure`. `tools/compare` needs node 23.6 or later (it runs TypeScript directly).

| Test | Needs | What it proves |
|---|---|---|
| `compare_units` | node | the compare tool's logic, on synthetic data |
| `import_units` | node | `tools/import.mts` (seeds, bound C) and `tools/annotate.mts`, on synthetic folders |
| `reset_equals_boot` | ROM | a reset brings the game back as power-on does |
| `save_state_resume` | ROM | a save state resumes the run |
| `replay_01_recorded` | ROM, node | replay 01 equals the stored MesenCE run: WRAM, VRAM, CGRAM, OAM at every 60th NMI |

## The stored reference

`tests/recorded/01-no-inputs-4m.crc` is a MesenCE run of replay 01, kept as CRCs and the eight RNG bytes the recomp is given. It
holds no game data. It lets anyone check the build without MesenCE, but says only where a difference starts: for the bytes, run
against MesenCE itself. Record it again with:

```sh
node tools/compare/compare.mts mesen recomp <rom> --replay tools/compare/replays/01-no-inputs-4m.evsmv \
  --regions wram,vram,cgram,oam --record tests/recorded/01-no-inputs-4m.crc
```

## Against MesenCE

MesenCE 2.2.1 from `/Applications/Mesen.app` or `$MESEN`.

```sh
node tools/compare/compare.mts mesen recomp <rom> --replay tools/compare/replays/01-no-inputs-4m.evsmv \
  --regions wram,vram,cgram,oam --verbose      # about 4 minutes
node tools/compare/full.mts <rom>              # every replay you have, all RAM, picture and S-DSP state
```

## Replays

Only replays without a save state or save data are committed: today 01. Anything else goes in `tools/compare/replays/local/`,
which git ignores, and `full.mts` finds it there. A `.mmo` qualifies only with no `SaveState.mss` and no battery file:
`unzip -l` tells. Replay 02's `.evsmv` carries real save data in its `# sram:` line, so it stays local too.
