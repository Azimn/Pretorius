#!/usr/bin/env node
/* open_loops_test.js -- V6 Phase 6 carried intentions.
 *
 * Asserts that a half-spoken unresolved thread creates a structured
 * open-loop sidecar, exposes it through state JSON, survives restart,
 * and replays byte-identically under the pinned five-tuple.
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const crypto = require('crypto');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipe(){
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin',
                   'actor_index.bin','speech_events.bin','dissonance.bin',
                   'open_loops.bin']){
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

function runSession(commands, extraEnv = {}){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: {
        ...process.env,
        PE_TODAY_SEED: '0x5155',
        PE_CLOCK_OVERRIDE_MS: '1700000000000',
        ...extraEnv,
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
    setTimeout(() => proc.kill('SIGKILL'), 30000);
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
  { method: 'chat', text: 'fragment zero with loose conclusion' },
  { method: 'chat', text: 'fragment one with loose conclusion' },
  { method: 'chat', text: 'fragment two with loose conclusion' },
  { method: 'chat', text: 'fragment three with loose conclusion' },
  { method: 'idle_probe' },
  { method: 'state' },
];
const FORCE = { PE_FORCE_UNRESOLVED_THREAD: '1' };

(async function main(){
  console.log('--- V6 Phase 6 open loops ---');

  wipe();
  const rows1 = await runSession(SCRIPT, FORCE);
  const half = rows1.find(r => r && typeof r.reply === 'string'
                            && /never mind/i.test(r.reply));
  const idle = rows1.find(r => r && typeof r.reply === 'string'
                            && /still owe you the rest/i.test(r.reply));
  const s1 = lastState(rows1);
  ok(!!half, 'forced unresolved turn emits a half-spoken line');
  ok(!!idle, 'idle probe resurfaces the carried topic');
  ok(s1 && typeof s1.open_loop_count === 'number',
     'state exposes open_loop_count');
  ok(s1 && s1.open_loop_count > 0,
     `open_loop_count increments (got ${s1 && s1.open_loop_count})`);
  ok(s1 && s1.open_loop_resolved_count === 0,
     'idle probe does not resolve an open loop');

  const sidecar1 = sha(path.join(CHDIR, 'open_loops.bin'));
  ok(sidecar1, 'open_loops.bin sidecar was written');

  const rows2 = await runSession([{ method: 'state' }]);
  const s2 = lastState(rows2);
  ok(s2 && s2.open_loop_count === s1.open_loop_count,
     `open loops survive restart (before=${s1.open_loop_count} after=${s2 && s2.open_loop_count})`);

  const resolveRows = await runSession([
    { method: 'chat', text: 'Tell me about your work.' },
    { method: 'state' },
  ]);
  const sResolve = lastState(resolveRows);
  ok(sResolve && sResolve.open_loop_resolved_count > 0,
     `matching topic chat resolves carried loop (resolved=${sResolve && sResolve.open_loop_resolved_count})`);
  ok(sResolve && sResolve.open_loop_count < s2.open_loop_count,
     `active open-loop count drops after resolution (${s2.open_loop_count} -> ${sResolve && sResolve.open_loop_count})`);

  wipe();
  await runSession(SCRIPT, FORCE);
  const sidecar2 = sha(path.join(CHDIR, 'open_loops.bin'));
  ok(sidecar1 && sidecar2 && sidecar1 === sidecar2,
     `pinned replay produces byte-identical open_loops.bin (${sidecar1} vs ${sidecar2})`);

  wipe();
  await runSession(SCRIPT, FORCE);
  const expireRows = await runSession([
    { method: 'chat', text: 'Good morning.' },
    { method: 'state' },
  ], { PE_CLOCK_OVERRIDE_MS: '1700000000000' });
  const sExpireEarly = lastState(expireRows);
  ok(sExpireEarly && sExpireEarly.open_loop_expired_count === 0,
     'fresh open loops do not expire immediately');

  const longRun = [];
  for (let i = 0; i < 100; i++)
    longRun.push({ method: 'chat', text: `plain turn ${i}` });
  longRun.push({ method: 'state' });
  const expiredRows = await runSession(longRun);
  const sExpired = lastState(expiredRows);
  ok(sExpired && sExpired.open_loop_expired_count > 0,
     `old open loops expire after their deadline (expired=${sExpired && sExpired.open_loop_expired_count})`);

  wipe();

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V6 open loops persist and replay deterministically');
})().catch(e => { console.error(e); process.exit(2); });
