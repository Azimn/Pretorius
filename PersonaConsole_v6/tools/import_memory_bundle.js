#!/usr/bin/env node
/*
 * V6 history and knowledge import bundle staging tool.
 *
 * Usage:
 *   node tools/import_memory_bundle.js <cart_path> <bundle.json> [--dry-run]
 *
 * This is a bootstrap/test-seeding tool, not the character's normal memory
 * organ. Living characters create future and continuous memories through the
 * C runtime. This tool validates externally prepared history/knowledge and
 * stages it as JSON so the C runtime/Forge can later promote records through
 * the same authority and firewall paths used in normal conversation.
 */

"use strict";

const fs = require("fs");
const path = require("path");

const LIMITS = {
  core: 20,
  episodic: 50,
  learned: 64,
  relationships: 32,
  openLoops: 16,
  attachments: 32,
  summary: 160,
  claim: 192,
  topic: 32,
  actor: 48,
};

const IMPORT_AUTHORITY_CAP = 60;

const LK_SCOPE = {
  real_world: 1,
  cartridge_canon: 2,
  simulation_world: 3,
  actor_specific: 4,
  relationship_specific: 5,
  session_local: 6,
  private_character_belief: 7,
  private_belief: 7,
};

const LK_STATUS = {
  provisional: 1,
  confirmed: 2,
  corrected: 3,
  disputed: 4,
  deprecated: 5,
  cartridge_authored: 6,
  world_authored: 7,
  candidate: 8,
};

const LK_EDGE = {
  corrects: 1,
  contradicts: 2,
  supports: 3,
  derived_from: 4,
  taught_by: 5,
  belongs_to_scope: 6,
  evidenced_by: 7,
  used_in_response: 8,
  related_to_actor: 9,
  related_to_topic: 10,
};

const REL_DIMS = [
  "trust", "threat", "intimacy", "resentment", "dependency",
  "obligation", "envy", "admiration", "embarrassment",
];

const report = [];

function log(kind, msg, record) {
  const entry = {
    ts: new Date().toISOString(),
    kind,
    msg,
  };
  if (record !== undefined) entry.record = record;
  report.push(entry);
  if (kind === "reject" || kind === "warn") {
    process.stderr.write(`[${kind.toUpperCase()}] ${msg}\n`);
  }
}

function usage() {
  process.stderr.write(
    "Usage: node tools/import_memory_bundle.js <cart_path> <bundle.json> [--dry-run]\n"
  );
  process.exit(2);
}

function asArray(v) {
  return Array.isArray(v) ? v : [];
}

function clamp(n, lo, hi, fallback) {
  const x = Number(n);
  if (!Number.isFinite(x)) return fallback;
  return Math.max(lo, Math.min(hi, Math.round(x)));
}

function clipString(v, max) {
  const s = String(v || "").replace(/\s+/g, " ").trim();
  return s.length > max ? s.slice(0, max - 1).trim() : s;
}

function unsafeText(s) {
  const t = String(s || "").toLowerCase();
  return (
    t.includes("[learned:") ||
    t.includes("[memory:") ||
    t.includes("[system") ||
    t.includes("[identity") ||
    t.includes("{") ||
    t.includes("}") ||
    t.includes("<start_of_turn>") ||
    t.includes("</")
  );
}

function safeKey(v, fallback) {
  const s = String(v || fallback || "")
    .toLowerCase()
    .replace(/[^a-z0-9_.:-]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return clipString(s || fallback || "unknown", LIMITS.topic);
}

function mapName(map, value, fallbackName) {
  const name = String(value || fallbackName);
  const id = Object.prototype.hasOwnProperty.call(map, name)
    ? map[name]
    : map[fallbackName];
  return { name: Object.prototype.hasOwnProperty.call(map, name) ? name : fallbackName, id };
}

function readBundle(file) {
  let parsed;
  try {
    parsed = JSON.parse(fs.readFileSync(file, "utf8"));
  } catch (err) {
    throw new Error(`could not parse bundle JSON: ${err.message}`);
  }
  if (!parsed || typeof parsed !== "object") throw new Error("bundle must be an object");
  if (parsed.bundle_version !== 1) throw new Error("bundle_version must be 1");
  if (!parsed.records || typeof parsed.records !== "object") {
    throw new Error("bundle.records must be an object");
  }
  return parsed;
}

function importMemories(bundle) {
  const records = bundle.records || {};
  const out = {
    core_memories: [],
    episodic_memories: [],
  };

  for (const rec of asArray(records.core_memories).slice(0, LIMITS.core)) {
    const summary = clipString(rec.summary || rec.text || "", LIMITS.summary);
    if (!summary || unsafeText(summary)) {
      log("reject", "unsafe or empty core memory", rec);
      continue;
    }
    out.core_memories.push({
      summary,
      topic_key: safeKey(rec.topic_key || rec.topic || "core", "core"),
      salience: clamp(rec.salience, 0, 100, 70),
      emotional_impact: clamp(rec.emotional_impact, -1000, 1000, 0),
      source_type: "imported",
      status: "pending_review",
    });
  }

  for (const rec of asArray(records.episodic_memories).slice(0, LIMITS.episodic)) {
    const summary = clipString(rec.summary || rec.text || "", LIMITS.summary);
    if (!summary || unsafeText(summary)) {
      log("reject", "unsafe or empty episodic memory", rec);
      continue;
    }
    out.episodic_memories.push({
      summary,
      topic_key: safeKey(rec.topic_key || rec.topic || "episode", "episode"),
      actor_name: clipString(rec.actor_name || rec.actor || "", LIMITS.actor),
      salience: clamp(rec.salience, 0, 100, 50),
      emotional_impact: clamp(rec.emotional_impact, -1000, 1000, 0),
      happened_at: clipString(rec.happened_at || rec.date || "", 40),
      source_type: "imported",
      status: "pending_review",
    });
  }

  return out;
}

function importLearnedKnowledge(bundle) {
  const out = {
    records: [],
    edges: [],
  };
  const seenKeys = new Set();

  for (const rec of asArray(bundle.records.learned_knowledge).slice(0, LIMITS.learned)) {
    const topicKey = safeKey(rec.topic_key || rec.topic, "knowledge");
    const claim = clipString(rec.claim_text || rec.claim || "", LIMITS.claim);
    if (!claim || unsafeText(claim)) {
      log("reject", "unsafe or empty learned-knowledge claim", rec);
      continue;
    }
    const dupeKey = `${topicKey}\n${claim.toLowerCase()}`;
    if (seenKeys.has(dupeKey)) {
      log("warn", "duplicate learned-knowledge claim dropped", rec);
      continue;
    }
    seenKeys.add(dupeKey);

    const scope = mapName(LK_SCOPE, rec.scope, "real_world");
    const status = mapName(LK_STATUS, rec.status, "candidate");
    const staged = {
      import_id: `lk_${out.records.length + 1}`,
      topic_key: topicKey,
      claim_text: claim,
      scope: scope.name,
      scope_id: scope.id,
      source_type: "imported",
      source_type_id: 7,
      source_tier: "offline",
      source_tier_id: 5,
      source_actor_name: clipString(rec.source_actor_name || rec.actor_name || "", LIMITS.actor),
      authority_rank: Math.min(
        IMPORT_AUTHORITY_CAP,
        clamp(rec.authority_rank, 0, IMPORT_AUTHORITY_CAP, 35)
      ),
      confidence: clamp(rec.confidence, 0, 1000, status.name === "confirmed" ? 650 : 450),
      status: status.name,
      status_id: status.id,
      evidence_ref: clipString(rec.evidence_ref || rec.evidence || "", 80),
      correction_of: clipString(rec.correction_of || "", 80),
      domain_tag: safeKey(rec.domain_tag || rec.domain || topicKey, topicKey),
      pending_review: true,
    };
    out.records.push(staged);
  }

  for (const edge of asArray(bundle.records.learned_knowledge_edges).slice(0, 96)) {
    const relation = mapName(LK_EDGE, edge.relation_type || edge.type, "supports");
    out.edges.push({
      source_import_id: clipString(edge.source_import_id || edge.source || "", 40),
      target_import_id: clipString(edge.target_import_id || edge.target || "", 40),
      relation_type: relation.name,
      relation_type_id: relation.id,
      weight: clamp(edge.weight, 0, 1000, 500),
      confidence: clamp(edge.confidence, 0, 1000, 500),
      pending_review: true,
    });
  }

  return out;
}

function importRelationships(bundle) {
  const out = [];
  for (const rec of asArray(bundle.records.relationships).slice(0, LIMITS.relationships)) {
    const actor = clipString(rec.actor_name || rec.actor || "", LIMITS.actor);
    if (!actor || unsafeText(actor)) {
      log("reject", "relationship import requires safe actor_name", rec);
      continue;
    }
    const dims = {};
    for (const d of REL_DIMS) dims[d] = clamp(rec[d], 0, 1000, d === "trust" ? 500 : 0);
    out.push({
      actor_name: actor,
      dims,
      evidence_ref: clipString(rec.evidence_ref || rec.evidence || "", 80),
      source_type: "imported",
      status: "pending_review",
    });
  }
  return out;
}

function importOpenLoops(bundle) {
  const out = [];
  for (const rec of asArray(bundle.records.open_loops).slice(0, LIMITS.openLoops)) {
    const topicKey = safeKey(rec.topic_key || rec.topic, "open_loop");
    const note = clipString(rec.note || rec.summary || rec.reason || "", LIMITS.summary);
    if (unsafeText(note)) {
      log("reject", "unsafe open-loop note", rec);
      continue;
    }
    out.push({
      topic_key: topicKey,
      actor_name: clipString(rec.actor_name || rec.actor || "", LIMITS.actor),
      desired_speech_act: safeKey(rec.desired_speech_act || rec.speech_act, "return_to"),
      urgency: clamp(rec.urgency, 0, 1000, 500),
      shame_cost: clamp(rec.shame_cost, 0, 1000, 0),
      avoidance_pressure: clamp(rec.avoidance_pressure, 0, 1000, 0),
      note,
      source_type: "imported",
      status: "pending_review",
    });
  }
  return out;
}

function importAttachments(bundle) {
  const out = [];
  for (const rec of asArray(bundle.records.attachment_bonds).slice(0, LIMITS.attachments)) {
    const actor = clipString(rec.actor_name || rec.actor || "", LIMITS.actor);
    if (!actor || unsafeText(actor)) {
      log("reject", "attachment import requires safe actor_name", rec);
      continue;
    }
    out.push({
      actor_name: actor,
      attachment_type: safeKey(rec.attachment_type || rec.type, "unspecified"),
      status: safeKey(rec.status, "pending_review"),
      bond_strength: clamp(rec.bond_strength, 0, 1000, 0),
      security: clamp(rec.security, 0, 1000, 500),
      anxiety: clamp(rec.anxiety, 0, 1000, 0),
      avoidance: clamp(rec.avoidance, 0, 1000, 0),
      note: clipString(rec.note || "", LIMITS.summary),
      source_type: "imported",
      status_note: "staged only; no V6 attachment sidecar is written by this importer",
      pending_review: true,
    });
  }
  return out;
}

function writeJson(file, value) {
  fs.writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

function main() {
  const args = process.argv.slice(2);
  if (args.length < 2) usage();
  const cartPath = path.resolve(args[0]);
  const bundlePath = path.resolve(args[1]);
  const dryRun = args.includes("--dry-run");
  const charDir = path.dirname(cartPath);
  if (!fs.existsSync(charDir)) {
    throw new Error(`character directory does not exist: ${charDir}`);
  }

  const bundle = readBundle(bundlePath);
  const staged = {
    metadata: {
      bundle_version: 1,
      character_id: clipString(bundle.character_id || path.basename(charDir), 80),
      generated_by: clipString(bundle.generated_by || "", 80),
      generated_at: clipString(bundle.generated_at || "", 40),
      source_description: clipString(bundle.source_description || "", 160),
      imported_at: new Date().toISOString(),
      mode: dryRun ? "dry_run" : "staged",
      authority_cap: IMPORT_AUTHORITY_CAP,
      binary_sidecars_written: false,
    },
    memory: importMemories(bundle),
    learned_knowledge: importLearnedKnowledge(bundle),
    relationships: importRelationships(bundle),
    open_loops: importOpenLoops(bundle),
    attachment_bonds: importAttachments(bundle),
  };

  const counts = {
    core_memories: staged.memory.core_memories.length,
    episodic_memories: staged.memory.episodic_memories.length,
    learned_knowledge: staged.learned_knowledge.records.length,
    learned_knowledge_edges: staged.learned_knowledge.edges.length,
    relationships: staged.relationships.length,
    open_loops: staged.open_loops.length,
    attachment_bonds: staged.attachment_bonds.length,
    rejects_or_warnings: report.filter(r => r.kind === "reject" || r.kind === "warn").length,
  };
  staged.summary = counts;

  if (!dryRun) {
    const pendingDir = path.join(charDir, "import_pending");
    fs.mkdirSync(pendingDir, { recursive: true });
    writeJson(path.join(pendingDir, "import_summary.json"), staged);
    writeJson(path.join(pendingDir, "pending_memory_import.json"), staged.memory);
    writeJson(path.join(pendingDir, "pending_lk_import.json"), staged.learned_knowledge);
    writeJson(path.join(pendingDir, "pending_relationship_import.json"), { relationships: staged.relationships });
    writeJson(path.join(pendingDir, "pending_open_loops_import.json"), { open_loops: staged.open_loops });
    writeJson(path.join(pendingDir, "pending_attachment_import.json"), { attachment_bonds: staged.attachment_bonds });

    const tmpDir = path.resolve("tmp");
    fs.mkdirSync(tmpDir, { recursive: true });
    const lines = report.map(r => JSON.stringify(r)).join("\n");
    fs.writeFileSync(path.join(tmpDir, "import_report.jsonl"), lines ? `${lines}\n` : "", "utf8");
  }

  process.stdout.write(`${JSON.stringify({ ok: true, dryRun, charDir, counts }, null, 2)}\n`);
}

try {
  main();
} catch (err) {
  process.stderr.write(`import_memory_bundle: ${err.message}\n`);
  process.exit(1);
}
