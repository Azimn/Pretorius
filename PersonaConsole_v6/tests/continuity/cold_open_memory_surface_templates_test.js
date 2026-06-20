#!/usr/bin/env node
/* cold_open_memory_surface_templates_test.js
 *
 * Verifies that cold-open memory callbacks keep engine ownership while allowing
 * cartridges to author the final surface phrasing. Pretorius should use his
 * authored bank. Kiki, which does not author that bank yet, should fall back to
 * the generic engine phrasing without stacking greetings.
 */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const { resolveHost } = require('../host_path');

const ROOT = path.join(__dirname, '..', '..');
const HOST = resolveHost(ROOT);

function ok(cond, msg){
  if (!cond){
    console.error(`not ok: ${msg}`);
    process.exitCode = 1;
  } else {
    console.log(`ok:   ${msg}`);
  }
}

function copyCart(srcCart, name){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `persona-cold-surface-${name}-`));
  const cart = path.join(dir, path.basename(srcCart));
  fs.copyFileSync(srcCart, cart);
  return { dir, cart };
}

function runSession(cart, commands, clockMs){
  return new Promise((resolve, reject) => {
    const stdin = commands.map(c => JSON.stringify(c)).join('\n') + '\n{"method":"close"}\n';
    const proc = spawn(HOST, [cart, '--stdio'], {
      env: {
        ...process.env,
        PE_RENDER_BACKEND: 'template',
        PE_TODAY_SEED: '0x434f4c44',
        PE_CLOCK_OVERRIDE_MS: String(clockMs),
      },
    });
    let stdout = '', stderr = '';
    proc.stdout.on('data', d => stdout += d.toString('utf8'));
    proc.stderr.on('data', d => stderr += d.toString('utf8'));
    proc.on('error', reject);
    proc.on('close', status => {
      if (status !== 0) return reject(new Error(`host exited ${status}: ${stderr}`));
      resolve(stdout.split('\n').filter(l => l.startsWith('{')).map(l => JSON.parse(l)));
    });
    proc.stdin.write(stdin);
    proc.stdin.end();
    setTimeout(() => proc.kill('SIGKILL'), 30000).unref();
  });
}

function chats(rows){
  return rows.filter(r => Object.prototype.hasOwnProperty.call(r, 'reply'));
}

function words(text){
  return String(text || '').trim().split(/\s+/).filter(Boolean);
}

function noBadSurface(reply, label){
  ok(words(reply).length <= 24, `${label} cold-open stays short (${words(reply).length} words): ${reply}`);
  ok(!/good morning|good evening|hello again|welcome back/i.test(reply),
     `${label} cold-open does not stack a greeting: ${reply}`);
  ok(!/i remember that you said|according to my records|my memory shows|database|session one/i.test(reply),
     `${label} cold-open avoids database phrasing: ${reply}`);
  ok(!/as an ai|i'?m here to help|how can i assist|let me know/i.test(reply),
     `${label} cold-open avoids assistant phrasing: ${reply}`);
}

async function exercise({ label, cart, actor, plant, expectedSource, continuityTerms }){
  const t0 = 1700100000000;
  await runSession(cart, [
    { method: 'set_user', user_id: actor },
    { method: 'chat', text: plant[0] },
    { method: 'chat', text: plant[1] },
  ], t0);

  const rows = await runSession(cart, [
    { method: 'set_user', user_id: actor },
    { method: 'chat', text: 'Good evening.' },
    { method: 'state' },
  ], t0 + 4 * 60 * 60 * 1000);

  const cold = chats(rows)[0] || {};
  const state = cold.state || rows.find(r => r.character) || {};
  const reply = cold.reply || '';
  const lower = reply.toLowerCase();
  const continuity = continuityTerms.some(t => lower.includes(t.toLowerCase()));

  ok(reply.length > 0, `${label} produced cold-open reply: ${reply}`);
  ok(continuity, `${label} reply references actor or planted thread: ${reply}`);
  ok(state.cold_open_callback_exclusive === 1,
     `${label} memory callback owns the whole cold-open turn`);
  ok(state.cold_open_callback_source === expectedSource,
     `${label} source ${state.cold_open_callback_source} matches expected ${expectedSource}`);
  noBadSurface(reply, label);
  console.log(`${label}_cold_open_reply:`, reply);
  return { reply, state };
}

(async function main(){
  console.log('--- cold-open memory surface template test ---');
  const pretorius = copyCart(path.join(ROOT, 'profiles', 'pretorius', 'pretorius.cart'), 'pretorius');
  const kiki = copyCart(path.join(ROOT, 'profiles', 'kiki', 'kiki.cart'), 'kiki');
  try {
    await exercise({
      label: 'pretorius',
      cart: pretorius.cart,
      actor: 'Kiki',
      expectedSource: 2,
      plant: [
        "Hi Doctor Pretorius. I'm Kiki. I understand modern code, even when I sound like a mall princess.",
        "Your homunculi are disturbing, but I cannot stop thinking about them."
      ],
      continuityTerms: ['kiki', 'homunculi', 'thread', 'matter', 'table'],
    });

    await exercise({
      label: 'kiki-generic',
      cart: kiki.cart,
      actor: 'Jay',
      expectedSource: 1,
      plant: [
        "Hi Kiki. I'm Jay, and I keep thinking about entropy and old computers.",
        "I should go, but do not lose the thread about entropy."
      ],
      continuityTerms: ['jay', 'thread', 'entropy'],
    });
  } finally {
    fs.rmSync(pretorius.dir, { recursive: true, force: true });
    fs.rmSync(kiki.dir, { recursive: true, force: true });
  }

  if (process.exitCode) process.exit(process.exitCode);
  console.log('PASSED -- cartridge surfaces and generic fallback are both covered');
})().catch(e => { console.error(e); process.exit(2); });
