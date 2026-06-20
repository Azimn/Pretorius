#!/usr/bin/env node
/* withhold_logging_test.js — V6 Phase 5c.
 *
 * The Phase 3 self-ledger reserved a withhold_reason slot from day one;
 * Phase 5c populates it. When the character's own speech act is a
 * refusal-class output (PE_SA_REFUSAL/PAUSE/EVASION/WITHDRAWAL/
 * DEFLECTION), the engine tags WHY using canonical state, with a
 * deterministic first-match cascade:
 *
 *   FATIGUE   — exhaustion is highest pressure
 *   SHAME     — feared_gap suggests withdrawal-as-protection
 *   CONFUSION — surprise_last says we did not understand
 *   DISTRUST  — relation_dims.trust is low
 *   STRATEGY  — rhetorical_mode is DEFLECT (chosen, not forced)
 *   PRIVACY   — default catchall
 *
 * "you avoided this before" becomes a true engine assertion — a later
 * actor (or the user, on revisit) can read the ledger and see the
 * reason. No renderer prose touches the firewall.
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs   = require('fs');
const { spawn } = require('child_process');

const HOST  = resolveHost(path.join(__dirname, '..', '..'));
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipe(){
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin',
                   'actor_index.bin','speech_events.bin','dissonance.bin','open_loops.bin','speech_habits.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations','aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
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
    if (rows[i] && rows[i].name && rows[i].last_withhold_reason !== undefined) return rows[i];
  }
  return null;
}

const VALID_REASONS = new Set([
  'none','shame','privacy','distrust','taboo','confusion','fatigue','strategy'
]);

const WITHHOLD_ACTS = new Set([
  'refusal','pause','evasion','withdrawal','deflection'
]);

(async function main(){
  console.log('--- V6 Phase 5c withhold logging ---');

  /* 1. State JSON exposes last_withhold_reason; a neutral turn yields
   *    "none" (non-withhold speech act ⇒ no reason). */
  wipe();
  const neutral = await runSession([
    { method: 'chat', text: 'Good morning, doctor.' },
    { method: 'state' },
  ]);
  const sNeutral = lastState(neutral);
  ok(sNeutral, 'state returned');
  ok(sNeutral && typeof sNeutral.last_withhold_reason === 'string',
     'state JSON exposes last_withhold_reason');
  ok(sNeutral && typeof sNeutral.last_withheld_intent === 'string',
     'state JSON exposes last_withheld_intent');
  ok(sNeutral && VALID_REASONS.has(sNeutral.last_withhold_reason),
     `last_withhold_reason is a valid PE_WR_* name (got "${sNeutral && sNeutral.last_withhold_reason}")`);
  if (sNeutral && !WITHHOLD_ACTS.has(sNeutral.last_speech_act)){
    ok(sNeutral.last_withhold_reason === 'none',
       `non-withhold speech act yields withhold_reason "none" (got speech_act="${sNeutral.last_speech_act}", reason="${sNeutral.last_withhold_reason}")`);
    ok(sNeutral.last_withheld_intent === 'none',
       `non-withhold speech act yields withheld_intent "none" (got "${sNeutral.last_withheld_intent}")`);
  }

  /* 2. Drive a long hostile sequence to push the character into a
   *    refusal-class output (withdrawal, evasion). Pretorius's pattern
   *    table routes sustained insult/threat through SCHEMA_USER_HOSTILE
   *    and eventually flips intent to PE_INTENT_WITHDRAW. When it does,
   *    withhold_reason must NOT be "none". */
  wipe();
  const hostile = await runSession([
    { method: 'chat', text: 'You are a fool.' },
    { method: 'chat', text: 'Your work is worthless and you are pathetic.' },
    { method: 'chat', text: 'I will destroy you.' },
    { method: 'chat', text: 'I hate everything about you.' },
    { method: 'chat', text: 'You should be ashamed.' },
    { method: 'chat', text: 'I am done with you.' },
    { method: 'chat', text: 'Why even bother.' },
    { method: 'state' },
  ]);
  const sH = lastState(hostile);
  ok(sH, 'hostile session returned state');
  /* The hostile pressure should drive at least one withhold somewhere
   * in the sequence — check the LAST recorded speech act. If it landed
   * on a withhold, the reason must be set. If it didn't, the test
   * doesn't gate on a specific final intent (Pretorius may push back
   * verbally instead) but DOES require any populated withhold_reason
   * to be deterministically valid. */
  ok(sH && VALID_REASONS.has(sH.last_withhold_reason),
     `hostile sequence: last_withhold_reason is valid (got speech_act="${sH && sH.last_speech_act}", reason="${sH && sH.last_withhold_reason}")`);
  if (sH && WITHHOLD_ACTS.has(sH.last_speech_act)){
    ok(sH.last_withhold_reason !== 'none',
       `withhold-class speech act has a non-none reason (got "${sH.last_withhold_reason}")`);
  } else {
    console.log(`info  last speech act "${sH.last_speech_act}" is not a withhold class; reason="${sH.last_withhold_reason}"`);
  }

  /* 3. End-to-end cascade trigger: 80 sustained hostile turns push
   *    exhaustion to 1000, which flips intent to PE_INTENT_PAUSE (or
   *    PE_INTENT_WITHDRAW via the high-exhaustion gate in engine.c).
   *    Phase 5c then must tag withhold_reason = FATIGUE, since
   *    exhaustion sits at the top of the cascade. This is the strict
   *    assertion that the reason cascade isn't just default-passing. */
  wipe();
  const fatigueCmds = [];
  for (let i = 0; i < 80; ++i) fatigueCmds.push({ method: 'chat', text: 'You are a fool.' });
  fatigueCmds.push({ method: 'state' });
  const fatigue = await runSession(fatigueCmds);
  const sF = lastState(fatigue);
  ok(sF && sF.exhaustion >= 700,
     `80 hostile turns drive exhaustion past the FATIGUE threshold (got ${sF && sF.exhaustion})`);
  ok(sF && WITHHOLD_ACTS.has(sF.last_speech_act),
     `exhausted character produces a withhold-class speech act (got "${sF && sF.last_speech_act}")`);
  ok(sF && sF.last_withhold_reason === 'fatigue',
     `exhaustion-driven withhold tagged as "fatigue" (got "${sF && sF.last_withhold_reason}")`);
  ok(sF && sF.last_withheld_intent !== 'none',
     `exhaustion-driven withhold records the withheld speech act (got "${sF && sF.last_withheld_intent}")`);

  /* 4. Pinned-replay determinism: same script ⇒ same final
   *    (speech_act, withhold_reason). */
  wipe();
  const replay = await runSession(fatigueCmds);
  const sR = lastState(replay);
  ok(sR && sF && sR.last_withhold_reason === sF.last_withhold_reason
       && sR.last_speech_act === sF.last_speech_act,
     `pinned replay reproduces (speech_act,withhold_reason) — got (${sR && sR.last_speech_act},${sR && sR.last_withhold_reason})`);
  ok(sR && sF && sR.last_withheld_intent === sF.last_withheld_intent,
     `pinned replay reproduces withheld_intent (${sF && sF.last_withheld_intent} -> ${sR && sR.last_withheld_intent})`);

  wipe();
  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V6 withhold logging: exposed, defaulted, valid, fatigue cascade, deterministic');
})().catch(e => { console.error(e); process.exit(2); });
