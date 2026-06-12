#!/usr/bin/env node
/* api_provider_test.js - verifies optional OpenAI-compatible API renderer.
 *
 * The mock is local HTTP, not a real cloud call. This protects the V7
 * frontier-renderer seam while preserving the rule that tests and normal
 * runtime need no account, internet, or model download.
 */
const net = require('net');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const { resolveHost } = require('../host_path');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const CART = path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.join(ROOT, 'profiles', 'pretorius');
const MARKER = 'MOCK-API-REPLY';

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

function makeMock(){
  const requests = [];
  const server = net.createServer(sock => {
    let chunks = [];
    let totalBytes = 0;
    sock.on('data', d => {
      chunks.push(d); totalBytes += d.length;
      const buf = Buffer.concat(chunks, totalBytes);
      const split = buf.indexOf('\r\n\r\n');
      if (split < 0) return;
      const head = buf.slice(0, split).toString('utf-8');
      const body = buf.slice(split + 4);
      const cl = head.match(/Content-Length:\s*(\d+)/i);
      if (!cl) return;
      const exp = parseInt(cl[1], 10);
      if (body.length < exp) return;
      let req;
      try { req = JSON.parse(body.slice(0, exp).toString('utf-8')); }
      catch (e){ console.error('[api mock] JSON parse fail:', e.message); return; }
      requests.push(req);
      const prompt = req.messages && req.messages[1] && req.messages[1].content || '';
      const hasFrame = prompt.includes('[TASK]') && prompt.includes('[WORLD]');
      const reply = `${MARKER}-${requests.length}${hasFrame ? '-grounded' : '-missing-frame'}?`;
      const respBody = JSON.stringify({
        id: 'chatcmpl-mock',
        object: 'chat.completion',
        choices: [{ index: 0, message: { role: 'assistant', content: reply }, finish_reason: 'stop' }]
      });
      sock.write(
        `HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n` +
        `Content-Length: ${Buffer.byteLength(respBody)}\r\nConnection: close\r\n\r\n` +
        respBody
      );
      sock.end();
    });
  });
  return { server, requests };
}

function runHost(port){
  return new Promise((resolve, reject) => {
    const env = Object.assign({}, process.env, {
      PE_RENDER_BACKEND: 'slm',
      PE_SLM_PROVIDER: 'api',
      PE_SLM_MODEL: 'mock-frontier',
      PE_API_URL: `http://127.0.0.1:${port}/v1/chat/completions`,
      PE_API_KEY: 'test-key',
      PE_API_MODEL: 'mock-frontier',
      PE_API_TIMEOUT_MS: '5000',
      PE_API_MAX_TOKENS: '128',
      PE_TODAY_SEED: '42',
    });
    const proc = spawn(HOST, [CART, '--stdio'], { env });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => resolve({ stdout, stderr, status: code }));
    proc.stdin.write([
      '{"method":"chat","text":"Good evening, doctor."}',
      '{"method":"chat","text":"Who are you?"}',
      '{"method":"chat","text":"Tell me about your work."}',
      '{"method":"close"}',
      ''
    ].join('\n'));
    setTimeout(() => proc.kill('SIGKILL'), 15000);
  });
}

async function main(){
  const mock = makeMock();
  await new Promise(r => mock.server.listen(0, '127.0.0.1', r));
  const port = mock.server.address().port;
  wipeState();
  const res = await runHost(port);
  mock.server.close();

  let fail = 0;
  if (res.status !== 0){
    console.error('persona_host exited', res.status);
    console.error(res.stderr);
    process.exit(1);
  }
  if (mock.requests.length !== 3){
    console.error(`FAIL: expected 3 API requests, got ${mock.requests.length}`);
    ++fail;
  } else {
    console.log('ok:   API provider dispatched 3 local mock requests');
  }
  const replies = res.stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => JSON.parse(l).reply);
  replies.forEach((r, i) => {
    if (r.includes(MARKER) && r.includes('grounded')){
      console.log(`ok:   reply ${i+1} uses API output and received grounding packet`);
    } else {
      console.error(`FAIL: reply ${i+1} missing API marker/grounding: ${r}`);
      ++fail;
    }
  });
  if (fail){
    console.error(`FAILED - ${fail} check(s)`);
    process.exit(1);
  }
  console.log('PASSED - optional API provider dispatch');
}

main().catch(e => { console.error(e); process.exit(2); });
