// The MesenCE backend: Mesen run headless from a generated Lua testrunner script.
// Mesen is the cycle-accurate reference emulator for WRAM comparisons (reference-emulators skill).
// Keys are aligned on the NMI frame counter $7E0100 when mixed with the recomp, or frame numbers.
import { mkdtempSync, writeFileSync, rmSync, existsSync, symlinkSync } from 'node:fs';
import { tmpdir, homedir } from 'node:os';
import { join, basename, extname, resolve } from 'node:path';
import { spawn } from 'node:child_process';
import { REGION_SIZE, SPC_SIDE, SOUND_FIELDS, type Region, type Crcs } from './diff.mts';
import { readReplay, type Replay } from './replay.mts';

// The testrunner ends a run after 100 s of wall time unless told otherwise, so a long replay used to stop part way at
// a key that depended on the machine's speed. The Lua script ends the run itself at its target key.
const TESTRUNNER_TIMEOUT = `--timeout=${process.env.MESEN_TIMEOUT ?? 3600}`;

export const MESEN_REGIONS: Region[] = ['wram', 'vram', 'cgram', 'oam', 'sram', 'picture', 'aram', 'dsp', 'sound'];

const DEFAULT_MESEN = '/Applications/Mesen.app/Contents/MacOS/Mesen';

/** Default Mesen executable: MESEN environment variable, or macOS app path. */
export function defaultMesenExe(): string {
    return process.env.MESEN || DEFAULT_MESEN;
}

// Mesen keeps battery RAM per ROM file name in its home's Saves folder and writes it back on exit, so a run on
// the ROM's own name starts from whatever the owner last saved there (a real save file: the game's SRAM checksum
// then sets a stack byte differently from the recomp's, NMI keys 320-321 of the attract replay). Each run gets a
// symlink with a name of its own instead: no .srm, so SRAM starts cleared (ramPowerOnState), and the .srm Mesen
// writes for it is removed afterwards. The owner's saves are never read or written.
const MESEN_HOMES = [join(homedir(), 'Library/Application Support/Mesen2'), join(homedir(), '.config/Mesen2')];

function isolatedRom(dir: string, rom: string): { rom: string; cleanup(): void } {
    const name = basename(dir) + extname(rom);
    const link = join(dir, name);
    symlinkSync(resolve(rom), link);
    const srm = name.slice(0, -extname(name).length || undefined) + '.srm';
    return { rom: link, cleanup: () => { for (const h of MESEN_HOMES) rmSync(join(h, 'Saves', srm), { force: true }); } };
}

export interface MesenJob {
    exe?: string;
    rom: string;
    /** Frames (align frame) or counter value to run to (align nmi). */
    to: number;
    align: 'frame' | 'nmi';
    regions: Region[];
    stride?: number;
    dumpAt?: number;
    replay?: string | null;
    /** Sample as each NMI is taken; the key is the NMI's number since power-on (the recomp's SOE_COMPARE_AT=nmi). */
    atNmi?: boolean;
    /** With atNmi: each NMI's number, master clock and the address it interrupted (a probe). */
    onNmi?: (nmi: number, clock: number, pc: number) => void;
    /** WRAM offsets reported after every emulated frame through `onPeek` (a probe). */
    peek?: number[];
    onPeek?: (frame: number, key: number, clock: number, bytes: number[]) => void;
    /** Lua appended to the script (a probe; it can read `nmi_taken`), and every stdout line no backend line claims. */
    lua?: string;
    onLine?: (line: string) => void;
}

const BUTTON_BITS: [number, string][] = [
    [0x8000, 'b'],
    [0x4000, 'y'],
    [0x2000, 'select'],
    [0x1000, 'start'],
    [0x0800, 'up'],
    [0x0400, 'down'],
    [0x0200, 'left'],
    [0x0100, 'right'],
    [0x0080, 'a'],
    [0x0040, 'x'],
    [0x0020, 'l'],
    [0x0010, 'r'],
];

/** A movie's save state, loaded at the first instruction (a state loads only from an exec callback): the recording itself. */
function startLua(start: NonNullable<Replay['start']>): string {
    const write = (bytes: Uint8Array, type: string) => `
do
  local hex = "${Buffer.from(bytes).toString('hex')}"
  for i = 0, #hex // 2 - 1 do
    local b = tonumber(hex:sub(i * 2 + 1, i * 2 + 2), 16)
    if b ~= 0 then emu.write(i, b, ${type}) end
  end
end`;
    if (start.fresh) return write(start.wram, 'wram') + write(start.sram, 'emu.memType.snesSaveRam');
    return `
do
  local state = ("${Buffer.from(start.state).toString('hex')}"):gsub("%x%x", function(c) return string.char(tonumber(c, 16)) end)
  local id
  id = emu.addMemoryCallback(function()
    emu.removeMemoryCallback(id, emu.callbackType.exec, 0, 0xFFFFFF)
    if not emu.loadSavestate(state) then print("the movie's save state did not load"); emu.stop(1) end
  end, emu.callbackType.exec, 0, 0xFFFFFF)
end`;
}

/** Generate Lua script for Mesen headless runner. */
export function generateMesenLua(job: MesenJob, dumpPath: string, pads: Uint16Array | null, start?: Replay['start']): string {
    const isNmi = job.align === 'nmi';
    const stride = job.stride || 60;
    const target = job.dumpAt !== undefined ? job.dumpAt : job.to;
    const dumpAt = job.dumpAt !== undefined ? job.dumpAt : -1;
    const regions = job.regions;
    const lua = (list: Region[]) => `{ ${list.map(r => `"${r}"`).join(', ')} }`;

    // Button mapping table
    const buttonsLua = BUTTON_BITS.map(([bit, name]) => `{ ${bit}, "${name}" }`).join(', ');

    // Pad inputs array
    let padsLua = '{}';
    if (pads && pads.length > 0) {
        // Lua array of numbers (1-indexed)
        const limit = Math.min(pads.length, isNmi ? job.to * 2 + 2000 : job.to + 100);
        padsLua = `{ ${Array.from(pads.subarray(0, limit)).join(',')} }`;
    }

    return `
local is_nmi = ${isNmi}
local stride = ${stride}
local target = ${target}
local dump_at = ${dumpAt}
local pads = ${padsLua}
local buttons = { ${buttonsLua} }
local regions = ${lua(regions.filter(r => !SPC_SIDE.has(r)))}
local spc_regions = ${lua(regions.filter(r => SPC_SIDE.has(r)))}
local want_picture = ${regions.includes('picture')}
local sound_fields = { ${SOUND_FIELDS.map(f => `"${f}"`).join(', ')} }
local peek = { ${(job.peek ?? []).join(', ')} }

local wram = emu.memType.snesWorkRam
local vram = emu.memType.snesVideoRam
local cgram = emu.memType.snesCgRam
local oam = emu.memType.snesSpriteRam
local sram = emu.memType.snesSaveRam
local aram = emu.memType.spcRam
local dsp = emu.memType.spcDspRegisters
local MEM = { wram = { wram, 0x20000 }, vram = { vram, 0x10000 }, cgram = { cgram, 0x200 }, oam = { oam, 0x220 },
              sram = { sram, 0x2000 }, aram = { aram, 0x10000 }, dsp = { dsp, 0x80 } }

${start ? startLua(start) : ''}
local f = 0
local frames = 0
local at_nmi = ${job.atNmi ? 'true' : 'false'}
local nmi_log = ${job.onNmi ? 'true' : 'false'}
local nmi_taken = 0
local raw_last, nmis = -1, 0
local last_key = -1

-- CRC32 IEEE 802.3 table
local crc_table = {}
for i = 0, 255 do
  local v = i
  for _ = 1, 8 do
    if (v & 1) ~= 0 then
      v = ((v >> 1) ~ 0xEDB88320) & 0xFFFFFFFF
    else
      v = (v >> 1) & 0xFFFFFFFF
    end
  end
  crc_table[i] = v
end

local function crc32_mem(mtype, size)
  local c = 0xFFFFFFFF
  for i = 0, size - 1 do
    local b = emu.read(i, mtype, false)
    c = (crc_table[(c ~ b) & 0xFF] ~ (c >> 8)) & 0xFFFFFFFF
  end
  return (c ~ 0xFFFFFFFF) & 0xFFFFFFFF
end

-- The S-DSP's state as 32-bit little-endian bytes.
local function sound_bytes()
  local st, out = emu.getState(), {}
  for _, f in ipairs(sound_fields) do
    local v = st[f]
    if v == nil then print("no state field " .. f); emu.stop(1); return out end
    if v == true then v = 1 elseif v == false then v = 0 end
    v = math.tointeger(v) & 0xFFFFFFFF
    for s = 0, 24, 8 do out[#out + 1] = (v >> s) & 0xFF end
  end
  return out
end

local function crc32_bytes(bytes)
  local c = 0xFFFFFFFF
  for _, b in ipairs(bytes) do c = (crc_table[(c ~ b) & 0xFF] ~ (c >> 8)) & 0xFFFFFFFF end
  return (c ~ 0xFFFFFFFF) & 0xFFFFFFFF
end

-- The picture is read at EndFrame: SendFrame then swaps the buffers, so at the NMI it is gone. Rows 7-230 of the 239
-- are the 224 lines. A high-resolution frame (512 x 478) gives its even rows and columns.
local picture, picture_hash = nil, 0
local function read_picture()
  local b = emu.getScreenBuffer()
  local px, h = {}, 2166136261
  local n, wide = 0, #b > 256 * 239
  for y = 0, 223 do
    local o = wide and (y + 7) * 2 * 512 or (y + 7) * 256
    for x = 1, 256 do
      local v = b[o + (wide and x * 2 - 1 or x)]
      n = n + 1
      if dump_at >= 0 then px[n] = v end
      h = ((h ~ v) * 16777619) & 0xFFFFFFFF
    end
  end
  picture, picture_hash = px, h
end

-- The testrunner's Lua has no io library: the dump goes to stdout as hex lines, 64 bytes each.
local function dump_bytes(n, at)
  for base = 0, n - 1, 64 do
    local parts = {}
    for i = base, math.min(base + 63, n - 1) do parts[#parts + 1] = string.format("%02X", at(i)) end
    print("X " .. table.concat(parts))
  end
end

local function dump_regions(list)
  for _, r in ipairs(list) do
    if r == "picture" then dump_bytes(#picture * 4, function(i) return (picture[i // 4 + 1] >> ((i % 4) * 8)) & 0xFF end)
    elseif r == "sound" then local s = sound_bytes(); dump_bytes(#s, function(i) return s[i + 1] end)
    else dump_bytes(MEM[r][2], function(i) return emu.read(i, MEM[r][1], false) end) end
  end
end

local function hash_region(r)
  if r == "picture" then return picture_hash end
  if r == "sound" then return crc32_bytes(sound_bytes()) end
  return crc32_mem(MEM[r][1], MEM[r][2])
end

-- The sound side is read at the first SPC700 instruction at least 16 cycles after the NMI's clock (the recomp glue's
-- CmpSpcEnter says why), through a callback that lives until then. masterClock comes as 32 bits; the SPC's cycle gives
-- the rest (a state's clock is past them).
local function at_spc(fn)
  local st = emu.getState()
  local ratio, m = st["spc.clockRatio"], st["masterClock"]
  m = m + math.floor((st["spc.cycle"] / ratio - m) / 4294967296 + 0.5) * 4294967296
  local target = (math.floor(m * ratio) + 1) // 2 * 2 + 32
  local id
  id = emu.addMemoryCallback(function()
    if emu.getState()["spc.cycle"] < target then return end
    emu.removeMemoryCallback(id, emu.callbackType.exec, 0, 0xFFFF, emu.cpuType.spc, emu.memType.spcMemory)
    fn()
  end, emu.callbackType.exec, 0, 0xFFFF, emu.cpuType.spc, emu.memType.spcMemory)
end

-- Inject pad inputs
emu.addEventCallback(function()
  local v = pads[frames + 1] or 0  -- by emulated frame: f stands still while NMI is off
  local inp = {}
  for _, b in ipairs(buttons) do
    if (v & b[1]) ~= 0 then inp[b[2]] = true end
  end
  emu.setInput(inp, 0)
end, emu.eventType.inputPolled)

local function sample(key)
  -- Checkpoint hash
  if (key % stride == 0 or key >= target) and dump_at < 0 then
    local out = "H " .. key
    for _, r in ipairs(regions) do out = out .. " " .. string.format("%08X", hash_region(r)) end
    if #spc_regions == 0 then print(out)
    else
      at_spc(function()
        for _, r in ipairs(spc_regions) do out = out .. " " .. string.format("%08X", hash_region(r)) end
        print(out)
        if key >= target then print("DONE " .. key); emu.stop(0) end
      end)
      if key >= target then return true end
    end
  end

  -- Dump if requested
  if dump_at >= 0 and key >= dump_at then
    dump_regions(regions)
    local function finish() print("D " .. key); emu.stop(0) end
    if #spc_regions == 0 then finish()
    else at_spc(function() dump_regions(spc_regions); finish() end) end
    return true
  end

  if key >= target then
    print("DONE " .. key)
    emu.stop(0)
    return true
  end
  return false
end

-- End of frame processing: check counter and compute checkpoints
emu.addEventCallback(function()
  if want_picture then read_picture() end
  -- The key is the NMI count since the reset code's 0: the attract demo zeroes $0100 when it loops,
  -- so a drop counts as a restart from 0 (the recomp glue's SOE_COMPARE key does the same).
  local raw = emu.read(0x0100, wram, false) | (emu.read(0x0101, wram, false) << 8)
  if raw_last < 0 then
    if raw == 0 then raw_last = 0 end
  elseif raw ~= raw_last then
    if raw > raw_last then nmis = nmis + raw - raw_last
    elseif raw_last > 0xFF00 then nmis = nmis + raw + 0x10000 - raw_last
    else nmis = nmis + raw end
    raw_last = raw
  end
  local cnt = raw_last < 0 and -1 or nmis
  local key = is_nmi and cnt or (f + 1)
  if #peek > 0 then
    local parts = { "P " .. frames .. " " .. cnt .. " " .. emu.getState()["masterClock"] }
    for _, a in ipairs(peek) do parts[#parts + 1] = string.format("%02x", emu.read(a, wram, false)) end
    print(table.concat(parts, " "))
  end
  frames = frames + 1

  if is_nmi then
    if key < 0 or key <= last_key then return end
  end
  last_key = key

  if not at_nmi and sample(key) then return end

  f = f + 1
end, emu.eventType.endFrame)

-- at_nmi: sample as each NMI is taken (after its frame is pushed); the key is the NMI's number since power-on.
if at_nmi then
  emu.addEventCallback(function()
    nmi_taken = nmi_taken + 1
    if nmi_log then
      local st = emu.getState()
      local sp = st["cpu.sp"]
      local pc = emu.read((sp + 2) & 0xFFFF, wram, false) | (emu.read((sp + 3) & 0xFFFF, wram, false) << 8) | (emu.read((sp + 4) & 0xFFFF, wram, false) << 16)
      print(string.format("N %d %d %06x", nmi_taken, st["masterClock"], pc))
    end
    sample(nmi_taken)
  end, emu.eventType.nmi)
end
${job.lua ?? ''}
`;
}

/** Run Mesen headless; `onCp` gets each checkpoint. Resolves with dump when dumpAt was set. */
export function runMesen(job: MesenJob, onCp: (key: number, crcs: Crcs) => void): { done: Promise<Uint8Array[] | null>; kill(): void } {
    const exe = job.exe || defaultMesenExe();
    if (!existsSync(exe)) throw new Error(`no Mesen executable at ${exe} (install Mesen or set MESEN environment variable)`);

    const dir = mkdtempSync(join(tmpdir(), 'soe-mesen-'));
    const luaPath = join(dir, 'compare.lua');
    const dumpPath = join(dir, 'dump.bin');

    let pads: Uint16Array | null = null, replay: Replay | null = null;
    if (job.replay) {
        replay = readReplay(job.replay);
        pads = replay.pads;
    }

    const script = generateMesenLua(job, dumpPath, pads, replay?.start);
    writeFileSync(luaPath, script);

    const iso = isolatedRom(dir, job.rom);
    // Frame skipping would leave stale pictures in the buffer at the testrunner's unlimited speed.
    const child = spawn(exe, ['--testrunner', iso.rom, luaPath, '--snes.ramPowerOnState=AllZeros', '--snes.disableFrameSkipping=true', TESTRUNNER_TIMEOUT], {
        cwd: dir,
        stdio: ['ignore', 'pipe', 'pipe'],
    });

    let log = '';
    child.stderr?.on('data', d => { log = (log + d).slice(-4000); });
    let dump: Uint8Array[] | null = null;
    const hex: string[] = [];
    let sawDone = false;

    let rest = '';
    child.stdout?.on('data', (d: Buffer) => {
        const lines = (rest + d.toString('utf8')).split('\n');
        rest = lines.pop()!;   // a line can span two chunks
        for (const line of lines) {
            const trimmed = line.trim();
            if (!trimmed) continue;
            if (trimmed.startsWith('H ')) {
                const parts = trimmed.split(' ');
                const key = Number(parts[1]);
                const crcs = parts.slice(2).map(x => parseInt(x, 16));
                onCp(key, crcs);
            } else if (trimmed.startsWith('N ')) {
                const p = trimmed.split(' ');
                job.onNmi?.(Number(p[1]), Number(p[2]), parseInt(p[3], 16));
            } else if (trimmed.startsWith('P ')) {
                const p = trimmed.split(' ');
                job.onPeek?.(Number(p[1]), Number(p[2]), Number(p[3]), p.slice(4).map(x => parseInt(x, 16)));
            } else if (trimmed.startsWith('X ')) {
                hex.push(trimmed.slice(2));
            } else if (trimmed.startsWith('D ')) {
                sawDone = true;
                const raw = Buffer.from(hex.join(''), 'hex');
                let off = 0;
                dump = job.regions.map(r => {
                    const slice = new Uint8Array(raw.subarray(off, off + REGION_SIZE[r]));
                    off += REGION_SIZE[r];
                    return slice;
                });
            } else if (trimmed.startsWith('DONE ')) {
                sawDone = true;
            } else job.onLine?.(trimmed);
        }
    });

    const done = new Promise<Uint8Array[] | null>((resolve, reject) => {
        child.on('close', code => {
            rmSync(dir, { recursive: true, force: true });
            iso.cleanup();
            // Mesen testrunner can exit 255 even on emu.stop(0) (reference-emulators skill trap)
            if (code !== 0 && code !== 255 && !sawDone && !dump && child.signalCode === null) {
                reject(new Error(`Mesen exited with ${code}: ${log.trim().split('\n').slice(-3).join(' | ')}`));
            } else {
                resolve(dump);
            }
        });
    });

    return { done, kill: () => { if (child.exitCode === null) child.kill(); } };
}
