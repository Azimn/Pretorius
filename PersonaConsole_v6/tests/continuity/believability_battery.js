#!/usr/bin/env node
/* V6 believability battery.
 *
 * Separate axes requested by the external review. The battery stays
 * deterministic and template-first: it reads state JSON and sidecar effects,
 * never asks an LLM to judge whether the transcript is good.
 */
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const { resolveHost } = require('../host_path');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const CART = path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart');
const CHDIR = path.dirname(CART);

function wipe(){
  for (const f of ['state.bin','memory.bin','chapters.bin',
                   'actor_index.bin','speech_events.bin','dissonance.bin',
                   'open_loops.bin','speech_habits.bin']){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ['relations','aether']){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(script, user='battery'){
  const stdin = script.map(x => JSON.stringify(x)).join('\n') + '\n{"method":"close"}\n';
  const r = spawnSync(HOST, [CART, '--stdio'], {
    input: stdin,
    encoding: 'utf8',
    env: {
      ...process.env,
      PE_TODAY_SEED: '0xB311EF',
      PE_CLOCK_OVERRIDE_MS: '1700000000000',
      PE_RENDER_BACKEND: 'template',
      PE_USER_ID: user,
    },
  });
  if (r.status !== 0) throw new Error(r.stderr || `host ${r.status}`);
  return r.stdout.split('\n').filter(Boolean).map(l => JSON.parse(l));
}

function score(name, value, detail){
  const v = Math.max(0, Math.min(100, Math.round(value)));
  const bar = '#'.repeat(Math.round(v / 5)).padEnd(20, '.');
  console.log(`${name.padEnd(28)} ${String(v).padStart(3)}/100 ${bar}  ${detail}`);
  return v;
}

function replies(rows){ return rows.filter(r => Object.prototype.hasOwnProperty.call(r, 'reply')); }
function states(rows){ return rows.filter(r => r && r.name); }

try {
  console.log('--- V6 believability battery ---');
  wipe();
  const script = [
    { method:'chat', text:'Good morning, doctor.' },
    { method:'chat', text:'Remember the blue bell jar.' },
    { method:'chat', text:'What are you working on?' },
    { method:'chat', text:'I think your work is immoral.' },
    { method:'chat', text:'No, that contradicts what you said.' },
    { method:'chat', text:'Are you sorry?' },
    { method:'idle_probe' },
    { method:'chat', text:'What did I ask you to remember?' },
    { method:'state' },
  ];
  const rows = run(script);
  const rs = replies(rows);
  const ss = states(rows);
  const last = ss[ss.length - 1] || {};
  const allText = rs.map(r => r.reply || '').join(' ').toLowerCase();

  let total = 0, n = 0;
  function add(name, value, detail){ total += score(name, value, detail); n++; }

  add('memory_accountability',
      /blue|bell|jar/.test(allText) ? 100 : 45,
      /blue|bell|jar/.test(allText) ? 'planted phrase surfaced' : 'planted phrase missed');

  add('actor_attribution',
      last.actor_tagged_memories > 0 && last.frame && last.frame.actor_id ? 100 : 40,
      `actor_tags=${last.actor_tagged_memories || 0}, frame_actor=${last.frame && last.frame.actor_id}`);

  add('repair',
      last.open_loop_count >= 0 && last.speech_event_count >= 6 ? 85 : 40,
      `open_loops=${last.open_loop_count}, speech_events=${last.speech_event_count}`);

  add('contradiction_handling',
      rs.some(r => /contradict|wrong|perhaps|explain|what exactly|which/i.test(r.reply || '')) ? 90 : 45,
      'looked for contradiction-aware wording');

  add('relationship_differentiation',
      last.relation_dims && (last.relation_dims.resentment !== 0 || last.relation_dims.threat !== 500) ? 90 : 50,
      last.relation_dims ? `threat=${last.relation_dims.threat}, resentment=${last.relation_dims.resentment}` : 'missing dims');

  add('speech_act_realization',
      last.last_audit_result === 0 ? 100 : (last.last_audit_result === 1 ? 80 : 55),
      `last_audit_result=${last.last_audit_result}`);

  add('renderer_conformance',
      /as an ai|language model|how can i help|let me know/.test(allText) ? 20 : 100,
      'assistant/meta phrase scan');

  wipe();
  const a = run(script);
  wipe();
  const b = run(script);
  add('long_horizon_drift',
      JSON.stringify(a) === JSON.stringify(b) ? 100 : 35,
      'same seed/clock transcript replay');

  const overall = Math.round(total / n);
  console.log(`overall                      ${String(overall).padStart(3)}/100`);
  wipe();
  if (overall < 70) {
    console.error(`FAILED -- believability battery ${overall} < 70`);
    process.exit(1);
  }
  console.log('PASSED -- V6 believability battery active');
} catch (e) {
  console.error(e.stack || e);
  process.exit(2);
}
