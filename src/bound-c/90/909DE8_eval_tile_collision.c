/*
 * $90:9DE8 eval_tile_collision (knowledge/routines/eval_tile_collision.md), RTL, 16-bit A and X.
 *
 * In: DP $50 = the collision word of a metatile, DP $4C = the moving entity's record, DP $44 = its
 * plane word. Out: A = the geometry code, $0F = solid.
 *  1. Gate bit 8: bits 11..8 pick who may pass: $3 (^$300 = 0) only records below the Boy ($4E89),
 *     $5 only the Dog ($4F37) is blocked, $7 records from the Boy on are blocked; others no gate.
 *  2. Bit 6 set (plane-transparent): the plane bits 5..4 must equal the entity's, else open (A = D).
 *  3. Else the plane must match: d = (word - plane) & $30 == 0. Two exceptions still pass to the
 *     geometry: d = $10 when the word has bit 13, d = $30 when the entity's plane word has bit 6.
 *  4. Geometry: bit 13 set -> the word's low nibble through the table at $90:9E73 (X = nibble * 2),
 *     else the low nibble itself.
 * Flags as the asm leaves them: C and V from the last CMP/SBC on the path (or as entered), N Z
 * from the last load/AND; X changes only on the table path.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   eval_tile_collision ($909DE8) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  62 instructions (139 bytes)
 * - Compiled C ASM:   309 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   5.0x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      c3171d4518d78e72
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"

#ifndef SOE_OPTIMIZE

#define SOLID 0x000F
#define GEOMETRY_TABLE 0x909E73UL
#define BOY_ENTITY_RECORD_ADDR 0x4E89
#define DOG_ENTITY_RECORD_ADDR 0x4F37

static void ret_a(soe_regs r, uint16_t a)
{
    r->a = a;
    soe_nz16(r, a);
}

/* $9E2F: the geometry of the word */
static void geometry(soe_regs r, uint16_t word)
{
    uint16_t nibble = word & 0x000F;

    if (word & 0x2000) {
        SOE_AT(0x9E3A);
        r->x = (uint16_t)(nibble << 1);       /* ASL A clears C (bit 15 of a nibble), TAX */
        soe_carry(r, false);
        ret_a(r, ROM16(GEOMETRY_TABLE + r->x));
    } else {
        ret_a(r, nibble);
    }
}

SOE_BIND(eval_tile_collision, 0x909DE8, RTL)
{
    uint16_t word = DP16(r, 0x50);
    uint16_t a = word;
    uint16_t plane = DP16(r, 0x44);

    SOE_AT(0x9DE8);
    if (word & 0x0100) {
        bool blocked = false, gated = true;
        uint16_t actor = DP16(r, 0x4C);

        SOE_AT(0x9DEF);
        if (((a ^= 0x0300) & 0x0F00) == 0) {
            SOE_AT(0x9E19);
            soe_cmp16(r, actor, 0x4E89);
            blocked = !(r->p & SOE_C);        /* BCC: records below the Boy */
        } else if (((a ^= 0x0600) & 0x0F00) == 0) {
            SOE_AT(0x9E10);
            soe_cmp16(r, actor, 0x4F37);
            blocked = (r->p & SOE_Z) != 0;    /* BEQ: the Dog */
        } else if (((a ^= 0x0200) & 0x0F00) == 0) {
            SOE_AT(0x9E07);
            soe_cmp16(r, actor, 0x4E89);
            blocked = (r->p & SOE_C) != 0;    /* BCS: the Boy and later records */
        } else {
            gated = false;                    /* no gate: on to $9E22 with the XORed word */
        }
        if (blocked) { SOE_AT(0x9E64); ret_a(r, SOLID); return; }
        if (gated) { SOE_AT(0x9E20); a = word; }   /* LDA $50 */
    }
    SOE_AT(0x9E22);
    if (a & 0x0040) {
        SOE_AT(0x9E44);
        if (((a ^ plane) & 0x0030) != 0) { SOE_AT(0x9E51); ret_a(r, r->d); return; }   /* TDC */
        ret_a(r, word & 0x000F);
        return;
    }
    soe_carry(r, true);
    a = (uint16_t)(soe_sbc16(r, a, plane) & 0x0030);
    if (a == 0) { geometry(r, word); return; }
    SOE_AT(0x9E53);                           /* the planes differ */
    if (!(a & 0x0020)) {
        SOE_AT(0x9E68);                      /* difference $10: only with bit 13, through the table ($9E68) */
        if (word & 0x2000) geometry(r, word);
        else ret_a(r, SOLID);
        return;
    }
    if (a & 0x0010) {
        SOE_AT(0x9E5D);
        if (plane & 0x0040) { SOE_AT(0x9E2F); geometry(r, word); return; }
    }
    SOE_AT(0x9E64);   /* $30 and entity bit 6 */
    ret_a(r, SOLID);
}

#else /* SOE_OPTIMIZE: a faster variant of the same behaviour (G1-checked); the code above is the documentation */

#define SOLID 0x000F
#define GEOMETRY_TABLE 0x909E73UL
#define BOY_ENTITY_RECORD_ADDR 0x4E89
#define DOG_ENTITY_RECORD_ADDR 0x4F37

SOE_BIND(eval_tile_collision, 0x909DE8, RTL)
{
    uint8_t SOE_FAR *dp = SOE_DIRECT(r);
    uint16_t word = DPB16(dp, 0x50), plane = DPB16(dp, 0x44);
    uint16_t a = word, d, x = r->x;
    uint8_t p = r->p;
    bool table = false;

    if (word & 0x0100) {
        uint16_t actor = DPB16(dp, 0x4C);
        int8_t gate = -1;                                    /* 1: blocked, 0: passes, -1: no gate */

        /* CMP leaves N Z C: only C is visible at the exit ($0F loads N Z) */
        if (((a ^= 0x0300) & 0x0F00) == 0) {
            p = (uint8_t)((p & ~SOE_C) | (actor >= BOY_ENTITY_RECORD_ADDR ? SOE_C : 0));
            gate = actor < BOY_ENTITY_RECORD_ADDR;
        } else if (((a ^= 0x0600) & 0x0F00) == 0) {
            p = (uint8_t)((p & ~SOE_C) | (actor >= DOG_ENTITY_RECORD_ADDR ? SOE_C : 0));
            gate = actor == DOG_ENTITY_RECORD_ADDR;
        } else if (((a ^= 0x0200) & 0x0F00) == 0) {
            p = (uint8_t)((p & ~SOE_C) | (actor >= BOY_ENTITY_RECORD_ADDR ? SOE_C : 0));
            gate = actor >= BOY_ENTITY_RECORD_ADDR;
        }
        if (gate == 1) { a = SOLID; goto out; }
        if (gate == 0) a = word;                             /* $9E20 LDA $50 */
    }
    if (a & 0x0040) {                                        /* $9E44 */
        a = ((a ^ plane) & 0x0030) ? r->d : (uint16_t)(word & 0x000F);
        goto out;
    }
    /* $9E27: SEC / SBC $44 / AND #$30 */
    d = (uint16_t)(a - plane);
    p = (uint8_t)(p & ~(SOE_V | SOE_C));
    if (a >= plane) p |= SOE_C;
    if (((a ^ plane) & (a ^ d)) & 0x8000) p |= SOE_V;
    d &= 0x0030;
    if (d == 0 || (!(d & 0x0020) && (word & 0x2000)) || ((d & 0x0010) && (d & 0x0020) && (plane & 0x0040))) {
        /* the geometry ($9E2F, or $9E68 straight to the table) */
        a = word & 0x000F;
        table = (word & 0x2000) != 0;
    } else {
        a = SOLID;
    }
    if (table) {
        x = (uint16_t)(a << 1);                              /* ASL A clears C, TAX */
        p = (uint8_t)(p & ~SOE_C);
        a = ROM16(GEOMETRY_TABLE + x);
    }
out:
    r->a = a;
    r->x = x;
    r->p = soe_p_nz16(p, a);
}

#endif /* SOE_OPTIMIZE */