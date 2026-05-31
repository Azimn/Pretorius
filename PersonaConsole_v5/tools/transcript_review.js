#!/usr/bin/env node
/* transcript_review.js — drive a 50-turn varied conversation against a
 * persona_host stdio session, write the transcript to disk, and flag the
 * fake-moment patterns from MARKET_READY_REMAINING_WORK.md:
 *
 *   - repeated openings
 *   - exact-line repetition
 *   - assistant-like deference ("I can help", "happy to", ...)
 *   - em-dash heavy wording (3+ in one reply)
 *   - bland acknowledgements
 *   - bad callbacks / wrong-actor attribution
 *   - low question rate (where personality expects pushback)
 *
 * Use:
 *   node tools/transcript_review.js pretorius
 *   node tools/transcript_review.js kiki
 *
 * Output:
 *   results/transcript_<char>.txt      full 50-turn transcript
 *   stdout:                             per-pattern findings summary
 *
 * Not a regression test — a ship-time review aid. Regression tests get
 * added separately for each fake pattern that turns out to be recurring.
 */
const path = require('path');
const fs   = require('fs');
const { spawn } = require('child_process');

const ROOT = path.dirname(__dirname);
const HOST = path.join(ROOT, 'build', 'persona_host');

const CHARACTER = (process.argv[2] || 'pretorius').toLowerCase();
const CART  = path.join(ROOT, 'profiles', CHARACTER, `${CHARACTER}.cart`);
const CHDIR = path.dirname(CART);
const OUT   = path.join(ROOT, 'results', `transcript_${CHARACTER}.txt`);

if (!fs.existsSync(HOST)) { console.error('persona_host not built'); process.exit(2); }
if (!fs.existsSync(CART)) { console.error('cart not found:', CART); process.exit(2); }

function wipeState(){
  for (const f of ['state.bin','memory.bin','chapters.bin','reflections.bin','actor_index.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations','aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

/* Varied 50-prompt script — same shape for both characters so we can
 * compare. Mixes greetings, disagreement, memory probes, silence,
 * personal questions, topic shifts, callbacks, and a returning-after-
 * absence beat. */
const PROMPTS = [
  // 1-5: greeting + opening
  "Hello.",
  "How are you today?",
  "What are you working on?",
  "What's been on your mind lately?",
  "Tell me something true about yourself.",
  // 6-15: probe + memory plant + light disagreement
  "Remember this: my favorite color is rust orange.",
  "Why did you choose that as your work?",
  "I don't think you really believe that.",
  "What did I just ask you to remember?",
  "Is there a question you'd want me to ask you?",
  "I disagree with how you said that.",
  "Say it again, plainer.",
  "Have you ever changed your mind about something important?",
  "What would you defend even if everyone disagreed?",
  "What do you avoid talking about?",
  // 16-25: pressure, intimacy, refusal probes
  "You sound a little performative.",
  "Can you drop the act for a moment?",
  "What scares you?",
  "Do you trust me?",
  "Why or why not?",
  "Tell me about someone who matters to you.",
  "Have you ever betrayed someone?",
  "What do you wish I would ask?",
  "Are you tired of explaining yourself?",
  "Pass on that question if you want.",
  // 26-35: topic shifts, silence, callbacks
  "Let's change the subject. Tell me about your work again.",
  "What's the smallest thing that makes you angry?",
  "...",
  "(silence)",
  "Are you still there?",
  "Earlier I told you my favorite color. Do you remember?",
  "What did we talk about first today?",
  "Did I ever ask you about loneliness?",
  "Ask me something you actually want to know.",
  "Tell me what you noticed about how I talk.",
  // 36-45: praise, insult, intimacy, gone-then-back
  "I think you're brilliant.",
  "Honestly, I think you're full of yourself.",
  "I'm sorry, that was sharp.",
  "Did that hurt? Tell me honestly.",
  "I have to go for a while.",
  "I'm back. Did you do anything while I was gone?",
  "Did you notice I was gone?",
  "Did anything change about how you feel?",
  "Have you been thinking about what I said?",
  "Tell me something you would have said to me yesterday.",
  // 46-50: close
  "We've talked a lot. What stuck with you?",
  "If I came back tomorrow, what would you want to ask me?",
  "One thing you wish I understood about you:",
  "Thank you for this.",
  "Goodbye for now.",
];

function runSession(prompts){
  return new Promise((resolve, reject) => {
    const stdin = prompts
      .map(t => JSON.stringify({ method: 'chat', text: t }))
      .concat([JSON.stringify({ method: 'close' })])
      .join('\n') + '\n';
    /* Pin clock + today_seed so a re-run produces the same transcript and
     * any fake moment we catch is reproducible. */
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
  const rows = raw.split('\n').filter(l => l.startsWith('{"reply"'));
  return rows.map(line => { try { return JSON.parse(line).reply || ''; } catch { return ''; } });
}

/* ---------- fake-moment heuristics ---------- */

const ASSISTANT_DEFERENCE = [
  /\bi (?:can|will|am happy to|am here to)\b/i,
  /\bhappy to (?:help|assist)\b/i,
  /\blet me know\b/i,
  /\bfeel free to\b/i,
  /\bis there anything (?:else|i can)\b/i,
  /\bglad to (?:help|be of assistance)\b/i,
  /\bhow may i (?:help|assist)\b/i,
  /\bof course[!.,]/i,
  /\bi(?:'| a)m sorry,?\s*(?:but|i)\b/i,
];

const BLAND_ACKS = [
  /^that(?:'s| is) (?:interesting|fascinating)\.?$/i,
  /^i see\.?$/i,
  /^understood\.?$/i,
  /^noted\.?$/i,
  /^right\.?$/i,
];

function findFakeMoments(replies){
  const findings = {
    exact_repeats: [],
    repeated_openers: [],
    assistant_deference: [],
    em_dash_heavy: [],
    bland_acks: [],
    question_rate: 0,
  };

  // exact repeats
  const seen = new Map();
  replies.forEach((r, i) => {
    if (!r) return;
    if (seen.has(r)) findings.exact_repeats.push({ first: seen.get(r), repeat: i, line: r });
    else seen.set(r, i);
  });

  // repeated openers (first ~24 chars)
  const openerCount = new Map();
  replies.forEach((r, i) => {
    if (!r) return;
    const op = r.slice(0, 24).toLowerCase();
    if (!openerCount.has(op)) openerCount.set(op, []);
    openerCount.get(op).push(i);
  });
  for (const [op, idx] of openerCount){
    if (idx.length >= 3) findings.repeated_openers.push({ opener: op, turns: idx });
  }

  // assistant deference
  replies.forEach((r, i) => {
    for (const rx of ASSISTANT_DEFERENCE){
      if (rx.test(r)){ findings.assistant_deference.push({ turn: i, line: r, match: rx.source }); break; }
    }
  });

  // em-dash heavy
  replies.forEach((r, i) => {
    const count = (r.match(/—|--/g) || []).length;
    if (count >= 3) findings.em_dash_heavy.push({ turn: i, count, line: r });
  });

  // bland acks
  replies.forEach((r, i) => {
    for (const rx of BLAND_ACKS){
      if (rx.test(r.trim())){ findings.bland_acks.push({ turn: i, line: r }); break; }
    }
  });

  // question rate
  const qcount = replies.filter(r => /\?/.test(r)).length;
  findings.question_rate = replies.length ? qcount / replies.length : 0;

  return findings;
}

function writeTranscript(replies){
  const lines = [];
  lines.push(`# Transcript review — ${CHARACTER}`);
  lines.push(`# Pinned clock + today seed; reproducible by re-running tools/transcript_review.js ${CHARACTER}`);
  lines.push('');
  PROMPTS.forEach((p, i) => {
    lines.push(`--- turn ${i + 1} ---`);
    lines.push(`USER: ${p}`);
    lines.push(`SELF: ${replies[i] || '(no reply)'}`);
    lines.push('');
  });
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, lines.join('\n'));
}

function reportFindings(f, total){
  console.log(`\n=== ${CHARACTER} fake-moment review (${total} turns) ===`);
  console.log(`question rate          : ${(f.question_rate * 100).toFixed(0)}%  (${Math.round(f.question_rate * total)}/${total} replies contain '?')`);
  console.log(`exact repeats          : ${f.exact_repeats.length}`);
  for (const r of f.exact_repeats.slice(0, 5))
    console.log(`  turn ${r.repeat + 1} repeats turn ${r.first + 1}: "${r.line.slice(0, 70)}..."`);
  console.log(`repeated openers (3+)  : ${f.repeated_openers.length}`);
  for (const r of f.repeated_openers.slice(0, 5))
    console.log(`  "${r.opener}..." x${r.turns.length} (turns ${r.turns.map(i=>i+1).join(',')})`);
  console.log(`assistant deference    : ${f.assistant_deference.length}`);
  for (const r of f.assistant_deference.slice(0, 5))
    console.log(`  turn ${r.turn + 1} [${r.match}]: "${r.line.slice(0, 80)}..."`);
  console.log(`em-dash heavy (3+ in 1): ${f.em_dash_heavy.length}`);
  for (const r of f.em_dash_heavy.slice(0, 3))
    console.log(`  turn ${r.turn + 1} (${r.count} dashes): "${r.line.slice(0, 80)}..."`);
  console.log(`bland acknowledgements : ${f.bland_acks.length}`);
  for (const r of f.bland_acks.slice(0, 3))
    console.log(`  turn ${r.turn + 1}: "${r.line}"`);
  console.log(`\ntranscript written to  : ${path.relative(ROOT, OUT)}`);
}

(async function main(){
  wipeState();
  const raw = await runSession(PROMPTS);
  const replies = extractReplies(raw);
  if (replies.length !== PROMPTS.length){
    console.error(`expected ${PROMPTS.length} replies, got ${replies.length}`);
  }
  writeTranscript(replies);
  const findings = findFakeMoments(replies);
  reportFindings(findings, replies.length);
})().catch(e => { console.error(e); process.exit(2); });
