# Practice hotkeys

The mod package `soe.practice.cheats` (feature *Practice hotkeys*, group Cheats, off by default) gives the practice
ROM's cheats on Start + a direction while you play. Code: [src/soe_cheats.c](../src/soe_cheats.c); manifest:
[mods/preloaded/packages/soe.practice.cheats](../mods/preloaded/packages/soe.practice.cheats/1.0.0/manifest.toml).
It changes the game (an ordinary plugin, not presentation-only).

| Default chord | Cheat | WRAM it writes (the practice ROM's, everscript `in/practice/main.evs`) |
|---|---|---|
| Start + Up | No clip, on / off | entity `+$11` bit `$04` (`attribute(.., NO_CLIP)`), held every field frame while on |
| Start + Right | Atlas glitch, on / off | `$0A3F`/`$0A41` (Boy), `$0A89`/`$0A8B` (Dog), entity `+$A0`/`+$A2` = `$EFFF`, `+$14` bit `$08`; held while on; off puts the game's own values back |
| Start + Left | All items, once | `unlock(ITEM.ALL)`: four purses 6,666,666, formulas `$2258-$225D`, charms `$2261-$2264`, weapons `$22DA-$22DB`, 99 of `$22FF-$2344`, ammo `$2345-$2347`, trade goods `$2517-$2530` = 99 |
| Start + Down | Weapon levels 3, once | `unlock(ITEM.WEAPON_LEVEL_3)` + `DOG_LEVEL_3`: `$0ADD-$0AF7` and `$0B07` raised to `$0300` |

Options: what each of the four chords does (any cheat or nothing), and whether no clip and the Atlas glitch apply to the
Boy, the Dog or both. A toast at the top left (`snes_osd_push`, host only, not guest state) says what happened.

## How it runs

- **Where:** `HleFieldTickFreeBegin` (`$8E:E45C`, src/soe_bindings.c), once per field tick, right after the joypad
  matcher `$8C:F121` compared the held buttons `$0EC6`. So nothing fires on the title, in menus or in Mode 7. The hook
  existed for widescreen E6; with the feature off the call returns at once and writes nothing.
- **Input:** `$0EC6` in `$4218` order (Start `$1000`, Up `$0800`, Down `$0400`, Left `$0200`, Right `$0100`). A chord
  fires once, when it first appears; holding it does nothing more.
- **Gate:** the Boy's `+$10 & $21` clear (not halted by a cutscene or a text box), the gate of everscript's
  `hacks/hotkeys`.
- **Kill switch:** `SOE_CHEATS=0`. `tests/replay_recorded.sh` sets it: the tests read the launcher's shared
  `state.toml`, so a selection made for play must not reach them (the same trap as E6, widescreen/progress.md).

## Checked (2026-11-10, headless, isolated copy of the build with its own `state.toml`)

From the save in hut room `0x51`: each chord wrote exactly the table's values; the second press of Up and Right cleared
the flags and put back attack `$0011`/`$0005` and `$0000` in the record. With the feature off the same script changed
none of them. Start held alone for 30 field frames: `$0B19` kept counting, so Start is free in the field. Walking left
120 frames then down 120: vanilla stops at the wall (x 575, y 169); with no clip the Boy walked on to x 510 and down to
y 337.
