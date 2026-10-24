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
#include <stdlib.h>
#include <string.h>

#include "beam_frame_driver.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "desktop/config.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"

/* Added to the framework's beam_frame_driver.c by tools/patch_framework.py (run by CMake). */
uint32_t snes_beam_frame_driver_resume_pc(void);
bool snes_beam_frame_driver_wai_halted(void);
void snes_beam_frame_driver_set_resume(uint32_t pc, bool wai_halted);

extern CpuState g_cpu;
extern Snes *g_snes;
extern uint8_t g_ram[0x20000];

/*
 * tools/compare (docs/debugging.md): when SOE_COMPARE_STRIDE is set, hash memory each time the NMI frame
 * counter $7E0100 reaches a new multiple of the stride and write "H <counter> <crc> ..." lines to fd 3, one CRC-32
 * per region of SOE_COMPARE_REGIONS (wram, vram, cgram, oam). SOE_COMPARE_MASK lists WRAM offsets zeroed before
 * hashing (hex, "a-b" ranges, comma separated). SOE_COMPARE_DUMP=<counter> instead writes "D <counter>\n" and the
 * raw regions once. Nothing touches the disk: compare reads fd 3 from a pipe.
 */
enum { CMP_WRAM, CMP_VRAM, CMP_CGRAM, CMP_OAM };
static struct {
    int state; /* 0 unread, 1 off, 2 on */
    unsigned stride;
    long dump_at;
    int regions[4], nregions;
    uint8_t mask[0x20000 / 8];
    long last;
    FILE *out;
} s_cmp;

static uint32_t s_crc_table[256];

static uint32_t CmpCrc(uint32_t c, const uint8_t *p, size_t n) {
    if (!s_crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v >> 1) ^ (v & 1 ? 0xEDB88320u : 0);
            s_crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = s_crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void CmpInit(void) {
    const char *stride = getenv("SOE_COMPARE_STRIDE"), *dump = getenv("SOE_COMPARE_DUMP");
    s_cmp.state = 1;
    if (!stride && !dump) return;
    s_cmp.out = fdopen(3, "wb");
    if (!s_cmp.out) return;
    s_cmp.stride = stride ? (unsigned)strtoul(stride, NULL, 10) : 0;
    s_cmp.dump_at = dump ? strtol(dump, NULL, 10) : -1;
    s_cmp.last = -1;
    const char *r = getenv("SOE_COMPARE_REGIONS");
    if (!r) r = "wram,vram";
    static const char *const names[] = { "wram", "vram", "cgram", "oam" };
    for (const char *p = r; *p && s_cmp.nregions < 4;) {
        for (int i = 0; i < 4; i++)
            if (!strncmp(p, names[i], strlen(names[i]))) s_cmp.regions[s_cmp.nregions++] = i;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
    for (const char *p = getenv("SOE_COMPARE_MASK"); p && *p;) {
        char *end;
        unsigned long a = strtoul(p, &end, 16), b = a;
        if (*end == '-') b = strtoul(end + 1, &end, 16);
        for (unsigned long o = a; o <= b && o < 0x20000; o++) s_cmp.mask[o >> 3] |= (uint8_t)(1u << (o & 7));
        p = *end == ',' ? end + 1 : NULL;
    }
    s_cmp.state = 2;
}

/* One region as bytes: the PPU keeps VRAM/CGRAM/OAM as little-endian words, the same bytes snes9x holds. */
static const uint8_t *CmpRegion(int r, size_t *n, uint8_t *scratch) {
    Ppu *ppu = g_snes->ppu;
    switch (r) {
    case CMP_WRAM:
        memcpy(scratch, g_ram, 0x20000);
        for (size_t o = 0; o < 0x20000; o++)
            if (s_cmp.mask[o >> 3] & (1u << (o & 7))) scratch[o] = 0;
        *n = 0x20000;
        return scratch;
    case CMP_VRAM: *n = sizeof(ppu->vram); return (const uint8_t *)ppu->vram;
    case CMP_CGRAM: *n = sizeof(ppu->cgram); return (const uint8_t *)ppu->cgram;
    default:
        memcpy(scratch, ppu->oam, sizeof(ppu->oam));
        memcpy(scratch + sizeof(ppu->oam), ppu->highOam, sizeof(ppu->highOam));
        *n = sizeof(ppu->oam) + sizeof(ppu->highOam);
        return scratch;
    }
}

static void CmpFrame(void) {
    static uint8_t scratch[0x20000];
    if (!s_cmp.state) CmpInit();
    if (s_cmp.state != 2) return;
    long key = g_ram[0x100] | (g_ram[0x101] << 8);
    if (s_cmp.last < 0 && key != 0) return; /* counts from the 0 the reset code writes, as in snes9x */
    if (key <= s_cmp.last) return;
    s_cmp.last = key;
    if (s_cmp.dump_at >= 0) {
        if (key < s_cmp.dump_at) return;
        fprintf(s_cmp.out, "D %ld\n", key);
        for (int i = 0; i < s_cmp.nregions; i++) {
            size_t n;
            const uint8_t *p = CmpRegion(s_cmp.regions[i], &n, scratch);
            fwrite(p, 1, n, s_cmp.out);
        }
        fflush(s_cmp.out);
        s_cmp.dump_at = -1;
        s_cmp.state = 1;
        return;
    }
    if (!s_cmp.stride || key % s_cmp.stride) return;
    fprintf(s_cmp.out, "H %ld", key);
    for (int i = 0; i < s_cmp.nregions; i++) {
        size_t n;
        const uint8_t *p = CmpRegion(s_cmp.regions[i], &n, scratch);
        fprintf(s_cmp.out, " %08x", CmpCrc(0, p, n));
    }
    fputc('\n', s_cmp.out);
    fflush(s_cmp.out);
}

void GameRunOneFrame(void) {
    snes_beam_frame_driver_run_frame();
    CmpFrame();
}

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
