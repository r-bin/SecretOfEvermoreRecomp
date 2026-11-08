# 3. Bus-exact compiled code

**Status:** draft, no PR yet, **blocked on a decision.** Depends on [#241](01-access-exact-timing.md).

Branch: `aot-bus-exact` in the fork, `b8eed84c` (first written as `9a4dc97c` on an older main, not published), without its nested-deadline part ([Not proposed](README.md#not-proposed)).

## The decision

Open PRs [#250](https://github.com/RetroPortingToolKit/snesrecomp/pull/250) and [#251](https://github.com/RetroPortingToolKit/snesrecomp/pull/251)
by `rages4calm` add `--instruction-blocks --region-paced`: one block per instruction, fetches priced by region, data through the paced
helpers. That is nearly the same contract as our `recompiler/v2/bus_exact.py`. Two ways on:

- **Build on #250/#251 (recommended).** Try their flags for this game; send only what is missing. Upstream gets one mechanism, not two.
- **Send ours.** It is proven on the replays of 2026-11-05 and on both committed replays. It would compete with a PR already in review.

Not tested yet: whether their flags keep Secret of Evermore in sync, and what would be missing. Our resume ([PR 4](04-compiled-resume.md))
hangs off `bus_exact.py`, so it would have to move too. Their docs mention MMIO and bank-boundary cases that return to the interpreter;
how often that happens here decides whether PR 4 still reaches its native share.

## To do before opening

1. Make the decision above.
2. Make the generator switches part of the output's identity. On main, `v2_emit` reuses published output without looking at
   `SNESRECOMP_EXACT_BUS` or `SNESRECOMP_AOT_RESUME`: a regenerate with the switch changed prints `0 bank(s) emitted, 28 reused` and
   leaves the old code in place. #250 does this for its flags, in `tools/generation_recipe.py`.
3. Carry the test stub, `87961ea1` on the fork's `develop` (done 2026-11-07: `g_aot_access_prefix` in
   `tests/dma/hdma_timing_test.c`, which `tests/v2/test_rdnmi_ownership.py` links).
4. Check read-modify-write and word accesses that straddle regions. This game has none.

## PR title

Bus-exact compiled code: charge each instruction as the interpreter does

## PR text

```markdown
Compiled blocks are billed `cycles x code speed`, before their accesses. So compiled and interpreted runs of one game drift apart,
and anything that depends on exact clocks (sound handshakes, H/V reads) only works interpreted. On Secret of Evermore a compiled run
parts from MesenCE at frame 320.

Generated with `SNESRECOMP_EXACT_BUS=1`, compiled code keeps the interpreter's timing instruction for instruction:
- Each instruction is its own CFG block, so the deadline is checked where the interpreter checks it.
- An instruction charges 6 x its cycles plus (region speed - 6) per bus transfer, with D, S and the effective address at run time.
- An instruction accessing memory through an operand charges after its effect and publishes the clocks up to its data access
  (`g_aot_access_prefix`), so an I/O register sees the same clock as under the interpreter.
- Each compiled instruction calls the host's per-instruction hook (`g_interp_bridge_pc_hook`), for frame drivers that judge the last
  instruction before an interrupt.

Off by default: without the switch the emitter, the interpreter and existing builds behave as before.

Result: Secret of Evermore's attract replay, compiled, identical to MesenCE on WRAM, VRAM, CGRAM, OAM and SRAM to its end, 14,305 frames.
Recipe: https://github.com/r-bin/SecretOfEvermoreRecomp/blob/<head>/docs/pr/README.md#reproduce

Doubts:
- This overlaps #250 and #251, whose `--instruction-blocks --region-paced` define nearly the same clock contract. I have not tested
  their flags on this game, so I can't say what would be missing. If they cover it, I'd rather rework this on top of them than add a
  second mechanism.
- It only helps compiled code: the interpreter already keeps these clocks.
- Read-modify-write and word accesses that straddle regions are untested; this game has none.

Depends on #241.
```

## Required

Native; the interpreter does not use this code. First NMI where WRAM parts:

| Change | Off | Replay 01 | Replay 02 |
|---|---|---|---|
| The whole change | regenerate with `SNESRECOMP_EXACT_BUS=0 SNESRECOMP_AOT_RESUME=0` | 320 (the old replay 01, 2026-11-05; needs a regeneration, not rerun) | not run |
| Per-instruction hook in compiled code | `ABL_PC_HOOK_AOT` | 334 with resume, 6840 without | 334 with resume, 1979 without |

## Not game specific?

Yes: the charges come from the 65816's bus cycles and the region map, as in the interpreter.

## Good for future games?

Yes, but only for compiled code, in games that need exact timing and compiled speed. An all-interpreted build stays in sync without it.
Compiled code needs this or something like it to pass a frame-by-frame comparison; #250/#251 may be that something. If they land, future
games get it from them instead, which is why the decision comes first.

## Reproduce

Replay 01. Setup: [README.md](README.md#reproduce), steps 1-6: in sync through 14424. Then, deleting `src/gen` because of the reuse above:
```sh
rm -rf src/gen
SNESRECOMP_EXACT_BUS=0 SNESRECOMP_AOT_RESUME=0 bash tools/regen.sh --rom /path/to/rom.sfc
cmake --build build -j
```
Check the log says `28 bank(s) emitted`. Step 6 then stops at `wram differ at counter $0100 = 320`. Regenerate the same way, without
the switches, to go back. The hook needs [ablation-gates.patch](ablation-gates.patch).
