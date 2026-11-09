/*
 * $8E:E5FC audio_master_volume_fade_tick (knowledge/routines/audio_master_volume_fade_tick.md), RTL, 16-bit A.
 *
 * Moves the brightness/volume byte $0106 (DB-relative) one step toward the target byte $0B83 while
 * $0B83 is negative (a fade is running) and $0106 bit 7 is clear; reaching the target clears the
 * word $0B83. Leaves A and N Z C from the last load or compare.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   audio_master_volume_fade_tick ($8EE5FC) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  24 instructions (54 bytes)
 * - Compiled C ASM:   298 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   12.4x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      2284fc3fadbab755
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"

SOE_BIND(audio_master_volume_fade_tick, 0x8EE5FC, RTL)
{
    uint16_t a;
    uint8_t target;

    SOE_AT(0xE5FC);
    a = ABS16(r, 0x0106);
    soe_nz16(r, a);
    r->a = a;
    if (a & 0x0080) {
        SOE_AT(0xE609);
        r->p &= (uint8_t)~SOE_Z;
        return;
    }
    SOE_AT(0xE604);
    a = ABS16(r, 0x0B83);
    soe_nz16(r, a);
    r->a = a;
    if (!(a & 0x8000)) {
        SOE_AT(0xE609);
        return;
    }
    SOE_AT(0xE60A);
    target = ABS8(r, 0x0B83);
    r->a = (uint16_t)((a & 0xFF00) | target);
    soe_cmp8(r, target, ABS8(r, 0x0106));
    if (!(r->p & SOE_Z)) {
        if (r->p & SOE_N) {
            SOE_AT(0xE616);
            ABS8(r, 0x0106) = (uint8_t)(ABS8(r, 0x0106) - 1);
            soe_cmp8(r, target, ABS8(r, 0x0106));
            if (!(r->p & SOE_Z)) {
                SOE_AT(0xE61E);
                r->p &= ~SOE_M;
                return;
            }
        } else {
            SOE_AT(0xE621);
            ABS8(r, 0x0106) = (uint8_t)(ABS8(r, 0x0106) + 1);
            soe_cmp8(r, target, ABS8(r, 0x0106));
            if (!(r->p & SOE_Z)) {
                SOE_AT(0xE629);
                r->p &= ~SOE_M;
                return;
            }
        }
    }
    SOE_AT(0xE62C);
    r->p &= ~SOE_M;
    ABS16(r, 0x0B83) = 0;
}