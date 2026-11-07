// Import what everscript-vscode knows into this repository, so that regen and build need no other checkout.
// It reads a plugin checkout after its `npm run recomp` and copies:
//   - the seeds: its out/recomp/asar/recomp/cfg/bank8x..9x.cfg replace ours, its data regions become one block in
//     recomp/bank00.cfg (committed);
//   - the bound C: the files in our CMake list SOE_BOUND_C and every header, from its reconstructed-c/ into
//     src/bound-c/ (committed);
//   - the names for the generated C: its annotations.tsv into imported/ (text from the user's ROM: never committed;
//     tools/regen.sh hands it to tools/annotate.mts).
// Nothing here runs the plugin's code. The seeds read our recomp/seed-exclusions.txt and src/gen/program_manifest.json
// on the plugin's side, so run its `npm run recomp` after a regen here and import again (docs/setup.md §4).
//
//   node tools/import.mts --from <everscript-vscode checkout>     (or EVERSCRIPT=<checkout>; no default)
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

const ROOT = resolve(import.meta.dirname, '..');
export const SEED_MARK = '# >>> everscript data regions';
export const SEED_END = '# <<< everscript data regions';
const EXPORT = join('out', 'recomp', 'asar', 'recomp');

/** Our seeds: bank8x..Fx cfgs replace the previous ones; bank00 keeps its own text and gains one data-region block. */
export function importSeeds(seedDir: string, root: string, from = 'everscript-vscode'): { banks: number; regions: number } {
    const cfgDir = join(root, 'recomp');
    for (const f of readdirSync(cfgDir)) if (/^bank[89a-f][0-9a-f]\.cfg$/i.test(f)) rmSync(join(cfgDir, f));
    let banks = 0;
    for (const f of readdirSync(seedDir)) {
        if (!/^bank[0-9a-f]{2}\.cfg$/i.test(f) || f === 'bank00.cfg') continue;
        copyFileSync(join(seedDir, f), join(cfgDir, f));
        banks++;
    }
    const b00 = join(cfgDir, 'bank00.cfg');
    let text = readFileSync(b00, 'utf8');
    const i = text.indexOf(SEED_MARK);
    if (i >= 0) text = text.slice(0, i).replace(/\n+$/, '\n');
    const regions = readFileSync(join(seedDir, 'bank00.cfg'), 'utf8').split('\n').filter(l => l.startsWith('data_region'));
    writeFileSync(b00, text + '\n' + SEED_MARK + ' (imported from ' + from + ' by tools/import.mts)\n' + regions.join('\n') + '\n' + SEED_END + '\n');
    return { banks, regions: regions.length };
}

/** The C we bind: the files in CMake's SOE_BOUND_C and all headers, from the plugin's reconstructed-c/ into src/bound-c/. */
export function importBoundC(source: string, root: string, from = 'everscript-vscode'): { files: number } {
    const list = /set\(SOE_BOUND_C\s+([^)]+)\)/.exec(readFileSync(join(root, 'CMakeLists.txt'), 'utf8'));
    if (!list) throw new Error('no SOE_BOUND_C in CMakeLists.txt');
    const target = join(root, 'src', 'bound-c');
    const files = [...list[1].trim().split(/\s+/), ...readdirSync(join(source, 'include')).map(f => join('include', f))];
    const missing = files.filter(f => !existsSync(join(source, f)));
    if (missing.length) throw new Error('not in ' + source + ': ' + missing.join(', '));
    rmSync(target, { recursive: true, force: true });
    for (const f of files) {
        mkdirSync(dirname(join(target, f)), { recursive: true });
        copyFileSync(join(source, f), join(target, f));
    }
    writeFileSync(join(target, 'README.md'), 'Imported from everscript-vscode\'s `reconstructed-c/` (' + from + ') by `tools/import.mts`. '
        + 'Do not edit here: change the source there and import again.\n');
    return { files: files.length };
}

/** "everscript-vscode 0.398.0 @ f2e9e183" (plus " (dirty)"): which plugin state was imported. */
function describe(plugin: string): string {
    const version = JSON.parse(readFileSync(join(plugin, 'package.json'), 'utf8')).version;
    const git = (argv: string[]) => spawnSync('git', ['-C', plugin, ...argv], { encoding: 'utf8' }).stdout?.trim() || '';
    const head = git(['rev-parse', '--short=8', 'HEAD']);
    const dirty = git(['status', '--porcelain', '--', 'reconstructed-c']) ? ' (dirty)' : '';
    return 'everscript-vscode ' + version + (head ? ' @ ' + head : '') + dirty;
}

function main() {
    const argv = process.argv.slice(2);
    let from = process.env.EVERSCRIPT || '';
    for (let i = 0; i < argv.length; i++) {
        if (argv[i] === '--from') from = argv[++i];
        else if (argv[i] === '-h' || argv[i] === '--help') { console.log('node tools/import.mts [--from <everscript-vscode checkout>]'); return; }
        else { console.error('import: unknown argument ' + argv[i]); process.exit(2); }
    }
    if (!from) { console.error('import: name the everscript-vscode checkout: --from <path> or EVERSCRIPT=<path>'); process.exit(2); }
    const plugin = resolve(from);
    const seedDir = join(plugin, EXPORT, 'cfg');
    if (!existsSync(join(plugin, 'reconstructed-c')) || !existsSync(join(plugin, 'package.json'))) {
        console.error('import: no everscript-vscode checkout at ' + plugin + ' (--from or EVERSCRIPT)'); process.exit(1);
    }
    if (!existsSync(join(seedDir, 'bank00.cfg'))) {
        console.error('import: no seeds in ' + seedDir + ': run `npm run recomp` in ' + plugin + ' first'); process.exit(1);
    }
    const what = describe(plugin);
    const seeds = importSeeds(seedDir, ROOT, what);
    const bound = importBoundC(join(plugin, 'reconstructed-c'), ROOT, what);
    let annotations = 'none';
    const table = join(plugin, EXPORT, 'annotations.tsv');
    if (existsSync(table)) {
        mkdirSync(join(ROOT, 'imported'), { recursive: true });
        copyFileSync(table, join(ROOT, 'imported', 'annotations.tsv'));
        annotations = 'imported/annotations.tsv';
    }
    console.log(`import from ${what}:\n  seeds: ${seeds.banks} bank cfgs, ${seeds.regions} data regions\n  bound C: ${bound.files} files in src/bound-c/\n  names for the generated C: ${annotations}`);
    console.log('Commit recomp/bank*.cfg and src/bound-c/; run tools/regen.sh for the new seeds and names.');
}

if (import.meta.url === `file://${process.argv[1]}`) main();
