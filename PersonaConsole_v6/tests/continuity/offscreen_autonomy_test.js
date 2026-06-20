#!/usr/bin/env node
/* offscreen_autonomy_test.js -- character state can advance while user is away. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const os = require('os');
const { spawn } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const SRC = path.join(ROOT, 'profiles', 'pretorius');
const CHDIR = path.join(os.tmpdir(), 'persona-offscreen-autonomy-profile');
const CART = path.join(CHDIR, 'pretorius.cart');
const RELDIR = path.join(CHDIR, 'relations');

function resetProfile(){
  fs.rmSync(CHDIR, { recursive: true, force: true });
  fs.cpSync(SRC, CHDIR, { recursive: true });
}

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin','open_loops.bin','speech_habits.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(commands){
  return new Promise((resolve, reject) => {
    const stdin = commands.map(c => JSON.stringify(c)).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '0xA770' }
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve(stdout.split('\n').filter(Boolean).map(l => JSON.parse(l)));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000).unref();
  });
}

function backdateRelation(days){
  const bins = fs.readdirSync(RELDIR).filter(f => f.endsWith('.bin'));
  if (bins.length !== 1) throw new Error(`expected one relation bin, found ${bins.length}`);
  const file = path.join(RELDIR, bins[0]);
  const buf = fs.readFileSync(file);
  const now = Math.floor(Date.now() / 1000);
  buf.writeUInt32LE(now - days * 86400, 8);
  fs.writeFileSync(file, buf);
}

function ok(cond, msg){
  if (!cond){
    console.error(`not ok: ${msg}`);
    process.exitCode = 1;
  } else {
    console.log(`ok:   ${msg}`);
  }
}

(async function main(){
  console.log('--- offscreen autonomy test ---');
  resetProfile();
  wipeState();
  await run([{ method: 'chat', text: 'Good evening, doctor.' }]);
  backdateRelation(2);
  const rows = await run([
    { method: 'chat', text: 'I am back.' },
    { method: 'chat', text: 'What did you do while I was gone?' },
    { method: 'state' }
  ]);
  const first = rows.find(r => typeof r.reply === 'string');
  const state = rows.find(r => Array.isArray(r.want_ages));
  ok(first && /In your absence, I occupied myself with/i.test(first.reply),
     `return after absence mentions autonomous offscreen activity: ${first && first.reply}`);
  ok(rows.some(r => typeof r.reply === 'string' && /bell-jar|weather and consequence|gin to last|work|lightning|Henry/i.test(r.reply)),
     'offscreen activity is tied to authored wants or preoccupations');
  ok(state && state.want_ages.some(v => v >= 24),
     `offscreen gap ages non-selected wants (${state && state.want_ages})`);

  fs.rmSync(CHDIR, { recursive: true, force: true });
  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- offscreen autonomy active');
})().catch(e => { console.error(e); process.exit(2); });
