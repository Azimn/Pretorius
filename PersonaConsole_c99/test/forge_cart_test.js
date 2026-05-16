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

const FORGE = path.join(__dirname, '..', '..', 'CartridgeForge', 'forge.html');
const HOST  = path.join(__dirname, '..', 'build', 'persona_host');
const OUT   = '/tmp/forge_test.cart';

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
  'exportObject','CH','LMBuilder','LMRuntime','PE_LM_DEFAULT_ORDER',
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

const { ARCHETYPES, defaultCharacter, buildCart,
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

/* Now ask persona_host to load it. */
if (!fs.existsSync(HOST)){
  console.error('persona_host not built — run `make host` first');
  process.exit(2);
}
console.log('--- launching persona_host ' + OUT + ' --stdio');
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
console.log('--- OK: Forge-produced .cart loaded and replied ---');
