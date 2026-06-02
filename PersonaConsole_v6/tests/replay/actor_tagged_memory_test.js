#!/usr/bin/env node
/* actor_tagged_memory_test.js -- V5 Phase 2.
 *
 * Verifies the per-slot actor index sidecar:
 *   1. New episodic commits get tagged with the active actor's hash.
 *   2. Switching to a different actor and committing more tags those slots
 *      with the new actor — counts accumulate across both.
 *   3. The actor_index.bin sidecar persists across a host close+reopen, so
 *      tagged count survives a restart.
 *   4. Under a pinned clock + seed, the tagged count is reproducible byte-
 *      for-byte (deterministic).
 *
 * What this does NOT test (and that's OK for Phase 2): rendering "Henry said
 * X, not you" callbacks at the surface level. That's a Phase 3 dialogue
 * affordance; here we only prove the canonical state mechanism works.
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
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin','actor_index.bin','open_loops.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations','aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function runSession(commands){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: {
        ...process.env,
        PE_TODAY_SEED:        '1234',
        PE_CLOCK_OVERRIDE_MS: '1700000000000',
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

function sha(p){
  return fs.existsSync(p)
    ? crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex').slice(0, 16)
    : null;
}

function lastState(rows){
  for (let i = rows.length - 1; i >= 0; --i){
    if (rows[i] && rows[i].name && rows[i].turn_count !== undefined) return rows[i];
  }
  return null;
}

(async function main(){
  console.log('--- actor-tagged memory (Phase 2) ---');

  /* 1. Fresh session, alternating actors. Each chat commits a memory and
   *    each commit tags its slot with the active actor's user_hash. */
  wipe();
  const rows1 = await runSession([
    { method: 'set_user', user_id: 'henry' },
    { method: 'chat', text: 'Frankenstein was wrong about the bell jar.' },
    { method: 'chat', text: 'Do you remember the third one clouding?' },
    { method: 'set_user', user_id: 'anna' },
    { method: 'chat', text: 'I want to understand your method, doctor.' },
    { method: 'chat', text: 'The lecture hall is quieter than usual.' },
    { method: 'state' },
  ]);
  const s1 = lastState(rows1);
  ok(s1, 'session 1 returned final state');
  ok(s1 && typeof s1.actor_tagged_memories === 'number',
     'state exposes actor_tagged_memories field');
  ok(s1 && s1.actor_tagged_memories >= 4,
     `session 1 tagged at least 4 commits (got ${s1 && s1.actor_tagged_memories})`);

  /* 2. The sidecar must exist on disk and have a non-empty count. */
  const sidecarSha = sha(path.join(CHDIR, 'actor_index.bin'));
  ok(sidecarSha, 'actor_index.bin sidecar was written');

  /* 3. Re-open without wiping: the saved actor tags must survive. */
  const rows2 = await runSession([{ method: 'state' }]);
  const s2 = lastState(rows2);
  ok(s2 && s2.actor_tagged_memories === s1.actor_tagged_memories,
     `tagged count survives restart (before=${s1.actor_tagged_memories} after=${s2 && s2.actor_tagged_memories})`);

  /* 4. Determinism: wipe and replay the same script under the same pins.
   *    The resulting sidecar must hash identically. */
  wipe();
  const rows3 = await runSession([
    { method: 'set_user', user_id: 'henry' },
    { method: 'chat', text: 'Frankenstein was wrong about the bell jar.' },
    { method: 'chat', text: 'Do you remember the third one clouding?' },
    { method: 'set_user', user_id: 'anna' },
    { method: 'chat', text: 'I want to understand your method, doctor.' },
    { method: 'chat', text: 'The lecture hall is quieter than usual.' },
    { method: 'state' },
  ]);
  const s3 = lastState(rows3);
  const sidecarSha3 = sha(path.join(CHDIR, 'actor_index.bin'));
  ok(s3 && s3.actor_tagged_memories === s1.actor_tagged_memories,
     'pinned-replay produces same tagged count');
  ok(sidecarSha && sidecarSha3 && sidecarSha === sidecarSha3,
     `pinned-replay produces byte-identical actor_index.bin (${sidecarSha} vs ${sidecarSha3})`);

  /* Clean up so subsequent gate tests start fresh. */
  wipe();

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- actor-tagged memory survives commit, restart, replay');
})().catch(e => { console.error(e); process.exit(2); });
