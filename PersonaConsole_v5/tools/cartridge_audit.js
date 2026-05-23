#!/usr/bin/env node
/* cartridge_audit.js — actionable cartridge-quality diagnostics.
 *
 * Where `cartridge_stats` reports COUNTS (how many patterns, how many
 * templates per intent), this tool answers WHY a cart underperforms on
 * the continuity benchmark.  In particular:
 *
 *   - obsession → template cross-reference
 *     For each declared obsession, count templates whose text contains
 *     the obsession word OR uses {topic}.  An obsession with 0 such
 *     templates is "orphan" — it can never reach the user.
 *
 *   - pattern → template group cross-reference
 *     For each pattern group (G_INSULT, G_PRAISE, etc.) that the cart
 *     declares, count templates in that group.  Patterns that route to
 *     empty groups produce fallback dominance.
 *
 *   - {topic} usage rate
 *     Templates with {topic} can carry any obsession.  Templates without
 *     are inert to topic_pull.  A cartridge with few {topic} templates
 *     will struggle to surface obsessions even when obsession_pressure
 *     is high.
 *
 *   - obsession_strength sanity
 *     A declared obsession with strength = 0 (default 50) ramps slower
 *     than authored 90+ obsessions.  Flag for explicit authoring.
 *
 * Output: markdown report with per-finding recommendations.  Optional
 * --json out.json for machine consumption.  Pass --fail-on-orphan to
 * exit non-zero if any obsessions are unreachable.
 *
 * Usage:
 *   node cartridge_audit.js profiles/pretorius
 *   node cartridge_audit.js profiles/kiki --json results/audit_kiki.json
 *   node cartridge_audit.js profiles/pretorius --fail-on-orphan
 */
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
let PROFILE = null;
let JSON_OUT = null;
let FAIL_ON_ORPHAN = false;
for (let i = 0; i < args.length; ++i){
  if (args[i] === '--json' && args[i+1]) JSON_OUT = args[++i];
  else if (args[i] === '--fail-on-orphan') FAIL_ON_ORPHAN = true;
  else if (!args[i].startsWith('--')) PROFILE = args[i];
}
if (!PROFILE){
  console.error('usage: cartridge_audit.js <profile-dir> [--json out.json] [--fail-on-orphan]');
  process.exit(1);
}

/* ----- layout constants — must mirror persona.h V5 ----- */
const PE_OBSESSION_COUNT  = 8;
const PE_ADDRESS_COUNT    = 4;
const PE_ADDRESS_LEN      = 16;
const PE_NAME_LEN         = 32;
const PE_CORE_SEED_MAX    = 20;
const PE_FLOURISH_COUNT   = 4;
const PE_FLOURISH_LEN     = 48;
const PE_EXPANSION_COUNT  = 4;
const PE_EXPANSION_LEN    = 48;
const PE_TOPIC_MAX        = 64;
const PE_TOPIC_NAME       = 16;
const PE_PATTERN_MAX      = 256;
const PE_PATTERN_KW_LEN   = 32;
const PE_TEMPLATE_MAX     = 1024;
const PE_TEMPLATE_TEXT    = 256;
const SZ_MEMNODE          = 127;

/* ----- read a NUL-terminated string of fixed max length ----- */
function readCstr(buf, off, max){
  let end = off;
  while (end < off + max && buf[end] !== 0) ++end;
  return buf.slice(off, end).toString('utf-8');
}

/* ----- parse identity.bin ----- */
function parseIdentity(file){
  const buf = fs.readFileSync(file);
  let off = 0;
  const big5 = {
    O: buf.readUInt16LE(off+0), C: buf.readUInt16LE(off+2),
    E: buf.readUInt16LE(off+4), A: buf.readUInt16LE(off+6),
    N: buf.readUInt16LE(off+8),
  };
  off += 10 + 2;                       /* big5 + _pad0 */
  const voice_flags = buf.readUInt32LE(off); off += 4;
  const obsessions = [];
  for (let i = 0; i < PE_OBSESSION_COUNT; ++i){
    obsessions.push(buf.readUInt16LE(off));
    off += 2;
  }
  const taboos = [];
  for (let i = 0; i < 8; ++i){
    taboos.push(buf.readUInt16LE(off));
    off += 2;
  }
  /* V5 layout: obsession_strength + _pad_obs come BEFORE address_user_as,
   * NOT at the end (the engine struct in persona.h is authoritative). */
  const obsession_strength = [];
  for (let i = 0; i < PE_OBSESSION_COUNT; ++i){
    obsession_strength.push(buf.readUInt8(off + i));
  }
  off += PE_OBSESSION_COUNT;
  off += PE_OBSESSION_COUNT;   /* _pad_obs */
  off += PE_ADDRESS_COUNT * PE_ADDRESS_LEN;
  const name = readCstr(buf, off, PE_NAME_LEN); off += PE_NAME_LEN;
  const core_count = buf.readUInt8(off);
  return { name, big5, voice_flags, obsessions, taboos, obsession_strength, core_count };
}

/* ----- parse topics.bin: count(u32) + entries ----- */
function parseTopics(file){
  const buf = fs.readFileSync(file);
  const count = buf.readUInt32LE(0);
  const topics = [];
  const ENTRY = 4 + PE_TOPIC_NAME + 6*2 + 2;
  for (let i = 0; i < PE_TOPIC_MAX; ++i){
    const off = 4 + i * ENTRY;
    if (off + ENTRY > buf.length) break;
    const id = buf.readUInt32LE(off);
    if (id === 0) continue;
    const name = readCstr(buf, off + 4, PE_TOPIC_NAME);
    if (name) topics.push({ id, name });
  }
  return { count, list: topics };
}

/* ----- parse templates.bin ----- */
function parseTemplates(file){
  const buf = fs.readFileSync(file);
  const count = buf.readUInt32LE(0);
  const ENTRY = 26 + PE_TEMPLATE_TEXT;
  const templates = [];
  for (let i = 0; i < PE_TEMPLATE_MAX; ++i){
    const off = 4 + i * ENTRY;
    if (off + ENTRY > buf.length) break;
    const id = buf.readUInt16LE(off + 0);
    if (id === 0) continue;
    const group = buf.readUInt16LE(off + 2);
    const intent = buf.readUInt8(off + 4);
    const text = readCstr(buf, off + 26, PE_TEMPLATE_TEXT);
    templates.push({ id, group, intent, text });
  }
  return { count, list: templates };
}

/* ----- parse patterns.bin ----- */
function parsePatterns(file){
  const buf = fs.readFileSync(file);
  const count = buf.readUInt32LE(0);
  const ENTRY = PE_PATTERN_KW_LEN + 11;  /* keyword + topic + 3*int8 + cls + flags + group + kw_len + first_char */
  const patterns = [];
  for (let i = 0; i < PE_PATTERN_MAX; ++i){
    const off = 4 + i * ENTRY;
    if (off + ENTRY > buf.length) break;
    const kw = readCstr(buf, off, PE_PATTERN_KW_LEN);
    if (!kw) continue;
    const topic_id = buf.readUInt16LE(off + PE_PATTERN_KW_LEN);
    const input_class = buf.readInt8(off + PE_PATTERN_KW_LEN + 5);
    const flags = buf.readUInt8(off + PE_PATTERN_KW_LEN + 6);
    const group = buf.readUInt16LE(off + PE_PATTERN_KW_LEN + 7);
    patterns.push({ kw, topic_id, input_class, flags, group });
  }
  return { count, list: patterns };
}

const INTENT_NAMES = ['answer','evade','accuse','flatter','threaten','probe',
                      'redirect','monologue','reminisce','withdraw','joke','boast',
                      'initiate','attend','clarify','pause'];
const intentName = i => INTENT_NAMES[i] || `intent${i}`;

/* ----- main ----- */
const id      = parseIdentity (path.join(PROFILE, 'identity.bin'));
const topics  = parseTopics   (path.join(PROFILE, 'dialogue', 'topics.bin'));
const tmpl    = parseTemplates(path.join(PROFILE, 'dialogue', 'templates.bin'));
const ptn     = parsePatterns (path.join(PROFILE, 'dialogue', 'patterns.bin'));

const topicById = Object.fromEntries(topics.list.map(t => [t.id, t.name]));
const obsessions = id.obsessions
  .filter(tid => tid !== 0)
  .map((tid, idx) => ({
    topic_id: tid,
    name: topicById[tid] || `topic_${tid}`,
    strength: id.obsession_strength[idx] || 50,   /* 0 = default 50 in engine */
  }));

/* --- count templates that explicitly mention each obsession word --- */
function countExplicitMentions(obsessionName){
  const needle = obsessionName.toLowerCase();
  let count = 0;
  for (const t of tmpl.list){
    if (t.text.toLowerCase().includes(needle)) ++count;
  }
  return count;
}

const TOPIC_SLOT_RE = /\{topic\}/i;
const topicSlotTemplates = tmpl.list.filter(t => TOPIC_SLOT_RE.test(t.text));

/* --- pattern groups → template groups cross reference --- */
const patternGroups = new Map();           /* group_id → set of pattern keywords */
for (const p of ptn.list){
  if (p.group === 0xFFFF) continue;
  if (!patternGroups.has(p.group)) patternGroups.set(p.group, new Set());
  patternGroups.get(p.group).add(p.kw);
}
const templateGroups = new Map();          /* group_id → count */
for (const t of tmpl.list){
  if (t.group === 0xFFFF) continue;
  templateGroups.set(t.group, (templateGroups.get(t.group) || 0) + 1);
}

/* --- findings collection --- */
const findings = [];
const orphan_obsessions = [];
const weak_obsessions = [];
const dead_pattern_groups = [];

/* Obsession audit */
const obsession_report = [];
for (const o of obsessions){
  const direct = countExplicitMentions(o.name);
  /* topic-slot templates COULD surface this obsession when target_topic is set */
  const via_slot = topicSlotTemplates.length;
  const total_reach = direct + (via_slot > 0 ? Math.min(via_slot, 3) : 0);
  obsession_report.push({
    name: o.name, strength: o.strength,
    direct_mentions: direct, topic_slot_templates: via_slot,
    effective_reach: total_reach,
    status: direct === 0 && via_slot === 0 ? 'orphan'
          : direct === 0 && via_slot > 0   ? 'topic-slot-only'
          : direct < 3                      ? 'thin'
          :                                  'healthy',
  });
  if (direct === 0 && via_slot === 0){
    orphan_obsessions.push(o.name);
  } else if (o.strength < 30){
    weak_obsessions.push(o.name);
  }
}

/* Pattern group audit */
for (const [group, kws] of patternGroups){
  if (group >= 0x7000) continue;   /* reserved baseline groups don't need cart templates */
  const tcount = templateGroups.get(group) || 0;
  if (tcount === 0){
    dead_pattern_groups.push({ group, patterns: [...kws] });
  }
}

/* --- generate findings list --- */
if (orphan_obsessions.length > 0){
  findings.push({
    severity: 'high',
    title: `Orphan obsession(s): ${orphan_obsessions.join(', ')}`,
    detail: `These obsessions are declared in identity.bin but NO template `
          + `references them by text AND no template has a {topic} placeholder. `
          + `The character will never surface them in conversation, so they `
          + `can't drive autobiographical_consistency. Either add 2-3 templates `
          + `whose text mentions the obsession word, OR add {topic} placeholders `
          + `to existing templates so the engine can route the obsession through them.`,
  });
}
if (topicSlotTemplates.length < 5){
  findings.push({
    severity: 'medium',
    title: `Only ${topicSlotTemplates.length} template(s) use {topic} placeholder`,
    detail: `Templates with {topic} can carry any obsession when topic_pull `
          + `is active.  Without them, the engine has no way to surface `
          + `obsessions in templates that don't already name them by text.  `
          + `Recommend at least 8-12 {topic} templates spread across `
          + `monologue/probe/reminisce/initiate intents.`,
  });
}
if (weak_obsessions.length > 0){
  findings.push({
    severity: 'low',
    title: `Obsession(s) with very low strength (<30): ${weak_obsessions.join(', ')}`,
    detail: `Default obsession_strength is 50; these are declared lower.  `
          + `Obsession_pressure ramps slower → less topic_pull on the planner. `
          + `If these are meant to be central concerns, raise them to 70-90.`,
  });
}
if (dead_pattern_groups.length > 0){
  findings.push({
    severity: 'medium',
    title: `Dead pattern group(s): ${dead_pattern_groups.length}`,
    detail: `Patterns route to template groups that have ZERO matching templates. `
          + `When these patterns fire, the engine falls back to generic dialogue. `
          + `Add templates for groups: ${dead_pattern_groups.map(d => d.group).join(', ')}.`,
  });
}

/* --- print report --- */
console.log(`# Cartridge audit: ${id.name || PROFILE}\n`);
console.log(`Profile: \`${PROFILE}\`\n`);

console.log(`## Obsession reach\n`);
console.log(`| Obsession | Strength | Direct mentions | Topic-slot reach | Status |`);
console.log(`|---|---:|---:|---:|---|`);
for (const r of obsession_report){
  const emoji = r.status === 'orphan' ? '❌'
              : r.status === 'topic-slot-only' ? '⚠️'
              : r.status === 'thin' ? '🟡' : '✅';
  console.log(`| ${r.name} | ${r.strength} | ${r.direct_mentions} | ${r.topic_slot_templates} | ${emoji} ${r.status} |`);
}

console.log(`\n## Template diagnostics\n`);
console.log(`- Total templates: **${tmpl.count}**`);
console.log(`- Templates with \`{topic}\` placeholder: **${topicSlotTemplates.length}**`);
console.log(`- Pattern groups declared: ${patternGroups.size}`);
console.log(`- Template groups declared: ${templateGroups.size}`);
console.log(`- Dead pattern groups (route to no templates): ${dead_pattern_groups.length}`);

if (findings.length > 0){
  console.log(`\n## Findings (${findings.length})\n`);
  for (const f of findings){
    console.log(`### ${f.severity === 'high' ? '🔴' : f.severity === 'medium' ? '🟡' : '🟢'} ${f.title}\n`);
    console.log(`${f.detail}\n`);
  }
} else {
  console.log(`\n✅ **No issues found.**  This cartridge is authoring-complete by the audit's heuristics.`);
}

if (JSON_OUT){
  fs.writeFileSync(JSON_OUT, JSON.stringify({
    profile: PROFILE, name: id.name,
    obsession_report, topic_slot_count: topicSlotTemplates.length,
    dead_pattern_groups, findings,
    template_count: tmpl.count, pattern_count: ptn.count,
    timestamp: new Date().toISOString(),
  }, null, 2));
  console.error(`\nwrote ${JSON_OUT}`);
}

if (FAIL_ON_ORPHAN && orphan_obsessions.length > 0){
  process.exit(1);
}
