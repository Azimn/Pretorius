#!/usr/bin/env node
/* knowledge_bridge_test.js -- LLM-assisted knowledge can become portable
 * offline knowledge only through a compact Layer 1 learned card.
 *
 * Scenario:
 *   1. Kiki asks Pretorius a technical electricity question in SLM mode.
 *   2. Mock Ollama answers with an intentionally weak/wrong simplification.
 *   3. Kiki corrects him with better domain knowledge.
 *   4. Close and reopen the same profile in template-only mode.
 *   5. Kiki asks again. Offline Pretorius must use Kiki's correction, not
 *      the model's earlier wrong claim.
 */
"use strict";

const net = require("net");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawn } = require("child_process");
const { resolveHost } = require("../host_path");

const ROOT = path.join(__dirname, "..", "..");
const HOST = resolveHost(ROOT);
const SRC_CART = path.join(ROOT, "profiles", "pretorius", "pretorius.cart");

function copyDir(src, dst){
  fs.mkdirSync(dst, { recursive: true });
  for (const ent of fs.readdirSync(src, { withFileTypes: true })){
    const s = path.join(src, ent.name);
    const d = path.join(dst, ent.name);
    if (ent.isDirectory()) copyDir(s, d);
    else if (ent.name.endsWith(".cart") || ent.name === "manifest.json")
      fs.copyFileSync(s, d);
  }
}

function makeTempProfile(){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "persona-knowledge-bridge-"));
  const profile = path.join(dir, "pretorius");
  copyDir(path.dirname(SRC_CART), profile);
  return path.join(profile, "pretorius.cart");
}

function makeMock(){
  let count = 0;
  const prompts = [];
  const server = net.createServer(sock => {
    let chunks = [], total = 0;
    sock.on("data", d => {
      chunks.push(d); total += d.length;
      const buf = Buffer.concat(chunks, total);
      const split = buf.indexOf("\r\n\r\n");
      if (split < 0) return;
      const head = buf.slice(0, split).toString("utf8");
      const body = buf.slice(split + 4);
      const m = head.match(/Content-Length:\s*(\d+)/i);
      if (!m) return;
      const len = parseInt(m[1], 10);
      if (body.length < len) return;
      const req = JSON.parse(body.slice(0, len).toString("utf8"));
      prompts.push(req.prompt || "");
      count++;
      const response = count === 1
        ? "electricity is the flow of positive charge through a wire. voltage pushes it and resistance slows it."
        : "correction accepted. in metal conductors the moving charges are electrons, and voltage is electric potential difference.";
      const respBody = JSON.stringify({ model: req.model, response, done: true });
      sock.write(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n" +
        `Content-Length: ${Buffer.byteLength(respBody)}\r\nConnection: close\r\n\r\n` +
        respBody
      );
      sock.end();
    });
  });
  return { server, prompts, count: () => count };
}

function runHost(cart, env, lines){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [cart, "--stdio"], { env: { ...process.env, ...env } });
    let stdout = "", stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", status => resolve({ status, stdout, stderr }));
    proc.stdin.write(lines.map(JSON.stringify).join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 20000);
  });
}

function parseRows(stdout){
  return stdout.split("\n").filter(l => l.startsWith("{")).map(JSON.parse);
}

(async function main(){
  let fail = 0;
  if (!fs.existsSync(HOST)) { console.error("persona_host not built"); process.exit(2); }
  const cart = makeTempProfile();
  const mock = makeMock();
  await new Promise(r => mock.server.listen(0, "127.0.0.1", r));
  const port = mock.server.address().port;

  const slm = await runHost(cart, {
    PE_RENDER_BACKEND: "slm",
    PE_SLM_PROVIDER: "ollama",
    PE_SLM_MODEL: "qwen3:8b",
    PE_OLLAMA_HOST: "127.0.0.1",
    PE_OLLAMA_PORT: String(port),
    PE_OLLAMA_TIMEOUT_MS: "3000",
    PE_TODAY_SEED: "713",
    V6_PACKET_MODE: "situation",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Can you explain how electricity works in a wire, technically?" },
    { method: "chat", text: "Actually, in metal wires the moving charges are electrons drifting through the conductor. Voltage is electric potential difference, not a fluid pressure." },
    { method: "close" },
  ]);
  mock.server.close();

  if (slm.status !== 0){
    console.error(slm.stderr);
    process.exit(1);
  }

  const offline = await runHost(cart, {
    PE_RENDER_BACKEND: "template",
    PE_TODAY_SEED: "713",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Can you tell me how electricity works in a wire again?" },
    { method: "close" },
  ]);
  if (offline.status !== 0){
    console.error(offline.stderr);
    process.exit(1);
  }

  const slmRows = parseRows(slm.stdout);
  const offRows = parseRows(offline.stdout);
  const slmChats = slmRows.filter(r => Object.prototype.hasOwnProperty.call(r, "reply"));
  const offChat = offRows.find(r => Object.prototype.hasOwnProperty.call(r, "reply")) || {};
  const firstReply = slmChats[0] && slmChats[0].reply || "";
  const offlineReply = offChat.reply || "";
  const profileDir = path.dirname(cart);
  const learnedPath = path.join(profileDir, "learned_knowledge.bin");
  const memoryPath = path.join(profileDir, "memory.bin");
  const memoryBytes = fs.existsSync(memoryPath) ? fs.readFileSync(memoryPath) : Buffer.alloc(0);

  function ok(cond, msg){
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }

  ok(mock.count() === 2, `SLM phase used mock model for two accepted turns (${mock.count()})`);
  ok(/positive charge/i.test(firstReply),
     `first SLM answer contained the wrong/provisional claim: ${firstReply}`);
  ok(fs.existsSync(learnedPath) && fs.statSync(learnedPath).size > 1024,
     "learned_knowledge.bin sidecar was written");
  ok(!memoryBytes.includes(Buffer.from("[learned:model]")) &&
     !memoryBytes.includes(Buffer.from("[learned:user_confirmed]")),
     "legacy prefix-parsed learned cards are not stored in episodic memory");
  ok(/electrons/i.test(offlineReply),
     `offline reply uses Kiki's taught correction: ${offlineReply}`);
  ok(/potential difference|resistance|impedes/i.test(offlineReply),
     `offline reply carries compact technical knowledge: ${offlineReply}`);
  ok(!/positive charge flow/i.test(offlineReply),
     `offline reply does not repeat provisional wrong model claim: ${offlineReply}`);
  ok(!/as an ai|language model|database|according to my records/i.test(offlineReply),
     `offline reply is not system/meta phrasing: ${offlineReply}`);

  if (fail) process.exit(1);
  console.log("PASSED -- LLM-taught knowledge bridges into offline corrected recall");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
