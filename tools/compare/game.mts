// What compare needs to know about Secret of Evermore to line two backends up. Facts with evidence live in the
// knowledge entries (knowledge/wram/PPU_FRAME_COUNTER_LOW.md); docs/debugging.md tells how they were measured.

/**
 * The NMI handler counts frames here ($7E0100, word). The key two backends are aligned on is the NMI count since
 * power-on: its increases, a drop counting as a restart from 0 (the attract demo zeroes it when it loops).
 */
export const FRAME_COUNTER = 0x0100;

/**
 * The key from successive $0100 reads: -1 until the reset code's 0, then the NMIs counted since. A wrap counts on,
 * any other drop is a restart from 0. The MesenCE Lua (mesen.mts) and the recomp glue (game_rtl.c) do the same.
 */
export function nmiKey(): (raw: number) => number {
    let last = -1, count = 0;
    return raw => {
        if (last < 0) { if (raw === 0) last = 0; return last < 0 ? -1 : 0; }
        if (raw !== last) {
            count += raw > last ? raw - last : last > 0xFF00 ? raw + 0x10000 - last : raw;
            last = raw;
        }
        return count;
    };
}
