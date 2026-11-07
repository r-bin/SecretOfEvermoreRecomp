# Replays

The recordings the recomp is compared with MesenCE on ([docs/testing.md](../../../docs/testing.md)). The committed ones
are MesenCE 2.2.1 movies recorded from power-on with RAM zeroed: the input and MesenCE's settings, nothing from the ROM.

| Replay | NMIs | What it plays |
|---|---|---|
| `01-no-inputs-1-loop.mmo` | 8,592 | Power-on with no button pressed: logo, intro, title, one loop of the attract demo. Every change must pass it |
| `02-new-game-to-flowers.mmo` | 23,756 | New game with the default names, the 1965 and 1995 prologue, the crash landing, Prehistoria to the flowers |

```sh
node tools/compare/compare.mts mesen recomp <rom> --replay tools/compare/replays/01-no-inputs-1-loop.mmo --regions wram,vram,cgram,oam --verbose
```

## Adding one

Record a movie in MesenCE 2.2.1 that starts from power-on, with the SNES RAM power-on state set to all zeros (the
movie's `GameSettings.txt` then says `snes.ramPowerOnState AllZeros`). Commit it only when `unzip -l` lists `Input.txt` and `GameSettings.txt` alone: a movie with a
`SaveState.mss` or battery RAM holds memory from your ROM. `compare_units` refuses such a movie here.

Anything else (movies from a save state, Everscript `.evsmv` recordings that carry save data, long runs that do not sync)
goes in `local/`, which git ignores; `full.mts` finds it there. Both backends play `.mmo` and `.evsmv`; a movie that
presses reset, or whose state is not at a field's line 225, is refused (`../movie.mts`).
