# Widescreen

Shows more of each room to the sides. Package `soe.enhancement.widescreen`, three alternative features (group Display),
off by default. The research, settings and every measurement are in [../widescreen/](../widescreen/README.md); this
page only tracks the mod.

## Features and options

| Feature | Plugin | Changes the game | What |
|---|---|---|---|
| `widescreen` *Widescreen (vanilla)* | `soe.widescreen` (presentation) | no | the room's map in the margins; characters only where the game draws them |
| `extended` *Widescreen (vanilla extended)* | `soe.widescreen` (presentation) | no | also the characters and projectiles the game holds in WRAM, drawn in the margins |
| `experimental` *Widescreen (experimental)* | `soe.widescreen` + `soe.widescreen.alive` | yes | also E6: the whole room alive, enemy logic in free time, an aggro cap on the 4:3 view |

Options (each feature has its own copy): aspect (adaptive, 16:10, 16:9, 21:9, 32:9), room edges, foreground; extended
and experimental also the look of characters in the margins; experimental the crowd cap and what counts. The manifest
has the wording; [../widescreen/progress.md](../widescreen/progress.md) what each does.

## How it works

`src/soe_widescreen.c` (margins, prefill from the room grid, field gate), `src/soe_ws_sprites.c` (characters from
WRAM), `src/soe_ws_alive.c` (E6, hooks in `src/soe_bindings.c`). Kill switches `SOE_WS_*`
([../widescreen/README.md](../widescreen/README.md#settings)); the replay tests set `SOE_WS_ALIVE=0`.

## MesenCE counterpart

None, and none possible for the picture: MesenCE renders a 256 px SNES frame and a Lua script draws only inside it, so
there is nowhere to show the margins. E6 alone (nobody sleeps, the aggro cap) could be hooked from Lua at the same
addresses, but its free time (enemy logic that costs no emulated time) cannot: on MesenCE the frames would lag where the
recomp's do not, so the two would not do the same. Not built.

## Checks

| Date | What | Result |
|---|---|---|
| 2026-11-09 | `replay_01_widescreen`, `replay_02_widescreen` (ctest): the stored MesenCE runs with widescreen on, 21:9 | identical: the vanilla modes change only the picture |
| 2026-11-09 | the rest | [../widescreen/progress.md](../widescreen/progress.md) |
