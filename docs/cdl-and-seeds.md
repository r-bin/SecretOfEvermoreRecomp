# The CDL, the Seeds, and What Gets Committed

The seeds in `recomp/bank*.cfg` are written by everscript-vscode's `npm run recomp` from its CDL
library: a recording of what the game did while it was played in the everscript emulator. This
page says how good that recording is for the recomp, and why only the seeds live here.

## How good the CDL is (2026-10-30)

| Measure | Value |
|---|---|
| ROM touched (code or data) | 90.3 % (119,478 code bytes, 2.72 MB data) |
| Rooms whose data was read | 125 of 127 (not 0x15, 0x75) |
| Dialogue / menu strings read | 1456 of 3002 (48 %) |
| Code banks never touched | ~70 KB in `$C0`, `$CC`–`$D1` (part padding, part code that never ran) |
| Source | 86 replays, recorded 2026-10-07 to 2026-10-30 |

**Good enough for the recomp.** Code the CDL never saw still runs: snesrecomp interprets it. More
coverage buys speed and a larger native share, not correctness. The risk is the opposite one: a
*wrong* seed, such as a routine that cannot be compiled (the script scheduler, `$8C:CFFD`, see
[bugs-and-todos.md](bugs-and-todos.md)). More recording pays where it reaches rooms 0x15 and 0x75,
rare fights, menus and deaths. The replays are now worth more as regression tests than as coverage.

## Why the CDL is not committed

| Part | Holds | Here? |
|---|---|---|
| `rom.cdl`, `rom.ext` | one flag byte per ROM byte (code, data, widths), no ROM bytes | no: nothing here reads them; the seeds carry what the recomp needs |
| value streams (`wram-values.bin`, `io-values.bin`, `regs.bin`, `word-ranges.bin`, `script-xrefs.bin`, ...) | values the game produced while running | no: derived from the game's data |
| replays (`.evsmv`) | controller input, with the SRAM they started from | no: the SRAM holds save data. The test replays are MesenCE movies from power-on, input only ([testing.md](testing.md)) |
| **the seeds** (`recomp/bank*.cfg`) | function roots, register widths, dispatch tables, data regions, our names | **yes**: no ROM bytes, reproducible from the CDL |

The library and its replays are kept as everscript recording bundles (zips), outside any
repository. A new CDL becomes new seeds through `npm run recomp`; commit the changed cfgs here.
