// tools/compare's pure logic (diff.mts): the tracker finds the first diverging checkpoint whatever order the setups
// report in, and the byte differences name where two runs part. Recorded references read back as written. The committed
// replays hold input and settings only, nothing from a ROM. No ROM, no game.
// Run: node tests/compare-units.test.mts
import assert from 'node:assert';
import { mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Tracker, combined, byteDiff, runs, inOrder, ALL_RAM, REGION_SIZE, SOUND_FIELDS, SPC_SIDE } from '../tools/compare/diff.mts';
import { generateMesenLua } from '../tools/compare/mesen.mts';
import { parseArgs } from '../tools/compare/compare.mts';
import { nmiKey } from '../tools/compare/game.mts';
import { parseEvsmv, parseMesenMovie, zipNames, unzipText } from '../tools/compare/movie.mts';
import { loadRecorded, saveRecorded } from '../tools/compare/recorded.mts';

const scratch = mkdtempSync(join(tmpdir(), 'compare-units-'));
let passed = 0, failed = 0;
function test(name: string, fn: () => void) {
    try { fn(); console.log('  ✓ ' + name); passed++; }
    catch (e) { console.error('  ✗ ' + name + '\n    ' + ((e as Error).stack || e)); failed++; }
}

/** WRAM and VRAM CRCs at a key; `bad` changes the WRAM one from that key on. */
const stream = (key: number, bad = Infinity) => [key < bad ? key * 7 : key * 7 + 1, key * 13];

test('combined hash changes with any region', () => {
    assert.notStrictEqual(combined([1, 2]), combined([1, 3]));
    assert.strictEqual(combined([5, 6]), combined([5, 6]));
});

test('first divergence with the reference first', () => {
    const t = new Tracker(1);
    for (let k = 60; k <= 600; k += 60) t.addRef(k, stream(k));
    const hits: number[] = [];
    for (let k = 60; k <= 600; k += 60) if (t.addTarget(0, k, stream(k, 300))) hits.push(k);
    assert.deepStrictEqual(hits, [300]);
    assert.deepStrictEqual(t.results[0].first, { lastGood: 240, key: 300, regions: [0] });
    assert.strictEqual(t.results[0].differing, 6);
});

test('first divergence with the target first (pending until the reference arrives)', () => {
    const t = new Tracker(2);
    for (let k = 60; k <= 600; k += 60) { t.addTarget(0, k, stream(k, 420)); t.addTarget(1, k, stream(k)); }
    const hits: number[][] = [];
    for (let k = 1; k <= 600; k++) { const h = t.addRef(k, stream(k)); if (h.length) hits.push([k, ...h]); }
    assert.deepStrictEqual(hits, [[420, 0]]);
    assert.strictEqual(t.results[0].first!.lastGood, 360);
    assert.strictEqual(t.results[1].first, null);
    assert.strictEqual(t.results[1].compared, 10);
});

test('every key from power-on is compared', () => {
    const t = new Tracker(1);
    t.addRef(0, [1, 1]); t.addRef(1, [2, 2]);
    assert.strictEqual(t.addTarget(0, 0, [9, 1]), true);
    assert.strictEqual(t.results[0].first?.key, 0);
});

test('each region keeps its own result, as runs of keys', () => {
    const t = new Tracker(1);
    for (let k = 1; k <= 8; k++) t.addRef(k, [k, k]);
    for (let k = 1; k <= 8; k++) t.addTarget(0, k, [k, [2, 3, 7].includes(k) ? 0 : k]);
    const r = t.results[0];
    assert.strictEqual(r.byRegion[0], undefined);
    assert.strictEqual(r.byRegion[1].differing, 3);
    assert.deepStrictEqual(r.byRegion[1].runs, [[2, 3], [7, 7]]);
    assert.strictEqual(r.compared, 8);
});

test('runs of keys follow the stride', () => {
    const t = new Tracker(1, 60);
    for (const k of [60, 120, 240]) { t.addRef(k, [k]); t.addTarget(0, k, [0]); }
    assert.deepStrictEqual(t.results[0].byRegion[0].runs, [[60, 120], [240, 240]]);
});

test('a job hashes the CPU side first, whatever order it was asked in', () => {
    assert.deepStrictEqual(inOrder(['sound', 'aram', 'picture', 'wram']), ['wram', 'picture', 'aram', 'sound']);
    const order = inOrder([...ALL_RAM, 'picture', 'sound']);
    assert.strictEqual(order.findIndex(r => SPC_SIDE.has(r)), order.length - 3);
});

test('the sound region is one 32-bit value a field', () => {
    assert.strictEqual(SOUND_FIELDS.length, 190);
    assert.strictEqual(new Set(SOUND_FIELDS).size, SOUND_FIELDS.length);
    assert.strictEqual(REGION_SIZE.sound, 190 * 4);
});

test('byte diff counts past the cap', () => {
    const a = new Uint8Array(16), b = new Uint8Array(16);
    b[2] = 1; b[3] = 1; b[4] = 1; b[9] = 5;
    const d = byteDiff(a, b, 2);
    assert.strictEqual(d.total, 4);
    assert.deepStrictEqual(d.diffs.map(x => x.offset), [2, 3]);
    assert.deepStrictEqual(runs([9, 2, 3, 4]), [[2, 4], [9, 9]]);
});

test('nmiKey counts NMIs across the attract demo zeroing $0100 and across a wrap', () => {
    const k = nmiKey();
    assert.deepStrictEqual([0x5555, 0, 0, 1, 3, 358, 0, 2].map(k), [-1, 0, 0, 1, 3, 358, 358, 360]);
    const w = nmiKey();
    assert.deepStrictEqual([0, 0xFFFE, 0xFFFF, 0, 1].map(w), [0, 0xFFFE, 0xFFFF, 0x10000, 0x10001]);
});

test('generateMesenLua creates valid script with pad inputs and crc calculations', () => {
    const pads = new Uint16Array([0, 0x1000, 0x8080]);
    const lua = generateMesenLua({
        rom: 'test.sfc',
        to: 120,
        align: 'nmi',
        regions: ['wram', 'vram'],
        stride: 60,
    }, '/tmp/dump.bin', pads);

    assert(lua.includes('local is_nmi = true'));
    assert(lua.includes('local stride = 60'));
    assert(lua.includes('crc_table'));
    assert(lua.includes('emu.addEventCallback'));
    assert(lua.includes('emu.setInput'));
    assert(lua.includes('local pads = { 0,4096,32896 }'));
});

test('parseArgs resolves single ROM across multiple backends without repeating ROM', () => {
    const rom = join(scratch, 'Secret of Evermore (U) [!].smc');
    writeFileSync(rom, '');
    const parsed1 = parseArgs(['mesen', 'recomp', rom]);
    assert.strictEqual(parsed1.setups.length, 2);
    assert.strictEqual(parsed1.setups[0].backend, 'mesen');
    assert.strictEqual(parsed1.setups[0].rom.endsWith(rom), true);
    assert.strictEqual(parsed1.setups[1].backend, 'recomp');
    assert.strictEqual(parsed1.setups[1].rom.endsWith(rom), true);

    const parsed2 = parseArgs([rom, '--backend', 'mesen,recomp']);
    assert.strictEqual(parsed2.setups.length, 2);
    assert.strictEqual(parsed2.setups[0].backend, 'mesen');
    assert.strictEqual(parsed2.setups[1].backend, 'recomp');
});

test('an .evsmv frame line is one column per pad bit, with repeats', () => {
    const pads = parseEvsmv('# Everscript movie 3\nF|B...............\nF|........A.......*2\nS|00000000\nF|................\n');
    assert.deepStrictEqual([...pads], [0x8000, 0x0080, 0x0080, 0]);
    assert.throws(() => parseEvsmv('# Everscript movie 3\nL|3\n'), /rewinds/);
    assert.throws(() => parseEvsmv('nothing'), /not an Everscript/);
});

// A committed movie may hold the input and MesenCE's settings, nothing else: a save state or battery RAM is memory from
// a ROM. The settings must be the ones compare runs MesenCE with (mesen.mts), so the movie replays as it was recorded.
const REPLAYS = new URL('../tools/compare/replays/', import.meta.url);
const committed = readdirSync(REPLAYS).filter(f => f.endsWith('.mmo'));
test('the committed replays are input and settings only, from power-on with zeroed RAM', () => {
    assert.deepStrictEqual(committed.sort(), ['01-no-inputs-1-loop.mmo', '02-new-game-to-flowers.mmo']);
    for (const name of committed) {
        const file = readFileSync(new URL(name, REPLAYS));
        assert.deepStrictEqual(zipNames(file).sort(), ['GameSettings.txt', 'Input.txt'], name);
        const movie = parseMesenMovie(file);
        assert.strictEqual(movie.start, null, name);
        const settings = unzipText(file, 'GameSettings.txt');
        assert.match(settings, /^snes\.ramPowerOnState AllZeros$/m, name);
        assert.match(settings, /^snes\.enableRandomPowerOnState false$/m, name);
        assert.match(settings, /^SHA1 79E7738630FFF5699217EF58ECC421BC8FCBCD89$/m, name);
    }
});

test('replay 01 presses no button', () => {
    const movie = parseMesenMovie(readFileSync(new URL('01-no-inputs-1-loop.mmo', REPLAYS)));
    assert.ok(movie.count > 8000);
    assert.ok(movie.pads.every(p => p === 0));
});

test('a recorded reference reads back as written', () => {
    const file = join(scratch, 'sub', 'r.crc');
    const r = { regions: ['wram', 'vram'] as ('wram' | 'vram')[], stride: 60, keys: new Map([[120, [0xDEADBEEF, 1]], [180, [2, 0]]]) };
    saveRecorded(file, r);
    assert.deepStrictEqual(loadRecorded(file), r);
    writeFileSync(file, '# rng 0102 115\n# regions wram\n');
    assert.throws(() => loadRecorded(file), /RNG copied in/);
});

rmSync(scratch, { recursive: true, force: true });
console.log(`\n${passed} passed, ${failed} failed`);
process.exitCode = failed ? 1 : 0;
