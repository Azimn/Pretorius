#!/usr/bin/env node
/* long_gap_decay_test.js -- host-level proof that long gaps do not collapse. */
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

const ROOT = path.join(__dirname, "..", "..");
const DOC = path.join(os.tmpdir(), `persona-long-gap-${Date.now()}.md`);

function ok(cond, msg){
  if (cond) console.log(`ok:   ${msg}`);
  else { console.error(`FAIL: ${msg}`); process.exitCode = 1; }
}

const res = spawnSync(process.execPath, [
  path.join(ROOT, "tests", "continuity", "long_gap_probe.js")
], {
  cwd: ROOT,
  encoding: "utf8",
  timeout: 180000,
  env: { ...process.env, PE_LONG_GAP_DOC: DOC },
});

if (res.status !== 0){
  console.error(res.stdout || "");
  console.error(res.stderr || "");
  process.exit(res.status || 2);
}

const text = fs.readFileSync(DOC, "utf8");
const rows = text.split(/\r?\n/)
  .filter(l => /^\| /.test(l) && !/---/.test(l) && !/Gap \|/.test(l))
  .map(l => l.split("|").slice(1, -1).map(x => x.trim()));

const byGap = new Map(rows.map(r => [r[0], r]));
const month = byGap.get("1 month");
const one = byGap.get("1 year");
const three = byGap.get("3 years");

ok(month && one && three, "probe produced 1-month, 1-year, and 3-year rows");
if (month && one){
  const monthMood = Number(month[7]);
  const oneMood = Number(one[7]);
  const monthSchema = [10, 11, 12, 13].map(i => Number(month[i])).join("/");
  const oneSchema = [10, 11, 12, 13].map(i => Number(one[i])).join("/");
  ok(monthMood !== oneMood || monthSchema !== oneSchema,
     `30-day and 365-day schema/mood state remains differentiated (${monthSchema}/${monthMood} vs ${oneSchema}/${oneMood})`);
  ok(Number(month[3]) === Number(one[3]) && Number(month[5]) === Number(one[5]),
     "bond-like trust and intimacy dimensions resist long-gap softening");
  ok(Number(one[6]) < Number(month[6]) && Number(one[4]) <= Number(month[4]),
     `single-event resentment/threat soften over months (${month[6]}/${month[4]} -> ${one[6]}/${one[4]})`);
}
if (one && three){
  const oneDisposition = Number(one[2]);
  const threeDisposition = Number(three[2]);
  const oneMood = Number(one[7]);
  const threeMood = Number(three[7]);
  const oneDrive = one[9];
  const threeDrive = three[9];
  ok(oneDisposition !== threeDisposition || oneMood !== threeMood || oneDrive !== threeDrive,
     `1-year and 3-year gaps produce different decayed state (disp ${oneDisposition} vs ${threeDisposition})`);
}

for (const label of ["1 month", "6 months", "1 year", "3 years"]){
  const r = byGap.get(label);
  ok(r && Number(r[14]) >= 1 && Number(r[15]) >= 800,
     `${label} keeps confirmed learned knowledge confidence available`);
}

try { fs.unlinkSync(DOC); } catch {}
if (process.exitCode) process.exit(process.exitCode);
console.log("PASSED -- V6 long-gap host decay remains differentiated");
