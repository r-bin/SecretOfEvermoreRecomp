# Camera, scroll shadows and camera limits

Tags `widescreen.camera`, `widescreen.limits`, `widescreen.room_bounds`, `widescreen.scroll`.
Marks: [README](README.md).

## The two cameras (V)

`camera_viewport_overworld_scroll_and_clamp` (`$90:9E93`) and
`camera_smooth_tracking_filter_and_screen_bounds` (`$90:9F21`), read whole:

1. Save the target camera DP `$BF`/`$C1` to DP `$12`/`$14`.
2. If the scripted target `$242B` is non-negative: `$BF`/`$C1` = `$242B`/`$242D`.
   Else run the deadzone filter (`$90:9FC9`, margins `$240F..$2415`) on the camera actor `$0F42`
   and, if set, on the second actor `$143A`.
3. Flight bias when header flag `$0F84` bit 14 is set.
4. Clamp `$BF` to [`$23E9`, `$23ED` − 256] and `$C1` to [`$23EB`, `$23EF` − 224], signed.
5. Limit the move per frame by `$242F` with a 4-bit sub-pixel accumulator `$0F38`.
6. **Rendered camera** `$59` = clamp(`$BF` + shake `$C5`, 0, `$08F6`), `$5B` = clamp(`$C1` + `$C7`, 0, `$08F8`).
7. Viewport box `$2401..$2407` = `$BF`, `$BF` + 256, `$C1`, `$C1` + 224 (no engine reader; scripts
   can read it through the operand table at `$8C:EC8E`).

Snaps (fade-in, room setup) go through `$90:94CD`: camera = actor − (128, 112), clamped to the
same limits, then a full redraw (V).

Every world-to-screen conversion subtracts `$59`/`$5B`: sprite depth `$8F:C773`, sleep/wake
`$8F:C40F`, spawner `$8F:DDBB`, targeting `$8C:8658`, damage popups `$8C:888F`, projectiles and
alchemy (searched all banks). The claim in `../research/enhancements.md` that the camera is `$7E:00E2/E4`
is wrong: `$E0`/`$E1` are OAM assembler counters and nothing uses `$E2`/`$E4` as a camera (V).

## Scroll shadows (V)

The camera-mode table `$90:8E68` (index `$241F`, from the room header) writes the shadows each
field frame. Mode 0 (`$90:9A98`, 118 of 127 rooms): BG1HOFS = BG2HOFS = `$59`, BG1VOFS = BG2VOFS =
`$5B` − 1. Other modes: [backgrounds-and-parallax](backgrounds-and-parallax.md). The NMI copies
`$010E..$0119` to `$210D..$2112`. Pixel phase for margins comes from the PPU's per-line scroll (P1);
the shadows give the frame value and the high bits.

## Camera limits (V)

`$23E9`/`$23EB`/`$23ED`/`$23EF` are `MAP_X_START`/`MAP_Y_START`/`MAP_X_END`/`MAP_Y_END` in the
everscript compiler. The room header reader (`$90:8F80`, the only routine that stores them) sets
the whole room: 0, 0, width·16, height·16. **Enter scripts narrow them** to one part of a map.

Example, room `0x51` (Fire Eyes' village huts and Blimp's hut, 50 × 56 cells = 800 × 896 px): the
enter script (`soe://rom/assets/maps/51/scripts/enter.evs`) picks one box per hut from
`HUT_HOUSE_TO_ENTER`, e.g. (0, 0)–(256, 288), (272, 0)–(528, 288), (544, 0)–(800, 288),
(0, 304)–(256, 592), (272, 304)–(528, 592), (544, 304)–(800, 592), (0, 608)–(256, 896). Each box is
256 px wide: exactly one 4:3 screen. A wide view centred on a hut would show the neighbouring hut
16 px further on. Maps with several 512 px "screens" use `script_op_teleport_screens` (`$90:A001`,
V): it moves the camera, the scroll shadows and every awake entity by multiples of 512 px; the
limits keep the parts apart.

Which rooms narrow the limits: the everscript-vscode Scripts tab already finds every enter-script
write to `$23E9`/`$23ED` (`src/games/evermore/scripts-tab/model/rooms.ts`, `cameraBounds`). Running
it over all rooms gives the list (not done yet; candidates: houses, huts, dungeons with sub-rooms,
boss arenas). Scripts can also change the limits mid-room (cutscenes): the recomp should read the
four words every frame, not once.

## What widescreen must do

* Use the **current limit box** `[$23E9, $23ED) × [$23EB, $23EF)` as the authored world for every
  BG layer, not the room size. Outside it: the "Edges" setting
  ([backgrounds-and-parallax](backgrounds-and-parallax.md)) — default blank (black) or pillarbox.
* When the box is narrower than the wide view (`$23ED − $23E9` < view width): pillarbox with
  `PpuSetExtraSideSpace(ppu, left, right, 0)` (`snes/ppu.h:770`), the per-side margin computed as
  the distance from the rendered camera to the box edge (the zelda3-style dynamic margin). Or mirror
  about the box edges (`PpuSetWidescreenLayerWorldMirrorBand`, `ppu.h:932`, with left/right in world
  pixels — the BG HOFS here is the world X). Same for the room edges at the camera clamps.
* Never re-centre the camera: changing `$BF`/`$59` changes the game (sprite positions, wake windows,
  spawns). Keep the camera vanilla and fit the presentation around it.
* Ghosts and wide sprites also stop at the box: an entity outside the active box belongs to
  another part of the map.

## Room sizes (from the local headers)

Width 17..128 cells, height 15..125. Narrower than 308 / 342 / 448 px: 6 / 13 / 27 rooms (16:10,
16:9, 21:9 at 8:7 pixels); 24 rooms narrower than 398 px (16:9 at square pixels). The header allows
255 cells (4080 px); `../research/enhancements.md`'s "up to 4096×4096" is the format limit, the largest
vanilla room is 2048 × 2000 px.
