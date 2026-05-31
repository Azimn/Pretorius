#!/usr/bin/env node
/* idle_probe_test.js -- verifies read-only conversational initiative. */
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }
if (!fs.existsSync(CART)){ console.error('cart not found:', CART); process.exit(2); }

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(commands){
  return new Promise((resolve, reject) => {
    const stdin = commands.map(c => JSON.stringify(c)).join('\n')
                + '\n{"method":"close"}\n';
    /* Pin PE_TODAY_SEED so the idle-probe pool selection is reproducible
     * across runs. Without this the today_seed falls back to wall-clock
     * seconds and back-to-back test runs land in the same second, drawing
     * from an unrelated probe pool. (Pinning the full engine clock as well
     * is tempting, but the engine's drive-decay path expects a small
     * positive delta between session_start and first turn — a frozen clock
     * makes delta=0 and changes downstream selection. Live wall progression
     * supplies that micro-delta naturally.) */
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '1234' }
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve(stdout.split('\n').filter(Boolean).map(l => JSON.parse(l)));
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

async function probeAfter(label, text, regex){
  wipeState();
  const rows = await run([
    { method: 'chat', text },
    { method: 'idle_probe' },
  ]);
  const probe = rows[1];
  ok(probe && typeof probe.reply === 'string' && regex.test(probe.reply),
     `${label}: idle probe is topic/state appropriate`);
  ok(probe.state.turn_count === 1,
     `${label}: idle probe does not increment turn_count`);
}

(async function main(){
  console.log('--- idle_probe test ---');

  wipeState();
  const fresh = await run([{ method: 'idle_probe' }]);
  ok(fresh[0].reply === null, 'fresh idle probe returns null before first turn');
  ok(fresh[0].state.turn_count === 0, 'fresh idle probe keeps turn_count at 0');

  await probeAfter('Henry momentum',
    'Frankenstein was the real genius, Henry understood more than you admit.',
    /henry|frankenstein/i);

  /* Regexes accept any thematically valid probe in the pool — the pool
   * legitimately includes both keyword-bearing lines ("the work", "the
   * spark") and oblique variants ("first breath", "the quiet has become
   * personal") that share the topic without using its keyword. */
  await probeAfter('work momentum',
    'Tell me about creation and your work.',
    /work|creation|method|permission|forbid|appetite|coming with me|body|spark|mind|breath/i);

  await probeAfter('loneliness momentum',
    'Do you ever feel loneliness?',
    /loneliness|solitude|silence|confession|confessing|misunderstood|quiet|withdr|wound|room|company|absence|personal/i);

  wipeState();
  const hostile = await run([
    { method: 'chat', text: 'You are a fool.' },
    { method: 'chat', text: 'I hate everything you have built.' },
    { method: 'idle_probe' },
    { method: 'idle_probe' },
  ]);
  ok(typeof hostile[2].reply === 'string'
     && /quiet|silence|conversation|fear|fatigue|wound|sharpen|objection|injury|politeness/i.test(hostile[2].reply),
     'negative mood probe uses silence/withdrawal pool');
  ok(hostile[2].state.turn_count === 2 && hostile[3].state.turn_count === 2,
     'back-to-back idle probes remain read-only');

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- idle_probe API intact');
})().catch(e => { console.error(e); process.exit(2); });
