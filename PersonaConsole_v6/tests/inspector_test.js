#!/usr/bin/env node
/* inspector_test.js -- smoke-tests the standalone browser Cartridge Inspector. */
const fs = require('fs');
const path = require('path');

const INSPECTOR = path.join(__dirname, '..', 'CartridgeInspector', 'cartridge_inspector.html');
const PRETORIUS = path.join(__dirname, '..', 'profiles', 'pretorius', 'pretorius.cart');
const TMPDIR = path.join(__dirname, 'tmp');
const LEGACY = path.join(TMPDIR, 'inspector_legacy_identity.cart');

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
  const src = fs.readFileSync(PRETORIUS);
  const entryCount = src.readUInt16LE(6);
  const headerSize = src.readUInt32LE(8);
  const payloadSize = src.readUInt32LE(12);
  const payload = src.subarray(headerSize, headerSize + payloadSize);
  const sections = [];
  for (let i = 0; i < entryCount && i < PE_CART_MAX_SECTIONS; i++){
    const eoff = HEADER_FIXED + i * ENTRY_SIZE;
    const name = readName(src, eoff);
    const off = src.readUInt32LE(eoff + 48);
    const size = src.readUInt32LE(eoff + 52);
    let data = Buffer.from(payload.subarray(off, off + size));
    if (name === 'identity.bin') data = data.subarray(0, LEGACY_IDENTITY_SIZE);
    sections.push({ name, data });
  }
  const header = Buffer.alloc(headerSize);
  const totalPayload = sections.reduce((n, s) => n + s.data.length, 0);
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
  fs.writeFileSync(LEGACY, Buffer.concat([header, outPayload]));
}

function loadInspector(){
  const html = fs.readFileSync(INSPECTOR, 'utf8');
  const m = html.match(/<script>([\s\S]*?)<\/script>/);
  if (!m) throw new Error('no inspector script tag found');
  const stub = {
    classList: { add(){}, remove(){} },
    addEventListener(){},
    querySelector(){ return stub; },
    get innerHTML(){ return ''; }, set innerHTML(v){},
    get className(){ return ''; }, set className(v){},
    get value(){ return ''; }, set value(v){},
  };
  global.document = { querySelector(){ return stub; } };
  global.FileReader = class {};
  const moduleObj = { exports: {} };
  const js = m[1] + '\n;module.exports = { lint };';
  new Function('module', js).call(global, moduleObj);
  return moduleObj.exports.lint;
}

function ok(cond, msg){
  if (!cond){
    console.error('not ok:', msg);
    process.exitCode = 1;
  } else {
    console.log('ok:  ', msg);
  }
}

console.log('--- cartridge inspector test ---');
makeLegacyCart();
const lint = loadInspector();

const v5 = lint(fs.readFileSync(PRETORIUS).buffer);
ok(!v5.fatal, 'Pretorius cart parses');
ok(v5.is_v5, 'Pretorius reports V5 identity');
ok(v5.errors === 0, 'Pretorius has no inspector errors');
ok(v5.stats.identity_format === 'V5', 'V5 stats identify identity format');
ok(v5.coverage && v5.coverage.Wants && v5.coverage.Preoccupations &&
   v5.coverage['Resumption lines'] && v5.coverage.Milestones,
   'V5 proactivity coverage is present');

const legacy = lint(fs.readFileSync(LEGACY).buffer);
ok(!legacy.fatal, 'legacy cart parses');
ok(!legacy.is_v5, 'legacy cart reports non-V5 identity');
ok(legacy.stats.identity_format === 'V4', 'legacy stats identify V4 identity');
ok(legacy.findings.some(f => /V4-era cartridge/.test(f.msg)), 'legacy cart explains skipped V5 checks');

const corrupt = Buffer.from(fs.readFileSync(PRETORIUS));
corrupt[corrupt.length - 1] ^= 0xff;
const bad = lint(corrupt.buffer);
ok(bad.errors > 0 && bad.findings.some(f => /integrity/i.test(f.msg)), 'corrupt cart reports integrity error');

if (process.exitCode) process.exit(process.exitCode);
console.log('PASSED -- Cartridge Inspector parses V5, V4, and corrupt carts');
