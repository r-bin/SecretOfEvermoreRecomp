# Reference Emulators: Which One We Trust for What

Three emulators sit next to the recomp. They differ in two ways: how far we trust their timing,
and how much of them we can drive from a script. Only one of them is both trusted and scriptable,
so only that one decides whether the recomp is right.

| Emulator | Trust | Control | Use it for |
|---|---|---|---|
| **MesenCE** | trusted (cycle-accurate SNES core) | good: headless Lua, memory reads/writes, trace logger, save states, MCP bridge | **the WRAM comparison**: the reference the recomp must match |
| **ares** | the precision reference | least: a GUI with a debugger; the command line only picks system, settings, fullscreen and a save-state slot | probing by hand: a second opinion on one frame when MesenCE's answer is in doubt |
| **snes9x 2005** (optional: everscript-vscode's core) | **not trusted**: known timing gaps | full: that repository's build, headless in node at ~10,000 fps, write breakpoints, CDL recorder | probing: fast experiments, "who writes this address", replays, coverage. Never the reference |

## Rules

1. **A WRAM comparison is against MesenCE.** "Matches snes9x" is not evidence that the recomp is
   right, and "differs from snes9x" is not evidence that it is wrong.
2. **snes9x is never treated as accurate.** Use it to find *where* to look (a frame, a writer, a
   routine), then confirm on MesenCE.
3. **ares settles disputes, by hand.** When MesenCE and the recomp disagree in a way that looks like
   an emulator question (timing, PPU, DMA), check that frame in ares.
4. **MesenCE is compared at every NMI from power-on**, every byte ([testing.md](testing.md)): the recomp runs clock for
   clock with it. Any other emulator drifts: compare with it at game events (a room change, a fight's end), never at
   equal frame numbers.
5. **Say which emulator** every result came from, in bug reports and in [bugs-and-todos.md](bugs-and-todos.md).

## How to run each

### MesenCE
- The 2.2.1 release: `nesdev-org/MesenCE`, tag `2.2.1`, commit `20ba206c` (the version string names it). The harness looks
  for `/Applications/Mesen.app` or `$MESEN`, and passes `--snes.ramPowerOnState=AllZeros --snes.disableFrameSkipping=true`;
  every other setting is the default.
- Headless: `Mesen --testRunner <script.lua> <rom> --timeout=<seconds>`. The script drives it with
  `emu.addEventCallback(fn, emu.eventType.endFrame)`, reads memory with
  `emu.read(addr, emu.memType.snesWorkRam)`, and stops with `emu.exit()`. Checked 2026-10-30:
  600 frames from power-on, `$7E0100` = 461. The process exit code was 255 although the script
  called `emu.exit(0)`, so read the script's printed output, not the exit code.
- Interactive: the debugger (trace logger, event viewer, memory and PPU viewers). everscript-vscode's
  `npm run recomp` writes `out/recomp/asar/mesen/labels.mlb` and `rom.cdl` for it: our names and
  the recorded code/data split.
- Lockstep against the recomp: `tools/compare/compare.mts mesen recomp` runs a generated Lua script that hashes at every
  NMI, as the recomp's `SOE_COMPARE_*` output does (`src/game_rtl.c`). How to run it: [testing.md](testing.md); the known
  differences: [imperfections/README.md](imperfections/README.md).
- Lua traps: the testrunner has no `io` library (dumps go over stdout); memory callbacks on a CPU address
  only see that bank, so register `$00:4200` and `$80:4200`, `$00:0106` and `$7E:0106`.

### ares
- v148. Load the ROM, use its debugger
  (memory, tracer, PPU views) on the frame in question; `--save-state <slot>` opens at a prepared
  state. No scripting, no headless mode.

### snes9x 2005
- Optional, in everscript-vscode: `tools/recomp/s9x-run.js` runs a recomp headless script on snes9x; write breakpoints in
  node; the emulator panel. Lockstep against snes9x drifted after NMI ~300 ([bugs-and-todos.md](bugs-and-todos.md) §1.1),
  which is why MesenCE replaced it.

### Not used
- higan (nightly 2020): superseded by ares.
- bsnes: the same lineage as ares; only of interest as a libretro core for snesrecomp's `snesref`.
