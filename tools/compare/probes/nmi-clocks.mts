// Each NMI's master clock and interrupted address, and the beam's H clock and line there, MesenCE against the recomp,
// up to key K: prints where the differences change (MesenCE's Lua reports the clock modulo 2^32, so both are compared
// so). The recomp's beam comes from its glue (SOE_COMPARE_NMI_BEAM, which walks the beam to the NMI's clock).
//
//   node tools/compare/probes/nmi-clocks.mts <rom> <replay.evsmv> <K> [exe] [from: print every NMI from here]
import { resolve } from 'node:path';
import { runMesen, askMesenRng } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';
import { SYNC_AT } from '../game.mts';

const [romArg, replay, ks, exeArg, fromArg] = process.argv.slice(2);
const FROM = fromArg ? +fromArg : 0;
if (!ks) { console.error('usage: nmi-clocks.mts <rom> <replay.evsmv> <K> [exe]'); process.exit(2); }
const rom = resolve(romArg), K = +ks, exe = exeArg ? resolve(exeArg) : defaultExe();
const base = { rom, align: 'nmi' as const, regions: ['wram' as const], mask: true, replay, to: K, stride: 1_000_000, atNmi: true };
const { bytes: rng, frame: rngFrame } = await askMesenRng({ ...base, atNmi: false, to: SYNC_AT });
const M = new Map<number, [number, number]>(), R = new Map<number, [number, number]>();
const MB = new Map<number, string>(), RB = new Map<number, string>();
// The beam at each NMI: MesenCE's H clock and line, the recomp glue's `[nmibeam]` stderr line.
const lua = `emu.addEventCallback(function() local st = emu.getState(); print(string.format("B %d %d %d", nmi_taken, st["memoryManager.hClock"], st["ppu.scanline"])) end, emu.eventType.nmi)`;
let rest = '';
process.env.SOE_COMPARE_NMI_BEAM = '1';
await Promise.all([
    runMesen({ ...base, rng, lua, onNmi: (k, c, pc) => M.set(k, [c, pc]), onLine: l => {
        if (l.startsWith('B ')) { const p = l.split(' '); MB.set(+p[1], p[2] + ' ' + p[3]); }
    } }, () => {}).done,
    runRecomp({ ...base, exe, rng, rngFrame, onNmi: (k, c, pc) => R.set(k, [c, pc]), onStderr: chunk => {
        const lines = (rest + chunk.toString()).split('\n');
        rest = lines.pop()!;
        for (const l of lines) { const m = /^\[nmibeam\] (\d+) (-?\d+) (\d+)/.exec(l); if (m) RB.set(+m[1], m[2] + ' ' + m[3]); }
    } }, () => {}).done,
]);
let lastBeam = '', beamShown = 0;
for (let k = 1; k <= K; k++) {
    const a = MB.get(k), b = RB.get(k);
    if (!a || !b) continue;
    const [ah, av] = a.split(' ').map(Number), [bh, bv] = b.split(' ').map(Number);
    const d = `${bh - ah} clocks, ${bv - av} lines (MesenCE ${a}, recomp ${b})`;
    const key = `${bh - ah} ${bv - av}`;
    if (k >= FROM ? true : key !== lastBeam && beamShown++ < 40) console.log(`beam at NMI ${k}: recomp - MesenCE = ${d}`);
    lastBeam = key;
}
const W = 2 ** 32, hx = (n: number) => '$' + n.toString(16).toUpperCase();
let last: number | null = null, shown = 0;
for (let k = 1; k <= K; k++) {
    const m = M.get(k), r = R.get(k);
    if (!m || !r) continue;
    const d = ((r[0] - m[0]) % W + W + W / 2) % W - W / 2;
    if (d !== last || m[1] !== r[1]) {
        if (shown++ < 60) console.log(`NMI ${k}: recomp - MesenCE = ${d} clocks; interrupted ${hx(m[1])} / ${hx(r[1])}`);
        last = d;
    }
}
console.log(`compared ${Math.min(M.size, R.size)} NMIs`);
