// The SPC700's instruction stream from NMI K to NMI K + n, MesenCE (Lua exec callback, spc.cycle) against the recomp
// (SNESRECOMP_SPC_EXEC_LOG): prints where pc, a, y or the SPC cycle first part, with the instructions before it.
// MesenCE first: its window sets the recomp's. For a port poll that parts while the 65816's clocks still agree.
//
//   node tools/compare/probes/spc-trace.mts <rom> <replay> <K> [n=1] [context=12]
import { resolve } from 'node:path';
import { runMesen, askMesenRng } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';
import { SYNC_AT } from '../game.mts';

const [romArg, replay, ks, nArg = '1', ctxArg = '12'] = process.argv.slice(2);
if (!ks) { console.error('usage: spc-trace.mts <rom> <replay> <K> [n] [context]'); process.exit(2); }
const rom = resolve(romArg), K = +ks, n = +nArg, ctx = +ctxArg, exe = defaultExe();
const base = { rom, align: 'nmi' as const, regions: ['wram' as const], mask: true, replay, to: K + n + 1, stride: 1_000_000, atNmi: true };
const { bytes: rng, frame: rngFrame } = await askMesenRng({ ...base, atNmi: false, to: SYNC_AT });
const lua = `
local spc_ref = nil
emu.addEventCallback(function()
  if nmi_taken == ${K} and not spc_ref then
    spc_ref = emu.addMemoryCallback(function(a)
      local st = emu.getState()
      if nmi_taken < ${K + n} then print(string.format("S %04X %d %02X %02X", a, st["spc.cycle"], st["spc.a"], st["spc.y"])) end
    end, emu.callbackType.exec, 0x0000, 0xFFFF, emu.cpuType.spc, emu.memType.spcMemory)
  end
end, emu.eventType.nmi)
`;
type Op = [pc: number, cycle: number, a: number, y: number];
const M: Op[] = [], R: Op[] = [];
await runMesen({ ...base, rng, lua, onNmi: () => {}, onLine: l => {
    if (l.startsWith('S ')) { const p = l.split(' '); M.push([parseInt(p[1], 16), Math.floor(+p[2] / 2), parseInt(p[3], 16), parseInt(p[4], 16)]); }
} }, () => {}).done;
if (!M.length) { console.error('no SPC instructions from MesenCE'); process.exit(1); }
process.env.SNESRECOMP_SPC_EXEC_LOG = `${M[0][1] - 64}-${M[M.length - 1][1]}`;
let rest = '';
await runRecomp({ ...base, exe, rng, rngFrame, onNmi: () => {}, onStderr: chunk => {
    const lines = (rest + chunk.toString()).split('\n');
    rest = lines.pop()!;
    for (const l of lines) {
        const m = /^\[sx\] ([0-9A-F]{4}) (\d+) .*a=([0-9A-F]{2}) y=([0-9A-F]{2})/.exec(l);
        if (m) R.push([parseInt(m[1], 16), +m[2], parseInt(m[3], 16), parseInt(m[4], 16)]);
    }
} }, () => {}).done;
const hx = (v: number, w = 2) => v.toString(16).toUpperCase().padStart(w, '0');
const op = (o: Op | undefined) => o ? `$${hx(o[0], 4)} ${o[1]} a=${hx(o[2])} y=${hx(o[3])}` : '-';
console.log(`MesenCE ${M.length} SPC instructions from cycle ${M[0][1]}, recomp ${R.length} from ${R[0]?.[1]}`);
const r0 = R.findIndex(r => r[1] >= M[0][1]);
console.log(`first: MesenCE ${op(M[0])}; recomp ${op(R[r0])}`);
const shift = R[r0] ? R[r0][1] - M[0][1] : 0;
for (let i = 0; i < M.length && r0 + i < R.length; i++) {
    const m = M[i], r = R[r0 + i];
    if (m[0] === r[0] && m[1] + shift === r[1] && m[2] === r[2] && m[3] === r[3]) continue;
    console.log(`part at #${i} (cycle shift so far ${shift}):`);
    for (let j = Math.max(0, i - ctx); j <= i + 3; j++) console.log(`${j === i ? '>' : ' '} ${op(M[j]).padEnd(32)} ${op(R[r0 + j])}`);
    process.exit(0);
}
console.log(`no difference; recomp - MesenCE = ${shift} cycles`);
