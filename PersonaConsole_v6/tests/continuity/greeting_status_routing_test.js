#!/usr/bin/env node
/* Verifies that common first-contact social acts route to their own pools. */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { resolveHost } = require('../host_path');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const SRC_CART = path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart');

const G_PRETORIUS_GREETING = 13;
const G_PRETORIUS_STATUS = 15;
const BL_GREETING = 0x7000 + 5;
const BL_STATUS = 0x7000 + 8;

function ok(cond, msg){
  if (!cond){
    console.error(`not ok: ${msg}`);
    process.exitCode = 1;
  } else {
    console.log(`ok:   ${msg}`);
  }
}

function makeTempCart(){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'persona-gs-'));
  const cart = path.join(dir, 'pretorius.cart');
  fs.copyFileSync(SRC_CART, cart);
  return { dir, cart };
}

function run(cart, texts){
  return new Promise((resolve, reject) => {
    const stdin = texts.map(text => JSON.stringify({ method: 'chat', text })).join('\n')
                + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [cart, '--stdio'], {
      env: {
        ...process.env,
        PE_RENDER_BACKEND: 'template',
        PE_TODAY_SEED: '0x47525453',
        PE_CLOCK_OVERRIDE_MS: '1700000000000',
      },
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf8'));
    proc.stderr.on('data', d => stderr += d.toString('utf8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) return reject(new Error(`host exited ${status}: ${stderr}`));
      resolve(stdout.split('\n')
        .filter(l => l.startsWith('{"reply"'))
        .map(l => JSON.parse(l)));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000).unref();
  });
}

(async function main(){
  console.log('--- greeting/status routing test ---');
  const tmp = makeTempCart();
  try {
    const turns = await run(tmp.cart, [
      'Good afternoon.',
      'Are you all right?',
      'Good morning again.',
      'How are you holding up?',
    ]);
    ok(turns.length === 4, 'host returned four replies');
    ok([G_PRETORIUS_GREETING, BL_GREETING].includes(turns[0].state.last_template_group),
       `good afternoon routes as greeting, got ${turns[0].state.last_template_group}`);
    ok([G_PRETORIUS_STATUS, BL_STATUS].includes(turns[1].state.last_template_group),
       `are-you-all-right routes as status, got ${turns[1].state.last_template_group}`);
    for (const [i, turn] of turns.entries()){
      ok(turn.reply && !turn.reply.includes('{'),
         `turn ${i + 1} has no raw slot token: ${turn.reply}`);
      ok(!/\bof\s*$/i.test(turn.reply || ''),
         `turn ${i + 1} does not end with empty memory slot: ${turn.reply}`);
    }
  } finally {
    fs.rmSync(tmp.dir, { recursive: true, force: true });
  }
  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- greeting/status routing stable');
})().catch(e => { console.error(e); process.exit(2); });
