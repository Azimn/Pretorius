#!/usr/bin/env node
/* relationships_roster_test.js -- multiple interlocutor relationship hooks. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin']){
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
      env: { ...process.env, PE_TODAY_SEED: '0xB0B0' }
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve(stdout.split('\n').filter(Boolean).map(l => JSON.parse(l)));
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
  console.log('--- relationships roster test ---');
  wipeState();
  const rows = await run([
    { method: 'set_user', user_id: 'Julia' },
    { method: 'chat', text: 'Hello doctor.' },
    { method: 'set_user', user_id: 'Victor' },
    { method: 'chat', text: 'You are a fool.' },
    { method: 'relationships' }
  ]);
  const roster = rows.find(r => Array.isArray(r.relationships));
  ok(roster && roster.count >= 2, `relationships endpoint lists multiple interlocutors (${roster && roster.count})`);
  ok(roster.relationships.some(r => r.known_as === 'Julia'), 'roster includes Julia');
  ok(roster.relationships.some(r => r.known_as === 'Victor'), 'roster includes Victor');
  const julia = roster.relationships.find(r => r.known_as === 'Julia');
  const victor = roster.relationships.find(r => r.known_as === 'Victor');
  ok(julia && victor && julia.user_hash !== victor.user_hash, 'relationships have distinct hashes');
  ok(julia && victor && julia.disposition > victor.disposition,
     `separate dispositions persist per interlocutor (${julia && julia.disposition} > ${victor && victor.disposition})`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- relationship roster hook active');
})().catch(e => { console.error(e); process.exit(2); });
