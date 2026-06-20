#!/usr/bin/env node
/* v5_want_pull_test.js -- verifies authored wants bias proactive questions. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const os = require('os');
const { spawn } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const SRC = path.join(ROOT, 'profiles', 'pretorius');
const CHDIR = path.join(os.tmpdir(), 'persona-want-pull-profile');
const CART = path.join(CHDIR, 'pretorius.cart');

function resetProfile(){
  fs.rmSync(CHDIR, { recursive: true, force: true });
  fs.cpSync(SRC, CHDIR, { recursive: true });
}

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(){
  wipeState();
  return new Promise((resolve, reject) => {
    const inputs = [
      'hm.',
      'I am listening.',
      'go on.',
      'interesting.',
      'and then?',
      'yes.',
      'hm.',
      'keep going.',
      'I am still listening.',
      'go on.',
      'what else?',
      'yes.'
    ];
    const stdin = inputs.map(text => JSON.stringify({ method: 'chat', text })).join('\n')
      + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '0x5eed' }
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
  console.log('--- V5 want pull test ---');
  resetProfile();
  const rows = await run();
  ok(rows.length >= 12, 'host returned neutral arc replies');
  const proactive = rows.find(r => r.state && r.state.intent === 'initiate'
    && typeof r.reply === 'string'
    && /work|lightning|Henry/i.test(r.reply));
  ok(!!proactive,
     `neglected authored want can steer proactive topic: ${proactive ? proactive.reply : rows.map(r => r.reply).join(' | ')}`);

  fs.rmSync(CHDIR, { recursive: true, force: true });
  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V5 want pull active');
})().catch(e => { console.error(e); process.exit(2); });
