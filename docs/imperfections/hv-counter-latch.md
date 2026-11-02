# H/V Counter Latch (`$2137`, `$213C`, `$213D`)

**Status:** fixed in snesrecomp on branch `spc-access-cycles` (`runner/src/snes/ppu.c` `$2137`, `snes.c`
`snes_latch_position`), 2026-10-31; `$2138`/`$213B` reads during rendering fixed on branch `ppu-render-reads`
(`ppu.c` `ppu_renderOamAddress`, `ppu_renderCgramAddress`), 2026-11-02.

## What hardware does (MesenCE)
- A `$2137` read latches the counters only while WRIO (`$4201`) bit 7 is set; WRIO is `$FF` after power-on. A
  write that clears bit 7 latches too.
- `$213C` and `$213D` each return the low byte, then bit 8, on alternate reads. Only a `$213F` read resets the two
  flip-flops; a latch leaves them alone.
- The latched H is a dot count: dots 323 and 327 are 6 master clocks long (`SnesPpu::GetCycle`). It is taken at the
  read's own clock.

## What the recomp did
snesrecomp latched on every `$2137` read, reset both flip-flops at each latch, and took `hPos / 4` at the
instruction's start (24 clocks early for `LDA $2137` in FastROM). WRIO started at 0.

## Effect
The attract replay's entropy mix at `$80:81A0` (`LDA $2137`, `EOR $2138`, `ADC $2137`, `EOR $213C`, `ADC $213D`,
`EOR $2180`, `ADC $2139`, `ADC $213B`, with `BCS` branches on the carry) read the low bytes where MesenCE reads bit 8:
the first difference at NMI 5998, after every NMI through 5997 was exact.

## Fix
The latch is gated by WRIO bit 7, leaves the flip-flops, and takes the beam at the access clock
(`interp_bridge_access_lead`, 2 clocks into the bus cycle) as a dot count. NMIs through 6100 exact afterwards.

## `$2137` reads open bus (2026-10-31)
SLHV drives no data: MesenCE's read returns open bus (`$21` after `LDA $2137`, the operand's high byte); snesrecomp
returned 0. Fixed in `ReadRegOpenBus` (branch).

## PPU reads during rendering (fixed 2026-11-02)
The same seed code reads `$2138` (OAM) and `$213B` (CGRAM) on line 124/125, during active display (`INIDISP` `$00`:
brightness 0, not blanked). MesenCE returns what the PPU itself is reading at that dot: OAM at the sprite
evaluation's index (`GetOamAddress`: the range phase's index up to H 1020, the time phase's after), CGRAM at the
palette address of its renderer's last lookup (`InternalCgramAddress`; CGRAM is free only from H 1096 to H 88, on
line 0, in vblank and in forced blank). Measured at NMI 5997 of the attract replay: OAM MesenCE `$79`, ours `$08`;
CGRAM MesenCE `$00`, ours `$83`. The routine mixes them into the RNG seed (`$7E0133–013A`), so WRAM parted at key
5998 although every NMI through 6358 landed on MesenCE's clock.

**Fix** (snesrecomp branch `ppu-render-reads`, `ppu.c` `ppu_renderOamAddress` / `ppu_renderCgramAddress`, the beam
from `snes.c` `g_ppu_beam_hook`; off: `SNESRECOMP_PPU_RENDER_READS=0`):
- OAM up to H clock 1020: sprite `(first + (dot + 1) / 2) & 127`, `first` the priority-rotation start.
- OAM after: the time phase, rebuilt from OAM on demand: the sprites in range for this line (at most 32) walked
  backward from dot 270, two dots per 8-pixel sliver; before dot 270 where the previous line's walk ended.
- CGRAM: the backdrop (0) while the sub screen has no layer enabled, as at key 5997 (`TS` `$00`): MesenCE's
  renderer then ends every span with a backdrop lookup. With sub screen layers the CPU's address is kept: the
  pixel's own palette entry is not modelled.
- Only with the master-clock APU timeline (exact access clocks); other games keep the CPU's addresses.

Result, all interpreted: the attract replay identical at every key (9,287 of 9,287), the new-game replay still at
every key. A/B with the switch off: parts at 5998 again. In 01 to key 6000 these are the only two `$2138`/`$213B`
reads with the display on outside vblank.

On line 124 of that field our beam's H stood 8 clocks off MesenCE's at the same master clock (latched dot 317
against MesenCE's ~319). Only bit 8 was read there, so it did not matter; a game that reads the low byte would see it.
