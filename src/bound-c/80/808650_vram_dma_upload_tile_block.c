/*
 * $80:8650 vram_dma_upload_tile_block (knowledge/routines/vram_dma_upload_tile_block.md), RTL, 16-bit X.
 *
 * Transfers or queues a block of tile data for PPU VRAM (word-increment mode $80):
 *  - In: DP $26..$28 = 24-bit source pointer, DP $2E = destination VRAM word address, X = transfer size in bytes.
 *  - When the PPU INIDISP shadow ($0106) has bit 7 set (forced blank), executes an immediate DMA channel 1 transfer
 *    to VMDATA ($2118) under the VRAM lock byte ($0F3A), restoring DB and returning with 16-bit A/X, A low byte = $02,
 *    X = $1801, and N Z from the restored DB (PLB).
 *  - When $0106 bit 7 is clear (display active), stages X into DP $12 and writes an 8-byte descriptor into the NMI
 *    VRAM DMA ring buffer at $013F,x (indexed by cursor $013D), then advances $013D = ($013D + 8) & $01FF.
 *    Leaves A = new cursor, X = old cursor, N Z from the masked cursor, C and V from the cursor addition.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   vram_dma_upload_tile_block ($808650) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  43 instructions (98 bytes)
 * - Compiled C ASM:   513 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   11.9x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      e947caa698d9476e
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"

#define PPU_INIDISP_SHADOW_ADDR    0x0106  /* $2100 INIDISP shadow; bit 7 = forced blank */
#define INIDISP_FORCED_BLANK       0x80

/* Direct page parameters and scratch */
#define DP_DMA_BYTE_COUNT          0x12    /* staged byte count on the ring-buffer path */
#define DP_DMA_SRC_ADDR            0x26    /* 16-bit source offset */
#define DP_DMA_SRC_BANK            0x28    /* 8-bit source bank */
#define DP_DMA_VRAM_DEST           0x2E    /* 16-bit VRAM word address */

/* PPU and DMA channel 1 hardware registers */
#define REG_VMAIN                  0x2115
#define REG_VMADDL                 0x2116
#define REG_MDMAEN                 0x420B
#define REG_DMAP1                  0x4310
#define REG_A1T1L                  0x4312
#define REG_A1B1                   0x4314
#define REG_DAS1L                  0x4315

#define VMAIN_INCREMENT_HIGH_WORD  0x80    /* increment VRAM word address after $2119 write */
#define DMA1_VRAM_WORD_WRITE       0x1801  /* 2-register write once to $2118/$2119 */
#define MDMAEN_DMA1                0x02    /* start DMA channel 1 */

/* 8-byte descriptor layout in VRAM_DMA_QUEUE ($7E:013F..$7E:033E) */
#define DMA_DESC_VMAIN_OFF         0
#define DMA_DESC_SRC_ADDR_OFF      1
#define DMA_DESC_SRC_BANK_OFF      3
#define DMA_DESC_VRAM_DEST_OFF     4
#define DMA_DESC_SIZE_OFF          6
#define DMA_DESC_STRIDE            0x0008
#define VRAM_DMA_QUEUE_MASK        0x01FF

/**
 * @brief Uploads a block of tile data to VRAM immediately during forced blank, or queues it for the next NMI.
 *
 * @param r the registers; DP $26..$28 = source pointer, DP $2E = VRAM word address, X = byte count
 */
SOE_BIND(vram_dma_upload_tile_block, 0x808650, RTL)
{
    uint8_t inidisp;
    uint16_t x, a;

    SOE_AT(0x8650);
    inidisp = ABS8(r, PPU_INIDISP_SHADOW_ADDR);
    if (inidisp & INIDISP_FORCED_BLANK) {
        uint8_t entry_db = r->db;

        SOE_AT(0x8657);
        r->db = 0x80;                                        /* PHB; PHK; PLB */
        ABS8(r, VRAM_DMA_LOCK_ADDR) = (uint8_t)(ABS8(r, VRAM_DMA_LOCK_ADDR) + 1);
        ABS8(r, REG_VMAIN) = VMAIN_INCREMENT_HIGH_WORD;
        ABS16(r, REG_DAS1L) = r->x;
        ABS16(r, REG_VMADDL) = DP16(r, DP_DMA_VRAM_DEST);
        ABS16(r, REG_A1T1L) = DP16(r, DP_DMA_SRC_ADDR);
        ABS8(r, REG_A1B1) = DP8(r, DP_DMA_SRC_BANK);
        r->x = DMA1_VRAM_WORD_WRITE;
        ABS16(r, REG_DMAP1) = DMA1_VRAM_WORD_WRITE;
        soe_set_a8(r, MDMAEN_DMA1);
        ABS8(r, REG_MDMAEN) = MDMAEN_DMA1;
        ABS8(r, VRAM_DMA_LOCK_ADDR) = (uint8_t)(ABS8(r, VRAM_DMA_LOCK_ADDR) - 1);
        r->db = entry_db;
        soe_nz8(r, entry_db);                                /* PLB */
        r->p &= (uint8_t)~(SOE_M | SOE_X);                   /* REP #$30 */
        return;
    }

    SOE_AT(0x8686);
    DP16(r, DP_DMA_BYTE_COUNT) = r->x;
    x = ABS16(r, VRAM_DMA_QUEUE_CURSOR_ADDR);
    ABSI8(r, VRAM_DMA_QUEUE_ADDR + DMA_DESC_VMAIN_OFF, x) = VMAIN_INCREMENT_HIGH_WORD;
    ABSI8(r, VRAM_DMA_QUEUE_ADDR + DMA_DESC_SRC_BANK_OFF, x) = DP8(r, DP_DMA_SRC_BANK);
    r->p &= (uint8_t)~(SOE_M | SOE_X);                       /* REP #$30 */
    ABSI16(r, VRAM_DMA_QUEUE_ADDR + DMA_DESC_SRC_ADDR_OFF, x) = DP16(r, DP_DMA_SRC_ADDR);
    ABSI16(r, VRAM_DMA_QUEUE_ADDR + DMA_DESC_VRAM_DEST_OFF, x) = DP16(r, DP_DMA_VRAM_DEST);
    ABSI16(r, VRAM_DMA_QUEUE_ADDR + DMA_DESC_SIZE_OFF, x) = DP16(r, DP_DMA_BYTE_COUNT);
    soe_carry(r, false);
    a = (uint16_t)(soe_adc16(r, x, DMA_DESC_STRIDE) & VRAM_DMA_QUEUE_MASK);
    soe_nz16(r, a);
    ABS16(r, VRAM_DMA_QUEUE_CURSOR_ADDR) = a;
    r->a = a;
    r->x = x;
}
