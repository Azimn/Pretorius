#!/usr/bin/env node
/* reflection_test.js — V5 reflective memory consolidation.
 *
 * Adapted from Park et al., "Generative Agents: Interactive Simulacra of
 * Human Behavior" (UIST 2023) §3.2.  Their generative agents reflect when
 * accumulated importance crosses a threshold.  Our implementation gates
 * on PE_REFLECTION_CONSOLIDATE_EVERY=8 turns and requires a cluster of
 * at least PE_REFLECTION_CLUSTER_MIN=3 similar memories.
 *
 * What this test asserts:
 *   1.  Persistence — after enough turns on a focused topic, at least one
 *       reflection exists in the ring.
 *   2.  Abstraction — reflection text contains a {topic} substitution
 *       (e.g. "creation", "homunculi") so it reads as a pattern observation,
 *       not an event recount.
 *   3.  Determinism — same input sequence with PE_TODAY_SEED produces the
 *       same reflection count and same topic across two runs.
 *   4.  Survives reload — closing and reopening the session keeps the ring.
 */
const path = require('path');
const fs   = require('fs');
const { spawn } = require('child_process');

const HOST  = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
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

function run(commands, opts={}){
  return new Promise((resolve, reject) => {
    const env = Object.assign({}, process.env, opts.env || {});
    const stdin = commands.map(c => JSON.stringify(c)).join('\n')
                + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], { env });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve(stdout.split('\n').filter(Boolean).map(l => {
        try { return JSON.parse(l); } catch { return { raw: l }; }
      }));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

let failed = 0;
function ok(cond, msg){
  if (!cond){ console.error(`not ok: ${msg}`); failed++; }
  else console.log(`ok:   ${msg}`);
}

/* A focused dialogue about creation / homunculi — Pretorius's central
 * obsession.  Long enough that the consolidation gate fires twice. */
const FOCUSED_TURNS = [
  'Tell me about your creations.',
  'I want to hear about the homunculi.',
  'Did you really make tiny living things?',
  'Why are you obsessed with creation?',
  'What does it feel like to make life?',
  'Tell me about your laboratory.',
  'You once told me about your homunculi.',
  'How does one go about creating life?',
  'Henry never understood your work, did he?',
  'Your work on creation was misunderstood.',
  'I want to know more about the homunculi.',
  'Did the queen really speak to you?',
  'Describe the process of creating life.',
  'You said something about a king and queen.',
  'What was it like, working alone on creation?',
  'Tell me how you brought the homunculi to life.',
  'Henry abandoned your research, didn\'t he?',
  'I am curious about the creation methods you used.',
  'You speak of homunculi as if they were children.',
  'Describe the moment of first life.',
  'What did the homunculi say to you?',
  'You were a god to them, weren\'t you?',
  'Tell me about creating something from nothing.',
  'I have always wanted to ask about the homunculi.',
  'Your work on creation deserves recognition.',
];

async function driveFocused(seed){
  wipeState();
  const cmds = FOCUSED_TURNS.map(t => ({ method: 'chat', text: t }));
  cmds.push({ method: 'reflections' });
  cmds.push({ method: 'state' });
  return run(cmds, { env: { PE_TODAY_SEED: String(seed) } });
}

(async function main(){
  console.log('--- reflection test (Park et al. 2023 adaptation) ---');

  /* Run 1: drive 25 focused turns, expect at least one reflection.
   * Output order: N chats + reflections + state + close.  Pick by name. */
  const rows1 = await driveFocused(0xC0FFEE);
  const refl1  = rows1.find(r => typeof r.count === 'number' && Array.isArray(r.reflections));
  const state1 = rows1.find(r => r && r.name && typeof r.turn_count === 'number');

  ok(refl1 && typeof refl1.count === 'number',
     'reflections endpoint returns count');
  ok(refl1.count >= 1,
     `at least one reflection synthesized (got ${refl1.count})`);
  ok(state1.turn_count === FOCUSED_TURNS.length,
     `turn_count advanced to ${FOCUSED_TURNS.length}`);

  if (refl1.count >= 1){
    const r = refl1.reflections[0];
    ok(typeof r.text === 'string' && r.text.length > 0,
       'first reflection has rendered text');
    /* The substitution should produce concrete topic-name interpolation
     * in the surface text — i.e. NOT raw "{topic}" but a noun phrase. */
    ok(!/\{topic\}/.test(r.text),
       'reflection text has {topic} substituted');
    ok(r.sources >= 3,
       `cluster has at least 3 source memories (got ${r.sources})`);
    ok(r.salience >= 100,
       `reflection salience is elevated (1.3× max source; got ${r.salience})`);
    console.log(`      sample: "${r.text}"`);
  }

  /* Run 2: same seed + same input → same reflection count + same topic.
   * Determinism is the V5 invariant — behavioral holography. */
  const rows2 = await driveFocused(0xC0FFEE);
  const refl2 = rows2.find(r => typeof r.count === 'number' && Array.isArray(r.reflections));
  ok(refl2.count === refl1.count,
     `reflection count is deterministic (${refl1.count} == ${refl2.count})`);
  if (refl1.count >= 1 && refl2.count >= 1){
    ok(refl1.reflections[0].topic === refl2.reflections[0].topic,
       'reflection topic is deterministic across runs');
  }

  /* Run 3: confirm reflections persist across host restart.
   * We reopen the same CHDIR with state still on disk and read back. */
  const rows3 = await run([{ method: 'reflections' }, { method: 'state' }],
                          { env: { PE_TODAY_SEED: String(0xC0FFEE) } });
  const refl3 = rows3.find(r => typeof r.count === 'number' && Array.isArray(r.reflections));
  ok(refl3.count === refl1.count,
     `reflections persisted across host restart (${refl3.count})`);

  if (failed === 0) console.log('PASSED -- reflective consolidation intact');
  else { console.error(`FAILED -- ${failed} assertions failed`); process.exit(1); }
})().catch(e => { console.error(e); process.exit(2); });
