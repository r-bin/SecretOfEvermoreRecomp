# Secret of Evermore Recomp — Agent Rules

These rules apply to every AI agent working in this repository.

## Standing rules

1. **Legal first.** No ROM or ROM bytes, no generated C (`src/gen/`), no dumps, screenshots or extracted assets in git.
   A committed replay holds input and settings only (`compare_units` checks it). Everyone regenerates from their own ROM.
2. **Never edit the framework by hand.** `snesrecomp/` and `recomp-ui/` are pinned public submodules (`framework_pins.txt`).
   Our framework changes are one file, `framework/snesrecomp.patch`, applied by `tools/regen.sh`; each goes upstream as a
   PR ([docs/pr/README.md](docs/pr/README.md)). The hardware timing rules are the framework's (PR 6); our glue adds the compare and host features through its hooks.
3. **This repository stands alone.** Build and test read nothing outside it and its submodules. Seeds, the
   data-region block in `recomp/bank00.cfg` and `src/bound-c/` come from everscript-vscode only through
   `tools/import.mts --from <checkout>`: change them there (or through `recomp/seed-exclusions.txt`), never by hand.
4. **MesenCE 2.2.1 is the reference.** "Correct" means identical to MesenCE at every NMI from power-on, every byte, nothing
   copied in or masked ([docs/testing.md](docs/testing.md)). snes9x is a probe and never decides; ares settles disputes by
   hand ([docs/reference-emulators.md](docs/reference-emulators.md)).
5. **Replay 01 is mandatory.** Every change passes `ctest` (both replays against the stored MesenCE runs) and, before a
   commit that changes timing, replay 01 against MesenCE itself. A change that alters what both do records the
   references again (testing.md).
6. **Write down what you learn.** A bug gets its root cause, fix and lesson in `docs/bugs-and-todos.md`, a difference from
   MesenCE a file in `docs/imperfections/`, in the same commit. Game facts (addresses, routines, RAM) belong in
   everscript-vscode's knowledge entries.
7. **A picture is not a running game.** Verify headless ([docs/headless-exploration.md](docs/headless-exploration.md)):
   the room id, `$7E0B19` advancing, a picture that matches the game.
8. **Branches and the shared checkout.** Work on `develop`; `main` holds releases. Several agents may share this checkout:
   stage only your own files by path, never `git add -A` or `git commit -a`.

## Where things are

| Where | What |
|---|---|
| `src/game_rtl.c` | our glue: config hook, save states, the compare harness (`SOE_COMPARE_*`) |
| `src/soe_frame_driver.c` | runs snesrecomp's beam frame driver with its MesenCE rules (PR 6, `docs/pr/06-frame-driver.md`) and our hooks: the compare, free time, the deadlines for C bindings |
| `src/soe_ppu_fixes.c` | PPU and register behaviour snesrecomp lacks, applied through its hooks, for DMA (open bus, writes during rendering) |
| `src/soe_bindings.c`, `src/bound-c/` | routines run as reconstructed C (`hle` lines in `recomp/seed-exclusions.txt`; proof rules in `docs/game-rtl.md` §4b) |
| `src/soe_mesen_state.c` | a MesenCE save state as a run's start (`SOE_COMPARE_MESEN_STATE`) |
| `recomp/` | seeds (`bank*.cfg`), `seed-exclusions.txt`, `symbols.toml` |
| `mods/` | `preloaded/packages/` (the mods the build ships), `mesen/` (their MesenCE Lua twins); docs/mods/README.md |
| `tests/` | ctest scripts and `recorded/` (stored MesenCE runs, CRCs only) |
| `tools/` | `regen.sh` (ROM to C), `compare/` (MesenCE against the recomp, replays, probes), `mods/` (pack, parity input), `import.mts`, `bisect.js` |
| `docs/` | [docs/README.md](docs/README.md): guides, the PR tracker, research notes |

## Skills

`.agents/skills/<name>/SKILL.md` (Claude Code reads them through `.claude/skills`):
`recomp` (build, test, ownership, seeds), `recomp-debugging` (reproduce, compare, classify, fix),
`c-bindings` (import, glue adaptation, cost marks, mutation control and replay-01 parity),
`mods` (a mod's package and plugin, its MesenCE Lua twin, the parity check, docs/mods),
`reference-emulators` (MesenCE decides, ares by hand, snes9x only probes).

Personas (`.agents/agents/`, `.claude/agents` for Claude Code): `c-bindings`.
