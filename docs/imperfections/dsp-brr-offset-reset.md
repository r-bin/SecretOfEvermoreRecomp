# BRR Offset at Power-On

**Status:** open, harmless. Shows in `compare --sound` only.

## What happens
A voice's BRR offset starts at 1 on MesenCE (`DspVoice::_brrOffset`) and 0 in snesrecomp's per-clock S-DSP
(`dsp_clock.c`, its reset). All 8 voices differ until each is first keyed on, which sets 1 on both sides, so replays from
power-on show `sound` differing at keys 1-320 (the committed 01 and 02 both). Replay 04 starts from a console already running, and 05 and
06 from a mid-game state, so they do not.

## Effect
None measured: only the `brrOffset` fields differ in that range, a voice not yet keyed on makes no sound, and ARAM, the
S-DSP registers and the output samples match.

## What would fix it
`brrOff = 1` in `dsp_clockReset`, in snesrecomp (for upstream).
