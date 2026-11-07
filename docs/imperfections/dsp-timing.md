# S-DSP Timing

**Status:** fixed in PR 1: the S-DSP runs per clock
(`runner/src/snes/dsp_clock.c`, `SNESRECOMP_DSP_PER_CLOCK=0` for the old per-sample model in `dsp.c`).

## What MesenCE does
Its S-DSP runs voice by voice through the 32 SPC cycles of a sample: each voice reads its directory entry, decodes
BRR 4 samples at a time 8 ahead of playback, and updates its envelope at its own clock. Key-on leaves the voice
silent for 5 samples and clears its ENDX bit; envelopes and noise step on one shared rate counter.

## What the recomp did, and what changed
snesrecomp's DSP (from LakeSnes) computes a whole sample at once at the sample boundary. Found by comparing every
SPC read of `$F3` with MesenCE's, from SPC cycle 8.19 M of `01-no-inputs-4m.evsmv` on:

| Difference | Effect | Branch |
|---|---|---|
| key-on starts playback at once | voices end 5 samples early; ENDX (`$7C`) set a read too soon | 5-sample delay, ENDX cleared |
| ENDX set when the block after the end block starts | ENDX up to 16 samples late | set at sample 4 of the end block |
| a rate counter per voice, reset at each step | envelopes (`ENVX`) step at other samples | one shared counter with the hardware offsets |

With these, the SPC's instruction stream matches MesenCE's to SPC cycle 8.82 M (was 8.31 M).

## Per clock (2026-10-31)
The sound driver switches a voice's `SRCN` and toggles `FLG` within about 5 samples; whether a loop in that window
takes the old or the new loop address depends on the clock inside the sample at which the voice reads its
directory entry. `dsp_clock.c` runs the hardware's schedule: 32 clocks per sample, nine steps per voice at fixed
clocks, staggered from voice to voice (anomie's S-DSP document), every register read at its own step; the SPC
steps it to the cycle of a `$F3` access before the access (`apu_dspTo`, as `apu_timersTo` does for the timers).
Our own code; behaviour checked against MesenCE's `$F3` reads.

That moved the first `$F3` read that differs from SPC cycle 8.82 M to 10.45 M, where the driver reads timer 0
(`MOV A,$FD` at SPC `$0741`) one cycle before MesenCE's tick: MesenCE's prescaler clocks a timer on the falling
edge of a half-period toggle, so a timer ticks on the last cycle of each 128-cycle (timer 2: 16) period from
power-on, ours did on the first. With that phase (`apu.c`, `SNESRECOMP_SPC_TIMER_PHASE`, default 1 with access
cycles) all 252,759 compared `$F3` reads match MesenCE's through NMI 1495.

## Left for a PR
- `tests/audio/dsp_output_voices_test.c` checks `sizeof(Dsp)` against the saved-state size; the fields added on
  the branch (`konDelay` on) sit after the saved range (`dsp_saveload` stops at `konDelay`) but grow the struct,
  so the check fails since round two. Move the host fields out of `Dsp` or restate the check.
- `apu_native.inc` (`SNESRECOMP_APU_NATIVE`, off by default) still runs the per-sample model.
- On the per-clock path the output filter and the enhancement shadow of `dsp_cycle` are not applied.
