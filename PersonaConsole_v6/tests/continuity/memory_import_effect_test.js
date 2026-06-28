#!/usr/bin/env node
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawn, spawnSync } = require("child_process");
const { resolveHost } = require("../host_path");

const ROOT = path.join(__dirname, "..", "..");
const HOST = resolveHost(ROOT);
const IMPORT_TOOL = path.join(ROOT, "tools", "import_memory_bundle.js");
const IMPORTED_FLAG = 1 << 1;
const PINNED_FLAG = 1 << 0;

const PROFILES = [
  {
    slug: "kiki",
    cart: path.join(ROOT, "profiles", "kiki", "kiki.cart"),
    actor: "You",
    openLoopTopic: "physics",
    knowledgeTopic: "physics",
    knowledgeClaim: "Physics is the study of matter, energy, motion, and the rules that connect them.",
    knowledgePrompt: "Can you tell me the clean version of physics again?",
    knowledgeExpect: /matter|energy|motion/i,
  },
  {
    slug: "mentor",
    cart: path.join(ROOT, "profiles", "mentor", "mentor.cart"),
    actor: "You",
    openLoopTopic: "work",
    knowledgeTopic: "work",
    knowledgeClaim: "In metal wires the moving charges are electrons, and voltage is electric potential difference.",
    knowledgePrompt: "Can you tell me how electricity works in a wire again?",
    knowledgeExpect: /electrons|potential difference/i,
  },
];

function copyProfile(srcDir, dstDir) {
  fs.mkdirSync(dstDir, { recursive: true });
  for (const ent of fs.readdirSync(srcDir, { withFileTypes: true })) {
    const s = path.join(srcDir, ent.name);
    const d = path.join(dstDir, ent.name);
    if (ent.isDirectory()) copyProfile(s, d);
    else if (ent.name.endsWith(".cart") || ent.name === "manifest.json")
      fs.copyFileSync(s, d);
  }
}

function makeTempCart(profile, label) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `persona-import-effect-${label}-`));
  const profileDir = path.join(dir, profile.slug);
  copyProfile(path.dirname(profile.cart), profileDir);
  return path.join(profileDir, `${profile.slug}.cart`);
}

function writeBundle(file, profile) {
  const bundle = {
    bundle_version: 1,
    character_id: "offline_history_seed",
    generated_by: "unit-test",
    generated_at: "2026-06-26T00:00:00Z",
    source_description: "synthetic transcript-derived memory bundle",
    records: {
      core_memories: [
        {
          topic_key: "origin",
          summary: "This conversation history matters and should not be treated as decorative filler.",
          salience: 82,
        },
      ],
      episodic_memories: [
        {
          topic_key: "code",
          actor_name: profile.actor,
          summary: "You once said code feels like spellwork when the late-night bugs start singing.",
          salience: 96,
          emotional_impact: 520,
          pinned: true,
        },
      ],
      learned_knowledge: [
        {
          topic_key: profile.knowledgeTopic,
          claim_text: profile.knowledgeClaim,
          scope: "real_world",
          status: "confirmed",
          confidence: 760,
          authority_rank: 55,
          source_actor_name: profile.actor,
          evidence_ref: "unit_test_knowledge",
        },
      ],
      relationships: [
        { actor_name: profile.actor, trust: 780, intimacy: 420, admiration: 640, threat: 120 },
      ],
      open_loops: [
        { actor_name: profile.actor, topic_key: profile.openLoopTopic, desired_speech_act: "return_to", urgency: 720, note: "Return to the important thread." },
      ],
      attachment_bonds: [],
      learned_knowledge_edges: [],
    },
  };
  fs.writeFileSync(file, `${JSON.stringify(bundle, null, 2)}\n`, "utf8");
}

function applyBundle(cart, bundle) {
  const run = spawnSync(process.execPath, [IMPORT_TOOL, cart, bundle, "--apply"], {
    cwd: ROOT,
    encoding: "utf8",
  });
  if (run.status !== 0) throw new Error(run.stderr || run.stdout || "import apply failed");
  return JSON.parse(run.stdout);
}

function runScenario(cart, actor, knowledgePrompt) {
  return new Promise((resolve, reject) => {
    const proc = spawn(HOST, ["--stdio", cart], {
      cwd: ROOT,
      env: { ...process.env, PE_RENDER_BACKEND: "template", PE_TODAY_SEED: "0x1a2b3c4d" },
    });
    let stdout = "";
    let stderr = "";
    proc.stdout.on("data", d => stdout += d.toString("utf8"));
    proc.stderr.on("data", d => stderr += d.toString("utf8"));
    proc.on("error", reject);
    proc.on("close", status => {
      if (status !== 0) {
        reject(new Error(stderr || `scenario exited ${status}`));
        return;
      }
      const rows = stdout
        .split("\n")
        .filter(line => line.trim().startsWith("{"))
        .map(line => JSON.parse(line));
      resolve(rows);
    });
    const turns = [{ method: "set_user", user_id: actor }];
    for (let i = 0; i < 14; ++i)
      turns.push({ method: "chat", text: "The room is quiet." });
    turns.push({ method: "chat", text: knowledgePrompt });
    turns.push({ method: "close" });
    proc.stdin.write(turns.map(t => JSON.stringify(t)).join("\n") + "\n");
    proc.stdin.end();
    setTimeout(() => proc.kill("SIGKILL"), 20000).unref();
  });
}

function importedCallback(rows) {
  let found = null;
  rows.forEach((row, index) => {
    if (!row.state) return;
    const flags = Number(row.state.last_callback_memory_flags || 0);
    if ((flags & IMPORTED_FLAG) !== 0) found = { row, index, flags };
  });
  return found;
}

(async function main() {
  let fail = 0;
  function ok(cond, msg) {
    if (cond) console.log(`ok:   ${msg}`);
    else { console.error(`FAIL: ${msg}`); fail++; }
  }

  if (!fs.existsSync(HOST)) {
    console.error("persona_host not built");
    process.exit(2);
  }

  for (const profile of PROFILES) {
    const baselineCart = makeTempCart(profile, `${profile.slug}-baseline`);
    const importedCart = makeTempCart(profile, `${profile.slug}-imported`);
    const bundle = path.join(path.dirname(importedCart), "history_bundle.json");
    writeBundle(bundle, profile);
    const applyResult = applyBundle(importedCart, bundle);
    const baselineRows = await runScenario(baselineCart, profile.actor, profile.knowledgePrompt);
    const importedRows = await runScenario(importedCart, profile.actor, profile.knowledgePrompt);
    const baselineHit = importedCallback(baselineRows);
    const importedHit = importedCallback(importedRows);
    const baselineReply = (baselineRows.filter(r => Object.prototype.hasOwnProperty.call(r, "reply")).slice(-1)[0] || {}).reply || "";
    const importedReply = (importedRows.filter(r => Object.prototype.hasOwnProperty.call(r, "reply")).slice(-1)[0] || {}).reply || "";

    ok(applyResult.counts.applied_episodic_memories >= 1,
       `${profile.slug}: import apply wrote episodic memory`);
    ok(applyResult.counts.applied_learned_knowledge >= 1,
       `${profile.slug}: import apply wrote learned knowledge`);
    ok(!baselineHit, `${profile.slug}: baseline run has no imported callback`);
    ok(!!importedHit, `${profile.slug}: imported history surfaces offline without direct prompting`);
    if (importedHit) {
      ok((importedHit.flags & PINNED_FLAG) !== 0,
         `${profile.slug}: imported callback preserved pinned significance`);
      ok(importedHit.row.state.intent === "probe" ||
         importedHit.row.state.intent === "reminisce" ||
         importedHit.row.state.intent === "attend" ||
         importedHit.row.state.intent === "redirect",
         `${profile.slug}: imported callback changes behavioral pressure (${importedHit.row.state.intent})`);
    }
    ok(profile.knowledgeExpect.test(importedReply),
       `${profile.slug}: imported learned knowledge answers offline with canonical content (${importedReply})`);
    ok(!profile.knowledgeExpect.test(baselineReply),
       `${profile.slug}: baseline offline reply does not already contain the imported knowledge (${baselineReply})`);
  }

  if (fail) process.exit(1);
  console.log("PASSED -- imported history changes offline memory and knowledge behavior");
})().catch(err => {
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(2);
});
