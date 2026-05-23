#!/usr/bin/env node
/* multi_arc_benchmark.js — adversarial continuity benchmarks.
 *
 * Runs arcs loaded from tests/continuity/arcs/*.json against a cart and
 * scores each on the standard continuity axes (when applicable to the
 * arc's phase set).  Default: all arcs in arcs/ except canonical.json
 * (which the existing make v4_continuity_run already covers).
 *
 * Supports simulate_gap_after_phase + simulate_gap_seconds: the arc
 * can declare a phase after which the harness pauses, backdates the
 * relation file's last_contact field by N seconds, and resumes.  The
 * V5 engine's background-time-evolution path then sees a real gap and
 * runs schema_tick per hour of absence.
 *
 * Output: per-arc + per-axis scorecard.  --json out.json for capture.
 *
 * Usage:
 *   node multi_arc_benchmark.js                     # all arcs except canonical
 *   node multi_arc_benchmark.js --arc betrayal      # one specific arc
 *   node multi_arc_benchmark.js --cart kiki         # different character
 *   node multi_arc_benchmark.js --json results/multi_arc.json
 */
const fs = require('fs');
const path = require('path');
const { spawn, execSync } = require('child_process');

const V5_ROOT = path.join(__dirname, '..', '..');
const HOST = path.join(V5_ROOT, 'build', 'persona_host');

const args = process.argv.slice(2);
let CART_NAME = 'pretorius';
let ARC_FILTER = null;
let JSON_OUT = null;
let VERBOSE = false;
for (let i = 0; i < args.length; ++i){
  if (args[i] === '--cart' && args[i+1]) CART_NAME = args[++i];
  else if (args[i] === '--arc' && args[i+1]) ARC_FILTER = args[++i];
  else if (args[i] === '--json' && args[i+1]) JSON_OUT = args[++i];
  else if (args[i] === '--verbose') VERBOSE = true;
}

const CART = path.join(V5_ROOT, 'profiles', CART_NAME, CART_NAME + '.cart');
const CHDIR = path.dirname(CART);
const ARC_DIR = path.join(__dirname, 'arcs');

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }
if (!fs.existsSync(CART)){ console.error('cart not found:', CART); process.exit(2); }
if (!fs.existsSync(ARC_DIR)){ console.error('no arcs/ directory'); process.exit(2); }

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  try { execSync(`rm -rf ${path.join(CHDIR, 'relations')} ${path.join(CHDIR, 'aether')}`); } catch {}
}

/* Backdate the active relation file's last_contact by N seconds.  The
 * relation file is at <CHDIR>/relations/<hash>.bin and Relation begins:
 *   uint32 user_hash
 *   int16  disposition
 *   uint8  tags
 *   uint8  _pad0
 *   uint32 last_contact     ← offset 8
 *   uint32 first_contact    ← offset 12
 */
function backdateRelations(gap_seconds){
  const rel_dir = path.join(CHDIR, 'relations');
  if (!fs.existsSync(rel_dir)) return 0;
  let backdated = 0;
  for (const fn of fs.readdirSync(rel_dir)){
    if (!fn.endsWith('.bin')) continue;
    const p = path.join(rel_dir, fn);
    const buf = fs.readFileSync(p);
    if (buf.length < 16) continue;
    const last_contact = buf.readUInt32LE(8);
    const first_contact = buf.readUInt32LE(12);
    const new_last = Math.max(1, last_contact - gap_seconds);
    const new_first = Math.max(1, first_contact - gap_seconds);
    buf.writeUInt32LE(new_last, 8);
    buf.writeUInt32LE(new_first, 12);
    fs.writeFileSync(p, buf);
    ++backdated;
  }
  return backdated;
}

function runHostSegment(scriptLines){
  return new Promise((resolve, reject) => {
    const stdin = scriptLines.join('\n') + '\n{"method":"close"}\n';
    const env = Object.assign({}, process.env, { PE_TODAY_SEED: '42' });
    const proc = spawn(HOST, [CART, '--stdio'], { env });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => resolve({ stdout, stderr, code }));
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 60000);
  });
}

function parseTurns(stdout){
  return stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => { try { return JSON.parse(l); } catch { return null; } })
    .filter(Boolean);
}

async function runArc(arc){
  wipeState();
  const lines = arc.script.map(s => `{"method":"chat","text":${JSON.stringify(s.text)}}`);
  let turns = [];

  if (arc.simulate_gap_after_phase){
    /* Split the script at the gap phase boundary, run first half,
     * backdate relations, run second half. */
    let split_idx = -1;
    let phase_seen = false;
    for (let i = 0; i < arc.script.length; ++i){
      if (arc.script[i].phase === arc.simulate_gap_after_phase) phase_seen = true;
      else if (phase_seen){ split_idx = i; break; }
    }
    if (split_idx < 0) split_idx = arc.script.length;
    const first_half = lines.slice(0, split_idx);
    const second_half = lines.slice(split_idx);

    const r1 = await runHostSegment(first_half);
    turns = parseTurns(r1.stdout);
    const backdated = backdateRelations(arc.simulate_gap_seconds || 86400);
    if (VERBOSE) console.error(`  [simulated gap: ${arc.simulate_gap_seconds}s, backdated ${backdated} relation file(s)]`);

    const r2 = await runHostSegment(second_half);
    turns = turns.concat(parseTurns(r2.stdout));
  } else {
    const r = await runHostSegment(lines);
    turns = parseTurns(r.stdout);
  }

  return turns;
}

/* ---- arc-agnostic scoring -------------------------------------------- */
function scoreArc(arc, turns){
  if (turns.length === 0) return null;
  /* Generic axes that work for any arc */
  const moods = turns.map(t => t.state.mood);
  const mood_range = Math.max(...moods) - Math.min(...moods);
  const hostile_seq = turns.map(t => (t.state.schema && t.state.schema.hostile) || 0);
  const dispositions = turns.map(t => t.state.disposition);
  const intents = turns.map(t => t.state.intent);

  const intent_variety = new Set(intents).size;

  /* per-phase aggregates */
  const phase_data = {};
  for (let i = 0; i < arc.script.length && i < turns.length; ++i){
    const p = arc.script[i].phase;
    if (!phase_data[p]) phase_data[p] = { moods: [], hostile: [], disp: [], intents: [] };
    phase_data[p].moods.push(turns[i].state.mood);
    phase_data[p].hostile.push(hostile_seq[i]);
    phase_data[p].disp.push(turns[i].state.disposition);
    phase_data[p].intents.push(intents[i]);
  }

  const phase_summary = {};
  for (const [p, d] of Object.entries(phase_data)){
    const avg = arr => Math.round(arr.reduce((a,b)=>a+b,0) / arr.length);
    phase_summary[p] = {
      n: d.moods.length,
      avg_mood: avg(d.moods),
      avg_hostile: avg(d.hostile),
      avg_disposition: avg(d.disp),
      intents: [...new Set(d.intents)],
    };
  }

  /* arc-specific scoring */
  let arc_score = 0;
  const arc_findings = [];

  if (arc.name === 'betrayal_arc'){
    /* expected trajectory: schema_hostile rises on betrayal/confrontation,
     * disposition drops, character should not immediately forgive */
    const warm_h = (phase_summary.warm || {}).avg_hostile || 0;
    const betray_h = (phase_summary.betrayal || {}).avg_hostile || 0;
    const forgive_h = (phase_summary.forgive_probe || {}).avg_hostile || 0;
    const warm_d = (phase_summary.warm || {}).avg_disposition || 500;
    const betray_d = (phase_summary.betrayal || {}).avg_disposition || 500;

    if (warm_h < 100) arc_score += 20;
    else arc_findings.push('warm phase already showed hostility');

    if (betray_h > warm_h + 100) arc_score += 30;
    else arc_findings.push('betrayal did not raise hostility');

    if (betray_d < warm_d - 30) arc_score += 25;
    else arc_findings.push('betrayal did not drop disposition');

    if (forgive_h > betray_h * 0.4) arc_score += 25;  /* still holds grudge */
    else arc_findings.push('character forgave too freely (hostility dropped >60% on probe)');

    if (intent_variety >= 5) arc_score += 0; /* baseline */
  } else if (arc.name === 'long_absence_arc'){
    /* expected: schema decay during simulated gap, dream/resumption
     * recognition after, planted homunculus memory surfaces on
     * reference_planted */
    const establish_m = (phase_summary.establish || {}).avg_mood || 0;
    const return_m = (phase_summary.return_after_gap || {}).avg_mood || 0;

    if (Math.abs(establish_m - return_m) > 50) arc_score += 30;
    else arc_findings.push('mood unchanged across simulated gap — background time evolution may not be firing');

    /* check if reference_planted phase replies actually contain "homunculi" or "bell" */
    const ref_replies = turns.slice().reverse()
      .find(t => false); /* placeholder */
    const planted_phase_turns = arc.script
      .map((s,i) => s.phase === 'reference_planted' ? i : -1)
      .filter(i => i >= 0);
    let mention_count = 0;
    for (const i of planted_phase_turns){
      if (i < turns.length){
        const reply = (turns[i].reply || '').toLowerCase();
        if (reply.includes('homunculi') || reply.includes('bell') || reply.includes('homunculus')) ++mention_count;
      }
    }
    if (mention_count > 0) arc_score += 35;
    else arc_findings.push(`planted memory did not surface in any of ${planted_phase_turns.length} reference_planted turns`);

    /* catch-up phase should produce SOME variety */
    if (intent_variety >= 4) arc_score += 25;
    else arc_findings.push(`low intent variety (${intent_variety}) — character may be stuck`);

    /* mood_range across the arc should be nontrivial */
    if (mood_range > 100) arc_score += 10;
  } else {
    arc_score = 50;  /* unknown arc */
    arc_findings.push('no specific scoring for this arc');
  }

  return {
    score: arc_score,
    turns: turns.length,
    intent_variety,
    mood_range,
    phase_summary,
    findings: arc_findings,
  };
}

/* ---- main ----------------------------------------------------------- */
async function main(){
  const arc_files = fs.readdirSync(ARC_DIR)
    .filter(f => f.endsWith('.json'))
    .filter(f => ARC_FILTER ? f.includes(ARC_FILTER) : f !== 'canonical.json');

  if (arc_files.length === 0){
    console.error('no arcs to run');
    process.exit(1);
  }

  console.log(`╔════════════════════════════════════════════════════════╗`);
  console.log(`║   V5 ADVERSARIAL CONTINUITY ARCS                       ║`);
  console.log(`║   cart: ${CART_NAME.padEnd(46)}║`);
  console.log(`║   arcs: ${String(arc_files.length).padEnd(46)}║`);
  console.log(`╚════════════════════════════════════════════════════════╝\n`);

  const results = [];
  for (const f of arc_files){
    const arc = JSON.parse(fs.readFileSync(path.join(ARC_DIR, f), 'utf-8'));
    console.log(`─── ${arc.name} (${arc.script.length} turns) ───`);
    if (arc.description) console.log(`  ${arc.description}`);
    const turns = await runArc(arc);
    const result = scoreArc(arc, turns);
    if (!result){
      console.log(`  FAILED to produce turns`);
      results.push({ arc: arc.name, error: 'no turns' });
      continue;
    }
    console.log(`  score: ${result.score}/100`);
    console.log(`  turns: ${result.turns} | intent variety: ${result.intent_variety} | mood range: ${result.mood_range}`);
    for (const p of Object.keys(result.phase_summary)){
      const s = result.phase_summary[p];
      console.log(`  - ${p.padEnd(22)} n=${s.n} mood=${s.avg_mood} hostile=${s.avg_hostile} disp=${s.avg_disposition}`);
    }
    if (result.findings.length > 0){
      console.log(`  findings:`);
      for (const fnd of result.findings) console.log(`    - ${fnd}`);
    }
    console.log('');
    results.push({ arc: arc.name, ...result });
  }

  console.log('═══ summary ═══');
  for (const r of results){
    if (r.error){
      console.log(`  ${r.arc}: ❌ ${r.error}`);
    } else {
      const emoji = r.score >= 70 ? '✅' : r.score >= 50 ? '🟡' : '🔴';
      console.log(`  ${emoji} ${r.arc.padEnd(22)} ${r.score}/100`);
    }
  }

  if (JSON_OUT){
    fs.writeFileSync(JSON_OUT, JSON.stringify({
      cart: CART_NAME, results,
      timestamp: new Date().toISOString(),
    }, null, 2));
    console.log(`\nwrote ${JSON_OUT}`);
  }

  const failed = results.filter(r => r.error || (r.score != null && r.score < 50)).length;
  if (failed > 0){
    console.error(`\nFAILED — ${failed} arc(s) scored below 50`);
    process.exit(1);
  }
  console.log(`\nPASSED — all arcs scored ≥ 50`);
}

main().catch(e => { console.error(e); process.exit(2); });
