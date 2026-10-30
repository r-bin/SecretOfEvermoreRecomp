# Replay Sync Against MesenCE

How much of each replay the recomp (native build, `develop` 2026-10-30) plays exactly as MesenCE does.
Replays: everscript-vscode `tools/compare/replays/`. Measured with

```sh
node tools/compare/compare.mts "Secret of Evermore (U) [!].smc" --backend mesen,recomp \
  --replay tools/compare/replays/<replay>.evsmv --keep-going
```

**Identical frames** = checkpoints whose WRAM + VRAM hashes are equal, one checkpoint every 60 keys (a key is
an NMI since power-on); the timing-noise bytes of `tools/compare/game.mts` are left out, and nothing before
key 115 is compared (the RNG is copied in there). Anything under 100 % is a failure.

| Replay | Identical frames | Comments |
|---|---|---|
| `01-no-inputs-4m.evsmv` (14,424 frames, attract loop) | **1.8 %** (4 of 227) | Identical through key 300, first difference by key 360 (the first scene load). After the drift, WRAM stays **98.3–99.95 %** equal (keys 1000–5800, best match within a few keys): same RNG values, drawn 1–2 frames off. Main cause: [late NMI](imperfections/late-nmi.md) (6 in this replay); smaller: [compiled cycle charges](imperfections/compiled-cycle-charges.md). |
| `02-newgame-boy-available.evsmv` (17,948 frames, New Game to free play) | **1.3 %** (4 of 299) | Same first difference (key 300–360, before any input matters). After the drift **97.4–99.4 %** WRAM equal (keys 1000–14500). Both sides start with empty SRAM; the replay's SRAM header is not loaded by either backend. Same causes as 01. |
| `03-complete-run-7h.evsmv` (1,559,442 frames) | not measured | Excluded by the owner; at MesenCE's ~150 keys per second a full run takes ~3 h. |

## Can it reach 100 %?

| Cause | Fixable? | Work |
|---|---|---|
| [Late NMI](imperfections/late-nmi.md): enabling NMI inside vblank fires no NMI | Yes, but only in snesrecomp, which we do not change. Not possible in the game's own files. | Small upstream: ~30 lines in `snes.c` and the beam frame driver. A trial patch (reverted) made the counters agree at equal master clock and an all-interpreter run identical to MesenCE through key 120; no longer run was tested with it. |
| [Compiled cycle charges](imperfections/compiled-cycle-charges.md): every compiled cycle billed at ROM speed | Yes, in snesrecomp's recompiler. Keeping hot code in the interpreter (seed exclusions) is possible here but did not improve the match. | Large upstream: a per-access cost model in `snes_cycles.py` / `emit_function.py`, then regenerate all compiled code. |
| [Host frames vs fields](imperfections/host-frames-and-fields.md) | Done for compare runs (glue). | — |
| The rest | Unknown: what an all-interpreter run still differs in after the missed NMIs is not attributed yet. | Investigation. |

With neither framework fix allowed, this table stays at ~1–2 % identical frames and ~99 % identical WRAM.
Compare at game events ([reference-emulators.md](reference-emulators.md) rule 4) to check that the recomp
plays the same game, not that it is cycle-identical.
