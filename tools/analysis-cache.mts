#!/usr/bin/env node
// A cache in front of snesrecomp's native analyzer (recompiler-rs snesrecomp-analyze), the 139 s of a 166 s regen. Its
// result depends only on the analyzer binary, the ROM, the cfg files and its arguments, not on the emission switches
// (SNESRECOMP_EXACT_BUS, SNESRECOMP_AOT_RESUME, --region-paced), so a variant regen reuses it. tools/regen.sh puts this
// file in SNESRECOMP_NATIVE_ANALYZER and the real binary in SOE_ANALYZER_REAL; v2_emit calls it with the analyzer's
// own arguments. Kept in out/analysis-cache/, keyed by a hash of all inputs. SOE_ANALYSIS_CACHE=0 in regen.sh skips it.
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, renameSync, statSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join, resolve } from 'node:path';
import { REPO } from './compare/paths.mts';

const real = process.env.SOE_ANALYZER_REAL;
if (!real) { console.error('analysis-cache: SOE_ANALYZER_REAL is not set (tools/regen.sh sets it)'); process.exit(2); }
const args = process.argv.slice(2);

const hash = createHash('sha256');
const add = (label: string, data: string | Buffer) => { hash.update(label); hash.update('\0'); hash.update(data); hash.update('\0'); };
const addDir = (dir: string) => {
    for (const name of readdirSync(dir).sort()) {
        const path = join(dir, name);
        if (statSync(path).isFile()) add(name, readFileSync(path));
    }
};
add('analyzer', readFileSync(real));
let manifest: string | null = null;
for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === '--manifest') { manifest = args[++i]; continue; }   // the output path, a fresh temporary each run
    add('arg', a);
    if (a === '--rom' || a === '--roots-file' || a === '--force-lle-file') add(a, readFileSync(args[++i]));
    else if (a === '--cfg-dir') addDir(args[++i]);
}
if (!manifest) { console.error('analysis-cache: no --manifest argument'); process.exit(2); }

const dir = join(REPO, 'out', 'analysis-cache');
const key = hash.digest('hex');
const cached = join(dir, key + '.json'), log = join(dir, key + '.stdout');
if (existsSync(cached) && existsSync(log)) {
    copyFileSync(cached, resolve(manifest));
    process.stdout.write(readFileSync(log));
    console.error(`analysis-cache: reused ${key.slice(0, 12)}`);
    process.exit(0);
}
const run = spawnSync(real, args, { stdio: ['ignore', 'pipe', 'inherit'], maxBuffer: 1 << 28 });
if (run.stdout) process.stdout.write(run.stdout);
if (run.status !== 0) process.exit(run.status ?? 1);
mkdirSync(dir, { recursive: true });
writeFileSync(log + '.tmp', run.stdout ?? '');
copyFileSync(resolve(manifest), cached + '.tmp');
renameSync(log + '.tmp', log);
renameSync(cached + '.tmp', cached);
