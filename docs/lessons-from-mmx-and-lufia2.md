# Lessons from Mega Man X Recompiled and Lufia II Recompiled

Two other snesrecomp ports, read on 2026-10-21 to find what makes a port accurate and what our
Secret of Evermore recomp should copy. Both are PolyForm Noncommercial: this page describes their
ideas in our words; no code, cfg lines or data were copied.

| Port | Revision read | Framework |
|---|---|---|
| [mstan/MegaManXSNESRecomp](https://github.com/mstan/MegaManXSNESRecomp) | `b5c0841` (2026-10-20) | `RetroPortingToolKit/snesrecomp` pinned as a submodule at `7a67b85` (2026-10-19) |
| [Cellenseres/Lufia2SNESRecomp](https://github.com/Cellenseres/Lufia2SNESRecomp) | `510fa68` (2026-10-20) | its own fork, `Cellenseres/snesrecomp-platform`, plus a separate decomp library `Cellenseres/Lufia2Decomp` |

Our checkout of snesrecomp (`../snesrecomp`, unpinned `main`) was at `7786708` (2026-10-06), 77
commits behind MMX's pin. MMX is by the snesrecomp author, so it is where the framework's
practices start; most of the lessons below come from the framework docs it pins (`docs/*.md` in
snesrecomp at `7a67b85`).

---

## 1. Summary

MMX is accurate because it treats the interpreter as the definition of correct and everything
else as something that has to be proven against it. Lufia 2 applies the same idea to
hand-decompiled C: no decompiled function replaces ROM code until a differential test proves it
equal to the interpreter.

What we should take, highest value first (§4 has the details):

1. Use the interpreter as the oracle. Compare the recomp with itself (interpreted against native)
   before comparing it with snes9x. That avoids the timing drift that stops our lockstep at NMI 315.
2. Pin snesrecomp, then update it, then check which of our hand-made exclusions are still needed.
3. Run the framework's runtime tripwires in every headless run (M/X claim, DB, stack drift,
   phantom PC, off-rails). Our worst bugs so far were stack-drift bugs.
4. Discover code with the recomp's own coverage capture during a long playthrough, in addition
   to the snes9x CDL.
5. Record replays all the time and replay them in regression tests that stop at the first
   divergence.
6. Before the decompiled C in `docs/disassembly/` replaces any routine, prove it the way Lufia 2
   does.

---

## 2. Mega Man X: what it does

### 2.1 The contract: LLE first

The README and the framework's `LLE_FIRST_ANALYSIS.md` state one rule. The ROM, run by the
interpreter (LLE), is the correctness model. A compiled (AOT) body is an *exact, proven*
optimization of it, and an HLE replacement is an optional overlay that can be turned off.
Anything the analysis cannot prove keeps running on the interpreter, "loudly".

The framework analyses functions as `(pc24, entry M, entry X)` nodes and classifies each one:

| State | Meaning |
|---|---|
| `LLE_ONLY` | reachable and valid, but not safe or not worth compiling; runs on the interpreter |
| `AOT_ELIGIBLE` | entry state, extent, return contract and code-generation facts are all proven |
| `AOT_EMITTED` | an eligible node chosen for compilation |
| `HLE_OVERLAY` | an optional replacement; turning it off must expose the correct code underneath |

At runtime, an M/X combination without an exact compiled body goes to the interpreter at the
original address. It never falls back to the nearest compiled sibling. An unresolved indirect
jump keeps *live* interpreter dispatch and does not need a per-game target table. Hand-written
hints are treated as migration tests: each one is removed once the generic path handles that case.

**For us.** Several entries in `src/platform/cdl/recomp-analysis.ts` are hand-written workarounds
for exactly these classes: `DISPATCH_EXCLUSIONS` for the stack-resident loop at `$8F:9F46` and
the dynamic hook at `$80:84BE`, and `FUNC_EXCLUSIONS` plus `force_lle` for the sprite allocator
cluster (bugs-and-todos.md §2). A newer framework may classify them as `LLE_ONLY` itself. Each
one should become a test: remove it, regenerate, and see whether the bug comes back.

### 2.2 Seeds: few, explained, and evidenced

MMX's `recomp/bank*.cfg` hold very little: `auto_vectors`, 17 `func` lines, 5 `hle_func`, 1
`hle_dispatch`, `tier_down_stubs`, and 1,402 non-promoting `symbol` names imported from a
disassembly project. Everything else is found by the analyzer. Every hand-written line has a
comment that gives the root cause, the date, and how it was found. For example, a task entry is
declared because the installer passes it as an `LDA #imm` operand that the analyzer cannot see,
and the comment says what broke while it was missing.

`tier_down_stubs` is worth knowing about: a call into a bank that is not translated runs the real
bytes on the interpreter instead of being dropped. The comment says "Dropping real code is never
faithful".

**For us.** We generate hundreds of roots from the CDL. That is fine, but each hand-made
directive (exclusions, `force_lle`) should carry the same kind of evidence comment in the
generated cfg, so a reader of `project/recomp/` can see why it is there.

### 2.3 Coverage capture: discovery from the recompiled game itself

The framework can record where the *recompiled* game falls back to the interpreter: which call
site, which target, which entry M/X, how often, in which frames (`COVERAGE_FEEDBACK.md`; MMX keeps
one such profile as `recomp/tier2_coverage.json`). An ingest tool turns that into candidate roots
for the next generation. The profile is bound to the ROM's SHA-256 and to the build's manifest;
an old profile only supplies seeds and says nothing about whether a new build is correct. MMX's
coverage report (`docs/validation/coverage-2026-09-28.md`) is honest about it: the added roots
gave "no coverage or performance gain", and a seeming jump from 4,708 to 5,309 entries was
duplicate mirrors and was rejected.

**For us.** Our seeds come from the snes9x CDL recorder. Once a long playthrough runs on the
recomp, its coverage capture tells us directly which interpreter fallbacks are frequent, and it
works in the native build where snes9x timing does not matter. The two sources complement each
other: the CDL records what the game executes, and the capture records where the native build
falls back to the interpreter.

### 2.4 Disassembly authority

`DISASSEMBLY_AUTHORITY.md` feeds an independent disassembly (one that reassembles to the exact
ROM) into the analyzer as constraints on instruction boundaries and code/data separation. An
audit then reports any data executed as code, any entry into the middle of an operand, and any
length conflict. The committed form is `disassembly-layout.json`: addresses, lengths, code/data
boundaries and source hashes, **without ROM bytes**. The doc also lists what a disassembly cannot
prove: CPU state, host hooks, interrupt scheduling and audio timing still need runtime validation.

**For us.** Our Asar export already reassembles byte for byte, and the ROM tab knows asset
regions. A layout file in this format would let snesrecomp use our knowledge without ROM bytes in
the repo. Only 41 % of our asset regions currently reach snesrecomp as `data_region`. Caveat:
the code boundaries in our export come from the same CDL that seeds the recomp, so they are not
independent for code. The asset regions decoded by the ROM tab are independent evidence for
data.

### 2.5 Names must not change the program

MMX imports names from a third-party disassembly project (`symbols/README.md`). The import adds
only non-promoting `symbol` lines: it creates no functions and moves no boundaries. To prove that,
the program is generated with and without the overlay, and the execution manifests and normalized
C (names, comments and declaration order removed) must be identical. This check was run across
X1–X3.

**For us.** `tools/recomp/names.js` writes our curated names into the cfgs. The same A/B check
(regenerate without names, normalize, compare) would show whether naming ever changes what gets
compiled.

### 2.6 Runtime tripwires

The generated program has always-on checks (`TRIPWIRES.md`), readable over the debug TCP port.
Each one latches on the first divergence and keeps a full state capture:

- **M/X claim check:** a function variant runs with flags other than its name claims (`_M1X0`).
- **Async M/X write:** the flags changed without any emitted SEP/REP/PLP/RTI/XCE.
- **DB tripwire** and **stack-drift tripwire.**
- **Phantom PC trap**, **WRAM watch slots**, **off-rails detector.**

**For us.** The `$8F:9F46` OAM corruption and the sprite-allocator corruption were both
host-stack/emulated-stack desyncs that we found from visible damage several frames later. A
headless run that fails on the first stack-drift trip would have pointed at the routine
directly. `debugging.md` should make "tripwires armed" the default for headless runs.

### 2.7 The validation ladder

MMX and the framework never call a build correct because one comparison passed. They run several
comparisons, each against something that is known to be good:

| Comparison | What it proves |
|---|---|
| A vs A (the same build twice) | the run is deterministic, so later diffs mean something |
| bounced vs interpreted (`SNESRECOMP_LLE_BOUNCE`) | the compiled bodies equal the interpreter |
| LLE scheduler vs HLE scheduler | the HLE overlay equals the ROM code it replaces |
| before vs after a change, the same 12,000-frame routes (attract, scripted gameplay) | the change keeps guest state, presented pixels, VRAM/OAM/CGRAM and audio |
| owner playtest | what automation cannot see; reported separately from automated coverage |

Rules from `DISASSEMBLY_AUTHORITY.md` and `DEBUG_ROUTES.md`:

- Compare continuous runs, never paused or stepped ones. Stepped captures are observations, not proof.
- A run with every compiled entry denied is not a valid interpreter oracle when the port relies
  on hooks (it broke Super Metroid's boot).
- "Matching finite routes is evidence for those routes." The control can contain the same bug.
- Replaying a broken sequence can match every hash. The Eagle lift softlock did. Regression
  tests must check the resulting gameplay as well as hashes.
- Do not load old save states into a changed build in place of a fresh boot.

**For us.** Our lockstep compares the recomp with snes9x, and snes9x timing makes it drift at
NMI 315 even with every routine interpreted (bugs-and-todos.md §1.1). So the external emulator
is the wrong primary oracle for "is the compiled code right". Interpreted against native inside
snesrecomp has no timing gap and checks exactly that. Use snes9x and Mesen2 for hardware
questions only.

### 2.8 Replays always on, and tests that pin each bug

- Every normal play records inputs, machine checkpoints, settings and per-frame state hashes. The
  recording is bounded (one hour, 256 MiB) and written on a background thread. A replay tool runs
  it headless and stops at the first WRAM, VRAM or camera mismatch, writing
  `replay-divergence.sav`. Bug reports are replays, not descriptions.
- Each fixed bug gets a test that fails before the fix. One example is `tests/reset_boot_test.py`:
  frames after a soft reset must be byte-identical to the same frames after power-on. The bug it
  pins was a host-side "booted" flag that survived the reset.
- CI builds a ROM-free host and runs every ROM-free test. ROM-backed tests are opt-in
  (`-DMMX_STATE_TESTS=ON`).

**For us.** Our sample playbacks (`sample-playbacks` skill) already do this for the emulator.
The recomp needs the same: a replay recorded in the recomp, replayed headless after each
regeneration, stopping at the first divergence. Our save-state lesson (load at launch *and*
mid-game, then check the game advances) is the same kind of pinned test and should become one.

### 2.9 HLE of control flow is a trap

MMX first replaced its cooperative task scheduler (`$00:8099`, coroutines switched with
`TCS`/`RTS`) with C fibers, one per task slot. That works, but it is HLE that defines semantics,
and it needed per-game task-PC tables. `LLE_SCHEDULER.md` records the move back: the real
scheduler runs on the interpreter, and task bodies run compiled through the "bounce". It took
three gates (A vs A, bounced vs interpreted, LLE vs HLE video state for 4,000 frames). The live
stutters blamed on the scheduler were three APU threading defects.

**For us.** Do not reach for `hle_func` to work around control flow such as yields, coroutines or
stack-resident loops. Keep those on the interpreter and let the bounce run their callees
compiled. `hle_func` is for understood leaf routines (our Milestone 3: the decompressors). The
framework's `HLE_FUNC.md` says the same: "Do not use it to hide a recompiler bug", and
`--no-hle` must always regenerate the faithful build for A/B.

### 2.10 Mods as restorable hook layers

Gameplay mods (widescreen, co-op, weapons) patch the generated C through scripts
(`tools/apply_*_hooks.py`) that inject marked, idempotent snippets. `--restore` strips them and
gives back the pristine output. Hooks change a *verdict* at a consumer (for example the
off-screen cull test) rather than the state a producer writes. An earlier widescreen attempt that
biased the camera snapshot froze scrolling, because another routine diffs that snapshot against
the live camera. Some hooks force specific PCs onto the interpreter so the mod can observe them.
With every mod off, the game is the vanilla build.

**For us.** This fits `snesmod-addons.md` and `enhancements.md`. Two lessons: patch the
comparison, not the stored state, and keep "all mods off" equal to vanilla as a tested property.

### 2.11 ROM identification as UX

The launcher refuses the wrong revision and says which dump the user picked and which one is
needed, with hashes. It recognizes other regions, collection extracts, patched ROMs and archives.
Our ROM library (`games-and-roms` skill) does the identification; the recomp's play scripts
should give the same kind of message.

---

## 3. Lufia II: what it does differently

Lufia 2 runs on its own fork of the framework, and its notes are long, dense reports. Its cfgs
are small and commented like MMX's (`bank82.cfg` declares one pointer dispatcher, `$05:8F1B` is
kept on the interpreter because it builds a PEA/PHA/RTS call frame, and the README explains why
there is no `bank81.cfg`).

The part that matters is a **decomp layer bound onto the recomp**:

- A separate library of hand-decompiled C (`lib/lufia2-decomp`) is bound to ROM entries one by
  one in `recomp/decomp_bindings.toml` (address → bridge function), about 1,900 bindings.
- Each binding is proven before it is selected. Real entry states are captured from the
  interpreter, either with a desktop capture build (`docs/decomp/CAPTURE.md`: a hotkey records
  entry/exit CPU state, WRAM snapshots and bus traces for chosen entries) or with generated
  states. The C runs both directly and through the production ABI. The test compares CPU, full
  WRAM, MMIO and the *ordered* data accesses, and it covers every child call site and every
  return.
- **Mutation controls:** finite deliberate mutations of the ROM routine must be *detected* by the
  same tests. This proves the test can fail, which a passing comparison alone does not.
- "No-access guards" check that the C reads and writes nothing the original does not.
- Every build writes `LUFIA2_FUNCTION_SOURCES.txt`: which routines run as decomp C, which
  candidates stayed on the recomp, which are HLE. A reference-only build mode exposes the library
  without activating it.
- Each report says what is *not* claimed (live timing, full regression, internals of peers it
  stubbed). `ISSUES.md` lists open gaps plainly, for example "save states are disabled until the
  hybrid CPU resume context is serialized", which is the same problem we fixed with the `SEXC`
  chunk.

What to avoid: the tests and fixtures are private ("private tests and fixtures stay
uncommitted"), so nobody else can reproduce the proofs, and the forked framework drifts from
upstream. The reports are also very hard to read.

**For us.** `docs/disassembly/` holds 330 functions decompiled to C (PROGRESS.md), as code
blocks in Markdown. None of it is compiled or bound into the recomp, and none passes a
differential test against the interpreter. Lufia 2 shows how a decompiled routine can start to
count:

1. Capture real entry states for the routine from the interpreter (our CDL recorder or the
   recomp can do this).
2. Make the C compile against the framework's `CpuState`, and bind it with `hle_func`.
3. Compare CPU, WRAM and ordered bus accesses against the interpreter on those states.
4. Mutate the ROM routine and check that the tests catch it.
5. Keep the fixtures reproducible from the user's own ROM, so the tests can be committed without
   ROM bytes.

Until a routine passes this, call its C a description, not a decompilation.

---

## 4. What to do, in order

| # | Step | Why | Where |
|---|---|---|---|
| 1 | Pin snesrecomp: record the SHA in `tools/recomp/`, refuse another or dirty checkout unless a flag allows it, and move to a current revision | Builds are reproducible; MMX's pin is 77 commits ahead of ours | `tools/recomp/host.js`, `setup.md` |
| 2 | Arm the tripwires in every headless run; fail on the first trip | Stack and M/X bugs surface at the routine, not frames later | `debugging.md`, `tools/recomp/` |
| 3 | Make interpreted vs native (bounce on/off) the primary lockstep; keep snes9x/Mesen2 for hardware questions | Sidesteps the NMI-315 timing drift | `tools/compare`, bugs-and-todos.md §1.1 |
| 4 | Turn each exclusion in `recomp-analysis.ts` into a test: drop it on the new framework, regenerate, rerun the bug's repro | Hints are migration tests; the framework may now prove these `LLE_ONLY` itself | `src/platform/cdl/recomp-analysis.ts` |
| 5 | Record replays in the recomp and replay them after each regeneration, stopping at the first divergence; pin the save-state and reset cases as tests | Regression evidence that survives timing changes | `tools/recomp/`, `sample-playbacks` |
| 6 | Run a long playthrough with coverage capture on; ingest it as seeds bound to the ROM SHA | 394 variants still interpreted; the CDL used so far covers ~10 s | `setup.md`, `README.md` §1 |
| 7 | Export a ROM-byte-free layout (asset regions as data) for the framework's authority input | Gets ROM-tab data regions into snesrecomp | `tools/recomp/`, ROM tab model |
| 8 | A/B check that names do not change the emitted C | Naming must stay cosmetic | `tools/recomp/names.js` |
| 9 | Prove decompiled C before binding it (§3) | Turns `docs/disassembly/` into verified code | `decomp-curator` agent |

Each step needs its own session and its own evidence. None of them was started with this page.
