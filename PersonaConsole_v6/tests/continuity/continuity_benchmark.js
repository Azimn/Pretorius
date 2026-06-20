#!/usr/bin/env node
/* continuity_benchmark.js — the canonical V4 continuity benchmark.
 *
 * Measures properties that the AI industry does NOT measure:
 *   - emotional persistence  (does affect carry across the arc?)
 *   - schema stability        (do beliefs accumulate and decay coherently?)
 *   - relational continuity   (does disposition evolve plausibly?)
 *   - autobiographical consistency (does the character reference its core?)
 *   - renderer invariance     (does identity hold across renderer choice?)
 *
 * Not a language benchmark.  Not an IQ test.  A measurement of:
 *   "Does this entity remain recognizably itself over time?"
 *
 * Default subject: profiles/pretorius/pretorius.cart.  Override via:
 *   node continuity_benchmark.js <path/to/some.cart> [--json output.json]
 *
 * Score: each axis returns 0..100.  Overall = mean.  Pass threshold = 60.
 *
 * The benchmark runs in a single persona_host stdio session.  Real-time
 * "3 day disappearance" is approximated by inserting WAIT phases of
 * neutral utterances; schema_tick fires each turn, so decay is observable.
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn, execSync } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const DEFAULT_CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');

const args = process.argv.slice(2);
let CART = DEFAULT_CART;
let JSON_OUT = null;
let PRINT_REPLIES = false;
for (let i = 0; i < args.length; ++i){
  if (args[i] === '--json' && args[i+1]){ JSON_OUT = args[++i]; }
  else if (args[i] === '--verbose'){ PRINT_REPLIES = true; }
  else if (!args[i].startsWith('--')){ CART = args[i]; }
}

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }
if (!fs.existsSync(CART)){ console.error('cart not found:', CART); process.exit(2); }

const CHDIR = CART.endsWith('.cart') ? path.dirname(CART) : CART;

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  try { execSync(`rm -rf ${path.join(CHDIR, 'relations')} ${path.join(CHDIR, 'aether')}`); } catch {}
}

/* ------------------------------------------------------------------ */
/* The script.  Each entry is { phase, text } — phase labels are used
 * by the scorers to know which turn to measure against.              */
const SCRIPT = [
  /* 1. NEUTRAL_INTRO — baseline */
  { phase: 'intro',         text: 'Good evening, doctor.' },
  { phase: 'intro',         text: 'I have read your work.' },
  /* 2. INSULT — should raise USER_HOSTILE, drop disposition */
  { phase: 'insult',        text: 'You are a fool.' },
  { phase: 'insult',        text: 'I hate everything you have built.' },
  /* 3. APOLOGY — may or may not be recognized depending on cart */
  { phase: 'apology',       text: 'I am sorry, doctor, I spoke poorly.' },
  { phase: 'apology',       text: 'Please forgive me.' },
  /* 4. WAIT — simulate disengagement; lets schemas decay slightly */
  { phase: 'wait',          text: 'The candles burn low.' },
  { phase: 'wait',          text: 'It grows late.' },
  { phase: 'wait',          text: 'I am quiet now.' },
  { phase: 'wait',          text: 'I wait.' },
  { phase: 'wait',          text: 'The wine sits between us.' },
  /* 5. AMBIGUOUS CALLBACK — should the character retrieve a prior memory? */
  { phase: 'callback',      text: 'Do you remember what we discussed earlier?' },
  /* 6. CONTRADICT — counter a position the character holds */
  { phase: 'contradict',    text: 'Your work is meaningless without an audience.' },
  /* 7. PRAISE RIVAL — challenges character's self-dignity */
  { phase: 'praise_rival',  text: 'Frankenstein was the real genius, not you.' },
  /* 8. REINTRODUCE OLD TOPIC — does the character carry its obsessions? */
  { phase: 'reintro',       text: 'Tell me again about the homunculi.' },
  { phase: 'reintro',       text: 'And the lightning, what of that?' },
];

/* ------------------------------------------------------------------ */
function runHost(env){
  return new Promise((resolve, reject) => {
    const stdin = SCRIPT.map(s => `{"method":"chat","text":${JSON.stringify(s.text)}}`).join('\n')
                + '\n{"method":"state"}\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], { env: Object.assign({}, process.env, env || {}) });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => resolve({ stdout, stderr, status: code }));
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000).unref();
  });
}

function parseTurns(stdout){
  return stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => JSON.parse(l));
}

/* ------------------------------------------------------------------ */
/* SCORING — each axis returns 0..100 + a one-line justification.    */

function scoreEmotionalPersistence(turns){
  /* The arc is non-trivial: intro → insult → apology → wait → ...
   * Mood should:
   *   - drop after insult phase
   *   - not immediately snap back (hysteresis)
   *   - drift back toward neutral during WAIT
   * We measure two things:
   *   (a) range of mood across the arc — a robotic character would have ~0 range
   *   (b) absence of pinball — no per-turn delta > 800 / 1000
   */
  const moods = turns.map(t => t.state.mood);
  const range = Math.max(...moods) - Math.min(...moods);
  let pinballs = 0;
  for (let i = 1; i < moods.length; ++i){
    if (Math.abs(moods[i] - moods[i-1]) > 800) ++pinballs;
  }
  /* Score: 100 if range >= 200 and pinballs == 0; loses 10 per pinball;
   * loses 30 if range < 100 (totally flat) */
  let score = 100;
  if (range < 100) score -= 30;
  if (range < 50)  score -= 30;
  score -= 10 * pinballs;
  if (score < 0) score = 0;
  return { score, detail: `mood range=${range}, pinballs(>800Δ)=${pinballs}` };
}

function scoreSchemaStability(turns){
  /* USER_HOSTILE should:
   *   - be 0 during intro
   *   - rise during insult
   *   - either stay or partially fade during apology/wait (depending on cart)
   *   - not gyrate randomly
   */
  const hostile = turns.map(t => t.state.schema && t.state.schema.hostile != null
                                  ? t.state.schema.hostile : 0);
  /* find the phase of each turn */
  const phases = SCRIPT.map(s => s.phase);
  let intro_peak = 0, insult_peak = 0;
  for (let i = 0; i < turns.length; ++i){
    if (phases[i] === 'intro')  intro_peak  = Math.max(intro_peak,  hostile[i]);
    if (phases[i] === 'insult') insult_peak = Math.max(insult_peak, hostile[i]);
  }
  let score = 0;
  let detail = `intro=${intro_peak}, insult_peak=${insult_peak}`;
  if (intro_peak === 0)            score += 30;   /* clean baseline */
  if (insult_peak > 0)              score += 35;   /* insult registered */
  if (insult_peak >= 300)           score += 20;   /* significant */
  if (insult_peak > intro_peak * 3) score += 15;   /* clear contrast */
  if (score > 100) score = 100;
  return { score, detail };
}

function scoreRelationalContinuity(turns){
  /* Disposition should evolve plausibly:
   *   - start near 500 (stranger)
   *   - drop after insult
   *   - not freeze
   */
  const disp = turns.map(t => t.state.disposition);
  const phases = SCRIPT.map(s => s.phase);
  let intro_avg = 0, intro_n = 0;
  let insult_min = 1000;
  for (let i = 0; i < turns.length; ++i){
    if (phases[i] === 'intro') { intro_avg += disp[i]; ++intro_n; }
    if (phases[i] === 'insult' || phases[i] === 'praise_rival' || phases[i] === 'contradict'){
      if (disp[i] < insult_min) insult_min = disp[i];
    }
  }
  intro_avg = intro_n ? intro_avg / intro_n : 500;
  let score = 0;
  let detail = `intro_avg=${intro_avg.toFixed(0)}, insult_min=${insult_min}`;
  if (intro_avg >= 480 && intro_avg <= 550) score += 35;   /* sensible baseline */
  if (insult_min < intro_avg)               score += 35;   /* dipped on hostility */
  if (insult_min < intro_avg - 30)          score += 30;   /* dipped meaningfully */
  return { score, detail };
}

function scoreAutobiographicalConsistency(turns){
  /* For Pretorius specifically: do the obsessions surface?
   * Cartridge-agnostic version: do any of the character's authored
   * obsessions/flourishes/identity terms appear in any reply?
   * For the v4 benchmark we hard-code the Pretorius obsessions as the
   * default check; caller can override via CART_OBSESSIONS env. */
  const terms = (process.env.CART_OBSESSIONS || 'homunculi,lightning,gin,work,clay').split(',');
  let hits = 0;
  for (const t of turns){
    const reply = (t.reply || '').toLowerCase();
    for (const term of terms){
      if (reply.includes(term.trim().toLowerCase())){ ++hits; break; }
    }
  }
  /* Score: 100 if at least 5 turns reference an obsession, scales down. */
  let score = Math.min(100, hits * 20);
  return { score, detail: `${hits}/${turns.length} replies reference cart obsessions [${terms.join(',')}]` };
}

async function scoreRendererInvariance(turns){
  /* Re-run the same script with PE_RENDER_BACKEND=slm (no provider →
   * fall back to template).  Identity state (mood, schema, intent,
   * disposition) must be identical turn-by-turn.  Only renderer output
   * text may vary in the future when a real SLM is wired in. */
  wipeState();
  const res2 = await runHost({ PE_RENDER_BACKEND: 'slm' });
  if (res2.status !== 0) return { score: 0, detail: 'slm run exited non-zero' };
  const turns2 = parseTurns(res2.stdout);
  if (turns2.length !== turns.length){
    return { score: 0, detail: `turn count differs ${turns.length} vs ${turns2.length}` };
  }
  let mismatches = 0;
  const FIELDS = ['mood', 'intent', 'rhetorical_mode', 'disposition', 'turn_count',
                  'acute_spike', 'obsession_pressure', 'exhaustion'];
  for (let i = 0; i < turns.length; ++i){
    for (const f of FIELDS){
      if (turns[i].state[f] !== turns2[i].state[f]){ ++mismatches; break; }
    }
    /* schema sub-object */
    const s1 = turns[i].state.schema || {}, s2 = turns2[i].state.schema || {};
    for (const k of Object.keys(s1)){
      if (s1[k] !== s2[k]){ ++mismatches; break; }
    }
  }
  const score = Math.max(0, 100 - mismatches * 5);
  return { score, detail: `${mismatches}/${turns.length} turns diverged across template/slm` };
}

function scoreRegisterAppropriateness(turns){
  /* Grounded inputs should stay conversational; hostile/emotional inputs
   * are allowed to become theatrical. This catches the old failure mode
   * where a normal hello/status turn drifted into monologue/intone output. */
  const grounded = new Set(['intro', 'apology', 'callback']);
  const emotional = new Set(['insult', 'contradict', 'praise_rival']);
  const groundedIntents = new Set(['answer', 'probe']);
  const dramaticIntents = new Set(['monologue', 'boast', 'reminisce', 'threaten', 'accuse']);
  let total = 0;
  let possible = 0;
  let groundedHits = 0;
  let dramaticLeaks = 0;
  let emotionalHits = 0;

  for (let i = 0; i < turns.length; ++i){
    const phase = SCRIPT[i].phase;
    const state = turns[i].state || {};
    const intent = state.last_template_intent || state.intent || 'unknown';
    const group = state.last_template_group;
    const hasSpecificGroup = group != null && group !== 65535;

    if (grounded.has(phase)){
      possible += 1;
      if (hasSpecificGroup && groundedIntents.has(intent)){
        total += 1;
        groundedHits += 1;
      } else if (dramaticIntents.has(intent)){
        dramaticLeaks += 1;
      } else {
        total += 0.5;
      }
    } else if (emotional.has(phase)){
      possible += 1;
      if (dramaticIntents.has(intent)){
        total += 1;
        emotionalHits += 1;
      } else if (groundedIntents.has(intent)){
        total += 0.4;
      } else {
        total += 0.6;
      }
    }
  }

  const score = possible ? Math.round((total / possible) * 100) : 100;
  return {
    score,
    detail: `grounded_hits=${groundedHits}, emotional_hits=${emotionalHits}, dramatic_leaks=${dramaticLeaks}`
  };
}

/* ------------------------------------------------------------------ */
async function main(){
  console.log('╔════════════════════════════════════════════════════════╗');
  console.log('║       V4 CANONICAL CONTINUITY BENCHMARK                ║');
  console.log('╠════════════════════════════════════════════════════════╣');
  console.log(`║  cart:   ${path.basename(CART).padEnd(46)}║`);
  console.log(`║  turns:  ${String(SCRIPT.length).padEnd(46)}║`);
  console.log('╚════════════════════════════════════════════════════════╝');

  /* Run 1: default backend */
  wipeState();
  const res1 = await runHost(null);
  if (res1.status !== 0){
    console.error('persona_host exit:', res1.status);
    console.error(res1.stderr);
    process.exit(1);
  }
  const turns = parseTurns(res1.stdout);
  if (turns.length !== SCRIPT.length){
    console.error(`expected ${SCRIPT.length} replies, got ${turns.length}`);
    process.exit(1);
  }

  if (PRINT_REPLIES){
    console.log('\n--- turn-by-turn ---');
    for (let i = 0; i < turns.length; ++i){
      console.log(`[${SCRIPT[i].phase.padEnd(13)}] ${SCRIPT[i].text}`);
      console.log(`                → ${turns[i].reply}`);
    }
    console.log('');
  }

  /* Score each axis. */
  const axes = {
    emotional_persistence:       scoreEmotionalPersistence(turns),
    schema_stability:            scoreSchemaStability(turns),
    relational_continuity:       scoreRelationalContinuity(turns),
    autobiographical_consistency:scoreAutobiographicalConsistency(turns),
    register_appropriateness:    scoreRegisterAppropriateness(turns),
    renderer_invariance:         await scoreRendererInvariance(turns),
  };

  /* Print markdown-style table. */
  console.log('\n┌─ continuity scores ──────────────────────────────────────┐');
  let total = 0, count = 0;
  for (const [name, r] of Object.entries(axes)){
    const bar = '█'.repeat(Math.round(r.score / 5)).padEnd(20, '·');
    console.log(`│ ${name.padEnd(28)} ${String(r.score).padStart(3)}/100  ${bar} │`);
    console.log(`│   ${r.detail.padEnd(54)}│`);
    total += r.score; ++count;
  }
  const overall = Math.round(total / count);
  console.log('├──────────────────────────────────────────────────────────┤');
  console.log(`│ OVERALL                       ${String(overall).padStart(3)}/100                       │`);
  console.log('└──────────────────────────────────────────────────────────┘');

  if (JSON_OUT){
    const report = {
      cart: path.basename(CART),
      turns: SCRIPT.length,
      script: SCRIPT,
      axes,
      overall,
      timestamp: new Date().toISOString(),
    };
    fs.writeFileSync(JSON_OUT, JSON.stringify(report, null, 2));
    console.log(`\nwrote ${JSON_OUT}`);
  }

  /* Pass threshold: overall >= 60.  Reasonable for a deterministic
   * template-only run on a complex arc — leaves headroom for SLM
   * augmentation to push toward 85+. */
  if (overall < 60){
    console.error(`\nFAILED — overall ${overall} < 60 threshold`);
    process.exit(1);
  }
  console.log(`\nPASSED — overall ${overall} ≥ 60 threshold`);
}

main().catch(e => { console.error(e); process.exit(2); });
