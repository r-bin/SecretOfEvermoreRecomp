// What compare needs to know about Secret of Evermore to line two backends up. Facts with evidence live in the
// knowledge entries (knowledge/wram/PPU_FRAME_COUNTER_LOW.md, RNG_SEED_0.md); SecretOfEvermoreRecomp docs/debugging.md tells how they were measured.

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
/** The reset handler seeds the RNG ($7E0133-$7E013A) from the PPU H/V counters, which no two backends share. */
export const RNG: [number, number] = [0x0133, 0x013B];
/** The counter value at which a recomp gets the reference's RNG bytes; keys up to it are not compared. */
export const SYNC_AT = 115;

function range(a: number, b: number): number[] { const r: number[] = []; for (let i = a; i < b; i++) r.push(i); return r; }

/**
 * WRAM bytes that differ between backends for timing reasons only: DP scratch, scroll shadows mid-update, stack
 * slots. Ignored (zeroed before hashing) only in runs that mix backends; the report lists them.
 */
export const CROSS_NOISE: ReadonlySet<number> = new Set([...range(0x02, 0x0F), ...range(0x10E, 0x116), 0x1FEE, 0x1FF4]);
