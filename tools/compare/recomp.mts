// The snesrecomp backend: the recomp repository's game run headless from a script, hashing through its
// glue (src/game_rtl.c there, SOE_COMPARE_*), which writes to fd 3. Keys are always the NMI counter $0100.
import { mkdtempSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawn } from 'node:child_process';
import { readReplay, type Replay } from './replay.mts';
import { REGION_SIZE, type Region, type Crcs } from './diff.mts';
import { REPO } from './paths.mts';

export const RECOMP_REGIONS: Region[] = ['wram', 'vram', 'cgram', 'oam', 'sram', 'picture', 'aram', 'dsp', 'sound'];

/** The game built here. */
export function defaultExe(): string {
    return join(REPO, 'build', 'SecretOfEvermoreSNESRecomp' + (process.platform === 'win32' ? '.exe' : ''));
}

export interface RecompJob {
    exe: string;
    rom: string;
    /** The counter value to run to. */
    to: number;
    regions: Region[];
    stride?: number;
    dumpAt?: number;
    replay?: string | null;
    /** Scale brightness by MesenCE's rule, truncated (SOE_COMPARE_FADE). */
    fade?: boolean;
    /** Sample as each NMI is taken (SOE_COMPARE_AT=nmi); the key is the NMI's number since power-on. */
    atNmi?: boolean;
    /** With atNmi: each NMI's number, master clock and the address it interrupted (SOE_COMPARE_NMI_LOG). */
    onNmi?: (nmi: number, clock: number, pc: number) => void;
    /** WRAM offsets reported after every emulated frame through `onPeek` (a probe; SOE_COMPARE_PEEK in the glue). */
    peek?: number[];
    onPeek?: (frame: number, key: number, clock: number, bytes: number[]) => void;
    /** The recomp's stderr as it arrives (a probe: the framework's SNESRECOMP_* traces go there). */
    onStderr?: (chunk: Buffer) => void;
}

const h = (n: number) => n.toString(16).toUpperCase();

/** Run the recomp; `onCp` gets each checkpoint. Resolves with the dump when dumpAt was set. */
export function runRecomp(job: RecompJob, onCp: (key: number, crcs: Crcs) => void): { done: Promise<Uint8Array[] | null>; kill(): void } {
    const bad = job.regions.filter(r => !RECOMP_REGIONS.includes(r));
    if (bad.length) throw new Error(`the recomp backend cannot hash ${bad.join(', ')}`);
    if (!existsSync(job.exe)) throw new Error(`no recomp executable at ${job.exe} (npm run recomp builds it)`);
    const dir = mkdtempSync(join(tmpdir(), 'soe-compare-'));
    const lines = ['turbo on'];
    let replay: Replay | null = null;

    if (job.replay) replay = readReplay(job.replay);   // the input goes by field (SOE_COMPARE_PADS, below)

    // The key is an NMI count, not the raw $0100 (the attract demo zeroes it), so the glue ends the run itself.
    lines.push(`wait ${job.to * 3 + 2000}`, 'quit');
    writeFileSync(join(dir, 'compare.txt'), lines.join('\n') + '\n');
    // SRAM from the run's own empty folder: cleared, as on MesenCE (mesen.mts isolatedRom), and the player's
    // saves/ stay untouched (the glue's SOE_COMPARE_SAVE_ROOT).
    // No window or audio device: with them a run has idled for good with no error (keys in the window, a locked Mac).
    const env: NodeJS.ProcessEnv = { SDL_VIDEO_DRIVER: 'dummy', SDL_AUDIO_DRIVER: 'dummy', ...process.env, SOE_COMPARE_REGIONS: job.regions.join(','), SOE_COMPARE_SAVE_ROOT: dir };
    env.SOE_COMPARE_QUIT_AT = String(job.dumpAt ?? job.to);
    if (replay) {
        writeFileSync(join(dir, 'pads.bin'), Buffer.from(replay.pads.buffer, replay.pads.byteOffset, replay.pads.byteLength));
        env.SOE_COMPARE_PADS = join(dir, 'pads.bin');
    }
    if (replay?.start) writeFileSync(join(dir, 'start.fields'), replay.start.fields);
    if (replay?.start?.midGame) env.SOE_COMPARE_MESEN_STATE = join(dir, 'start.fields');
    else if (replay?.start) {   // a movie's memory and sound, before the first instruction
        for (const name of replay.start.fresh ? ['wram', 'sram'] as const : ['wram', 'sram', 'hw'] as const) {
            writeFileSync(join(dir, 'start.' + name), replay.start[name]);
            env['SOE_COMPARE_' + name.toUpperCase()] = join(dir, 'start.' + name);
        }
        if (!replay.start.fresh) env.SOE_COMPARE_MESEN_APU = join(dir, 'start.fields');
    }
    if (job.atNmi) { env.SOE_COMPARE_AT = 'nmi'; env.SOE_COMPARE_MESEN_ROM_MIRROR = '1'; }   // atNmi: the reference is MesenCE
    if (job.onNmi) env.SOE_COMPARE_NMI_LOG = '1';
    if (job.fade) env.SOE_COMPARE_FADE = '1';
    if (job.stride) env.SOE_COMPARE_STRIDE = String(job.stride);
    if (job.dumpAt !== undefined) env.SOE_COMPARE_DUMP = String(job.dumpAt);
    if (job.peek?.length) env.SOE_COMPARE_PEEK = job.peek.map(h).join(',');
    const child = spawn(job.exe, ['--no-launcher', '--script', join(dir, 'compare.txt'), job.rom], { env, stdio: ['ignore', 'ignore', 'pipe', 'pipe'] });
    let log = '';
    child.stderr!.on('data', (d: Buffer) => { log = (log + d).slice(-4000); job.onStderr?.(d); });
    let buf = Buffer.alloc(0), dump: Uint8Array[] | null = null;
    const dumpSize = job.regions.reduce((s, r) => s + REGION_SIZE[r], 0);
    (child.stdio[3] as NodeJS.ReadableStream).on('data', (d: Buffer) => {
        buf = Buffer.concat([buf, d]);
        for (;;) {
            const nl = buf.indexOf(10);
            if (nl < 0) return;
            const head = buf.subarray(0, nl).toString();
            if (head.startsWith('D ')) {
                if (buf.length < nl + 1 + dumpSize) return;
                let o = nl + 1;
                dump = job.regions.map(r => { const b = new Uint8Array(buf.subarray(o, o + REGION_SIZE[r])); o += REGION_SIZE[r]; return b; });
                buf = buf.subarray(o);
                continue;
            }
            buf = buf.subarray(nl + 1);
            const p = head.split(' ');
            if (p[0] === 'H') onCp(Number(p[1]), p.slice(2).map(x => parseInt(x, 16)));
            else if (p[0] === 'N') job.onNmi?.(Number(p[1]), Number(p[2]), parseInt(p[3], 16));
            else if (p[0] === 'P') job.onPeek?.(Number(p[1]), Number(p[2]), Number(p[3]), p.slice(4).map(x => parseInt(x, 16)));
        }
    });
    const done = new Promise<Uint8Array[] | null>((resolve, reject) => child.on('close', code => {
        rmSync(dir, { recursive: true, force: true });
        if (code && code !== 0 && child.signalCode === null) reject(new Error(`recomp exited with ${code}: ${log.trim().split('\n').slice(-3).join(' | ')}`));
        else resolve(dump);
    }));
    return { done, kill: () => { if (child.exitCode === null) child.kill(); } };
}
