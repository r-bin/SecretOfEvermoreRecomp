# Widescreen for the Secret of Evermore recomp

Research, 2026-11-09. How to build a widescreen mode on the snesrecomp framework, what the game
does outside its 256 px screen, and which options are safe. A first approach (field backgrounds,
vanilla enemies) is in `src/soe_widescreen.c`; state and checks: [progress](progress.md).

**Goal.** A wider picture that, by default, changes nothing in the game: the 256 px centre and all
of WRAM/VRAM/CGRAM/OAM stay identical to 4:3, and the margins only show what the game already
holds (the room, the HUD placed for the wider frame). Options that change the game (more enemies
active, wider sprite drawing) are experiments, each behind its own switch.

**Sources.** The game's code, read in the CDL-annotated disassembly that everscript-vscode
generates locally from the user's ROM (`out/recomp/asar/banks/`, not committed), its knowledge
entries and its decoded scripts (`soe://rom/assets/maps/<id>/scripts/`); the framework in
`snesrecomp/` (cited by file:line); the local room headers for statistics. No third-party code.

**Marks.** **V** verified: the whole routine, or the cited instructions, read on 2026-11-09.
**L** likely: read in part, or follows from verified code. **U** unverified: an existing note or a
guess. Addresses are US: `$80:xxxx` code, `$7E:xxxx` WRAM, DP = direct page (D = 0).

**Tags.** Every address is tagged `widescreen` + a sub-tag in its everscript-vscode knowledge entry,
and the tree is `soe://tags/widescreen/` (`src/games/evermore/resources/tags/tags.json`): `gate`,
`menu`, `camera`, `limits`, `scroll`, `parallax`, `streaming`, `room_bounds`, `collision`, `cull`,
`oam`, `activation`, `spawn`, `entity`, `hud`, `window`, `fade`, `audio`, `cutscene`, `mode7`.

## What we found, in short

1. Two cameras in DP: the target `$BF`/`$C1` and the rendered `$59`/`$5B` (target + shake, clamped).
   Every world-to-screen conversion uses `$59`/`$5B`. The active **camera limits** `$23E9..$23EF`
   are narrowed by scripts to one part of a map (one hut of room `0x51`). → [camera-and-limits](camera-and-limits.md)
2. Only a 17 × 15-cell window of the 32 × 32 tilemap ring is ever drawn; the margins need a
   prefill from the room grid `$7F:0000`, which holds every layer. → [backgrounds-and-parallax](backgrounds-and-parallax.md)
3. Collision is whole-room and camera-free. The "viewport collision rows" and the "frustum" were
   misread: they are streaming buffers and a room-edge box. → [enemies](enemies.md)
4. The OAM path culls with signed compares but cannot place a sprite at X ≥ 256. → [sprites-and-cull](sprites-and-cull.md)
5. Entities sleep or wake 16/48 px around the 4:3 screen, only when the awake budget (6, lowered
   on lag) is full; spawners create enemies 16..48 px off screen, i.e. inside a 16:9 margin. → [enemies](enemies.md)
6. Fades are INIDISP only and already cover the margins. Full-screen menu pages need a 4:3
   fallback. → [fades](fades.md), [menus-and-gates](menus-and-gates.md)
7. The framework's Mode 7 renderer continues the affine transform into the margins. → [mode7](mode7.md)

## Settings

| Setting | Values | Default | Topic |
|---|---|---|---|
| Aspect | adaptive (follow the window), 16:10, 16:9, 21:9, 32:9 in the mod; any W:H or width up to 800 px for tests | adaptive when on | [progress](progress.md) |
| Pixel aspect | 8:7, 1:1, 4:3 CRT (separate from the view aspect, as in MMX) | 8:7 | [other-recomps](other-recomps.md) |
| Edges (parallax, room and camera-limit edges) | clip to terrain box / mirror / extend / off | clip | [backgrounds-and-parallax](backgrounds-and-parallax.md), [camera-and-limits](camera-and-limits.md) |
| Small rooms / limit boxes narrower than the view | pillarbox / mirror / clip | pillarbox | [camera-and-limits](camera-and-limits.md) |
| Mode | vanilla / vanilla extended (entities drawn from WRAM) / experimental (own renderer, later) | vanilla extended | [progress](progress.md) |
| UI placement | vanilla / edges / safe area / custom | vanilla | [hud-and-adaptive-ui](hud-and-adaptive-ui.md) |
| Fog of war | off / dim / fog / black, strength, feather, ghosts through fog | off | [fog-of-war](fog-of-war.md) |
| Cutscenes | keep / vanilla enemies / 4:3 | 4:3 | [cutscenes](cutscenes.md) |
| Menu pages | pillarbox / floating window over a field snapshot / blurred snapshot | pillarbox | [menus-and-gates](menus-and-gates.md#floating-menu-window) |
| Mode 7 | wide / 4:3 / stretch | 4:3 until checked | [mode7](mode7.md) |

Margins at the SNES 8:7 pixel aspect (256 px = 4:3): 16:10 → 308 px (26/side), 16:9 → 342 px
(43/side), 21:9 → 448 px (96/side). Of 127 rooms, 6 / 13 / 27 are narrower than these widths.

Framework: `SnesDesktopHostGame.widescreen_supported`, `native_widescreen`
(`snesrecomp/runner/src/desktop/host_main.h:85,92`), `g_ws_extra`/`g_ws_active`
(`runner/src/widescreen.h`), `PpuSetExtraSpace`/`PpuSetExtraSideSpace` (`snes/ppu.h:753–770`),
`SNESRECOMP_WS_EXTRA` for tests (`PpuWsExtraOverride`, `ppu.h:804`). Delivery: a default-disabled
`.snesmod` in `mods/preloaded/packages/` (mods are always on, `CMakeLists.txt:35`) with `[[option]]`
choices read by `snes_mod_runtime_feature_option_value_c` (`runner/src/mod_runtime.h:258`).
Presentation-only parts register with `snes_mod_register_presentation_plugin` (`mod_runtime.h:285`);
anything that changes the game registers as an ordinary plugin. Manifest format:
`snesrecomp/docs/MOD_AUTHORING.md` (`../research/snesmod-addons.md` is an idea note).

Kill switches (rule P14), each forcing the vanilla path: `SOE_WS_PREFILL`, `SOE_WS_EDGES`,
`SOE_WS_HUD`, `SOE_WS_FOG`, `SOE_WS_GHOSTS`, `SOE_WS_MENU`, `SOE_WS_CUTSCENE`, `SOE_WS_MODE7`,
`SOE_WS_OAM` (E1), `SOE_WS_WAKE` (E2), `SOE_WS_TARGET` (E3), `SOE_WS_ALIVE` (E5).

## Status per topic

| Topic | State |
|---|---|
| [checklist](checklist.md) | P1–P16 mapped; P5/P6 (gate, liveness) open |
| [camera-and-limits](camera-and-limits.md) | verified; list of rooms with narrowed limits not built |
| [backgrounds-and-parallax](backgrounds-and-parallax.md) | verified for mode 0; parallax rooms to measure |
| [sprites-and-cull](sprites-and-cull.md) | verified |
| [enemies](enemies.md) | verified (sleep/wake, spawner, collision); friendly flags U |
| [modes](modes.md) | proposal |
| [hud-and-adaptive-ui](hud-and-adaptive-ui.md) | HUD sprites V, BG3 split U |
| [menus-and-gates](menus-and-gates.md) | menu page markers L; ring menu dim unknown |
| [cutscenes](cutscenes.md) | signals known, script list not built |
| [fades](fades.md) | brightness V; flashes, mosaic U |
| [fog-of-war](fog-of-war.md) | proposal |
| [audio](audio.md) | partly V |
| [mode7](mode7.md) | framework V, game tables U |
| [bindings](bindings.md) | list |
| [advance-work](advance-work.md) | what can start now |
| [other-recomps](other-recomps.md) | compared with the MMX recomp and SoE-Recomp |
| [progress](progress.md) | step 1 (field backgrounds) working; what is verified |

## Order of work

Follows the framework order (`snesrecomp/docs/WIDESCREEN_PATTERNS.md`, "Order of work"):

1. **P16 baseline.** Record replay 01 with widescreen off; it must equal MesenCE
   (`replay_01_recorded`, the only mandatory recomp test; never replay 03; always with timeouts).
   Save frame dumps of field rooms from save states (`SNESRECOMP_FRAME_BMP_DIR`).
2. **Host wiring** in `src/main.c`: `widescreen_supported`, `native_widescreen`, aspect labels.
3. **Gate** ([menus-and-gates](menus-and-gates.md)): field vs menu page vs Mode 7; cutscene watch.
4. **Backgrounds**: prefill, edges, camera-limit clip, small rooms.
5. **Sprites clipped to 256, HUD placement, fog.**
6. **Ghosts** (hybrid).
7. **Experiments** E1..E5, only after the bound C of each consumer is proven equal to the ROM.

## Test plan

* Widescreen off: `replay_01_recorded` must pass (WRAM, VRAM, CGRAM, OAM every 60th NMI).
* Widescreen on in vanilla or hybrid mode: the same test must still pass. That proves the
  feature is presentation-only. (Replay 01 is power-on, intro, title and one attract loop, so it
  mostly exercises the gate; field checks come from save states.)
* Headless visual checks (frame dumps at 16:9 and 21:9, `SNESRECOMP_WS_EXTRA` pinned): a mode-0
  room both scroll directions (outermost column correct on its first frame), room `0x51` (one hut
  only), the 17-cell room `0x50` (pillarbox), room `0x4B` (parallax), a mode-2 room, a spawner
  room, the ring menu, a menu page, a fade, a flight scene.
* Experiments: per room from a save state, diff WRAM with the mode on and off; never claim replay
  compatibility.
