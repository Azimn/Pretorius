#!/usr/bin/env node
/* proactive_intents_test.js -- verifies V5 initiative/attention intents. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of [
    'state.bin', 'memory.bin', 'chapters.bin',
    'actor_index.bin', 'speech_ledger.bin', 'relation_dims.bin',
    'dissonance.bin', 'open_loops.bin', 'speech_habits.bin',
    'reflections.bin'
  ]){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(texts, seed = '0x51A7EED'){
  return new Promise((resolve, reject) => {
    const stdin = texts.map(text => JSON.stringify({ method: 'chat', text })).join('\n')
                + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: seed }
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve(stdout.split('\n').filter(l => l.startsWith('{"reply"')).map(l => JSON.parse(l)));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

function ok(cond, msg){
  if (!cond){
    console.error(`not ok: ${msg}`);
    process.exitCode = 1;
  } else {
    console.log(`ok:   ${msg}`);
  }
}

(async function main(){
  console.log('--- proactive intents test ---');
  wipeState();

  const neutralTurns = [
    'fragment alpha',
    'fragment beta',
    'fragment gamma',
    'fragment delta',
    'fragment epsilon',
    'fragment zeta',
    'fragment eta',
    'fragment theta',
    'fragment iota',
    'fragment kappa',
    'fragment lambda',
    'fragment mu',
  ];
  const neutral = await run(neutralTurns);
  ok(neutral.length === neutralTurns.length, 'host returned all neutral replies');
  ok(neutral.some(t => t.state.intent === 'initiate'),
     'neutral unmatched run eventually produces initiate intent');
  for (const turn of neutral){
    ok(turn.reply && !turn.reply.includes('{'), `reply has filled slots: ${turn.reply}`);
  }

  wipeState();
  const rich = await run([
    'It has been one of those strange shapeless days where I cannot decide whether I am worried, bored, or waiting for something to happen.'
  ], '0x12345678');
  ok(rich.length === 1, 'host returned rich-input reply');
  ok(['attend', 'clarify', 'answer', 'probe'].includes(rich[0].state.intent),
     `rich ambiguous input stays conversational (${rich[0].state.intent})`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- proactive intents active');
})().catch(e => { console.error(e); process.exit(2); });
