#!/usr/bin/env node
/* speech_habits_test.js -- V6 Phase 6 conversation rhythm habits.
 *
 * Asserts that the engine writes a tiny speech_habits.bin sidecar,
 * exposes rhythm pressure in state JSON, persists it across restart,
 * and reproduces it byte-identically under the pinned five-tuple.
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const os = require('os');
const crypto = require('crypto');
const { spawn } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const SRC = path.join(ROOT, 'profiles', 'pretorius');
const CHDIR = path.join(os.tmpdir(), 'persona-speech-habits-profile');
const CART = path.join(CHDIR, 'pretorius.cart');

function resetProfile(){
  fs.rmSync(CHDIR, { recursive: true, force: true });
  fs.cpSync(SRC, CHDIR, { recursive: true });
}

function wipe(){
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin',
                   'actor_index.bin','speech_events.bin','dissonance.bin',
                   'open_loops.bin','speech_habits.bin']){
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

function runSession(commands){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: {
        ...process.env,
        PE_TODAY_SEED: '0x6168',
        PE_CLOCK_OVERRIDE_MS: '1700000000000',
      },
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => {
      if (code !== 0) reject(new Error(`host exit ${code}: ${stderr}`));
      else resolve(stdout.split('\n').filter(Boolean).map(l => JSON.parse(l)));
    });
    const stdin = commands.map(c => JSON.stringify(c)).join('\n')
                + '\n{"method":"close"}\n';
    proc.stdin.write(stdin); proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000).unref();
  });
}

function ok(cond, msg){
  if (!cond){ console.error(`not ok: ${msg}`); process.exitCode = 1; }
  else { console.log(`ok:   ${msg}`); }
}

function lastState(rows){
  for (let i = rows.length - 1; i >= 0; --i)
    if (rows[i] && rows[i].name && rows[i].turn_count !== undefined) return rows[i];
  return null;
}

const SCRIPT = [
  { method: 'chat', text: 'Good morning, doctor.' },
  { method: 'chat', text: 'I am thinking about the laboratory.' },
  { method: 'chat', text: 'The weather feels quiet today.' },
  { method: 'chat', text: 'I am still listening.' },
  { method: 'state' },
];

(async function main(){
  console.log('--- V6 Phase 6 speech habits ---');
  resetProfile();

  wipe();
  const rows1 = await runSession(SCRIPT);
  const s1 = lastState(rows1);
  ok(s1 && typeof s1.habit_turns_observed === 'number',
     'state exposes habit_turns_observed');
  ok(s1 && s1.habit_turns_observed >= 4,
     `speech habits observe chat turns (${s1 && s1.habit_turns_observed})`);
  ok(s1 && typeof s1.habit_question_bias === 'number'
        && typeof s1.habit_brevity_bias === 'number'
        && typeof s1.habit_initiative_bias === 'number',
     'state exposes rhythm pressure fields');
  ok(s1 && typeof s1.habit_avg_reply_words === 'number',
     'state exposes average reply length');

  const sidecar1 = sha(path.join(CHDIR, 'speech_habits.bin'));
  ok(sidecar1, 'speech_habits.bin sidecar was written');

  const rows2 = await runSession([{ method: 'state' }]);
  const s2 = lastState(rows2);
  ok(s2 && s2.habit_turns_observed === s1.habit_turns_observed,
     `speech habits survive restart (${s1 && s1.habit_turns_observed} -> ${s2 && s2.habit_turns_observed})`);

  wipe();
  await runSession(SCRIPT);
  const sidecar2 = sha(path.join(CHDIR, 'speech_habits.bin'));
  ok(sidecar1 && sidecar2 && sidecar1 === sidecar2,
     `pinned replay produces byte-identical speech_habits.bin (${sidecar1} vs ${sidecar2})`);

  fs.rmSync(CHDIR, { recursive: true, force: true });

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V6 speech habits persist and replay deterministically');
})().catch(e => { console.error(e); process.exit(2); });
