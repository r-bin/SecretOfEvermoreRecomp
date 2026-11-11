# 4. Compiled resume on region-paced code

**Status:** open, https://github.com/RetroPortingToolKit/snesrecomp/pull/283 (2026-11-11), from the fork's branch `aot-resume-main`: one commit, `e8c37cc1`, and a merge of
main `80a41241` (`ef46b5da`; one conflict: paired continuations now keep the caller's bank, kept beside resume's return paths).
Rechecked on that main: replays 01 and 02 in sync native and interpreted, 94.05 % native, pytest 638 passed. **Kind: performance
only**: no emulated state changes (identical with resume on, off and all-interpreted); more of region-paced code runs compiled.
Related open PR: [#276](https://github.com/RetroPortingToolKit/snesrecomp/pull/276) (pogoflow: region-paced code yields at H/V IRQ
latches, more stops that resume continues after); not yet run together. Depends on
nothing open: it builds on upstream's region-paced emission (#250/#251, merged). Our build runs it since 2026-11-11 (`develop-rp` in
the fork: main + this + PRs 2, 5, 7 + tier stats).

First written on our bus-exact code ([PR 3](03-bus-exact-compiled-code.md), obsolete): `aot-resume` (`246a8e6c`, `44ae0bd2`,
`09cf15ec`). Moving it onto region-paced code found three bugs that bus-exact code never hit, now fixed in the commit:

| Bug | Seen as | Fix |
|---|---|---|
| Region-paced code hands an instruction it does not model back to the interpreter at its own PC; resume sent it straight back | The reset code ran no compiled instruction at all | The interpreter runs such an instruction once before resuming there |
| The prologue's stack-window fallback (S in `$1FFB-$6004`) hands back the variant's entry, which a resumed frame has run past | `$80:8034` (`LDA #0`) ran again and again, no NMI | With a resume pending it hands back the resume point |
| A frameless frame's normal return (bound C that ran its own RTL) went on at an address read from the assumed frame | Replay 02 parted at NMI 20526: `$8C:B5E9 JML` into the bound `$80:8650`, back at `$8C:E38D`, not `$8C:ADE4` | Read the address the return popped (2 bytes: RTS, 3: RTL) |

## Required?

Not for sync: with resume off both replays stay identical to MesenCE, only mostly interpreted. It is what makes region-paced code
native, measured on main `e9c20732` + our stack, `-O1`, M4:

| Region-paced code | Replay 01 native share | Replay 02 native share | Replay 02, wall time |
|---|---|---|---|
| without resume | about 13 % (57.6 M interpreted) | — | 93 s |
| with resume | **94.05 %** | **92.43 %** | **79 s** (15 % faster end to end) |

Wall time is the whole run (PPU, APU, WRAM hashing at every NMI), so the CPU part gains more than 15 %. For comparison, our bus-exact
code with resume had 99.65 % native: region-paced code hands MMIO and some pointer modes to the interpreter.

Sync with this commit: replays 01 and 02 identical to MesenCE 2.2.1 at every NMI (WRAM, VRAM, CGRAM, OAM), native, with resume
off, and all-interpreted. `ctest` (8 tests) passes. Upstream's `pytest tests/v2 tests/test_snes_cycles.py`: 637 passed (main: 622,
plus the region tests' new stubs and `test_aot_resume.py`).

## PR title

Compiled resume on region-paced code (generate --aot-resume)

## PR text

As posted on #283.

````markdown
## Summary

With `--instruction-blocks --region-paced`, compiled code checks the master-clock deadline before every instruction, so a CPU
slice can stop at any instruction. The scheduler then interprets the rest of the routine, and its callers after its return,
until the next call bounces. A host that slices often (DRAM refresh points, vblank edges, single steps around NMI enables)
ends up mostly interpreted: about 13 % of Secret of Evermore's instructions ran compiled.

`--aot-resume` gives each region-paced variant a resume switch, so code a slice stopped in goes on compiled:

```diff
 deadline reached in compiled code
   unwind to the scheduler
+  record each abandoned frame (S, entry S, return contract)
 scheduler at PC
-  interpret until the next call bounces
+  frame recorded at this S?  re-enter that variant at PC (cpu_dispatch_resume_paired)
+  no frame known?            enter as a frame one below S; its RTS/RTL hands back the popped PC
```

Off by default; the flag is part of the generation identity only when on (existing recipe digests do not change).
`SNESRECOMP_AOT_RESUME=0` at run time turns it off, `=2` keeps suspended frames only.

What keeps the tiers alike:
- An instruction compiled code hands back in place (unmodeled, MMIO, a pointer it cannot preview) runs once in the
  interpreter before resuming there again.
- The prologue's stack-window fallback hands back the resume point, not the variant's entry.
- A frameless frame's normal return (a bound routine that ran its own RTS/RTL) goes on at the address it popped.
- A compiled RTI goes on at the popped PC while the scheduler owns the run, so interrupt handlers resume too.
- The JSL envelope restores PB after a normal return even with an unwind pending.
- Each region-paced instruction that stays compiled reports itself to `g_interp_bridge_pc_hook` (`cpu_aot_insn_begin`),
  as the interpreter does.
- The beam is synced before compiled code reads PPU, joypad and `$42xx` registers.

Related: #276 adds yields at H/V IRQ latches to region-paced code; each such stop would otherwise hand the rest of the
routine to the interpreter, so the two complement each other. The compiled RTI here also covers what #251's Secret of Mana
run needed as a test-only addition (resumable interrupt handlers).

## Evidence

Secret of Evermore (USA), two MesenCE 2.2.1 movies from power-on (8,592 and 23,756 NMIs), WRAM, VRAM, CGRAM and OAM
compared at every NMI, nothing copied or masked, on main `80a41241`:

- **Before** (`--instruction-blocks --region-paced`): both replays identical; about 13 % of instructions compiled; the
  second replay runs in 93 s.
  **After** (`--aot-resume` added): both replays identical; 94.05 % / 92.43 % compiled; the second replay runs in 79 s
  (15 % faster end to end, PPU and APU included). Also identical with resume off at run time and all-interpreted.

`pytest tests/v2 tests/test_snes_cycles.py`: 638 passed. New: `tests/v2/test_aot_resume.py` (a resume entry per instruction,
off by default, needs region pacing, the fallback hands back the resume point, the resume table keeps the longest variant,
the flag check). The region-pacing harness gets stubs for the two resume helpers and `cpu_aot_insn_begin`.

Recipe: https://github.com/r-bin/SecretOfEvermoreRecomp/blob/<head>/docs/pr/README.md#reproduce

## Merge Danger

**Door:** two-way. Opt-in at generation; off at run time with `SNESRECOMP_AOT_RESUME=0`.

**Blast Radius:** small for projects that do not pass `--aot-resume`. Two changes reach everyone using region pacing or the
bridge: region-paced instructions now call `cpu_aot_insn_begin` (a hook call, no timing change), and the beam is synced
before compiled register reads.

Who gains: projects that pass `--region-paced` and whose host stops the CPU often (per-line slices, single steps around
interrupts, IRQ-latch yields as in #276). With `beam_frame_driver`'s few stops per field the gain is small; without region
pacing nothing changes.

Limits: measured on one game; the speed gain is one replay's wall time. Not yet run together with #276. Frameless entries
assume a frame one below S; a routine that returns from such an entry in an unusual way goes back to the interpreter at
the popped PC.
````

## Reproduce

Setup: [README.md](README.md#reproduce), steps 1-6 (`tools/regen.sh` generates with `--instruction-blocks --region-paced --aot-resume`).
Native share, and resume off at run time:
```sh
node tools/compare/probes/tier-stats.mts <rom> tools/compare/replays/01-no-inputs-1-loop.mmo 8592
SNESRECOMP_AOT_RESUME=0 node tools/compare/probes/tier-stats.mts <rom> tools/compare/replays/01-no-inputs-1-loop.mmo 8592
```
Expected: `94.05 % native`; with resume off the same run, mostly interpreted.
