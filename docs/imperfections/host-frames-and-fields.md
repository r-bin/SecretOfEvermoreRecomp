# One Host Frame Can Hold Several Video Fields

**Status:** open for replays with input. (A glue workaround was tried and removed, see below.)

## What happens
The frame driver runs one host frame up to the beam's next V=225. A DMA charges its whole time at once, so a
DMA longer than a field carries the beam past one or more V=225 inside a single host frame. The reset code's
three 64 KB clears (≈4.4 fields) end after 4 host frames where MesenCE shows 5 fields.

## Effect
The host takes one joypad input per host frame (headless scripts: one per `wait`/`press` frame). After such a
frame every replay input reaches the game one field later than on MesenCE. The no-input replay is not
affected; NMI-keyed comparison (`SOE_COMPARE_AT=nmi`) does not depend on host frames.

## The workaround that was removed
Compare runs used to spend an owed host frame without emulating (its input unread, as in a loading field).
With extended frame timing ([apu-time-in-long-frames.md](apu-time-in-long-frames.md)) every host frame still
gives the APU at least one field of time, so a skipped frame ran the SPC700 a field ahead. Removed 2026-10-30.

## Replay input in compare runs (2026-10-31)
`tools/compare/recomp.mts` (everscript-vscode) now gives each pad value to the field MesenCE gives it: the runner's
script spends frames of its own (a `poke` 2, a `press` of n frames n + 1), which come off the next segment, and the
script's frame f is MesenCE's frame f + 1 (the reset code's 5 fields in 4 host frames). With that,
`02-newgame-boy-available.evsmv` is identical at every NMI key to its end. A later DMA that packs two fields into one
host frame would still move the input by a field; none does in the two replays.

## Frame start past V=225 (2026-10-31)
A host frame can begin lines after V=225 (the last one ran on). The driver took that line as its line 225, so the
auto-joypad window and the refresh points of the field were placed lines late: on the 02 replay a late NMI's handler
polled `$4212` bit 0 busy where MesenCE had finished the read 16,000 clocks before. `s_line0` now counts back to
line 225.

## Fix (open)
Feed replay input per field: the tool knows each host frame's field count from the master clock (the `P`
peek lines), or the frame driver could stop the CPU at a field boundary inside a long DMA (upstream).
