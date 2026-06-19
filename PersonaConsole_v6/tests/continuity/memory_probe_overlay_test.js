#!/usr/bin/env node
/* memory_probe_overlay_test.js -- memory probes get grounded continuity.
 *
 * Plants a user memory, then asks whether Pretorius remembers it. The mock
 * model only produces a grounded answer when the situation packet contains
 * [MEMORY_PROBE_OVERLAY] and the planted memory text. The final reply must
 * cite canonical memory or grounded uncertainty, never generic deflection.
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
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "persona-memory-probe-"));
  const cart = path.join(dir, "pretorius.cart");
  fs.copyFileSync(SRC_CART, cart);
  return cart;
}

function makeMock(){
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
      const prompt = req.prompt || "";
      prompts.push(prompt);
      let response = "Yes, yes. And then?";
      if (prompt.includes("[MEMORY_PROBE_OVERLAY]") &&
          /spellwork/i.test(prompt)){
        response = "You compared modern code to spellwork with better shoes, Kiki. The symbols obey rules, but they still want a little lightning, don't they?";
      }
      const respBody = JSON.stringify({ model: req.model, response, done: true });
      sock.write(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n" +
        `Content-Length: ${Buffer.byteLength(respBody)}\r\nConnection: close\r\n\r\n` +
        respBody
      );
      sock.end();
    });
  });
  return { server, prompts };
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
        PE_TODAY_SEED: "0x4b494b49",
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
      JSON.stringify({ method: "chat", text: "Modern code feels like spellwork with better shoes, but it still obeys symbols and consequences." }),
      JSON.stringify({ method: "chat", text: "Do you remember what I said about code feeling like spellwork?" }),
      JSON.stringify({ method: "close" }),
    ].join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 15000);
  });
}

(async function main(){
  let fail = 0;
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
  const chats = rows.filter(r => Object.prototype.hasOwnProperty.call(r, "reply"));
  const probe = chats[1] || {};
  const reply = probe.reply || "";
  const state = probe.state || {};
  const probePrompt = mock.prompts[1] || "";

  function ok(cond, msg){
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }

  ok(probePrompt.includes("[MEMORY_PROBE_OVERLAY]"), "memory probe prompt includes overlay");
  ok(probePrompt.includes("do not substitute an unrelated memory"),
     "memory probe overlay forbids unrelated memory substitution");
  ok(probePrompt.includes("Preserve actor attribution"),
     "memory probe overlay preserves actor attribution");
  ok(/spellwork/i.test(probePrompt), "memory probe prompt carries canonical planted memory");
  ok(/spellwork|symbols|consequences|better shoes/i.test(reply),
     `reply cites canonical memory content: ${reply}`);
  ok(!/say it another way|ask differently|i do not know what you mean/i.test(reply),
     `reply avoids generic deflection: ${reply}`);
  ok(state.last_audit_result !== 2,
     `memory probe did not fall through to template fallback (${state.last_audit_result})`);

  if (fail) process.exit(1);
  console.log("PASSED -- memory probe overlay grounds continuity response");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
