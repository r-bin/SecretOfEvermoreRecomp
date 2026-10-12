# Recomp Bugs, Findings, and Roadmap

A comprehensive record of open issues, resolved bugs, upstream limitations, and prioritized
next steps for the Secret of Evermore static recompilation.

---

## 1. Open Bugs & Active Investigations

### 1.1 Thraxx Attract Demo Sprite Flicker & Wrong Tiles (~Frame 2850)
- **Status:** **Open** (Highest priority gameplay bug).
- **Symptoms:**
  During the automatic attract demo playback of the Thraxx boss fight (~frame 2850), sprites for
  the boy and the HUD status portraits flicker and show wrong tile numbers (e.g. tile 17 instead of tile 33/0x21).
- **Findings to Date:**
  1. **Clean in Interpreter-Only Mode:** Running without seeds (`regen.sh` without `--cfg-roots`) renders
     the demo cleanly. This proves the bug is **not** an upstream PPU renderer or timing defect; it is
     caused by our native CDL seeds generating flawed C code.
  2. **Persists with "Funcs Only":** Disabling all `indirect_dispatch` lines does not resolve it. The fault
     is located within a `func` seed line:
     - An incorrect function entry width ($M/X$),
     - An incorrect exit width specialization,
     - Or an erroneous function boundary splitting a routine in half.
  3. **Attract Demo Timing Divergence:** With native C execution, subtle sub-cycle differences cause
     the AI input script to diverge slightly: the boy attacks instead of charging his weapon. Because of
     this, simple pixel-diff checks against the interpreter build produce false positives.
- **How to Fix / Next Steps:**
  1. **Bank-Level Bisection:** Use `tools/bisect.js` to isolate which specific bank (`bank80`, `bank8F`, `bank90`)
     introduces the sprite corruption.
  2. **Automated OAM Assertion:** The test harness in `tools/bisect.js` already asserts on the
     specific corrupted sprite tile in OAM at frame 2850:
     ```javascript
     // Sprite 2 tile: 33 (0x21) in reference, 17 in broken seeded run
     const tile2 = oam[2 * 4 + 2];
     const pass = (tile2 === 33);
     ```
  3. **Synchronize State Before the Fight:** Create a headless script that pauses before the Thraxx scene
     (~frame 2700), synchronizes RNG and WRAM via `poke` from a reference save state, and runs lockstep
     native vs interpreter frame-by-frame. The very first divergent WRAM address will pinpoint the exact
     routine at fault.

---

### 1.2 Pointer Dispatch Risks (`ptrcall` at `$80:84BE`)
- **Status:** **Open Risk**.
- **Symptoms / Mechanism:**
  At address `$80:84BE`, the game performs an indirect call through a WRAM pointer at `$7E0125`.
  Our CDL export currently emits an `indirect_dispatch` directive with only one observed runtime target:
  `$80:858B` (stored at `$7E012B..$7E012D` by `$80:8507`).
- **The Architectural Hazard:**
  snesrecomp handles unrecognized targets in `ptrcall` directives by **silently skipping invocation**
  (`default: break`). Unlike jump tables (`idx:X`), which fall back to the interpreter, an unlisted
  pointer call behaves as if the handler immediately returned!
  If a cutscene or menu installs a new pointer hook that the CDL never recorded, the callback will silently fail.
- **How to Fix / Next Steps:**
  1. Audit the ROM for all stores to `$7E0125` / `$7E012B` to construct an exhaustive static target list.
  2. Inject all valid targets into `targets:` in the seed generator (`recomp-analysis.js`).
  3. If exhaustive targets cannot be guaranteed, remove the `ptrcall` directive entirely and allow
     snesrecomp to handle this site via safe interpreter dispatch.

---

### 1.3 Timing Drift Against snes9x After NMI Counter ~315
- **Status:** **Open Investigation**.
- **Symptoms:**
  WRAM comparison against `snes9x2005-wasm` matches bit-for-bit through NMI counter 305.
  - At counter 315: 4 bytes differ (`$7E0106` brightness shadow, `$7E0128`).
  - At counters 325–335 (intro fade and asset decompression): divergence spikes to 500–2,100 bytes.
  - At counters 345–479: divergence settles at 70–190 bytes (mostly shifted RNG sequence).
- **Findings to Date:**
  - **Identical Across Recomp Modes:** The pure interpreter-only build and the 682-native-variant build
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
| **"Press START" screen had corrupted sprites** | Jump table `jmp ($A3D8,x)` at `$8F:9F46` has 16 entries. CDL only saw entries 0–7. Fallback interpreter broke because the instruction accesses stack data (`lda $03,s`). | Implemented `staticTableCount` in `recomp-analysis.js`: statically scans contiguous code pointers from the table base. Native variants increased from 682 to 758. | Static table analysis must complement runtime CDL logging for shared jump dispatchers. |

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
 │ 1. Fix Thraxx Sprite Corruption (bisect bank cfgs via tools/bisect.js) │
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

### Milestone 1: Resolve Thraxx Attract Demo Bug
- Run automated `tools/bisect.js` to identify the single offending `func` directive.
- Correct the $M/X$ entry/exit width or function boundary in `recomp-analysis.js`.

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
