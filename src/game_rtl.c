/*
 * Secret of Evermore: game-specific runtime glue for the snesrecomp host.
 * Ours (MIT); the rest of src/ comes from the snesrecomp scaffold.
 *
 * What SoE needs from the host (docs/game-rtl.md):
 *   - Reset $80:8020 sets up the hardware, enables NMI + auto-joypad ($4200 = $81) and jumps to
 *     the main loop at $80:8B66, which never returns: the host cuts frames at vblank.
 *   - The NMI handler $80:823A copies the PPU shadows $7E0106-$0125 and counts frames in $7E0100.
 * The framework's beam-aligned driver does both (one frame = one PPU field, NMI at the vblank
 * edge), and boots SoE into the intro. SoE-specific frame handling goes here once needed.
 */

#include "game_rtl.h"
#include "beam_frame_driver.h"
#include "desktop/config.h"

void GameRunOneFrame(void) { snes_beam_frame_driver_run_frame(); }

void GameDrawPpuFrame(void) { snes_beam_frame_driver_draw_ppu_frame(); }

/* A new session (reset / return to title) must not inherit driver state from the last one. */
void GameSessionReset(void) { snes_beam_frame_driver_reset(); }

/*
 * Called by the desktop host after config.ini is parsed (hooked in src/main.c as the host
 * descriptor's after_config). snesrecomp's optimized PPU ("NewRenderer") draws BG modes 4-6 through
 * its mode-7 path; SoE's character-name screen is mode 4 (BG1 8bpp, written at $2105 = $04), so
 * it came out as noise. The reference renderer (ppu_legacy.c) handles every mode: use it until
 * the optimized one learns mode 4. Found 2026-10-12 (docs/game-rtl.md §4).
 */
void SoeAfterConfig(void) { g_config.new_renderer = false; }

const RtlGameInfo kGameInfo = {
    .title = "secretofevermore",
    .initialize = NULL,
    .run_frame = &GameRunOneFrame,
    .draw_ppu_frame = &GameDrawPpuFrame,
    .save_name_prefix = "save",
    .session_reset = &GameSessionReset,
};
