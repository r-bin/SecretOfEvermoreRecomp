# APU Port Handshake Timing

**Status:** open, in snesrecomp's APU port model (`runner/src/snes/apu.c`). The first difference left.

## Evidence (MesenCE write callbacks on `$2140`; recomp `SNESRECOMP_APU_PORT_DIAG=1`, all interpreted)
The reset code uploads the sound driver through the SPC700 boot ROM (`$8C:8000` → `$8C:810E`), 214 writes
to `$2140`. Against MesenCE the recomp's writes are 252–376 clocks early throughout (the ±42 clocks are
2 SPC cycles of handshake granularity), steady, so the per-byte rate matches. The last three (`$D2`, `$D3`,
then `$D7`) open it to 796 clocks: the recomp's SPC700 answers in ~276–318 clocks where MesenCE's needs ~528.

## Effect
NMI 1 (taken after the upload) is 612 clocks early. The idle loop the main code waits in then leaves at a
different point (it alternates between `$80:858F` and `$80:8592`); in the lag frames of the next load the CPU
is interrupted at other instructions, and from NMI key 320 WRAM differs (`$0B33`, `$0B37–$0B38`, the stack).

## Next
Compare the SPC700 side of the last bytes (what the boot ROM does after index `$D1`), and how
`apu_schedulePortWrite` times a port write against the SPC cycle (MesenCE delays a write that lands in the
second half of an SPC cycle: `Spc.cpp:365`).
