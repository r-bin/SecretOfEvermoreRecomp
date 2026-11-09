/*
 * Widescreen experiment E6: the whole room alive, at most `cap` entities in the 4:3 view
 * (docs/widescreen/enemies.md, "E6 as built"). Changes the game.
 */
#ifndef SOE_WS_ALIVE_H
#define SOE_WS_ALIVE_H

#include <stdint.h>

/* Where $8F:C40F entity_sleep_wake goes on for the entity in `slot`, with the camera at `cam_x`/`cam_y`
 * ($59/$5B): kAliveRom runs the routine as the ROM has it; the others are its own instructions, at which
 * the ROM code goes on (Y = slot): RTS (nothing changes), the sleep path, the wake path. Also keeps the
 * budget $1076 at what rule 4 allows. kAliveRom whenever E6 is off or the frame is not a field frame. */
enum {
    kAliveRom = 0,
    kAliveKeep = 0xC450,
    kAliveSleep = 0xC451,
    kAliveWake = 0xC4B1,
};
uint16_t SoeAliveDecide(uint16_t slot, uint16_t cam_x, uint16_t cam_y);

/* E6 is on: only the mod's experimental feature turns it on (SOE_WS_ALIVE=0 turns it off). Held entities are
 * then drawn in the centre too. */
int SoeAliveEnabled(void);
/* E6 is on and this is a field frame: the entity and sprite part of the field tick runs in free time. */
int SoeAliveFreeTime(void);

#endif
