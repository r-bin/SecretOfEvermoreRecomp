# What a Great Recomp Needs: Gap Analysis Against Mega Man X Recompiled

How our port compares with [mstan/MegaManXSNESRecomp](https://github.com/mstan/MegaManXSNESRecomp)
(read at `bc2894b`, 2026-11-01), what we still lack to be called a great recomp, and how the
knowledge in everscript-vscode can close each gap. MMX is PolyForm Noncommercial: this page
describes its ideas in our words. No code, cfg lines, data or screenshots were copied.

The accuracy lessons (LLE-first contract, tripwires, validation ladder, coverage capture, proven
decomp bindings) are already in [lessons-from-mmx-and-lufia2.md](lessons-from-mmx-and-lufia2.md)
(read 2026-10-21) and are not repeated here. This page covers what came after: the gap between
"correct" and "great".

---

## 1. Summary

**Accuracy on what we have measured is not our gap. Players and proof of the whole game are.** Our native build
plays five replays with all memory identical to MesenCE at every NMI and 99.9 % of their instructions outside the
interpreter ([progress.md](../progress.md)), but that is about 17.5 minutes of distinct play. MMX measures itself against its
own interpreter, not an outside reference, and it interprets its task scheduler on purpose.

MMX is a great recomp because it is a **product**: people download it, play it start to finish,
turn mods on and off, and send in replays when something breaks. We have none of that. We have
no release, no test suite, no CI build, and no proof that the game can be finished. We have no
player features beyond save states.

| Criterion | MMX | Us (2026-11-04) | Gap |
|---|---|---|---|
| Matches a reference | own interpreter (A vs A, bounced vs interpreted, LLE vs HLE) | MesenCE on five replays: all memory, picture but for fade rounding, S-DSP state per frame | none on what is measured |
| Native share | interprets its scheduler by design (~11 M interpreted instructions per 12,000-frame route) | 13 k to 236 k interpreted per replay | none |
| Reproducible from the public repo | framework pinned, upstream is its author's | GitHub lists no branches yet, and sync needs four snesrecomp branches that are not upstream | **large** |
| Whole game proven | owner playtests plus 12,000-frame routes; issues get replays | replays reach Prehistoria; `03-complete-run-7h` does not sync | **large** |
| Released | Windows ZIP, Linux AppImage, Steam Deck folder, launcher | `release.yml` and setup pack written; no tag, `main` is the scaffold | **large** |
| Tests and CI | 63 test files, `ctest`, ROM-free CI on every push, ROM tests opt-in | 4 `ctest` tests, no CI; the full replay set runs by hand (`node tools/compare/full.mts`) | **large** |
| Bug reports | always-on replay recording, session logs, crash reports | none | medium |
| Player features | widescreen, co-op, netplay with rollback, MSU-1, rewind, run-ahead, save-state browser, boss rush, character and weapon mods | save states (F1) | **large** |
| Mods | 15 packages, hook layers, all off = vanilla | `mods/preloaded/` empty; [snesmod-addons.md](snesmod-addons.md), [enhancements.md](enhancements.md) are plans | **large** |
| Performance | measured FPS per route, accuracy-preserving burndown, LLE reference build kept | 368-500 frames/s measured on Apple Silicon, one machine | small |
| ROM handling | refuses wrong revisions with a message naming the dump and the right one | refuses wrong ROMs; US only | small |
| Contributor path | `CONTRIBUTING.md`, `bootstrap.sh`, pins checked | `setup.md`, pins checked by `release.yml` | small |

---

## 2. The gaps, in detail

### 2.1 Reproducibility: the headline result must build from `main`

The MesenCE sync in [progress.md](../progress.md) is measured on snesrecomp branches
(`spc-access-cycles`, `ppu-render-reads`, `aot-bus-exact`, `aot-resume`). Our rule is never to
patch the framework, so nobody who clones this repo can get that result yet. MMX never has this
problem because its author owns the framework.

What great looks like: `framework_pins.txt` names upstream commits, and the pinned build passes
the two sync replays. Until then, the README must say which pins the numbers belong to.

**How:** [upstream-access-timing.md](upstream-access-timing.md) is the proposal. Every change in
it has a file in [imperfections/](../imperfections/README.md) and an A/B measurement
([mesence-parity-lessons.md](mesence-parity-lessons.md)). That is the evidence a framework
maintainer needs. Send them as separate, game-neutral PRs, smallest first.

### 2.2 Proof that the whole game works

MMX does not call a route correct because its hashes match. Its Storm Eagle softlock replayed
with every hash matching, so its regression tests also check gameplay outcomes. We have exact
sync on 30 minutes of play and spot checks after that (21 fights in room `0x38`, save states).
A great recomp is one somebody has finished.

What great looks like: a record that every region, boss and ending was reached on the native
build, and regression runs that check *game events* (room entered, boss flag set, item gained),
not frame numbers.

**How, with what we know:**
- **The WRAM map and the knowledge entries** (everscript-vscode `soe://ram/`, the `memory-map`
  skill) tell us which addresses hold the room id, boss flags, inventory and party HP. A
  milestone check is a list of `(address, expected value)` pairs, written as named facts instead
  of magic numbers.
- **The simulation's route steps** (everscript-vscode `src/games/evermore/simulation/`, the
  Routes tab) already play a route step against WRAM. Run those steps against the recomp's WRAM
  instead of the emulator's.
- **The sample playbacks** (`sample-playbacks` skill) and `03-complete-run-7h`: 03 does not sync
  frame-exactly, but it does not need to for an event check. Play it on the native build and
  check that it reaches the same rooms and flags in the same order. Write down any event it
  misses (the `no replay 03 for parity` rule is about frame sync only).
- **The script VM and opcode docs** (`docs/scripts/`) say which flags each boss and story script
  sets. The milestone list can be generated from them rather than written by hand.

### 2.3 A release people can play

MMX's front page has a download link, a feature table, a "which ROM" table with hashes, and a
quick start in four steps. Our README is for developers. There is no tag, and `main` still holds
the scaffold.

**How:** `scripts/package_release.sh` and `.github/workflows/release.yml` already build a setup
pack with no ROM and no generated C. What is missing is the decision (the open `LICENSE` question
in [bugs-and-todos.md](../bugs-and-todos.md) §4), merging to `main`, and a player-facing README. That
README needs a feature table, a ROM table, controls, and a "reporting problems" section.
**Legal:** MMX commits gameplay screenshots. We must not (no screenshots, AGENTS.md rule 1); link
to a video hosted elsewhere if at all.

### 2.4 Tests and CI in this repository

MMX builds a ROM-free host in CI and runs every ROM-free test on each push. ROM-backed state tests
are opt-in (`-DMMX_STATE_TESTS=ON`). Each fixed bug gets a test that failed before the fix (for
example, frames after a soft reset must equal the same frames after power-on). Our checks live in
`tools/compare/` (with probes) and are run by hand.

**How:**
1. CI: configure with `-DSNESRECOMP_SETUP_HOST=ON` (as `package_release.sh` does) and build on
   every push. This catches glue and frame-driver breakage without a ROM.
2. ROM-backed `ctest` targets, skipped when no ROM is given: boot to title, the reset-equals-boot
   test, save state at launch and mid-game, then the game keeps advancing. These are the lessons
   already written up in [bugs-and-todos.md](../bugs-and-todos.md) §2.
3. A `tools/check-sync.sh` that runs both sync replays against MesenCE through everscript-vscode's
   `compare.mts` and fails at the first differing NMI. Run it before each release, not on every
   push (`test-discipline`: slow tiers on demand).

### 2.5 Bug reports that arrive as replays

MMX records every normal session: inputs, machine checkpoints, settings and per-frame state hashes.
The recording is capped at one hour and 256 MiB and written on a background thread. A tool replays
it headless and stops at the first divergence. Its README tells players exactly which files to zip.

**How:** our `.evsmv` replays and `compare.mts` already do the replay half. Recording needs the
framework's replay support or a small writer in `game_rtl.c`, kept in memory with only hashes per
frame on disk (the "spare the SSD" rule in everscript-vscode). A report is then something
`compare.mts --backend recomp,mesen` can open.

### 2.6 Player features, built from what we already know

This is where MMX is furthest ahead, and where our knowledge base gives us the most. Each feature
below is a mod (§2.7). With all mods off, the game must stay vanilla.

| Feature | MMX has | What we know that makes it feasible | Where it is written down |
|---|---|---|---|
| Widescreen | adaptive, 16:9, 21:9, 32:9 | room tilemaps decoded (margin prefill from room data, not guesswork), camera and scroll from the PPU, HUD and ring menu pinning | [enhancements.md](enhancements.md) §1; everscript-vscode `maps/`, `docs/map-format/` |
| Co-op (couch) | X + Zero, separate health, join and leave | an analysis of the existing 2-player patch with its corrections, the input hooks, free WRAM | everscript-vscode `docs/soe-2player-patch-analysis.md`, `docs/plans/multiplayer-mod.md`, `docs/unused-wram.md` |
| Netplay | rollback, lobbies through recomp-net | needs a deterministic native build (we have it: native = interpreted at every key) and save states; `recomp-net` is already in `framework_pins.txt` | [progress.md](../progress.md) |
| MSU-1 / music | observes SPC music commands, plays PCM, falls back to SPC | the sound driver's port commands and the music-to-package tables | everscript-vscode SPC dumping notes, `music/` |
| Rewind, run-ahead, save-state browser | yes | save states already work, including mid-slice resume (`SEXC` chunk) | [bugs-and-todos.md](../bugs-and-todos.md) §2 |
| Bugfix mods | KNC bugfix | the 5-status-effects RAM corruption, with its cause | everscript-vscode `docs/5-status-effects-bug.md` |
| Quality of life | hotkeys, password prefill | the cycle-exact hotkey hack (ring menu shortcuts), cheats list, credits skip candidates | everscript `hacks/hotkeys`, everscript-vscode `docs/cheats.md`, `docs/credits-skip-candidates.md` |
| Overlays | hitbox overlay (developer mod) | entity boxes, collision words, live map | everscript-vscode `map-entities` skill, Rooms tab; [enhancements.md](enhancements.md) §3 |
| Translations | none | text, font and localization tables | everscript-vscode `localizations/`; [enhancements.md](enhancements.md) §2 |
| Other releases | refuses v1.0, EU and JP, with a message | how each PAL build differs from US, and why US addresses do not carry over | everscript-vscode `docs/evermore-releases.md` |

Start with the features that need no new reverse engineering and are easy to prove: rewind and
the save-state browser, then widescreen, then the 5-status-effects fix. Co-op and netplay come
last: they change game state, so they need the full §2.2 proof first.

### 2.7 Mods as hooks that leave vanilla intact

MMX injects marked, idempotent snippets into the generated C. A restore step removes them, and
with every mod off the build is vanilla. Two of its lessons carry over directly:
- **Patch the verdict, not the state.** A hook changes a decision where it is consumed (a cull
  test, a camera clamp), not a value another routine later compares. MMX's first widescreen froze
  scrolling because it biased a camera snapshot.
- **"All mods off equals vanilla" is a test.** Run the two sync replays with every mod compiled
  in and disabled. They must still match MesenCE.

For us, mods are trusted plugins built against our glue (`game_rtl.c`) and the framework's mod
API, never edits to `src/gen/` or the framework. The everscript-vscode WRAM names give each hook
a readable address.

### 2.8 Performance, measured

MMX publishes FPS per route and keeps an LLE reference build next to the faster default, so
every speed-up can be checked against it. We have never measured. Bus-exact compiled code
(`SNESRECOMP_EXACT_BUS=1`) and per-access timing cost speed, and we chose them deliberately.

**How:** measure FPS uncapped on 01 and 02 with `SDL_VIDEO_DRIVER=dummy`, per build mode, and
record the numbers in [progress-internal.md](../progress-internal.md). Only if a target machine falls short, add a
faster mode that stays optional, the way MMX does. The accurate build stays the default
reference (the same rule as "preserve, not improve" for reconstructed C).

### 2.9 Readable code, proven

The generated C is not a decompilation. everscript-vscode has 309 reconstructed-C routines
(`reconstructed-c/`) and curated names for many more. None of them runs in this recomp yet.
Lufia II binds hand-written C only after a differential test against the interpreter
([lessons-from-mmx-and-lufia2.md](lessons-from-mmx-and-lufia2.md) §3). We now have the oracle
for that test: the interpreter and MesenCE agree to the end of both replays.

**How:** bind one leaf routine (a decompressor) through `hle_func`, run both sync replays, and
check that they still match MesenCE. Then generate the build with and without the names overlay
and check that the normalized C is identical (names stay cosmetic). This is the road to a recomp
people can read and mod at the source level, not just play.

---

## 3. What to do, in order

| # | Step | Closes | Evidence that it is done |
|---|---|---|---|
| 1 | Upstream the four snesrecomp changes; pin upstream commits | §2.1 | the sync replays pass on the pins in `framework_pins.txt` |
| 2 | ROM-free CI build; ROM-backed `ctest` for boot, reset, save state; `check-sync.sh` | §2.4 | green CI; tests that fail on a reverted fix. **2026-11-03:** `ctest` reset test (found and pinned a real soft-reset bug); CI waits on step 1 (the pinned snesrecomp commit is not on GitHub) |
| 3 | Event-based milestone checks generated from the WRAM map and scripts; play 03 on the native build | §2.2 | every region, boss and the ending reached, written in [progress.md](../progress.md) |
| 4 | Settle `LICENSE`, merge to `main`, player README, first tagged setup pack | §2.3 | a public release a stranger can install |
| 5 | Session replay recording and a reporting section | §2.5 | a player's report replays in `compare.mts` |
| 6 | Mod hook layer with "all off = vanilla" in CI; rewind, save-state browser, widescreen, 5-status fix | §2.6, §2.7 | each mod's own test; sync replays unchanged with mods off |
| 7 | FPS per build mode | §2.8 | numbers in [progress-internal.md](../progress-internal.md). **Done 2026-11-03:** native 500 / 390 frames/s; the PPU renderer dominates |
| 8 | First proven `hle_func` binding; names A/B | §2.9 | sync replays unchanged; normalized C identical. **Done 2026-11-03:** the RNG bound ([progress.md](../progress.md), "Bound C"); names A/B by hand ([game-rtl.md](../game-rtl.md) §4a) |
| 9 | Co-op, then netplay | §2.6 | after 3 and 6 |

Steps 1 to 4 decide whether the project can be called a recomp of the whole game. Steps 5 to 9
decide whether it is a great one.
