# Migrating from V5 to V6

V5 and V6 are sibling products, not successive releases. V5 is the
deterministic local character runtime and the demo vehicle for first
external testers; its scope is frozen at the Phase 2 head. V6 is the
bounded-continuity engine and is where new psychological substrate
(speech ledger, multi-dim relation, attention, typed dissonance,
self-image guardian) develops.

This document describes the boundary: what carries forward unchanged,
what is additive, what is net-new in V6, and how a cartridge author
moves a character from V5 to V6 without rebuilding it from scratch.

---

## Compatibility summary

| Layer | V5 cartridge in V6 engine | V6 cartridge in V5 engine |
| --- | --- | --- |
| `identity.bin` | Loads with defaults for new fields | Refused / silently ignored if version exceeds V5's max |
| `drives.bin`, `topics.bin`, `patterns.bin`, `templates.bin`, `goals.bin`, `today.bin`, `banks.bin`, `voice.lm` | Identical | Identical |
| `reflections.bin` | Identical | Identical |
| `actor_index.bin` (Phase 2 sidecar) | Identical | V5 ignores if absent; V6 loads or initializes |
| V6 new sidecars: `speech_events.bin`, `relation_dims.bin`, `open_loops.bin`, `habit_rules.bin`, `contradictions.bin`, `long_arc.bin` | V6 loads or initializes; V5 cartridges have none and that is fine | V5 ignores them entirely |

**Net effect:** a V5 cartridge loads in V6 and behaves exactly as a V5
character that simply hasn't built up V6 state yet. A V6 cartridge does
not regress when read by V5 — V5 does not look for the new sidecars and
the existing sections it does read have unchanged layout.

---

## What is identical across V5 and V6

The following are the same byte-for-byte and require no migration:

- **The V4 invariants.** Cartridge-is-the-character, memory firewall,
  behavioral holography, continuity beats sophistication.
- **The cartridge binary ABI for legacy sections.** Identity (with the
  same field offsets), drives, topics, patterns, templates, goals,
  today entries, voice flags, obsessions, taboos, wants, milestones,
  resumption lines, formative memory seeds.
- **Phase 1 substrate.** Engine clock interface, `pe_sidecar_header_t`,
  the five-tuple determinism contract, the renderer audit categories
  for length / illegal tags / repetition.
- **Phase 2 substrate.** Actor-tagged memory: `actor_index.bin` sidecar
  shipped on both V5 and V6, used identically.
- **Runtime tooling.** `compile_cartridge`, `compile_pretorius`,
  `compile_kiki`, `build_lm`, `cartridge_lint`, `cartridge_stats`,
  `persona_host`, the web UI bridge.
- **Test gates.** `runtime_gate` / `v5_gate` aliases; all V5 tests carry
  over and remain green in V6.
- **Practice.** `DEV_WORKFLOW.md` (the practice half) is shared; only
  the doctrine half differs (V5 keeps its merged document, V6 has the
  larger `V6_DOCTRINE.md` plus a slim practice file).

A character that ships in the V5 demo will run identically in V6 with
the existing engine behavior, growing into V6's new layers only as new
turns commit speech events, relation dimensions update, and so on.

---

## What is additive (V6 builds on top, no break)

V6 introduces several optional persistent layers, each as a versioned
sidecar that V5 simply does not look for. Authors do not have to
migrate. The engine accumulates the layer as the character is used:

- **`speech_events.bin`** — engine-authored speech ledger. Created on
  first commit under V6, never required on disk. Phase 3.
- **`relation_dims.bin`** (per actor) — multi-dimensional relational
  profile. Defaults fill from `disposition` if the V5 cartridge has no
  V6-authored seeds. Phase 4.
- **`open_loops.bin`** — carried intentions. Empty on first V6 run.
  Phase 6.
- **`habit_rules.bin`** — procedural bias learned from repeated speech
  events. Empty on first V6 run. Phase 6.
- **`contradictions.bin`** — contradiction ledger. Empty on first V6
  run. Phase 6.
- **`long_arc.bin`** — slow-moving counters (wounds, growth, shame
  load, trust injury, ...). Defaults from cartridge baselines if
  authored, otherwise zero. Phase 6.

Every new sidecar carries `pe_sidecar_header_t` and validates through
`pe_sidecar_validate` (magic, version, too-old, too-new, overflow). The
`F_OPTIONAL` flag means a missing or corrupt sidecar is non-fatal —
the engine resets to the safe init state and continues. Required
cartridge sections still hard-fail when corrupted.

---

## What is net-new in V6 cartridge authoring

V6 cartridges *may* declare new identity fields that the engine reads to
shape its V6 layers. They are all optional; absent fields fall back to
defaults derived from V5 fields or zero.

| Field | Type | Purpose |
| --- | --- | --- |
| `ideal_self` | short text or structured tags | Drives ideal/actual dissonance (Higgins). |
| `ought_self` | short text or structured tags | Drives ought/actual dissonance (guilt, anxiety). |
| `feared_self` | short text or structured tags | Drives feared-self proximity (panic, denial, overcorrection). |
| `attachment_style` | enum: secure / anxious / avoidant / disorganized | Conditions relation dimension decay and repair speed. |
| `defense_tendencies` | bitfield over defense modes | Which defenses this character reaches for under pressure. |
| `attention_bias` | weights per attention source | Vain characters notice flattery; anxious ones notice threat. |
| `dominant_recall_mode` | enum (see V6 §16) | Baseline retrieval intent. |
| `desired_impression_default` | enum (V6 §17) | Initial outer mask before per-actor learning. |
| `archetype` | enum | Forge scaffold key for pre-filling 80% of the above. |

The Forge will gain archetype-scaffold UI in Phase 7+ so authors do not
hand-fill these.

A V5 cartridge with **none** of these fields is a valid V6 cartridge:
defaults are derived. The point of V6 authoring is opt-in depth, not
required ceremony.

---

## The migration ratchet

The `v5_legacy_identity_migration_run` test that V4/V5 used to enforce
backward compatibility is the model. V6 extends it:

- V5 cartridges (Pretorius v5, Kiki v5) must load in V6 and pass a
  determinism replay with the existing test inputs.
- V6 cartridges with the new fields must also load in V6 and pass the
  same replay (additional fields are read but do not alter the V5 test
  inputs' outcomes by default).
- Every new V6 sidecar adds its own pinned-replay test (the
  `clock_pinned_replay_test.js` and `actor_tagged_memory_test.js`
  pattern: byte-identical sidecar across runs, different sidecar when
  the clock advances).

The legacy migration test is part of `runtime_gate` on V6 and is a hard
gate failure if it regresses.

---

## Suggested transition workflow

For the lead working on the V5 demo:

1. Continue shipping V5 as the first external-tester demo. The V5 tree
   is frozen at Phase 2 head; only release-readiness fixes (Forge
   polish, `release_check.ps1`, README text) need to land there.
2. Treat V6 as an independent development branch family. The lead does
   not need to follow V6 work to ship V5.
3. When V5 ships and feedback comes in, ship a V5.x point release with
   demo polish; do not retrofit V6 features into V5.

For development of new psychological features:

1. Land on V6 only. New phases (3 onward) target V6.
2. Each new V6 module follows the doctrine: deterministic, inspectable,
   sidecar-versioned, replay-tested, character-agnostic.
3. The Forge author scaffolds (V6 §27) land alongside Phase 5/6 so V6
   cartridges remain authorable.

For cartridge authors:

1. A V5 cartridge runs in V6 today. Do nothing.
2. To opt into V6 depth, add `ideal_self` / `ought_self` / `feared_self`
   text to the cartridge manifest first — the highest-leverage three
   fields. Everything else (attachment, defense tendencies, attention
   bias) can come from the Forge archetype once that exists.
3. Speech-event memory accumulates automatically once the character
   runs in V6. The character does not have to be rebuilt to gain a
   self-ledger; it starts logging from the first V6 turn.

---

## The boundary, one paragraph

V5 ships. V6 evolves. The Phase 1 + Phase 2 substrate is shared and
preserved on both sides. V5 cartridges are valid V6 cartridges. V6
cartridges fail gracefully on V5 (V5 does not see the new sidecars).
The V6 doctrine binds V6 but does not retroactively constrain V5's
demo scope. When V6 reaches its capstone (the self-image guardian),
the lead can decide whether to fold V6 back into a single product or
ship V6 as a separate "deeper engine" alongside V5's "demo engine."
That decision is later.
