// Every CPU write to $2140 from power-on to NMI <to>, and how many $2140 reads follow before the next write:
// MesenCE (Lua) against the all-interpreted recomp (SNESRECOMP_CYC_WATCH, each instruction's operand decoded from
// the ROM). Equal counts mean the SPC700 answered within the same poll pass on both sides.
//
//   node tools/compare/probes/port-handshakes.mts <rom> <replay.evsmv> <to> [show=20]
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { runMesen, askMesenRng } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';
import { SYNC_AT } from '../game.mts';
import { allInterpretedEnv, cycLines } from './interp.mts';

const [romArg, replay, to, show = '20'] = process.argv.slice(2);
const rom = resolve(romArg);
if (!to) { console.error('usage: port-handshakes.mts <rom> <replay.evsmv> <to> [show]'); process.exit(2); }
const NMI_VECTOR = 0x00823A, image = readFileSync(rom);
const exe = defaultExe();
Object.assign(process.env, allInterpretedEnv(exe), { SNESRECOMP_CYC_WATCH: '000000-FFFFFF' });
const base = { rom, align: 'nmi' as const, regions: ['wram' as const], mask: true, replay, to: +to, stride: 1_000_000, atNmi: true };
const { bytes: rng, frame: rngFrame } = await askMesenRng({ ...base, atNmi: false, to: SYNC_AT });

type Handshake = { value: number; reads: number; nmi: number };
const mesen: Handshake[] = [], recomp: Handshake[] = [];
const add = (list: Handshake[], write: boolean, value: number, nmi: number) => {
    if (write) list.push({ value, reads: 0, nmi });
    else if (list.length) list[list.length - 1].reads++;
};
// Absolute and long accesses to $2140 (HiROM: the ROM offset is the bus address's low 22 bits).
const WRITES = new Set([0x8D, 0x8E, 0x8C, 0x9C, 0x8F]), READS = new Set([0xCD, 0xAD, 0x2C, 0xEC, 0xCC, 0xAE, 0xAC, 0xCF, 0xAF]);
const LONG = new Set([0x8F, 0xCF, 0xAF]);
const kinds = new Map<number, 0 | 1 | 2>();
const kindOf = (pc: number): 0 | 1 | 2 => {
    let k = kinds.get(pc);
    if (k !== undefined) return k;
    const o = (pc & 0x3FFFFF) % image.length, op = image[o];
    const port = (image[o + 1] | image[o + 2] << 8) === 0x2140 && (!LONG.has(op) || (image[o + 3] & 0x40) === 0);
    k = port ? (WRITES.has(op) ? 2 : READS.has(op) ? 1 : 0) : 0;
    kinds.set(pc, k);
    return k;
};
const isRom = (pc: number) => pc >= 0xC00000 || (pc & 0xFFFF) >= 0x8000 && (pc < 0x400000 || pc >= 0x800000);
const lua = `
for _, bank in ipairs({0x00, 0x80}) do
  local b = bank * 0x10000 + 0x2140
  emu.addMemoryCallback(function(a, v) print(string.format("Q W %02X %d", v, nmi_taken)) end, emu.callbackType.write, b, b)
  emu.addMemoryCallback(function(a, v) print(string.format("Q R %02X %d", v, nmi_taken)) end, emu.callbackType.read, b, b)
end
`;
let nmis = 0;
await Promise.all([
    runMesen({ ...base, rng, lua, onNmi: () => {}, onLine: l => {
        if (l.startsWith('Q ')) { const p = l.split(' '); add(mesen, p[1] === 'W', parseInt(p[2], 16), +p[3]); }
    } }, () => {}).done,
    runRecomp({ ...base, exe, rng, rngFrame, onNmi: () => {}, onStderr: cycLines(pc => {
        if (pc === NMI_VECTOR) nmis++;
        if (!isRom(pc)) return;
        const k = kindOf(pc);
        if (k) add(recomp, k === 2, -1, nmis);
    }) }, () => {}).done,
]);
console.log(`writes to $2140: MesenCE ${mesen.length}, recomp ${recomp.length}`);
let shown = 0, same = 0;
const n = Math.min(mesen.length, recomp.length);
for (let i = 0; i < n; i++) {
    const m = mesen[i], r = recomp[i];
    if (m.reads === r.reads) { same++; continue; }
    if (shown++ < +show) console.log(`write #${i} $${m.value.toString(16).toUpperCase()} (NMI ${m.nmi}): reads MesenCE ${m.reads}, recomp ${r.reads}`);
}
console.log(`same number of reads: ${same} of ${n} (${(100 * same / n).toFixed(1)} %)`);
