#!/usr/bin/env node
/* milestone_catchup_test.js -- missed relationship milestones catch up later. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);
const RELDIR = path.join(CHDIR, 'relations');

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin']){
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
      env: { ...process.env, PE_TODAY_SEED: '0x7171' }
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

function backdateRelation(firstDays, lastDays){
  const bins = fs.readdirSync(RELDIR).filter(f => f.endsWith('.bin'));
  if (bins.length !== 1) throw new Error(`expected one relation bin, found ${bins.length}`);
  const file = path.join(RELDIR, bins[0]);
  const buf = fs.readFileSync(file);
  const now = Math.floor(Date.now() / 1000);
  buf.writeUInt32LE(now - lastDays * 86400, 8);
  buf.writeUInt32LE(now - firstDays * 86400, 12);
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
  console.log('--- milestone catch-up test ---');
  wipeState();
  await run('Good evening, doctor.');
  backdateRelation(8, 2);
  const row = await run('I am back.');
  ok(row && typeof row.reply === 'string', 'host returned post-gap reply');
  ok(/A week of you/i.test(row.reply),
     `day-7 milestone catches up on day 8 return: ${row && row.reply}`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- milestone catch-up active');
})().catch(e => { console.error(e); process.exit(2); });
