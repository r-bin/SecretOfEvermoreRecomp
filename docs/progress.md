# Progress: Replays Against MesenCE

How far each replay plays exactly as MesenCE 2.2.1 plays it: WRAM identical, compared each time an NMI is taken
(the "frame" below is that NMI's number). Measured 2026-11-03 on branch `snesrecomp-spc-access-cycles`, to the end
of each replay. The native column is the build with compiled resume (snesrecomp `aot-resume`, below).

| Replay | In sync, interpreted | In sync, native | Notes |
|---|---|---|---|
| `01-no-inputs-4m` (attract demo, 14,424 frames of input) | **all**: 120–14424 | **all**: 120–14424 | Needs the snesrecomp changes below and the beam alignment after long DMAs ([short-scanline.md](imperfections/short-scanline.md)). |
| `02-newgame-boy-available` (New Game to free play, 17,948 frames) | **all**: 120–17948 | **all**: 120–17948 | Inputs arrive on MesenCE's fields ([host-frames-and-fields.md](imperfections/host-frames-and-fields.md)). |

`03-complete-run-7h` is not a sync replay: it does not stay in sync with MesenCE, so it is not measured here.

Frames before 120 are not compared: the RNG is copied in from MesenCE at 115. Native and interpreted runs of one
build have every NMI on the same master clock and WRAM identical at every key from 1 on, both replays to their end
(everscript-vscode `tools/compare/probes/native-vs-interp.mts`).

## Native share

How much of a replay runs as compiled code: instructions per tier over the whole run (snesrecomp
`SNESRECOMP_TIER_STATS=1`; everscript-vscode `tools/compare/probes/tier-stats.mts`). The goal is 100 %.

| Replay | Compiled | Interpreted | Native | 2026-11-03 steps: resume → exclusions lifted → NMI, traps |
|---|---|---|---|---|
| `01-no-inputs-4m` (to key 14424) | 209,107,308 | 921,154 | **99.56 %** | 16.3 → 72.3 → 97.2 → 99.56 % |
| `02-newgame-boy-available` (to key 17948) | 259,865,614 | 651,824 | **99.75 %** | 21.7 → 62.6 → 96.7 → 99.75 % |

Measured 2026-11-03 (5,795 routine variants: 5,387 compiled, 408 interpreter-only); native in sync with MesenCE to
the end of both replays. The last step: the NMI and IRQ handler bodies are seeded (their vectors' `JML` stubs made
them unreachable as roots), the snesrecomp fixes that compiled interrupt handlers needed (`docs/bugs-and-todos.md`,
"Compiled NMI handler"), BRK assertion traps declared `authority_data`, and jump-table entries only up to targets
the CDL saw run.

**Compiled resume** (snesrecomp `aot-resume`, generated with `SNESRECOMP_AOT_RESUME=1`, which our `tools/regen.sh`
sets). Before it, each CPU slice (it ends at every line's refresh point) left the code it stopped in to the
interpreter, and that code and its callers stayed interpreted until the next call bounced. Now every instruction of a
compiled variant is also an entry (a switch in its prologue, `g_aot_resume_table` in `dispatch_v2.c`), and the
scheduler goes back into compiled code two ways:
- **Suspended frames.** An exit from compiled code to the scheduler (the deadline, a transfer out of a variant's CFG,
  a deadline inside an interpreted callee) records each compiled frame it abandons: the S it continues at, its entry
  S and return frame. When the run reaches a record's S, the frame goes on compiled at the instruction it is at, and
  its RTS/RTL returns to the scheduler as a bounced call's does.
- **No frame known** (code reached by a jump, the reset code's main loop): compiled code is entered as a frame one
  below the current S, so its first return above that hands back to the scheduler at the popped address.

Which tier runs an instruction does not change the run, because bus-exact code keeps the interpreter's timing. Making
it so took four fixes in snesrecomp (`docs/bugs-and-todos.md` §2): the beam synced when compiled code returns to the
scheduler and before it reads PPU, joypad and `$42xx` registers; a word's second byte charged at its own address; the
operand's last byte on the data bus before an access (open bus, which `$2137` reads); resume off under the
interpreter levers (`SNESRECOMP_LLE_BOUNCE=0`, the target file). Variants with an RTI get no resume entries.
`SNESRECOMP_AOT_RESUME=0` at run time turns resume off, `=2` keeps only suspended frames.

What still runs interpreted (0.25-0.44 %, no single run above 0.02 %):
- **Interpreter-only variants** (408). The analyzer's reasons: callees whose exit widths it cannot prove
  (`unproven_callee_exit`, `unproven_call_at_…`), call continuations it cannot follow (`truncated_call_continuation`),
  and decodes into data. The busiest: `$8C:D037`/`$8C:D09C` (script scheduler handlers), `$8C:E52A`, `$90:D3CD`–`$90:D400`.
- **The NMI stub** `$00:823A` (`I_NMI` in `recomp/symbols.toml`, 4 bytes, interpreted by choice) and what the
  interpreter runs after it until the next call or slice.
- Code the CDL library never recorded.

## What it took

**In snesrecomp** (branches `spc-access-cycles`, `ppu-render-reads`, `aot-bus-exact` and `aot-resume`, each on the one before;
without them interpreted parts at 319 and native at NMI 1):

| Change | Why | Details |
|---|---|---|
| SPC700 port accesses on their own cycle, APU on the master clock | sound-driver handshakes took one poll more or less | [apu-port-handshake.md](imperfections/apu-port-handshake.md), [port-word-writes.md](imperfections/port-word-writes.md) |
| S-DSP per clock, SPC timers on their cycle | the driver reads ENDX/ENVX in tight loops | [dsp-timing.md](imperfections/dsp-timing.md) |
| DMA start/end alignment | NMI recognition after `$420B` | [dma-overhead.md](imperfections/dma-overhead.md) |
| H/V latch (`$2137`, `$213C/D`) and OAM/CGRAM reads during rendering | the attract demo's RNG seed reads them with the display on | [hv-counter-latch.md](imperfections/hv-counter-latch.md) |
| Bus-exact compiled code (`SNESRECOMP_EXACT_BUS=1`, which our `tools/regen.sh` sets) | compiled code was billed at code speed and checked the deadline once per block | [compiled-cycle-charges.md](imperfections/compiled-cycle-charges.md) |
| Compiled resume (`aot-resume`, `SNESRECOMP_AOT_RESUME=1`) | native share; with it, the beam, word-access and open-bus fixes | above, `docs/bugs-and-todos.md` §2 |
| Compiled RTI goes on at the popped PC under the scheduler; no `$4212` polling tick for bus-exact code | the NMI handler compiled | `docs/bugs-and-todos.md` §2 |

**In our frame driver** (`src/soe_frame_driver.c`): late NMI, NMI recognition, interrupt cost, DRAM refresh at
MesenCE's positions (also inside the NMI handler), the auto-joypad busy window, the short scanline and the beam
alignment after a long DMA ([imperfections/README.md](imperfections/README.md) has one file each).

**Still open:** the snesrecomp changes are not upstream yet ([upstream-access-timing.md](upstream-access-timing.md));
native share below 100 % (the list above: interpreter-only variants).

## How to measure

In everscript-vscode, with the recomp built:

```sh
node tools/compare/compare.mts rom.smc --backend mesen,recomp \
  --replay tools/compare/replays/<replay>.evsmv --regions wram --verbose --keep-going
```

That is the native column; the native share is `tools/compare/probes/tier-stats.mts <rom> <replay> <to>`. Headless
with no display or audio device (a locked Mac: the GL swap blocks, CoreAudio fails), run them with
`SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/dev/null`; the instruction counts are the
same as with real devices. For the interpreted one, run it with the environment from
`tools/compare/probes/interp.mts` (`allInterpretedEnv`). MesenCE's testrunner stops after 100 s of wall time unless
told otherwise; `tools/compare/mesen.mts` passes `--timeout=3600` since 2026-11-03 (earlier measurements of 01 ended
between keys 10,100 and 11,000 for that reason). History and lessons: [replay-sync.md](replay-sync.md),
[mesence-parity-lessons.md](mesence-parity-lessons.md).
