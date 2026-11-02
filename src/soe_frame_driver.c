/*
 * Secret of Evermore's frame driver: snesrecomp's beam frame driver (runner/src/beam_frame_driver.c, PolyForm
 * Noncommercial, see snesrecomp/LICENSE), run from our glue so the framework stays untouched, with the timing
 * rules MesenCE has and the framework's driver lacks (one file each in docs/imperfections/):
 * - late NMI: enabling NMI ($4200 bit 7) while the vblank flag is still set fires it at once (late-nmi.md);
 * - the reset clock and the short scanline: field lengths as MesenCE's (short-scanline.md);
 * - the interrupt sequence's 60 clocks (interrupt-cost.md) and DRAM refresh at H=538 (dram-refresh.md);
 * - the SPC700's clock rate and port access point (spc-clock-rate.md, apu-port-handshake.md); on branch
 *   snesrecomp-spc-access-cycles both come from the framework's master-clock APU timeline instead, fed the port
 *   access clock with the refresh stall in front of it (access_clock);
 * - the automatic joypad read's busy window (auto-joypad-read.md);
 * - where the field's NMI is recognised (nmi-recognition.md, with soe_last_cycle.c).
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

#include <stdbool.h>
#include <stdint.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/apu.h"
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
#define MAX_SLICES_PER_FIELD 1024
#define RESET_CLOCKS 186u
/* Single-instruction slices while waiting for a late NMI enable: a vblank holds ~37 lines of them. */
#define MAX_LATE_NMI_STEPS 200000

static uint32_t s_resume_pc; /* 0 until the first frame has booted from the reset vector */
static bool s_wai_halted;

uint32_t soe_frame_driver_resume_pc(void) { return s_resume_pc; }
bool soe_frame_driver_wai_halted(void) { return s_wai_halted; }
void soe_frame_driver_set_resume(uint32_t pc, bool wai_halted) { s_resume_pc = pc; s_wai_halted = wai_halted; }

static void align_field_length(void);

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

static uint64_t mesen_line0(void);
/*
 * Refresh point j of the field (line 225 + j). MesenCE (memoryManager.dramRefreshPosition) puts it at 538 - (the
 * line's start & 7) clocks into the line: lines are 1364 clocks (4 mod 8), so it alternates between H=538 and 534.
 * Lines 225-240 of the fields whose short line this driver takes at V=225 start 4 clocks later on MesenCE
 * (mesen_line0); from line 241 on its lines and the beam's agree.
 */
static uint64_t refresh_point(uint64_t j) {
    const uint64_t line = (j < 16 ? mesen_line0() : s_line0) + j * LINE_CLOCKS;
    return line + REFRESH_H - (line & 7u);
}

static uint64_t refresh_points_upto(uint64_t m) {
    if (m < refresh_point(0)) return 0;
    uint64_t j = (m - s_line0) / LINE_CLOCKS;
    while (j > 0 && refresh_point(j) > m) j--;
    while (refresh_point(j + 1) <= m) j++;
    return j + 1;
}

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
static uint64_t access_clock(uint64_t start, uint32_t lead) {
    const uint64_t a = start + lead;
    return refresh_points_upto(a) > refresh_points_upto(start - 1) ? a + REFRESH_CLOCKS : a;
}

static void refresh_begin(void) { snes_refresh_state_set(0, g_cpu.master_cycles); }

static void refresh_end(uint64_t before) {
    uint64_t phase = 0, upto = 0;
    snes_refresh_state_get(&phase, &upto);
    const uint64_t after = g_cpu.master_cycles;
    /* From phase 0 at `before`: after = before + work + 40 * charged, phase = work - 1364 * charged. */
    const uint64_t charged = after > before + phase ? (after - before - phase) / (LINE_CLOCKS + REFRESH_CLOCKS) : 0;
    uint64_t crossed = refresh_points_upto(after) - refresh_points_upto(before);
    while (crossed > charged) {
        const uint64_t was = g_cpu.master_cycles;
        g_cpu.master_cycles += REFRESH_CLOCKS;
        crossed += refresh_points_upto(g_cpu.master_cycles) - refresh_points_upto(was);
        crossed--;
    }
    snes_refresh_state_set(0, g_cpu.master_cycles);
    slow_apu_to_reference();
}

/*
 * The automatic joypad read. MesenCE (internalRegisters.autoReadClockStart) starts it on the first master clock
 * that is 128 past a multiple of 256 and at least 3 clocks after its line 225 began, and $4212 bit 0 reads busy
 * for 4352 clocks (17 x 256) from there, as the interpreter samples it: at the instruction's start clock, where
 * MesenCE samples 2 clocks into the access cycle, 20 clocks in for BIT/LDA absolute. MesenCE's line 225 is
 * field_end(k): in the fields whose short line this driver takes at V=225 the beam's line 225 starts 4 clocks
 * earlier (see the short scanline below). Measured over NMIs 2-330 of 01-no-inputs-4m.evsmv: every start, and with
 * it every NMI's clock and interrupted address through 319 (docs/imperfections/auto-joypad-read.md).
 * snesrecomp's beam holds the bit from V=225 for 4224 clocks; once the beam has crossed V=225 the timer is set to end
 * at MesenCE's end.
 */
#define AUTO_JOY_GRID 128u
#define AUTO_JOY_MIN_AFTER 3u
#define AUTO_JOY_CLOCKS 4352u
#define AUTO_JOY_READ_LEAD 20u /* BIT/LDA absolute: 3 fetches at 6, then MesenCE's sample 2 clocks into the read */
static bool s_auto_joy_due; /* this field's read still needs MesenCE's timing */

static void auto_joypad_begin(void) {
    if (!s_auto_joy_due || g_snes->beamMasterLast <= s_line0) return;
    s_auto_joy_due = false;
    const uint64_t from = mesen_line0() + AUTO_JOY_MIN_AFTER;
    const uint64_t start = from + (AUTO_JOY_GRID - from % 256u + 256u) % 256u;
    const uint64_t end = start + AUTO_JOY_CLOCKS - AUTO_JOY_READ_LEAD, beam = g_snes->beamMasterLast;
    static int log = -1;
    if (log < 0) log = getenv("SOE_AUTO_JOY_LOG") != NULL;
    if (log)
        fprintf(stderr, "[aj] line0=%llu start=%llu end=%llu beam=%llu cpu=%llu on=%d timer=%u\n",
                (unsigned long long)mesen_line0(), (unsigned long long)start, (unsigned long long)end,
                (unsigned long long)beam, (unsigned long long)g_cpu.master_cycles, g_snes->autoJoyRead,
                (unsigned)g_snes->autoJoyTimer);
    if (!g_snes->autoJoyRead) return;
    g_snes->autoJoyTimer = end > beam ? (uint16_t)(end - beam) : 0;
}

/*
 * The interrupt sequence's own time, which the framework's driver does not charge: 8 CPU cycles in native mode,
 * 2 internal (6 master clocks each), PB/PCH/PCL/P pushed to the WRAM stack (8 each), the vector read from bank $00
 * ROM (8 each) = 60 master clocks. MesenCE takes each field's NMI 76 clocks after V=225: the instruction the beam
 * crossed it in, then these 60.
 */
#define INTERRUPT_CYCLES 8u
#define INTERRUPT_CLOCKS (2u * 6u + 4u * 8u + 2u * 8u)

static void run_interrupt(uint32_t vector, uint64_t deadline, bool nmi) {
    cpu_push_interrupt_frame_at(&g_cpu, s_resume_pc);
    g_cpu.cycles += INTERRUPT_CYCLES;
    /* A refresh point inside the sequence stalls it (MesenCE: the handler starts 40 clocks later); the handler's
     * own refresh accounting below only starts after it. */
    const uint64_t seq = g_cpu.master_cycles;
    g_cpu.master_cycles += INTERRUPT_CLOCKS;
    if (refresh_points_upto(g_cpu.master_cycles - 1) > refresh_points_upto(seq - 1)) g_cpu.master_cycles += REFRESH_CLOCKS;
    if (nmi && s_auto_joy_due) {
        snes_sync_master_clock(g_snes, g_cpu.master_cycles); /* the beam crosses V=225 before the handler reads $4212 */
        auto_joypad_begin();
    }
    if (nmi && soe_frame_driver_nmi_hook) soe_frame_driver_nmi_hook();
    const uint64_t before = g_cpu.master_cycles;
    /* The NMI handler runs as an ordinary slice from its vector (the frame is pushed above), so it stops at the next
     * refresh point like every other slice and goes on in the field's slices. Run whole to its RTI, which ignores
     * the deadline, it got the framework's refresh charge (40 per 1364 clocks of work from its start), whose stalls
     * fall after MesenCE's when the handler starts late in vblank (docs/imperfections/dram-refresh.md). */
    const uint64_t refresh = next_refresh_after(before) + 1;
    refresh_begin();
    if (nmi) {
        interp_bridge_set_master_deadline(refresh < deadline ? refresh : deadline);
        (void)interp_bridge_run_until_quiescent(&g_cpu, vector);
    } else {
        interp_bridge_set_master_deadline(deadline);
        (void)interp_bridge_run_interrupt(&g_cpu, vector);
    }
    interp_bridge_set_master_deadline(0);
    refresh_end(before);
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

static uint32_t s_last_pc;
static uint64_t s_last_dma; /* g_snes_dma_charged as the last instruction started */
static void record_pc(uint32_t pc24, int m_flag, int x_flag) {
    (void)m_flag; (void)x_flag;
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
    refresh_begin();
    interp_bridge_set_master_deadline(before + 1);
    (void)interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
    interp_bridge_set_master_deadline(0);
    refresh_end(before);
    update_resume_pc();
    if (interp_bridge_lle_took_wai()) s_wai_halted = true;
}

/* NMI can still fire late in this field: it is off, and the vblank flag has not been read or cleared. */
static bool late_nmi_possible(void) { return !g_snes->nmiEnabled && g_snes->rdnmiPending; }

void soe_frame_driver_run_frame(void) {
    const bool booting = s_resume_pc == 0;
    int slices = 0, late_steps = 0;
    uint64_t frame_end;
    {
        uint32_t to_vblank = snes_master_clocks_until_line(g_snes, VBLANK_LINE);
        frame_end = g_snes->beamMasterLast + (to_vblank ? to_vblank : FIELD_CLOCKS);
    }
    if (booting) {
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
    s_line0 = g_snes->beamMasterLast - g_snes->hPos;
    if (g_snes->vPos > VBLANK_LINE && g_snes->vPos < 262u)
        s_line0 -= (uint64_t)(g_snes->vPos - VBLANK_LINE) * LINE_CLOCKS;
    s_auto_joy_due = !booting;
    RtlSetCpuAccessClockHook(access_clock);

    if (!booting && g_snes->nmiEnabled) run_nmi(frame_end);

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
            const bool approach = !g_interp_bridge_pc_hook;
            if (approach) { s_last_pc = 0; g_interp_bridge_pc_hook = record_pc; }
            refresh_begin();
            interp_bridge_set_master_deadline(deadline);
            (void)interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
            interp_bridge_set_master_deadline(0);
            refresh_end(before);
            if (approach) g_interp_bridge_pc_hook = 0;
            update_resume_pc();
            if (interp_bridge_lle_took_wai()) s_wai_halted = true;
            if (approach && !s_wai_halted) finish_nmi_approach(frame_end);
            auto_joypad_begin();
            const bool refresh_stop = !watch && deadline < cpu_end && g_cpu.master_cycles > before;
            /* A step that ran nothing (a parked wait) falls through to the parking below, as unwatched. */
            if (watch && g_cpu.master_cycles > before && g_cpu.master_cycles < frame_end) {
                late_steps++;
                slices--; /* single steps do not count against the field's slice budget */
                if (g_snes->nmiEnabled && g_snes->rdnmiPending) {
                    /* MesenCE (InternalRegisters $4200, SnesCpu::DetectNmiSignalEdge): the write sets a counter of 2,
                     * counted down at each following cycle start, so the NMI comes after the next instruction. */
                    const uint64_t at = g_cpu.master_cycles;
                    refresh_begin();
                    interp_bridge_set_master_deadline(at + 1);
                    (void)interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
                    interp_bridge_set_master_deadline(0);
                    refresh_end(at);
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
    align_field_length();
}

/*
 * The short scanline. On NTSC without interlace, line 240 of every other field lasts 1360 master clocks, not 1364
 * (fullsnes; MesenCE SnesPpu.cpp: "scanline 240 of every other frame ... is only 1360 cycles"). snesrecomp's beam
 * makes every field 357,368 clocks, so the recomp gained 2 clocks a field on MesenCE: 774 by field 378, enough to
 * move where the decompressor stood when NMI came back (docs/imperfections/short-scanline.md).
 * Field k ends (V=225) at FIRST_VBLANK + k * 357,366 (+2 when k is odd), measured on MesenCE from power-on. At the
 * end of each field the beam's H position is set so that the next V=225 falls there; the 4 clocks are taken at
 * V=225 rather than at line 240, which moves H by 4 clocks for lines 225-240 of those fields only.
 */
#define FIRST_VBLANK (225ull * LINE_CLOCKS)
#define MEAN_FIELD 357366ull

static uint64_t field_end(uint64_t k) { return FIRST_VBLANK + k * MEAN_FIELD + (k & 1 ? 2 : 0); }

/* Where MesenCE's line 225 of this field started: field_end(k). Our beam's starts 4 clocks earlier in the fields
 * whose short line it takes at V=225 (see above). */
static uint64_t mesen_line0(void) {
    if (s_line0 < FIRST_VBLANK) return s_line0;
    const uint64_t k = (s_line0 - FIRST_VBLANK + MEAN_FIELD / 2) / MEAN_FIELD;
    return field_end(k) == s_line0 + 4 ? s_line0 + 4 : s_line0; /* only once the field grid holds (not in boot) */
}

static void align_field_length(void) {
    /* The beam can stand past H=0 here: an instruction that ended after V=225 already moved it. */
    const uint64_t line = g_snes->beamMasterLast - g_snes->hPos; /* where the beam's line 225 started */
    if (line < FIRST_VBLANK || g_snes->vPos != VBLANK_LINE) return;
    const uint64_t k = (line - FIRST_VBLANK + MEAN_FIELD / 2) / MEAN_FIELD;
    const int64_t h = (int64_t)g_snes->hPos + (int64_t)line + (int64_t)FIELD_CLOCKS - (int64_t)field_end(k + 1);
    if (h >= 0 && h < LINE_CLOCKS) g_snes->hPos = (uint16_t)h;
}

void soe_frame_driver_reset(void) {
    s_resume_pc = 0;
    s_wai_halted = false;
}
