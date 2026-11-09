/*
 * $8F:A963 elevation_vector_shear_step (knowledge/routines/elevation_vector_shear_step.md), RTS, 8-bit accumulator.
 *
 * min(|dx|, |dy|) of the signed bytes at DP $12 (dx) and DP $13 (dy); |dx| is left in DP $14.
 * The comparison is CMP's N flag (the sign of |dy| - |dx|), so it is signed: |-128| = $80 counts as
 * negative. Leaves A low byte = the result, C from the CMP, V as entered.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   elevation_vector_shear_step ($8FA963) [RTS]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  15 instructions (26 bytes)
 * - Compiled C ASM:   172 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   11.5x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      6110912c3506a9c0
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"

SOE_BIND(elevation_vector_shear_step, 0x8FA963, RTS)
{
    uint8_t ax, ay;

    SOE_AT(0xA963);
    ax = DP8(r, 0x12);
    if (ax & 0x80) {
        SOE_AT(0xA967);
        ax = (uint8_t)(0 - ax);
    }
    SOE_AT(0xA96A);
    DP8(r, 0x14) = ax;

    ay = DP8(r, 0x13);
    if (ay & 0x80) {
        SOE_AT(0xA970);
        ay = (uint8_t)(0 - ay);
    }
    SOE_AT(0xA973);

    soe_cmp8(r, ay, ax);
    if (r->p & SOE_N) {
        SOE_AT(0xA97A);
        /* BIT #$FF: Z from A, N and V stay as the CMP left them */
        soe_set_a8(r, ay);
        r->p = (uint8_t)((r->p & ~SOE_Z) | (ay == 0 ? SOE_Z : 0));
    } else {
        SOE_AT(0xA977);
        soe_set_a8(r, ax);
        soe_nz8(r, ax);
    }
}