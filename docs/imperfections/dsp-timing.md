# S-DSP Timing

**Status:** partly fixed in snesrecomp on branch `spc-access-cycles` (`runner/src/snes/dsp.c`); the rest is open
and is now the first difference on the attract replay (NMI 1492).

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

## Left open
The sound driver switches a voice's `SRCN` and toggles `FLG` within about 5 samples. Whether a loop in that window
takes the old or the new loop address depends on the clock inside the sample at which the voice reads its
directory entry. That needs the DSP stepped per clock (voice by voice), not per sample. Until then the SPC's music
code drifts after cycle 8.82 M, and at NMI 1492 a sound command (`apu_command_handshake`, `$8C:8214`) is answered
240 SPC cycles late.
