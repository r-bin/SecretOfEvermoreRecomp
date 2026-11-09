# Enemy modes: vanilla, hybrid (ghosts), experimental

Tags `widescreen.entity`, `widescreen.activation`, `widescreen.spawn`. Facts: [enemies](enemies.md),
[sprites-and-cull](sprites-and-cull.md). Marks: [README](README.md).

## Vanilla (default)

Only backgrounds fill the margins. All world sprites are clipped to the native 256 columns
renderer-side; HUD sprites follow the UI setting ([hud-and-adaptive-ui](hud-and-adaptive-ui.md)).
No guest hook. Enemies appear exactly where and when vanilla shows them, including spawns. Must pass
the P16 test with widescreen **on** ([README](README.md#test-plan)).

## Hybrid: ghosts in the margins

Vanilla, plus renderer-side "ghosts":

* **Sources:** awake entities whose position is in the margin (real, only culled); asleep entities
  in the margin (frozen); optionally spawn points that would fire now (count > 0, timer 0; the
  decision is deterministic and RNG-free, but the moment depends on the camera, so this stays off
  by default).
* **Data, read-only:** position `+$1A`/`+$1C` (height `+$1E`), facing `+$22`, character record `+$60`
  (bank `$8E`), its stand animation (`+$32` of the record; used by the wake path, V), palette from
  the record (whether an asleep entity's palette stays in CGRAM is unknown, so decode it).
* **Drawing:** decode the stand animation's first frame from ROM (formats: everscript-vscode
  `docs/entities/sprite_format.md`, `docs/entities/animation_script.md`), convert to RGB15 with
  opacity, submit with `PpuSetExtraObjects` (`snes/ppu.h:384`; composed at the OBJ stage with
  priority, windows and brightness; not part of the guest snapshot). Darken and make translucent.
  Depth order: the same key as `$8F:C773` (Y-based), computed host-side.
* **P15:** a ghost fades out over ~8 frames once the entity's box enters [0, 256) (the real sprite
  takes over). Spawn-point ghosts vanish when the count drops.
* **Never:** for script-driven actors or during cutscenes ([cutscenes](cutscenes.md)); outside the
  camera-limit box ([camera-and-limits](camera-and-limits.md)).
* **Sound:** none — no guest code runs ([audio](audio.md)).

## Experimental (change the game; own switch each; not replay-compatible)

| Id | What changes | Hook ([bindings](bindings.md)) | Risk / breaks |
|---|---|---|---|
| E1 Wide drawing | real sprites in the margins: box cull + emitters widened, X8 for X ≥ 256, right hints | `$80:904D`, `$80:90F8`, `$80:91FD`, `$80:92EB` | OAM 128 / `$D8` budget, flicker; OAM and LRU WRAM differ from vanilla |
| E2 Wide activation | wake/sleep windows widened by the margin (never for spawner-made) | `$8F:C40F` | more AI, RNG, lag → `$1076` drops; spawns shift because extra awake enemies use the budget |
| E3 Wide targeting | `$8C:8658` window widened | `$8C:8658` | ring-menu cursor can pick margin enemies; local |
| E4 Frozen tableau | asleep entities drawn at their real frame and position, no AI | renderer only | none to game state; looks like hybrid without fading |
| E5 Whole room alive | nothing sleeps, budget forced high | `$8F:C40F`, `$8F:DC34` | [enemies](enemies.md#keep-the-whole-room-alive-experiment-e5) |

The spawner (`$8F:DDBB`) is never widened in any mode.

## Policy per kind

| Kind | Vanilla | Hybrid | E1 | E2 | E4 |
|---|---|---|---|---|---|
| Normal (script-placed) enemy | clipped at 256 | ghost, fades on entry | drawn | woken by the wide window | drawn frozen while asleep |
| Friendly / NPC | clipped | ghost, not darkened | drawn | widened | drawn |
| Spawner-made enemy | clipped | ghost while awake in the margin | drawn | **vanilla windows** | n/a (despawned, not asleep) |
| Spawn point | — | ghost only if enabled and deterministic | — | never | — |
| Script-driven actor / cutscene | clipped | no ghost | drawn outside cutscenes only | vanilla | no |
| Bosses (`+$6C`, `$0010`) | clipped | ghost | drawn | already awake | drawn |

**Keeping the 4:3 view identical on room entry with spawners:** vanilla, hybrid, E1, E3 and E4 do
not touch the spawner, the windows or the budget, so the 256 px view holds exactly vanilla's
enemies at every frame (the guest code and its inputs are unchanged; E1 only adds drawing work,
which does not feed back except through OAM/LRU state, not entity logic — L). E2 cannot guarantee
it, even with vanilla windows for spawner-made enemies, because extra awake script-placed enemies
count against `$1076`.
