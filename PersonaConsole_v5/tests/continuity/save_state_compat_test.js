#!/usr/bin/env node
/* save_state_compat_test.js -- V5 runtime state survives restart. */
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = path.join(__dirname, '..', '..', 'build', 'persona_host');
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

function run(commands, extraEnv = {}){
  return new Promise((resolve, reject) => {
    const stdin = commands.map(c => JSON.stringify(c)).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '0x5A7E', ...extraEnv }
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

function backdateRelation(days){
  const bins = fs.readdirSync(RELDIR).filter(f => f.endsWith('.bin'));
  if (bins.length < 1) throw new Error('expected at least one relation bin');
  const file = path.join(RELDIR, bins[0]);
  const buf = fs.readFileSync(file);
  const now = Math.floor(Date.now() / 1000);
  buf.writeUInt32LE(now - days * 86400, 8);
  buf.writeUInt32LE(now - days * 86400, 12);
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

function lastState(rows){
  for (let i = rows.length - 1; i >= 0; i--){
    if (rows[i] && typeof rows[i].turn_count === 'number') return rows[i];
    if (rows[i] && rows[i].state && typeof rows[i].state.turn_count === 'number') return rows[i].state;
  }
  return null;
}

(async function main(){
  console.log('--- save-state compatibility test ---');
  wipeState();

  await run([{ method: 'chat', text: 'Good evening, doctor.' }]);
  backdateRelation(2);

  const commands = [
    { method: 'chat', text: 'I have returned.' },
  ];
  for (let i = 0; i < 8; i++){
    commands.push({ method: 'chat', text: `fragment ${i} with loose conclusion` });
  }
  commands.push({ method: 'state' });

  const rows = await run(commands, { PE_FORCE_UNRESOLVED_THREAD: '1' });
  const before = lastState(rows);
  const statePath = path.join(CHDIR, 'state.bin');
  ok(fs.existsSync(statePath), 'state.bin exists after proactive/offscreen session');
  ok(fs.statSync(statePath).size >= 1024,
     `state.bin keeps full runtime payload (${fs.statSync(statePath).size} bytes)`);
  ok(before && before.turn_count >= 9,
     `turn_count advanced before restart (${before && before.turn_count})`);
  ok(before && Array.isArray(before.want_ages) && before.want_ages.some(v => v >= 24),
     `offscreen want ages saved before restart (${before && before.want_ages})`);
  ok(before && before.unresolved_count > 0,
     `unresolved thread count saved before restart (${before && before.unresolved_count})`);
  ok(before && typeof before.turns_since_question === 'number',
     `conversation rhythm counter exposed before restart (${before && before.turns_since_question})`);

  const reopenedRows = await run([{ method: 'state' }]);
  const after = lastState(reopenedRows);
  ok(after && after.turn_count === before.turn_count,
     `turn_count survives restart (${before && before.turn_count} -> ${after && after.turn_count})`);
  ok(after && JSON.stringify(after.want_ages) === JSON.stringify(before.want_ages),
     `want ages survive restart (${before && before.want_ages} -> ${after && after.want_ages})`);
  ok(after && after.unresolved_count === before.unresolved_count,
     `unresolved_count survives restart (${before && before.unresolved_count} -> ${after && after.unresolved_count})`);
  ok(after && after.turns_since_question === before.turns_since_question,
     `turns_since_question survives restart (${before && before.turns_since_question} -> ${after && after.turns_since_question})`);
  ok(after && after.last_reply_had_question === before.last_reply_had_question,
     `last_reply_had_question survives restart (${before && before.last_reply_had_question} -> ${after && after.last_reply_had_question})`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- V5 save-state fields persist across restart');
})().catch(e => { console.error(e); process.exit(2); });
