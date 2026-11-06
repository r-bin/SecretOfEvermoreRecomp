// The recomp's native build against itself all-interpreted (which matches MesenCE through both replays): each NMI's
// master clock and interrupted address, and the WRAM hash at each NMI key. Prints the first NMI whose clock differs
// and the first key whose WRAM differs. MesenCE runs only to fetch the RNG copied in at key 115.
//
//   node tools/compare/probes/native-vs-interp.mts <rom> <replay.evsmv> <to> [exe]
import { resolve } from 'node:path';
import { askMesenRng } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';
import { SYNC_AT } from '../game.mts';
import { allInterpretedEnv } from './interp.mts';
import { readReplay } from '../replay.mts';

const [romArg, replay, toArg, exeArg] = process.argv.slice(2);
if (!toArg) { console.error('usage: native-vs-interp.mts <rom> <replay.evsmv> <to> [exe]'); process.exit(2); }
const rom = resolve(romArg), to = +toArg, exe = exeArg ? resolve(exeArg) : defaultExe();
const base = { rom, align: 'nmi' as const, regions: ['wram' as const], mask: true, replay, to, stride: 1, atNmi: true };
// A movie from mid-game starts from one state on every run: nothing to copy.
const { bytes: rng, frame: rngFrame } = readReplay(replay).start?.midGame ? { bytes: null, frame: 0 } : await askMesenRng({ ...base, atNmi: false, to: SYNC_AT });

type Side = { nmi: Map<number, [number, number]>; wram: Map<number, number> };
function run(env: Record<string, string>): { side: Side; done: Promise<unknown> } {
    const side: Side = { nmi: new Map(), wram: new Map() };
    const saved = { ...process.env };
    Object.assign(process.env, env);
    const job = runRecomp({ ...base, exe, rng, rngFrame, onNmi: (k, clock, pc) => side.nmi.set(k, [clock, pc]) },
        (key, crcs) => side.wram.set(key, crcs[0]));
    for (const k of Object.keys(env)) if (saved[k] === undefined) delete process.env[k]; else process.env[k] = saved[k];
    return { side, done: job.done };
}
const t0 = Date.now();
// Two copies of one executable started at once can fail to stage its mods ("cannot publish mod state"): start apart.
const nat = run({});
await new Promise(r => setTimeout(r, 1500));
const int = run(allInterpretedEnv(exe));
await Promise.all([nat.done, int.done]);
const hx = (n: number) => '$' + n.toString(16).toUpperCase();
let firstWram = -1, same = 0, prev = 0;
for (const [k, [c, pc]] of [...int.side.nmi].sort((a, b) => a[0] - b[0])) {
    const n = nat.side.nmi.get(k);
    if (!n) break;
    if (n[0] !== c || n[1] !== pc) { console.log(`first NMI apart: ${k}: interp clock ${c} at ${hx(pc)}, native ${n[0]} at ${hx(n[1])} (${n[0] - c}); NMI ${k - 1} at clock ${prev}`); break; }
    prev = c;
}
for (const [k, h] of [...int.side.wram].sort((a, b) => a[0] - b[0])) {
    const n = nat.side.wram.get(k);
    if (n === undefined) continue;
    if (n !== h) { if (firstWram < 0) firstWram = k; } else if (firstWram < 0) same++;
}
console.log(`NMIs compared: ${int.side.nmi.size} / ${nat.side.nmi.size}; WRAM keys identical before the first difference: ${same}; first WRAM key apart: ${firstWram < 0 ? 'none' : firstWram}; ${((Date.now() - t0) / 1000).toFixed(0)} s`);
