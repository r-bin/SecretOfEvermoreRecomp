# A Refresh Stall Before a Port Access

**Status:** fixed on branch `snesrecomp-spc-access-cycles` (our `src/soe_frame_driver.c`, `access_clock`, through the
framework hook `RtlSetCpuAccessClockHook`). Not applicable on `develop` (its APU clock has no access point).

## What hardware does (MesenCE)
The DRAM refresh stalls the CPU 40 clocks at H=538, inside whatever instruction runs then. A port access after the
stall happens 40 clocks later. In the sound uploads' poll loop (`$8C:813E CMP $2140 / BNE`) MesenCE shows it as a
64-clock `CMP` (24 + 40) whose read sees the SPC's later value.

## What the recomp did
Our frame driver charges the stall after the instruction (dram-refresh.md), so the framework placed the port read
before it: one poll pass early about every 25 handshakes.

## Fix
The access clock is the instruction's start plus the lead (20 for absolute), plus 40 when a refresh point lies from
the start to 4 clocks past the access. The 4 clocks are measured, not derived: with 0 the load's port accesses match
MesenCE's through the 3,384th after NMI 319, with 4 through the 12,748th.
