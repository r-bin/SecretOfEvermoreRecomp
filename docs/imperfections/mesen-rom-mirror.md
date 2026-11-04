# MesenCE's ROM Mirror Past 3 MB

**Status:** a MesenCE bug, copied only in compare runs (`SOE_COMPARE_MESEN_ROM_MIRROR`, `src/game_rtl.c`; `tools/compare/recomp.mts`
sets it when the reference is MesenCE). A normal launch keeps the cartridge's mirror.

## What happens
`$80:D113` (everscript-vscode knowledge entry `rom/80d113.md`) reads `$B0:E293+X`, past the end of the 3 MB ROM, and copies
that into `$7E2090-$7E20AF`. What it gets:

| Who | File offset for `$B0:E653` |
|---|---|
| snesrecomp, and a cartridge by the usual 2 MB + 1 MB mirror (no hardware test) | `0x20E653` |
| MesenCE | `0x200653` |

`BaseCartridge::Init` fills the handlers up to 4 MB with `for(i = 0; i < extraHandlers; i += 0x1000)`: `extraHandlers` counts
pages, `i` steps in bytes, so one pass adds only the page at 2 MB. All of `0x300000-0x3FFFFF` shows file page `0x200000`.

## Effect
Replay 05, key 3142: 32 bytes differ in `$2090-$20AF`, with clocks equal.

## Fix
With the variable set, the glue fills the 4th MB of its ROM copy with that page. Without it the recomp keeps the mirror above:
the variable makes the comparison fair, it does not make the game better.
