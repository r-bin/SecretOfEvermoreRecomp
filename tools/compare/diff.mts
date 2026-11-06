// The pure part of compare: checkpoints in, first divergence and byte / write differences out. No emulator, no files,
// so the unit test drives it with synthetic hash streams (tests/compare/compare-units.test.mts).
import { crc32 } from 'node:zlib';

export type Region = 'wram' | 'vram' | 'cgram' | 'oam' | 'sram' | 'picture' | 'aram' | 'dsp' | 'sound';
/** `sound`: the S-DSP's state from emu.getState(), in the recomp glue's order (CmpSound). */
const VOICE_FIELDS = ['envVolume', 'prevCalculatedEnv', 'interpolationPos', 'brrAddress', 'brrOffset', 'keyOnDelay', 'envOut', 'bufferPos', ...Array.from({ length: 12 }, (_, i) => `sampleBuffer${i}`)];
export const SOUND_FIELDS = [
    ...['step', 'counter', 'noiseLfsr', 'outSamples0', 'outSamples1', 'voiceOutput'],
    ...Array.from({ length: 16 }, (_, i) => `echoHistory${i}`),
    ...['echoHistoryPos', 'echoPointer', 'echoOffset', 'echoLength', 'echoIn0', 'echoIn1', 'echoOut0', 'echoOut1'],
].map(f => `spc.dsp.${f}`).concat(Array.from({ length: 8 }, (_, v) => VOICE_FIELDS.map(f => `spc.dsp.voices[${v}].${f}`)).flat());
/** Battery RAM's size is Evermore's; `sound` is the S-DSP's state in 32-bit values, `picture` 256 x 224 pixels of 4 bytes. */
export const REGION_SIZE: Record<Region, number> = { wram: 0x20000, vram: 0x10000, cgram: 0x200, oam: 0x220, sram: 0x2000, picture: 256 * 224 * 4, aram: 0x10000, dsp: 0x80, sound: SOUND_FIELDS.length * 4 };
/** Every RAM of the console, in the order a run hashes them. */
export const ALL_RAM: Region[] = ['wram', 'vram', 'cgram', 'oam', 'sram', 'aram', 'dsp'];
/** The order of regions in a job: the CPU side first, then what MesenCE reads at the SPC700's next instruction. */
const ORDER: Region[] = ['wram', 'vram', 'cgram', 'oam', 'sram', 'picture', 'aram', 'dsp', 'sound'];
export const SPC_SIDE: ReadonlySet<Region> = new Set(['aram', 'dsp', 'sound']);
export const inOrder = (regions: Region[]): Region[] => ORDER.filter(r => regions.includes(r));

/** One region's CRC-32 per entry of `regions`, at one key (a frame count, or the game's NMI counter $0100). */
export type Crcs = number[];

/** The one hash over all regions the default mode compares: a CRC-32 of the region CRCs. */
export function combined(crcs: Crcs): number {
    return crc32(new Uint8Array(Uint32Array.from(crcs).buffer));
}

export interface Divergence {
    /** The last key both agreed on (0 = none compared before it). */
    lastGood: number;
    /** The first key they differ at. */
    key: number;
    /** Indexes into `regions` of the regions that differ there. */
    regions: number[];
}

/** One region's differences over a run: a run compares every region at every key. */
export interface RegionResult {
    differing: number;
    /** The keys that differ, as [first, last] runs. */
    runs: [number, number][];
}

export interface TargetResult {
    /** The last key compared, whether it matched or not. */
    lastCompared: number;
    compared: number;
    differing: number;
    first: Divergence | null;
    /** By index into `regions`. */
    byRegion: RegionResult[];
}

/**
 * Compares targets with one reference as checkpoints arrive, in any order between setups but in rising key order
 * within one. A key either side lacks is not compared; keys at or below `skipThrough` are not compared either
 * (before the RNG sync of a cross-backend run).
 */
export class Tracker {
    readonly ref = new Map<number, Crcs>();
    private readonly pending: Map<number, Crcs>[];
    private readonly lastMatch: number[];
    readonly results: TargetResult[];
    private readonly skipThrough: number;
    private readonly stride: number;

    constructor(targets: number, skipThrough = -1, stride = 1) {
        this.skipThrough = skipThrough;
        this.stride = stride;
        this.pending = Array.from({ length: targets }, () => new Map());
        this.lastMatch = new Array(targets).fill(0);
        this.results = Array.from({ length: targets }, () => ({ lastCompared: 0, compared: 0, differing: 0, first: null, byRegion: [] }));
    }

    /** Returns the targets that diverged for the first time at this key. */
    addRef(key: number, crcs: Crcs): number[] {
        this.ref.set(key, crcs);
        const hit: number[] = [];
        this.pending.forEach((p, i) => {
            const t = p.get(key);
            if (!t) return;
            p.delete(key);
            if (this.check(i, key, t)) hit.push(i);
        });
        return hit;
    }

    /** True when this target diverged for the first time at this key. */
    addTarget(i: number, key: number, crcs: Crcs): boolean {
        if (key <= this.skipThrough) return false;
        const r = this.ref.get(key);
        if (!r) { this.pending[i].set(key, crcs); return false; }
        return this.check(i, key, crcs);
    }

    private check(i: number, key: number, crcs: Crcs): boolean {
        const res = this.results[i];
        res.compared++;
        res.lastCompared = Math.max(res.lastCompared, key);
        const r = this.ref.get(key)!;
        if (combined(r) === combined(crcs)) {
            if (!res.first) this.lastMatch[i] = Math.max(this.lastMatch[i], key);
            return false;
        }
        res.differing++;
        crcs.forEach((c, j) => {
            if (c === r[j]) return;
            const region = res.byRegion[j] ??= { differing: 0, runs: [] };
            region.differing++;
            const last = region.runs[region.runs.length - 1];
            if (last && last[1] === key - this.stride) last[1] = key;
            else region.runs.push([key, key]);
        });
        if (res.first) return false;
        res.first = { lastGood: this.lastMatch[i], key, regions: crcs.flatMap((c, j) => c === r[j] ? [] : [j]) };
        return true;
    }
}

export interface ByteDiff { offset: number; a: number; b: number }

/** The differing bytes of two snapshots of one region, skipping the ignored offsets; at most `max`. */
export function byteDiff(a: Uint8Array, b: Uint8Array, ignore: ReadonlySet<number> = new Set(), max = Infinity): { diffs: ByteDiff[]; total: number } {
    const diffs: ByteDiff[] = [];
    let total = 0;
    const n = Math.min(a.length, b.length);
    for (let i = 0; i < n; i++) {
        if (a[i] === b[i] || ignore.has(i)) continue;
        total++;
        if (diffs.length < max) diffs.push({ offset: i, a: a[i], b: b[i] });
    }
    return { diffs, total };
}

/** Consecutive offsets as [first, last] runs, for a short listing. */
export function runs(offsets: number[]): [number, number][] {
    const out: [number, number][] = [];
    for (const o of [...offsets].sort((x, y) => x - y)) {
        const last = out[out.length - 1];
        if (last && o === last[1] + 1) last[1] = o;
        else out.push([o, o]);
    }
    return out;
}
