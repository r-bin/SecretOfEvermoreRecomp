# Late NMI: Enabling NMI Inside Vblank Fires No NMI

**Status:** open, in snesrecomp. Not fixed here (owner: no framework changes).

## What hardware does
The NMI flag (`$4210` bit 7) is set at V=225 and stays set until `$4210` is read or V=0. Writing `$4200` with
bit 7 set while the flag is set fires an NMI at once, a few clocks after the write.

## What the recomp does
snesrecomp's beam frame driver (`runner/src/beam_frame_driver.c`) delivers NMI only at the start of a host
frame, if NMI is enabled then. An NMI enabled a few lines later in the same vblank is never delivered. The
framework knows: `runner/src/snes/snes.c`, case `0x4200`, has
`// TODO: enabling nmi during vblank with inNmi still set generates nmi`.

## Evidence (MesenCE 2.2.1, `--testRunner`, memory callbacks on `$00:4200`/`$80:4200`)
- The reset code at `$80:8020` clears VRAM and both WRAM banks with three 64 KB DMAs (≈4.4 fields), then
  writes `$4200 = $81` at master clock 1,738,856 (V=226). MesenCE's first NMI follows at 1,738,942.
- The recomp reaches the same write at the same master clock (checked with `SOE_COMPARE_PEEK`: equal state at
  equal clock), but its counter `$7E0100` is one lower from then on.
- Over the 14,424 frames of `01-no-inputs-4m.evsmv`, MesenCE takes 6 such late NMIs (frames 5, 2693, 4046,
  8815, 10168, 12732, at V=226…258): scene loads switch NMI off and back on inside vblank.
- With a trial fix (a hook on that `$4200` edge that stops the bridge and runs the NMI where it was raised;
  since reverted), the recomp's counter matched MesenCE at equal master clock, and a full-interpreter run
  matched MesenCE byte for byte through key 120.

## Effect
Each missed NMI leaves the game logic one NMI "ahead" of the counter. Everything the NMI handler counts
(the frame counter, timers, the fade steps) shifts against MesenCE; the RNG draws happen one or two frames
off. For play this is invisible (one frame at a scene change).

## Fix (upstream candidate)
In `snes.c` `$4200`: when bit 7 rises while `rdnmiPending` is set, signal the frame driver; the driver sets the
bridge's master deadline to now (`interp_bridge_set_master_deadline`), so the interpreter and compiled blocks
yield at their next deadline poll, and runs `run_interrupt(nmi_vector(), frame_end)` there.

## Working with it
Compare at game events, not equal counters ([../reference-emulators.md](../reference-emulators.md) rule 4),
or allow a few keys of offset, as the measurement in [README.md](README.md) does.
