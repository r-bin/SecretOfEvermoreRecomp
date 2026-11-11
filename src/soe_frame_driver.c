/*
 * Secret of Evermore's frame driver: snesrecomp's beam frame driver (runner/src/beam_frame_driver.c, PolyForm
 * Noncommercial, see snesrecomp/LICENSE), run from our glue so the framework stays untouched, with the timing
 * rules MesenCE has and the framework's driver lacks (one file each in docs/imperfections/):
 * - late NMI: enabling NMI ($4200 bit 7) while the vblank flag is still set fires it at once (late-nmi.md);
 * - the reset clock and the short scanline: field lengths as MesenCE's (short-scanline.md);
 * - the interrupt sequence's 60 clocks (interrupt-cost.md) and DRAM refresh at H=538 (dram-refresh.md);
 * - the SPC700's clock rate and port access point (spc-clock-rate.md, apu-port-handshake.md): both come from the
 *   master-clock APU timeline of snesrecomp PR #241 (framework/snesrecomp.patch), fed the port access clock with the
 *   refresh stall in front of it (access_clock);
 * - the automatic joypad read's busy window (auto-joypad-read.md);
 * - where the field's NMI is recognised (nmi-recognition.md, with soe_last_cycle.c);
 * - $4212 bit 7 as the access sees it (vblank-flag-at-access.md).
 * What the PPU and registers lack (DMA during rendering, open bus, 15-bit CGRAM) is in soe_ppu_fixes.c. Only hardware
 * rules belong here, no game: the candidate for upstream (docs/pr/05-frame-driver.md). One exception, not part of
 * that candidate and never on in the reference build: free time (soe_frame_driver_free_time), which game-changing
 * options of the glue use to let a region of game code cost no emulated time.
 *
 * The game's reset code and its scene loads switch NMI back on inside vblank (MesenCE: the reset code at V=226,
 * 6 times in the first 4 minutes). While NMI is off and the vblank flag is set, the CPU runs one instruction per
 * bridge call (a master-clock deadline one clock ahead) and the NMI is taken after the instruction that enabled it.
 *
 * The rendering half (snes_beam_frame_driver_draw_ppu_frame) is still the framework's. Save states read and set
 * the resume point through soe_frame_driver_*.
 */
#include "soe_frame_driver.h"
#include "soe_last_cycle.h"
#include "soe_ppu_fixes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/apu.h"
#include "snes/cart.h"
#include "snes/cx4.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/sa1.h"
#include "snes/snes.h"
#include "snes/superfx.h"

static void set_deadline(uint64_t clock);
static void set_slice_deadline(uint64_t clock, uint64_t hard);

extern CpuState g_cpu;
extern Ppu *g_ppu;

#define FIELD_CLOCKS 357368ull
#define LINE_CLOCKS 1364u
#define VBLANK_LINE 225u
#define MAX_SLICES_PER_FIELD 1024
#define RESET_CLOCKS 186u
/* Single-instruction slices while waiting for a late NMI enable: a vblank holds ~37 lines of them. */
#define MAX_LATE_NMI_STEPS 200000

static uint32_t s_resume_pc; /* 0 until the first frame has booted from the reset vector */
static bool s_wai_halted;

uint32_t soe_frame_driver_resume_pc(void) { return s_resume_pc; }
bool soe_frame_driver_wai_halted(void) { return s_wai_halted; }
void soe_frame_driver_set_resume(uint32_t pc, bool wai_halted) { s_resume_pc = pc; s_wai_halted = wai_halted; }


static uint32_t read_vector(uint32_t addr) {
    return ((uint32_t)snes_read(g_snes, addr + 1u) << 8) | snes_read(g_snes, addr);
}

static void update_resume_pc(void) {
    uint32_t resume = interp_bridge_lle_resume_pc();
    if (resume) s_resume_pc = resume;
}

static bool cart_irq_pending(void) {
    const Cart *cart = g_snes->cart;
    if (!cart) return false;
    if (cart->superfx && cart->superfx->irq_pending) return true;
    if (cart->cx4 && cx4_irq_pending(cart->cx4)) return true;
    if (cart->sa1 && sa1_cpu_irq_pending(cart->sa1)) return true;
    return false;
}

static bool irq_line_asserted(void) { return g_snes->inIrq || cart_irq_pending(); }
static bool irq_wanted(void) { return !g_cpu._flag_I && irq_line_asserted(); }

static uint64_t beam_debt(void) {
    return g_cpu.master_cycles > g_snes->beamMasterLast ? g_cpu.master_cycles - g_snes->beamMasterLast : 0;
}

static void advance_parked(uint64_t until) {
    g_cpu.master_cycles = until;
    snes_refresh_exempt();
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
    cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
}

void (*soe_frame_driver_nmi_hook)(void);

/*
 * DRAM refresh. Hardware stalls the CPU (and DMA) for 40 master clocks at H=538 of every line: 40 of each line's
 * 1364 clocks (MesenCE SnesMemoryManager: a DramRefresh event each scanline). snesrecomp's runtime charges 40 per
 * 1364 clocks of CPU time instead, at no fixed beam position: 0.09 % short (1,400 clocks over the boot's DMAs), and
 * the stall falls on other instructions. Here each CPU slice ends at the next refresh point; the framework's own
 * charge for the slice is worked out from its refresh state and topped up to one stall per point the slice crossed.
 * docs/imperfections/dram-refresh.md.
 */
#define REFRESH_H 538u
#define REFRESH_CLOCKS 40u
static uint64_t s_line0; /* master clock where this field's first line (V=225) started */
static bool s_field_parity; /* this field's parity (snes.h fieldParity): whether its line 240 is the short one */

/* Where line 225 + j starts, from this field's line 225 on: every line 1364 clocks, the short lines 240 4 fewer. */
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

/*
 * Refresh point j of the field (line 225 + j). MesenCE (memoryManager.dramRefreshPosition) puts it at 538 - (the
 * line's start & 7) clocks into the line: lines are 1364 clocks (4 mod 8), so it alternates between H=538 and 534.
 * From line 241 on, a field with the short line 240 has its lines 4 clocks earlier.
 */
static uint64_t refresh_point(uint64_t j) {
    const uint64_t line = line_start(j);
    return line + REFRESH_H - (line & 7u);
}

static uint64_t refresh_points_upto(uint64_t m) {
    if (m < refresh_point(0)) return 0;
    uint64_t j = (m - s_line0) / LINE_CLOCKS;
    while (j > 0 && refresh_point(j) > m) j--;
    while (refresh_point(j + 1) <= m) j++;
    return j + 1;
}

uint64_t soe_frame_driver_refresh_points_upto(uint64_t clock) { return refresh_points_upto(clock); }
uint64_t soe_frame_driver_line0(void) { return s_line0; }

static uint64_t next_refresh_after(uint64_t m) {
    return refresh_point(refresh_points_upto(m));
}

/*
 * The SPC700's clock rate. snesrecomp maps 357,368 master clocks to 17,088 SPC cycles (1.02528 MHz at exactly
 * 60 Hz): 0.16 % faster than MesenCE, which runs it at 32,040 Hz x 32 = 1,025,280 cycles per 21,477,270 master
 * clocks, so every handshake with the sound driver was answered early. After each slice (at most a line) the APU's
 * guest-time anchor moves forward by the difference (docs/imperfections/spc-clock-rate.md).
 * The anchor also starts 20 master clocks behind: the interpreter reaches the APU ports at an instruction's start
 * clock, where MesenCE runs the SPC700 up to 2 clocks into the access cycle, 20 clocks in for the game's absolute
 * CMP/STA $2140 (docs/imperfections/apu-port-handshake.md).
 */
#define SPC_PER_MASTER (1025280.0 / 21477270.0)
#define APU_ACCESS_LEAD 20.0
static void slow_apu_to_reference(void) {
    if (rtl_apu_master_clock()) return; /* the framework's master-clock timeline has MesenCE's rate and access point */
    static uint64_t last;
    static double owed = -APU_ACCESS_LEAD * SPC_PER_MASTER;
    const uint64_t now = g_cpu.master_cycles;
    if (now > last) owed += (double)(now - last) * (17088.0 / 357368.0 - SPC_PER_MASTER);
    last = now;
    if (owed >= 1.0 && g_snes->apu->portTimeValid) {
        const uint64_t whole = (uint64_t)owed;
        g_snes->apu->portGuestAnchor += whole;
        owed -= (double)whole;
    }
}

/*
 * The clock of a port access `lead` clocks into the instruction starting at `start` (the interpreter supplies the
 * access's own lead), for the framework's master-clock APU timeline. MesenCE stalls the CPU for DRAM refresh at the
 * refresh point itself; this driver charges the stall after the instruction. A refresh point from the instruction's
 * start up to the access comes before it (fitted on 14,780 port reads of the attract run's loads: no exception).
 */
static bool s_free_apu;
static uint64_t access_clock(uint64_t start, uint32_t lead) {
    s_free_apu = true; /* free time: this slice talked to the APU (or synced it) and keeps its time */
    return start + lead; /* the refresh stall in front of the access is the framework's (snes_refresh_at_beam) */
}

/* DRAM refresh is the framework's (snes_refresh_at_beam): one stall per refresh point the CPU crosses, charged per
 * opcode or block. The slices stop at each point so the stall falls between the right instructions; a slice's tail
 * is charged when it ends. */
static void refresh_begin(void) {}
static void refresh_end(void) { snes_refresh_charge(); slow_apu_to_reference(); }

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

/*
 * The interrupt sequence's own time, which the framework's driver does not charge: 8 CPU cycles in native mode,
 * 2 internal (6 master clocks each), PB/PCH/PCL/P pushed to the WRAM stack (8 each), the vector read from bank $00
 * ROM (8 each) = 60 master clocks. MesenCE takes each field's NMI 76 clocks after V=225: the instruction the beam
 * crossed it in, then these 60.
 */
#define INTERRUPT_CYCLES 8u
#define INTERRUPT_CLOCKS (2u * 6u + 4u * 8u + 2u * 8u)

static void run_handler(uint32_t vector, uint64_t deadline, bool nmi, bool counted);

static void run_interrupt(uint32_t vector, uint64_t deadline, bool nmi) {
    if (nmi) soe_ppu_fixes_nmi_entry();
    cpu_push_interrupt_frame_at(&g_cpu, s_resume_pc);
    /* The 65816 clears D as it takes an interrupt, cpu_push_interrupt_frame_at does not: an NMI inside the BCD routine at
     * $80:B283 ran its handler in decimal mode and its DMA queue walked off (NMI 6844 of replay 05). */
    g_cpu._flag_D = 0;
    g_cpu.P &= (uint8_t)~CPU_P_D;
    g_cpu.cycles += INTERRUPT_CYCLES;
    /* A refresh point inside the sequence stalls it (MesenCE: the handler starts 40 clocks later); the handler's
     * own refresh accounting below only starts after it. */
    const uint64_t seq = g_cpu.master_cycles;
    g_cpu.master_cycles += INTERRUPT_CLOCKS;
    (void)seq;
    snes_refresh_charge();
    run_handler(vector, deadline, nmi, true);
}

/* An interrupt's handler from `vector`, its frame already pushed. `counted`: an NMI the compare samples as a key (an NMI
 * a MesenCE state had already taken is not one: MesenCE's first key is the next NMI). */
static void run_handler(uint32_t vector, uint64_t deadline, bool nmi, bool counted) {
    if (nmi) soe_ppu_fixes_nmi_taken();
    if (nmi && counted && soe_frame_driver_nmi_hook) soe_frame_driver_nmi_hook();
    const uint64_t before = g_cpu.master_cycles;
    /* The NMI handler runs as an ordinary slice from its vector (the frame is pushed above), so it stops at the next
     * refresh point like every other slice and goes on in the field's slices. Run whole to its RTI, which ignores
     * the deadline, it got the framework's refresh charge (40 per 1364 clocks of work from its start), whose stalls
     * fall after MesenCE's when the handler starts late in vblank (docs/imperfections/dram-refresh.md). */
    const uint64_t refresh = next_refresh_after(before) + 1;
    refresh_begin();
    if (nmi) {
        set_deadline(refresh < deadline ? refresh : deadline);
        (void)interp_bridge_run_until_quiescent(&g_cpu, vector);
    } else {
        set_deadline(deadline);
        (void)interp_bridge_run_interrupt(&g_cpu, vector);
    }
    set_deadline(0);
    refresh_end();
    update_resume_pc();
}

static void run_nmi(uint64_t deadline) {
    g_snes->inNmi = true;
    s_wai_halted = false;
    run_interrupt(read_vector(0x00FFEAu), deadline, true);
    g_snes->inNmi = false;
}

/*
 * Where the field's NMI is recognised. MesenCE raises the NMI line at H=6 of line 225 (InternalRegisters, hClock 6)
 * and the CPU's edge detector sees it at the start of its next cycle (SnesCpu::DetectNmiSignalEdge); the NMI is
 * taken after the instruction that cycle belongs to. So the instruction must still start a cycle at V=225+6 or
 * later. The framework's driver took the NMI after the first instruction to end past V=225, up to an instruction
 * early (the idle loop at $80:858F interrupted at the other of its two instructions). The CPU runs on to V=225+12
 * (a 6-clock last cycle starting at +6); the final slice records each instruction's address, and when the last
 * one's last cycle started before +6 (an 8- or 12-clock access ending before +14 or +18) one more instruction runs
 * (docs/imperfections/nmi-recognition.md).
 */
#define NMI_LINE_CLOCKS 6u
#define NMI_SEEN_CLOCKS 12u

/* Every deadline goes through here, so a C binding can tell whether it would run past one. `hard` is the first
 * stop that is not a DRAM refresh: a refresh is charged after the slice from the points it crossed (refresh_end),
 * so code that only touches WRAM may run past one; past `hard` (the field end, a single step, an interrupt run's
 * end) nothing may. */
static uint64_t s_deadline, s_hard_deadline;
static void set_slice_deadline(uint64_t clock, uint64_t hard) {
    s_deadline = clock;
    s_hard_deadline = hard;
    interp_bridge_set_master_deadline(clock);
}
static void set_deadline(uint64_t clock) { set_slice_deadline(clock, clock); }
uint64_t soe_frame_driver_deadline(void) { return s_deadline; }
/* For a C binding that runs on past refresh stops: what it crossed is charged now, so the clock it checks is true. */
void soe_frame_driver_settle_refresh(void) { refresh_end(); }
uint64_t soe_frame_driver_hard_deadline(void) { return s_hard_deadline; }
bool soe_frame_driver_irq_possible(void) {
    return !g_cpu._flag_I && (g_snes->hIrqEnabled || g_snes->vIrqEnabled || irq_line_asserted());
}

static uint32_t s_last_pc;
static uint64_t s_last_dma; /* g_snes_dma_charged as the last instruction started */
/* SOE_BANK_STATS=1: instructions run per bank (compiled and interpreted), printed at exit as "[bank] $8C: n". It
 * shows which mirror copies of the code ever run (docs/progress.md, "Banks that run"). The counting hook stays
 * installed between slices and is treated as "no hook" there, so the run is the same with it on. */
static uint64_t s_bank_insns[256], s_insns, s_steps, s_step_insns;
static int s_bank_stats = -1;
static uint32_t *s_pc_counts; /* SOE_PC_COUNTS=<file>: runs per PC, written at exit as "<pc24> <count>" lines */
static void count_pc(uint32_t pc24, int m_flag, int x_flag) {
    (void)m_flag; (void)x_flag;
    s_bank_insns[(pc24 >> 16) & 0xFF]++;
    s_insns++;
    if (s_pc_counts) s_pc_counts[pc24 & 0xFFFFFF]++;
}
static void print_bank_stats(void) {
    for (int b = 0; b < 256; b++)
        if (s_bank_insns[b]) fprintf(stderr, "[bank] $%02X: %llu\n", b, (unsigned long long)s_bank_insns[b]);
    fprintf(stderr, "[bank] single-step slices (late-NMI watch, NMI approach): %llu, %llu instructions\n",
            (unsigned long long)s_steps, (unsigned long long)s_step_insns);
    const char *path = getenv("SOE_PC_COUNTS");
    FILE *f = s_pc_counts && path ? fopen(path, "w") : NULL;
    if (!f) return;
    for (uint32_t pc = 0; pc < 0x1000000u; pc++)
        if (s_pc_counts[pc]) fprintf(f, "%06X %u\n", pc, s_pc_counts[pc]);
    fclose(f);
}
/* Around a single-step slice: how many there are and what they run (the tier stats count them as interpreted). */
static uint64_t step_begin(void) { if (s_bank_stats > 0) s_steps++; return s_insns; }
static void step_end(uint64_t from) { if (s_bank_stats > 0) s_step_insns += s_insns - from; }
static void bank_stats_init(void) {
    if (s_bank_stats >= 0) return;
    const char *e = getenv("SOE_BANK_STATS");
    s_bank_stats = e && e[0] == '1';
    if (!s_bank_stats) return;
    if (getenv("SOE_PC_COUNTS")) s_pc_counts = calloc(0x1000000u, sizeof *s_pc_counts);
    g_interp_bridge_pc_hook = count_pc;
    atexit(print_bank_stats);
}

uint32_t soe_frame_driver_last_pc(void) { return s_last_pc; }

static void record_pc(uint32_t pc24, int m_flag, int x_flag) {
    if (s_bank_stats > 0) count_pc(pc24, m_flag, x_flag);
    s_last_pc = pc24;
    s_last_dma = g_snes_dma_charged;
}

/* After the slice that reached V=225+12: run one more instruction if MesenCE would. */
static void finish_nmi_approach(uint64_t frame_end) {
    if (!s_last_pc || g_cpu.master_cycles < frame_end + NMI_SEEN_CLOCKS) return;
    /* A DMA the last instruction started ($420B) runs in the next instruction's first cycle on MesenCE: its clocks,
     * which snesrecomp charges to the write, do not count as a cycle of this instruction. */
    const uint64_t dma = g_snes_dma_charged - s_last_dma;
    const unsigned last = soe_last_cycle_clocks(s_last_pc, s_resume_pc);
    if (g_cpu.master_cycles - dma - last >= frame_end + NMI_LINE_CLOCKS) return;
    const uint64_t before = g_cpu.master_cycles;
    const uint64_t counted = step_begin();
    refresh_begin();
    set_deadline(before + 1);
    (void)interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
    set_deadline(0);
    refresh_end();
    step_end(counted);
    update_resume_pc();
    if (interp_bridge_lle_took_wai()) s_wai_halted = true;
}

/* NMI can still fire late in this field: it is off, and the vblank flag has not been read or cleared. */
static bool late_nmi_possible(void) { return !g_snes->nmiEnabled && g_snes->rdnmiPending; }

/* A MesenCE state sits at line 225 with the instruction that NMI will follow still to run (its line rises at H=6), or,
 * when MesenCE left its frame loop later in the line, with the NMI already taken: the PC on the handler's first
 * instruction, the frame pushed (replay 07: H=80, $00:823A). Then the first field runs that handler instead of
 * taking another NMI (docs/imperfections/mesen-state.md). */
static bool s_state_in_nmi;

static void start_from_mesen_state(void) {
    const uint32_t pc = soe_mesen_state_load();
    if (!pc) return;
    s_resume_pc = pc;
    soe_compare_ready();
    interp_bridge_set_lle_resume_pc(pc);
    soe_compare_pad_first(g_snes->beamMasterLast - g_snes->hPos + FIELD_CLOCKS);
    s_state_in_nmi = g_snes->nmiEnabled && pc == read_vector(0x00FFEAu) && g_snes->hPos >= NMI_LINE_CLOCKS + INTERRUPT_CLOCKS;
    if (s_state_in_nmi) return;
    set_deadline(g_cpu.master_cycles + 1);
    (void)interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
    set_deadline(0);
    update_resume_pc();
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
}

void soe_frame_driver_run_frame(void) {
    bank_stats_init();
    soe_ppu_fixes_install();
    if (s_resume_pc == 0) start_from_mesen_state();
    const bool booting = s_resume_pc == 0;
    int slices = 0, late_steps = 0;
    uint64_t frame_end;
    {
        uint32_t to_vblank = snes_master_clocks_until_line(g_snes, VBLANK_LINE);
        frame_end = g_snes->beamMasterLast + (to_vblank ? to_vblank : FIELD_CLOCKS);
    }
    soe_compare_pad(g_snes->beamMasterLast);
    if (s_free) { s_free = false; snes_beam_hold(0); } /* never into a new field */
    s_free_limit = frame_end > 2u * LINE_CLOCKS ? frame_end - 2u * LINE_CLOCKS : 0;
    if (booting) {
        soe_compare_start_memory();
        s_resume_pc = read_vector(0x00FFFCu);
        /* The 65816's reset sequence: MesenCE runs the first instruction at master clock 186, the beam from 0. */
        if (g_cpu.master_cycles < RESET_CLOCKS) advance_parked(RESET_CLOCKS);
    } else {
        ppu_checkOverscan(g_ppu);
        ppu_handleVblank(g_ppu);
    }
    snes_set_hdma_beam_enabled(g_snes, false);
    snes_set_raster_irq_beam_enabled(true);
    ppu_rasterBegin(g_ppu);
    /* Line 225's start: a host frame can begin lines past it (the last one ran on, as a late NMI's watch or a long
     * DMA does); the auto-joypad window and the refresh points count from line 225 itself. */
    {
        uint64_t back = g_snes->hPos;
        const bool past_wrap = !booting && g_snes->vPos < VBLANK_LINE;
        s_field_parity = g_snes->fieldParity != past_wrap;
        if (g_snes->vPos > VBLANK_LINE && g_snes->vPos < 262u) back += (uint64_t)(g_snes->vPos - VBLANK_LINE) * LINE_CLOCKS;
        /* Past the wrap: a DMA ran through the whole vblank (NMI 3065 of replay 05 comes on line 29), the window long over. */
        else if (past_wrap) back += (uint64_t)(g_snes->vPos + 262u - VBLANK_LINE) * LINE_CLOCKS;
        if ((past_wrap || g_snes->vPos > 240u) && snes_line_clocks(g_snes, 240u, s_field_parity) != LINE_CLOCKS) back -= 4u;
        /* A state loaded before the first frame: snesrecomp sets the beam's clock to the CPU's, still near 0, under the
         * saved V/H, so line 225 would have started before clock 0 and every refresh point below wrapped around (a
         * hang in refresh_points_upto). Pinned to 0 the first field's refresh points sit up to a line off; from the
         * next field they are exact again. Found 2026-11-03 (tests/save_state_resume.sh, docs/bugs-and-todos.md). */
        s_line0 = g_snes->beamMasterLast > back ? g_snes->beamMasterLast - back : 0;
    }
    RtlSetCpuAccessClockHook(access_clock);

    if (s_state_in_nmi) {   /* the state's NMI, already taken: its handler, not counted as a key */
        s_state_in_nmi = false;
        g_snes->inNmi = true;
        s_wai_halted = false;
        run_handler(s_resume_pc, frame_end, true, false);
        g_snes->inNmi = false;
    } else if (!booting && g_snes->nmiEnabled) run_nmi(frame_end);

    const uint64_t cpu_end = frame_end + NMI_SEEN_CLOCKS;
    while (g_cpu.master_cycles < cpu_end && slices++ < MAX_SLICES_PER_FIELD) {
        if (s_wai_halted && irq_line_asserted()) s_wai_halted = false;
        if (!s_wai_halted && !irq_wanted()) {
            /* While a late NMI is possible, stop after every instruction to see whether it enabled NMI. */
            const bool watch = !booting && late_nmi_possible() && late_steps < MAX_LATE_NMI_STEPS;
            const uint64_t before = g_cpu.master_cycles;
            uint64_t deadline = watch ? before + 1 : next_refresh_after(before) + 1;
            if (deadline > cpu_end) deadline = cpu_end;
            /* Every slice records its instructions: a slice that stops for a refresh can still end past V=225 (a
             * long DMA), and its last instruction decides the NMI as the final slice's does. */
            const bool approach = !g_interp_bridge_pc_hook || g_interp_bridge_pc_hook == count_pc;
            if (approach) { s_last_pc = 0; g_interp_bridge_pc_hook = record_pc; }
            const uint64_t counted = watch ? step_begin() : 0;
            refresh_begin();
            /* A refresh stop before the field end: only the field end (or the watched step) is hard. */
            set_slice_deadline(deadline, watch || deadline >= frame_end ? deadline : frame_end);
            (void)interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
            if (watch) step_end(counted);
            set_deadline(0);
            refresh_end();
            if (free_slice_end()) slices--; /* free slices do not count against the field's slice budget */
            if (approach) g_interp_bridge_pc_hook = s_bank_stats > 0 ? count_pc : 0;
            update_resume_pc();
            /* The beam where the CPU is: RDNMI and the vblank flag for the late-NMI watch below. */
            if (!s_free) snes_sync_master_clock(g_snes, g_cpu.master_cycles);
            soe_ppu_fixes_slice_end();
            if (interp_bridge_lle_took_wai()) s_wai_halted = true;
            if (approach && !s_wai_halted) finish_nmi_approach(frame_end);
            const bool refresh_stop = !watch && deadline < cpu_end && g_cpu.master_cycles > before;
            /* A step that ran nothing (a parked wait) falls through to the parking below, as unwatched. */
            if (watch && g_cpu.master_cycles > before && g_cpu.master_cycles < frame_end) {
                late_steps++;
                slices--; /* single steps do not count against the field's slice budget */
                if (g_snes->nmiEnabled && g_snes->rdnmiPending) {
                    /* MesenCE (InternalRegisters $4200, SnesCpu::DetectNmiSignalEdge): the write sets a counter of 2,
                     * counted down at each following cycle start, so the NMI comes after the next instruction. */
                    const uint64_t at = g_cpu.master_cycles, counted = step_begin();
                    refresh_begin();
                    set_deadline(at + 1);
                    (void)interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
                    set_deadline(0);
                    step_end(counted);
                    refresh_end();
                    update_resume_pc();
                    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
                    run_nmi(frame_end);
                }
                continue;
            }
            if (refresh_stop && g_cpu.master_cycles < cpu_end) {
                slices--; /* refresh stops are not parks: they do not count against the slice budget */
                continue;
            }
        }
        if (g_cpu.master_cycles >= frame_end) break;
        if (irq_wanted()) {
            s_wai_halted = false;
            run_interrupt(read_vector(0x00FFEEu), frame_end, false);
            continue;
        }
        if (beam_debt()) {
            snes_sync_master_clock(g_snes, g_cpu.master_cycles);
            cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
            continue;
        }
        uint32_t to_irq = snes_master_clocks_until_irq(g_snes);
        if (to_irq) to_irq += 1;
        if (to_irq == 0 || to_irq > LINE_CLOCKS) to_irq = LINE_CLOCKS;
        uint64_t next = g_cpu.master_cycles + to_irq;
        if (next > frame_end) next = frame_end;
        advance_parked(next);
    }

    {
        int walk = 0;
        if (g_cpu.master_cycles < frame_end) advance_parked(frame_end);
        while (g_snes->beamMasterLast < frame_end && walk++ < 1200) {
            uint64_t before = g_snes->beamMasterLast, want = frame_end - before;
            snes_advance_master_cycles(g_snes, want > UINT32_MAX ? UINT32_MAX : (uint32_t)want);
            if (g_snes->beamMasterLast == before) break;
        }
        cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
    }
}

void soe_frame_driver_reset(void) {
    s_resume_pc = 0;
    s_wai_halted = false;
}
