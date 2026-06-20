#!/usr/bin/env node
/* society_template_hygiene_test.js -- long template-only callback hygiene.
 *
 * Runs a 48-turn Pretorius/Kiki society probe and asserts the compositor does
 * not repeatedly surface the same reflection callback or leak raw slot/tag
 * punctuation artifacts into the transcript.
 */
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

const ROOT = path.join(__dirname, "..", "..");
const OUT = path.join(os.tmpdir(), `persona-society-hygiene-${Date.now()}.json`);

function ok(cond, msg){
  if (cond) console.log(`ok:   ${msg}`);
  else { console.error(`FAIL: ${msg}`); process.exitCode = 1; }
}

const env = {
  ...process.env,
  PE_RENDER_BACKEND: "template",
  PE_SOCIETY_TURNS: "48",
  PE_SOCIETY_JSON: OUT,
  PE_TODAY_SEED: "0x50C1E7",
};

const res = spawnSync(process.execPath, [
  path.join(ROOT, "tests", "continuity", "society_pair_probe.js")
], { cwd: ROOT, env, encoding: "utf8", timeout: 180000 });

if (res.status !== 0){
  console.error(res.stdout || "");
  console.error(res.stderr || "");
  process.exit(res.status || 2);
}

const data = JSON.parse(fs.readFileSync(OUT, "utf8"));
const replies = data.transcript.map(t => String(t.reply || ""));
const joined = replies.join("\n");

const reflectionLines = replies
  .map(s => s.trim().toLowerCase().replace(/\s+/g, " "))
  .filter(s =>
    s.includes("always the work returns") ||
    s.includes("always physics returns") ||
    s.includes("begin to think you sense its weight") ||
    s.includes("there is a pattern in how we talk") ||
    s.includes("we keep arriving at"));
const duplicateReflection = reflectionLines.find((line, idx) =>
  reflectionLines.indexOf(line) !== idx);

ok(!duplicateReflection,
   duplicateReflection
     ? `reflection callback repeated: ${duplicateReflection}`
     : "each rendered reflection callback fires at most once in the 48-turn window");
ok(!/\{[a-z_]+\}/i.test(joined),
   "no unresolved {slot} placeholders in long transcript");
ok(!/\[[a-z_]+\]/i.test(joined),
   "no raw [tag] markers in long transcript");
ok(!/[!?]{2,}|\.{2,}/.test(joined),
   "no doubled terminal punctuation or ellipses in long transcript");
ok(!/(^|\s)[.!?][A-Za-z]/.test(joined),
   "no leading terminal punctuation stuck to a word");
ok(!/undefined|null/.test(joined),
   "no undefined/null text leaks in long transcript");

try { fs.unlinkSync(OUT); } catch {}
if (process.exitCode) process.exit(process.exitCode);
console.log("PASSED -- 48-turn society template hygiene stable");
