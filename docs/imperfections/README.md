# Imperfections: Where the Recomp Differs from MesenCE

The recomp is compared with MesenCE, the reference ([../reference-emulators.md](../reference-emulators.md)),
through everscript-vscode's `tools/compare` (`--backend mesen,recomp`). Each file below is one known way the
recomp differs: what it is, how it was measured, why it stays, and what would fix it. The owner's rule:
**snesrecomp is not changed**; we work only in the files that adjust the game (glue `src/`, seeds `recomp/`,
tools). What snesrecomp would have to change is written down here as a candidate for upstream.

| Imperfection | Where | Effect | Status |
|---|---|---|---|
| [late-nmi.md](late-nmi.md) | snesrecomp frame driver | an NMI the game raises by enabling NMI inside vblank is never delivered: the NMI counter falls one behind, 6 times in the 4-minute attract replay | open (framework) |
| [compiled-cycle-charges.md](compiled-cycle-charges.md) | snesrecomp recompiler | compiled code is billed every CPU cycle at code-fetch speed; WRAM accesses cost 8 clocks on hardware, so FastROM code that works on WRAM runs ~4 % fast | open (framework) |
| [host-frames-and-fields.md](host-frames-and-fields.md) | snesrecomp frame driver | a DMA longer than a field packs several fields into one host frame, so replay inputs landed a field late | worked around in the glue for compare runs |

Per replay, identical frames and what would fix them: [../replay-sync.md](../replay-sync.md).

## Measured (2026-10-30, `01-no-inputs-4m.evsmv`, native build)

WRAM bytes equal, recomp at key *k* against MesenCE at the best key within *k*−3…*k*+8 (timing-noise bytes,
the counter and the stack page left out):

| key (NMIs since power-on) | 300 | 1000 | 2000 | 3000 | 4500 | 5800 |
|---|---|---|---|---|---|---|
| equal | 100 % | 99.95 % | 99.83 % | 99.11 % | 98.35 % | 98.83 % |

`02-newgame-boy-available.evsmv` (New Game, the prologue, until the Boy can be moved; both sides start with
empty SRAM), same method:

| key | 1000 | 4000 | 8000 | 12000 | 14500 |
|---|---|---|---|---|---|
| equal | 97.38 % | 99.37 % | 99.08 % | 98.93 % | 98.78 % |

- Identical through key 300 (the Square logo, the boot loads).
- From the first scene load on, the RNG (`$0133`–`$013B`) draws the same values as MesenCE, but up to two
  frames earlier or later; whatever the RNG drives (the demo fights) then parts. That is why the whole-WRAM
  hash differs at most checkpoints while the bytes stay ~99 % equal.
- An all-interpreter run gives the same numbers within a few hundred bytes, so the compiled code is not the
  main cause: missed NMIs are ([late-nmi.md](late-nmi.md)). The remainder after them is not yet attributed.

## Open risk (not measured as a difference)
The seed export flags 208 pushed-address returns (`# pushed-address return ...` in `recomp/bank*.cfg`). A
compiled routine that such a return lands in breaks, as the script scheduler `$8C:CFFD` did
([../bugs-and-todos.md](../bugs-and-todos.md) §2); only that one is excluded so far, `$8F:9817` is suspected.

## How to measure
```sh
# in everscript-vscode
node tools/compare/compare.mts "Secret of Evermore (U) [!].smc" --backend mesen,recomp \
  --replay tools/compare/replays/01-no-inputs-4m.evsmv --frames 3000 --keep-going --regions wram
```
The key is the number of NMIs since power-on (the glue's and the Lua script's running count of `$7E0100`,
which the attract demo zeroes when it loops). `SOE_COMPARE_PEEK` (see `src/game_rtl.c`) follows chosen
bytes frame by frame with the master clock, which is how each file here was measured.
