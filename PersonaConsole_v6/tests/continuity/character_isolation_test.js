#!/usr/bin/env node
/* character_isolation_test.js -- cartridge voice and web session isolation.
 *
 * Verifies two things:
 *  - Kiki and Pretorius can answer the same template-mode prompts without
 *    leaking obvious markers from the other cartridge.
 *  - An already-running HTTP host can be switched from Pretorius to Kiki via
 *    /load and /state before chat is accepted, matching Run_Character.ps1.
 */
"use strict";

const fs = require("fs");
const http = require("http");
const net = require("net");
const os = require("os");
const path = require("path");
const { spawn } = require("child_process");
const { resolveHost } = require("../host_path");

const ROOT = path.join(__dirname, "..", "..");
const HOST = resolveHost(ROOT);
const WEB_ROOT = path.join(ROOT, "bridges", "web");
const PRET_SRC = path.join(ROOT, "profiles", "pretorius", "pretorius.cart");
const KIKI_SRC = path.join(ROOT, "profiles", "kiki", "kiki.cart");
const FRIENDLY_SRC = path.join(ROOT, "profiles", "friendly", "friendly.cart");
const RIVAL_SRC = path.join(ROOT, "profiles", "rival", "rival.cart");
const QUIET_SRC = path.join(ROOT, "profiles", "quiet", "quiet.cart");
const MENTOR_SRC = path.join(ROOT, "profiles", "mentor", "mentor.cart");

const PROMPTS = [
  "Good evening.",
  "Tell me what you have been thinking about.",
  "Do you remember what matters to you?",
  "What should we talk about next?",
  "You feeling down?",
  "Ok you going to the mall later?",
  "You busy?",
  "You are wrong about creation.",
];

const PRETORIUS_MARKERS = [
  "my dear", "my boy", "henry", "septimus", "sacrament",
  "blasphemous", "homunculus", "homunculi", "altar", "gin",
  "ballerina", "my mother's funeral", "lightning, oh the lightning",
  "calling fear morality", "where, exactly, must creation stop",
];

const KIKI_MARKERS = [
  "babe", "obvi", "scully", "punky", "cher horowitz",
  "carl sagan", "cosmic paperwork", "like, the whole", "omg", "rad",
];

const PROFILE_MARKERS = {
  pretorius: PRETORIUS_MARKERS,
  kiki: KIKI_MARKERS,
  friendly: ["dear one", "repair this carefully", "emotionally safe", "warmth can still"],
  rival: ["competitor", "rival", "rematch", "keeping score", "underestimated"],
  quiet: ["trusted one", "silence settle", "small note", "not fill every room"],
  mentor: ["student", "colleague", "checklist", "next useful action", "signal from noise"],
};

const SHARED_VOICE_LEAKS = [
  "i begin to think you sense its weight",
  "always self returns",
  "always the work returns",
  "begin wherever the thought is warmest",
  "i am glad you returned",
  "what shall we examine first",
  "i still owe you the rest",
  "i can answer only the part that is grounded",
  "i have the thread",
  "the short answer is that context matters",
  "ask it more precisely and i will answer more precisely",
  "{topic}",
];

function tempCart(src, slug){
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `persona-isolation-${slug}-`));
  const cart = path.join(dir, `${slug}.cart`);
  fs.copyFileSync(src, cart);
  return cart;
}

function runStdio(cart, lines){
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, ["--stdio", cart], {
      env: { ...process.env, PE_RENDER_BACKEND: "template" },
    });
    let stdout = "";
    let stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", status => resolve({ status, stdout, stderr }));
    proc.stdin.write(lines.map(JSON.stringify).join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 20000).unref();
  });
}

function rows(stdout){
  return stdout.split("\n").filter(l => l.trim().startsWith("{")).map(JSON.parse);
}

function replies(stdout){
  return rows(stdout)
    .filter(r => Object.prototype.hasOwnProperty.call(r, "reply") && r.reply)
    .map(r => String(r.reply));
}

function markerHits(text, markers){
  const low = text.toLowerCase();
  return markers.filter(m => low.includes(m));
}

function freePort(){
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.on("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const port = server.address().port;
      server.close(() => resolve(port));
    });
  });
}

function httpJson(port, method, route, body){
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : "";
    const req = http.request({
      hostname: "127.0.0.1",
      port,
      path: route,
      method,
      headers: payload ? {
        "Content-Type": "application/json",
        "Content-Length": Buffer.byteLength(payload),
      } : {},
      timeout: 4000,
    }, res => {
      let data = "";
      res.setEncoding("utf8");
      res.on("data", d => data += d);
      res.on("end", () => {
        try { resolve(JSON.parse(data)); }
        catch (e) { reject(new Error(`bad JSON from ${route}: ${data}`)); }
      });
    });
    req.on("error", reject);
    req.on("timeout", () => { req.destroy(new Error(`timeout ${route}`)); });
    if (payload) req.write(payload);
    req.end();
  });
}

async function waitState(port){
  const deadline = Date.now() + 5000;
  let last = null;
  while (Date.now() < deadline){
    try { return await httpJson(port, "GET", "/state"); }
    catch (e) { last = e; await new Promise(r => setTimeout(r, 120)); }
  }
  throw last || new Error("host did not answer /state");
}

(async function main(){
  let fail = 0;
  function ok(cond, msg){
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }

  if (!fs.existsSync(HOST)) {
    console.error("persona_host not built");
    process.exit(2);
  }
  if (!fs.existsSync(PRET_SRC) || !fs.existsSync(KIKI_SRC)) {
    console.error("pretorius/kiki carts not built");
    process.exit(2);
  }

  const pretCart = tempCart(PRET_SRC, "pretorius");
  const kikiCart = tempCart(KIKI_SRC, "kiki");
  const mentorCart = fs.existsSync(MENTOR_SRC) ? tempCart(MENTOR_SRC, "mentor") : null;
  const script = [
    { method: "set_user", user_id: "tester" },
    ...PROMPTS.map(text => ({ method: "chat", text })),
    { method: "close" },
  ];

  const kiki = await runStdio(kikiCart, script);
  const pret = await runStdio(pretCart, script);
  ok(kiki.status === 0, "Kiki stdio run exits cleanly");
  ok(pret.status === 0, "Pretorius stdio run exits cleanly");

  const kikiText = replies(kiki.stdout).join("\n");
  const pretText = replies(pret.stdout).join("\n");
  const kikiLeaks = markerHits(kikiText, PRETORIUS_MARKERS);
  const pretLeaks = markerHits(pretText, KIKI_MARKERS);
  const sharedLeaks = markerHits(`${kikiText}\n${pretText}`, SHARED_VOICE_LEAKS);
  ok(kikiLeaks.length === 0, `Kiki emits no Pretorius markers (${kikiLeaks.join(", ") || "none"})`);
  ok(pretLeaks.length === 0, `Pretorius emits no Kiki markers (${pretLeaks.join(", ") || "none"})`);
  ok(sharedLeaks.length === 0, `shared callback phrases are neutralized (${sharedLeaks.join(", ") || "none"})`);

  const allProfiles = [
    ["pretorius", PRET_SRC],
    ["kiki", KIKI_SRC],
    ["friendly", FRIENDLY_SRC],
    ["rival", RIVAL_SRC],
    ["quiet", QUIET_SRC],
    ["mentor", MENTOR_SRC],
  ];
  for (const [slug, src] of allProfiles){
    if (!fs.existsSync(src)) continue;
    const cart = tempCart(src, `all-${slug}`);
    const run = await runStdio(cart, script);
    ok(run.status === 0, `${slug} stdio purity run exits cleanly`);
    const text = replies(run.stdout).join("\n");
    const own = PROFILE_MARKERS[slug] || [];
    const otherMarkers = Object.entries(PROFILE_MARKERS)
      .filter(([other]) => other !== slug)
      .flatMap(([, markers]) => markers);
    const cross = markerHits(text, otherMarkers);
    const shared = markerHits(text, SHARED_VOICE_LEAKS);
    ok(cross.length === 0, `${slug} emits no other-cartridge markers (${cross.join(", ") || "none"})`);
    ok(shared.length === 0, `${slug} emits no generic engine leak phrases (${shared.join(", ") || "none"})`);
    if (own.length) {
      console.log(`sample:${slug}: ${replies(run.stdout).slice(0, 2).map(r => r.replace(/\s+/g, " ")).join(" | ")}`);
    }
  }

  const port = await freePort();
  const proc = spawn(HOST, ["--port", String(port), "--web-root", WEB_ROOT, pretCart], {
    env: { ...process.env, PE_RENDER_BACKEND: "template" },
  });
  let stderr = "";
  proc.stderr.on("data", d => stderr += d.toString("utf8"));
  try {
    const first = await waitState(port);
    ok(/pretorius/i.test(first.name || ""), "HTTP host starts as Pretorius");
    const load = await httpJson(port, "POST", "/load", { path: kikiCart });
    ok(load && load.ok === true, "HTTP /load accepts Kiki cart");
    const webUser = await httpJson(port, "POST", "/set_user", { user_id: "You" });
    ok(webUser && webUser.ok === true, "HTTP web path establishes local actor identity");
    const after = await waitState(port);
    ok(/^kiki$/i.test(after.name || "") || /^kiki$/i.test(after.profile_slug || ""),
       `HTTP /state reports Kiki after load (name=${after.name}, slug=${after.profile_slug})`);
    ok(after.user_id === "You", `HTTP /state reports web actor identity (${after.user_id})`);
    const chat = await httpJson(port, "POST", "/chat", { text: "Good evening." });
    const reply = String(chat.reply || "");
    const leaks = markerHits(reply, PRETORIUS_MARKERS);
    ok(leaks.length === 0, `Kiki HTTP reply after /load has no Pretorius markers (${leaks.join(", ") || "none"})`);
    ok(!/^Someone[,.]/.test(reply), `Kiki HTTP reply does not address anonymous Someone (${reply})`);
    const reset = await httpJson(port, "POST", "/reset_runtime", {});
    ok(reset && reset.ok === true, "HTTP /reset_runtime clears current cartridge runtime state");
    const afterReset = await waitState(port);
    ok(/^kiki$/i.test(afterReset.name || "") || /^kiki$/i.test(afterReset.profile_slug || ""),
       `HTTP /reset_runtime keeps the active cartridge loaded as Kiki (name=${afterReset.name}, slug=${afterReset.profile_slug})`);
    ok(Number(afterReset.turn_count) === 0,
       `HTTP /reset_runtime returns Kiki to a fresh turn_count (${afterReset.turn_count})`);
    ok(afterReset.user_id === "You", `HTTP /reset_runtime preserves web actor identity (${afterReset.user_id})`);
    if (mentorCart){
      const loadMentor = await httpJson(port, "POST", "/load", { path: mentorCart });
      ok(loadMentor && loadMentor.ok === true, "HTTP /load accepts Mentor cart after Kiki");
      const mentorState = await waitState(port);
      ok(/marin|mentor/i.test(mentorState.name || "") || /mentor/i.test(mentorState.profile_slug || ""),
         `HTTP /state reports Mentor after second load (name=${mentorState.name}, slug=${mentorState.profile_slug})`);
      const mentorChat = await httpJson(port, "POST", "/chat", { text: "Good evening." });
      const mentorLeaks = markerHits(String(mentorChat.reply || ""), SHARED_VOICE_LEAKS);
      ok(mentorLeaks.length === 0, `Mentor HTTP reply has no shared reflection leak (${mentorLeaks.join(", ") || "none"})`);
    }
  } finally {
    proc.kill("SIGTERM");
  }

  if (fail) {
    console.error(stderr);
    process.exit(1);
  }
  console.log("PASSED -- character isolation holds for template and web load paths");
})().catch(e => {
  console.error(e && e.stack || e);
  process.exit(2);
});
