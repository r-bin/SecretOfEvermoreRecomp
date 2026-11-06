// Every sync replay against MesenCE at every key, on every RAM, the picture and the sound state, one after the other
// (runs side by side slow each other down). The sync replays are replays/ and your own replays/local/ (03 does not
// sync, so it is not one of them). Reports go to out/compare/full/; the exit code is 0 only when nothing differed.
//   node tools/compare/full.mts [rom] [--only 01,06] [--no-picture] [--no-sound] [--interpreted] [--fade]
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync } from 'node:fs';
import { basename, join } from 'node:path';
import { REPO } from './paths.mts';

const argv = process.argv.slice(2);
const flag = (f: string) => argv.includes(f);
const onlyAt = argv.indexOf('--only');
const only = onlyAt >= 0 ? argv[onlyAt + 1].split(',') : null;
const rom = argv.find((a, i) => !a.startsWith('--') && i !== onlyAt + 1);
const dirs = ['replays', 'replays/local'].map(d => join(REPO, 'tools', 'compare', d)), outDir = join(REPO, 'out', 'compare', 'full');
mkdirSync(outDir, { recursive: true });

const replays = dirs.flatMap(d => existsSync(d) ? readdirSync(d).filter(f => /\.(evsmv|mmo)$/.test(f) && !f.startsWith('03-') && (!only || only.some(o => f.startsWith(o)))).map(f => join(d, f)) : []);
let bad = 0;
for (const path of replays) {
    const replay = basename(path);
    const out = join(outDir, replay.replace(/\.\w+$/, '') + (flag('--interpreted') ? '.interpreted' : '') + (flag('--fade') ? '.fade' : '') + '.md');
    rmSync(out, { force: true });
    const t0 = Date.now();
    let code = 0;
    try {
        execFileSync(process.execPath, ['tools/compare/compare.mts', 'mesen', 'recomp', ...(rom ? [rom] : []), '--replay', path, '--full',
            ...(flag('--no-picture') ? [] : ['--picture']), ...(flag('--no-sound') ? [] : ['--sound']),
            ...(flag('--interpreted') ? ['--interpreted'] : []), ...(flag('--fade') ? ['--fade'] : []), '--out', out], { cwd: REPO, stdio: ['ignore', 'ignore', 'inherit'] });
    } catch (e) { code = (e as { status: number }).status ?? 1; }
    bad += code ? 1 : 0;
    const done = existsSync(out);
    console.log(`\n${replay}: ${!done ? 'NO RESULT (the run failed)' : code ? 'DIFFERS' : 'identical'} (${Math.round((Date.now() - t0) / 1000)} s)`);
    if (done) console.log(readFileSync(out, 'utf8').split('\n').filter(l => l.startsWith('| ') && !l.includes('offset')).slice(0, 12).join('\n'));
}
process.exit(bad ? 1 : 0);
