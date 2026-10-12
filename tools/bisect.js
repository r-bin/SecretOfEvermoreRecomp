'use strict';

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const REPO = path.resolve(__dirname, '..', '..');
const PROJECT = path.join(REPO, 'out', 'recomp', 'project');
const SAVED = path.join(REPO, 'out', 'recomp', 'saved_cfgs');
const CFG_DIR = path.join(PROJECT, 'recomp');
const ROM = path.join(REPO, 'out', 'rom.sfc');
const EXE = path.join(PROJECT, 'build', 'SecretOfEvermoreSNESRecomp' + (process.platform === 'win32' ? '.exe' : ''));
const DUMP_DIR = '/tmp/recomp-bisect-dump';

function run(cmd, argv, opts) {
  const r = spawnSync(cmd, argv, { encoding: 'utf8', maxBuffer: 64 << 20, ...opts });
  return { status: r.status, out: (r.stdout || '') + (r.stderr || '') };
}

function configure(bankFiles) {
  // Clear bank cfgs
  for (const f of fs.readdirSync(CFG_DIR)) {
    if (/^bank[89a-f][0-9a-f]\.cfg$/i.test(f)) {
      fs.rmSync(path.join(CFG_DIR, f));
    }
  }
  // Revert bank00.cfg
  const b00 = path.join(CFG_DIR, 'bank00.cfg');
  let text = fs.readFileSync(path.join(SAVED, 'bank00.cfg'), 'utf8');
  const i = text.indexOf('# >>> everscript data regions');
  if (i >= 0) {
    text = text.slice(0, i).replace(/\n+$/, '\n');
  }
  fs.writeFileSync(b00, text);

  // Copy selected banks
  for (const b of bankFiles) {
    fs.copyFileSync(path.join(SAVED, b), path.join(CFG_DIR, b));
  }

  // Regen
  const env = { ...process.env, SNESRECOMP_ROM: ROM, PATH: require('os').homedir() + '/.cargo/bin:' + process.env.PATH };
  const rGen = run('bash', ['tools/regen.sh', '--cfg-roots', '--rom', ROM], { cwd: PROJECT, env });
  if (rGen.status !== 0) {
    console.error('Regen failed:\n', rGen.out);
    return false;
  }

  // Build
  const rBuild = run('cmake', ['--build', 'build', '-j', '--config', 'Release'], { cwd: PROJECT, env });
  if (rBuild.status !== 0) {
    console.error('Build failed:\n', rBuild.out);
    return false;
  }
  return true;
}

function testRun() {
  fs.rmSync(DUMP_DIR, { recursive: true, force: true });
  fs.mkdirSync(DUMP_DIR, { recursive: true });

  const script = path.join(DUMP_DIR, 'script.txt');
  fs.writeFileSync(script, ['turbo on', 'wait 2850', 'dump f2850', 'quit'].join('\n') + '\n');

  const r = run(EXE, ['--no-launcher', '--script', script, ROM], {
    env: { ...process.env, SNESRECOMP_DUMP_DIR: DUMP_DIR },
    timeout: 30000
  });

  const oamFile = path.join(DUMP_DIR, 'f2850.oam.bin');
  if (!fs.existsSync(oamFile)) {
    return { pass: false, reason: 'no dump produced' };
  }
  const oam = fs.readFileSync(oamFile);
  // Sprite 2 tile: 33 (0x21) in reference, 17 in broken seeded run
  const tile2 = oam[2 * 4 + 2];
  const tile3 = oam[3 * 4 + 2];
  const pass = (tile2 === 33 && tile3 === 34);
  return { pass, tile2, tile3 };
}

async function main() {
  console.log('=== Step 1: Baseline checks ===');
  console.log('Testing interpreter (0 banks)...');
  configure([]);
  const rInterp = testRun();
  console.log('Interpreter result:', rInterp);

  const allBanks = fs.readdirSync(SAVED).filter(f => /^bank[89a-f][0-9a-f]\.cfg$/i.test(f));
  console.log('\nTesting all banks (' + allBanks.join(', ') + ')...');
  configure(allBanks);
  const rAll = testRun();
  console.log('All banks result:', rAll);

  if (!rInterp.pass) {
    console.error('FATAL: Interpreter did not pass baseline check!');
    process.exit(1);
  }
  if (rAll.pass) {
    console.error('FATAL: All banks unexpectedly passed baseline check!');
    process.exit(1);
  }

  console.log('\n=== Step 2: Testing each bank individually ===');
  for (const bank of allBanks) {
    process.stdout.write(`Testing only ${bank.padEnd(12)} ... `);
    configure([bank]);
    const res = testRun();
    console.log(res.pass ? 'PASS (tile=' + res.tile2 + ')' : 'FAIL *** (tile=' + res.tile2 + ')');
  }
}

main().catch(err => { console.error(err); process.exit(1); });

