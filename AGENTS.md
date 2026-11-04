# Secret of Evermore Recomp — Agent Rules

These rules apply to every AI agent working in this repository.

## Standing rules

1. **Legal first.** No ROM or ROM bytes, no generated C (`src/gen/`), no dumps, screenshots or
   extracted assets in git. Everyone regenerates from their own ROM.
2. **Never edit the framework.** `snesrecomp/` and `recomp-ui/` are pinned submodules
   (`framework_pins.txt`), not patched either: everything this port needs is in our glue (our own
   frame driver `src/soe_frame_driver.c` replaces the framework's), and what upstream should change
   is written up in `docs/imperfections/` and `docs/bugs-and-todos.md` §3.
3. **Seeds come from everscript-vscode.** `recomp/bank8x..9x.cfg` and the data-region block in
   `recomp/bank00.cfg` are written by its `npm run recomp` (sibling checkout). Change them through
   `recomp/seed-exclusions.txt` and a new export, never by hand.
4. **Write down what you learn.** Every bug gets its root cause, fix and lesson in
   `docs/bugs-and-todos.md` in the same commit. Game facts (addresses, routines, RAM) belong in
   everscript-vscode's knowledge entries, not here.
5. **MesenCE is the reference.** WRAM comparisons are against MesenCE; snes9x is a probe and is never
   treated as accurate; ares settles disputes by hand (`docs/reference-emulators.md`).
6. **A picture is not a running game.** Verify headless (`docs/headless-exploration.md`): the room
   id, `$7E0B19` advancing, a screenshot that matches the game.
7. **Branches.** Work on `develop`; `main` holds releases. Several agents may share this checkout:
   stage only your own files, never `git add -A`.

## Where things are

| Where | What |
|---|---|
| `src/game_rtl.c` | our glue: frame driver, renderer hook, save states, compare hashes |
| `src/soe_bindings.c` | routines run as everscript-vscode's reconstructed C (`hle` lines in `recomp/seed-exclusions.txt`; proof rules in `docs/game-rtl.md` §4b) |
| `src/soe_mesen_state.c` | a MesenCE save state as a run's start (`SOE_COMPARE_MESEN_STATE`; `docs/imperfections/mesen-state.md`) |
| `src/main.c` | the scaffold's host descriptor, plus our `after_config` line |
| `tests/` | ROM-backed `ctest` tests (`-DSOE_TEST_ROM=...`), each pinning a fixed bug |
| `recomp/` | seeds (`bank*.cfg`), `seed-exclusions.txt`, `symbols.toml` |
| `tools/` | `regen.sh` (ROM -> C), `bisect.js`, `verify-intro.js`, `check-obj-tiles.js` |
| `docs/` | `README.md` (status, map), setup, glue, debugging, headless exploration, bugs and roadmap, research |

## Skills

`.agents/skills/<name>/SKILL.md` (Claude Code reads them through `.claude/skills`):
`recomp` (build, ownership, seeds), `recomp-debugging` (reproduce, compare, classify, fix),
`reference-emulators` (MesenCE decides, ares by hand, snes9x only probes).
