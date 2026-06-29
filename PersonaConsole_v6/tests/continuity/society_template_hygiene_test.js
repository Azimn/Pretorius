#!/usr/bin/env node
/* society_template_hygiene_test.js -- long template-only callback hygiene.
 *
 * Runs a 48-turn Pretorius/Kiki society probe and asserts the compositor does
 * not repeatedly surface the same reflection callback, leak raw slot/tag
 * punctuation artifacts, or fall into a tiny template-loop pocket.
 *
 * Repeat ceiling rationale:
 * - Exact repeat ceiling is zero. In a 48-turn two-character run each speaker
 *   only has 24 turns, and both showcase cartridges have enough generic and
 *   topic-specific templates that an exact full-line repeat means the selector
 *   bypassed blackout or the cartridge has an undersized high-traffic group.
 * - Opener repeat ceiling is two. Some human-ish discourse markers are allowed
 *   to recur once ("May I change...", "Wait..."), but three or more identical
 *   five-word openers in 48 turns reads like a loop rather than style.
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
const MAX_EXACT_REPEATS = Number(process.env.PE_SOCIETY_MAX_EXACT_REPEATS || 0);
const MAX_OPENER_REPEATS = Number(process.env.PE_SOCIETY_MAX_OPENER_REPEATS || 2);

function norm(s){ return String(s || "").toLowerCase().replace(/\s+/g, " ").trim(); }
function opener(s){ return norm(String(s || "").split(/\s+/).slice(0, 5).join(" ")); }
function duplicates(items){
  const seen = new Map();
  for (const item of items) seen.set(item, (seen.get(item) || 0) + 1);
  return [...seen.entries()].filter(([, count]) => count > 1)
    .map(([text, count]) => `${count}x ${text}`);
}

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
const exactRepeats = data.metrics && typeof data.metrics.exactRepeats === "number"
  ? data.metrics.exactRepeats
  : replies.length - new Set(replies.map(norm)).size;
const openerRepeats = data.metrics && typeof data.metrics.openerRepeats === "number"
  ? data.metrics.openerRepeats
  : replies.map(opener).filter(Boolean).length - new Set(replies.map(opener).filter(Boolean)).size;
const exactDetails = duplicates(replies.map(norm));
const openerDetails = duplicates(replies.map(opener).filter(Boolean));

ok(!duplicateReflection,
   duplicateReflection
     ? `reflection callback repeated: ${duplicateReflection}`
     : "each rendered reflection callback fires at most once in the 48-turn window");
ok(exactRepeats <= MAX_EXACT_REPEATS,
   exactRepeats <= MAX_EXACT_REPEATS
     ? `exact repeats within ceiling (${exactRepeats}/${MAX_EXACT_REPEATS})`
     : `exact repeats exceed ceiling (${exactRepeats}/${MAX_EXACT_REPEATS}): ${exactDetails.join(" | ")}`);
ok(openerRepeats <= MAX_OPENER_REPEATS,
   openerRepeats <= MAX_OPENER_REPEATS
     ? `opener repeats within ceiling (${openerRepeats}/${MAX_OPENER_REPEATS})`
     : `opener repeats exceed ceiling (${openerRepeats}/${MAX_OPENER_REPEATS}): ${openerDetails.join(" | ")}`);
ok(!/\{[a-z_]+\}/i.test(joined),
   "no unresolved {slot} placeholders in long transcript");
ok(!/\[[a-z_]+\]/i.test(joined),
   "no raw [tag] markers in long transcript");
ok(!/[!?]{2,}|\.{2,}/.test(joined),
   "no doubled terminal punctuation or ellipses in long transcript");
ok(!/[.!?],[.!?]?/.test(joined),
   "no punctuation collision before flourish commas");
ok(!/[\u2013\u2014]/.test(joined),
   "no en dash or em dash leaks into rendered transcript");
ok(!/\s[.!?](\s|$)/.test(joined),
   "no blank optional slot leaves a space before punctuation");
ok(!/(^|\s)[.!?][A-Za-z]/.test(joined),
   "no leading terminal punctuation stuck to a word");
ok(!/undefined|null/.test(joined),
   "no undefined/null text leaks in long transcript");

try { fs.unlinkSync(OUT); } catch {}
if (process.exitCode) process.exit(process.exitCode);
console.log("PASSED -- 48-turn society template hygiene stable");
