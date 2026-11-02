# DRAM Refresh

**Status:** fixed in our frame driver (`src/soe_frame_driver.c`), 2026-10-30; the NMI handler too, 2026-11-02.

## What hardware does
Every scanline the CPU (and a running DMA) stalls for 40 master clocks at H=538: 40 of the line's 1364
clocks. MesenCE: a `DramRefresh` event per scanline in `SnesMemoryManager.cpp` (at `538 - (clock & 7)`).

## What snesrecomp does
`snes_refresh_charge` adds 40 clocks per 1364 clocks of CPU time, wherever the CPU happens to be. That is
0.09 % short (40 per 1364 of work instead of per 1324) and puts the stall on other instructions. Over the
boot's three 64 KB DMAs it came to ~1,000 clocks.

## Fix
Our driver ends each CPU slice at the next refresh point (H=538 of the line), works out what the framework
charged for the slice from its refresh state (`snes_refresh_state_get`), and tops it up to one 40-clock stall
per refresh point the slice crossed. A long DMA gets the hardware's count; normally the framework charges
none and the stall lands after the instruction that reached H=538.

## Left
The ±7-clock jitter of MesenCE's refresh position (`538 - (clock & 7)`) is not modelled.

## The NMI handler (2026-11-02)
The handler ran as one call to its RTI (`interp_bridge_run_interrupt` ignores the master deadline), so inside it only
the framework's own charge applied: 40 per 1364 clocks of work from the handler's start. In a normal field the
handler starts ~140 clocks after line 225 and those charges happen to fall before the joypad wait at `$80:8473`
ends. At NMI 9865 of the attract replay the reset had enabled NMI inside vblank, the handler started 524 clocks in,
and the line-227 stall fell after the wait: every `$4212` poll came 40 clocks before MesenCE's, one pass more, the
next NMI 30 clocks late and WRAM (a stack byte, `$1FD6`) different from key 9866. Fix: the driver pushes the frame as
before and runs the handler as an ordinary slice from its vector (`interp_bridge_run_until_quiescent` with the next
refresh point as deadline), so it stops there and goes on in the field's refresh-cut slices; its `$4212` reads then
land on MesenCE's clocks. IRQ handlers still run whole.

## Correction (2026-10-31, branch `snesrecomp-spc-access-cycles`)
MesenCE's refresh point is not a fixed H=538: `memoryManager.dramRefreshPosition` is 538 − (the line's start & 7),
and lines are 1364 clocks (4 mod 8), so it alternates between 538 and 534 line by line. In the fields whose short
line our driver takes at V=225 (short-scanline.md), MesenCE's lines 225–240 start 4 clocks after the beam's; from
line 241 on they agree. Both matter for a port access a few clocks from a refresh point.
