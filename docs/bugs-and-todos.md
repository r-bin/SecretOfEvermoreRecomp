# Recomp Bugs, Findings, and Roadmap

A comprehensive record of open issues, resolved bugs, upstream limitations, and prioritized
next steps for the Secret of Evermore static recompilation.

---

## 1. Open Bugs & Active Investigations

### 1.1 Timing Against MesenCE (the reference)
- **Status:** ~99 % (2026-10-30): WRAM equal through NMI key 300, 98.3–99.95 % equal up to key 5800 on the
  attract replay, the rest from known framework limits. One file per known difference, with the
  measurements: [imperfections/README.md](imperfections/README.md).
- The earlier snes9x drift after counter ~315 (identical in interpreter and native runs) is explained by the
  first of them: the NMI the game raises by enabling NMI inside vblank ([imperfections/late-nmi.md](imperfections/late-nmi.md)).

---

## 2. Resolved Bugs & Lessons Learned

| Bug | Root Cause | Fix Applied | Architectural Lesson |
|---|---|---|---|
| **Character-name screen rendered as noise** | Screen uses **BG Mode 4** (`$2105 = $04`, 8bpp planar). snesrecomp's default `NewRenderer` only implements modes 0–3, falling back to mode 7. | Injected `SoeAfterConfig()` hook into `src/main.c` setting `g_config.new_renderer = false` to force the legacy reference renderer. | Do not assume visual artifacts originate from code recompilation; always test pure interpreter mode first. |
| **"Press START" screen corrupted in Cycle 2 ("Press : RT")** | Directive `indirect_dispatch 9f46 16 idx:X` at `$8F:9F46` (`jmp ($A3D8,x)`) inside `func_CF9F05`. The loop counter is kept on the 65816 stack via `pha`/`pla` at `$A3B1`. Emitting `indirect_dispatch` generated a native tail call (`RecompStackPop(); return _r;`), popping the native C host call stack frame on each iteration while the emulated SNES stack remained looped. This stack desync corrupted WRAM `$7E:ABDE` (OAM) in Cycle 2. | `dispatch 8F9F46` in `recomp/seed-exclusions.txt` (covers `$CF:9F46`). Falling back to interpreter executes the loop coherently with no mismatched native pops. Both Cycle 1 and Cycle 2 are 100% clean. | Indirect jump directives must not be emitted for jump sites that are internal to a stack-resident loop. |
| **HUD portrait tile overwritten in combat; dark smears under the Dog (found and fixed 2026-10-12)** | The game caches dynamic 8×8 OBJ tiles in two doubly linked lists (`$7E9727` next / `$7E96A5` prev; sentinel `$80` = LRU cache, `$81` = used this frame). The effect-sprite routine `$90:D749` calls `direct_sprite_draw_oam` `$80:9033` with stack-relative locals. `$80:9033` was in `FUNC_EXCLUSIONS`, but that only drops its root `func` line: snesrecomp rediscovered it as the JSL target and compiled it. The native call corrupted the lists within the first fight, a broken link led the allocator `$80:A4B5` to sentinel `$81`, and it handed out OBJ tile `$81`, the upper-right quarter of the HUD portrait. | The seed export emits `force_lle <pc24>` for every `func` line of `recomp/seed-exclusions.txt`, which keeps the boundary on the interpreter for all callers (685 native variants instead of 710). Verified headless: 21 fights in room `0x38`, tile `$81` unchanged and both lists consistent in every dump; same as snes9x. | Excluding a root is not the same as keeping a routine off the native tier: use `force_lle`. To find such a body: `SNESRECOMP_LLE_BOUNCE=0` (bounced vs interpreted must be bit-exact; if it fixes the run, a compiled body is at fault), then bisect the compiled entries of `src/gen/dispatch_v2.c` with `SNESRECOMP_LLE_INTERP_TARGET_FILE` (debugging.md §5.4). Note that a target file of function *roots* only is not an all-interpreter run, which first misled this investigation. The white streaks in the Boy's hit pose are real art (ROM `w_damage` frame 0). |
| **F1 save states could not be restored (black screen / frozen game)** | snesrecomp's file snapshot stores the machine (WRAM, PPU, APU, the legacy interpreter's `Cpu`) but neither `g_cpu` (A/X/Y/S/P/D/DB/PB the recompiled code and the bridge run on) nor the beam driver's resume PC. A load kept the running session's registers and PC under the saved RAM: it only looked right when both moments idled in the same loop (and even then the game logic froze while NMI kept counting); a load before the first frame (resume PC 0) cold-booted from the reset vector on top of the loaded RAM. | `src/game_rtl.c` appends a chunk (`SEXC`: resume PC, WAI flag, `g_cpu` up to `ram`) via `state_save_extra`, reads it in `state_load_extra` and applies it in `on_state_loaded`; our frame driver `src/soe_frame_driver.c` (since 2026-10-30; before that a patch to the framework's `beam_frame_driver.c`) exposes `soe_frame_driver_{resume_pc,wai_halted,set_resume}`. Verified: a state saved mid-frame in `$8F:985A` loads at frame 2 and the Boy walks. States written before this fix carry no chunk and still cannot be restored: load the SRAM save from the title (Continue, B, B) and press F1 again. | A recompiled game's position in its own code is host state; every save/load path must carry it. Test a load both at launch and mid-game, and check that the game *advances* afterwards (`$7E0B19`), not only that a picture appears. |
| **Save loads into room `0x61` with frozen logic after the full-play CDL (2026-10-30)** | With the 90 % CDL (2026-10-30), snesrecomp compiled the script scheduler `$8C:CFFD` natively (1238 native variants, was 685). Its per-thread handlers, reached through `jsr ($D029,x)`, return into its loop at `$8C:D01B` by a pushed address (the seed export flags these as "pushed-address return"). The compiled body could not follow that: after Continue the game sat in room `0x61` with Boy x = −50, `$7E0B19` stuck at 280, and the interpreter spun out its step cap in the camera-wait handler `$8C:D065`. `SNESRECOMP_LLE_BOUNCE=0` fixed it; bisecting the 1216 compiled entries (15 runs) found `$8C:CFFD` alone. | `func 8CCFFD` in `recomp/seed-exclusions.txt` → `force_lle`. Verified headless: the save loads in room `0x51`, the Boy walks out of the hut into Fire Eyes' village (room `0x25`), the picture matches the game. | A routine that pushed-address returns land inside cannot be compiled: check the export's `# pushed-address return … -> <target>` comments when a new CDL compiles more code (still open: `field_spare_time_loop` `$8F:9817` is the target of ~10 such returns at `$8F:9821` and logs `interp_cap` in native runs only). For bisecting, a timing-exact WRAM match is too strict: compare an invariant (room id, `$0B19` advancing). |
| **NMI Dynamic Hook Silent Skip Risk (`$80:84BE`)** | Dynamic hook `JML [$0125]` called via `PHK; PEA $84C0` (3-byte return frame, not 2). Emitting `indirect_dispatch ... ptrcall` with only 1 observed target causes snesrecomp to silently skip unobserved runtime callbacks. | `dispatch 8084BE` in `recomp/seed-exclusions.txt` (covers `$00:84BE`). | Do not emit static pointer dispatch directives for dynamic hook vectors with incomplete target observations. |
| **Thraxx Attract Demo False-Positive (Frame 2850)** | Comparing raw byte OAM at fixed frame 2850 showed sprite tile 17 instead of 33. Investigation proved native C runs the intro ~4 frames faster than the interpreter (NMI 2671 vs 2675), so the boy's charging animation was simply 4 frames ahead. | Verified intro sequence dynamically over 12,300 frames (`tools/verify-intro.js`); game modes `0x0DE1` and `0x0DDD` run smoothly and loop cleanly into Cycle 3. | When testing animated scenes, align on game events or NMI counters rather than arbitrary host frame indices. |
| **Sprite Corruption, Overlapping Arm, Missing Portraits & Hair Holes in Attract Demo** | Emitting root `func` directives for dynamic sprite allocator routines (`$80:960A`, `$80:9685`, `$80:96BB`) and direct OAM draw routines (`$80:9033`, `$80:9040`, `$80:8FEE`) caused snesrecomp to merge instructions across `$9040..$96FE` into a single 5,700-line mega-function while compiling `$96BB`'s `JSL $809040` / `PLY` sequence with cross-routine host call frames. Native stack pops desynchronized from the emulated 65816 hardware stack, corrupting sprite allocation indices in `$7E:5127` and `$7E:50AD`. The allocator erroneously assigned body sprite tiles to VRAM slot `0x80` (the HUD portrait slot), resulting in corrupted portrait hair, overlapping arm chunks, dog sprite vertical artifacts, and disappearing HUD portraits when offscreen tiles were assigned. | `func` lines in `recomp/seed-exclusions.txt` keep the 6 dynamic sprite allocator routines from being emitted as root `func` lines. Running the sprite allocator in the LLE interpreter preserves exact 65816 stack pushes/pulls, keeping OAM buffer `$7E:ABDE` and VRAM allocations 100% clean across all cycles and scenes. | Tightly coupled routines with internal non-standard stack frames (`PHX; JSL; PLY`) and mutual control flow must not be split into separate native function roots; keep the cluster in the interpreter. |

---

## 3. Upstream Bug Reports & Contributions

These issues should be submitted to the [RetroPortingToolKit/snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) repository:

1. **BG Mode 4 Support in `NewRenderer`:**
   - **File:** `runner/src/snes/ppu.c` (`PpuDrawBackgrounds`).
   - **Issue:** Modes 4, 5, and 6 fall through to Mode 7 logic. Secret of Evermore uses Mode 4 for its
     name input screen.
   - **Resolution:** Port Mode 4 drawing logic from `ppu_legacy.c` into `ppu.c`. Once merged, our
     `SoeAfterConfig` hook can be safely removed.
2. **File save states omit the CPU and the beam driver's resume point:** `RtlSaveSnapshot` /
   `RtlLoadSnapshot` (`runner/src/common_rtl.c`) serialize `snes->cpu`, not `g_cpu`, and
   `beam_frame_driver.c` keeps `s_resume_pc` / `s_wai_halted` as statics with no accessor. Any game
   on the beam driver loses its place on load. Our glue works around it (§2); upstream should
   save both and drop the `booting` path when a state was loaded.
3. **Access-exact timing** (closes the open MesenCE differences): [upstream-access-timing.md](upstream-access-timing.md).
4. **Stack-Frame Context Loss in `interp_tier_dispatch_tail`:**
   - **Issue:** When an indirect jump index exceeds the directive count inside a function that has
     allocated stack-frame variables (e.g. via `PEA` or `PHA`), transitioning to the interpreter
     fallback can misalign the stack frame, causing corrupt return addresses or register loads.

---

## 4. Prioritized Recomp Roadmap

```text
 ┌────────────────────────────────────────────────────────────────────────┐
 │ [COMPLETED] Milestone 1: Headless Intro & Attract Loop Across Cycles   │
 └───────────────────────────────────┬────────────────────────────────────┘
                                     ▼
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 2. Title Screen & Room 0 Input Automation (press start, name boy/dog)  │
 └───────────────────────────────────┬────────────────────────────────────┘
                                     ▼
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 3. Deep In-Game Lockstep Verification (Podunk village physics & scripts)│
 └───────────────────────────────────┬────────────────────────────────────┘
                                     ▼
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 4. First HLE C Function Replacements (hle_func for LZ decompressor)    │
 └───────────────────────────────────┬────────────────────────────────────┘
                                     ▼
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 5. Upstream PRs (BG Mode 4 in NewRenderer, interpreter stack fallback) │
 └────────────────────────────────────────────────────────────────────────┘
```

### Milestone 1: Headless Intro & Attract Mode Across Cycles (COMPLETED)
- Verified over 12,300 frames (~3.4 minutes emulated) via `tools/verify-intro.js`.
- Boot -> Squaresoft logo -> Intro story cutscene -> Title screen ("Press START") -> Attract demo (Thraxx fight).
- Loops cleanly from Cycle 1 to Cycle 2 (~frame 6164), runs Cycle 2 with identical 100% clean sprites/OAM, and loops into Cycle 3 (~frame 12250).
- Excluded stack-resident loop dispatcher `$8F:9F46` and dynamic hook `$80:84BE` in `recomp/seed-exclusions.txt`.

### Milestone 2: Automated In-Game Playthrough
- Write a headless script that presses Start at the title screen, accepts default names, and loads
  the opening cutscene and first playable room (`room_00`, Podunk).
- Extend `tools/compare` (recomp `press` input from a replay) to verify WRAM integrity across player movement and weapon swings.

### Milestone 3: High-Level Emulation (HLE) Replacements (`hle_func`)
- snesrecomp supports replacing any recompiled routine with a clean C implementation using `hle_func <pc16> <c_func_name>`.
- Replace the leaf routines first:
  1. LZ decompressors (`src/games/evermore/maps/` already has verified TypeScript decoders).
  2. Room tilemap loader and collision unpacker.
- Lockstep WRAM comparison validates that hand-written C behaves identically to the original 65816 assembly.

### Milestone 4: Broaden Static CDL Coverage
- The CDL covers 90.3 % of the ROM since 2026-10-30, enough for now ([cdl-and-seeds.md](cdl-and-seeds.md)).
  Ghidra / REA exports (`static.json`) could seed the rest; more native code is speed, not correctness.

### Open tasks (2026-10-30), most important first
1. **Re-verify on the current framework pin** (`ef0b3ff`): `tools/verify-intro.js`, the 21 fights in
   room `0x38`, save states. Only the save load and the hut -> village walk were checked on it; the
   pin moved far and native code grew from 1237 to 4871 variants.
2. **Publish**: settle `LICENSE` (the scaffold's names its author), then push `main` and `develop`.
3. **Attribute the rest of the MesenCE difference** (§1.1): what remains in an all-interpreter run after the
   missed NMIs; the 208 flagged pushed-address returns ([imperfections/README.md](imperfections/README.md)).
4. **Replays as regression tests**: drive the recomp with everscript-vscode's 86 recorded replays
   and compare at game events (room entered, fight over), not frame numbers (milestone 2).


