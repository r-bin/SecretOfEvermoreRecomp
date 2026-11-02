# What the MesenCE Parity Work Taught Us (2026-10-31)

Four rounds of making the recomp's WRAM identical to MesenCE 2.2.1's at every NMI, on snesrecomp branch
`spc-access-cycles` (pinned by this repo's branch `snesrecomp-spc-access-cycles`). The fixes and their evidence are
in [imperfections/](imperfections/README.md) and [upstream-access-timing.md](upstream-access-timing.md); this file
keeps what we learned on the way: about snesrecomp, about the method, and about the traps.

## Result
All interpreted, both sides with cleared SRAM:

| Replay | Identical NMI keys | Glue only (develop) |
|---|---|---|
| `02-newgame-boy-available.evsmv` | every key, 9,227 of 9,227 | 120–319 |
| `01-no-inputs-4m.evsmv` | every key, 9,287 of 9,287 (since 2026-11-02; was 120–5997) | 120–319 |

The native build is still exact only to NMI 1: compiled code is billed at code speed (part B of the proposal).

## Without the framework changes it does not sync
Each snesrecomp change has a switch; one at a time turned off, on `01-no-inputs-4m.evsmv` to key 6000:

| Turned off | First differing key |
|---|---|
| nothing | 5998 |
| `SNESRECOMP_APU_MASTER_CLOCK=0` (master-clock APU timeline) | 319 |
| `SNESRECOMP_SPC_ACCESS_CYCLES=0` (SPC700 accesses on their own cycle) | 320 |
| `SNESRECOMP_DMA_ALIGN=0` (DMA start/end alignment, empty `$420B` free) | 320 |
| `SNESRECOMP_DSP_PER_CLOCK=0` (S-DSP per clock) | 1492 |
| `SNESRECOMP_SPC_TIMER_PHASE=0` (SPC timers tick at the end of their period) | 1492 |
| `SNESRECOMP_HBLANK_EXACT=0` (`$4212` H-blank at the read's clock) | 5998: not needed by this replay |
| `SNESRECOMP_PPU_RENDER_READS=0` (`$2138`/`$213B` during rendering read what the PPU fetches; 2026-11-02) | 5998 |

Key 319/320 is where the glue-only work stopped: there the SPC700's port timing decides, and our files cannot reach
inside the SPC700, the S-DSP or the DMA charge. The H/V latch fix (no switch) does not move the first WRAM
difference, but without it the NMIs drift from 5998 on.

The changes are hardware rules, not Secret of Evermore rules: every number comes from MesenCE's source or from
measurements against it, and none names an address of the game. They help any game whose sound driver handshakes
tightly or whose code reads the beam. Two are fitted on this game's code, and a PR has to say so: the DMA's 30-clock
lead assumes `STA $420B` in FastROM code, and the per-access clocks (APU ports, `$4212`, `$2137`) are exact only in
the interpreter.

The frame-driver fixes (late NMI, NMI recognition, refresh, auto-joypad window, line 225) live in our
`src/soe_frame_driver.c`, because snesrecomp lets a game bring its own driver. They are just as game-neutral and
belong upstream too (part F of the proposal).

## Updating snesrecomp's main
Upstream's ten commits between our old base `ef0b3ff7` and `98f41838` were HD rendering, MSU-1 playback and a
rollback flag; none touched the SPC700, the APU, DMA or the recompiler. The branch rebased without a conflict and
measured the same. Lessons: upstream is not working on timing, so nothing there will close these gaps for us; and
`ppu.c` is where upstream is busy (HD raster jobs), so keep the branch's PPU change small and rebase it before a PR.

## What we learned about snesrecomp
- The SPC700 core ran a whole instruction at its first cycle, and the S-DSP a whole sample at one clock. Both are fine
  for sound, both are too coarse for a sound driver that polls ports and reads `ENDX`/`ENVX` in tight loops.
- The interpreter is cycle-exact per instruction (every opcode costs what MesenCE charges); its trace
  `SNESRECOMP_CYC_WATCH` now prints the absolute clock (`now=`, the end of the instruction).
- A host frame is not a field: a long DMA carries the beam past V=225 inside one host frame, and a host frame can
  start lines after V=225. Anything placed from the frame start must count back to line 225.
- DMA is charged to the `$420B` write; on hardware it runs in the next instruction's first cycle. That matters
  wherever an instruction's last cycle decides something (NMI recognition).
- The runner's headless script spends frames of its own: a `poke` takes two, a `press` of n frames n + 1. A replay
  turned into a script lands every press late unless the tool takes those frames back (everscript-vscode
  `tools/compare/recomp.mts` does).
- snesrecomp's own test `tests/audio/dsp_output_voices_test.c` checks `sizeof(Dsp)`; any new field in `Dsp` breaks it.
  The cosim build excludes `common_rtl.c` but `snes.c` now calls into it. Both must be settled before a PR.
- `ppu.c` is also compiled alone by snesrecomp's PPU tests: no calls from it into the rest of the runtime (we used a
  hook that `snes.c` installs).
- An env switch the glue overrides is no switch: `RtlApuUseMasterClock` in our glue used to ignore
  `SNESRECOMP_APU_MASTER_CLOCK=0`, so its first A/B run measured nothing. Our glue now honours it.

## Method that worked
1. Find the first key where WRAM differs, then the first NMI whose clock or interrupted address differs
   (`SOE_COMPARE_NMI_LOG` against MesenCE's `nmi` event). Fix the earliest one only, then measure again.
2. Line both sides up by absolute master clock, instruction by instruction, in that one frame (MesenCE exec callbacks
   against `SNESRECOMP_CYC_WATCH` `now=`). A refresh stall booked one instruction later on our side is a harmless blip;
   a difference that lasts is the cause.
3. When the instruction streams part, diff WRAM at that key and log the reads that fed the branch on both sides
   (port handshakes, `$F3` reads, `$4212`, PPU registers). The value that differs names the subsystem.
4. Read MesenCE's source for the rule, implement it in our own code, and keep a switch to turn it off for A/B runs.

The probes live in the session scratch space; worth moving into everscript-vscode's `tools/compare/probes/`: the
absolute-clock trace, the WRAM diff at a key, a per-frame peek, the `$F3` and handshake comparisons, the DMA and H/V
latch logs (`SNESRECOMP_DMA_LOG`, `SNESRECOMP_HV_LOG`, `SNESRECOMP_PPU_READ_LOG`, `SNESRECOMP_DSP_LOG`,
`SNESRECOMP_PORT_LOG`).

## Traps
- MesenCE keeps battery RAM per ROM file name and writes it back: a run on the ROM's own name starts from the owner's
  save. Compare runs use their own name and an empty save folder (`SOE_COMPARE_SAVE_ROOT`).
- MesenCE's memory callbacks fire for every bank mirror only if registered for each: `$2140` written through bank
  `$8C` is missed by a callback on `$00`/`$80` only. Its `$F3` read callback also fires on IPL-upload accesses the
  program does not make.
- MesenCE exec callbacks on every instruction are slow: arm them from an NMI near the frame of interest. The NMI count
  is not linear in time (NMI is off during loads), so find the frame by its NMI clock first.
- A probe that keeps every instruction since power-on runs out of memory: filter by clock window on both sides.
- macOS has no `timeout`: `perl -e 'alarm N; exec @ARGV' …`. Two full-length key runs at once crashed node.
- MesenCE skips rendering on a wall-clock timer when it runs at maximum speed (the testrunner does), which leaves
  its CGRAM read address (the last palette lookup of its renderer) stale on skipped frames. Checked on 01 to key
  6100: identical hashes with `--snes.disableFrameSkipping=true`, so the reference stays deterministic here; a game
  with sub screen layers on during such a read could differ between runs.
- "99 %" bytes equal and "identical keys" are different measures: one differing byte (an RNG seed) makes every later
  key differ.
