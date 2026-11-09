/*
 * Widescreen, first approach: field backgrounds only (docs/widescreen/progress.md).
 *
 * Presentation-only: nothing here writes guest memory or registers, so with
 * widescreen on the game runs exactly as in 4:3. Off (the default, config
 * `Widescreen = 0`), the frame stays 256 wide and none of this runs.
 */
#ifndef SOE_WIDESCREEN_H
#define SOE_WIDESCREEN_H

#include <stdint.h>

/* SnesDesktopHostGame.prepare_frame: frame width for this frame. */
void SoeWsPrepareFrame(int drawable_w, int drawable_h, int *frame_w, int *frame_h);
/* SnesDesktopHostGame.window_base_width: the window's logical width at the widescreen shape. */
int SoeWsWindowBaseWidth(int frame_w);
/* SnesDesktopHostGame.begin_sim_frame: margins and prefill for the raster. */
void SoeWsBeginFrame(unsigned number);
/* SnesDesktopHostGame.end_sim_frame: blacks the margin columns the PPU did not draw. */
void SoeWsEndFrame(const uint8_t *field, unsigned number);

#endif
