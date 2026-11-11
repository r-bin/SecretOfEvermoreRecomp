/*
 * Secret of Evermore's frame driver: snesrecomp's beam frame driver (runner/src/beam_frame_driver.c, PolyForm
 * Noncommercial, see snesrecomp/LICENSE) with its MesenCE rules on (snes_beam_frame_driver_exact: the interrupt
 * sequence, NMI recognition, the late NMI, slices to each DRAM refresh point; snes.c: the short line, $4212 at the
 * access, DRAM refresh at the beam). docs/pr/06-frame-driver.md has where each rule came from; the evidence is one
 * file each in docs/imperfections/.
 *
 * What stays here is ours, through the driver's hooks (SnesBeamFrameHooks):
 * - the compare: a replay's input per field, the memory a compare run starts with, the NMI samples, starting from a
 *   MesenCE state (soe_mesen_state.c), also inside its NMI handler;
 * - what the PPU and registers lack (soe_ppu_fixes.c), which needs the beam's line at a DMA's bytes (line_start);
 * - free time (soe_frame_driver_free_time), which game-changing options of the glue use to let a region of game code
 *   cost no emulated time; never on in the reference build;
 * - the deadlines the C bindings must not run past (src/soe_bindings.c), and SOE_BANK_STATS.
 *
 * The rendering half (snes_beam_frame_driver_draw_ppu_frame) is the framework's. Save states read and set the resume
 * point through soe_frame_driver_*.
 */
#include "soe_frame_driver.h"
#include "soe_ppu_fixes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "beam_frame_driver.h"
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/cx4.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/sa1.h"
#include "snes/snes.h"
#include "snes/superfx.h"

extern CpuState g_cpu;
extern Ppu *g_ppu;

#define FIELD_CLOCKS 357368ull
#define LINE_CLOCKS 1364u
#define VBLANK_LINE 225u
#define REFRESH_H 538u
/* The interrupt sequence (60 master clocks, beam_frame_driver.c) and where the NMI line rises (H=6 of line 225). */
#define INTERRUPT_CLOCKS 60u
#define NMI_LINE_CLOCKS 6u

uint32_t soe_frame_driver_resume_pc(void) {
    uint32_t pc;
    bool wai;
    snes_beam_frame_driver_get_position(&pc, &wai);
    return pc;
}
bool soe_frame_driver_wai_halted(void) {
    uint32_t pc;
    bool wai;
    snes_beam_frame_driver_get_position(&pc, &wai);
    return wai;
}
void soe_frame_driver_set_resume(uint32_t pc, bool wai_halted) { snes_beam_frame_driver_set_position(pc, wai_halted); }

static uint32_t read_vector(uint32_t addr) {
    return ((uint32_t)snes_read(g_snes, addr + 1u) << 8) | snes_read(g_snes, addr);
}

void (*soe_frame_driver_nmi_hook)(void);

/*
 * The beam's lines from this field's line 225 on, for soe_ppu_fixes.c, which places a DMA's bytes on them (a DMA can
 * run through a whole field): every line 1364 clocks, the short lines 240 4 fewer (snes_line_clocks), and the DRAM
 * refresh point of each at 538 - (its start & 7) (MesenCE memoryManager.dramRefreshPosition).
 */
static uint64_t s_line0; /* master clock where this field's first line (V=225) started */
static bool s_field_parity; /* this field's parity (snes.h fieldParity): whether its line 240 is the short one */

/* Where line 225 + j starts. */
static uint64_t line_start(uint64_t j) {
    uint64_t start = s_line0 + j * LINE_CLOCKS;
    bool parity = s_field_parity;
    for (uint64_t m = 240u - VBLANK_LINE; m < j; m += 262u, parity = !parity)
        start -= LINE_CLOCKS - snes_line_clocks(g_snes, 240u, parity);
    return start;
}

/* The line (225 + j) the beam is on at master clock m >= line 225's start, and H there. */
static uint64_t line_at(uint64_t m, uint64_t *h) {
    uint64_t j = (m - s_line0) / LINE_CLOCKS;
    while (line_start(j + 1) <= m) j++;
    if (h) *h = m - line_start(j);
    return j;
}

void soe_frame_driver_beam_at(uint64_t clock, unsigned *line, unsigned *h) {
    uint64_t hh = 0;
    const uint64_t j = clock > s_line0 ? line_at(clock, &hh) : 0;
    *line = (unsigned)((VBLANK_LINE + j) % 262u);
    *h = (unsigned)hh;
}

static uint64_t refresh_point(uint64_t j) {
    const uint64_t line = line_start(j);
    return line + REFRESH_H - (line & 7u);
}

uint64_t soe_frame_driver_refresh_points_upto(uint64_t m) {
    if (m < refresh_point(0)) return 0;
    uint64_t j = (m - s_line0) / LINE_CLOCKS;
    while (j > 0 && refresh_point(j) > m) j--;
    while (refresh_point(j + 1) <= m) j++;
    return j + 1;
}
uint64_t soe_frame_driver_line0(void) { return s_line0; }

/* Line 225's start: a host frame can begin lines past it (the last one ran on, as a late NMI's watch or a long DMA
 * does), and past the wrap when a DMA ran through the whole vblank (NMI 3065 of replay 05 comes on line 29). A state
 * loaded before the first frame: snesrecomp sets the beam's clock to the CPU's, still near 0, under the saved V/H, so
 * line 225 would have started before clock 0; pinned to 0 the first field's lines sit up to a line off, from the next
 * field they are exact again (tests/save_state_resume.sh, docs/bugs-and-todos.md). */
static void find_line0(bool booting) {
    uint64_t back = g_snes->hPos;
    const bool past_wrap = !booting && g_snes->vPos < VBLANK_LINE;
    s_field_parity = g_snes->fieldParity != past_wrap;
    if (g_snes->vPos > VBLANK_LINE && g_snes->vPos < 262u) back += (uint64_t)(g_snes->vPos - VBLANK_LINE) * LINE_CLOCKS;
    else if (past_wrap) back += (uint64_t)(g_snes->vPos + 262u - VBLANK_LINE) * LINE_CLOCKS;
    if ((past_wrap || g_snes->vPos > 240u) && snes_line_clocks(g_snes, 240u, s_field_parity) != LINE_CLOCKS) back -= 4u;
    s_line0 = g_snes->beamMasterLast > back ? g_snes->beamMasterLast - back : 0;
}

/* A port access to the APU marks the slice for free time below; its clock (and the refresh stall in front of it) is
 * the framework's. */
static bool s_free_apu;
static uint64_t access_clock(uint64_t start, uint32_t lead) {
    s_free_apu = true; /* free time: this slice talked to the APU (or synced it) and keeps its time */
    return start + lead;
}

/*
 * Free time: a host feature for game-changing options of the glue (widescreen E6, src/soe_ws_alive.c), never on in
 * the reference build. Between soe_frame_driver_free_time(true) and (false) the CPU's work costs no time: the beam is
 * held (snes_beam_hold) and after every slice the clock goes back to where free time began, so no line, NMI, IRQ,
 * HDMA or refresh sees that work. A slice that touched the APU keeps its time, so a handshake with the sound CPU
 * still gets its answer (the SPC700 runs on the master clock); free time then goes on from there.
 * Free time stays two lines clear of the field's vblank: it does not start later than that (the frame runs as usual),
 * and ends when kept APU time reaches it. So the NMI never comes inside it: the handler waits for the joypad read,
 * which a held beam never finishes.
 */
#define MAX_FREE_SLICES 200000
static bool s_free;
static uint64_t s_free_mark, s_free_cycles;
static int s_free_slices;
static uint64_t s_free_limit; /* this field's vblank less two lines */
static struct { unsigned long long rewound, kept, refused; } s_free_stats;
static void free_mark(void) { s_free_mark = g_cpu.master_cycles; s_free_cycles = g_cpu.cycles; s_free_apu = false; }
static void free_rewind(void) {
    if (s_free_apu) {
        s_free_stats.kept++;
        free_mark();
        if (g_cpu.master_cycles >= s_free_limit) { s_free = false; snes_beam_hold(0); } /* the rest of it in real time */
        return;
    }
    s_free_stats.rewound++;
    g_cpu.master_cycles = s_free_mark;
    g_cpu.cycles = s_free_cycles;
    snes_refresh_state_set(0, s_free_mark);
}
static void print_free_stats(void) {
    fprintf(stderr, "[free] %llu slices rewound, %llu kept (APU), %llu starts refused (near vblank)\n",
            s_free_stats.rewound, s_free_stats.kept, s_free_stats.refused);
}
void soe_frame_driver_free_time(bool on) {
    static int s_debug = -1;
    if (s_debug < 0) { const char *e = getenv("SOE_FREE_STATS"); s_debug = e && e[0] == '1'; if (s_debug) atexit(print_free_stats); }
    if (on == s_free) return;
    if (on) {
        if (g_cpu.master_cycles >= s_free_limit) { s_free_stats.refused++; return; }
        free_mark(); s_free = true; s_free_slices = 0; snes_beam_hold(1);
        return;
    }
    free_rewind();
    s_free = false;
    snes_beam_hold(0);
}
/* After a slice: free time sets the clock back (a runaway region ends free time rather than hang the field). */
static bool free_slice_end(void) {
    if (!s_free) return false;
    if (++s_free_slices > MAX_FREE_SLICES) { fprintf(stderr, "soe_frame_driver: free time ran away at $%06X, off\n", (unsigned)interp_bridge_lle_resume_pc()); soe_frame_driver_free_time(false); return false; }
    free_rewind();
    return true;
}

/* The deadlines of the slice the driver runs, so a C binding can tell whether it would run past one. `hard` is the
 * first stop that is not a DRAM refresh: a refresh is charged from the points the CPU crossed, so code that only
 * touches WRAM may run past one; past `hard` (the field end, a single step, an interrupt run's end) nothing may. */
uint64_t soe_frame_driver_deadline(void) { uint64_t d; snes_beam_frame_driver_deadlines(&d, NULL); return d; }
uint64_t soe_frame_driver_hard_deadline(void) { uint64_t h; snes_beam_frame_driver_deadlines(NULL, &h); return h; }
/* For a C binding that runs on past refresh stops: what it crossed is charged now, so the clock it checks is true. */
void soe_frame_driver_settle_refresh(void) { snes_refresh_charge(); }
static bool cart_irq_pending(void) {
    const Cart *cart = g_snes->cart;
    if (!cart) return false;
    if (cart->superfx && cart->superfx->irq_pending) return true;
    if (cart->cx4 && cx4_irq_pending(cart->cx4)) return true;
    if (cart->sa1 && sa1_cpu_irq_pending(cart->sa1)) return true;
    return false;
}
bool soe_frame_driver_irq_possible(void) {
    return !g_cpu._flag_I && (g_snes->hIrqEnabled || g_snes->vIrqEnabled || g_snes->inIrq || cart_irq_pending());
}
uint32_t soe_frame_driver_last_pc(void) { return snes_beam_frame_driver_last_pc(); }

/* SOE_BANK_STATS=1: instructions run per bank (compiled and interpreted), printed at exit as "[bank] $8C: n". It
 * shows which mirror copies of the code ever run (docs/progress.md, "Banks that run"). The driver chains its own
 * instruction hook to this one. */
static uint64_t s_bank_insns[256];
static int s_bank_stats = -1;
static uint32_t *s_pc_counts; /* SOE_PC_COUNTS=<file>: runs per PC, written at exit as "<pc24> <count>" lines */
static void count_pc(uint32_t pc24, int m_flag, int x_flag) {
    (void)m_flag; (void)x_flag;
    s_bank_insns[(pc24 >> 16) & 0xFF]++;
    if (s_pc_counts) s_pc_counts[pc24 & 0xFFFFFF]++;
}
static void print_bank_stats(void) {
    for (int b = 0; b < 256; b++)
        if (s_bank_insns[b]) fprintf(stderr, "[bank] $%02X: %llu\n", b, (unsigned long long)s_bank_insns[b]);
    const char *path = getenv("SOE_PC_COUNTS");
    FILE *f = s_pc_counts && path ? fopen(path, "w") : NULL;
    if (!f) return;
    for (uint32_t pc = 0; pc < 0x1000000u; pc++)
        if (s_pc_counts[pc]) fprintf(f, "%06X %u\n", pc, s_pc_counts[pc]);
    fclose(f);
}
static void bank_stats_init(void) {
    if (s_bank_stats >= 0) return;
    const char *e = getenv("SOE_BANK_STATS");
    s_bank_stats = e && e[0] == '1';
    if (!s_bank_stats) return;
    if (getenv("SOE_PC_COUNTS")) s_pc_counts = calloc(0x1000000u, sizeof *s_pc_counts);
    g_interp_bridge_pc_hook = count_pc;
    atexit(print_bank_stats);
}

/* A MesenCE state sits at line 225 with the instruction that NMI will follow still to run (its line rises at H=6), or,
 * when MesenCE left its frame loop later in the line, with the NMI already taken: the PC on the handler's first
 * instruction, the frame pushed (replay 07: H=80, $00:823A). Then the first field runs that handler instead of
 * taking another NMI (docs/imperfections/mesen-state.md). */
static bool s_state_in_nmi;

static void start_from_mesen_state(void) {
    const uint32_t pc = soe_mesen_state_load();
    if (!pc) return;
    snes_beam_frame_driver_set_position(pc, false);
    soe_compare_ready();
    interp_bridge_set_lle_resume_pc(pc);
    soe_compare_pad_first(g_snes->beamMasterLast - g_snes->hPos + FIELD_CLOCKS);
    s_state_in_nmi = g_snes->nmiEnabled && pc == read_vector(0x00FFEAu) && g_snes->hPos >= NMI_LINE_CLOCKS + INTERRUPT_CLOCKS;
    if (s_state_in_nmi) return;
    interp_bridge_set_master_deadline(g_cpu.master_cycles + 1);
    (void)interp_bridge_run_until_quiescent(&g_cpu, pc);
    interp_bridge_set_master_deadline(0);
    const uint32_t resume = interp_bridge_lle_resume_pc();
    if (resume) snes_beam_frame_driver_set_position(resume, false);
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
}

static void on_frame_start(uint64_t frame_end, bool booting) {
    soe_compare_pad(g_snes->beamMasterLast);
    if (s_free) { s_free = false; snes_beam_hold(0); } /* never into a new field */
    s_free_limit = frame_end > 2u * LINE_CLOCKS ? frame_end - 2u * LINE_CLOCKS : 0;
    if (booting) soe_compare_start_memory();
    find_line0(booting);
    RtlSetCpuAccessClockHook(access_clock);
}

static void on_interrupt_entry(bool nmi) { if (nmi) soe_ppu_fixes_nmi_entry(); }

static void on_nmi_taken(void) {
    soe_ppu_fixes_nmi_taken();
    if (soe_frame_driver_nmi_hook) soe_frame_driver_nmi_hook();
}

/* The state's NMI, already taken: its handler as the driver runs an NMI's (a slice to the next refresh point), not
 * counted as a key: MesenCE's first key is the next NMI. */
static bool on_field_nmi(uint64_t frame_end) {
    if (!s_state_in_nmi) return false;
    s_state_in_nmi = false;
    uint32_t pc;
    bool wai;
    snes_beam_frame_driver_get_position(&pc, &wai);
    g_snes->inNmi = true;
    soe_ppu_fixes_nmi_taken();
    const uint64_t refresh = snes_next_refresh(g_snes, g_cpu.master_cycles) + 1u;
    interp_bridge_set_master_deadline(refresh < frame_end ? refresh : frame_end);
    (void)interp_bridge_run_until_quiescent(&g_cpu, pc);
    interp_bridge_set_master_deadline(0);
    snes_refresh_charge();
    const uint32_t resume = interp_bridge_lle_resume_pc();
    if (resume) snes_beam_frame_driver_set_position(resume, false);
    g_snes->inNmi = false;
    return true;
}

static bool on_slice_end(void) {
    const bool taken_back = free_slice_end();
    soe_ppu_fixes_slice_end();
    return taken_back;
}

static const SnesBeamFrameHooks s_hooks = {
    .frame_start = on_frame_start,
    .interrupt_entry = on_interrupt_entry,
    .nmi_taken = on_nmi_taken,
    .field_nmi = on_field_nmi,
    .slice_end = on_slice_end,
};

void soe_frame_driver_run_frame(void) {
    bank_stats_init();
    soe_ppu_fixes_install();
    snes_beam_frame_driver_set_hooks(&s_hooks);
    if (soe_frame_driver_resume_pc() == 0) start_from_mesen_state();
    snes_beam_frame_driver_run_frame();
}

void soe_frame_driver_reset(void) { snes_beam_frame_driver_reset(); }
