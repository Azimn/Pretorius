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
const one = byGap.get("1 year");
const three = byGap.get("3 years");

ok(one && three, "probe produced 1-year and 3-year rows");
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
