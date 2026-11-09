# Gates: gameplay vs menus, ring menu dim, full-screen pages, dialogue

Tags `widescreen.gate`, `widescreen.menu`, `widescreen.window`. Marks: [README](README.md).
Rules P5/P6: [checklist](checklist.md).

## What decides "field gameplay" (P5)

No single game-mode byte is known. Signals:

| Signal | Address | What it proves | Mark |
|---|---|---|---|
| field tick ran this frame | `per_frame_field_tick` `$8E:E41E` (entry hook) | the field is drawn. Also runs while dialogue is open (dialogue routines call it) and in flight | V |
| menu page set-up / field restore | `$8C:B743` (forced blank, TM `$13`, CGADSUB `$41`, window/DMA reset) … `$8E:E08B` (TM `$17`, TS `$11`) | a full-screen page is up between the two | L (read in part) |
| Mode 7 flight | `$0E70` (flight state), `script_op_99_mode7_scene` `$8C:E66F` | flight | U |
| ring menu open | `$05A9` bit 15 | ring menu | U |

Proposed gate: field = field tick ran ∧ no menu page latched ∧ not flight. Implement both
markers with observational entry hooks (`snesrecomp/docs/ENTRY_HOOK.md`; they may not change state),
plus a cross-check on PPU registers (BGMODE 1, TM). Verify by watching the candidates with the
debug server while walking through title, intro, field, ring menu, every page below, shops,
dialogue, a save, flight (P5 "check").

## Full-screen pages (fallback to 4:3)

These pages are BG tilemaps drawn at scroll 0, so in a wide view the sides show leftover map. The
routines that call `$8C:B743` (from its CDL callers, L):

| Page | Entry |
|---|---|
| Save / load menu | `script_op_8c_save_menu` `$8C:E39A` |
| Name entry | `script_op_7f_name_entry` `$8C:E17A` |
| Alchemy selection page | `script_op_63_select_alchemy` `$8C:DF75` |
| Status (Dog entry) | `status_menu_open_dog_entry` `$8E:9325` |
| Options | `ring_menu_options_open_entry` `$8E:95A6` |
| others | 6 more callers of `$8C:B743` not listed yet |

Fallback options (setting "Menu pages"): pillarbox black (`PpuSetExtraSpaceCentered`,
`ppu.h:758`, then black margins) or a blurred, darkened copy of the last field frame in the
margins (presenter only). Shops and dialogue run on the field ([dialogue](#dialogue)).

## Floating menu window

Instead of a pillarbox, show the page as a 256 × 224 window floating over the last gameplay frame.

What the page does to the PPU (`$8C:B743`, L — values read, order of the stores not traced
instruction by instruction): it forces blank, rewrites the BG map and character bases
(`$2107`, `$2108`, `$210B`) and BGMODE (`$2105`), TM `$13`, CGADSUB `$41`, uploads palettes and tile
blocks (`$80:8650`) — the field keeps BG1's map at VRAM `$0000`, so the page draws **into the
room's VRAM**. The map behind is gone; when the page closes, `$8E:E08B` restores the field setup
(BG1SC `$00`, BG2SC `$04`, BG3SC `$08`, BG12NBA `$22`, BGMODE `$39`) and the field is redrawn.

So the window must be **snapshot-backed**, all in the presenter:

1. Keep the last composed wide field frame (the host already holds it).
2. On the open marker (entry of `$8C:B743`) freeze it as the background; on the close marker
   (`$8E:E08B`) drop it. While forced blank / fading, keep the snapshot (dim it with the fade).
3. Draw the page's native 256 × 224 output centred, with a border and drop shadow; the snapshot
   in the margins dimmed (and optionally blurred).

| Option | Look | Needs |
|---|---|---|
| Pillarbox 4:3 | black sides | `PpuSetExtraSpaceCentered` |
| Snapshot-backed floating window | frozen field around the page | presenter snapshot + markers |
| Blurred snapshot | same, blurred and darker | + blur pass |

Purely renderer-side: no guest state changes, off = 4:3 identical. The page's own fade-in/out
(INIDISP) should also fade the snapshot ([fades](fades.md)). Fallback if a marker is missed: the
gate's PPU check (BGMODE ≠ `$39`) switches to pillarbox.

## Ring menu dim (unknown)

How the ring menu darkens the field was not found. Checked: it does not use the brightness fade
target `$0B83` with a partial value (all writers set full `$800F` or `$8000`, V). Candidates: colour
math with a fixed colour (the field's CGWSEL/CGADSUB come from the room header, `$0F82`/`$0F83`,
applied by `$8C:E247`, V; the dim may change them), or a palette cross-fade (`$90:D481`, `$150C..`,
U). Either way the recomp PPU applies colour math and palettes per pixel across the widened line, so
the margins dim the same — **if** no window limits the effect to the 256 columns. Check with a
frame dump of the open ring menu at 16:9; if the margins stay bright, extend the window
(`PpuSetWidescreenWindowExpansion`, `ppu.h:858`).

## Dialogue

Dialogue windows are BG3 (`$7F:C800`, U) over a live field (the field tick keeps running). Keep the
field wide and the window centred ([hud-and-adaptive-ui](hud-and-adaptive-ui.md)); scripts that
stage actors during dialogue are a cutscene question ([cutscenes](cutscenes.md)).
