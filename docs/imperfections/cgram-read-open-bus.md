# CGRAM Reads Leave Bit 7 of the High Byte at 0

**Status:** fixed for DMA reads, the only reads the game uses for it, in our frame driver (`cgram_dma_open_bus`). snesrecomp
itself still returns 0.

## What happens
A CGRAM word has 15 bits. MesenCE (`SnesPpu.cpp`, `$213B`) returns bit 7 of the high byte from the PPU2 open bus, which
the low byte's read just set. snesrecomp returns 0 there and keeps no PPU2 open bus.

## Effect
`04-newgame-flowers.mmo` with `SOE_MMO_FRESH=1`, key 23787: the game reads CGRAM back into its palette buffer, 85 bytes
at `$618A`-`$61D6` differ, each by bit 7 of a high byte (`FF` against `7F`, `83` against `03`). Native and interpreted runs agree.

## Fix
The game reads CGRAM back by DMA from `$213B`. `snes_set_wram_write_log_hook` passes every DMA store into WRAM after it landed:
for a channel reading `$213B`, the hook keeps the low byte and ORs its bit 7 into the high byte. It calls the framework's own
log hook first. Replay 05: key 3093 passes.

A CPU read of the high byte is not covered: the game's only one is the reset seed's, which reads the low byte. For upstream: one
PPU2 open bus for all of MesenCE's PPU2 reads (`$213B`-`$213F`).
