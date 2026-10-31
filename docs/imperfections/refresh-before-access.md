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
The access clock is the instruction's start plus the access's own lead, plus 40 when a refresh point lies from the
instruction's start up to the access. With MesenCE's refresh positions (dram-refresh.md) this one rule fits all
14,780 port reads of the load. (A first version used a 4-clock slack past the access; it only stood in for the
wrong refresh position.)
