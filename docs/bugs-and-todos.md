# Recomp Bugs, Findings, and Roadmap

A comprehensive record of open issues, resolved bugs, upstream limitations, and prioritized
next steps for the Secret of Evermore static recompilation.

---

## 1. Open Bugs & Active Investigations

### 1.1 Timing Against MesenCE (the reference)
- **Status:** ~99 % (2026-10-30): WRAM equal through NMI key 300, 98.3–99.95 % equal up to key 5800 on the
  attract replay, the rest from known framework limits. One file per known difference, with the
  measurements: [imperfections/README.md](imperfections/README.md).
- **Branch `snesrecomp-spc-access-cycles` (2026-10-31, 2026-11-02):** both replays (01 attract, 02 new game)
  identical at every NMI key, all interpreted; glue alone stopped at 319. Since 2026-11-03 native too, to the end of
  both replays (snesrecomp `aot-bus-exact`, [compiled-cycle-charges.md](imperfections/compiled-cycle-charges.md); the
  beam after long DMAs, [short-scanline.md](imperfections/short-scanline.md)). Table: [progress.md](progress.md). Lessons and the A/B per framework switch:
  [mesence-parity-lessons.md](mesence-parity-lessons.md).
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
| **Compiled resume parted from the interpreter (2026-11-03): four causes** | Resuming compiled code mid-routine (snesrecomp `aot-resume`) ran code compiled that native runs had always interpreted, and exposed four places where compiled code was not the interpreter: (1) compiled code never advanced the beam, so a slice that ended in compiled code left it behind and the frame driver started the auto-joypad window late (`$4016` read before the auto-read latch, 02 parted at key 3399); (2) a 16-bit data access charged both bytes at the first byte's region speed (`LDA $0000,Y` across a speed boundary at `$8C:CACD`, 2 clocks); (3) compiled code never put its instruction fetches on the data bus, so `$2137` (open bus) read the last data byte, not the operand's `$21`, and the attract demo's RNG seed at `$80:81A1` differed (01 parted at 5998); (4) resume ignored `SNESRECOMP_LLE_BOUNCE=0` and the interpreter target file, so the "all-interpreted" reference was partly native and agreed with broken builds. | In snesrecomp `aot-resume`: beam synced when a compiled bounce returns and before every PPU, joypad and `$42xx` read; the word's second byte charged at its own address; `cpu->open_bus` set to the operand's last byte before each data access without a pointer; resume off under the interpreter levers. RTI-ending variants get no resume entries (RTI drops the popped PC). | A tier switch is only free when both tiers keep the *whole* machine alike, not only registers and clocks: beam, open bus and host-side device state count. Check the reference run is really what it claims (the A/B probe agreed with a broken build until the levers were honoured). |
| **Exclusions lifted (2026-11-03)** | Eight of the nine lines of `recomp/seed-exclusions.txt` (the raycast loop dispatch `$8F:9F46`, the sprite allocator cluster `$80:8FEE`–`$80:96BB`, the script scheduler `$8C:CFFD`) kept about a third of each replay interpreted. Each guarded a compiled-code bug found before bus-exact code, compiled resume and the tier-switch fixes above; none of them reproduces now. | Lines removed (`dispatch 8084BE` stays: it sits in the NMI handler, which is not compiled). Verified: native and all-interpreted builds WRAM-identical at every NMI key of 01 and 02, the native build in sync with MesenCE to the end of both, a save loads into room `0x51` and the Boy walks. Native share 72.3 % → 97.2 % (01), 62.6 % → 96.7 % (02). | An exclusion is a workaround with a date: after a framework or driver change that touches how tiers meet, try the build without each one (native vs interpreted A/B is the cheap test), and keep only what still fails. |
| **Compiled NMI handler parted at NMI 2, then crashed (2026-11-03): three causes** | Seeding the NMI body `$80:823E` (reached by its vector stub's `JML`, so never a root) moved ~1.5 % of each replay to compiled code and broke the run: (1) the export wrote the NMI hook `PHK; PEA $84C0; JML [$0125]` at `$80:84BE` as `ptrcall … frame:2` and the old `dispatch 8084BE` exclusion made compiled code charge that `JML` and then hand its own PC to the interpreter, which ran it again (42 clocks twice); (2) a compiled `RTI` discarded the popped PC and returned to the host, which is right under `interp_bridge_run_interrupt` but not for our NMI, which the frame driver runs as an ordinary scheduler slice: the scheduler went on at an address read off the stack (the 02 replay ended in `$5E:1F1x`); (3) every compiled `$4212` read advanced the beam by the static-recomp polling tick (64 clocks, `snes.c`), so the NMI tail's joypad wait put H 64 clocks ahead of the beam clock and the next slice's refresh grid moved (first visible as a 40-clock refresh stall in the idle loop). | (1) everscript-vscode's export writes `frame:3` when a `PHK` precedes the `PEA`; the exclusion is gone. (2) snesrecomp `aot-resume`: a compiled RTI calls `interp_bridge_aot_rti`, which yields to the scheduler at the popped PB:PC when the scheduler owns the run (and records no suspended frame); RTI variants get resume entries. (3) the polling tick only for code without a bus-exact access prefix; the beam is also synced when the outermost bridge run returns to the host. The export seeds `nmi_handler_body` / `irq_handler_body` from the vectors' `JML` stubs. | Code that was always interpreted hides the compiled tier's leftovers from older timing models (the `$4212` tick): before moving a hot routine to compiled code, A/B it against the interpreter, then bisect with `clock-trace.mts` and a beam print per instruction (`g_interp_bridge_pc_hook`). |
| **408 interpreter-only variants were mostly the seeds' own doing (2026-11-03)** | Two export errors, both settled by the CDL library: (1) an exit width taken from a MODIFIED return (the shadow stack matched the frame but control went elsewhere: an RTL in bank `$90`) gave `script_eval_expression` `$8C:EA43` the exit set M0X0,M1X0. The analyzer then decoded the code after each `JSL $8C:EA43` with 8-bit A, so `CMP #$0002` became a 2-byte instruction plus a `BRK`, and ~50 script handlers were `unproven_call` or `brk_at` poison. The CDL saw every one of those call sites run with 16-bit A, and the routine return normally only as M0X0. (2) The indexed-dispatch count took the highest table word equal to an observed target: `jsr ($C3FF,x)` at `$8F:AC98` has 8 handlers, then code, and word 105 matched by chance, so 106 entries seeded garbage targets (`$8F:0000`, `$8F:8900`, ...) and hundreds of decodes in the mirror banks. | everscript-vscode export (v0.387.32): exit widths from NORMAL returns, MODIFIED only for an entry variant with no normal one; a table ends where its bytes ran as code. `I_NMI` (the `$00:823A` stub) compiled (`symbols.toml` emit = true). Interpreter-only: 408 → 74 (11 in the banks the game runs). | Before blaming the analyzer for `unproven_*` / `brk_at_*`, check the seed's claim against the CDL's per-instruction widths (`ext` M16/X16 bits) at the call site: a width the CDL never saw there is a seed error. |
| **Soft reset: black screen, NMI counter stuck at 0 (2026-11-03)** | The Reset hotkey and a script's `reset` call snesrecomp's `RtlReset`, which resets the machine and the legacy core's registers (`SnesEnterNativeMode`) but not `g_cpu`, the registers the bridge and compiled code run on; and its `session_reset` hook (ours clears the frame driver's resume PC) runs only on a content-variant change. The driver resumed at the old PC on reset hardware, and with the resume PC cleared the reset handler's first `lda.b #$8F` still ran with the game's 16-bit accumulator, read a 16-bit operand and ran off into data (`$00:888F`, `$21:1A08`). Found by the first reset test (900 frames after power-on vs after a reset). | `hardware_reset` in `src/game_rtl.c`: a 65816 reset of `g_cpu` (emulation, M = X = I = 1, D = DB = PB = 0, stack in page 1) and the driver's resume PC cleared. `tests/reset_equals_boot.sh` (ctest): at the same NMI count VRAM, CGRAM and OAM equal power-on's; it fails without the fix. | Test the paths a player uses but no replay takes (reset, save-state load). Two copies of the CPU registers mean two resets: check which one the code you call clears. |

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
3. **Access-exact timing** (closes the open MesenCE differences): [upstream-access-timing.md](upstream-access-timing.md);
   prototype on snesrecomp branch `spc-access-cycles`.
4. **An empty `$420B` write is charged as a DMA:** `snes.c` bills `12 + Σ` even when no channel is set; hardware
   starts nothing. Evermore's reset code writes 0 there before its first WRAM clear, which put the CPU 18 clocks
   behind MesenCE (found 2026-10-31 by MesenCE's exec clock at the first `STA $420B`: 2934 against the recomp's
   2952; lesson: compare the absolute clock at the first event of a kind before fitting a formula to many).
5. **Stack-Frame Context Loss in `interp_tier_dispatch_tail`:**
   - **Issue:** When an indirect jump index exceeds the directive count inside a function that has
     allocated stack-frame variables (e.g. via `PEA` or `PHA`), transitioning to the interpreter
     fallback can misalign the stack frame, causing corrupt return addresses or register loads.
6. **Function names choose resume entries:** `emit_aot_resume_table` (`recompiler/v2/program_emit.py`, branch
   `aot-resume`) gives an instruction several variants hold to the one with the most instructions, and on a tie
   to the first by **name**. Renaming is therefore not cosmetic. Found 2026-11-03 with the `symbol` names from
   everscript-vscode: after normalising names back to `bank_XX_XXXX`, all 315 bank files were identical, but
   312 resume entries in the mirror banks `$0C`/`$4C`/`$CC` moved between two equal-sized variants (e.g.
   `$0C:D224`: `$0C:D20F` before, `$0C:D217` after). The game runs those routines from `$8C`, whose names did
   not change. Fix upstream: break the tie by entry PC. Lesson: an A/B of names must compare
   `dispatch_v2.c` too, not only the bank files.
7. **Every HiROM mirror of the code is compiled.** snesrecomp emits each routine in all four banks that map its
   bytes (`$0C`/`$4C`/`$8C`/`$CC`, ...): 233 of the 317 files in `src/gen` hold the `$0x`/`$1x`/`$4x`/`$5x`/
   `$Cx`/`$Dx` copies. Counted per bank on both sync replays (glue `SOE_BANK_STATS=1`, everscript-vscode
   `tools/compare/probes/run-stats.mts --stats`, 2026-11-03): every instruction ran in `$80` and `$8C`-`$91`,
   except one per NMI in `$00` (the native NMI vector's `JML` stub) and the reset's first five. No instruction
   ran in any other mirror. Proposal upstream: a cfg directive naming the banks code runs in (or "the banks with
   a cfg"), so the other mirrors are left to the interpreter as code outside the seeds already is. It would cut
   the generated C and its compile time by roughly 70 %. Until then nothing changes here: an unseeded mirror
   that did run would still be correct, only interpreted.
8. **`getenv` on every interpreter entry:** `_interp_run_core` (`runner/src/snes/interp_bridge.c`) reads
   `SNESRECOMP_WLOG_STATE` each call without caching it: about 1 % of the main thread in a native run (macOS
   `sample`, 2026-11-03; docs/progress.md, "Speed"). Cache it in a static as the neighbouring switches do.
9. **`RtlReset` does not reset `g_cpu`:** `runner/src/common_rtl.c` resets the legacy core's registers
   (`SnesEnterNativeMode`) but not the `CpuState` the bridge and compiled code run on, and `session_reset` is not
   called on a console reset. Every game on the beam/generic drivers keeps its widths and resume PC through a
   reset; ours fixes it in `hardware_reset` (§2, "Soft reset"). Upstream: put `g_cpu` into the 65816 reset state
   in `RtlReset`, and call `session_reset` there.

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


