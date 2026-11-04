#ifndef SOE_FRAME_DRIVER_H
#define SOE_FRAME_DRIVER_H
/* Secret of Evermore's frame driver: snesrecomp's beam frame driver plus the late NMI (soe_frame_driver.c). */
#include <stdbool.h>
#include <stdint.h>

void soe_frame_driver_run_frame(void);
void soe_frame_driver_reset(void);

/* Called as each NMI is taken, its return frame pushed and the handler not yet run: the instruction boundary
 * where the CPU was interrupted, where MesenCE fires its Lua nmi event (tools/compare samples there). */
extern void (*soe_frame_driver_nmi_hook)(void);

/* The resume point (where the CPU half continues next field), for save states. */
uint32_t soe_frame_driver_resume_pc(void);
bool soe_frame_driver_wai_halted(void);
void soe_frame_driver_set_resume(uint32_t pc, bool wai_halted);

/* The master clock the CPU runs to in the current slice (0: none), for C bindings that must not run past it
 * (src/soe_bindings.c). */
uint64_t soe_frame_driver_deadline(void);
/* The first stop of the slice that is not a DRAM refresh (0: none), and whether an IRQ could be taken in it. */
uint64_t soe_frame_driver_hard_deadline(void);
bool soe_frame_driver_irq_possible(void);

/* game_rtl.c: the WRAM and SRAM a compare run starts with (SOE_COMPARE_WRAM, _SRAM), before the first instruction. */
void soe_compare_start_memory(void);
/* game_rtl.c: a replay's input for the field at this beam clock (SOE_COMPARE_PADS); `first`: where the first poll is. */
void soe_compare_pad(uint64_t beam);
void soe_compare_pad_first(uint64_t clock);
void soe_compare_ready(void);
/* soe_mesen_state.c: the machine from a MesenCE state (SOE_COMPARE_MESEN_STATE); its PC, or 0. */
uint32_t soe_mesen_state_load(void);
void soe_mesen_state_load_apu(void);

#endif
