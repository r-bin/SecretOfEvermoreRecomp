/*
 * The strike an entity's animation ran in its last tick, found without a hook (src/soe_anim_walk.c). Shared by the
 * debug overlay (strikes layer) and the combat RNG mod (whiffs and blocked hits).
 *
 * Reads WRAM and the ROM only.
 */
#ifndef SOE_ANIM_WALK_H
#define SOE_ANIM_WALK_H

#include <stdint.h>

/* Where an entity's animation stands: resume pointer +$00 (bank +$02), restart point +$03, frame timer +$05. */
typedef struct { uint32_t anim; uint16_t restart; uint8_t timer; } SoeAnimCursor;

SoeAnimCursor SoeAnimCursorOf(uint16_t entity);
/* The address of the strike command (`$47`) the tick between `was` and `now` ran, or 0. */
uint32_t SoeAnimTickStrike(const SoeAnimCursor *was, const SoeAnimCursor *now);

#endif
