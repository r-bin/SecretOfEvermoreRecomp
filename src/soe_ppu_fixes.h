#ifndef SOE_PPU_FIXES_H
#define SOE_PPU_FIXES_H
/* PPU and register behaviour snesrecomp lacks, applied through its hooks (soe_ppu_fixes.c). */

/* The framework hooks: DMA charge, WRAM and VRAM write logs. Every frame, before the CPU runs. */
void soe_ppu_fixes_install(void);
/* As an NMI is taken, before its frame is pushed: $4212's open-bus bits in A. */
void soe_ppu_fixes_nmi_entry(void);
/* The NMI's frame pushed, before the handler and the compare hook: CGRAM to 15 bits. */
void soe_ppu_fixes_nmi_taken(void);
/* After each CPU slice: a CGRAM DMA during rendering, walked by its bytes' beam positions. */
void soe_ppu_fixes_slice_end(void);

#endif
