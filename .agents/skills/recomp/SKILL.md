---
name: recomp
description: Use when building, running, testing or debugging the Secret of Evermore static recompilation (snesrecomp) — npm run recomp, the generated host project, our glue in tools/recomp/host/, seeds from the CDL, save states, headless scripts, comparing against snes9x, or hunting a glitch that only the recompiled game shows. Covers the workflow and the traps that cost days.
applyTo: "tools/recomp/**,docs/**,src/emulator/cdl/recomp-*.js"
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
  `src/main.c` and `beam_frame_driver.c`) or our glue `src/game_rtl.c`.
- Seeds come from `src/emulator/cdl/recomp-seeds.js` / `recomp-analysis.js`.

## Traps (each cost hours)
1. **Excluding a root is not keeping code off the native tier.** snesrecomp rediscovers a routine
   as a JSL target and compiles it. Use `force_lle <pc24>` (emitted for every `FUNC_EXCLUSIONS`
   entry since v0.201.19) and check `src/gen/dispatch_v2.c`: an excluded address must have only
   `NULL` variants.
2. **A target file of func roots is not an interpreter-only run.** For "is it our C?" use
   `SNESRECOMP_LLE_BOUNCE=0`; bounced vs interpreted must be bit-exact. Then bisect the compiled
   entries of `dispatch_v2.c` with `SNESRECOMP_LLE_INTERP_TARGET_FILE`.
3. **A picture is not proof the game runs.** Check that `$7E0B19` (field frames) advances, not
   only `$7E0100` (NMIs). A frozen game keeps showing its last frame.
4. **Save states**: the file snapshot lacks `g_cpu` and the beam driver's resume PC; our glue adds
   them (chunk `SEXC`). States from before v0.201.18 cannot be restored.
5. **Keep timing identical when narrowing down**: `dump` takes no frame, `wait`/`press` do.
6. **Real art can look like a glitch**: compare with the ROM's own frames
   (`soe://rom/assets/characters/<id>/animations/...`) before chasing it (the Boy's hit pose).
7. Pure framework suspicion (PPU, DMA, timing) needs the same script on snes9x
   (`tools/recomp/s9x-run.js`) before anything else.

## Finishing
Record every address learned (`game-knowledge` skill), add the bug with its lesson to
`docs/bugs-and-todos.md`, then the `release-ritual`.
