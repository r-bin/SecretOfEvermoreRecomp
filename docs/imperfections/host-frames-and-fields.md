# One Host Frame Can Hold Several Video Fields

**Status:** worked around in the glue for compare runs (`src/game_rtl.c`, `GameRunOneFrame`); normal play
unchanged.

## What happens
The beam frame driver runs one host frame up to the beam's next V=225. A DMA charges its whole time at once,
so a DMA longer than a field carries the beam past one or more V=225 inside a single host frame. The reset
code's three 64 KB clears (≈4.4 fields) end after 4 host frames where MesenCE shows 5 fields: host frame 3 of
the recomp ends at master clock 1,736,372, MesenCE's frame 4 at 1,736,364.

## Effect
The host takes one joypad input per host frame (headless scripts: one per `wait`/`press` frame). After such a
frame, every replay input reached the game one field later than on MesenCE, which reads one per field.

## Workaround (compare runs only)
When `SOE_COMPARE_*` is active, `GameRunOneFrame` counts the V=225 the beam has crossed; a host frame that is
owed one runs nothing and only counts. Its input is overwritten unread, as hardware's joypad latch is in a
field the game spends loading. Since then the recomp's frame *n* and MesenCE's frame *n* end at the same
field: at frame 4 the two clocks differ by 8 master clocks, at frame 2,995 by ~6,000 (4 scanlines), and both
show the same key. A replay feeds both the same input per field.

Left as is in normal play: a skipped frame would draw the same picture twice and gives a player nothing.

## Fix (upstream candidate)
The frame driver could stop the CPU half at a field boundary inside a long DMA and resume it in the next host
frame, so one host frame is always one field.
