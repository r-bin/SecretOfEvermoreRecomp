# C bindings needed

C lives in everscript-vscode `reconstructed-c/` (`SOE_BIND`, skills `code-vfs` and `c-style`); the
recomp replaces a routine with `hle_func <pc> <Helper>` in `recomp/bank*.cfg` (regenerates
`src/gen`, never hand-edited) and a helper in `src/soe_bindings.c` that runs the bound C when it
fits the slice deadline and the ROM bytes otherwise (`run_rom_bytes`, `src/soe_bindings.c:188`;
contract: `snesrecomp/docs/HLE_FUNC.md`). A widescreen hook adds one branch: switch off → exactly
today's path.

## Needed for hooks (experiments only)

| Routine | Address | Size | C now | For | Priority |
|---|---|---|---|---|---|
| `entity_sleep_wake` | `$8F:C40F` | 261 B | G1 (v0.399.16), not bound: calls asm; hooked (`HleEntitySleepWake`) for E6 | E2, E5, E6: window constants from a setting; E6 asks a state table | high (first experiment) |
| `oam_meta_sprite_assembler_continue` | `$80:904D` | 171 B | pseudocode (`80/809033_sprite_clip_cull.c`), not bound | E1: box window | medium |
| `sprite_slice_clip_and_emit` | `$80:90F8` | 166 B | none | E1: window + X8 | medium |
| `sprite_slice_16x16_clip_and_emit` | `$80:91FD` | 238 B | none | E1 | medium |
| `oam_sprite_composite_blit_and_clip` | `$80:92EB` | 328 B | none | E1: keep wide boxes off the 8-bit path | medium |
| `target_candidate_filter` | `$8C:8658` | 193 B | none | E3 | low |
| scheduler tick | `$8F:DC34` | 156 B | G1 (v0.399.17), not bound: calls asm | E5 (budget, lag rule); E6 does without it | low |
| `animation_script_play_direct_sfx` / panned | `$90:8921` / `$90:88BC` | 45 / 55 B | none | E2/E5 audio mitigation | low |

## Useful but not hooked

| Routine | Address | C now | Why |
|---|---|---|---|
| spawner | `$8F:DDBB` (221 B) | none | understanding only; never widened |
| `entity_sprite_depth` | `$8F:C773` | bound | ghosts copy its depth rule (read only) |
| camera | `$90:9E93`, `$90:9F21` | pseudocode; `$90:9FC9` bound | the camera stays vanilla |

## No binding needed (renderer-side or read-only)

Margin prefill and edges (reads `$7F:0000`), camera-limit clipping (reads `$23E9..$23EF`), parallax
clip (reads `$2417..$241D`, `$22FA`), HUD placement, fog, ghosts, fades, the menu/field gate (entry
hooks are observational), cutscene watch (reads `$28FC`), Mode 7 margins (framework). Collision
needs nothing: it is already whole-room ([enemies](enemies.md)).

## Bound C that calls asm

Every routine bound so far runs to its end without calling another routine. `entity_sleep_wake` calls
`$8F:C514`, `$8F:DF41`, `$8F:B31E`, `$8F:C554`, `$8F:B29B` and `$90:817B`; the tick calls the RNG, `$8F:DCD0`
and `entity_sleep_wake`. `soe_call_rts`/`soe_call_rtl` (everscript-vscode `soe.h`) have no implementation in
`src/soe_bindings.c`: a callee's length is not known before the C starts, so the one-step proof (§4b of
[game-rtl](../game-rtl.md)) does not hold. Two ways: run the C up to a call and yield into the compiled callee with
the continuation as its return (as the raycast hands over between steps), or, for E6 only, hook the entry and
continue in the ROM bytes at the branch the variant chooses (`$C450` RTS, `$C451` sleep, `$C4B1` wake), so the
switch-off path stays the ROM's. E6 took the second way (2026-11-09; its free time adds two more `hle` lines, `$8E:E45C` and `$8E:E4B0`, that only switch free time and leave the `JSL` to the interpreter); the first is a roadmap item ([roadmap](../roadmap.md) §7).
