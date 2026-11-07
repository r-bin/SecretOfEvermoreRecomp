# Compiled Code Is Billed at Code-Fetch Speed

**Status:** fixed with PR 1 (2026-11-03) with PR 3:
generated with `SNESRECOMP_EXACT_BUS=1` (our `tools/regen.sh` sets it), compiled code keeps the interpreter's timing
instruction for instruction; native runs of both replays are identical to MesenCE at every NMI key
([../progress.md](../progress.md)). Open on develop and upstream.

## What hardware does
Each bus access costs master clocks by region: 6 for FastROM (`$80`–`$FF:8000`+ with `$420D` = 1), 8 for
WRAM (direct page, stack, `$7E`/`$7F`), 12 for the joypad I/O block; an internal cycle costs 6. Evermore is
FastROM (header `$FFD5` = `$31`, the reset code writes `$420D = 1`).

## What the recomp did
- **Interpreter:** charges each bus transfer at its region's speed (`cpu_state.c`). Exact: all 331
  decompressor instructions (`$8C:9880–9BFF`) cost the same master clocks as on MesenCE, refresh stalls aside
  (MesenCE exec callbacks against `SNESRECOMP_CYC_WATCH`, 300,000 instructions each).
- **Compiled blocks:** the recompiler folded a block's CPU cycles into one constant and emitted
  `cpu->master_cycles += <cycles> * (FastROM ? 6 : 8)` (`recompiler/v2/emit_function.py`, `snes_cycles.py`).
  Every cycle was billed at code speed, so each WRAM access in FastROM code was 2 clocks short; the whole block
  was charged before its first access, and the deadline was checked only at block starts.

## Evidence
- Example, `$8C:9ADF` (the decompressor's bit reader, `lda [$2A],y` and shifts): the generated body charged
  `19 * 6` clocks; the pointer and data reads go to WRAM at 8.
- Native against the same build all-interpreted (`tools/compare/probes/native-vs-interp.mts`):
  NMI 1 already 14 clocks apart (1,738,956 against 1,738,942).

## The fix (snesrecomp `aot-bus-exact`, `recompiler/v2/bus_exact.py`)
Found one step at a time with `native-vs-interp.mts` (the first NMI apart) and `clock-trace.mts` (both runs'
instructions with their start clocks in that window: the interpreter's `[cyc] … start=`, compiled code's `[acyc]`):
1. **Charges.** Every instruction is its own block and costs `6 × cycles + Σ (region speed − 6)` over its
   transfers: fetches at code speed; pointer, stack and data bytes at the speed of the address they reach (D, S and
   the effective address at run time); D.l, index page-cross (also `(dp),Y`) and MVN/MVP per byte as the interpreter
   counts them. Native then matched through NMI 431.
2. **I/O at its own clock.** An instruction with a memory operand charges after its effect; before it,
   `g_aot_access_prefix` holds the clocks up to its data access, so an I/O register gets the interpreter's lead.
3. **The host's per-instruction hook.** Compiled instructions call `g_interp_bridge_pc_hook` too: our driver's NMI
   recognition judges the last instruction before V=225+6, and after a compiled one it judged an older interpreted
   one (NMI 432, one instruction late). Native then matched through NMI 2298 (01) / 4041 (02).
4. **Nested interpreter runs stop at the deadline.** An interpreted routine called from compiled code ran to its
   return past the slice's deadline (NMI 2299: 394 clocks late, `$8F:9F20`). It now leaves through the existing
   deadline unwind (`SNESRECOMP_NESTED_DEADLINE=0` turns it off). Native then equalled interpreted to each replay's
   end.

Without the switch the emitter, the interpreter and old generated code behave as before (native still 14 clocks off
at NMI 1). Speed: 6,000 frames headless in 23.4 s before, 23.5 s with it.

## Earlier, rejected: a seed exclusion
Keeping the decompressor's 336 entries in the interpreter (`func` lines in `recomp/seed-exclusions.txt`) traded load
speed for timing and did not improve WRAM agreement with MesenCE (99.95 / 99.72 / 98.95 % at keys 1000 / 2000 /
3000, against 99.95 / 99.83 / 99.11 % native).
