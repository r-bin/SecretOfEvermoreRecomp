# 4. Compiled resume

**Status:** draft, no PR yet. Was built on [PR 3](03-bus-exact-compiled-code.md), which is obsolete: upstream's region-paced emission
replaces it. Being moved onto region-paced code (below): suspended frames work (38 % native, in sync), frameless entries stall.

Branch: `aot-resume` in the fork, up to the fix `09cf15ec` (first written as `25d2f42f`, `88bc5ef8` on an older main, not published).

This one is not needed for sync. It is what makes the native build native: without it most of a run stays in the interpreter.
Replay 01, native share of instructions: **16.31 % → 99.70 %** (`SNESRECOMP_TIER_STATS=1`, resume off and on). If that is not worth the
size of the change upstream, this PR is the one to drop.

## On region-paced code (prototype, 2026-11-11)

Fork branch `aot-resume-region-paced` (local, on `develop`): `bdcdb6a2`, `252760a1`. What ties resume to bus-exact code is one condition
in `emit_function.py` (`bus_exact.resume_enabled()` needs `SNESRECOMP_EXACT_BUS=1`); the switch per instruction, the resume table
and the runtime are general. The prototype:

- enables resume with `--region-paced` and `SNESRECOMP_AOT_RESUME=1` (`bus_exact.resume_requested()`), the switch in the bank cache key;
- has each region-paced instruction that stays compiled report itself as bus-exact code does (`cpu_aot_insn_begin`: tier stats and the
  bridge's PC hook, which our frame driver's NMI rule reads);
- runs an instruction compiled code handed back in place (`interp_tier_dispatch_tail` with target = site, region-paced's fallback for
  what it does not model) once in the interpreter before resuming there again. Without it the reset code never ran a compiled
  instruction: resume and fallback took turns at `$80:8037`.

Replay 01, main `e9c20732` + our stack, region-paced, ours off, built against the fork checkout (`SNESRECOMP_ROOT`):

| Resume | Replay 01 | Native share |
|---|---|---|
| off | in sync | about 13 % (57.6 M interpreted) |
| suspended frames only (`SNESRECOMP_AOT_RESUME=2` at run time) | **in sync** | **38.26 %** (24.3 M compiled, 39.2 M interpreted) |
| with frameless entries (`=1`) | **stalls** in the reset code: `$80:8034` (`LDA #0`) runs again and again, no NMI | — |
| bus-exact + resume, for comparison | in sync | 99.65 % |

Open: why a frameless entry at `$80:8037` (`TCD`, after `TXS / PEA $0000`) sends the run back to `$80:8034`. Next step is a trace of
the resume path there (`SNESRECOMP_CYC_WATCH=808030-808045` shows the repeats). Once frameless entries work, measure the share again;
the gap to 99.65 % is what decides whether region-paced code needs more of what it now hands back.

## To do before opening

1. Move `$4212`'s check to #247's `snes_set_cpu_read_clocked` scope. Our test of `g_aot_access_prefix` does the same job; upstream's
   way is the one to keep. Simply having no polling tick for compiled code was identical on the five replays of 2026-11-05 (`ABL_TICK_UPSTREAM`).
2. Take out the outermost beam sync ([Not proposed](README.md#not-proposed)).
3. Carry the test stubs, `d56d8da9` on the fork's `develop` (done 2026-11-07: `interp_bridge_aot_rti` in the shared harness of
   `tests/v2/test_hle_func_seam.py`, the resume symbols in `tests/interp816/bridge_test.c`). Add a resume test to `tests/v2/`.
4. Squash the three commits.

## PR title

Compiled resume: continue compiled after a deadline, in every instruction of a variant

## PR text

```markdown
With instruction-exact compiled code (#<PR 3>) a CPU slice can stop at any instruction. Today the scheduler then interprets the rest of
the routine, and its callers after its return, until the next call bounces. On Secret of Evermore's attract replay that leaves 16.31 %
of instructions compiled.

`SNESRECOMP_AOT_RESUME=1` at generation gives each compiled variant a switch in its prologue (`g_aot_resume_table` in `dispatch_v2.c`),
so code a slice stopped in goes on compiled. The scheduler re-enters compiled frames from records taken when they exit to it (S, entry
S and return frame). `SNESRECOMP_AOT_RESUME=0` at run time turns it off. Off by default.

Native share on that replay: 99.70 %.

This only helps the native share. It fixes no game behaviour and no sync: with resume off, both committed replays stay identical to
MesenCE, just mostly interpreted. If that is not worth this much code in the runtime, it is fine to leave it out.

What keeps the tiers alike:
- A compiled RTI goes on at the popped PC while the scheduler owns the run (`interp_bridge_aot_rti`), so handlers can run as slices.
- The beam is synced before compiled code reads PPU, joypad and `$42xx` registers.
- A `$4212` read from compiled code takes no polling tick, through #247's clocked read scope.
- The JSL envelope restores PB after a normal return even with an unwind pending. #248 keeps the live PB for a suspended continuation;
  with resume the flag can still be set when a resumed callee returns, and the caller then ran on in the callee's bank.

Result: Secret of Evermore identical to MesenCE on two replays from power-on, 32,348 NMIs, with and without resume. Without the PB line the attract
replay parts at NMI 2938. Recipe: https://github.com/r-bin/SecretOfEvermoreRecomp/blob/<head>/docs/pr/README.md#reproduce

Doubts:
- The speed gain is not measured: 99.70 % counts instructions, not time.
- It is built on `bus_exact.py` from #<PR 3>. If that gives way to #250/#251, this has to be redone on top of them, and their fallbacks to
  the interpreter may lower the share.

Depends on #<PR 3>.
```

## Required

Replay 01, on current main, native. The interpreter does not run any of this.

On the committed replays (2026-11-07), native; first NMI where WRAM parts:

| Change | Off | Replay 01 | Replay 02 |
|---|---|---|---|
| Resume itself | `SNESRECOMP_AOT_RESUME=0` | in sync (native share 16.31 % on the old replay 01) | in sync |
| Compiled RTI yields at the popped PC | `ABL_RTI_YIELD` | 2 | 2 |
| Beam sync before register reads | `ABL_BEAM_READS` | 3 | 3 |
| No `$4212` polling tick | `ABL_TICK4212` | 2 | 2 |
| PB restored after a normal return | `09cf15ec` left out | 2938 on the old replay 01, 6 clocks short (needs a rebuild, not rerun) | not run |

With `SNESRECOMP_AOT_RESUME=0`, RTI yield and beam sync stay in sync on both replays: they are needed only with resume, which is
why they are here and not in PR 3, and why the compiled-interrupt commit is no PR of its own. The `$4212` tick is not quite that:
without resume it is unseen on 01 but parts 02 at NMI 19685, so compiled code needs it with or without resume. It may belong in
PR 3.

The PB fix was found while rebasing: on our pin the old epilogue restored PB always, and #248 changed that. A resume entry setting PB
from its target turned out not to be needed (`ABL_RESUME_PB`, the five replays of 2026-11-05), so it is not in the fix.

Also in `25d2f42f`, not tested alone: a 16-bit access charges its second byte at its own region's speed, and the operand's last byte is
on the data bus before an access. Both are generator changes with no switch. They need a regenerate to test, or they move to PR 3.

## Not game specific?

Yes: mechanism only, no addresses. It assumes a host that runs CPU slices through the scheduler with a deadline, as ours does. Off by
default at generation.

## Good for future games?

Only for the native share, and only for hosts that want instruction-exact timing and compiled speed together. It makes no game more
correct. Hosts with block-level timing gain nothing and pay nothing.

## Reproduce

Replay 01. Setup: [README.md](README.md#reproduce), steps 1-6. Native share:
```sh
SNESRECOMP_TIER_STATS=1 node tools/compare/probes/tier-stats.mts <rom> tools/compare/replays/01-no-inputs-1-loop.mmo 8592
SNESRECOMP_AOT_RESUME=0 SNESRECOMP_TIER_STATS=1 node tools/compare/probes/tier-stats.mts <rom> tools/compare/replays/01-no-inputs-1-loop.mmo 8592
```
Expected: `99.70 % native`, `16.31 % native`. The PB fix: check out `44ae0bd2`, the commit before it, regenerate and build: step 6 stops at
`wram differ at counter $0100 = 2938`. The other rows need [ablation-gates.patch](ablation-gates.patch).
