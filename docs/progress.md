# Progress: Replays Against MesenCE

How far each replay plays exactly as MesenCE 2.2.1 plays it: WRAM identical, compared each time an NMI is taken
(the "frame" below is that NMI's number). Measured 2026-11-02 on branch `snesrecomp-spc-access-cycles`.

| Replay | In sync, interpreted | In sync, native | Notes |
|---|---|---|---|
| `01-no-inputs-4m` (attract demo, 14,424 frames of input) | **all**: 120–10886 | 120–319 | Interpreted needs the snesrecomp changes below. Native parts at 320: compiled code is billed at code speed ([compiled-cycle-charges.md](imperfections/compiled-cycle-charges.md)). |
| `02-newgame-boy-available` (New Game to free play, 17,948 frames) | **all**: 120–10741 | 120–319 | Same as 01. Inputs arrive on MesenCE's fields ([host-frames-and-fields.md](imperfections/host-frames-and-fields.md)). |
| `03-complete-run-7h` (whole game, 1,559,442 frames) | not measured | not measured | A run takes hours. |

Frames before 120 are not compared: the RNG is copied in from MesenCE at 115.

## What it took

**In snesrecomp** (branches `spc-access-cycles` and `ppu-render-reads`; without them interpreted parts at 319):

| Change | Why | Details |
|---|---|---|
| SPC700 port accesses on their own cycle, APU on the master clock | sound-driver handshakes took one poll more or less | [apu-port-handshake.md](imperfections/apu-port-handshake.md), [port-word-writes.md](imperfections/port-word-writes.md) |
| S-DSP per clock, SPC timers on their cycle | the driver reads ENDX/ENVX in tight loops | [dsp-timing.md](imperfections/dsp-timing.md) |
| DMA start/end alignment | NMI recognition after `$420B` | [dma-overhead.md](imperfections/dma-overhead.md) |
| H/V latch (`$2137`, `$213C/D`) and OAM/CGRAM reads during rendering | the attract demo's RNG seed reads them with the display on | [hv-counter-latch.md](imperfections/hv-counter-latch.md) |

**In our frame driver** (`src/soe_frame_driver.c`): late NMI, NMI recognition, interrupt cost, DRAM refresh at
MesenCE's positions (also inside the NMI handler), the auto-joypad busy window, the short scanline
([imperfections/README.md](imperfections/README.md) has one file each).

**Still open:** native timing ([compiled-cycle-charges.md](imperfections/compiled-cycle-charges.md)); the
snesrecomp changes are not upstream yet ([upstream-access-timing.md](upstream-access-timing.md)).

## How to measure

In everscript-vscode, with the recomp built:

```sh
node tools/compare/compare.mts rom.smc --backend mesen,recomp \
  --replay tools/compare/replays/<replay>.evsmv --regions wram --verbose --keep-going
```

For the interpreted column, run it with the environment from `tools/compare/probes/interp.mts`
(`allInterpretedEnv`). History and lessons: [replay-sync.md](replay-sync.md),
[mesence-parity-lessons.md](mesence-parity-lessons.md).
