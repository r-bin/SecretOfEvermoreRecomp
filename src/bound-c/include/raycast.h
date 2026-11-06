/* $8F:9F05 companion_metatile_raycast_line_stepper in two parts, so a caller can stop between steps. */
#ifndef RAYCAST_H
#define RAYCAST_H

typedef struct {
    uint16_t direction, steps;      /* the asm's $03,s and $01,s */
} RaycastLoop;

bool raycast_start(soe_regs r, RaycastLoop SOE_FAR *l);
bool raycast_step(soe_regs r, RaycastLoop SOE_FAR *l);

#endif
