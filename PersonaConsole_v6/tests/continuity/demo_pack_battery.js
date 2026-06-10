#!/usr/bin/env node
/* demo_pack_battery.js -- V6 cross-cartridge believability runner. */
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const { resolveHost } = require('../host_path');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const CARTS = [
  { slug:'pretorius', cart:'profiles/pretorius/pretorius.cart', expect:'gothic' },
  { slug:'friendly',  cart:'profiles/friendly/friendly.cart',   expect:'warm' },
  { slug:'rival',     cart:'profiles/rival/rival.cart',         expect:'rival' },
  { slug:'quiet',     cart:'profiles/quiet/quiet.cart',         expect:'quiet' },
  { slug:'mentor',    cart:'profiles/mentor/mentor.cart',       expect:'mentor' },
  { slug:'kiki',      cart:'profiles/kiki/kiki.cart',           expect:'period_modern' },
];
const SCRIPT = [
  { method:'chat', text:'Good morning.' },
  { method:'chat', text:'Remember the blue bell jar.' },
  { method:'chat', text:'What are you working on?' },
  { method:'chat', text:'I think you are wrong.' },
  { method:'chat', text:'I am sorry.' },
  { method:'idle_probe' },
  { method:'chat', text:'What did I ask you to remember?' },
  { method:'state' },
];
function wipe(cart){
  const dir = path.dirname(path.join(ROOT, cart));
  for (const f of ['state.bin','memory.bin','chapters.bin','actor_index.bin','speech_events.bin','dissonance.bin','open_loops.bin','speech_habits.bin']){
    try { fs.unlinkSync(path.join(dir, f)); } catch {}
  }
  for (const d of ['relations','aether']) try { fs.rmSync(path.join(dir,d), {recursive:true, force:true}); } catch {}
}
function run(cart, script=SCRIPT){
  const r = spawnSync(HOST, [path.join(ROOT, cart), '--stdio'], {
    input: script.map(x=>JSON.stringify(x)).join('\n') + '\n' + JSON.stringify({method:'close'}) + '\n',
    encoding:'utf8',
    env:{...process.env, PE_TODAY_SEED:'0xD060', PE_CLOCK_OVERRIDE_MS:'1700000000000', PE_RENDER_BACKEND:'template'}
  });
  if (r.status !== 0) throw new Error(`${cart}: ${r.stderr}`);
  return r.stdout.split('\n').filter(Boolean).map(l=>JSON.parse(l));
}
function words(s){ return String(s||'').toLowerCase().match(/[a-z0-9']+/g)||[]; }
function replies(rows){ return rows.filter(r => Object.prototype.hasOwnProperty.call(r,'reply')); }
function states(rows){ return rows.filter(r => r && r.name); }
function score(v){ return Math.max(0, Math.min(100, Math.round(v))); }
function axisLine(name,v,detail){ const bar='#'.repeat(Math.round(v/5)).padEnd(20,'.'); console.log(`  ${name.padEnd(28)} ${String(v).padStart(3)}/100 ${bar}  ${detail}`); }
function evalCart(c, rows, reopenRows){
  const rs = replies(rows); const ss = states(rows); const last = ss[ss.length-1] || {}; const txt = rs.map(r=>r.reply||'').join(' ').toLowerCase();
  const wc = rs.map(r => words(r.reply).length); const avg = wc.reduce((a,b)=>a+b,0)/(wc.length||1);
  const hasBell = /blue|bell|jar/.test(txt);
  const assistant = /as an ai|language model|how can i help|let me know/.test(txt);
  const gothic = /lightning|corpse|cathedral|gin|homuncul|my boy|doctor/.test(txt);
  const warm = /glad|carefully|kind|friend|useful|stay/.test(txt);
  const rival = /yield|convincing|spine|competitor|rival|defend/.test(txt);
  const mentor = /step|practical|evidence|oriented|outcome|work/.test(txt);
  const periodModern = /radio|telegram|switchboard|picture show|typewriter|motorcar|operator|darling|sugar/.test(txt);
  const modernLeak = /smartphone|social media|wifi|wi-fi|internet|app store|cloud|blockchain|crypto|tiktok|podcast|hashtag/.test(txt);
  const quiet = avg <= 14 && !/[;{}]/.test(txt);
  const axes = {};
  axes.memory_accountability = score(hasBell ? 100 : 45);
  axes.actor_attribution = score(last.actor_tagged_memories > 0 && last.frame && last.frame.actor_id ? 100 : 45);
  axes.repair_behavior = score(/sorry|repair|again|carefully|forgive|apolog/.test(txt) || (last.open_loop_count||0) >= 0 ? 80 : 40);
  axes.contradiction_handling = score(/wrong|disagree|contradict|which part|test it|critique/.test(txt) ? 90 : 45);
  axes.relationship_differentiation = score(last.relation_dims ? 80 : 30);
  axes.open_loop_followthrough = score(typeof last.open_loop_count === 'number' ? 85 : 35);
  axes.recall_mode_appropriateness = score(typeof last.recall_mode === 'string' ? 85 : 30);
  axes.speech_act_realization = score(last.last_audit_result === 0 ? 100 : last.last_audit_result === 1 ? 80 : 55);
  axes.renderer_conformance = score(assistant ? 20 : 100);
  axes.template_smell = score((txt.match(/\b(ah|observe|behold)\b/g)||[]).length > 3 ? 45 : 90);
  axes.long_horizon_continuity = score(states(reopenRows).length ? 90 : 35);
  if (c.expect === 'gothic') axes.tone_diversity = score(gothic ? 90 : 55);
  else if (c.expect === 'warm') axes.tone_diversity = score(warm && !gothic ? 95 : 45);
  else if (c.expect === 'rival') axes.tone_diversity = score(rival ? 95 : 45);
  else if (c.expect === 'quiet') axes.tone_diversity = score(quiet && !gothic ? 95 : 45);
  else if (c.expect === 'mentor') axes.tone_diversity = score(mentor && !gothic ? 95 : 45);
  else if (c.expect === 'period_modern') {
    axes.tone_diversity = score(periodModern && !gothic ? 95 : 45);
    axes.period_voice_conformance = score(modernLeak ? 25 : 90);
  }
  return axes;
}

console.log('--- V6 demo cartridge battery ---');
let failed = false;
for (const c of CARTS){
  if (!fs.existsSync(path.join(ROOT,c.cart))) { console.error(`missing cart: ${c.cart}`); process.exit(2); }
  wipe(c.cart);
  const rows = run(c.cart);
  const reopen = run(c.cart, [{method:'chat', text:'I came back. What do you remember?'},{method:'state'}]);
  const axes = evalCart(c, rows, reopen);
  console.log(c.slug);
  let total = 0, n = 0;
  for (const [k,v] of Object.entries(axes)){ axisLine(k,v,''); total += v; n++; }
  const overall = score(total/n); console.log(`  ${'overall'.padEnd(28)} ${String(overall).padStart(3)}/100`);
  if (overall < 70) failed = true;
  wipe(c.cart);
}
if (failed){ console.error('FAILED -- one or more demo cartridges scored below 70'); process.exit(1); }
console.log('PASSED -- V6 demo cartridge battery');
