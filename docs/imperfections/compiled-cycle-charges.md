# Compiled Code Is Billed at Code-Fetch Speed

**Status:** open, in snesrecomp. Not fixed here (owner: no framework changes).

## What hardware does
Each bus access costs master clocks by region: 6 for FastROM (`$80`–`$FF:8000`+ with `$420D` = 1), 8 for
WRAM (direct page, stack, `$7E`/`$7F`), 12 for the joypad I/O block; an internal cycle costs 6. Evermore is
FastROM (header `$FFD5` = `$31`, the reset code writes `$420D = 1`).

## What the recomp does
- **Interpreter:** charges each bus transfer at its region's speed (`cpu_state.c`). Matches MesenCE: per
  instruction in the decompressor loop the costs agree within DRAM refresh.
- **Compiled blocks:** the recompiler folds a block's CPU cycles into one constant and emits
  `cpu->master_cycles += <cycles> * (FastROM ? 6 : 8)` (`recompiler/v2/emit_function.py`, `snes_cycles.py`).
  Every cycle is billed at code speed, so each WRAM access in FastROM code is 2 clocks short. `snes_cycles.py`
  calls its combiner a "first-cut model" and names the bus-exact charge as a later refinement.

## Evidence
- Example, `$8C:9ADF` (the decompressor's bit reader, `lda [$2A],y` and shifts): the generated body charges
  `19 * 6` clocks; the pointer and data reads go to WRAM at 8.
- Native build against the interpreter on the same input: the boot decompression (keys 4–35) finishes one
  frame earlier native. Bisecting the 4,840 compiled entries through `SNESRECOMP_LLE_INTERP_TARGET_FILE`
  found no single routine: the shortfall adds up over the decompressor's helpers `$8C:9A76`–`$8C:9BDD`.

## Effect
FastROM code that works on WRAM runs a few percent fast. Loads finish up to a frame early; RNG draws after a
load shift. Measured on the attract replay, an all-interpreter run is barely closer to MesenCE than the
native one, so this is the smaller of the open causes ([README.md](README.md)).

## Why no seed exclusion
Keeping the decompressor's 336 entries in the interpreter (`func` lines in `recomp/seed-exclusions.txt`)
would trade load speed for timing. Tried at run time: WRAM agreement with MesenCE did not improve (99.95 /
99.72 / 98.95 % at keys 1000 / 2000 / 3000, against 99.95 / 99.83 / 99.11 % native). Not applied.

## Fix (upstream candidate)
Fold each instruction into code-fetch bytes (code speed), internal cycles (6) and data accesses (region of the
addressing mode: direct page and stack are WRAM; long operands are known; absolute depends on DB, charge at
run time or assume WRAM below `$2000`), as `instr_master_cycles` already sketches.
