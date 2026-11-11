# The Interrupt Sequence's Time

**Status:** fixed in snesrecomp by PR 6 ([../pr/06-frame-driver.md](../pr/06-frame-driver.md)): the beam frame driver, `SNESRECOMP_BEAM_EXACT`, 2026-11-11; our glue no longer does it. Earlier: fixed in our frame driver (`src/soe_frame_driver.c`, `run_interrupt`), 2026-10-30.

snesrecomp's frame driver pushes the interrupt frame without charging it. On the 65816 in native mode an
interrupt takes 8 cycles: 2 internal (6 master clocks each), PB/PCH/PCL/P pushed to the WRAM stack (8 each),
the vector read from bank `$00` ROM (8 each): 60 master clocks. Without it every NMI handler ran 55–60 clocks
early against MesenCE, which takes each field's NMI 76 clocks after V=225 (the instruction in progress, then
these 60). Our driver charges them; since then NMIs 2–400 land within ±32 clocks of MesenCE's.
