# PersonaConsole — Project Documentation

A deterministic, low-resource, persistent synthetic identity runtime with
optional semantic rendering augmentation.

**This is not an AI assistant.** It is a runtime for characters with
persistent identity, stable beliefs, and continuity across sessions —
which can optionally be rendered through any language model as a voice,
without that model ever becoming the character.

---

## Table of Contents

1. [Core Thesis](#core-thesis)
2. [Architecture Overview](#architecture-overview)
3. [The Four V4 Invariants](#the-four-v4-invariants)
4. [Subsystem Reference](#subsystem-reference)
5. [Project Components](#project-components)
6. [Revision History](#revision-history)
7. [Test Suite Catalog](#test-suite-catalog)
8. [Build & Run](#build--run)
9. [Authoring a New Character](#authoring-a-new-character)
10. [Running Against Ollama](#running-against-ollama)
11. [What is Explicitly Out of Scope](#what-is-explicitly-out-of-scope)

---

## Core Thesis

```
The LLM is not the character.
The cartridge is the character.
The runtime is the nervous system.
The model is only a renderer.
```

Every engineering decision preserves that hierarchy.

The corollary: **never allow semantic sophistication to compromise
continuity determinism.** A dumber but stable character is preferable
to a smarter but unstable one.

The project's niche is **persistent synthetic identity under
constrained compute**. Not the smartest AI. Not the most agentic. The
one that remains recognizably itself over time.

The agency boundary is now more precise: **internal agency is core;
unsupervised external agency is out of scope.** A character may want,
initiate, remember, reflect, simulate private offscreen preoccupations,
and change its relationship posture. It may not call tools, browse,
modify files, send messages, control devices, or affect the outside
world unless a future interface adds explicit user approval around that
external action. Internal life belongs in Layer 1; external tool use is
a permissioned shell around the character, not the character itself.

---

## Architecture Overview

V4 separates concerns into three strictly enforced layers:

### Layer 1 — Canonical Identity Runtime  *(authoritative)*

Lives in `core/`, `memory/`, `schema/`. Owns all state. Nothing
external may mutate it directly. Includes:

- Engine pipeline (`core/engine.c`, `plan.c`, `intent.c`, `today.c`)
- Working memory + AETHER long-term episodic store
- LSH SimHash signatures + nightly consolidation
- Theory-of-Mind UserModel embedded in Relation
- Predictive coding (surprise computation)
- Layered affect (baseline / acute spike / suppression / obsession pressure)
- Embodiment (intoxication / exhaustion / irritation_carry / fixation)
- Plan layer (rhetorical_mode × stance × cert/aggr/theat/hedge)
- **V4: Memory firewall** — every Layer 1 write declares a `MemorySource`
- **V4: Schema state** — 8-slot per-relation compressed beliefs
- **V4: Affect curves** — salience-weighted decay, hysteresis, habituation

### Layer 2 — Rendering Abstraction  *(non-authoritative)*

Lives in `render/`. Produces disposable text output. **Cannot write to
Layer 1.**

- `render_backend.h` — strict RenderBackend interface contract
- `prompt_compiler.c` — projects Layer 1 state into rigid tagged blocks
- `templates/` — the v3.2 deterministic template renderer, wrapped as a RenderBackend
- `slm/` — TinySLM backend
- `providers/` — Ollama (shipped), llama.cpp (future), cloud (future)

### Layer 3 — Interface

Lives in `bridges/`. HTTP server, FFI, stdio, web chat UI.

```
PersonaConsole_v4/
├── core/                Layer 1 canonical identity runtime
├── memory/              Layer 1 episodic + AETHER + firewall + affect curves
├── schema/              Layer 1 compressed identity interpretations
├── render/              Layer 2 non-authoritative renderers
│   ├── templates/       deterministic v3.2 path wrapped as RenderBackend
│   ├── slm/             tiny SLM backend (Ollama target)
│   └── providers/       ollama_provider.{h,c}
├── instrumentation/     observability (state_trace ring buffer)
├── bridges/             Layer 3 HTTP / FFI / stdio / web UI
├── tests/               unit + integration + replay + continuity
│   ├── replay/          V4 determinism + holography + module tests
│   └── continuity/      V4 behavioral measurement (the canonical benchmark)
├── results/             JSON reports from continuity/drift runs
├── tools/               cartridge compilers, build_lm
├── profiles/            character cartridges (was characters/)
├── data/                LM corpus sources
└── docs/                V4_ARCHITECTURE.md, PORTABILITY.md, this file
```

---

## The Four V4 Invariants

### 1. RenderBackend interface

```c
typedef struct RenderBackend {
    const char *name;
    int (*initialize)(struct RenderBackend *self);
    int (*shutdown)  (struct RenderBackend *self);
    int (*render)    (struct RenderBackend *self,
                      const RenderContext  *ctx,    /* CONST */
                      RenderResult         *out);
    void *priv;
} RenderBackend;
```

`RenderContext` is `const` — backends observe Layer 1 state, never
mutate it. Hot-swappable via `PE_RENDER_BACKEND` env var. Falls back
to `template` on any failure.

### 2. Memory firewall — `memory/memory_firewall.{h,c}`

> No generated text may directly become memory.  Ever.

Every Layer 1 write declares a `MemorySource`:
```
PE_SRC_USER_INPUT       allowed
PE_SRC_SYSTEM_TICK      allowed (decay only, not creation)
PE_SRC_CARTRIDGE_LOAD   allowed (seed only)
PE_SRC_RENDERER_OUTPUT  ALWAYS DENIED
```

**Empirically verified.** `tests/continuity/hallucination_firewall_test.js`
spawns a mock Ollama that injects fabricated wives, sons, places, and
dates into every reply. 0/8 saved state files contain any forbidden
token. Schema responds only to USER classification, not renderer text.

### 3. Schema state — `schema/schema_state.{h,c}`

Eight belief slots per relation, each (−1000…+1000) strength + evidence
count:

```
USER_TRUSTWORTHY  USER_HOSTILE       USER_INTIMATE      USER_COMPETENT
USER_DECEPTIVE    RELATIONSHIP_OWED  RELATIONSHIP_OWES  SELF_DIGNITY
```

Updated only by symbolic events (`SCHEMA_EVT_INSULTED_US`,
`SCHEMA_EVT_BROKE_PROMISE`, etc.) routed through affect curves. Persists
to `<relations_dir>/<hash>.schema`.

### 4. Affect curves — `memory/affect_curve.{h,c}`

Nonlinear emotional persistence:

| Property | Implementation |
|---|---|
| Salience-weighted decay | `decay_factor = base_rate × (1 − sal/1000)²` |
| Hysteresis | Same-sign deltas damp at extremes; opposite-sign attenuated by current magnitude |
| Habituation | Each consecutive identical stimulus halves effective magnitude |
| Trait amplifiers | High-N → sticky hostility; high-A → sticky trust; high-C → defended dignity |

All saturating fixed-point. No floats. No allocations. Deterministic.

---

## Subsystem Reference

### Cognitive pipeline (`core/engine.c`)

```
input
  → pe_prep_input              lowercase + char bitmap + LSH input_sig
  → pe_classify_input          pattern sweep with negation window
  → schema_apply_event         V4: symbolic event into per-relation schema
  → schema_tick                V4: idle decay across all slots
  → pe_compute_surprise        actual vs. last turn's prediction
  → pe_update_user_model       EMA of speaker affect; belief_about_me
  → pe_associative_recall      VAD + LSH + salience + AETHER cold fallback
  → pe_update_drives_from_input
  → pe_compute_mood            V4: affect_hysteresis_apply (no pinball)
  → pe_update_embodiment       intoxication (flag-driven)
  → pe_update_layered_affect   baseline / spike / suppression / obsession
  → pe_update_topic_momentum
  → pe_select_goal / pe_select_intent
  → pe_build_plan              UtterancePlan: mode × stance × cert/aggr/...
  → V4: RenderBackend.render() dispatch
      ├─ template backend → defers to pe_generate_response (legacy)
      └─ slm backend      → prompt_compile() → Ollama → reply text
  → pe_generate_response       template scoring + voice rerank + mutator
  → dream prefix               first turn after ≥8 h gap
  → pe_trace_push
  → pe_predict_next_input
  → opportunistic AETHER consolidation (every 16 turns when WAL dirty)
  → V5: pe_consolidate_reflections   importance-scored top-K → SimHash
                                cluster → bitwise-majority signature
                                synthesis → reflection ring (every 8 turns)
  → persona_save               state.bin / memory.bin / chapters.bin
                                + reflections.bin + per-relation .bin
                                + .schema + AETHER
```

### Determinism

Same `today_seed` + same input sequence + same cartridge →
byte-identical state, response, AETHER WAL contents, instrumentation,
and schema slot values across replays. **Verified empirically** by
`tests/replay/replay_determinism_test.js` across 6 turns × 2 renderers.

All RNG is xorshift32 seeded from `(today_seed XOR turn_count × 2654435761u)`.
Voice rerank, mutator expansion, plan building, and AETHER promotion
are pure functions of state.

### Behavioral Holography

Given identical:
- seed
- WAL
- drives
- relation state
- elapsed time
- retrieved memories

The character must:
- hold the same grudges
- reference the same memories
- maintain the same emotional stance
- preserve the same interpersonal framing

…whether rendered through templates, a 1B SLM, an 8B model, or a
cloud model. Only **linguistic fidelity** varies. **Identity does not.**

Measured by:
- `tests/replay/replay_determinism_test.js` — byte-identical state JSON across backends
- `tests/continuity/behavioral_drift.js` — quantified per-axis drift (BDI)

### Prompt compiler output (V4 rigid format)

```
[IDENTITY]
name=Dr. Septimus Pretorius
big5=O96 C25 E81 A18 N68

[AFFECT]
mood=-299
acute_spike=-914
obsession_pressure=228
exhaustion=166

[STANCE]
user_hostile=920

[MEMORY]
recent=Expelled from the university for blasphemous research
recent=My mother's funeral, the cold music
recent=They called my work obscene. I called it Tuesday

[INTENT]
intent=reminisce
rhet=hedge
stance=dominant
cert=133 aggr=255 theat=125 hedge=109

[VOICE]
flags=no-direct-affirm abstract sardonic metaphorical self-interrupt mood-bleed callback-prone self-contradict delayed
flourish= — like cathedrals of bone, do you see
flourish= — and the lightning sings.
flourish= — an arrangement of clay.
flourish= — a tincture, a gesture.

[USER]
What do you think of me now?

[TASK]
Reply as Dr. Septimus Pretorius. One short turn, <=40 words.
Obey [AFFECT] [STANCE] [INTENT] [VOICE] as constraints.
Do NOT invent people, places, events, family, or memories not listed in [MEMORY].
Do NOT use the bracket tags in your reply.
```

Identical RenderContext → byte-identical prompt. Small instruct models
obey rigid structured constraint blocks better than natural-language
roleplay prompts. We are CONSTRAINING the model, not immersing it.

---

## Project Components

The project is four components in one repository:

### Component 1: PersonaConsole engine  *(`./`)*

Deterministic C99 cognitive runtime. <10 ms/turn target, <300 KB/character
on disk, no malloc in `process_input`. Character-agnostic — engine
binary contains zero per-character strings.

### Component 2: PersonaHost harness  *(`bridges/`)*

Wrapper exposing the engine via:
- HTTP + JSON on `:7777` with a minimal chat UI at `/`
- JSON-over-stdio for scripting and tests
- C FFI shared library (`libpersona_host.so`) for Unity / Unreal / Godot

### Component 3: CartridgeForge  *(`CartridgeForge/forge.html`)*

Browser-based character authoring tool. Single self-contained HTML
file. No install, no account, no upload. Produces `.cart` and
`bundle.zip` files directly from the browser. Runs on:

- Chromebook
- iPad / iPhone 6+
- Android tablet
- 10-year-old PC

Surfaces authorable for every cartridge-borne data type: identity, Big
Five, voice flags, drives, banks, dialogue (templates / patterns /
goals / fallbacks), today states, core memories, deep memories from
imported chat logs, voice LM corpus.

Import parsers: ChatGPT, Claude (Anthropic export), Gemini (API +
Google Takeout), character.ai, SillyTavern, generic JSONL.

V5 authoring surface: wants, current preoccupations, resumption lines,
relationship milestones, Forge-side preflight, and `cartridge_lint`
roundtrip tests for exported carts and all gallery archetypes.

### Component 4: CartridgeInspector  *(`CartridgeInspector/cartridge_inspector.html`)*

Browser-based read-only cartridge checker. Drop a `.cart` onto the page
to inspect identity format, topic coverage, memories, voice data, and V5
proactivity hooks. It mirrors the high-value `cartridge_lint` checks
without requiring a terminal, so users can verify cartridges they receive
from someone else before installing them.

---

## Revision History

### v3.2 FIN — Architecture maturity (the agnostic engine)

| Subsystem | What it did |
|---|---|
| The Voice | 1-ply counterfactual rerank — predict speaker's response, score how well it advances the current goal |
| AETHER | Long-term episodic store: 4 bands × 256 buckets, LSM WAL, atomic rewrite |
| Story | Autobiographical chapters from memory clusters; dream-recall on session resume |
| Theory of Mind | UserModel embedded in Relation: speaker affect EMA, belief_about_me, knowledge_level |
| Predictive coding | End-of-turn prediction; surprise > 600 flips planner toward HEDGE/CONFESS |
| Layered affect | baseline_temperament, acute_spike, suppression_mask, obsession_pressure |
| Embodiment | Intoxication (flag-driven), exhaustion, irritation_carry, fixation lock |
| Plasticity | LM-based register reranker; cartridge-borne synonym banks |
| Arousal-modulated decay | High-arousal memories saturate 2× slower |

Engine code became character-agnostic. All "gin"-style strings, all
hardcoded weight tables, all per-character flourishes moved into
cartridges. `grep` the engine sources for character names — they
appear nowhere.

### v3.3 — Forge feature-complete

- **6834e86** In-browser n-gram LM builder (binary-compatible with engine)
- **95a102d** Deep-memory bundle for long chat-log imports (cart + AETHER WAL → zip)
- **de279f8** Custom dialogue tables editor (templates / patterns / goals / fallbacks)
- **ab8b467** Gallery 6 → 12 archetypes, each with LM corpus + signature templates
- **baf9c3e** Import parsers: Claude / Gemini / character.ai
- **44fd095** Today-states editor — last unauthorable cartridge surface closed

All cartridge-borne data now authorable from the browser.

### v4 — Architecture hardening

V4 is **not a rewrite**. It is a continuity-preserving augmentation
layer. The deterministic engine remains canonical.

| Commit | Priority | What it added |
|---|---|---|
| `98b69d2` | scaffold | Three-layer separation + four V4 contracts (interface headers + min-viable impls); 28 module tests + 12 replay tests |
| `7be1f4b` | 1 | `engine.c` dispatches through `RenderBackend->render()` before falling through to legacy. The architectural seam every renderer plugs into. |
| `b760016` | 2 | Ollama provider — first real semantic renderer. Minimal POSIX HTTP/1.1 client. Determinism preserved (temperature=0 + per-turn seed propagation). Mock-server test (5 assertions). |
| `672f074` | 3 | Schema events wired into live classification. `pe_classify_input → input_class → SchemaEvent → schema_apply_event`. Persists to `<hash>.schema`. Surfaces in `/state` JSON. 5 integration assertions. |
| `b5d8519` | 4 | Linear affect logic replaced with nonlinear curves. Drives route through `affect_decay` (salience-weighted). Mood routes through `affect_hysteresis_apply` (no pinball). |
| `60045ea` | 5 | Instrumentation foundation: `state_trace` ring buffer, 12 event types, 10 module checks. Engine hot paths wired (schema events, render dispatch, render fallback). |
| `9b08cb9` | benchmark | Canonical continuity benchmark. 16-turn arc, 5 axes (emotional_persistence / schema_stability / relational_continuity / autobiographical_consistency / renderer_invariance). |
| `64fc423` | A+B+C | Rigid prompt format `[IDENTITY] [AFFECT] [STANCE] ...`. Behavioral Drift Index (7 axes). Hallucination firewall test (operational proof). |

### v5 — Reflective memory consolidation (Park et al. 2023)

Adapted from Park, J. S., O'Brien, J. C., Cai, C. J., Morris, M. R.,
Liang, P., & Bernstein, M. S., *Generative Agents: Interactive
Simulacra of Human Behavior*, UIST 2023 (§3.2 Reflection / §3.3
Retrieval).  Park et al. generate reflections via an LLM ("what
high-level abstraction summarises these recent observations?"); we
replace the LLM step with deterministic primitives we already ship,
preserving the V4 invariants:

- **Importance scoring** (`pe_memory_importance`): salience × 0.40 +
  |valence| × 0.25 + arousal × 0.20 + recency × 0.15.
- **Clustering**: top-K candidates grouped by SimHash Hamming distance
  ≤ 16 (Park's "cosine similarity ≥ threshold", swapped for our LSH).
- **Synthesis**: bitwise-majority consensus signature across cluster
  members (existing AETHER primitive in `consolidate.c`).  Salience
  boosted 1.3× — reflections retrieve preferentially.
- **Surface text**: deterministic template pool keyed by topic class
  (`core/baseline_reflections.c`).  Future cartridge format bump will
  let cartridges override with character-specific reflection templates.

Cost: amortised < 1 µs per turn; ~50 µs on the consolidation turn
(every 8 turns).  Disk: 2 KB per character (`reflections.bin`).

| File | Role |
|---|---|
| `memory/reflection.{h,c}` | scoring + clustering + synthesis + retrieval + persistence |
| `core/baseline_reflections.c` | template pool (GENERIC / CHARGED / INTIMATE / OBSESSION × 4-6 variants) |
| `core/persona.h` | reflection ring embedded on Engine (single layout authority) |
| `tests/continuity/reflection_test.js` | 11 assertions — synthesis, abstraction, determinism, persistence |

The reflection mechanism is what Park et al. demonstrated produces the
"lived-in" quality their agents exhibited.  Sample output after 25
turns about creation in the Pretorius cartridge:

> *"Always the work returns to the conversation.  I begin to think you sense its weight."*

That is a pattern observation — distinct from event recall.

### v5.1 — Recall-Coupled Plasticity (2024-2026 synthesis)

A unification of four converging threads in 2024-2026 memory
neuroscience that all point at the same paradigm shift: **retrieval
is a write event, not a read**.  Every time a memory is recalled it
is reconstructed through the current schema, briefly destabilised so
current affect can rewrite it, strengthened against future decay, and
its temporal neighbours are linked tighter through offline replay.

| Thread | Source | What RCP does |
|---|---|---|
| Reconsolidation | Nader & Hardt 2009; Schiller/Phelps lab follow-ups | Each recalled memory's emotion EMA-blends 1/20 toward current event affect |
| Testing effect | Roediger & Karpicke 2006; replicated through 2026 | `decay_counter` reset on access (already in `pe_associative_recall`) — RCP completes the loop |
| Schema-biased survival | Bartlett 1932; Gilboa & Marlatte modern reviews | Memories whose valence sign matches the dominant live schema get +1 salience |
| Replay coupling | Buzsáki SWR; Pfeiffer 2024 review | Adjacent pairs in the active recall set blend one differing LSH bit toward each other |

Park et al. (UIST 2023) gave us *offline* abstraction (reflection).
RCP gives us *online* plasticity — what happens to the raw episodic
substrate every time we touch it.  The two compose: RCP shapes what
reflection eventually sees.

Cost: ≤ 4 µs/turn at `active_count = PE_ACTIVE_MAX`.  Determinism
preserved.  Core memories (foundational autobiography) are explicitly
immovable — the same firewall principle V4 established for the
renderer applies here for retrieval.

Observable: warm prefix vs hostile prefix on the same 11-turn recall
sequence yields a 327-point schema delta — the same memories cool or
sour by reconsolidation depending on the affective context of recall.

| File | Role |
|---|---|
| `memory/recall_plasticity.{h,c}` | one function, one hook in engine.c after `pe_associative_recall` |
| `tests/continuity/recall_plasticity_test.js` | 8 assertions — determinism, cooling, reflections-still-form |

### v5.2 — Proactive presence and offscreen autonomy

V5.2 extends the "character feels alive" work from response quality into
presence. The character is no longer only a reply machine for the current user.
It can ask questions to break one-way monologue rhythm, carry unresolved
threads forward, and create deterministic internal self-events after long
absences. These self-events are deliberately non-agentic: they do not call
outside services, manipulate the world, or learn new identity. They update the
character's memory and return posture so the user feels an ongoing inner life.
Long gaps also age unsatisfied character wants, exposed in `/state` as
`want_ages`, so tools and tests can see private-life pressure building.

The host also exposes a relationship roster hook. Each interlocutor already has
separate relation files; the new endpoint makes that surface inspectable for
future multi-user and multi-character scenes.

| File | Role |
|---|---|
| `core/engine.c` | offscreen autonomy tick, return-line composition, question rhythm |
| `bridges/persona_ffi.{h,c}` | `ps_relationships` roster inspection |
| `bridges/persona_host.c` | stdio `relationships` method |
| `tests/continuity/offscreen_autonomy_test.js` | verifies absence can produce authored private-life activity |
| `tests/continuity/relationships_roster_test.js` | verifies distinct persisted interlocutor relationships |

---

## Test Suite Catalog

20+ test groups, all green in the current V5 workspace:

### Layer 1 — engine correctness (v3.x legacy, preserved through V4)

| Test | Assertions | Coverage |
|---|---:|---|
| `make lsh_test` | 12 | SimHash signatures + hamming distance + consolidation |
| `make plasticity_run` | 14 | LM scoring + paraphrase preservation + mutator banks |
| `make aether_run` | 19 | AETHER standalone (open/put/query/consolidate/1k-scale) |
| `make aether_pe_run` | 12 | AETHER↔engine integration (demotion on eviction, cold scratch) |
| `make voice_run` | 12 | Counterfactual rerank: goal alignment + hostile damping + determinism |
| `make host_run` | 12 | HTTP + FFI surface (ps_open/reply/state/save/load) |

### V4 — architecture hardening

| Test | Assertions | Coverage |
|---|---:|---|
| `make v4_modules_run` | 28 | Firewall verdicts + schema events + affect curves + registry + prompt compiler |
| `make v4_replay_run` | 12 | Determinism + cross-backend holography (6 turns × 2 backends) |
| `make v4_ollama_run` | 5 | Ollama provider dispatch + seed determinism (mock server) |
| `make v4_schema_run` | 5 | Schema integration: insult accumulates with diminishing deltas |
| `make v4_instr_run` | 10 | state_trace ring + JSON dump + eviction semantics |
| `make v4_continuity_run` | 5-axis | Canonical continuity benchmark, pass threshold 60/100 |
| `make v4_drift_run` | 7-axis | Behavioral Drift Index, pass threshold < 30 |
| `make v4_firewall_run` | 3 | Hallucination firewall under adversarial SLM injection |
| `make v4_reflection_run` | 11 | Reflective consolidation: synthesis + abstraction + determinism + persistence |
| `make v4_recall_plasticity_run` | 8 | RCP: determinism + reconsolidation cooling + reflection compatibility |
| `make v5_conversation_rhythm_run` | 1+ | Long monologue rhythm eventually yields a character question |
| `make v5_offscreen_autonomy_run` | 2 | Long absence produces deterministic private-life activity |
| `make v5_relationships_roster_run` | 5 | Multiple interlocutors persist as distinct relationships |
| `make v5_offscreen_memory_render_run` | 2 | Offscreen memory markers do not leak into dialogue |
| `make v5_milestone_catchup_run` | 2 | Missed relationship milestones surface on later returns |
| `make v5_legacy_identity_migration_run` | 2 | V4-sized identity sections load safely in V5 |
| `make v4_adversarial_arcs` | 2 arcs | Betrayal/grudge and long-absence recall trajectories |
| `make lint_test` | 2 carts | Static authoring lint for shipped showcase cartridges |

### Forge — end-to-end browser-to-engine

| Test | Coverage |
|---|---|
| `make forge_cart_test` | Forge JS → .cart → persona_host roundtrip |
| `make forge_bundle_test` | Forge bundle.zip (cart + AETHER WAL) → deep-memory cold-fallback |
| `make forge_dialogue_test` | Custom dialogue pack overrides + custom today-states |
| `make forge_archetypes_test` | All 12 gallery archetypes build, lint, and reply |
| `make forge_parsers_test` | 21 parser assertions (ChatGPT/Claude/Gemini/character.ai/SillyTavern) |

### Inspector — cartridge verification

| Test | Coverage |
|---|---|
| `make inspector_test` | Inspector JS parses V5, V4 identity, and corrupt carts |

### Aggregate

```
make v4_all_tests       # runs all V4-specific tests
make test kiki_test     # v3.x sanity REPL scripts
```

---

## Build & Run

```bash
cd PersonaConsole_v5
make                              # builds all libs, tools, profiles, cartridges
make host                         # builds persona_host + libpersona_host.so

# Verify the suite
make v4_all_tests
make lsh_test plasticity_run aether_run aether_pe_run voice_run host_run

# Talk to a character via terminal
./build/repl profiles/pretorius/pretorius.cart user

# Or via HTTP + web chat UI
./build/persona_host profiles/pretorius/pretorius.cart
# open http://localhost:7777
```

**Hardware budget:** typical 265 KB/character on disk, <100 MB RAM,
<10 ms/turn on P3-class hardware. AETHER cold storage scales
separately (4 × 256 bucket files); RAM cost is one bucket-file mmap
per band per query, paged on demand.

**Modern low-resource standard:** the original P3-era budget remains a useful
minimal target, but new production cartridges are now authored against larger
struct limits so characters can feel less repetitive without requiring an LLM:
1024 templates, 256 patterns, 256-character templates, and 20 core seed
memories. This keeps runtime RAM well under the existing <100 MB goal for
ordinary cartridges while increasing fully-authored dialogue tables from
"hundreds of KB" toward "low MB" scale.

---

## Authoring a New Character

For cartridge design standards, coverage targets, phrase-bank strategy,
repetition-control practices, and acceptance tests, see
[`docs/CHARACTER_AUTHORING_GUIDE.md`](CHARACTER_AUTHORING_GUIDE.md).

The expected path is the Forge:

0. For a non-technical entry point, open `START_HERE.html`.
1. Open `CartridgeForge/forge.html` in any browser.
2. Pick an archetype OR import a chat log to seed identity.
3. Edit identity / voice / V5 internal life / drives / memories / banks / dialogue tables / today states.
4. Paste training text in the LM tab, click **Build LM**.
5. Use the Export preflight panel to fix hard errors.
6. Click **Download bundle.zip** (or `.cart` only).
7. Optionally open `CartridgeInspector/cartridge_inspector.html` and drop the cart there.
8. Drop the bundle next to `profiles/<your_char>/` and run `./build/persona_host my_character.cart`.

The Forge handles binary compilation in-browser. No C tools, no
compile step, no terminal. The output is byte-identical to what
`compile_cartridge` would produce.

For programmatic authoring, see `tools/compile_pretorius.c` as a
template.

Before publishing a cartridge, run the static authoring lint:

```bash
make lint CART=profiles/pretorius/pretorius.cart
make lint_test
```

`tools/cartridge_lint.c` reads only authored cartridge/profile sections. It
does not open the runtime engine and does not create `state.bin`, `relations/`,
or `aether/`. It catches dangling topics, missing V5 proactivity hooks, unknown
template slots, missing voice data, and other authoring problems that compile
but make a character feel inert.

---

## Running Against Ollama

V4 ships with an Ollama provider — the first real semantic renderer.
Determinism preserved (temperature=0 + per-turn seed propagation).
Falls back to template on any failure (network error, model not
found, timeout) with zero identity drift.

```bash
# Make sure Ollama is running and has a small model pulled:
ollama serve &
ollama pull gemma2:2b

# Run persona_host with the SLM backend:
PE_RENDER_BACKEND=slm \
PE_SLM_PROVIDER=ollama \
PE_OLLAMA_MODEL=gemma2:2b \
  ./build/persona_host profiles/pretorius/pretorius.cart
# open http://localhost:7777
```

All env vars supported:

| Var | Default | Purpose |
|---|---|---|
| `PE_RENDER_BACKEND` | `template` | `template` or `slm` |
| `PE_SLM_PROVIDER` | (unset) | `ollama` (only provider shipped) |
| `PE_OLLAMA_HOST` | `127.0.0.1` | Ollama host |
| `PE_OLLAMA_PORT` | `11434` | Ollama port |
| `PE_OLLAMA_MODEL` | `gemma2:2b` | model tag |
| `PE_OLLAMA_TIMEOUT_MS` | `8000` | request timeout |
| `PE_OLLAMA_TEMP` | `0` | per-mille temperature (0 = greedy) |
| `PE_OLLAMA_NUM_PRED` | `160` | max tokens per response |
| `PE_TRACE_ENABLE` | `0` | enable instrumentation ring |
| `PE_TRACE_FILE` | (unset) | mirror trace to file |
| `PE_TRACE_JSON` | `0` | mirror trace as JSON lines to stderr |

### Measuring Identity Stability Across Models

```bash
# Produce reports per model:
PE_RENDER_BACKEND=slm PE_SLM_PROVIDER=ollama PE_OLLAMA_MODEL=gemma2:2b \
  node tests/continuity/behavioral_drift.js --slm \
    --json results/drift_gemma2-2b.json

PE_RENDER_BACKEND=slm PE_SLM_PROVIDER=ollama PE_OLLAMA_MODEL=qwen2.5:1.5b-instruct \
  node tests/continuity/behavioral_drift.js --slm \
    --json results/drift_qwen2.5-1.5b.json

PE_RENDER_BACKEND=slm PE_SLM_PROVIDER=ollama PE_OLLAMA_MODEL=smollm2:1.7b \
  node tests/continuity/behavioral_drift.js --slm \
    --json results/drift_smollm2-1.7b.json

# Aggregate into a comparison table:
make v4_aggregate
# or directly:
node tests/continuity/aggregate_results.js > docs/MODEL_COMPARISON.md
```

The aggregator produces a markdown table comparing BDI per axis across
every model that's been run. Drop reports into `results/` and run
again — the comparison grows automatically.

Recommended target models (small, instruction-following, CPU-viable):

- **gemma2:2b** — Google's 2B instruct model
- **qwen2.5:1.5b-instruct** — Alibaba's compact instruct
- **smollm2:1.7b** — HuggingFace's small instruct
- **phi3.5:3.8b-mini-instruct** — Microsoft's mini class
- **tinyllama:1.1b** — for sub-2 GB RAM systems

Avoid large contexts initially. Force compression pressure so the
runtime state stays authoritative instead of the model silently
becoming long-term memory.

---

## What is Explicitly Out of Scope

The project does not pursue:

- Autonomous agents
- Internet-connected assistants
- Tool-using copilots
- Generalized AGI
- Maximum-intelligence rendering

Those domains are overcrowded and structurally unstable. V4's niche
is **persistent synthetic identity under constrained compute** —
unique, technically defensible, philosophically coherent.

The deterministic engine is the canonical implementation. SLM and
cloud paths are *augmentations.* The architecture is designed so the
engine retains authority regardless of how much fluency the renderer
adds on top.

---

## Strategic Direction

> Most AI projects solve fluency first and discover later that they
> have no coherent identity substrate underneath.  This project
> solved substrate first.  That was harder.
>
> The next phase is where the project becomes publicly persuasive —
> not academically, *experientially*.  Once RenderBackend dispatch is
> live, a local SLM is wired in, schema events feed live interaction,
> and affect curves drive real dialogue modulation, people stop
> saying "this is a chatbot" and start saying "this character
> remembers who I am."  That transition is the actual milestone.

The instrumentation, drift index, firewall test, and continuity
benchmark together make V4 the first version of the project where
those experiential claims become testable rather than asserted.

The continuity benchmark may eventually matter more than the runtime
itself, because almost nobody is systematically measuring whether
synthetic entities remain recognizably themselves over time. That is
the project's territory.
