# Imperfections: Where the Recomp Differs from MesenCE

The recomp is compared with MesenCE, the reference ([../reference-emulators.md](../reference-emulators.md)),
through everscript-vscode's `tools/compare` (`--backend mesen,recomp`). Each file below is one known way the
recomp differs: what it is, how it was measured, and how it was fixed or what would fix it. The owner's
rule: **snesrecomp is not changed**; we work only in the files that adjust the game (glue `src/`, our frame
driver `src/soe_frame_driver.c`, seeds `recomp/`, tools). What snesrecomp itself should change is written down
here for upstream.

Ordered as found, measured on `01-no-inputs-4m.evsmv` (all against MesenCE 2.2.1):

| Imperfection | Where | Effect | Status |
|---|---|---|---|
| [late-nmi.md](late-nmi.md) | snesrecomp frame driver | NMI enabled inside vblank never delivered: the NMI count one behind from frame 5, again at 6 loads | fixed in our frame driver |
| [sampling-point.md](sampling-point.md) | the comparison | a frame's end is a different moment on the two sides | solved: both sample at NMI entry |
| [short-scanline.md](short-scanline.md) | snesrecomp beam | no 1360-clock line 240 every other field (+2 clocks a field); CPU started at clock 0, not 186 | fixed in our frame driver |
| [apu-time-in-long-frames.md](apu-time-in-long-frames.md) | snesrecomp APU timing | SPC700 got no time before the first port write: the boot upload waited 46,700 clocks | fixed in our glue |
| [interrupt-cost.md](interrupt-cost.md) | snesrecomp frame driver | the 8-cycle interrupt sequence uncharged: handlers 60 clocks early | fixed in our frame driver |
| [dram-refresh.md](dram-refresh.md) | snesrecomp runtime | refresh 0.09 % short and at no fixed beam position | fixed in our frame driver |
| [spc-clock-rate.md](spc-clock-rate.md) | snesrecomp APU constant | SPC700 0.16 % fast | corrected in our frame driver |
| [apu-port-handshake.md](apu-port-handshake.md) | snesrecomp APU port model | the boot upload's last 3 bytes answered ~250 clocks sooner each: NMI 1 612 clocks early | **open: the first difference left** |
| [dma-overhead.md](dma-overhead.md) | snesrecomp DMA | start/end alignment a few clocks off per DMA | open, small |
| [compiled-cycle-charges.md](compiled-cycle-charges.md) | snesrecomp recompiler | compiled code billed at code speed: FastROM WRAM work ~4 % fast | open, not the first difference now |
| [host-frames-and-fields.md](host-frames-and-fields.md) | snesrecomp frame driver | a long DMA packs several fields into one host frame: replay input lands late | open for replays with input |

## Measured (2026-10-30, native build, all fixes above)

- Every NMI from 2 to 400 lands within ±32 master clocks of MesenCE's (NMI 1: 612 early, the upload).
- The framework's interpreter costs every decompressor instruction exactly as MesenCE does.
- WRAM identical at every NMI key from 120 to 319, native and all-interpreter alike; from 320 on, the first
  load's lag frames part (5 bytes at first). Afterwards 99.0–99.98 % of WRAM bytes stay equal.
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
interrupted address per NMI: the tool that found most of the above).
