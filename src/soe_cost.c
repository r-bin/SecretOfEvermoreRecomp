/*
 * What a path through a routine costs, from the ROM's own bytes. A bound C routine marks its basic blocks with
 * SOE_AT(pc) (a no-op outside this build); this walks the instructions between two marks and adds up what bus-exact
 * code charges: 6 master clocks per CPU cycle, each code fetch and data access at its speed. Which way a branch went
 * follows from the next mark (a branch is taken when its target is that mark), so a binding needs no cost code of its
 * own. A mark the bytes do not reach is an error in the C's marks, and stops the run.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common_rtl.h"
#include "cpu_state.h"
#include "soe_cost.h"

static SoeCostTrace s_t;

static uint8_t rom8(uint32_t pc) { return g_rom[((pc & 0x3F0000u) | (pc & 0xFFFFu))]; }
static uint16_t rom16(uint32_t pc) { return (uint16_t)(rom8(pc) | rom8(pc + 1) << 8); }

static void stop_at(const char *why, uint32_t pc) {
    fprintf(stderr, "soe_cost: %s at $%06X (next mark $%04X)\n", why, (unsigned)pc, (unsigned)s_t.want);
    abort();
}

static uint32_t speed(uint32_t addr24) { return cpu_region_speed(addr24 & 0xFFFFFFu) - 6u; }
static void data(uint32_t addr24, unsigned bytes) { for (unsigned i = 0; i < bytes; i++) s_t.data += speed(addr24 + i); }
static void wram(unsigned bytes) { s_t.data += 2u * bytes; }      /* direct page, stack, pointers: all WRAM here */
static void step(unsigned cycles, unsigned len) { s_t.cycles += cycles; s_t.fetches += len; }
static unsigned dl(void) { return (s_t.d & 0xFF) ? 1u : 0u; }

/* The read/write group: ORA AND EOR ADC STA LDA CMP SBC by the low bits of the opcode. */
static void alu(uint8_t op, uint32_t pc, unsigned w) {
    const bool store = (op & 0xE0) == 0x80;
    const uint32_t db = (uint32_t)s_t.db << 16;
    const unsigned m = w - 1;
    switch (op & 0x1F) {
    case 0x01: step(6 + m + dl(), 2); wram(w); break;                                    /* (dp,x) */
    case 0x03: step(4 + m, 2); wram(w); break;                                           /* sr,s */
    case 0x05: step(3 + m + dl(), 2); wram(w); break;                                    /* dp */
    case 0x07: case 0x17: {                                                              /* [dp], [dp],y */
        const uint16_t dp = (uint16_t)(s_t.d + rom8(pc + 1));
        const uint32_t ptr = (uint32_t)g_ram[dp] | (uint32_t)g_ram[(uint16_t)(dp + 1)] << 8 | (uint32_t)g_ram[(uint16_t)(dp + 2)] << 16;
        step(6 + m + dl(), 2); wram(3); data(ptr, w); break;
    }
    case 0x09: step(2 + m, 1 + w); break;                                                /* # */
    case 0x0D: step(4 + m, 3); data(db | rom16(pc + 1), w); break;                       /* abs */
    case 0x0F: step(5 + m, 4); data(rom16(pc + 1) | (uint32_t)rom8(pc + 3) << 16, w); break;   /* long */
    case 0x11: step(6 + m + dl(), 2); wram(w + 2); break;                                /* (dp),y */
    case 0x12: step(5 + m + dl(), 2); wram(w + 2); break;                                /* (dp) */
    case 0x13: step(7 + m, 2); wram(w + 2); break;                                       /* (sr,s),y */
    case 0x15: step(4 + m + dl(), 2); wram(w); break;                                    /* dp,x */
    case 0x19: case 0x1D:                                                                /* abs,y abs,x */
        if (!s_t.x16 && !store) stop_at("8-bit index: its page cross needs the index", pc);
        step(5 + m, 3); data(db | rom16(pc + 1), w); break;
    case 0x1F: step(5 + m, 4); data(rom16(pc + 1) | (uint32_t)rom8(pc + 3) << 16, w); break;   /* long,x */
    default: stop_at("unknown ALU mode", pc);
    }
}

/* One instruction at s_t.pc; the conditional branches are decided by `want`. */
static void walk_one(void) {
    const uint32_t pc = s_t.pc, bank = pc & 0xFF0000u;
    const uint8_t op = rom8(pc);
    const unsigned w = s_t.m16 ? 2 : 1, xw = s_t.x16 ? 2 : 1;
    const uint32_t db = (uint32_t)s_t.db << 16;
    uint32_t next = 0;
    unsigned len = 0;
    if ((op & 0x03) == 0x01 || (op & 0x03) == 0x03) {
        if (op == 0x89 || op == 0x0B || op == 0x2B || op == 0x4B || op == 0x6B || op == 0x8B || op == 0xAB || op == 0xCB || op == 0xEB
            || op == 0x1B || op == 0x3B || op == 0x5B || op == 0x7B || op == 0x9B || op == 0xBB || op == 0xDB || op == 0xFB) goto other;
        alu(op, pc, w);
        static const uint8_t lens[32] = { 0,2,0,2,0,2,0,2,0,0,0,0,0,3,0,4,0,2,2,2,0,2,0,2,0,3,0,0,0,3,0,4 };
        len = (op & 0x1F) == 0x09 ? 1 + w : lens[op & 0x1F];
        s_t.pc = pc + len;
        return;
    }
other:
    switch (op) {
    /* implied, 2 cycles */
    case 0x18: case 0x38: case 0x58: case 0x78: case 0xB8: case 0xD8: case 0xF8: case 0xEA: case 0xAA: case 0xA8: case 0x8A: case 0x98: case 0xBB: case 0x9B:
    case 0x5B: case 0x7B: case 0x1B: case 0x3B: case 0xBA: case 0x9A: case 0xE8: case 0xC8: case 0xCA: case 0x88: case 0x1A: case 0x3A: case 0x0A: case 0x4A: case 0x2A: case 0x6A:
        step(2, 1); break;
    case 0xEB: step(3, 1); break;                                                        /* XBA */
    case 0x89: step(2 + w - 1, 1 + w); break;                                            /* BIT # */
    case 0xA2: case 0xA0: case 0xE0: case 0xC0: step(2 + xw - 1, 1 + xw); break;         /* LDX LDY CPX CPY # */
    case 0xC2: case 0xE2: {                                                              /* REP SEP */
        const uint8_t bits = rom8(pc + 1);
        step(3, 2);
        if (bits & 0x20) s_t.m16 = op == 0xC2;
        if (bits & 0x10) s_t.x16 = op == 0xC2;
        break;
    }
    case 0xA6: case 0xA4: case 0x86: case 0x84: case 0xE4: case 0xC4:                    /* LDX LDY STX STY CPX CPY dp */
        step(3 + xw - 1 + dl(), 2); wram(xw); break;
    case 0xAE: case 0xAC: case 0x8E: case 0x8C: case 0xEC: case 0xCC:                    /* the same, abs */
        step(4 + xw - 1, 3); data(db | rom16(pc + 1), xw); break;
    case 0x24: step(3 + w - 1 + dl(), 2); wram(w); break;                                /* BIT dp */
    case 0x2C: step(4 + w - 1, 3); data(db | rom16(pc + 1), w); break;                   /* BIT abs */
    case 0x64: step(3 + w - 1 + dl(), 2); wram(w); break;                                /* STZ dp */
    case 0x9C: step(4 + w - 1, 3); data(db | rom16(pc + 1), w); break;                   /* STZ abs */
    case 0x06: case 0x26: case 0x46: case 0x66: case 0xC6: case 0xE6: case 0x04: case 0x14:   /* RMW dp */
        step(5 + 2 * (w - 1) + dl(), 2); wram(2 * w); break;
    case 0x0E: case 0x2E: case 0x4E: case 0x6E: case 0xCE: case 0xEE: case 0x0C: case 0x1C:   /* RMW abs */
        step(6 + 2 * (w - 1), 3); data(db | rom16(pc + 1), w); data(db | rom16(pc + 1), w); break;
    case 0x1E: case 0x3E: case 0x5E: case 0x7E: case 0xDE: case 0xFE:                    /* RMW abs,x */
        step(7 + 2 * (w - 1), 3); data(db | rom16(pc + 1), w); data(db | rom16(pc + 1), w); break;
    case 0x48: step(3 + w - 1, 1); wram(w); s_t.s = (uint16_t)(s_t.s - w); break;        /* PHA */
    case 0x68: step(4 + w - 1, 1); wram(w); s_t.s = (uint16_t)(s_t.s + w); break;        /* PLA */
    case 0xDA: case 0x5A: step(3 + xw - 1, 1); wram(xw); s_t.s = (uint16_t)(s_t.s - xw); break;   /* PHX PHY */
    case 0xFA: case 0x7A: step(4 + xw - 1, 1); wram(xw); s_t.s = (uint16_t)(s_t.s + xw); break;   /* PLX PLY */
    case 0x8B: step(3, 1); wram(1); g_ram[s_t.s] = s_t.db; s_t.s = (uint16_t)(s_t.s - 1); break;               /* PHB */
    case 0x4B: step(3, 1); wram(1); g_ram[s_t.s] = (uint8_t)(pc >> 16); s_t.s = (uint16_t)(s_t.s - 1); break;  /* PHK */
    case 0x08: step(3, 1); wram(1); s_t.s = (uint16_t)(s_t.s - 1); break;                /* PHP */
    case 0xAB: step(4, 1); wram(1); s_t.s = (uint16_t)(s_t.s + 1); s_t.db = g_ram[s_t.s]; break;               /* PLB */
    case 0x28: step(4, 1); wram(1); s_t.s = (uint16_t)(s_t.s + 1); break;                /* PLP */
    case 0xF4:                                                                           /* PEA */
        step(5, 3); wram(2);
        g_ram[s_t.s] = rom8(pc + 2);
        g_ram[(uint16_t)(s_t.s - 1)] = rom8(pc + 1);
        s_t.s = (uint16_t)(s_t.s - 2);
        break;
    case 0x80: step(3, 2); next = (uint32_t)(pc + 2 + (int8_t)rom8(pc + 1)); break;      /* BRA */
    case 0x4C: step(3, 3); next = bank | rom16(pc + 1); break;                           /* JMP */
    case 0x7C: step(6, 3); data(bank | rom16(pc + 1), 2); next = bank | s_t.want; break; /* JMP (abs,x): to the mark */
    case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xB0: case 0xD0: case 0xF0: {
        const uint32_t target = bank | (uint16_t)(pc + 2 + (int8_t)rom8(pc + 1)), fall = bank | (uint16_t)(pc + 2);
        const bool taken = (target & 0xFFFF) == s_t.want;   /* a mark stands at every target the C takes */
        step(taken ? 3 : 2, 2);
        s_t.pc = taken ? target : fall;
        return;
    }
    case 0x22:                                                                           /* JSL */
        step(8, 4); wram(3);
        if (s_t.depth == SOE_COST_DEPTH) stop_at("calls too deep", pc);
        s_t.ret[s_t.depth++] = pc + 4;
        g_ram[s_t.s] = (uint8_t)(pc >> 16);                                              /* what it leaves on the stack */
        g_ram[(uint16_t)(s_t.s - 1)] = (uint8_t)((pc + 3) >> 8);
        g_ram[(uint16_t)(s_t.s - 2)] = (uint8_t)(pc + 3);
        s_t.s = (uint16_t)(s_t.s - 3);
        s_t.pc = rom16(pc + 1) | (uint32_t)rom8(pc + 3) << 16;
        return;
    case 0x6B: case 0x60:                                                                /* RTL RTS */
        step(6, 1); wram(op == 0x6B ? 3 : 2);
        s_t.s = (uint16_t)(s_t.s + (op == 0x6B ? 3 : 2));
        if (!s_t.depth) {                                /* a mark past the routine's own return: the code it returns to */
            if (s_t.want) s_t.pc = bank | s_t.want;
            else s_t.done = true;
            return;
        }
        s_t.pc = s_t.ret[--s_t.depth];
        return;
    default: stop_at("opcode not modelled", pc);
    }
    if (next) { s_t.pc = bank | (next & 0xFFFF); return; }
    static const uint8_t two[] = { 0xC2, 0xE2, 0xA6, 0xA4, 0x86, 0x84, 0xE4, 0xC4, 0x24, 0x64, 0x06, 0x26, 0x46, 0x66, 0xC6, 0xE6, 0x04, 0x14 };
    len = 1;
    for (unsigned i = 0; i < sizeof two; i++) if (op == two[i]) len = 2;
    if (op == 0x89) len = 1 + w;
    if (op == 0xA2 || op == 0xA0 || op == 0xE0 || op == 0xC0) len = 1 + xw;
    if (op == 0xAE || op == 0xAC || op == 0x8E || op == 0x8C || op == 0xEC || op == 0xCC || op == 0x2C || op == 0x9C || op == 0xF4
        || op == 0x0E || op == 0x2E || op == 0x4E || op == 0x6E || op == 0xCE || op == 0xEE || op == 0x0C || op == 0x1C
        || op == 0x1E || op == 0x3E || op == 0x5E || op == 0x7E || op == 0xDE || op == 0xFE) len = 3;
    s_t.pc = pc + len;
}

void soe_cost_begin(uint32_t pc24, uint16_t d, uint8_t db, bool m16, bool x16, uint16_t s) {
    s_t = (SoeCostTrace){ .pc = pc24, .d = d, .db = db, .m16 = m16, .x16 = x16, .s = s };
}

/* The cost so far, for a binding that charges as it goes. */
void soe_cost_take(SoeCostTrace *out) {
    *out = s_t;
    s_t.cycles = s_t.fetches = s_t.data = 0;
}

/* SOE_COST_TRACE=1: each walked instruction and what it costs, against the interpreter's SNESRECOMP_CYC_WATCH lines. */
static void walk_traced(void) {
    static int on = -1;
    if (on < 0) on = getenv("SOE_COST_TRACE") != NULL;
    if (!on) { walk_one(); return; }
    const uint32_t pc = s_t.pc;
    const uint64_t before = 6 * s_t.cycles + s_t.data;
    walk_one();
    fprintf(stderr, "[wk] %06X %llu\n", (unsigned)pc, (unsigned long long)(6 * s_t.cycles + s_t.data - before));
}

void soe_at(uint16_t pc) {
    static int trace = -1;
    if (trace < 0) trace = getenv("SOE_COST_TRACE") != NULL;
    s_t.want = pc;
    if (trace) fprintf(stderr, "[wk] @%04X\n", pc);
    if (!s_t.started) {
        s_t.started = true;
        if ((s_t.pc & 0xFFFF) != pc) stop_at("first mark is not the entry", s_t.pc);
        return;
    }
    for (int n = 0;; n++) {
        walk_traced();
        if ((s_t.pc & 0xFFFF) == pc) return;
        if (s_t.done || n > 512) stop_at("mark not reached", s_t.pc);
    }
}

/* From the last mark to the routine's return. */
void soe_cost_end(SoeCostTrace *out) {
    s_t.want = 0;
    for (int n = 0; !s_t.done; n++) {
        walk_traced();
        if (n > 512) stop_at("no return after the last mark", s_t.pc);
    }
    *out = s_t;
}
