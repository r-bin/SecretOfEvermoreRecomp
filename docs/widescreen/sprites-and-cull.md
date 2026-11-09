# Sprites: cull windows, the OAM emitter, sprite limits

Tags `widescreen.oam`, `widescreen.cull`. Marks: [README](README.md).

## The draw path (V)

| Step | Address | Window and test |
|---|---|---|
| Screen position, depth key | `entity_sprite_depth` `$8F:C773` (bound C in the recomp) | `$02`/`$04` = world − `$59`/`$5B` (BG1 scroll in camera mode 2 for floor-0 sprites); depth key outside 9 bits → 0 |
| Box cull | `$80:9040` / `$80:904D` | sprites with ≥ 3 chunks carry a box (`+2` x s8, `+3` y s8, `+4` w, `+5` h). Signed tests against [0, 256) × [0, 224): fully inside → fast emitter, crossing an edge → clipping emitters, outside → skipped |
| Fast emitter | `$80:92EB` | chunk offsets added in 8 bit: cannot reach X ≥ 256 |
| 8×8 clipping emitter | `$80:90F8` | X ∈ [−7, 256), Y ∈ [−7, 224) (signed 16-bit `cmp #$FFF9` / `#$0100`); X8 = sign of X (`bit $17`) |
| 16×16 clipping emitter | `$80:91FD` | X ∈ [−15, 256), Y ∈ [−15, 224); X8 = sign |
| Budgets | DP `$D3`/`$D4` (OAM index, stops at 128), `$D8` (new-tile budget: 3 per 8×8, 7 per 16×16), LRU caches `$95A5..`/`$AA58..` | per frame |
| Finalise | `$80:8FEE` | blanks unused OAM (U) |

HUD sprites go through `direct_sprite_draw_oam` (`$80:9033`) at fixed coordinates
([hud-and-adaptive-ui](hud-and-adaptive-ui.md)).

## What that means for widescreen

* Left margin: the compares are signed, so only the window bounds need widening (P8's unsigned trap
  does not apply).
* Right margin: X8 is set only for negative X, and the clip stops at 255. A sprite at X ≥ 256 needs
  X8 = 1, and the PPU must be told it means +256..+511, not −256..−1: `PpuWsSetOamRightHints`
  (`snes/ppu.h:829`) each frame.
* Boxes that are fully inside a widened window must not take the 8-bit fast path.
* More sprites on screen run into OAM 128 and `$D8` sooner (flicker, missing chunks). The recomp PPU
  can lift the 32 sprites / 34 tiles per line (`kPpuRenderFlags_NoSpriteLimits`, `ppu.h:153`), not
  the game's own budgets.
* Every change here writes guest WRAM (OAM buffers, LRU caches, `$D8`) → experimental E1 only
  ([modes](modes.md)), via `hle_func` + bound C ([bindings](bindings.md)).

## Default: clip to the 256 columns

In the vanilla and hybrid modes no guest sprite code changes; sprites that the game draws partly
outside [0, 256) (chunks at X −15..−1) must still be clipped at column 0 renderer-side, or they
show in the left margin where vanilla showed nothing. Options: an OBJ clamp in the line renderer
(the framework has per-BG clamps, `PpuSetWidescreenLayerClamp`, `ppu.h:853`; an OBJ equivalent or
`SNESRECOMP_WS_OBJ_EDGE_CLIP` in `ppu.c` ~3070 needs checking), or overlay extraction of OBJ outside
the band (`PpuSetOverlayCapture`, `ppu.h:742`). Exception: HUD sprites moved by the UI setting.

## Targeting window (V)

`target_candidate_filter` (`$8C:8658`) accepts only X − `$59` ∈ [−8, 264), Y − `$5B` ∈ [−8, 248)
(unless `$05AD` bit 2). Visible margin enemies cannot be targeted; widening it is experiment E3.
