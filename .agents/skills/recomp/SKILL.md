---
name: recomp
description: Use when building, running or testing the Secret of Evermore static recompilation (npm run recomp): commands, the generated host project, our glue, CDL seeds, headless runs.
applyTo: "tools/recomp/**,docs/**,src/platform/cdl/recomp-*.js"
---

# Skill: the SoE recomp

Docs: `docs/README.md` (status, map), `headless-exploration.md` (driving the game from a
script), `debugging.md` (modes, dumps, bisection), `bugs-and-todos.md` (open and solved bugs,
each with its lesson), `game-rtl.md` (our glue). Read the bug table before a new hunt: the same
failure classes repeat.

## Commands
`scripts/build-recomp.sh` (= `npm run recomp -- --rom out/rom.sfc`), `scripts/play-recomp.sh`,
`scripts/lockstep.sh`. `out/rom.sfc` is the clean ROM. Headless: `<game>/SecretOfEvermore
--no-launcher --script s.txt <rom>` with `SNESRECOMP_DUMP_DIR`. Copy the package to the scratchpad
before experimenting: `saves/`, `config.ini` belong to the user.

## Ownership
- the repository root is generated (snesrecomp, PolyForm Noncommercial): never commit it, never
  copy its files into the repo. Change it only through `tools/recomp/host.js` hooks (it patches
  snesrecomp's `main.c` and `beam_frame_driver.c`) or our glue `src/game_rtl.c`.
- Seeds come from `src/platform/cdl/recomp-seeds.js` / `recomp-analysis.js`.

## Bugs
Reproducing, comparing against snes9x / Mesen2, classifying and fixing a recomp bug, and where
every file lives: **`recomp-debugging` skill**. Its trap list is the short version of
`docs/bugs-and-todos.md` §2.

## Finishing
Record every address learned (`game-knowledge` skill), add the bug with its lesson to
`docs/bugs-and-todos.md`, then the `release-ritual`.
