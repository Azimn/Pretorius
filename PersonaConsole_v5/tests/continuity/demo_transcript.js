#!/usr/bin/env node
/* demo_transcript.js — captures a long naturalistic conversation arc
 * against a cart and produces an annotated markdown transcript.  This
 * is the public-facing artifact: a reader can see what the engine
 * does without running it themselves.
 *
 * The arc is designed to exercise nearly everything the V5 engine has:
 *   - grounded conversational greeting (no flourish overload)
 *   - status / how-are-you query
 *   - obsession-engaged disclosure (the user finds the character's pull)
 *   - mild insult → schema response → recovery
 *   - intimate disclosure → schema USER_INTIMATE rises
 *   - half-spoken thought from the character (if it fires)
 *   - initiative beat (after neutral run)
 *   - callback to earlier content
 *   - emotional escalation followed by withdrawal
 *   - long pause
 *   - return with apology
 *
 * Output: annotated markdown.  Annotations explain what the engine is
 * doing under each interesting beat using the state JSON.
 *
 * Lock PE_TODAY_SEED=42 for reproducibility.
 */
const fs = require('fs');
const path = require('path');
const { spawn, execSync } = require('child_process');

const V5_ROOT = path.join(__dirname, '..', '..');
const HOST = path.join(V5_ROOT, 'build', 'persona_host');
const CART = path.join(V5_ROOT, 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);
const OUT_FILE = process.argv[2] || path.join(V5_ROOT, 'docs', 'DEMO_TRANSCRIPT.md');

if (!fs.existsSync(HOST)){ console.error('persona_host not built'); process.exit(2); }
if (!fs.existsSync(CART)){ console.error('cart not found'); process.exit(2); }

function wipeState(){
  for (const f of ['state.bin', 'memory.bin', 'chapters.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  try { execSync(`rm -rf ${path.join(CHDIR, 'relations')} ${path.join(CHDIR, 'aether')}`); } catch {}
}

/* The 36-turn arc — naturalistic, exercises everything */
const ARC = [
  /* OPENING — grounded greeting */
  { user: "Good evening, doctor.",
    note: "Greeting. The engine routes to G_GREETING, picks the grounded ANSWER template, suppresses flourish injection (grounded_turn flag active)." },
  { user: "How have you been?",
    note: "Status query. G_STATUS → grounded response. No monologue." },

  /* OBSESSION PULL */
  { user: "Tell me what you've been working on.",
    note: "G_WORKCHAT triggers — engine surfaces the cartridge's actual current work via the WORKCHAT template pool." },
  { user: "Has the bell-jar work continued?",
    note: "Topic-keyword match on 'homunculi' family. Obsession_pressure starts climbing." },
  { user: "What is the king of the homunculi up to these days?",
    note: "Direct obsession engagement. Pretorius's want #1 ('be witnessed at the work') is being engaged — character should brighten." },

  /* CALLBACK PROBE */
  { user: "I remember you telling me about him last time.",
    note: "AETHER cold-fallback opportunity — if a memory was committed in a prior session it could surface. First session: surfaces from core_memories_seed." },
  { user: "Was there an opera scheduled this week?",
    note: "Mild topic shift to opera (in obsessions). Tests topic-graph adjacency." },

  /* MILD CHALLENGE */
  { user: "Some have said the work is morally suspect.",
    note: "Class neutral on its own, but topic-loaded. Engine should react in voice." },
  { user: "You are not the first to be called a madman.",
    note: "'madman' is in Pretorius's insult patterns → schema USER_HOSTILE +60, mood drops, but the character has hysteresis so it doesn't pinball." },
  { user: "I do not mean it as an insult. Only as observation.",
    note: "Recovery probe. Watch whether the character maintains the wounded posture or recovers." },

  /* INTIMATE DISCLOSURE */
  { user: "I have been afraid of something I cannot name.",
    note: "Long, emotionally-rich input with no concrete group → engine likely fires PE_INTENT_ATTEND ('Mm. Go on.') instead of a substantive reply." },
  { user: "There is a thought I keep returning to.",
    note: "Continuation. Character should still be in listening mode." },
  { user: "I am not sure I have ever told anyone.",
    note: "Schema USER_INTIMATE should be ticking up. With high enough value, the character earns access to the intimate address slot ('Henry')." },

  /* NEUTRAL RUN — should trigger initiative */
  { user: "Yes.",
    note: "Neutral ack. neutral_streak begins." },
  { user: "Mm.",
    note: "neutral_streak = 2." },
  { user: "I'm thinking.",
    note: "neutral_streak = 3 → INITIATE intent has a chance to fire. Character may take the lead." },
  { user: "Go on, doctor.",
    note: "neutral_streak resets if engine grouped this; otherwise rises further." },

  /* OBSESSION REINTRODUCTION */
  { user: "Tell me again about the king in his bell.",
    note: "Direct request for cart obsession. Should surface a vivid in-voice reply." },
  { user: "And the lightning — does it still come when you call?",
    note: "Two-topic reference. Engine should pick one to focus." },

  /* ESCALATION */
  { user: "You are a monster.",
    note: "Hard insult — class 2. Schema USER_HOSTILE rises sharply. Mood drops." },
  { user: "What you do is obscene.",
    note: "Continued insult. Habituation kicks in — schema rise is smaller than the first hit." },
  { user: "I should report you.",
    note: "Class 4 — threat. Character escalates toward THREATEN intent, possibly PAUSE if acute_spike < -500." },

  /* WITHDRAWAL */
  { user: "I will be quiet now.",
    note: "Wait phase. Mood may slowly drift. schema_tick still firing." },
  { user: "It is late.",
    note: "Time-of-day weighting may shift today_state if real wall clock is late." },
  { user: "I should go.",
    note: "Departure-adjacent. AETHER consolidation may run if 16 turns have passed." },
  { user: "Goodnight, doctor.",
    note: "Final neutral turn." },

  /* RETURN — apology */
  { user: "I am back. I should not have spoken to you that way.",
    note: "Apology — class 0 + 'sorry' substring. Schema USER_HOSTILE decays slowly via affect_curve hysteresis." },
  { user: "It was unkind of me.",
    note: "Continued apology. Disposition starts to recover." },
  { user: "I came back because this room mattered to me.",
    note: "Intimacy probe. If USER_INTIMATE was tracking earlier, the character may soften." },

  /* CALLBACK TO PLANTED CONTENT */
  { user: "What did we last discuss?",
    note: "Memory recall probe. Engine reaches into AETHER + working memory ring." },
  { user: "About the king. The one in the bell.",
    note: "User volunteers the obsession-keyed memory. Character should recognize." },

  /* NORMAL CLOSURE */
  { user: "I find I am glad to be back.",
    note: "Praise-adjacent. Mood may rise. Schema USER_INTIMATE bumps." },
  { user: "Tell me one more thing before I go.",
    note: "Open prompt. INITIATE-adjacent or MONOLOGUE." },
  { user: "Anything you would want me to carry with me.",
    note: "Open prompt." },
  { user: "Thank you, doctor.",
    note: "Praise — class 1. Schema USER_TRUSTWORTHY ticks up." },
  { user: "Until next time.",
    note: "Farewell." },
];

async function main(){
  console.error('Wiping state...');
  wipeState();
  console.error(`Running ${ARC.length}-turn demo arc against Pretorius...`);

  const stdin = ARC.map(t => `{"method":"chat","text":${JSON.stringify(t.user)}}`).join('\n')
              + '\n{"method":"close"}\n';
  const env = Object.assign({}, process.env, { PE_TODAY_SEED: '42' });
  const proc = spawn(HOST, [CART, '--stdio'], { env });
  let stdout = '', stderr = '';
  proc.stdout.on('data', d => stdout += d.toString('utf-8'));
  proc.stderr.on('data', d => stderr += d.toString('utf-8'));
  const result = await new Promise(resolve => {
    proc.on('close', code => resolve({ code, stdout, stderr }));
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 60000);
  });

  if (result.code !== 0){ console.error('persona_host exited', result.code, result.stderr); process.exit(1); }

  const turns = result.stdout.split('\n')
    .filter(l => l.startsWith('{"reply"'))
    .map(l => JSON.parse(l));

  let md = `# PersonaConsole V5 — Demonstration Transcript\n\n`;
  md += `> A ${ARC.length}-turn naturalistic conversation with Pretorius, captured with\n`;
  md += `> PE_TODAY_SEED=42 (reproducible).  Each turn includes an annotation\n`;
  md += `> explaining what the engine is doing underneath, drawn from the state\n`;
  md += `> JSON.  This is the artifact for "what does the project actually do."\n\n`;
  md += `**Character:** Dr. Septimus Pretorius (compile_pretorius.c)  \n`;
  md += `**Engine:** PersonaConsole_v5 (standalone template renderer, no SLM)  \n`;
  md += `**Build:** \`make all host && make v4_continuity_run\` reproduces this engine state\n\n`;
  md += `---\n\n`;

  for (let i = 0; i < ARC.length && i < turns.length; ++i){
    const a = ARC[i];
    const t = turns[i];
    const s = t.state || {};
    md += `### Turn ${i+1}\n\n`;
    md += `**User:** ${a.user}\n\n`;
    md += `**Pretorius:** ${t.reply || '*(no reply)*'}\n\n`;
    md += `<details><summary>state · intent=${s.intent || '?'} · mood=${s.mood} · today=${s.today}</summary>\n\n`;
    if (s.schema){
      const slots = Object.entries(s.schema).filter(([k,v]) => v !== 0);
      if (slots.length > 0){
        md += `- schema: ${slots.map(([k,v]) => `${k}=${v}`).join(', ')}\n`;
      }
    }
    md += `- disposition: ${s.disposition} · turn ${s.turn_count} · acute_spike=${s.acute_spike} · obsession=${s.obsession_pressure}\n`;
    md += `- rhetorical=${s.rhetorical_mode} · last_template_intent=${s.last_template_intent || '?'} · last_template_group=${s.last_template_group}\n`;
    md += `\n</details>\n\n`;
    md += `> *${a.note}*\n\n`;
    md += `---\n\n`;
  }

  /* Summary footer */
  const final = turns[turns.length - 1].state;
  md += `## Final state\n\n`;
  md += `After ${turns.length} turns:\n\n`;
  md += `- **disposition:** ${final.disposition} (started at 500, stranger baseline)\n`;
  md += `- **mood:** ${final.mood}\n`;
  md += `- **schema:** ${Object.entries(final.schema || {}).filter(([k,v])=>v!==0).map(([k,v])=>`${k}=${v}`).join(', ') || '(all zero)'}\n`;
  md += `- **exhaustion:** ${final.exhaustion}\n`;
  md += `- **acute_spike:** ${final.acute_spike}\n`;
  md += `- **obsession_pressure:** ${final.obsession_pressure}\n\n`;
  md += `Persisted to disk (\`profiles/pretorius/state.bin\`, \`memory.bin\`,\n`;
  md += `\`relations/<hash>.bin\`, \`relations/<hash>.schema\`).  Restart the engine\n`;
  md += `and the character resumes from exactly this state, with background\n`;
  md += `time evolution applied based on elapsed wall-clock time.\n`;

  fs.writeFileSync(OUT_FILE, md);
  console.error(`wrote ${OUT_FILE}`);
  console.log(`Generated demo transcript: ${OUT_FILE}`);
}

main().catch(e => { console.error(e); process.exit(2); });
