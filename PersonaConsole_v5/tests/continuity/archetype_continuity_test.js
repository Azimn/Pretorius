#!/usr/bin/env node
/* archetype_continuity_test.js — run the full 16-turn continuity
 * benchmark against every Forge archetype.  Produces a per-archetype
 * scorecard revealing which cartridges have authoring gaps.
 *
 * For each of the 12 archetypes:
 *   - extract its seed from CartridgeForge/forge.html
 *   - build a .cart via the Forge's in-browser-equivalent JS path
 *   - run continuity_benchmark.js with that cart + the archetype's
 *     obsessions injected via CART_OBSESSIONS env var
 *   - capture the per-axis scores
 *
 * Prints a markdown table.  Optional --json out.json for machine-
 * readable capture into a cartridge-authoring dashboard.
 *
 * Pass criterion: every archetype scores ≥60 overall (continuity
 * benchmark's standard pass threshold).
 */
const fs = require('fs');
const path = require('path');
const { spawnSync, spawn, execSync } = require('child_process');

const args = process.argv.slice(2);
let JSON_OUT = null;
for (let i = 0; i < args.length; ++i){
  if (args[i] === '--json' && args[i+1]) JSON_OUT = args[++i];
}

const V5_ROOT = path.join(__dirname, '..', '..');
const REPO_ROOT = path.join(V5_ROOT, '..');
const FORGE = path.join(REPO_ROOT, 'CartridgeForge', 'forge.html');
const HOST = path.join(V5_ROOT, 'build', 'persona_host');
const BENCHMARK = path.join(__dirname, 'continuity_benchmark.js');
const TMP_DIR = '/tmp/archetype_continuity';

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }
if (!fs.existsSync(FORGE)){ console.error('forge.html not found'); process.exit(2); }

/* ------- extract Forge JS the same way forge_archetypes_test does ------- */
const html = fs.readFileSync(FORGE, 'utf-8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
let js = m[1];

const STUB = new Proxy({}, {
  get(t, p){
    if (p === 'addEventListener') return () => {};
    if (p === 'classList')        return { add(){}, remove(){}, toggle(){} };
    if (p === 'appendChild')      return () => {};
    if (p === 'querySelectorAll') return () => [];
    if (p === 'querySelector')    return () => STUB;
    if (p === 'createElement')    return () => STUB;
    if (p === 'dataset')          return {};
    if (p === 'style')            return {};
    if (p === 'value' || p === 'textContent' || p === 'innerHTML') return '';
    if (p === 'hidden' || p === 'checked')                          return false;
    if (typeof p === 'string' && p.startsWith('on'))                return null;
    return STUB;
  },
  set(){ return true; }
});
global.document = {
  getElementById: () => STUB, querySelectorAll: () => [],
  querySelector: () => STUB, createElement: () => STUB,
};
global.window = { scrollTo(){}, addEventListener(){} };
global.FileReader = class { readAsText(){} };
global.Blob = class { constructor(b){ this.b = b; } };
global.URL = { createObjectURL(){ return ''; }, revokeObjectURL(){} };
global.TextEncoder = require('util').TextEncoder;
global.performance = { now: () => Date.now() };
global.confirm = () => false;
global.alert = () => {};

const exposeNames = ['ARCHETYPES','defaultCharacter','buildCart',
                     'LMBuilder','PE_LM_DEFAULT_ORDER','GENERIC_TEMPLATES'];
js += `\n;module.exports = { ${exposeNames.map(n => n + ':typeof ' + n + '!=="undefined"?' + n + ':null').join(',')} };`;
const moduleObj = { exports: {} };
new Function('module', js).call(global, moduleObj);
const { ARCHETYPES, defaultCharacter, buildCart, LMBuilder,
        PE_LM_DEFAULT_ORDER, GENERIC_TEMPLATES } = moduleObj.exports;

/* ------- per-archetype cart build (same path as forge_archetypes_test) ------- */
function buildCartForArchetype(a){
  const ch = defaultCharacter();
  ch.name = a.seed.name;
  ch.identity.O = a.seed.O; ch.identity.C = a.seed.C;
  ch.identity.E = a.seed.E; ch.identity.A = a.seed.A; ch.identity.N = a.seed.N;
  ch.identity.verbosity = a.seed.verb;
  ch.identity.voice_flags = [...a.seed.flags];
  ch.identity.address_user_as = [...a.seed.address];
  ch.identity.obsessions = [...a.seed.obsessions];
  ch.identity.taboos = [...a.seed.taboos];
  ch.identity.flourishes = [...a.seed.flourishes].concat(['','','','']).slice(0,4);
  ch.identity.expansions = [...a.seed.expansions].concat(['','','','']).slice(0,4);
  ch.identity.core_memories = a.seed.memories.map(m => ({...m}));
  if (a.seed.templates && a.seed.templates.length){
    ch.dialoguePack = {
      templates: GENERIC_TEMPLATES.concat(a.seed.templates.map(t => Object.assign({}, t))),
      patterns: null, goals: null, fallbacks: null,
    };
  }
  if (a.seed.lm_corpus){
    const lm = new LMBuilder(PE_LM_DEFAULT_ORDER);
    lm.addCorpus(a.seed.lm_corpus);
    ch.lmBytes = lm.serialize();
  }
  return buildCart(ch);
}

/* ------- run the continuity benchmark against a cart ------- */
function runContinuityAgainst(cartPath, obsessions){
  const tmp_json = path.join(TMP_DIR, 'report.json');
  try { fs.unlinkSync(tmp_json); } catch {}
  const env = Object.assign({}, process.env, {
    CART_OBSESSIONS: obsessions.join(','),
    PE_TODAY_SEED: '42',     /* deterministic */
  });
  const res = spawnSync('node', [BENCHMARK, cartPath, '--json', tmp_json], {
    env, encoding: 'utf-8', timeout: 30000,
  });
  if (res.status !== 0 && !fs.existsSync(tmp_json)){
    return null;
  }
  if (!fs.existsSync(tmp_json)) return null;
  return JSON.parse(fs.readFileSync(tmp_json, 'utf-8'));
}

/* ------- main ------- */
fs.mkdirSync(TMP_DIR, { recursive: true });
console.log(`╔════════════════════════════════════════════════════════╗`);
console.log(`║   V5 ARCHETYPE CONTINUITY SCORECARD                    ║`);
console.log(`║   ${ARCHETYPES.length} archetypes × 16-turn arc each              ║`);
console.log(`╚════════════════════════════════════════════════════════╝\n`);

const results = [];
for (const a of ARCHETYPES){
  process.stdout.write(`[${a.id.padEnd(20)}] building...  `);
  let cart;
  try { cart = buildCartForArchetype(a); }
  catch (e){
    console.log(`BUILD FAILED: ${e.message}`);
    results.push({ id: a.id, name: a.seed.name, error: 'build_failed', detail: e.message });
    continue;
  }
  const cartPath = path.join(TMP_DIR, a.id + '.cart');
  fs.writeFileSync(cartPath, Buffer.from(cart));
  process.stdout.write('benchmarking...  ');

  const report = runContinuityAgainst(cartPath, a.seed.obsessions);
  if (!report){
    console.log('BENCHMARK FAILED');
    results.push({ id: a.id, name: a.seed.name, error: 'benchmark_failed' });
    continue;
  }
  console.log(`overall ${report.overall}/100`);
  results.push({
    id: a.id, name: a.seed.name, overall: report.overall,
    axes: Object.fromEntries(Object.entries(report.axes).map(([k,v]) => [k, v.score])),
  });
}

/* ------- markdown table ------- */
console.log('\n## Per-archetype continuity scorecard\n');
const cols = ['emotional_persistence','schema_stability','relational_continuity',
              'autobiographical_consistency','register_appropriateness','renderer_invariance'];
const headers = ['Archetype', 'Overall', ...cols.map(c => c.replace(/_/g, ' '))];
console.log('| ' + headers.join(' | ') + ' |');
console.log('|---' + cols.map(()=>'|---:').join('') + '|---:|');
for (const r of results){
  if (r.error){
    console.log(`| ${r.id} | ❌ ${r.error} |${cols.map(()=>'').join('|')}|`);
  } else {
    const cells = [r.id, r.overall, ...cols.map(c => r.axes[c] != null ? r.axes[c] : '—')];
    console.log('| ' + cells.join(' | ') + ' |');
  }
}

/* ------- summary ------- */
const passed = results.filter(r => !r.error && r.overall >= 60).length;
const failed = results.filter(r => r.error || r.overall < 60).length;
console.log(`\n## Summary\n`);
console.log(`- Passing (overall ≥ 60): **${passed}/${ARCHETYPES.length}**`);
console.log(`- Failing: ${failed}`);
if (failed > 0){
  console.log('\nFailing archetypes (and their weakest axes):');
  for (const r of results){
    if (r.error){
      console.log(`  - ${r.id}: ${r.error}`);
    } else if (r.overall < 60){
      const weak = Object.entries(r.axes).sort((a,b)=>a[1]-b[1]).slice(0,2);
      console.log(`  - ${r.id} (${r.overall}/100): weakest = ${weak.map(([k,v])=>`${k}=${v}`).join(', ')}`);
    }
  }
}

if (JSON_OUT){
  fs.writeFileSync(JSON_OUT, JSON.stringify({
    archetype_count: ARCHETYPES.length, results,
    summary: { passed, failed },
    timestamp: new Date().toISOString(),
  }, null, 2));
  console.log(`\nwrote ${JSON_OUT}`);
}

if (failed > 0){
  console.error(`\nFAILED — ${failed} archetype(s) below threshold or build-broken`);
  process.exit(1);
}
console.log(`\nPASSED — all ${ARCHETYPES.length} archetypes meet continuity threshold`);
