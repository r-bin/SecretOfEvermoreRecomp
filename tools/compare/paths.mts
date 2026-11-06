// Where the repository is, and the ROM a run uses when the command line names none.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

export const REPO = resolve(dirname(new URL(import.meta.url).pathname), '..', '..');
export const hex = (v: number, n = 6) => '$' + v.toString(16).toUpperCase().padStart(n, '0');

/** As tools/regen.sh looks for it: SNESRECOMP_ROM, a known name at the repo root, the path in rom.cfg. */
export function defaultRom(): string | null {
    if (process.env.SNESRECOMP_ROM) return resolve(process.env.SNESRECOMP_ROM);
    for (const name of ['rom.sfc', 'secretofevermore.sfc', 'secretofevermore.smc'])
        if (existsSync(join(REPO, name))) return join(REPO, name);
    const cfg = join(REPO, 'rom.cfg');
    const path = existsSync(cfg) ? readFileSync(cfg, 'utf8').trim() : '';
    return path && existsSync(path) ? path : null;
}
