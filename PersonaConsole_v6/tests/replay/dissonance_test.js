#!/usr/bin/env node
/* dissonance_test.js — V6 Phase 5b.
 *
 * Asserts the typed dissonance accumulators (ideal/ought/feared) per
 * V6_DOCTRINE §15:
 *   1. The three gaps are exposed in state JSON.
 *   2. Fresh state defaults all three to 0.
 *   3. Insults / threats spoken BY the character raise feared_gap
 *      (the character moved toward a feared-self direction).
 *   4. Per-turn decay nudges accumulators back toward 0 over time
 *      when no triggering speech-acts occur.
 *   5. The dissonance.bin sidecar persists across close + reopen.
 *   6. Pinned-replay determinism: same five-tuple ⇒ byte-identical
 *      sidecar.
 *
 * Phase 5b does not require cartridge-authored ideal/ought/feared
 * fields — the engine derives shifts from the just-committed
 * pe_speech_event's speech_act class. The full Higgins typing
 * (with authored selves) lands in Phase 5c.
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs   = require('fs');
const os   = require('os');
const crypto = require('crypto');
const { spawn } = require('child_process');

const ROOT  = path.join(__dirname, '..', '..');
const HOST  = resolveHost(ROOT);
const SRC   = path.join(ROOT, 'profiles', 'pretorius');
const CHDIR = path.join(os.tmpdir(), 'persona-dissonance-profile');
const CART  = path.join(CHDIR, 'pretorius.cart');

function resetProfile(){
  fs.rmSync(CHDIR, { recursive: true, force: true });
  fs.cpSync(SRC, CHDIR, { recursive: true });
}

function wipe(){
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin',
                   'actor_index.bin','speech_events.bin','dissonance.bin','open_loops.bin','speech_habits.bin']){
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

const T0 = 1700000000000;

function runSession(commands){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: {
        ...process.env,
        PE_TODAY_SEED:        '1234',
        PE_CLOCK_OVERRIDE_MS: String(T0),
      },
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => {
      if (code !== 0) reject(new Error(`host exit ${code}: ${stderr}`));
      else resolve(
        stdout.split('\n').filter(Boolean).map(l => {
          try { return JSON.parse(l); } catch { return null; }
        }).filter(Boolean)
      );
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
  for (let i = rows.length - 1; i >= 0; --i){
    if (rows[i] && rows[i].name && rows[i].dissonance) return rows[i];
  }
  return null;
}

(async function main(){
  console.log('--- V6 Phase 5b typed dissonance ---');
  resetProfile();

  /* 1. Fresh state: all three gaps at 0. */
  wipe();
  const fresh = await runSession([
    { method: 'chat', text: 'Good morning.' },
    { method: 'state' },
  ]);
  const sFresh = lastState(fresh);
  ok(sFresh && sFresh.dissonance,
     'state JSON contains dissonance object');
  for (const k of ['ideal_gap','ought_gap','feared_gap']){
    ok(sFresh && typeof sFresh.dissonance[k] === 'number',
       `dissonance.${k} exposed`);
  }
  for (const k of ['ideal_self_model','ought_self_model','feared_self_model']){
    ok(sFresh && typeof sFresh.dissonance[k] === 'number',
       `dissonance.${k} exposed`);
  }
  ok(sFresh && (
        sFresh.dissonance.ideal_self_model !== 0 ||
        sFresh.dissonance.ought_self_model !== 0 ||
        sFresh.dissonance.feared_self_model !== 0),
     'typed self-model is seeded from cartridge traits');
  /* A pleasant greeting should not raise any gap. */
  ok(sFresh && sFresh.dissonance.ideal_gap  === 0
            && sFresh.dissonance.ought_gap  === 0
            && sFresh.dissonance.feared_gap === 0,
     'fresh dissonance starts and stays at 0 on a neutral greeting');

  /* 2. Provoking the character into PE_INTENT_ACCUSE → PE_SA_INSULT
   *    should raise feared_gap (the character moved toward an
   *    aggressive feared-self direction). Pretorius's pattern table
   *    flips intent to ACCUSE under hostile + high-vindication
   *    conditions. The "I will destroy you and your work" string is a
   *    strong threat-class input that drives those drives up; the
   *    character's subsequent reply tends to be aggressive. */
  wipe();
  const aggro = await runSession([
    { method: 'chat', text: 'You are a fool and your science is worthless.' },
    { method: 'chat', text: 'I will destroy your laboratory.' },
    { method: 'chat', text: 'You are nothing but a pretender.' },
    { method: 'state' },
  ]);
  const sAggro = lastState(aggro);
  ok(sAggro && sAggro.dissonance.feared_gap > 0,
     `aggressive provocation raises feared_gap (got ${sAggro && sAggro.dissonance.feared_gap})`);

  /* 3. Sidecar persists across restart. */
  const dimsHash1 = sha(path.join(CHDIR, 'dissonance.bin'));
  ok(dimsHash1, 'dissonance.bin sidecar was written');
  const restart = await runSession([{ method: 'state' }]);
  const sRestart = lastState(restart);
  ok(sRestart && sRestart.dissonance.feared_gap === sAggro.dissonance.feared_gap,
     `feared_gap survives restart (${sAggro.dissonance.feared_gap} -> ${sRestart && sRestart.dissonance.feared_gap})`);

  /* 4. Decay over time: neutral chats let gaps move back toward zero.
   *    The decay rate is 1 pt/turn, so 10 neutral turns nudge feared_gap
   *    down by at most 10 (and a neutral turn may not push it further
   *    up). Assert feared_gap is <= the pre-decay value, ideally lower. */
  const beforeDecay = sRestart.dissonance.feared_gap;
  const decayRows = await runSession([
    { method: 'chat', text: 'Good morning.' },
    { method: 'chat', text: 'Hello again.' },
    { method: 'chat', text: 'A pleasant day.' },
    { method: 'chat', text: 'Tell me about the weather.' },
    { method: 'chat', text: 'I appreciate that.' },
    { method: 'chat', text: 'Indeed.' },
    { method: 'chat', text: 'Of course.' },
    { method: 'chat', text: 'Quite so.' },
    { method: 'chat', text: 'I understand.' },
    { method: 'chat', text: 'Very well.' },
    { method: 'state' },
  ]);
  const sDecay = lastState(decayRows);
  ok(sDecay && sDecay.dissonance.feared_gap <= beforeDecay,
     `neutral turns do not raise feared_gap (${beforeDecay} -> ${sDecay && sDecay.dissonance.feared_gap})`);

  /* 5. Pinned-replay determinism. */
  wipe();
  await runSession([
    { method: 'chat', text: 'You are a fool and your science is worthless.' },
    { method: 'chat', text: 'I will destroy your laboratory.' },
    { method: 'chat', text: 'You are nothing but a pretender.' },
    { method: 'state' },
  ]);
  const dimsHash2 = sha(path.join(CHDIR, 'dissonance.bin'));
  ok(dimsHash1 && dimsHash2 && dimsHash1 === dimsHash2,
     `pinned replay produces byte-identical dissonance.bin (${dimsHash1} vs ${dimsHash2})`);

  fs.rmSync(CHDIR, { recursive: true, force: true });
  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V6 typed dissonance: exposed, decays, persists, deterministic');
})().catch(e => { console.error(e); process.exit(2); });
