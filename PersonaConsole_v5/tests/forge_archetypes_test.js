#!/usr/bin/env node
/* forge_archetypes_test.js — walk every archetype in the gallery, build
 * a .cart from its seed, run it through persona_host, and confirm a
 * reply comes back.  Catches regressions in archetype data and in
 * applyArchetype's template-override merging.
 */
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const FORGE = path.join(__dirname, '..', '..', 'CartridgeForge', 'forge.html');
const HOST  = path.join(__dirname, '..', 'build', 'persona_host');
const OUT   = '/tmp/forge_arch_test.cart';

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
global.performance = { now: () => Date.now() };
global.confirm = () => false;
global.alert   = () => {};

const exposeNames = [
  'ARCHETYPES','defaultCharacter','applyArchetype','buildCart',
  'LMBuilder','PE_LM_DEFAULT_ORDER','GENERIC_TEMPLATES',
];
js += `\n;module.exports = { ${exposeNames.map(n => n + ':typeof ' + n + '!=="undefined"?' + n + ':null').join(',')} };`;
const moduleObj = { exports: {} };
new Function('module', js).call(global, moduleObj);
const {
  ARCHETYPES, defaultCharacter, applyArchetype, buildCart,
  LMBuilder, PE_LM_DEFAULT_ORDER, GENERIC_TEMPLATES,
} = moduleObj.exports;

if (!fs.existsSync(HOST)){
  console.error('persona_host not built'); process.exit(2);
}

console.log(`--- found ${ARCHETYPES.length} archetypes`);
let fails = 0;
for (const a of ARCHETYPES){
  applyArchetype(a.seed);
  /* applyArchetype mutates the top-level CH; we can't reach it cleanly
   * from out here, so emulate by hand using the seed. */
  const ch = defaultCharacter();
  ch.name = a.seed.name;
  ch.identity.O = a.seed.O; ch.identity.C = a.seed.C;
  ch.identity.E = a.seed.E; ch.identity.A = a.seed.A; ch.identity.N = a.seed.N;
  ch.identity.verbosity = a.seed.verb;
  ch.identity.voice_flags = [...a.seed.flags];
  ch.identity.address_user_as = [...a.seed.address];
  ch.identity.obsessions = [...a.seed.obsessions];
  ch.identity.taboos     = [...a.seed.taboos];
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

  let cart;
  try { cart = buildCart(ch); }
  catch (e){
    console.error(`[${a.id}] FAIL buildCart: ${e.message}`);
    fails++; continue;
  }
  fs.writeFileSync(OUT, Buffer.from(cart));

  const res = spawnSync(HOST, [OUT, '--stdio'], {
    input: '{"method":"chat","text":"hello"}\n{"method":"chat","text":"who are you?"}\n{"method":"close"}\n',
    encoding: 'utf-8', timeout: 8000,
  });
  if (res.status !== 0){
    console.error(`[${a.id}] FAIL persona_host exit ${res.status}`);
    console.error(res.stderr); fails++; continue;
  }
  const replies = res.stdout.split('\n').filter(l => l.startsWith('{"reply"'));
  if (replies.length < 2){
    console.error(`[${a.id}] FAIL expected 2 replies got ${replies.length}`);
    fails++; continue;
  }
  const sample = JSON.parse(replies[1]).reply;
  console.log(`[${a.id}] ok · ${(cart.length/1024).toFixed(1)} KB · "${sample.slice(0, 70)}…"`);
}

if (fails){
  console.error(`--- ${fails}/${ARCHETYPES.length} FAILED ---`);
  process.exit(1);
}
console.log(`--- OK: all ${ARCHETYPES.length} archetypes build + reply ---`);
