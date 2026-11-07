# Progress: Replays Against MesenCE

How much of each committed replay plays exactly as MesenCE 2.2.1 plays it, at every NMI from power-on, every byte, nothing
copied in or masked. Measured 2026-11-07 on `develop` with `framework/snesrecomp.patch`, to the end of each replay
(`node tools/compare/full.mts`, native and `--interpreted`; `tools/compare/probes/run-stats.mts --stats`). How to reproduce:
[testing.md](testing.md).

| Replay | NMIs | WRAM, VRAM, CGRAM, OAM, SRAM | ARAM | S-DSP registers | S-DSP state | Picture | Native |
|---|---|---|---|---|---|---|---|
| `01-no-inputs-1-loop` | 8,592 | **identical at every NMI** | **identical** | **identical** | 325 NMIs differ | 289 NMIs differ | 99.69 % |
| `02-new-game-to-flowers` | 23,756 | **identical at every NMI** | 81 NMIs differ, from 18,503 | 77 NMIs differ, from 18,503 | 579 NMIs differ | 948 NMIs differ | 99.72 % |

Native and all-interpreted runs give the same result in every column.

**Columns**
- **S-DSP state:** output samples, voices, envelopes, BRR buffers and echo, once per NMI; not the audio stream.
- **Picture:** the 224 rendered lines, pixel for pixel.
- **Native:** instructions that ran compiled or as C, not in the interpreter. 15 routines are bound to reconstructed C; 14 of
  them take their C path on these replays (`anim_start_record` never does). Per routine: `run-stats.mts --stats`.

**What differs, and why**
- **S-DSP state at NMIs 1-320** on both: a voice's BRR offset starts at 0, not 1, until its first key-on; harmless
  ([imperfections/dsp-brr-offset-reset.md](imperfections/dsp-brr-offset-reset.md)).
- **S-DSP state at single NMIs later, and replay 02's ARAM and S-DSP registers from NMI 18,503:** a voice's end flag (ENDX,
  `$7C`) is visible to the SPC700 a clock early; on 02 for a stretch of NMIs, not one. Open
  ([imperfections/dsp-endx-read.md](imperfections/dsp-endx-read.md)). The CPU side never sees it: WRAM stays identical.
- **Picture:** pixels 8 of 255 apart during fades, the brightness rounding of snesrecomp's renderer
  ([imperfections/fade-rounding.md](imperfections/fade-rounding.md)); `--fade` does not account for all of these frames yet.

**What this proves and what not:** about 9 minutes of play, from power-on through the attract demo, and from a new game to
Prehistoria. The whole game is not proven. Earlier measurements, on the replays of 2026-11-05 (now local, outside git):
[progress-details.md](progress-details.md).
