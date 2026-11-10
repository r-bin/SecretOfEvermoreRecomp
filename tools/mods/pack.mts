// Packs each mod package of mods/preloaded/packages/<id>/<version>/ into out/mods/<id>-<version>.snesmod, the archive
// players install through the launcher's Mods page (snesrecomp docs/MOD_AUTHORING.md: a ZIP with the manifest at its
// root; stored entries are accepted). Only the package's own files go in; docs/mods/README.md.
//
//   node tools/mods/pack.mts [package id ...]
import { mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import { crc32 } from 'node:zlib';

const REPO = join(import.meta.dirname, '..', '..');
const PACKAGES = join(REPO, 'mods', 'preloaded', 'packages');
const OUT = join(REPO, 'out', 'mods');

function files(dir: string): string[] {
    return readdirSync(dir).sort().flatMap(n => {
        const p = join(dir, n);
        return statSync(p).isDirectory() ? files(p) : [p];
    });
}

/** A ZIP of stored entries, names relative to `root`, a fixed timestamp so the same files give the same bytes. */
export function zip(root: string, paths: string[]): Buffer {
    const local: Buffer[] = [], central: Buffer[] = [];
    let offset = 0;
    const DOS_TIME = 0, DOS_DATE = (2026 - 1980) << 9 | 1 << 5 | 1;
    for (const p of paths) {
        const name = Buffer.from(relative(root, p).split('\\').join('/'), 'utf8');
        const data = readFileSync(p), crc = crc32(data);
        const head = Buffer.alloc(30);
        head.writeUInt32LE(0x04034b50, 0); head.writeUInt16LE(20, 4); head.writeUInt16LE(0x0800, 6);   // UTF-8 names
        head.writeUInt16LE(DOS_TIME, 10); head.writeUInt16LE(DOS_DATE, 12);
        head.writeUInt32LE(crc, 14); head.writeUInt32LE(data.length, 18); head.writeUInt32LE(data.length, 22);
        head.writeUInt16LE(name.length, 26);
        local.push(head, name, data);
        const dir = Buffer.alloc(46);
        dir.writeUInt32LE(0x02014b50, 0); dir.writeUInt16LE(20, 4); dir.writeUInt16LE(20, 6); dir.writeUInt16LE(0x0800, 8);
        dir.writeUInt16LE(DOS_TIME, 12); dir.writeUInt16LE(DOS_DATE, 14);
        dir.writeUInt32LE(crc, 16); dir.writeUInt32LE(data.length, 20); dir.writeUInt32LE(data.length, 24);
        dir.writeUInt16LE(name.length, 28); dir.writeUInt32LE(offset, 42);
        central.push(dir, name);
        offset += head.length + name.length + data.length;
    }
    const size = central.reduce((n, b) => n + b.length, 0);
    const end = Buffer.alloc(22);
    end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(paths.length, 8); end.writeUInt16LE(paths.length, 10);
    end.writeUInt32LE(size, 12); end.writeUInt32LE(offset, 16);
    return Buffer.concat([...local, ...central, end]);
}

if (import.meta.url === `file://${process.argv[1]}`) {
    const only = process.argv.slice(2);
    mkdirSync(OUT, { recursive: true });
    for (const id of readdirSync(PACKAGES).sort().filter(n => statSync(join(PACKAGES, n)).isDirectory())) {
        if (only.length && !only.includes(id)) continue;
        for (const version of readdirSync(join(PACKAGES, id)).sort()) {
            const root = join(PACKAGES, id, version);
            const manifest = readFileSync(join(root, 'manifest.toml'), 'utf8');
            const declared = /^id\s*=\s*"([^"]+)"/m.exec(manifest)?.[1], v = /^version\s*=\s*"([^"]+)"/m.exec(manifest)?.[1];
            if (declared !== id || v !== version) throw new Error(`${root}: manifest says ${declared} ${v}`);
            const to = join(OUT, `${id}-${version}.snesmod`);
            writeFileSync(to, zip(root, files(root)));
            console.log(relative(REPO, to));
        }
    }
}
