# The Game Runtime Layer (Game RTL): Architecture and Glue Code

In static recompilation projects powered by [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp),
the game-specific glue code is formally designated as the **Game Runtime Layer (Game RTL)**.

This document explains what the Game RTL is, why it is necessary, how Secret of Evermore's
Game RTL is structured, how the desktop host hooks work, and how licensing isolation is maintained.

---

## 1. Architectural Taxonomy

A static recompilation project consists of four distinct architectural tiers:

```text
 ┌────────────────────────────────────────────────────────┐
 │            1. Recompiled Game Code (gen/)              │
 │  • Generated native C functions from 65816 assembly   │
 │  • Operates on CpuState *cpu context                   │
 └───────────────────────────▲────────────────────────────┘
                             │ invokes & executes
 ┌───────────────────────────┴────────────────────────────┐
 │         2. Game Runtime Layer (Game RTL)               │
 │  • game_rtl.c / game_rtl.h                             │
 │  • Bridges the framework runner to game-specific logic │
 │  • Frame pacing, interrupts, lifecycle, session resets │
 └───────────────────────────▲────────────────────────────┘
                             │ coordinates
 ┌───────────────────────────┴────────────────────────────┐
 │         3. Recomp Runner / Runtime (runner/)           │
 │  • Virtual SNES hardware platform                      │
 │  • PPU rasterizer, APU/SPC700 emulator, DMA, memory bus│
 └───────────────────────────▲────────────────────────────┘
                             │ embedded inside
 ┌───────────────────────────┴────────────────────────────┐
 │         4. Desktop Host Frontend (desktop/)            │
 │  • SDL window, audio buffer streaming, joypad polling  │
 │  • config.ini parsing, key bindings, script automation │
 └────────────────────────────────────────────────────────┘
```

The **Game RTL** is the singular bridge connecting the generic SNES hardware runner to the
recompiled Evermore code.

---

## 2. Why the Game RTL is Necessary

When snesrecomp recompiles 65816 assembly, it outputs pure, passive C functions
(e.g., `func_808020_M0X0(CpuState *cpu)`). By themselves, these functions cannot run:

1. **No Entry Caller:** Real SNES cartridges do not have a `main()` function. They execute a Reset
   vector at `$80:8020`.
2. **Infinite Loops:** Evermore's reset handler clears memory, initializes hardware, and jumps
   directly into the engine main loop at `$80:8B66` (`JML $80:8B66`). This loop never returns to
   a caller.
3. **Frame Pacing:** Hardware does not have an OS frame loop. The recompiled game runs continuously;
   the runtime layer must decide when a frame boundary has occurred.
4. **Hardware Interrupt Timing:** Non-Maskable Interrupts (NMI) and IRQs must be dispatched at precise
   scanline intervals (e.g. VBLANK at scanline 225) while auto-joypad registers (`$4218/$4219`) are polled.
5. **Session Management:** Loading a save state, resetting the game, or returning to the title screen
   must reset hardware driver states without terminating the OS process.

The Game RTL satisfies all these requirements.

---

## 3. Secret of Evermore's Game RTL (`src/game_rtl.c`)

Secret of Evermore's runtime implementation is located in `src/game_rtl.c`:

> **Reconstructed C Decompilation**:
> Source moved to [`reconstructed-c/80/808020_game_rtl.c`](https://github.com/r-bin/everscript-vscode/blob/develop/reconstructed-c/80/808020_game_rtl.c).
> [View in VFS: soe://rom/code/808020](soe://rom/code/808020?c=1&asm=0)


### 3.1 The Beam Frame Driver
Secret of Evermore uses snesrecomp's `beam_frame_driver`:
- **Execution granularity:** Advances scanline by scanline across the 262 scanlines of an NTSC field.
- **Interrupt delivery:** At scanline 225 (the start of VBLANK), the driver asserts NMI.
- **Evermore engine interaction:**
  - Evermore enables NMI by writing `$81` to `$4200` (mirrored in WRAM at `$7E0E6B`).
  - When NMI asserts, the CPU jumps to `$80:823A`.
  - The handler skips VRAM uploads if `$7E0F3A` is non-zero, copies PPU shadow registers
    (`$7E0106..$7E0125`) to the PPU hardware, flushes the CGRAM palette queue, and increments
    the frame counter at `$7E0100`.

### 3.2 Session Reset
When a player loads a save state (F1–F10) or triggers a soft reset (Ctrl+R), `GameSessionReset()`
calls `snes_beam_frame_driver_reset()`. This clears scanline counters and pending interrupt flags
so the new session does not inherit stale beam timing from the previous session.

---

## 4. Host Configuration Hook (`SoeAfterConfig`)

In addition to `game_rtl.c`, the desktop runner's entry point `src/main.c` (from the scaffold)
carries one line of ours: `.after_config = SoeAfterConfig` in the host descriptor.

### The Hook Mechanism
In `src/main.c`:
> **Reconstructed C Decompilation**:
> Source moved to [`reconstructed-c/80/808020_game_rtl.c`](https://github.com/r-bin/everscript-vscode/blob/develop/reconstructed-c/80/808020_game_rtl.c).
> [View in VFS: soe://rom/code/808020](soe://rom/code/808020?c=1&asm=0)


`SoeAfterConfig()` executes immediately after `config.ini` is parsed from disk and sets:
```c
g_config.new_renderer = false;
```

### Why This Hook is Critical: BG Mode 4
During the opening sequence, pressing Start navigates to the character-name screen ("Boy / Dog").
Initially, this screen rendered as illegible digital noise in snesrecomp.

Investigation revealed:
1. The game sets up the screen in two steps: first `$2105 = $39` (Mode 1), and two frames later
   `$2105 = $04` (**SNES BG Mode 4**: BG1 8bpp, BG2 2bpp).
2. The stone tablet background is an 8bpp planar image.
3. snesrecomp's default optimized renderer (`NewRenderer`) only implements BG Modes 0–3, sending
   unimplemented modes down its Mode 7 fallback path.
4. snesrecomp's reference renderer (`ppu_legacy.c`) correctly implements all SNES modes, including Mode 4.
5. Forcing `g_config.new_renderer = false` restores pixel-perfect rendering of the character name
   screen while maintaining emulation performance above 400 FPS.

*(Once BG Mode 4 is implemented upstream in `NewRenderer`, this hook can be retired).*

---

## 5. Licensing and Structural Separation

| Component | License | Where |
|---|---|---|
| **snesrecomp framework** | PolyForm Noncommercial 1.0.0 | the `snesrecomp/` submodule, pinned in `framework_pins.txt`; never copied |
| **Scaffold files** (`CMakeLists.txt`, `src/main.c`, `src/host_contract.c`, `tools/regen.sh`, CI, packaging) | the scaffold's `LICENSE` | rendered once by snesrecomp's `tools/new_project/setup_project.sh` (the first commit), edited here since |
| **Our glue** (`src/game_rtl.c`, `tools/patch_framework.py`, `tools/*.js`) | MIT | this repository |
| **Seeds** (`recomp/*.cfg`, `recomp/seed-exclusions.txt`) | ours, no ROM bytes | written by everscript-vscode's export, committed here |
| **Generated C** (`src/gen/`) | derived from the ROM | never committed: `.gitignore`; everyone regenerates from their own ROM |

### Changes to the framework
The framework is never edited by hand. The one change this port needs, three accessors for the
beam driver's resume point (save states), is applied by `tools/patch_framework.py` when CMake
configures; it is idempotent and fails loudly when the framework's layout changes. `.gitmodules`
ignores the dirty submodule tree this leaves.
