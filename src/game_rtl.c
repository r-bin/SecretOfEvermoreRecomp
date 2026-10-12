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

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "beam_frame_driver.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "desktop/config.h"
#include "snes/interp_bridge.h"
#include "snes/saveload.h"

/* Added to the framework's beam_frame_driver.c by tools/patch_framework.py (run by CMake). */
uint32_t snes_beam_frame_driver_resume_pc(void);
bool snes_beam_frame_driver_wai_halted(void);
void snes_beam_frame_driver_set_resume(uint32_t pc, bool wai_halted);

extern CpuState g_cpu;

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

/*
 * Save states. The framework's file snapshot holds the machine (WRAM, PPU, APU, the legacy
 * interpreter's Cpu) but not g_cpu -- the registers the recompiled code and the bridge run on --
 * nor the beam driver's resume PC. A load therefore kept the running session's A/X/Y/S/P and PC
 * under the saved RAM: it only looked right when both moments sat in the same idle loop, and a
 * load before the first frame (resume PC still 0) cold-booted from the reset vector on top of the
 * loaded RAM, which is a black screen. Found 2026-10-12 (docs/bugs-and-todos.md).
 * This chunk follows the guest blob; saves made before it load as before.
 */
#define SOE_EXEC_MAGIC 0x43584553u /* 'SEXC' */
#define SOE_EXEC_VERSION 1u
#define SOE_CPU_REGS_SIZE offsetof(CpuState, ram) /* A .. open_bus: no host pointer, no clocks */

typedef struct SoeExecChunk {
    uint32_t magic, version, regs_size, resume_pc;
    uint8_t wai_halted, pad[3];
} SoeExecChunk;

static SoeExecChunk s_loaded_exec;
static uint8_t s_loaded_regs[SOE_CPU_REGS_SIZE];
static bool s_loaded_exec_valid;

static void SoeStateSaveExtra(SaveLoadInfo *sli) {
    SoeExecChunk c = { SOE_EXEC_MAGIC, SOE_EXEC_VERSION, (uint32_t)SOE_CPU_REGS_SIZE,
                       snes_beam_frame_driver_resume_pc(), snes_beam_frame_driver_wai_halted(), { 0 } };
    sli->func(sli, &c, sizeof(c));
    sli->func(sli, &g_cpu, SOE_CPU_REGS_SIZE);
}

static void SoeStateLoadExtra(SaveLoadInfo *sli, uint32_t version) {
    (void)version;
    s_loaded_exec_valid = false;
    if (RtlStateBytesRemaining(sli) < sizeof(SoeExecChunk) + SOE_CPU_REGS_SIZE) return;
    sli->func(sli, &s_loaded_exec, sizeof(s_loaded_exec));
    if (s_loaded_exec.magic != SOE_EXEC_MAGIC || s_loaded_exec.version != SOE_EXEC_VERSION ||
        s_loaded_exec.regs_size != SOE_CPU_REGS_SIZE || !s_loaded_exec.resume_pc) return;
    sli->func(sli, s_loaded_regs, SOE_CPU_REGS_SIZE);
    s_loaded_exec_valid = true;
}

static void SoeOnStateLoaded(uint32_t version) {
    (void)version;
    if (!s_loaded_exec_valid) {
        printf("[soe] save state has no CPU chunk (made before v0.201.18): CPU and PC are not restored\n");
        return;
    }
    s_loaded_exec_valid = false;
    memcpy(&g_cpu, s_loaded_regs, SOE_CPU_REGS_SIZE);
    interp_bridge_set_lle_resume_pc(s_loaded_exec.resume_pc);
    snes_beam_frame_driver_set_resume(s_loaded_exec.resume_pc, s_loaded_exec.wai_halted != 0);
}

const RtlGameInfo kGameInfo = {
    .title = "secretofevermore",
    .initialize = NULL,
    .run_frame = &GameRunOneFrame,
    .draw_ppu_frame = &GameDrawPpuFrame,
    .save_name_prefix = "save",
    .state_save_extra = &SoeStateSaveExtra,
    .state_load_extra = &SoeStateLoadExtra,
    .on_state_loaded = &SoeOnStateLoaded,
    .session_reset = &GameSessionReset,
};
