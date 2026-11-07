# 2. OAM and CGRAM reads during rendering

**Status:** draft, no PR yet. Depends on [#241](01-access-exact-timing.md): the beam hook is answered only on the master-clock timeline.

Commit: `3b73e98c` (`abeba9ab` on `pr-stack-on-main`, one conflict with `mstan`'s `s_latch_snes`, resolved their way).

## To do before opening

1. Add a game-neutral test: a PPU harness that reads `$2138` and `$213B` at chosen H/V positions, in `tests/ppu/`.
2. Carry the test stub, `7359f4f6` on `pr-stack-on-main` (`g_ppu_beam_hook` in `tests/dma/hdma_timing_test.c`).
3. Open it stacked on #241.

## PR title

Return what the PPU fetches for OAM and CGRAM reads during rendering

## PR text

```markdown
During active display the PPU owns OAM and CGRAM. A CPU read of `$2138` or `$213B` returns the byte at the address the PPU is using at
that moment, not the CPU's address (which still advances). snesrecomp always answers with the CPU's address.

Games can see this without meaning to. Secret of Evermore seeds its RNG at reset from reads that land on line 124 and 125, so one
wrong byte changes the rest of the run.

Rules, as MesenCE 2.2.1 has them:
- OAM, H clock <= 1020: the range phase's sprite, one per two dots from the priority-rotation start.
- OAM, later: the time phase, walking this line's in-range sprites backward from dot 270, two dots per sliver.
- CGRAM: free in H clocks 0-87 and 1096-1363, on line 0, from the NMI line on and in forced blank. Otherwise the backdrop while the
  sub screen has no layer enabled.

Not modelled: CGRAM with sub screen layers on keeps the CPU's address.

Only active on the master-clock APU timeline, whose access clocks are exact, so other games keep today's reads.
`SNESRECOMP_PPU_RENDER_READS=0` turns it off.

Result: the attract replay of Secret of Evermore stays identical to MesenCE to its end, 14,305 frames, compiled and interpreted.
Turned off, it parts at frame 5998. Recipe: https://github.com/r-bin/SecretOfEvermoreRecomp/blob/<head>/docs/pr/README.md#reproduce

Doubts: the rules are checked against one game's reads only, and each `$2138` read during rendering walks the lines above the beam, which
would cost a game that polls it.

Depends on #241.
```

## Required

Each turned off alone on the committed replays (2026-11-07): the first NMI where WRAM parts. Native and interpreted alike.

| Change | Switch | Replay 01 | Replay 02 |
|---|---|---|---|
| The whole change | `SNESRECOMP_PPU_RENDER_READS=0` | 5998 | in sync |
| OAM rule alone | `ABL_RENDER_OAM` | 5998 | in sync |
| CGRAM rule alone | `ABL_RENDER_CGRAM` | 5998 | in sync |

Only the attract demo reads OAM and CGRAM during rendering (its RNG seed at NMI 5998); the new game does not.

## Not game specific?

Yes. The rules are PPU hardware, taken from MesenCE, with no Evermore address or value. The tables are the OBSEL size pairs.

Two limits: the rules are checked against one game's reads only, and the CGRAM rule covers one of its two cases. `ppu_renderOamAddress`
walks all lines above the beam on each read, which costs nothing here but would on a game that polls `$2138` during rendering.

## Good for future games?

Yes, for any game reading these registers during active display, deliberately or by accident. It needs the master-clock timeline.

## Reproduce

Replay 01. Setup: [README.md](README.md#reproduce), steps 1-6: in sync through 14424. Step 7 with `SNESRECOMP_PPU_RENDER_READS=0`:
`wram differ at counter $0100 = 5998`. The two single rules need [ablation-gates.patch](ablation-gates.patch).
