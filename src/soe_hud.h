/*
 * Adaptive HUD (docs/mods/hud.md): the field HUD moved to the top, to the screen edges, or hidden.
 *
 * Presentation only: the PPU renders the HUD into host-side overlay surfaces and leaves it out of
 * the game picture; nothing here writes guest memory or registers. Off (the default), none of
 * this runs and the field is drawn as before.
 */
#ifndef SOE_HUD_H
#define SOE_HUD_H

#include <stdint.h>

/* From the host's prepare_frame, once the frame width is known: finds the HUD and sets this
 * frame's capture. Returns nonzero when the capture needs the optimized renderer. */
int SoeHudPrepareFrame(int frame_w);
/* From end_sim_frame, after the widescreen pass: draws the captured HUD at its new place. */
void SoeHudEndFrame(const uint8_t *field, unsigned number);

#endif
