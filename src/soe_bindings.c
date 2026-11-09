/*
 * Routines replaced with everscript-vscode's reconstructed C (`hle_func`, docs/game-rtl.md §4b).
 *
 * The C is compiled from src/bound-c/, exported from everscript-vscode's reconstructed-c (CMake: SOE_RECONSTRUCTED_C).
 * It is the documented code, never edited for us: this file only adapts the machine to it. A
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
#include "soe_cost.h"
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "soe_ws_alive.h"

#ifdef SOE_RECONSTRUCTED_C
#define SOE_COST
#include "soe.h"

/* soe.h's host bus: WRAM through any bank that maps it, and HiROM for read-only descriptor tables. */
uint8_t *soe_bus(uint32_t addr) {
    const uint8_t bank = (uint8_t)(addr >> 16);
    if (bank == 0x7E || bank == 0x7F) return &g_ram[addr & 0x1FFFF];
    if (!(bank & 0x40) && (addr & 0xFFFF) < 0x2000) return &g_ram[addr & 0x1FFF];
    if ((bank & 0x40) || (addr & 0x8000)) return (uint8_t *)&g_rom[(addr & 0x3F0000) | (addr & 0xFFFF)];
    fprintf(stderr, "soe_bindings: bus access $%06X outside WRAM/ROM\n", (unsigned)addr);
    abort();
}

void random_number_generator_bind(soe_regs r);
void update_entity_plane_bind(soe_regs r);
void eval_tile_collision_bind(soe_regs r);
void init_entity_pool_bind(soe_regs r);
void entity_sprite_depth_bind(soe_regs r);
void vram_dma_upload_tile_block_bind(soe_regs r);
void map_anim_tile_channel_tick_and_vram_stream_bind(soe_regs r);
void oam_depth_sorted_sprite_insert_node_bind(soe_regs r);
void sprite_sorting_canopy_occlusion_and_render_depth_bind(soe_regs r);
void direction_index_from_signed_byte_pair_bind(soe_regs r);
void anim_start_record_bind(soe_regs r);
void companion_waypoint_history_poll_bind(soe_regs r);
void audio_master_volume_fade_tick_bind(soe_regs r);
void elevation_vector_shear_step_bind(soe_regs r);
void elevation_manhattan_distance_eval_bind(soe_regs r);
void elevation_compass_direction_resolve_bind(soe_regs r);
#include "raycast.h"
#include "spare_time.h"

/* The raycast's STA $004202 and LDA $004216, in the registers the game can read back. */
uint16_t soe_multiply(uint16_t factors) {
    g_snes->multiplyA = (uint8_t)factors;
    g_snes->multiplyResult = (uint16_t)((factors & 0xFF) * (factors >> 8));
    return g_snes->multiplyResult;
}
#endif

enum {
    RNG, PLANE, TILE, POOL, DEPTH, RAY, SPARE, WAIT, DMA, ANIM, OAM_INSERT, SPRITE_SORT, DIR_PAIR, ANIM_START, WAYPOINT_POLL,
    AUDIO_FADE, SHEAR_STEP, MANHATTAN_DIST, COMPASS_DIR, SLEEP_WAKE, FREE_BEGIN, FREE_END, PICK_TARGET, CHECK_AGGRO, STATIONARY_PLAYER, SPAWNER,
    BINDINGS
};
static struct { const char *name; unsigned long long c, across, compiled; bool mutate; } s_bind[BINDINGS] = {
    { "random_number_generator" }, { "update_entity_plane" }, { "eval_tile_collision" },
    { "init_entity_pool" }, { "entity_sprite_depth" }, { "companion_metatile_raycast_line_stepper" },
    { "field_spare_time_pass" }, { "wait_for_nmi_pass" },
    { "vram_dma_upload_tile_block" }, { "map_anim_tile_channel_tick_and_vram_stream" },
    { "oam_depth_sorted_sprite_insert_node" }, { "sprite_sorting_canopy_occlusion_and_render_depth" },
    { "direction_index_from_signed_byte_pair" }, { "anim_start_record" }, { "companion_waypoint_history_poll" },
    { "audio_master_volume_fade_tick" }, { "elevation_vector_shear_step" },
    { "elevation_manhattan_distance_eval" }, { "elevation_compass_direction_resolve" }, { "entity_sleep_wake" },
    { "field_tick_free_begin" }, { "field_tick_free_end" },
    { "ai_pick_target" }, { "enemy_ai_check_aggro" }, { "ai_stationary_player" }, { "spawner" },
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
/* LDA/STA/... abs,X or abs,Y with 16-bit A and index: the index always costs its cycle. */
static void abs_i16(Cost *c, uint32_t addr24) { insn(c, 6, 3); word_at(c, addr24); }
static void abs16(Cost *c, uint32_t addr24) { insn(c, 5, 3); word_at(c, addr24); }
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
/* The RTL/RTS at the end of the C path: S, the open bus its last read leaves, and when a tail transfer from a
 * frameless-resumed routine reached this helper, the unwind back to the scheduler at the popped return address. */
static RecompReturn leave_rtl(CpuState *cpu) {
    const uint16_t s0 = cpu->S;
    const uint32_t rpc = ((g_ram[(uint16_t)(s0 + 1)] | (uint16_t)g_ram[(uint16_t)(s0 + 2)] << 8) + 1u) & 0xFFFFu;
    const uint8_t rpb = g_ram[(uint16_t)(s0 + 3)];
    cpu->open_bus = rpb;
    cpu->PB = rpb;
    cpu->S = (uint16_t)(s0 + 3);
    if (interp_bridge_return_targets_owner(s0, cpu->S) || (interp_bridge_in_lle_scheduler() && rpc == 0x9821u))
        return interp_bridge_lle_yield_unwind(cpu, (uint32_t)rpb << 16 | rpc);
    return RECOMP_RETURN_NORMAL;
}
static RecompReturn leave_rts(CpuState *cpu) {
    const uint16_t s0 = cpu->S;
    const uint32_t rpc = ((g_ram[(uint16_t)(s0 + 1)] | (uint16_t)g_ram[(uint16_t)(s0 + 2)] << 8) + 1u) & 0xFFFFu;
    cpu->open_bus = g_ram[(uint16_t)(s0 + 2)];
    cpu->S = (uint16_t)(s0 + 2);
    if (interp_bridge_return_targets_owner(s0, cpu->S))
        return interp_bridge_lle_yield_unwind(cpu, (uint32_t)cpu->PB << 16 | rpc);
    return RECOMP_RETURN_NORMAL;
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

/* The next step of a long routine: past refresh stops as `fits`, whose first check it has passed once already. */
static bool fits_more(uint64_t master) {
    const uint64_t end = g_cpu.master_cycles + master;
    return soe_frame_driver_deadline() == 0 || (end < soe_frame_driver_hard_deadline() && !soe_frame_driver_irq_possible());
}

/* The ROM bytes, compiled: `first` runs the routine's first instruction, then the scheduler goes on at `body`, the
 * compiled root behind it, between the same two instructions as any compiled code. A deadline already reached at
 * the entry stops there first. `first` is NULL when the widths or the operand's place are not the ones it handles:
 * the interpreter then runs from the entry, as it does outside the scheduler (a nested run). */
static RecompReturn run_rom_bytes_frame(CpuState *cpu, int which, uint16_t entry16, uint16_t body16, void (*first)(CpuState *), int frame) {
    const uint32_t bank = (uint32_t)cpu->PB << 16, entry = bank | entry16;
    s_bind[which].compiled++;
    if (!interp_bridge_in_lle_scheduler()) return interp_tier_run_call_frame(cpu, entry, entry, frame, NULL);
    if (!first || interp_bridge_lle_master_deadline_reached(cpu)) return interp_bridge_lle_yield_unwind(cpu, entry);
    cpu_aot_insn_begin(cpu, entry16);
    cpu_mirrors_to_p(cpu);
    first(cpu);
    cpu_p_to_mirrors(cpu);
    return interp_bridge_lle_yield_unwind(cpu, bank | body16);
}
static RecompReturn run_rom_bytes(CpuState *cpu, int which, uint16_t entry16, uint16_t body16, void (*first)(CpuState *)) {
    return run_rom_bytes_frame(cpu, which, entry16, body16, first, 3);
}

/* Registers the C sees and hands back. A, X and P are all a binding here changes. */
#ifdef SOE_RECONSTRUCTED_C
static void run_c(CpuState *cpu, int which, void (*bind)(soe_regs), const Cost *c) {
    soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
    bind(&r);
    cpu->A = s_bind[which].mutate ? (uint16_t)(r.a ^ 1) : r.a;
    cpu->X = r.x;
    cpu->Y = r.y;
    cpu->P = r.p;
    cpu_p_to_mirrors(cpu);
    leave_rtl(cpu);
    charge(cpu, c);
    s_bind[which].c++;
}
/* run_c with the marks' cost held against the one worked out by hand: they must agree on every call. */
static void run_checked(CpuState *cpu, int which, void (*bind)(soe_regs), const Cost *c, uint32_t entry) {
    SoeCostTrace t;
    soe_cost_begin(entry, cpu->D, cpu->DB, wide_a(cpu), !cpu->x_flag, cpu->S);
    run_c(cpu, which, bind, c);
    soe_cost_end(&t);
    if (t.cycles != c->cycles || t.fetches != c->fetches || t.data != c->data) {
        fprintf(stderr, "soe_cost: %s %llu/%llu/%llu, by hand %llu/%llu/%llu\n", s_bind[which].name, (unsigned long long)t.cycles,
                (unsigned long long)t.fetches, (unsigned long long)t.data, (unsigned long long)c->cycles,
                (unsigned long long)c->fetches, (unsigned long long)c->data);
        abort();
    }
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
            run_checked(cpu, PLANE, update_entity_plane_bind, &c, 0x8FA914);
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
            run_checked(cpu, TILE, eval_tile_collision_bind, &c, 0x909DE8);
            return RECOMP_RETURN_NORMAL;
        }
    }
#endif
    return run_rom_bytes(cpu, TILE, 0x9DE8, 0x9DEA, wide_a(cpu) && dp_in_wram(cpu, 0x51) ? tile_first : NULL);
}

/* $80:9685, first instruction: PHB. */
static void pool_first(CpuState *cpu) {
    Cost c = { 0 };
    insn(&c, 3, 1); c.data += extra(cpu->S);
    g_ram[cpu->S] = cpu->DB;
    cpu->S = (uint16_t)(cpu->S - 1);
    cpu->open_bus = cpu->DB;
    charge(cpu, &c);
}

/*
 * $80:9685 init_entity_pool (RTL; knowledge/routines/init_entity_pool.md). One path: 16 passes of a fixed loop. The stack
 * bytes PHB and PEA leave below S are the asm's, not the C's: WRAM must hold them as the ROM leaves them.
 */
RecompReturn HleInitEntityPool(CpuState *cpu) {
    stats_init();
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (wide_a(cpu) && !cpu->x_flag && !(cpu->P & CPU_P_D) && cpu->S < 0x2000 && cpu->S > 2) {
        const uint32_t pool = 0x7E0000u | 0x50A7;
        const uint16_t s = cpu->S;
        Cost c = { 0 };
        insn(&c, 3, 1); c.data += extra(s);                                              /* PHB */
        insn(&c, 5, 3); c.data += extra((uint16_t)(s - 1)) + extra((uint16_t)(s - 2));   /* PEA #$7E7E */
        insn(&c, 4, 1); c.data += extra((uint16_t)(s - 2));                              /* PLB */
        insn(&c, 4, 1); c.data += extra((uint16_t)(s - 1));                              /* PLB */
        imm16(&c); imm16(&c); abs_i16(&c, pool);                                         /* LDX, LDA, STA $0004,x */
        implied(&c); imm16(&c);                                                          /* CLC, LDA */
        for (int pass = 0; pass < 16; pass++) {
            implied(&c); abs_i16(&c, pool);                                              /* TAY, STA $0002,x */
            implied(&c); abs_i16(&c, pool); abs_i16(&c, pool);                           /* TXA, STA $0006,x, STA $0000,y */
            imm16(&c); abs_i16(&c, pool);                                                /* LDA, STA $0004,y */
            implied(&c); implied(&c); imm16(&c); imm16(&c);                              /* TYX, TYA, ADC, CMP */
            branch(&c, pass < 15);                                                       /* BNE */
        }
        imm16(&c); abs16(&c, pool);                                                      /* LDA, STA $1276 */
        insn(&c, 4, 1); c.data += extra(s);                                              /* PLB */
        rtl(&c, cpu);
        if (fits(cpu, master_of(&c, cpu), POOL)) {
            g_ram[s] = cpu->DB; g_ram[(uint16_t)(s - 1)] = 0x7E; g_ram[(uint16_t)(s - 2)] = 0x7E;
            run_c(cpu, POOL, init_entity_pool_bind, &c);
            if (s_bind[POOL].mutate) g_ram[0x50AF + 2] ^= 8;                             /* A is dead here: corrupt a link */
            return RECOMP_RETURN_NORMAL;
        }
    }
#endif
    return run_rom_bytes(cpu, POOL, 0x9685, 0x9686, cpu->S < 0x2000 ? pool_first : NULL);
}

/* $8F:C773, first instruction: LDA $0010,y. */
static void depth_first(CpuState *cpu) {
    const uint16_t a = (uint16_t)(cpu->Y + 0x0010);
    Cost c = { 0 };
    abs_i16(&c, 0x7E0000u | a);
    cpu->A = (uint16_t)(g_ram[a] | g_ram[a + 1] << 8);
    set_nz16(cpu, cpu->A);
    cpu->open_bus = (uint8_t)(cpu->A >> 8);
    charge(cpu, &c);
}

#ifdef SOE_RECONSTRUCTED_C
/* The path of $8F:C773: every branch follows from the record, DP and two words of WRAM, read before the C runs. */
static void depth_path(Cost *c, const CpuState *cpu) {
    const uint16_t y = cpu->Y, d = cpu->D;
    const uint32_t bank = 0x7E0000u;
    #define REC(o) ((uint16_t)(g_ram[(uint16_t)(y + (o))] | g_ram[(uint16_t)(y + (o)) + 1] << 8))
    #define REC_AT(o) (bank | (uint16_t)(y + (o)))
    enum { CC30, ATTR };
    int to = -1;
    abs_i16(c, REC_AT(0x10)); imm16(c);                                                  /* LDA, BIT #$0400 */
    branch(c, !(REC(0x10) & 0x0400));
    if (REC(0x10) & 0x0400) {
        const uint16_t v = (uint16_t)(g_ram[0x23C1] | g_ram[0x23C2] << 8);
        abs16(c, bank | 0x23C1);
        branch(c, v);
        if (v) to = CC30;
    }
    if (to < 0) {
        const uint16_t x = REC(0x3C), a = (uint16_t)(x ^ 0x0800);
        abs_i16(c, REC_AT(0x3C)); implied(c); imm16(c); imm16(c);                        /* LDA, TAX, EOR, BIT */
        branch(c, a & 0x0F00);
        if (!(a & 0x0F00)) {                                                             /* TDC, STA $08, STA $0084,y */
            implied(c); dp16(c, cpu, 0x08); abs_i16(c, REC_AT(0x84));
            return;
        }
        const uint16_t mine = a & 0x0030, plane = REC(0x18) & 0x0030;
        imm16(c); dp16(c, cpu, 0x12);                                                    /* AND, STA $12 */
        abs_i16(c, REC_AT(0x18)); imm16(c); dp16(c, cpu, 0x12);                          /* LDA, AND, CMP $12 */
        branch(c, plane == mine);
        if (plane == mine) {
            implied(c); imm16(c);                                                        /* TXA, BIT #$1000 */
            branch(c, x & 0x1000);
            if (x & 0x1000) to = CC30;
            else {
                const bool fc20 = ((x ^ 0x0400) & 0x0F00) == 0;
                imm16(c); imm16(c);                                                      /* EOR, BIT */
                branch(c, fc20);
                imm16(c); branch(c, true);                                               /* LDA, BRA */
                to = ATTR;
            }
        } else {
            const bool negative = ((uint16_t)(plane - mine) & 0x8000) != 0;
            branch(c, negative);
            if (negative) to = CC30;
            else { imm16(c); branch(c, true); to = ATTR; }                               /* LDA, BRA */
        }
    }
    if (to == CC30) imm16(c);                                                            /* $C7C1 LDA #$CC30 */
    dp16(c, cpu, 0x06);                                                                  /* STA $06 */
    abs_i16(c, REC_AT(0x0C));
    branch(c, REC(0x0C));
    if (!REC(0x0C)) imm16(c);                                                            /* ORA #$0200 */
    dp16(c, cpu, 0x06); word_at(c, (uint16_t)(d + 0x06)); insn(c, 3, 0);                 /* TSB $06: read, write */
    abs_i16(c, REC_AT(0x1A)); implied(c); dp16(c, cpu, 0x59); dp16(c, cpu, 0x02);        /* LDA, SEC, SBC $59, STA $02 */
    abs_i16(c, REC_AT(0x1C)); implied(c); dp16(c, cpu, 0x5B); dp16(c, cpu, 0x04);        /* LDA, SEC, SBC $5B, STA $04 */
    uint16_t y_on_screen = (uint16_t)(REC(0x1C) - dp_word(cpu, 0x5B));
    abs_i16(c, REC_AT(0x18)); imm16(c);                                                  /* LDA, AND */
    uint16_t a = REC(0x18) & 0x0030;
    branch(c, a);
    if (!a) {
        const uint16_t mode = (uint16_t)(g_ram[0x241F] | g_ram[0x2420] << 8);
        insn(c, 6, 4); word_at(c, bank | 0x241F); imm16(c);                              /* LDA $7E241F, CMP #2 */
        branch(c, mode != 2);
        if (mode == 2) {
            abs_i16(c, REC_AT(0x1A)); implied(c); abs16(c, bank | 0x010E); dp16(c, cpu, 0x02);
            abs_i16(c, REC_AT(0x1C)); implied(c); abs16(c, bank | 0x0110); implied(c); dp16(c, cpu, 0x04);
            y_on_screen = (uint16_t)(REC(0x1C) - (uint16_t)(g_ram[0x0110] | g_ram[0x0111] << 8) + 1);
        }
        implied(c);                                                                      /* TDC */
        a = d;
    }
    a ^= 0x0030;
    const unsigned shifted = (unsigned)a << 1;                                           /* ASL: the carry goes into ADC */
    const unsigned sum = (shifted & 0xFFFF) + 0x0040 + (shifted >> 16);
    const uint16_t total = (uint16_t)((sum & 0xFFFF) + y_on_screen + (sum >> 16));
    imm16(c); implied(c); imm16(c); dp16(c, cpu, 0x04); imm16(c);                        /* EOR, ASL, ADC, ADC $04, BIT */
    branch(c, total & 0xFE00);
    if (total & 0xFE00) implied(c);                                                      /* TDC */
    else { implied(c); imm16(c); }                                                       /* CLC, ADC #$0080 */
    dp16(c, cpu, 0x08); abs_i16(c, REC_AT(0x84));                                        /* STA $08, STA $0084,y */
    #undef REC
    #undef REC_AT
}
#endif

/* $8F:C773 entity_sprite_depth (RTL; knowledge/routines/entity_sprite_depth.md). Records live in bank $7E. */
RecompReturn HleEntitySpriteDepth(CpuState *cpu) {
    stats_init();
    const bool in_bank = cpu->DB == 0x7E && (uint32_t)cpu->Y + 0x90 < 0x10000u;
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (wide_a(cpu) && !cpu->x_flag && !(cpu->P & CPU_P_D) && in_bank && dp_in_wram(cpu, 0x5C)) {
        Cost c = { 0 };
        depth_path(&c, cpu);
        rtl(&c, cpu);
        if (fits(cpu, master_of(&c, cpu), DEPTH)) {
            run_checked(cpu, DEPTH, entity_sprite_depth_bind, &c, 0x8FC773);
            if (s_bind[DEPTH].mutate) g_ram[(uint16_t)(cpu->Y + 0x84)] ^= 1;             /* A is dead here: corrupt the depth */
            return RECOMP_RETURN_NORMAL;
        }
    }
#endif
    return run_rom_bytes(cpu, DEPTH, 0xC773, 0xC776, wide_a(cpu) && !cpu->x_flag && in_bank ? depth_first : NULL);
}

/* $8F:9F05, first instruction: SEP #$30. */
static void ray_first(CpuState *cpu) {
    Cost c = { 0 };
    insn(&c, 3, 2);
    cpu->P |= CPU_P_M | CPU_P_X;
    cpu->X &= 0xFF;
    cpu->Y &= 0xFF;
    cpu->open_bus = 0x30;
    charge(cpu, &c);
}

#ifdef SOE_RECONSTRUCTED_C
/* Far more than any one step's clocks: a step fits when this does. */
#define RAY_STEP_BOUND 6000u
/* The same for a pass of field_spare_time_loop and a task's head, about 1,100 at their longest. */
#define SPARE_PASS_BOUND 1500u
/* And for a pass of wait_for_nmi, about 80. */
#define WAIT_PASS_BOUND 200u

static void charge_trace(CpuState *cpu) {
    SoeCostTrace t;
    soe_cost_take(&t);
    const Cost c = { t.cycles, t.fetches, t.data };
    charge(cpu, &c);
    soe_frame_driver_settle_refresh();
}
static void take_regs(CpuState *cpu, const soe_regs_t *r) {
    cpu->A = r->a; cpu->X = r->x; cpu->Y = r->y; cpu->P = r->p;
    cpu_p_to_mirrors(cpu);
}
#endif

/*
 * $8F:9F05 companion_metatile_raycast_line_stepper (RTS; knowledge/routines). A walk can outlast a slice, so its C runs a step
 * at a time and each step is charged by its marks. Everything a step carries over is in WRAM or on the stack, kept here as
 * the asm keeps it, so at $9F43 the compiled ROM code can take over between two instructions when the next step might not fit.
 */
RecompReturn HleRaycast(CpuState *cpu) {
    stats_init();
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint16_t s0 = cpu->S;
    if (interp_bridge_in_lle_scheduler() && !(cpu->P & CPU_P_D) && dp_in_wram(cpu, 0x6B) && db_in_wram(cpu)
        && (g_ram[(uint16_t)(cpu->D + 0x58)] & 0xFE) == 0x7E && s0 > 16 && s0 < 0x1FF0
        && fits(cpu, RAY_STEP_BOUND, RAY)) {
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        RaycastLoop l;
        SoeCostTrace t;
        const bool m16 = wide_a(cpu), x16 = !cpu->x_flag;
        soe_cost_begin(0x8F9F05, cpu->D, cpu->DB, m16, x16, s0);
        bool go = raycast_start(&r, &l);
        if (go) {
            g_ram[s0] = (uint8_t)(l.direction >> 8); g_ram[(uint16_t)(s0 - 1)] = (uint8_t)l.direction;   /* PHX, PHY */
            g_ram[(uint16_t)(s0 - 2)] = (uint8_t)(l.steps >> 8); g_ram[(uint16_t)(s0 - 3)] = (uint8_t)l.steps;
            soe_at(0x9F43);
            charge_trace(cpu);
            cpu->S = (uint16_t)(s0 - 4);
            while (go) {
                if (!fits_more(RAY_STEP_BOUND) || (l.direction & 1) || l.direction > 0xE) {
                    take_regs(cpu, &r);
                    s_bind[RAY].compiled++;
                    return interp_bridge_lle_yield_unwind(cpu, 0x8F9F43);
                }
                go = raycast_step(&r, &l);
                if (go) {
                    g_ram[(uint16_t)(s0 - 3)] = (uint8_t)l.steps; g_ram[(uint16_t)(s0 - 2)] = (uint8_t)(l.steps >> 8);   /* PHA */
                    charge_trace(cpu);
                }
            }
        }
        soe_cost_end(&t);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        cpu->S = s0;
        cpu->open_bus = g_ram[(uint16_t)(s0 + 2)];
        cpu->S = (uint16_t)(s0 + 2);
        charge(cpu, &c);
        if (s_bind[RAY].mutate) g_ram[(uint16_t)(cpu->D + 0x66)] ^= 1;
        s_bind[RAY].c++;
        return RECOMP_RETURN_NORMAL;
    }
#endif
    return run_rom_bytes(cpu, RAY, 0x9F05, 0x9F07, ray_first);
}

/* $8F:9821, first instruction: LDA $0100. */
static void spare_first(CpuState *cpu) {
    const uint32_t a = (uint32_t)cpu->DB << 16 | 0x0100;
    Cost c = { 0 };
    insn(&c, 5, 3); word_at(&c, a);
    cpu->A = (uint16_t)(g_ram[0x0100] | g_ram[0x0101] << 8);
    set_nz16(cpu, cpu->A);
    cpu->open_bus = g_ram[0x0101];
    charge(cpu, &c);
}

/*
 * $8F:9821, the head of field_spare_time_loop (knowledge/routines). Nothing but the NMI ends the loop, and the NMI comes
 * between slices, so a pass in C cannot see the frame counter move: the C runs passes while one fits before the next stop, and
 * the compiled ROM code from $9824 runs the passes around the NMI and sees the new frame. The head of a task is C too; the rest
 * of a task goes on in compiled code inside the routine (an entry has no resume case), and its return to $9821 comes back here.
 */
RecompReturn HleSpareTime(CpuState *cpu) {
    stats_init();
    soe_frame_driver_free_time(false);
    if (!interp_bridge_in_lle_scheduler()) {
        fprintf(stderr, "soe_bindings: field_spare_time_loop outside the scheduler\n");
        abort();
    }
    const bool plain = wide_a(cpu) && !cpu->x_flag && cpu->DB == 0x7E;
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint16_t s = cpu->S;
    const uint16_t frame = (uint16_t)(g_ram[(uint16_t)(s + 1)] | g_ram[(uint16_t)(s + 2)] << 8);
    if (plain && !(cpu->P & CPU_P_D) && dp_in_wram(cpu, 0x4F) && s > 16 && s < 0x1FF0
        && frame == (uint16_t)(g_ram[0x0100] | g_ram[0x0101] << 8)
        && soe_frame_driver_deadline() && fits(cpu, SPARE_PASS_BOUND, SPARE)) {
        const uint32_t bank = (uint32_t)cpu->PB << 16;
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        soe_cost_begin(bank | 0x9821, cpu->D, cpu->DB, true, true, s);
        soe_at(0x9821);
        for (;;) {
            const uint16_t next = field_spare_time_pass(&r, frame);
            charge_trace(cpu);
            s_bind[SPARE].c++;
            if (next == SPARE_NEW_FRAME) {
                fprintf(stderr, "soe_bindings: the frame counter moved inside a C pass\n");
                abort();
            }
            if (next != SPARE_NEXT) {
                g_ram[s] = cpu->PB;                                                      /* PHK, PEA $9820 */
                g_ram[(uint16_t)(s - 1)] = 0x98;
                g_ram[(uint16_t)(s - 2)] = 0x20;
                const uint16_t rest = field_spare_time_task(&r, next);
                charge_trace(cpu);
                if (rest != SPARE_NEXT) {
                    cpu->S = (uint16_t)(s - 3);
                    take_regs(cpu, &r);
                    if (s_bind[SPARE].mutate) g_ram[0x3BC5] ^= 2;                        /* the next task */
                    return interp_bridge_lle_yield_unwind(cpu, bank | rest);
                }
            }
            if (!soe_frame_driver_deadline() || !fits_more(SPARE_PASS_BOUND)) break;
        }
        take_regs(cpu, &r);
    }
#endif
    return run_rom_bytes(cpu, SPARE, 0x9821, 0x9824, plain ? spare_first : NULL);
}

/* $80:858F, first instruction: CMP $0100. */
static void wait_first(CpuState *cpu) {
    const uint32_t a = (uint32_t)cpu->DB << 16 | 0x0100;
    const uint16_t m = (uint16_t)(g_ram[0x0100] | g_ram[0x0101] << 8), d = (uint16_t)(cpu->A - m);
    Cost c = { 0 };
    insn(&c, 5, 3); word_at(&c, a);
    set_nz16(cpu, d);
    cpu->P = (uint8_t)((cpu->P & ~CPU_P_C) | (cpu->A >= m ? CPU_P_C : 0));
    cpu->open_bus = g_ram[0x0101];
    charge(cpu, &c);
}

/*
 * $80:858F, the spin of wait_for_nmi (knowledge/routines). As in HleSpareTime the NMI comes between slices: the C spins while a
 * pass fits before the next stop, and the compiled ROM code from $8592 runs the last passes and sees the counter move.
 */
RecompReturn HleWaitForNmi(CpuState *cpu) {
    stats_init();
    soe_frame_driver_free_time(false);
    if (!interp_bridge_in_lle_scheduler()) {
        fprintf(stderr, "soe_bindings: wait_for_nmi outside the scheduler\n");
        abort();
    }
    const bool plain = wide_a(cpu) && db_in_wram(cpu);
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (plain && cpu->A == (uint16_t)(g_ram[0x0100] | g_ram[0x0101] << 8) && soe_frame_driver_deadline()
        && fits(cpu, WAIT_PASS_BOUND, WAIT)) {
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0x858F, cpu->D, cpu->DB, true, !cpu->x_flag, cpu->S);
        soe_at(0x858F);
        do {
            if (!wait_for_nmi_pass(&r)) {
                fprintf(stderr, "soe_bindings: the frame counter moved inside a C pass\n");
                abort();
            }
            charge_trace(cpu);
            s_bind[WAIT].c++;
        } while (soe_frame_driver_deadline() && fits_more(WAIT_PASS_BOUND));
        take_regs(cpu, &r);
        if (s_bind[WAIT].mutate) cpu->master_cycles += 6;                                /* a late NMI */
    }
#endif
    return run_rom_bytes(cpu, WAIT, 0x858F, 0x8592, plain ? wait_first : NULL);
}

/* $80:8650, first instruction: SEP #$20. */
static void dma_first(CpuState *cpu) {
    Cost c = { 0 };
    insn(&c, 3, 2);
    cpu->P |= CPU_P_M;
    cpu->open_bus = 0x20;
    charge(cpu, &c);
}

/*
 * $80:8650 vram_dma_upload_tile_block (RTL; knowledge/routines/vram_dma_upload_tile_block.md). During active display
 * ($0106 bit 7 clear) the routine only stages an 8-byte descriptor into the WRAM ring buffer at $013F,x and advances
 * $013D; during forced blank ($0106 bit 7 set) it writes PPU/DMA registers directly, which stays in compiled ROM code.
 */
RecompReturn HleVramDmaUploadTileBlock(CpuState *cpu) {
    stats_init();
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint32_t db = (uint32_t)cpu->DB << 16;
    if (!cpu->emulation && !cpu->x_flag && !(cpu->P & CPU_P_D) && db_in_wram(cpu) && dp_in_wram(cpu, 0x2F)
        && !(g_ram[0x0106] & 0x80)) {
        Cost c = { 0 };
        insn(&c, 3, 2);                                                                  /* SEP #$20 */
        insn(&c, 4, 3); c.data += extra(db | 0x0106);                                    /* LDA $0106 */
        branch(&c, true);                                                                /* BPL $8686 */
        dp16(&c, cpu, 0x12);                                                             /* STX $12 */
        abs16(&c, db | 0x013D);                                                          /* LDX $013D */
        insn(&c, 2, 2);                                                                  /* LDA #$80 */
        insn(&c, 5, 3); c.data += extra(db | 0x013F);                                    /* STA $013F,x */
        insn(&c, (cpu->D & 0xFF) ? 4 : 3, 2); c.data += extra((uint16_t)(cpu->D + 0x28));/* LDA $28 */
        insn(&c, 5, 3); c.data += extra(db | 0x0142);                                    /* STA $0142,x */
        insn(&c, 3, 2);                                                                  /* REP #$30 */
        dp16(&c, cpu, 0x26); abs_i16(&c, db | 0x0140);                                   /* LDA $26, STA $0140,x */
        dp16(&c, cpu, 0x2E); abs_i16(&c, db | 0x0143);                                   /* LDA $2E, STA $0143,x */
        dp16(&c, cpu, 0x12); abs_i16(&c, db | 0x0145);                                   /* LDA $12, STA $0145,x */
        implied(&c); implied(&c); imm16(&c); imm16(&c);                                  /* TXA, CLC, ADC #$0008, AND #$01FF */
        abs16(&c, db | 0x013D);                                                          /* STA $013D */
        rtl(&c, cpu);
        if (fits(cpu, master_of(&c, cpu), DMA)) {
            run_checked(cpu, DMA, vram_dma_upload_tile_block_bind, &c, (uint32_t)cpu->PB << 16 | 0x8650);
            if (s_bind[DMA].mutate) g_ram[0x013D] ^= 8;
            return RECOMP_RETURN_NORMAL;
        }
    }
#endif
    return run_rom_bytes(cpu, DMA, 0x8650, 0x8652, !cpu->emulation ? dma_first : NULL);
}

/*
 * $90:A1B4 map_anim_tile_channel_tick_and_vram_stream (RTL; knowledge/routines/map_anim_tile_channel_tick_and_vram_stream.md).
 * Ticks the active room's background tile animation channels and queues expired 16x16 tiles for VRAM DMA via $80:8650.
 * When active display is on ($0106 bit 7 clear), all reads/writes are in WRAM and ROM, and the path is charged from the
 * SOE_AT marks across both $90:A1B4 and nested $80:8650 calls.
 */
RecompReturn HleMapAnimTileChannelTick(CpuState *cpu) {
    stats_init();
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint16_t s0 = cpu->S;
    if (wide_a(cpu) && !cpu->x_flag && !(cpu->P & CPU_P_D) && dp_in_wram(cpu, 0xDA) && s0 > 16 && s0 < 0x1FF0
        && !(g_ram[0x0106] & 0x80)) {
        const uint16_t count = (uint16_t)(g_ram[0x106C] | g_ram[0x106D] << 8);
        const uint8_t desc_bank = g_ram[(uint16_t)(cpu->D + 0xA9)];
        const uint16_t desc_off = dp_word(cpu, 0xA7);
        const bool desc_in_rom = (desc_bank & 0x40) != 0 || desc_off >= 0x8000;
        if (count <= 50 && g_ram[0x504A + count] != 0 && desc_in_rom) {
            int updates = 0, last_ch = -1;
            for (uint16_t ch = 0; ch < count && updates < 10; ch++) {
                if (g_ram[0x504A + ch] != 0 && ((uint8_t)(g_ram[0x5018 + ch] - 1) & 0x80) != 0) {
                    last_ch = (int)ch;
                    updates++;
                }
            }
            const uint64_t bound = 500u + 250u * count + 2600u * (unsigned)updates;
            if (fits(cpu, bound, ANIM)) {
                soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
                SoeCostTrace t;
                soe_cost_begin((uint32_t)cpu->PB << 16 | 0xA1B4, cpu->D, cpu->DB, true, true, s0);
                map_anim_tile_channel_tick_and_vram_stream_bind(&r);
                soe_cost_end(&t);
                if (last_ch >= 0) {
                    g_ram[(uint16_t)(s0 - 1)] = (uint8_t)(last_ch >> 8);
                    g_ram[(uint16_t)(s0 - 2)] = (uint8_t)last_ch;
                }
                const Cost c = { t.cycles, t.fetches, t.data };
                take_regs(cpu, &r);
                charge(cpu, &c);
                if (s_bind[ANIM].mutate) g_ram[(uint16_t)(cpu->D + 0xD8)] ^= 4;
                s_bind[ANIM].c++;
                if (last_ch >= 0) s_bind[DMA].c += (unsigned long long)(2 * updates);
                return leave_rtl(cpu);
            }
        }
    }
#endif
    return run_rom_bytes(cpu, ANIM, 0xA1B4, 0xA1B5, cpu->S < 0x2000 ? pool_first : NULL);
}

/*
 * $80:960A oam_depth_sorted_sprite_insert_node (RTL; knowledge/routines/oam_depth_sorted_sprite_insert_node.md).
 * Allocates a 15-byte OAM sprite node from the free pool at $7E1276 and inserts it into the depth-sorted doubly
 * linked list at $7E1278 in bank $7E. Charged from SOE_AT marks.
 */
RecompReturn HleOamInsertNode(CpuState *cpu) {
    stats_init();
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint16_t s0 = cpu->S;
    if (wide_a(cpu) && !cpu->x_flag && !(cpu->P & CPU_P_D) && dp_in_wram(cpu, 0x15) && s0 > 8 && s0 < 0x1FF8
        && (uint16_t)(g_ram[0x1276] | g_ram[0x1277] << 8) < 0xC000u
        && (uint16_t)(g_ram[0x1278] | g_ram[0x1279] << 8) < 0xC000u
        && fits(cpu, 3000u, OAM_INSERT)) {
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        SoeCostTrace t;
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0x960A, cpu->D, cpu->DB, true, true, s0);
        oam_depth_sorted_sprite_insert_node_bind(&r);
        soe_cost_end(&t);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        charge(cpu, &c);
        if (s_bind[OAM_INSERT].mutate) g_ram[0x1276] ^= 0x0F;
        s_bind[OAM_INSERT].c++;
        return leave_rtl(cpu);
    }
#endif
    return run_rom_bytes(cpu, OAM_INSERT, 0x960A, 0x960B, cpu->S < 0x2000 ? pool_first : NULL);
}

/* $8F:C6C5, first instruction: LDY $4C. */
static void sort_first(CpuState *cpu) {
    Cost c = { 0 };
    dp16(&c, cpu, 0x4C);
    cpu->Y = dp_word(cpu, 0x4C);
    set_nz16(cpu, cpu->Y);
    cpu->open_bus = (uint8_t)(cpu->Y >> 8);
    charge(cpu, &c);
}

/*
 * $8F:C6C5 sprite_sorting_canopy_occlusion_and_render_depth (RTS; knowledge/routines/sprite_sorting_canopy_occlusion_and_render_depth.md).
 * Computes sprite depth via $8F:C773, evaluates BG canopy occlusion, and inserts the entity's main and shadow OAM
 * nodes via $80:960A. Charged from SOE_AT marks across $8F:C6C5 and its nested $8F:C773 / $80:960A calls.
 */
RecompReturn HleSpriteSort(CpuState *cpu) {
    stats_init();
    const bool plain = wide_a(cpu) && !cpu->x_flag && dp_in_wram(cpu, 0x5C);
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint16_t s0 = cpu->S;
    if (plain && !(cpu->P & CPU_P_D) && cpu->DB == 0x7E && s0 > 16 && s0 < 0x1FF0
        && (uint32_t)dp_word(cpu, 0x4C) + 0x90 < 0x10000u
        && (uint16_t)(g_ram[0x1276] | g_ram[0x1277] << 8) < 0xC000u
        && (uint16_t)(g_ram[0x1278] | g_ram[0x1279] << 8) < 0xC000u
        && fits(cpu, 8500u, SPRITE_SORT)) {
        const uint16_t free_before = (uint16_t)(g_ram[0x1276] | g_ram[0x1277] << 8);
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        SoeCostTrace t;
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0xC6C5, cpu->D, cpu->DB, true, true, s0);
        sprite_sorting_canopy_occlusion_and_render_depth_bind(&r);
        soe_cost_end(&t);
        const uint16_t free_after = (uint16_t)(g_ram[0x1276] | g_ram[0x1277] << 8);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        charge(cpu, &c);
        if (s_bind[SPRITE_SORT].mutate) g_ram[(uint16_t)(dp_word(cpu, 0x4C) + 0x84)] ^= 1;
        s_bind[SPRITE_SORT].c++;
        s_bind[DEPTH].c++;
        if (free_after > free_before) s_bind[OAM_INSERT].c += (unsigned long long)((free_after - free_before) / 0x0Fu);
        return leave_rts(cpu);
    }
#endif
    return run_rom_bytes_frame(cpu, SPRITE_SORT, 0xC6C5, 0xC6C7, plain ? sort_first : NULL, 2);
}

/* $8F:AA51, first instruction: XBA. */
static void dir_first(CpuState *cpu) {
    Cost c = { 0 };
    insn(&c, 3, 1);
    cpu->A = (uint16_t)((cpu->A & 0xFF) << 8 | (cpu->A >> 8));
    cpu->P = (uint8_t)((cpu->P & ~(CPU_P_N | CPU_P_Z)) | ((cpu->A & 0xFF) == 0 ? CPU_P_Z : 0) | (cpu->A & 0x80 ? CPU_P_N : 0));
    cpu->open_bus = 0xEB;
    charge(cpu, &c);
}

/*
 * $8F:AA51 direction_index_from_signed_byte_pair (RTL; knowledge/routines/direction_index_from_signed_byte_pair.md).
 * Pure register routine (entered with 8-bit A) that maps signed (dx, dy) in A low/high bytes to an even direction
 * index $00..$10. Charged from SOE_AT marks.
 */
RecompReturn HleDirectionFromSignedBytePair(CpuState *cpu) {
    stats_init();
    const bool plain = !cpu->emulation && cpu->m_flag;
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (plain && !(cpu->P & CPU_P_D) && fits(cpu, 250u, DIR_PAIR)) {
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        SoeCostTrace t;
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0xAA51, cpu->D, cpu->DB, false, !cpu->x_flag, cpu->S);
        direction_index_from_signed_byte_pair_bind(&r);
        soe_cost_end(&t);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        charge(cpu, &c);
        if (s_bind[DIR_PAIR].mutate) cpu->A ^= 2;
        s_bind[DIR_PAIR].c++;
        return leave_rtl(cpu);
    }
#endif
    return run_rom_bytes(cpu, DIR_PAIR, 0xAA51, 0xAA52, plain ? dir_first : NULL);
}

/* $90:8124, first instruction: LDA $C40002,x. */
static void anim_start_first(CpuState *cpu) {
    const uint32_t a = 0xC40002u + cpu->X;
    Cost c = { 0 };
    insn(&c, 6, 4); word_at(&c, a);
    cpu->A = (uint16_t)(*soe_bus(a) | *soe_bus(a + 1) << 8);
    set_nz16(cpu, cpu->A);
    cpu->open_bus = (uint8_t)(cpu->A >> 8);
    charge(cpu, &c);
}

/*
 * $90:8124 anim_start_record (RTL; knowledge/routines/anim_start_record.md). Initializes an animation record in
 * bank $7E from the table entry at $C4:0000,x. Charged from SOE_AT marks.
 */
RecompReturn HleAnimStartRecord(CpuState *cpu) {
    stats_init();
    const bool plain = wide_a(cpu) && !cpu->x_flag && cpu->X < 0xFFFCu;
    return run_rom_bytes(cpu, ANIM_START, 0x8124, 0x8128, plain ? anim_start_first : NULL);
}

/* $8F:A723, first instruction: PHY. */
static void poll_first(CpuState *cpu) {
    Cost c = { 0 };
    insn(&c, 4, 1); word_at(&c, (uint16_t)(cpu->S - 1));
    g_ram[cpu->S] = (uint8_t)(cpu->Y >> 8);
    g_ram[(uint16_t)(cpu->S - 1)] = (uint8_t)cpu->Y;
    cpu->S = (uint16_t)(cpu->S - 2);
    cpu->open_bus = (uint8_t)cpu->Y;
    charge(cpu, &c);
}

/*
 * $8F:A723 companion_waypoint_history_poll (RTL; knowledge/routines/companion_waypoint_history_poll.md). Advances the
 * 16-entry companion waypoint ring buffer in bank $7E when the leader moves by more than 16 pixels on either axis.
 * Charged from SOE_AT marks.
 */
RecompReturn HleCompanionWaypointHistoryPoll(CpuState *cpu) {
    stats_init();
    const bool plain = wide_a(cpu) && !cpu->x_flag && cpu->S > 4 && cpu->S < 0x1FFE;
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint16_t s0 = cpu->S;
    if (plain && !(cpu->P & CPU_P_D) && cpu->DB == 0x7E && cpu->X < 0xC000u
        && (uint16_t)(g_ram[0x3BFF + cpu->X] | g_ram[0x3C00 + cpu->X] << 8) < 0xC000u
        && fits(cpu, 1200u, WAYPOINT_POLL)) {
        g_ram[s0] = (uint8_t)(cpu->Y >> 8);
        g_ram[(uint16_t)(s0 - 1)] = (uint8_t)cpu->Y;
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        SoeCostTrace t;
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0xA723, cpu->D, cpu->DB, true, true, s0);
        companion_waypoint_history_poll_bind(&r);
        soe_cost_end(&t);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        charge(cpu, &c);
        if (s_bind[WAYPOINT_POLL].mutate) g_ram[0x3C12 + cpu->X] ^= 0x10;
        s_bind[WAYPOINT_POLL].c++;
        return leave_rtl(cpu);
    }
#endif
    return run_rom_bytes(cpu, WAYPOINT_POLL, 0xA723, 0xA724, plain ? poll_first : NULL);
}

/* $8E:E5FC, first instruction: LDA $0106. */
static void audio_fade_first(CpuState *cpu) {
    Cost c = { 0 };
    abs16(&c, (uint32_t)cpu->DB << 16 | 0x0106);
    cpu->A = (uint16_t)(g_ram[0x0106] | g_ram[0x0107] << 8);
    set_nz16(cpu, cpu->A);
    cpu->open_bus = (uint8_t)(cpu->A >> 8);
    charge(cpu, &c);
}

/*
 * $8E:E5FC audio_master_volume_fade_tick (RTL; knowledge/routines/audio_master_volume_fade_tick.md).
 * Updates master audio volume / brightness fading towards target byte $0B83. Charged from SOE_AT marks.
 */
RecompReturn HleAudioMasterVolumeFadeTick(CpuState *cpu) {
    stats_init();
    const bool plain = wide_a(cpu) && db_in_wram(cpu);
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (plain && !(cpu->P & CPU_P_D) && fits(cpu, 350u, AUDIO_FADE)) {
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        SoeCostTrace t;
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0xE5FC, cpu->D, cpu->DB, true, !cpu->x_flag, cpu->S);
        audio_master_volume_fade_tick_bind(&r);
        soe_cost_end(&t);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        charge(cpu, &c);
        if (s_bind[AUDIO_FADE].mutate) g_ram[0x0106] ^= 1;
        s_bind[AUDIO_FADE].c++;
        return leave_rtl(cpu);
    }
#endif
    return run_rom_bytes(cpu, AUDIO_FADE, 0xE5FC, 0xE5FF, plain ? audio_fade_first : NULL);
}

/* $8F:A963 / $8F:A97D / $8F:A999, first instruction: LDA $12 (8-bit A). */
static void shear_first(CpuState *cpu) {
    const uint16_t addr = (uint16_t)(cpu->D + 0x12);
    Cost c = { 0 };
    insn(&c, (cpu->D & 0xFF) ? 4 : 3, 2);
    c.data += extra(addr);
    const uint8_t b = g_ram[addr & 0x1FFF];
    cpu->A = (uint16_t)((cpu->A & 0xFF00) | b);
    cpu->P = (uint8_t)((cpu->P & ~(CPU_P_N | CPU_P_Z)) | (b == 0 ? CPU_P_Z : 0) | (b & 0x80 ? CPU_P_N : 0));
    cpu->open_bus = b;
    charge(cpu, &c);
}

/*
 * $8F:A963 elevation_vector_shear_step (RTS; knowledge/routines/elevation_vector_shear_step.md).
 * min(|dx|, |dy|) of signed DP $12, $13; stores |dx| to DP $14. Charged from SOE_AT marks.
 */
RecompReturn HleElevationVectorShearStep(CpuState *cpu) {
    stats_init();
    const bool plain = !cpu->emulation && cpu->m_flag && dp_in_wram(cpu, 0x14);
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (plain && !(cpu->P & CPU_P_D) && fits(cpu, 250u, SHEAR_STEP)) {
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        SoeCostTrace t;
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0xA963, cpu->D, cpu->DB, false, !cpu->x_flag, cpu->S);
        elevation_vector_shear_step_bind(&r);
        soe_cost_end(&t);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        charge(cpu, &c);
        if (s_bind[SHEAR_STEP].mutate) cpu->A ^= 1;
        s_bind[SHEAR_STEP].c++;
        return leave_rts(cpu);
    }
#endif
    return run_rom_bytes_frame(cpu, SHEAR_STEP, 0xA963, 0xA965, plain ? shear_first : NULL, 2);
}

/*
 * $8F:A97D elevation_manhattan_distance_eval (RTS; knowledge/routines/elevation_manhattan_distance_eval.md).
 * Evaluates Manhattan distance |dx| + |dy| from signed DP $12, $13. Charged from SOE_AT marks.
 */
RecompReturn HleElevationManhattanDistanceEval(CpuState *cpu) {
    stats_init();
    const bool plain = !cpu->emulation && cpu->m_flag && dp_in_wram(cpu, 0x14);
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (plain && !(cpu->P & CPU_P_D) && fits(cpu, 250u, MANHATTAN_DIST)) {
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        SoeCostTrace t;
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0xA97D, cpu->D, cpu->DB, false, !cpu->x_flag, cpu->S);
        elevation_manhattan_distance_eval_bind(&r);
        soe_cost_end(&t);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        charge(cpu, &c);
        if (s_bind[MANHATTAN_DIST].mutate) cpu->A ^= 1;
        s_bind[MANHATTAN_DIST].c++;
        return leave_rts(cpu);
    }
#endif
    return run_rom_bytes_frame(cpu, MANHATTAN_DIST, 0xA97D, 0xA97F, plain ? shear_first : NULL, 2);
}

/*
 * $8F:A999 elevation_compass_direction_resolve (RTS; knowledge/routines/elevation_compass_direction_resolve.md).
 * Resolves 4-way compass direction from signs of DP $12, $13. Charged from SOE_AT marks.
 */
RecompReturn HleElevationCompassDirectionResolve(CpuState *cpu) {
    stats_init();
    const bool plain = !cpu->emulation && cpu->m_flag && dp_in_wram(cpu, 0x13);
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    if (plain && !(cpu->P & CPU_P_D) && fits(cpu, 250u, COMPASS_DIR)) {
        soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
        SoeCostTrace t;
        soe_cost_begin((uint32_t)cpu->PB << 16 | 0xA999, cpu->D, cpu->DB, false, !cpu->x_flag, cpu->S);
        elevation_compass_direction_resolve_bind(&r);
        soe_cost_end(&t);
        const Cost c = { t.cycles, t.fetches, t.data };
        take_regs(cpu, &r);
        charge(cpu, &c);
        if (s_bind[COMPASS_DIR].mutate) cpu->A ^= 2;
        s_bind[COMPASS_DIR].c++;
        return leave_rts(cpu);
    }
#endif
    return run_rom_bytes_frame(cpu, COMPASS_DIR, 0xA999, 0xA99B, plain ? shear_first : NULL, 2);
}

/*
 * $8F:C40F entity_sleep_wake (RTS; knowledge/routines/entity_sleep_wake.md). Its C (reconstructed-c/8F/8FC40F) calls
 * routines that are still asm, which no binding here can do yet (docs/widescreen/bindings.md), so the ROM code
 * runs. With widescreen experiment E6 on (src/soe_ws_alive.c) the decision is E6's: the helper does the first
 * instruction (LDY $4C) and the ROM code goes on at the instruction of the branch it picked (RTS, the sleep path
 * or the wake path), so every change of state is still the game's own. E6 changes the game; its path is never
 * taken with E6 off, and then this is the ROM routine.
 */
RecompReturn HleEntitySleepWake(CpuState *cpu) {
    stats_init();
    const bool plain = wide_a(cpu) && !cpu->x_flag && dp_in_wram(cpu, 0x5C);
    if (plain && interp_bridge_in_lle_scheduler() && !interp_bridge_lle_master_deadline_reached(cpu)) {
        const uint16_t d = cpu->D;
#define DP_WORD(o) (uint16_t)(g_ram[(uint16_t)(d + (o))] | g_ram[(uint16_t)(d + (o) + 1)] << 8)
        const uint16_t to = SoeAliveDecide(DP_WORD(0x4C), DP_WORD(0x59), DP_WORD(0x5B));
#undef DP_WORD
        if (to != kAliveRom) {
            s_bind[SLEEP_WAKE].c++;
            cpu_aot_insn_begin(cpu, 0xC40F);
            cpu_mirrors_to_p(cpu);
            sort_first(cpu);
            cpu_p_to_mirrors(cpu);
            return interp_bridge_lle_yield_unwind(cpu, (uint32_t)cpu->PB << 16 | to);
        }
    }
    return run_rom_bytes_frame(cpu, SLEEP_WAKE, 0xC40F, 0xC411, plain ? sort_first : NULL, 2);
}

/*
 * Free time for widescreen E6 (src/soe_ws_alive.c, docs/widescreen/enemies.md "E6 as built"): the script and entity
 * part of per_frame_field_tick, from its JSL script_scheduler_pass at $8E:E45C to its JSL field_spare_time_loop at
 * $8E:E4B0, costs no emulated time while E6 is on, so every enemy of a room fits a frame. A wait for the NMI inside it
 * (HleWaitForNmi, HleSpareTime) ends free time first: time has to pass there.
 * Both helpers only switch free time (soe_frame_driver.c) and leave the JSL to the interpreter (no compiled code at a
 * bound entry): with E6 off they are the ROM's instructions, exactly.
 */
RecompReturn HleFieldTickFreeBegin(CpuState *cpu) {
    stats_init();
    SoeAliveFrame();
    if (SoeAliveFreeTime()) { soe_frame_driver_free_time(true); s_bind[FREE_BEGIN].c++; }
    return run_rom_bytes(cpu, FREE_BEGIN, 0xE45C, 0xE460, NULL);
}
RecompReturn HleFieldTickFreeEnd(CpuState *cpu) {
    stats_init();
    soe_frame_driver_free_time(false);
    return run_rom_bytes(cpu, FREE_END, 0xE4B0, 0xE4B4, NULL);
}

/* $8F:D879, first instruction: LDX $0F42. */
static void pick_target_first(CpuState *cpu) {
    const uint32_t a = (uint32_t)cpu->DB << 16 | 0x0F42;
    Cost c = { 0 };
    abs16(&c, a);
    cpu->X = (uint16_t)(g_ram[a & 0x1FFFF] | g_ram[(a + 1) & 0x1FFFF] << 8);
    set_nz16(cpu, cpu->X);
    cpu->open_bus = (uint8_t)(cpu->X >> 8);
    charge(cpu, &c);
}

/* $8F:D69B and $8F:D715, first instruction: LDA $002E,Y. */
static void stamina_first(CpuState *cpu) {
    const uint32_t a = ((uint32_t)cpu->DB << 16) + 0x002Eu + cpu->Y;
    Cost c = { 0 };
    abs_i16(&c, a);
    cpu->A = (uint16_t)(g_ram[a & 0x1FFFF] | g_ram[(a + 1) & 0x1FFFF] << 8);
    set_nz16(cpu, cpu->A);
    cpu->open_bus = (uint8_t)(cpu->A >> 8);
    charge(cpu, &c);
}

/*
 * Widescreen E6's aggro cap (src/soe_ws_alive.c, SoeAliveMayAggro) at the three places an enemy takes the party as its
 * target. When the cap says no, the ROM code goes on at the routine's own "no target" exit, as if its tests had failed:
 * $8F:D911 (ai_pick_target: TDC, TAX, STA $24,Y, RTS; the caller wanders), $8F:D6C9 (enemy_ai_check_aggro: PLA, JML to
 * the default action), $8F:D766 (ai_stationary_player: JML walk). Otherwise, and always with E6 off, the ROM routine.
 */
static RecompReturn aggro_gate(CpuState *cpu, int which, uint16_t entry, uint16_t body, uint16_t no, void (*first)(CpuState *)) {
    stats_init();
    const bool plain = wide_a(cpu) && !cpu->x_flag && cpu->DB == 0x7E && dp_in_wram(cpu, 0x5C);
    if (plain && interp_bridge_in_lle_scheduler() && !interp_bridge_lle_master_deadline_reached(cpu)) {
        const uint16_t d = cpu->D;
#define DP_WORD(o) (uint16_t)(g_ram[(uint16_t)(d + (o))] | g_ram[(uint16_t)(d + (o) + 1)] << 8)
        if (!SoeAliveMayAggro(cpu->Y, DP_WORD(0x59), DP_WORD(0x5B))) {
#undef DP_WORD
            s_bind[which].c++;
            return interp_bridge_lle_yield_unwind(cpu, (uint32_t)cpu->PB << 16 | no);
        }
    }
    return run_rom_bytes_frame(cpu, which, entry, body, plain ? first : NULL, 2);
}
RecompReturn HleAiPickTarget(CpuState *cpu) { return aggro_gate(cpu, PICK_TARGET, 0xD879, 0xD87C, 0xD911, pick_target_first); }
RecompReturn HleEnemyAiCheckAggro(CpuState *cpu) { return aggro_gate(cpu, CHECK_AGGRO, 0xD69B, 0xD69E, 0xD6C9, stamina_first); }
RecompReturn HleAiStationaryPlayer(CpuState *cpu) { return aggro_gate(cpu, STATIONARY_PLAYER, 0xD715, 0xD718, 0xD766, stamina_first); }

/*
 * $8F:DE43, the spawner's spawn step (PLA of its loop count, then the record in X is spawned). With widescreen E6 on a
 * record with too many spawned enemies around its point (src/soe_ws_alive.c, SoeAliveSpawnAllowed) is skipped: the ROM
 * code goes on at $DE3B, where the loop goes on to its next record (PLA, DEC, ...) with the same stack. Otherwise the
 * ROM's instruction (interpreted at a bound entry).
 */
RecompReturn HleSpawnerSpawn(CpuState *cpu) {
    stats_init();
    const bool plain = wide_a(cpu) && !cpu->x_flag && cpu->DB == 0x7E;
    if (plain && interp_bridge_in_lle_scheduler() && !interp_bridge_lle_master_deadline_reached(cpu)
        && !SoeAliveSpawnAllowed(cpu->X)) {
        s_bind[SPAWNER].c++;
        return interp_bridge_lle_yield_unwind(cpu, (uint32_t)cpu->PB << 16 | 0xDE3B);
    }
    return run_rom_bytes(cpu, SPAWNER, 0xDE43, 0xDE44, NULL);
}
