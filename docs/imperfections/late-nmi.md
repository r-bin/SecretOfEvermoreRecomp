# Late NMI: Enabling NMI Inside Vblank

**Status:** fixed in our frame driver (`src/soe_frame_driver.c`), 2026-10-30. snesrecomp itself still lacks it.

## What hardware does
The NMI flag (`$4210` bit 7) is set at V=225 and stays set until `$4210` is read or V=0. Writing `$4200` with
bit 7 set while the flag is set fires an NMI at once, after the current instruction.

## What snesrecomp does
Its beam frame driver delivers NMI only at the start of a field, if NMI is enabled then.
`runner/src/snes/snes.c`, case `0x4200`, has
`// TODO: enabling nmi during vblank with inNmi still set generates nmi`.

## Evidence (MesenCE 2.2.1, `--testRunner`, write callbacks on `$00:4200` / `$80:4200`)
- The reset code writes `$4200 = $81` at V=226 (`$80:B5C0`, master clock 1,738,856); MesenCE's first NMI
  follows at 1,738,942. 6 such late NMIs in the first 14,424 frames of the attract replay (scene loads).
- Without the fix the recomp's NMI count was one behind from frame 5 on.

## Fix
While NMI is off and the vblank flag is set, our driver runs the CPU one instruction per bridge call (a
master-clock deadline one clock ahead) and takes the NMI after the instruction that enabled it. Measured:
the recomp's NMIs 2–400 land within ±32 master clocks of MesenCE's.

## One instruction more (2026-10-31)
MesenCE's `$4200` write sets the NMI counter to 2 (`SnesCpu::SetNmiFlag(2)`), counted down at each following
cycle start, so the NMI comes after the instruction that follows the enabling write, not after the write itself.
Our driver took it one instruction early: NMI 2515 of the attract replay (`STA $4200` at `$8C:D717`, then
`REP #$20`) came 18 clocks early and the interrupted address differed. It now runs one more instruction first.

## Upstream
In `snes.c` `$4200`: when bit 7 rises while `rdnmiPending` is set, signal the frame driver; it pulls the
bridge's deadline to now and runs the NMI there.
