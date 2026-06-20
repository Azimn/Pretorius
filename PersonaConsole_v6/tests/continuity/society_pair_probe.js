#!/usr/bin/env node
/* Long-form two-character society probe.
 *
 * This is a diagnostic harness, not a hard gate. It copies cartridges into a
 * temp workspace, lets two characters talk to each other, and reports whether
 * actor-tagged memory, relationships, repetition control, and renderer tone
 * survive a longer exchange. Real profile memories are never touched.
 */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { resolveHost } = require('../host_path');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const DEFAULT_TMP_ROOT = process.env.USERPROFILE
  ? path.join(process.env.USERPROFILE, 'AppData', 'Local', 'Temp')
  : os.tmpdir();
const TMP_ROOT = process.env.PE_SOCIETY_TMP ||
  path.join(DEFAULT_TMP_ROOT, 'persona_society_probe');

const cfg = {
  turns: parseInt(process.env.PE_SOCIETY_TURNS || '24', 10),
  backend: process.env.PE_RENDER_BACKEND || 'template',
  model: process.env.PE_OLLAMA_MODEL || '',
  guide: process.env.PE_SOCIETY_GUIDE === '1',
  seed: process.env.PE_TODAY_SEED || '0x50C1E7',
  clock: process.env.PE_CLOCK_OVERRIDE_MS || '1700000000000',
  aName: process.env.PE_SOCIETY_A || 'pretorius',
  bName: process.env.PE_SOCIETY_B || 'kiki',
  starter: process.env.PE_SOCIETY_STARTER ||
    'Good evening. I want to test whether two minds can keep a thread alive without pretending to be assistants.',
};

const profiles = {
  pretorius: path.join(ROOT, 'profiles', 'pretorius'),
  kiki:      path.join(ROOT, 'profiles', 'kiki'),
  friendly: path.join(ROOT, 'profiles', 'friendly'),
  rival:    path.join(ROOT, 'profiles', 'rival'),
  quiet:    path.join(ROOT, 'profiles', 'quiet'),
  mentor:   path.join(ROOT, 'profiles', 'mentor'),
};

function usage(){
  console.log('Usage: node tests/continuity/society_pair_probe.js');
  console.log('Env: PE_SOCIETY_A=pretorius PE_SOCIETY_B=kiki PE_SOCIETY_TURNS=24');
  console.log('     PE_RENDER_BACKEND=template|slm|api PE_OLLAMA_MODEL=qwen3:8b');
}

function die(msg){ console.error(msg); process.exit(2); }

function copyProfile(name){
  const src = profiles[name];
  if (!src || !fs.existsSync(src)) die(`unknown or missing profile: ${name}`);
  const dst = path.join(TMP_ROOT, name);
  fs.rmSync(dst, { recursive: true, force: true });
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.cpSync(src, dst, { recursive: true });
  wipeRuntime(dst);
  return {
    name,
    dir: dst,
    cart: path.join(dst, `${name}.cart`),
  };
}

function wipeRuntime(dir){
  for (const f of [
    'state.bin', 'memory.bin', 'chapters.bin', 'reflections.bin',
    'actor_index.bin', 'open_loops.bin', 'speech_habits.bin',
    'speech_events.bin', 'dissonance.bin',
  ]){
    try { fs.unlinkSync(path.join(dir, f)); } catch {}
  }
  for (const d of ['relations', 'aether']){
    try { fs.rmSync(path.join(dir, d), { recursive: true, force: true }); } catch {}
  }
}

function envFor(userId, profileName){
  const env = {
    ...process.env,
    PATH: `C:/cygwin64/bin;${process.env.PATH || ''}`,
    PE_TODAY_SEED: cfg.seed,
    PE_CLOCK_OVERRIDE_MS: cfg.clock,
    PE_RENDER_BACKEND: cfg.backend,
    PE_USER_ID: userId,
    PE_TRACE_ENABLE: '1',
    PE_TRACE_FILE: path.join(TMP_ROOT, `${profileName}.trace.log`),
  };
  if (cfg.model){
    env.PE_SLM_MODEL = cfg.model;
    env.PE_OLLAMA_MODEL = cfg.model;
  }
  return env;
}

class Host {
  constructor(profile, userId){
    this.profile = profile;
    this.userId = userId;
    this.buf = '';
    this.queue = [];
    this.proc = spawn(HOST, [profile.cart, '--stdio'], {
      cwd: profile.dir,
      env: envFor(userId, profile.name),
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    this.proc.stdout.on('data', d => this.onData(d.toString('utf8')));
    this.proc.stderr.on('data', d => {
      const s = d.toString('utf8').trim();
      if (s) process.stderr.write(`[${profile.name} stderr] ${s}\n`);
    });
  }
  onData(s){
    this.buf += s;
    let idx;
    while ((idx = this.buf.indexOf('\n')) >= 0){
      const line = this.buf.slice(0, idx).trim();
      this.buf = this.buf.slice(idx + 1);
      if (!line) continue;
      let obj;
      try { obj = JSON.parse(line); } catch { obj = { parse_error: line }; }
      const next = this.queue.shift();
      if (next) next(obj);
    }
  }
  send(obj, timeoutMs=90000){
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${this.profile.name} timeout on ${obj.method}`)), timeoutMs);
      timer.unref();
      this.queue.push(r => { clearTimeout(timer); resolve(r); });
      this.proc.stdin.write(`${JSON.stringify(obj)}\n`);
    });
  }
  async close(){
    try { await this.send({ method: 'close' }, 5000); } catch {}
    try { this.proc.kill(); } catch {}
  }
}

function words(s){ return (s || '').trim().split(/\s+/).filter(Boolean); }
function opener(s){ return words(s).slice(0, 5).join(' ').toLowerCase(); }
function norm(s){ return (s || '').toLowerCase().replace(/\s+/g, ' ').trim(); }

function scoreTranscript(lines, states){
  const replies = lines.map(l => l.reply || '').filter(Boolean);
  const normalized = replies.map(norm);
  const unique = new Set(normalized);
  const openers = replies.map(opener).filter(Boolean);
  const openerRepeats = openers.length - new Set(openers).size;
  const exactRepeats = normalized.length - unique.size;
  const all = normalized.join(' ');
  const lengths = replies.map(r => words(r).length);
  const avgLen = lengths.length ? lengths.reduce((a,b)=>a+b,0) / lengths.length : 0;
  const questions = replies.filter(r => /\?/.test(r)).length;
  const assistant = /as an ai|language model|how can i help|let me know if|happy to help/.test(all);
  const rough = /\s\.\s|\. \.|(^|\s)[.!?][A-Za-z]|undefined|null|\{address\}|\[[a-z_]+]/i.test(replies.join(' '));
  const actorTags = states.reduce((n, s) => n + (s.actor_tagged_memories || 0), 0);
  const speechEvents = states.reduce((n, s) => n + (s.speech_event_count || 0), 0);
  const openLoops = states.reduce((n, s) => n + (s.open_loop_count || 0), 0);
  const auditCounts = {};
  for (const l of lines){
    const k = String(l.audit_result ?? 'unknown');
    auditCounts[k] = (auditCounts[k] || 0) + 1;
  }

  return {
    replies: replies.length,
    exactRepeats,
    openerRepeats,
    avgLen: Math.round(avgLen),
    questionRate: replies.length ? Math.round(100 * questions / replies.length) : 0,
    assistantTone: assistant,
    roughPunctuationOrTags: rough,
    actorTags,
    speechEvents,
    openLoops,
    auditCounts,
  };
}

function traceStats(profileName){
  const p = path.join(TMP_ROOT, `${profileName}.trace.log`);
  const out = { dispatches: 0, providerFallbacks: 0, zeroLength: 0 };
  let text = '';
  try { text = fs.readFileSync(p, 'utf8'); } catch { return out; }
  for (const line of text.split(/\r?\n/)){
    if (!line.includes('render_dispatch')) continue;
    out.dispatches++;
    if (line.includes('delta=2') || line.includes('flags=0x2')) out.providerFallbacks++;
    if (line.includes('out_len=0')) out.zeroLength++;
  }
  return out;
}

function guidedInput(speaker, reply, turn){
  if (!cfg.guide || turn === 0) return reply;
  return `${speaker} says: "${reply}"\nAnswer ${speaker} directly. Keep the thread moving, but do not act like an assistant. Do not copy ${speaker}'s exact metaphor unless you are challenging it.`;
}

async function main(){
  if (process.argv.includes('--help')) { usage(); return; }
  if (cfg.turns < 2) die('PE_SOCIETY_TURNS must be at least 2');
  fs.rmSync(TMP_ROOT, { recursive: true, force: true });
  fs.mkdirSync(TMP_ROOT, { recursive: true });

  const a = copyProfile(cfg.aName);
  const b = copyProfile(cfg.bName);
  if (!fs.existsSync(a.cart)) die(`missing cart: ${a.cart}`);
  if (!fs.existsSync(b.cart)) die(`missing cart: ${b.cart}`);

  const ha = new Host(a, cfg.bName);
  const hb = new Host(b, cfg.aName);
  const transcript = [];

  try {
    let speaker = a;
    let host = ha;
    let listener = b;
    let text = cfg.starter;

    for (let i = 0; i < cfg.turns; ++i){
      await host.send({ method: 'set_user', user_id: listener.name });
      const response = await host.send({ method: 'chat', text });
      const reply = response.reply || '';
      const state = await host.send({ method: 'state' });
      transcript.push({
        turn: i + 1,
        speaker: speaker.name,
        listener: listener.name,
        input: text,
        reply,
        audit_result: state.last_audit_result,
        intent: state.intent,
        mode: state.mode,
        frame: state.frame,
      });
      text = guidedInput(speaker.name, reply || '(silence)', i + 1);
      if (speaker === a){
        speaker = b; host = hb; listener = a;
      } else {
        speaker = a; host = ha; listener = b;
      }
    }

    const states = [];
    for (const h of [ha, hb]){
      const st = await h.send({ method: 'state' });
      states.push({ character: h.profile.name, ...st });
    }

    const metrics = scoreTranscript(transcript, states);
    const traces = {
      [a.name]: traceStats(a.name),
      [b.name]: traceStats(b.name),
    };
    const out = { config: cfg, temp_root: TMP_ROOT, transcript, states, metrics, traces };
    const outPath = process.env.PE_SOCIETY_JSON || path.join(TMP_ROOT, 'society_pair_probe.json');
    fs.writeFileSync(outPath, JSON.stringify(out, null, 2));

    console.log('--- society pair probe ---');
    console.log(`${cfg.aName} <-> ${cfg.bName}, turns=${cfg.turns}, backend=${cfg.backend}${cfg.model ? `, model=${cfg.model}` : ''}`);
    console.log(`temp_root=${TMP_ROOT}`);
    console.log('');
    for (const t of transcript){
      console.log(`${String(t.turn).padStart(2)} ${t.speaker} -> ${t.listener}: ${t.reply}`);
    }
    console.log('');
    console.log('metrics');
    for (const [k, v] of Object.entries(metrics)){
      console.log(`  ${k}: ${typeof v === 'object' ? JSON.stringify(v) : v}`);
    }
    for (const s of states){
      const rd = s.relation_dims || {};
      console.log(`  ${s.character}: turns=${s.turn_count} actor_tags=${s.actor_tagged_memories || 0} open_loops=${s.open_loop_count || 0} disposition=${s.disposition} trust=${rd.trust} resentment=${rd.resentment}`);
    }
    for (const [name, tr] of Object.entries(traces)){
      console.log(`  ${name} trace: dispatches=${tr.dispatches} provider_fallbacks=${tr.providerFallbacks} zero_len=${tr.zeroLength}`);
    }
    console.log(`json=${outPath}`);
  } finally {
    await ha.close();
    await hb.close();
  }
}

main().catch(e => { console.error(e.stack || e); process.exit(2); });
