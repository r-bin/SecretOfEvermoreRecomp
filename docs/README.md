# Documentation

Start with the repository [README](../README.md). Each guide below is an instruction for one job; the research notes at the
end are background, dated and not kept current.

## Using the recomp

| Guide | For |
|---|---|
| [setup.md](setup.md) | Toolchain, build, new seeds from everscript-vscode, troubleshooting |
| [testing.md](testing.md) | The tests, the replays, reproducing every result against MesenCE |
| [progress.md](progress.md) | How far each replay matches MesenCE, per memory region; how much runs compiled and as C |

## Working on the recomp

| Guide | For |
|---|---|
| [game-rtl.md](game-rtl.md) | Our glue: the frame driver, the config hook, names in the generated C, routines bound to C |
| [framework.md](framework.md) | The snesrecomp submodule and our patch: how to move to a new upstream state |
| [pr/README.md](pr/README.md) | Each framework change as an upstream pull request: tracker, texts, what each needs, how to reproduce it |
| [debugging.md](debugging.md) | Narrowing a bug: execution modes, dumps, scripts, seed bisection |
| [headless-exploration.md](headless-exploration.md) | Driving the game from a script: into a save, moving, fighting, finding the frame of a glitch |
| [reference-emulators.md](reference-emulators.md) | Which emulator decides what: MesenCE the reference, ares by hand, snes9x only a probe |
| [imperfections/README.md](imperfections/README.md) | Every known way the recomp differed from MesenCE, how it was measured and fixed |
| [bugs-and-todos.md](bugs-and-todos.md) | Open and solved bugs, each with its root cause and lesson |
| [roadmap.md](roadmap.md) | The open work, in working order |
| [cdl-and-seeds.md](cdl-and-seeds.md) | How good the recording behind the seeds is, and why only the seeds are committed |
| [progress-internal.md](progress-internal.md) | The local replays (save states, battery RAM: not in git) and the measurements behind progress.md: native share, speed, banks, bound C |

## Pipeline

```text
 everscript-vscode (npm run recomp)              this repository
 ┌──────────────────────────────────────┐        ┌──────────────────────────────────────┐
 │ ROM + recorded CDL + names           │ import │ recomp/bank*.cfg      seeds           │
 │ -> seeds (roots, widths, dispatch,   │ ─────▶ │ src/bound-c/          reconstructed C │
 │    data regions), reconstructed C    │        │ tools/regen.sh        ROM -> src/gen/ │
 │                                      │ ◀───── │ recomp/seed-exclusions.txt            │
 └──────────────────────────────────────┘        │ CMake                 -> build/       │
                                                 └──────────────────────────────────────┘
```

Only the import (`tools/import.mts --from <checkout>`) reads another checkout; build and test never do.

## Research notes

[research/README.md](research/README.md): the upstream timing proposal, the parity work's history and lessons, comparisons
with other ports, and ideas for mods, widescreen and rendering.
