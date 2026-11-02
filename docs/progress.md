# Progress: Replays Against MesenCE

How far each replay plays exactly as MesenCE 2.2.1 plays it: WRAM identical, compared each time an NMI is taken
(the "frame" below is that NMI's number). Measured 2026-11-03 on branch `snesrecomp-spc-access-cycles`, to the end
of each replay.

| Replay | In sync, interpreted | In sync, native | Notes |
|---|---|---|---|
| `01-no-inputs-4m` (attract demo, 14,424 frames of input) | **all**: 120–14424 | **all**: 120–14424 | Needs the snesrecomp changes below and the beam alignment after long DMAs ([short-scanline.md](imperfections/short-scanline.md)). |
| `02-newgame-boy-available` (New Game to free play, 17,948 frames) | **all**: 120–17948 | **all**: 120–17948 | Inputs arrive on MesenCE's fields ([host-frames-and-fields.md](imperfections/host-frames-and-fields.md)). |
| `03-complete-run-7h` (whole game, 1,559,442 frames) | not measured | not measured | A run takes hours. |

Frames before 120 are not compared: the RNG is copied in from MesenCE at 115. Native and interpreted runs of one
build also have every NMI on the same master clock (everscript-vscode `tools/compare/probes/native-vs-interp.mts`).

## Native share

How much of a replay runs as compiled code: instructions per tier over the whole run (snesrecomp
`SNESRECOMP_TIER_STATS=1`; everscript-vscode `tools/compare/probes/tier-stats.mts`). The goal is 100 %.

| Replay | Compiled | Interpreted | Native |
|---|---|---|---|
| `01-no-inputs-4m` (to key 14424) | 34,178,730 | 175,922,106 | **16.3 %** |
| `02-newgame-boy-available` (to key 17948) | 56,620,399 | 204,166,377 | **21.7 %** |

Measured 2026-11-03 (4,871 compiled routine variants, 759 interpreter-only). What keeps it low, from the probe's list
of the interpreted runs that ran the most (by the PC each run started at):
- **Resumes.** Each CPU slice ends at the next DRAM refresh point (every line) or at the deadline, and the scheduler
  resumes in the interpreter, mid-routine; it gets back into compiled code only at the next call. Most of the top
  entries are such resume points: `$8F:9821`–`$8F:9851` (the field's spare-time loop), `$90:9DE8`–`$90:9E31`,
  `$8F:A3B3`–`$8F:A3C8`, `$8F:A914`. Fix: resume compiled code at an instruction boundary (bus-exact code has a
  block per instruction, so every instruction is a possible entry).
- **The idle loop** `$80:858F`/`$80:8592` (wait for NMI): 6.4 % of 01, 3.0 % of 02.
- **Routines kept off the native tier** (`recomp/seed-exclusions.txt`, LLE-only variants), and code the CDL never saw.

## What it took

**In snesrecomp** (branches `spc-access-cycles`, `ppu-render-reads` and `aot-bus-exact`, each on the one before;
without them interpreted parts at 319 and native at NMI 1):

| Change | Why | Details |
|---|---|---|
| SPC700 port accesses on their own cycle, APU on the master clock | sound-driver handshakes took one poll more or less | [apu-port-handshake.md](imperfections/apu-port-handshake.md), [port-word-writes.md](imperfections/port-word-writes.md) |
| S-DSP per clock, SPC timers on their cycle | the driver reads ENDX/ENVX in tight loops | [dsp-timing.md](imperfections/dsp-timing.md) |
| DMA start/end alignment | NMI recognition after `$420B` | [dma-overhead.md](imperfections/dma-overhead.md) |
| H/V latch (`$2137`, `$213C/D`) and OAM/CGRAM reads during rendering | the attract demo's RNG seed reads them with the display on | [hv-counter-latch.md](imperfections/hv-counter-latch.md) |
| Bus-exact compiled code (`SNESRECOMP_EXACT_BUS=1`, which our `tools/regen.sh` sets) | compiled code was billed at code speed and checked the deadline once per block | [compiled-cycle-charges.md](imperfections/compiled-cycle-charges.md) |

**In our frame driver** (`src/soe_frame_driver.c`): late NMI, NMI recognition, interrupt cost, DRAM refresh at
MesenCE's positions (also inside the NMI handler), the auto-joypad busy window, the short scanline and the beam
alignment after a long DMA ([imperfections/README.md](imperfections/README.md) has one file each).

**Still open:** the snesrecomp changes are not upstream yet ([upstream-access-timing.md](upstream-access-timing.md));
replay 03 is not measured.

## How to measure

In everscript-vscode, with the recomp built:

```sh
node tools/compare/compare.mts rom.smc --backend mesen,recomp \
  --replay tools/compare/replays/<replay>.evsmv --regions wram --verbose --keep-going
```

That is the native column. For the interpreted one, run it with the environment from
`tools/compare/probes/interp.mts` (`allInterpretedEnv`). MesenCE's testrunner stops after 100 s of wall time unless
told otherwise; `tools/compare/mesen.mts` passes `--timeout=3600` since 2026-11-03 (earlier measurements of 01 ended
between keys 10,100 and 11,000 for that reason). History and lessons: [replay-sync.md](replay-sync.md),
[mesence-parity-lessons.md](mesence-parity-lessons.md).
