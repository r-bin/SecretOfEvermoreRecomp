// tools/import.mts and tools/annotate.mts on synthetic folders: the seeds replace our bank cfgs and leave one data-region
// block in bank00.cfg however often they are imported, the bound C is the CMake list plus the headers, and the comments
// for the generated C replace each other. No ROM, no plugin checkout.
// Run: node tests/import.test.mts
import assert from 'node:assert';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { importSeeds, importBoundC } from '../tools/import.mts';

const scratch = mkdtempSync(join(tmpdir(), 'import-units-'));
let passed = 0, failed = 0;
function test(name: string, fn: () => void) {
    try { fn(); console.log('  ✓ ' + name); passed++; }
    catch (e) { console.error('  ✗ ' + name + '\n    ' + ((e as Error).stack || e)); failed++; }
}
const write = (file: string, text: string) => { mkdirSync(resolve(file, '..'), { recursive: true }); writeFileSync(file, text); };

test('seeds: bank cfgs replaced, bank00 keeps its block and gains one data-region block', () => {
    const root = join(scratch, 'seeds-root'), seeds = join(scratch, 'seeds');
    write(join(root, 'recomp', 'bank00.cfg'), 'bank = 0\nauto_vectors\n');
    write(join(root, 'recomp', 'bank8f.cfg'), 'stale\n');
    write(join(seeds, 'bank00.cfg'), 'bank = 00\nfunc x 8000\ndata_region c0 0000 08000\n');
    write(join(seeds, 'bank80.cfg'), 'bank = 80\nfunc random_number_generator 859b\n');
    importSeeds(seeds, root);
    const r = importSeeds(seeds, root);
    const b00 = readFileSync(join(root, 'recomp', 'bank00.cfg'), 'utf8');
    assert.deepStrictEqual(r, { banks: 1, regions: 1 });
    assert.ok(b00.startsWith('bank = 0\nauto_vectors\n'), 'our block kept');
    assert.strictEqual((b00.match(/^data_region/gm) || []).length, 1, 'idempotent');
    assert.strictEqual((b00.match(/^# >>> everscript data regions/gm) || []).length, 1, 'one block');
    assert.ok(!/func x/.test(b00), 'the plugin\'s bank00 funcs are not copied');
    assert.ok(!existsSync(join(root, 'recomp', 'bank8f.cfg')), 'stale bank removed');
    assert.ok(readFileSync(join(root, 'recomp', 'bank80.cfg'), 'utf8').includes('random_number_generator'));
});

test('bound C: the CMake list and every header, old files gone, a missing file refused', () => {
    const root = join(scratch, 'c-root'), source = join(scratch, 'reconstructed-c');
    write(join(root, 'CMakeLists.txt'), 'set(SOE_BOUND_C 80/80859B_rng.c\n    8F/8FA914_plane.c)\n');
    write(join(root, 'src', 'bound-c', 'old.c'), 'gone\n');
    write(join(source, '80', '80859B_rng.c'), 'rng\n');
    write(join(source, '8F', '8FA914_plane.c'), 'plane\n');
    write(join(source, '8F', 'unbound.c'), 'not listed\n');
    write(join(source, 'include', 'soe_ram.h'), 'ram\n');
    assert.deepStrictEqual(importBoundC(source, root), { files: 3 });
    const bound = join(root, 'src', 'bound-c');
    assert.strictEqual(readFileSync(join(bound, '80', '80859B_rng.c'), 'utf8'), 'rng\n');
    assert.ok(existsSync(join(bound, 'include', 'soe_ram.h')) && existsSync(join(bound, 'README.md')));
    assert.ok(!existsSync(join(bound, 'old.c')) && !existsSync(join(bound, '8F', 'unbound.c')));
    rmSync(join(source, '8F', '8FA914_plane.c'));
    assert.throws(() => importBoundC(source, root), /8FA914_plane/);
    assert.ok(existsSync(join(bound, '8F', '8FA914_plane.c')), 'a refused import leaves the old copy');
});

test('annotate: routine and instruction comments, a second run replaces them', () => {
    const gen = join(scratch, 'gen'), table = join(scratch, 'annotations.tsv');
    write(table, '# off\tkind\ttext\n00859b\tf\trandom_number_generator: four reads\n00859b\ti\tLDA $0E\n');
    const c = 'RecompReturn f(CpuState *cpu) {\n  cpu_trace_func_entry(cpu, 0x80859B, 0);\n  cpu_aot_insn_begin(cpu, 0x859Bu);\n}\n';
    write(join(gen, 'bank80.c'), c);
    const run = () => spawnSync(process.execPath, [join(import.meta.dirname, '..', 'tools', 'annotate.mts'), gen, table], { encoding: 'utf8' });
    assert.strictEqual(run().status, 0);
    const once = readFileSync(join(gen, 'bank80.c'), 'utf8');
    assert.ok(once.includes('/* evs: random_number_generator: four reads */'));
    assert.ok(once.includes('/* evs: $80:859B  LDA $0E */'));
    run();
    assert.strictEqual(readFileSync(join(gen, 'bank80.c'), 'utf8'), once);
});

rmSync(scratch, { recursive: true, force: true });
console.log(`${passed} passed, ${failed} failed`);
if (failed) process.exit(1);
