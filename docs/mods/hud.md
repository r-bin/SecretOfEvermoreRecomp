# Adaptive HUD

Moves the field HUD (each character's box with HP and the charge gauge, and the portraits) to the
top of the screen, to the screen edges of a wide picture, or hides it. Package
`soe.enhancement.hud` 1.0.0, feature `hud`, plugin `soe.hud`, off by default. Presentation only:
the game runs exactly as without it.

## Features and options

| Option | Values | What it does |
|---|---|---|
| Position | **Bottom**, Top, Hidden | Bottom is where the game draws it. Top puts it 6 lines below the top edge (the gap the game leaves at the bottom). Hidden leaves it out; the room shows underneath. |
| Layout in widescreen | **Screen edges**, Centred | Screen edges puts the Boy's half (x < 128) at the left edge of a wide picture and the Dog's half at the right. No difference in 4:3. |

Bottom with Centred, or Bottom in 4:3, draws exactly what the game draws (checked: pixel-identical).

## How it works

What the game draws, measured in this repo's dumps (everscript-vscode
`docs/disassembly/ui/field-hud.md`, tag `hud`):

- BG3 rows 24..27 of the map at VRAM word `$0800`, screen lines 193..223 (BG3 scroll `$3FF`):
  a box per character, columns 3..12 and 19..28, palette 2, priority set. A box's top-left corner
  is tile `$3EA` in row 25.
- Two 16×16 sprites, OBJ tiles `$080` (Boy) and `$082` (Dog), at y 201, always in two adjacent
  OAM slots. Which slots moves: 0/1 in the field, up to 11/12 under a ring menu.
- Dialogue and menu windows use BG3 rows 0..23 (palettes 0 and 1), never the HUD rows.

Each frame, in the host's `prepare_frame` (`SoePrepareFrame`, `src/game_rtl.c`):

1. **Gate.** BG mode 1, display on, a box corner in row 25 (Boy's or Dog's). Anything else (cutscenes
   without HUD, Mode 7, menu pages, the title) is drawn as the game draws it.
2. **Capture.** snesrecomp's host-overlay capture (`snesrecomp/docs/HOST_OVERLAY_EXTRACTION.md`):
   BG3 and the portrait slots of lines 193..223 are rendered into two surfaces of the plugin's own
   and removed from the game picture. The portrait slots are found each frame by their tiles; if
   they are not adjacent, the portraits stay where they are.
3. **Renderer.** The capture exists only in the optimized renderer, so a captured field uses it
   (`snesrecomp_desktop_set_new_renderer`); the field otherwise keeps the reference renderer
   (game-rtl.md §4: the name screen's mode 4).
4. **Top taken.** At the top, any BG3 tile in the rows that start on lines 0..31 means a window is
   open there (dialogue, a ring menu's title): the HUD stays at the bottom while it is open.

In `end_sim_frame` (`SoeEndSimFrame`, `src/main.c`), after the widescreen pass blacked the margins,
`SoeHudEndFrame` copies the captured pixels back, moved by the position and the layout.

Writes no guest state: WRAM, VRAM, CGRAM and OAM stay byte-identical with it on (checks below).
Switches: `SOE_HUD=0|1` (off, or on with the defaults), `SOE_HUD_POSITION=bottom|top|hidden`,
`SOE_HUD_LAYOUT=centered|edges`; `SOE_HUD_DEBUG=1` prints each capture; `SOE_HUD_SHOT=<dir>` with
`SOE_HUD_SHOT_FRAMES=<n,...>` saves whole wide fields (a script `dump` keeps only the 4:3 part).

Traps met on the way:
- At the field's BG3 scroll, the last line of row 31 is screen line 0, and rows 28..31 hold no
  map (other data). A first "top taken" test read row 31 and kept the HUD at the bottom forever.
- OAM slots 0/1 are not the portraits under a menu; capture by slot number alone moves the ring.

## MesenCE counterpart

None. The mod changes only how the frame is drawn; there is no guest state for a parity check to
compare, and MesenCE's Lua cannot take a layer out of its picture.

## Checks

| Date | What | Result |
|---|---|---|
| 2026-11-10 | Headless from a save (room `0x51`): field, a ring menu, a dialogue; positions Bottom, Top, Hidden in 4:3; Bottom and Top with Screen edges at 64 px margins; pictures compared by eye and by pixel | Top moves the HUD in the field and keeps it at the bottom under the ring menu's title and the dialogue; Hidden removes it in all three; Bottom + Centred is pixel-identical to off; Screen edges puts the boxes and portraits at the picture's edges |
| 2026-11-10 | Same runs: WRAM, VRAM, CGRAM, OAM of Hidden against off; two Hidden runs against each other | identical; repeatable |
| 2026-11-10 | `ctest` (8 tests), with `replay_0x_widescreen` now also running Top + Screen edges (`CMakeLists.txt`) | all pass |
