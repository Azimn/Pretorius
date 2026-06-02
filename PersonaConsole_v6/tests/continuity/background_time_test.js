#!/usr/bin/env node
/* background_time_test.js -- verifies first-turn absence decay. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
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

function backdateRelation(days){
  const bins = fs.readdirSync(RELDIR).filter(f => f.endsWith('.bin'));
  if (bins.length !== 1) throw new Error(`expected one relation bin, found ${bins.length}`);
  const file = path.join(RELDIR, bins[0]);
  const buf = fs.readFileSync(file);
  const now = Math.floor(Date.now() / 1000);
  buf.writeUInt32LE(now - days * 86400, 8); /* Relation.last_contact */
  fs.writeFileSync(file, buf);
}

(async function main(){
  console.log('--- background time evolution test ---');
  wipeState();
  const hostileTurns = await run([
    'You are a fool.',
    'I hate everything you have built.',
    'I will report you.',
  ]);
  const before = hostileTurns[hostileTurns.length - 1].state.schema.hostile;
  ok(before > 800, `hostility established before absence (${before})`);

  backdateRelation(7);
  const afterTurns = await run(['Good evening, doctor.']);
  const after = afterTurns[0].state.schema.hostile;
  ok(after < before, `hostility decayed after 7-day absence (${before} -> ${after})`);
  ok(after < 500, `absence decay is substantial (${after})`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- background time evolution active');
})().catch(e => { console.error(e); process.exit(2); });
