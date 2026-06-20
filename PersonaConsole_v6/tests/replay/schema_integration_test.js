#!/usr/bin/env node
/* schema_integration_test.js — V4 priority 3 proof.
 *
 * Drives persona_host through a fixed scripted arc:
 *   3 insults in a row → SCHEMA_USER_HOSTILE should rise nonlinearly
 *   (each consecutive insult contributes less due to habituation +
 *    hysteresis as the slot approaches saturation).
 *
 * Then verifies schemas persist across session restarts (saved to
 * <relations_dir>/<hash>.schema, reloaded on next pe_load_relation).
 */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn, execSync } = require('child_process');

const HOST  = resolveHost(path.join(__dirname, '..', '..'));
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.join(__dirname, '..', '..', 'profiles', 'pretorius');

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  try { execSync(`rm -rf ${path.join(CHDIR, 'relations')} ${path.join(CHDIR, 'aether')}`); } catch {}
}

function runHost(scriptLines){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, '--stdio'], { env: process.env });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => resolve({ stdout, stderr, status: code }));
    proc.stdin.write(scriptLines.join('\n') + '\n');
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 10000).unref();
  });
}

function parseSchemas(stdout){
  return stdout.split('\n')
    .filter(l => l.startsWith('{"reply"') || l.startsWith('{"name"'))
    .map(l => {
      const o = JSON.parse(l);
      const s = o.state || o;
      return s.schema || null;
    })
    .filter(Boolean);
}

async function main(){
  let fail = 0;
  /* Use insult words present in Pretorius's PatternTable so input_class
   * resolves to 2 (insult) and the schema event fires.  Engine cannot
   * react to substrings the cartridge author didn't declare — that's
   * cartridge-borne, not engine-borne.  These three are all in
   * tools/compile_pretorius.c. */
  const INSULT_SCRIPT = [
    '{"method":"chat","text":"You are a fool."}',
    '{"method":"chat","text":"I hate you."}',
    '{"method":"chat","text":"You are a madman."}',
    '{"method":"state"}',
    '{"method":"close"}',
  ];

  console.log('--- V4 schema integration ---');
  wipeState();
  const r1 = await runHost(INSULT_SCRIPT);
  if (r1.status !== 0){
    console.error('persona_host exited', r1.status);
    console.error(r1.stderr);
    process.exit(1);
  }
  const schemas = parseSchemas(r1.stdout);

  if (schemas.length < 3){
    console.error(`FAIL: expected at least 3 schema readings, got ${schemas.length}`);
    process.exit(1);
  }

  console.log(`hostile schema progression: ${schemas.map(s => s.hostile).join(' → ')}`);

  if (schemas[0].hostile <= 0){
    console.error(`FAIL: first insult did not raise hostility (got ${schemas[0].hostile})`); ++fail;
  } else {
    console.log(`ok:   first insult raised hostility (${schemas[0].hostile})`);
  }
  if (schemas[2].hostile <= schemas[0].hostile){
    console.error(`FAIL: third insult should accumulate (got ${schemas[2].hostile} vs ${schemas[0].hostile})`); ++fail;
  } else {
    console.log(`ok:   hostility accumulates across insults (${schemas[0].hostile} → ${schemas[2].hostile})`);
  }
  /* habituation: each successive delta should be smaller than the previous */
  const d1 = schemas[1].hostile - schemas[0].hostile;
  const d2 = schemas[2].hostile - schemas[1].hostile;
  if (d2 < d1){
    console.log(`ok:   nonlinear decay — delta shrinks (Δ1=${d1}, Δ2=${d2})`);
  } else {
    console.error(`FAIL: expected diminishing deltas, got Δ1=${d1} Δ2=${d2}`); ++fail;
  }

  /* persistence: restart session, schema should still be there */
  console.log('--- persistence: restart session, read state ---');
  const r2 = await runHost(['{"method":"state"}', '{"method":"close"}']);
  if (r2.status !== 0){
    console.error('persona_host exited on restart', r2.status); process.exit(1);
  }
  const schemas2 = parseSchemas(r2.stdout);
  if (!schemas2.length){
    console.error('FAIL: no schema in restart state'); ++fail;
  } else if (schemas2[0].hostile <= 0){
    console.error(`FAIL: schema did not persist across restart (got ${schemas2[0].hostile})`); ++fail;
  } else {
    console.log(`ok:   schema survives restart (hostile=${schemas2[0].hostile})`);
  }

  /* schema file present on disk */
  const schemaFiles = fs.existsSync(path.join(CHDIR, 'relations'))
    ? fs.readdirSync(path.join(CHDIR, 'relations')).filter(f => f.endsWith('.schema'))
    : [];
  if (schemaFiles.length > 0){
    console.log(`ok:   schema persisted to disk: ${schemaFiles.join(', ')}`);
  } else {
    console.error('FAIL: no .schema file written'); ++fail;
  }

  if (fail){
    console.error(`FAILED — ${fail} check(s)`);
    process.exit(1);
  }
  console.log('PASSED — schema events accumulate, decay, and persist');
}

main().catch(e => { console.error(e); process.exit(2); });
