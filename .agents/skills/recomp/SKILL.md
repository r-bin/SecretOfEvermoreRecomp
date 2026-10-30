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
New seeds: `npm run recomp` in the sibling everscript-vscode checkout (writes `recomp/bank*.cfg` here).

## Ownership
- Ours: `src/game_rtl.c`, the `after_config` line in `src/main.c`, `tools/`, `docs/`,
  `recomp/seed-exclusions.txt`. The seeds `recomp/bank8x..9x.cfg` and the data-region block of
  `recomp/bank00.cfg` are written by everscript-vscode's export: change them there (or through the exclusions
  file), never by hand.
- `snesrecomp/` is a pinned submodule (PolyForm Noncommercial): never edit or patch it (owner's
  rule). Timing the game needs lives in our frame driver `src/soe_frame_driver.c`.
- `src/gen/` is derived from the ROM: never commit it. No ROM bytes, dumps or screenshots in git.

## Bugs
Reproducing, comparing against snes9x / Mesen2, classifying and fixing a recomp bug, and where
every file lives: **`recomp-debugging` skill**. Its trap list is the short version of
`docs/bugs-and-todos.md` §2.

## Finishing
Add the bug with its lesson to `docs/bugs-and-todos.md`; game facts (addresses, routines) go to
everscript-vscode's knowledge entries. Work on `develop`; `main` only takes releases. Commit only your files.
