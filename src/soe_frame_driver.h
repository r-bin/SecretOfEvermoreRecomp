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

#endif
