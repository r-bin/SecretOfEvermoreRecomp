/*
 * PPU and register behaviour MesenCE has and snesrecomp's PPU lacks, applied from outside through the framework's
 * hooks (one file each in docs/imperfections/). They are not frame timing: the right place for each is snesrecomp's
 * PPU or register code, where they could cover CPU writes too; here they cover what the game does, by DMA
 * (docs/pr/README.md, "Our own code that could follow"):
 * - CGRAM words have 15 bits (snesrecomp keeps the high byte's bit 7);
 * - $213B reads bit 7 of the high byte from the PPU2 open bus (cgram-read-open-bus.md);
 * - $4212 bits 1-5 are open bus (vblank-flag-at-access.md);
 * - CGRAM writes during rendering go to the colour being read (cgram-writes-during-rendering.md);
 * - VRAM writes outside vblank and forced blank are dropped (vram-writes-during-rendering.md).
 * The beam position of a DMA's bytes comes from the frame driver: its line 225 and its DRAM refresh points.
 */
#include "soe_ppu_fixes.h"
#include "soe_frame_driver.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/dma.h"
#include "snes/ppu.h"
#include "snes/snes.h"

extern CpuState g_cpu;
extern Ppu *g_ppu;
extern Snes *g_snes;

#define LINE_CLOCKS 1364u
#define VBLANK_LINE 225u
#define REFRESH_CLOCKS 40u

static bool refresh_between(uint64_t from, uint64_t to) {
    return soe_frame_driver_refresh_points_upto(to) > soe_frame_driver_refresh_points_upto(from);
}

/*
 * $4212 bits 1-5 are open bus on MesenCE, $42 from the address after LDA $4212: bit 1 reads 1. snesrecomp reads 0 there
 * and the read goes past our files, but the game only tests bits 6, 7 and 0 of it, so only an NMI sees the rest: the
 * handler pushes A, the frame P, while the value still sits in A (docs/imperfections/vblank-flag-at-access.md).
 */
void soe_ppu_fixes_nmi_entry(void) {
    uint32_t pc = soe_frame_driver_last_pc();
    if (!pc || !g_cpu.m_flag) return;
    const uint8_t op = snes_read(g_snes, pc);
    if (op == 0x10 || op == 0x30) pc -= 3; /* the wait's BPL / BMI */
    if (snes_read(g_snes, pc) != 0xAD || snes_read(g_snes, pc + 1) != 0x12 || snes_read(g_snes, pc + 2) != 0x42) return;
    g_cpu.A |= 0x02;
    g_cpu._flag_Z = 0;
    g_cpu.P &= (uint8_t)~CPU_P_Z;
}

void soe_ppu_fixes_nmi_taken(void) {
    for (int i = 0; i < 256; i++) g_ppu->cgram[i] &= 0x7FFF; /* 15 bits; snesrecomp stores the high byte's bit 7 */
}

/*
 * A CGRAM word has 15 bits: MesenCE's $213B returns bit 7 of the high byte from the PPU2 open bus, which the low byte's
 * read just set; snesrecomp returns 0 and has no such bus. The game reads CGRAM back by DMA, whose stores into WRAM pass
 * this hook after they landed (docs/imperfections/cgram-read-open-bus.md).
 */
static void cgram_dma_open_bus(uint32_t wram, uint8_t value, const char *via) {
    static uint8_t low;
    wlog_addr_note_direct(wram, value, via);
    int i = 0;
    while (i < 8 && !g_snes->dma->channel[i].dmaActive) i++;
    if (i == 8 || !g_snes->dma->channel[i].fromB || g_snes->dma->channel[i].bAdr != 0x3B) return;
    if (g_ppu->cgramSecondWrite) low = value; /* the read has already moved the latch on */
    else g_snes->ram[wram] = (uint8_t)(value | (low & 0x80));
}

/*
 * A DMA to $2122 during rendering. Outside vblank, forced blank and H<88, H>=1096 MesenCE writes a colour to the one the
 * PPU is reading, not the CGRAM address; snesrecomp to the address. Which colour needs the pixel, which we do not have:
 * colour 0 here, as in all 349 such writes of replay 05. The charge comes before the transfer: CGRAM is kept here, and the
 * writes are walked when the slice ends (docs/imperfections/cgram-writes-during-rendering.md).
 */
#define CGRAM_DMA_LEAD 30u /* STA $420B to the next opcode fetch, as snesrecomp's charge counts it */
static struct { bool pending, second; uint8_t pointer; uint16_t size; uint64_t start; uint16_t cgram[256]; } s_cgram_dma;

static bool cgram_writable(uint64_t t) {
    unsigned line, h;
    soe_frame_driver_beam_at(t, &line, &h);
    return line >= VBLANK_LINE || line == 0 || h < 88u || h >= 1096u;
}

/*
 * A DMA to VRAM outside vblank and forced blank: MesenCE drops each byte, the address still counts; snesrecomp stores it
 * (its `TODO: vram access during rendering`). Replay 01's NMI 5997 sends 60 KB through the picture. VRAM is kept when such
 * a DMA starts, and each byte that lands while the picture is drawn is put back as it passes the write hook.
 */
static struct { unsigned left; uint64_t t; uint16_t vram[0x8000]; } s_vram_dma;

static bool vram_writable(uint64_t t) {
    unsigned line, h;
    soe_frame_driver_beam_at(t, &line, &h);
    return line >= VBLANK_LINE;
}

static void vram_dma_write(uint32_t byte_addr, uint8_t value) {
    (void)value;
    if (!s_vram_dma.left) return;
    if (refresh_between(s_vram_dma.t, s_vram_dma.t + 8u)) s_vram_dma.t += REFRESH_CLOCKS;
    if (!vram_writable(s_vram_dma.t)) {
        const uint16_t mask = byte_addr & 1u ? 0xFF00 : 0x00FF, word = (uint16_t)(byte_addr >> 1);
        g_ppu->vram[word] = (uint16_t)((g_ppu->vram[word] & ~mask) | (s_vram_dma.vram[word] & mask));
    }
    s_vram_dma.t += 8u;
    s_vram_dma.left--;
}

static uint64_t dma_first_byte(uint64_t start) { return start + (8u - (start & 7u)) + 24u; } /* setup, channel, the byte itself */

static void charge_dma(Snes *snes, uint64_t clocks) {
    const uint64_t start = g_cpu.master_cycles + CGRAM_DMA_LEAD;
    s_vram_dma.left = 0;
    for (int i = 0; i < 8 && !PPU_forcedBlank(g_ppu); i++) {
        const DmaChannel *c = &snes->dma->channel[i];
        /* The hook is not told the channels and old sizes stay in the others: this one, alone, is what the charge fits. */
        const uint64_t body = 16u + 8u * (c->size ? (uint64_t)c->size : 0x10000u);
        if (c->fromB || clocks < body || clocks > body + 32u * (1u + clocks / LINE_CLOCKS)) continue;
        if (c->bAdr == 0x18 || c->bAdr == 0x19) {
            if (vram_writable(start) && vram_writable(start + clocks) && clocks < LINE_CLOCKS * (262u - VBLANK_LINE)) break;
            s_vram_dma.left = c->size ? c->size : 0x10000u;
            s_vram_dma.t = dma_first_byte(start);
            memcpy(s_vram_dma.vram, g_ppu->vram, sizeof s_vram_dma.vram);
            break;
        }
        if (c->bAdr != 0x22 || c->mode != 0 || !c->size) continue;
        s_cgram_dma.pending = true;
        s_cgram_dma.second = g_ppu->cgramSecondWrite;
        s_cgram_dma.pointer = g_ppu->cgramPointer;
        s_cgram_dma.size = c->size;
        s_cgram_dma.start = start;
        memcpy(s_cgram_dma.cgram, g_ppu->cgram, sizeof s_cgram_dma.cgram);
        break;
    }
    g_cpu.master_cycles += clocks;
    snes_sync_master_clock(snes, g_cpu.master_cycles);
}

void soe_ppu_fixes_slice_end(void) {
    if (!s_cgram_dma.pending) return;
    s_cgram_dma.pending = false;
    uint64_t t = dma_first_byte(s_cgram_dma.start);
    bool second = s_cgram_dma.second;
    uint8_t pointer = s_cgram_dma.pointer;
    for (unsigned i = 0; i < s_cgram_dma.size; i++, t += 8u) {
        if (refresh_between(t, t + 8u)) t += REFRESH_CLOCKS;
        if (second) {
            s_cgram_dma.cgram[cgram_writable(t) ? pointer : 0] = g_ppu->cgram[pointer];
            pointer++;
        }
        second = !second;
    }
    memcpy(g_ppu->cgram, s_cgram_dma.cgram, sizeof s_cgram_dma.cgram);
}

void soe_ppu_fixes_install(void) {
    snes_set_wram_write_log_hook(cgram_dma_open_bus);
    snes_set_master_clock_charge_hook(charge_dma);
    ppu_set_vram_write_log_hook(vram_dma_write);
}
