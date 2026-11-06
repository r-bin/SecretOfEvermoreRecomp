// Speed and coverage of one recomp build over a replay: wall time and emulated frames per second uncapped (the
// scripted run does not wait for vsync), per build mode; with --stats also snesrecomp's tier counts
// (SNESRECOMP_TIER_STATS), the instructions run per bank (the glue's SOE_BANK_STATS: which mirror copies of
// the code ever run) and how often each C binding ran its C (SOE_HLE_STATS). Stats cost time: measure speed without them.
//
//   node tools/compare/probes/run-stats.mts <rom> <replay.evsmv> [--to N (default: its last input)] [--exe path] [--mode native|interp|no-resume] [--stats]
//
// Headless: SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=/dev/null.
import { resolve } from 'node:path';
import { runRecomp, defaultExe } from '../recomp.mts';
import { allInterpretedEnv } from './interp.mts';
import { readReplay } from '../replay.mts';

const args = process.argv.slice(2);
const opt = (name: string) => { const i = args.indexOf(name); return i >= 0 ? args.splice(i, 2)[1] : undefined; };
const stats = args.includes('--stats') ? (args.splice(args.indexOf('--stats'), 1), true) : false;
const toArg = opt('--to'), mode = opt('--mode') || 'native';
const exe = resolve(opt('--exe') || defaultExe());
const [romArg, replay] = args;
if (!replay) { console.error('usage: run-stats.mts <rom> <replay.evsmv> [--to N] [--exe path] [--mode native|interp|no-resume] [--stats]'); process.exit(2); }
// Default: the replay's last input (its key: 14424 for 01). Past it the game runs on without input until --to.
const to = toArg ? +toArg : readReplay(replay).count;

if (mode === 'interp') Object.assign(process.env, allInterpretedEnv(exe));
else if (mode === 'no-resume') process.env.SNESRECOMP_AOT_RESUME = '0';
else if (mode !== 'native') { console.error('unknown --mode ' + mode); process.exit(2); }
if (stats) { process.env.SNESRECOMP_TIER_STATS = '1'; process.env.SOE_BANK_STATS = '1'; process.env.SOE_HLE_STATS = '1'; }

let rest = '', last = 0;
const banks: [number, number][] = [];
const t0 = performance.now();
await runRecomp({ exe, rom: resolve(romArg), to, regions: ['wram'], stride: 1_000_000, mask: false, rng: null, replay,
    atNmi: true, onNmi: nmi => { last = nmi; }, onStderr: chunk => {
        const lines = (rest + chunk.toString()).split('\n');
        rest = lines.pop()!;
        for (const l of lines) {
            if (l.startsWith('[tier]')) console.log('tier ' + l.slice(7));
            if (l.startsWith('[hle]')) console.log('hle ' + l.slice(6));   // the glue's SOE_HLE_STATS=1 (C bindings)
            const b = /^\[bank\] \$([0-9A-F]{2}): (\d+)/.exec(l);
            if (b) banks.push([parseInt(b[1], 16), +b[2]]);
            else if (l.startsWith('[bank]')) console.log('bank ' + l.slice(7));   // single-step slices
        }
    } }, () => {}).done;
const s = (performance.now() - t0) / 1000;
console.log(`${mode}: ${last} NMIs in ${s.toFixed(1)} s = ${(last / s).toFixed(0)} frames/s (${(last / s / 60.0988).toFixed(2)}x real time)` + (stats ? ', with stats' : ''));
if (banks.length) {
    const total = banks.reduce((a, [, n]) => a + n, 0);
    for (const [b, n] of banks) console.log(`bank $${b.toString(16).toUpperCase().padStart(2, '0')}: ${n} instructions (${(100 * n / total).toFixed(3)} %)`);
}
