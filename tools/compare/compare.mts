// Run MesenCE and the recomp side by side, hash their memory every 60 keys (every key with --verbose) and report how long
// each target stays in sync with the first setup and what differs at the first checkpoint that does not match. Every key
// from power-on is compared, every byte: nothing is copied from one backend into the other and nothing is masked.
//   node tools/compare/compare.mts [backend[:exe]=]<rom> ... [--replay x.evsmv|x.mmo] [--frames N] [--verbose]
//       [--keep-going] [--aram] [--regions wram,vram,cgram,oam,sram,aram,dsp,sound,picture] [--align frame|nmi]
//       [--full] [--picture] [--sound] [--out file.md] [--record file] [--reference file]
//   --full: every RAM at every key, on to the end, with each region's result; --picture and --sound add those
//   --interpreted: the recomp all-interpreted
//   --fade: the recomp scales brightness by MesenCE's rule, to see what else differs in the picture
//   --record: keep the reference's hashes in a file; --reference: compare with that file, no MesenCE needed
//   --mesen-lua <file>: run a MesenCE mod (mods/mesen/*.lua) with MesenCE, for a mod's parity check (docs/mods/README.md)
//   backend: mesen | recomp (exe: build/ by default); no ROM after '=' means the clean ROM (--rom, SNESRECOMP_ROM,
//   a known name at the repo root, rom.cfg)
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { basename, join, relative, resolve } from 'node:path';
import { REPO, defaultRom } from './paths.mts';
import { Tracker, byteDiff, inOrder, ALL_RAM, type Crcs, type Region } from './diff.mts';
import { runRecomp, defaultExe } from './recomp.mts';
import { runMesen, defaultMesenExe } from './mesen.mts';
import { allInterpretedEnv } from './probes/interp.mts';
import { loadRecorded, saveRecorded } from './recorded.mts';
import { report, summaryLine, type Detail, type TargetReport, type RunInfo } from './report.mts';
import { readReplay } from './replay.mts';

const DEFAULT_FRAMES = 8414;     // the 2:20 intro

export interface Setup { label: string; backend: 'recomp' | 'mesen'; exe: string | null; rom: string }

export const KNOWN_BACKENDS = new Set(['recomp', 'mesen']);

const argv = process.argv.slice(2);
export function parseArgs(argv: string[]) {
    const VALUED = new Set(['replay', 'frames', 'regions', 'align', 'out', 'rom', 'backend', 'record', 'reference', 'mesen-lua']);
    const opts: Record<string, string | true> = {}, positional: string[] = [];
    for (let i = 0; i < argv.length; i++) {
        if (!argv[i].startsWith('--')) { positional.push(argv[i]); continue; }
        const k = argv[i].slice(2);
        opts[k] = VALUED.has(k) ? argv[++i] : true;
    }
    const opt = (k: string) => typeof opts[k] === 'string' ? opts[k] as string : null;

    // Determine shared ROM: explicit --rom, or any positional argument that is a ROM file
    let explicitRom = opt('rom');
    const setupTokens: string[] = [];

    if (opt('backend')) {
        const bks = opt('backend')!.split(/[,\s]+/);
        setupTokens.push(...bks);
    }

    for (const p of positional) {
        const eq = p.indexOf('=');
        const head = (eq < 0 ? p : p.slice(0, eq)).split(':')[0];
        if (KNOWN_BACKENDS.has(head)) {
            setupTokens.push(p);
        } else if (/\.(smc|sfc|swc|fig)$/i.test(p) || existsSync(p)) {
            if (!explicitRom) explicitRom = p;
            else setupTokens.push(p);
        } else {
            setupTokens.push(p);
        }
    }

    const found = explicitRom ? resolve(explicitRom) : defaultRom();
    if (!found) {
        console.error('no ROM: pass one, use --rom, or set SNESRECOMP_ROM (see the header of compare.mts)');
        process.exit(2);
    }
    const vanilla = found;
    if (!existsSync(vanilla)) throw new Error(`no ROM at ${vanilla}`);

    function parseSetup(s: string): Setup {
        const eq = s.indexOf('=');
        const left = eq < 0 ? s : s.slice(0, eq);
        const colon = left.indexOf(':');
        const backendCandidate = colon < 0 ? left : left.slice(0, colon);
        if (!KNOWN_BACKENDS.has(backendCandidate)) throw new Error(`unknown backend ${backendCandidate}: mesen or recomp`);
        const backend = backendCandidate as Setup['backend'];
        const romTarget = eq >= 0 ? s.slice(eq + 1) : '';
        const rom = resolve(romTarget || vanilla);
        if (!existsSync(rom)) throw new Error(`no ROM at ${rom}`);
        const exe = resolve(colon >= 0 ? left.slice(colon + 1) : backend === 'recomp' ? defaultExe() : defaultMesenExe());
        return { label: `${basename(rom)} (${backend})`, backend, exe, rom };
    }

    const setups = (setupTokens.length ? setupTokens : ['mesen', 'recomp']).map(parseSetup);
    if (setups.length === 1) setups.unshift(parseSetup('mesen'));
    setups.forEach((s, i) => { if (setups.findIndex(x => x.label === s.label) !== i) s.label += ` #${i + 1}`; });

    return { opts, opt, vanilla, setups };
}

export async function main() {
const { opts, opt, vanilla, setups } = parseArgs(argv);

// Every recomp setup all-interpreted, for the reference column of a native build.
if (opts.interpreted) { const r = setups.find(s => s.backend === 'recomp'); if (r) Object.assign(process.env, allInterpretedEnv(r.exe!)); }
const replay = opt('replay') ? resolve(opt('replay')!) : null;
// A MesenCE mod (mods/mesen/*.lua) run with the reference: the parity check of a mod (docs/mods/README.md).
const mesenLua = opt('mesen-lua') ? readFileSync(resolve(opt('mesen-lua')!), 'utf8') : undefined;
const hasRecomp = setups.some(s => s.backend === 'recomp');
// MesenCE and the recomp both sample as each NMI is taken (an instruction boundary on both).
const atNmi = true;
const align = (opt('align') || 'nmi') as 'frame' | 'nmi';
if (align === 'frame' && hasRecomp) { console.error('the recomp aligns on the NMI counter only (--align nmi)'); process.exit(2); }
const regions = inOrder([
    ...(opt('regions') ? opt('regions')!.split(',') as Region[] : opts.full ? ALL_RAM : ['wram', 'vram'] as Region[]),
    ...(opts.aram ? ['aram'] as Region[] : []), ...(opts.picture ? ['picture'] as Region[] : []), ...(opts.sound ? ['sound'] as Region[] : []),
]);
const movie = replay ? readReplay(replay) : null;
const movieCount = movie?.count ?? 0;
const fromState = movie?.start?.midGame ?? false;   // both start from one machine state: nothing to copy, every key counts
const frames = Number(opt('frames') || (replay ? movieCount : DEFAULT_FRAMES));
const recorded = opt('reference') ? loadRecorded(resolve(opt('reference')!)) : null;
if (recorded && recorded.regions.join() !== regions.join()) { console.error(`--reference holds ${recorded.regions.join(',')}: use --regions with that`); process.exit(2); }
const stride = recorded ? recorded.stride : opts.verbose || opts.full ? 1 : 60;
const keepGoing = !!(opts['keep-going'] || opts.full);

// --- hash all setups at once, the reference from a recorded run when one is given -------------------------------------------
const ref = setups[0], targets = setups.slice(1);
const tracker = new Tracker(targets.length, stride);
const ended = new Array(targets.length).fill(0);
const errors: (string | undefined)[] = new Array(targets.length);
const building = opt('record') ? new Map<number, Crcs>() : null;

const kills: (() => void)[] = [];
let refKill = () => {}, open = targets.length;
// Stop the reference early only when no target still needs its keys: every one parted or failed. A target that
// ran to the end first (the recomp outruns Mesen) still has keys waiting for the reference.
const targetDone = (i: number) => {
    if (--open === 0 && !building && !keepGoing && tracker.results.every((r, j) => r.first || errors[j])) refKill();
};
// A state starts inside a field, so the first picture was partly drawn before the run: not comparable.
const pictureAt = regions.indexOf('picture');
const comparable = (k: number, c: Crcs): Crcs => fromState && k === 1 && pictureAt >= 0 ? c.map((x, j) => j === pictureAt ? 0 : x) : c;
const onTarget = (i: number) => (k: number, c: Crcs) => {
    c = comparable(k, c);
    if (tracker.addTarget(i, k, c) && !keepGoing) kills[i]();
};
const onRef = (k: number, c: Crcs) => {
    c = comparable(k, c);
    building?.set(k, c);
    for (const i of tracker.addRef(k, c)) if (!keepGoing) kills[i]();
};

const runs: Promise<unknown>[] = [];
function start(s: Setup, extra: { stride: number }, onCp: (k: number, c: Crcs) => void, onEnd: (k: number) => void, onErr: (m: string) => void) {
    if (s.backend === 'mesen') {
        const r = runMesen({ exe: s.exe || undefined, rom: s.rom, to: frames, align, regions, stride: extra.stride, replay, atNmi, lua: mesenLua }, onCp);
        runs.push(r.done.then(dump => { onEnd(frames); return dump; }).catch(e => onErr((e as Error).message)));
        return r.kill;
    }
    const r = runRecomp({ exe: s.exe!, rom: s.rom, to: frames, regions, stride: extra.stride, replay, atNmi, fade: !!opts.fade }, onCp);
    runs.push(r.done.then(dump => { onEnd(frames); return dump; }).catch(e => onErr((e as Error).message)));
    return r.kill;
}

const t0 = Date.now();
targets.forEach((s, i) => {
    let finished = false;
    const fin = () => { if (!finished) { finished = true; targetDone(i); } };
    kills[i] = start(s, { stride }, onTarget(i), k => { ended[i] = k; fin(); }, m => { errors[i] = m; fin(); });
    const kill = kills[i];
    kills[i] = () => { kill(); fin(); };
});
if (recorded) for (const [k, c] of recorded.keys) onRef(k, c);
else refKill = start(ref, { stride }, onRef, () => {}, m => { console.error(`reference: ${m}`); targets.forEach((_, i) => kills[i]()); });
await Promise.all(runs);
if (building) saveRecorded(resolve(opt('record')!), { regions, keys: building, stride });

// --- the first divergence of each target, in detail -----------------------------------------------------------------
async function snapshot(s: Setup, at: number): Promise<Uint8Array[]> {
    if (s.backend === 'mesen') {
        const dump = await runMesen({ exe: s.exe || undefined, rom: s.rom, to: at, align, regions, dumpAt: at, replay, atNmi, lua: mesenLua }, () => {}).done;
        if (!dump) throw new Error(`Mesen did not reach counter ${at}`);
        return dump;
    }
    const dump = await runRecomp({ exe: s.exe!, rom: s.rom, to: at, regions, dumpAt: at, replay, atNmi, fade: !!opts.fade }, () => {}).done;
    if (!dump) throw new Error(`the recomp did not reach counter ${at}`);
    return dump;
}

const reports: TargetReport[] = await Promise.all(targets.map(async (s, i): Promise<TargetReport> => {
    const result = tracker.results[i];
    const base: TargetReport = { setup: s, result, ended: ended[i], details: [], error: errors[i] };
    const f = result.first;
    if (!f) return base;
    if (recorded) { base.error = `differs at key ${f.key} (last good ${f.lastGood}); run against MesenCE for the bytes`; return base; }
    try {
        const [a, b] = await Promise.all([snapshot(ref, f.key), snapshot(s, f.key)]);
        const details: Detail[] = f.regions.map(j => {
            const d = byteDiff(a[j], b[j], 4096);
            return { region: regions[j], total: d.total, diffs: d.diffs };
        });
        base.details = details;
    } catch (e) { base.error = (e as Error).message; }
    return base;
}));

// --- report ----------------------------------------------------------------------------------------------------------
const shown = (s: Setup) => ({ ...s, rom: relative(REPO, s.rom) });
for (const t of reports) t.setup = shown(t.setup as Setup);
const run: RunInfo = { reference: shown(ref), replay: replay && relative(REPO, replay), frames, align, regions, stride, keepGoing };
const md = report(run, reports);
const out = opt('out') ? resolve(opt('out')!) : join(REPO, 'out', 'compare', setups.map(s => s.label.replace(/[^\w.-]+/g, '_')).join('--').slice(0, 120) + '.md');
mkdirSync(join(out, '..'), { recursive: true });
writeFileSync(out, md);
console.log(`reference ${ref.label}${recorded ? ' (recorded)' : building ? ' (recording written)' : ''}; ${((Date.now() - t0) / 1000).toFixed(1)} s`);
for (const t of reports) console.log(summaryLine(run, t));
console.log(`report: ${relative(REPO, out)}`);
process.exit(reports.every(t => !t.error && !t.result.first && t.result.compared) ? 0 : 1);
}

if (import.meta.url === `file://${process.argv[1]}`) {
    await main();
}
