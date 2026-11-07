---
name: reference-emulators
description: Use before comparing the recomp with an emulator or calling a result correct: MesenCE is the WRAM reference, ares the by-hand precision check, snes9x only a probe that is never accurate.
applyTo: "**"
---

# Skill: which emulator decides

| Emulator | Trust | Control | Role |
|---|---|---|---|
| **MesenCE 2.2.1** (the release, `nesdev-org/MesenCE` tag `2.2.1`; `/Applications/Mesen.app` or `$MESEN`) | trusted | good: `--testRunner <script.lua> <rom>` headless, Lua memory access, debugger, `mesen2-mcp` | **the reference for every WRAM comparison** |
| **ares** (v148) | precision reference | least: GUI debugger; CLI picks system, settings, a save-state slot | probing by hand: a disputed frame |
| **snes9x 2005** (optional: everscript-vscode's core) | **not trusted** | full: node, ~10,000 fps, write breakpoints, CDL | probing: where to look, who writes what. Never the reference |

## Rules
0. Two builds on the **same** host (native against interpreter, hooks against none) is a differential
   test and needs no reference. The rules below apply when an emulator stands in for the hardware.
1. "The recomp is right" means it matches **MesenCE**. Matching snes9x proves nothing; differing
   from snes9x proves nothing.
2. Use snes9x to find the frame, the writer, the routine; confirm the finding on MesenCE.
3. When MesenCE's answer itself is in doubt (timing, PPU, DMA), check that frame in ares by hand.
4. MesenCE and the recomp are compared at every NMI from power-on (`tools/compare`); against any other emulator,
   compare at game events (`$7E0100`, room change, fight end), never at equal frame numbers.
5. Name the emulator behind every result in `docs/bugs-and-todos.md`.

## Traps
- MesenCE `--testRunner` exited with 255 although the script called `emu.exit(0)`: read the
  script's output, not the exit code.
- The Lua testrunner has no `io` library; memory callbacks see one bank only (`$00:4200` ≠ `$80:4200`).
- The recomp differs from MesenCE in known ways (late NMIs, compiled cycle charges): check
  `docs/imperfections/` before chasing a difference.

Details and how to run each: `docs/reference-emulators.md`.
