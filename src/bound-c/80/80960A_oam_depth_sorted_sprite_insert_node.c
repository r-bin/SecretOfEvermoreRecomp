/*
 * $80:960A oam_depth_sorted_sprite_insert_node (knowledge/routines/oam_depth_sorted_sprite_insert_node.md), RTL, 16-bit A and X.
 *
 * Inserts the 15-byte sprite node at $7E:1276 (saved in DP $14) into the doubly-linked depth-sorted
 * sprite list ($7E:50A7..; +0 = prev, +2 = next, +4 = depth key) and advances $7E:1276 by $000F.
 * Bucket finger at $7E:50AD + ((key & $03C0) >> 3) gives the starting node Y and is updated to the
 * new node; walks forward (+2) when key >= Y[+4] or backward (+0) when key < Y[+4]. DB is $7E during
 * the work and restored on exit. Leaves A = X = prev/next neighbor, Y = inserted node, C V from ADC #$000F,
 * N Z from the restored DB (PLB).
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   oam_depth_sorted_sprite_insert_node ($80960A) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  57 instructions (123 bytes)
 * - Compiled C ASM:   635 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   11.1x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      8a9b99e0e4dfee28
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"

SOE_BIND(oam_depth_sorted_sprite_insert_node, 0x80960A, RTL)
{
    uint8_t saved_db = r->db;
    uint16_t node, key, x, y;

    SOE_AT(0x960A);
    r->db = 0x7E;
    y = ABS16(r, SPRITE_FREE_LIST_HEAD_ADDR);
    DP16(r, 0x14) = y;
    key = ABSI16(r, 0x0004, y);
    DP16(r, 0x12) = key;
    x = (uint16_t)((key & 0x03C0) >> 3);
    node = y;
    y = ABSI16(r, 0x50AD, x);
    ABSI16(r, 0x50AD, x) = node;
    soe_cmp16(r, key, ABSI16(r, 0x0004, y));
    if (!(r->p & SOE_N)) {
        for (;;) {
            SOE_AT(0x962F);
            x = ABSI16(r, 0x0002, y);
            soe_cmp16(r, key, ABSI16(r, 0x0004, x));
            if (!(r->p & SOE_C)) goto link_after_y;
            SOE_AT(0x9637);
            y = ABSI16(r, 0x0002, x);
            soe_cmp16(r, key, ABSI16(r, 0x0004, y));
            if (!(r->p & SOE_C)) break;
        }
link_after_x:
        SOE_AT(0x963F);
        node = DP16(r, 0x14);
        ABSI16(r, 0x0000, y) = node;
        ABSI16(r, 0x0002, x) = node;
        soe_carry(r, false);
        ABS16(r, SPRITE_FREE_LIST_HEAD_ADDR) = soe_adc16(r, node, 0x000F);
        ABSI16(r, 0x0002, node) = y;
        ABSI16(r, 0x0000, node) = x;
    } else {
        for (;;) {
            SOE_AT(0x965A);
            x = ABSI16(r, 0x0000, y);
            soe_cmp16(r, key, ABSI16(r, 0x0004, x));
            if (!(r->p & SOE_N)) goto link_after_x;
            SOE_AT(0x9662);
            y = ABSI16(r, 0x0000, x);
            soe_cmp16(r, key, ABSI16(r, 0x0004, y));
            if (!(r->p & SOE_N)) break;
        }
link_after_y:
        SOE_AT(0x966A);
        node = DP16(r, 0x14);
        ABSI16(r, 0x0002, y) = node;
        ABSI16(r, 0x0000, x) = node;
        soe_carry(r, false);
        ABS16(r, SPRITE_FREE_LIST_HEAD_ADDR) = soe_adc16(r, node, 0x000F);
        ABSI16(r, 0x0000, node) = y;
        ABSI16(r, 0x0002, node) = x;
    }
    r->a = x;
    r->x = x;
    r->y = node;
    r->db = saved_db;
    soe_nz8(r, saved_db);
}
