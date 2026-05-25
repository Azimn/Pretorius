#!/usr/bin/env node
/* unresolved_thread_test.js -- verifies half-spoken thought bookmarking. */
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
  const commands = [];
  for (let i = 0; i < 8; i++){
    commands.push({ method: 'chat', text: `fragment ${i} with loose conclusion` });
  }
  commands.push({ method: 'idle_probe' });
  return new Promise((resolve, reject) => {
    const stdin = commands.map(c => JSON.stringify(c)).join('\n')
                + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: {
        ...process.env,
        PE_TODAY_SEED: String(seed),
        PE_FORCE_UNRESOLVED_THREAD: '1',
      }
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
  console.log('--- unresolved thread test ---');
  const rows = await run(0x5155);
  const chatRows = rows.filter(r => Object.prototype.hasOwnProperty.call(r, 'reply') && r.state);
  const half = chatRows.find(r => typeof r.reply === 'string' && /never mind/i.test(r.reply));
  const idle = rows[rows.length - 2];
  const found = (half && half.state.unresolved_count > 0
              && idle && typeof idle.reply === 'string'
              && /still owe you the rest/i.test(idle.reply))
              ? { half, idle } : null;
  if (!found){
    console.error('diagnostic replies:');
    for (const r of chatRows.slice(0, 4))
      console.error(JSON.stringify({ reply: r.reply, intent: r.state.intent, group: r.state.last_template_group, unresolved_count: r.state.unresolved_count }));
    console.error('idle:', JSON.stringify(idle));
  }

  ok(!!found, 'long neutral arc can bookmark a half-spoken thought');
  if (found){
    ok(found.half.state.unresolved_count > 0,
       `unresolved_count increments (${found.half.state.unresolved_count})`);
    ok(/still owe you the rest/i.test(found.idle.reply),
       `idle probe resurfaces unresolved thought: ${found.idle.reply}`);
  }

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- unresolved thread bookmarking active');
})().catch(e => { console.error(e); process.exit(2); });
