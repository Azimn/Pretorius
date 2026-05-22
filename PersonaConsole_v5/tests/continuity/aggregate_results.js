#!/usr/bin/env node
/* aggregate_results.js — V4 model-comparison harness.
 *
 * Ingests JSON reports produced by:
 *   continuity_benchmark.js --json results/<run>.json
 *   behavioral_drift.js     --json results/<run>.json
 *
 * Produces a side-by-side table comparing:
 *   - same script across template + multiple SLMs
 *   - same SLM across different quantizations
 *   - same SLM across repeated runs (stability check)
 *
 * Output: markdown table to stdout (suitable for paste into README,
 * GDC talk, paper, or wiki).  Optional --html and --csv exporters.
 *
 * Usage:
 *   node aggregate_results.js [--md|--csv|--html] [results/*.json]
 *   node aggregate_results.js                       # defaults: results/*.json → markdown
 *
 * Convention for filenames (any pattern works, but these get prettier
 * labels): results/<runner>_<model>_<quant>.json
 *   results/continuity_template.json
 *   results/continuity_gemma2-2b_q4.json
 *   results/drift_qwen2.5-1.5b_q4.json
 *   results/drift_smollm2-1.7b_q4.json
 */
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
let format = 'md';
const inputs = [];
for (let i = 0; i < args.length; ++i){
  if (args[i] === '--md')   format = 'md';
  else if (args[i] === '--csv')  format = 'csv';
  else if (args[i] === '--html') format = 'html';
  else inputs.push(args[i]);
}

if (inputs.length === 0){
  const dir = path.join(__dirname, '..', '..', 'results');
  if (fs.existsSync(dir)){
    for (const f of fs.readdirSync(dir).sort()){
      if (f.endsWith('.json')) inputs.push(path.join(dir, f));
    }
  }
}

if (inputs.length === 0){
  console.error('no JSON inputs found.  produce some with:');
  console.error('  node tests/continuity/continuity_benchmark.js --json results/continuity_template.json');
  console.error('  PE_RENDER_BACKEND=slm PE_SLM_PROVIDER=ollama PE_OLLAMA_MODEL=gemma2:2b \\');
  console.error('    node tests/continuity/behavioral_drift.js --slm --json results/drift_gemma2-2b.json');
  process.exit(1);
}

const reports = [];
for (const p of inputs){
  try {
    const r = JSON.parse(fs.readFileSync(p, 'utf-8'));
    r._source = path.basename(p);
    r._kind   = r.bdi != null ? 'drift'
              : r.axes && r.axes.emotional_persistence != null ? 'continuity'
              : 'unknown';
    reports.push(r);
  } catch (e){
    console.error(`skip ${p}: ${e.message}`);
  }
}

/* Derive a short label from filename pattern <kind>_<model>_<quant>.json */
function labelOf(r){
  const base = r._source.replace(/\.json$/, '');
  return base.split('_').slice(1).join('_') || base;
}

const continuity = reports.filter(r => r._kind === 'continuity');
const drift      = reports.filter(r => r._kind === 'drift');

/* ----- continuity table ----- */
function renderContinuityMD(){
  if (continuity.length === 0) return '';
  const cols = ['emotional_persistence','schema_stability','relational_continuity',
                'autobiographical_consistency','renderer_invariance'];
  let out = '\n## Continuity Benchmark — score per axis (0–100, higher better)\n\n';
  out += '| Model | Overall | ' + cols.map(c =>
    c.replace(/_/g, ' ').replace(/\b\w/g, ch => ch.toUpperCase())
  ).join(' | ') + ' |\n';
  out += '|---|---:|' + cols.map(() => '---:').join('|') + '|\n';
  for (const r of continuity){
    const row = [labelOf(r), r.overall];
    for (const c of cols){
      row.push(r.axes[c] ? r.axes[c].score : '—');
    }
    out += '| ' + row.join(' | ') + ' |\n';
  }
  return out;
}

/* ----- drift table ----- */
function renderDriftMD(){
  if (drift.length === 0) return '';
  const cols = ['stance_preservation','hostility_leakage','memory_omission',
                'invented_lore','emotional_mismatch','schema_contradiction',
                'slm_self_consistency'];
  let out = '\n## Behavioral Drift Index — divergence per axis (0–100, lower better)\n\n';
  out += '| Model | BDI | ' + cols.map(c =>
    c.replace(/_/g, ' ').replace(/\b\w/g, ch => ch.toUpperCase())
  ).join(' | ') + ' |\n';
  out += '|---|---:|' + cols.map(() => '---:').join('|') + '|\n';
  for (const r of drift){
    const row = [labelOf(r), r.bdi];
    for (const c of cols) row.push(r.axes[c] != null ? r.axes[c] : '—');
    out += '| ' + row.join(' | ') + ' |\n';
  }
  return out;
}

/* ----- summary block ----- */
function renderSummaryMD(){
  let out = '# V4 Model Comparison\n\n';
  out += `Generated: ${new Date().toISOString()}\n\n`;
  out += `Inputs: ${reports.length} report(s)\n`;
  for (const r of reports){
    out += `  - ${r._source} (${r._kind})\n`;
  }
  return out;
}

function renderCSV(){
  let rows = [];
  for (const r of reports){
    if (r._kind === 'continuity'){
      const axes = r.axes;
      rows.push([
        'continuity', labelOf(r), r.overall,
        axes.emotional_persistence.score,
        axes.schema_stability.score,
        axes.relational_continuity.score,
        axes.autobiographical_consistency.score,
        axes.renderer_invariance.score,
      ].join(','));
    } else if (r._kind === 'drift'){
      const a = r.axes;
      rows.push([
        'drift', labelOf(r), r.bdi,
        a.stance_preservation, a.hostility_leakage, a.memory_omission,
        a.invented_lore, a.emotional_mismatch, a.schema_contradiction,
        a.slm_self_consistency,
      ].join(','));
    }
  }
  let header = 'kind,model,score,a1,a2,a3,a4,a5,a6,a7\n';
  return header + rows.join('\n') + '\n';
}

function renderHTML(){
  let body = '<style>body{font:14px/1.5 system-ui;margin:40px}table{border-collapse:collapse}th,td{padding:6px 12px;border:1px solid #ddd}th{background:#f4f4f4;text-align:left}</style>';
  body += renderSummaryMD().replace(/^#/, '<h1>').replace(/$/, '</h1>');
  /* lazy: just render the markdown tables as <pre> */
  body += '<pre>' + renderContinuityMD() + renderDriftMD() + '</pre>';
  return '<!doctype html><html><body>' + body + '</body></html>';
}

let out = '';
if (format === 'md'){
  out = renderSummaryMD() + renderContinuityMD() + renderDriftMD();
} else if (format === 'csv'){
  out = renderCSV();
} else if (format === 'html'){
  out = renderHTML();
}
process.stdout.write(out);
