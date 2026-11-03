/*
 * Routines replaced with everscript-vscode's reconstructed C (`hle_func`, docs/game-rtl.md §4b).
 *
 * The C is compiled from the sibling checkout (CMake: SOE_RECONSTRUCTED_C, default ../everscript-vscode/
 * reconstructed-c). It is the documented code, never edited for us: this file only adapts the machine to it. A
 * binding must be indistinguishable from the ROM routine, so a helper takes the C path only when that is provable:
 *  - the C runs in one step, so it runs only when the routine would finish before the frame driver's next
 *    deadline, or crosses only a DRAM refresh stop with no IRQ possible (fits() below);
 *  - it charges the cycles the asm would charge on the path taken (6 master clocks per CPU cycle plus each bus
 *    access at its region's speed, as bus-exact compiled code does) and leaves S, registers, flags and the open
 *    bus as the RTL does;
 *  - it touches only WRAM, so the moment of each access inside the routine cannot be seen.
 * Otherwise the helper runs the routine's first instruction itself and yields to the scheduler at the second, a
 * compiled root (`body=` in recomp/seed-exclusions.txt), so the ROM bytes run compiled, exactly. Without the
 * reconstructed C the same path always runs.
 *
 * SOE_HLE_STATS=1 prints how often each helper took each path at exit. SOE_HLE_MUTATE=<helper's name> (1: all)
 * corrupts the C path's result: a control that shows the sync replays catch a wrong binding.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "soe_frame_driver.h"

#ifdef SOE_RECONSTRUCTED_C
#include "soe.h"

/* soe.h's host bus: WRAM through any bank that maps it. Bindings run only where their accesses are WRAM. */
uint8_t *soe_bus(uint32_t addr) {
    const uint8_t bank = (uint8_t)(addr >> 16);
    if (bank == 0x7E || bank == 0x7F) return &g_ram[addr & 0x1FFFF];
    if (!(bank & 0x40) && (addr & 0xFFFF) < 0x2000) return &g_ram[addr & 0x1FFF];
    fprintf(stderr, "soe_bindings: bus access $%06X outside WRAM\n", (unsigned)addr);
    abort();
}

void random_number_generator_bind(soe_regs r);
void update_entity_plane_bind(soe_regs r);
void eval_tile_collision_bind(soe_regs r);
#endif

enum { RNG, PLANE, TILE, BINDINGS };
static struct { const char *name; unsigned long long c, across, compiled; bool mutate; } s_bind[BINDINGS] = {
    { "random_number_generator" }, { "update_entity_plane" }, { "eval_tile_collision" },
};
static int s_stats = -1;
static void print_stats(void) {
    for (int i = 0; i < BINDINGS; i++)
        fprintf(stderr, "[hle] %s: %llu in C (%llu across a refresh stop), %llu compiled\n", s_bind[i].name,
                s_bind[i].c, s_bind[i].across, s_bind[i].compiled);
}
static void stats_init(void) {
    if (s_stats >= 0) return;
    const char *e = getenv("SOE_HLE_STATS"), *m = getenv("SOE_HLE_MUTATE");
    s_stats = e && e[0] == '1';
    for (int i = 0; m && i < BINDINGS; i++) s_bind[i].mutate = !strcmp(m, "1") || !strcmp(m, s_bind[i].name);
    if (s_stats) atexit(print_stats);
}

/* Master clocks of one access at a 24-bit address, beyond the 6 every CPU cycle costs. */
static uint64_t extra(uint32_t addr24) { return cpu_region_speed(addr24 & 0xFFFFFFu) - 6u; }

/* What a path through a routine costs, as bus-exact code charges an instruction: 6 master clocks per CPU cycle, each
 * code fetch at the code's speed, each data access at its address's (extra() is the part above 6). */
typedef struct { uint64_t cycles, fetches, data; } Cost;
static void insn(Cost *c, unsigned cycles, unsigned bytes) { c->cycles += cycles; c->fetches += bytes; }
static void imm16(Cost *c) { insn(c, 3, 3); }                                /* LDA AND ORA EOR BIT CMP # */
static void implied(Cost *c) { insn(c, 2, 1); }
static void branch(Cost *c, bool taken) { insn(c, taken ? 3 : 2, 2); }
static void word_at(Cost *c, uint32_t addr24) { c->data += extra(addr24) + extra(addr24 + 1u); }
/* A 16-bit direct page read or write: DL != 0 costs a cycle. */
static void dp16(Cost *c, const CpuState *cpu, uint8_t offset) {
    insn(c, (cpu->D & 0xFF) ? 5 : 4, 2);
    word_at(c, (uint16_t)(cpu->D + offset));
}
static void rtl(Cost *c, const CpuState *cpu) {
    const uint16_t s = cpu->S;
    insn(c, 6, 1);                                                           /* 3 pulls */
    c->data += extra((uint16_t)(s + 1)) + extra((uint16_t)(s + 2)) + extra((uint16_t)(s + 3));
}
static uint64_t master_of(const Cost *c, const CpuState *cpu) {
    const uint64_t fetch = ((g_memsel && (cpu->PB & 0x80)) ? 6u : 8u) - 6u;
    return 6 * c->cycles + c->fetches * fetch + c->data;
}
static void charge(CpuState *cpu, const Cost *c) {
    cpu->cycles += c->cycles;
    cpu->master_cycles += master_of(c, cpu);
}
/* The RTL at the end of the C path: S, and the open bus its last read leaves (the return bank). */
static void leave_rtl(CpuState *cpu) {
    cpu->open_bus = g_ram[(uint16_t)(cpu->S + 3)];
    cpu->S = (uint16_t)(cpu->S + 3);
}
static void set_nz16(CpuState *cpu, uint16_t v) {
    cpu->P = (uint8_t)((cpu->P & ~(CPU_P_N | CPU_P_Z)) | (v ? 0 : CPU_P_Z) | (v & 0x8000 ? CPU_P_N : 0));
}

static bool wide_a(const CpuState *cpu) { return !cpu->emulation && !cpu->m_flag; }
/* Direct page in the low WRAM mirror, where soe_bus finds it. */
static bool dp_in_wram(const CpuState *cpu, uint16_t last) { return (uint16_t)(cpu->D + last) < 0x2000 && cpu->D < 0x2000; }
static bool db_in_wram(const CpuState *cpu) { return cpu->DB == 0x7E || !(cpu->DB & 0x40); }

/* The C may run when the routine ends before the slice's deadline (0: none), or runs past a DRAM refresh stop
 * only: the frame driver charges a refresh after the slice from the points it crossed, so a WRAM-only routine
 * cannot tell. Then it must still end before the next stop that is not a refresh, with no IRQ to take: the driver
 * takes IRQs between slices, so the original could be interrupted at the refresh stop. */
static bool fits(const CpuState *cpu, uint64_t master, int which) {
    if (interp_bridge_lle_master_deadline_reached(cpu)) return false;
    const uint64_t deadline = soe_frame_driver_deadline(), end = cpu->master_cycles + master;
    if (!deadline || end < deadline) return true;
    if (end < soe_frame_driver_hard_deadline() && !soe_frame_driver_irq_possible()) { s_bind[which].across++; return true; }
    return false;
}

/* The ROM bytes, compiled: `first` runs the routine's first instruction, then the scheduler goes on at `body`, the
 * compiled root behind it, between the same two instructions as any compiled code. A deadline already reached at
 * the entry stops there first. `first` is NULL when the widths or the operand's place are not the ones it handles:
 * the interpreter then runs from the entry, as it does outside the scheduler (a nested run). */
static RecompReturn run_rom_bytes(CpuState *cpu, int which, uint16_t entry16, uint16_t body16, void (*first)(CpuState *)) {
    const uint32_t bank = (uint32_t)cpu->PB << 16, entry = bank | entry16;
    s_bind[which].compiled++;
    if (!interp_bridge_in_lle_scheduler()) return interp_tier_run_call_frame(cpu, entry, entry, 3, NULL);
    if (!first || interp_bridge_lle_master_deadline_reached(cpu)) return interp_bridge_lle_yield_unwind(cpu, entry);
    cpu_aot_insn_begin(cpu, entry16);
    cpu_mirrors_to_p(cpu);
    first(cpu);
    cpu_p_to_mirrors(cpu);
    return interp_bridge_lle_yield_unwind(cpu, bank | body16);
}

/* Registers the C sees and hands back. A, X and P are all a binding here changes. */
#ifdef SOE_RECONSTRUCTED_C
static void run_c(CpuState *cpu, int which, void (*bind)(soe_regs), const Cost *c) {
    soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
    bind(&r);
    cpu->A = s_bind[which].mutate ? (uint16_t)(r.a ^ 1) : r.a;
    cpu->X = r.x;
    cpu->P = r.p;
    cpu_p_to_mirrors(cpu);
    leave_rtl(cpu);
    charge(cpu, c);
    s_bind[which].c++;
}
static uint16_t dp_word(const CpuState *cpu, uint8_t offset) {
    const uint16_t a = (uint16_t)(cpu->D + offset);
    return (uint16_t)(g_ram[a] | g_ram[a + 1] << 8);
}
#endif

/* $80:859B, first instruction: LDA $0139. */
static void rng_first(CpuState *cpu) {
    const uint32_t a = (uint32_t)cpu->DB << 16 | 0x0139;
    Cost c = { 0 };
    insn(&c, 5, 3); word_at(&c, a);
    cpu->A = (uint16_t)(g_ram[0x0139] | g_ram[0x013A] << 8);
    set_nz16(cpu, cpu->A);
    cpu->open_bus = g_ram[0x013A];
    charge(cpu, &c);
}

/*
 * $80:859B random_number_generator (RTL; everscript-vscode knowledge/rom/80859b.md). Four times: LDA abs, ROL A,
 * BCC over an INC abs, ADC abs, STA abs; then RTL. The branches depend only on bit 15 of each rotated word, which
 * follow from the state at entry, so the path is known before the C runs.
 */
RecompReturn HleRandomNumberGenerator(CpuState *cpu) {
    stats_init();
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint32_t db = (uint32_t)cpu->DB << 16;
    if (wide_a(cpu) && !(cpu->P & CPU_P_D) && db_in_wram(cpu)) {
        static const uint16_t rot[4] = { 0x0139, 0x0137, 0x0135, 0x0133 };   /* each step: rotate, bump, add, store */
        static const uint16_t bump[4] = { 0x0137, 0x0135, 0x0133, 0x0139 };
        static const uint16_t add[4] = { 0x0135, 0x0133, 0x0137, 0x0139 };
        Cost c = { 0 };
        uint16_t bumped[4] = { 0 };   /* +1 a word got from an earlier step's INC, before it is rotated */
        for (int i = 0; i < 4; i++) {
            const uint16_t w = (uint16_t)(g_ram[rot[i]] | g_ram[rot[i] + 1] << 8);
            const bool carry = ((uint16_t)(w + bumped[i]) & 0x8000) != 0;
            insn(&c, 5, 3); word_at(&c, db | rot[i]);                                    /* LDA abs */
            implied(&c);                                                                 /* ROL A */
            branch(&c, !carry);                                                          /* BCC */
            if (carry) {                                                                 /* INC abs: read, write */
                insn(&c, 8, 3); word_at(&c, db | bump[i]); word_at(&c, db | bump[i]);
                for (int j = i + 1; j < 4; j++) if (rot[j] == bump[i]) bumped[j]++;
            }
            insn(&c, 5, 3); word_at(&c, db | add[i]);                                    /* ADC abs */
            insn(&c, 5, 3); word_at(&c, db | rot[i]);                                    /* STA abs */
        }
        rtl(&c, cpu);
        if (fits(cpu, master_of(&c, cpu), RNG)) {
            run_c(cpu, RNG, random_number_generator_bind, &c);
            return RECOMP_RETURN_NORMAL;
        }
    }
#endif
    return run_rom_bytes(cpu, RNG, 0x859B, 0x859E, wide_a(cpu) && db_in_wram(cpu) ? rng_first : NULL);
}

/* $8F:A914, first instruction: BIT #$2040 (immediate: Z only). */
static void plane_first(CpuState *cpu) {
    Cost c = { 0 };
    imm16(&c);
    cpu->P = (uint8_t)((cpu->P & ~CPU_P_Z) | (cpu->A & 0x2040 ? 0 : CPU_P_Z));
    cpu->open_bus = 0x20;
    charge(cpu, &c);
}

/*
 * $8F:A914 update_entity_plane (RTL; knowledge/rom/8fa914.md). The path follows from A alone: BIT #$2040, BNE;
 * then AND, STA $44 or BIT #$0040, BNE and AND, ORA, STA $44 or LDA #$0040, TRB $44.
 */
RecompReturn HleUpdateEntityPlane(CpuState *cpu) {
    stats_init();
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (wide_a(cpu) && dp_in_wram(cpu, 0x45)) {
        Cost c = { 0 };
        const uint16_t a = cpu->A;
        imm16(&c);                                                                       /* BIT #$2040 */
        branch(&c, a & 0x2040);
        if (!(a & 0x2040)) {
            imm16(&c); dp16(&c, cpu, 0x44);                                              /* AND, STA $44 */
        } else {
            imm16(&c);                                                                   /* BIT #$0040 */
            branch(&c, a & 0x0040);
            if (!(a & 0x0040)) {
                imm16(&c); imm16(&c); dp16(&c, cpu, 0x44);                               /* AND, ORA, STA $44 */
            } else {
                imm16(&c);                                                               /* LDA #$0040 */
                dp16(&c, cpu, 0x44); word_at(&c, (uint16_t)(cpu->D + 0x44)); insn(&c, 3, 0);   /* TRB $44: read, write */
            }
        }
        rtl(&c, cpu);
        if (fits(cpu, master_of(&c, cpu), PLANE)) {
            run_c(cpu, PLANE, update_entity_plane_bind, &c);
            return RECOMP_RETURN_NORMAL;
        }
    }
#endif
    return run_rom_bytes(cpu, PLANE, 0xA914, 0xA917, wide_a(cpu) ? plane_first : NULL);
}

/* $90:9DE8, first instruction: LDA $50. */
static void tile_first(CpuState *cpu) {
    const uint16_t a = (uint16_t)(cpu->D + 0x50);
    Cost c = { 0 };
    dp16(&c, cpu, 0x50);
    cpu->A = (uint16_t)(g_ram[a & 0x1FFF] | g_ram[(a + 1) & 0x1FFF] << 8);
    set_nz16(cpu, cpu->A);
    cpu->open_bus = (uint8_t)(cpu->A >> 8);
    charge(cpu, &c);
}

#ifdef SOE_RECONSTRUCTED_C
/* $90:9E2F and $90:9E3A on: the geometry of the word, ending in the routine's RTL. */
static void tile_table(Cost *c, uint16_t word) {
    imm16(c); implied(c); implied(c);                                                    /* AND, ASL A, TAX */
    insn(c, 6, 4); word_at(c, 0x909E73u + (uint32_t)((word & 0x000F) << 1));             /* LDA long,X */
}
static void tile_geometry(Cost *c, const CpuState *cpu, uint16_t word) {
    dp16(c, cpu, 0x50); imm16(c);                                                        /* LDA $50, BIT #$2000 */
    branch(c, word & 0x2000);
    if (word & 0x2000) tile_table(c, word);
    else imm16(c);                                                                       /* AND #$000F */
}
/* The path from $90:9DE8 to its RTL, instruction by instruction; the branches follow from DP $44, $4C, $50. */
static void tile_path(Cost *c, const CpuState *cpu) {
    const uint16_t word = dp_word(cpu, 0x50), plane = dp_word(cpu, 0x44), actor = dp_word(cpu, 0x4C);
    uint16_t a = word;
    bool solid = false;
    dp16(c, cpu, 0x50); imm16(c);                                                        /* LDA $50, BIT #$0100 */
    branch(c, !(word & 0x0100));
    if (word & 0x0100) {
        bool gate = true;
        a ^= 0x0300; imm16(c); imm16(c);                                                 /* EOR, BIT */
        branch(c, !(a & 0x0F00));
        if (!(a & 0x0F00)) {                                                             /* $9E19 */
            dp16(c, cpu, 0x4C); imm16(c);
            solid = actor < 0x4E89;
            branch(c, solid);
        } else {
            a ^= 0x0600; imm16(c); imm16(c);
            branch(c, !(a & 0x0F00));
            if (!(a & 0x0F00)) {                                                         /* $9E10 */
                dp16(c, cpu, 0x4C); imm16(c);
                solid = actor == 0x4F37;
                branch(c, solid);
                if (!solid) branch(c, true);                                             /* BRA $9E20 */
            } else {
                a ^= 0x0200; imm16(c); imm16(c);
                gate = !(a & 0x0F00);
                branch(c, !gate);                                                        /* BNE $9E22 */
                if (gate) {
                    dp16(c, cpu, 0x4C); imm16(c);
                    solid = actor >= 0x4E89;
                    branch(c, solid);
                    if (!solid) branch(c, true);                                         /* BRA $9E20 */
                }
            }
        }
        if (solid) { imm16(c); return; }                                                 /* $9E64 LDA #$000F */
        if (gate) { dp16(c, cpu, 0x50); a = word; }                                      /* $9E20 LDA $50 */
    }
    imm16(c);                                                                            /* $9E22 BIT #$0040 */
    branch(c, a & 0x0040);
    if (a & 0x0040) {                                                                    /* $9E44 EOR $44, AND */
        const bool differ = ((a ^ plane) & 0x0030) != 0;
        dp16(c, cpu, 0x44); imm16(c);
        branch(c, differ);
        if (differ) implied(c);                                                          /* $9E51 TDC */
        else { dp16(c, cpu, 0x50); imm16(c); }                                           /* LDA $50, AND */
        return;
    }
    const uint16_t d = (uint16_t)(a - plane) & 0x0030;
    implied(c); dp16(c, cpu, 0x44); imm16(c);                                            /* SEC, SBC $44, AND */
    branch(c, d);
    if (!d) { tile_geometry(c, cpu, word); return; }
    imm16(c);                                                                            /* $9E53 BIT #$0020 */
    branch(c, !(d & 0x0020));
    if (!(d & 0x0020)) {                                                                 /* $9E68 */
        dp16(c, cpu, 0x50); imm16(c);                                                    /* LDA $50, BIT #$2000 */
        branch(c, word & 0x2000);
        if (word & 0x2000) tile_table(c, word);
        else imm16(c);                                                                   /* LDA #$000F */
        return;
    }
    imm16(c);                                                                            /* BIT #$0010 */
    branch(c, !(d & 0x0010));
    if (d & 0x0010) {
        dp16(c, cpu, 0x44); imm16(c);                                                    /* LDA $44, BIT #$0040 */
        branch(c, plane & 0x0040);
        if (plane & 0x0040) { tile_geometry(c, cpu, word); return; }
    }
    imm16(c);                                                                            /* $9E64 LDA #$000F */
}
#endif

/* $90:9DE8 eval_tile_collision (RTL; knowledge/rom/909de8.md). */
RecompReturn HleEvalTileCollision(CpuState *cpu) {
    stats_init();
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (wide_a(cpu) && !cpu->x_flag && !(cpu->P & CPU_P_D) && dp_in_wram(cpu, 0x51)) {
        Cost c = { 0 };
        tile_path(&c, cpu);
        rtl(&c, cpu);
        if (fits(cpu, master_of(&c, cpu), TILE)) {
            run_c(cpu, TILE, eval_tile_collision_bind, &c);
            return RECOMP_RETURN_NORMAL;
        }
    }
#endif
    return run_rom_bytes(cpu, TILE, 0x9DE8, 0x9DEA, wide_a(cpu) && dp_in_wram(cpu, 0x51) ? tile_first : NULL);
}
