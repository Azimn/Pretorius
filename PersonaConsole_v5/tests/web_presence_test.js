#!/usr/bin/env node
/* web_presence_test.js -- static guard for the proactive-presence UI.
 *
 * The browser UI is intentionally dependency-free, so this test keeps the
 * contract simple: required controls exist, app.js calls idle_probe, and
 * the settings are persisted client-side.
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(ROOT, 'bridges', 'web', 'index.html'), 'utf8');
const js = fs.readFileSync(path.join(ROOT, 'bridges', 'web', 'app.js'), 'utf8');
const css = fs.readFileSync(path.join(ROOT, 'bridges', 'web', 'style.css'), 'utf8');

let fails = 0;
function ok(cond, msg){
  if (cond) console.log('ok:  ', msg);
  else { console.error('FAIL:', msg); fails++; }
}

console.log('--- web proactive presence test ---');
ok(/id="presence"/.test(html), 'presence badge exists');
ok(/id="current-thought"/.test(html), 'inner-loop thought exists');
ok(/id="need-focus"/.test(html), 'focus meter exists');
ok(/id="need-energy"/.test(html), 'energy meter exists');
ok(/id="need-rapport"/.test(html), 'rapport meter exists');
ok(/id="prompt-character"/.test(html), 'manual prompt button exists');
ok(/id="proactive-enabled"/.test(html), 'proactive toggle exists');
ok(/id="speak-first"/.test(html), 'speak-first toggle exists');
ok(/id="idle-delay"/.test(html), 'idle delay selector exists');
ok(/character may speak during quiet/.test(html), 'quiet initiative copy exists');
ok(/character may speak first/.test(html), 'first-move copy exists');

ok(/SETTINGS_KEY/.test(js) && /localStorage/.test(js), 'presence settings persist in localStorage');
ok(/fetch\("\/idle_probe"/.test(js), 'UI calls idle_probe endpoint');
ok(/allowFresh/.test(js), 'first-move path can use idle_probe before turn 1');
ok(/meta: "first move"/.test(js), 'first move is labeled in transcript');
ok(/presenceFromState/.test(js), 'presence label derives from engine state');
ok(/thoughtFromState/.test(js), 'inner-loop thought derives from engine state');
ok(/updateInnerLife/.test(js), 'inner-loop meters derive from engine state');
ok(/manualProbeTurn/.test(js), 'manual prompt has per-turn cooldown');
ok(/obsession_pressure/.test(js), 'presence can surface preoccupation');
ok(/last_reply_had_question/.test(js), 'presence can surface waiting-on-user state');

ok(/\.presence/.test(css), 'presence badge has CSS');
ok(/\.inner-life/.test(css), 'inner-loop panel has CSS');
ok(/\.meter/.test(css), 'need meters have CSS');
ok(/\.ghost-button/.test(css), 'manual prompt button has CSS');
ok(/\.settings/.test(css), 'presence settings have CSS');
ok(/\.msg\.idle/.test(css), 'idle messages are visually distinct');

if (fails){
  console.error(`FAILED -- ${fails} web presence assertion(s) failed`);
  process.exit(1);
}
console.log('PASSED -- web proactive presence hooks intact');
