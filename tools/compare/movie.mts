// The two replay formats: Everscript's .evsmv (text) and MesenCE's .mmo. Only what a comparison needs: the pad of every
// frame and how the recording began. A state holds memory from your own ROM: never commit it.
//
// .evsmv: "# " header lines, then "F|<16 columns>[*n]" per frame (column i = bit 15 - i of the pad word, '.' = released), with other event lines between them.
// .mmo: a zip of Input.txt (one line per input poll: "|<system>|<pad>"), GameSettings.txt ("<key> <value>" lines) and,
// when the recording's RAM setting is Random or it started from a state, SaveState.mss.
//
// SaveState.mss: "MSS", u32 emulator version, u32 format, u32 console, u32 frame buffer size, width, height, scale,
// compressed size, the compressed frame; u32 name length, the ROM's name; u8 compressed, u32 size, u32 compressed size,
// zlib data: fields of a NUL-ended key, a u32 length and the bytes, one after another.
import { inflateRawSync, inflateSync } from 'node:zlib';

const KEYS = 'ABXYLRSTUDLR';
const MMO_BITS = [0x0080, 0x8000, 0x0040, 0x4000, 0x0020, 0x0010, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100];
const MID_GAME_LINE = 225;        // where MesenCE leaves its frame loop, the only moment a state is saved
const RESET_CLOCK_LIMIT = 1000;   // master clocks: a state taken at the reset vector has run nothing yet

/** The pads of an .evsmv recording. A rewind ('L') cannot be replayed. */
export function parseEvsmv(text: string): Uint16Array {
    if (!/^# Everscript movie \d/.test(text)) throw new Error('not an Everscript recording');
    const pads: number[] = [];
    for (const line of text.split('\n')) {
        if (line[0] === 'L') throw new Error('the recording rewinds: not supported');
        if (line[0] !== 'F') continue;
        const body = line.slice(line.indexOf('|') + 1), star = body.indexOf('*');
        const keys = star < 0 ? body : body.slice(0, star);
        let word = 0;
        for (let i = 0; i < 16 && i < keys.length; i++) if (!'. \r'.includes(keys[i])) word |= 1 << (15 - i);
        for (let n = star < 0 ? 1 : Math.max(1, parseInt(body.slice(star + 1), 10) || 1); n > 0; n--) pads.push(word);
    }
    return Uint16Array.from(pads);
}

function readZip(buf: Buffer): { name: string; data: Buffer }[] {
    const out: { name: string; data: Buffer }[] = [];
    const endAt = buf.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
    if (endAt < 0) throw new Error('not a zip: no end record');
    let p = buf.readUInt32LE(endAt + 16);
    for (let i = buf.readUInt16LE(endAt + 10); i > 0; i--) {
        if (buf.readUInt32LE(p) !== 0x02014b50) throw new Error('bad central directory entry');
        const method = buf.readUInt16LE(p + 10), size = buf.readUInt32LE(p + 20), nameLen = buf.readUInt16LE(p + 28);
        const extra = buf.readUInt16LE(p + 30), note = buf.readUInt16LE(p + 32), local = buf.readUInt32LE(p + 42);
        const at = local + 30 + buf.readUInt16LE(local + 26) + buf.readUInt16LE(local + 28);
        const body = buf.subarray(at, at + size);
        out.push({ name: buf.subarray(p + 46, p + 46 + nameLen).toString('utf8'), data: method === 8 ? inflateRawSync(body) : Buffer.from(body) });
        p += 46 + nameLen + extra + note;
    }
    return out;
}

/** The names of the files in a zip (a movie). */
export function zipNames(file: Buffer): string[] {
    return readZip(file).map(f => f.name);
}

/** One text file of a zip (a movie's GameSettings.txt). */
export function unzipText(file: Buffer, name: string): string {
    const f = readZip(file).find(x => x.name === name);
    if (!f) throw new Error(`no ${name} in the zip`);
    return f.data.toString('utf8');
}

/** The state file of a movie, or the file itself when it is a state. */
export function stateOf(file: Buffer, isMovie: boolean): Buffer | undefined {
    return isMovie ? readZip(file).find(f => f.name === 'SaveState.mss')?.data : file;
}

export function inflateState(state: Buffer): Buffer {
    if (state.toString('latin1', 0, 3) !== 'MSS') throw new Error('not a Mesen save state');
    let p = 15 + 16;
    p += 4 + state.readUInt32LE(p);                 // compressed size, the frame
    p += 4 + state.readUInt32LE(p);                 // name length, the name
    const compressed = state[p] !== 0;
    const size = state.readUInt32LE(p + 1), packed = state.readUInt32LE(p + 5);
    p += 9;
    return compressed ? inflateSync(state.subarray(p, p + packed)) : state.subarray(p, p + size);
}

function stateFields(data: Buffer): Map<string, Buffer> {
    const fields = new Map<string, Buffer>();
    for (let q = 0; q < data.length;) {
        const end = data.indexOf(0, q);
        if (end < 0) break;
        const length = data.readUInt32LE(end + 1);
        fields.set(data.toString('latin1', q, end), data.subarray(end + 5, end + 5 + length));
        q = end + 5 + length;
    }
    return fields;
}

function padFromKeys(text: string): number {
    let word = 0;
    for (let i = 0; i < KEYS.length; i++) if (text[i] && text[i] !== '.') word |= MMO_BITS[i];
    return word;
}

/** The movie's input rows (one pad word per poll) and, from its state, the machine it began as. */
export function parseMesenMovie(file: Buffer) {
    const files = new Map(readZip(file).map(f => [f.name, f.data]));
    const input = files.get('Input.txt');
    if (!input || !files.get('GameSettings.txt')) throw new Error('not a Mesen movie');
    const rows = input.toString('latin1').split('\n').filter(line => line[0] === '|').map(line => line.slice(1).split('|'));
    if (rows.some(row => /[^.]/.test(row[0]))) throw new Error('the movie presses reset or power: not supported');
    const pads = Uint16Array.from(rows, row => padFromKeys(row[1] || ''));
    const state = files.get('SaveState.mss');
    let start: { state: Buffer; midGame: boolean; fields: Buffer; wram: Buffer; sram: Buffer; aram: Buffer; dsp: Buffer; hw: Buffer } | null = null;
    if (state) {
        const data = inflateState(state), fields = stateFields(data);
        const clock = fields.get('memoryManager.masterClock'), wram = fields.get('memoryManager.workRam'), sram = fields.get('cart.saveRam');
        const aram = fields.get('spc.ram'), dsp = fields.get('spc.dsp.externalRegs');
        if (!clock || !wram || !sram || !aram || !dsp) throw new Error('the movie\'s save state is not a SNES state');
        const midGame = Number(clock.readBigUInt64LE(0)) > RESET_CLOCK_LIMIT;
        if (midGame && (fields.get('ppu.scanline')?.readUInt16LE(0) ?? 0) !== MID_GAME_LINE) throw new Error('the movie\'s save state is not at a field\'s line 225');
        // What the state's SPC700 and S-DSP do not cover of a reset of a running console: FastROM on.
        start = { state, midGame, fields: data, wram, sram, aram, dsp, hw: Buffer.from([fields.get('internalRegisters.enableFastRom')?.[0] ?? 0]) };
    }
    return { pads, count: pads.length, start };
}
