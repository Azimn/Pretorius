#!/usr/bin/env node
/* conversation_rhythm_test.js -- avoids one-way LLM-style answer loops. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin','open_loops.bin','speech_habits.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(commands){
  return new Promise((resolve, reject) => {
    const stdin = commands.map(c => JSON.stringify(c)).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '0x515151' }
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
  console.log('--- conversation rhythm test ---');
  wipeState();
  const rows = await run([
    { method: 'chat', text: 'Good evening.' },
    { method: 'chat', text: 'I am here.' },
    { method: 'chat', text: 'Go on.' },
    { method: 'chat', text: 'Interesting.' },
    { method: 'chat', text: 'I am listening.' },
    { method: 'chat', text: 'Continue.' },
    { method: 'chat', text: 'Yes.' },
    { method: 'chat', text: 'Still here.' }
  ]);
  ok(rows.length >= 8, 'host returned rhythm arc replies');
  const debtBreak = rows.find(r => r.state
    && r.state.last_reply_had_question === 1
    && /[?]/.test(r.reply)
    && ['probe', 'initiate', 'clarify'].includes(r.state.intent));
  ok(!!debtBreak,
     `engine breaks one-way rhythm with a question: ${debtBreak ? debtBreak.reply : rows.map(r => `${r.state && r.state.intent}:${r.reply}`).join(' | ')}`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- conversation rhythm active');
})().catch(e => { console.error(e); process.exit(2); });
