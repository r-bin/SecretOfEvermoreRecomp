// The PPU/WRAM port reads ($2137-$213F, $2180) between NMI K-1 and K, MesenCE against the recomp: value and master
// clock of each, in order, and the first read whose value differs. For an RNG seed that parts (the reset handler's
// seed reads $2137-$213D, $2180 and $213B: knowledge entry rom/808020).
//
//   node tools/compare/probes/ppu-reads.mts <rom> <replay.evsmv> <K> [exe]
import { resolve } from 'node:path';
import { runMesen, askMesenRng } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';
import { SYNC_AT } from '../game.mts';

const [romArg, replay, ks, exeArg] = process.argv.slice(2);
if (!ks) { console.error('usage: ppu-reads.mts <rom> <replay.evsmv> <K> [exe]'); process.exit(2); }
const rom = resolve(romArg), K = +ks, exe = exeArg ? resolve(exeArg) : defaultExe();
const base = { rom, align: 'nmi' as const, regions: ['wram' as const], mask: true, replay, to: K + 1, stride: 1_000_000, atNmi: true };
const { bytes: rng, frame: rngFrame } = await askMesenRng({ ...base, atNmi: false, to: SYNC_AT });
const lua = `
local function rd(a, v)
  if nmi_taken == ${K - 1} then local st = emu.getState(); print(string.format("R %04X %02X %d %d %d", a & 0xFFFF, v, st["masterClock"], st["memoryManager.hClock"], st["ppu.scanline"])) end
end
for _, b in ipairs({0x000000, 0x800000}) do
  emu.addMemoryCallback(rd, emu.callbackType.read, b + 0x2137, b + 0x213F)
  emu.addMemoryCallback(rd, emu.callbackType.read, b + 0x2180, b + 0x2180)
end
`;
const M: [number, number, number][] = [], R: [number, number, number][] = [];
const nmi = new Map<number, number>();
const MH: string[] = [];
let rest = '';
process.env.SNESRECOMP_PPU_READ_LOG = '1';
process.env.SNESRECOMP_HV_LOG = '1';
const HV: string[] = [];
await Promise.all([
    runMesen({ ...base, rng, lua, onNmi: () => {}, onLine: l => {
        if (l.startsWith('R ')) { const p = l.split(' '); M.push([parseInt(p[1], 16), parseInt(p[2], 16), +p[3]]); if (p[1] === '2137') MH.push(`h clock ${p[4]} line ${p[5]}`); }
    } }, () => {}).done,
    runRecomp({ ...base, exe, rng, rngFrame, onNmi: (k, clock) => nmi.set(k, clock), onStderr: chunk => {
        const lines = (rest + chunk.toString()).split('\n');
        rest = lines.pop()!;
        for (const l of lines) {
            const m = /^\[ppur\] ([0-9A-F]{4}) ([0-9A-F]{2}) (\d+)/.exec(l);
            if (m) R.push([parseInt(m[1], 16), parseInt(m[2], 16), +m[3]]);
            const hv = /^\[hv\] h=(-?\d+) v=(\d+) dot=(\d+) at=(\d+)/.exec(l);
            if (hv) HV.push(`h=${hv[1]} v=${hv[2]} dot=${hv[3]} at=${hv[4]}`);
        }
    } }, () => {}).done,
]);
const lo = nmi.get(K - 1) ?? 0, hi = nmi.get(K) ?? Infinity;
const Rw = R.filter(r => r[2] >= lo && r[2] < hi);
const hx = (n: number, w = 2) => n.toString(16).toUpperCase().padStart(w, '0');
console.log(`NMI ${K - 1}..${K}: MesenCE ${M.length} reads, recomp ${Rw.length} (its $2180 reads are not logged)`);
const Mp = M.filter(m => m[0] !== 0x2180);
for (let i = 0; i < Math.max(Mp.length, Rw.length); i++) {
    const m = Mp[i], r = Rw[i];
    const mark = !m || !r || m[0] !== r[0] || m[1] !== r[1] ? '>' : ' ';
    console.log(`${mark} $${m ? hx(m[0], 4) : '----'} ${m ? hx(m[1]) : '--'} @${m?.[2] ?? '-'}   $${r ? hx(r[0], 4) : '----'} ${r ? hx(r[1]) : '--'} @${r?.[2] ?? '-'}`);
}
for (const m of M.filter(m => m[0] === 0x2180)) console.log(`MesenCE $2180 ${hx(m[1])} @${m[2]}`);
console.log('MesenCE at its $2137 reads:', MH.join('; '));
const lo2 = nmi.get(K - 1) ?? 0, hi2 = nmi.get(K) ?? Infinity;
console.log('recomp latches:', HV.filter(x => { const at = +x.split('at=')[1]; return at >= lo2 && at < hi2; }).join('; '));
