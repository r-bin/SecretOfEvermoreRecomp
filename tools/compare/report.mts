// The Markdown report of one compare run, with WRAM bytes named from src/bound-c/include/soe_ram.h.
import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { hex, REPO } from './paths.mts';
import { runs, SOUND_FIELDS, type ByteDiff, type Region, type TargetResult } from './diff.mts';

const FPS = 60.0988;

let ramEntries: { addr: number; end: number; name: string }[] | null = null;

/** Every WRAM entry of soe_ram.h: <NAME>_ADDR and <NAME>_SIZE. */
function readRamEntries(): { addr: number; end: number; name: string }[] {
    const f = join(REPO, 'src', 'bound-c', 'include', 'soe_ram.h');
    if (!existsSync(f)) return [];
    const text = readFileSync(f, 'utf8'), sizes = new Map<string, number>();
    for (const m of text.matchAll(/^#define (\w+)_SIZE (\d+)/gm)) sizes.set(m[1], Number(m[2]));
    return [...text.matchAll(/^#define (\w+)_ADDR (0x[0-9A-Fa-f]+)u?/gm)]
        .map(m => ({ name: m[1], addr: parseInt(m[2], 16), end: parseInt(m[2], 16) + (sizes.get(m[1]) ?? 1) - 1 }));
}

/** The WRAM entry an offset falls in, as `NAME` or `NAME+3`; '' when none is known. */
export function ramName(offset: number): string {
    ramEntries ??= readRamEntries();
    const e = ramEntries.find(x => x.addr <= offset && offset <= x.end);
    return e ? e.name + (offset > e.addr ? '+' + (offset - e.addr) : '') : '';
}

const b2 = (v: number) => v.toString(16).toUpperCase().padStart(2, '0');

/** Each region's result over the run, and where it differed. */
function regionLines(run: RunInfo, t: TargetReport): string[] {
    const L = ['| region | keys that differ | of | first | runs |', '|---|---|---|---|---|'];
    run.regions.forEach((r, j) => {
        const x = t.result.byRegion[j];
        const text = x ? x.runs.slice(0, 12).map(([a, b]) => a === b ? `${a}` : `${a}-${b}`).join(', ') + (x.runs.length > 12 ? `, … (${x.runs.length} runs)` : '') : '';
        L.push(`| ${r} | ${x?.differing ?? 0} | ${t.result.compared} | ${x?.runs[0][0] ?? '—'} | ${text} |`);
    });
    return L;
}

export interface SetupInfo { label: string; backend: string; rom: string }

export interface Detail {
    region: Region;
    total: number;
    diffs: ByteDiff[];
}

export interface TargetReport {
    setup: SetupInfo;
    result: TargetResult;
    ended: number;
    details: Detail[];
    error?: string;
}

export interface RunInfo {
    reference: SetupInfo;
    replay: string | null;
    frames: number;
    align: 'frame' | 'nmi';
    regions: Region[];
    stride: number;
    keepGoing: boolean;
}

const keyText = (align: string, k: number) => align === 'frame' ? `frame ${k} (${(k / FPS).toFixed(1)} s)` : `counter $0100 = ${k} (≈${(k / FPS).toFixed(1)} s)`;

function detailLines(d: Detail): string[] {
    const out = [`### ${d.region.toUpperCase()}: ${d.total} byte(s) differ`, ''];
    const spans = runs(d.diffs.map(x => x.offset)).slice(0, 12).map(([a, b]) => a === b ? hex(a, 4) : `${hex(a, 4)}-${hex(b, 4)}`);
    out.push(`Where: ${spans.join(', ')}${d.diffs.length < d.total ? ' …' : ''}`, '');
    out.push('| offset | name | reference | target |', '|---|---|---|---|');
    for (const x of d.diffs.slice(0, 32))
        out.push(`| ${hex(d.region === 'wram' ? 0x7E0000 + x.offset : x.offset, d.region === 'wram' ? 6 : 4)} | ${d.region === 'sound' ? SOUND_FIELDS[x.offset >> 2] : d.region === 'wram' ? ramName(x.offset) : ''} | ${b2(x.a)} | ${b2(x.b)} |`);
    if (d.total > 32) out.push(`| … | ${d.total - 32} more | | |`);
    return [...out, ''];
}

export function summaryLine(run: RunInfo, t: TargetReport): string {
    if (t.error) return `${t.setup.label}: error: ${t.error}`;
    const r = t.result;
    if (!r.compared) return `${t.setup.label}: nothing compared (ran to key ${t.ended})`;
    if (!r.first) return `${t.setup.label}: in sync through ${keyText(run.align, r.lastCompared)}, ${r.compared} checkpoints`;
    const regions = r.first.regions.map(i => run.regions[i]).join('+');
    return `${t.setup.label}: in sync through ${keyText(run.align, r.first.lastGood)}; ${regions} differ at ${keyText(run.align, r.first.key)}`
        + (run.keepGoing ? `; ${r.differing} of ${r.compared} checkpoints differ` : '');
}

export function report(run: RunInfo, targets: TargetReport[]): string {
    const L = [`# compare: ${[run.reference, ...targets.map(t => t.setup)].map(s => s.label).join(' vs ')}`, ''];
    L.push(`- Reference: **${run.reference.label}** (${run.reference.backend}, \`${run.reference.rom}\`)`);
    L.push(`- Input: ${run.replay ? '`' + run.replay + '`' : 'none (power-on, no input)'}; run to ${keyText(run.align, run.frames)}`);
    L.push(`- Aligned on ${run.align === 'frame' ? 'emulated frames' : 'the NMI counter $0100'}; regions ${run.regions.join(', ')}; checked every ${run.stride} key(s)`);
    L.push('');
    for (const t of targets) {
        L.push(`## ${t.setup.label} (${t.setup.backend}, \`${t.setup.rom}\`)`, '', summaryLine(run, t), '');
        if (run.keepGoing && run.regions.length > 1) L.push(...regionLines(run, t), '');
        const f = t.result.first;
        if (!f) continue;
        L.push(`First divergence between ${keyText(run.align, f.lastGood)} and ${keyText(run.align, f.key)}. Bytes at ${keyText(run.align, f.key)}:`, '');
        for (const d of t.details) L.push(...detailLines(d));
    }
    return L.join('\n');
}
