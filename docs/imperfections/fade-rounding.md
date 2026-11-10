# Fades Round Differently

**Status:** explained. Two brightness rules, snesrecomp's and MesenCE's; a compare run can switch the recomp to MesenCE's
(`--fade`), and then the picture of both committed replays is identical at every NMI. Play keeps snesrecomp's rule.

## What happens
Below full brightness (INIDISP `$2100` bits 0-3 under 15) a colour component `c` (5 bits) is scaled by the brightness `l`:

| | Rule | Colour 1 at brightness 10 |
|---|---|---|
| snesrecomp since `81d410f2` (2026-11-04, in our pin) | `round(c * l / 15)`, then widened to 8 bits: bsnes's light table | 1 → 8 |
| MesenCE 2.2.1 (`SnesPpu::ApplyBrightness`) | `c * l / 15`, truncated, then widened | 0 → 0 |

So during fades and in dimmed scenes some pixels are one 5-bit step (8 of 255) brighter here. Which rule the console follows is not
settled here: the two reference emulators disagree, and the hardware scales in its analog stage. It is not a recompilation error:
WRAM, VRAM, CGRAM and OAM, everything the PPU draws from, are identical at every NMI.

Before `81d410f2`, snesrecomp widened to 8 bits first and then scaled; this file described that until 2026-11-07, and the old `--fade`
rewrite modelled it. Against the new rounding that rewrite was wrong, which is why it made more keys differ, not fewer.

## Measured (2026-11-07)
`node tools/compare/full.mts`, every NMI of the committed replays:

| Picture | Replay 01 (8,592 NMIs) | Replay 02 (23,756 NMIs) |
|---|---|---|
| snesrecomp's rule | 289 differ | 948 differ |
| `--fade`: MesenCE's rule | **0** | **0** |

## How `--fade` works
`SOE_COMPARE_FADE` sets `g_ppu_brightness_truncate`, a switch in snesrecomp's `ppu_brightness_level` (the framework patch,
[../pr/05-brightness-rule.md](../pr/05-brightness-rule.md), upstream [#281](https://github.com/RetroPortingToolKit/snesrecomp/pull/281)), before the first frame. Every renderer path then scales as MesenCE does.
Nothing is rewritten after drawing, so a brightness change in the middle of a frame is drawn as it happens.

## Still open
A local replay from a save state (key 2992 of the 2026-11-05 replay 05) differed in rows 0-24 at the end of a fade to black: MesenCE
drew them at brightness 1, the recomp at 0. Measured with the old rewrite; not run again with the new switch
([../progress-internal.md](../progress-internal.md)).
