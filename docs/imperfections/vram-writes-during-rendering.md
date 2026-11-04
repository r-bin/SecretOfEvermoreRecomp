# VRAM Writes During Rendering

**Status:** fixed for DMA in our frame driver (`charge_dma`, `vram_dma_write`). snesrecomp still stores them (`TODO: vram access
during rendering`).

## What MesenCE does
`SnesPpu::Write` `$2118`/`$2119`: outside vblank and forced blank (`CanAccessVram`) the byte is dropped; the address still counts.

## Effect
Replay 01's NMI 5997 sends about 60 KB through the visible picture: on MesenCE only what falls in vblank lands. 35,396 VRAM
bytes differed at key 5998 on our side, which only a compare with `--regions vram` shows: WRAM stayed identical.

## Fix
The charge hook keeps VRAM when a DMA to `$2118`/`$2119` is not wholly inside vblank (one inside it costs nothing) and forced blank
is off. Each byte then passes `ppu_set_vram_write_log_hook` after it landed, on the same byte clock as CGRAM's
([cgram-writes-during-rendering.md](cgram-writes-during-rendering.md)), and is put back when the picture is being drawn.

## Limits
CPU writes to `$2118`/`$2119` during rendering are not covered: the hook runs after the store, and only a DMA has a copy to restore
from. Nothing has needed them.
