# Hunting recomp bugs: method, findings, open issues

How we track down wrong pictures and behaviour in the snesrecomp build of SoE
(`npm run recomp` → `out/recomp/SecretOfEvermore-<os>-<arch>/`), what we found on 2026-10-12, and
what is still open. Setup and tools: [recomp-host.md](recomp-host.md).

## 1. The method

**1. Reproduce headless.** The host runs from an input script. Everything up to the attract demo
needs no input, because the demo plays itself (Thraxx fight around frame 2850). Menus need
`press start` / `press a` / `press b`.

```text
turbo on
wait 2250
dump t          # picture + WRAM/VRAM/CGRAM/OAM/registers/PPU-write log into SNESRECOMP_DUMP_DIR
quit
```

**2. Get the reference picture.** Run the same frames (and the same button presses) in our snes9x
core (`src/emulator/core/snes9x2005-wasm-vanilla`, loaded under node). Line both runs up on the
NMI frame counter `$7E0100`, not on frame numbers: the two emulators start counting differently
and drift a few frames apart. `tools/recomp/lockstep.js` does this for WRAM.

**3. Split framework from our code.** This is the most useful single step. Build the project once
**without our CDL seeds**: remove `recomp/bank8x/9x.cfg` and the data-region block, run
`tools/regen.sh` without `--cfg-roots`, build into a second build dir. Everything then runs in
snesrecomp's interpreter.
- Bug also there → framework or host (renderer, timing, glue).
- Bug gone → our seeds made some native C wrong.

**4. Narrow it down with the dumps.**
- `*.vram.bin` vs snes9x VRAM: are the graphics there at all?
- `*.regs.json`: PPU mode, OBSEL, layer enables.
- `*.ppuw.tsv`: every PPU register write with its frame and line. Dump every frame to find when
  the game sets a mode.
- `*.oam.bin`: which sprite tiles are drawn.

**5. Bisect the seeds** (when step 3 points at them). Keep all `func` lines, drop all
`indirect_dispatch` lines, and test. Then halve the suspicious set until one line remains, and
confirm with "all lines except that one". Each test is regen + incremental build + headless run,
about 1.5 minutes. It needs an automatic pass/fail check, for example "OAM tiles at y=105 are
232/234/236/238" for the "Press START" screen.

## 2. Findings

### 2.1 Character-name screen garbled: framework renderer (fixed by glue)

- Also broken without seeds, so not our code. VRAM matched snes9x.
- The PPU-write log showed the game switching to **BG mode 4** (`$2105 = $04`, BG1 8bpp).
- snesrecomp's optimized renderer handles modes 0–3 and draws everything else as mode 7.
- Fix: our glue forces the reference renderer (`SoeAfterConfig`, hooked into `main.c` by
  `tools/recomp/host.js`). See [recomp-host.md](recomp-host.md) §3.

### 2.2 "Press START" screen with garbage sprites: under-counted jump table from the CDL (fixed)

- Clean without seeds, so our seeds were at fault.
- VRAM differed in the sprite area (`OBSEL = 3` → `$C000–$FFFF`). The OAM held wrong tile numbers
  (128/10/12 instead of 232/234/236/238).
- Bisect: all 402 `func` lines are fine. One `indirect_dispatch` line breaks it:
  `indirect_dispatch 9f46 8 idx:X` for `jmp ($A3D8,x)` at `$8F:9F46`.
- The table at `$8F:A3D8` has **16** entries; the CDL had only seen 0–7. Entries 8–15 are shared
  with a second dispatcher (`jmp ($A3E8,x)` at `$8F:9ECA`), and the title screen uses them. With
  `count = 8`, snesrecomp sends the other indices to its interpreter fallback, which goes wrong
  at this site (it sits right after `lda $03,s`, so probably a stack-frame issue).
- Fix in the seed generator (`staticTableCount` in `src/emulator/cdl/recomp-analysis.js`): the
  observed count is the minimum. Keep counting while an entry is a same-bank code pointer, the
  table bytes were never executed and are not read by a non-dispatch instruction (the next table:
  `lda $8FA3F8,x`), and the target is not recorded as data only. A sibling `jmp/jsr (abs,X)`
  may share the bytes.
- Result: 27 tables grow; native variants go from 682 to 758; "Press START" and the name screen
  are correct.

**Answer to "would a complete CDL fix it?"** For 2.2, yes: a recording that had seen indices 8–15
at `$8F:9F46` would have produced the right count. The static scan now covers that gap for tables.
For 2.1, no: that one was in the framework.

### 2.3 What snesrecomp does with an incomplete target list

| Directive form | Unknown runtime target | Risk |
|---|---|---|
| `idx:X` (jmp/jsr `(abs,X)`), index ≥ count | interpreter fallback (`interp_tier_dispatch_tail`) | should be safe, but went wrong at `$8F:9F46` |
| `ptrcall` (pointer CALL, e.g. our `$80:84BE`) | **handler skipped**, as if it returned at once | silent behaviour change |
| `ptrtail` | `default: break` | silent behaviour change |

Our seeds still carry one `ptrcall` with observed targets only (`$80:84BE`, a call through a WRAM
pointer, `$0125` per the seed comment; 1 observed target `$80:858B`, which `$80:8507` stores at
`$012B–$012D`, so probably the same hook). It did not cause 2.2, but it is the same class of risk.

## 3. Still open

1. **Thraxx (attract demo, ~frame 2850): sprites flicker and show wrong tiles** (the boy, the HUD
   portraits). Clean in the build without seeds, so our seeds again. Not solved yet:
   - "funcs only" (no dispatch lines) is not clean either, so this time it may be a `func` line:
     wrong entry or exit widths from the CDL, or a function boundary.
   - The pixel-diff check against the interpreter build does not work here. With native code the
     demo plays out slightly differently (the boy attacks instead of charging), so whole frames
     differ even when drawn correctly.
   - Next: a check that does not depend on demo timing, e.g. sync RNG + WRAM between the two
     builds before the fight (`poke` from a dump), then compare WRAM frame by frame
     (lockstep native vs interpreter, both snesrecomp). The first diverging address points at
     the faulty routine; bisect `func` lines by bank, then by function.
2. **Pointer dispatch** (`ptrcall` at `$80:84BE`): either drop it from the seeds, or make sure all
   targets are listed. Scenes that set their own NMI callback would otherwise skip it.
3. **Timing drift vs snes9x** from NMI counter ~315 (intro fade / loading). Probably harmless,
   but it limits lockstep against snes9x to the first ~5 seconds. snes9x 2005 is not
   cycle-accurate either; a second reference (Mesen2) would tell which side is off.
4. **Upstream:** report the mode-4 renderer gap to snesrecomp (then drop our renderer hook), and
   the `$8F:9F46` fallback behaviour (index ≥ count inside a function with stack data).
5. **Tooling:** the seed bisect harness lives in the session scratchpad. It should become
   `tools/recomp/bisect` (seed subsets → regen → build → scripted run → pass/fail), with the
   pass/fail checks above as presets.
6. **CDL coverage** (9.3 %) remains the root of these risks: every seed is "observed only". The
   static table scan is one guard; exit widths and function boundaries have none yet.

## 4. Known addresses from this hunt

| Address | What |
|---|---|
| `$8F:9F46` | `jmp ($A3D8,x)`, 16-entry dispatch; entries 8+ used on the title screen |
| `$8F:9ECA` | `jmp ($A3E8,x)`, shares entries 8–15 of the same table |
| `$8F:A3D8` | the shared jump table (in `tables.js`) |
| `$8F:A3F8` | table read by `lda $8FA3F8,x` at `$8F:9F4B` (in `tables.js`) |
| `$80:84BE` | `ptrcall` through a WRAM pointer (one observed target `$80:858B`; `$80:8507` stores that value at `$012B–$012D`) |
| `$2105 = $04` | the character-name screen's BG mode 4 (BG1 8bpp) |
