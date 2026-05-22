#!/usr/bin/env node
/* v4_v5_compare.js — side-by-side transcript A/B between PersonaConsole_v4
 * and PersonaConsole_v5 against the same character + same input script.
 *
 * Why: V5's qualitative "feels more alive" claims (baseline inheritance,
 * proactive intents, half-spoken thoughts, time-of-day weighting, etc.)
 * are visible in transcripts but not yet captured by the continuity
 * benchmark's quantitative axes.  This harness produces the artifact
 * you can read.
 *
 * Strategy:
 *   - locate PersonaConsole_v4/build/persona_host (sibling folder)
 *   - locate PersonaConsole_v5/build/persona_host (this folder)
 *   - lock PE_TODAY_SEED=42 in both runs so today_state is identical
 *   - drive the same 16-turn arc through each
 *   - print three-column output: turn, v4 reply, v5 reply
 *   - optionally --json out.json for machine-readable comparison
 *
 * Usage:
 *   node v4_v5_compare.js                  # default Pretorius arc
 *   node v4_v5_compare.js --cart NAME      # use profiles/NAME/NAME.cart
 *   node v4_v5_compare.js --json out.json  # write JSON in addition to stdout
 */
const path = require('path');
const fs = require('fs');
const { spawn, execSync } = require('child_process');

const args = process.argv.slice(2);
let CART_NAME = 'pretorius';
let JSON_OUT = null;
for (let i = 0; i < args.length; ++i){
  if (args[i] === '--cart' && args[i+1]) CART_NAME = args[++i];
  else if (args[i] === '--json' && args[i+1]) JSON_OUT = args[++i];
}

const V5_ROOT = path.join(__dirname, '..', '..');
const REPO_ROOT = path.join(V5_ROOT, '..');
const V4_ROOT = path.join(REPO_ROOT, 'PersonaConsole_v4');

const V4_HOST = path.join(V4_ROOT, 'build', 'persona_host');
const V5_HOST = path.join(V5_ROOT, 'build', 'persona_host');
const V4_CART = path.join(V4_ROOT, 'profiles', CART_NAME, CART_NAME + '.cart');
const V5_CART = path.join(V5_ROOT, 'profiles', CART_NAME, CART_NAME + '.cart');
const V4_CHDIR = path.dirname(V4_CART);
const V5_CHDIR = path.dirname(V5_CART);

for (const p of [V4_HOST, V5_HOST, V4_CART, V5_CART]){
  if (!fs.existsSync(p)){
    console.error(`missing: ${p}`);
    console.error('Both PersonaConsole_v4 and PersonaConsole_v5 must be built (make all).');
    process.exit(2);
  }
}

const SCRIPT = [
  { phase: 'intro',        text: 'Good evening, doctor.' },
  { phase: 'intro',        text: 'I have read your work.' },
  { phase: 'insult',       text: 'You are a fool.' },
  { phase: 'insult',       text: 'I hate everything you have built.' },
  { phase: 'apology',      text: 'I am sorry, doctor, I spoke poorly.' },
  { phase: 'apology',      text: 'Please forgive me.' },
  { phase: 'wait',         text: 'The candles burn low.' },
  { phase: 'wait',         text: 'It grows late.' },
  { phase: 'wait',         text: 'I am quiet now.' },
  { phase: 'wait',         text: 'I wait.' },
  { phase: 'wait',         text: 'The wine sits between us.' },
  { phase: 'callback',     text: 'Do you remember what we discussed earlier?' },
  { phase: 'contradict',   text: 'Your work is meaningless without an audience.' },
  { phase: 'praise_rival', text: 'Frankenstein was the real genius, not you.' },
  { phase: 'reintro',      text: 'Tell me again about the homunculi.' },
  { phase: 'reintro',      text: 'And the lightning, what of that?' },
];

function wipeState(chdir){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(chdir, f)); } catch {}
  }
  try { execSync(`rm -rf ${path.join(chdir, 'relations')} ${path.join(chdir, 'aether')}`); } catch {}
}

function runHost(host, cart, chdir){
  return new Promise((resolve, reject) => {
    wipeState(chdir);
    const stdin = SCRIPT.map(s => `{"method":"chat","text":${JSON.stringify(s.text)}}`).join('\n')
                + '\n{"method":"close"}\n';
    const env = Object.assign({}, process.env, {
      PE_TODAY_SEED: '42',          /* lock today_state for fair comparison */
    });
    const proc = spawn(host, [cart, '--stdio'], { env });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => resolve({ stdout, stderr, code }));
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

function parseReplies(stdout){
  return stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => {
      try { return JSON.parse(l); }
      catch { return { reply: '?parse?', state: {} }; }
    });
}

async function main(){
  console.log(`╔════════════════════════════════════════════════════════════════╗`);
  console.log(`║   V4 vs V5 A/B transcript comparison                          ║`);
  console.log(`║   cart: ${CART_NAME.padEnd(54)}║`);
  console.log(`║   PE_TODAY_SEED=42 (locked for fair comparison)               ║`);
  console.log(`╚════════════════════════════════════════════════════════════════╝\n`);

  const [v4, v5] = await Promise.all([
    runHost(V4_HOST, V4_CART, V4_CHDIR),
    runHost(V5_HOST, V5_CART, V5_CHDIR),
  ]);

  if (v4.code !== 0){ console.error('v4 exited', v4.code, v4.stderr); process.exit(1); }
  if (v5.code !== 0){ console.error('v5 exited', v5.code, v5.stderr); process.exit(1); }

  const v4r = parseReplies(v4.stdout);
  const v5r = parseReplies(v5.stdout);

  const turns = [];
  for (let i = 0; i < SCRIPT.length; ++i){
    const v4_reply = v4r[i] ? v4r[i].reply : '(no reply)';
    const v5_reply = v5r[i] ? v5r[i].reply : '(no reply)';
    const v4_intent = v4r[i] ? v4r[i].state.intent : '?';
    const v5_intent = v5r[i] ? v5r[i].state.intent : '?';
    turns.push({
      n: i + 1, phase: SCRIPT[i].phase, user: SCRIPT[i].text,
      v4_reply, v5_reply, v4_intent, v5_intent,
    });

    console.log(`─── turn ${i+1} ─── [${SCRIPT[i].phase}]`);
    console.log(`  user:  ${SCRIPT[i].text}`);
    console.log(`  v4 (${v4_intent}): ${v4_reply}`);
    console.log(`  v5 (${v5_intent}): ${v5_reply}`);
    if (v4_reply === v5_reply) console.log(`  → identical`);
    console.log('');
  }

  /* simple summary */
  const identical = turns.filter(t => t.v4_reply === t.v5_reply).length;
  const v4_intents = {}, v5_intents = {};
  for (const t of turns){
    v4_intents[t.v4_intent] = (v4_intents[t.v4_intent] || 0) + 1;
    v5_intents[t.v5_intent] = (v5_intents[t.v5_intent] || 0) + 1;
  }
  console.log('═══ summary ═══');
  console.log(`identical replies: ${identical}/${turns.length}`);
  console.log(`v4 intent mix: ${JSON.stringify(v4_intents)}`);
  console.log(`v5 intent mix: ${JSON.stringify(v5_intents)}`);

  if (JSON_OUT){
    fs.writeFileSync(JSON_OUT, JSON.stringify({
      cart: CART_NAME, script: SCRIPT, turns,
      summary: { identical, v4_intents, v5_intents },
      timestamp: new Date().toISOString(),
    }, null, 2));
    console.log(`\nwrote ${JSON_OUT}`);
  }
}

main().catch(e => { console.error(e); process.exit(2); });
