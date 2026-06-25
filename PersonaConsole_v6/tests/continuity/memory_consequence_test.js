#!/usr/bin/env node
/* memory_consequence_test.js -- selected memories change posture, not just text.
 *
 * The same neutral follow-up sequence is run twice. One run has no planted
 * user-pinned memory. The other starts with an explicit "remember this"
 * request. The flagged memory should win attention and change the next
 * neutral turn's intent/stance/rhetorical mode through the consequence mapper.
 */
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawn } = require("child_process");
const { resolveHost } = require("../host_path");

const ROOT = path.join(__dirname, "..", "..");
const HOST = resolveHost(ROOT);

const PROFILES = [
  { slug: "kiki", cart: path.join(ROOT, "profiles", "kiki", "kiki.cart"), actor: "KikiTester" },
  { slug: "mentor", cart: path.join(ROOT, "profiles", "mentor", "mentor.cart"), actor: "MentorTester" },
];

function makeTempCart(profile, label){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `persona-memory-consequence-${label}-`));
  const cart = path.join(dir, `${profile.slug}.cart`);
  fs.copyFileSync(profile.cart, cart);
  return cart;
}

function runScenario({ profile, label, plant }){
  return new Promise((resolve, reject) => {
    const cart = makeTempCart(profile, label);
    const proc = spawn(HOST, ["--stdio", cart], {
      env: {
        ...process.env,
        PE_RENDER_BACKEND: "template",
        PE_TODAY_SEED: "0x6d656d63",
      },
    });
    let stdout = "";
    let stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", status => {
      if (status !== 0) {
        reject(new Error(`${label} exited ${status}: ${stderr}`));
        return;
      }
      const rows = stdout.split("\n")
        .filter(l => l.trim().startsWith("{"))
        .map(JSON.parse)
        .filter(r => Object.prototype.hasOwnProperty.call(r, "reply"));
      resolve(rows);
    });

    const turns = [
      { method: "set_user", user_id: profile.actor },
    ];
    if (plant) {
      turns.push({
        method: "chat",
        text: "Remember this: the blue bell jar made me feel safe and curious.",
      });
    }
    for (let i = 0; i < 14; ++i) {
      turns.push({ method: "chat", text: "The room is quiet." });
    }
    turns.push({ method: "close" });
    proc.stdin.write(turns.map(JSON.stringify).join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 20000).unref();
  });
}

function pinnedConsequence(rows){
  let found = null;
  rows.forEach((r, index) => {
    if (r.state && (r.state.last_callback_memory_flags & 1)) {
      found = { row: r, index };
    }
  });
  return found;
}

(async function main(){
  let fail = 0;
  function ok(cond, msg){
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }

  for (const profile of PROFILES) {
    const baselineRows = await runScenario({ profile, label: `${profile.slug}-baseline`, plant: false });
    const plantedRows = await runScenario({ profile, label: `${profile.slug}-planted`, plant: true });
    const baselinePinned = pinnedConsequence(baselineRows);
    const plantedPinned = pinnedConsequence(plantedRows);

    ok(!baselinePinned, `${profile.slug}: baseline has no user-pinned callback`);
    ok(!!plantedPinned, `${profile.slug}: user-pinned memory surfaces without direct prompting`);
    if (!plantedPinned) continue;

    const planted = plantedPinned.row;
    const baseline = baselineRows[plantedPinned.index] || null;
    const st = planted.state || {};
    const fr = st.frame || {};
    const changedFromBaseline = !baseline ||
      baseline.state.intent !== st.intent ||
      (baseline.state.frame && baseline.state.frame.stance) !== fr.stance ||
      (baseline.state.frame && baseline.state.frame.rhetorical_mode) !== fr.rhetorical_mode;

    ok(changedFromBaseline,
       `${profile.slug}: selected memory changes behavior pressure (baseline=${baseline ? baseline.state.intent + "/" + baseline.state.frame.stance + "/" + baseline.state.frame.rhetorical_mode : "none"} planted=${st.intent}/${fr.stance}/${fr.rhetorical_mode})`);
    ok(st.intent === "probe" || st.intent === "reminisce" || st.intent === "attend" || st.intent === "redirect",
       `${profile.slug}: memory consequence yields a behavioral intent (${st.intent})`);
    ok(fr.stance !== undefined && fr.rhetorical_mode,
       `${profile.slug}: memory consequence is visible in turn frame (stance=${fr.stance}, rhet=${fr.rhetorical_mode})`);
    ok(/blue bell jar|safe|curious/i.test(planted.reply || "") ||
       st.intent === "probe" ||
       st.intent === "attend" ||
       st.intent === "redirect",
       `${profile.slug}: reply or frame reflects consequence, not a bare callback (${planted.reply})`);
  }

  if (fail) process.exit(1);
  console.log("PASSED -- memory consequence changes offline behavior");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
