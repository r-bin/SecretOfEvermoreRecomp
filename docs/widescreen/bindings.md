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
| `entity_sleep_wake` | `$8F:C40F` | 261 B | none | E2, E5: window constants from a setting | high (first experiment) |
| `oam_meta_sprite_assembler_continue` | `$80:904D` | 171 B | pseudocode (`80/809033_sprite_clip_cull.c`), not bound | E1: box window | medium |
| `sprite_slice_clip_and_emit` | `$80:90F8` | 166 B | none | E1: window + X8 | medium |
| `sprite_slice_16x16_clip_and_emit` | `$80:91FD` | 238 B | none | E1 | medium |
| `oam_sprite_composite_blit_and_clip` | `$80:92EB` | 328 B | none | E1: keep wide boxes off the 8-bit path | medium |
| `target_candidate_filter` | `$8C:8658` | 193 B | none | E3 | low |
| scheduler tick | `$8F:DC34` | 156 B | none | E5 (budget, lag rule) | low |
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
