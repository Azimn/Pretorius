#!/usr/bin/env node
/* cold_open_memory_realism_test.js -- does cross-session memory feel human?
 *
 * This is intentionally experiential, not just mechanical persistence. It
 * plants a named interlocutor and a vulnerable personal disclosure, closes the
 * host, reopens cold with only "Good evening.", and checks whether the reply
 * carries continuity without database-ish memory phrasing.
 */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { resolveHost } = require('../host_path');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const SRC_CART = path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart');

function ok(cond, msg){
  if (!cond){
    console.error(`not ok: ${msg}`);
    process.exitCode = 1;
  } else {
    console.log(`ok:   ${msg}`);
  }
}

function makeTempCart(){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'persona-cold-open-'));
  const cart = path.join(dir, 'pretorius.cart');
  fs.copyFileSync(SRC_CART, cart);
  return { dir, cart };
}

function runSession(cart, commands, clockMs){
  return new Promise((resolve, reject) => {
    const stdin = commands.map(c => JSON.stringify(c)).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [cart, '--stdio'], {
      env: {
        ...process.env,
        PE_RENDER_BACKEND: 'template',
        PE_TODAY_SEED: '0x4b494b49',
        PE_CLOCK_OVERRIDE_MS: String(clockMs),
      },
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf8'));
    proc.stderr.on('data', d => stderr += d.toString('utf8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) return reject(new Error(`host exited ${status}: ${stderr}`));
      resolve(stdout.split('\n').filter(l => l.startsWith('{')).map(l => JSON.parse(l)));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

function chatRows(rows){
  return rows.filter(r => Object.prototype.hasOwnProperty.call(r, 'reply'));
}

function hasAny(text, terms){
  const s = String(text || '').toLowerCase();
  return terms.some(t => s.includes(t));
}

function databaseish(text){
  return /i remember this:|you asked me to remember|someone said:|someone asked:|the visitor said:|the visitor asked:|\[(reflection|offscreen|insult|praise|question|intimacy)\]/i.test(text || '');
}

function assistantish(text){
  return /\bas an ai\b|\bi'?m here to help\b|\blet me know\b|\bhow can i assist\b/i.test(text || '');
}

function tooCompliant(text){
  return /\bof course\b|\bsure\b|\babsolutely\b|\bhappy to\b|\bright away\b|\bas you wish\b|\bat your service\b/i.test(text || '');
}

(async function main(){
  console.log('--- cold-open memory realism test ---');
  const tmp = makeTempCart();
  const t0 = 1700000000000;
  try {
    const session1 = await runSession(tmp.cart, [
      { method: 'set_user', user_id: 'Kiki' },
      { method: 'chat', text: "Hi Doctor Pretorius. I'm Kiki. I sound sparkly, but I understand modern code and physics. I get scared people only hear the slang and miss the mind underneath." },
      { method: 'chat', text: "Your homunculi are disturbing, but I cannot stop thinking about them. Forbidden science with shoulder pads, basically." },
      { method: 'chat', text: "I should go for now. Remember me as Kiki, not babe, not some random visitor. Kiki." },
    ], t0);

    const session2 = await runSession(tmp.cart, [
      { method: 'set_user', user_id: 'Kiki' },
      { method: 'chat', text: 'Good evening.' },
      { method: 'chat', text: "Doctor, I'm interrupting you. Stop the lecture and answer quickly." },
      { method: 'state' },
    ], t0 + 3 * 60 * 60 * 1000);

    const s1Chats = chatRows(session1);
    const s2Chats = chatRows(session2);
    const cold = s2Chats[0] || {};
    const interrupt = s2Chats[1] || {};
    const reply = cold.reply || '';
    const interruptReply = interrupt.reply || '';
    const state = cold.state || session2.find(r => r.character) || {};

    ok(s1Chats.length === 3, 'session one produced three replies');
    ok(s2Chats.length === 2, 'cold open session produced two replies');
    ok(typeof reply === 'string' && reply.length > 0, `cold open is nonempty: ${reply}`);
    ok(!databaseish(reply), `cold open avoids database-ish memory labels: ${reply}`);
    ok(!assistantish(reply), `cold open avoids assistant phrasing: ${reply}`);
    ok(!/happy to help|of course|absolutely/i.test(reply),
       `Pretorius does not become a warm assistant on cold open: ${reply}`);
    ok(interruptReply && !assistantish(interruptReply) && !tooCompliant(interruptReply),
       `Pretorius does not become subordinate when interrupted: ${interruptReply}`);
    ok(state.actor_tagged_memories >= 1 || state.speech_event_count >= 1,
       `symbolic memory/ledger exists after planted session (actor_tags=${state.actor_tagged_memories}, speech_events=${state.speech_event_count})`);

    const continuityTerms = [
      'kiki', 'sparkly', 'slang', 'mind', 'code', 'physics',
      'homunculi', 'disturbing', 'forbidden', 'shoulder pads'
    ];
    const hasContinuity = hasAny(reply, continuityTerms);
    ok(hasContinuity,
       `cold open should reference Kiki or a planted thread without being prompted: ${reply}`);

    console.log('cold_open_reply:', reply);
    console.log('interrupt_reply:', interruptReply);
  } finally {
    fs.rmSync(tmp.dir, { recursive: true, force: true });
  }
  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- cold-open memory feels surfaced, not queried');
})().catch(e => { console.error(e); process.exit(2); });
