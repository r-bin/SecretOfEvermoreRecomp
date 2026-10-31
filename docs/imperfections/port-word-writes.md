# 16-Bit Writes to the APU Ports

**Status:** open, in snesrecomp; not seen to change a handshake yet.

## What hardware does (MesenCE)
A 16-bit `STA $2142` writes `$2142` and then `$2143` one bus cycle (6 clocks) later. MesenCE's write callbacks show
the low byte first.

## What the recomp does
The port log (`SNESRECOMP_PORT_LOG=1`) shows both bytes at the same master clock, the high byte first (seen after
NMI 319 of `01-no-inputs-4m.evsmv`, the sound upload's `STA $2142`). The SPC can therefore see the new high byte
before the new low byte. Where the framework splits the word is not traced yet.

## Fix (upstream)
Queue the two bytes in bus order, the second one bus cycle later.
