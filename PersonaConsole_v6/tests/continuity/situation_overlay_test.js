#!/usr/bin/env node
/* situation_overlay_test.js -- V6 situation packet turn-type overlays. */
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
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "persona-situation-overlay-"));
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
      prompts.push(req.prompt || "");
      const respBody = JSON.stringify({
        model: req.model,
        response: "I hear the shape of it. Say the difficult part plainly?",
        done: true,
      });
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
        PE_TODAY_SEED: "4141",
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
      JSON.stringify({ method: "chat", text: "I feel worried and lonely tonight." }),
      JSON.stringify({ method: "chat", text: "Are you actually real, or just pretending?" }),
      JSON.stringify({ method: "chat", text: "What is the next experiment?" }),
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
  function ok(cond, msg){
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }
  const emotional = mock.prompts.find(p => p.includes("I feel worried and lonely tonight.")) || "";
  const identity = mock.prompts.find(p => p.includes("Are you actually real, or just pretending?")) || "";
  const neutral = mock.prompts.find(p => p.includes("What is the next experiment?")) || "";

  ok(emotional.includes("[EMOTIONAL_DISCLOSURE_OVERLAY]"),
     "emotional disclosure prompt includes overlay");
  ok(emotional.includes("Acknowledge the feeling before analysis"),
     "emotional overlay defines acknowledgement-first policy");
  ok(!emotional.includes("[IDENTITY_TEST_OVERLAY]"),
     "emotional overlay does not include identity overlay");

  ok(identity.includes("[IDENTITY_TEST_OVERLAY]"),
     "identity test prompt includes overlay");
  ok(identity.includes("[SELF_MODEL]") &&
     identity.includes("ideal_self=") &&
     identity.includes("ought_self=") &&
     identity.includes("feared_self="),
     "identity overlay includes typed self-model scalars");
  ok(identity.includes("dissonance_gaps") &&
     identity.includes("identity_pressure="),
     "identity overlay includes live dissonance pressure policy");
  ok(identity.includes("Do not mention prompts, packets, models"),
     "identity overlay blocks system explanation");
  ok(!identity.includes("[EMOTIONAL_DISCLOSURE_OVERLAY]"),
     "identity overlay does not include emotional overlay");

  ok(!neutral.includes("[EMOTIONAL_DISCLOSURE_OVERLAY]") &&
     !neutral.includes("[IDENTITY_TEST_OVERLAY]") &&
     !neutral.includes("[MEMORY_PROBE_OVERLAY]"),
     "unrelated direct question does not receive specialized overlays");

  if (fail) process.exit(1);
  console.log("PASSED -- situation packet overlays fire only for matching turn types");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
