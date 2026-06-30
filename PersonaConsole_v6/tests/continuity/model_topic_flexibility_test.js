#!/usr/bin/env node
/* model_topic_flexibility_test.js -- model mode can learn a new user detail
 * and answer general-knowledge questions without confusing either case for a
 * personal-memory probe. Runs across divergent cartridges to guard against
 * character-specific fixes.
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

const CASES = [
  { slug: "r0r1", cart: path.join(ROOT, "profiles", "r0r1", "r0r1.cart"), user: "Rory" },
  { slug: "kiki", cart: path.join(ROOT, "profiles", "kiki", "kiki.cart"), user: "Tester" },
  { slug: "pretorius", cart: path.join(ROOT, "profiles", "pretorius", "pretorius.cart"), user: "Tester" },
];

function makeTempCart(src, slug){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `persona-topic-flex-${slug}-`));
  const cart = path.join(dir, `${slug}.cart`);
  fs.copyFileSync(src, cart);
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
      let response = "I am listening. What should we keep in view?";
      if (prompt.includes("[MEMORY_COMMIT_OVERLAY]")){
        response = "I will keep D&D and Dungeon Master learning in view. Which part should we explore first?";
      } else if (/Can you teach me about D&D/i.test(prompt)){
        response = "D&D is a pretend-adventure game. A Dungeon Master describes the world, players choose actions, and dice decide risky moments. Want a tiny starter quest?";
      } else if (/Can you explain the Solar System/i.test(prompt)){
        response = "The Solar System is our neighborhood of the Sun, planets, moons, asteroids, and comets. Jupiter is huge, Mars is rocky, and Earth is our cozy home base. Want the tour?";
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

function runHost(port, cart, user){
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
        PE_TODAY_SEED: "7272",
        V6_PACKET_MODE: "situation",
      },
    });
    let stdout = "", stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", status => resolve({ status, stdout, stderr }));
    proc.stdin.write([
      JSON.stringify({ method: "set_user", user_id: user }),
      JSON.stringify({ method: "chat", text: "Can you explain the Solar System?" }),
      JSON.stringify({ method: "chat", text: "Please remember that I want to learn D&D and become a Dungeon Master." }),
      JSON.stringify({ method: "chat", text: "Can you teach me about D&D?" }),
      JSON.stringify({ method: "close" }),
    ].join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 15000).unref();
  });
}

function parseRows(stdout){
  return stdout.split("\n")
    .filter(l => l.trim().startsWith("{"))
    .map(l => JSON.parse(l));
}

(async function main(){
  let fail = 0;
  function ok(cond, msg){
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }

  for (const c of CASES){
    if (!fs.existsSync(c.cart)){
      ok(false, `${c.slug}: missing cartridge ${c.cart}`);
      continue;
    }
    const cart = makeTempCart(c.cart, c.slug);
    const mock = makeMock();
    await new Promise(r => mock.server.listen(0, "127.0.0.1", r));
    const port = mock.server.address().port;
    const res = await runHost(port, cart, c.user);
    mock.server.close();
    ok(res.status === 0, `${c.slug}: host exits cleanly`);
    if (res.status !== 0){
      console.error(res.stderr);
      continue;
    }

    const rows = parseRows(res.stdout);
    const chats = rows.filter(r => Object.prototype.hasOwnProperty.call(r, "reply"));
    const firstKnowledgeReply = (chats[0] && chats[0].reply) || "";
    const commitReply = (chats[1] && chats[1].reply) || "";
    const knowledgeReply = (chats[2] && chats[2].reply) || "";
    const commitPrompt = mock.prompts.find(p => p.includes("[MEMORY_COMMIT_OVERLAY]")) || "";
    const knowledgePrompt = mock.prompts.find(p => /Can you teach me about D&D/i.test(p)) || "";
    const firstKnowledgePrompt = mock.prompts.find(p => /Can you explain the Solar System/i.test(p)) || "";
    const commitState = (chats[1] && chats[1].state) || {};

    ok(firstKnowledgePrompt.length > 0, `${c.slug}: first outside-topic prompt reached model`);
    ok(/Solar System|Jupiter|Mars|Earth/i.test(firstKnowledgeReply),
       `${c.slug}: model can answer an unrelated outside topic`);
    ok(!/not sure enough|not certain|say it another way/i.test(firstKnowledgeReply),
       `${c.slug}: first outside-topic answer avoids lore fallback`);
    ok(commitPrompt.includes("act=memory_commit"), `${c.slug}: commit prompt classified as memory_commit`);
    ok(commitPrompt.includes("[MEMORY_COMMIT_OVERLAY]"), `${c.slug}: commit prompt includes memory commit overlay`);
    ok(!commitPrompt.includes("[MEMORY_PROBE_OVERLAY]"), `${c.slug}: commit prompt is not a memory probe`);
    ok(!/not certain about that memory|not sure enough|not sure yet|say it another way/i.test(commitReply),
       `${c.slug}: commit reply avoids memory-probe fallback: ${commitReply}`);
    ok((commitState.actor_tagged_memories || 0) > 0,
       `${c.slug}: explicit remember request creates actor-tagged memory state`);

    ok(knowledgePrompt.length > 0, `${c.slug}: direct D&D teaching prompt reached model`);
    ok(/Dungeon Master|players|dice/i.test(knowledgeReply), `${c.slug}: model can answer outside cartridge topic`);
    ok(!/not sure enough|not certain|say it another way/i.test(knowledgeReply),
       `${c.slug}: general knowledge answer avoids lore fallback`);
  }

  if (fail) process.exit(1);
  console.log("PASSED -- model topic flexibility and memory commit are generic across cartridges");
})().catch(e => { console.error(e && e.stack ? e.stack : String(e)); process.exit(2); });
