// One frame, instruction by instruction: MesenCE (Lua exec callbacks, master clock) against the all-interpreted
// recomp (SNESRECOMP_CYC_WATCH charges), from NMI K's entry to NMI K+1's. Prints each instruction whose clocks
// differ by more than a 40-clock DRAM refresh stall, and where the two instruction streams part. Passes of the
// idle loop $80:858F (wait_for_nmi) are folded into one entry: their count is the loop's phase, not a cost.
//
//   node tools/compare/probes/frame-trace.mts <rom> <replay.evsmv> <K> [show=40]
import { resolve } from 'node:path';
import { runMesen, askMesenRng } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';
import { SYNC_AT } from '../game.mts';
import { allInterpretedEnv, cycLines } from './interp.mts';

const [romArg, replay, ks, show = '40'] = process.argv.slice(2);
const rom = resolve(romArg);
if (!ks) { console.error('usage: frame-trace.mts <rom> <replay.evsmv> <K> [show]'); process.exit(2); }
const K = +ks, NMI_VECTOR = 0x00823A, IDLE = new Set([0x80858F, 0x808592]);
const exe = defaultExe();
Object.assign(process.env, allInterpretedEnv(exe), { SNESRECOMP_CYC_WATCH: '000000-FFFFFF' });
const base = { rom, align: 'nmi' as const, regions: ['wram' as const], mask: true, replay, to: K + 2, stride: 1_000_000, atNmi: true };
const { bytes: rng, frame: rngFrame } = await askMesenRng({ ...base, atNmi: false, to: SYNC_AT });
const lua = `
local tr_on = false
emu.addEventCallback(function() tr_on = (nmi_taken == ${K}) end, emu.eventType.nmi)
emu.addMemoryCallback(function(a)
  if tr_on then print(string.format("T %06X %d", a, emu.getState()["masterClock"])) end
end, emu.callbackType.exec, 0x000000, 0xFFFFFF)
`;
const M: [number, number][] = [], R: [number, number][] = [], entry = new Map<number, number>();
let nmis = 0;
await Promise.all([
    runMesen({ ...base, rng, lua, onNmi: () => {}, onLine: l => {
        if (l.startsWith('T ')) { const p = l.split(' '); M.push([parseInt(p[1], 16), +p[2]]); }
    } }, () => {}).done,
    runRecomp({ ...base, exe, rng, rngFrame, onNmi: (k, clock) => entry.set(k, clock), onStderr: cycLines((pc, clocks) => {
        if (pc === NMI_VECTOR) nmis++;
        if (nmis === K) R.push([pc, clocks]);
    }) }, () => {}).done,
]);
// [pc, clocks to the next instruction], idle-loop passes folded (-1)
const fold = (a: [number, number][]) => {
    const o: [number, number][] = [];
    for (const [pc, d] of a) {
        if (IDLE.has(pc) && o.length && o[o.length - 1][0] === -1) o[o.length - 1][1] += d;
        else o.push([IDLE.has(pc) ? -1 : pc, d]);
    }
    return o;
};
const Mf = fold(M.slice(0, -1).map((m, i) => [m[0], M[i + 1][1] - m[1]]));
const Rf = fold(R);
const name = (pc: number) => pc === -1 ? 'idle' : '$' + pc.toString(16).toUpperCase();
console.log(`NMI ${K}: MesenCE ${M.length} instructions from clock ${M[0]?.[1]}, recomp ${R.length} from ${entry.get(K)}`);
let shown = 0, refresh = 0, total = 0;
for (let i = 0; i < Math.min(Mf.length, Rf.length); i++) {
    if (Mf[i][0] !== Rf[i][0]) { console.log(`#${i}: the streams part: MesenCE ${name(Mf[i][0])}, recomp ${name(Rf[i][0])}`); break; }
    const x = Mf[i][1] - Rf[i][1];
    if (x === 40 && Mf[i][0] !== -1) { refresh++; continue; }
    if (x !== 0) {
        total += x;
        if (shown++ < +show) console.log(`#${i} ${name(Mf[i][0])}: MesenCE ${Mf[i][1]} clocks, recomp ${Rf[i][1]} (${x > 0 ? '+' : ''}${x}, total ${total})`);
    }
}
console.log(`refresh stalls matched: ${refresh}; NMI ${K + 1} on the recomp at ${entry.get(K + 1)}`);
