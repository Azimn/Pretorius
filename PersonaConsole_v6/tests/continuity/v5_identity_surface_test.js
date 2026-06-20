#!/usr/bin/env node
/* v5_identity_surface_test.js -- verifies V5 identity fields are packed and surfaced. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin',
                   'reflections.bin', 'actor_index.bin', 'speech_events.bin',
                   'dissonance.bin', 'open_loops.bin', 'speech_habits.bin',
                   'learned_knowledge.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether', 'learned_knowledge']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(seed){
  wipeState();
  return new Promise((resolve, reject) => {
    const stdin = [
      { method: 'chat', text: 'What are you working on?' },
      { method: 'chat', text: 'Tell me about your work.' },
      { method: 'chat', text: 'What is the work today?' },
    ].map(JSON.stringify).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: String(seed) }
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
  console.log('--- V5 identity surface test ---');
  const header = fs.readFileSync(CART).subarray(0, 8);
  ok(header.readUInt32LE(0) === 0x54524143, 'cart magic is CART');
  ok(header.readUInt16LE(4) === 2, 'cart version is V5');

  let seen = false;
  let sample = [];
  for (let seed = 1; seed <= 16 && !seen; seed++){
    const rows = await run(seed);
    sample = sample.concat(rows.map(r => r.reply));
    seen = rows.some(r => typeof r.reply === 'string'
      && /bell-jar|weather and consequence|gin to last until Tuesday/i.test(r.reply));
  }
  if (!seen) console.error('diagnostic replies:', sample.slice(0, 8).join(' | '));
  ok(seen, 'Pretorius work chat can surface current_preoccupations');

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V5 identity surface active');
})().catch(e => { console.error(e); process.exit(2); });
