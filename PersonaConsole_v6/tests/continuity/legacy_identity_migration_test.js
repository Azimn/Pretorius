#!/usr/bin/env node
/* legacy_identity_migration_test.js -- V4-sized identity sections load in V5. */
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = path.join(__dirname, '..', '..', 'build', 'persona_host');
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const TMPDIR = path.join(__dirname, '..', 'tmp');
const OUT = path.join(TMPDIR, 'legacy_identity.cart');
const PE_CART_MAGIC = 0x54524143;
const PE_CART_VERSION_V4 = 1;
const PE_CART_MAX_SECTIONS = 32;
const PE_CART_NAME_LEN = 48;
const ENTRY_SIZE = 60;
const HEADER_FIXED = 32;
const LEGACY_IDENTITY_SIZE = 3092;

function checksum(buf){
  let h = 2166136261 >>> 0;
  for (const b of buf){
    h ^= b;
    h = Math.imul(h, 16777619) >>> 0;
  }
  return h || 1;
}

function readName(buf, off){
  const end = buf.indexOf(0, off);
  return buf.subarray(off, end >= 0 && end < off + PE_CART_NAME_LEN ? end : off + PE_CART_NAME_LEN).toString('utf8');
}

function makeLegacyCart(){
  fs.mkdirSync(TMPDIR, { recursive: true });
  const src = fs.readFileSync(CART);
  const magic = src.readUInt32LE(0);
  const entryCount = src.readUInt16LE(6);
  const headerSize = src.readUInt32LE(8);
  const payloadSize = src.readUInt32LE(12);
  if (magic !== PE_CART_MAGIC) throw new Error('source cart magic mismatch');
  if (entryCount > PE_CART_MAX_SECTIONS) throw new Error('too many cart sections');
  const payload = src.subarray(headerSize, headerSize + payloadSize);
  const sections = [];
  for (let i = 0; i < entryCount; i++){
    const eoff = HEADER_FIXED + i * ENTRY_SIZE;
    const name = readName(src, eoff);
    const off = src.readUInt32LE(eoff + 48);
    const size = src.readUInt32LE(eoff + 52);
    let data = Buffer.from(payload.subarray(off, off + size));
    if (name === 'identity.bin'){
      if (data.length <= LEGACY_IDENTITY_SIZE) throw new Error(`identity already legacy-sized (${data.length})`);
      data = data.subarray(0, LEGACY_IDENTITY_SIZE);
    }
    sections.push({ name, data });
  }
  if (!sections.some(s => s.name === 'identity.bin' && s.data.length === LEGACY_IDENTITY_SIZE))
    throw new Error('failed to derive legacy identity section');

  const header = Buffer.alloc(headerSize);
  let totalPayload = 0;
  for (const s of sections) totalPayload += s.data.length;
  const outPayload = Buffer.alloc(totalPayload);
  header.writeUInt32LE(PE_CART_MAGIC, 0);
  header.writeUInt16LE(PE_CART_VERSION_V4, 4);
  header.writeUInt16LE(sections.length, 6);
  header.writeUInt32LE(headerSize, 8);
  header.writeUInt32LE(totalPayload, 12);
  let cursor = 0;
  for (let i = 0; i < sections.length; i++){
    const s = sections[i];
    const eoff = HEADER_FIXED + i * ENTRY_SIZE;
    header.write(s.name, eoff, PE_CART_NAME_LEN, 'utf8');
    header.writeUInt32LE(cursor, eoff + 48);
    header.writeUInt32LE(s.data.length, eoff + 52);
    header.writeUInt32LE(checksum(s.data), eoff + 56);
    s.data.copy(outPayload, cursor);
    cursor += s.data.length;
  }
  header.writeUInt32LE(checksum(outPayload), 16);
  fs.writeFileSync(OUT, Buffer.concat([header, outPayload]));
}

function wipeTempState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin']){
    try { fs.unlinkSync(path.join(TMPDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(TMPDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(){
  return new Promise((resolve, reject) => {
    const stdin = [
      { method: 'chat', text: 'Good evening, doctor.' },
      { method: 'state' }
    ].map(JSON.stringify).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [OUT, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '0x515151' }
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
  console.log('--- legacy identity migration test ---');
  makeLegacyCart();
  wipeTempState();
  const rows = await run();
  const reply = rows.find(r => typeof r.reply === 'string');
  const state = rows.find(r => r.name);
  ok(reply && reply.reply.length > 0, `legacy identity cart produces a reply: ${reply && reply.reply}`);
  ok(state && state.name === 'Dr. Septimus Pretorius',
     `legacy identity preserves character name: ${state && state.name}`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- legacy identity migration safe');
})().catch(e => { console.error(e); process.exit(2); });
