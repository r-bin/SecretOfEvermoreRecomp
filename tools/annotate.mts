// Comments for the recomp's generated C: before each compiled instruction, the instruction as our Asar
// export writes it (ROM labels, the WRAM addresses it touched with their names), and before each function,
// the routine it belongs to with its note. Only lines are added, each one a `/* evs: ... */` comment on its
// own line; no generated line is changed, so the compiled program is the same. Running it again replaces
// the old comments.
//
//   node tools/annotate.mts <src/gen dir> [annotations.tsv]
//
// The table is imported/annotations.tsv: everscript-vscode's `npm run recomp` writes it from the user's own ROM and
// tools/import.mts copies it here; the generated C and the table both stay out of every repository. tools/regen.sh
// runs this after each generation when the table is there.
import { existsSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const ROOT = resolve(import.meta.dirname, '..');

const MARK = '/* evs: ';
const FUNC_HEAD = /^(?:static )?RecompReturn \w+\(CpuState \*cpu\) \{$/;
const FUNC_ENTRY = /^\s*cpu_trace_func_entry\(cpu, 0x([0-9A-F]{6}),/;
const BLOCK = /^\s*cpu_trace_block\(cpu, 0x([0-9A-F]{6})\);/;
const INSN = /^(\s*)cpu_aot_insn_begin\(cpu, 0x([0-9A-F]{4})u\);$/;

/** ROM offset of a code address, or -1 for WRAM code and the lower halves of $00-$3F / $80-$BF. */
function romOffset(pc24: number): number {
    const bank = pc24 >>> 16;
    if (bank === 0x7E || bank === 0x7F) return -1;
    if ((bank & 0x40) === 0 && (pc24 & 0xFFFF) < 0x8000) return -1;
    return pc24 & 0x3FFFFF;   // HiROM: the file maps linearly, the top two bank bits are mirrors
}

function readTable(file: string) {
    const insns = new Map<number, string>(), routines = new Map<number, string>();
    for (const line of readFileSync(file, 'utf8').split('\n')) {
        if (!line || line.startsWith('#')) continue;
        const [off, kind, text] = line.split('\t');
        (kind === 'f' ? routines : insns).set(parseInt(off, 16), text);
    }
    return { insns, routines, starts: [...routines.keys()].sort((a, b) => a - b) };
}

type Table = ReturnType<typeof readTable>;

/** "dialogue_glyph_decode: <note>" at a routine start; "inside lz_copy_loop (+$91)" within one. */
function routineText(t: Table, off: number): string | null {
    if (t.routines.has(off)) return t.routines.get(off)!;
    let lo = 0, hi = t.starts.length - 1, best = -1;
    while (lo <= hi) { const mid = (lo + hi) >> 1; if (t.starts[mid] <= off) { best = t.starts[mid]; lo = mid + 1; } else hi = mid - 1; }
    if (best < 0 || (best >>> 16) !== (off >>> 16)) return null;
    return 'inside ' + t.routines.get(best)!.split(':')[0] + ' (+$' + (off - best).toString(16).toUpperCase() + ')';
}

function annotate(text: string, t: Table): { text: string; insns: number; funcs: number } {
    const lines = text.split('\n');
    const out: string[] = [];
    let bank = -1, insns = 0, funcs = 0;
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (line.trimStart().startsWith(MARK)) continue;
        if (FUNC_HEAD.test(line)) {
            for (let j = i + 1; j < Math.min(i + 10, lines.length); j++) {
                const e = FUNC_ENTRY.exec(lines[j]);
                if (!e) continue;
                const off = romOffset(parseInt(e[1], 16));
                const what = off >= 0 ? routineText(t, off) : null;
                if (what) { out.push(MARK + what + ' */'); funcs++; }
                break;
            }
        }
        const pc = FUNC_ENTRY.exec(line) || BLOCK.exec(line);
        if (pc) bank = parseInt(pc[1], 16) >>> 16;
        const ins = INSN.exec(line);
        if (ins && bank >= 0) {
            const off = romOffset((bank << 16) | parseInt(ins[2], 16));
            const asm = off >= 0 ? t.insns.get(off) : undefined;
            if (asm) { out.push(ins[1] + MARK + '$' + bank.toString(16).toUpperCase().padStart(2, '0') + ':' + ins[2] + '  ' + asm + ' */'); insns++; }
        }
        out.push(line);
    }
    return { text: out.join('\n'), insns, funcs };
}

function main() {
    const [genArg, tableArg] = process.argv.slice(2);
    if (!genArg) { console.error('usage: node tools/annotate.mts <src/gen dir> [annotations.tsv]'); process.exit(2); }
    const table = resolve(tableArg || join(ROOT, 'imported', 'annotations.tsv'));
    if (!existsSync(table)) { console.error('annotate: no ' + table + ' (run node tools/import.mts first)'); process.exit(1); }
    const t = readTable(table);
    let insns = 0, funcs = 0, files = 0;
    for (const f of readdirSync(genArg).filter(n => n.endsWith('.c')).sort()) {
        const file = join(genArg, f);
        const before = readFileSync(file, 'utf8');
        const r = annotate(before, t);
        if (r.text !== before) { writeFileSync(file, r.text); files++; }
        insns += r.insns; funcs += r.funcs;
    }
    console.log(`annotate: ${insns} instructions and ${funcs} functions commented, ${files} files changed`);
}

main();
