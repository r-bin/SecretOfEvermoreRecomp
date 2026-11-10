/*
 * Widescreen experiment E6: the whole room alive, at most `cap` characters drawn into a fight in the 4:3 view
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

/* E6 is on: only the mod's experimental feature turns it on (SOE_WS_ALIVE=0 turns it off). */
int SoeAliveEnabled(void);
/* E6 is on: the script and entity part of the field tick runs in free time. */
int SoeAliveFreeTime(void);

/* Whether the enemy in `slot` may take the party as its target: always inside the 4:3 view at `cam_x`/`cam_y`
 * ($59/$5B), outside it only while fewer than `cap` characters are in the view. 1 whenever E6 is off. */
int SoeAliveMayAggro(uint16_t slot, uint16_t cam_x, uint16_t cam_y);

/* Whether the spawner's spawn step may spawn record `record` ($5DC3..) now: its box is not full and the 4:3 view at
 * `cam_x`/`cam_y` holds fewer than `cap` characters. 1 whenever E6 is off. */
int SoeAliveSpawnAllowed(uint16_t record, uint16_t cam_x, uint16_t cam_y);
/* During a room's load (its first 90 logic frames): the record the spawner $8F:DDBB should spawn now, 0 for none this
 * frame. kAliveSpawnRom when E6 is off or the room has loaded (the ROM's spawner runs). */
enum { kAliveSpawnRom = 0xFFFF };
uint16_t SoeAliveSpawnPick(void);
/* Once per field frame, from the start of free time: keeps the budget $1076 where E6 wants it and, during a room's
 * load, takes or holds the placed enemies. */
void SoeAliveFrame(void);
/* The entity in `slot` is a placed enemy held on the room's load (asleep, to be drawn as a ghost). 0 when E6 is off. */
int SoeAliveHeld(uint16_t slot);

#endif
