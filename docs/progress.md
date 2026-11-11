# Progress: Replays Against MesenCE

How much of each committed replay plays exactly as MesenCE 2.2.1 plays it, at every NMI from power-on, every byte, nothing
copied in or masked. Measured 2026-11-07 on `develop` with `framework/snesrecomp.patch`, to the end of each replay; since 2026-11-11
the build generates with upstream's region-paced code plus our resume (PR 4): sync rechecked, native 94.05 % (01) and 92.43 % (02)
(`node tools/compare/full.mts`, native and `--interpreted`, the picture with `--fade`; `tools/compare/probes/run-stats.mts --stats`). How to reproduce:
[testing.md](testing.md).

| Replay | NMIs | WRAM, VRAM, CGRAM, OAM, SRAM | ARAM | S-DSP registers | S-DSP state | Picture, MesenCE's brightness rule | Native |
|---|---|---|---|---|---|---|---|
| `01-no-inputs-1-loop` | 8,592 | **identical at every NMI** | **identical** | **identical** | 325 NMIs differ | **identical** (289 NMIs differ with snesrecomp's) | 99.69 % |
| `02-new-game-to-flowers` | 23,756 | **identical at every NMI** | 81 NMIs differ, from 18,503 | 77 NMIs differ, from 18,503 | 579 NMIs differ | **identical** (948 NMIs differ with snesrecomp's) | 99.72 % |

Native and all-interpreted runs give the same result in every memory and sound column (the picture with `--fade` was measured
native only).

**Columns**
- **S-DSP state:** output samples, voices, envelopes, BRR buffers and echo, once per NMI; not the audio stream.
- **Picture:** the 224 rendered lines, pixel for pixel, with brightness scaled as MesenCE scales it (`--fade`; play keeps
  snesrecomp's rule, see below).
- **Native:** instructions that ran compiled or as C, not in the interpreter. 19 routines are bound to reconstructed C; 18 of
  them take their C path on these replays (`anim_start_record` falls back to compiled bytes). Per routine: `run-stats.mts --stats`.

**What differs, and why**
- **S-DSP state at NMIs 1-320** on both: the boot and the Squaresoft logo, which plays no sound. Until a voice is first keyed on
  (the intro's music, loaded right after NMI 319) its BRR offset is 0 here and 1 on MesenCE; harmless
  ([imperfections/dsp-brr-offset-reset.md](imperfections/dsp-brr-offset-reset.md)).
- **S-DSP state at single NMIs later, and replay 02's ARAM and S-DSP registers from NMI 18,503:** a voice's end flag (ENDX,
  `$7C`) reads differently. At the single NMIs a clock early; on 02 from 18,503 the read is on the same clock on both sides and
  voice 7 reaches its sample end only in snesrecomp's S-DSP. Open ([imperfections/dsp-endx-read.md](imperfections/dsp-endx-read.md)). The CPU side never sees it: WRAM stays identical.
- **Picture with snesrecomp's own brightness rule:** one 5-bit step apart during fades. snesrecomp rounds as bsnes does, MesenCE
  truncates; with MesenCE's rule nothing else differs ([imperfections/fade-rounding.md](imperfections/fade-rounding.md)).

**What this proves and what not:** about 9 minutes of play, from power-on through the attract demo, and from a new game to
Prehistoria. The whole game is not proven. Earlier measurements, on the replays of 2026-11-05 (now local, outside git):
[progress-internal.md](progress-internal.md).
