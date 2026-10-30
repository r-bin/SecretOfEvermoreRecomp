# Replay Sync Against MesenCE

How much of each replay the recomp (native build, `develop` 2026-10-30) plays exactly as MesenCE does.
Replays: everscript-vscode `tools/compare/replays/`. Measured with

```sh
node tools/compare/compare.mts "Secret of Evermore (U) [!].smc" --backend mesen,recomp \
  --replay tools/compare/replays/<replay>.evsmv --keep-going
```

Classes, from the WRAM hash at **every** key on both sides, sampled as each NMI is taken (the key is the
NMI's number; timing-noise bytes of `tools/compare/game.mts` masked; nothing before key 120 is compared, the
RNG is copied in there):

| Class | Meaning |
|---|---|
| 🟢 | identical WRAM at every key |
| 🟡 | the recomp passes through the same WRAM states as MesenCE, in order, but not always at the same key (e.g. one dirty frame in between); checked: each recomp hash appears in MesenCE within ±30 keys, never going back |
| 🔴 | drift: from some key on, the recomp's WRAM appears nowhere near in MesenCE |

| Replay | Class | Identical keys | Comments |
|---|---|---|---|
| `01-no-inputs-4m.evsmv` (14,424 frames, attract loop) | 🔴 | 1.9 % (200 of 10,798) | Identical at every NMI key 120–319 (since the own frame driver, 2026-10-30; was 116–313); drift from key 320, the first load's lag frames, 5 bytes at first (`$0B33`, `$0B37–$0B38`, the stack). After the drift **99.0–99.98 %** byte-equal (keys 1000–5800). Every NMI from 2 on lands within ±32 clocks of MesenCE's. First cause left: [APU port handshake](imperfections/apu-port-handshake.md) (NMI 1, after the sound-driver upload, 612 clocks early). |
| `02-newgame-boy-available.evsmv` (17,948 frames, New Game to free play) | 🔴 | 1.9 % (200 of 10,647) | Same as 01 up to the drift at key 320, before any input matters. After it **98.6–99.8 %** byte-equal (keys 1000–12000). Inputs reach the recomp a field late after each long DMA ([host frames vs fields](imperfections/host-frames-and-fields.md)); both sides start with empty SRAM (the replay's SRAM header is loaded by neither). |
| `03-complete-run-7h.evsmv` (1,559,442 frames) | — | not measured | Excluded by the owner; a full run takes ~3 h. |

The key counts are the keys both runs reported (the probe compares only keys present on both sides).
Probe: a per-key hash run (`stride: 1`, WRAM, masked) of `runMesen` / `runRecomp` from `tools/compare`,
then a search of each recomp hash in MesenCE's hashes nearby.

## Can it reach 100 %?

Without changing snesrecomp (owner's rule), timing is ours to set in our frame driver and glue
([imperfections/README.md](imperfections/README.md) lists each fix). Fixed so far: the late NMI, the
sampling point, the short scanline and reset clock, the APU's time before the first port write, the
interrupt cost, DRAM refresh, the SPC700's rate. The framework's interpreter is cycle-exact on the code
measured. Open, in the order they part the replays:

| Cause | Fixable in our files? | Work |
|---|---|---|
| [APU port handshake](imperfections/apu-port-handshake.md): the upload's last 3 bytes answered ~250 clocks sooner | Unknown: the port queue is the framework's; our glue could only shift the APU's time, not how it reads a port | Investigate the SPC700 side of those bytes first |
| [DMA overhead](imperfections/dma-overhead.md): start/end alignment a few clocks off | Yes, through the public DMA charge hook | Small, once the clocks match exactly |
| [Compiled cycle charges](imperfections/compiled-cycle-charges.md): compiled code billed at code speed | Yes, by keeping code in the interpreter (seeds) | Small to do, costs speed; native and interpreter part at the same key today |
| [Host frames vs fields](imperfections/host-frames-and-fields.md): replay input lands late after long DMAs | Yes, in the tool (feed input per field) | Medium; only replays with input |
