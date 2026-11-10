/*
 * Debug overlay (docs/mods/debug-overlay.md): collision, triggers, map objects and characters drawn over
 * the running field, in the colours of everscript-vscode's map editor.
 *
 * Presentation only: reads WRAM, the ROM and the PPU's scroll, writes no guest state. Off (the default),
 * none of this runs.
 */
#ifndef SOE_DEBUG_OVERLAY_H
#define SOE_DEBUG_OVERLAY_H

#include <stdint.h>

/* From the host's prepare_frame, once the frame width is known. */
void SoeDebugPrepareFrame(int frame_w);
/* From end_sim_frame, after the widescreen pass and before the HUD mod draws its HUD back. */
void SoeDebugEndFrame(const uint8_t *field, unsigned number);

#endif
