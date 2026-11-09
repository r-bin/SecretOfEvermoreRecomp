/*
 * $90:8124 anim_start_record (knowledge/routines/anim_start_record.md), RTL, 16-bit A and X.
 *
 * In: X = an animation's offset into the $C4 animation table, Y = an entity record (DB-relative).
 * An entry ($C4:0000+X) is a word and a byte. Bit 15 of the word after it ($C4:0002+X) asks for
 * two record steps (X += 2 * rec[$22]), bit 14 for one from the table at $90:815B indexed by
 * rec[$22]. If the record already plays this animation (word at rec+3, byte at rec+2) nothing
 * changes; else the record restarts it: rec+3 = rec+0 = word, rec+$0E = D, rec+2 = byte, rec+5 = 1.
 * Leaves X = the final entry offset, A and N V Z C as the asm does (see the paths below).
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   anim_start_record ($908124) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  36 instructions (86 bytes)
 * - Compiled C ASM:   440 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   12.2x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      c8a907dfdfa79659
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"

#define ANIM_TABLE 0xC40000UL
#define FACING_STEP 0x90815BUL

SOE_BIND(anim_start_record, 0x908124, RTL)
{
    uint16_t x = r->x, y = r->y;
    uint16_t a;
    uint16_t word;
    uint8_t byte;

    SOE_AT(0x8124);
    a = ROM16(ANIM_TABLE + 2 + x);
    if (a & 0x8000) {                                        /* $8150 */
        uint16_t step;
        SOE_AT(0x8150);
        step = ABSI16(r, ENTITY_RECORD_FACING_OFF, y);
        soe_carry(r, false);
        a = soe_adc16(r, x, step);
        a = soe_adc16(r, a, step);
        x = a;
    } else if (a & 0x4000) {                                 /* $812F */
        uint16_t i;
        SOE_AT(0x812F);
        i = ABSI16(r, ENTITY_RECORD_FACING_OFF, y);
        soe_carry(r, false);
        x = soe_adc16(r, x, ROM16(FACING_STEP + i));
    }
    SOE_AT(0x8139);
    r->x = x;
    word = ROM16(ANIM_TABLE + x);
    soe_cmp16(r, word, ABSI16(r, ENTITY_RECORD_ANIM_RESTART_OFF, y));
    if (r->p & SOE_Z) {
        SOE_AT(0x8142);
        byte = ROM8(ANIM_TABLE + 2 + x);
        soe_cmp8(r, byte, ABSI8(r, ENTITY_RECORD_ANIM_PTR_OFF + 2, y));
        if (r->p & SOE_Z) {
            SOE_AT(0x814F);
            r->a = (uint16_t)((word & 0xFF00) | byte);       /* LDA in 8-bit keeps the word's high byte */
            r->p &= ~SOE_M;
            return;
        }
        SOE_AT(0x817B);
    } else {
        SOE_AT(0x817F);
    }
    /* $817B / $817F: restart */
    ABSI16(r, ENTITY_RECORD_ANIM_RESTART_OFF, y) = word;
    ABSI16(r, ENTITY_RECORD_ANIM_PTR_OFF, y) = word;
    ABSI16(r, 0x000E, y) = r->d;                             /* TDC; +$0E: meaning not known yet */
    ABSI8(r, ENTITY_RECORD_ANIM_PTR_OFF + 2, y) = ROM8(ANIM_TABLE + 2 + x);
    ABSI8(r, ENTITY_RECORD_ANIM_TIMER_OFF, y) = 1;
    r->a = (uint16_t)((r->d & 0xFF00) | 0x01);
    soe_nz8(r, 0x01);
    r->p &= ~SOE_M;
}