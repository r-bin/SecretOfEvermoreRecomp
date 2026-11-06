/*
 * $8F:C773 entity_sprite_depth (knowledge/routines/entity_sprite_depth.md), RTL, 16-bit A and X. Runs on into
 * $8F:C780 sprite_depth_against_the_canopy, which is only reached by that branch (no call recorded).
 *
 * In: Y = an entity record (DB-relative). Builds the sprite's attribute word in DP $06 and its depth
 * in DP $08 (also rec[$84]); DP $02/$04 = the position relative to the camera (DP $59/$5B).
 *  - rec[$10] bit 10 with $23C1 set: priority attributes $CC30 straight away ($C773 only);
 *  - X = rec[$3C] (the metatile under it): gate nibble $8 in bits 11..8 hides it (depth 0);
 *  - its plane (rec[$3C] & $30) against the entity's (rec[$18] & $30): higher -> $CC30, lower ->
 *    $CC20; equal -> $CC30 when bit 12, else $CC20, or $FC20 with gate nibble $4;
 *  - +$0200 when rec[$0C] is 0 (TSB);
 *  - depth = ((plane ^ $30) << 1) + $40 + y-on-screen + carries, + $80; 0 when bit 9.. set (>= $200).
 * With plane 0 and $7E:241F = 2 the position is taken against $010E/$0110 instead (y + 1).
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   entity_sprite_depth ($8FC773) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  78 instructions (177 bytes)
 * - Compiled C ASM:   711 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   9.1x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      640efad2dc0915a7
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"

#ifndef SOE_OPTIMIZE

SOE_BIND(entity_sprite_depth, 0x8FC773, RTL)
{
    uint16_t y = r->y, a, x, attr;

    SOE_AT(0xC773);
    if (ABSI16(r, ENTITY_RECORD_FLAGS_OFF, y) & 0x0400) {
        SOE_AT(0xC77B);
        if (ABS16(r, 0x23C1) != 0) {
            SOE_AT(0xC7C1);
            soe_nz16(r, ABS16(r, 0x23C1));
            attr = 0xCC30;
            goto attributes;
        }
    }
    SOE_AT(0xC780);
    x = ABSI16(r, ENTITY_RECORD_TILE_COLLISION_OFF, y);
    r->x = x;
    a = (uint16_t)(x ^ 0x0800);
    if ((a & 0x0F00) == 0) {                                 /* hidden */
        SOE_AT(0xC78C);
        soe_nz16(r, r->d);                                   /* TDC */
        DP16(r, 0x08) = r->d;
        ABSI16(r, ENTITY_RECORD_SPRITE_DEPTH_OFF, y) = r->d;
        r->a = r->d;
        return;
    }
    SOE_AT(0xC793);
    DP16(r, 0x12) = (uint16_t)(a & 0x0030);
    soe_cmp16(r, (uint16_t)(ABSI16(r, ENTITY_RECORD_PLANE_OFF, y) & 0x0030), DP16(r, 0x12));
    if (r->p & SOE_Z) {
        SOE_AT(0xC7A9);
        if (x & 0x1000) { SOE_AT(0xC7C1); attr = 0xCC30; }
        else {
            SOE_AT(0xC7AF);
            if (((x ^ 0x0400) & 0x0F00) == 0) { SOE_AT(0xC7BC); attr = 0xFC20; }
            else { SOE_AT(0xC7B7); attr = 0xCC20; }
        }
    } else {
        SOE_AT(0xC7A2);
        if (r->p & SOE_N) { SOE_AT(0xC7C1); attr = 0xCC30; }
        else { SOE_AT(0xC7A4); attr = 0xCC20; }
    }
attributes:
    SOE_AT(0xC7C4);
    DP16(r, 0x06) = attr;
    a = ABSI16(r, ENTITY_RECORD_PALETTE_SLOT_OFF, y);
    if (a == 0) { SOE_AT(0xC7CB); a = 0x0200; }
    SOE_AT(0xC7CE);
    r->p = (uint8_t)((r->p & ~SOE_Z) | ((a & DP16(r, 0x06)) == 0 ? SOE_Z : 0));   /* TSB $06 */
    DP16(r, 0x06) = (uint16_t)(DP16(r, 0x06) | a);
    soe_carry(r, true);
    DP16(r, 0x02) = soe_sbc16(r, ABSI16(r, ENTITY_RECORD_POS_X_OFF, y), DP16(r, 0x59));
    soe_carry(r, true);
    DP16(r, 0x04) = soe_sbc16(r, ABSI16(r, ENTITY_RECORD_POS_Y_OFF, y), DP16(r, 0x5B));
    a = (uint16_t)(ABSI16(r, ENTITY_RECORD_PLANE_OFF, y) & 0x0030);
    if (a == 0) {
        SOE_AT(0xC7E8);
        soe_cmp16(r, LONG16(0x7E241FUL), 0x0002);
        if (r->p & SOE_Z) {
            SOE_AT(0xC7F1);
            soe_carry(r, true);
            DP16(r, 0x02) = soe_sbc16(r, ABSI16(r, ENTITY_RECORD_POS_X_OFF, y), ABS16(r, 0x010E));
            soe_carry(r, true);
            DP16(r, 0x04) = (uint16_t)(soe_sbc16(r, ABSI16(r, ENTITY_RECORD_POS_Y_OFF, y), ABS16(r, 0x0110)) + 1);
        }
        SOE_AT(0xC804);
        a = r->d;                                            /* TDC */
    }
    SOE_AT(0xC805);
    a ^= 0x0030;
    soe_carry(r, a & 0x8000);                                /* ASL A */
    a = (uint16_t)(a << 1);
    a = soe_adc16(r, a, 0x0040);
    a = soe_adc16(r, a, DP16(r, 0x04));
    if (a & 0xFE00) {
        SOE_AT(0xC81D);
        a = r->d;                                            /* TDC */
        soe_nz16(r, a);
    } else {
        SOE_AT(0xC813);
        soe_carry(r, false);
        a = soe_adc16(r, a, 0x0080);
    }
    DP16(r, 0x08) = a;
    ABSI16(r, ENTITY_RECORD_SPRITE_DEPTH_OFF, y) = a;
    r->a = a;
}

#else /* SOE_OPTIMIZE: a faster variant of the same behaviour (G1-checked); the code above is the documentation */

#define E(field) BANKI16(bank, ENTITY_RECORD_##field##_OFF, y)

SOE_BIND(entity_sprite_depth, 0x8FC773, RTL)
{
    uint8_t SOE_FAR *bank = SOE_BANK(r);
    uint8_t SOE_FAR *dp = SOE_DIRECT(r);
    uint16_t y = r->y, a, x, attr, plane, s, m;
    uint8_t p = r->p;

    if ((E(FLAGS) & 0x0400) && BANK16(bank, 0x23C1) != 0) {
        attr = 0xCC30;                                       /* LDA $23C1 / BNE: N Z from $23C1 */
        p = soe_p_nz16(p, BANK16(bank, 0x23C1));
        goto attributes;
    }
    x = E(TILE_COLLISION);
    r->x = x;
    a = (uint16_t)(x ^ 0x0800);
    if ((a & 0x0F00) == 0) {                                 /* $C78C: hidden, TDC */
        DPB16(dp, 0x08) = r->d;
        E(SPRITE_DEPTH) = r->d;
        r->a = r->d;
        r->p = soe_p_nz16(p, r->d);
        return;
    }
    DPB16(dp, 0x12) = (uint16_t)(a & 0x0030);
    plane = (uint16_t)(E(PLANE) & 0x0030);
    p = soe_flags_sub16(p, plane, DPB16(dp, 0x12), (uint16_t)(plane - DPB16(dp, 0x12)), 1, true);
    if (p & SOE_Z) {                                         /* $C7A9 */
        if (x & 0x1000) attr = 0xCC30;
        else if (((x ^ 0x0400) & 0x0F00) == 0) attr = 0xFC20;
        else attr = 0xCC20;
    } else {
        attr = (p & SOE_N) ? 0xCC30 : 0xCC20;
    }
attributes:
    a = E(PALETTE_SLOT);
    if (a == 0) a = 0x0200;
    p = (uint8_t)((p & ~SOE_Z) | ((a & attr) == 0 ? SOE_Z : 0));   /* TSB $06 */
    DPB16(dp, 0x06) = (uint16_t)(attr | a);
    s = E(POS_X); m = DPB16(dp, 0x59);
    DPB16(dp, 0x02) = (uint16_t)(s - m);
    s = E(POS_Y); m = DPB16(dp, 0x5B);
    a = (uint16_t)(s - m);
    p = soe_flags_sub16(p, s, m, a, 1, false);
    DPB16(dp, 0x04) = a;
    a = (uint16_t)(E(PLANE) & 0x0030);
    if (a == 0) {
        if (RAM16(0x241F) == 0x0002) {
            uint16_t yy = E(POS_Y);
            DPB16(dp, 0x02) = (uint16_t)(E(POS_X) - BANK16(bank, 0x010E));
            a = (uint16_t)(yy - BANK16(bank, 0x0110));
            p = soe_flags_sub16(p, yy, BANK16(bank, 0x0110), a, 1, false);
            DPB16(dp, 0x04) = (uint16_t)(a + 1);
        } else {
            p = soe_flags_sub16(p, RAM16(0x241F), 2, (uint16_t)(RAM16(0x241F) - 2), 1, true);
        }
        a = r->d;                                            /* TDC */
    }
    a ^= 0x0030;
    {
        uint16_t c = a >> 15;                                /* ASL A */
        uint16_t b = (uint16_t)(a << 1), t, u, w = DPB16(dp, 0x04);
        t = (uint16_t)(b + 0x0040 + c);                      /* ADC #$40 */
        c = c ? t <= b : t < b;
        u = (uint16_t)(t + w + c);                           /* ADC $04 */
        c = c ? u <= t : u < t;
        p = soe_flags_add16(p, t, w, u, c);
        a = u;
    }
    if (a & 0xFE00) {
        a = r->d;                                            /* TDC */
        p = soe_p_nz16(p, a);
    } else {
        uint16_t b = a;
        a = (uint16_t)(b + 0x0080);                          /* CLC / ADC #$80 */
        p = soe_flags_add16(p, b, 0x0080, a, a < b);
    }
    DPB16(dp, 0x08) = a;
    E(SPRITE_DEPTH) = a;
    r->a = a;
    r->p = p;
}

#endif /* SOE_OPTIMIZE */