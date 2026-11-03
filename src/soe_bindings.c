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
 * Otherwise the helper yields to the scheduler at the routine's entry, and the interpreter runs the ROM bytes,
 * exactly. Without the reconstructed C the helpers always do that.
 *
 * SOE_HLE_STATS=1 prints how often each helper took each path at exit. SOE_HLE_MUTATE=1 corrupts the C path's
 * result: a control that shows the sync replays catch a wrong binding.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
#endif

static int s_stats = -1, s_mutate;
static unsigned long long s_rng_c, s_rng_lle, s_rng_across;
static void print_stats(void) {
    fprintf(stderr, "[hle] random_number_generator: %llu in C (%llu across a refresh stop), %llu interpreted\n",
            s_rng_c, s_rng_across, s_rng_lle);
}
static void stats_init(void) {
    if (s_stats >= 0) return;
    const char *e = getenv("SOE_HLE_STATS"), *m = getenv("SOE_HLE_MUTATE");
    s_stats = e && e[0] == '1';
    s_mutate = m && m[0] == '1';
    if (s_stats) atexit(print_stats);
}

/* Master clocks of one access at a 24-bit address, beyond the 6 every CPU cycle costs. */
static uint64_t extra(uint32_t addr24) { return cpu_region_speed(addr24 & 0xFFFFFFu) - 6u; }

/* The C may run when the routine ends before the slice's deadline (0: none), or runs past a DRAM refresh stop
 * only: the frame driver charges a refresh after the slice from the points it crossed, so a WRAM-only routine
 * cannot tell. Then it must still end before the next stop that is not a refresh, with no IRQ to take: the driver
 * takes IRQs between slices, so the original could be interrupted at the refresh stop. */
static bool fits(const CpuState *cpu, uint64_t master) {
    if (interp_bridge_lle_master_deadline_reached(cpu)) return false;
    const uint64_t deadline = soe_frame_driver_deadline(), end = cpu->master_cycles + master;
    if (!deadline || end < deadline) return true;
    if (end < soe_frame_driver_hard_deadline() && !soe_frame_driver_irq_possible()) { s_rng_across++; return true; }
    return false;
}

/*
 * $80:859B random_number_generator (RTL; everscript-vscode knowledge/rom/80859b.md). Four times: LDA abs, ROL A,
 * BCC over an INC abs, ADC abs, STA abs; then RTL. The branches depend only on bit 15 of each rotated word, which
 * follow from the state at entry, so the path is known before the C runs.
 */
RecompReturn HleRandomNumberGenerator(CpuState *cpu) {
    stats_init();
    const uint32_t entry = ((uint32_t)cpu->PB << 16) | 0x859Bu;
#ifdef SOE_RECONSTRUCTED_C
    cpu_mirrors_to_p(cpu);
    const uint32_t db = (uint32_t)cpu->DB << 16;
    const bool wram = cpu->DB == 0x7E || !(cpu->DB & 0x40);
    if (!cpu->emulation && !cpu->m_flag && !(cpu->P & CPU_P_D) && wram) {
        static const uint16_t rot[4] = { 0x0139, 0x0137, 0x0135, 0x0133 };   /* each step: rotate, bump, add, store */
        static const uint16_t bump[4] = { 0x0137, 0x0135, 0x0133, 0x0139 };
        static const uint16_t add[4] = { 0x0135, 0x0133, 0x0137, 0x0139 };
        /* As bus-exact code charges an instruction: 6 master clocks per CPU cycle, each code fetch at the code's
         * speed, each data access at its address's (extra() is the part above 6). */
        const uint64_t fetch = ((g_memsel && (cpu->PB & 0x80)) ? 6u : 8u) - 6u;
        uint64_t cycles = 0, fetches = 0, data = 0;
        uint16_t bumped[4] = { 0 };   /* +1 a word got from an earlier step's INC, before it is rotated */
        for (int i = 0; i < 4; i++) {
            const uint16_t w = (uint16_t)(g_ram[rot[i]] | g_ram[rot[i] + 1] << 8);
            const bool carry = ((uint16_t)(w + bumped[i]) & 0x8000) != 0;
            const uint64_t at_rot = extra(db | rot[i]) + extra(db | (rot[i] + 1u));
            cycles += 5; fetches += 3; data += at_rot;                                   /* LDA abs */
            cycles += 2; fetches += 1;                                                   /* ROL A */
            cycles += carry ? 2 : 3; fetches += 2;                                       /* BCC, taken +1 */
            if (carry) {                                                                 /* INC abs: read, write */
                cycles += 8; fetches += 3; data += 2 * (extra(db | bump[i]) + extra(db | (bump[i] + 1u)));
                for (int j = i + 1; j < 4; j++) if (rot[j] == bump[i]) bumped[j]++;
            }
            cycles += 5; fetches += 3; data += extra(db | add[i]) + extra(db | (add[i] + 1u));   /* ADC abs */
            cycles += 5; fetches += 3; data += at_rot;                                   /* STA abs */
        }
        const uint16_t s = cpu->S;
        cycles += 6; fetches += 1;                                                       /* RTL: 3 pulls */
        data += extra((uint16_t)(s + 1)) + extra((uint16_t)(s + 2)) + extra((uint16_t)(s + 3));
        const uint64_t master = 6 * cycles + fetches * fetch + data;
        if (fits(cpu, master)) {
            soe_regs_t r = { cpu->A, cpu->X, cpu->Y, cpu->D, cpu->DB, cpu->P };
            random_number_generator_bind(&r);
            cpu->A = s_mutate ? (uint16_t)(r.a ^ 1) : r.a;
            cpu->P = r.p;
            cpu_p_to_mirrors(cpu);
            cpu->open_bus = g_ram[(uint16_t)(s + 3)];   /* the RTL's last read: the return bank */
            cpu->S = (uint16_t)(s + 3);
            cpu->cycles += cycles;
            cpu->master_cycles += master;
            s_rng_c++;
            return RECOMP_RETURN_NORMAL;
        }
    }
#endif
    s_rng_lle++;
    if (interp_bridge_in_lle_scheduler()) return interp_bridge_lle_yield_unwind(cpu, entry);
    return interp_tier_run_call_frame(cpu, entry, entry, 3, NULL);
}
