#!/usr/bin/env node
/* forge_schema_drift_test.js — catches Forge↔engine layout drift.
 *
 * The Forge has hardcoded SZ_IDENTITY / SZ_PATTERN_TABLE /
 * SZ_TEMPLATE_TABLE / SZ_FALLBACK_TABLE constants that must match the
 * engine's #pragma pack(push,1) struct sizes byte-for-byte.  When the
 * engine bumps capacity (PE_TEMPLATE_MAX, PE_CORE_SEED_MAX, etc.) the
 * Forge constants have to follow in lockstep — otherwise Forge-built
 * carts fail validation at engine load.
 *
 * This test reads the engine's actual section sizes from a freshly
 * compiled cartridge directory and compares them against the values
 * baked into CartridgeForge/forge.html.  Drift → fail.
 *
 * Strategy:
 *   - read profiles/pretorius/{identity,drives,today,banks,dialogue/*}.bin
 *   - extract each file's size
 *   - extract the matching SZ_* constant from forge.html
 *   - assert equality per section
 */
const fs = require('fs');
const path = require('path');

const V5_ROOT    = path.join(__dirname, '..');                        /* PersonaConsole_v5/ */
const REPO_ROOT  = path.join(V5_ROOT, '..');                          /* Pretorius/         */
const FORGE_HTML = path.join(REPO_ROOT, 'CartridgeForge', 'forge.html');
const PROFILE    = path.join(V5_ROOT, 'profiles', 'pretorius');

/* Map of forge constant name → on-disk section path. */
const SECTIONS = {
  SZ_IDENTITY:        path.join(PROFILE, 'identity.bin'),
  SZ_DRIVE_TABLE:     path.join(PROFILE, 'drives.bin'),
  SZ_TODAY_TABLE:     path.join(PROFILE, 'today.bin'),
  SZ_BANK_REGISTRY:   path.join(PROFILE, 'banks.bin'),
  SZ_PATTERN_TABLE:   path.join(PROFILE, 'dialogue', 'patterns.bin'),
  SZ_TEMPLATE_TABLE:  path.join(PROFILE, 'dialogue', 'templates.bin'),
  SZ_FALLBACK_TABLE:  path.join(PROFILE, 'dialogue', 'fallback.bin'),
  SZ_TOPIC_TABLE:     path.join(PROFILE, 'dialogue', 'topics.bin'),
  SZ_GOAL_TABLE:      path.join(PROFILE, 'dialogue', 'goals.bin'),
};

if (!fs.existsSync(FORGE_HTML)){
  console.error(`forge.html not found at ${FORGE_HTML}`);
  process.exit(2);
}
const forge_src = fs.readFileSync(FORGE_HTML, 'utf-8');

function extractForgeConstant(name){
  const re = new RegExp(`const\\s+${name}\\s*=\\s*(\\d+)\\s*;`);
  const m = forge_src.match(re);
  return m ? parseInt(m[1], 10) : null;
}

let fail = 0;
console.log('--- Forge ↔ engine schema drift check ---');
for (const [constName, binPath] of Object.entries(SECTIONS)){
  const forge_val = extractForgeConstant(constName);
  if (forge_val === null){
    console.error(`FAIL: ${constName} not found in forge.html`);
    ++fail; continue;
  }
  if (!fs.existsSync(binPath)){
    console.error(`FAIL: section file missing: ${binPath} — run \`make\` first?`);
    ++fail; continue;
  }
  const actual = fs.statSync(binPath).size;
  if (forge_val !== actual){
    console.error(`FAIL: ${constName} = ${forge_val} in forge.html, ` +
                  `but ${path.basename(binPath)} on disk is ${actual} bytes ` +
                  `(diff: ${actual - forge_val} bytes)`);
    ++fail;
  } else {
    console.log(`ok:   ${constName.padEnd(20)} ${actual} bytes`);
  }
}

if (fail){
  console.error(`\nFAILED — ${fail} section(s) out of sync`);
  console.error('Fix: update the SZ_* constants in CartridgeForge/forge.html');
  console.error('     to match the engine\'s actual section sizes, AND verify');
  console.error('     the writeIdentitySection / write*Table functions emit');
  console.error('     the new fields in the correct order.');
  process.exit(1);
}
console.log('\nPASSED — Forge constants match engine struct sizes');
