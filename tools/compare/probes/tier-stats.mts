// How much of a replay the recomp runs as compiled (native) code: snesrecomp's SNESRECOMP_TIER_STATS counts the
// instructions each tier ran (compiled ones in bus-exact code only) and names the interpreted runs that ran the most,
// by the PC each started at. The run is the replay as compare.mts plays it, without the RNG copy (no MesenCE).
//
//   node tools/compare/probes/tier-stats.mts <rom> <replay.evsmv> [to-key] [exe]
import { resolve } from 'node:path';
import { runRecomp, defaultExe } from '../recomp.mts';

const [romArg, replay, toArg, exeArg] = process.argv.slice(2);
if (!replay) { console.error('usage: tier-stats.mts <rom> <replay.evsmv> [to-key] [exe]'); process.exit(2); }
const to = toArg ? +toArg : 1_000_000, exe = exeArg ? resolve(exeArg) : defaultExe();
process.env.SNESRECOMP_TIER_STATS = '1';
let rest = '';
await runRecomp({ exe, rom: resolve(romArg), to, regions: ['wram'], stride: 1_000_000, mask: false, rng: null, replay,
    atNmi: true, onStderr: chunk => {
        const lines = (rest + chunk.toString()).split('\n');
        rest = lines.pop()!;
        for (const l of lines) if (l.startsWith('[tier]')) console.log(l.slice(7));
    } }, () => {}).done;
