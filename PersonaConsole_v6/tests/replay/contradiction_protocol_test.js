#!/usr/bin/env node
/* contradiction_protocol_test.js -- V6 deterministic contradiction scaffold. */
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');
const { resolveHost } = require('../host_path');
const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);
const CART = path.join(ROOT, 'profiles', 'friendly', 'friendly.cart');
const CHDIR = path.dirname(CART);
function wipe(){ for(const f of ['state.bin','memory.bin','chapters.bin','actor_index.bin','speech_events.bin','dissonance.bin','open_loops.bin','speech_habits.bin']) try{fs.unlinkSync(path.join(CHDIR,f));}catch{}; for(const d of ['relations','aether']) try{fs.rmSync(path.join(CHDIR,d),{recursive:true,force:true});}catch{} }
function run(cmds,user='contradiction_user'){
 const r=spawnSync(HOST,[CART,'--stdio'],{input:cmds.map(JSON.stringify).join('\n')+'\n'+JSON.stringify({method:'close'})+'\n',encoding:'utf8',env:{...process.env,PE_TODAY_SEED:'0xC0DE',PE_CLOCK_OVERRIDE_MS:'1700000000000',PE_RENDER_BACKEND:'template'}});
 if(r.status!==0) throw new Error(r.stderr);
 return r.stdout.split('\n').filter(Boolean).map(l=>JSON.parse(l));
}
function ok(c,m){ if(!c){ console.error('not ok:',m); process.exitCode=1; } else console.log('ok:  ',m); }
console.log('--- V6 contradiction protocol scaffold ---');
wipe();
const rows=run([
 {method:'chat',text:'Remember that my notebook is blue.'},
 {method:'chat',text:'No, you are wrong. My notebook was never blue.'},
 {method:'chat',text:'You are making that up.'},
 {method:'idle_probe'},
 {method:'state'}
]);
const replies=rows.filter(r=>Object.prototype.hasOwnProperty.call(r,'reply')).map(r=>r.reply||'').join(' ').toLowerCase();
const states=rows.filter(r=>r && r.name); const s=states[states.length-1]||{};
ok(s.frame && s.frame.actor_id, 'canonical frame exposes actor for contradiction context');
ok(s.speech_event_count >= 3, 'speech ledger records symbolic acts through contradiction');
ok(typeof s.open_loop_count === 'number', 'open-loop count exposed for unresolved contradiction');
ok(/wrong|which part|not|remember|boundary|carefully|really|certain/.test(replies), 'reply uses challenge/clarify/refusal language instead of silent overwrite');
wipe();
if(process.exitCode) process.exit(process.exitCode);
console.log('PASSED -- contradiction scaffold remains symbolic and deterministic');
