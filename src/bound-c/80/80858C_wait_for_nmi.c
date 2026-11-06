/*
 * $80:858C wait_for_nmi (knowledge/routines/wait_for_nmi.md), RTL by way of its tail jump. Spins until the NMI has moved the frame
 * counter, then JMP vram_oam_buffer_dma_commit_on_nmi.
 *
 * A = the counter at the entry. The spin is CMP $0100, BEQ, two instructions at $858F, and it is where the game waits when a
 * frame's work is done. Only a pass is C: what ends the loop is the NMI, so a caller runs the passes.
 */
#include "soe.h"
#include "soe_ram.h"
#include "spare_time.h"

/**
 * @brief One pass of the spin from $858F.
 *
 * @param r the registers; A is the frame counter at the routine's entry
 * @return true when the counter has not moved and the spin goes on at $858F
 */
bool wait_for_nmi_pass(soe_regs r)
{
    soe_cmp16(r, r->a, ABS16(r, PPU_FRAME_COUNTER_LOW_ADDR));
    if (r->a == ABS16(r, PPU_FRAME_COUNTER_LOW_ADDR)) {
        SOE_AT(0x858F);
        return true;
    }
    SOE_AT(0x8594);
    return false;
}
