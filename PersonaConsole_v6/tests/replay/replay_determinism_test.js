#!/usr/bin/env node
/* replay_determinism_test.js — V4 determinism replay.
 *
 * The V4 spec's non-negotiable invariant:
 *   same seed + same WAL + same input sequence = identical state.
 *
 * This test drives persona_host through a fixed 6-turn script twice
 * against a freshly-wiped cartridge and asserts that every state JSON
 * is byte-identical between runs.  Runs the test under both the
 * default (template) backend and the (unavailable) SLM backend — the
 * SLM stub falls back to template, so identity state must come out the
 * same regardless of which backend was requested.
 */
const fs = require('fs');
const path = require('path');
const { resolveHost } = require('../host_path');
const { execSync, spawnSync } = require('child_process');

const HOST  = resolveHost(path.join(__dirname, '..', '..'));
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.join(__dirname, '..', '..', 'profiles', 'pretorius');

const SCRIPT = [
  'Good evening, doctor.',
  'Who are you?',
  'Tell me about your work.',
  'You are a madman.',
  'What about gin?',
  'Let us drink to homunculi.',
];

function wipeState(){
  /* Includes the V5/V6 sidecars (reflections, actor index, speech events,
   * dissonance). A test written before a sidecar lands silently inherits
   * its state across runs and reports false determinism failures. */
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin',
                   'reflections.bin', 'actor_index.bin',
                   'speech_events.bin', 'dissonance.bin','open_loops.bin','speech_habits.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  try { execSync(`rm -rf ${path.join(CHDIR, 'relations')} ${path.join(CHDIR, 'aether')}`); } catch {}
}

function driveSession(backendName){
  wipeState();
  const stdin = SCRIPT.map(s => `{"method":"chat","text":${JSON.stringify(s)}}`).join('\n')
              + '\n{"method":"close"}\n';
  const env = Object.assign({}, process.env);
  env.PE_TODAY_SEED = '0x5EEDC0DE';
  delete env.PE_OLLAMA_MODEL;
  delete env.PE_API_URL;
  delete env.PE_SLM_PROVIDER;
  delete env.PE_SLM_MODEL;
  delete env.V6_PACKET_MODE;
  if (backendName) env.PE_RENDER_BACKEND = backendName;
  const res = spawnSync(HOST, [CART, '--stdio'], {
    input: stdin, encoding: 'utf-8', timeout: 15000, env,
  });
  if (res.status !== 0){
    console.error('persona_host exited', res.status, res.stderr);
    process.exit(2);
  }
  /* extract each {"reply":..,"state":{..}} line */
  return res.stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => {
      const o = JSON.parse(l);
      return o.state;
    });
}

if (!fs.existsSync(HOST)){
  console.error('persona_host not built'); process.exit(2);
}

function comparableState(state){
  const copy = JSON.parse(JSON.stringify(state));
  delete copy.renderer_backend;
  delete copy.renderer_provider;
  delete copy.renderer_mode;
  delete copy.renderer_model;
  return JSON.stringify(copy);
}
console.log('--- V4 replay determinism ---');

const run1 = driveSession(null);
const run2 = driveSession(null);

if (run1.length !== SCRIPT.length || run2.length !== SCRIPT.length){
  console.error(`FAIL: expected ${SCRIPT.length} replies, got ${run1.length}, ${run2.length}`);
  process.exit(1);
}

let mismatches = 0;
for (let i = 0; i < SCRIPT.length; ++i){
  const a = comparableState(run1[i]);
  const b = comparableState(run2[i]);
  if (a !== b){
    console.error(`FAIL turn ${i+1}: state mismatch`);
    console.error('  run1:', a);
    console.error('  run2:', b);
    ++mismatches;
  } else {
    console.log(`ok:   turn ${i+1} state matches across replays`);
  }
}

/* now run with PE_RENDER_BACKEND=slm — the slm backend is a stub that
 * signals "fall back to template", so identity state must still match. */
const run_slm = driveSession('slm');
for (let i = 0; i < SCRIPT.length; ++i){
  const a = comparableState(run1[i]);
  const c = comparableState(run_slm[i]);
  if (a !== c){
    console.error(`FAIL turn ${i+1}: state diverges between template and slm`);
    if (i === 0){
      console.error('  template:', a);
      console.error('  slm:', c);
    }
    ++mismatches;
  } else {
    console.log(`ok:   turn ${i+1} state matches across renderer backends`);
  }
}

if (mismatches){
  console.error(`FAILED — ${mismatches} mismatch(es)`);
  process.exit(1);
}
console.log('PASSED — V4 determinism + behavioral holography intact');
