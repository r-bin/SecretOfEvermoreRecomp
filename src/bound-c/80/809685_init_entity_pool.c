/*
 * $80:9685 init_entity_pool (knowledge/routines/init_entity_pool.md), RTL, 16-bit A and X.
 *
 * Links the 8-byte nodes $7E:50AF..$7E:5127 behind the head $7E:50A7 (head +4 = $FFFE): for each
 * node, the one before it gets +2 = the node and +6 = itself, the node gets +0 = the one before
 * and +4 = $FFFF. $7E:1276 = $512F (the end). DB is $7E during the work and restored. Leaves
 * A = $512F, X = Y = $5127, C set (CMP equal), V from the last ADC, N Z from the restored DB (PLB).
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   init_entity_pool ($809685) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  25 instructions (54 bytes)
 * - Compiled C ASM:   208 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   8.3x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      90cef9fcba2a72c0
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"

SOE_BIND(init_entity_pool, 0x809685, RTL)
{
    uint16_t x = 0x50A7, y;
    uint16_t a = 0x50AF;

    RAM16(x + 4) = 0xFFFE;
    soe_carry(r, false);
    do {
        y = a;
        RAM16(x + 2) = a;
        RAM16(x + 6) = x;
        RAM16(y + 0) = x;
        RAM16(y + 4) = 0xFFFF;
        x = y;
        a = soe_adc16(r, y, 0x0008);
        soe_cmp16(r, a, 0x512F);
    } while (a != 0x512F);
    RAM16(0x1276) = 0x512F;
    r->a = 0x512F;
    soe_nz8(r, r->db);                                       /* PLB */
    r->x = x;
    r->y = y;
}