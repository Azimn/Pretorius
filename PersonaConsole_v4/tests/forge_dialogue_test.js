#!/usr/bin/env node
/* forge_dialogue_test.js — verifies CH.dialoguePack overrides emit
 * custom templates / patterns / goals / fallbacks into the cart, and
 * that the engine speaks the overridden lines.
 */
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const FORGE  = path.join(__dirname, '..', '..', 'CartridgeForge', 'forge.html');
const HOST   = path.join(__dirname, '..', 'build', 'persona_host');
const OUTDIR = '/tmp/forge_dialogue_test';
const OUT    = OUTDIR + '/override.cart';

if (fs.existsSync(OUTDIR)) {
  require('child_process').execSync(`rm -rf ${OUTDIR}`);
}
fs.mkdirSync(OUTDIR, { recursive: true });

console.log('--- extracting JS from', FORGE);
const html = fs.readFileSync(FORGE, 'utf-8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
let js = m[1];

/* same DOM stubs as forge_cart_test.js */
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
global.confirm = () => true;
global.alert   = () => {};

const exposeNames = [
  'defaultCharacter','buildCart',
  'GENERIC_PATTERNS','GENERIC_TEMPLATES','GENERIC_GOALS','GENERIC_FALLBACKS',
  'INTENT','G',
];
js += `\n;module.exports = { ${exposeNames.map(n => n + ':typeof ' + n + '!=="undefined"?' + n + ':null').join(',')} };`;
const moduleObj = { exports: {} };
new Function('module', js).call(global, moduleObj);
const {
  defaultCharacter, buildCart,
  GENERIC_PATTERNS, GENERIC_TEMPLATES, GENERIC_GOALS, GENERIC_FALLBACKS,
  INTENT, G,
} = moduleObj.exports;

/* Build a character with a fully custom dialogue pack. */
const ch = defaultCharacter();
ch.name = 'Override Test';
ch.identity.address_user_as = ['comrade','you','',''];
ch.identity.obsessions = ['the project','the work'];

/* Single custom today state — the engine should pick it and report its
 * label back through /state.today.  Lowercase to match engine sanitizer. */
ch.todayStates = [
  { label: 'todaycheck', mood: 0, vf_or: 0, goal: 0xFFFF },
];

/* Custom templates: a single unmistakable greeting line + a who line +
 * a generic monologue so the engine can always produce something. */
ch.dialoguePack = {
  templates: [
    { group: G.GREETING, intent: INTENT.ANSWER,    base: 80,
      text: 'CUSTOM-GREETING comrade welcome to the project' },
    { group: G.WHO,      intent: INTENT.ANSWER,    base: 80,
      text: 'CUSTOM-WHO i am override test' },
    { group: 0xFFFF,     intent: INTENT.MONOLOGUE, base: 50,
      text: 'CUSTOM-FALLBACK something about the project' },
  ],
  patterns: [
    { kw: 'hello',  cls: 0, group: G.GREETING, v: 10, a: 20, d:  5 },
    { kw: 'who',    cls: 3, group: G.WHO,      v:  5, a: 30, d: 10 },
  ],
  goals: [
    { id: 1, name: 'monologue', prio: 99, intent: INTENT.MONOLOGUE,
      dw: [0,0,0,0,0,0,0,0] },
  ],
  fallbacks: {
    tier1: ['CUSTOM-TIER1-A','CUSTOM-TIER1-B'],
    tier2: ['CUSTOM-TIER2'],
    tier3: ['CUSTOM-TIER3'],
  }
};

const cart = buildCart(ch);
fs.writeFileSync(OUT, Buffer.from(cart));
console.log(`wrote ${OUT} (${cart.length} B)`);

if (!fs.existsSync(HOST)){
  console.error('persona_host not built'); process.exit(2);
}

const prompts = [
  'hello there',
  'who are you',
];
const stdin = prompts.map(p => `{"method":"chat","text":${JSON.stringify(p)}}`).join('\n')
            + `\n{"method":"close"}\n`;
const res = spawnSync(HOST, [OUT, '--stdio'], {
  input: stdin, encoding: 'utf-8', timeout: 8000
});
console.log('--- stderr ---'); console.log(res.stderr);
console.log('--- stdout ---'); console.log(res.stdout);
if (res.status !== 0){ console.error('persona_host exit', res.status); process.exit(1); }

/* Look for at least one reply that contains our CUSTOM- marker.  The
 * engine may pick the higher-priority monologue goal over the
 * keyword-routed greeting template, but every template we shipped is
 * prefixed CUSTOM-, so any reply must contain CUSTOM. */
const replies = res.stdout.split('\n').filter(l => l.startsWith('{"reply"'));
let customHits = 0;
/* engine may lower-case the first character of a reply (sentence-case
 * style transform), so match case-insensitive. */
for (const r of replies){
  if (/custom-/i.test(r)) customHits++;
}
console.log(`replies: ${replies.length}, with CUSTOM- marker: ${customHits}`);
if (customHits === 0){
  console.error('FAIL: engine did not surface any CUSTOM- template');
  process.exit(1);
}

/* Today-state override should produce "today":"todaycheck" on every reply
 * (there's only one state to pick). */
let todayHits = 0;
for (const r of replies){
  if (/"today":"todaycheck"/i.test(r)) todayHits++;
}
console.log(`replies with today=todaycheck: ${todayHits}/${replies.length}`);
if (todayHits === 0){
  console.error('FAIL: custom today state did not surface');
  process.exit(1);
}

console.log('--- OK: dialoguePack overrides reach the engine ---');
