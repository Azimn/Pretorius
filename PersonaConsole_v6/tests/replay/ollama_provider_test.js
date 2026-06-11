#!/usr/bin/env node
/* ollama_provider_test.js — proves the SLM backend dispatches to an
 * Ollama-compatible endpoint and that the result wins over the
 * template path when it passes the engine's render audit.
 *
 * Strategy: spawn a tiny HTTP/1.1 mock that speaks Ollama's
 * /api/generate contract.  The mock echoes back a deterministic
 * marker plus the seed it received, so we can verify:
 *   - the engine reached the provider
 *   - the per-turn seed propagated (determinism)
 *   - the engine USED the provider's audit-compatible reply instead of
 *     repairing/falling back to the template
 *
 * Implementation note: persona_host must run under async spawn — a
 * spawnSync blocks node's event loop and the mock TCP server can
 * never accept the engine's connection.
 */
const net = require('net');
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST  = resolveHost(path.join(__dirname, '..', '..'));
const CART  = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.join(__dirname, '..', '..', 'profiles', 'pretorius');
const MARKER = 'MOCK-OLLAMA-REPLY';

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }

function wipeState(){
  for (const f of fs.readdirSync(CHDIR)){
    if (/\.schema$/.test(f)) {
      try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
    }
  }
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin',
                   'reflections.bin', 'actor_index.bin',
                   'speech_events.bin', 'dissonance.bin',
                   'open_loops.bin', 'speech_habits.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

/* Build an Ollama-mock TCP server.  Records every seed it sees. */
function makeMock(){
  const seedsSeen = [];
  const server = net.createServer(sock => {
    let chunks = [];
    let totalBytes = 0;
    sock.on('data', d => {
      chunks.push(d);
      totalBytes += d.length;
      /* Buffer-mode is mandatory: string concat would UTF-8 decode and
       * any em-dash in the prompt would skew offsets vs Content-Length. */
      const buf = Buffer.concat(chunks, totalBytes);
      const split = buf.indexOf('\r\n\r\n');
      if (split < 0) return;
      const head = buf.slice(0, split).toString('utf-8');
      const body = buf.slice(split + 4);
      const cl = head.match(/Content-Length:\s*(\d+)/i);
      if (!cl) return;
      const exp = parseInt(cl[1], 10);
      if (body.length < exp) return;     /* body.length is byte count on Buffer */
      let req;
      try { req = JSON.parse(body.slice(0, exp).toString('utf-8')); }
      catch (e){ console.error('[mock] JSON parse fail:', e.message); return; }
      seedsSeen.push(req.options && req.options.seed);
      /* The V6 render audit may require question realization on a turn.
       * Keep the marker, but make the mock text question-compatible so this
       * provider test does not accidentally become an audit-fallback test. */
      const replyText = `${MARKER}-seed${req.options.seed}?`;
      const respBody = JSON.stringify({
        model: req.model, response: replyText, done: true,
      });
      sock.write(
        `HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n` +
        `Content-Length: ${Buffer.byteLength(respBody)}\r\nConnection: close\r\n\r\n` +
        respBody
      );
      sock.end();
    });
  });
  return { server, seedsSeen };
}

/* Run persona_host async with stdin script.  Returns Promise<{stdout, stderr, status}>. */
function runHost(port, scriptLines){
  return new Promise((resolve, reject) => {
    const env = Object.assign({}, process.env, {
      PE_RENDER_BACKEND: 'slm',
      PE_SLM_PROVIDER:   'ollama',
      PE_SLM_MODEL:      'mock-model',
      PE_OLLAMA_HOST:    '127.0.0.1',
      PE_OLLAMA_PORT:    String(port),
      PE_OLLAMA_TIMEOUT_MS: '3000',
    });
    const proc = spawn(HOST, [CART, '--stdio'], { env });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => resolve({ stdout, stderr, status: code }));
    /* feed the input script then close stdin to signal EOF */
    proc.stdin.write(scriptLines.join('\n') + '\n');
    proc.stdin.end();
    /* hard timeout */
    setTimeout(() => proc.kill('SIGKILL'), 15000);
  });
}

async function main(){
  let fail = 0;
  const SCRIPT = [
    '{"method":"chat","text":"Good evening, doctor."}',
    '{"method":"chat","text":"Who are you?"}',
    '{"method":"chat","text":"Tell me about your work."}',
    '{"method":"close"}',
  ];

  /* ----- Run 1: dispatch correctness ----- */
  const m1 = makeMock();
  await new Promise(r => m1.server.listen(0, '127.0.0.1', r));
  const port1 = m1.server.address().port;
  console.log(`--- mock Ollama listening on 127.0.0.1:${port1}`);

  wipeState();
  const res1 = await runHost(port1, SCRIPT);
  m1.server.close();

  if (res1.status !== 0){
    console.error('persona_host exited', res1.status);
    console.error(res1.stderr);
    process.exit(1);
  }

  const replies1 = res1.stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => JSON.parse(l).reply);

  console.log(`requests received by mock: ${m1.seedsSeen.length}`);
  if (m1.seedsSeen.length !== 3){
    console.error(`FAIL: expected 3 mock requests, got ${m1.seedsSeen.length}`); ++fail;
    console.error('  stderr:', res1.stderr.split('\n').slice(0,8).join('\n  '));
  } else {
    console.log('ok:   engine dispatched 3 generations to the provider');
  }
  for (let i = 0; i < replies1.length; ++i){
    if (replies1[i].includes(MARKER)){
      console.log(`ok:   reply ${i+1} uses provider output: "${replies1[i].slice(0,60)}…"`);
    } else {
      console.error(`FAIL: reply ${i+1} missing marker: "${replies1[i]}"`);
      ++fail;
    }
  }

  /* ----- Run 2 + 3: seed determinism ----- */
  const m2 = makeMock();
  await new Promise(r => m2.server.listen(0, '127.0.0.1', r));
  const port2 = m2.server.address().port;
  wipeState();
  await runHost(port2, SCRIPT);
  const seedsRun1 = [...m2.seedsSeen];
  m2.seedsSeen.length = 0;

  wipeState();
  await runHost(port2, SCRIPT);
  const seedsRun2 = [...m2.seedsSeen];
  m2.server.close();

  const sameSeeds = seedsRun1.length === seedsRun2.length
                 && seedsRun1.every((s, i) => s === seedsRun2[i]);
  if (sameSeeds && seedsRun1.length === 3){
    console.log(`ok:   per-turn seeds reproducible across replays: ${seedsRun1.join(',')}`);
  } else {
    console.error(`FAIL: seeds diverged. run1=${seedsRun1} run2=${seedsRun2}`);
    ++fail;
  }

  if (fail){
    console.error(`FAILED — ${fail} check(s)`);
    process.exit(1);
  }
  console.log('PASSED — Ollama provider dispatch + seed determinism');
}

main().catch(e => { console.error(e); process.exit(2); });
