# Headless Exploration: Reaching Any Scene Without Playing

How to drive the recompiled game (and snes9x) to a room, an enemy or a glitch from a script,
reproducibly. Found while hunting the HUD-portrait corruption (v0.201.17–19); every step below
was verified in that hunt.

The idea: **read where the game is from WRAM, read where to go from the room data in the ROM,
write the Boy's coordinates next to the target and let the game walk the last step.** No
walking routes, no menus beyond the title, and every run from a save state is deterministic.

---

## 1. Into the Game

Headless scripts are the runner's `--script` files (commands in [debugging.md §5.1](debugging.md)).

| Need | How |
|---|---|
| Start from the player's progress | Continue the in-game save (`saves/save.srm`) from the title: `wait 700`, `press start 2`, `wait 400`, `press start 2`, `wait 150` opens the load list with the first save **already selected**; `press b 4`, `wait 60`, `press b 4` (select, confirm), `wait 500`. Pressing down first selects an empty slot. |
| A save state of a moment | There is no `savestate` script command. Set `Autosave = 1` in `config.ini`: the host loads slot 0 on start and **saves slot 0 on exit**, so a script that ends in `quit` leaves `saves/save0.sav`. Copy it aside and set `Autosave = 0` again, or every later run starts from it. |
| Start every run from that moment | `loadstate 0` as the first line. Since v0.201.18 states carry the CPU and resume PC, so this works on frame 0. |
| Same thing on snes9x | `node tools/recomp/s9x-run.js <rom> <save.srm> <script> <outdir>`: same commands; start with the title sequence above (it ignores `loadstate`). |

A run from a state is **deterministic**: the same script gives byte-identical dumps. Keep that
property when narrowing a bug down (§5).

## 2. Where Am I?

All from a `dump` (`<tag>.wram.bin`, 128 KB, offset = address − `$7E0000`):

| Address | What |
|---|---|
| `$7E0ADB` | room id (`soe://localization/maps/index.md` names them) |
| `$7E4EA3` / `$4EA5` | Boy x / y in room pixels (entity record `$7E4E89`, +`$1A`/`$1C`; HP at +`$2A` = `$4EB3`) |
| `$7E4F51` / `$4F53` | Dog x / y (record `$7E4F37`; HP `$4F61`) |
| `$7E0112` / `$0114` | camera x / y: screen position = entity − camera |
| `$7E0F86` / `$0F88` | room origin in 16-px cells (also bytes 0/1 of the room's data blob) |
| `$7E0100` | NMI frame counter; `$7E0B19` field frame counter. If `$0100` rises but `$0B19` does not, the game logic is frozen. |

## 3. Where Can I Go? (`tools/recomp/room-nav.js`)

```sh
node tools/recomp/room-nav.js out/rom.sfc 25            # table: triggers with destinations, enemy spawns
node tools/recomp/room-nav.js out/rom.sfc 25 --script   # the same as ready-to-paste script blocks
```

It reads the room through `buildRoomScriptModel` (`src/script/room-scripts.ts`):

- **Step-on triggers** are stored as boxes in absolute 16-px cells. Pixel box =
  `(cell − origin) × 16`. The destination room and landing point come from the trigger
  script's room change (`transitions`).
- **Enemy spawns** come from the room's enter script (`enter.spawns`), in **absolute 8-px
  units**: pixel = unit × 8, no origin. The walk follows every branch, so treat them as
  candidates; which ones exist depends on story flags.

The same data is readable through MCP: `soe://rom/assets/maps/<room>/scripts/index.md`
(triggers) and `.../enter.json` (`spawns`).

## 4. Moving and Fighting

**Changing rooms.** Write the Boy's x/y about 24 px *outside* a step-on box and walk into it:

```text
pokefor 4ea3 a002 3     # x = 0x02A0 = 672 (little-endian bytes)
pokefor 4ea5 f800 3     # y = 0x00F8 = 248, 24 px above the hut exit at y 272
press down 40           # the trigger fires only while the player walks onto it
wait 250                # fade, load, fade in
```

Standing on the box after a teleport does nothing. `room-nav.js` picks the side facing into the
room: vertical entry for wide boxes, horizontal for tall ones.

**Camera.** The camera follows only after a few walked frames: after a long teleport, walk
(`press down 12`, `wait 60`) before looking at the picture, or the Boy is off screen.

**Fighting.** Enemies only take hits from a few pixels away. What worked on Wimpy Flowers:
teleport 44 px above (camera), walk, wait, then put the Boy **16 px above** the enemy, Dog beside
him, `press down 1` to face it, and swing with `press b 3` / `wait 26`. Lock HP so the run
survives: `forcepoke 4eb3 1e00` (Boy 30), `forcepoke 4f61 2400` (Dog 36). A killed enemy stays
dead; move on to the next spawn rather than swinging at nothing.

Pitfalls met on the way:
- Teleporting into a wall or the canopy hides the Boy (dark screen, no sprite): not a glitch.
- Writing coordinates during a fade-in (too short a `wait` after a load) is ignored.
- `wait`, `press` (+1 idle frame) and `pokefor` consume frames; `dump`, `until`, `turbo` do not.

## 5. From "it glitches sometimes" to the Frame

1. **Make it happen in a script.** A tour over every spawn of a room with ten swings each
   (the HUD bug showed from fight 5).
2. **Find a cheap signal**, e.g. a hash of the 32 bytes of one OBJ tile
   (`VRAM $C000 + tile × 32`), or a WRAM invariant. `node tools/check-obj-tiles.js
   --vram <dumps>` checks the OBJ tile cache lists and hashes the portrait tile.
3. **Check the reference.** Run the same script through `s9x-run.js`. Glitch absent there =
   a recomp problem.
4. **Narrow down without changing timing.** Coarse dumps per fight, then per swing, then per
   frame. Replace a `wait 26` by `wait 6`/`dump`/`wait 6`/`dump`/`wait 6`/`dump`/`wait 8`/`dump`:
   dumps take no frames, so the run stays identical. Changing the waits (6×4 = 24) changed the
   outcome once and hid the bug.
5. **Look at the bad frame.** The dump's `ppuw.tsv` shows the VRAM addresses written in vblank,
   `oam.bin` which sprite uses which tile, WRAM queues show what the game asked for. Diff the
   previous frame's WRAM to see which writes went wrong.
6. **Native code or runtime?** `SNESRECOMP_LLE_BOUNCE=0` (no bounces into compiled bodies)
   should change nothing; if it removes the bug, a compiled body is wrong. Bisect the compiled
   entries ([debugging.md §5.4](debugging.md)) and keep the culprit on the interpreter with
   `force_lle`.

## 6. What the Scratch Tools Were

Throw-away helpers during the hunt that are now proper tools or documented steps:

| Hunt helper | Now |
|---|---|
| reading trigger boxes and spawns by hand | `tools/recomp/room-nav.js` |
| list-consistency check over dumps | `tools/check-obj-tiles.js` |
| same script on snes9x | `tools/recomp/s9x-run.js` |
| a tour generator (teleport + 10 swings per spawn) | `room-nav.js --script`, one block per spawn |
| bisection over compiled entries | the procedure in [debugging.md §5.4](debugging.md) |
