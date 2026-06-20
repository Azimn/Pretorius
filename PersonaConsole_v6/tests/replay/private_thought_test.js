#!/usr/bin/env node
/* private_thought_test.js -- V6 Phase 5d private thought frame.
 *
 * The private-thought frame is Layer-1 diagnostic state. It is not a hidden
 * renderer monologue and it is not prose memory. This test proves the engine
 * keeps an internal pressure frame distinct from the expressed speech act,
 * exposes it for debugging, and does not leak debug labels into dialogue.
 */
"use strict";

const fs = require("fs");
const path = require("path");
const { spawn } = require("child_process");
const { resolveHost } = require("../host_path");

const ROOT = path.join(__dirname, "..", "..");
const HOST = resolveHost(ROOT);
const CART = path.join(ROOT, "profiles", "pretorius", "pretorius.cart");
const CHDIR = path.dirname(CART);

function wipe(){
  for (const f of [
    "state.bin", "memory.bin", "chapters.bin", "reflections.bin",
    "actor_index.bin", "speech_events.bin", "dissonance.bin",
    "open_loops.bin", "speech_habits.bin", "learned_knowledge.bin"
  ]){
    try { fs.unlinkSync(path.join(CHDIR, f)); } catch {}
  }
  for (const d of ["relations", "aether", "learned_knowledge"]){
    try { fs.rmSync(path.join(CHDIR, d), { recursive: true, force: true }); } catch {}
  }
}

function runSession(commands){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [CART, "--stdio"], {
      env: {
        ...process.env,
        PE_TODAY_SEED: "5151",
        PE_CLOCK_OVERRIDE_MS: "1700000000000",
        PE_RENDER_BACKEND: "template",
      },
    });
    let stdout = "", stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", code => {
      if (code !== 0) reject(new Error(`host exit ${code}: ${stderr}`));
      else resolve(stdout.split(/\r?\n/).filter(Boolean).map(l => {
        try { return JSON.parse(l); } catch { return { parse_error: l }; }
      }));
    });
    proc.stdin.write(commands.map(c => JSON.stringify(c)).join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 30000);
  });
}

function ok(cond, msg){
  if (cond) console.log(`ok:   ${msg}`);
  else { console.error(`FAIL: ${msg}`); process.exitCode = 1; }
}

function lastReply(rows){
  for (let i = rows.length - 1; i >= 0; --i)
    if (rows[i] && typeof rows[i].reply === "string") return rows[i].reply;
  return "";
}

function lastState(rows){
  for (let i = rows.length - 1; i >= 0; --i)
    if (rows[i] && rows[i].private_thought) return rows[i];
  return null;
}

(async function main(){
  console.log("--- V6 Phase 5d private thought frame ---");
  wipe();
  const rows = await runSession([
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "You are dodging the question. Are you real, or just pretending?" },
    { method: "state" },
    { method: "close" },
  ]);
  const reply = lastReply(rows);
  const state = lastState(rows);
  ok(state, "state exposes private_thought object");
  ok(state && typeof state.private_thought.kind === "string",
     "private_thought has an internal kind label");
  ok(state && typeof state.private_thought.expressed === "string",
     "private_thought has an expressed speech-act label");
  ok(state && state.private_thought.internal_hash !== state.private_thought.expressed_hash,
     "internal frame hash differs from expressed output frame hash");
  ok(state && state.private_thought.pressure >= 0,
     "private thought pressure is bounded numeric state");
  ok(reply && !/private_thought|internal_hash|expressed_hash|dissonance_gaps|SELF_MODEL/.test(reply),
     "rendered reply does not leak private diagnostic frame");
  wipe();
  if (process.exitCode) process.exit(process.exitCode);
  console.log("PASSED -- V6 private thought frame stays internal and inspectable");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
