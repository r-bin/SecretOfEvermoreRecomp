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
| `01-no-inputs-4m.evsmv` (14,424 frames, attract loop) | 🔴 | 1.9 % (200 of 10,458) | Identical at every NMI key 120–319 (since the own frame driver, 2026-10-30; was 116–313); drift from key 320, the first load's lag frames, 5 bytes at first (`$0B33`, `$0B37–$0B38`, the stack). After the drift **97.9–99.98 %** byte-equal (keys 1000–8000, at NMI offsets up to 6). All interpreted, NMIs 2–341 land within ±32 clocks of MesenCE's, long runs exactly. First cause left: [APU port handshake](imperfections/apu-port-handshake.md): the load after NMI 319 waits one poll more at `$8C:817C`; needs snesrecomp. |
| `02-newgame-boy-available.evsmv` (17,948 frames, New Game to free play) | 🔴 | 1.9 % (200 of 10,528) | Same as 01 up to the drift at key 320, before any input matters. After it **98.6–99.8 %** byte-equal (keys 1000–8000). Inputs reach the recomp a field late after each long DMA ([host frames vs fields](imperfections/host-frames-and-fields.md)); both sides start with empty SRAM (the replay's SRAM header is loaded by neither). |
| `03-complete-run-7h.evsmv` (1,559,442 frames) | — | not measured | Excluded by the owner; a full run takes ~3 h. |

The key counts are the keys both runs reported (the probe compares only keys present on both sides).
Probe: a per-key hash run (`stride: 1`, WRAM, masked) of `runMesen` / `runRecomp` from `tools/compare`,
then a search of each recomp hash in MesenCE's hashes nearby.

## On branch `snesrecomp-spc-access-cycles` (2026-10-31)
All interpreted, cleared SRAM on both sides: `02-newgame-boy-available.evsmv` 🟢 through key 9779;
`01-no-inputs-4m.evsmv` 🟢 from 120 to 5997, then the RNG seed from PPU reads during rendering.
With snesrecomp `ppu-render-reads` (pin `3b73e98c`) and the NMI handler sliced at refresh points
(dram-refresh.md), 2026-11-02, all interpreted, both 🟢 at every key to the end of their input, measured with
everscript-vscode's `node tools/compare/compare.mts rom.smc --backend mesen,recomp --replay <replay> --regions wram --verbose --keep-going` (the recomp's environment from `tools/compare/probes/interp.mts`):

| Replay | Keys compared, all identical |
|---|---|
| `01-no-inputs-4m.evsmv` (14,424 frames) | 120–10886 (10,767) |
| `02-newgame-boy-available.evsmv` (17,948 frames) | 120–10741 (10,622) |

Native still exact only to NMI 1. (Counts of 9,287 / 9,227 written earlier the same day came from a MesenCE hash cache
that ended at key 9406: the run parted at 9866, past it.) What it took and why the framework had to
change: [mesence-parity-lessons.md](mesence-parity-lessons.md).

## Can it reach 100 %?

Without changing snesrecomp (owner's rule), timing is ours to set in our frame driver and glue
([imperfections/README.md](imperfections/README.md) lists each fix). Fixed so far: the late NMI, the
sampling point, the short scanline and reset clock, the APU's time before the first port write, the
interrupt cost, DRAM refresh, the SPC700's rate, the auto-joypad busy window, where the NMI is recognised,
the APU's access-cycle lead. The framework's interpreter is cycle-exact on the code measured. Open, in the
order they part the replays:

| Cause | Fixable in our files? | Work |
|---|---|---|
| [APU port handshake](imperfections/apu-port-handshake.md): 55 % of sound-port handshakes take one poll pass more or fewer | **No.** The SPC700 does an instruction's port accesses on its first cycle; shifting its time or delaying CPU writes from our glue did not help (43–47 % match at every setting) | snesrecomp: port accesses on their own SPC cycle. Until then 🔴 from key 320 |
| [DMA overhead](imperfections/dma-overhead.md): start/end alignment a few clocks off | Yes, through the public DMA charge hook | Small, once the clocks match exactly |
| [Compiled cycle charges](imperfections/compiled-cycle-charges.md): compiled code billed at code speed | Yes, by keeping code in the interpreter (seeds) | Small to do, costs speed; native and interpreter part at the same key today |
| [Host frames vs fields](imperfections/host-frames-and-fields.md): replay input lands late after long DMAs | Yes, in the tool (feed input per field) | Medium; only replays with input |
