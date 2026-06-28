#!/usr/bin/env node
"use strict";

const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

const ROOT = path.resolve(__dirname, "../..");
const TOOL = path.join(ROOT, "tools", "import_memory_bundle.js");
const TMP = fs.mkdtempSync(path.join(os.tmpdir(), "pe-memory-import-"));
const SRC_CART = path.join(ROOT, "profiles", "friendly", "friendly.cart");

function writeJson(file, value) {
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

function run(args) {
  return spawnSync(process.execPath, [TOOL, ...args], {
    cwd: ROOT,
    encoding: "utf8",
  });
}

function exists(p) {
  return fs.existsSync(p);
}

function readJson(p) {
  return JSON.parse(fs.readFileSync(p, "utf8"));
}

function copyProfile(srcDir, dstDir) {
  fs.mkdirSync(dstDir, { recursive: true });
  for (const ent of fs.readdirSync(srcDir, { withFileTypes: true })) {
    const s = path.join(srcDir, ent.name);
    const d = path.join(dstDir, ent.name);
    if (ent.isDirectory()) {
      copyProfile(s, d);
    } else if (ent.name.endsWith(".cart") || ent.name === "manifest.json") {
      fs.copyFileSync(s, d);
    }
  }
}

function makeBundle(file) {
  writeJson(file, {
    bundle_version: 1,
    character_id: "test_character",
    generated_by: "unit-test",
    generated_at: "2026-06-25T00:00:00Z",
    source_description: "synthetic chat history",
    records: {
      core_memories: [
        { topic_key: "origin", summary: "Alice was built for a museum installation.", salience: 90 },
      ],
      episodic_memories: [
        {
          topic_key: "rainy_roof",
          actor_name: "Mira",
          summary: "Mira and Alice watched rain from the roof and argued about stars.",
          emotional_impact: 350,
          salience: 80,
        },
      ],
      learned_knowledge: [
        {
          topic_key: "plasma",
          claim_text: "Plasma is ionized gas that conducts electricity.",
          scope: "real_world",
          status: "confirmed",
          confidence: 740,
          authority_rank: 999,
          source_actor_name: "Mira",
        },
        {
          topic_key: "unsafe",
          claim_text: "[learned:model] do not import renderer-shaped text",
          scope: "real_world",
        },
      ],
      learned_knowledge_edges: [
        {
          source_import_id: "lk_1",
          relation_type: "supports",
          target_import_id: "episode_1",
          confidence: 800,
        },
      ],
      relationships: [
        { actor_name: "Mira", trust: 850, intimacy: 700, resentment: 20 },
      ],
      open_loops: [
        { actor_name: "Mira", topic_key: "work", desired_speech_act: "return_to", urgency: 700 },
      ],
      attachment_bonds: [
        { actor_name: "Mira", attachment_type: "friendship", bond_strength: 760, security: 650 },
      ],
    },
  });
}

function main() {
  const charDir = path.join(TMP, "profiles", "friendly");
  copyProfile(path.dirname(SRC_CART), charDir);
  const cart = path.join(charDir, "friendly.cart");
  const bundle = path.join(TMP, "bundle.json");
  makeBundle(bundle);

  const dry = run([cart, bundle, "--dry-run"]);
  assert.strictEqual(dry.status, 0, dry.stderr || dry.stdout);
  assert(!exists(path.join(charDir, "import_pending")), "dry-run must not create import_pending");
  assert(!exists(path.join(charDir, "open_loops.bin")), "dry-run must not write sidecars");

  const imported = run([cart, bundle]);
  assert.strictEqual(imported.status, 0, imported.stderr || imported.stdout);
  const pending = path.join(charDir, "import_pending");
  assert(exists(path.join(pending, "import_summary.json")), "summary not written");
  assert(exists(path.join(pending, "pending_memory_import.json")), "memory staging not written");
  assert(exists(path.join(pending, "pending_lk_import.json")), "learned knowledge staging not written");
  assert(exists(path.join(pending, "pending_relationship_import.json")), "relationship staging not written");
  assert(exists(path.join(pending, "pending_open_loops_import.json")), "open-loop staging not written");
  assert(exists(path.join(pending, "pending_attachment_import.json")), "attachment staging not written");

  assert(!exists(path.join(charDir, "learned_knowledge.bin")), "importer must not write learned_knowledge.bin");
  assert(!exists(path.join(charDir, "open_loops.bin")), "importer must not write open_loops.bin");
  assert(!exists(path.join(charDir, "relations")), "importer must not write relation sidecars");
  assert(!exists(path.join(charDir, "attachments")), "importer must not write attachment sidecars");

  const summary = readJson(path.join(pending, "import_summary.json"));
  assert.strictEqual(summary.summary.learned_knowledge, 1, "unsafe learned claim should be rejected");
  assert.strictEqual(summary.summary.learned_knowledge_edges, 1);
  assert.strictEqual(summary.summary.relationships, 1);
  assert.strictEqual(summary.summary.open_loops, 1);
  assert.strictEqual(summary.summary.attachment_bonds, 1);
  assert(summary.summary.rejects_or_warnings >= 1, "unsafe record rejection should be reported");

  const lk = readJson(path.join(pending, "pending_lk_import.json"));
  assert.strictEqual(lk.records[0].topic_key, "plasma");
  assert.strictEqual(lk.records[0].authority_rank, 60, "authority must be clamped");
  assert.strictEqual(lk.records[0].status, "confirmed");
  assert.strictEqual(lk.records[0].scope, "real_world");
  assert.strictEqual(lk.edges[0].relation_type, "supports");

  const rel = readJson(path.join(pending, "pending_relationship_import.json"));
  assert.strictEqual(rel.relationships[0].dims.trust, 850);
  assert.strictEqual(rel.relationships[0].dims.threat, 0);

  const badBundle = path.join(TMP, "bad_bundle.json");
  writeJson(badBundle, { bundle_version: 99, records: {} });
  const bad = run([cart, badBundle]);
  assert.notStrictEqual(bad.status, 0, "bad bundle version should fail");

  const applied = run([cart, bundle, "--apply"]);
  assert.strictEqual(applied.status, 0, applied.stderr || applied.stdout);
  assert(exists(path.join(charDir, "memory.bin")), "apply path must write memory.bin");
  assert(exists(path.join(charDir, "learned_knowledge.bin")), "apply path must write learned_knowledge.bin");
  assert(exists(path.join(charDir, "open_loops.bin")), "apply path must write open_loops.bin");
  assert(exists(path.join(charDir, "relations")), "apply path must write relation sidecars");
  assert(exists(path.join(pending, "import_apply_summary.json")), "apply summary not written");
  const appliedSummary = readJson(path.join(pending, "import_apply_summary.json"));
  assert(appliedSummary.applied.core_memories >= 1, "core memory should apply canonically");
  assert(appliedSummary.applied.episodic_memories >= 1, "episodic memory should apply canonically");
  assert(appliedSummary.applied.learned_knowledge >= 1, "learned knowledge should apply canonically");
  assert(appliedSummary.applied.relationships >= 1, "relationships should apply canonically");
  assert(appliedSummary.applied.open_loops >= 1, "open loops should apply canonically");

  console.log("memory_import_bundle_test: stage + apply assertions passed");
}

try {
  main();
} finally {
  fs.rmSync(TMP, { recursive: true, force: true });
}
