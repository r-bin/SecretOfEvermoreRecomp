/*
 * $8F:9F05 companion_metatile_raycast_line_stepper (knowledge/routines/companion_metatile_raycast_line_stepper.md), RTS.
 *
 * In: A low = the direction (an even index, 0..$E, into the table at $8F:A3D8), A high = the steps to walk. Walks that many
 * metatiles from the cell ($66, $67) in that direction on plane $68, and leaves the cell it reached there. C set when
 * every step could be taken, C clear at the first that could not; C set with no steps. A = the direction when it walked.
 *
 * Each step goes through the tile tables by the class in $52: $A3F8 says which ways the class can be left, $A408 and $A418
 * which ways the next tile can be entered, $A428..$A448 the same for the cells a diagonal crosses. The direction's handler
 * asks eval_tile_collision about the cells on the way; $08 gets the way the step went (0..$B).
 *
 * The direction is an even index up to $E: the table holds eight, and the asm jumps through whatever is next for any
 * other, which no caller passes and this C does not describe (the recomp's glue runs the ROM bytes for those).
 * SOE_FUZZ_A(0x00F1, 0x0000)
 *
 * The asm keeps the direction and the steps left on the stack ($03,s and $01,s). Here they are a RaycastLoop, and the walk is
 * split at $9F43 into raycast_start and raycast_step, so the recomp's glue can stop between two steps.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   companion_metatile_raycast_line_stepper ($8F9F05) [RTS]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  33 instructions (68 bytes)
 * - Compiled C ASM:   42 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   1.3x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      9065a514ce1a5670
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"
#include "raycast.h"

#define LEAVE_CLASS   0x8FA3F8UL     /* ways a tile class can be left */
#define ENTER_STRAIGHT 0x8FA408UL
#define ENTER_NS      0x8FA418UL
#define DIAG_FIRST    0x8FA428UL
#define DIAG_SECOND   0x8FA438UL
#define DIAG_THIRD    0x8FA448UL
#define ROW_UP        0x7F0000UL     /* a cell's metatile, by its offset */
#define NEXT_CELL     0x7F0002UL
#define PREV_CELL     0x7EFFFEUL
#define TILE_WORD     0x7F0004UL

void eval_tile_collision_bind(soe_regs r);
void update_entity_plane_bind(soe_regs r);

/* LDA map,x / TAX / LDA $7F0004,x / STA $50 */
static uint16_t tile_at(soe_regs r, uint32_t map, uint16_t offset)
{
    uint16_t cell = LONG16(map + offset);
    uint16_t word;

    r->x = cell;
    word = LONG16(TILE_WORD + cell);
    DP16(r, 0x50) = word;
    return word;
}

/* JSL $909DE8 / TAX: the class the tile gives. */
static uint16_t tile_class(soe_regs r)
{
    eval_tile_collision_bind(r);
    r->x = r->a;
    return r->a;
}

static uint16_t row_step(soe_regs r, bool up)
{
    uint16_t offset;

    soe_carry(r, up);
    offset = up ? soe_sbc16(r, DP16(r, 0x56), ABS16(r, 0x0F46)) : soe_adc16(r, DP16(r, 0x56), ABS16(r, 0x0F46));
    DP16(r, 0x04) = offset;
    return offset;
}

/* $A3D4 */
static bool blocked(soe_regs r, RaycastLoop SOE_FAR *l, uint16_t at)
{
    SOE_AT(at);
    SOE_AT(0xA3D4);
    r->a = l->direction;
    soe_nz16(r, r->a);
    soe_carry(r, false);
    return false;
}

/* $A3B1: the step is taken. */
static bool stepped(soe_regs r, RaycastLoop SOE_FAR *l, uint16_t way)
{
    uint16_t a;

    SOE_AT(0xA3B1);
    DP16(r, 0x08) = way;
    DP16(r, 0x52) = DP16(r, 0x54);
    a = DP16(r, 0x06);
    DP16(r, 0x50) = a;
    r->a = a;
    update_entity_plane_bind(r);
    a = DP8(r, 0x44);
    DP8(r, 0x68) = (uint8_t)a;
    a = (uint16_t)(l->steps - 1);
    if (a == 0) {
        SOE_AT(0xA3CF);
        r->a = l->direction;
        soe_nz16(r, r->a);
        soe_carry(r, true);
        return false;
    }
    l->steps = a;
    r->a = a;
    soe_nz16(r, a);
    SOE_AT(0x9F43);
    return true;
}

/* $9F49 up and $A022 down: straight on, or past a corner on either side. */
static bool walk_vertical(soe_regs r, RaycastLoop SOE_FAR *l, uint16_t b, bool up, uint16_t leave, uint16_t straight,
                          uint16_t left, uint16_t left_leave, uint16_t right, uint16_t right_leave,
                          uint16_t way, uint16_t way_left, uint16_t way_right)
{
    uint16_t a, offset, word;
    int16_t dy = up ? -1 : 1;

    SOE_AT(b);
    r->x = DP16(r, 0x52);
    a = (uint16_t)(ROM16(LEAVE_CLASS + r->x) & leave);
    if (a == 0) return blocked(r, l, (uint16_t)(b + 0x43));
    SOE_AT((uint16_t)(b + 0x0B));
    DP16(r, 0x02) = a;
    offset = row_step(r, up);
    word = tile_at(r, ROW_UP, offset);
    DP16(r, 0x06) = word;
    a = tile_class(r);
    DP16(r, 0x54) = a;
    a = (uint16_t)(ROM16(ENTER_NS + a) & DP16(r, 0x02));
    if (a == 0) return blocked(r, l, (uint16_t)(b + 0x43));
    SOE_AT((uint16_t)(b + 0x32));
    if (a & straight) {
        DP16(r, 0x67) = (uint16_t)(DP16(r, 0x67) + dy);
        DP16(r, 0x56) = DP16(r, 0x04);
        return stepped(r, l, way);
    }
    SOE_AT((uint16_t)(b + 0x46));
    if (a & left) {
        SOE_AT((uint16_t)(b + 0x4B));
        tile_at(r, PREV_CELL, DP16(r, 0x56));
        a = tile_class(r);
        a = (uint16_t)(ROM16(LEAVE_CLASS + a) & left_leave);
        if (a != 0) {
            SOE_AT((uint16_t)(b + 0x66));
            DP16(r, 0x02) = a;
            tile_at(r, PREV_CELL, DP16(r, 0x04));
            a = tile_class(r);
            a = (uint16_t)(ROM16(ENTER_NS + a) & DP16(r, 0x02));
            if (a != 0) {
                SOE_AT((uint16_t)(b + 0x82));
                DP16(r, 0x67) = (uint16_t)(DP16(r, 0x67) + dy);
                DP16(r, 0x56) = DP16(r, 0x04);
                return stepped(r, l, way_left);
            }
        }
    }
    SOE_AT((uint16_t)(b + 0x8E));
    if ((a & right) == 0) return blocked(r, l, (uint16_t)(b + 0xD6));
    SOE_AT((uint16_t)(b + 0x93));
    tile_at(r, NEXT_CELL, DP16(r, 0x56));
    a = tile_class(r);
    a = (uint16_t)(ROM16(LEAVE_CLASS + a) & right_leave);
    if (a == 0) return blocked(r, l, (uint16_t)(b + 0xD6));
    SOE_AT((uint16_t)(b + 0xAE));
    DP16(r, 0x02) = a;
    tile_at(r, NEXT_CELL, DP16(r, 0x04));
    a = tile_class(r);
    a = (uint16_t)(ROM16(ENTER_NS + a) & DP16(r, 0x02));
    if (a == 0) return blocked(r, l, (uint16_t)(b + 0xD6));
    SOE_AT((uint16_t)(b + 0xCA));
    DP16(r, 0x67) = (uint16_t)(DP16(r, 0x67) + dy);
    DP16(r, 0x56) = DP16(r, 0x04);
    return stepped(r, l, way_right);
}

/* $A0FB right and $A162 left: one cell, three ways to enter it. */
static bool walk_horizontal(soe_regs r, RaycastLoop SOE_FAR *l, uint16_t b, bool right, uint16_t leave,
                            uint16_t bit1, uint16_t way1, uint16_t bit2, uint16_t way2, uint16_t bit3, uint16_t way3)
{
    uint16_t a, word, way;
    int16_t dx = right ? 1 : -1;

    SOE_AT(b);
    r->x = DP16(r, 0x52);
    a = (uint16_t)(ROM16(LEAVE_CLASS + r->x) & leave);
    if (a == 0) return blocked(r, l, (uint16_t)(b + 0x64));
    SOE_AT((uint16_t)(b + 0x0B));
    DP16(r, 0x02) = a;
    word = tile_at(r, right ? NEXT_CELL : PREV_CELL, DP16(r, 0x56));
    DP16(r, 0x06) = word;
    a = tile_class(r);
    DP16(r, 0x54) = a;
    a = (uint16_t)(ROM16(ENTER_STRAIGHT + a) & DP16(r, 0x02));
    if (a == 0) return blocked(r, l, (uint16_t)(b + 0x64));
    SOE_AT((uint16_t)(b + 0x2B));
    if (a & bit1) way = way1;
    else {
        SOE_AT((uint16_t)(b + 0x3E));
        if (a & bit2) way = way2;
        else {
            SOE_AT((uint16_t)(b + 0x51));
            if ((a & bit3) == 0) return blocked(r, l, (uint16_t)(b + 0x64));
            way = way3;
        }
    }
    DP16(r, 0x56) = (uint16_t)(DP16(r, 0x56) + dx * 2);
    DP16(r, 0x66) = (uint16_t)(DP16(r, 0x66) + dx);
    return stepped(r, l, way);
}

/* $A1C9, $A243, $A2BD, $A337: a diagonal, through both cells beside it. */
static bool walk_diagonal(soe_regs r, RaycastLoop SOE_FAR *l, uint16_t b, bool right, bool up, uint16_t bit, uint16_t way)
{
    uint32_t side = right ? NEXT_CELL : PREV_CELL;
    uint16_t word, offset, class_;
    int16_t dx = right ? 1 : -1, dy = up ? -1 : 1;

    SOE_AT(b);
    r->x = DP16(r, 0x52);
    r->a = ROM16(LEAVE_CLASS + r->x);
    if ((r->a & bit) == 0) return blocked(r, l, (uint16_t)(b + 0x77));
    SOE_AT((uint16_t)(b + 0x0B));
    tile_at(r, side, DP16(r, 0x56));
    class_ = tile_class(r);
    r->a = ROM16(DIAG_FIRST + class_);
    if ((r->a & bit) == 0) return blocked(r, l, (uint16_t)(b + 0x77));
    SOE_AT((uint16_t)(b + 0x26));
    offset = row_step(r, up);
    tile_at(r, ROW_UP, offset);
    class_ = tile_class(r);
    r->a = ROM16(DIAG_SECOND + class_);
    if ((r->a & bit) == 0) return blocked(r, l, (uint16_t)(b + 0x77));
    SOE_AT((uint16_t)(b + 0x48));
    word = tile_at(r, side, DP16(r, 0x04));
    DP16(r, 0x06) = word;
    {
        uint16_t a = tile_class(r);
        DP16(r, 0x54) = a;
        r->a = ROM16(DIAG_THIRD + a);
    }
    if ((r->a & bit) == 0) return blocked(r, l, (uint16_t)(b + 0x77));
    SOE_AT((uint16_t)(b + 0x67));
    DP16(r, 0x66) = (uint16_t)(DP16(r, 0x66) + dx);
    DP16(r, 0x67) = (uint16_t)(DP16(r, 0x67) + dy);
    DP16(r, 0x56) = (uint16_t)(DP16(r, 0x04) + dx * 2);
    return stepped(r, l, way);
}

/* $8F:9F05 to $9F43. False when there are no steps: the routine is done. */
bool raycast_start(soe_regs r, RaycastLoop SOE_FAR *l)
{
    uint8_t direction = (uint8_t)r->a, steps = (uint8_t)(r->a >> 8);
    uint16_t a, product;

    SOE_AT(0x9F05);
    r->x = direction;                                        /* SEP #$30 clears X and Y high */
    r->y &= 0x00FF;
    r->a = (uint16_t)((uint16_t)direction << 8 | steps);     /* XBA */
    r->p = (uint8_t)(r->p & ~(SOE_M | SOE_X));               /* REP #$30, on either way out */
    if (steps == 0) {
        soe_nz8(r, 0);
        soe_carry(r, true);
        return false;
    }
    SOE_AT(0x9F0F);
    r->y = steps;
    l->direction = direction;
    l->steps = steps;
    a = (uint16_t)((DP16(r, 0x66) & 0xFF00) | ABS16(r, 0x08EE));
    product = soe_multiply(a);                               /* $4202 x $4203 */
    a = (uint16_t)(DP16(r, 0x66) & 0x00FF);
    soe_carry(r, false);
    a = soe_adc16(r, a, product);
    soe_carry(r, (a & 0x8000) != 0);                         /* ASL A */
    a = (uint16_t)(a << 1);
    DP16(r, 0x56) = a;
    DP16(r, 0x44) = (uint16_t)(DP16(r, 0x68) & 0x00FF);
    r->x = LONG16((uint32_t)DP8(r, 0x58) << 16 | a);         /* LDA [$56] / TAX */
    DP16(r, 0x50) = LONG16(TILE_WORD + r->x);
    eval_tile_collision_bind(r);
    DP16(r, 0x52) = r->a;
    return true;
}

/* One pass from $9F43. False when the walk is over. */
bool raycast_step(soe_regs r, RaycastLoop SOE_FAR *l)
{
    r->a = l->direction;                                     /* LDA $03,s / TAX / JMP ($A3D8,x) */
    r->x = l->direction;
    switch (l->direction) {
    case 0x0: return walk_vertical(r, l, 0x9F49, true, 0x0083, 0x0001, 0x0080, 0x0002, 0x0002, 0x0080, 0, 1, 7);
    case 0x2: return walk_diagonal(r, l, 0xA1C9, true, true, 0x0002, 1);
    case 0x4: return walk_horizontal(r, l, 0xA0FB, true, 0x000E, 0x0004, 2, 0x0002, 8, 0x0008, 9);
    case 0x6: return walk_diagonal(r, l, 0xA243, true, false, 0x0008, 3);
    case 0x8: return walk_vertical(r, l, 0xA022, false, 0x0038, 0x0010, 0x0020, 0x0008, 0x0008, 0x0020, 4, 3, 5);
    case 0xA: return walk_diagonal(r, l, 0xA337, false, false, 0x0020, 5);
    case 0xC: return walk_horizontal(r, l, 0xA162, false, 0x00E0, 0x0040, 6, 0x0080, 0x000B, 0x0020, 0x000A);
    default:  return walk_diagonal(r, l, 0xA2BD, false, true, 0x0080, 7);
    }
}

SOE_BIND(companion_metatile_raycast_line_stepper, 0x8F9F05, RTS)
{
    RaycastLoop l;

    if (!raycast_start(r, &l)) return;
    while (raycast_step(r, &l)) { }
}
