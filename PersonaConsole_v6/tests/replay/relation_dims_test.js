#!/usr/bin/env node
/* relation_dims_test.js — V6 Phase 4.
 *
 * Asserts the multi-dimensional Relation sidecar (relations/<hash>.dims):
 *   1. State JSON exposes all nine dimensions.
 *   2. A fresh actor's dims are derived from disposition (trust ≈ disp,
 *      threat ≈ 1000-disp, intimacy=0 below the confidant threshold,
 *      admiration ≈ 0.7·disp, others=0).
 *   3. The .dims sidecar file is written under relations/<hash>.dims.
 *   4. Dims persist across host close + reopen.
 *   5. Two different actors get isolated dims files (asymmetry works).
 *   6. Pinned five-tuple ⇒ byte-identical sidecar across replays.
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs   = require('fs');
const crypto = require('crypto');
const { spawn } = require('child_process');

const HOST  = resolveHost(path.join(__dirname, '..', '..'));
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);
const RDIR  = path.join(CHDIR, 'relations');

function wipe(){
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin',
                   'actor_index.bin','speech_events.bin','open_loops.bin','speech_habits.bin']){
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
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

function ok(cond, msg){
  if (!cond){ console.error(`not ok: ${msg}`); process.exitCode = 1; }
  else { console.log(`ok:   ${msg}`); }
}

function lastState(rows){
  for (let i = rows.length - 1; i >= 0; --i){
    if (rows[i] && rows[i].name && rows[i].relation_dims) return rows[i];
  }
  return null;
}

(async function main(){
  console.log('--- V6 Phase 4 multi-dim relation ---');

  /* 1. Fresh session with actor "henry" — dims should appear in state. */
  wipe();
  const rows1 = await runSession([
    { method: 'set_user', user_id: 'henry' },
    { method: 'chat', text: 'Hello, doctor.' },
    { method: 'state' },
  ]);
  const s1 = lastState(rows1);
  ok(s1, 'session returned final state');
  const dims1 = s1 && s1.relation_dims;
  ok(dims1, 'state JSON contains relation_dims object');
  for (const k of ['trust','threat','intimacy','resentment','dependency',
                   'obligation','envy','admiration','embarrassment']){
    ok(dims1 && typeof dims1[k] === 'number',
       `relation_dims.${k} exposed`);
  }

  /* 2. Fresh actor at disposition≈500 ⇒ trust≈500, threat≈500, intimacy=0,
   *    admiration≈350, others=0. */
  ok(dims1.trust > 400 && dims1.trust < 600,
     `henry trust ~500 from default disposition (got ${dims1.trust})`);
  ok(dims1.threat > 400 && dims1.threat < 600,
     `henry threat ~500 (got ${dims1.threat})`);
  ok(dims1.intimacy === 0,
     `henry intimacy = 0 (below confidant threshold) (got ${dims1.intimacy})`);
  ok(dims1.resentment === 0 && dims1.dependency === 0
       && dims1.obligation === 0 && dims1.envy === 0
       && dims1.embarrassment === 0,
     'event-driven dims start at zero for a brand-new actor');

  /* 3. The per-actor .dims file is written. The engine implicitly loads
   * a "Someone" actor at session open before set_user runs, so there's
   * always one extra .dims file for Someone alongside henry's. */
  const dimsFiles = fs.existsSync(RDIR)
    ? fs.readdirSync(RDIR).filter(f => f.endsWith('.dims'))
    : [];
  ok(dimsFiles.length === 2,
     `relations/ holds .dims for Someone + henry (got ${dimsFiles.length})`);
  /* Composite hash of the whole .dims set (sorted) so the replay check
   * is order-independent. */
  const allDimsHash = (dir) => {
    const files = fs.readdirSync(dir).filter(f => f.endsWith('.dims')).sort();
    const h = crypto.createHash('sha256');
    for (const f of files){
      h.update(f); h.update('\0');
      h.update(fs.readFileSync(path.join(dir, f)));
    }
    return h.digest('hex').slice(0, 16);
  };
  const compositeAfterRun1 = allDimsHash(RDIR);
  ok(compositeAfterRun1, 'all .dims files composite-hashable');

  /* 4. Restart preserves dims. */
  const rows2 = await runSession([
    { method: 'set_user', user_id: 'henry' },
    { method: 'state' },
  ]);
  const s2 = lastState(rows2);
  ok(s2 && s2.relation_dims.trust === dims1.trust
       && s2.relation_dims.admiration === dims1.admiration,
     `dims survive close+reopen (trust=${s2 && s2.relation_dims.trust}, admiration=${s2 && s2.relation_dims.admiration})`);

  /* 5. A second actor gets isolated dims. */
  const rows3 = await runSession([
    { method: 'set_user', user_id: 'anna' },
    { method: 'chat', text: 'Hi.' },
    { method: 'state' },
  ]);
  const s3 = lastState(rows3);
  const allDimsFiles = fs.readdirSync(RDIR).filter(f => f.endsWith('.dims'));
  /* Someone + henry + anna = 3 files (the implicit Someone is kept). */
  ok(allDimsFiles.length === 3,
     `three .dims files exist after Someone+henry+anna (got ${allDimsFiles.length})`);
  ok(s3 && s3.relation_dims, 'anna gets her own relation_dims block in state');

  /* 6. Pinned-replay determinism — the entire .dims set, byte-identical
   * across runs when the five-tuple is pinned. */
  wipe();
  await runSession([
    { method: 'set_user', user_id: 'henry' },
    { method: 'chat', text: 'Hello, doctor.' },
    { method: 'state' },
  ]);
  const compositeAfterReplay = allDimsHash(RDIR);
  ok(compositeAfterRun1 && compositeAfterReplay
       && compositeAfterRun1 === compositeAfterReplay,
     `pinned replay produces byte-identical .dims set (${compositeAfterRun1} vs ${compositeAfterReplay})`);

  /* 7. V6 Phase 5a: event-driven dim updates. Same five-tuple, fresh
   *    state, different inputs ⇒ dims move in the expected direction.
   *    No reference to specific Pretorius cartridge content — the
   *    classifier is pattern-table-driven and character-agnostic. */
  wipe();
  const praiseRows = await runSession([
    { method: 'set_user', user_id: 'henry' },
    { method: 'chat', text: 'You are brilliant.' },
    { method: 'state' },
  ]);
  const sPraise = lastState(praiseRows);
  ok(sPraise && sPraise.relation_dims,
     'praise input returns dims');
  /* Defaults at fresh-actor: trust=500, admiration=350. After praise,
   * one or both must rise (the suspicion branch is only triggered when
   * threat>600, which a fresh actor's 500 baseline does not reach). */
  ok(sPraise && (sPraise.relation_dims.trust > 500
              || sPraise.relation_dims.admiration > 350),
     `praise lifts trust and/or admiration (got trust=${sPraise && sPraise.relation_dims.trust}, admiration=${sPraise && sPraise.relation_dims.admiration})`);

  wipe();
  const insultRows = await runSession([
    { method: 'set_user', user_id: 'henry' },
    { method: 'chat', text: 'You are a fool. Your work is worthless.' },
    { method: 'state' },
  ]);
  const sIns = lastState(insultRows);
  ok(sIns && sIns.relation_dims,
     'insult input returns dims');
  ok(sIns && sIns.relation_dims.threat > 500,
     `insult lifts threat above 500 baseline (got ${sIns && sIns.relation_dims.threat})`);
  ok(sIns && sIns.relation_dims.resentment > 0,
     `insult lifts resentment from 0 (got ${sIns && sIns.relation_dims.resentment})`);
  ok(sIns && sIns.relation_dims.trust < 500,
     `insult lowers trust below 500 baseline (got ${sIns && sIns.relation_dims.trust})`);

  /* 8. Suspicion-asymmetry — same praise input from a HIGH-threat actor
   *    reads differently. We push henry's threat above 600 by repeated
   *    insults, then a praise: in the suspicion branch praise does NOT
   *    raise trust (which a low-threat praise would). This proves the
   *    same input class produces different dim updates depending on
   *    current relational state — the asymmetry V6 §13 promises. */
  wipe();
  const susRows = await runSession([
    { method: 'set_user', user_id: 'henry' },
    { method: 'chat', text: 'You are a fool.' },
    { method: 'chat', text: 'You are a fool.' },
    { method: 'chat', text: 'You are a fool.' },
    { method: 'chat', text: 'You are a fool.' },
    { method: 'state' },
    /* now praise once: suspicion branch should engage */
    { method: 'chat', text: 'You are brilliant.' },
    { method: 'state' },
  ]);
  const states = susRows.filter(r => r && r.relation_dims);
  const beforePraise = states[states.length - 2];
  const afterPraise  = states[states.length - 1];
  ok(beforePraise && beforePraise.relation_dims.threat > 600,
     `four insults push threat above 600 (got ${beforePraise && beforePraise.relation_dims.threat})`);
  ok(afterPraise && afterPraise.relation_dims.trust === beforePraise.relation_dims.trust,
     `praise in suspicion branch does NOT lift trust (before=${beforePraise && beforePraise.relation_dims.trust}, after=${afterPraise && afterPraise.relation_dims.trust})`);

  wipe();
  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V6 multi-dim relation: dims exposed, defaulted, persisted, per-actor, deterministic, event-driven, and asymmetric');
})().catch(e => { console.error(e); process.exit(2); });
