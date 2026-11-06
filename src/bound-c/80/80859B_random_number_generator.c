/*
 * $80:859B random_number_generator (knowledge/routines/random_number_generator.md), RTL. Gate: see the knowledge entry.
 *
 * Four words of state at DB:$0133..$013A (RNG_STATE). Four times: rotate one word left through
 * carry, carry out bumps the next word, then add another word with carry and store. The carry the
 * caller enters with goes into the first rotate. Leaves A = the new first word, N V Z C from the
 * last ADC; X, Y, DB, D untouched. Entered with a 16-bit accumulator.
 */

/*
 * Routine Rating & Verification:
 * - Target Routine:   random_number_generator ($80859B) [RTL]
 * - Test Method:      G1 differential testing (trampoline + 4th MB bridge in snes9x / cpu65816)
 * - Initial ROM ASM:  25 instructions (61 bytes)
 * - Compiled C ASM:   63 instructions [Calypsi 65816 -O1 --speed]
 * - Code Expansion:   2.5x instructions vs ROM
 * - ASM Closeness:    100% state parity per call (A, X, Y, S, D, DB, P, WRAM, I/O order match ROM)
 * - Rating Hash:      30c6e3bd671db0ee
 * - Rating Status:    VALID (unmodified since rating)
 */
#include "soe.h"
#include "soe_ram.h"

#ifndef SOE_OPTIMIZE

#define W0 (RNG_SEED_0_ADDR + 0)
#define W1 (RNG_SEED_0_ADDR + 2)
#define W2 (RNG_SEED_0_ADDR + 4)
#define W3 (RNG_SEED_0_ADDR + 6)

/* rotate ABS16(rot) through carry; carry out increments ABS16(bump); A + ABS16(add) + carry -> ABS16(store) */
static uint16_t step(soe_regs r, uint16_t rot, uint16_t bump, uint16_t add, uint16_t store)
{
    uint16_t a = soe_rol16(r, ABS16(r, rot));
    if (r->p & SOE_C)
        ABS16(r, bump) = (uint16_t)(ABS16(r, bump) + 1);
    a = soe_adc16(r, a, ABS16(r, add));
    ABS16(r, store) = a;
    return a;
}

SOE_BIND(random_number_generator, 0x80859B, RTL)
{
    step(r, W3, W2, W1, W3);
    step(r, W2, W1, W0, W2);
    step(r, W1, W0, W2, W1);
    r->a = step(r, W0, W3, W3, W0);
}

#else /* SOE_OPTIMIZE: compact ~1.0x variant (G1-checked); the code above is the documentation */

#define W0 (RNG_SEED_0_ADDR + 0)
#define W1 (RNG_SEED_0_ADDR + 2)
#define W2 (RNG_SEED_0_ADDR + 4)
#define W3 (RNG_SEED_0_ADDR + 6)

SOE_BIND(random_number_generator, 0x80859B, RTL)
{
#ifdef __CALYPSI_TARGET_65816__
    uint16_t p_val;
    __asm (
        "  txa\n"
        "  lsr a\n"
        "  lda 0x0139\n"
        "  rol a\n"
        "  bcc s1\n"
        "  inc 0x0137\n"
        "s1:\n"
        "  adc 0x0135\n"
        "  sta 0x0139\n"
        "  lda 0x0137\n"
        "  rol a\n"
        "  bcc s2\n"
        "  inc 0x0135\n"
        "s2:\n"
        "  adc 0x0133\n"
        "  sta 0x0137\n"
        "  lda 0x0135\n"
        "  rol a\n"
        "  bcc s3\n"
        "  inc 0x0133\n"
        "s3:\n"
        "  adc 0x0137\n"
        "  sta 0x0135\n"
        "  lda 0x0133\n"
        "  rol a\n"
        "  bcc s4\n"
        "  inc 0x0139\n"
        "s4:\n"
        "  adc 0x0139\n"
        "  sta 0x0133\n"
        "  php\n"
        "  sep #0x20\n"
        "  pla\n"
        "  rep #0x20\n"
        "  and ##0x00ff\n"
        : "=Kc" (p_val)
        : "Kx" ((uint16_t)r->p)
    );
    r->p = (uint8_t)p_val;
    r->a = ABS16(r, W0);
#else
    uint8_t SOE_FAR *bank = SOE_BANK(r);
    uint16_t c = r->p & SOE_C, a = 0, m = 0, out = 0;

#define STEP(rot, bump, add, store)                                     \
    a = BANK16(bank, rot);                                              \
    out = (uint16_t)(a << 1 | c);                                       \
    c = a >> 15;                                                        \
    if (c) BANK16(bank, bump) = (uint16_t)(BANK16(bank, bump) + 1);     \
    a = out;                                                            \
    m = BANK16(bank, add);                                              \
    out = (uint16_t)(a + m + c);                                        \
    c = c ? out <= a : out < a;                                         \
    BANK16(bank, store) = out;

    STEP(W3, W2, W1, W3)
    STEP(W2, W1, W0, W2)
    STEP(W1, W0, W2, W1)
    STEP(W0, W3, W3, W0)
#undef STEP
    r->a = out;
    r->p = soe_flags_add16(r->p, a, m, out, c);
#endif
}

#endif /* SOE_OPTIMIZE */