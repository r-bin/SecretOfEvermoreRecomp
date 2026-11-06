// A reference run kept in a file, so the recomp can be checked without MesenCE: the first line "# rng <hex> <frame>" (the
// bytes the reference gave the recomp), then one line per key, "key crc crc ...". CRCs only: no game data.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import type { Crcs, Region } from './diff.mts';

export interface Recorded { regions: Region[]; rng: Uint8Array | null; rngFrame: number; keys: Map<number, Crcs>; stride: number }

export function loadRecorded(file: string): Recorded {
    const keys = new Map<number, Crcs>();
    let rng: Uint8Array | null = null, rngFrame = 115, regions: Region[] = [], stride = 60;
    for (const line of readFileSync(file, 'utf8').split('\n')) {
        const p = line.split(' ');
        if (p[0] === '#' && p[1] === 'rng') { rng = p[2] === '-' ? null : Uint8Array.from(Buffer.from(p[2], 'hex')); rngFrame = Number(p[3]); }
        else if (p[0] === '#' && p[1] === 'regions') regions = p[2].split(',') as Region[];
        else if (p[0] === '#' && p[1] === 'stride') stride = Number(p[2]);
        else if (line && p[0] !== '#') keys.set(Number(p[0]), p.slice(1).map(x => parseInt(x, 16)));
    }
    return { regions, rng, rngFrame, keys, stride };
}

export function saveRecorded(file: string, r: Recorded): void {
    mkdirSync(dirname(file), { recursive: true });
    const lines = [`# rng ${r.rng ? Buffer.from(r.rng).toString('hex') : '-'} ${r.rngFrame}`, `# regions ${r.regions.join(',')}`, `# stride ${r.stride}`,
        ...[...r.keys].map(([k, c]) => k + ' ' + c.map(x => (x >>> 0).toString(16).padStart(8, '0')).join(' '))];
    writeFileSync(file, lines.join('\n') + '\n');
}
