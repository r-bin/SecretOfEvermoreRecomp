# Where a Comparison Samples

**Status:** solved in the tooling (`SOE_COMPARE_AT=nmi`, `tools/compare` `atNmi`), 2026-10-30.

Sampling at the end of a frame is not the same moment on both sides: MesenCE's `endFrame` fires exactly at
V=225, inside an instruction; the recomp's CPU finishes the instruction that crossed V=225 (16–50 clocks more).
That once showed the decompressor's pointer `$2A–$2C` changed on one side only (a store in those clocks).
Both sides now sample as each NMI is taken, after its return frame is pushed (MesenCE's Lua `nmi` event,
our driver's `soe_frame_driver_nmi_hook`): an instruction boundary on both. The key is the NMI's number since
power-on; runs that include snes9x keep the frame-end sampling.
