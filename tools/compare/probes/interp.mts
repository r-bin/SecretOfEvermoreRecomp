// The environment for an all-interpreted recomp run: SNESRECOMP_LLE_BOUNCE=0 and every compiled entry of the
// recomp's generated code in SNESRECOMP_LLE_INTERP_TARGET_FILE (a list of function roots is not enough:
// SecretOfEvermoreRecomp docs/debugging.md). The interpreter's timing is the framework's cycle-exact one.
import { readdirSync, readFileSync, writeFileSync, mkdtempSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';

export function allInterpretedEnv(exe: string): Record<string, string> {
    const gen = join(dirname(dirname(exe)), 'src', 'gen');   // <repo>/build/<exe> -> <repo>/src/gen
    const pcs = new Set<string>();
    for (const f of readdirSync(gen).filter(f => f.endsWith('.c')))
        for (const m of readFileSync(join(gen, f), 'utf8').matchAll(/cpu_trace_func_entry\(cpu, 0x([0-9A-Fa-f]+)/g))
            pcs.add(m[1].toUpperCase().padStart(6, '0'));
    const file = join(mkdtempSync(join(tmpdir(), 'soe-interp-')), 'targets.txt');
    writeFileSync(file, [...pcs].sort().join('\n') + '\n');
    return { SNESRECOMP_LLE_BOUNCE: '0', SNESRECOMP_LLE_INTERP_TARGET_FILE: file };
}

/** The `[cyc]` lines of SNESRECOMP_CYC_WATCH as (pc, master clocks charged), from stderr chunks. */
export function cycLines(onInstruction: (pc: number, clocks: number) => void): (chunk: Buffer) => void {
    let rest = '';
    return chunk => {
        const lines = (rest + chunk.toString()).split('\n');
        rest = lines.pop()!;
        for (const l of lines) {
            const m = /^\[cyc\] f=\d+ pc=\$([0-9A-F]+) .*master_delta=(\d+)/.exec(l);
            if (m) onInstruction(parseInt(m[1], 16), +m[2]);
        }
    };
}
