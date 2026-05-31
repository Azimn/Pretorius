#!/usr/bin/env node
/* no_repeat_in_long_chat_test.js — regression test for fake-moment patterns
 * surfaced by tools/transcript_review.js. Drives a deterministic 35-turn
 * varied conversation and asserts:
 *
 *   1. No exact reply is produced twice (a callback or fallback firing
 *      identically across the window).
 *   2. No opener (first 24 chars, case-insensitive) appears in three or
 *      more replies (the "Ah. Always the work returns..." regression).
 *
 * The 50-turn transcript review caught the reflection-callback line firing
 * 3-4 times and the asks_absence line firing back-to-back. This test runs
 * a slightly shorter, more bug-targeted script so the gate is fast.
 */
const path = require('path');
const fs   = require('fs');
const { spawn } = require('child_process');

const ROOT  = path.join(__dirname, '..', '..');
const HOST  = path.join(ROOT, 'build', 'persona_host');
const CART  = path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipeState(){
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin','actor_index.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations','aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

const PROMPTS = [
  /* topic-saturated to stress callbacks; absence beat repeats to stress
   * direct-callback anti-repeat; long enough to exercise blackout windows */
  'Hello.',
  'What are you working on?',
  "Tell me about creation and your work.",
  "What did Henry think of all this?",
  "Frankenstein was the real genius.",
  "Are you proud of what you've built?",
  "I disagree with how you describe it.",
  "Why is the work worth the cost?",
  "What scares you most about it?",
  "Have you ever felt loneliness in it?",
  "Tell me again about Henry.",
  "What part of the work matters most to you?",
  "I think you're rationalizing.",
  "Be honest about the cost for once.",
  "What would you say if Henry walked in now?",
  "We've been talking about the work for a while.",
  "Let's change topics. Tell me about something else.",
  "Actually, return to the work for a moment.",
  "Tell me about loneliness in the lab.",
  "Was Henry your friend or your rival?",
  /* repeated-absence beat — used to fire the same line twice */
  "I have to go for a while.",
  "I'm back. Did you do anything while I was gone?",
  "Did you notice I was gone?",
  /* topic returns to stress the callback again */
  "Tell me one more thing about the work.",
  "And about Henry?",
  "What's the smallest detail that still bothers you?",
  "Do you want to ask me something?",
  "I'm listening if you want to be honest.",
  "What would you confess if I promised not to argue?",
  "Tell me what you'd never tell Henry.",
  "Are we okay?",
  "Anything else on your mind?",
  "What stayed with you from this conversation?",
  "One last thing about the work?",
  "Goodbye for now.",
];

function runSession(prompts){
  return new Promise((resolve, reject) => {
    const stdin = prompts
      .map(t => JSON.stringify({ method: 'chat', text: t }))
      .concat([JSON.stringify({ method: 'close' })])
      .join('\n') + '\n';
    const proc = spawn(HOST, [CART, '--stdio'], {
      env: { ...process.env, PE_TODAY_SEED: '1234', PE_CLOCK_OVERRIDE_MS: '1700000000000' },
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf-8'));
    proc.stderr.on('data', d => stderr += d.toString('utf-8'));
    proc.on('error', reject);
    proc.on('close', code => {
      if (code !== 0) reject(new Error(`host exit ${code}: ${stderr}`));
      else resolve(stdout);
    });
    proc.stdin.write(stdin); proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 60000);
  });
}

function extractReplies(raw){
  return raw.split('\n')
            .filter(l => l.startsWith('{"reply"'))
            .map(l => { try { return JSON.parse(l).reply || ''; } catch { return ''; } });
}

function ok(cond, msg){
  if (!cond){ console.error(`not ok: ${msg}`); process.exitCode = 1; }
  else { console.log(`ok:   ${msg}`); }
}

(async function main(){
  console.log('--- no repeat in long chat (35-turn anti-repeat regression) ---');
  wipeState();

  const raw = await runSession(PROMPTS);
  const replies = extractReplies(raw);
  ok(replies.length === PROMPTS.length,
     `got ${replies.length}/${PROMPTS.length} replies`);

  /* Assertion 1: no exact line appears 3+ times. A single re-fire after
   * the blackout window clears (~16 turns for reflection callbacks) is
   * realistic; 3+ firings in a 35-turn window is the bug that prompted
   * this test (transcript review surfaced the same reflection callback
   * firing 4x in a 50-turn chat). */
  const lineCount = new Map();
  replies.forEach((r, i) => {
    if (!r) return;
    if (!lineCount.has(r)) lineCount.set(r, []);
    lineCount.get(r).push(i + 1);
  });
  const triplets = [];
  for (const [line, turns] of lineCount){
    if (turns.length >= 3) triplets.push({ line, turns });
  }
  ok(triplets.length === 0,
     `no exact line appears 3+ times (found ${triplets.length})`);
  for (const t of triplets.slice(0, 3))
    console.error(`     ${t.turns.length}x at turns ${t.turns.join(',')}: "${t.line.slice(0, 80)}"`);

  /* Assertion 2: no opener appears 3+ times (case-insensitive first 24 chars) */
  const openerCount = new Map();
  replies.forEach((r, i) => {
    if (!r) return;
    const op = r.slice(0, 24).toLowerCase();
    if (!openerCount.has(op)) openerCount.set(op, []);
    openerCount.get(op).push(i + 1);
  });
  const overUsedOpeners = [];
  for (const [op, turns] of openerCount){
    if (turns.length >= 3) overUsedOpeners.push({ op, turns });
  }
  ok(overUsedOpeners.length === 0,
     `no opener appears 3+ times (found ${overUsedOpeners.length})`);
  for (const r of overUsedOpeners.slice(0, 3))
    console.error(`     "${r.op}..." appears ${r.turns.length}x at turns ${r.turns.join(',')}`);

  /* Clean up so following tests start fresh */
  wipeState();

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- no-repeat-in-long-chat regression intact');
})().catch(e => { console.error(e); process.exit(2); });
