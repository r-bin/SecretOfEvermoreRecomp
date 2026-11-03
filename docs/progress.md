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

| Replay | Compiled | Interpreted | Native | 2026-11-03 steps: resume → exclusions → NMI, traps → seed widths, tables |
|---|---|---|---|---|
| `01-no-inputs-4m` (to key 14424) | 209,970,461 | 58,001 | **99.97 %** | 16.3 → 72.3 → 97.2 → 99.56 → 99.97 % |
| `02-newgame-boy-available` (to key 17948) | 260,487,114 | 30,324 | **99.99 %** | 21.7 → 62.6 → 96.7 → 99.75 → 99.99 % |

Measured 2026-11-03 (5,642 routine variants: 5,568 compiled, 74 interpreter-only); native in sync with MesenCE to
the end of both replays, WRAM identical to the all-interpreted run at every key. The last step needed no framework
change: the seeds had given `$8C:EA43` an exit width the CDL never saw at a call site, and one jump table 98
garbage entries (`docs/bugs-and-todos.md`, "408 interpreter-only variants"); `I_NMI` is compiled.

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

What still runs interpreted (0.01-0.03 %, no run above 0.001 %):
- **Resume points that are not taken**: `$8C:C9DC`-`$8C:CBCA` (`load_a_16_16_map_graphic_through_3_id`) and
  `$8F:9821` have resume entries but run interpreted after a slice; not yet looked into.
- **Interpreter-only variants** (74, 11 in the banks the game runs): the menu and alchemy dispatchers
  `$8E:9EBE`-`$8E:AD3D` call handlers through `jsr (abs,x)` that never return normally (the CDL knows: the seeds
  carry "never returned normally" comments), and snesrecomp's `noreturn_jsr` / `terminal_jsr` only take a direct
  `JSR`; `$91:B7FA` / `$91:BB72` dispatch through an array of 16-byte records (`jsr ($AE33,x)`), which the
  `idx:X` form cannot describe. Neither runs in the two replays.
- Code the CDL library never recorded.

## Speed

Emulated frames per second, uncapped (the scripted run does not wait for vsync), headless, Apple Silicon, to the
end of each replay (everscript-vscode `tools/compare/probes/run-stats.mts --mode <mode>`; 60.1 frames/s is real
time). Measured 2026-11-03 on the build above.

| Replay | Native | Native, no compiled resume | All interpreted |
|---|---|---|---|
| `01-no-inputs-4m` | **500** (8.3x) | 304 (5.1x) | 280 (4.7x) |
| `02-newgame-boy-available` | **390** (6.5x) | 259 (4.3x) | 234 (3.9x) |

Where the time goes (macOS `sample`, 10 s of the native run on 01, the main thread): about half in the PPU's
per-pixel renderer (`ppu_resolve_pixel`, `ppu_draw_whole_line_legacy`; our glue keeps the legacy renderer for BG
mode 4, bugs-and-todos.md §3.1), the per-clock S-DSP 5-8 % (`SNESRECOMP_DSP_PER_CLOCK=0`, not MesenCE-exact, runs 524 instead of
500), the compiled code and the interpreter a few percent each. One `getenv` on every interpreter entry
(`_interp_run_core`, `SNESRECOMP_WLOG_STATE`) costs about 1 %. Compiled code is therefore not what limits speed:
the renderer is. Every mode is several times faster than real time.

## Banks that run

Instructions per bank over both replays (glue `SOE_BANK_STATS=1`, `run-stats.mts --stats`): all in `$80` and
`$8C`-`$91`, except one per NMI in `$00` (the native NMI vector's `JML` stub) and the reset's first five. The
copies snesrecomp compiles for the other mirrors (`$0x`, `$1x`, `$4x`, `$5x`, `$Cx`, `$Dx`: 233 of the 317 files in
`src/gen`) never ran (bugs-and-todos.md §3.7).

| Replay | `$80` | `$8C` | `$8D` | `$8E` | `$8F` | `$90` | `$91` |
|---|---|---|---|---|---|---|---|
| 01 | 20.6 % | 10.1 % | 0.04 % | 0.40 % | 53.1 % | 14.8 % | 1.03 % |
| 02 | 15.8 % | 7.1 % | 0.01 % | 0.36 % | 57.2 % | 18.5 % | 1.13 % |

## Bound C

Routines that run as everscript-vscode's reconstructed C instead of compiled ROM code ([game-rtl.md](game-rtl.md)
§4b). A binding counts as proven when both sync replays stay WRAM-identical to MesenCE at every key with it, its C
path ran often, and a corrupted C path (`SOE_HLE_MUTATE=1`) is caught.

| Routine | 01: C / interpreted | 02: C / interpreted | Sync with MesenCE | Control |
|---|---|---|---|---|
| `$80:859B` `random_number_generator` | 34,242 (18,091 across a refresh stop) / 1,580 | 65,224 (34,618) / 2,936 | to 14424 and 17948 | caught at key 360 |

Measured 2026-11-03 (`run-stats.mts --stats`, `compare.mts --backend mesen,recomp`). The C path runs only when the
routine ends before the next stop that is not a DRAM refresh, with no IRQ possible; the rest run the ROM bytes
interpreted. Native share with it: 99.95 % (01) and 99.96 % (02); the RNG's instructions no longer count as
compiled, and the interpreted fallbacks leave their callers interpreted until the next resume. A first version
that never crossed a refresh stop ran the C only 45 % of the time (native share 99.76 % / 99.65 %).

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
