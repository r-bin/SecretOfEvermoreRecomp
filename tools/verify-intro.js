'use strict';

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const REPO = path.resolve(__dirname, '..', '..');
const ROM = path.join(REPO, 'out', 'rom.sfc');
const EXE = path.join(REPO, 'out', 'recomp', 'SecretOfEvermore-macos-arm64', 'SecretOfEvermore');
const DUMP_DIR = path.join(REPO, 'out', 'recomp', 'verify_dump');

if (!fs.existsSync(ROM)) {
  console.error('ROM not found at', ROM);
  process.exit(1);
}
if (!fs.existsSync(EXE)) {
  console.error('Recomp executable not found at', EXE);
  process.exit(1);
}

fs.rmSync(DUMP_DIR, { recursive: true, force: true });
fs.mkdirSync(DUMP_DIR, { recursive: true });

// Checkpoints across Cycle 1 and Cycle 2:
// Cycle 1:
//   ~2500: Title screen "Press START"
//   ~3000: Attract demo Thraxx fight (HUD portraits at $7E:ABDE / OAM)
//   ~4500: Attract demo cutscene
// Cycle 2 (loops around frame 6164):
//   ~8600: Title screen "Press START" (2nd time)
//   ~9164: Attract demo Thraxx fight (2nd time)
//   ~10600: Attract demo cutscene (2nd time)
//   ~12300: Reset into Cycle 3
const scriptLines = [
  'turbo on',
  'wait 2500',
  'dump c1_title',
  'wait 500',       // total 3000 (Thraxx fight)
  'dump c1_thraxx',
  'wait 1500',      // total 4500
  'dump c1_demo',
  'wait 4100',      // total 8600
  'dump c2_title',
  'wait 564',       // total 9164 (Thraxx fight 2nd time)
  'dump c2_thraxx',
  'wait 1436',      // total 10600
  'dump c2_demo',
  'wait 1700',      // total 12300
  'dump c3_boot',
  'quit'
];

const scriptPath = path.join(DUMP_DIR, 'script.txt');
fs.writeFileSync(scriptPath, scriptLines.join('\n') + '\n');

console.log('Running SecretOfEvermore headlessly for 12,300 frames (~3.4 minutes of emulated gameplay)...');
const t0 = Date.now();
const res = spawnSync(EXE, ['--no-launcher', '--script', scriptPath, ROM], {
  env: { ...process.env, SNESRECOMP_DUMP_DIR: DUMP_DIR },
  timeout: 180000,
  encoding: 'utf8'
});
const elapsed = ((Date.now() - t0) / 1000).toFixed(1);
console.log(`Execution completed in ${elapsed}s (exit code: ${res.status})`);

if (res.status !== 0) {
  console.error('Run failed with stdout/stderr:', res.stdout, res.stderr);
  process.exit(1);
}

function checkTitle(name, wramFile) {
  if (!fs.existsSync(wramFile)) {
    console.error(`FAIL: ${name} WRAM dump missing`);
    return false;
  }
  const wram = fs.readFileSync(wramFile);
  // OAM buffer for Press START at $7E:ABDE:
  // Expected 16 bytes: 92 69 e8 3a 82 69 ea 3a [72 69 (ec 3a | 64 3b) 62 69 (ee 3a | 66 3b)]
  // Expected letters "P", "r", "e", "s" ("Press START")
  const slice = wram.subarray(0xABDE, 0xABDE + 16);
  const hexStr = slice.toString('hex');
  const validVariants = [
    '9269e83a8269ea3a7269ec3a6269ee3a',
    '9269e83a8269ea3a7269643b6269663b'
  ];
  console.log(`${name} OAM buffer at $7E:ABDE: ${hexStr}`);
  if (validVariants.includes(hexStr)) {
    console.log(`  [PASS] ${name} title screen OAM buffer is 100% correct!`);
    return true;
  } else {
    console.error(`  [FAIL] ${name} title screen OAM buffer mismatch! Expected one of ${validVariants.join(' or ')}, got ${hexStr}`);
    return false;
  }
}

function checkPortrait(name, oamFile) {
  if (!fs.existsSync(oamFile)) {
    console.error(`FAIL: ${name} OAM dump missing`);
    return false;
  }
  const oam = fs.readFileSync(oamFile);
  const boyTile = oam[2];
  const dogTile = oam[6];
  console.log(`${name}: Boy HUD portrait tile=0x${boyTile.toString(16)} (X=${oam[0]}, Y=${oam[1]}), Dog HUD portrait tile=0x${dogTile.toString(16)} (X=${oam[4]}, Y=${oam[5]})`);
  if (boyTile === 0x80 && dogTile === 0x82) {
    console.log(`  [PASS] ${name} HUD portrait sprites are 100% clean (tiles 0x80, 0x82)!`);
    return true;
  } else {
    console.error(`  [FAIL] ${name} HUD portrait sprite corruption detected! Expected tiles 0x80/0x82, got 0x${boyTile.toString(16)}/0x${dogTile.toString(16)}`);
    return false;
  }
}

function checkDemo(name, wramFile) {
  if (!fs.existsSync(wramFile)) {
    console.error(`FAIL: ${name} WRAM dump missing`);
    return false;
  }
  const wram = fs.readFileSync(wramFile);
  const mode = wram.readUInt16LE(0x0B19);
  console.log(`${name} game mode ($7E:0B19): 0x${mode.toString(16)}`);
  return true;
}

let allOk = true;
allOk = checkTitle('Cycle 1 Title (frame 2500)', path.join(DUMP_DIR, 'c1_title.wram.bin')) && allOk;
allOk = checkPortrait('Cycle 1 Thraxx HUD Portrait (frame 3000)', path.join(DUMP_DIR, 'c1_thraxx.oam.bin')) && allOk;
allOk = checkDemo('Cycle 1 Demo (frame 4500)', path.join(DUMP_DIR, 'c1_demo.wram.bin')) && allOk;
allOk = checkTitle('Cycle 2 Title (frame 8600)', path.join(DUMP_DIR, 'c2_title.wram.bin')) && allOk;
allOk = checkPortrait('Cycle 2 Thraxx HUD Portrait (frame 9164)', path.join(DUMP_DIR, 'c2_thraxx.oam.bin')) && allOk;
allOk = checkDemo('Cycle 2 Demo (frame 10600)', path.join(DUMP_DIR, 'c2_demo.wram.bin')) && allOk;

// Check Cycle 3 boot reached
const c3File = path.join(DUMP_DIR, 'c3_boot.wram.bin');
if (fs.existsSync(c3File)) {
  console.log('Cycle 3 boot reached cleanly at frame 12300!');
} else {
  console.error('Cycle 3 dump missing!');
  allOk = false;
}

if (allOk) {
  console.log('\n ALL CHECKS PASSED! Intro works flawlessly across loops!');
  process.exit(0);
} else {
  console.error('\n❌ Verification failed.');
  process.exit(1);
}

