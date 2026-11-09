# Work we can do now

Before any widescreen code, in order of value. Marks and tags: [README](README.md).

## In everscript-vscode (facts)

1. **Rooms with narrowed camera limits.** Run the Scripts tab's `cameraBounds` over all rooms and
   publish the list (room, condition, box) as a table: drives the edge clip and the tests
   ([camera-and-limits](camera-and-limits.md)).
2. **Cutscene script list.** From the script tree, every script that moves the camera, halts or
   teleports the Boy, or stages actors; export start addresses ([cutscenes](cutscenes.md)).
3. **Ring menu dim and the `+$14 & $20` bit.** Find how the ring menu darkens the field and who sets
   the animation "off screen" bit ([menus-and-gates](menus-and-gates.md), [audio](audio.md)).
4. **HUD layout.** Which HUD parts are BG3 and which OAM entries, and whether HUD sprites have fixed
   OAM slots ([hud-and-adaptive-ui](hud-and-adaptive-ui.md)).
5. **Ghost decoder.** A host-usable decoder for "character record → stand animation frame 0 →
   RGB15 + palette", reusing the Characters tab code (`src/games/evermore/sprites/`), as a spec the
   recomp can port ([modes](modes.md)).
6. **Rename stale entries** whose meaning was corrected (`JOYPAD1_TRIGGERED` → INIDISP shadow,
   `AUDIO_TARGET_VOLUME`, `viewport_collision_row_*`, `VIEWPORT_CULL_*`, `MINIMAP_RADAR_WINDOW_*`,
   `ROOM_PALETTE_GROUP_ID`, `ACTIVE_COMPANION_HEAD`, `hud_compass_needle_update`,
   `camera_frustum_cull_test`, `entity_wake_up_and_resume_ai`,
   `dog_foraging_ingredient_node_spawn_dice_roll`, `companion_dog_sniff_and_forage_timer_tick`).
   Names feed `soe_ram.h`, C files and recomp cfg comments, so rename in one pass with the
   generator (`tools/c-to-rom/gen-ram-header.mts`).
7. **Bound C** of `entity_sleep_wake` (`$8F:C40F`), G1-rated ([bindings](bindings.md)).

## In the recomp (no guest change)

1. Host wiring behind a disabled mod: aspect, `native_widescreen`, `SNESRECOMP_WS_EXTRA` in tests.
2. The menu/field gate with entry hooks on `$8E:E41E`, `$8C:B743`, `$8E:E08B`; log it to verify.
3. Margin prefill from `$7F:0000` with edge clip to the camera-limit box, behind `SOE_WS_PREFILL`;
   frame dumps of the test rooms.
4. A test that runs replay 01 with widescreen on in vanilla mode and compares the CRCs.
