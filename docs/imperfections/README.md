# Imperfections: Where the Recomp Differs from MesenCE

The recomp is compared with MesenCE, the reference ([../reference-emulators.md](../reference-emulators.md)),
through everscript-vscode's `tools/compare` (`--backend mesen,recomp`). Each file below is one known way the
recomp differs: what it is, how it was measured, and how it was fixed or what would fix it. The owner's
rule: **snesrecomp is not changed**; we work only in the files that adjust the game (glue `src/`, our frame
driver `src/soe_frame_driver.c`, seeds `recomp/`, tools). What snesrecomp itself should change is written down
here for upstream; all open ones together as one proposal: [../upstream-access-timing.md](../upstream-access-timing.md).

Ordered as found, measured on `01-no-inputs-4m.evsmv` (all against MesenCE 2.2.1):

| Imperfection | Where | Effect | Status |
|---|---|---|---|
| [late-nmi.md](late-nmi.md) | snesrecomp frame driver | NMI enabled inside vblank never delivered: the NMI count one behind from frame 5, again at 6 loads | fixed in our frame driver |
| [sampling-point.md](sampling-point.md) | the comparison | a frame's end is a different moment on the two sides | solved: both sample at NMI entry |
| [short-scanline.md](short-scanline.md) | snesrecomp beam | no 1360-clock line 240 every other field (+2 clocks a field); CPU started at clock 0, not 186 | fixed in our frame driver (alignment at any H since 2026-10-30) |
| [apu-time-in-long-frames.md](apu-time-in-long-frames.md) | snesrecomp APU timing | SPC700 got no time before the first port write: the boot upload waited 46,700 clocks | fixed in our glue |
| [interrupt-cost.md](interrupt-cost.md) | snesrecomp frame driver | the 8-cycle interrupt sequence uncharged: handlers 60 clocks early | fixed in our frame driver |
| [dram-refresh.md](dram-refresh.md) | snesrecomp runtime | refresh 0.09 % short and at no fixed beam position | fixed in our frame driver |
| [spc-clock-rate.md](spc-clock-rate.md) | snesrecomp APU constant | SPC700 0.16 % fast | corrected in our frame driver |
| [auto-joypad-read.md](auto-joypad-read.md) | snesrecomp beam | `$4212` bit 0 busy from V=225, not from MesenCE's 256-clock point: the NMI handler's wait up to 9 passes short | fixed in our frame driver |
| [nmi-recognition.md](nmi-recognition.md) | snesrecomp frame driver | NMI taken after the first instruction past V=225, not after the first with a cycle from V=225+6 | fixed in our frame driver |
| [apu-port-handshake.md](apu-port-handshake.md) | snesrecomp SPC700 model | SPC instructions do their port accesses on their first cycle: 55 % of sound-port handshakes take one poll pass more or fewer | **open on develop: the first difference left;** mostly fixed in snesrecomp on branch `spc-access-cycles` |
| [dma-overhead.md](dma-overhead.md) | snesrecomp DMA | start/end alignment 2–8 clocks off per DMA; an empty `$420B` write costs 12 | open on develop; fixed in snesrecomp on branch `spc-access-cycles` (rule measured) |
| [refresh-before-access.md](refresh-before-access.md) | our frame driver | a refresh stall inside an instruction came after its port access | fixed on branch `snesrecomp-spc-access-cycles` |
| [port-word-writes.md](port-word-writes.md) | snesrecomp port writes | a 16-bit port write lands both bytes at once, high first | fixed on branch `spc-access-cycles` (per-byte clocks, queue in time order) |
| [dsp-timing.md](dsp-timing.md) | snesrecomp S-DSP | per-sample DSP: key-on, ENDX and envelope steps off; directory reads not at their clock | partly fixed on branch `spc-access-cycles`; **the first difference left (NMI 1492)** |
| [compiled-cycle-charges.md](compiled-cycle-charges.md) | snesrecomp recompiler | compiled code billed at code speed: FastROM WRAM work ~4 % fast | open, not the first difference now |
| [host-frames-and-fields.md](host-frames-and-fields.md) | snesrecomp frame driver | a long DMA packs several fields into one host frame: replay input lands late | open for replays with input |

## Branch `snesrecomp-spc-access-cycles` (2026-10-31)
snesrecomp changed on its own branch `spc-access-cycles` (the owner's experiment for an upstream PR;
[../upstream-access-timing.md](../upstream-access-timing.md)): access-exact SPC ports, the master-clock APU timeline,
MesenCE's DMA rule. With it and the corrected auto-joypad rule ([auto-joypad-read.md](auto-joypad-read.md)), all
interpreted: every NMI from 2 to 319 has MesenCE's clock and interrupted address (develop: 86 of 328), the boot
upload's handshakes take MesenCE's number of polls in 212 of 214 (develop: 160), the load after NMI 319 in 63 %
(develop: 45 %), and its port accesses match MesenCE's one for one through the 12,748th. WRAM still parts at key 320 on both; the native
build is exact only to NMI 1 (compiled-cycle-charges.md).

### Second round (2026-10-31)
All interpreted, `01-no-inputs-4m.evsmv` against MesenCE: every NMI from 2 to 1491 exact; all 18,461 handshakes
of the first load exact; WRAM identical at every NMI key from 120 to 319 and from 322 to 1491 (develop: 120–319).
Keys 320–321 differ in one stack byte, `$7E1FF5`: the V flag the NMI pushes inside the boot's SRAM checksum
(`$8D:B478`, `ADC` over battery RAM), from different SRAM contents on the two sides, not from timing. From key 1492
on: the S-DSP ([dsp-timing.md](dsp-timing.md)). `02-newgame-boy-available.evsmv`: identical 120–319, 322–331 and
335–396. The native build is still exact only to NMI 1 ([compiled-cycle-charges.md](compiled-cycle-charges.md)).

## Measured (2026-10-30, all fixes above)

- All interpreted: every NMI from 2 to 341 lands within ±32 master clocks of MesenCE's (NMI 1: 152 early, the
  boot upload); runs of NMIs match MesenCE's clock and interrupted address exactly (e.g. 258–320). Native: NMI 1
  558 early and NMI 25 84,706 late (the boot decompression's late NMI enable, [compiled cycle
  charges](compiled-cycle-charges.md)), the others within ±30.
- The framework's interpreter costs every instruction traced (the decompressor; whole frames 200 and 245) exactly
  as MesenCE does.
- WRAM identical at every NMI key from 120 to 319, native and all-interpreter alike. From 320 on, the first
  load's lag frames part, after a sound-upload handshake that takes one poll more. After that, 97.9–99.98 % of
  WRAM bytes stay equal, at an NMI offset that grows to 6.
- Per replay and class: [../replay-sync.md](../replay-sync.md).

## Open risk (not measured as a difference)
The seed export flags 208 pushed-address returns (`# pushed-address return ...` in `recomp/bank*.cfg`). A
compiled routine that such a return lands in breaks, as the script scheduler `$8C:CFFD` did
([../bugs-and-todos.md](../bugs-and-todos.md) §2); only that one is excluded so far, `$8F:9817` is suspected.

## How to measure
```sh
# in everscript-vscode
node tools/compare/compare.mts "Secret of Evermore (U) [!].smc" --backend mesen,recomp \
  --replay tools/compare/replays/01-no-inputs-4m.evsmv --frames 3000 --keep-going --regions wram
```
The key is the number of NMIs since power-on (the glue's and the Lua script's running count of `$7E0100`,
which the attract demo zeroes when it loops), or, for MesenCE against the recomp, the NMI's number. Probes
(`src/game_rtl.c`): `SOE_COMPARE_PEEK` (bytes and master clock per frame), `SOE_COMPARE_NMI_LOG` (clock and
interrupted address per NMI: the tool that found most of the above). For one frame, instruction by instruction:
MesenCE exec callbacks (`MesenJob.lua`) against the all-interpreted recomp's `SNESRECOMP_CYC_WATCH=000000-FFFFFF`
on stderr (`RecompJob.onStderr`); the SPC side through MesenCE callbacks with `emu.cpuType.spc,
emu.memType.spcMemory` and `spc.cycle / spc.clockRatio` as the clock (the SPC runs in catch-up batches, so
`masterClock` is useless there).
