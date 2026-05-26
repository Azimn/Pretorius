#!/usr/bin/env node
/* forge_cart_test.js — verifies Forge-produced .cart files load + run.
 *
 * Strategy: extract the JS from forge.html, install a minimal DOM stub,
 * call applyArchetype() to seed a character, call buildCart() to get
 * bytes, write to disk, then spawn ./build/persona_host to confirm the
 * engine loads it.
 */
const fs   = require('fs');
const path = require('path');
const { execSync, spawnSync } = require('child_process');

const FORGE = path.join(__dirname, '..', 'CartridgeForge', 'forge.html');
const HOST  = path.join(__dirname, '..', 'build', 'persona_host');
const LINT_BASE = path.join(__dirname, '..', 'build', 'cartridge_lint');
const LINT  = fs.existsSync(LINT_BASE) ? LINT_BASE : LINT_BASE + '.exe';
const OUT   = path.join(__dirname, 'tmp', 'forge_test.cart');
const QUALITY_SCRIPT = path.join(__dirname, 'tmp', 'forge_quality_script.json');
fs.mkdirSync(path.dirname(OUT), { recursive: true });

function wipeCartState(cartPath){
  const dir = path.dirname(cartPath);
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(dir, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(dir, d), { recursive: true, force: true }); } catch {}
  }
}

console.log('--- extracting JS from', FORGE);
const html = fs.readFileSync(FORGE, 'utf-8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
if (!m) { console.error('no <script> in forge.html'); process.exit(2); }
let js = m[1];

/* The forge script ends with show('landing') and assumes DOM is present.
 * Stub everything it touches so the IIFE runs without throwing. */
const stub = {
  addEventListener(){},
  classList: { toggle(){}, add(){}, remove(){} },
  setAttribute(){},
  appendChild(){},
  removeChild(){},
  querySelectorAll(){ return []; },
  querySelector(){ return this; },
  get hidden(){ return false; }, set hidden(v){},
  get textContent(){ return ''; }, set textContent(v){},
  get innerHTML(){ return ''; },  set innerHTML(v){},
  get value(){ return ''; },      set value(v){},
  get checked(){ return false; }, set checked(v){},
  get style(){ return {}; },
  get dataset(){ return {}; },
};
const STUB = new Proxy(stub, {
  get(t, p){ if (p in t) return t[p]; return STUB; },
  set(t, p, v){ return true; }
});
global.document = {
  getElementById: () => STUB,
  querySelectorAll: () => [],
  querySelector:    () => STUB,
  createElement:    () => STUB,
};
global.window = { scrollTo(){}, addEventListener(){} };
global.FileReader = class { readAsText(){} };
global.Blob = class { constructor(b){ this.b = b; } };
global.URL = { createObjectURL(){ return ''; }, revokeObjectURL(){} };
global.TextEncoder = require('util').TextEncoder;
global.confirm = () => false;
global.alert   = () => {};

/* The forge script declares functions/consts at top-level; expose them. */
const exposeNames = [
  'ARCHETYPES','defaultCharacter','applyArchetype','buildCart',
  'internalLifeForSeed','generateStarterInternalLife','preflightCharacter',
  'dialogueQualityReport','inferRelationshipPosture','relationshipPostureReport',
  'relationshipContract','livingCharacterChecklist',
  'exportObject','refreshExport','CH','LMBuilder','LMRuntime','PE_LM_DEFAULT_ORDER',
  'LM_SAMPLE_PRETORIUS','LM_SAMPLE_KIKI'
];
js += `
\n;module.exports = { ${exposeNames.map(n => n + ': typeof ' + n + '!=="undefined"?' + n + ':null').join(',')} };
`;

/* Wrap in a function so we can capture exports. */
const moduleObj = { exports: {} };
try {
  new Function('module', js).call(global, moduleObj);
} catch (e){
  console.error('JS load error:', e.message);
  process.exit(2);
}

const { ARCHETYPES, defaultCharacter, buildCart, internalLifeForSeed,
        generateStarterInternalLife,
        preflightCharacter, dialogueQualityReport,
        inferRelationshipPosture, relationshipPostureReport, relationshipContract,
        livingCharacterChecklist, refreshExport,
        LMBuilder, LMRuntime, PE_LM_DEFAULT_ORDER,
        LM_SAMPLE_PRETORIUS } = moduleObj.exports;
if (!buildCart){ console.error('buildCart not exported'); process.exit(2); }

/* Build a cart from the first archetype. */
console.log('--- building cart from archetype', ARCHETYPES[0].id);
/* applyArchetype mutates the module-scope `CH`, but we can't reach it
 * cleanly from out here.  Easier: call defaultCharacter() ourselves and
 * pass an explicit char object to buildCart. */
const ch = defaultCharacter();
ch.name = ARCHETYPES[0].seed.name;
const seed = ARCHETYPES[0].seed;
ch.identity.O = seed.O;
ch.identity.C = seed.C;
ch.identity.E = seed.E;
ch.identity.A = seed.A;
ch.identity.N = seed.N;
ch.identity.verbosity = seed.verb;
ch.identity.voice_flags = [...seed.flags];
ch.identity.address_user_as = [...seed.address];
ch.identity.obsessions = [...seed.obsessions];
ch.identity.taboos     = [...seed.taboos];
ch.identity.flourishes = [...seed.flourishes].concat(['','','','']).slice(0,4);
ch.identity.expansions = [...seed.expansions].concat(['','','','']).slice(0,4);
ch.identity.core_memories = seed.memories.map(m => ({ ...m }));
const life = internalLifeForSeed && internalLifeForSeed(seed);
if (life) Object.assign(ch.identity, life);
const preflight = preflightCharacter(ch);
console.log(`preflight: ${preflight.errors} error(s), ${preflight.warnings} warning(s)`);
if (preflight.errors || preflight.warnings){
  console.error('FAIL: Forge archetype preflight found issues:', preflight.issues);
  process.exit(1);
}
const broken = defaultCharacter();
broken.name = 'Broken Want';
broken.identity.wants = [{ name: 'want without topic', target_topic: '', target_class: 0, intensity: 100 }];
const brokenPreflight = preflightCharacter(broken);
if (!brokenPreflight.errors){
  console.error('FAIL: preflight did not catch dangling want target');
  process.exit(1);
}
console.log('preflight negative case catches dangling want target');

const staleDialogue = defaultCharacter();
staleDialogue.name = 'Stale Dialogue';
staleDialogue.dialoguePack = {
  templates: Array.from({ length: 12 }, (_, i) => ({
    group: 0xFFFF, intent: 7, base: 30,
    text: i % 2 === 0
      ? 'Ah -- observe the magnificently overdetermined architecture of my suffering.'
      : 'Ah -- observe the magnificently overdetermined architecture of my triumph.'
  })),
  patterns: null,
  goals: null,
  fallbacks: { tier1: ['...'], tier2: ['...'], tier3: ['...'] },
};
const staleReport = dialogueQualityReport(staleDialogue);
if (!staleReport.warnings){
  console.error('FAIL: dialogue quality report did not warn on stale dialogue');
  process.exit(1);
}
console.log('dialogue quality negative case catches stale/ornate dialogue');

const assistantDialogue = defaultCharacter();
assistantDialogue.name = 'Assistant Smell';
assistantDialogue.dialoguePack = {
  templates: [
    { group: 0xFFFF, intent: 0, base: 50, text: 'How can I help you today?' },
    { group: 0xFFFF, intent: 0, base: 45, text: 'As an AI, I am here to help.' },
  ],
  patterns: null,
  goals: null,
  fallbacks: { tier1: ['Tell me more.'], tier2: ['Go on.'], tier3: ['Mm.'] },
};
const assistantReport = dialogueQualityReport(assistantDialogue);
if (!assistantReport.warnings || assistantReport.assistant_smell_count < 2){
  console.error('FAIL: dialogue quality report did not catch assistant-coded language', assistantReport);
  process.exit(1);
}
console.log('dialogue quality catches assistant-coded language');

const dominantPosture = defaultCharacter();
dominantPosture.name = 'Septimus Halloway';
dominantPosture.identity.A = 20;
dominantPosture.identity.E = 80;
dominantPosture.identity.voice_flags = ['SARDONIC','NO_DIRECT_AFFIRM','ALLOW_CONTRADICT'];
dominantPosture.identity.obsessions = ['the work','homunculi','god'];
dominantPosture.dialoguePack = {
  templates: [{ group: 0xFFFF, intent: 0, base: 50, text: 'How can I help you today?' }],
  fallbacks: { tier1: [], tier2: [], tier3: [] },
};
const dom = relationshipPostureReport(dominantPosture);
if (dom.posture !== 'dominant' || !dom.warnings){
  console.error('FAIL: dominant posture did not warn on assistant/service language', dom);
  process.exit(1);
}
console.log('relationship posture catches subordinate language for dominant characters');
const domContract = relationshipContract(dominantPosture);
if (domContract.posture !== 'dominant'
    || !domContract.avoid.some(s => /assistant language/i.test(s))
    || !domContract.may.some(s => /lead/i.test(s))
    || !domContract.asks.some(s => /forbid/i.test(s))){
  console.error('FAIL: dominant relationship contract lacks anti-subordinate guidance', domContract);
  process.exit(1);
}
console.log('relationship contract gives dominant characters non-subordinate rules');

const generatedDominant = defaultCharacter();
generatedDominant.name = 'Dominant Starter';
generatedDominant.identity.A = 18;
generatedDominant.identity.E = 78;
generatedDominant.identity.voice_flags = ['SARDONIC','NO_DIRECT_AFFIRM','ALLOW_CONTRADICT'];
generatedDominant.identity.obsessions = ['the work','homunculi','forbidden science'];
generatedDominant.identity.core_memories = [
  { text: 'Public humiliation by academic peers', v: -70, a: 60, d: -20 },
  { text: 'The first successful artificial animation', v: 90, a: 80, d: 70 },
];
generateStarterInternalLife(generatedDominant, 'starter');
if (!generatedDominant.identity.wants.some(w => /test|taken seriously|answer sharply/i.test(w.name))
    || generatedDominant.identity.resumption_lines.some(s => /what do you need|how can i help|glad to help/i.test(s))){
  console.error('FAIL: dominant starter internal life did not preserve non-subordinate posture', generatedDominant.identity);
  process.exit(1);
}
console.log('starter internal life preserves dominant posture');

const servingPosture = defaultCharacter();
servingPosture.name = 'Kiki';
servingPosture.identity.A = 82;
servingPosture.identity.obsessions = ['helping','growth','listening'];
servingPosture.dialoguePack = {
  templates: [{ group: 0xFFFF, intent: 0, base: 50, text: 'How can I help, babe?' }],
  fallbacks: { tier1: [], tier2: [], tier3: [] },
};
const srv = relationshipPostureReport(servingPosture);
if (srv.posture !== 'serving' || srv.warnings){
  console.error('FAIL: serving posture should allow helper language', srv);
  process.exit(1);
}
console.log('relationship posture allows service language for serving characters');
const srvContract = relationshipContract(servingPosture);
if (srvContract.posture !== 'serving'
    || !srvContract.may.some(s => /offer help/i.test(s))
    || srvContract.avoid.some(s => /assistant language/i.test(s))){
  console.error('FAIL: serving relationship contract should allow helper posture', srvContract);
  process.exit(1);
}
console.log('relationship contract preserves serving/helper characters');

const checklist = livingCharacterChecklist(ch);
if (!Array.isArray(checklist)
    || !checklist.some(i => /Wants: 3 \/ 3/.test(i.title) && i.status === 'pass')
    || !checklist.some(i => /Return lines: 4 \/ 4/.test(i.title) && i.status === 'pass')
    || !checklist.some(i => /Relationship posture/.test(i.title))){
  console.error('FAIL: living character checklist did not summarize internal life readiness', checklist);
  process.exit(1);
}
console.log('living character checklist summarizes internal-life readiness');

const thinCharacter = defaultCharacter();
thinCharacter.name = 'Thin Character';
thinCharacter.identity.wants = [];
thinCharacter.identity.current_preoccupations = [];
thinCharacter.identity.resumption_lines = [];
thinCharacter.identity.milestones = [];
thinCharacter.identity.core_memories = [];
thinCharacter.identity.obsessions = [];
const thinChecklist = livingCharacterChecklist(thinCharacter);
if (!thinChecklist.some(i => /Wants: 0 \/ 3/.test(i.title) && i.status === 'fail')
    || !thinChecklist.some(i => /Core memories: 0/.test(i.title) && i.status === 'fail')){
  console.error('FAIL: living character checklist did not flag thin character', thinChecklist);
  process.exit(1);
}
console.log('living character checklist flags thin characters');

const exportDlStub = {
  innerHTML: '',
  classList: { add(){}, remove(){}, toggle(){} },
  set textContent(v){ this._textContent = v; },
  get textContent(){ return this._textContent || ''; },
};
const livingChecklistStub = { innerHTML: '' };
const oldGetElementById = global.document.getElementById;
global.document.getElementById = (id) => {
  if (id === 'export-dl') return exportDlStub;
  if (id === 'living-checklist') return livingChecklistStub;
  return oldGetElementById(id);
};
try {
  refreshExport();
  if (!/relationship/.test(exportDlStub.innerHTML) || !/contract/.test(exportDlStub.innerHTML)){
    console.error('FAIL: export summary does not surface relationship contract', exportDlStub.innerHTML);
    process.exit(1);
  }
  if (!/Living Character Checklist/.test(livingChecklistStub.innerHTML)
      || !/Wants:/.test(livingChecklistStub.innerHTML)){
    console.error('FAIL: export tab does not render living character checklist', livingChecklistStub.innerHTML);
    process.exit(1);
  }
  console.log('export summary surfaces relationship contract');
  console.log('export tab renders living character checklist');
} finally {
  global.document.getElementById = oldGetElementById;
}

/* --- BUILD AN LM in the browser-equivalent path, embed in cart --- */
console.log('--- building LM in JS from Pretorian sample corpus');
const t0 = Date.now();
const lmb = new LMBuilder(PE_LM_DEFAULT_ORDER);
lmb.addCorpus(LM_SAMPLE_PRETORIUS);
const lmBytes = lmb.serialize();
console.log(`LM built in ${Date.now() - t0} ms · ${lmBytes.length} bytes · ${lmb.stats().totalEntries} entries`);
ch.lmBytes = lmBytes;

/* Live-preview scoring should produce something reasonable. */
const rt = new LMRuntime(lmBytes);
console.log('  in-register score:    ',  rt.scoreText('Mmm. I had begun to think.').perChar, 'milli-nats/char');
console.log('  out-of-register score:',  rt.scoreText('OMG that is so totally rad.').perChar, 'milli-nats/char');

const cart = buildCart(ch);
console.log(`cart bytes: ${cart.length} (LM included)`);
fs.writeFileSync(OUT, Buffer.from(cart));
console.log('wrote', OUT);

if (!fs.existsSync(LINT)){
  console.error('cartridge_lint not built - run `make build/cartridge_lint` first');
  process.exit(2);
}
console.log('--- linting Forge-produced cart');
const lint = spawnSync(LINT, [OUT], {
  encoding: 'utf-8',
  timeout: 10000,
});
console.log('--- lint stdout ---'); console.log(lint.stdout);
console.log('--- lint stderr ---'); console.log(lint.stderr);
if (lint.status !== 0){
  console.error('FAIL: cartridge_lint exited with', lint.status);
  process.exit(1);
}
if (!lint.stdout.includes('0 error(s), 0 warning(s)')){
  console.error('FAIL: Forge-produced cart has lint warnings/errors');
  process.exit(1);
}

/* Now ask persona_host to load it. */
if (!fs.existsSync(HOST) && !fs.existsSync(HOST + '.exe')){
  console.error('persona_host not built — run `make host` first');
  process.exit(2);
}
console.log('--- launching persona_host ' + OUT + ' --stdio');
wipeCartState(OUT);
const res = spawnSync(HOST, ['--stdio', OUT], {
  input: '{"method":"chat","text":"Hello?"}\n{"method":"close"}\n',
  encoding: 'utf-8',
  timeout: 10000,
});
console.log('--- stderr ---'); console.log(res.stderr);
console.log('--- stdout ---'); console.log(res.stdout);
if (res.status !== 0){
  console.error('FAIL: persona_host exited with', res.status);
  process.exit(1);
}
if (!res.stdout.includes('reply')){
  console.error('FAIL: no reply in host stdout');
  process.exit(1);
}
const firstReplyLine = res.stdout.trim().split(/\r?\n/).find(line => line.includes('"reply"'));
const firstReply = firstReplyLine ? JSON.parse(firstReplyLine).reply : '';
if (/^[a-z]/.test(firstReply)){
  console.error('FAIL: first Forge-produced reply begins like a clipped fragment:', firstReply);
  process.exit(1);
}
console.log('--- OK: Forge-produced .cart loaded and replied ---');

const qualityScript = [
  'Good morning.',
  'How are you today?',
  'What are you thinking about?',
  'Tell me about entropy.',
  'Can you explain that plainly?',
  'That sounds beautiful.',
  'Ask me something you actually want to know.',
  'Remember this phrase: the blue bell jar.',
  'What did I ask you to remember?',
  'What do you like about stars?',
  'I do not understand the physics.',
  'Could you give me the short version?',
  'What should we talk about next time?',
  'Ok.',
  'Goodnight.'
];
fs.writeFileSync(QUALITY_SCRIPT, JSON.stringify(qualityScript, null, 2));
console.log('--- running transcript quality on Forge-produced cart');
const quality = spawnSync(process.execPath, [
  path.join(__dirname, 'continuity', 'transcript_quality_test.js'),
  OUT,
  '--script', QUALITY_SCRIPT,
  '--terms', 'entropy,science,stars,physics,curiosity,explain,cosmos,Sagan,blue bell jar'
], {
  encoding: 'utf-8',
  timeout: 15000,
});
console.log('--- transcript quality stdout ---'); console.log(quality.stdout);
console.log('--- transcript quality stderr ---'); console.log(quality.stderr);
if (quality.status !== 0){
  console.error('FAIL: Forge-produced cart failed transcript quality');
  process.exit(1);
}
console.log('--- OK: Forge-produced .cart passes transcript quality ---');
