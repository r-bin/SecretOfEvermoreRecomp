# Checklist: framework rules P1–P16 against Evermore

Rules from `snesrecomp/docs/WIDESCREEN_PATTERNS.md`. Marks and tags: [README](README.md).

## Rules

| Rule | Evermore answer | Status | Evidence | Tag | How to close |
|---|---|---|---|---|---|
| P1 phase from the PPU | per-line `hScroll`/`vScroll` of BG1/BG2; `$59`/`$5B` (world, unwrapped) only for the high bits | known, V | `$90:9A98`, [camera-and-limits](camera-and-limits.md) | scroll | — |
| P2 margins populated | prefill from `$7F:0000` per layer (`WsShadowPrefillTile`) | known, V | `$90:93AA`, `$90:98FA` | streaming | implement; outermost column right on its first frame |
| P2b bounded areas | reflect/clip about the **camera-limit box** `$23E9..$23EF`, not the room | known, V | room `0x51` enter script | limits | [camera-and-limits](camera-and-limits.md) |
| P2c continuous status bar | no full-width bar: HUD is sprites + short BG3 runs | L | `$90:8CEE` | hud | `SNESRECOMP_LAYER_MASK=4` |
| P3 periodic layers | none in mode 0; parallax room `0x4B`, mode-2 rooms to check | L | header counts | parallax | measure |
| P4 key to rendered scroll | shadows are written before the NMI copies them | known | NMI `$80:823E` | scroll | — |
| P5 gameplay gate | field tick ran (`$8E:E41E`) and no menu page (`$8C:B743` … `$8E:E08B`) and not flight (`$0E70`) | L | [menus-and-gates](menus-and-gates.md) | gate, menu | watch across all modes |
| P6 liveness | candidates `$05A9` bit 15, `$22EE` bit 0, `$242B` ≥ 0, `$0F42`, script slots | unknown | [cutscenes](cutscenes.md) | gate, cutscene | trace scripted scenes in MesenCE |
| P7 cull windows | box cull [0,256); slice clip [−7/−15,256); wake [−16,272); sleep [−48,304); spawn [−48,304)∖[−16,272); target [−8,264). Separate bases: do not unify | known, V | [sprites-and-cull](sprites-and-cull.md), [enemies](enemies.md) | cull, activation, spawn | — |
| P8 OAM emitter | signed compares; X8 only from the sign → no X ≥ 256 | known, V | `$80:90F8`, `$80:91FD`, `$80:92EB` | oam | E1 only |
| P9 spawn slack | spawner band 16..48 px off screen is visible in a 43 px margin | known, V | `$8F:DDBB` | spawn | never widen; clip sprites at 256 |
| P10 progression records | spawn-record enemies (`+$8C`) vs script-placed ones are distinct | known, V | `$8F:C40F` | spawn, entity | — |
| P11 balanced synthetic call | no dual pass proposed | n/a | — | — | — |
| P12 large objects | multipart bosses hold `+$6C` ≥ 0 and never sleep | L | `$8F:C40F` | activation | check a boss arena |
| P13 staging lead | no camera-driven CHR staging; sprite CHR is a per-frame LRU; rooms change through doors | L | `$80:90F8` | streaming | — |
| P14 kill switches | one per piece | proposed | [README](README.md) | — | — |
| P15 previews stand down | ghosts fade when the real sprite is drawn | proposed | [modes](modes.md) | entity | — |
| P16 4:3 bit-identical | off: nothing hooked; vanilla/hybrid on: renderer only, replay 01 CRCs unchanged | proposed | [README](README.md) | — | test plan |

## WRAM

| Address | Name (entry) | Meaning | Mark | Tag |
|---|---|---|---|---|
| DP `$BF`/`$C1` | in `DIRECT_PAGE_BLOCK` | target camera | V | camera |
| DP `$59`/`$5B` | in `DIRECT_PAGE_BLOCK` | rendered camera | V | camera |
| DP `$C5`/`$C7` | in `DIRECT_PAGE_BLOCK` | shake offsets | V | camera |
| `$0106` | JOYPAD1_TRIGGERED (name stale) | INIDISP shadow | V | fade |
| `$010E..$0114` | PPU_SHADOW_BG1/2 H/VOFS | scroll shadows | V | scroll |
| `$0116`/`$0118` | PPU_SHADOW_BG3H/VOFS | BG3 scroll | U | hud |
| `$013D` | VRAM_DMA_QUEUE_CURSOR | VRAM queue | V | streaming |
| `$051D`/`$0561` | HUD_BOY/DOG_STATUS_WIDGET | HUD widgets | U | hud |
| `$05A9` | RING_MENU_STATUS_FLAGS | ring menu open (bit 15) | U | gate, hud |
| `$08EE..$08F4` | ROOM_METATILE_*, ROOM_PIXEL_* | room size | V | collision, room_bounds |
| `$08F6`/`$08F8` | CAMERA_MAX_ROOM_X/Y | clamp of `$59`/`$5B` | V | camera, room_bounds |
| `$08FA..$09F9` | viewport_collision_row_0..7 (names stale) | stream buffers | V | streaming |
| `$0B83` | AUDIO_TARGET_VOLUME (name stale) | brightness fade target | V | fade |
| `$0E70` | MODE7_FLIGHT_STATE | flight scene | U | mode7, gate |
| `$0EBC` / `$0EBE` | ENTITY_ROUND_ROBIN_CURSOR / SPAWN_RING_HEAD_PTR | schedulers | V | activation / spawn |
| `$0F42` / `$143A` | CAMERA_TARGET/SECONDARY_ACTOR_PTR | camera actors | V | camera, cutscene |
| `$0F48..$0F4E` | VIEWPORT_CULL_* (names stale) | room-edge box | V | collision, room_bounds |
| `$0F8C..$0F9A` | MINIMAP_RADAR_WINDOW_*, BG2_METATILE_VIEWPORT_* | streamed windows | V | streaming |
| `$0FAC` | ROOM_PALETTE_GROUP_ID (name stale) | stream width | V | streaming |
| `$1070`/`$1072`/`$1074`/`$1076` | ENTITY_BATCH_*, ROOM_* counts | awake count, asleep count, spawn count, budget | V | activation, spawn |
| `$107A`/`$107C` | ENTITY_SCHEDULER_* | lag detection | V | activation |
| `$22EE` | SCRIPT_EXECUTION_LOCK_FLAGS | dialogue lock | U | cutscene, gate |
| `$23E9..$23EF` | CAMERA_ROOM_BOUND_* | camera limits | V | camera, limits |
| `$2401..$2407` | VIEWPORT_* | viewport box (scripts) | V | camera |
| `$240F..$2415` | CAMERA_DEADZONE_* | deadzone | V | camera |
| `$2417..$241D`, `$22FA`/`$22FB` | parallax origin, offset, ratio | parallax | V | parallax |
| `$241F` | CAMERA_SCROLL_MODE | camera mode | V | scroll, window |
| `$242B`/`$242D`/`$242F` | CAMERA_SCRIPTED_TARGET_*, CAMERA_PAN_VELOCITY | scripted camera | V | camera, cutscene |
| `$2433`/`$2435` | SPAWN_SYSTEM_ENABLE / ROOM_AMBIENT_LIGHT_LEVEL | spawner count, cap | L | spawn |
| `$28FC` / `$2F28` | VM_SCRIPT_SLOTS / SCRIPT_SLOT_QUEUE_TAIL | script slots | L | cutscene |
| `$3DDF` / `$3DE1` | ACTIVE_ENTITY_HEAD / ACTIVE_COMPANION_HEAD (name stale) | awake / asleep lists | V | activation |
| `$5DC3` | SPAWN_RECORDS_TABLE | 60 spawn records | V | spawn |
| `$ABDE` / `$ADDE` | LOW/HIGH_OAM_TABLE_BUF | OAM buffers | V | oam |
| `$7F:0000` | ROOM_METATILE_GRID | room grid + cell definitions | V | collision, streaming |
| `$7F:8000` | VRAM_BG1_TILEMAP_BUF | blank word for the map clear | V | streaming |
| `$7F:C800` | SHADOW_TILEMAP_BUF | dialogue/HUD tilemap | U | hud |

## ROM

| Address | Name (entry) | Purpose | Mark | Tag |
|---|---|---|---|---|
| `$90:9E93`, `$90:9F21`, `$90:9FC9` | camera_* | camera | V | camera |
| `$90:94AB`, `$90:94CD` | camera_center_and_clamp, …_seam_check | snap, redraw | V | camera |
| `$90:8F80` | room_header_reader_to | room size, limits, mode | V | room_bounds, limits |
| `$90:A001` | script_op_teleport_screens | move by 512 px screens | V | limits |
| `$90:8E68` | ptrs_908E68 | camera-mode table | V | scroll, window |
| `$90:9A98`, `$90:9D45`, `$90:9BA7` | mode 0, heat shimmer, parallax position | scroll | V | scroll, parallax |
| `$90:9876` | render_map_overlay_compass_hud (name stale) | field camera tick | V | camera |
| `$90:98FA`, `$90:999C`, `$90:99FB` | streamer, row, column | streaming | V | streaming |
| `$90:9379`, `$90:93AA` | full upload | streaming | V | streaming |
| `$90:9815`, `$90:9545`, `$90:9561`, `$8F:AF48` | collision lookups | collision | V | collision |
| `$8F:C773` | entity_sprite_depth (bound C) | screen pos, depth | V | oam |
| `$80:9033`, `$80:9040`, `$80:904D` | OAM box cull | cull | V | oam, cull |
| `$80:90F8`, `$80:91FD`, `$80:92EB` | emitters | clip, X8 | V | oam, cull |
| `$80:960A`, `$80:8FEE` | depth insert, finalise | OAM | U | oam |
| `$8F:C40F`, `$8F:C514`, `$8F:DC34` | sleep/wake, sleep, scheduler | activation | V | activation |
| `$8F:DDBB`, `$8F:DF41`, `$8F:B0A4` | spawner, despawn, slot alloc | spawn | V, V, L | spawn |
| `$8C:8658` | target_candidate_filter | targeting window | V | cull |
| `$90:8CEE`, `$90:8D2D`, `$8C:888F` | HUD gauges, damage popups | HUD | V | hud |
| `$8C:B743`, `$8E:E08B` | menu page set-up / field restore | menu gate | L | menu, gate |
| `$8E:E41E` | per_frame_field_tick | gate candidate | V | gate |
| `$80:DE96` (+ fade family), `$8C:BB72`, `$8C:E247` | fades, mosaic, colour math | fade | V / U / V | fade, window |
| `$90:8921`, `$90:88BC`, `$8C:82DC`, `$8C:81FD`, `$8C:8589` | SFX paths | audio | V / U | audio |
| `$8C:E66F` and the flight routines | Mode 7 scene | mode7 | U | mode7 |
