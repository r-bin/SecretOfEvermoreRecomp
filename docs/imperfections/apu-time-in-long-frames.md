# The SPC700 Lost Its Time Before the First Port Write

**Status:** fixed in our glue (`src/game_rtl.c`: `AnchorApuAtPowerOn`, `RtlEnableExtendedFrameTiming` in
`SoeAfterConfig`), 2026-10-30.

## What happened
- snesrecomp's APU runs on guest time only after its first CPU→APU port write anchors that time
  (`apu_schedulePortWrite` sets `portTimeValid`; `apu_runToGuestCycle` returns at once before it). Until then
  only the audio callback moves it, and headless runs have none.
- By default the APU's guest time per host frame is clamped to one field, and the reset code's 192 KB of DMA
  packs 5 fields into 4 host frames.
- So the SPC700 started its boot ROM at the game's first port read: the recomp read `$2140 = 0000` at master
  clock 1,624,200 and saw `$BBAA` only at 1,670,920. MesenCE's SPC700 has run since power-on and answers
  `$BBAA` at the first read. Every later NMI came 44,300 clocks late.

## Fix
Write port 0's power-on value 0 at guest cycle 0 on the first frame (the SPC700 sees no change; its time is
anchored), and switch on extended frame timing so a multi-field host frame gives the APU its full time.

## Upstream
Anchor the APU's guest time at reset rather than at the first port write.
