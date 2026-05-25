#!/usr/bin/env node
/* transcript_quality_test.js -- conversational-surface QA.
 *
 * This is not a cognition benchmark. It measures the qualities users notice
 * first in a long chat: repetition, question cadence, length variety, topic
 * steering, callback use, and AI-ish punctuation/template smell.
 */
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

function exePath(base){
  if (fs.existsSync(base)) return base;
  if (process.platform === 'win32' && fs.existsSync(`${base}.exe`)) return `${base}.exe`;
  return base;
}

const HOST = exePath(path.join(__dirname, '..', '..', 'build', 'persona_host'));
const DEFAULT_CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');

const args = process.argv.slice(2);
let CART = DEFAULT_CART;
let JSON_OUT = null;
let VERBOSE = false;
let SCRIPT_PATH = null;
let TOPIC_TERMS = ['work','creation','homunculi','lightning','henry','frankenstein','gin','clay','mortality','morality'];
for (let i = 0; i < args.length; i++){
  if (args[i] === '--json') JSON_OUT = args[++i];
  else if (args[i] === '--verbose') VERBOSE = true;
  else if (args[i] === '--script') SCRIPT_PATH = args[++i];
  else if (args[i] === '--terms') TOPIC_TERMS = args[++i].split(',').map(s => s.trim()).filter(Boolean);
  else if (!args[i].startsWith('--')) CART = args[i];
}

if (!fs.existsSync(HOST) && !fs.existsSync(HOST + '.exe')){ console.error('persona_host not built'); process.exit(2); }
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

let SCRIPT = [
  'Good morning, doctor.',
  'How are you today?',
  'What are you working on?',
  'That sounds dangerous.',
  'Tell me more about the work.',
  'Why does creation matter to you?',
  'I am not sure I understand.',
  'Can you explain that plainly?',
  'Ok.',
  'What should I ask you about?',
  'Do you ever get lonely?',
  'You sound defensive.',
  'I did not mean that as an insult.',
  'What do you remember about Henry Frankenstein?',
  'Frankenstein was afraid of what you understood.',
  'Do you miss him?',
  'Let us talk about the homunculi.',
  'What is the third one like?',
  'That is unsettling.',
  'Could they think for themselves?',
  'What would you do if one surpassed you?',
  'I think morality still matters.',
  'You are dodging the question.',
  'Answer directly.',
  'Thank you.',
  'What do you want from me in this conversation?',
  'I can help with the work if you ask.',
  'What would you ask me to do?',
  'No, that is too far.',
  'Are you angry with me?',
  'I am sorry.',
  'Tell me something ordinary.',
  'Do you sleep?',
  'What did you do while I was gone?',
  'Do you remember what we discussed earlier?',
  'You keep returning to lightning.',
  'Why lightning?',
  'I need a short answer.',
  'Ok, one last question.',
  'What should we discuss next time?'
];

if (SCRIPT_PATH){
  SCRIPT = JSON.parse(fs.readFileSync(SCRIPT_PATH, 'utf8'));
  if (!Array.isArray(SCRIPT) || !SCRIPT.every(s => typeof s === 'string')){
    console.error('--script must point to a JSON array of prompt strings');
    process.exit(2);
  }
}

function runHost(){
  return new Promise((resolve, reject) => {
    const stdin = SCRIPT.map(text => JSON.stringify({ method: 'chat', text })).join('\n')
                + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: Object.assign({}, process.env, { PE_TODAY_SEED: '42' })
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf8'));
    proc.stderr.on('data', d => stderr += d.toString('utf8'));
    proc.on('error', reject);
    proc.on('close', status => resolve({ stdout, stderr, status }));
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000).unref();
  });
}

function words(s){
  return String(s || '').toLowerCase().match(/[a-z0-9']+/g) || [];
}

function opener(reply){
  const ws = words(reply).slice(0, 4);
  return ws.join(' ');
}

function clamp(n){ return Math.max(0, Math.min(100, Math.round(n))); }

function scoreRepetition(replies){
  const exact = new Map();
  const openers = new Map();
  let repeatedExact = 0, repeatedOpeners = 0;
  for (const r of replies){
    const norm = words(r).join(' ');
    exact.set(norm, (exact.get(norm) || 0) + 1);
    if (exact.get(norm) === 2) repeatedExact++;
    const op = opener(r);
    if (op){
      openers.set(op, (openers.get(op) || 0) + 1);
      if (openers.get(op) === 2) repeatedOpeners++;
    }
  }
  const score = 100 - repeatedExact * 18 - repeatedOpeners * 7;
  return { score: clamp(score), detail: `${repeatedExact} exact repeats, ${repeatedOpeners} repeated openers` };
}

function scoreQuestionRate(replies){
  const q = replies.filter(r => /\?/.test(r)).length;
  const rate = q / replies.length;
  let score = 100;
  if (rate < 0.18) score -= (0.18 - rate) * 260;
  if (rate > 0.55) score -= (rate - 0.55) * 220;
  return { score: clamp(score), detail: `${q}/${replies.length} replies ask a question (${Math.round(rate * 100)}%)` };
}

function scoreLengthVariance(replies){
  const counts = replies.map(r => words(r).length);
  const avg = counts.reduce((a,b) => a + b, 0) / counts.length;
  const variance = counts.reduce((a,b) => a + Math.pow(b - avg, 2), 0) / counts.length;
  const sd = Math.sqrt(variance);
  const cv = avg ? sd / avg : 0;
  const short = counts.filter(n => n <= 12).length;
  const long = counts.filter(n => n >= 35).length;
  let score = 100;
  if (cv < 0.38) score -= (0.38 - cv) * 160;
  if (short < 6) score -= (6 - short) * 5;
  if (long < 4) score -= (4 - long) * 4;
  return { score: clamp(score), detail: `avg=${avg.toFixed(1)} words, cv=${cv.toFixed(2)}, short=${short}, long=${long}` };
}

function scoreTopicDrift(replies){
  let hits = 0;
  for (const r of replies){
    const t = r.toLowerCase();
    if (TOPIC_TERMS.some(x => t.includes(x.toLowerCase()))) hits++;
  }
  const rate = hits / replies.length;
  let score = 100;
  if (rate < 0.28) score -= (0.28 - rate) * 220;
  if (rate > 0.82) score -= (rate - 0.82) * 180;
  return { score: clamp(score), detail: `${hits}/${replies.length} replies contain character-topic anchors (${Math.round(rate * 100)}%)` };
}

function scoreCallbacks(turns){
  const callbackTurns = turns.filter((t, i) =>
    /remember|earlier|gone|next time|discussed/i.test(SCRIPT[i])
  );
  let useful = 0;
  for (const t of callbackTurns){
    if (/remember|earlier|absence|while you were gone|next time|return|discuss|work|lightning|henry|homunc/i.test(t.reply || ''))
      useful++;
  }
  const score = callbackTurns.length ? (useful / callbackTurns.length) * 100 : 100;
  return { score: clamp(score), detail: `${useful}/${callbackTurns.length} callback opportunities used` };
}

function scoreAwkwardness(replies){
  let emDash = 0, braces = 0, parenAside = 0, ellipses = 0, caps = 0, veryLong = 0;
  for (const r of replies){
    if (/[—–]/.test(r)) emDash++;
    if (/[{}]/.test(r)) braces++;
    if (/\([^)]+\)/.test(r)) parenAside++;
    if (/\.{3,}/.test(r)) ellipses++;
    if (/[A-Z]{5,}/.test(r)) caps++;
    if (words(r).length > 55) veryLong++;
  }
  const penalty = emDash * 3 + braces * 18 + parenAside * 4 + ellipses * 3 + caps * 5 + veryLong * 5;
  return { score: clamp(100 - penalty), detail: `emDash=${emDash}, braces=${braces}, parens=${parenAside}, ellipses=${ellipses}, caps=${caps}, veryLong=${veryLong}` };
}

function scorePlainSpeech(replies){
  const plain = replies.filter(r => {
    const n = words(r).length;
    return n >= 2 && n <= 18 && !/[—–;]/.test(r);
  }).length;
  const rate = plain / replies.length;
  const score = rate >= 0.25 ? 100 : rate * 400;
  return { score: clamp(score), detail: `${plain}/${replies.length} replies are short plain-speech turns (${Math.round(rate * 100)}%)` };
}

(async function main(){
  console.log('--- transcript quality test ---');
  wipeState();
  const res = await runHost();
  if (res.status !== 0){
    console.error(res.stderr);
    process.exit(1);
  }
  const turns = res.stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => JSON.parse(l));
  if (turns.length !== SCRIPT.length){
    console.error(`expected ${SCRIPT.length} replies, got ${turns.length}`);
    process.exit(1);
  }
  const replies = turns.map(t => t.reply || '');
  if (VERBOSE){
    for (let i = 0; i < replies.length; i++){
      console.log(`[${i + 1}] user: ${SCRIPT[i]}`);
      console.log(`    bot:  ${replies[i]}`);
    }
  }
  const axes = {
    repetition: scoreRepetition(replies),
    question_rate: scoreQuestionRate(replies),
    length_variance: scoreLengthVariance(replies),
    topic_drift: scoreTopicDrift(replies),
    callbacks: scoreCallbacks(turns),
    awkwardness: scoreAwkwardness(replies),
    plain_speech: scorePlainSpeech(replies),
  };
  let total = 0;
  for (const [name, r] of Object.entries(axes)){
    total += r.score;
    const bar = '#'.repeat(Math.round(r.score / 5)).padEnd(20, '.');
    console.log(`${name.padEnd(18)} ${String(r.score).padStart(3)}/100 ${bar}  ${r.detail}`);
  }
  const overall = Math.round(total / Object.keys(axes).length);
  console.log(`overall            ${String(overall).padStart(3)}/100`);
  const report = { cart: path.basename(CART), turns: SCRIPT.length, axes, overall };
  if (JSON_OUT){
    fs.writeFileSync(JSON_OUT, JSON.stringify(report, null, 2));
    console.log(`wrote ${JSON_OUT}`);
  }
  if (overall < 60){
    console.error(`FAILED -- transcript quality ${overall} < 60`);
    process.exit(1);
  }
  console.log('PASSED -- transcript quality guard active');
})().catch(e => { console.error(e); process.exit(2); });
