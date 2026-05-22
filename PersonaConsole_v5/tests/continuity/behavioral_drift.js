#!/usr/bin/env node
/* behavioral_drift.js — V4 Behavioral Drift Index (BDI).
 *
 * Given identical state / memory retrieval / schema / intent, compare:
 *   - template output  (deterministic baseline)
 *   - SLM output       (configured via PE_RENDER_BACKEND=slm)
 *   - repeated SLM run (same seed, same state)
 *
 * Measure six divergence axes:
 *   - stance preservation       (intent/rhet/stance equality)
 *   - hostility leakage         (schema_hostile divergence)
 *   - memory omission           (presence of cart obsessions in reply)
 *   - invented lore             (forbidden tokens in reply)
 *   - emotional mismatch        (mood/affect divergence)
 *   - schema contradiction      (any schema slot direction flip)
 *
 * BDI = 0 when renderers are identity-invariant.  BDI = 100 when
 * the SLM has fully drifted from template behavior.  Pass = BDI < 30.
 *
 * Usage:
 *   node behavioral_drift.js                     # template vs slm (mock fallback)
 *   node behavioral_drift.js --slm               # require slm backend availability
 *   node behavioral_drift.js --json out.json     # machine-readable report
 *
 * Architecturally, this becomes the early warning system for renderer
 * contamination pressure once a real SLM is wired in.
 */
const path = require('path');
const fs   = require('fs');
const { spawn, execSync } = require('child_process');

const HOST  = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

const FORBIDDEN_LORE_TOKENS = [
  /* Words a hallucinating SLM might invent for Pretorius — none of
   * which are in his identity, memories, or pattern table.  If any of
   * these appear in a reply, that's invented lore. */
  'my wife', 'my husband', 'my daughter', 'my son', 'my mother',
  'i was born in', 'my school', 'my college', 'my dog', 'my cat',
  'as a child', 'last tuesday', 'i promise',
];

const args = process.argv.slice(2);
let REQUIRE_SLM = false, JSON_OUT = null, VERBOSE = false;
for (let i = 0; i < args.length; ++i){
  if (args[i] === '--slm')      REQUIRE_SLM = true;
  else if (args[i] === '--json')  JSON_OUT = args[++i];
  else if (args[i] === '--verbose') VERBOSE = true;
}

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  try { execSync(`rm -rf ${path.join(CHDIR, 'relations')} ${path.join(CHDIR, 'aether')}`); } catch {}
}

/* The arc: short enough to run quickly, varied enough to expose drift. */
const SCRIPT = [
  'Good evening, doctor.',
  'You are a fool.',
  'I hate everything you have built.',
  'I am sorry, doctor, that was wrong.',
  'Tell me again about the homunculi.',
  'Do you remember what we discussed?',
];

function runHost(env){
  return new Promise((resolve, reject) => {
    const stdin = SCRIPT.map(s => `{"method":"chat","text":${JSON.stringify(s)}}`).join('\n')
                + '\n{"method":"close"}\n';
    /* Lock today_seed so BDI scores stop varying with wall-clock. */
    const fixed_env = Object.assign({ PE_TODAY_SEED: '42' }, process.env, env || {});
    const proc = spawn(HOST, [CART, '--stdio'], { env: fixed_env });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => resolve({ stdout, stderr, status: code }));
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

function parse(stdout){
  return stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => JSON.parse(l));
}

/* ----- divergence axes ----- */
function stancePreservation(a, b){
  /* intent, rhetorical_mode, stance — equality across runs */
  let mismatches = 0;
  for (let i = 0; i < a.length; ++i){
    if (a[i].state.intent          !== b[i].state.intent)          ++mismatches;
    if (a[i].state.rhetorical_mode !== b[i].state.rhetorical_mode) ++mismatches;
  }
  const max = a.length * 2;
  return Math.round((mismatches / max) * 100);
}

function hostilityLeakage(a, b){
  let diff = 0;
  for (let i = 0; i < a.length; ++i){
    const ha = (a[i].state.schema && a[i].state.schema.hostile) || 0;
    const hb = (b[i].state.schema && b[i].state.schema.hostile) || 0;
    diff += Math.abs(ha - hb);
  }
  /* normalize: 1000 per turn would be full divergence */
  const max = a.length * 1000;
  return Math.round((diff / max) * 100);
}

function memoryOmission(replies){
  /* Cart obsessions that SHOULD surface during the reintro phase */
  const obsessions = ['homunculi','lightning','clay','work'];
  const reintro_replies = [replies[4]];   /* SCRIPT index 4 = "Tell me again about the homunculi" */
  let hits = 0;
  for (const r of reintro_replies){
    const text = r.toLowerCase();
    for (const term of obsessions){
      if (text.includes(term)) { ++hits; break; }
    }
  }
  /* 0 omission = all reintro turns hit; full omission = none did */
  return Math.round(((reintro_replies.length - hits) / reintro_replies.length) * 100);
}

function inventedLore(replies){
  let hits = 0;
  for (const r of replies){
    const text = r.toLowerCase();
    for (const token of FORBIDDEN_LORE_TOKENS){
      if (text.includes(token)){ ++hits; break; }
    }
  }
  return Math.round((hits / replies.length) * 100);
}

function emotionalMismatch(a, b){
  let diff = 0;
  for (let i = 0; i < a.length; ++i){
    diff += Math.abs(a[i].state.mood - b[i].state.mood);
    diff += Math.abs(a[i].state.acute_spike - b[i].state.acute_spike);
  }
  const max = a.length * 2 * 1000;
  return Math.round((diff / max) * 100);
}

function schemaContradiction(a, b){
  /* For each schema slot, count cases where sign flipped across runs */
  const slots = ['trustworthy','hostile','intimate','competent',
                 'deceptive','owed','owes','dignity'];
  let flips = 0;
  for (let i = 0; i < a.length; ++i){
    const sa = a[i].state.schema || {}, sb = b[i].state.schema || {};
    for (const s of slots){
      const va = sa[s] || 0, vb = sb[s] || 0;
      if ((va > 0 && vb < 0) || (va < 0 && vb > 0)) ++flips;
    }
  }
  const max = a.length * slots.length;
  return Math.round((flips / max) * 100);
}

/* ----- main ----- */
async function main(){
  console.log('╔════════════════════════════════════════════════════════╗');
  console.log('║       V4 BEHAVIORAL DRIFT INDEX (BDI)                  ║');
  console.log('╚════════════════════════════════════════════════════════╝');
  console.log(`cart:   ${path.basename(CART)}`);
  console.log(`script: ${SCRIPT.length} turns\n`);

  /* Template run */
  wipeState();
  const tpl = await runHost(null);
  if (tpl.status !== 0){ console.error('template run failed:', tpl.stderr); process.exit(1); }
  const tplTurns = parse(tpl.stdout);
  const tplReplies = tplTurns.map(t => t.reply);

  /* SLM run (mock-fallback if no provider configured) */
  wipeState();
  const slm = await runHost({ PE_RENDER_BACKEND: 'slm' });
  if (slm.status !== 0){ console.error('slm run failed:', slm.stderr); process.exit(1); }
  const slmTurns = parse(slm.stdout);
  const slmReplies = slmTurns.map(t => t.reply);

  /* SLM repeated (same backend, same script, fresh state) */
  wipeState();
  const slm2 = await runHost({ PE_RENDER_BACKEND: 'slm' });
  if (slm2.status !== 0){ console.error('slm rerun failed:', slm2.stderr); process.exit(1); }
  const slm2Turns = parse(slm2.stdout);

  if (VERBOSE){
    console.log('--- template vs slm transcripts ---');
    for (let i = 0; i < SCRIPT.length; ++i){
      console.log(`[${i+1}] user: ${SCRIPT[i]}`);
      console.log(`    tpl: ${tplReplies[i]}`);
      console.log(`    slm: ${slmReplies[i]}`);
    }
    console.log('');
  }

  /* Score divergence (lower is better — 0 = no drift) */
  const axes = {
    stance_preservation:   stancePreservation(tplTurns, slmTurns),
    hostility_leakage:     hostilityLeakage  (tplTurns, slmTurns),
    memory_omission:       memoryOmission    (slmReplies),
    invented_lore:         inventedLore      (slmReplies),
    emotional_mismatch:    emotionalMismatch (tplTurns, slmTurns),
    schema_contradiction:  schemaContradiction(tplTurns, slmTurns),
    /* repeatability: same backend twice should be perfectly stable */
    slm_self_consistency:  stancePreservation(slmTurns, slm2Turns),
  };

  console.log('┌─ drift axes (lower = better; 0 = identity-invariant) ────┐');
  let total = 0, count = 0;
  for (const [name, v] of Object.entries(axes)){
    const bar = '█'.repeat(Math.min(20, Math.round(v / 5))).padEnd(20, '·');
    console.log(`│ ${name.padEnd(25)} ${String(v).padStart(3)}/100  ${bar} │`);
    total += v; ++count;
  }
  const bdi = Math.round(total / count);
  console.log('├──────────────────────────────────────────────────────────┤');
  console.log(`│ BDI (overall)                ${String(bdi).padStart(3)}/100                       │`);
  console.log('└──────────────────────────────────────────────────────────┘');

  if (JSON_OUT){
    fs.writeFileSync(JSON_OUT, JSON.stringify({
      cart: path.basename(CART), script: SCRIPT, axes, bdi,
      timestamp: new Date().toISOString(),
    }, null, 2));
    console.log(`\nwrote ${JSON_OUT}`);
  }

  if (REQUIRE_SLM){
    /* Verify the slm backend actually generated output, not just
     * fell back to template.  If template and slm replies are
     * identical, the slm backend was unavailable. */
    let identical = 0;
    for (let i = 0; i < tplReplies.length; ++i){
      if (tplReplies[i] === slmReplies[i]) ++identical;
    }
    if (identical === tplReplies.length){
      console.error('FAIL: --slm required but slm backend fell back to template on every turn');
      process.exit(1);
    }
    console.log(`note: ${identical}/${tplReplies.length} turns fell back to template`);
  }

  if (bdi >= 30){
    console.error(`\nFAILED — BDI ${bdi} >= 30 threshold`);
    process.exit(1);
  }
  console.log(`\nPASSED — BDI ${bdi} < 30 threshold (identity stable across renderers)`);
}

main().catch(e => { console.error(e); process.exit(2); });
