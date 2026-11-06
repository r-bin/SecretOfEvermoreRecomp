/*
 * $8F:C6C5 sprite_sorting_canopy_occlusion_and_render_depth (knowledge/routines/sprite_sorting_canopy_occlusion_and_render_depth.md), RTS, 16-bit A and X.
 *
 * In: DP $4C = entity record offset in DB ($7E). Calls $8F:C773 entity_sprite_depth to compute
 * screen-relative X/Y (DP $02/$04), OAM attribute flags (DP $06) and depth key (DP $08); returns
 * immediately when depth is 0 (hidden/off-screen). Otherwise emits up to two 15-byte sprite sort
 * nodes at $1276 via $80:960A oam_depth_sorted_sprite_insert_node:
 *  - secondary/shadow sprite (rec+$09..$0B) at depth DP $08 - 10 when rec+$0A != 0 and rec+$12 bit 14 is clear;
 *  - primary sprite (rec+$06..$08) at depth DP $08, shifted up by (rec+$1E >> 4) when height rec+$1E > 0
 *    (with priority forced to 3 in DP $06 when rec+$1E >= $23C5), skipped if rec+$07 == 0, rec+$12 bit 15 is set,
 *    or rec+$1E < 0.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   sprite_sorting_canopy_occlusion_and_render_depth ($8FC6C5) [RTS]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  62 instructions (150 bytes)
 * - Compiled C ASM:   781 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   12.6x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      e1caeea19921d226
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"

void entity_sprite_depth_bind(soe_regs r);
void oam_depth_sorted_sprite_insert_node_bind(soe_regs r);

SOE_BIND(sprite_sorting_canopy_occlusion_and_render_depth, 0x8FC6C5, RTS)
{
    uint16_t x, y, a;

    SOE_AT(0xC6C5);
    r->y = DP16(r, 0x4C);
    entity_sprite_depth_bind(r);
    if (r->p & SOE_Z) {
        SOE_AT(0xC6C4);
        return;
    }
    SOE_AT(0xC6CD);
    y = ABS16(r, SPRITE_FREE_LIST_HEAD_ADDR);
    x = DP16(r, 0x4C);
    a = ABSI16(r, 0x000A, x);
    if (a != 0) {
        SOE_AT(0xC6D7);
        ABSI16(r, 0x000D, y) = a;
        a = ABSI16(r, ENTITY_RECORD_STATE_OFF, x);
        soe_bit_imm(r, a, 0x4000);
        if (!(a & 0x4000)) {
            SOE_AT(0xC6E2);
            ABSI16(r, 0x000C, y) = ABSI16(r, 0x0009, x);
            ABSI16(r, 0x0006, y) = DP16(r, 0x02);
            ABSI16(r, 0x0008, y) = DP16(r, 0x04);
            ABSI16(r, 0x000A, y) = DP16(r, 0x06);
            soe_carry(r, true);
            ABSI16(r, 0x0004, y) = soe_sbc16(r, DP16(r, 0x08), 0x000A);
            r->x = x;
            r->y = y;
            oam_depth_sorted_sprite_insert_node_bind(r);
            SOE_AT(0xC704);
            y = ABS16(r, SPRITE_FREE_LIST_HEAD_ADDR);
            x = DP16(r, 0x4C);
        }
    }
    SOE_AT(0xC709);
    a = ABSI16(r, 0x0007, x);
    r->a = a;
    r->x = x;
    r->y = y;
    soe_nz16(r, a);
    if (a == 0) {
        SOE_AT(0xC759);
        return;
    }
    SOE_AT(0xC70E);
    ABSI16(r, 0x000D, y) = a;
    a = ABSI16(r, ENTITY_RECORD_STATE_OFF, x);
    r->a = a;
    soe_nz16(r, a);
    soe_bit_imm(r, a, 0x8000);
    if (a & 0x8000) {
        SOE_AT(0xC759);
        return;
    }
    SOE_AT(0xC719);
    ABSI16(r, 0x000C, y) = ABSI16(r, ENTITY_RECORD_SPRITE_OFF, x);
    a = ABSI16(r, ENTITY_RECORD_HEIGHT_OFF, x);
    r->a = a;
    soe_nz16(r, a);
    if (a != 0) {
        SOE_AT(0xC724);
        if (a & 0x8000) {
            SOE_AT(0xC759);
            return;
        }
        SOE_AT(0xC726);
        soe_cmp16(r, a, ABS16(r, COMPANION_LEASH_RADIUS_ADDR));
        if (!(r->p & SOE_N)) {
            SOE_AT(0xC72B);
            DP16(r, 0x06) = (uint16_t)((DP16(r, 0x06) & 0xCFFF) | 0x0030);
            a = ABSI16(r, ENTITY_RECORD_HEIGHT_OFF, x);
        }
        SOE_AT(0xC738);
        a = (uint16_t)((uint16_t)((a >> 4) - 1) ^ 0xFFFF);
    }
    SOE_AT(0xC740);
    soe_carry(r, false);
    ABSI16(r, 0x0008, y) = soe_adc16(r, a, DP16(r, 0x04));
    ABSI16(r, 0x0006, y) = DP16(r, 0x02);
    ABSI16(r, 0x000A, y) = DP16(r, 0x06);
    ABSI16(r, 0x0004, y) = DP16(r, 0x08);
    r->x = x;
    r->y = y;
    oam_depth_sorted_sprite_insert_node_bind(r);
    SOE_AT(0xC759);
}
