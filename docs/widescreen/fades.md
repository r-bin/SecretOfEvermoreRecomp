# Fades to and from black and white

Tag `widescreen.fade`. Marks: [README](README.md).

## Brightness fades (V)

* `$7E:0106` (byte) is the **INIDISP shadow**: the NMI (`$80:823E`) loads it and stores it to
  `$2100` first. The knowledge entry is still named `JOYPAD1_TRIGGERED`; the pad is `$0EC6`/`$0104`.
* `$0B83` is the fade target (bit 15 = running, low byte = brightness); `$80:DE96` steps `$0106`
  by one per frame toward it and clears `$0B83` at the target. The entry is still named
  `AUDIO_TARGET_VOLUME`.
* Callers: script opcodes `0x26`/`0x27`/`0x90`/`0x91` (`$8C:DC10..DC56`), menus (full brightness
  `$800F` on exit), synchronous fades `$80:DD72..$80:DE6D` (U for each).
* The recomp PPU applies the brightness to every composed pixel, margins included
  (`snes/ppu.c` ~829–836, 2587). Fades to black need nothing.

## Fades to white, flashes (U)

Not via INIDISP. Candidates: colour math with the fixed colour (`screen_flash_effect_timer_tick`
`$90:98ED`), palette cross-fades (`$90:D481`). The PPU composes colour math and palettes per pixel
across the widened line, so margins follow, unless a window limits the effect to [0, 256): then
`PpuSetWidescreenWindowExpansion` (`ppu.h:858`).

## Mosaic (U)

`screen_mosaic_pixelate_transition` (`$8C:BB72`, `$0878`). Check that margin pixels use the same
block grid (`PpuMosaicAt` in `ppu.c`); a margin that stays sharp during a mosaic is the failure to
look for.

## Host-side layers

Fog and the presenter's menu backdrop are drawn after the PPU: multiply them by the current
brightness (`$0106 & $0F` / 15, or the PPU's value) so they fade with the picture. Ghosts drawn with
`PpuSetExtraObjects` are inside the PPU and fade automatically.

Check: frame dumps through a door fade, a script fade-out, a flash, at 16:9.
