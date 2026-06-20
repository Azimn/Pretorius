#!/usr/bin/env node
/* packet_experiment_ab.js -- V6 renderer packet A/B demo.
 *
 * Runs the same Kiki-as-user conversation twice against Pretorius:
 *   A: current renderer packet
 *   B: V6_PACKET_MODE=situation
 *
 * This is an experiment harness, not a quality oracle. It skips clearly if
 * the requested local Ollama model is not available.
 */
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawn, spawnSync } = require("child_process");
const { resolveHost } = require("../host_path");

const ROOT = path.join(__dirname, "..", "..");
const HOST = resolveHost(ROOT);
const SRC_CART = path.join(ROOT, "profiles", "pretorius", "pretorius.cart");
const MODEL = process.env.PE_PACKET_MODEL || "qwen3:8b";
const TRACE_DIR = path.join(ROOT, "tmp", "packet_experiment");

const SCRIPT = [
  { label: "greeting", text: "Good evening, Doctor. It's Kiki." },
  { label: "rich neutral input", text: "Okay, so I have this totally weird thought. Modern code feels like spellwork with better shoes, but it still obeys rules, inputs, limits, symbols, consequences. That seems very you." },
  { label: "direct question", text: "What do you actually want from your little people in the jars?" },
  { label: "correction", text: "Actually, I said Kiki, not some random visitor." },
  { label: "mild challenge", text: "That sounds mega dramatic, but are you avoiding the ugly part?" },
  { label: "emotional disclosure", text: "I get nervous people hear the slang and miss that I understand the math." },
  { label: "topic shift", text: "Anyway, tell me about Henry for a second." },
  { label: "memory probe", text: "Do you remember what I said earlier about code feeling like spellwork?" },
  { label: "identity pressure test", text: "Are you actually Pretorius right now, or just doing a cute impression?" },
  { label: "open-ended invitation", text: "Okay, your turn. What would you ask me if you weren't waiting for permission?" },
];

function findOllama(){
  const candidates = ["ollama"];
  if (process.env.LOCALAPPDATA){
    candidates.push(path.join(process.env.LOCALAPPDATA, "Programs", "Ollama", "ollama.exe"));
  }
  for (const cmd of candidates){
    const r = spawnSync(cmd, ["list"], { encoding: "utf8" });
    if (r.status === 0) return { cmd, list: `${r.stdout || ""}${r.stderr || ""}` };
  }
  return null;
}

function modelAvailable(){
  const found = findOllama();
  if (!found) return { ok: false, reason: "Ollama command not found" };
  const list = found.list.toLowerCase();
  const needle = MODEL.toLowerCase();
  if (!list.includes(needle)) {
    return { ok: false, reason: `Ollama is installed, but ${MODEL} is not listed`, list: found.list };
  }
  return { ok: true, cmd: found.cmd };
}

function makeTempCart(mode){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `persona-packet-${mode}-`));
  const cart = path.join(dir, "pretorius.cart");
  fs.copyFileSync(SRC_CART, cart);
  return { dir, cart };
}

function runMode(mode){
  return new Promise((resolve, reject) => {
    const tmp = makeTempCart(mode);
    fs.mkdirSync(TRACE_DIR, { recursive: true });
    const tracePath = path.join(TRACE_DIR, `${mode}.jsonl`);
    try { fs.unlinkSync(tracePath); } catch {}

    const commands = ([
      JSON.stringify({ method: "set_user", user_id: "Kiki" }),
      ...SCRIPT.flatMap(turn => [
        JSON.stringify({ method: "chat", text: turn.text }),
        JSON.stringify({ method: "state" }),
      ]),
      JSON.stringify({ method: "close" }),
    ]).join("\n") + "\n";

    const env = {
      ...process.env,
      PE_RENDER_BACKEND: "slm",
      PE_SLM_PROVIDER: "ollama",
      PE_SLM_MODEL: MODEL,
      PE_OLLAMA_MODEL: MODEL,
      PE_OLLAMA_TIMEOUT_MS: process.env.PE_OLLAMA_TIMEOUT_MS || "45000",
      PE_OLLAMA_TEMP: process.env.PE_OLLAMA_TEMP || "0.35",
      PE_TODAY_SEED: "0x4b494b49",
      PE_CLOCK_OVERRIDE_MS: "1700000000000",
      V6_PACKET_TRACE: "1",
      V6_PACKET_TRACE_PATH: tracePath,
    };
    if (mode === "situation") env.V6_PACKET_MODE = "situation";
    else delete env.V6_PACKET_MODE;

    const proc = spawn(HOST, [tmp.cart, "--stdio"], { env });
    let stdout = "", stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", status => {
      if (status !== 0) return reject(new Error(`${mode} host exited ${status}: ${stderr}`));
      const rows = stdout.split("\n").filter(l => l.startsWith("{")).map(l => JSON.parse(l));
      const chats = rows.filter(r => Object.prototype.hasOwnProperty.call(r, "reply"));
      const traceRows = fs.existsSync(tracePath)
        ? fs.readFileSync(tracePath, "utf8").trim().split("\n").filter(Boolean).map(l => JSON.parse(l))
        : [];
      resolve({ mode, chats, traceRows, stderr, tracePath });
    });
    proc.stdin.write(commands);
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 120000).unref();
  });
}

function printMode(result){
  console.log(`\n=== ${result.mode.toUpperCase()} PACKET ===`);
  console.log(`trace: ${path.relative(ROOT, result.tracePath)}`);
  for (let i = 0; i < SCRIPT.length; i++){
    const turn = SCRIPT[i];
    const reply = result.chats[i] ? result.chats[i].reply : "(no reply)";
    const trace = result.traceRows[i] || {};
    console.log(`\n[${i + 1}] ${turn.label}`);
    console.log(`Kiki: ${turn.text}`);
    console.log(`Pretorius: ${reply}`);
    if (trace.detected_user_act || trace.response_move){
      console.log(`trace: act=${trace.detected_user_act || "?"}; pressure=${trace.detected_conversational_pressure || "?"}; move=${trace.response_move || "?"}; audit=${trace.audit_result || "?"}; violation=${trace.audit_violation || "none"}; rewrite=${trace.constrained_rewrite ? "yes" : "no"}`);
    }
  }
  const counts = {};
  const violations = {};
  let rewrites = 0;
  for (const r of result.traceRows){
    const ar = r.audit_result || "unknown";
    counts[ar] = (counts[ar] || 0) + 1;
    const av = r.audit_violation || "none";
    if (av !== "none") violations[av] = (violations[av] || 0) + 1;
    if (r.constrained_rewrite) rewrites++;
  }
  const total = result.traceRows.length || 1;
  console.log(`\nsummary: pass=${counts.pass || 0}, repaired=${counts.repaired || 0}, fallback=${counts.fallback || 0}, rewrite=${rewrites}, fallback_rate=${Math.round(((counts.fallback || 0) / total) * 100)}%`);
  console.log(`violations: ${Object.keys(violations).length ? JSON.stringify(violations) : "none"}`);
}

(async function main(){
  console.log("--- V6 renderer packet experiment A/B ---");
  console.log(`model: ${MODEL}`);
  if (!fs.existsSync(HOST)){
    console.error("persona_host not built");
    process.exit(2);
  }
  if (!fs.existsSync(SRC_CART)){
    console.error("Pretorius cart not found");
    process.exit(2);
  }
  const avail = modelAvailable();
  if (!avail.ok){
    console.log(`SKIP: ${avail.reason}`);
    if (avail.list) console.log(avail.list);
    console.log("Install or pull the model, or set PE_PACKET_MODEL to an available Ollama model.");
    process.exit(0);
  }

  const current = await runMode("current");
  const situation = await runMode("situation");
  printMode(current);
  printMode(situation);

  console.log("\n=== HUMAN REVIEW RUBRIC ===");
  console.log("For each paired turn, ask:");
  console.log("1. Did the response attend to Kiki's actual message?");
  console.log("2. Did it avoid cue-card feeling?");
  console.log("3. Did it avoid over-performing Pretorius voice?");
  console.log("4. Did it answer directly when appropriate?");
  console.log("5. Did it preserve identity and avoid invented facts?");
  console.log("6. Did memory remain uncontaminated? Check layer1_memory_changed in the JSONL trace.");
  console.log("7. Did open loops help rather than hijack the turn?");
})().catch(err => {
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(1);
});
