# BRR Offset at Power-On

**Status:** open, harmless. Shows in `compare --sound` only.

## What happens
A voice's BRR offset starts at 1 on MesenCE (`DspVoice::_brrOffset`) and 0 in snesrecomp's per-clock S-DSP
(`dsp_clock.c`, its reset). All 8 voices differ until each is first keyed on, which sets 1 on both sides, so replays from
power-on show `sound` differing at keys 1-320 (the committed 01 and 02 both). Replay 04 starts from a console already running, and 05 and
06 from a mid-game state, so they do not.

## Why keys 1-320
They are the boot and the Squaresoft logo, which plays no sound: the reset code uploads the sound driver, decompresses, checks the
save memory, and the logo fades in and out (recomp picture, replay 01: the logo fading at NMI 300, black at 330, the intro scene
fading in at 420; checked 2026-11-07, pictures not kept). No voice is keyed on in that time. The first sound load after the logo,
right after NMI 319, uploads the intro's music and keys its voices on; from then on both sides have offset 1.

The same load was the wall the parity work stood at for days: everything through NMI 319 matched MesenCE early on, and that load's
sound-port handshakes were the first thing that parted ([apu-port-handshake.md](apu-port-handshake.md), PR 1).

## Effect
None measured: only the `brrOffset` fields differ in that range, a voice not yet keyed on makes no sound, and ARAM, the
S-DSP registers and the output samples match.

## What would fix it
`brrOff = 1` in `dsp_clockReset`, in snesrecomp (for upstream).
