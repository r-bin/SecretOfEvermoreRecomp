# ENDX Seen a Clock Early

**Status:** open, in snesrecomp's per-clock S-DSP or its SPC700 access timing. The cause of the isolated `sound` keys is
proven for two keys; the rest are the same pattern, not traced one by one.

## What happens
At single keys, one voice differs around the end or start of a sample, and the next key matches again:

| Replay, key | What differs |
|---|---|
| 02, 5990 | ARAM `$54` 80 / 88 (MesenCE / recomp), voice 3 `brrAddress` 792 / 54 |
| 04, 19074 | S-DSP `$7C` (ENDX) 131 / 135, voice 2 `brrAddress` 81 / 27, four BRR samples |
| 01, 2671 and 8685; 02, 653; 04, 663 | one voice's BRR samples only (at 04's 663 by about 30 of 32768) |

`tools/compare/probes/spc-trace.mts` on 02 from NMI 5989: both SPC700s run 265,484 instructions alike, then the read at
`$081D` gives `$50` on MesenCE and `$58` here. Bit 3 is voice 3, and 04's `$7C` differs in voice 2's bit the same way: the
ENDX bit is visible to that read here and not yet on MesenCE. The driver stores the value (`$54`), so its path differs
for a moment, and the two meet again before the next key.

## Effect
WRAM, VRAM, CGRAM, OAM and SRAM stay identical at every key of all five replays, and the sound state matches again one key
later. How this sounds has not been measured: at worst a voice starts a few samples apart for one frame. Count per
replay: [../progress-details.md](../progress-details.md), "Picture and sound".

## What would fix it
Find which side of the clock an SPC700 read of `$F3` falls on when ENDX is set in the same cycle (`dspAhead` in `apu.c`,
`Dsp::Read` on MesenCE). A framework change, for upstream.
