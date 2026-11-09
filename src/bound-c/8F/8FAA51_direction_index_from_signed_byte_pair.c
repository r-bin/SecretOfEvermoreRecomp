/*
 * $8F:AA51 direction_index_from_signed_byte_pair (knowledge/routines/direction_index_from_signed_byte_pair.md), RTL, 8-bit A.
 *
 * In: A = two signed bytes (high byte H, low byte L). Tests the sign/zero of H and L via XBA and
 * returns an even 8-way direction index 0..14 in the low byte of A, leaving the high byte of A as
 * the last XBA left it (|H| when H != 0, |L| when H == 0) and 8-bit N Z from the result byte.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   direction_index_from_signed_byte_pair ($8FAA51) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  32 instructions (49 bytes)
 * - Compiled C ASM:   172 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   5.4x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      74df107cd387c784
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"

SOE_BIND(direction_index_from_signed_byte_pair, 0x8FAA51, RTL)
{
    uint8_t lo = (uint8_t)r->a;
    uint8_t hi = (uint8_t)(r->a >> 8);
    uint8_t out;

    SOE_AT(0xAA51);
    if (hi & 0x80) {
        SOE_AT(0xAA64);
        hi = (uint8_t)(0u - hi);
        if (lo & 0x80) {
            SOE_AT(0xAA6F);
            out = 0x0E;
        } else if (lo == 0) {
            SOE_AT(0xAA71);
            out = 0x00;
        } else {
            out = 0x02;
        }
    } else if (hi == 0) {
        SOE_AT(0xAA72);
        if (lo & 0x80) {
            SOE_AT(0xAA7B);
            hi = (uint8_t)(0u - lo);
            out = 0x0C;
        } else if (lo == 0) {
            SOE_AT(0xAA81);
            hi = 0x00;
            out = 0x00;
        } else {
            hi = lo;
            out = 0x04;
        }
    } else {
        if (lo & 0x80) {
            SOE_AT(0xAA5E);
            out = 0x0A;
        } else if (lo == 0) {
            SOE_AT(0xAA61);
            out = 0x08;
        } else {
            out = 0x06;
        }
    }
    r->a = (uint16_t)((uint16_t)hi << 8 | out);
    soe_nz8(r, out);
}
