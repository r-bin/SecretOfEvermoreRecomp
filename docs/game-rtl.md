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

```c
#include "game_rtl.h"
#include "beam_frame_driver.h"
#include "desktop/config.h"

void GameRunOneFrame(void) {
    snes_beam_frame_driver_run_frame();
}

void GameDrawPpuFrame(void) {
    snes_beam_frame_driver_draw_ppu_frame();
}

/* A new session (reset / return to title) must not inherit driver state. */
void GameSessionReset(void) {
    snes_beam_frame_driver_reset();
}

/* Host configuration hook: forces legacy PPU for BG Mode 4 compatibility. */
void SoeAfterConfig(void) {
    g_config.new_renderer = false;
}

const RtlGameInfo kGameInfo = {
    .title = "secretofevermore",
    .initialize = NULL,
    .run_frame = &GameRunOneFrame,
    .draw_ppu_frame = &GameDrawPpuFrame,
    .save_name_prefix = "save",
    .session_reset = &GameSessionReset,
};
```

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

In addition to `game_rtl.c`, our build pipeline patches the desktop runner's entry point (`src/main.c`)
via `tools/recomp/host.js`.

### The Hook Mechanism
In `src/main.c`:
```c
static const SnesDesktopHostGame kGameHost = {
    .game                = &kGameInfo,
    .after_config        = SoeAfterConfig,   /* everscript glue */
    // ...
};
```

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

To prevent license contamination:

| Component | License | Repository Status |
|---|---|---|
| **snesrecomp framework & templates** | PolyForm Noncommercial 1.0.0 | **Never committed.** Cloned and scaffolded at build time in the repository root (git-ignored). |
| **everscript-vscode & Game RTL glue** | MIT | Committed in `tools/recomp/host/` and tracked in git. |

### How the Overlay Works
1. `npm run recomp` runs `setup_project.sh` from snesrecomp to create the repository root.
2. `tools/recomp/host.js` copies our `tools/recomp/host/` directory directly over the repository root.
3. Our `src/game_rtl.c` replaces the scaffold's default template.
4. Our `hookMain()` patches `src/main.c` with the `SoeAfterConfig` hook.
5. Clean, strict legal separation is maintained: no proprietary or PolyForm template files ever enter
   the everscript-vscode repository.

