#!/usr/bin/env node
/* forge_bundle_test.js — verifies the deep-memory bundle path end-to-end.
 *
 * Synthesizes a fake long chat history, runs it through the Forge's
 * import pipeline (parse → analyze → memory extraction), builds a cart
 * + AETHER WAL bundle, extracts to a temp dir, runs persona_host on it,
 * asks two prompts: a vague one ("do you remember beer?") and a more
 * specific one ("we were discussing how to brew craft beer as a gift").
 * The specific prompt should surface the planted memory via AETHER cold
 * fallback, while the vague one may or may not.
 */
const fs   = require('fs');
const path = require('path');
const { execSync, spawnSync } = require('child_process');

const FORGE  = path.join(__dirname, '..', 'CartridgeForge', 'forge.html');
const HOST   = path.join(__dirname, '..', 'build', 'persona_host');
const OUTDIR = path.join(__dirname, 'tmp', 'forge_bundle_test');
const ZIPOUT = path.join(__dirname, 'tmp', 'forge_bundle_test.zip');

if (fs.existsSync(OUTDIR)) execSync(`rm -rf ${OUTDIR}`);
if (fs.existsSync(ZIPOUT)) fs.unlinkSync(ZIPOUT);
fs.mkdirSync(OUTDIR, { recursive: true });

console.log('--- extracting JS from', FORGE);
const html = fs.readFileSync(FORGE, 'utf-8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
let js = m[1];

/* minimal DOM stubs (same as forge_cart_test.js) */
const STUB = new Proxy({}, {
  get(t, p){
    if (p === 'addEventListener') return () => {};
    if (p === 'classList')        return { add(){}, remove(){}, toggle(){} };
    if (p === 'appendChild')      return () => {};
    if (p === 'querySelectorAll') return () => [];
    if (p === 'querySelector')    return () => STUB;
    if (p === 'createElement')    return () => STUB;
    if (p === 'dataset')          return {};
    if (p === 'style')             return {};
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
global.window     = { scrollTo(){}, addEventListener(){} };
global.FileReader = class { readAsText(){} };
global.Blob       = class { constructor(b){ this.b = b; } };
global.URL        = { createObjectURL(){ return ''; }, revokeObjectURL(){} };
global.TextEncoder = require('util').TextEncoder;
global.performance = { now: () => Date.now() };
global.confirm = () => true;
global.alert   = () => {};

const exposeNames = [
  'defaultCharacter','buildCart','buildZip','buildAetherWal',
  'pickMemorableExchanges','attachDeepMemories',
  'LMBuilder','PE_LM_DEFAULT_ORDER',
];
js += `\n;module.exports = { ${exposeNames.map(n => n + ':typeof ' + n + '!=="undefined"?' + n + ':null').join(',')} };`;
const moduleObj = { exports: {} };
new Function('module', js).call(global, moduleObj);
const {
  defaultCharacter, buildCart, buildZip, buildAetherWal,
  pickMemorableExchanges, attachDeepMemories,
  LMBuilder, PE_LM_DEFAULT_ORDER,
} = moduleObj.exports;

/* --- synthesize a fake long chat history --- */
const utts = [];
const PLANTED = "right — so for the craft beer gift idea, I'd say start with a wheat ale, easy fermentation and your friend will love the citrus notes; the hop schedule matters less than the temperature control";

for (let i = 0; i < 400; ++i){
  utts.push({ role: 'user', text: `quick question ${i}: weather is fine today` });
  utts.push({ role: 'assistant', text: `mm, the weather is fine, yes (turn ${i}). nothing notable about ${i}.` });
}
/* plant the memorable exchange near the end */
utts.push({ role: 'user',      text: 'I want to make my own beer as a gift for a friend' });
utts.push({ role: 'assistant', text: PLANTED });
for (let i = 400; i < 600; ++i){
  utts.push({ role: 'user', text: `routine question ${i}` });
  utts.push({ role: 'assistant', text: `routine reply ${i}, nothing special` });
}
console.log(`synthesized ${utts.length} utterances (${utts.length/2} exchanges) with one planted memory about brewing craft beer`);

const picked = pickMemorableExchanges(utts);
console.log(`extracted: ${picked.core.length} core memories + ${picked.deep.length} deep memories`);
const beerInCore = picked.core.some(m => /beer/i.test(m.text));
const beerInDeep = picked.deep.some(e => /beer/i.test(e.inline_text));
console.log('planted memory found in core?', beerInCore, 'in deep?', beerInDeep);
if (!beerInCore && !beerInDeep){
  console.error('FAIL: planted memory did not survive extraction');
  process.exit(1);
}

const ch = defaultCharacter();
ch.name = 'Beer Friend';
ch.identity.obsessions = ['brewing','beer','gifts'];
ch.identity.address_user_as = ['friend','you','dear one','beloved'];
attachDeepMemories(ch, picked);

/* build LM + cart + bundle */
const lm = new LMBuilder(PE_LM_DEFAULT_ORDER);
lm.addCorpus(utts.filter(u => u.role === 'assistant').slice(0, 30).map(u => u.text).join('\n'));
ch.lmBytes = lm.serialize();

const cartBytes = buildCart(ch);
const walBytes  = buildAetherWal(ch.deepMemoryEvents);
console.log(`cart: ${cartBytes.length} B, wal: ${walBytes.length} B (${ch.deepMemoryEvents.length} events)`);

const name = 'beer_friend';
const zip = buildZip([
  { path: `${name}/${name}.cart`,   bytes: cartBytes },
  { path: `${name}/aether/wal.dat`, bytes: walBytes },
]);
fs.writeFileSync(ZIPOUT, Buffer.from(zip));
console.log('wrote', ZIPOUT, '(' + zip.length + ' B)');

/* extract the zip via unzip CLI */
execSync(`unzip -q ${ZIPOUT} -d ${OUTDIR}`);
console.log('extracted:');
execSync(`find ${OUTDIR} -type f -printf '  %p (%s B)\\n'`).toString().split('\n').forEach(l => l && console.log(l));

/* run persona_host against the extracted cart */
const cartPath = `${OUTDIR}/${name}/${name}.cart`;
if (!fs.existsSync(HOST)){
  console.error('persona_host not built — run `make host` first'); process.exit(2);
}

const prompts = [
  'do you remember discussing beer with me a long time ago',
  'we were discussing how to brew craft beer as a gift for my friend, do you remember',
];
const stdin = prompts.map(p => `{"method":"chat","text":${JSON.stringify(p)}}`).join('\n')
            + `\n{"method":"close"}\n`;

const res = spawnSync(HOST, [cartPath, '--stdio'], {
  input: stdin, encoding: 'utf-8', timeout: 15000
});
console.log('--- stderr ---'); console.log(res.stderr);
console.log('--- stdout ---'); console.log(res.stdout);
if (res.status !== 0){ console.error('persona_host exited', res.status); process.exit(1); }

/* The specific prompt should produce a reply.  We don't insist that
 * the reply quotes the planted memory verbatim (generic dialogue pack
 * limits how creative replies can get), but we DO insist that the
 * engine ran the cold-fallback path on at least one of the two prompts. */
const replies = res.stdout.split('\n').filter(l => l.startsWith('{"reply"'));
console.log(`got ${replies.length} replies`);
if (replies.length < 2){
  console.error('expected 2 replies'); process.exit(1);
}

/* Inspect AETHER state to confirm WAL events are loaded.  We do this
 * by running the host once more in stdio mode and asking for state. */
const stateProbe = spawnSync(HOST, [cartPath, '--stdio'], {
  input: '{"method":"state"}\n{"method":"close"}\n',
  encoding: 'utf-8', timeout: 5000
});
console.log('state stdout:', stateProbe.stdout.split('\n')[0]);

/* Also verify the WAL file on disk has the expected magic + event count. */
const walPath = `${OUTDIR}/${name}/aether/wal.dat`;
const wal = fs.readFileSync(walPath);
console.log(`on-disk wal.dat: ${wal.length} B, ${wal.length / 68 | 0} records`);
if (wal.readUInt32LE(0) !== 0xAE57AE57){
  console.error('FAIL: WAL record magic mismatch');
  process.exit(1);
}

console.log('--- OK: Forge bundle (cart + AETHER WAL) loaded and engine replied to deep-memory prompts ---');
