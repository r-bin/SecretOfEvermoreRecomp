/*
 * $8F:A97D elevation_manhattan_distance_eval (knowledge/routines/elevation_manhattan_distance_eval.md), RTS, 8-bit accumulator.
 *
 * Signed bytes dx = DP $12, dy = DP $13. Signs differ: |dx + dy|; signs match: |dx - dy|, in 8 bits
 * (the sum or difference wraps; a result of $80 stays $80). Leaves A low byte = the result; C and V
 * from the ADC/SBC, N and Z from the last instruction that set them.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   elevation_manhattan_distance_eval ($8FA97D) [RTS]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  15 instructions (28 bytes)
 * - Compiled C ASM:   144 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   9.6x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      9400eb65f6e7e836
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"

SOE_BIND(elevation_manhattan_distance_eval, 0x8FA97D, RTS)
{
    uint8_t dx, dy, a;

    SOE_AT(0xA97D);
    dx = DP8(r, 0x12);
    dy = DP8(r, 0x13);

    if ((dx ^ dy) & 0x80) {
        SOE_AT(0xA983);
        soe_carry(r, false);
        a = soe_adc8(r, dx, dy);
        if (a & 0x80) {
            SOE_AT(0xA98A);
            a = (uint8_t)(0 - a);
            soe_nz8(r, a);
        } else {
            SOE_AT(0xA98D);
        }
    } else {
        SOE_AT(0xA98E);
        soe_carry(r, true);
        a = soe_sbc8(r, dx, dy);
        if (a & 0x80) {
            SOE_AT(0xA995);
            a = (uint8_t)(0 - a);
            soe_nz8(r, a);
        } else {
            SOE_AT(0xA98D);
        }
    }
    soe_set_a8(r, a);
}