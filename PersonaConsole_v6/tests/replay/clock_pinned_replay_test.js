#!/usr/bin/env node
/* clock_pinned_replay_test.js -- Layer 1 state is a pure function of the
 * canonical five-tuple (cartridge, seed, WAL, inputs, clock). With the
 * clock pinned via PE_CLOCK_OVERRIDE_MS, replaying the same inputs must
 * produce byte-identical Layer 1 state (state.bin). And advancing the
 * pinned clock must change time-driven state — that's how we know the
 * clock is actually being consulted, not silently bypassed. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');
const crypto = require('crypto');

const HOST  = resolveHost(path.join(__dirname, '..', '..'));
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin','open_loops.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations','aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function sha(p){
  return fs.existsSync(p)
    ? crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex').slice(0, 16)
    : null;
}

function runOnce(envOverride, inputs){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '0xC0FFEE', ...envOverride }
    });
    let stderr = '';
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.stdout.on('data', () => {});  /* drain */
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve();
    });
    const lines = inputs.map(t => JSON.stringify({ method: 'chat', text: t }));
    lines.push(JSON.stringify({ method: 'close' }));
    proc.stdin.write(lines.join('\n') + '\n');
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

function ok(cond, msg){
  if (!cond){ console.error(`not ok: ${msg}`); process.exitCode = 1; }
  else { console.log(`ok:   ${msg}`); }
}

/* Anchor: Tue Nov 14 2023 22:13:20 UTC. Arbitrary but stable. */
const T0 = 1700000000000;
const INPUTS = [
  'Good morning, doctor.',
  'What are you working on?',
  'I think your work is immoral.',
  'Tell me something honestly.',
];

function snapshot(){
  return {
    state:       sha(path.join(CHDIR, 'state.bin')),
    memory:      sha(path.join(CHDIR, 'memory.bin')),
    chapters:    sha(path.join(CHDIR, 'chapters.bin')),
    reflections: sha(path.join(CHDIR, 'reflections.bin')),
  };
}

(async function main(){
  console.log('--- clock-pinned replay (Phase 1) ---');

  wipeState();
  await runOnce({ PE_CLOCK_OVERRIDE_MS: String(T0) }, INPUTS);
  const snap1 = snapshot();

  wipeState();
  await runOnce({ PE_CLOCK_OVERRIDE_MS: String(T0) }, INPUTS);
  const snap2 = snapshot();

  ok(snap1.state && snap2.state, 'both pinned-clock runs wrote state.bin');

  let identical = true;
  for (const k of Object.keys(snap1)){
    if (snap1[k] !== snap2[k]){
      identical = false;
      console.error(`  diverged: ${k}  run1=${snap1[k]}  run2=${snap2[k]}`);
    }
  }
  ok(identical, 'pinned clock + identical inputs ⇒ byte-identical Layer 1 state');

  /* Advance the clock by ~8 days; state.bin must differ — proves the
   * clock is actually being consulted by canonical paths. */
  wipeState();
  const T1 = T0 + 8 * 86400 * 1000;
  await runOnce({ PE_CLOCK_OVERRIDE_MS: String(T1) }, INPUTS);
  const snap3 = snapshot();

  ok(snap3.state, 'advanced-clock run wrote state.bin');
  ok(snap1.state && snap3.state && snap1.state !== snap3.state,
     'advancing pinned clock by 8 days changes Layer 1 state (clock is consulted)');

  /* Clean up so subsequent tests start from a fresh state. */
  wipeState();

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- clock-pinned replay determinism intact');
})().catch(e => { console.error(e); process.exit(2); });
