# Practice hotkeys

The practice ROM's cheats (everscript `in/practice/main.evs`) on Start + a direction while you play: no clip and the
Atlas glitch (the vanilla glitches, switched on and off), all items, weapon levels. Package `soe.practice.cheats`,
feature *Practice hotkeys* (group Cheats), off by default. Changes the game: an ordinary plugin, `soe.cheats`.

## Features and options

| Default chord | Cheat | WRAM it writes (the practice ROM's) |
|---|---|---|
| Start + Up | No clip, on / off | entity `+$11` bit `$04` (`attribute(.., NO_CLIP)`), held every field frame while on |
| Start + Right | Atlas glitch, on / off | `$0A3F`/`$0A41` (Boy), `$0A89`/`$0A8B` (Dog), entity `+$A0`/`+$A2` = `$EFFF`, `+$14` bit `$08`; held while on; off puts the game's own values back |
| Start + Left | All items, once | `unlock(ITEM.ALL)`: four purses 6,666,666, formulas `$2258-$225D`, charms `$2261-$2264`, weapons `$22DA-$22DB`, 99 of `$22FF-$2344`, ammo `$2345-$2347`, trade goods `$2517-$2530` = 99 |
| Start + Down | Weapon levels 3, once | `unlock(ITEM.WEAPON_LEVEL_3)` + `DOG_LEVEL_3`: `$0ADD-$0AF7` and `$0B07` raised to `$0300` |

Options: what each chord does (`up`, `right`, `left`, `down`: `noclip`, `atlas`, `items`, `levels`, `none`) and whom no
clip and the Atlas glitch apply to (`target`: `both`, `boy`, `dog`). A toast at the top left says what happened
(`snes_osd_push`: host only, not guest state). The toggles are plugin state: a save state does not hold them.

## How it works

Code: [src/soe_cheats.c](../../src/soe_cheats.c).

- **Hook:** `HleFieldTickFreeBegin` (`$8E:E45C`, per_frame_field_tick's JSL script_scheduler_pass; src/soe_bindings.c),
  once per field tick, right after the joypad matcher `$8C:F121` compared the held buttons `$0EC6`. Nothing fires on
  the title, in menus or in Mode 7. With the mod off the call returns at once and writes nothing.
- **Input:** `$0EC6` in `$4218` order (Start `$1000`, Up `$0800`, Down `$0400`, Left `$0200`, Right `$0100`). A chord
  fires once, when it first appears; holding it does nothing more. Start alone does nothing in the field.
- **Gate:** the Boy's `+$10 & $21` clear (not halted by a cutscene or a text box), the gate of everscript's
  `hacks/hotkeys`.
- **Switches:** `SOE_CHEATS=0` off (set by `tests/replay_recorded.sh`: the tests read the launcher's shared
  `state.toml`, the trap E6 met, widescreen/progress.md); `SOE_CHEATS=1` on with the defaults, without the launcher.

## MesenCE counterpart

[mods/mesen/soe.practice.cheats.lua](../../mods/mesen/soe.practice.cheats.lua): MesenCE 2.2.1, Debug > Script Window >
Open, with the US ROM running. Options are the locals `ACTIONS` and `TARGET` at the top. Same rules at the same
instruction: an exec callback on `$8E:E45C`, so both write the same WRAM before the same JSL. Differences: the message
is `emu.displayMessage` instead of the launcher's toast; a Lua script is not switched from a launcher.

## Checks

| Date | What | Result |
|---|---|---|
| 2026-11-10 | Recomp headless, isolated copy of the build with its own `state.toml`, save in hut room `0x51`: each chord, then Up and Right again | each wrote exactly the table's values; the second presses cleared the flags and put back attack `$0011`/`$0005` and `$0000` in the record. Feature off: none changed |
| 2026-11-10 | Same, Start held alone 30 field frames | `$0B19` kept counting: Start is free in the field |
| 2026-11-10 | Same, walk left 120 frames then down 120 | vanilla stops at the wall (x 575, y 169); no clip walked on to x 510, then down to y 337 |
| 2026-11-10 | `ctest` with the plugin built in | 8/8 |
| 2026-11-10 | Parity: replay 02 + `tools/mods/cheats-hotkeys.txt` (every chord, both toggles off again), MesenCE 2.2.1 with the Lua script against the recomp with `SOE_CHEATS=1`, again from a copy of the build whose own `state.toml` has the feature off | **same**: WRAM, VRAM, CGRAM and OAM identical at every 60th NMI to the end (key 24,174), both times |
| 2026-11-10 | Control: the recomp with `SOE_CHEATS=0` against that MesenCE recording; MesenCE's own vanilla run (`tests/recorded/02`) against it | both part at key 23,040 (good to 22,980): the first hotkey; the input is the same before it |
| 2026-11-10 | `ctest` with *Practice hotkeys* selected in the launcher | 8/8: `SOE_CHEATS=0` in `tests/replay_recorded.sh` keeps the selection out |
