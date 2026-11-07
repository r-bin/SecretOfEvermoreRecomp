// The SPC700's reads of the S-DSP ($F3) from NMI K to NMI K + n, MesenCE (Lua read callback) against the recomp
// (SNESRECOMP_DSP_LOG): prints the first read whose value, register or cycle differs, with the reads before it.
// spc-trace.mts compares pc, a and y only, and misses a read the driver moves with MOV dp,dp (dsp-endx-read.md).
//
//   node tools/compare/probes/dsp-reads.mts <rom> <replay> <K> [n=1] [context=8]
import { resolve } from 'node:path';
import { runMesen } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';

const [romArg, replay, ks, nArg = '1', ctxArg = '8'] = process.argv.slice(2);
if (!ks) { console.error('usage: dsp-reads.mts <rom> <replay> <K> [n] [context]'); process.exit(2); }
const rom = resolve(romArg), K = +ks, n = +nArg, ctx = +ctxArg, exe = defaultExe();
const base = { rom, align: 'nmi' as const, regions: ['wram' as const], replay, to: K + n + 1, stride: 1_000_000, atNmi: true };
const lua = `
local ref = nil
emu.addEventCallback(function()
  if nmi_taken == ${K} and not ref then
    ref = emu.addMemoryCallback(function(a, v)
      local st = emu.getState()
      if nmi_taken < ${K + n} then print(string.format("F3 %d %02X %02X", st["spc.cycle"], v, st["spc.dspReg"])) end
    end, emu.callbackType.read, 0x00F3, 0x00F3, emu.cpuType.spc, emu.memType.spcMemory)
  end
end, emu.eventType.nmi)
`;
type Read = [cycle: number, value: number, reg: number];
const M: Read[] = [], R: Read[] = [];
await runMesen({ ...base, lua, onNmi: () => {}, onLine: l => {
    if (l.startsWith("F3 ")) { const p = l.split(" "); M.push([Math.floor(+p[1] / 2), parseInt(p[2], 16), parseInt(p[3], 16)]); }
} }, () => {}).done;
if (!M.length) { console.error('no $F3 reads from MesenCE'); process.exit(1); }
const to = M[M.length - 1][0] + 64;
process.env.SNESRECOMP_DSP_LOG = '1';
let rest = '';
await runRecomp({ ...base, exe, onNmi: () => {}, onStderr: chunk => {
    const lines = (rest + chunk.toString()).split('\n');
    rest = lines.pop()!;
    for (const l of lines) {
        const m = /^\[pl\] SD ([0-9A-F]{2}) (\d+) ([0-9A-F]{2})/.exec(l);
        if (m && +m[2] >= M[0][0] - 64 && +m[2] <= to) R.push([+m[2], parseInt(m[1], 16), parseInt(m[3], 16)]);
    }
} }, () => {}).done;
const hx = (v: number) => v.toString(16).toUpperCase().padStart(2, '0');
const rd = (r: Read | undefined) => r ? `${r[0]} $${hx(r[2])}=${hx(r[1])}` : '-';
console.log(`MesenCE ${M.length} $F3 reads from cycle ${M[0][0]}, recomp ${R.length} from ${R[0]?.[0]}`);
const r0 = R.findIndex(r => r[0] >= M[0][0] - 2 && r[2] === M[0][2]);
const shift = R[r0] ? R[r0][0] - M[0][0] : 0;
for (let i = 0; i < M.length && r0 + i < R.length; i++) {
    const m = M[i], r = R[r0 + i];
    if (m[1] === r[1] && m[2] === r[2] && m[0] + shift === r[0]) continue;
    console.log(`part at read #${i} (cycle shift ${shift}):`);
    for (let j = Math.max(0, i - ctx); j <= i + 3; j++) console.log(`${j === i ? '>' : ' '} ${rd(M[j]).padEnd(20)} ${rd(R[r0 + j])}`);
    process.exit(0);
}
console.log(`no difference in ${M.length} reads; recomp - MesenCE = ${shift} cycles`);
