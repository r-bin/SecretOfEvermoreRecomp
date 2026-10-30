/*
 * Secret of Evermore's frame driver: snesrecomp's beam frame driver (runner/src/beam_frame_driver.c, PolyForm
 * Noncommercial, see snesrecomp/LICENSE), run from our glue so the framework stays untouched, with one hardware
 * rule it lacks: enabling NMI ($4200 bit 7) while the vblank NMI flag is still set fires an NMI at once.
 *
 * The game's reset code and its scene loads switch NMI back on inside vblank (MesenCE: the reset code at V=226,
 * 6 times in the first 4 minutes). The framework's driver delivers NMI only at the start of a field, so the
 * recomp's NMI count fell one behind at each of them (docs/imperfections/late-nmi.md). Here, while NMI is off and
 * the vblank flag is set, the CPU runs one instruction per bridge call (a master-clock deadline one clock ahead)
 * and the NMI is taken after the instruction that enabled it.
 *
 * Everything else follows the framework's driver; the rendering half (snes_beam_frame_driver_draw_ppu_frame)
 * is still the framework's. Save states read and set the resume point through soe_frame_driver_*.
 */
#include "soe_frame_driver.h"

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

static uint64_t refresh_points_upto(uint64_t m) {
    const uint64_t first = s_line0 + REFRESH_H;
    return m < first ? 0 : (m - first) / LINE_CLOCKS + 1;
}

static uint64_t next_refresh_after(uint64_t m) {
    return s_line0 + REFRESH_H + refresh_points_upto(m) * LINE_CLOCKS;
}

/*
 * The SPC700's clock rate. snesrecomp maps 357,368 master clocks to 17,088 SPC cycles (1.02528 MHz at exactly
 * 60 Hz): 0.16 % faster than MesenCE, which runs it at 32,040 Hz x 32 = 1,025,280 cycles per 21,477,270 master
 * clocks, so every handshake with the sound driver was answered early. After each slice (at most a line) the APU's
 * guest-time anchor moves forward by the difference (docs/imperfections/spc-clock-rate.md).
 */
#define SPC_PER_MASTER (1025280.0 / 21477270.0)
static void slow_apu_to_reference(void) {
    static uint64_t last;
    static double owed;
    const uint64_t now = g_cpu.master_cycles;
    if (now > last) owed += (double)(now - last) * (17088.0 / 357368.0 - SPC_PER_MASTER);
    last = now;
    if (owed >= 1.0 && g_snes->apu->portTimeValid) {
        const uint64_t whole = (uint64_t)owed;
        g_snes->apu->portGuestAnchor += whole;
        owed -= (double)whole;
    }
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
    g_cpu.master_cycles += INTERRUPT_CLOCKS;
    if (nmi && soe_frame_driver_nmi_hook) soe_frame_driver_nmi_hook();
    const uint64_t before = g_cpu.master_cycles;
    refresh_begin();
    interp_bridge_set_master_deadline(deadline);
    (void)interp_bridge_run_interrupt(&g_cpu, vector);
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
    s_line0 = g_snes->beamMasterLast - g_snes->hPos;

    if (!booting && g_snes->nmiEnabled) run_nmi(frame_end);

    while (g_cpu.master_cycles < frame_end && slices++ < MAX_SLICES_PER_FIELD) {
        if (s_wai_halted && irq_line_asserted()) s_wai_halted = false;
        if (!s_wai_halted && !irq_wanted()) {
            /* While a late NMI is possible, stop after every instruction to see whether it enabled NMI. */
            const bool watch = !booting && late_nmi_possible() && late_steps < MAX_LATE_NMI_STEPS;
            const uint64_t before = g_cpu.master_cycles;
            uint64_t deadline = watch ? before + 1 : next_refresh_after(before) + 1;
            if (deadline > frame_end) deadline = frame_end;
            refresh_begin();
            interp_bridge_set_master_deadline(deadline);
            (void)interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
            interp_bridge_set_master_deadline(0);
            refresh_end(before);
            const bool refresh_stop = !watch && deadline < frame_end && g_cpu.master_cycles > before;
            update_resume_pc();
            if (interp_bridge_lle_took_wai()) s_wai_halted = true;
            /* A step that ran nothing (a parked wait) falls through to the parking below, as unwatched. */
            if (watch && g_cpu.master_cycles > before && g_cpu.master_cycles < frame_end) {
                late_steps++;
                slices--; /* single steps do not count against the field's slice budget */
                if (g_snes->nmiEnabled && g_snes->rdnmiPending) {
                    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
                    run_nmi(frame_end);
                }
                continue;
            }
            if (refresh_stop && g_cpu.master_cycles < frame_end) {
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

static void align_field_length(void) {
    const uint64_t beam = g_snes->beamMasterLast;
    if (beam < FIRST_VBLANK || g_snes->vPos != VBLANK_LINE || g_snes->hPos != 0) return;
    const uint64_t k = (beam - FIRST_VBLANK + MEAN_FIELD / 2) / MEAN_FIELD;
    const int64_t h = (int64_t)beam + (int64_t)FIELD_CLOCKS - (int64_t)field_end(k + 1);
    if (h > 0 && h < LINE_CLOCKS) g_snes->hPos = (uint16_t)h;
}

void soe_frame_driver_reset(void) {
    s_resume_pc = 0;
    s_wai_halted = false;
}
