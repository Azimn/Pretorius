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
ok(/id="renderer-status"/.test(html), 'renderer status card exists');
ok(/id="renderer-label"/.test(html) && /id="renderer-detail"/.test(html), 'renderer status copy exists');
ok(/id="current-thought"/.test(html), 'inner-loop thought exists');
ok(/id="need-focus"/.test(html), 'focus meter exists');
ok(/id="need-energy"/.test(html), 'energy meter exists');
ok(/id="need-rapport"/.test(html), 'rapport meter exists');
ok(/id="prompt-character"/.test(html), 'manual prompt button exists');
ok(/id="character-select"/.test(html), 'character selector exists');
ok(/value="kiki"/.test(html) && /value="mentor"/.test(html), 'selector includes multiple cartridges');
ok(/id="proactive-enabled"/.test(html), 'proactive toggle exists');
ok(/id="speak-first"/.test(html), 'speak-first toggle exists');
ok(/id="idle-delay"/.test(html), 'idle delay selector exists');
ok(/character may speak during quiet/.test(html), 'quiet initiative copy exists');
ok(/character may speak first/.test(html), 'first-move copy exists');
ok(/id="import-memory-bundle"/.test(html), 'memory bundle import button exists');
ok(/id="reset-runtime"/.test(html), 'fresh-session reset button exists');

ok(/SETTINGS_KEY/.test(js) && /localStorage/.test(js), 'presence settings persist in localStorage');
ok(/WEB_USER_KEY/.test(js), 'web chat has a local actor identity key');
ok(/function ensureWebUser/.test(js), 'UI establishes local user identity');
ok(/fetch\("\/set_user"/.test(js), 'UI calls set_user endpoint before chat');
ok(/fetch\("\/idle_probe"/.test(js), 'UI calls idle_probe endpoint');
ok(/fetch\("\/load"/.test(js), 'UI calls load endpoint for cartridge switching');
ok(/postJson\("\/reset_runtime"/.test(js), 'UI can reset runtime state through host endpoint');
ok(/function loadCharacter/.test(js), 'UI has explicit character load transaction');
ok(/function resetRuntimeSession/.test(js), 'UI has explicit fresh-session reset transaction');
ok(/state check failed after load/.test(js), 'UI verifies state after load');
ok(/replaceChildren/.test(js), 'UI clears transcript on character switch');
ok(/Continuing the existing local/.test(js), 'UI warns when refresh resumes an existing local session');
ok(/allowFresh/.test(js), 'first-move path can use idle_probe before turn 1');
ok(/meta: "first move"/.test(js), 'first move is labeled in transcript');
ok(/sessionHasUserTurn/.test(js), 'UI tracks whether the user has engaged this browser session');
ok(/!sessionHasUserTurn && latestTurn > 0/.test(js), 'quiet proactive speech waits for user re-engagement on resumed sessions');
ok(/presenceFromState/.test(js), 'presence label derives from engine state');
ok(/rendererStatusFromState/.test(js), 'renderer badge derives from engine state');
ok(/renderer_mode/.test(js) && /renderer_model/.test(js), 'renderer badge reads mode and model');
ok(/thoughtFromState/.test(js), 'inner-loop thought derives from engine state');
ok(/updateInnerLife/.test(js), 'inner-loop meters derive from engine state');
ok(/manualProbeTurn/.test(js), 'manual prompt has per-turn cooldown');
ok(/obsession_pressure/.test(js), 'presence can surface preoccupation');
ok(/last_reply_had_question/.test(js), 'presence can surface waiting-on-user state');

ok(/\.presence/.test(css), 'presence badge has CSS');
ok(/\.renderer-status/.test(css), 'renderer status has CSS');
ok(/\.renderer-online/.test(css) && /\.renderer-offline/.test(css), 'renderer status supports online and offline states');
ok(/\.character-picker/.test(css), 'character picker has CSS');
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
