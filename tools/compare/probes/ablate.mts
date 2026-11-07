// Which framework changes does a replay need? MesenCE runs a replay once (its hashes at every NMI key are kept in
// out/compare/ablate/), then each variant of the recomp runs alone against them, several at a time. A variant is a
// switch turned off or an executable built without a change. A run stops at its first difference in WRAM, VRAM,
// CGRAM or OAM: from there on the game is another game. The other regions count to the end.
//
//   node tools/compare/probes/ablate.mts <replay> <variants.json> [--rom x.sfc] [--jobs 4] [--out results.json] [--fresh]
// variants.json: [{ "name": "no per-clock DSP", "env": { "SNESRECOMP_DSP_PER_CLOCK": "0" }, "exe": "path", "interpreted": true }]
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { basename, join, resolve } from 'node:path';
import { runMesen } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';
import { readReplay } from '../replay.mts';
import type { Region } from '../diff.mts';
import { allInterpretedEnv } from './interp.mts';
import { REPO, defaultRom } from '../paths.mts';

const REGIONS: Region[] = ['wram', 'vram', 'cgram', 'oam', 'sram', 'picture', 'aram', 'dsp', 'sound'];
const SYNC = 4;                 // the first four regions decide whether the run is still the same game
const STALL_MS = 120_000;       // no key for this long: the game stopped (a sound handshake that never ends, a crash)

interface Variant { name: string; env?: Record<string, string>; exe?: string; interpreted?: boolean }
interface Reference { keys: Record<string, number[]> }

const argv = process.argv.slice(2);
const opt = (f: string) => { const i = argv.indexOf(f); return i < 0 ? undefined : argv[i + 1]; };
const [replayArg, variantsArg] = argv;
if (!variantsArg) { console.error('usage: ablate.mts <replay> <variants.json> [--rom x.sfc] [--jobs 4] [--out results.json] [--fresh]'); process.exit(2); }
const replay = resolve(replayArg), jobs = +(opt('--jobs') ?? 4);
const rom = opt('--rom') ? resolve(opt('--rom')!) : defaultRom();
if (!rom) { console.error('no ROM: pass --rom, or set SNESRECOMP_ROM'); process.exit(2); }
const movie = readReplay(replay), to = movie.count, midGame = movie.start?.midGame ?? false;
const base = { rom, align: 'nmi' as const, regions: REGIONS, replay, to, stride: 1, atNmi: true };

async function reference(): Promise<Reference> {
    const dir = join(REPO, 'out', 'compare', 'ablate'), file = join(dir, basename(replay) + '.json');
    mkdirSync(dir, { recursive: true });
    if (existsSync(file) && !argv.includes('--fresh')) return JSON.parse(readFileSync(file, 'utf8'));
    const keys: Reference['keys'] = {};
    await runMesen(base, (k, c) => { keys[k] = c; }).done;
    const ref = { keys };
    writeFileSync(file, JSON.stringify(ref));
    return ref;
}

interface Result { name: string; keys: number; synced: boolean; end: string; first: Record<string, number>; differ: Record<string, number>; seconds: number }

function run(v: Variant, ref: Reference): Promise<Result> {
    const exe = resolve(v.exe ?? defaultExe()), t0 = Date.now();
    const env = { ...(v.interpreted ? allInterpretedEnv(exe) : {}), ...v.env };
    const res: Result = { name: v.name, keys: 0, synced: true, end: 'ran to the end', first: {}, differ: {}, seconds: 0 };
    const saved = { ...process.env };
    Object.assign(process.env, env);
    let stall: NodeJS.Timeout;
    const job = runRecomp({ ...base, exe }, (k, c) => {
        stall.refresh();
        const r = ref.keys[k];
        if (!r) return;
        res.keys++;
        REGIONS.forEach((name, i) => {
            if (c[i] === r[i] || (midGame && k === 1 && name === 'picture')) return;
            res.first[name] ??= k;
            res.differ[name] = (res.differ[name] ?? 0) + 1;
            if (i < SYNC && res.synced) { res.synced = false; res.end = `${name} parted at ${k}`; job.kill(); }
        });
    });
    process.env = saved;
    stall = setTimeout(() => { if (res.synced) res.synced = false; res.end = `no key after ${res.keys} compared`; job.kill(); }, STALL_MS);
    return job.done.catch((e: Error) => { res.synced = false; res.end = e.message.slice(0, 160); }).then(() => {
        clearTimeout(stall);
        if (res.synced && res.keys < Object.keys(ref.keys).length) { res.synced = false; res.end = `ended after ${res.keys} keys`; }
        res.seconds = Math.round((Date.now() - t0) / 1000);
        return res;
    });
}

const ref = await reference();
const variants: Variant[] = JSON.parse(readFileSync(variantsArg, 'utf8'));
const results: Result[] = [];
let next = 0;
await Promise.all(Array.from({ length: jobs }, async (_, w) => {
    await new Promise(r => setTimeout(r, w * 2000));   // two copies started at once can fail to stage their mods
    while (next < variants.length) {
        const r = await run(variants[next++], ref);
        results.push(r);
        const others = Object.entries(r.differ).map(([k, n]) => `${k} ${n} (from ${r.first[k]})`).join(', ') || 'nothing differs';
        console.log(`${r.synced ? 'SYNC' : 'LOST'} | ${r.name} | ${r.end} | ${r.keys} keys | ${others} | ${r.seconds} s`);
    }
}));
if (opt('--out')) writeFileSync(opt('--out')!, JSON.stringify(results, null, 1));
