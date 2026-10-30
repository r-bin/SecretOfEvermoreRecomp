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

## Fix (open)
Feed replay input per field: the tool knows each host frame's field count from the master clock (the `P`
peek lines), or the frame driver could stop the CPU at a field boundary inside a long DMA (upstream).
