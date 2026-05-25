#!/usr/bin/env node
/* strategic_pause_test.js -- verifies V5 silence as an intentional reply. */
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(seed){
  wipeState();
  const script = [
    'You are a fool.',
    'I hate everything you have built.',
    'You are worthless.',
    'Shut up.',
    'You are a fool.',
    'I hate you.',
    'You are stupid.',
    'You are a fool.',
  ];
  return new Promise((resolve, reject) => {
    const stdin = script.map(text => JSON.stringify({ method: 'chat', text })).join('\n')
                + '\n{"method":"close"}\n';
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
    setTimeout(() => proc.kill('SIGKILL'), 30000).unref();
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
  console.log('--- strategic pause test ---');
  let pauseTurn = null;
  for (let seed = 1; seed <= 64 && !pauseTurn; seed++){
    const turns = await run(seed);
    pauseTurn = turns.find(t => t.pause === true);
  }

  ok(!!pauseTurn, 'severe hostile arc can produce an intentional pause');
  if (pauseTurn){
    ok(pauseTurn.reply === null, 'pause reply is JSON null, not an empty string');
    ok(pauseTurn.state && pauseTurn.state.intent === 'pause',
       `pause exposes intent in state (${pauseTurn.state && pauseTurn.state.intent})`);
    ok(pauseTurn.state && pauseTurn.state.turn_count > 0,
       'pause still advances and saves turn state');
  }

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- strategic pause active');
})().catch(e => { console.error(e); process.exit(2); });
