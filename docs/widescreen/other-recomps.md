# Other recomps' widescreen, compared with this plan

Read 2026-11-09, shallow clones, for comparison only. Both projects are PolyForm Noncommercial:
nothing here is copied from them, only what they do, in our words.

* **MMX:** [mstan/MegaManXSNESRecomp](https://github.com/mstan/MegaManXSNESRecomp) at `aa7357b`
  (2026-11-06). Another game on the same framework; the source of `WIDESCREEN_PATTERNS.md`.
* **SoE-Recomp:** [aitorcachero/SoE-Recomp](https://github.com/aitorcachero/SoE-Recomp) at
  `34c1f93` (2026-10-26, v0.1.0). The same game (the Spanish ROM), another project.

## At a glance

| | MMX | SoE-Recomp | This plan |
|---|---|---|---|
| Delivery | default-off mod package, one option (view aspect) | launcher profiles / config flag, no mod | default-off mod, options per topic ([README](README.md#settings)) |
| View aspect | adaptive (fits window), 16:9, 21:9, 32:9; netplay fixed only | fixed 384 px (64/side) | 4:3, 16:10, 16:9, 21:9, custom |
| Pixel aspect | separate setting: 4:3 CRT, 8:7, 1:1 | not separate | margins computed at 8:7 only — **gap** |
| Margin source | host-side line enhancer reads the stage map from ROM, per scanline | framework shadow (`WsShadow*`), reset when the room width changes | prefill from the room grid `$7F:0000` ([backgrounds](backgrounds-and-parallax.md)) |
| Edges | per stage; boss-door twin columns hidden by a structural test | margins clamped to the room width from the camera `$59` and `$08EE` | clip to the camera-limit box `$23E9..$23EF` ([camera](camera-and-limits.md)) |
| Menus | pillarboxed | pillarboxed when BG mode 5 or 6 | pillarbox, or a floating window over a field snapshot ([menus](menus-and-gates.md)) |
| Mode 7 | — | widened 64 px unconditionally | wide / 4:3 / stretch, 4:3 until the game tables are checked ([mode7](mode7.md)) |
| HUD | health bars anchored to the screen edges | not handled | vanilla / edges / safe area / custom ([HUD](hud-and-adaptive-ui.md)) |
| Sprites | expanded capacity, no per-line limit | not handled | clipped to 256 by default; E1 widens ([sprites](sprites-and-cull.md)) |
| Enemies | live in the margins: widened spawn and cull, per-object policy | not handled (pop-in at the 256 px edge, margins show sprites only if the game draws them) | vanilla / hybrid ghosts / E1..E5 ([modes](modes.md)) |
| Ghosts / previews | frozen icons of future enemies, right side only, superseded by live spawning | — | hybrid: idle-animated ghosts that fade at the 4:3 edge |
| Fog of war | — | — | proposal ([fog-of-war](fog-of-war.md)) |
| Cutscenes | scripted encounters keep native spawn timing | — | detection + fallback ([cutscenes](cutscenes.md)) |
| Framework | used as is, plus its own renderer | patched (`patches/snesrecomp_fixes.patch`: margin clearing in `ppu.c`, `ws_shadow.c`) | used as is (house rule) |
| Tests | display, adaptive-state and renderer tests; playtested to the ending | — | replay 01 with widescreen off **and** on in vanilla/hybrid; frame dumps |

## What MMX teaches us

1. **Live enemies in the margins are possible, but cost a policy per object.** MMX runs the
   stage's spawn list twice per frame: the native pass at the native edge and a widened pass
   with its own persistent cursor, so a record rejected by one pass is not consumed for the
   other. The cursor is resynchronised outside play (death, checkpoint restart). Bosses and any
   object whose set-up locks the camera keep native timing; a list of identities overrides the
   broad rule; specific enemies get their own fixes (leashes for flyers, entrance fixes). This
   is our E2/E5 ([enemies](enemies.md)): expect the same long tail, and our spawners (16..48 px
   off screen, despawn beyond 48 px) map onto their "scripted encounters keep native timing".
2. **Previews stand down when real objects arrive** (rule P15) and only look forward, so
   defeated enemies behind the camera never reappear. MMX draws them from the ROM's spawn list
   (static), brightness-matched, not faded. Our hybrid ghosts read live WRAM instead (sleeping
   slots and spawner records), which also covers enemies behind the camera; we must keep their
   rule: never draw a ghost for anything already defeated.
3. **Pixel aspect is its own setting.** View aspect decides how much is shown, Display Aspect
   decides the pixel shape. Add it to our settings: 16:9 is 342 px at 8:7 pixels but 398 px at
   square pixels, which changes how many rooms need pillarboxing (`camera-and-limits.md` already
   counts both).
4. **"Adaptive" follows the window**, bounded by 256 px and the renderer's capacity. Cheap to
   offer once margins are per-frame.
5. **Netplay uses fixed aspects**, so both players see the same world. Relevant when co-op comes.
6. **Twin boundary columns.** MMX hides the neighbouring room's door column that the native
   camera never showed. Our version is the camera-limit box (huts in room `0x51` are 16 px apart).

## What SoE-Recomp shows

1. **It confirms the camera and room-width addresses.** It reads DP `$59`/`$5B` as the camera and
   `$7E:08EE` as the room width in cells, on the Spanish ROM. That suggests these WRAM addresses
   are the same in the ESP and US builds (unverified here; `docs/evermore-releases.md` in
   everscript-vscode warns that PAL ROM addresses differ).
2. **Clamping to the room is not enough.** It limits the margins to the room width only, so
   rooms with narrowed camera limits (the hut map `0x51`) would show the neighbouring hut. Our
   clip uses `$23E9..$23EF`.
3. **BG mode is a usable gate for some screens.** It pillarboxes when BG mode is 5 or 6
   (hi-res screens such as name entry). Our gate checks BGMODE ≠ `$39` plus the page markers
   ([menus-and-gates](menus-and-gates.md)). The menu pages rewrite BGMODE, but to which value is
   not yet read, so whether mode 5/6 alone would catch them is open.
4. **Room change by width is fragile.** It resets the margin shadow when the room width
   changes; two rooms of equal width keep stale margins. Use the room load instead.
5. **Mode 7 widened blindly** shows whatever the per-line tables produce; we keep 4:3 until the
   game tables are checked.
6. Nothing on enemies, HUD, sprites, fades or cutscenes; the framework is patched, which our
   house rule forbids.

## Changes to this plan

* Add a **pixel aspect** setting (8:7 / 1:1 / 4:3 CRT) next to the view aspect, and an
  **adaptive** view aspect (README settings).
* Hybrid ghosts: never for defeated or already-despawned-by-death entities; stand down per
  entity the frame the real one wakes (P15).
* Experiments E2/E5: plan for a dual spawn pass with its own cursor and a per-object override
  list from the start, as MMX had to.
