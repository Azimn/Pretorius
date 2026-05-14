# Persona Engine — Project Status

_Last updated: 2026-05-14_
_Branch: `claude/persona-engine-runtime-swQXs`_

## TL;DR

There are now **three** folders on the branch, each a checkpoint of a
different design phase. The newest, `PersonaConsole_c99/`, is the
character-agnostic "console" — Pretorius is just one cartridge in a
larger architecture that any character can load into.

| Folder | Role | Lineage |
| --- | --- | --- |
| `Pretorius_C99/` | v1 reference implementation | original |
| `Pretorius_c99-1/` | v3.1 — character-coupled, full cognitive stack | fork of v1 |
| `PersonaConsole_c99/` | character-agnostic engine + Pretorius cartridge | fork of v3.1 |

All three build clean. All three pass their tests.

---

## Architecture (cartridge / console split)

The retro-gaming analogy made flesh:

```
┌────────────────────────────────────────────────────────────────┐
│  PersonaConsole_c99/                                           │
│  ───────────────────                                           │
│                                                                │
│  src/        engine — character-agnostic C99 code              │
│  include/    persona.h + persona_internal.h                    │
│  tools/      compile_pretorius.c (one tool per cartridge)      │
│                                                                │
│        ┌──────────────────────────────────────┐                │
│        │   characters/pretorius/  ← cartridge │                │
│        │     identity.bin                     │                │
│        │     drives.bin                       │                │
│        │     today.bin                        │                │
│        │     dialogue/{patterns,templates,    │                │
│        │              fallback,goals,topics}  │                │
│        │     voice.lm        (n-gram LM)      │                │
│        └──────────────────────────────────────┘                │
└────────────────────────────────────────────────────────────────┘
```

The console knows: how to load arbitrary `*.bin` files in a character
directory, how to run the cognitive pipeline, how to score templates
against an utterance plan. It does **not** know what gin is, who
Henry is, or why Pretorius is afraid of bones. That all lives in the
cartridge.

---

## What the engine knows how to do

### 1. Cognitive pipeline (one turn)

```
input
  → pe_prep_input           lowercase + 256-bit char bitmap + LSH signature
  → pe_classify_input       pattern sweep (with negation window) + last_matched_flags
  → pe_compute_surprise     compare actual vs. last turn's prediction
  → pe_update_user_model    EMA of speaker affect, belief-about-me, knowledge
  → pe_associative_recall   topic + LSH-hamming + disclosure-gate fused score
  → pe_update_drives_from_input
  → pe_compute_mood         drives × data-driven mood_weights (no hardcoded array)
  → pe_update_embodiment    intox (flag-driven), exhaust, irrit, fixation
  → pe_update_layered_affect baseline, acute_spike, suppression, obsession_pressure
  → pe_update_topic_momentum
  → pe_select_goal / pe_select_intent
  → pe_build_plan           UtterancePlan: mode/stance/cert/verb/aggr/theat/hedge
  → pe_generate_response    template score + LM rerank + plan-driven style + mutator
  → dream prefix            (first turn after ≥8 h gap)
  → pe_trace_push
  → pe_predict_next_input
  → persona_save            atomic write of state.bin / memory.bin / chapters.bin
```

### 2. Per-character data fields (the cartridge API)

* `Identity` — Big Five, voice flags, obsessions, taboos, address slots, name, core memories
* `DriveTable` — eight drives with baselines, decay rates, personality weights, and `mood_weight` (new — replaces hardcoded array)
* `PatternTable` — keyword/topic/emotion/class/group plus `flags` (new — currently only `PE_PATTERN_FLAG_INTOXICANT`)
* `TemplateTable` — text with `{slot}` markers and `[bank]` markers, gated by rhetorical mode mask, stance mask, and certainty/aggression/theatricality thresholds
* `GoalTable`, `TopicTable`, `TodayTable`, `FallbackTable`

### 3. Engine subsystems (three libraries)

| Library | Purpose |
| --- | --- |
| `libpersona.a` | the cognitive runtime |
| `liblsh.a` | 64-bit SimHash signatures + k-means consolidation (vertical majority vote) |
| `libplasticity.a` | character n-gram LM (stupid-backoff, integer-log) + template mutator with synonym banks |

---

## What is verified working today

### Engine-agnosticism (the most recent commit, `3c3db92` + final polish)

1. `pe_update_embodiment` no longer calls `strstr("gin")` — intoxication is triggered by `state.last_matched_flags & PE_PATTERN_FLAG_INTOXICANT`. The flag is set in `pe_classify_input` from the matched `Pattern.flags`.
2. `pe_compute_mood` no longer reads a static `int8_t drive_mood_w[]` array — the weights come from `eng->drives.drives[i].mood_weight`, which is populated by `compile_pretorius.c::make_drives` from the character's defs.
3. `persona_open` no longer hardcodes `"pretorius.lm"` — it loads `<character_dir>/voice.lm`. Each cartridge owns its register.
4. The Makefile writes the LM directly to `characters/pretorius/voice.lm`, not to a global `data/` path.
5. `tools/compile_pretorius.c` populates both new fields. Adding a second character means writing `tools/compile_<name>.c` with the same shape.

### Intimacy / comfort gating (same commit)

1. `dialogue.c` `fill_slots`: pet-name addresses [2..N] are gated by `disposition ≥ 700 && (now − first_contact) ≥ 30 d`. Otherwise the random pick is capped to the formal/stranger tier [0..1]. "My love" / "darling" no longer escape on turn one.
2. `dialogue.c` `apply_style`: self-interruption probability gets a flat **+30** boost when the interlocutor is tagged `PE_TAG_CONFIDANT`. People stumble more around close friends.
3. `plan.c` `pe_build_plan`: confidant flag subtracts **50** from hedging and **20** from verbosity before clamping. Directness with trust.

### Cognitive stack (carried over from earlier work)

* **Plan layer** (v2): rhetorical mode + stance + certainty/verbosity/aggression/theatricality/hedging drive both template scoring and style transforms.
* **Embodiment** (v2): intoxication, exhaustion, irritation_carry, physical_fragility, fixation_topic with 6-turn lock.
* **Layered affect** (v2): baseline_temperament, acute_spike, suppression_mask, obsession_pressure.
* **Trace ring** (v2): 64-turn instrumentation buffer with ASCII sparklines and intent-transition timeline.
* **Plasticity** (v2.1): LM reranker (~+100 score for in-register templates, ~−150 for off-register) + mutator with 10 synonym banks (theatricality/aggression-gated).
* **Theory of Mind** (v3.0): UserModel embedded in `Relation` — speaker valence/arousal/dominance EMA, belief_about_me, knowledge_level, engagement.
* **Predictive coding** (v3.0): end-of-turn prediction → start-of-next-turn surprise; surprise > 600 flips the planner toward HEDGE/CONFESS; the slow EMA `prediction_error_accum` adds to plan.hedging.
* **LSH-fused recall** (v3.0): input SimHash, memory SimHash, Hamming distance bonus folded into associative match score (max +250).
* **Autobiographical chapters** (v3.1, carried over): up to 16 64-byte chapter records, each holding theme SimHash centroid, dominant_mood, salience_peak, and a 32-byte key phrase.
* **Self-disclosure gate** (v3.1): high-salience memories carry a `private_threshold`. Recall only surfaces them if `disposition + engagement/4 ≥ threshold × 4`.
* **Dream recall** (v3.1): if a new session opens ≥8 h after `last_update_time`, chapters are crystallized and a mood-toned dream phrase is prepended to the first response of the new session.

### Tests

| Test | Assertions | Status |
| --- | --- | --- |
| `make test` (8-turn deterministic sanity) | n/a (qualitative) | ✓ green |
| `make lsh_test` | 12 | ✓ green |
| `make plasticity_run` | 14 | ✓ green |

The 8-turn sanity includes the gin/drink utterances; the trace confirms `intox` climbs in the final turns via the new flag path. The same script includes an explicit negation case ("I am not insulting you") to exercise the negation-aware lookback in `pe_classify_input`.

---

## Hardware budget

| Item | Bytes | % of 300 KB spec |
| --- | --- | --- |
| Pretorius cartridge (`identity.bin` + 6× dialogue/* + `drives.bin` + `today.bin`) | ~110 KB | 37 % |
| `voice.lm` | 144 KB | 48 % |
| `chapters.bin` (when written) | 1124 B | 0.4 % |
| **total per character** | **~255 KB** | **85 %** |

Headroom: ~45 KB for future per-character features (banks, additional .bin files, larger templates).
The LM dominates; corpus expansion or `min_count` tightening is the lever to free space.

---

## Known intentional gaps (deliberate scope cuts)

1. **Mutator synonym banks are still hand-coded in `src/mutator.c`.** Strings like "homunculi", "the substrate" sit in the engine binary, not in the cartridge. Moving banks into per-character data files is a future refactor — the mutator API takes a `BankRegistry` rather than relying on the static one.
2. **No second character yet.** The console architecture is in place; we have not actually compiled and run a non-Pretorius cartridge. The first new character will reveal the rough edges (probably some helper that still implicitly assumes one specific character's value ranges).
3. **Semantic Vault / offline Dream Engine deferred.** The user's spec for this iteration was explicit about deferring these. `liblsh.a` and `src/story.c` are the foundations; a true offline consolidation pass that rewrites memory.bin is the next step.
4. **PE-SPEC v1 deviations preserved.** Pattern matcher is still a sorted keyword table with first-char bitmap (not a true trie). Style transforms are direct function chains (not bytecode). Both are documented in the original CLAUDE.md as intentional.

---

## Suggested next steps (in roughly increasing scope)

### A. Validation / hardening (small, high-value)

1. **Compile a second character.** Pick a different register — say a clipped, formal one (Holmes? Mr. Carson?). Write `tools/compile_<name>.c` modelled on `compile_pretorius.c`. Confirm the engine runs on it with no code changes. This is the actual proof of agnosticism.
2. **Behavioral regression tests in C.** A `test/agnostic_test.c` that loads pretorius, fires a `gin`-containing input, asserts `last_matched_flags & PE_PATTERN_FLAG_INTOXICANT` and that `intox > 0`. Cheap, catches future regressions.
3. **A long-session test for the intimacy gate.** Today's sanity test runs 8 turns in seconds — it can never accumulate 30 days of `first_contact` age. Either add a test that backdates `relation.first_contact` directly, or expose a debug command in the REPL to fast-forward time.

### B. Move banks into the cartridge (medium scope)

Make `mutator_expand` take a `BankRegistry*` argument; load it from `<character_dir>/banks.bin`. `compile_pretorius` writes Pretorian banks; other characters write their own. The engine binary stops carrying any cartridge-specific text.

### C. Second-cartridge feature work

* **Banks per character + corpus per character** — each cartridge ships its own `voice.lm`, `banks.bin`, and `corpus.txt`.
* **Cartridge-defined pattern flags** beyond `INTOXICANT` — e.g., `PE_PATTERN_FLAG_SOOTHING`, `PE_PATTERN_FLAG_NOSTALGIC`, with engine effects scoped to one obvious behavior each. Adding a new flag is mechanical.
* **A cartridge manifest** — a tiny `cartridge.bin` declaring "I require engine version X and these flags". Forward-compat shield.

### D. Cognitive deepening (continuing the original v3.x roadmap)

* **Semantic Vault** (deferred) — periodic offline pass that rewrites `memory.bin` to consolidate similar memories into single high-salience nodes, freeing slots and strengthening the strongest threads.
* **Offline Dream Engine** (deferred) — full overnight consolidation that produces chapters AND rules (via `liblsh.a::generate_rules`), then surfaces a richer dream-recall passage that quotes both the chapter and a discovered rule.
* **v3.2 — "The Voice"** (from the earlier roadmap): inner monologue / suppressed thought channel, 1-ply counterfactual lookahead, catchphrase registry with usage decay.

---

## Quick reference — where things live

```
PersonaConsole_c99/
├── CLAUDE.md                      project orientation
├── Makefile                       three libraries + tools + LM target
├── include/
│   ├── persona.h                  public API + on-disk structs
│   ├── persona_internal.h         cross-module helpers
│   ├── lsh_memory.h               SimHash signatures (libl­sh.a)
│   ├── consolidate.h              k-means + co-occurrence rules
│   ├── ngram_lm.h                 stupid-backoff LM
│   └── mutator.h                  bank-driven template expansion
├── src/
│   ├── engine.c                   process_input loop + lifecycle
│   ├── topic_goal.c               classify, prep_input, goals, intents
│   ├── dialogue.c                 score/select/transform templates
│   ├── plan.c                     rhetorical planner
│   ├── memory.c                   episodic store + LSH-fused recall
│   ├── relations.c                per-interlocutor on-disk store
│   ├── today.c                    daily mood seed
│   ├── serialize.c                atomic writes
│   ├── rng.c                      xorshift32 (deterministic)
│   ├── user_model.c               theory of mind (v3.0)
│   ├── story.c                    chapters + dream recall (v3.1)
│   ├── lsh_memory.c, consolidate.c  liblsh.a
│   └── ngram_lm.c, mutator.c        libplasticity.a
├── tools/
│   ├── compile_pretorius.c        ⇒ characters/pretorius/*.bin
│   └── build_lm.c                 ⇒ <character_dir>/voice.lm
├── test/
│   ├── repl.c                     interactive harness (`:dump :plan :trace`)
│   ├── lsh_test.c                 12 assertions
│   └── plasticity_test.c          14 assertions
├── data/
│   └── corpus.txt                 Pretorian-register bootstrap text
└── characters/
    └── pretorius/                 the one cartridge that exists today
        ├── identity.bin, drives.bin, today.bin
        ├── dialogue/{patterns,templates,fallback,goals,topics}.bin
        ├── voice.lm
        ├── state.bin              (runtime; gitignored)
        ├── memory.bin             (runtime; gitignored)
        ├── chapters.bin           (runtime; gitignored)
        └── relations/             (runtime; gitignored)
```

---

_End of status doc — pick A1 ("compile a second character") and we will know within a day whether the agnosticism is real._
