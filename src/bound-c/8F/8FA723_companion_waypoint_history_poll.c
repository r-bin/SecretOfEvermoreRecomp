/*
 * $8F:A723 companion_waypoint_history_poll (knowledge/routines/companion_waypoint_history_poll.md), RTL, 16-bit A and X.
 *
 * In: X = a follower record (DB-relative, fields at $3BF9.. of the record base), Y saved and restored.
 * Y' = rec[$3BFF] indexes the breadcrumb list. Arrived (set bits $0030 in rec[$3C12]) when the
 * breadcrumb at Y' ($3BC6/$3BC7) equals the target ($3BF9/$3BFA), or, with rec[$3C12] bit 12
 * (near-enough mode), when both 8-bit differences + 1 are below 3; else bit 4 of rec[$3C12] is cleared.
 * Leaves A = the new rec[$3C12], N Z from the restored Y (PLY), C and V from the last compare/subtract.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   companion_waypoint_history_poll ($8FA723) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  36 instructions (81 bytes)
 * - Compiled C ASM:   508 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   14.1x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      38e3f9112ae8ec97
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"

SOE_BIND(companion_waypoint_history_poll, 0x8FA723, RTL)
{
    uint16_t x = r->x;
    uint16_t y;
    bool arrived;

    SOE_AT(0xA723);
    y = ABSI16(r, 0x3BFF, x);
    if (!(ABSI16(r, 0x3C12, x) & 0x1000)) {
        soe_cmp16(r, ABSI16(r, 0x3BC6, y), ABSI16(r, 0x3BF9, x));
        arrived = (r->p & SOE_Z) != 0;
        if (arrived) {
            SOE_AT(0xA737);
            soe_cmp16(r, ABSI16(r, 0x3BC7, y), ABSI16(r, 0x3BFA, x));
            arrived = (r->p & SOE_Z) != 0;
            if (arrived) SOE_AT(0xA769);
            else SOE_AT(0xA73F);
        } else {
            SOE_AT(0xA73F);
        }
    } else {
        uint8_t d;
        SOE_AT(0xA74E);
        soe_carry(r, true);                                  /* SEP #$21 */
        d = soe_sbc8(r, ABSI8(r, 0x3BC6, y), ABSI8(r, 0x3BF9, x));
        d = (uint8_t)(d + 1);
        soe_cmp8(r, d, 0x03);
        arrived = !(r->p & SOE_C);
        if (arrived) {
            SOE_AT(0xA75B);
            soe_carry(r, true);
            d = soe_sbc8(r, ABSI8(r, 0x3BC7, y), ABSI8(r, 0x3BFA, x));
            d = (uint8_t)(d + 1);
            soe_cmp8(r, d, 0x03);
            arrived = !(r->p & SOE_C);
            if (arrived) SOE_AT(0xA769);
            else SOE_AT(0xA73F);
        } else {
            SOE_AT(0xA74A);
        }
        r->a = (uint16_t)((r->a & 0xFF00) | d);
    }
    {
        uint16_t v = ABSI16(r, 0x3C12, x);
        v = arrived ? (uint16_t)(v | 0x0030) : (uint16_t)(v & 0xFFEF);
        ABSI16(r, 0x3C12, x) = v;
        r->a = v;
    }
    soe_nz16(r, r->y);                                       /* PLY */
}