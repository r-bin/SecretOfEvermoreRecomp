// A replay for the backends, from either format: an .evsmv recording or a MesenCE movie (.mmo). `start` is how a movie
// began: MesenCE loads the save state itself, the recomp its memory, FastROM, SPC700 and S-DSP (the RNG follows at the copy
// at key 115). SOE_MMO_FRESH=1 compares the backends instead: both power on with the movie's WRAM and SRAM only, in
// lockstep, though that run is not the recording (other RNG, the sound start 20 fields shorter).
import { readFileSync } from 'node:fs';
import { parseEvsmv, parseMesenMovie } from './movie.mts';

const POLL_SHIFT = 1;

export interface Replay { pads: Uint16Array; count: number; start: { state: Uint8Array; midGame: boolean; fields: Uint8Array; wram: Uint8Array; sram: Uint8Array; aram: Uint8Array; dsp: Uint8Array; hw: Uint8Array; fresh: boolean } | null }

export function readReplay(path: string): Replay {
    if (/\.mmo$/i.test(path)) {
        const movie = parseMesenMovie(readFileSync(path));
        // Row r is the poll at the end of frame r; the backends take pads[n] at the poll after n frames.
        const pads = new Uint16Array(movie.count + POLL_SHIFT);
        pads.set(movie.pads, POLL_SHIFT);
        return { pads, count: pads.length, start: movie.start && { ...movie.start, fresh: process.env.SOE_MMO_FRESH === '1' } };
    }
    const pads = parseEvsmv(readFileSync(path, 'utf8'));
    return { pads, count: pads.length, start: null };
}
