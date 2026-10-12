# Recomp Bugs, Findings, and Roadmap

A comprehensive record of open issues, resolved bugs, upstream limitations, and prioritized
next steps for the Secret of Evermore static recompilation.

---

## 1. Open Bugs & Active Investigations

### 1.1 Timing Drift Against snes9x After NMI Counter ~315
- **Status:** **Open Investigation**.
- **Symptoms:**
  WRAM comparison against `snes9x2005-wasm` matches bit-for-bit through NMI counter 305.
  - At counter 315: 4 bytes differ (`$7E0106` brightness shadow, `$7E0128`).
  - At counters 325–335 (intro fade and asset decompression): divergence spikes to 500–2,100 bytes.
  - At counters 345–479: divergence settles at 70–190 bytes (mostly shifted RNG sequence).
- **Findings to Date:**
  - **Identical Across Recomp Modes:** The pure interpreter-only build and the native-variant build
    diverge from snes9x at the **exact same frame counter by the exact same byte counts**.
  - This proves the divergence is **not** caused by recompiled C code. It is an artifact of
    **CPU cycle timing differences** between snes9x2005 and snesrecomp's beam driver during loops
    where NMI is disabled (`$7E0F3A != 0`).
- **How to Fix / Next Steps:**
  1. Compare execution against **Mesen2** (cycle-accurate) to determine which emulator is deviating from
     real SNES hardware timings.
  2. The drift is currently harmless for gameplay, but resolving it will extend automated lockstep verification
     deep into later rooms.

---

## 2. Resolved Bugs & Lessons Learned

| Bug | Root Cause | Fix Applied | Architectural Lesson |
|---|---|---|---|
| **Character-name screen rendered as noise** | Screen uses **BG Mode 4** (`$2105 = $04`, 8bpp planar). snesrecomp's default `NewRenderer` only implements modes 0–3, falling back to mode 7. | Injected `SoeAfterConfig()` hook into `src/main.c` setting `g_config.new_renderer = false` to force the legacy reference renderer. | Do not assume visual artifacts originate from code recompilation; always test pure interpreter mode first. |
| **"Press START" screen corrupted in Cycle 2 ("Press : RT")** | Directive `indirect_dispatch 9f46 16 idx:X` at `$8F:9F46` (`jmp ($A3D8,x)`) inside `func_CF9F05`. The loop counter is kept on the 65816 stack via `pha`/`pla` at `$A3B1`. Emitting `indirect_dispatch` generated a native tail call (`RecompStackPop(); return _r;`), popping the native C host call stack frame on each iteration while the emulated SNES stack remained looped. This stack desync corrupted WRAM `$7E:ABDE` (OAM) in Cycle 2. | Added `DISPATCH_EXCLUSIONS` in `src/emulator/cdl/recomp-analysis.js` to exclude `$8F:9F46` (and `$CF:9F46`). Falling back to interpreter executes the loop coherently with no mismatched native pops. Both Cycle 1 and Cycle 2 are 100% clean. | Indirect jump directives must not be emitted for jump sites that are internal to a stack-resident loop. |
| **NMI Dynamic Hook Silent Skip Risk (`$80:84BE`)** | Dynamic hook `JML [$0125]` called via `PHK; PEA $84C0` (3-byte return frame, not 2). Emitting `indirect_dispatch ... ptrcall` with only 1 observed target causes snesrecomp to silently skip unobserved runtime callbacks. | Added `0x8084BE` (and `0x0084BE`) to `DISPATCH_EXCLUSIONS` in `src/emulator/cdl/recomp-analysis.js`. | Do not emit static pointer dispatch directives for dynamic hook vectors with incomplete target observations. |
| **Thraxx Attract Demo False-Positive (Frame 2850)** | Comparing raw byte OAM at fixed frame 2850 showed sprite tile 17 instead of 33. Investigation proved native C runs the intro ~4 frames faster than the interpreter (NMI 2671 vs 2675), so the boy's charging animation was simply 4 frames ahead. | Verified intro sequence dynamically over 12,300 frames (`tools/verify-intro.js`); game modes `0x0DE1` and `0x0DDD` run smoothly and loop cleanly into Cycle 3. | When testing animated scenes, align on game events or NMI counters rather than arbitrary host frame indices. |

---

## 3. Upstream Bug Reports & Contributions

These issues should be submitted to the [RetroPortingToolKit/snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) repository:

1. **BG Mode 4 Support in `NewRenderer`:**
   - **File:** `runner/src/snes/ppu.c` (`PpuDrawBackgrounds`).
   - **Issue:** Modes 4, 5, and 6 fall through to Mode 7 logic. Secret of Evermore uses Mode 4 for its
     name input screen.
   - **Resolution:** Port Mode 4 drawing logic from `ppu_legacy.c` into `ppu.c`. Once merged, our
     `SoeAfterConfig` hook can be safely removed.
2. **Stack-Frame Context Loss in `interp_tier_dispatch_tail`:**
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
- Excluded stack-resident loop dispatcher `$8F:9F46` and dynamic hook `$80:84BE` in `src/emulator/cdl/recomp-analysis.js`.

### Milestone 2: Automated In-Game Playthrough
- Write a headless script that presses Start at the title screen, accepts default names, and loads
  the opening cutscene and first playable room (`room_00`, Podunk).
- Extend `lockstep.js` to verify WRAM integrity across player movement and weapon swings.

### Milestone 3: High-Level Emulation (HLE) Replacements (`hle_func`)
- snesrecomp supports replacing any recompiled routine with a clean C implementation using `hle_func <pc16> <c_func_name>`.
- Replace the leaf routines first:
  1. LZ decompressors (`src/maps/` already has verified TypeScript decoders).
  2. Room tilemap loader and collision unpacker.
- Lockstep WRAM comparison validates that hand-written C behaves identically to the original 65816 assembly.

### Milestone 4: Broaden Static CDL Coverage
- Use Ghidra / REA headless exports (`static.json`) to seed unplayed routines, raising native C
  coverage from ~10% toward 100%.


