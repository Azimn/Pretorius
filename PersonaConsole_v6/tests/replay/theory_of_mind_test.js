#!/usr/bin/env node
/* theory_of_mind_test.js -- V6 per-actor inferred mind state. */
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawn } = require("child_process");
const { resolveHost } = require("../host_path");

const ROOT = path.join(__dirname, "..", "..");
const HOST = resolveHost(ROOT);
const SRC = path.join(ROOT, "profiles", "pretorius");
const CHDIR = path.join(os.tmpdir(), "persona-theory-of-mind-profile");
const CART = path.join(CHDIR, "pretorius.cart");

function resetProfile(){
  fs.rmSync(CHDIR, { recursive: true, force: true });
  fs.cpSync(SRC, CHDIR, { recursive: true });
}

function wipe(){
  for (const f of ["state.bin","memory.bin","chapters.bin","reflections.bin",
                   "actor_index.bin","speech_events.bin","dissonance.bin",
                   "open_loops.bin","speech_habits.bin","learned_knowledge.bin"]){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ["relations","aether"]){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function run(commands){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, "--stdio"], {
      env: { ...process.env, PE_TODAY_SEED: "6161", PE_CLOCK_OVERRIDE_MS: "1700000000000" },
    });
    let stdout = "", stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", code => {
      if (code !== 0) reject(new Error(`host exit ${code}: ${stderr}`));
      else resolve(stdout.split(/\r?\n/).filter(Boolean).map(l => {
        try { return JSON.parse(l); } catch { return null; }
      }).filter(Boolean));
    });
    proc.stdin.write(commands.map(c => JSON.stringify(c)).join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 30000).unref();
  });
}

function state(rows){
  for (let i = rows.length - 1; i >= 0; --i)
    if (rows[i] && rows[i].theory_of_mind) return rows[i];
  return null;
}

function ok(cond, msg){
  if (cond) console.log(`ok:   ${msg}`);
  else { console.error(`FAIL: ${msg}`); process.exitCode = 1; }
}

(async function main(){
  console.log("--- V6 theory of mind ---");
  resetProfile();
  wipe();
  const praise = [];
  for (let i = 0; i < 8; ++i) praise.push({ method: "chat", text: "Your work is brilliant." });
  praise.push({ method: "state" });
  let s = state(await run([{ method: "set_user", user_id: "Kiki" }, ...praise, { method: "close" }]));
  ok(s && s.theory_of_mind, "state exposes theory_of_mind object");
  ok(s && s.theory_of_mind.believed_valence > 20,
     `repeated praise builds positive believed_valence (${s && s.theory_of_mind.believed_valence})`);
  ok(s && s.theory_of_mind.confidence > 160,
     `belief confidence rises (${s && s.theory_of_mind.confidence})`);

  s = state(await run([
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Actually, your work is worthless and you are wrong." },
    { method: "state" },
    { method: "close" },
  ]));
  ok(s && s.theory_of_mind.mismatch_count > 0,
     `mismatch fires when observed input contradicts believed actor mood (${s && s.theory_of_mind.mismatch_count})`);
  const tomFiles = fs.existsSync(path.join(CHDIR, "relations"))
    ? fs.readdirSync(path.join(CHDIR, "relations")).filter(f => f.endsWith(".tom"))
    : [];
  ok(tomFiles.length >= 1, "per-actor .tom sidecar persisted");
  fs.rmSync(CHDIR, { recursive: true, force: true });
  if (process.exitCode) process.exit(process.exitCode);
  console.log("PASSED -- V6 ToM belief state diverges and mismatch is detected");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
