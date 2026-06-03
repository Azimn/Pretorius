#!/usr/bin/env node
/* self_ledger_test.js — V6 Phase 3 replay test.
 *
 * Asserts the engine-authored speech-event sidecar:
 *   1. Exposes speech_event_count and last_speech_act in state JSON.
 *   2. Records one event per chat turn — count grows monotonically.
 *   3. last_speech_act is a real PE_SA_* name, not "none" or "unknown".
 *   4. The speech_events.bin sidecar is written to disk.
 *   5. The count survives a host close + reopen.
 *   6. Under a pinned engine clock + today seed, two identical input
 *      sequences produce byte-identical sidecars (no wall-clock leaks
 *      into the ledger).
 *   7. Advancing the pinned clock by ~8 days changes the sidecar
 *      bytes — proving clock_ms in each event actually flows through
 *      the canonical clock, not silently bypassed.
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs   = require('fs');
const crypto = require('crypto');
const { spawn } = require('child_process');

const HOST  = resolveHost(path.join(__dirname, '..', '..'));
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

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

function runSession(commands, clockMs){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: {
        ...process.env,
        PE_TODAY_SEED:        '1234',
        PE_CLOCK_OVERRIDE_MS: String(clockMs),
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
    if (rows[i] && rows[i].name && rows[i].turn_count !== undefined) return rows[i];
  }
  return null;
}

const T0 = 1700000000000;
const SCRIPT = [
  { method: 'chat', text: 'Hello, doctor.' },
  { method: 'chat', text: 'What are you working on?' },
  { method: 'chat', text: 'I think your work is immoral.' },
  { method: 'chat', text: 'Tell me about Henry.' },
  { method: 'state' },
];

(async function main(){
  console.log('--- V6 Phase 3 self-ledger (engine-authored speech events) ---');

  /* 1. Fresh session — speech events should accumulate per turn. */
  wipe();
  const rows1 = await runSession(SCRIPT, T0);
  const s1 = lastState(rows1);
  ok(s1, 'session returned final state');
  ok(s1 && typeof s1.speech_event_count === 'number',
     'state exposes speech_event_count');
  ok(s1 && typeof s1.last_speech_act === 'string',
     'state exposes last_speech_act');
  /* One event per chat turn; 4 chats in SCRIPT before the state probe. */
  ok(s1 && s1.speech_event_count === 4,
     `speech_event_count grew to 4 across the four chat turns (got ${s1 && s1.speech_event_count})`);
  ok(s1 && s1.last_speech_act && s1.last_speech_act !== 'none' && s1.last_speech_act !== 'unknown',
     `last_speech_act is a real act name (got "${s1 && s1.last_speech_act}")`);

  /* 2. Sidecar exists on disk. */
  const sidecar1 = sha(path.join(CHDIR, 'speech_events.bin'));
  ok(sidecar1, 'speech_events.bin sidecar was written');

  /* 3. Restart preserves count. */
  const rows2 = await runSession([{ method: 'state' }], T0);
  const s2 = lastState(rows2);
  ok(s2 && s2.speech_event_count === s1.speech_event_count,
     `count survives close+reopen (before=${s1.speech_event_count} after=${s2 && s2.speech_event_count})`);

  /* 4. Deterministic replay — same five-tuple ⇒ byte-identical sidecar. */
  wipe();
  await runSession(SCRIPT, T0);
  const sidecar2 = sha(path.join(CHDIR, 'speech_events.bin'));
  ok(sidecar1 && sidecar2 && sidecar1 === sidecar2,
     `pinned replay produces byte-identical sidecar (${sidecar1} vs ${sidecar2})`);

  /* 5. Advancing the clock changes the sidecar (proves clock_ms actually
   *    flows into events; otherwise the bytes would be identical). */
  wipe();
  await runSession(SCRIPT, T0 + 8 * 86400 * 1000);
  const sidecar3 = sha(path.join(CHDIR, 'speech_events.bin'));
  ok(sidecar1 && sidecar3 && sidecar1 !== sidecar3,
     `advancing clock by 8 days changes sidecar (clock_ms is consulted)`);

  /* Clean up so subsequent tests start fresh. */
  wipe();

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V6 self-ledger records, persists, replays, and observes the clock');
})().catch(e => { console.error(e); process.exit(2); });
