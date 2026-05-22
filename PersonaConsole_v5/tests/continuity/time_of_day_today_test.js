#!/usr/bin/env node
/* time_of_day_today_test.js -- verifies wall-clock bias in today-state selection. */
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

const LABELS = [
  'bright_and_grandiose',
  'sulky_and_paranoid',
  'tipsy_and_intimate',
  'convalescent',
  'expansive_evening',
  'lecturing_mood',
  'manic_fixation',
  'drunk_brilliant',
  'fragile_theatrical',
];

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function xorshift32(x){
  x >>>= 0;
  x ^= (x << 13) >>> 0;
  x ^= x >>> 17;
  x ^= (x << 5) >>> 0;
  return x >>> 0;
}

function dayHash(seed){
  const d = new Date();
  const key = d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
  return xorshift32((seed ^ key) >>> 0);
}

function weight(label, hour){
  let match = false;
  if (hour >= 6 && hour < 10)
    match = /composed|restless|convalescent/.test(label);
  else if (hour >= 10 && hour < 14)
    match = /lectur|work|expansive/.test(label);
  else if (hour >= 14 && hour < 18)
    match = /expansive|bright|grandiose/.test(label);
  else if (hour >= 18 && hour < 22)
    match = /tipsy|intimate|evening/.test(label);
  else
    match = /theatrical|manic|drunk|paranoid/.test(label);
  return match ? 4 : 1;
}

function pick(seed, hour){
  const weights = LABELS.map(l => weight(l, hour));
  const total = weights.reduce((a, b) => a + b, 0);
  let r = dayHash(seed) % total;
  for (let i = 0; i < weights.length; i++){
    if (r < weights[i]) return LABELS[i];
    r -= weights[i];
  }
  return LABELS[0];
}

function chooseSeed(){
  for (let seed = 1; seed < 10000; seed++){
    const night = pick(seed, 3);
    const day = pick(seed, 14);
    if (night !== day
        && /theatrical|manic|drunk|paranoid/.test(night)
        && /expansive|bright|grandiose/.test(day))
      return seed;
  }
  throw new Error('could not find deterministic seed for time-band check');
}

function run(seed, hour){
  wipeState();
  return new Promise((resolve, reject) => {
    const stdin = JSON.stringify({ method: 'chat', text: 'Hello.' })
                + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: String(seed), PE_TEST_HOUR: String(hour) }
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else {
        const row = stdout.split('\n').filter(l => l.startsWith('{"reply"')).map(l => JSON.parse(l))[0];
        resolve(row && row.state && row.state.today);
      }
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
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
  console.log('--- time-of-day today-state test ---');
  const seed = chooseSeed();
  const night = await run(seed, 3);
  const day = await run(seed, 14);

  ok(night !== day, `same seed selects different states by hour (${night} vs ${day})`);
  ok(/theatrical|manic|drunk|paranoid/.test(night),
     `03:00 favors night-coded state (${night})`);
  ok(/expansive|bright|grandiose/.test(day),
     `14:00 favors day-coded state (${day})`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- time-of-day today-state weighting active');
})().catch(e => { console.error(e); process.exit(2); });
