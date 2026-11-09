# Enemies off screen: activation, spawners, collision, entity kinds

Tags `widescreen.activation`, `widescreen.spawn`, `widescreen.entity`, `widescreen.collision`.
Marks: [README](README.md). What each mode does with this: [modes](modes.md).

## Entity slots and lists (V)

30 slots at `$7E:3DE5` (stride `$8E`), Boy `$4E89`, Dog `$4F37`. Awake list head `$3DDF`, asleep
list head `$3DE1` (link `+$5E`). `$8F:B29B` moves a record between them; asleep entities are not
visited by the per-frame entity pass: no AI, not drawn. New slots start asleep (`$8F:B0A4`, `$1072`
+ 1). The Dog is created asleep and woken by the first pass.

## Sleep and wake (V)

Scheduler tick `$8F:DC34` (named after the Dog's sniff timer):

* lag rule: after 3 frames that missed the frame counter (`$0100 − 1 ≠ $1078`), the budget `$1076`
  = awake count `$1070` (if ≥ 5); after 1800 clean frames `$1076` + 1 (cap 6). Initial: `$1070` = 2
  (Boy, Dog), `$1076` = 6;
* round robin: 6 slots per frame from `$0EBC`, calling `entity_sleep_wake` (`$8F:C40F`).

`entity_sleep_wake`, positions relative to `$59`/`$5B`, signed:

| State | Condition | Result |
|---|---|---|
| awake | `+$10 & $0030`, HP 0, `$1070 < $1076`, or `+$6C` ≥ 0 (holds a multipart/waypoint node) | stays awake, no distance test |
| awake | otherwise, outside [−48, 304) on X or Y | spawner-made (`+$8C` ≠ 0): despawn (`$8F:DF41`); else sleep (`$8F:C514`) |
| asleep | HP 0 | freed unless `+$3E` links it |
| asleep | `+$10 & $0008` | never wakes |
| asleep | `& $0070`, `+$6C` ≥ 0, `$1070 + 2 < $1076`, or `+$8C` ≠ 0 | wakes at once |
| asleep | otherwise, inside [−16, 272) on X and Y | wakes, restarts its stand animation |

So with few enemies awake (budget 6 − Boy − Dog), **every enemy runs its AI anywhere in the room**;
only with the budget full do far enemies freeze.

## Spawners (V)

Script opcodes `0x3C`, `0xBA`, `0xA2` create an entity directly; `0xC2` creates a spawn record
with its count from `$2433` (everscript-vscode `docs/scripts/enemy_spawns.md`). The spawner
`$8F:DDBB` (named after Dog foraging) visits 10 of the 60 records (`$5DC3`, 16 bytes; `+$02`/`+$04`
point, `+$08` timer, `+$0C` count) per frame and spawns when:

* count > 0 and timer expired;
* `$1070 < $1076` and `$1074 < $2435` (room cap);
* the point is **16..48 px outside the screen**: inside [−48, 304)² but not inside [−16, 272)².

Then count − 1, timer re-rolled with an RNG byte, the entity keeps the record in `+$8C`. When it
drifts beyond 48 px with the budget full, `$8F:DF41` returns it (timer 0, count + 1) and frees the
slot. The spawn decision uses no RNG; only the next timer does.

Consequence: in 16:9 (43 px margin) spawns appear **inside the visible margin**; in 21:9 (96 px)
despawns happen in plain sight. Room entry is identical to vanilla as long as the spawner, the
windows and the budget are untouched.

## Collision is whole-room (V)

`tile_property_at_pixel_xy` (`$90:9815`): index = 2·(((Y & `$FF0`) >> 4)·width + ((X & `$FF0`) >> 4))
into `$7F:0000`, collision word `$7F:0004 + grid word`. `probe_metatile_collision` (`$8F:AF48`):
trail followers (`+$10 & $0800`) skip everything; phasing (`$0400`) only tests the room-edge box;
otherwise grid, elevation gate, `$90:9561` (room-edge box + grid walk) and bodies (`$8F:B46D`,
skipped with `$0080`). The room-edge box `$0F48..$0F4E` is 14/7 px inside the room, written once by
`$90:8F80`. Nothing depends on the camera.

**Refuted:** collision is not limited to the viewport; `$08FA..` are streaming buffers.
**So a "respect whole-map collision" toggle is not needed**: awake off-screen enemies already
collide correctly. What would change behaviour is waking more of them (E2/E5 in [modes](modes.md)).

## What if (vanilla code unchanged unless said)

| Scenario | Vanilla code | Seen in a wide view (sprites drawn in margins) | Game state / replay |
|---|---|---|---|
| Margin enemy walks into a wall | whole-room collision | correct | unchanged |
| Margin enemy aggros the Boy | world distance (`$8F:D8D2`, U) | approaches from where it is drawn | unchanged |
| Weapon hits a margin enemy | world hit test (`$8F:B5FC`, U) | hit lands | unchanged |
| Alchemy targets a margin enemy | `$8C:8658` rejects beyond 8 px | cannot target a visible enemy | widening = E3 |
| Asleep enemy in the margin | invisible until 16 px from the 4:3 edge | pops in inside the margin | unchanged |
| Spawner fires | 16..48 px off the 4:3 edge | appears in the margin | unchanged; widening moves spawns and RNG |
| Spawner enemy leaves | despawned beyond 48 px (budget full) | vanishes in sight (21:9) | unchanged |
| Dog in the margin | always awake while active | fine | unchanged |
| Enemy at a room or limit edge | camera stops at the box | margin past the box | [camera-and-limits](camera-and-limits.md) |
| Doors, step triggers in the margin | fire on the Boy's position | seen earlier, fire as before | unchanged |
| Boss arenas | bosses never sleep (`+$6C`) | box edges visible | P2b policy |
| More sprites | OAM 128, `$D8` | flicker | only with E1 |

## Entity kinds (`widescreen.entity`)

| Kind | WRAM test | Mark |
|---|---|---|
| Boy, Dog | slot `$4E89`, `$4F37` | V |
| Script-placed enemy | enemy slot, `+$8C` = 0 | V |
| Spawner-made enemy | `+$8C` ≠ 0 | V |
| Friendly / NPC | `+$10 & $0002` (no contact) or `& $0004` (party side) | U (`docs/entities/entities-reference.md`) |
| Never sleeps (bosses, scripted) | `+$10 & $0010`, or `+$6C` ≥ 0 | V |
| Script-driven actor | a script slot `$28FC` `+$0D` points at it | L |
| Spawn point (no entity yet) | record count `+$0C` > 0 | V |

Per-kind policy for every mode: [modes](modes.md).

## Keep the whole room alive (experiment E5)

Limits: 30 slots; budget `$1076` with the lag rule; 6 slots per frame in the round robin; OAM 128
and `$D8`; 32 sprites / 34 tiles per line on hardware (the recomp PPU can lift it, the game's own
budgets stay). The engine is time-sliced, so more work moves slice boundaries and everything after.

Known consequences: enemies roam far from their areas, aggro chains, more RNG drawn (drops, crits,
spawn timers all change), the lag rule fights the change. Unknown: kill counters or flags tied to
enemies, scripts that wait for an enemy to sleep, rooms that open when cleared. Not comparable with
MesenCE.

Hooks (no hand edits of `src/gen`, no framework change): `hle_func` on `$8F:C40F` and `$8F:DC34`
with bound C and a "never sleep" variant, or a frame callback forcing `$1076` (guest write, so not
a presentation plugin). Test: per room from an entry save state, run N frames with E5 off and on,
diff WRAM outside the entity pool and list differing story-flag bytes (`$2200..`); while E5 is on,
write SRAM only to a separate slot.

## The whole room alive, six in the 4:3 view (experiment E6)

Proposal, 2026-11-09, for the experimental full-screen mode (step 10 of [progress](progress.md)). E5
with one rule added: the native 256 × 224 view never holds more than vanilla's crowd. Facts it rests
on are the V rows above; what it adds is marked as a design, and the open questions are U.

**Goals.** (1) Every enemy and NPC of the room runs its AI all the time, wherever it is. (2)
Spawners fire as in vanilla: same ring 16..48 px off the 4:3 view, same timers, same room cap. (3)
The 4:3 view `$59`/`$5B` + [0, 256) × [0, 224) holds at most **6 counted entities**, Boy and Dog
included, as vanilla's budget `$1076` counts them, so a fight in the old screen is as crowded as
vanilla allows and no more. (4) Scripts never miss a free slot.

**Three states per entity** (host-side table, indexed by slot, reset on room change `$0ADB`):

| State | Game side | AI | Drawn |
|---|---|---|---|
| free | awake, outside the view | yes | in the margins (vanilla extended drawing) |
| admitted | awake, counts against the view cap | yes | as the game draws it |
| held | asleep through the game's own `$8F:C514` | no | in the margins, frozen in its last frame, greyscale (the *Characters in the margins* look) |

**Rules, once per frame in the scheduler tick** (all 30 slots, not 6 by round robin):

1. *Never sleep by distance.* An awake entity outside [−48, 304) stays awake; a spawner-made one is
   not returned to its record for distance (rule 4 does that).
2. *The door.* A free entity whose position (`+$1A`/`+$1C` minus the camera) enters [−16, 272) ×
   [−16, 240), vanilla's wake window, is admitted if fewer than 6 are admitted, else held at that
   spot. An admitted entity becomes free again only past [−48, 304), vanilla's sleep window: the
   same hysteresis vanilla has, so nothing flickers at the edge.
3. *Re-admission.* When an admitted entity leaves, dies (HP 0) or is freed, the held entity that
   has waited longest is woken (tie: nearest to the Boy, then lowest slot). No RNG, no host clock:
   the choices follow from WRAM alone, so a replay of the mode reproduces itself.
4. *Slot reserve.* Keep 4 slots free for scripts (opcodes `0x3C`, `0xBA`, `0xA2` create entities
   directly). The spawner is not changed: its gate `$1070 < $1076` is steered by writing `$1076` =
   `$1070` + (free slots − reserve), floor `$1070`. If a script still finds the pool short, the
   spawner-made entity farthest from the view is returned to its record through `$8F:DF41` (timer 0,
   count + 1), as vanilla does with far ones.
5. *Exempt, never held:* `+$10 & $0030`, `+$6C` ≥ 0 (bosses, multipart, waypoint nodes), HP 0, the
   Boy, the Dog, and actors a script slot `$28FC` `+$0D` points at. They count against the cap but
   may push it over 6 (a boss arena keeps its adds).
6. *Camera pushes.* When the camera, not the entity, brings more than 6 into the view (the Boy runs
   into a crowd), the excess farthest from the Boy is held; exempt ones and anything that hit or was
   hit in the last 60 frames are kept.
7. *Lag rule off.* The budget is no longer lowered after missed frames (it would fight rule 4).
8. *Gate.* Only on field frames with the player in control ([menus-and-gates](menus-and-gates.md));
   in cutscenes ([cutscenes](cutscenes.md)) every held entity wakes and the tick runs vanilla.

**Why hold = asleep.** It is the state vanilla already puts entities in when its budget is full,
the wake path restarts the stand animation, and asleep entities get neither AI nor drawing, so
nothing new runs in the game. Vanilla does it with a hidden entity; here the renderer still shows it.

**Hooks** ([bindings](bindings.md); no hand edits of `src/gen`, no framework change): a variant of
`entity_sleep_wake` (`$8F:C40F`) that asks the state table instead of the distance windows, and
of the scheduler tick (`$8F:DC34`) that runs rules 2–7 over all slots. Both routines are G1-rated C in
everscript-vscode since v0.399.16/.17, but both call routines that are still asm (`soe_call_rts`/`soe_call_rtl`),
which no binding here supports yet: see [bindings](bindings.md). The spawner `$8F:DDBB` stays
the ROM's. Both bindings come from everscript-vscode's reconstructed C, proven equal to the ROM
with the switch off before the variant is written (replay 01). Switch `SOE_WS_ALIVE` (E5) gets a
value `view6`; the cap and the reserve are options of the experimental feature.

**Open (U), to settle before building:**

* Is sleeping a spawner-made entity safe? Vanilla never does it (it returns them, `+$8C` ≠ 0 wakes
  at once). If not, hold it by returning it to its record at the door, which looks like a despawn in
  the margin.
* What `$8F:B0A4` does when no slot is free: a script that fails there may soft-lock a room.
* Segmented bodies (record `+$07` bit 0; Tar Skull, Salabog) take one of ten segment lists at `$58AF`
  when they wake. With all ten taken the wake fails after `$1070` + 1 / `$1072` − 1 (V, `$8F:C4D6`): the
  counts drift. Rule 3 must not wake a segmented body while no list is free.
* Sound: 30 entities' SFX across the room. Mute or pan by distance in `$90:8921` / `$90:88BC` (E2/E5
  audio mitigation).
* Frame time: the engine is time-sliced; 30 awake entities may not fit a frame, and the game slows
  as it would on hardware. Measure per room before deciding on more CPU time per frame.
* Rooms that open when cleared, kill counters, scripts that wait for an enemy to sleep: the
  per-room WRAM diff of the E5 test finds them.

**Test.** E5's per-room test, plus a host-side check each frame: admitted ≤ 6 unless an exempt
entity is in view, free slots ≥ reserve after the tick, and no entity drawn by the game while held.
Spawner rooms first, then a boss arena and a town.
