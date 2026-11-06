/*
 * soe.h: what every reconstructed C file sees of the machine, so all of them share one view of
 * WRAM, ROM and I/O (docs/reverse-engineering/c-to-rom.md §10).
 *
 * Two compilers read this file:
 *  - Calypsi (65816, large code and data model): tools/c-to-rom/build.mts compiles bound files into
 *    the 4th MB of a HiROM image. Every access below is a 24-bit long access, so DB never matters.
 *  - a host C compiler (SOE_HOST): gate G0, the file compiles with -Werror. WRAM is g_ram.
 *
 * Rules for C that goes into the ROM:
 *  - no static or global variables: the build gives the linker no RAM for them. State lives in WRAM
 *    under its knowledge-entry name (soe_ram.h), or in locals;
 *  - tables and assets are read from the ROM image (ROM8/ROM16/ROM24) by bus address, never copied
 *    into C;
 *  - integer types of the width the asm uses, explicit casts where it wraps;
 *  - no comma expressions in conditions (`else if (soe_cmp16(...), r->p & SOE_N)`): built with
 *    Calypsi 5.18 that took the wrong branch on $8F:C02B; write the compare and the test apart;
 *  - after a binding's last register restore (PLY, PLX, PLB, TDC ...) set N and Z as that
 *    instruction does: the asm's exit flags are part of the contract G1 checks;
 *  - no call or side effect in the argument of ROM8/ROM16 and the like: on the host they use it twice;
 *  - the C preserves the game, it does not improve it: the default code follows the asm and is
 *    never changed for speed. Faster variants live only under `#ifdef SOE_OPTIMIZE` (off).
 */
#ifndef SOE_H
#define SOE_H

#include <stdint.h>
#include <stdbool.h>

/*
 * SOE_AT(pc): this point of the C is the ROM's basic block at $pc of the routine's bank. Nothing in the ROM build; the
 * recomp's glue works out the path's cost from these marks (SecretOfEvermoreRecomp src/soe_cost.c): a branch counts as
 * taken when its target is the next mark. So a mark stands at each target the C takes, and on the fall-through of a branch
 * when a later branch has the same target.
 */
#ifdef SOE_COST
void soe_at(uint16_t pc);
#define SOE_AT(pc) soe_at(pc)
#else
#define SOE_AT(pc) ((void)0)
#endif

#ifdef __CALYPSI_TARGET_65816__
#define SOE_FAR __far
#else
#define SOE_FAR
extern uint8_t g_ram[0x20000];       /* WRAM $7E0000-$7FFFFF */
extern uint8_t g_io[0x10000];        /* bank $00 I/O, $2000-$5FFF */
extern const uint8_t *g_rom;         /* the ROM image (HiROM, file order) */
#endif

/* ---- WRAM: an offset 0x00000-0x1FFFF from $7E:0000 (bank $7F = 0x10000 + addr) ---- */
#ifdef __CALYPSI_TARGET_65816__
#define RAM8(off)   (*(uint8_t SOE_FAR *)(0x7E0000UL + (uint32_t)(off)))
#define RAM16(off)  (*(uint16_t SOE_FAR *)(0x7E0000UL + (uint32_t)(off)))
#define RAMPTR(type, off) ((type SOE_FAR *)(0x7E0000UL + (uint32_t)(off)))
#else
#define RAM8(off)   (g_ram[(uint32_t)(off) & 0x1FFFF])
#define RAM16(off)  (*(uint16_t *)&g_ram[(uint32_t)(off) & 0x1FFFF])
#define RAMPTR(type, off) ((type *)&g_ram[(uint32_t)(off) & 0x1FFFF])
#endif

/* ---- ROM: a 24-bit bus address ($C0-$FF full banks, $80-$BF upper halves) ---- */
#ifdef __CALYPSI_TARGET_65816__
#define ROM8(bus)   (*(const uint8_t SOE_FAR *)(uint32_t)(bus))
#define ROM16(bus)  (*(const uint16_t SOE_FAR *)(uint32_t)(bus))
#else
#define SOE_ROM_OFFSET(bus) ((uint32_t)(bus) & 0x3FFFFF)
#define ROM8(bus)   (g_rom[SOE_ROM_OFFSET(bus)])
#define ROM16(bus)  ((uint16_t)(g_rom[SOE_ROM_OFFSET(bus)] | g_rom[SOE_ROM_OFFSET(bus) + 1] << 8))
#endif
#define ROM24(bus)  ((uint32_t)ROM16(bus) | (uint32_t)ROM8((uint32_t)(bus) + 2) << 16)

/* ---- I/O registers in bank $00 ($2100-$21FF PPU, $4200-$43FF CPU/DMA): always volatile ---- */
#ifdef __CALYPSI_TARGET_65816__
#define IO8(addr)   (*(volatile uint8_t SOE_FAR *)(uint32_t)(addr))
#define IO16(addr)  (*(volatile uint16_t SOE_FAR *)(uint32_t)(addr))
#else
#define IO8(addr)   (*(volatile uint8_t *)&g_io[(addr) & 0xFFFF])
#define IO16(addr)  (*(volatile uint16_t *)&g_io[(addr) & 0xFFFF])
#endif

/* ---- A bound routine: the CPU state at its entry, and what it leaves ----
 * The bridge (tools/c-to-rom/build.mts) pushes the registers as this record, calls the binding with a
 * pointer to it and pulls them back afterwards, P included. A binding writes every register and flag
 * the asm leaves, since the caller may read any of them; G1 compares all of them. Widths: a and x/y
 * are the full 16-bit registers (with x8, the high byte of x and y is 0). */
typedef struct {
    uint16_t a, x, y, d;
    uint8_t db, p;
} soe_regs_t;

typedef soe_regs_t SOE_FAR *soe_regs;

#define SOE_N 0x80
#define SOE_V 0x40
#define SOE_M 0x20     /* 8-bit accumulator */
#define SOE_X 0x10     /* 8-bit index registers */
#define SOE_D 0x08
#define SOE_I 0x04
#define SOE_Z 0x02
#define SOE_C 0x01

/* N and Z as a load of v leaves them (16-bit, or 8-bit when the asm ends with an 8-bit accumulator). */
static inline void soe_nz16(soe_regs r, uint16_t v) {
    r->p = (uint8_t)((r->p & ~(SOE_N | SOE_Z)) | (v & 0x8000 ? SOE_N : 0) | (v == 0 ? SOE_Z : 0));
}
static inline void soe_nz8(soe_regs r, uint8_t v) {
    r->p = (uint8_t)((r->p & ~(SOE_N | SOE_Z)) | (v & 0x80 ? SOE_N : 0) | (v == 0 ? SOE_Z : 0));
}
static inline void soe_carry(soe_regs r, bool c) {
    r->p = (uint8_t)(c ? r->p | SOE_C : r->p & ~SOE_C);
}

/* ---- Absolute addressing as the asm does it: bank DB at entry (LDA $0139 reads DB:$0139) ----
 * Use these where the asm uses 16-bit absolute operands, so the C reads what the asm reads for any
 * DB the callers pass ($7E, $80, ...); RAM8/RAM16 are for long ($7E:xxxx) operands. */
#ifdef __CALYPSI_TARGET_65816__
#define ABS8(r, a)  (*(uint8_t SOE_FAR *)((uint32_t)(r)->db << 16 | (uint16_t)(a)))
#define ABS16(r, a) (*(uint16_t SOE_FAR *)((uint32_t)(r)->db << 16 | (uint16_t)(a)))
#else
uint8_t *soe_bus(uint32_t addr);
#define ABS8(r, a)  (*soe_bus((uint32_t)(r)->db << 16 | (uint16_t)(a)))
#define ABS16(r, a) (*(uint16_t *)soe_bus((uint32_t)(r)->db << 16 | (uint16_t)(a)))
#endif

/* Absolute indexed (LDA $0022,y): DB:base + index as a 24-bit sum, so it carries into the next bank
 * the way the CPU does. */
#ifdef __CALYPSI_TARGET_65816__
#define ABSI8(r, base, i)  (*(uint8_t SOE_FAR *)(((uint32_t)(r)->db << 16 | (uint16_t)(base)) + (uint16_t)(i)))
#define ABSI16(r, base, i) (*(uint16_t SOE_FAR *)(((uint32_t)(r)->db << 16 | (uint16_t)(base)) + (uint16_t)(i)))
#else
#define ABSI8(r, base, i)  (*soe_bus(((uint32_t)(r)->db << 16 | (uint16_t)(base)) + (uint16_t)(i)))
#define ABSI16(r, base, i) (*(uint16_t *)soe_bus(((uint32_t)(r)->db << 16 | (uint16_t)(base)) + (uint16_t)(i)))
#endif

/* ---- Direct page as the asm does it: bank $00, D + offset, wrapping in the bank ---- */
#ifdef __CALYPSI_TARGET_65816__
#define DP8(r, o)   (*(uint8_t SOE_FAR *)(uint32_t)(uint16_t)((r)->d + (o)))
#define DP16(r, o)  (*(uint16_t SOE_FAR *)(uint32_t)(uint16_t)((r)->d + (o)))
#else
#define DP8(r, o)   (*soe_bus((uint16_t)((r)->d + (o))))
#define DP16(r, o)  (*(uint16_t *)soe_bus((uint16_t)((r)->d + (o))))
#endif

/* ---- Any bus address, and the 24-bit pointers the asm keeps in the direct page (LDA [$82]) ---- */
#ifdef __CALYPSI_TARGET_65816__
#define LONG8(bus)  (*(uint8_t SOE_FAR *)(uint32_t)(bus))
#define LONG16(bus) (*(uint16_t SOE_FAR *)(uint32_t)(bus))
#else
#define LONG8(bus)  (*soe_bus((uint32_t)(bus)))
#define LONG16(bus) (*(uint16_t *)soe_bus((uint32_t)(bus)))
#endif
/* the pointer in DP o..o+2; [o],y is SOE_DPPTR(r, o) + y, carrying into the next bank */
#define SOE_DPPTR(r, o) ((uint32_t)DP16(r, o) | (uint32_t)DP8(r, (o) + 2) << 16)

/* ---- The 8-bit accumulator: its low byte; B (the high byte) stays as the caller left it ---- */
#define SOE_A8(r)   ((uint8_t)(r)->a)
static inline void soe_set_a8(soe_regs r, uint8_t v) { r->a = (uint16_t)((r->a & 0xFF00) | v); }

/* ---- The flag arithmetic of single instructions, for routines whose callers read the flags ---- */
/* CMP/CPX/CPY: N Z C as a - m leaves them. */
static inline void soe_cmp8(soe_regs r, uint8_t a, uint8_t m) {
    uint8_t d = (uint8_t)(a - m);
    r->p = (uint8_t)((r->p & ~(SOE_N | SOE_Z | SOE_C)) | (d & 0x80) | (d == 0 ? SOE_Z : 0) | (a >= m ? SOE_C : 0));
}
static inline void soe_cmp16(soe_regs r, uint16_t a, uint16_t m) {
    uint16_t d = (uint16_t)(a - m);
    r->p = (uint8_t)((r->p & ~(SOE_N | SOE_Z | SOE_C)) | (d & 0x8000 ? SOE_N : 0) | (d == 0 ? SOE_Z : 0) | (a >= m ? SOE_C : 0));
}
/* BIT #imm: only Z changes (Z = no common bit); N and V stay. */
static inline void soe_bit_imm(soe_regs r, uint16_t a, uint16_t imm) {
    r->p = (uint8_t)((r->p & ~SOE_Z) | ((a & imm) == 0 ? SOE_Z : 0));
}
/* ADC (8-bit, binary mode): a + m + carry; sets N V Z C. */
static inline uint8_t soe_adc8(soe_regs r, uint8_t a, uint8_t m) {
    uint16_t sum = (uint16_t)(a + m + (r->p & SOE_C));
    uint8_t out = (uint8_t)sum;
    r->p = (uint8_t)((r->p & ~(SOE_N | SOE_V | SOE_Z | SOE_C)) | (out & 0x80) | (out == 0 ? SOE_Z : 0)
        | (sum > 0xFF ? SOE_C : 0) | ((~(a ^ m) & (a ^ out) & 0x80) ? SOE_V : 0));
    return out;
}
/* SBC (8-bit, binary mode): a - m - !carry; sets N V Z C (C = no borrow). */
static inline uint8_t soe_sbc8(soe_regs r, uint8_t a, uint8_t m) {
    return soe_adc8(r, a, (uint8_t)~m);
}
/* ROL A (16-bit): carry in from P, bit 15 out to P. N and Z as the result. */
static inline uint16_t soe_rol16(soe_regs r, uint16_t v) {
    uint16_t out = (uint16_t)(v << 1 | (r->p & SOE_C));
    soe_carry(r, (v & 0x8000) != 0);
    soe_nz16(r, out);
    return out;
}
/* ADC (16-bit, binary mode): a + m + carry; sets N, V, Z, C. */
static inline uint16_t soe_adc16(soe_regs r, uint16_t a, uint16_t m) {
    uint32_t sum = (uint32_t)a + m + (r->p & SOE_C);
    uint16_t out = (uint16_t)sum;
    soe_carry(r, sum > 0xFFFF);
    r->p = (uint8_t)((r->p & ~SOE_V) | ((~(a ^ m) & (a ^ out) & 0x8000) ? SOE_V : 0));
    soe_nz16(r, out);
    return out;
}

/* SBC (16-bit, binary mode): a - m - !carry; sets N V Z C (C = no borrow). */
static inline uint16_t soe_sbc16(soe_regs r, uint16_t a, uint16_t m) {
    return soe_adc16(r, a, (uint16_t)~m);
}

/* MVN / MVP with 16-bit index registers: copy A + 1 bytes from src:X to dst:Y, one byte at a time
 * (so overlapping moves behave as on the CPU), upward (MVN) or downward (MVP). Leaves A = $FFFF,
 * X and Y past the block, DB = the destination bank; flags as they were. */
static inline void soe_block_move(soe_regs r, uint8_t dst, uint8_t src, int step) {
    do {
#ifdef __CALYPSI_TARGET_65816__
        *(uint8_t SOE_FAR *)((uint32_t)dst << 16 | r->y) = *(uint8_t SOE_FAR *)((uint32_t)src << 16 | r->x);
#else
        *soe_bus((uint32_t)dst << 16 | r->y) = *soe_bus((uint32_t)src << 16 | r->x);
#endif
        r->x = (uint16_t)(r->x + step);
        r->y = (uint16_t)(r->y + step);
        r->a = (uint16_t)(r->a - 1);
    } while (r->a != 0xFFFF);
    r->db = dst;
}
#define soe_mvn(r, dst, src) soe_block_move(r, dst, src, 1)
#define soe_mvp(r, dst, src) soe_block_move(r, dst, src, -1)
/* BRK, COP and STP all halt this game's CPU (knowledge/routines/cop_handler.md): the C equivalent. */
void soe_stp(void);

/* SOE_BIND(name, 0x80AD62, RTL) { ... } replaces the ROM routine at that address (its execution
 * address, $8x) with this function; RTS routines are reached through a JSL/RTS trampoline. The build
 * finds bindings by this macro. */
#define SOE_BIND(name, addr, ret) void name##_bind(soe_regs r)

/* STA $4202 with both factors, then LDA $4216: the hardware multiplier. */
#ifdef SOE_COST
uint16_t soe_multiply(uint16_t factors);
#else
static inline uint16_t soe_multiply(uint16_t factors) { IO16(0x4202) = factors; return IO16(0x4216); }
#endif

/* ---- Calling a routine that is still asm: registers in and out through a record ----
 * soe_call_rtl: the routine ends in RTL. soe_call_rts: it ends in RTS; the runtime reaches it from
 * its own bank through an RTL byte in that bank (tools/c-to-rom/runtime.s). */
void soe_call_rtl(uint32_t target, soe_regs r);
void soe_call_rts(uint32_t target, soe_regs r);

/* ==== SOE_OPTIMIZE (off by default): helpers for faster variants ====
 * The default code is the documentation of the game and stays as it is. A file may add a faster
 * variant of a routine under `#ifdef SOE_OPTIMIZE` (build.mts --optimize); G1 checks it like the
 * default. Add one only when a measured need exists, and write that need down (c-to-rom.md §10). */
#ifdef SOE_OPTIMIZE
/* N and Z of a 16-bit value into p (pure: for a local copy of P). */
static inline uint8_t soe_p_nz16(uint8_t p, uint16_t v) {
    uint16_t f = p & (uint16_t)~(SOE_N | SOE_Z);
    if (v & 0x8000) f |= SOE_N;
    if (v == 0) f |= SOE_Z;
    return (uint8_t)f;
}

/* The data bank as a far base pointer: BANK16(bank, a) is DB:a without rebuilding a 24-bit pointer
 * per access (offsets are 16-bit: no carry into the next bank). For hot routines. */
#define SOE_BANK(r) ((uint8_t SOE_FAR *)((uint32_t)(r)->db << 16))
#define BANK8(bank, a)  (*(uint8_t SOE_FAR *)((bank) + (uint16_t)(a)))
#define BANK16(bank, a) (*(uint16_t SOE_FAR *)((bank) + (uint16_t)(a)))
/* Indexed (LDA $3BFF,x): base + index carries into the next bank as on the CPU; the 24-bit sum is
 * only built when the 16-bit one wraps. */
#ifdef __CALYPSI_TARGET_65816__
#define SOE_BANKI(bank, base, i) ((uint16_t)((uint16_t)(base) + (uint16_t)(i)) >= (uint16_t)(base) \
    ? (bank) + (uint16_t)((uint16_t)(base) + (uint16_t)(i)) \
    : (uint8_t SOE_FAR *)((uint32_t)(bank) + (uint16_t)(base) + (uint16_t)(i)))
#else
#define SOE_BANKI(bank, base, i) ((bank) + (uint32_t)(uint16_t)(base) + (uint16_t)(i))
#endif
#define BANKI8(bank, base, i)  (*(uint8_t SOE_FAR *)SOE_BANKI(bank, base, i))
#define BANKI16(bank, base, i) (*(uint16_t SOE_FAR *)SOE_BANKI(bank, base, i))

/* The direct page the same way: SOE_DIRECT(r) points at $00:D, DPB16(dp, o) wraps in bank $00 like
 * the CPU's direct page does. */
#define SOE_DIRECT(r)   ((uint8_t SOE_FAR *)(uint32_t)(r)->d)
#define DPB8(dp, o)     (*(uint8_t SOE_FAR *)((dp) + (uint16_t)(o)))
#define DPB16(dp, o)    (*(uint16_t SOE_FAR *)((dp) + (uint16_t)(o)))


/* P after a 16-bit ADC whose operands, result and carry out the caller already has (no 32-bit
 * math): N V Z C set, the rest of p kept. Pure: works on a local copy of P too. */
static inline uint8_t soe_flags_add16(uint8_t p, uint16_t a, uint16_t m, uint16_t out, uint16_t carry) {
    uint16_t f = p & (uint16_t)~(SOE_N | SOE_V | SOE_Z | SOE_C);
    if (carry) f |= SOE_C;
    if ((~(a ^ m) & (a ^ out)) & 0x8000) f |= SOE_V;
    if (out & 0x8000) f |= SOE_N;
    if (out == 0) f |= SOE_Z;
    return (uint8_t)f;
}
/* P after a 16-bit SBC/CMP-style subtraction out = a - m - !carry_in (CMP: carry_in = 1, and V
 * kept: pass keep_v). N V Z C set from the operands, the rest of p kept. Pure. */
static inline uint8_t soe_flags_sub16(uint8_t p, uint16_t a, uint16_t m, uint16_t out, uint16_t carry_in, bool keep_v) {
    uint16_t f = p & (uint16_t)~(SOE_N | SOE_Z | SOE_C | (keep_v ? 0 : SOE_V));
    if (carry_in ? a >= m : a > m) f |= SOE_C;
    if (!keep_v && (((a ^ m) & (a ^ out)) & 0x8000)) f |= SOE_V;
    if (out & 0x8000) f |= SOE_N;
    if (out == 0) f |= SOE_Z;
    return (uint8_t)f;
}
/* MVN / MVP $7E,$7E by the CPU itself (tools/c-to-rom/runtime.s): exact and fast */
#ifdef __CALYPSI_TARGET_65816__
void soe_mvn_7e(soe_regs r);
void soe_mvp_7e(soe_regs r);
#else
#define soe_mvn_7e(r) soe_mvn(r, 0x7E, 0x7E)
#define soe_mvp_7e(r) soe_mvp(r, 0x7E, 0x7E)
#endif

#endif /* SOE_OPTIMIZE */

#endif /* SOE_H */
