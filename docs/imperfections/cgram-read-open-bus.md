# CGRAM Reads Leave Bit 7 of the High Byte at 0

**Status:** open, needs snesrecomp (`runner/src/snes/ppu.c`, `case 0x3b`). Not fixed here: the framework is not changed.

## What happens
A CGRAM word has 15 bits. MesenCE (`SnesPpu.cpp`, `$213B`) returns bit 7 of the high byte from the PPU2 open bus, which
the low byte's read just set. snesrecomp returns 0 there and keeps no PPU2 open bus.

## Effect
`04-newgame-flowers.mmo` with `SOE_MMO_FRESH=1`, key 23787: the game reads CGRAM back into its palette buffer, 85 bytes
at `$618A`-`$61D6` differ, each by bit 7 of a high byte (`FF` against `7F`, `83` against `03`). Native and interpreted runs agree.

## Fix, tried in a throwaway copy
Keep the last `$213B` value and OR its bit 7 into the high byte: the run then stays in sync to key 23834 instead of 23786.
A real fix keeps one PPU2 open bus for all of MesenCE's PPU2 reads (`$213B`-`$213F`).
