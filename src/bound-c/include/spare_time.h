/* The loops that wait for the NMI, $8F:9817 field_spare_time_loop and $80:858C wait_for_nmi, a pass at a time: the NMI ends
 * them, so a caller runs the passes. */
#ifndef SPARE_TIME_H
#define SPARE_TIME_H

/* What a pass or a task's head tells its caller, when it is not an address in bank $8F. */
typedef enum {
    SPARE_NEXT      = 0,            /* go on at $9821 */
    SPARE_NEW_FRAME = 1             /* the counter moved: go on at $9861 */
} SpareOutcome;

uint16_t field_spare_time_pass(soe_regs r, uint16_t frame_at_entry);
uint16_t field_spare_time_task(soe_regs r, uint16_t task_address);
bool wait_for_nmi_pass(soe_regs r);

#endif
