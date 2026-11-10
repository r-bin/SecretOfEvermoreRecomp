---
name: recomp
description: Use when building, running or testing the Secret of Evermore recompilation: regen and build, the tests and replays, the seeds from everscript-vscode, our glue, the framework patch, headless runs.
applyTo: "**"
---

# Skill: the SoE recomp

Docs: `docs/README.md` (map), `setup.md` (build, new seeds), `testing.md` (tests, replays, MesenCE), `game-rtl.md` (our
glue), `headless-exploration.md` (driving the game from a script), `debugging.md` (modes, dumps, bisection),
`bugs-and-todos.md` (each solved bug with its lesson: read it before a new hunt, the same failure classes repeat).

## Commands

```sh
bash tools/regen.sh --rom <rom>                      # ROM -> src/gen/ (~2-3 min); the log must say `28 bank(s) emitted`
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSOE_TEST_ROM=<rom> && cmake --build build -j
ctest --test-dir build --output-on-failure           # 4-6 min: tool tests, reset, save state, both replays
```

- Headless: `build/SecretOfEvermoreSNESRecomp --no-launcher --script s.txt <rom>` with `SNESRECOMP_DUMP_DIR`. Copy `build/`
  to a scratch folder before experimenting: `saves/`, `config.ini` belong to the user.
- `regen.sh` deletes `src/gen` first: upstream's emitter reuses it without looking at `SNESRECOMP_EXACT_BUS` /
  `SNESRECOMP_AOT_RESUME`.
- New seeds, bound C, names: `npm run recomp` in an everscript-vscode checkout (writes nothing here), then
  `node tools/import.mts --from <checkout>`. Nothing else reads another checkout.
- No `timeout` on macOS: bound a long run with `perl -e 'alarm 900; exec @ARGV' <command>`.

## Which replays to run

The committed replays are MesenCE movies from power-on (`tools/compare/replays/`, testing.md):
- **`01-no-inputs-1-loop.mmo` is mandatory** for every change: `ctest` (against the stored run), and against MesenCE itself
  before committing a timing change (`compare.mts mesen recomp ... --verbose`, about 2 minutes). Native against
  interpreted needs no MesenCE: `tools/compare/probes/native-vs-interp.mts`.
- `02-new-game-to-flowers.mmo` runs in `ctest` too; against MesenCE it takes about 5 minutes. Run it before a commit that
  touches input, the field or sound.
- Replays in `replays/local/` are extra: run them when it costs nothing, from a copy of the executable, and never block a
  commit on them. A full-game recording does not sync: never use it for parity or native share.
- A replay becomes mandatory only once it is known to contain one specific action, and then for changes to that action.
- **Replays run vanilla:** WRAM and picture are compared with no mod active unless the user asks. A mod that changes the
  game (above all *Widescreen (experimental)*, which changes how enemies sleep, spawn and aggro; the practice hotkeys) never
  has to match a replay. Runs read the launcher's shared `build/mods/preloaded/state.toml`: set the kill switches
  (`SOE_WS_ALIVE=0 SOE_CHEATS=0`, as `tests/replay_recorded.sh` does) or run from a copy of the build with none selected.

## Ownership

- Ours: `src/` (but `src/gen/` and `src/bound-c/`), `tools/`, `tests/`, `docs/`, `recomp/seed-exclusions.txt`,
  `recomp/symbols.toml`, `framework/snesrecomp.patch`.
- Imported, never edited here: `recomp/bank8x..9x.cfg`, the data-region block of `recomp/bank00.cfg`, `src/bound-c/`.
- `snesrecomp/` is upstream plus `framework/snesrecomp.patch`; to change the patch, work on a framework branch and
  regenerate the patch (`docs/framework.md`). `docs/pr/` holds each change as an upstream PR.
- `src/gen/` is derived from the ROM: never commit it. No ROM bytes, dumps or screenshots in git.

## Bugs

Reproducing, comparing, classifying and fixing a recomp bug: **`recomp-debugging` skill**.

## Finishing

The bug with its lesson in `docs/bugs-and-todos.md`, a difference from MesenCE in `docs/imperfections/`; game facts go to
everscript-vscode's knowledge entries. Work on `develop`; `main` only takes releases. Commit only your files, by path.
