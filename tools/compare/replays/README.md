# Curated Replays for Integration Testing & Differential Comparisons

This folder contains the curated suite of `.evsmv` and MesenCE `.mmo` replay recordings used for integration testing and differential verification across execution backends (`snesrecomp`, `mesence`, `snes9x-wasm`, and `ares`).

## The Replay Suite

| Replay | Frames | Duration | Coverage / Description |
|---|---|---|---|
| `01-no-inputs-4m.evsmv` | 14,424 | ~4m 00s | **Attract loop & demo cycle:** Power-on with no inputs through the Square logo, prologue intro, title screen, and full demo cycles across all four eras (Prehistoria, Antiqua, Gothica, Omnitopia) until looping. Validates cold boot, decompression, and background tile loaders. |
| `02-newgame-boy-available.evsmv` | 17,948 | ~4m 58s | **Title screen $\to$ Name entry $\to$ Opening cutscenes $\to$ Free play:** Starts a New Game, navigates character naming prompts with default names, advances through the Carlton 1965 prologue, Podunk 1995 streets, mansion machine explosion, crash-landing in Prehistoria, and waits until the Boy entity is fully active and player control is unlocked. |
| `03-complete-run-7h.evsmv` | 1,559,442 | ~7h 12m 28s | **Full game playthrough:** Complete start-to-finish game run traversing all acts, bosses, dialogue sequences, alchemy formulae, map transitions, and end credits. Used for exhaustive stress testing and regression verification. |
| `04-newgame-flowers.mmo` | 25,923 | ~7m 11s | **Played by hand from power-on, recorded in MesenCE:** many short presses. The movie's save state sits at the reset vector and carries leftover WRAM and the SRAM of a save: MesenCE loads the state, the recomp its WRAM, SRAM, FastROM, SPC700 and S-DSP before the first instruction, then the RNG at the usual copy. `SOE_MMO_FRESH=1` powers both backends on with the movie's WRAM and SRAM instead: they run in lockstep, but that is not the recording. |
| `05-flowers-from-state.mmo` | 8,433 | ~2m 20s | **04 without the intro:** from a MesenCE save state mid-game. The recomp takes the state over whole, so nothing is copied and every key counts. |
| `06-flowers-raptors.mmo` | 4,682 | ~1m 18s | **Flowers and raptors screens:** from a MesenCE save state mid-game, taken over whole like 05. |

Like `.evsmv`, `.mmo` files stay out of git: the save state inside holds memory from the user's ROM. Only `mesen` and `recomp` play `.mmo`; a movie that presses reset, or whose state is not at a field's line 225, is refused (`src/platform/tas/mesen-movie.ts`).

## Running Comparisons

Use `tools/compare/compare.mts` to execute lockstep differential hashing against any replay:

```bash
# 1. Quick attract loop check (4 minutes)
node dist/tools/compare/compare.mts \
  --rom "Secret of Evermore (U) [!].smc" \
  --replay tools/compare/replays/01-no-inputs-4m.evsmv \
  --backend mesen recomp

# 2. Opening cutscene through Boy control
node dist/tools/compare/compare.mts \
  --rom "Secret of Evermore (U) [!].smc" \
  --replay tools/compare/replays/02-newgame-boy-available.evsmv \
  --backend mesen recomp

# 3. Targeted frame-window comparison of the complete run
node dist/tools/compare/compare.mts \
  --rom "Secret of Evermore (U) [!].smc" \
  --replay tools/compare/replays/03-complete-run-7h.evsmv \
  --frames 50000 \
  --backend mesen recomp
```

