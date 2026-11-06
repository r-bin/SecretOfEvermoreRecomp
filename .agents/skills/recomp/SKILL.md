---
name: recomp
description: Use when building, running or testing the Secret of Evermore recompilation: regen and build, the seeds from everscript-vscode, our glue, the framework pin, headless runs.
applyTo: "**"
---

# Skill: the SoE recomp

Docs: `docs/README.md` (status, map), `setup.md` (build, new seeds), `headless-exploration.md`
(driving the game from a script), `debugging.md` (modes, dumps, bisection), `bugs-and-todos.md`
(open and solved bugs, each with its lesson), `game-rtl.md` (our glue). Read the bug table before a
new hunt: the same failure classes repeat.

## Commands
`SNESRECOMP_ROM=<rom> bash tools/regen.sh` (ROM -> `src/gen/`, ~2-3 min), then
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` -> `build/SecretOfEvermoreSNESRecomp`.
Headless: `build/SecretOfEvermoreSNESRecomp --no-launcher --script s.txt <rom>` with `SNESRECOMP_DUMP_DIR`.
Copy `build/` to a scratch folder before experimenting: `saves/`, `config.ini` belong to the user.
Upstream's `v2_emit` reuses `src/gen` without looking at `SNESRECOMP_EXACT_BUS` / `SNESRECOMP_AOT_RESUME`, so `regen.sh` deletes
`src/gen` first; the log must say `28 bank(s) emitted`.
New seeds: `npm run recomp` in the sibling everscript-vscode checkout (writes `recomp/bank*.cfg` here).

## Ownership
- Ours: `src/game_rtl.c`, the `after_config` line in `src/main.c`, `tools/`, `docs/`,
  `recomp/seed-exclusions.txt`. The seeds `recomp/bank8x..9x.cfg` and the data-region block of
  `recomp/bank00.cfg` are written by everscript-vscode's export: change them there (or through the exclusions
  file), never by hand.
- `snesrecomp/` is a submodule on public upstream plus `framework/snesrecomp.patch`, applied by `regen.sh`
  (`docs/framework.md`). Never edit it here; game timing lives in our frame driver `src/soe_frame_driver.c`.
- `src/bound-c/` is exported from everscript-vscode's `reconstructed-c`: never edit it here.
- `src/gen/` is derived from the ROM: never commit it. No ROM bytes, dumps or screenshots in git.

## Bugs
Reproducing, comparing against snes9x / Mesen2, classifying and fixing a recomp bug, and where
every file lives: **`recomp-debugging` skill**. Its trap list is the short version of
`docs/bugs-and-todos.md` §2.

## Finishing
Add the bug with its lesson to `docs/bugs-and-todos.md`; game facts (addresses, routines) go to
everscript-vscode's knowledge entries. Work on `develop`; `main` only takes releases. Commit only your files.

## Which replays to run
`01-no-inputs-4m` (no buttons pressed) is the one mandatory replay: it is the baseline for a change, and what the C share and
native share are worked on. The others (02, 04, 05, 06) are random scenarios, recorded as played: only a fraction of their
content adds to 01, and they were not made to exercise any one action. So:
- Mandatory for a change: 01, native against interpreted (`tools/compare/probes/native-vs-interp.mts`, about a minute, no
  MesenCE), then against MesenCE before a commit.
- The other replays are verbose: more testing helps, so run them when it costs nothing, but never wait on them, block a build
  on them or hold a commit for them. Run them from a copy of the executable, so a build can go on beside them.
- A replay becomes mandatory only once it is known to contain one specific action, and then for changes to that action. A short
  recording made for one action is worth more than a long one that happens to pass it.
- Never run `03-complete-run-7h` for parity or native share: it does not sync.
