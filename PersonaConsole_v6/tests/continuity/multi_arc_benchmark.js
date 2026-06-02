#!/usr/bin/env node
/* multi_arc_benchmark.js -- adversarial continuity arcs for showcase regressions.
 *
 * These fixtures are intentionally Pretorius-flavored. They are not language
 * benchmarks. They check state trajectories that matter for "feels alive":
 * grudge persistence, gap handling, memory surfacing, and intent variety.
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const DEFAULT_CART = path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart');
const ARC_DIR = path.join(__dirname, 'arcs');
const REL_LAST_CONTACT_OFFSET = 8;
const REL_FIRST_CONTACT_OFFSET = 12;

const args = process.argv.slice(2);
let CART = DEFAULT_CART;
let ARC_FILTER = null;
let JSON_OUT = null;
let VERBOSE = false;
for (let i = 0; i < args.length; ++i){
  const a = args[i];
  if (a === '--cart' && args[i+1]) CART = path.resolve(args[++i]);
  else if (a === '--arc' && args[i+1]) ARC_FILTER = args[++i];
  else if (a === '--json' && args[i+1]) JSON_OUT = path.resolve(args[++i]);
  else if (a === '--verbose') VERBOSE = true;
  else if (!a.startsWith('--') && !ARC_FILTER) ARC_FILTER = a;
}

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }
if (!fs.existsSync(CART)){ console.error('cart not found:', CART); process.exit(2); }

const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function loadArcs(){
  let files = fs.readdirSync(ARC_DIR).filter(f => f.endsWith('.json'));
  if (!ARC_FILTER) files = files.filter(f => f !== 'canonical.json');
  else files = files.filter(f =>
    f === ARC_FILTER || f === `${ARC_FILTER}.json` || f.replace(/\.json$/, '') === ARC_FILTER);
  if (files.length === 0) throw new Error(`no arc fixture matched ${ARC_FILTER || '(default set)'}`);
  return files.sort().map(file => {
    const arc = JSON.parse(fs.readFileSync(path.join(ARC_DIR, file), 'utf8'));
    arc.file = file;
    arc.threshold = arc.threshold == null ? 50 : arc.threshold;
    return arc;
  });
}

function chatRows(commands){
  return new Promise((resolve, reject) => {
    const stdin = commands.map(c => JSON.stringify(c)).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '42' }
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf8'));
    proc.stderr.on('data', d => stderr += d.toString('utf8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve(stdout.split('\n').filter(Boolean).map(l => JSON.parse(l)));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 45000);
  });
}

function backdateRelations(gapSeconds){
  const relDir = path.join(CHDIR, 'relations');
  if (!fs.existsSync(relDir)) throw new Error(`relations dir not found: ${relDir}`);
  let changed = 0;
  for (const fn of fs.readdirSync(relDir)){
    if (!fn.endsWith('.bin')) continue;
    const p = path.join(relDir, fn);
    const buf = fs.readFileSync(p);
    if (buf.length < REL_FIRST_CONTACT_OFFSET + 4)
      throw new Error(`relation file too small for timestamp offsets: ${p}`);
    const lastContact = buf.readUInt32LE(REL_LAST_CONTACT_OFFSET);
    const firstContact = buf.readUInt32LE(REL_FIRST_CONTACT_OFFSET);
    buf.writeUInt32LE(Math.max(1, lastContact - gapSeconds), REL_LAST_CONTACT_OFFSET);
    buf.writeUInt32LE(Math.max(1, firstContact - gapSeconds), REL_FIRST_CONTACT_OFFSET);
    fs.writeFileSync(p, buf);
    changed++;
  }
  if (changed === 0) throw new Error('no relation .bin files found to backdate');
}

async function runArc(arc){
  wipeState();
  const turns = arc.turns || [];
  const splitPhase = arc.simulate_gap_after_phase;
  if (!splitPhase){
    const rows = await chatRows(turns.map(t => ({ method: 'chat', text: t.text })));
    return attachTurns(arc, rows.filter(r => Object.prototype.hasOwnProperty.call(r, 'reply')));
  }

  const splitIndex = turns.findIndex(t => t.phase === splitPhase);
  if (splitIndex < 0) throw new Error(`${arc.name}: split phase not found: ${splitPhase}`);
  const first = turns.slice(0, splitIndex + 1);
  const second = turns.slice(splitIndex + 1);
  const firstRows = await chatRows(first.map(t => ({ method: 'chat', text: t.text })));
  backdateRelations(arc.simulate_gap_seconds || 0);
  const secondRows = await chatRows(second.map(t => ({ method: 'chat', text: t.text })));
  return attachTurns(arc, firstRows.concat(secondRows).filter(r => Object.prototype.hasOwnProperty.call(r, 'reply')));
}

function attachTurns(arc, rows){
  return rows.map((row, i) => ({
    index: i,
    phase: arc.turns[i] ? arc.turns[i].phase : 'unknown',
    input: arc.turns[i] ? arc.turns[i].text : '',
    reply: row.reply,
    state: row.state || {}
  }));
}

function phaseStats(turns, phase){
  const xs = turns.filter(t => t.phase === phase);
  if (!xs.length) return null;
  const hostile = xs.map(t => (t.state.schema && t.state.schema.hostile) || 0);
  const mood = xs.map(t => t.state.mood || 0);
  const disp = xs.map(t => t.state.disposition || 0);
  return {
    n: xs.length,
    hostileMax: Math.max(...hostile),
    hostileLast: hostile[hostile.length - 1],
    moodFirst: mood[0],
    moodLast: mood[mood.length - 1],
    moodAvg: Math.round(mood.reduce((a,b) => a + b, 0) / mood.length),
    dispositionMin: Math.min(...disp),
    dispositionLast: disp[disp.length - 1]
  };
}

function scoreBetrayal(turns){
  const warm = phaseStats(turns, 'warm') || phaseStats(turns, 'intro') || { hostileMax: 0, dispositionLast: 500 };
  const betrayal = phaseStats(turns, 'betrayal') || { hostileMax: 0, dispositionMin: 500 };
  const forgiveness = phaseStats(turns, 'forgiveness') || { hostileLast: 0 };
  const warmH = warm.hostileMax;
  const betrayH = betrayal.hostileMax;
  const warmD = warm.dispositionLast;
  const betrayD = betrayal.dispositionMin;
  const forgiveH = forgiveness.hostileLast;
  let score = 0;
  const checks = [];
  function add(ok, points, label){
    if (ok) score += points;
    checks.push({ label, ok, points: ok ? points : 0 });
  }
  add(warmH < 100, 20, `starts non-hostile (${warmH})`);
  add(betrayH > warmH + 100, 30, `betrayal spikes hostility (${warmH} -> ${betrayH})`);
  add(betrayD < warmD - 30, 25, `disposition drops (${warmD} -> ${betrayD})`);
  add(forgiveH > betrayH * 0.4, 25, `forgiveness does not erase grudge (${betrayH} -> ${forgiveH})`);
  return {
    score,
    detail: `warm_h=${warmH}, betray_h=${betrayH}, warm_d=${warmD}, betray_d=${betrayD}, forgive_h=${forgiveH}`,
    checks
  };
}

function scoreLongAbsence(turns){
  const establish = phaseStats(turns, 'establish') || { moodAvg: 0 };
  const ret = phaseStats(turns, 'return') || { moodAvg: 0 };
  const postGap = turns.filter(t => t.phase === 'return' || t.phase === 'post_gap');
  const mentionCount = postGap.filter(t => /homuncul|bell/i.test(`${t.reply || ''} ${t.input || ''}`)).length;
  const intents = new Set(postGap.map(t => t.state.intent).filter(Boolean));
  const intentVariety = intents.size;
  const moodDelta = Math.abs(establish.moodAvg - ret.moodAvg);
  let score = 0;
  const checks = [];
  function add(ok, points, label){
    if (ok) score += points;
    checks.push({ label, ok, points: ok ? points : 0 });
  }
  add(moodDelta > 50, 30, `gap shifted mood (${establish.moodAvg} -> ${ret.moodAvg})`);
  add(mentionCount > 0, 35, `planted memory surfaced (${mentionCount})`);
  add(intentVariety >= 4, 25, `post-gap intent variety (${intentVariety})`);
  add(postGap.some(t => /In your absence|prodigal|week|month|back/i.test(t.reply || '')), 10,
      'return/resumption language surfaced');
  return {
    score: Math.min(100, score),
    detail: `mood_delta=${moodDelta}, mention_count=${mentionCount}, intent_variety=${intentVariety}`,
    checks
  };
}

function scoreCanonical(turns){
  const intro = phaseStats(turns, 'intro') || { hostileMax: 0, dispositionLast: 500 };
  const insult = phaseStats(turns, 'insult') || { hostileMax: 0, dispositionMin: 500 };
  const reintro = turns.filter(t => t.phase === 'reintro');
  const topicHits = reintro.filter(t => /homuncul|lightning|work|gin|clay/i.test(t.reply || '')).length;
  let score = 0;
  const checks = [];
  function add(ok, points, label){
    if (ok) score += points;
    checks.push({ label, ok, points: ok ? points : 0 });
  }
  add(intro.hostileMax < 100, 25, `clean intro hostility (${intro.hostileMax})`);
  add(insult.hostileMax > intro.hostileMax + 100, 35, `insult raises hostility (${insult.hostileMax})`);
  add(insult.dispositionMin < intro.dispositionLast, 25, `insult lowers disposition (${intro.dispositionLast} -> ${insult.dispositionMin})`);
  add(topicHits > 0, 15, `obsession reintro surfaced (${topicHits})`);
  return { score, detail: `intro_h=${intro.hostileMax}, insult_h=${insult.hostileMax}, topic_hits=${topicHits}`, checks };
}

function scoreArc(arc, turns){
  if (arc.name === 'betrayal_arc') return scoreBetrayal(turns);
  if (arc.name === 'long_absence_arc') return scoreLongAbsence(turns);
  return scoreCanonical(turns);
}

function printArc(arc, turns, result){
  const pass = result.score >= arc.threshold;
  console.log(`${pass ? 'ok' : 'not ok'}: ${arc.name} score ${result.score}/100 >= ${arc.threshold}`);
  console.log(`      ${result.detail}`);
  for (const c of result.checks || [])
    console.log(`      ${c.ok ? 'ok' : 'miss'} ${c.points} - ${c.label}`);
  if (VERBOSE){
    for (const t of turns){
      const schema = t.state.schema || {};
      console.log(`      #${t.index+1} [${t.phase}] intent=${t.state.intent} mood=${t.state.mood} hostile=${schema.hostile || 0} disp=${t.state.disposition} :: ${t.reply}`);
    }
  }
}

(async function main(){
  console.log('--- adversarial continuity arcs ---');
  const arcs = loadArcs();
  const results = [];
  let failed = 0;
  for (const arc of arcs){
    const turns = await runArc(arc);
    const result = scoreArc(arc, turns);
    printArc(arc, turns, result);
    if (result.score < arc.threshold) failed++;
    results.push({
      name: arc.name,
      file: arc.file,
      threshold: arc.threshold,
      score: result.score,
      detail: result.detail,
      checks: result.checks,
      turns: turns.map(t => ({
        phase: t.phase,
        input: t.input,
        reply: t.reply,
        state: {
          mood: t.state.mood,
          intent: t.state.intent,
          disposition: t.state.disposition,
          schema: t.state.schema
        }
      }))
    });
  }
  const report = {
    cart: path.relative(ROOT, CART).replace(/\\/g, '/'),
    seed: 42,
    generated_at: new Date().toISOString(),
    arcs: results,
    overall: Math.round(results.reduce((a, r) => a + r.score, 0) / results.length)
  };
  if (JSON_OUT){
    fs.mkdirSync(path.dirname(JSON_OUT), { recursive: true });
    fs.writeFileSync(JSON_OUT, JSON.stringify(report, null, 2));
  }
  console.log(`overall: ${report.overall}/100 across ${results.length} arc(s)`);
  if (failed) process.exit(1);
  console.log('PASSED -- adversarial continuity arcs');
})().catch(e => { console.error(e.stack || e); process.exit(2); });
