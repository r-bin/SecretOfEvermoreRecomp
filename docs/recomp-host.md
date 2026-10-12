# The snesrecomp host: making the generated C run

snesrecomp turns the game code into C functions. On its own that is a library: nothing calls
the reset handler, nothing decides what one frame is, nothing raises the NMI, nothing shows a
picture. The **host** is the program around the C that does those things. This page says how
we set it up, what it does today, and how to check it against the real game.

Status 2026-10-12: **it boots.** The scaffolded host shows the Squaresoft logo at frame 120 and
the intro scene at frame 600. With our CDL seeds, 682 of 1033 function variants run as native C.
WRAM matches snes9x exactly up to NMI counter 305, then drifts (§3).

## 1. What the host has to do

| Job | SoE specifics | Who does it today |
|---|---|---|
| Start the game | reset handler `$80:8020` | framework (reset vector) |
| Decide what one frame is | the reset handler never returns; it ends in `JML $80:8B66` (main loop) and the game waits for vblank | framework `beam_frame_driver`: one frame = one PPU field, cut at the vblank edge |
| Raise NMI / IRQ at the hardware edge | NMI handler `$80:823A`; NMI + auto-joypad are switched on by writing `$81` to `$4200` | framework `beam_frame_driver` |
| Input | auto-joypad registers `$4218/$4219` | framework desktop host (keyboard, gamepad, input scripts) |
| Picture and sound | PPU and SPC700 stay emulated | framework (`RtlDrawPpuFrame`, APU catch-up) |
| Code snesrecomp could not translate | 351 LLE variants (mostly jump tables) | framework interpreter fallback |

The game-specific parts (frame boundary, interrupt delivery) live in the project's
`src/game_rtl.c`. For SoE the generic beam driver works so far, so that file is still the
scaffold's.

## 2. One command builds the game

```sh
SOE_ROM=<clean rom> npm run recomp            # add --fresh to scaffold the project again
open "out/recomp/SecretOfEvermore-macos-arm64/play.command"     # Linux: play.sh, Windows: play.bat
```

`tools/recomp/build.js` (with `tools/recomp/host.js`) does, after the Asar export and rebuild:

1. **Scaffold** the repository root with snesrecomp's `tools/new_project/setup_project.sh`
   (first run, or `--fresh`): `--players 1 --no-netplay --no-ci --no-github --no-generate --no-build`.
   This clones the framework submodules (network). The project is a local git repo; its
   `.gitignore` keeps the ROM and the generated C (`src/gen/`, `recomp/funcs.h`) out. It lives
   under `out/`, which this repo ignores. **Never publish it.**
2. **Glue:** copy `tools/recomp/host/` over the project (today `src/game_rtl.c`). Only files we
   write live there; snesrecomp and its templates are PolyForm Noncommercial 1.0.0, so nothing
   of theirs is copied into this repo.
3. **Seeds:** replace `project/recomp/bank8x/9x.cfg` with the export's (named `func` entries,
   exit widths, dispatch directives). Append the export's `data_region` lines to the project's
   `bank00.cfg` between marker lines, leaving its generated block (reset and NMI forced LLE).
4. **Generate:** `tools/regen.sh --cfg-roots --rom <clean rom>` → 406 roots, 1033 variants,
   **682 native**, 351 LLE.
5. **Build:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
6. **Package:** copy the executable, `assets/` and `mods/` to
   `out/recomp/SecretOfEvermore-<os>-<arch>/` and write a start script with the ROM path. Only
   those are replaced on a rebuild: `saves/` (SRAM `save.srm`, save states `save<N>.sav`),
   `config.ini` and `keybinds.ini` belong to the player and are kept.

Keys (from the framework's default `config.ini`): Shift+F1…F10 save a state, F1…F10 load it,
`r` toggles the PPU renderer, Tab is turbo, Ctrl+R resets. A save state is the way to hand over a
scene for debugging: `--script` with `loadstate <slot>` replays it headless.

The ROM probe reports HiROM, no coprocessor, checksum valid.

## 3. Running and checking

The host runs headless-ish from an input script: `turbo on`, `wait N`, `until16 <wram offset> == <hex>`,
`poke <wram offset> <hex bytes>`, `dump <tag>` (picture + WRAM/VRAM/CGRAM/OAM/registers into
`SNESRECOMP_DUMP_DIR`), `quit`.

```sh
npm run recomp:lockstep -- --rom <clean rom>        # tools/recomp/lockstep.js
```

The lockstep tool runs the ROM in our snes9x core and in the recomp without input, aligns both
on the NMI frame counter `$7E0100`, and compares WRAM at the same counter values. It first
copies snes9x's RNG state into the recomp: the reset handler seeds the RNG from the PPU's H/V
counters, so any two emulators start with different values.

Results (2026-10-12):

| NMI counter | Differing WRAM bytes (timing noise excluded) |
|---|---|
| 115, 295, 305 | 0 |
| 315 | 4 (`$0106` brightness shadow or `$0128`, stack slots) |
| 325–335 | 500–2100 (loading phase, NMI off, snapshots land at different points) |
| 345–479 | 70–190, including the RNG state |

The interpreter-only build and the build with 682 native variants diverge at the same counter
by the same amount. So the cause is **timing**, not the generated C: during the intro fade and
the loading phase the two emulators spend a different number of frames, so the fade and the
RNG calls shift. Caveat: the reference, snes9x 2005, is not cycle-accurate either.

### Known framework gap: BG mode 4 (fixed by glue)

Pressing Start in the intro leads to the character-name screen. In the recomp it was noise, while
intro, title and attract demo were pixel-perfect. Findings (2026-10-12):

- **Not the CDL, not our C:** a build without any of our seeds (everything interpreted) shows the
  same noise. A more complete CDL would not change it.
- VRAM at that screen matches snes9x (402 of 65 536 bytes differ, text area only).
- The game sets up the screen in two steps: `$2105 = $39` (mode 1, BG1/BG2 16×16 tiles), two
  frames later `$2105 = $04` (**mode 4**: BG1 8bpp, BG2 2bpp), `$2108 = $08`, `$210B = $02`,
  `$212C = $13`. The stone texture is an 8bpp background.
- snesrecomp's optimized renderer (`NewRenderer`, the default) handles modes 0–3 and sends
  everything else down its mode-7 path (`PpuDrawBackgrounds` in `runner/src/snes/ppu.c`). The
  reference renderer (`ppu_legacy.c`) handles all modes.
- Fix: our glue sets `g_config.new_renderer = false` from the host's `after_config` hook. Intro
  and demo are pixel-identical with both renderers, and speed stays above 400 fps. Worth
  reporting upstream (mode 4/5/6 in the optimized renderer); then the hook can go.

## 4. What the boot does (learned while setting this up)

- **Reset `$80:8020`:** switches to native mode, sets the stack to `$1FFF` and D to 0, clears
  the PPU / CPU registers, fills VRAM and both WRAM banks by fixed-source DMA, writes the PPU
  shadow defaults (`$0106` = `$8F`, screen off), seeds the RNG `$0133–$0139` from the PPU H/V
  counters and constants, calls `$8C:8000` and other setup routines (probably the sound driver
  upload; not checked), enables NMI + auto-joypad (`$4200 = $81`, mirrored to `$0E6B`), then
  jumps to the main loop at `$80:8B66`.
- **NMI `$80:823A`:** skips the VRAM upload queues while `$0F3A` is non-zero, copies the PPU
  shadow registers `$0106`–`$0125` (brightness, scroll, window) to the PPU, runs the CGRAM queue
  unless `$0F3C` is set, and increments the frame counter `$0100`.

The entries are in `src/localizations/functions.js` and `src/localizations/knowledge/ram/`.

## 5. Next steps

1. Find the cause of the divergence at counter 315. Compare the fade code's frame counts in
   both, and cross-check with a cycle-accurate emulator (Mesen2) to see which side is off.
2. ~~Copy the seeds into the project automatically~~ done: `npm run recomp` builds the game (§2).
3. Title screen and first room with an input script (`press start`), plus lockstep there.
4. First `hle_func` replacements (decompressors, room loader), checked by lockstep.
