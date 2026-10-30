# The Short Scanline and the Reset Clock

**Status:** fixed in our frame driver (`src/soe_frame_driver.c`), 2026-10-30.

## Short scanline
On NTSC without interlace, line 240 of every other field is 1360 master clocks, not 1364 (MesenCE
`SnesPpu.cpp:412`: "scanline 240 of every other frame ... is only 1360 cycles"). snesrecomp's beam makes
every field 357,368 clocks; MesenCE's alternate 357,368 / 357,364. The recomp gained 2 clocks a field:
774 by field 378. Our driver sets the beam's H position at each field's end so the next V=225 falls where
MesenCE's does (the 4 clocks are taken at V=225 instead of line 240: H differs by 4 for lines 225–240 only).

## Reset clock
MesenCE runs the first instruction at master clock 186 (the 65816 reset sequence) while the beam starts at 0;
the recomp started the CPU at 0. Our driver parks the CPU to 186 before the first instruction.
