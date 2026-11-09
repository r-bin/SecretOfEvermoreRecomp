/*
 * $8F:A999 elevation_compass_direction_resolve (knowledge/routines/elevation_compass_direction_resolve.md), RTS, 8-bit accumulator.
 *
 * One of four direction codes from the signs of dx = DP $12 and dy = DP $13:
 * dx < 0: dy < 0 -> $0E, else $0A; dx >= 0: dy < 0 -> $02, else $06.
 * Leaves A low byte = the code (N and Z as LDA #code sets them), C and V as entered.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   elevation_compass_direction_resolve ($8FA999) [RTS]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  57 instructions (119 bytes)
 * - Compiled C ASM:   94 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   1.6x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      09ffa14a3c349f8b
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"

SOE_BIND(elevation_compass_direction_resolve, 0x8FA999, RTS)
{
    uint8_t dx, dy, code;

    SOE_AT(0xA999);
    dx = DP8(r, 0x12);
    if (!(dx & 0x80)) {
        SOE_AT(0xA9A7);
        dy = DP8(r, 0x13);
        if (!(dy & 0x80)) {
            SOE_AT(0xA9AE);
            code = 0x06;
        } else {
            SOE_AT(0xA9AB);
            code = 0x02;
        }
    } else {
        SOE_AT(0xA99D);
        dy = DP8(r, 0x13);
        if (!(dy & 0x80)) {
            SOE_AT(0xA9A4);
            code = 0x0A;
        } else {
            SOE_AT(0xA9A1);
            code = 0x0E;
        }
    }
    soe_set_a8(r, code);
    soe_nz8(r, code);
}