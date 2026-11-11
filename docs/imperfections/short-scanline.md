# The Short Scanline and the Reset Clock

**Status:** fixed in snesrecomp by PR 6 ([../pr/06-frame-driver.md](../pr/06-frame-driver.md)): `snes.c`, `SNESRECOMP_SHORT_LINE` and `SNESRECOMP_RESET_CLOCK`, 2026-11-11; our glue no longer does it. Earlier: fixed in our frame driver (`src/soe_frame_driver.c`), 2026-10-30; fields a long DMA crosses, 2026-11-03.

## Short scanline
On NTSC without interlace, line 240 of every other field is 1360 master clocks, not 1364 (MesenCE
`SnesPpu.cpp:412`: "scanline 240 of every other frame ... is only 1360 cycles"). snesrecomp's beam makes
every field 357,368 clocks; MesenCE's alternate 357,368 / 357,364. The recomp gained 2 clocks a field:
774 by field 378. Our driver sets the beam's H position at each field's end so the next V=225 falls where
MesenCE's does (the 4 clocks are taken at V=225 instead of line 240: H differs by 4 for lines 225–240 only).

The alignment first worked only when the beam stood exactly at H=0 of line 225 at the field's end. When the last
instruction had already pulled it past H=0, the field kept the framework's 357,368 clocks. The recomp's V=225 then
drifted up to 188 clocks late and snapped back now and then (measured: the beam clock at each frame end against
MesenCE's field ends, `01-no-inputs-4m.evsmv`). Since 2026-10-30 the alignment works from wherever the beam stands
on line 225; the field ends now differ only by the last instruction's overrun (0–38 clocks).

### Fields a long DMA carries the beam across (2026-11-03)
The alignment runs at the end of a host frame, and only when the beam stands on line 225. The reset code's three
64 KB clears (~540,000 clocks each, more than a field) carry the beam across field ends inside one instruction, so
those fields kept 357,368 clocks: the beam ran 4 clocks behind MesenCE's per short field it missed, until the next
aligned field end put it back. NMIs hide it (they fall on instruction boundaries); the attract demo's RNG seed does
not: when the demo loops, the seed code runs right after those clears and reads the H counter. Replay 01, NMI 11986:
every NMI on MesenCE's clock, but `$213C` read `$F6` instead of `$F8` (the beam at H clock 984 against MesenCE's 996
on line 124), a different seed, WRAM apart from key 11987 to the end (everscript-vscode
`tools/compare/probes/ppu-reads.mts`, `nmi-clocks.mts`).

Fix until PR 6 (`align_beam`, removed: the beam now has the short line itself, so no field needs moving): after each CPU slice the beam is put where this convention has it at its own clock (the next
V=225 on MesenCE's field end), when the move stays on its line and on one side of H=1024 and H=0, so no line, HDMA or
auto-joypad event is skipped or repeated; otherwise the next slice does it. `SOE_BEAM_LOG=1` prints each move.
Both replays, native and interpreted: every NMI key identical to MesenCE to the end ([../progress.md](../progress.md)).

## Reset clock
MesenCE runs the first instruction at master clock 186 (the 65816 reset sequence) while the beam starts at 0;
the recomp started the CPU at 0. Our driver parks the CPU to 186 before the first instruction.
