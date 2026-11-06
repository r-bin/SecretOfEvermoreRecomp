# tools/compare — run setups side by side, find where they part

```sh
npm run compare -- out/my-hack.sfc                     # one ROM: against vanilla
npm run compare -- a.sfc b.sfc c.sfc                   # the first is the reference
npm run compare -- recomp=                             # the recomp against snes9x: a fast lead
npm run compare -- mesen recomp "rom.smc"              # compare mesen and recomp on a single ROM
npm run compare -- "rom.smc" --backend mesen,recomp    # same, using --backend
npm run compare -- mesen recomp "rom.smc" --replay tools/compare/replays/01-no-inputs-4m.evsmv
npm run compare -- recomp:build/a=rom.sfc recomp:build/b=rom.sfc   # hooks against no hooks
npm run c-to-rom:compare                               # the C-built image against vanilla, + quality.md
```

A **setup** is `backend[:exe]=rom`; a bare path means `snes9x=<path>`, `recomp=` with no ROM means the clean
ROM with the recomp executable, and `mesen=` means the clean ROM with MesenCE (`/Applications/Mesen.app` or
`$MESEN`). Backends:
- `snes9x` (our fork's debugger core, `src/core/emulator/core/snes9x2005-wasm/`)
- `recomp` (the game built in the recomp repository, `../SecretOfEvermoreRecomp/build/`, or the executable after the colon)
- `mesen` (MesenCE running headless via `--testrunner` and Lua; the cycle-accurate reference for WRAM)

Options: `--replay x.evsmv` (runs across snes9x, recomp, and mesen), `--frames N` (default 8414, the 2:20 intro; a replay's
length with `--replay`), `--verbose` (check every frame, not every 60th), `--keep-going` (count every
differing checkpoint), `--aram`, `--regions wram,vram,cgram,oam,sram,aram,dsp,sound,picture`, `--align frame|nmi` (default `nmi` when mixing
backends), `--no-cache`, `--out file.md`, `--rom <clean rom>`.

Comparing everything, MesenCE against the recomp:

| Option | What |
|---|---|
| `--full` | every RAM (WRAM, VRAM, CGRAM, OAM, SRAM, ARAM, S-DSP registers) at every key, on to the end, each region's result in the report |
| `--picture` | adds the rendered picture: the 224 lines before each NMI |
| `--sound` | adds the S-DSP's whole state: output samples, voices, envelopes, BRR buffers, echo |
| `--fade` | with `--picture`: the recomp's pixels rewritten by MesenCE's brightness rounding, to see what else differs (SecretOfEvermoreRecomp `docs/imperfections/fade-rounding.md`) |
| `--interpreted` | the recomp all-interpreted, for the reference column of a native build |
| `npm run compare:full` | `tools/compare/full.mts`: all sync replays with all of the above, one after the other; reports in `out/compare/full/`. `--only 01,06`, `--no-picture`, `--no-sound`, `--interpreted`, `--fade` |

## How it works

1. All setups start at once as child processes. Each one hashes its regions (CRC-32; default WRAM + VRAM)
   at every 60th key and streams the hashes back. Nothing is written to disk.
2. The **key** is the emulated frame when every setup is snes9x. When backends are mixed, it is the number of
   NMIs since power-on: the increases of the game's counter `$7E0100` from the 0 the reset code writes, a drop
   counting as a restart from 0 (the attract demo zeroes it when it loops; `nmiKey` in `game.mts`, mirrored in
   the Mesen Lua and the recomp glue). Mixed runs also copy the reference's RNG `$0133–$013A` into the recomp
   after the frame the reference reached key 115, compare nothing before that, and ignore the timing-noise
   bytes in `game.mts`. The report lists them.
3. The default comparison is one hash over all regions (a CRC of the region CRCs). A target stops at its
   first differing checkpoint, unless `--keep-going` is set.
4. Then, for each target that parted from the reference, both setups run again to that checkpoint and hand
   over their memory in RAM: the differing bytes, with names from the WRAM map (`data/wram-map.md`). For snes9x setups, the
   window from the last matching checkpoint is run again with write watchpoints on up to 256 differing
   WRAM bytes. The first CPU write where the two sides part gives the frame, the PC and the routine
   (`localizations/functions`, or the C function from `out/c-to-rom/build.json` for PCs in the 4th MB).
   DMA writes are not watched.
5. Report: a summary on the terminal and `out/compare/<setups>.md`. Exit code 0 only when every target
   stayed in sync for the whole run.

A snes9x reference's hashes for every frame are cached in `out/compare/cache/`, keyed by core, ROM, replay,
length and regions, so vanilla runs once (~5 s for the intro) and costs nothing after that.

| File | What |
|---|---|
| `compare.mts` | the command: setups, scheduling, the cache, the detail pass |
| `diff.mts` | the pure part (tracker, byte and write diffs); `tests/compare/compare-units.test.mts` |
| `snes9x.mts` | the snes9x child process: `hash`, `dump`, `watch`, `rng` jobs |
| `recomp.mts` | the recomp: a headless script plus the recomp glue's `SOE_COMPARE_*` hashing on fd 3 |
| `mesen.mts` | the MesenCE child process: headless Lua testrunner, IEEE CRC32 hashing, RNG sync, dumps |
| `full.mts` | every sync replay with every region, one after the other |
| `game.mts` | the Evermore facts it aligns on (frame counter, RNG, noise bytes) |
| `report.mts`, `cache.mts` | the Markdown report and naming; the hash cache |

## Sampling at NMIs (MesenCE against the recomp)

When no setup is snes9x, both sides sample as each NMI is taken, after its return frame is pushed (`atNmi`:
MesenCE's Lua `nmi` event, the recomp glue's `SOE_COMPARE_AT=nmi`). That is an instruction boundary on both,
which a frame's end is not, and the key is then the NMI's number since power-on. `onNmi(nmi, clock, pc)` reports
each NMI's master clock and the address it interrupted on both sides: where the two clocks part.

## Sampling the sound side and the picture

`aram`, `dsp` and `sound` are read at one SPC700 instruction boundary on both sides, the first at least 16 cycles after
the NMI's clock. The two SPC700s stop at different places when an NMI is taken: MesenCE's runs in catch-up batches, the
last at the start of line 225, and can stop inside an instruction; the recomp's has run each instruction at its first
clock and been synced up to the NMI. MesenCE's Lua waits for that boundary with an SPC exec callback (`at_spc`), so a key's
sound values arrive a little after its CPU values. The recomp runs ahead on a copy of its APU and puts it back
(`CmpSpcEnter` in the glue), so a compare run plays as any other; it stops on the last clock of the instruction before,
where the S-DSP stands where MesenCE's does.

`picture` is read at MesenCE's EndFrame: `SendFrame` then swaps the buffers, so at the NMI the frame is gone. Runs need
`--snes.disableFrameSkipping=true` (at unlimited speed MesenCE skips rendering), which `mesen.mts` always passes. Rows 7-230
of its 239 are the recomp's 224. A run from a mid-game state does not compare the picture at key 1: that field began
before the state.

Both were checked by breaking them, on 300 keys of replay 06: `SNESRECOMP_LAYER_MASK=0x0F` (no sprites) fails `picture`
alone at 299 keys; `SNESRECOMP_DSP_PER_CLOCK=0` (the per-sample S-DSP) fails `dsp`, `sound` and `aram` from keys 1-2, and
WRAM from key 69.

## Probing a few bytes frame by frame

`runMesen` and `runRecomp` take `peek: [offsets]` and `onPeek(frame, key, masterClock, bytes)`: the WRAM bytes
after every emulated frame, on both sides. In compare runs the recomp keeps one host frame per video field,
so frame *n* is the same field on both (SecretOfEvermoreRecomp `docs/imperfections/host-frames-and-fields.md`).
The known differences between the recomp and MesenCE are listed there in `docs/imperfections/`.

## Probes (`probes/`): where clocks part

For the recomp against MesenCE, below one key. Both run the recomp all-interpreted (`probes/interp.mts` builds
the target list from the recomp's `src/gen/`) with the framework's per-instruction charges
(`SNESRECOMP_CYC_WATCH`) streamed through `RecompJob.onStderr`, against MesenCE Lua appended through
`MesenJob.lua` / `onLine`:

```sh
# one frame, instruction by instruction, from NMI 200's entry to NMI 201's
node tools/compare/probes/frame-trace.mts "Secret of Evermore (U) [!].smc" tools/compare/replays/01-no-inputs-4m.evsmv 200
# every sound-port handshake up to NMI 320: same number of polls on both sides?
node tools/compare/probes/port-handshakes.mts "Secret of Evermore (U) [!].smc" tools/compare/replays/01-no-inputs-4m.evsmv 320
```

The recomp's native build against itself all-interpreted (the interpreter matches MesenCE, and this needs no
MesenCE run past key 115), for a native build generated bus-exact (SecretOfEvermoreRecomp `tools/regen.sh`):

```sh
# each NMI's clock and the WRAM hash at every key: the first NMI and key where native and interpreted part
node tools/compare/probes/native-vs-interp.mts "Secret of Evermore (U) [!].smc" tools/compare/replays/01-no-inputs-4m.evsmv 14424
# instruction by instruction in a master-clock window (from the last NMI both agree on to the first that differs)
node tools/compare/probes/clock-trace.mts "Secret of Evermore (U) [!].smc" tools/compare/replays/01-no-inputs-4m.evsmv 433 196500920 196858300
# how much of the replay ran native (instructions per tier) and the busiest interpreted runs
node tools/compare/probes/tier-stats.mts "Secret of Evermore (U) [!].smc" tools/compare/replays/01-no-inputs-4m.evsmv 14424
```

Which framework changes a replay needs (SecretOfEvermoreRecomp `docs/pr/`): MesenCE runs once, its hashes stay in `out/compare/ablate/`,
then every variant (a switch off, or another executable) runs alone against them, several at a time, about a minute each:

```sh
node tools/compare/probes/ablate.mts tools/compare/replays/01-no-inputs-4m.evsmv variants.json --jobs 6 --out results.json
```

Against MesenCE, for a key where WRAM parts while the clocks agree:

```sh
# every NMI's clock, interrupted address and beam position (H clock, line), MesenCE against the recomp
node tools/compare/probes/nmi-clocks.mts "Secret of Evermore (U) [!].smc" tools/compare/replays/01-no-inputs-4m.evsmv 11987
# the PPU/WRAM port reads between NMI K-1 and K (an RNG seed), value and clock, and MesenCE's beam at $2137
node tools/compare/probes/ppu-reads.mts "Secret of Evermore (U) [!].smc" tools/compare/replays/01-no-inputs-4m.evsmv 11987
```

`frame-trace` folds the idle loop `$80:858F` into one entry (its pass count is a phase, not a cost); the last
entry runs to the next NMI and holds that stretch's refresh stalls. MesenCE's SPC700 callbacks need
`emu.cpuType.spc, emu.memType.spcMemory` and report a frozen `masterClock` (the SPC runs in catch-up batches):
time them with `spc.cycle / spc.clockRatio`.

`spc-trace.mts <rom> <replay> <K> [n] [context]` compares the SPC700's instruction stream from NMI K for n NMIs: in sync the
recomp's cycles read 1 above MesenCE's (where each logs). Bisect K to find where sound parts.

## Limits

- `sound` is the S-DSP's state once per key, not the sample stream: MesenCE's Lua has no audio access. That state holds
  the output sample, every voice's position and buffers and the echo history, and the echo buffer is in ARAM, so samples
  between keys can only differ if the two S-DSPs compute them differently from the same state.
- `sram`, `dsp`, `sound` and `picture` need MesenCE and the recomp: the snes9x backend refuses them.
- A Lua error in a callback ends the script but not MesenCE, so a run then waits for its limit with no message: `sound_bytes`
  reports a missing state field and stops. `envMode` is not in `emu.getState()`, so it is not compared.
- A recomp or MesenCE child outlives a killed `compare.mts`, even with its process group killed (MesenCE was seen orphaned at full speed): after a timeout check `pgrep -f 'Mesen --testrunner'`.
- `dist/` is shared: another session's build makes `compare.mts` fail at its start (`Cannot find module`). For a long run, copy `tools/`, `dist/` and `package.json` elsewhere, link `node_modules`, and set `SOE_RECOMP`.
- MesenCE's testrunner Lua has no `io` library: its dumps come over stdout as hex lines; a Mesen run hashes
  ~150 keys per second.
- MesenCE's testrunner ends a run after 100 s of wall time by default, wherever it is: `mesen.mts` passes
  `--timeout=3600` (`MESEN_TIMEOUT` sets it). Before 2026-11-03 a long replay stopped at a key that depended on the
  machine's speed (01 at 10,100-11,000 of its 14,424), with no warning.
- A replay that rewinds (`L` events) is refused.
- Checkpoints are every 60 keys (~1 s), so the first divergence is known to within that window. The
  write watch covers the whole window.
