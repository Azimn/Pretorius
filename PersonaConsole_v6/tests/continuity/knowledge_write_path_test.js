#!/usr/bin/env node
/* knowledge_write_path_test.js -- V6 SLM-to-candidate learned write path.
 *
 * Proves:
 *   1. A model explanation becomes a low-authority candidate, not confirmed
 *      knowledge.
 *   2. A user correction confirms knowledge through Layer 1.
 *   3. A later conflicting SLM claim cannot silently overwrite the confirmed
 *      entry, and the prompt packet surfaces the existing evidence graph.
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

function makeTempCart(){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "persona-knowledge-write-"));
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
      const response = "electricity is the flow of positive charge through a wire. voltage pushes it and resistance slows it.";
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

function firstReply(run){
  const row = parseRows(run.stdout).find(r => Object.prototype.hasOwnProperty.call(r, "reply"));
  return row && row.reply || "";
}

(async function main(){
  let fail = 0;
  function ok(cond, msg){
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }

  if (!fs.existsSync(HOST)) { console.error("persona_host not built"); process.exit(2); }
  const cart = makeTempCart();
  const profileDir = path.dirname(cart);

  const mock = makeMock();
  await new Promise(r => mock.server.listen(0, "127.0.0.1", r));
  const port = mock.server.address().port;

  const slm1 = await runHost(cart, {
    PE_RENDER_BACKEND: "slm",
    PE_SLM_PROVIDER: "ollama",
    PE_SLM_MODEL: "qwen3:8b",
    PE_OLLAMA_HOST: "127.0.0.1",
    PE_OLLAMA_PORT: String(port),
    PE_OLLAMA_TIMEOUT_MS: "3000",
    PE_TODAY_SEED: "921",
    V6_PACKET_MODE: "situation",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Can you explain how electricity works in a wire, technically?" },
    { method: "close" },
  ]);
  ok(slm1.status === 0, "candidate SLM phase exits cleanly");
  ok(/positive charge/i.test(firstReply(slm1)),
     `candidate model answer accepted as speech: ${firstReply(slm1)}`);

  const offlineCandidate = await runHost(cart, {
    PE_RENDER_BACKEND: "template",
    PE_TODAY_SEED: "921",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Can you tell me how electricity works in a wire again?" },
    { method: "close" },
  ]);
  const candidateReply = firstReply(offlineCandidate);
  ok(/provisional/i.test(candidateReply),
     `model-only learned claim remains candidate/provisional offline: ${candidateReply}`);
  ok(!/better account|kiki corrected/i.test(candidateReply),
     "candidate is not treated as confirmed user-taught knowledge");

  const corrected = await runHost(cart, {
    PE_RENDER_BACKEND: "template",
    PE_TODAY_SEED: "921",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Actually, in metal wires the moving charges are electrons drifting through the conductor. Voltage is electric potential difference, not a fluid pressure." },
    { method: "close" },
  ]);
  ok(corrected.status === 0, "user correction phase exits cleanly");

  const beforePromptCount = mock.prompts.length;
  const slmConflict = await runHost(cart, {
    PE_RENDER_BACKEND: "slm",
    PE_SLM_PROVIDER: "ollama",
    PE_SLM_MODEL: "qwen3:8b",
    PE_OLLAMA_HOST: "127.0.0.1",
    PE_OLLAMA_PORT: String(port),
    PE_OLLAMA_TIMEOUT_MS: "3000",
    PE_TODAY_SEED: "922",
    V6_PACKET_MODE: "situation",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Explain electricity in a wire one more time." },
    { method: "close" },
  ]);
  mock.server.close();
  ok(slmConflict.status === 0, "conflicting SLM phase exits cleanly");

  const conflictPrompts = mock.prompts.slice(beforePromptCount).join("\n");
  ok(/\[LEARNED_KNOWLEDGE\]/.test(conflictPrompts) &&
     /status=confirmed/.test(conflictPrompts) &&
     /edge_\d+/.test(conflictPrompts),
     "conflict prompt surfaces confirmed learned knowledge and evidence graph before audit");

  const offlineFinal = await runHost(cart, {
    PE_RENDER_BACKEND: "template",
    PE_TODAY_SEED: "923",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Can you tell me how electricity works in a wire again?" },
    { method: "close" },
  ]);
  const finalReply = firstReply(offlineFinal);
  ok(/electrons/i.test(finalReply) && /potential difference/i.test(finalReply),
     `confirmed correction remains winner after conflicting SLM answer: ${finalReply}`);
  ok(!/positive charge flow/i.test(finalReply),
     "conflicting SLM claim did not overwrite confirmed learned knowledge");
  ok(fs.existsSync(path.join(profileDir, "learned_knowledge.bin")),
     "learned sidecar exists after write-path test");

  if (fail) process.exit(1);
  console.log("PASSED -- SLM candidate write path requires confirmation and preserves authority");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
