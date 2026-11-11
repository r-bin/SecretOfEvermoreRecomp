/*
 * Combat RNG (docs/mods/combat-rng.md): every dice roll of a fight shown where it lands — hit or miss and the
 * chance, a swing that whiffed by how many pixels or struck a target that could not be hit and why, the damage
 * roll within its range, the knockback length within its range.
 *
 * Presentation only: the two hooks read the CPU and WRAM at a routine's entry and write nothing; the labels
 * are drawn into the finished picture. Off (the default), the hooks return at once and nothing is drawn.
 */
#ifndef SOE_COMBAT_RNG_H
#define SOE_COMBAT_RNG_H

#include <stdint.h>

typedef struct CpuState CpuState;

/* From src/soe_bindings.c, at the entry of $80:859B random_number_generator and $90:8124 anim_start_record. */
void SoeCombatRngOnRandom(const CpuState *cpu);
void SoeCombatRngOnAnimStart(const CpuState *cpu);

/* From the host's prepare_frame, once the frame width is known. */
void SoeCombatRngPrepareFrame(int frame_w);
/* From end_sim_frame, after the debug overlay and before the HUD mod draws its HUD back. */
void SoeCombatRngEndFrame(const uint8_t *field, unsigned number);

#endif
