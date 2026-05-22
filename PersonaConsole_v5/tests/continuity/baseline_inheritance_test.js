#!/usr/bin/env node
/* baseline_inheritance_test.js -- verifies engine-side social pattern/template inheritance. */
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART = path.join(__dirname, '..', '..', 'profiles', 'kiki', 'kiki.cart');
const CHDIR = path.dirname(CART);

const BL = {
  THREAT: 0x7000 + 3,
  GREETING: 0x7000 + 5,
  STATUS: 0x7000 + 8,
  APOLOGY: 0x7000 + 10,
};

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(texts){
  return new Promise((resolve, reject) => {
    const stdin = texts.map(text => JSON.stringify({ method: 'chat', text })).join('\n')
                + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio']);
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
  console.log('--- baseline inheritance test ---');
  wipeState();
  const turns = await run([
    'Good morning.',
    'How are you?',
    'Please forgive me.',
    'I will report you.',
  ]);

  ok(turns.length === 4, 'host returned four chat replies');
  ok(turns[0].state.last_template_group === BL.GREETING,
     'Kiki inherits baseline good-morning greeting');
  ok(turns[1].state.last_template_group === BL.STATUS,
     'Kiki inherits baseline status question');
  ok(turns[2].state.last_template_group === BL.APOLOGY,
     'Kiki inherits baseline apology handling');
  ok(turns[3].state.last_template_group === BL.THREAT,
     'Kiki inherits baseline report-threat handling');
  for (const turn of turns){
    ok(turn.reply && !turn.reply.includes('{'),
       `baseline reply filled slots: ${turn.reply}`);
  }

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- baseline inheritance active');
})().catch(e => { console.error(e); process.exit(2); });
