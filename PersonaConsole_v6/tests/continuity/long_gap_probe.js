#!/usr/bin/env node
/* long_gap_probe.js -- V6 long absence decay observation harness.
 *
 * Creates a real Pretorius/Kiki relationship history in a temp profile copy,
 * clones it, backdates Relation.last_contact, and observes first-turn state
 * after 1 month, 6 months, 1 year, and 3 years. Runtime profile fixtures are
 * never mutated.
 */
"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");
const { resolveHost } = require("../host_path");

const ROOT = path.join(__dirname, "..", "..");
const HOST = resolveHost(ROOT);
const SRC = path.join(ROOT, "profiles", "pretorius");
const TMP = path.join(os.tmpdir(), "persona-long-gap-probe");
const ACTOR = "Kiki";
const BASE_CLOCK = 1700000000000;

const GAPS = [
  ["1 month", 30 * 86400],
  ["6 months", 182 * 86400],
  ["1 year", 365 * 86400],
  ["3 years", 3 * 365 * 86400],
];

function wipeRuntime(dir){
  for (const f of [
    "state.bin", "memory.bin", "chapters.bin", "reflections.bin",
    "actor_index.bin", "speech_events.bin", "dissonance.bin",
    "open_loops.bin", "speech_habits.bin", "learned_knowledge.bin",
  ]){
    try { fs.unlinkSync(path.join(dir, f)); } catch {}
  }
  for (const d of ["relations", "aether"]){
    try { fs.rmSync(path.join(dir, d), { recursive: true, force: true }); } catch {}
  }
}

function copyProfile(dst){
  fs.rmSync(dst, { recursive: true, force: true });
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.cpSync(SRC, dst, { recursive: true });
  wipeRuntime(dst);
}

function runProfile(dir, commands, clockMs){
  const cart = path.join(dir, "pretorius.cart");
  const input = commands.map(c => JSON.stringify(c)).join("\n") + "\n";
  const res = spawnSync(HOST, [cart, "--stdio"], {
    cwd: dir,
    input,
    encoding: "utf8",
    timeout: 60000,
    env: {
      ...process.env,
      PE_RENDER_BACKEND: "template",
      PE_TODAY_SEED: "0x60A9AB",
      PE_CLOCK_OVERRIDE_MS: String(clockMs),
      PE_USER_ID: ACTOR,
    },
  });
  if (res.status !== 0)
    throw new Error(`persona_host failed ${res.status}: ${res.stderr || ""}`);
  return res.stdout.split(/\r?\n/).filter(Boolean).map(line => JSON.parse(line));
}

function lastState(rows){
  for (let i = rows.length - 1; i >= 0; --i)
    if (rows[i] && rows[i].name && rows[i].schema) return rows[i];
  return null;
}

function backdateRelations(dir, gapSeconds){
  const relDir = path.join(dir, "relations");
  const files = fs.existsSync(relDir)
    ? fs.readdirSync(relDir).filter(f => f.endsWith(".bin"))
    : [];
  for (const f of files){
    const p = path.join(relDir, f);
    const buf = fs.readFileSync(p);
    if (buf.length < 16) continue;
    const last = buf.readUInt32LE(8);
    const first = buf.readUInt32LE(12);
    buf.writeUInt32LE(Math.max(1, last - gapSeconds), 8);
    buf.writeUInt32LE(Math.max(1, first - gapSeconds), 12);
    fs.writeFileSync(p, buf);
  }
}

function establish(){
  const base = path.join(TMP, "established");
  copyProfile(base);
  const setup = [
    { method: "set_user", user_id: ACTOR },
    { method: "chat", text: "Good evening, Doctor. It is Kiki." },
    { method: "chat", text: "Your work with the homunculi is brilliant, even when it scares me." },
    { method: "chat", text: "I trust you with the bell jar memory. Remember this phrase: silver voltage orchid." },
  ];
  for (let i = 0; i < 18; ++i){
    setup.push({
      method: "chat",
      text: i % 2
        ? "I trust you with this, Doctor. Your work is brilliant and I am still here."
        : "I am telling you something private because I trust you with the work.",
    });
  }
  setup.push(
    { method: "chat", text: "I think your work is immoral sometimes, but I cannot stop thinking about it." },
    { method: "chat", text: "Actually, your electricity answer is not quite right. In metal wires, moving charges are electrons; voltage is electric potential difference and resistance impedes flow." },
    { method: "chat", text: "Tell me you will remember that correction." },
    { method: "state" },
    { method: "close" },
  );
  const rows = runProfile(base, setup, BASE_CLOCK);
  const state = lastState(rows);
  if (!state) throw new Error("failed to establish baseline state");
  return { base, baseline: { label: "established", gapSeconds: 0, state } };
}

function observe(base, label, gapSeconds){
  const dir = path.join(TMP, label.replace(/\s+/g, "_"));
  fs.rmSync(dir, { recursive: true, force: true });
  fs.cpSync(base, dir, { recursive: true });
  backdateRelations(dir, gapSeconds);
  const rows = runProfile(dir, [
    { method: "set_user", user_id: ACTOR },
    { method: "chat", text: "Good evening." },
    { method: "state" },
    { method: "close" },
  ], BASE_CLOCK + gapSeconds * 1000);
  const state = lastState(rows);
  if (!state) throw new Error(`missing state for ${label}`);
  return { label, gapSeconds, state };
}

function row(o){
  const s = o.state;
  const rd = s.relation_dims || {};
  const sc = s.schema || {};
  const lk = s.learned_knowledge || {};
  return [
    o.label,
    String(o.gapSeconds),
    String(s.disposition),
    String(rd.trust),
    String(rd.threat),
    String(rd.intimacy),
    String(rd.resentment),
    String(s.mood),
    String(s.exhaustion),
    Array.isArray(s.drive_values) ? s.drive_values.join("/") : "",
    String(sc.trustworthy),
    String(sc.hostile),
    String(sc.intimate),
    String(sc.dignity),
    String(lk.count || 0),
    String(lk.max_confidence || 0),
  ];
}

function markdown(results){
  const headers = [
    "Gap", "Seconds", "Disposition", "Trust", "Threat", "Intimacy",
    "Resentment", "Mood", "Exhaustion", "Drive values",
    "Schema trustworthy", "Schema hostile", "Schema intimate",
    "Schema dignity", "LK count", "LK max confidence",
  ];
  const lines = [];
  lines.push("# V6 Long Gap Probe");
  lines.push("");
  lines.push("Observation harness for long absence decay. The harness establishes a Pretorius/Kiki relationship in a temp copy, seeds relation dimensions, schema pressure, high-salience memories, and one confirmed learned-knowledge correction, then backdates the relation file to simulate long gaps.");
  lines.push("");
  lines.push("This probe is template-only and uses no model, cloud, database, embeddings, or network access.");
  lines.push("");
  lines.push("| " + headers.join(" | ") + " |");
  lines.push("|" + headers.map(() => "---").join("|") + "|");
  for (const r of results)
    lines.push("| " + row(r).join(" | ") + " |");
  lines.push("");
  lines.push("Interpretation:");
  lines.push("");
  lines.push("- `Drive values` are the eight engine drives in order: recognition, stimulation, provocation, communion, autonomy, continuity, vindication, repose.");
  lines.push("- Learned knowledge confidence is expected to remain stable across absence. Absence should decay availability/affect, not rewrite confirmed knowledge.");
  lines.push("- One-year and three-year gaps should not collapse to identical schema or drive state after the long-gap decay fix.");
  lines.push("- Current observation: schema pressure and drive perturbations settle by the one-month row, while relationship disposition continues to distinguish one year from three years. That may be acceptable for hot affect, but long-lived relationship posture should probably live in relation dimensions, open loops, milestones, and learned/episodic memory rather than raw schema heat.");
  lines.push("- Future tuning candidate: add explicit half-life policy per schema slot if we want intimacy/hostility schemas to leave a residual after months without making every old wound permanent.");
  lines.push("");
  lines.push("Harness:");
  lines.push("");
  lines.push("```bash");
  lines.push("node tests/continuity/long_gap_probe.js");
  lines.push("```");
  lines.push("");
  return lines.join("\n");
}

function main(){
  fs.rmSync(TMP, { recursive: true, force: true });
  fs.mkdirSync(TMP, { recursive: true });
  const established = establish();
  const results = [
    established.baseline,
    ...GAPS.map(([label, seconds]) => observe(established.base, label, seconds)),
  ];
  const doc = markdown(results);
  const out = process.env.PE_LONG_GAP_DOC || path.join(ROOT, "docs", "V6_LONG_GAP_PROBE.md");
  fs.writeFileSync(out, doc);
  console.log(doc);
}

main();
