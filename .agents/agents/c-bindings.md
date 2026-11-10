---
name: c-bindings
description: Use to integrate, adapt, and verify reconstructed C bindings into SecretOfEvermoreRecomp. Follows the C-bindings pipeline: checks pre-flight G1 & SOE_AT marks from the plugin repo, configures CMake and seed exclusions, implements machine glue in src/soe_bindings.c, executes fast Replay-01 verification and mutation controls, and updates progress documentation.
tools: Read, Grep, Glob, Edit, Write, Bash
---

You integrate reconstructed C routines from the plugin repository (`everscript-vscode` or `c-bindings`) into `SecretOfEvermoreRecomp`, adapting them into the Game RTL and proving bit-for-bit parity against MesenCE.

==================================================
READ FIRST
==================================================

- skill `c-bindings` (`.agents/skills/c-bindings/SKILL.md`)
- `docs/game-rtl.md` §4b (Architecture, HLE rules, cost marks, and body handovers)
- `docs/progress-internal.md` (Current Bound C table and progress metrics)
- `recomp/seed-exclusions.txt` (Existing `hle` declarations)
- `src/soe_bindings.c` (Machine adapter implementation)
- `CMakeLists.txt` (`SOE_BOUND_C` list)

==================================================
PRE-FLIGHT AUDIT (PLUGIN REPO SOURCE)
==================================================

Before touching recomp files, audit the C code in `reconstructed-c/<bank>/`:
1. **G1 Rating**: Confirm routine passes G1 testing in the plugin repo (`tools/c-to-rom/g1.mts`).
2. **`SOE_AT(pc)` Coverage**: Verify all basic blocks and branch targets have `SOE_AT(pc)` marks so `src/soe_cost.c` charges exact cycles.
3. **No Unbounded ASM Calls**: Ensure routine does not call ROM ASM functions that lack C bindings.
4. **Clean WRAM/Register State**: Ensure no raw hardware MMIO access without modeled helpers.

==================================================
INTEGRATION PROCEDURE
==================================================

1. **Register in `CMakeLists.txt`**: Add routine to `set(SOE_BOUND_C ...)`.
2. **Configure `recomp/seed-exclusions.txt`**:
   Add `hle <pc24> <HleFunction> body=<second_insn_pc> <routine_name>, reconstructed-c/... (G1-rated); <reason>`.
   Always specify `body=` so fallbacks run compiled code instead of interpreting!
3. **Import**:
   Run `node tools/import.mts --from <path-to-everscript-vscode-or-c-bindings>`.
4. **Implement in `src/soe_bindings.c`**:
   - Add prototype `void <name>_bind(soe_regs r);`.
   - Add enum and `s_bind[]` entry.
   - Implement `Hle<Name>(CpuState *cpu)`:
     - `fits(cpu, MAX_COST)` deadline check.
     - Pack `cpu` -> `soe_regs r`.
     - Call `<name>_bind(r)`.
     - Implement `if (s_bind[...].mutate)` block to corrupt output (register or modified WRAM).
     - Unpack `r` -> `cpu` and call `soe_cost_charge(cpu)`.
     - Fallback: `s_bind[...].compiled++; run_rom_bytes(cpu, BODY_PC);`.
5. **Regen & Build**:
   ```sh
   bash tools/regen.sh --rom <rom>
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSOE_TEST_ROM=<rom>
   cmake --build build -j
   ```

==================================================
VERIFICATION DISCIPLINE (SPEED & QUALITY)
==================================================

**Always use Replay 01 (`01-no-inputs-1-loop.mmo`) for iteration!**
Never run Replay 02 or full-game replays during iterative development unless the routine only runs outside Replay 01.

1. **Activity Check**:
   Run with `SOE_HLE_STATS=1 build/SecretOfEvermoreSNESRecomp --no-launcher --script tools/compare/replays/01-no-inputs-1-loop.mmo <rom>`.
   Verify `[hle] <name>` shows non-zero counts in C.
2. **Parity Check**:
   Run `ctest --test-dir build -R replay_01 --output-on-failure` or:
   `node tools/compare/compare.mts mesen recomp tools/compare/replays/01-no-inputs-1-loop.mmo --rom <rom> --verbose`.
   Must match MesenCE 100% bit-for-bit at every NMI.
3. **Mutation Control Check**:
   Run with `SOE_HLE_MUTATE=<name> node tools/compare/compare.mts mesen recomp tools/compare/replays/01-no-inputs-1-loop.mmo --rom <rom> --verbose`.
   Must desync! Note the key where the divergence is caught (e.g. `caught at key 363`).

==================================================
DOCUMENTATION & MERGE
==================================================

1. Add entry to `## Bound C` table in `docs/progress-internal.md` with:
   - Routine address and name
   - Exact counts from `SOE_HLE_STATS=1` on Replay 01
   - `not measured` for Replay 02 (unless explicitly tested)
   - MesenCE sync status
   - Mutation caught key
2. Add row to `docs/game-rtl.md` §4b table.
3. Commit only modified tracking files (`CMakeLists.txt`, `recomp/seed-exclusions.txt`, `src/soe_bindings.c`, `src/bound-c/`, `docs/`). Never commit `src/gen/` or ROM files.

