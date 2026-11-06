// Instruction by instruction in a master-clock window: the recomp's native build against itself all-interpreted.
// Both run with SNESRECOMP_CYC_WATCH: the interpreter's `[cyc] ... start=` lines and the bus-exact compiled code's
// `[acyc] pc= start=` lines (snesrecomp built with SNESRECOMP_EXACT_BUS=1). Prints where the (pc, start clock)
// streams part, with the instructions before it. Take the window from native-vs-interp.mts (the last NMI both
// agree on, to the first that differs).
//
//   node tools/compare/probes/clock-trace.mts <rom> <replay.evsmv> <to-key> <from-clock> <to-clock> [exe] [context=12]
import { resolve } from 'node:path';
import { askMesenRng } from '../mesen.mts';
import { runRecomp, defaultExe } from '../recomp.mts';
import { SYNC_AT } from '../game.mts';
import { allInterpretedEnv } from './interp.mts';

const [romArg, replay, toArg, fromArg, untilArg, exeArg, ctxArg = '12'] = process.argv.slice(2);
if (!untilArg) { console.error('usage: clock-trace.mts <rom> <replay> <to-key> <from-clock> <to-clock> [exe] [context]'); process.exit(2); }
const rom = resolve(romArg), to = +toArg, from = +fromArg, until = +untilArg, exe = exeArg ? resolve(exeArg) : defaultExe();
const base = { rom, align: 'nmi' as const, regions: ['wram' as const], mask: true, replay, to, stride: 1_000_000, atNmi: true };
const { bytes: rng, frame: rngFrame } = await askMesenRng({ ...base, atNmi: false, to: SYNC_AT });

function run(env: Record<string, string>) {
    const out: [number, number, string][] = [];   // pc, start, kind
    let rest = '';
    const saved = { ...process.env };
    Object.assign(process.env, env, { SNESRECOMP_CYC_WATCH: '000000-FFFFFF' });
    const job = runRecomp({ ...base, exe, rng, rngFrame, onStderr: chunk => {
        const lines = (rest + chunk.toString()).split('\n');
        rest = lines.pop()!;
        for (const l of lines) {
            const m = /^\[(a?cyc)\] f=\d+ pc=\$([0-9A-F]+) .*?start=(\d+)/.exec(l);
            if (!m) continue;
            const st = +m[3];
            if (st >= from && st <= until) out.push([parseInt(m[2], 16) & 0x7FFFFF, st, m[1]]);
        }
    } }, () => {});
    for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
    Object.assign(process.env, saved);
    return { out, done: job.done };
}
// Two copies of one executable started at once can fail to stage its mods ("cannot publish mod state"): start apart.
const nat = run({});
await new Promise(r => setTimeout(r, 1500));
const int = run(allInterpretedEnv(exe));
await Promise.all([nat.done, int.done]);
const N = nat.out, I = int.out, ctx = +ctxArg;
const hx = (n: number) => '$' + n.toString(16).toUpperCase().padStart(6, '0');
console.log(`native ${N.length} instructions, interp ${I.length} in [${from}, ${until}]`);
for (let i = 0; i < Math.min(N.length, I.length); i++) {
    if (N[i][0] === I[i][0] && N[i][1] === I[i][1]) continue;
    for (let j = Math.max(0, i - ctx); j <= Math.min(i + 3, N.length - 1, I.length - 1); j++)
        console.log(`${j === i ? '>' : ' '} #${j} native ${hx(N[j][0])} ${N[j][1]} ${N[j][2].padEnd(4)} (+${j ? N[j][1] - N[j - 1][1] : 0})   interp ${hx(I[j][0])} ${I[j][1]} (+${j ? I[j][1] - I[j - 1][1] : 0})`);
    process.exit(0);
}
console.log('no difference in the window');
