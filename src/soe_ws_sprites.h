/*
 * Widescreen "extended" sprites: entities drawn in the margins from WRAM (src/soe_ws_sprites.c).
 * Presentation only.
 */
#ifndef SOE_WS_SPRITES_H
#define SOE_WS_SPRITES_H

/* Called once per frame from the widescreen begin_sim_frame on field frames. `enabled` 0 still
 * takes the snapshot (so switching on mid-room has last frame's state) but draws nothing; `grey`
 * draws them in grey (the colours still follow CGRAM, so fades apply); `held` (experiment E6) draws the
 * asleep ones in grey everywhere, the 4:3 centre too, where the game does not draw them. Camera and
 * margins are those of the field being drawn. */
void SoeWsSpritesFrame(int enabled, int grey, int held, int cam_x, int cam_y, int left, int right);
/* Drops the snapshot and the PPU's extra objects (room change, non-field frames). */
void SoeWsSpritesReset(void);

#endif
