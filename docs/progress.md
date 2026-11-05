# Progress: Replays Against MesenCE

How much of each replay plays exactly as MesenCE 2.2.1 plays it, at every NMI. Measured 2026-11-04 on branch
`snesrecomp-spc-access-cycles`, to the end of each replay. Everything behind the table: [progress-details.md](progress-details.md).

| Replay | In sync, interpreted | In sync, native | SRAM | ARAM | S-DSP registers | S-DSP state | Picture | Native | C bindings | Notes |
|---|---|---|---|---|---|---|---|---|---|---|
| `01-no-inputs-4m` (14,424 frames) | **all**: 120–14424 | **all**: 120–14424 | **all** | **all** | **all** | 98.52 % | 95.54 % | 99.86 % | 51.67 % | Attract demo, no input |
| `02-newgame-boy-available` (17,948 frames) | **all**: 120–17948 | **all**: 120–17948 | **all** | all but 1 | **all** | 98.68 % | 97.19 % | 99.96 % | 36.94 % | New Game to free play |
| `04-newgame-flowers.mmo` (25,923 frames) | **all**: 120–25923 | **all**: 120–25923 | **all** | **all** | all but 1 | all but 2 | 93.38 % | 99.96 % | 32.55 % | Played by hand in MesenCE |
| `05-flowers-from-state.mmo` (8,434 frames) | **all**: 1–8434 | **all**: 1–8434 | **all** | **all** | **all** | **all** | 82.86 % | 99.97 % | 15.60 % | 04 without the intro, from a mid-game state |
| `06-flowers-raptors.mmo` (4,683 frames) | **all**: 1–4683 | **all**: 1–4683 | **all** | **all** | **all** | **all** | 97.61 % | 99.98 % | 10.08 % | Flowers and raptors, from a mid-game state |

**Columns**
- **In sync:** WRAM, VRAM, CGRAM and OAM identical at every compared frame.
- **SRAM to Picture:** the frames identical. Interpreted and native give the same results in every one of these columns.
- **S-DSP state:** output samples, voices, envelopes, BRR buffers and echo, once per frame; not the audio stream.
- **Picture:** the 224 rendered lines, pixel for pixel.
- **Native:** instructions that ran outside the interpreter, compiled or as C. **C bindings:** the part of all instructions that ran
  as reconstructed C, in ten routines (`random_number_generator`, `update_entity_plane`, `eval_tile_collision`, `init_entity_pool`,
  `entity_sprite_depth`, `companion_metatile_raycast_line_stepper`, `field_spare_time_loop`, `wait_for_nmi`,
  `vram_dma_upload_tile_block`, `map_anim_tile_channel_tick_and_vram_stream`).

**Notes**
- The picture differs by rounding below full brightness, up to 8 of 255 ([fade-rounding.md](imperfections/fade-rounding.md)). With that
  rounding taken out, every frame matches but one, in 05.
- S-DSP state on 01 and 02: 201 frames each are a harmless power-on default ([dsp-brr-offset-reset.md](imperfections/dsp-brr-offset-reset.md)).
  The rest, and the single frames in the other columns, are a voice's end flag seen a clock early, open
  ([dsp-endx-read.md](imperfections/dsp-endx-read.md)).
- Frames before 120 on 01, 02 and 04 are not compared: the RNG is copied in from MesenCE at 115. 05 and 06 start from a MesenCE
  state, and their picture at frame 1 is not compared.
- `03-complete-run-7h` does not stay in sync with MesenCE, so it is not measured here. The five replays are about 17.5 minutes of
  distinct play: the whole game is not proven.
- Replay 01 is the mandatory one, measured on the current build. The others last ran with the raycast's binding (`59ee9d7`): all columns as shown. They have not run with the two NMI loops bound, so their Native and C columns are from then too.
- Measured with everscript-vscode `npm run compare:full` and `tools/compare/probes/run-stats.mts`. The results need four snesrecomp
  branches that are not upstream ([upstream-access-timing.md](upstream-access-timing.md)).
