# Imperfections: Where the Recomp Differed from MesenCE

Each file is one way the recomp differed from MesenCE 2.2.1, the reference ([../reference-emulators.md](../reference-emulators.md)):
what it is, how it was measured, and how it was fixed or what would fix it. A fix is in one of two places: our glue
(`src/`, mostly the frame driver `src/soe_frame_driver.c`), or `framework/snesrecomp.patch`, whose changes go upstream as
pull requests ([../pr/README.md](../pr/README.md)). With all of them, the committed replays match MesenCE at every NMI
([../testing.md](../testing.md)).

Ordered as found. "Fixed in PR n" means in the framework patch, as pull request n of [../pr/README.md](../pr/README.md).

| Imperfection | Where | Effect | Status |
|---|---|---|---|
| [late-nmi.md](late-nmi.md) | snesrecomp frame driver | NMI enabled inside vblank never delivered: the NMI count one behind from frame 5, again at 6 loads | fixed in our frame driver |
| [sampling-point.md](sampling-point.md) | the comparison | a frame's end is a different moment on the two sides | solved: both sample at NMI entry |
| [short-scanline.md](short-scanline.md) | snesrecomp beam | no 1360-clock line 240 every other field (+2 clocks a field); CPU started at clock 0, not 186; fields a long DMA crosses kept 357,368 clocks (the attract seed at NMI 11986) | fixed in our frame driver (alignment at any H since 2026-10-30, after each slice since 2026-11-03) |
| [apu-time-in-long-frames.md](apu-time-in-long-frames.md) | snesrecomp APU timing | SPC700 got no time before the first port write: the boot upload waited 46,700 clocks | fixed in our glue |
| [interrupt-cost.md](interrupt-cost.md) | snesrecomp frame driver | the 8-cycle interrupt sequence uncharged: handlers 60 clocks early | fixed in our frame driver |
| [dram-refresh.md](dram-refresh.md) | snesrecomp runtime | refresh 0.09 % short and at no fixed beam position | fixed in our frame driver |
| [spc-clock-rate.md](spc-clock-rate.md) | snesrecomp APU constant | SPC700 0.16 % fast | corrected in our frame driver |
| [auto-joypad-read.md](auto-joypad-read.md) | snesrecomp beam | `$4212` bit 0 busy from V=225, not from MesenCE's 256-clock point: the NMI handler's wait up to 9 passes short | fixed in our frame driver |
| [nmi-recognition.md](nmi-recognition.md) | snesrecomp frame driver | NMI taken after the first instruction past V=225, not after the first with a cycle from V=225+6 | fixed in our frame driver |
| [apu-port-handshake.md](apu-port-handshake.md) | snesrecomp SPC700 model | SPC instructions do their port accesses on their first cycle: 55 % of sound-port handshakes take one poll pass more or fewer | fixed in PR 1 |
| [dma-overhead.md](dma-overhead.md) | snesrecomp DMA | start/end alignment 2–8 clocks off per DMA; an empty `$420B` write costs 12 | fixed in PR 1 (rule measured) |
| [refresh-before-access.md](refresh-before-access.md) | our frame driver | a refresh stall inside an instruction came after its port access | fixed in our frame driver |
| [port-word-writes.md](port-word-writes.md) | snesrecomp port writes | a 16-bit port write lands both bytes at once, high first | fixed in PR 1 (per-byte clocks, queue in time order) |
| [dsp-timing.md](dsp-timing.md) | snesrecomp S-DSP | per-sample DSP: key-on, ENDX and envelope steps off; directory reads not at their clock | fixed in PR 1 (per-clock S-DSP, timer phase) |
| [compiled-cycle-charges.md](compiled-cycle-charges.md) | snesrecomp recompiler | compiled code billed at code speed, charged before its accesses, deadline per block: native exact only to NMI 1 | fixed in PR 3 |
| [hv-counter-latch.md](hv-counter-latch.md) | snesrecomp PPU | `$2137` latched always, reset the `$213C/$213D` flip-flops, read 0; `$2138`/`$213B` during rendering read the CPU's address | fixed in PR 1 (latch) and PR 2 (reads during rendering) |
| [host-frames-and-fields.md](host-frames-and-fields.md) | snesrecomp frame driver | a long DMA packs several fields into one host frame: replay input lands late | open for replays with input |
| [mesen-state.md](mesen-state.md) | the comparison | a MesenCE save state as the start of a run, taken over whole | works: a local replay from a state identical to its end |
| [mmo-reset-start.md](mmo-reset-start.md) | the comparison | a MesenCE movie's start is a reset of a running console | solved: a local movie with a reset start identical to its end |
| [cgram-read-open-bus.md](cgram-read-open-bus.md) | snesrecomp PPU | `$213B` high byte's bit 7 is 0, not the PPU2 open bus | fixed for DMA reads in our glue (`src/soe_ppu_fixes.c`) |
| [vblank-flag-at-access.md](vblank-flag-at-access.md) | snesrecomp registers | `$4212` bit 7 read at the instruction's start, bits 1-5 read 0 | fixed in our frame driver (bit 7) and `src/soe_ppu_fixes.c` (bits 1-5) |
| [vram-writes-during-rendering.md](vram-writes-during-rendering.md) | snesrecomp PPU | VRAM writes outside vblank are stored, not dropped | fixed for DMA in our glue (`src/soe_ppu_fixes.c`) |
| [fade-rounding.md](fade-rounding.md) | snesrecomp PPU | below full brightness a pixel differs by one 5-bit step: snesrecomp rounds (bsnes), MesenCE truncates | explained: with MesenCE's rule (`--fade`, PR 5) both replays' pictures are identical |
| [dsp-endx-read.md](dsp-endx-read.md) | snesrecomp S-DSP | an SPC700 read sees a voice's ENDX bit a clock early: single keys of `sound`, once of ARAM | open |
| [dsp-brr-offset-reset.md](dsp-brr-offset-reset.md) | snesrecomp S-DSP | a voice's BRR offset starts at 0, not 1, until its first key-on | open, harmless |
| [mesen-rom-mirror.md](mesen-rom-mirror.md) | MesenCE | its 4th MB is one file page over and over | copied in compare runs only |
| [cgram-writes-during-rendering.md](cgram-writes-during-rendering.md) | snesrecomp PPU | CGRAM writes during rendering go to the CGRAM address, not the colour being read; bit 7 kept | fixed for DMA in our glue (`src/soe_ppu_fixes.c`), colour 0 as measured |

## Open risk (not measured as a difference)
The seed export flags 208 pushed-address returns (`# pushed-address return ...` in `recomp/bank*.cfg`). A
compiled routine that such a return lands in breaks, as the script scheduler `$8C:CFFD` did
([../bugs-and-todos.md](../bugs-and-todos.md) §2); only that one is excluded so far, `$8F:9817` is suspected.

## How to measure
[../testing.md](../testing.md) runs the replays against MesenCE. To find where two runs part, clock by clock: the probes in
[../../tools/compare/README.md](../../tools/compare/README.md) (each NMI's clock and interrupted address, one frame
instruction by instruction, the sound-port handshakes).
