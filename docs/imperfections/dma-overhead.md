# DMA Start and End Overhead

**Status:** open; small. Could be fixed in our glue (`snes_set_master_clock_charge_hook` is public).

snesrecomp charges a general DMA `12 + Σ(8 + 8 × bytes)` master clocks (`snes.c`, `$420B`). MesenCE
(`SnesDmaController.cpp`): `SyncStartDma` waits `8 - (clock & 7)` (1–8) to an 8-clock boundary, then 8, then
`8 + 8 × bytes` per channel, then `SyncEndDma` waits `speed - (counter % speed)` (1–8) to a whole CPU cycle.
The two differ by a few clocks per DMA, depending on the clock's phase. The NMI handler starts DMAs every frame,
so in lag frames the difference adds up. Exact only once the clock itself matches MesenCE's to the clock.
