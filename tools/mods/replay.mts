// A replay for a mod's parity check: a committed replay's input, then more input from a small script, written as an
// Everscript movie (.evsmv) that tools/compare reads like any replay (docs/mods/README.md).
//
//   node tools/mods/replay.mts <base .mmo|.evsmv> <script.txt> <out.evsmv>
//
// Script lines: `press <buttons> <frames>` holds the buttons (b y select start up down left right a x l r, joined
// with +) for that many frames; `wait <frames>` holds nothing; `#` starts a comment. The base must start at power-on
// (no save state): the movie written has no start.
import { readFileSync, writeFileSync } from 'node:fs';
import { readReplay } from '../compare/replay.mts';

const BUTTONS = ['b', 'y', 'select', 'start', 'up', 'down', 'left', 'right', 'a', 'x', 'l', 'r'];   // bit 15 first

export function scriptPads(text: string): number[] {
    const pads: number[] = [];
    for (const raw of text.split('\n')) {
        const line = raw.replace(/#.*/, '').trim();
        if (!line) continue;
        const [cmd, ...args] = line.split(/\s+/);
        if (cmd === 'wait') { for (let n = Number(args[0]); n > 0; n--) pads.push(0); continue; }
        if (cmd !== 'press' || args.length !== 2) throw new Error(`bad line: ${raw}`);
        let word = 0;
        for (const b of args[0].split('+')) {
            const i = BUTTONS.indexOf(b);
            if (i < 0) throw new Error(`unknown button ${b} in: ${raw}`);
            word |= 1 << (15 - i);
        }
        for (let n = Number(args[1]); n > 0; n--) pads.push(word);
    }
    return pads;
}

export function evsmv(pads: ArrayLike<number>, source: string): string {
    const out = ['# Everscript movie 3', '# start: power-on, empty SRAM', `# source: ${source}`];
    const row = (w: number) => Array.from({ length: 16 }, (_, i) => (w & (1 << (15 - i)) ? 'X' : '.')).join('');
    for (let i = 0; i < pads.length;) {
        let n = 1;
        while (i + n < pads.length && pads[i + n] === pads[i]) n++;
        out.push(`F|${row(pads[i])}${n > 1 ? '*' + n : ''}`);
        i += n;
    }
    return out.join('\n') + '\n';
}

if (import.meta.url === `file://${process.argv[1]}`) {
    const [base, script, to] = process.argv.slice(2);
    if (!to) { console.error('usage: replay.mts <base .mmo|.evsmv> <script.txt> <out.evsmv>'); process.exit(2); }
    const replay = readReplay(base);
    if (replay.start) { console.error(`${base} starts from a save state: use a power-on replay`); process.exit(2); }
    const more = scriptPads(readFileSync(script, 'utf8'));
    writeFileSync(to, evsmv([...replay.pads, ...more], `${base} + ${script}`));
    console.log(`${to}: ${replay.pads.length} + ${more.length} frames`);
}
