/*
 * $8F:9817 field_spare_time_loop (knowledge/routines/field_spare_time_loop.md), RTL by way of its tail jump.
 *
 * The end of a field frame: until the NMI bumps the frame counter, run child tasks round robin. PHB, DB = $7E, and the counter
 * is kept on the stack ($01,s); then each pass from $9821 asks whether the counter moved and, if not, takes the next child.
 * On a new frame: PLA, PLB, JML vram_oam_buffer_dma_commit_on_nmi.
 *
 * Only a pass and the head of a task are C. The loop has no end of its own: what ends it is the NMI, and most of a task is
 * other routines, so a caller runs the passes and does what the return values say. Nearly every task call ends in its head.
 */
#include "soe.h"
#include "soe_ram.h"
#include "soe_dp.h"
#include "spare_time.h"

#define CHILD_RECORD_SIZE      0x0059   /* 6 records in the pool */
#define CHILD_POOL_END         0x0216   /* the cursor goes back to the first record here */
#define TASK_SELECTOR_MASK     0x000E   /* eight table entries, two bytes each */
#define TASK_TABLE             0x8F9867
#define CHILD_FLAG_SUSPENDED   0x0200   /* the task loop skips this child */
#define CHILD_FLAG_NO_SEARCH   0x0010   /* the waypoint search has nothing to do for this child */

/* The two tasks the table holds, by entry address in bank $8F. */
typedef enum {
    TASK_WALK_STEPS      = 0x9AC6,      /* walk the steps a child has planned */
    TASK_WAYPOINT_SEARCH = 0xA4CA       /* look for a way to the next waypoint */
} ChildTask;

/**
 * @brief One pass of the loop from $9821: moves on to the next child and picks its task.
 *
 * Moves the cursor on by one child record. Past the last it goes back to the first and the task selector moves on (0, 2 .. $E).
 * A suspended child, or one with no parent, is skipped. For the others the direct page gets the record and the parent, and the
 * selector picks the task, which the asm enters with a return address of $9821 on the stack (PHK, PEA $9820, JMP ($9867,x)).
 *
 * @param r              the registers; DB is $7E
 * @param frame_at_entry the frame counter when the routine began ($01,s)
 * @return SPARE_NEXT or SPARE_NEW_FRAME, or the task's entry address, with X = the selector and the return address still to push
 */
uint16_t field_spare_time_pass(soe_regs r, uint16_t frame_at_entry)
{
    uint16_t task_address;

    r->a = ABS16(r, PPU_FRAME_COUNTER_LOW_ADDR);
    soe_cmp16(r, r->a, frame_at_entry);
    if (r->a != frame_at_entry) {
        SOE_AT(0x9861);
        return SPARE_NEW_FRAME;
    }
    r->a = ABS16(r, CHILD_TASK_CURSOR_ADDR);
    soe_carry(r, false);
    r->a = soe_adc16(r, r->a, CHILD_RECORD_SIZE);
    soe_cmp16(r, r->a, CHILD_POOL_END);
    if (r->a >= CHILD_POOL_END) {
        SOE_AT(0x9834);
        ABS16(r, SCRIPT_TASK_SELECTOR_ADDR) = (uint16_t)((ABS16(r, SCRIPT_TASK_SELECTOR_ADDR) + 2) & TASK_SELECTOR_MASK);
        r->a = 0;
        soe_nz16(r, 0);
    }
    SOE_AT(0x9842);
    ABS16(r, CHILD_TASK_CURSOR_ADDR) = r->a;
    r->x = r->a;
    r->a = ABS16(r, (uint16_t)(COMPANION_NAV_FLAGS_ADDR + r->x));
    soe_nz16(r, r->a);
    soe_bit_imm(r, r->a, CHILD_FLAG_SUSPENDED);
    if (r->a & CHILD_FLAG_SUSPENDED) {
        SOE_AT(0x9821);
        return SPARE_NEXT;
    }
    SOE_AT(0x984E);
    r->a = ABS16(r, (uint16_t)(ACTOR_PARENT_LINKS_ADDR + r->x));
    soe_nz16(r, r->a);
    if (r->a == 0) {
        SOE_AT(0x9821);
        return SPARE_NEXT;
    }
    SOE_AT(0x9853);
    DP16(r, DP_CHILD_RECORD) = r->x;
    DP16(r, DP_PARENT_ENTITY) = r->a;
    r->x = ABS16(r, SCRIPT_TASK_SELECTOR_ADDR);
    soe_nz16(r, r->x);
    task_address = ROM16(TASK_TABLE + r->x);
    SOE_AT(task_address);
    return task_address;
}

/**
 * @brief The head of a task, entered with $9821 as the return address: the checks that usually end it at once.
 *
 * @param r            the registers
 * @param task_address the entry from the task table, a ChildTask
 * @return SPARE_NEXT when the head ended in its RTL and the loop goes on at $9821; otherwise the address in this bank where the
 *         rest of the task goes on
 */
uint16_t field_spare_time_task(soe_regs r, uint16_t task_address)
{
    r->x = DP16(r, DP_CHILD_RECORD);
    soe_nz16(r, r->x);
    if (task_address == TASK_WALK_STEPS) {
        r->a = ABS16(r, (uint16_t)(COMPANION_STEP_COUNT_ADDR + r->x));
        soe_nz16(r, r->a);
        r->a = (uint16_t)(r->a << 8 | r->a >> 8);            /* XBA: the steps left; N Z from them */
        soe_nz8(r, (uint8_t)r->a);
        if ((uint8_t)r->a == 0) {
            SOE_AT(0x9ACE);
            return 0x9ACE;
        }
        SOE_AT(0x9ADE);
        if (!(r->p & SOE_N)) {
            SOE_AT(0x9AE1);
            return 0x9AE1;
        }
    } else {
        r->a = ABS16(r, (uint16_t)(COMPANION_NAV_FLAGS_ADDR + r->x));
        soe_nz16(r, r->a);
        soe_bit_imm(r, r->a, CHILD_FLAG_NO_SEARCH);
        if (r->a & CHILD_FLAG_NO_SEARCH) {
            SOE_AT(0xA4C9);
            SOE_AT(0x9821);
            return SPARE_NEXT;
        }
        SOE_AT(0xA4D4);
        r->a = r->x;
        soe_cmp16(r, r->a, ABS16(r, (uint16_t)(BREADCRUMB_QUEUE_TAIL_ADDR + r->x)));
        if (r->a != ABS16(r, (uint16_t)(BREADCRUMB_QUEUE_TAIL_ADDR + r->x))) {
            SOE_AT(0xA4DA);
            return 0xA4DA;
        }
        SOE_AT(0xA4C9);
    }
    SOE_AT(0x9821);
    return SPARE_NEXT;
}
