#!/usr/bin/env node
/* reflection_surface_test.js -- reflections surface through ordinary dialogue. */
const path = require('path');
const { resolveHost } = require('../host_path');
const fs = require('fs');
const { spawn } = require('child_process');

const HOST = resolveHost(path.join(__dirname, '..', '..'));
const CART = path.join(__dirname, '..', '..', 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin','open_loops.bin','speech_habits.bin']){
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
      env: { ...process.env, PE_TODAY_SEED: '0xC0FFEE' }
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

const FOCUSED = [
  'Tell me about your creations.',
  'I want to hear about the homunculi.',
  'Did you really make tiny living things?',
  'Why are you obsessed with creation?',
  'What does it feel like to make life?',
  'Tell me about your laboratory.',
  'You once told me about your homunculi.',
  'How does one go about creating life?',
  'Henry never understood your work, did he?',
  'Your work on creation was misunderstood.',
  'I want to know more about the homunculi.',
  'Did the queen really speak to you?',
  'Describe the process of creating life.',
  'You said something about a king and queen.',
  'What was it like, working alone on creation?',
  'Tell me how you brought the homunculi to life.',
  'Henry abandoned your research, did he?',
  'I am curious about the creation methods you used.',
  'You speak of homunculi as if they were children.',
  'Describe the moment of first life.',
  'What did the homunculi say to you?',
  'You were a god to them, were you not?',
  'Tell me about creating something from nothing.',
  'I have always wanted to ask about the homunculi.',
  'Your work on creation deserves recognition.'
];

const PROBES = [
  'hm.',
  'that again.',
  'the work stays with me.',
  'I keep circling that subject.',
  'yes, the same subject.',
  'go on.',
  'what pattern do you notice?',
  'does it keep returning?',
  'and the work?',
  'what does that remind you of?',
  'say what comes back.',
  'one more thought.'
];

(async function main(){
  console.log('--- reflection surface test ---');
  wipeState();
  const setup = FOCUSED.map(text => ({ method: 'chat', text }));
  setup.push({ method: 'reflections' });
  const first = await run(setup);
  const refl = first.find(r => typeof r.count === 'number' && Array.isArray(r.reflections));
  ok(refl && refl.count >= 1, 'setup synthesized at least one reflection');

  const rows = await run(PROBES.map(text => ({ method: 'chat', text })));
  const surfaced = rows.find(r => typeof r.reply === 'string'
    && /(always .* returns|sense its weight|pattern in how we talk|accumulating|circled|unfinished between us|comes back to me now|years are not kind)/i.test(r.reply)
    && !/\[reflection/i.test(r.reply));
  ok(!!surfaced,
     `reflection can surface via dialogue memory slot: ${surfaced ? surfaced.reply : rows.map(r => `${r.state && r.state.intent}:${r.reply}`).join(' | ')}`);

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- reflection dialogue surfacing active');
})().catch(e => { console.error(e); process.exit(2); });
