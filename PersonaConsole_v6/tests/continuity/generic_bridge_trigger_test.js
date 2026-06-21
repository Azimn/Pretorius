#!/usr/bin/env node
/* generic_bridge_trigger_test.js -- learned bridge is topic-data driven.
 *
 * Uses the cartridge-authored Pretorius "homunculi" topic to prove the
 * recognize -> candidate -> correction -> offline surface path is not tied to
 * the old electricity probe.
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
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "persona-generic-bridge-"));
  const profile = path.join(dir, "pretorius");
  copyDir(path.dirname(SRC_CART), profile);
  return path.join(profile, "pretorius.cart");
}

function makeMock(){
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
      count++;
      const response = count === 1
        ? "homunculi are clockwork dolls in little jars, wound by hidden springs."
        : "correction accepted. homunculi are tiny living people in glass jars, not clockwork dolls.";
      const respBody = JSON.stringify({ model: req.model, response, done: true });
      sock.write(
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n" +
        `Content-Length: ${Buffer.byteLength(respBody)}\r\nConnection: close\r\n\r\n` +
        respBody
      );
      sock.end();
    });
  });
  return { server, count: () => count };
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
    setTimeout(() => proc.kill("SIGKILL"), 20000).unref();
  });
}

function parseRows(stdout){
  return stdout.split("\n").filter(l => l.startsWith("{")).map(JSON.parse);
}

function firstReply(run){
  const row = parseRows(run.stdout).find(r => Object.prototype.hasOwnProperty.call(r, "reply"));
  return row && row.reply || "";
}

function lastReply(run){
  const rows = parseRows(run.stdout).filter(r => Object.prototype.hasOwnProperty.call(r, "reply"));
  const row = rows[rows.length - 1];
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

  const slm = await runHost(cart, {
    PE_RENDER_BACKEND: "slm",
    PE_SLM_PROVIDER: "ollama",
    PE_SLM_MODEL: "qwen3:8b",
    PE_OLLAMA_HOST: "127.0.0.1",
    PE_OLLAMA_PORT: String(port),
    PE_OLLAMA_TIMEOUT_MS: "3000",
    PE_TODAY_SEED: "1441",
    V6_PACKET_MODE: "situation",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Can you explain the homunculi in the jars, technically?" },
    { method: "chat", text: "Actually, homunculi are tiny living people in glass jars, not clockwork dolls." },
    { method: "close" },
  ]);
  mock.server.close();
  ok(slm.status === 0, "SLM/correction phase exits cleanly");
  ok(mock.count() >= 1, `mock model was used for the first topic answer (${mock.count()})`);
  ok(/clockwork dolls/i.test(firstReply(slm)),
     `first SLM answer carried the provisional wrong claim: ${firstReply(slm)}`);

  const offline = await runHost(cart, {
    PE_RENDER_BACKEND: "template",
    PE_TODAY_SEED: "1441",
  }, [
    { method: "set_user", user_id: "Kiki" },
    { method: "chat", text: "Good evening." },
    { method: "chat", text: "Can you tell me about the homunculi again?" },
    { method: "close" },
  ]);
  ok(offline.status === 0, "offline phase exits cleanly");
  const reply = lastReply(offline);
  ok(/tiny living people/i.test(reply) && /glass jars/i.test(reply),
     `offline reply surfaces corrected non-electricity knowledge: ${reply}`);
  ok(!/are clockwork dolls|clockwork dolls in little jars|wound by hidden springs/i.test(reply),
     `offline reply does not repeat provisional wrong claim: ${reply}`);
  ok(fs.existsSync(path.join(profileDir, "learned_knowledge.bin")),
     "learned_knowledge.bin sidecar was written");

  if (fail) process.exit(1);
  console.log("PASSED -- generic learned bridge trigger uses cartridge topic data");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
