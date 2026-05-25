#!/usr/bin/env node
/* relation_layout_compat_test.js -- relation save layout stays compatible. */
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);
const RELDIR = path.join(CHDIR, 'relations');
const USER_ID = 'LayoutCompatUser';

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
      env: { ...process.env, PE_TODAY_SEED: '0xC0A7' }
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    const killTimer = setTimeout(() => proc.kill('SIGKILL'), 30000);
    proc.on('close', status => {
      clearTimeout(killTimer);
      if (status !== 0) reject(new Error(`host exited ${status}: ${stderr}`));
      else resolve(stdout.split('\n').filter(Boolean).map(l => JSON.parse(l)));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
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

function inspectRelation(file){
  const buf = fs.readFileSync(file);
  return {
    file,
    buf,
    user_hash: buf.readUInt32LE(0),
    disposition: buf.readInt16LE(4),
    tags: buf.readUInt8(6),
    last_contact: buf.readUInt32LE(8),
    first_contact: buf.readUInt32LE(12),
    known_as: buf.toString('utf8', 32).replace(/\0.*$/s, '')
  };
}

function relationBinFor(userId){
  const bins = fs.readdirSync(RELDIR).filter(f => f.endsWith('.bin'));
  const rels = bins.map(f => inspectRelation(path.join(RELDIR, f)));
  const rel = rels.find(r => r.known_as.includes(userId));
  if (!rel){
    const names = rels.map(r => `${path.basename(r.file)}:${r.known_as}`).join(', ');
    throw new Error(`could not find relation for ${userId}; saw ${names}`);
  }
  return rel.file;
}

(async function main(){
  console.log('--- relation layout compatibility test ---');
  wipeState();

  await run([
    { method: 'set_user', user_id: USER_ID },
    { method: 'chat', text: 'Good evening, doctor.' }
  ]);

  const file = relationBinFor(USER_ID);
  const schemaFile = file.replace(/\.bin$/, '.schema');
  const rel = inspectRelation(file);
  ok(rel.buf.length >= 80, `relation file keeps full V5 payload (${rel.buf.length} bytes)`);
  ok(rel.user_hash !== 0, `user_hash lives at byte 0 (${rel.user_hash})`);
  ok(rel.disposition >= -1000 && rel.disposition <= 1000,
     `disposition lives at byte 4 (${rel.disposition})`);
  ok(rel.tags >= 0, `tags live at byte 6 (${rel.tags})`);
  ok(rel.first_contact > 0 && rel.last_contact > 0 && rel.first_contact <= rel.last_contact,
     `timestamps live at bytes 8/12 (${rel.last_contact}/${rel.first_contact})`);
  ok(rel.known_as.includes(USER_ID), `known_as survives in relation payload (${rel.known_as})`);
  ok(fs.existsSync(schemaFile), 'schema sidecar persists beside relation bin');

  const backdated = Buffer.from(rel.buf);
  const twoDays = 2 * 86400;
  backdated.writeUInt32LE(Math.max(1, rel.last_contact - twoDays), 8);
  backdated.writeUInt32LE(Math.max(1, rel.first_contact - twoDays), 12);
  fs.writeFileSync(file, backdated);

  const rows = await run([
    { method: 'set_user', user_id: USER_ID },
    { method: 'chat', text: 'I have returned.' },
    { method: 'state' }
  ]);
  const reply = rows.find(r => typeof r.reply === 'string');
  const state = rows.find(r => Array.isArray(r.want_ages));
  const reread = inspectRelation(file);

  ok(reply && /absence|returned|occupied|week|day/i.test(reply.reply),
     `backdated relation triggers return-aware reply: ${reply && reply.reply}`);
  ok(state && state.want_ages.some(v => v >= 24),
     `backdated relation advances offscreen want ages (${state && state.want_ages})`);
  ok(reread.last_contact >= rel.last_contact,
     `last_contact at byte 8 refreshes after resumed chat (${reread.last_contact})`);
  ok(reread.first_contact <= reread.last_contact,
     `first_contact at byte 12 remains plausible (${reread.first_contact})`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- relation layout/backdate compatibility intact');
})().catch(e => { console.error(e); process.exit(2); });
