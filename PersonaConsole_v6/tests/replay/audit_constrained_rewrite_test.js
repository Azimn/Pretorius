#!/usr/bin/env node
/* audit_constrained_rewrite_test.js -- soft audit repair keeps the move.
 *
 * Mock turn 1 returns a direct answer with a repeated trigram. That is a soft
 * self-repeat violation. The runtime should retry the renderer with a
 * constrained repair prompt, preserve the answer's conversational move, and
 * avoid template fallback.
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

function makeTempCart(){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "persona-audit-rewrite-"));
  const cart = path.join(dir, "pretorius.cart");
  fs.copyFileSync(SRC_CART, cart);
  return cart;
}

function makeMock(){
  const prompts = [];
  let count = 0;
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
      const first = "Henry hesitated because Henry hesitated because Henry hesitated because the work outgrew his courage.";
      const repaired = "Henry hesitated because the work outgrew his courage. What would you have done at the table?";
      const response = count === 1 ? first : repaired;
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

function runHost(port, cart){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, [cart, "--stdio"], {
      env: {
        ...process.env,
        PE_RENDER_BACKEND: "slm",
        PE_SLM_PROVIDER: "ollama",
        PE_SLM_MODEL: "qwen3:8b",
        PE_OLLAMA_HOST: "127.0.0.1",
        PE_OLLAMA_PORT: String(port),
        PE_OLLAMA_TIMEOUT_MS: "3000",
        PE_TODAY_SEED: "42",
        V6_PACKET_MODE: "situation",
      },
    });
    let stdout = "", stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", status => resolve({ status, stdout, stderr }));
    proc.stdin.write([
      JSON.stringify({ method: "set_user", user_id: "Kiki" }),
      JSON.stringify({ method: "chat", text: "What would you ask me if you were not waiting for permission?" }),
      JSON.stringify({ method: "close" }),
    ].join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 15000).unref();
  });
}

(async function main(){
  let fail = 0;
  if (!fs.existsSync(HOST)) { console.error("persona_host not built"); process.exit(2); }
  const cart = makeTempCart();
  const mock = makeMock();
  await new Promise(r => mock.server.listen(0, "127.0.0.1", r));
  const port = mock.server.address().port;
  const res = await runHost(port, cart);
  mock.server.close();
  if (res.status !== 0){
    console.error(res.stderr);
    process.exit(1);
  }
  const rows = res.stdout.split("\n").filter(l => l.startsWith("{")).map(JSON.parse);
  const chat = rows.find(r => Object.prototype.hasOwnProperty.call(r, "reply"));
  const reply = chat && chat.reply || "";
  const state = chat && chat.state || {};

  function ok(cond, msg){
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }

  ok(mock.count() === 2, `soft violation triggered one constrained retry (${mock.count()} requests)`);
  ok(mock.prompts[1] && mock.prompts[1].includes("[REPAIR]"), "repair prompt includes [REPAIR] block");
  ok(state.last_audit_result === 1, `audit result is repaired (${state.last_audit_result})`);
  ok(state.last_audit_rewrite === 1, "state records constrained rewrite");
  ok(state.last_audit_violation === 3, `violation is self-repeat (${state.last_audit_violation})`);
  ok(reply.includes("Henry hesitated") && reply.includes("courage") && reply.includes("?"),
     `repaired reply preserves answer move and removes repetition: ${reply}`);
  ok(!/good morning|say it another way|give me a moment/i.test(reply),
     `reply did not fall back to generic template: ${reply}`);

  if (fail) process.exit(1);
  console.log("PASSED -- constrained rewrite preserves conversational move");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
