/*
 * Practice cheats on Start + a direction (docs/cheats.md): the practice ROM's no clip, Atlas glitch and item
 * unlocks, from the launcher's "Practice hotkeys" feature. Changes the game.
 */
#ifndef SOE_CHEATS_H
#define SOE_CHEATS_H

/* Once per field frame, at the start of free time ($8E:E45C, after the joypad matcher read $0EC6): a new Start +
 * direction chord runs its cheat, and the cheats switched on are held. Nothing at all while the feature is off. */
void SoeCheatsFieldFrame(void);

#endif
