---
name: c-bindings
description: Use when integrating, adapting, or verifying routines bound to reconstructed C: what to check in the plugin repo, hle directives, src/soe_bindings.c glue, fast replay-01 verification, mutation controls, and updating progress-internal.md.
applyTo: "**"
---

# Skill: Reconstructed C Bindings Integration & Verification

Docs: `docs/game-rtl.md` §4b (architecture & proof rules), `docs/progress-internal.md` (metrics & Bound C table),
`docs/testing.md` (replays & controls), `docs/setup.md` (import & regen).

---

## 1. Architecture & Division of Responsibility

Routines run as reconstructed C instead of recompiled ROM code (`hle_func`, `docs/game-rtl.md` §4b).

- **Plugin Repo (`everscript-vscode` or `c-bindings`)**:
  - Authorship of clean C under `reconstructed-c/<bank>/`.
  - Reconstructed C quality standards (skill `c-style`): types, structs, enums, no magic numbers.
  - G1 parity verification against ROM disassembly (`node tools/c-to-rom/g1.mts <name>`).
  - `SOE_AT(pc)` marks for cycle costing.
  - Seed export via `npm run recomp`.
- **Recomp Repo (`SecretOfEvermoreRecomp`)**:
  - Imports C code and seed directives via `node tools/import.mts --from <checkout>`.
  - Configures `SOE_BOUND_C` in `CMakeLists.txt` and `hle` entries in `recomp/seed-exclusions.txt`.
  - Implements the machine adapter in `src/soe_bindings.c` (CPU context <-> `soe_regs`, deadline gating, mutation checks, and fallback paths).
  - Proves bit-for-bit behavioral and timing parity against MesenCE across replays.
  - Documents metrics and verification proofs in `docs/progress-internal.md`.

---

## 2. Pre-flight Checks (Plugin Repo C Code)

Before integrating a routine into `SecretOfEvermoreRecomp`, verify its C source in the plugin repo meets all binding constraints:

1. **G1 Parity Rating**: Must be verified equal to the ROM routine in the plugin repo (`npm test` / `g1.mts`).
2. **`SOE_AT(pc)` Basic Block Marks**:
   - `src/soe_cost.c` charges cycles by walking ROM instructions between `SOE_AT(pc)` marks.
   - Every branch target, basic block start, and fall-through target after a conditional branch MUST have a matching `SOE_AT(0xXXXX)` mark.
   - Missing marks result in missed cycle charges and subtle replay desyncs!
3. **No Unhandled Callee / Calls to ROM ASM**:
   - The recomp does not yet support arbitrary calls from C back into recompiled ASM (`soe_call_rts`/`soe_call_rtl`).
   - If a C routine calls subroutines, those subroutines must either be self-contained in C or already bound. Routines that call unbounded ROM ASM cannot run as pure C yet (e.g. `entity_sleep_wake`).
4. **Clean Hardware / Bus Boundaries**:
   - A bound C routine must only access WRAM (`$7E0000-$7FFFFF`) and CPU registers (`A, X, Y, S, flags`).
   - Direct accesses to MMIO/PPU registers (`$21xx`, `$42xx`) must use modeled wrappers (such as `soe_multiply()` in `src/soe_bindings.c`) or be disqualified from pure C binding.
5. **Macro Argument Side Effects Trap**:
   - Host macros (`ROM16()`, `ABS16()`, etc.) evaluate arguments multiple times. Never write `ROM16(table + compute_index(r))` — store to a local variable first!

---

## 3. Step-by-Step Binding Workflow in Recomp

### Step 1: Register in `CMakeLists.txt`
Add the relative path of the C file to `set(SOE_BOUND_C ...)`:
```cmake
set(SOE_BOUND_C
    ...
    8F/8FA963_elevation_vector_shear_step.c
)
```

### Step 2: Add Exclusion Directive in `recomp/seed-exclusions.txt`
Add an `hle` line:
```text
hle <pc24> <HleFunction> body=<pc24_second_insn> <routine_name>, reconstructed-c/<bank>/... (G1-rated); <reason>
```
*Crucial Rule on `body=`*:
- Always define `body=<second instruction PC>`.
- If the routine entry cannot fit before the current slice's deadline, the helper executes the first instruction manually and yields to the compiled code at `body=`.
- Without `body=`, fallback calls would have to be interpreted, dropping the game's native execution share!

### Step 3: Import into Recomp
Run the importer from the root of `SecretOfEvermoreRecomp`:
```sh
node tools/import.mts --from /path/to/everscript-vscode
```
This updates `src/bound-c/` and the seeds in `recomp/`.
*(Tip for rapid local iteration)*: You can configure CMake with `-DSOE_RECONSTRUCTED_C=/path/to/reconstructed-c` to compile against live edits without running `import.mts` every iteration, but run `import.mts` prior to final testing and committing.

### Step 4: Implement Glue in `src/soe_bindings.c`
1. **Declare Prototype** (under `#ifdef SOE_RECONSTRUCTED_C`):
   ```c
   void <routine_name>_bind(soe_regs r);
   ```
2. **Add Enum & Stats Entry**:
   ```c
   enum {
       ...,
       MY_ROUTINE,
       BINDINGS
   };
   static struct { const char *name; ... } s_bind[BINDINGS] = {
       ...,
       { "my_routine" },
   };
   ```
3. **Implement Helper Function**:
   ```c
   void HleMyRoutine(CpuState *cpu) {
   #ifdef SOE_RECONSTRUCTED_C
       stats_init();
       // Check if execution fits before slice deadline (estimate max cycles)
       if (fits(cpu, 250)) {
           s_bind[MY_ROUTINE].c++;
           // 1. Pack CpuState into soe_regs
           soe_regs r = {
               .a = cpu->a,
               .x = cpu->x,
               .y = cpu->y,
               .s = cpu->s,
               .d = cpu->d,
               .db = cpu->db,
               .flags = cpu->flags
           };
           // 2. Run bound C
           my_routine_bind(r);
           // 3. Mutate control check (verifies tests catch broken binding)
           if (s_bind[MY_ROUTINE].mutate) {
               r.a ^= 0x00FF; // or corrupt output register / WRAM result
           }
           // 4. Unpack soe_regs back into cpu
           cpu->a = r.a;
           cpu->x = r.x;
           cpu->y = r.y;
           cpu->flags = r.flags;
           // Charge cycles calculated via soe_cost
           soe_cost_charge(cpu);
           return;
       }
   #endif
       // Fallback: run 1st instruction & resume compiled execution at body
       s_bind[MY_ROUTINE].compiled++;
       run_rom_bytes(cpu, BODY_PC);
   }
   ```

### Step 5: Regenerate & Build
```sh
bash tools/regen.sh --rom <rom>
cmake --build build -j
```

---

## 4. Verification Discipline (Speed & Quality)

> [!IMPORTANT]
> **Iterate ONLY with Replay 01 (`01-no-inputs-1-loop.mmo`)!**
> Replay 01 runs in under 1 minute and exercises title sequence, intro demos, movement, raycasts, NMI, sorting, audio fade, and RNG.
> Never run long full-game replays or Replay 02 during iterative development. Only use another replay if the target routine is provably uncalled during Replay 01.

### 1. Activity Verification (`SOE_HLE_STATS=1`)
Confirm the C implementation is actually taking the C path:
```sh
SOE_HLE_STATS=1 build/SecretOfEvermoreSNESRecomp --no-launcher --script tools/compare/replays/01-no-inputs-1-loop.mmo <rom>
```
Look for the stats line on exit:
```text
[hle] my_routine: 14502 in C (12 across a refresh stop), 430 compiled
```
- **If `in C` == 0**: The binding never ran in C! Check whether:
  - The routine is actually hit in Replay 01.
  - The deadline check `fits(cpu, cost)` is too conservative or failing.
  - The routine was not listed in `CMakeLists.txt` or `recomp/seed-exclusions.txt`.

### 2. Lockstep Parity Verification (MesenCE vs Recomp)
Run the automated comparison against stored or live MesenCE runs:
```sh
ctest --test-dir build -R replay_01 --output-on-failure
```
Or run the full comparator:
```sh
node tools/compare/compare.mts mesen recomp tools/compare/replays/01-no-inputs-1-loop.mmo --rom <rom> --verbose
```
Must match 100% bit-for-bit at every single NMI checkpoint!

### 3. Mutation Control Verification (`SOE_HLE_MUTATE=<name>`)
A binding only counts as proven if intentionally corrupting its output causes a detected failure:
```sh
SOE_HLE_MUTATE=my_routine node tools/compare/compare.mts mesen recomp tools/compare/replays/01-no-inputs-1-loop.mmo --rom <rom> --verbose
```
- **Must desync** and report the exact checkpoint key where the failure was caught (e.g., `caught at key 363`).
- If mutating `r.a` does not cause a desync:
  - The register may be dead/overwritten immediately by the caller.
  - Mutate the specific output register or WRAM variable modified by the routine.
  - Record the key where it was caught.

### 4. Cycle Cost Debugging
If a desync occurs with the uncorrupted binding:
- Set `SOE_COST_TRACE=1` to print each basic block mark and instruction charge.
- Check if any branch in the C code lacks a corresponding `SOE_AT(pc)` mark in the source C file.

---

## 5. End-Testing & Documentation Before Commit

Before committing and merging the new C binding:

1. **Verify Replay 01 Passes**:
   ```sh
   ctest --test-dir build -R replay_01 --output-on-failure
   ```
2. **Update `docs/progress-internal.md`**:
   Add a row to the table in `## Bound C`:
   ```markdown
   | `$8F:A963` `elevation_vector_shear_step` | 12,450 / 320 | not measured | to 14424 | caught at key 363 |
   ```
   - Record exact counts from `SOE_HLE_STATS=1`.
   - Leave Replay 02 as `not measured` unless specifically needed.
   - Record the mutation control catch key.
3. **Update `docs/game-rtl.md`**:
   Add the routine to the table in §4b with its address, C file, and proof note.
4. **Git Staging Hygiene**:
   - Only stage and commit:
     - `CMakeLists.txt`
     - `recomp/seed-exclusions.txt`
     - `src/soe_bindings.c`
     - `src/bound-c/` (if imported)
     - `docs/progress-internal.md`
     - `docs/game-rtl.md`
   - Never commit `src/gen/`, ROM files, or `.mmo` files.

