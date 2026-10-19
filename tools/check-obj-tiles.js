// TODO port to TS (docs/todo/port-to-ts.md)
'use strict';

// Check the game's OBJ tile cache in WRAM dumps (recomp `dump` or tools/recomp/s9x-run.js output).
//   node tools/check-obj-tiles.js <tag.wram.bin>... [--vram]
// The game keeps its 128 dynamic 8x8 OBJ tiles in two doubly linked lists: next[] at $7E9727,
// prev[] at $7E96A5, sentinel $80 = LRU cache, $81 = used this frame (knowledge/ram/7e9727.md).
// On real hardware and snes9x they stay consistent at every frame boundary; a broken link
// hands out OBJ tile $81 and overwrites the HUD portrait (bugs-and-todos.md, v0.201.19).
// With --vram it also prints a hash of OBJ tile $81 from the matching <tag>.vram.bin
// (clean portrait: 326e64).

const fs = require('fs');
const crypto = require('crypto');

const NEXT = 0x9727, PREV = 0x96A5, CACHE = 0x80, USED = 0x81;

/** Problems found in one WRAM image (empty when both lists are consistent) and the used-list length. */
function checkLists(w) {
    const errs = [], owner = new Map();
    let used = 0;
    for (const s of [CACHE, USED]) {
        let prev = s, n = w[NEXT + s], steps = 0;
        while (n !== s && steps++ < 200) {
            if (n === CACHE || n === USED) { errs.push(`list ${s.toString(16)} reaches sentinel ${n.toString(16)}`); break; }
            if (owner.has(n)) { errs.push(`slot ${n.toString(16)} in two lists or a loop`); break; }
            if (w[PREV + n] !== prev) errs.push(`prev[${n.toString(16)}]=${w[PREV + n].toString(16)}, expected ${prev.toString(16)}`);
            owner.set(n, s); if (s === USED) used++;
            prev = n; n = w[NEXT + n];
        }
        if (w[PREV + s] !== prev) errs.push(`prev[sentinel ${s.toString(16)}]=${w[PREV + s].toString(16)}, expected ${prev.toString(16)}`);
    }
    const missing = [];
    for (let i = 0; i < 0x80; i++) if (!owner.has(i)) missing.push(i);
    if (missing.length) errs.push(`${missing.length} slots in no list`);
    return { errs, used };
}

if (require.main === module) {
    const args = process.argv.slice(2), vram = args.includes('--vram');
    const files = args.filter(a => a !== '--vram');
    if (!files.length) { console.error('usage: node tools/check-obj-tiles.js <tag.wram.bin>... [--vram]'); process.exit(2); }
    let bad = 0;
    for (const f of files) {
        const { errs, used } = checkLists(fs.readFileSync(f));
        let tile = '';
        const vf = f.replace(/\.wram\.bin$/, '.vram.bin');
        if (vram && fs.existsSync(vf)) {
            const v = fs.readFileSync(vf);
            tile = '  tile81 ' + crypto.createHash('md5').update(v.subarray(0xC000 + 0x81 * 32, 0xC000 + 0x82 * 32)).digest('hex').slice(0, 6);
        }
        if (errs.length) bad++;
        console.log(`${f.split('/').pop()}  used=${used}${tile}  ${errs.length ? 'BROKEN: ' + errs.slice(0, 3).join('; ') : 'ok'}`);
    }
    process.exit(bad ? 1 : 0);
}

module.exports = { checkLists };
