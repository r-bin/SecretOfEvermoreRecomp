# Combat RNG

Shows every dice roll of a fight over the character it lands on: whether a swing hit or missed and at what
chance, a swing that whiffed and by how many pixels, a swing that reached a target that could not be hit and
why, where the damage fell in its range, and how long the knockback rolled. A label is green when the roll
went the player's way and red when not, white in between; it floats up from the character's head and fades
after 1.5 s. Package `soe.debug.combat_rng` 1.0.0, feature `combat_rng`, plugin `soe.combat_rng`, off by
default. Presentation only.

## Features and options

| Option | Values |
|---|---|
| Detail | **Short** (default): one term per roll. **Detailed**: the numbers. |

| Roll | Short | Detailed | Green when |
|---|---|---|---|
| To-hit | `HIT`, `MISS` (a certain hit shows nothing) | `HIT 59% ROLL 51`: the chance, then the roll on the same 0–100 scale; a hit when the roll is below the chance | the party's swing hits, an enemy's misses |
| Damage | `HIGH DMG` / `LOW DMG` in the top or bottom quarter of the range, else nothing | `DMG 10 (6-10)`: the damage and its range at this charge | high from the party, low on the party |
| Knockback | `LONG KB`, `SHORT KB` (outer thirds), `KB` | `KB +28 (8-47)`: the rolled hold and its range, in ticks; the knockback's fixed part comes on top | short for the party, long for an enemy |
| Whiff | `WHIFF` (once an attack) | `WHIFF 7PX`: how far the strike box fell short of the nearest target within 32 px | an enemy whiffs |
| Reached, no roll | `IMMUNE` | the first gate the game's sweep stopped at: `UNTARGETABLE`, `INVULNERABLE` (mode `+$16` bit `$20`), `LOCKED 15F` (the same attacker rolled on it less than 21 ticks ago: the later strike frames of one attack), `BUSY` (`+$14 & $070D`), `OTHER LEVEL`, `HEIGHT` | an enemy's swing is stopped |

`LOCKED` is shown only in detailed mode: in short mode it would mark every attack with several strike frames.
Key `9` (while the mod is on) cycles short, detailed and off, with an OSD toast; a save state does not keep it.

## How it works

Two existing `hle` entries of `src/soe_bindings.c` call the plugin at their entry; it reads, never writes:

- **`$80:859B` random_number_generator.** The return address the JSL left at S+1..S+3 says who rolls:
  `$8F:BA9A` the to-hit roll (DP `$12` the threshold), `$80:AD6B` with `$8F:C08F` at S+5 the damage roll
  (DP `$02` = base + 1), `$90:8387` the animation command `hold_random` (`[DP $5D]` → range, base); Y is the
  target and DP `$4C` the attacker. The result is read from `$7E:0133`, where the generator also leaves it, at
  the next roll or at the frame's end.
- **`$90:8124` anim_start_record.** X equal to the entity's record `+$40` is a knockback start
  (`$8F:C2C4`); a `hold_random` before the entity's next animation start is the knockback's.

Strikes that rolled nothing leave no trace, so at the frame's end each awake entity's tick is replayed
(`src/soe_anim_walk.c`, shared with the debug overlay's strikes layer) and the strike box is tested against
the other side as the game's sweep `$8F:B5FC` does; positions are read after the tick, so a whiff's pixels
can be off by a step. The facts and their evidence: everscript-vscode `docs/entities/combat.md` §4–§6 and
§10, and the head comment of `src/soe_combat_rng.c`.

`SoeCombatRngEndFrame` runs in `end_sim_frame` after the debug overlay and before the HUD mod. Field frames
only. Switches: `SOE_COMBAT_RNG=0|1`, `SOE_COMBAT_RNG_DETAIL=short|detailed`, `SOE_COMBAT_RNG_LOG=1` (each
label on stderr with its frame and entity).

## MesenCE counterpart

None: presentation only, no guest state to compare.

## Checks

| Date | What | Result |
|---|---|---|
| 2026-11-11 | Headless from the save, room `0x38`, the Boy beside the Wimpy Flower at 232,280 (10 swings, 600 frames standing, 3 swings from 44 px), HP locked; detailed and short | Boy on flower `HIT 100%`, `DMG 12 (7-12)` at full charge and `DMG 0 (0-1)` at a few % charge (the game's popup showed 0), flower `KB +74 (40-79)`; flower on Boy `HIT 59% ROLL 51`, `DMG 10 (6-10)`, `KB +28 (8-47)`, `LOCKED 15F` on its next strike frame, later `MISS 59% ROLL 97` and `INVULNERABLE`; the Boy's swings from 44 px `WHIFF 7PX`/`13PX`: all as docs/entities/combat.md predicts. Labels read clearly and stack without overlap. The same run twice gives the same labels |
| 2026-11-11 | The same run with the mod on and off | WRAM, VRAM, CGRAM and OAM identical at both dumps |
| 2026-11-11 | `ctest` (build/, reconstructed C on, mod off) | 8 of 8 pass, replays 01 and 02 in sync with the stored MesenCE runs |

## Missing

- **Not checked**: the Dog's attacks, projectiles and spells (the to-hit and damage rolls go through the same
  code and are shown; their whiffs are not), bosses, enemies with other hit boxes.
- **Knockback** shows the rolled part only; the fixed holds of the animation come on top (the Boy's 54 ticks).
- **Alchemy** rolls its own damage elsewhere and is not shown.
