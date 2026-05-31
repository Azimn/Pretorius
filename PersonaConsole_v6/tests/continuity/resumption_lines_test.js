#!/usr/bin/env node
/* resumption_lines_test.js -- verifies V5 authored gap greetings. */
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);
const RELDIR = path.join(CHDIR, 'relations');

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(text){
  return new Promise((resolve, reject) => {
    const stdin = JSON.stringify({ method: 'chat', text }) + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '0x5150' }
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve(stdout.split('\n').filter(l => l.startsWith('{"reply"')).map(l => JSON.parse(l))[0]);
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

function backdateRelation(days){
  const bins = fs.readdirSync(RELDIR).filter(f => f.endsWith('.bin'));
  if (bins.length !== 1) throw new Error(`expected one relation bin, found ${bins.length}`);
  const file = path.join(RELDIR, bins[0]);
  const buf = fs.readFileSync(file);
  const now = Math.floor(Date.now() / 1000);
  buf.writeUInt32LE(now - days * 86400, 8);
  fs.writeFileSync(file, buf);
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
  console.log('--- V5 resumption lines test ---');
  wipeState();
  await run('Good evening, doctor.');
  backdateRelation(3);
  const row = await run('Good evening again.');
  ok(row && typeof row.reply === 'string', 'host returned post-gap reply');
  ok(/Back so soon|A day, was it|productive|prodigal returns|arrested|candles/i.test(row.reply),
     `post-gap reply prepends authored resumption line: ${row && row.reply}`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V5 resumption lines active');
})().catch(e => { console.error(e); process.exit(2); });
