/*
 * $90:A1B4 map_anim_tile_channel_tick_and_vram_stream (knowledge/routines/map_anim_tile_channel_tick_and_vram_stream.md),
 * RTL, 16-bit A and X.
 *
 * Ticks the active room's background tile animation channels ($7E:4FE6/$5018/$504A, count in $7E:106C) and queues up to
 * 10 expired 16x16 4bpp tiles (two 64-byte halves from the bank-$EE tile catalog) for VRAM DMA via $80:8650. When the
 * 10-tile per-frame budget is exhausted, remaining active channel timers still tick down without streaming. Deducts
 * 12 words per updated tile from the per-frame VRAM budget at $7E:00D8 (clamped at 0), restores DB, and returns.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   map_anim_tile_channel_tick_and_vram_stream ($90A1B4) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  114 instructions (227 bytes)
 * - Compiled C ASM:   869 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   7.6x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      0df9a718c6dacb9d
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"

#define MAX_UPDATES_PER_FRAME      0x000A
#define ANIM_LOOP_INFINITE         0xFF
#define TILE_HALF_BYTES            0x0040
#define SECOND_HALF_VRAM_BIT       0x0100
#define VRAM_COST_PER_UPDATE       12      /* (updates * 3) << 2 */
#define TILE_CATALOG_BASE          0xEE0000UL

/* Direct page slots used during channel tick and DMA setup */
#define DP_UPDATES_LEFT            0x02    /* remaining tile upload budget (starts at 10) */
#define DP_SCRATCH                 0x12    /* max frame count / current frame / shifted slot */
#define DP_DMA_SRC_ADDR            0x26    /* 24-bit source pointer in DP $26..$28 */
#define DP_DMA_VRAM_DEST           0x2E    /* 16-bit destination VRAM word address */
#define DP_ANIM_DESC_PTR           0xA7    /* 24-bit room animation descriptor pointer */
#define DP_VRAM_BUDGET             0xD8    /* per-frame VRAM transfer budget ($00D8) */

void vram_dma_upload_tile_block_bind(soe_regs r);

/**
 * @brief Ticks active background tile animation channels and queues expired 16x16 tiles for VRAM DMA.
 *
 * @param r the registers; DP $A7..$A9 points to the active room's tile animation descriptor table
 */
SOE_BIND(map_anim_tile_channel_tick_and_vram_stream, 0x90A1B4, RTL)
{
    uint8_t entry_db = r->db;
    uint16_t x = 0xFFFF, y = r->y;
    uint16_t updates, deduct, a;

    SOE_AT(0xA1B4);
    r->db = 0x7E;                                            /* PHB; PEA $7E7E; PLB; PLB */
    DP16(r, DP_UPDATES_LEFT) = MAX_UPDATES_PER_FRAME;

    for (;;) {
        uint8_t cycles, timer, max_frames, frame;
        uint32_t desc;
        uint16_t slot_off, tile_id, ptr_off;

        SOE_AT(0xA1C4);
        x = (uint16_t)(x + 1);
        cycles = ABSI8(r, MAP_ANIM_CYCLES_ADDR, x);
        if (cycles == 0) continue;
        SOE_AT(0xA1CA);
        if (x == ABS16(r, ACTIVE_ANIM_CHANNELS_ADDR)) {
            SOE_AT(0xA1D6);
            break;
        }
        timer = (uint8_t)(ABSI8(r, MAP_ANIM_TIMERS_ADDR, x) - 1);
        ABSI8(r, MAP_ANIM_TIMERS_ADDR, x) = timer;
        if (!(timer & 0x80)) continue;

        /* $90:A1D9: timer expired, advance keyframe index in $7E:4FE6,x */
        y = (uint16_t)((x << 2) + 1);
        desc = SOE_DPPTR(r, DP_ANIM_DESC_PTR);
        max_frames = LONG8(desc + y);
        DP8(r, DP_SCRATCH) = max_frames;
        frame = (uint8_t)(ABSI8(r, MAP_ANIM_FRAMES_ADDR, x) + 1);
        if (frame == max_frames) {
            SOE_AT(0xA1EE);
            r->a = r->d;                                     /* TDC */
            frame = (uint8_t)r->a;
        } else {
            SOE_AT(0xA1EF);
        }
        ABSI8(r, MAP_ANIM_FRAMES_ADDR, x) = frame;

        /* $90:A1F2: resolve 3-byte frame record at [$A7],(ch*4+2) + frame*3 */
        DP16(r, DP_SCRATCH) = frame;
        y = (uint16_t)(y + 1);
        soe_carry(r, false);
        y = soe_adc16(r, (uint16_t)(frame * 3), LONG16(desc + y));

        /* $90:A200: reload timer and decrement finite loop counter */
        ABSI8(r, MAP_ANIM_TIMERS_ADDR, x) = soe_adc8(r, LONG8(desc + y), ABSI8(r, MAP_ANIM_TIMERS_ADDR, x));
        cycles = ABSI8(r, MAP_ANIM_CYCLES_ADDR, x);
        if (cycles == ANIM_LOOP_INFINITE) {
            SOE_AT(0xA215);
        } else {
            SOE_AT(0xA211);
            ABSI8(r, MAP_ANIM_CYCLES_ADDR, x) = (uint8_t)(cycles - 1);
        }

        /* $90:A215: compute destination VRAM word address from base tile slot $7E:106E */
        slot_off = (uint16_t)((x + ABS16(r, ANIM_TILE_VRAM_BASE_ADDR)) << 5);
        DP16(r, DP_SCRATCH) = slot_off;
        DP16(r, DP_DMA_VRAM_DEST) = (uint16_t)((slot_off & 0x00E0) | ((slot_off << 1) & 0xFE00));

        /* $90:A230: look up 3-byte tile pointer in $EE:0000 and skip its 1-byte header */
        y = (uint16_t)(y + 1);
        tile_id = LONG16(desc + y);
        soe_carry(r, (tile_id & 0x8000) != 0);               /* ASL A */
        ptr_off = soe_adc16(r, (uint16_t)(tile_id << 1), LONG16(desc + y));
        DP16(r, DP_DMA_SRC_ADDR + 1) = ROM16(TILE_CATALOG_BASE + 1 + ptr_off);
        DP16(r, DP_DMA_SRC_ADDR) = (uint16_t)(ROM16(TILE_CATALOG_BASE + ptr_off) + 1);

        /* $90:A244: upload both 64-byte halves of the 16x16 4bpp tile */
        r->x = TILE_HALF_BYTES;
        vram_dma_upload_tile_block_bind(r);
        soe_carry(r, false);
        DP16(r, DP_DMA_SRC_ADDR) = soe_adc16(r, DP16(r, DP_DMA_SRC_ADDR), TILE_HALF_BYTES);
        DP16(r, DP_DMA_VRAM_DEST) |= SECOND_HALF_VRAM_BIT;
        r->x = TILE_HALF_BYTES;
        vram_dma_upload_tile_block_bind(r);

        /* $90:A25E: restore channel index; if 10-update budget is spent, tick remaining timers only */
        r->y = y;
        DP8(r, DP_UPDATES_LEFT) = (uint8_t)(DP8(r, DP_UPDATES_LEFT) - 1);
        if (DP8(r, DP_UPDATES_LEFT) == 0) {
            for (;;) {
                SOE_AT(0xA273);
                x = (uint16_t)(x + 1);
                if (ABSI8(r, MAP_ANIM_CYCLES_ADDR, x) == 0) continue;
                SOE_AT(0xA279);
                if (x == ABS16(r, ACTIVE_ANIM_CHANNELS_ADDR)) {
                    SOE_AT(0xA283);
                    break;
                }
                ABSI8(r, MAP_ANIM_TIMERS_ADDR, x) = (uint8_t)(ABSI8(r, MAP_ANIM_TIMERS_ADDR, x) - 1);
            }
            break;
        }
    }

    /* $90:A283: deduct (updates * 12) from the per-frame VRAM budget $00D8, clamping at 0 */
    r->p &= (uint8_t)~(SOE_M | SOE_X);
    updates = (uint16_t)(MAX_UPDATES_PER_FRAME - DP16(r, DP_UPDATES_LEFT));
    DP16(r, DP_UPDATES_LEFT) = updates;
    deduct = (uint16_t)(updates * VRAM_COST_PER_UPDATE);
    soe_carry(r, false);
    a = soe_adc16(r, (uint16_t)((uint16_t)(deduct - 1) ^ 0xFFFF), DP16(r, DP_VRAM_BUDGET));
    if (r->p & SOE_N) {
        SOE_AT(0xA29C);
        a = r->d;                                            /* TDC */
    } else {
        SOE_AT(0xA29D);
    }
    DP16(r, DP_VRAM_BUDGET) = a;
    r->a = a;
    r->x = x;
    r->db = entry_db;
    soe_nz8(r, entry_db);                                    /* PLB */
}
