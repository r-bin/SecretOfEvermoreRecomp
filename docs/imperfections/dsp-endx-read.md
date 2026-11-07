# ENDX Seen a Clock Early

**Status:** open, in snesrecomp's per-clock S-DSP or its SPC700 access timing. The cause of the isolated `sound` keys is
proven for two keys; the rest are the same pattern, not traced one by one. Replay 02's stretch from NMI 18503 is traced to its first
read (below): there the read is on time and voice 7's state differs.

## What happens
At single keys, one voice differs around the end or start of a sample, and the next key matches again. The first rows name the
replays of 2026-11-04 (now local); the last is the committed replay 02:

| Replay, key | What differs |
|---|---|
| 02, 5990 | ARAM `$54` 80 / 88 (MesenCE / recomp), voice 3 `brrAddress` 792 / 54 |
| 04, 19074 | S-DSP `$7C` (ENDX) 131 / 135, voice 2 `brrAddress` 81 / 27, four BRR samples |
| 01, 2671 and 8685; 02, 653; 04, 663 | one voice's BRR samples only (at 04's 663 by about 30 of 32768) |
| `02-new-game-to-flowers.mmo`, 18503 (2026-11-07) | S-DSP `$7C` (ENDX) `00` / `80`: voice 7. Not a single key: S-DSP registers differ at 77 keys and ARAM at 81 from there, in about 35 short runs, while WRAM, VRAM, CGRAM, OAM and SRAM stay identical |

`tools/compare/probes/spc-trace.mts` on 02 from NMI 5989: both SPC700s run 265,484 instructions alike, then the read at
`$081D` gives `$50` on MesenCE and `$58` here. Bit 3 is voice 3, and 04's `$7C` differs in voice 2's bit the same way: the
ENDX bit is visible to that read here and not yet on MesenCE. The driver stores the value (`$54`), so its path differs
for a moment, and the two meet again before the next key.

### Replay 02, NMI 18503 (2026-11-07)
`tools/compare/probes/spc-trace.mts` from NMI 18502: the SPC700s run the same 4,328 instructions (pc, a, y, cycle). That does not
clear the reads, because the driver can move a `$F3` value with `MOV dp,dp`, past a and y. `tools/compare/probes/dsp-reads.mts`
compares the reads themselves: 226 reads alike, then at SPC cycle 328,301,057, the same cycle on both, the driver reads ENDX:

| Read | MesenCE | Recomp |
|---|---|---|
| `$7C` at 328,301,057 | `00` | `80` (voice 7) |
| `$7C` at 328,301,068 | `00` | `80` |

The last reads before it (voice volumes `$00`-`$70`, at 328,300,333-328,300,666) agree. So here the read is on the same clock on both
sides, and it is the S-DSP that differs: voice 7 reached a sample end in snesrecomp's S-DSP within those ~400 cycles and not in
MesenCE's (or MesenCE's cleared it). "A clock early" fits the single keys above; it is not shown for this one.

**Next:** voice 7's state per sample (BRR address, offset, `keyOnDelay`) from NMI 18502 on both sides: MesenCE through
`emu.getState()["spc.dsp.voices[7]..."]` in an SPC exec callback, the recomp through a print in `dsp_clock.c` under a probe build.

## Effect
WRAM, VRAM, CGRAM, OAM and SRAM stay identical at every key of all five replays, and the sound state matches again one key
later. How this sounds has not been measured: at worst a voice starts a few samples apart for one frame. Count per
replay: [../progress-internal.md](../progress-internal.md), "Picture and sound".

## What would fix it
Find which side of the clock an SPC700 read of `$F3` falls on when ENDX is set in the same cycle (`dspAhead` in `apu.c`,
`Dsp::Read` on MesenCE). A framework change, for upstream.
