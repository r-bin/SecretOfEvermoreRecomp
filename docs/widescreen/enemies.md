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

Built 2026-11-09 for the experimental full-screen mode (step 10 of [progress](progress.md)); what the code does
and where it differs from this design: [Built](#e6-as-built) below. E5
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

### E6 as built

`src/soe_ws_alive.c` (decisions), `HleEntitySleepWake`, `HleFieldTickFree*`, `HleAiPickTarget`,
`HleEnemyAiCheckAggro`, `HleAiStationaryPlayer`, `HleSpawner`, `HleSpawnerSpawn` in `src/soe_bindings.c` (hooks), free time in
`src/soe_frame_driver.c`. Only the launcher's *Widescreen (experimental)* turns it on; `SOE_WS_ALIVE=0` is the kill
switch (`tests/replay_recorded.sh` sets it), `SOE_WS_FREE=0` switches off only the free time. Options: *Crowd in the
4:3 view* (3–30, default 6), *Count* (everyone / enemies only). Test knobs: `SOE_WS_ALIVE_RESERVE`,
`SOE_WS_ALIVE_DEBUG=1`, `SOE_FREE_STATS=1`, `SOE_WS_ALIVE_AFTER=<$0B19>` (E6 off until that logic frame, so a vanilla
replay stays in sync up to there).

* **Spawners: load with the room, then as in vanilla, gated by crowding; ghosts** (owner, 2026-11-10). A record's
  box (every 4:3 view that could hold its point: 256 × 224 either way; 2026-11-10, a box of one view let 18 of the
  east jungle `0x5b`'s 18 spawn on the load, this one 11 with 7 ghosts) is *full* with `cap` − 2 enemies (4: vanilla's budget less the Boy and
  the Dog; `cap` with Count = enemies only), placed or spawned, alive, not friendly. Each enemy counts at its home: a spawned one
  at its record's point (`+$8C`), a placed one where it stood on the load (2026-11-10: counted where they were, the
  enemies near the player chased or wandered off and every ghost spawned in a few walks); so a box has room again
  only when its enemies die.
  * *Room load* (its first 90 logic frames, the fade-in): `hle_func` at the spawner `$8F:DDBB` spawns, one per frame,
    every record whose box is not full, wherever its point is (the helper does `INC $6185` and the `PHA` that the ROM's
    spawn code at `$DE43` pulls; at a bound entry the interpreter runs those ROM bytes).
  * *Kept clear of the entry* (2026-11-10): a record whose point is in the 4:3 view or within half a view of it on
    the load stays a ghost, and the ROM's spawner may take it only after the camera has reached it (view or spawn ring)
    and left it behind beyond the ring. Entering the east jungle `0x5b` from the north: 8 ghosts, nothing spawns on the
    way in (vanilla's spawner had put the Wimpy Flower at (152,328) on the path 56 px below the first view), and the
    three near the exit spawn out of sight on the way back north.
  * *Afterwards* the ROM decides when and where (timer, the ring 16..48 px off the 4:3 screen), and `hle_func` at its
    spawn step `$8F:DE43` skips a record (on at `$DE3B`, the loop's next record, same stack) while its box is full or the
    player's 4:3 view holds `cap` characters (counted as for aggro). E6 takes nobody back, so walking through again
    spawns more only where it is thin.
  * *Ghosts* (`src/soe_ws_sprites.c`): every record with a count left is drawn in the margins as a dark silhouette of the
    largest frame seen on a live entity of its character (until one is seen, its first standing frame: a Wimpy
    Flower's is its burrowed patch). Only the side margins show them: a ghost above or below the 4:3 view is off the
    picture. Drawn in every extended mode, vanilla extended too (2026-11-10, the owner: the same ghosts there; drawing
    them only reads WRAM), thinned by a 4×4 ordered dither whose
    density follows the screen distance from the 4:3 edge (half from 32 px out, none at the edge), so it fades out as
    it comes into the 4:3 view and back in as it leaves; when it spawns, the enemy takes its place.
  * Checked with synthetic records in `0x38` (no spawner room is reachable from replay 06's save; the village guards its
    east exit): poked during the load, a count-6 record at a sparse point spawned 3 (box at 4), one at a full point none;
    poked later, records off the ring stayed ghosts, at 74 px out at half density, at 6 px out nearly invisible (32:9
    frame dump). Superseded: spawns anywhere out of sight after the load (2026-11-09); a gate counting only spawned
    enemies, no load spawning and a plain 50 % stipple (2026-11-10).

* **Placed enemies: held on a crowded load** (owner, 2026-11-10). Most rooms have no spawn record: the flower field
  `0x38` places 7 Mosquitoes and 14 Wimpy Flowers from its enter script (`+$8C` = 0), and vanilla keeps the far ones
  asleep where they stand. During the load the placed enemies (hostile, alive, `+$6C` < 0, not segmented) are taken
  nearest the Boy first; one whose box already holds the limit (taken placed ones and spawned ones) is *held*: put to
  sleep (`$C451`), kept asleep (`$C450`) and drawn as a ghost where it stands. It wakes once it is 16 px or more outside
  the 4:3 view, its box has room and the crowd (below) is under `cap`. One the game itself wakes is let be. The only
  state E6 keeps: held/taken per slot (with the character record, so a reused slot starts fresh) and the chasers (below),
  both forgotten on a room change. Checked: six Wimpy Flowers poked into one box before E6 started, 4 taken, 2 held as
  ghosts in the 32:9 margin.

* **Nobody sleeps.** `hle_func` at `$8F:C40F`: E6 off, the ROM routine; on, the helper does `LDY $4C` and the ROM code
  goes on at `$C450` (RTS: an awake entity stays awake) or `$C4B1` (wake). Asleep with `+$10 & $0078`, HP 0,
  `+$6C` ≥ 0 or a spawner record goes the ROM's way; a segmented body waits for a free segment list. `$1076` is
  written at every visit (spawner reserve; overrules the lag rule). Not tied to the widescreen field gate: with the
  gate, a menu or a narrow room in between let vanilla put the room back to sleep.
* **The aggro cap, not a cap on who is active** (owner, 2026-11-09: characters must never turn inactive; enemies
  must not aggro while the 4:3 view already holds 6). The three places an enemy takes the party as its target are
  hooked: `ai_pick_target` `$8F:D879` (chase, roll, shoot types), `enemy_ai_check_aggro` `$8F:D69B` (stationary types),
  `ai_stationary_player` `$8F:D715`. An enemy inside the 4:3 view (`$59`/`$5B` + 256 × 224) acts as in vanilla; one
  outside goes on at the routine's own "no target" exit while the view holds `cap` or more awake characters (Boy and Dog
  counted): `$D911` (`TDC TAX STA $24,Y RTS`, the caller wanders), `$D6C9` (`PLA`, JML default action), `$D766` (JML
  walk). The helpers do the first instruction natively (`LDX $0F42`, `LDA $002E,Y`), so the ROM path stays compiled.
  An attack begun before the view filled runs to its end (`+$78` = 7, set by `enemy_engage_target` `$8F:DC1E`).
  `+$24` is no aggro marker: it holds a party pointer for idle enemies too.
  *The crowd* (2026-11-10) is the awake characters in the 4:3 view plus the enemies outside it that chase the party:
  their last check at one of the three hooks let them, and `+$24` is still the Boy or the Dog. An enemy outside may
  aggro while the crowd, counting only chasers nearer the Boy than itself, is under `cap`, so the nearest keep their
  place and the farthest lets go. Before, only the view counted: all 7 Mosquitoes of `0x38` chased the Dog from across
  the room while the view held fewer than 6 (the flower field; the user's screenshot from the east jungle had both causes); now 3 chase and 4 let go. The
  spawn step and a held enemy's wake first used the same crowd; since 2026-11-10 they count the view alone (Skelesnails
  chasing from behind the cliffs held the crowd at 6 and no ghost spawned). Only `ai_pick_target` makes a chaser:
  stationary Wimpy Flowers keep a party pointer once let through.
* **E6 decides past the slice deadline too.** The helpers used to fall back to the ROM once the slice's deadline was
  reached, which let the ROM decide: 4 spawns of a walk through `0x5b` bypassed the density gate. E6's branches run
  in free time, where the clock is put back after every slice, so they now apply at the deadline as well; the ROM's
  way still stops there.
* **Free time: the logic is not limited to the SNES CPU's speed.** Whole-room AI does not fit the 65816's frame (the
  flower field ran at ~55 % speed with all 21 awake; the cost is spread over the per-entity work: animation, stamina,
  gravity, AI, pair overlap tests). So `per_frame_field_tick` from its `JSL script_scheduler_pass` (`$8E:E45C`) to its
  `JSL field_spare_time_loop` (`$8E:E4B0`) costs no emulated time: two `hle` lines there switch free time on and off
  and leave the `JSL` to the interpreter (exact with E6 off). In free time the frame driver holds the beam
  (`snes_beam_hold`) and puts the CPU clock back to where free time began after every slice, so no line, NMI, IRQ,
  HDMA or refresh sees the work; the host simply runs more CPU per frame (the recomp stays ~11× real time).
  * A slice that touched the APU keeps its time: an SFX handshake (`apu_command_handshake`) waits for the SPC700,
    which runs on the master clock.
  * Free time stays two lines clear of vblank (it does not start later, and ends when kept APU time gets there), and
    a new field never starts in it. **Trap:** the first version let a late frame's region run into the next field;
    the NMI handler then waited at `$80:8473` for the joypad read (`$4212` bit 0), which a held beam never ends.
  * A wait for the NMI reached inside it (`HleWaitForNmi`, `HleSpareTime`) ends free time first.
  * Starting the region at `$E464` (entity physics) left the game at ~81 %: the script scheduler before it runs the
    enemies' scripts.
* **Superseded the same day:** holding entities asleep at the edge of a full 4:3 view (drawn grey) made villagers turn
  inactive (user's screenshot); an adaptive active radius. Trap from the latter: the `$8F:C40F` hook is not called every
  frame (the round robin visits empty slots too), so a gap in `$0100` between two calls is not lag.

Measured on replay 06 (local; flower field `0x38`, 22 entities, 16:9), 2026-11-09:

| Run | Awake in `0x38` | `$0B19` per 1200 frames | Host |
|---|---|---|---|
| E6 off (vanilla) | 5 (17 asleep) | +1195 | 675 fps |
| E6 without free time, cap 6 | 21; at most 6 in view; `$1070` = awake + 2 throughout | +660 | — |
| E6 with free time from `$E464` | 21 | +978 | — |
| E6 with free time from `$E45C` (built) | 19–21 | +1200 | 668 fps |
| E6 with the aggro cap (built) | 17–20, none asleep; view 2–6; ~21,800 turn-aways; attacks from outside only while the view held < 6 | +1200 | — |

Replay 02 with E6 on runs through the same rooms (intro, dialogue, flight), a few frames ahead of vanilla because
vanilla's own lag frames are gone; no free-time runaway. Wide frames show the margin enemies; dialogue scenes clean.
