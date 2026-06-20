#!/usr/bin/env node
/* 50-turn greeting/status depth probe for the offline template tier. */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { resolveHost } = require('../host_path');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const SRC_CART = path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart');

const greetings = [
  'Good morning, Doctor.',
  'Hello, Doctor.',
  'Good evening, Pretorius.',
  'I am back.',
  'There you are.',
  'Good to see you.',
  'Hello again.',
  'Good morning again.',
  'I have returned.',
  'Evening, Doctor.',
];

const statuses = [
  'How are you today?',
  'How do you feel?',
  'Are you well?',
  'How is your mood?',
  'Are you tired?',
  'How are you holding up?',
  'Are you all right?',
  'How has the day treated you?',
  'Are you in a good mood?',
  'How are you, honestly?',
];

const GROUP_LABELS = {
  13: 'GREETING',
  15: 'STATUS',
  17: 'ACK',
  [0x7000 + 5]: 'BASELINE_GREETING',
  [0x7000 + 8]: 'BASELINE_STATUS',
  [0x7000 + 9]: 'BASELINE_ACK',
  65535: 'NONE',
};

function makeTempCart(){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'persona-gs50-'));
  const cart = path.join(dir, 'pretorius.cart');
  fs.copyFileSync(SRC_CART, cart);
  return { dir, cart };
}

function run(cart, prompts){
  return new Promise((resolve, reject) => {
    const stdin = prompts.map(text => JSON.stringify({ method: 'chat', text })).join('\n')
                + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [cart, '--stdio'], {
      env: {
        ...process.env,
        PE_RENDER_BACKEND: 'template',
        PE_TODAY_SEED: '0x47525453',
        PE_CLOCK_OVERRIDE_MS: '1700000000000',
      },
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf8'));
    proc.stderr.on('data', d => stderr += d.toString('utf8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) return reject(new Error(`host exited ${status}: ${stderr}`));
      resolve(stdout.split('\n')
        .filter(l => l.startsWith('{"reply"'))
        .map(l => JSON.parse(l)));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000).unref();
  });
}

function opener(s){
  return (s || '').replace(/\s+/g, ' ').trim().split(/\s+/)
    .slice(0, 4).join(' ').toLowerCase();
}

function repeats(rows, keyFn){
  const seen = new Map();
  for (const row of rows){
    const key = keyFn(row);
    if (!seen.has(key)) seen.set(key, []);
    seen.get(key).push(row.turn);
  }
  return [...seen.entries()]
    .filter(([, turns]) => turns.length > 1)
    .map(([key, turns]) => ({ key, turns }));
}

function firstRepeatTurn(items){
  return items.length ? Math.min(...items.map(x => x.turns[1])) : null;
}

(async function main(){
  console.log('--- greeting/status 50-turn exhaustion probe ---');
  const prompts = [];
  for (let i = 0; i < 25; ++i){
    prompts.push(greetings[i % greetings.length]);
    prompts.push(statuses[i % statuses.length]);
  }

  const tmp = makeTempCart();
  try {
    const turns = await run(tmp.cart, prompts);
    const rows = turns.filter(x => x.reply).map((x, i) => ({
      turn: i + 1,
      prompt: prompts[i],
      reply: x.reply,
      group: x.state ? x.state.last_template_group : 65535,
      intent: x.state ? x.state.intent : 65535,
      templateIntent: x.state ? x.state.last_template_intent : 65535,
    }));

    const exact = repeats(rows, row => row.reply.trim().toLowerCase());
    const openers = repeats(rows, row => opener(row.reply));
    const groupCounts = {};
    for (const row of rows){
      const label = GROUP_LABELS[row.group] || String(row.group);
      groupCounts[label] = (groupCounts[label] || 0) + 1;
    }

    const out = {
      replies: rows.length,
      groupCounts,
      exactRepeatCount: exact.reduce((n, x) => n + x.turns.length - 1, 0),
      openerRepeatCount: openers.reduce((n, x) => n + x.turns.length - 1, 0),
      firstExactRepeatTurn: firstRepeatTurn(exact),
      firstOpenerRepeatTurn: firstRepeatTurn(openers),
      exactRepeats: exact,
      openerRepeats: openers,
      transcript: rows,
    };

    const outPath = path.join(os.tmpdir(), `pretorius_greeting_status_50_${Date.now()}.json`);
    fs.writeFileSync(outPath, JSON.stringify(out, null, 2));
    console.log(JSON.stringify({
      replies: out.replies,
      groupCounts: out.groupCounts,
      exactRepeatCount: out.exactRepeatCount,
      openerRepeatCount: out.openerRepeatCount,
      firstExactRepeatTurn: out.firstExactRepeatTurn,
      firstOpenerRepeatTurn: out.firstOpenerRepeatTurn,
      json: outPath,
    }, null, 2));
    for (const row of rows.slice(0, 30)){
      const label = GROUP_LABELS[row.group] || row.group;
      console.log(`${row.turn}. [${label}] ${row.prompt} -> ${row.reply}`);
    }

    if (out.firstExactRepeatTurn !== null && out.firstExactRepeatTurn < 20){
      console.error(`not ok: exact repeat before turn 20 at ${out.firstExactRepeatTurn}`);
      process.exitCode = 1;
    }
    if (out.firstOpenerRepeatTurn !== null && out.firstOpenerRepeatTurn < 20){
      console.error(`not ok: opener repeat before turn 20 at ${out.firstOpenerRepeatTurn}`);
      process.exitCode = 1;
    }
  } finally {
    fs.rmSync(tmp.dir, { recursive: true, force: true });
  }
  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- no early greeting/status repeats');
})().catch(e => { console.error(e); process.exit(2); });
