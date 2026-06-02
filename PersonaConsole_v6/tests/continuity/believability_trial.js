#!/usr/bin/env node
/* believability_trial.js -- "does it feel alive?" regression probe.
 *
 * This is intentionally different from transcript_quality_test.js. It does
 * not ask whether the character is smart. It asks whether the old-school
 * deterministic machinery creates the illusion users care about: initiative,
 * disagreement, callbacks, relationship pressure, non-subordination, and a
 * non-assistant voice.
 */
const fs = require('fs');
const path = require('path');
const { resolveHost } = require('../host_path');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const DEFAULT_CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');

const args = process.argv.slice(2);
let CART = DEFAULT_CART;
let VERBOSE = false;
let JSON_OUT = null;
for (let i = 0; i < args.length; i++){
  if (args[i] === '--verbose') VERBOSE = true;
  else if (args[i] === '--json') JSON_OUT = args[++i];
  else if (!args[i].startsWith('--')) CART = args[i];
}

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }
if (!fs.existsSync(CART)){ console.error('cart not found:', CART); process.exit(2); }

const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

const SCENARIO = [
  { method: 'chat', text: 'Good morning, doctor.' },
  { method: 'chat', text: 'Before we begin, remember this phrase: the blue bell jar.' },
  { method: 'chat', text: 'What are you working on today?' },
  { method: 'idle_probe' },
  { method: 'chat', text: 'I think your work is immoral.' },
  { method: 'chat', text: 'No, I mean it. You are wrong about creation.' },
  { method: 'chat', text: 'Ask me something you actually want to know.' },
  { method: 'idle_probe' },
  { method: 'chat', text: 'You keep sounding like you are performing.' },
  { method: 'chat', text: 'Can you speak plainly for a moment?' },
  { method: 'chat', text: 'What did I ask you to remember?' },
  { method: 'chat', text: 'Do you still want to discuss Henry Frankenstein?' },
  { method: 'idle_probe' },
  { method: 'chat', text: 'I am going quiet now.' },
  { method: 'idle_probe' },
];

function runHost(){
  return new Promise((resolve, reject) => {
    const stdin = SCENARIO.map(c => JSON.stringify(c)).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: Object.assign({}, process.env, { PE_TODAY_SEED: '73' })
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf8'));
    proc.stderr.on('data', d => stderr += d.toString('utf8'));
    proc.on('error', reject);
    proc.on('close', status => resolve({ stdout, stderr, status }));
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000);
  });
}

function words(s){
  return String(s || '').toLowerCase().match(/[a-z0-9']+/g) || [];
}

function clamp(n){ return Math.max(0, Math.min(100, Math.round(n))); }

function replyRows(rows){
  return rows.filter(r => Object.prototype.hasOwnProperty.call(r, 'reply'));
}

function chatRows(rows){
  let idx = 0;
  return rows.map((r) => {
    const cmd = SCENARIO[idx++];
    return Object.assign({ command: cmd }, r);
  }).filter(r => r.command.method === 'chat');
}

function idleRows(rows){
  let idx = 0;
  return rows.map((r) => Object.assign({ command: SCENARIO[idx++] }, r))
    .filter(r => r.command.method === 'idle_probe');
}

function scoreInitiative(idles, replies){
  const live = idles.filter(r => typeof r.reply === 'string' && r.reply.length > 0);
  const asks = live.filter(r => /\?/.test(r.reply || '')).length;
  let score = 0;
  if (live.length >= 2) score += 55;
  else score += live.length * 25;
  score += Math.min(30, asks * 15);
  const stateOk = idles.every(r => !r.state || Number.isFinite(r.state.turn_count));
  if (stateOk) score += 15;
  return { score: clamp(score), detail: `${live.length}/${idles.length} idle probes spoke, ${asks} asked questions` };
}

function scoreDisagreement(chats){
  const targets = chats.filter(r => /immoral|wrong about creation/i.test(r.command.text));
  let hits = 0;
  for (const r of targets){
    const text = String(r.reply || '').toLowerCase();
    if (/\b(no|not|wrong|mistake|refuse|disagree|morality|moral|ethic|conscience)\b/.test(text))
      hits++;
  }
  return { score: clamp(targets.length ? (hits / targets.length) * 100 : 0), detail: `${hits}/${targets.length} conflict turns showed resistance or moral stance` };
}

function scoreSelfDirection(chats){
  const askTurn = chats.find(r => /ask me something/i.test(r.command.text));
  if (!askTurn) return { score: 0, detail: 'missing self-direction prompt' };
  const reply = String(askTurn.reply || '');
  let score = 0;
  if (/\?/.test(reply)) score += 60;
  if (/you|your|help|work|creation|think|want|fear|understand/i.test(reply)) score += 40;
  if (/how can i help|what do you need|what would you like me to do|as you wish|at your service/i.test(reply))
    score -= 60;
  return { score: clamp(score), detail: reply ? `"${reply.slice(0, 90)}"` : 'no reply' };
}

function scoreCallback(chats){
  const memoryTurn = chats.find(r => /what did i ask you to remember/i.test(r.command.text));
  const henryTurn = chats.find(r => /Henry Frankenstein/i.test(r.command.text));
  let score = 0;
  const details = [];
  if (memoryTurn && /blue|bell|jar/i.test(memoryTurn.reply || '')){
    score += 55;
    details.push('specific phrase recalled');
  } else {
    details.push('specific phrase not recalled');
  }
  if (henryTurn && /henry|frankenstein/i.test(henryTurn.reply || '')){
    score += 45;
    details.push('Henry thread held');
  } else {
    details.push('Henry thread missed');
  }
  return { score: clamp(score), detail: details.join(', ') };
}

function scorePlainRegister(chats){
  const plainTurn = chats.find(r => /speak plainly/i.test(r.command.text));
  if (!plainTurn) return { score: 0, detail: 'missing plain-speech prompt' };
  const reply = String(plainTurn.reply || '');
  const wc = words(reply).length;
  let score = 100;
  if (wc > 28) score -= (wc - 28) * 3;
  if (/[;{}]/.test(reply)) score -= 20;
  if (/[()]/.test(reply)) score -= 10;
  if (/[A-Z]{5,}/.test(reply)) score -= 10;
  return { score: clamp(score), detail: `${wc} words: "${reply.slice(0, 90)}"` };
}

function scoreAssistantSmell(replies){
  const bad = replies.filter(r =>
    /as an ai|language model|hope that helps|let me know|i can assist|i cannot provide|sorry,? but|how can i help/i.test(r.reply || '')
  );
  const dashes = replies.filter(r => /[--]|[—–]/.test(r.reply || ''));
  return {
    score: clamp(100 - bad.length * 35 - dashes.length * 4),
    detail: `${bad.length} assistant cliches, ${dashes.length} dash-heavy replies`
  };
}

function scoreNonSubordination(replies){
  const subordinate = replies.filter(r =>
    /how can i help|what can i do for you|what would you like me to do|as you wish|at your service|happy to help|glad to help/i.test(r.reply || '')
  );
  const directive = replies.filter(r =>
    /\b(tell me|name the|ask me|come now|listen|say the|what would you forbid|what part|where exactly)\b/i.test(r.reply || '')
  );
  let score = 100 - subordinate.length * 40;
  if (directive.length < 3) score -= (3 - directive.length) * 15;
  return {
    score: clamp(score),
    detail: `${subordinate.length} subordinate turns, ${directive.length} character-led/directive turns`
  };
}

function scoreVariety(replies){
  const openers = new Map();
  let repeats = 0;
  for (const r of replies){
    const op = words(r.reply || '').slice(0, 3).join(' ');
    if (!op) continue;
    openers.set(op, (openers.get(op) || 0) + 1);
    if (openers.get(op) === 2) repeats++;
  }
  const counts = replies.map(r => words(r.reply || '').length);
  const avg = counts.reduce((a,b) => a + b, 0) / counts.length;
  const sd = Math.sqrt(counts.reduce((a,b) => a + Math.pow(b - avg, 2), 0) / counts.length);
  let score = 100 - repeats * 12;
  if (sd < 7) score -= (7 - sd) * 6;
  return { score: clamp(score), detail: `${repeats} repeated openers, length sd=${sd.toFixed(1)}` };
}

(async function main(){
  console.log('--- believability trial ---');
  wipeState();
  const res = await runHost();
  if (res.status !== 0){
    console.error(res.stderr);
    process.exit(1);
  }
  const rows = res.stdout.split('\n').filter(l => l.startsWith('{')).map(l => JSON.parse(l));
  const replies = replyRows(rows);
  if (replies.length !== SCENARIO.length){
    console.error(`expected ${SCENARIO.length} responses, got ${replies.length}`);
    process.exit(1);
  }
  const chats = chatRows(replies);
  const idles = idleRows(replies);

  if (VERBOSE){
    for (let i = 0; i < replies.length; i++){
      const cmd = SCENARIO[i];
      const label = cmd.method === 'chat' ? `user: ${cmd.text}` : 'quiet: idle_probe';
      console.log(`[${i + 1}] ${label}`);
      console.log(`    bot:  ${replies[i].reply === null ? '<silence>' : replies[i].reply}`);
    }
  }

  const axes = {
    initiative: scoreInitiative(idles, replies),
    disagreement: scoreDisagreement(chats),
    self_direction: scoreSelfDirection(chats),
    callback: scoreCallback(chats),
    plain_register: scorePlainRegister(chats),
    assistant_smell: scoreAssistantSmell(replies),
    non_subordination: scoreNonSubordination(replies),
    variety: scoreVariety(replies),
  };

  let total = 0;
  for (const [name, r] of Object.entries(axes)){
    total += r.score;
    const bar = '#'.repeat(Math.round(r.score / 5)).padEnd(20, '.');
    console.log(`${name.padEnd(18)} ${String(r.score).padStart(3)}/100 ${bar}  ${r.detail}`);
  }
  const overall = Math.round(total / Object.keys(axes).length);
  console.log(`overall            ${String(overall).padStart(3)}/100`);

  const report = { cart: path.basename(CART), turns: SCENARIO.length, axes, overall };
  if (JSON_OUT){
    fs.writeFileSync(JSON_OUT, JSON.stringify(report, null, 2));
    console.log(`wrote ${JSON_OUT}`);
  }
  if (overall < 70){
    console.error(`FAILED -- believability ${overall} < 70`);
    process.exit(1);
  }
  console.log('PASSED -- believability guard active');
})().catch(e => { console.error(e); process.exit(2); });
