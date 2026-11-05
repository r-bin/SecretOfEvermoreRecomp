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


### 3.1 The Frame Driver (`src/soe_frame_driver.c`)
Secret of Evermore runs on its own frame driver, derived from snesrecomp's `beam_frame_driver`
(which the framework lets a host replace). One host frame is one NTSC field, ending where the beam
reaches V=225; NMI is taken at the start of the field. Added to the framework's logic, each
measured against MesenCE ([imperfections/README.md](imperfections/README.md)):
- **Late NMI:** enabling NMI inside vblank fires it at once (the reset code does, at V=226).
- **Interrupt cost:** the 8-cycle interrupt sequence is charged (60 master clocks).
- **DRAM refresh:** a 40-clock stall at H=538 of every line, in place of the runtime's per-CPU-time charge.
- **SPC700 rate:** the APU's guest anchor follows MesenCE's 1,025,280 Hz instead of 17,088 cycles a frame.
- **Short scanline:** every other field is 4 clocks shorter, aligned from wherever the beam stands on line 225;
  **reset:** the CPU starts at master clock 186.
- **Auto-joypad read:** `$4212` bit 0 busy from MesenCE's 256-clock start point, for the interpreter's read lead.
- **NMI recognition:** the CPU runs to V=225+12 and one instruction more when the last one's last cycle began
  before +6 (`src/soe_last_cycle.c` sizes it).
- **APU access point:** the APU's guest anchor starts 20 clocks back (MesenCE syncs the SPC 20 clocks into an
  absolute `$2140` access).
- **Sampling hook:** `soe_frame_driver_nmi_hook` runs as each NMI is taken (tools/compare samples there).

The glue adds, around it: the APU anchored at power-on, extended frame timing
(`RtlEnableExtendedFrameTiming`), and the save-state chunk with the driver's resume point.
- **Evermore engine interaction:**
  - Evermore enables NMI by writing `$81` to `$4200` (mirrored in WRAM at `$7E0E6B`).
  - When NMI asserts, the CPU jumps to `$80:823A`.
  - The handler skips VRAM uploads if `$7E0F3A` is non-zero, copies PPU shadow registers
    (`$7E0106..$7E0125`) to the PPU hardware, flushes the CGRAM palette queue, and increments
    the frame counter at `$7E0100`.

### 3.2 Session Reset
When a player loads a save state (F1–F10) or triggers a soft reset (Ctrl+R), `GameSessionReset()`
calls `soe_frame_driver_reset()`, so the new session does not inherit the last one's resume point.

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

## 4a. Names in the Generated C

snesrecomp names the files (`bank8c_part05_v2.c`: bank, a 4 KB slice of entry PCs, the generator's version);
that is fixed in the framework (`recompiler/v2/translation_units.py`), so we do not rename them. What a reader
needs is inside them, and both come from everscript-vscode's ROM and WRAM maps:

- **Function names.** Every `func` seed carries its ROM-map name. The entries snesrecomp finds itself (mirror-bank
  copies, entries into the middle of a routine) get `symbol` lines from the same export: the known name, a bank
  suffix for a copy (`wait_for_nmi_00`), or `<routine>_<pc16>` inside a routine (`lz_copy_loop_9a76`).
  `symbol` adds no roots. The export reads the entries from the last generation's `src/gen/program_manifest.json`,
  so one `npm run recomp` after a generation names them. 2026-11-03: address-only names (`bank_8C_9A76`) went from
  4,532 of 5,510 to 15; with names mapped back, all 315 bank files are identical (one exception:
  [bugs-and-todos.md](bugs-and-todos.md) §3.6).
- **Comments.** `tools/regen.sh` runs everscript-vscode's `tools/recomp/annotate.mts`: before each function the
  routine and its note, before each instruction the instruction as the Asar export writes it, with the WRAM
  addresses it touched and their names:
  `/* evs: $80:883F  dec.w $0343  ; [$7E0343 joypad_repeat_timer, $7E0344] */`. Only comment lines are added.
  The table (`out/recomp/asar/recomp/annotations.tsv`) is made from the user's ROM and stays in everscript-vscode's
  `out/`. A name missing here is a gap in the maps: fix it in everscript-vscode's knowledge entries, then rerun
  `npm run recomp`.

## 4b. Routines Bound to Reconstructed C

everscript-vscode's `reconstructed-c/` documents routines as C (G1-rated against the ROM there). A routine runs as that
C here when `recomp/seed-exclusions.txt` has an `hle <pc24> <function> <reason>` line: the export writes
`hle_func`, and `src/soe_bindings.c` adapts the machine to the C, which CMake compiles from the sibling checkout
(`SOE_RECONSTRUCTED_C`). The C itself is never edited for the recomp.

A binding must not be told apart from the ROM routine, so the helper runs the C only when it can prove that:
- **Nothing observable inside it.** The C runs in one step, so the routine must end before the slice's deadline
  (`soe_frame_driver_deadline()`), or cross only a DRAM refresh stop: the frame driver charges refreshes after
  the slice from the points it crossed, which a WRAM-only routine cannot tell apart. Then it must still end before
  the next stop that is not a refresh (`soe_frame_driver_hard_deadline()`: the field end, a watched step, an
  interrupt run's end), with no IRQ possible (the driver takes IRQs between slices). Otherwise the ROM bytes run:
  the helper does the first instruction and yields at the second, a compiled root (`body=` on the `hle` line,
  since `hle_func` replaces the entry in every mirror). A deadline already reached at the entry stops there, and
  the interpreter then runs that one first instruction: no compiled code exists at a bound entry.
- **Same cycles.** It charges what bus-exact code charges on the path taken: 6 master clocks per CPU cycle, each
  code fetch and data access at its speed. The path is worked out before the C runs.
- **Same state.** Registers, flags, S (the RTL's pop) and the open bus (the RTL's last read) as the asm leaves them;
  only WRAM is touched, so when inside the routine each access happens cannot be seen.

**Costs from marks.** New bindings need no cost code. The C marks its basic blocks with `SOE_AT(pc)`, a no-op in the ROM build, and
`src/soe_cost.c` walks the ROM's bytes between two marks, adding up what bus-exact code charges. A branch counts as taken when its
target is the next mark, so a mark stands at each target the C takes, and on the fall-through of a branch when a later branch has
the same target. `run_checked` holds the marks' cost against the hand-written cost of the older bindings on every call (it agreed on
all 1,923,901 calls of replay 01). The walk also writes the return addresses a `JSL` leaves below S, which the replays hash.
`SOE_COST_TRACE=1` prints the marks and each walked instruction with its cost.

**Routines longer than a slice.** The raycast's C runs a step at a time (`raycast_start`, `raycast_step`). Everything a step carries
over lives in WRAM and on the stack as the asm keeps it, so when the next step might not fit the compiled ROM code takes over at
`$9F43`, between two instructions. Each step is charged at once, and `soe_frame_driver_settle_refresh` pays the refreshes crossed so
far: a walk can run on past many refresh stops, and refreshes left to the end of the slice would hide up to 5,000 clocks from the
deadline check. The driver's refresh count works from the framework's own high-water mark, since the framework has not seen what C
charged. `RAY_STEP_BOUND` is an upper limit on a step, not its cost.

**Loops that wait for the NMI.** `field_spare_time_loop` and `wait_for_nmi` are bound at their loop heads, `$8F:9821` and `$80:858F`, so an `hle` line can stand inside a routine: every edge into that address, compiled branches and task returns too, becomes a tail transfer to the helper. The NMI comes between slices, so C cannot see the frame counter move: the C runs passes while one fits before the next stop, and the compiled code from the second instruction runs the passes around the NMI. With no deadline it must not start at all, or it would spin for ever. The tasks of the spare-time loop usually end in their first few instructions, so those heads are C as well; the rest of a task goes on from an instruction inside it, because the first instruction of a compiled routine has no resume case and would run interpreted.

Without the reconstructed C (no sibling checkout) every helper runs the ROM bytes: the build stays exact.
`SOE_HLE_STATS=1` counts both paths; `SOE_HLE_MUTATE=<helper's name>` (`1`: all) corrupts the C path's result, the
control that shows the sync replays catch a wrong binding. To pick the next routine: `SOE_BANK_STATS=1
SOE_PC_COUNTS=<file>` writes how often each PC ran.

| Routine | C | Proof |
|---|---|---|
| `$80:859B` `random_number_generator` | `reconstructed-c/80/80859B_random_number_generator.c` | [progress-details.md](progress-details.md), "Bound C" |
| `$8F:A914` `update_entity_plane` | `reconstructed-c/8F/8FA914_update_entity_plane.c` | same |
| `$90:9DE8` `eval_tile_collision` | `reconstructed-c/90/909DE8_eval_tile_collision.c` | same |
| `$80:9685` `init_entity_pool` | `reconstructed-c/80/809685_init_entity_pool.c` | same |
| `$8F:C773` `entity_sprite_depth` | `reconstructed-c/8F/8FC773_entity_sprite_depth.c` | same |
| `$8F:9F05` `companion_metatile_raycast_line_stepper` | `reconstructed-c/8F/8F9F05_companion_metatile_raycast_line_stepper.c` | same |
| `$8F:9817` `field_spare_time_loop`, bound at `$9821` | `reconstructed-c/8F/8F9817_field_spare_time_loop.c` | same; no G1, it only ends on the NMI |
| `$80:858C` `wait_for_nmi`, bound at `$858F` | `reconstructed-c/80/80858C_wait_for_nmi.c` | same; no G1, it only ends on the NMI |
| `$80:8650` `vram_dma_upload_tile_block` | `reconstructed-c/80/808650_vram_dma_upload_tile_block.c` | same |
| `$90:A1B4` `map_anim_tile_channel_tick_and_vram_stream` | `reconstructed-c/90/90A1B4_map_anim_tile_channel_tick_and_vram_stream.c` | same |
| `$80:960A` `oam_depth_sorted_sprite_insert_node` | `reconstructed-c/80/80960A_oam_depth_sorted_sprite_insert_node.c` | same |
| `$8F:C6C5` `sprite_sorting_canopy_occlusion_and_render_depth` | `reconstructed-c/8F/8FC6C5_sprite_sorting_canopy_occlusion_and_render_depth.c` | same |
| `$8F:AA51` `direction_index_from_signed_byte_pair` | `reconstructed-c/8F/8FAA51_direction_index_from_signed_byte_pair.c` | same |
| `$8F:A723` `companion_waypoint_history_poll` | `reconstructed-c/8F/8FA723_companion_waypoint_history_poll.c` | same |

---

## 5. Licensing and Structural Separation

| Component | License | Where |
|---|---|---|
| **snesrecomp framework** | PolyForm Noncommercial 1.0.0 | the `snesrecomp/` submodule, pinned in `framework_pins.txt`; never copied |
| **Scaffold files** (`CMakeLists.txt`, `src/main.c`, `src/host_contract.c`, `tools/regen.sh`, CI, packaging) | the scaffold's `LICENSE` | rendered once by snesrecomp's `tools/new_project/setup_project.sh` (the first commit), edited here since |
| **Our glue** (`src/game_rtl.c`, `tools/*.js`) | MIT | this repository |
| **Our frame driver** (`src/soe_frame_driver.c`) | derived from snesrecomp's `beam_frame_driver.c`: PolyForm Noncommercial | this repository |
| **Seeds** (`recomp/*.cfg`, `recomp/seed-exclusions.txt`) | ours, no ROM bytes | written by everscript-vscode's export, committed here |
| **Generated C** (`src/gen/`) | derived from the ROM | never committed: `.gitignore`; everyone regenerates from their own ROM |

### Changes to the framework
None: the framework is neither edited nor patched (owner's rule). Where this game needs other
behaviour, our glue provides it, chiefly our own frame driver (§3.1).
