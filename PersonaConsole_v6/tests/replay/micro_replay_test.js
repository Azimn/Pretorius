#!/usr/bin/env node
/* micro_replay_test.js -- deterministic smoke test for PE_MICRO_MODE.
 *
 * This intentionally avoids AETHER/reflection expectations. Micro mode must
 * prove the canonical low-hardware core still runs template-only, records
 * sidecars, and replays the same state under the pinned five-tuple.
 */
const path = require('path');
const fs = require('fs');
const { spawnSync } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const hostBase = process.env.PE_MICRO_HOST
  ? path.join(ROOT, process.env.PE_MICRO_HOST)
  : path.join(ROOT, 'build', 'persona_host_micro');
const HOST = fs.existsSync(hostBase) ? hostBase : `${hostBase}.exe`;
const CART = path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipe(){
  for (const f of ['state.bin','memory.bin','chapters.bin',
                   'actor_index.bin','speech_events.bin','dissonance.bin',
                   'open_loops.bin','speech_habits.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations','aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

const SCRIPT = [
  { method: 'chat', text: 'Good morning, doctor.' },
  { method: 'chat', text: 'What are you working on?' },
  { method: 'chat', text: 'I disagree with your morality.' },
  { method: 'state' },
  { method: 'close' },
];

function run(){
  const stdin = SCRIPT.map(x => JSON.stringify(x)).join('\n') + '\n';
  const r = spawnSync(HOST, [CART, '--stdio'], {
    input: stdin,
    encoding: 'utf8',
    env: {
      ...process.env,
      PE_TODAY_SEED: '0x6060',
      PE_CLOCK_OVERRIDE_MS: '1700000000000',
      PE_RENDER_BACKEND: 'template',
    },
  });
  if (r.status !== 0)
    throw new Error(`host exited ${r.status}: ${r.stderr}`);
  return r.stdout.split('\n').filter(Boolean).map(l => JSON.parse(l));
}

function ok(cond, msg){
  if (!cond){ console.error(`not ok: ${msg}`); process.exitCode = 1; }
  else console.log(`ok:   ${msg}`);
}

console.log('--- Micro Mode replay ---');
if (!fs.existsSync(HOST)){
  console.error('micro host not built:', HOST);
  process.exit(2);
}
if (!fs.existsSync(CART)){
  console.error('cart not found:', CART);
  process.exit(2);
}

wipe();
const a = run();
const stateA = a.find(x => x && x.name && x.turn_count !== undefined);
const repliesA = a.filter(x => typeof x.reply === 'string').map(x => x.reply);

wipe();
const b = run();
const stateB = b.find(x => x && x.name && x.turn_count !== undefined);
const repliesB = b.filter(x => typeof x.reply === 'string').map(x => x.reply);

ok(repliesA.length === 3, 'micro host emitted three template replies');
ok(JSON.stringify(repliesA) === JSON.stringify(repliesB),
   'micro replies are deterministic under pinned seed and clock');
ok(stateA && stateB && JSON.stringify(stateA) === JSON.stringify(stateB),
   'micro state JSON is deterministic under replay');
ok(stateA && Number(stateA.speech_event_count || 0) >= 3,
   'speech ledger remains enabled in micro mode');
ok(stateA && stateA.relation_dims && typeof stateA.relation_dims.trust === 'number',
   'relation dimensions remain enabled in micro mode');

wipe();
if (process.exitCode) process.exit(process.exitCode);
console.log('PASSED -- Micro Mode template replay deterministic');
