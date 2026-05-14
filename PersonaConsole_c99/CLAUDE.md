# Persona Console — CLAUDE.md

Deterministic synthetic-personality runtime per PE-SPEC-001.
Single-threaded C99, no heap allocation in `process_input`, all math
saturating fixed-point. Target: < 10 ms / turn, < 300 KB / character on
P3-class hardware.

**This folder is the character-agnostic engine** ("the console"). Pretorius
is one cartridge — the data lives in `characters/pretorius/*.bin`. The
engine code in `src/` and `include/` no longer contains any
character-specific keywords, drive weights, or hardcoded triggers.

Lineage: forked from `Pretorius_c99-1` (v3.1). Character-coupled behaviour
that had crept into the engine — `strstr("gin")` in embodiment, the static
`drive_mood_w[]` mood array — has been lifted into per-character data
fields (`Pattern.flags`, `DriveDef.mood_weight`). The same engine now
hosts any character whose `compile_*` tool populates these fields.

## Engine-agnostic refactor (this fork)

| Old (character-coupled)                                | New (data-driven)                                |
| ------------------------------------------------------ | ------------------------------------------------ |
| `strstr(eng->lowered, "gin")` in `pe_update_embodiment`| `state.last_matched_flags & PE_PATTERN_FLAG_INTOXICANT` |
| `static int8_t drive_mood_w[]` in `pe_compute_mood`    | `eng->drives.drives[i].mood_weight`              |
| Fixed `r % PE_ADDRESS_COUNT` for pet-name picking      | Gated by `disposition >= 700 && age >= 30 d`     |
| Self-interrupt prob from intox/exhaust only            | + 30 boost when interlocutor is `PE_TAG_CONFIDANT` |
| Hedging/verbosity ignore long-term relationship        | Confidant: hedging −50, verbosity −20            |

Pattern flags so far:
- `PE_PATTERN_FLAG_INTOXICANT` (1<<0) — match raises intoxication.

Adding a new flag means: define the macro in `persona.h`, set it in the
character's `compile_*.c`, and consume it in the engine module that owns
the side-effect (no per-character if-trees).

## What changed vs Pretorius_C99 (the v1 fork sitting alongside this folder)

| Critique target | v2 response |
| --- | --- |
| **Dialogue layer too template-centric** — needs a planning stage between intent and realization | New `plan.c` module + `UtterancePlan` struct. `pe_build_plan()` runs after intent selection and before template choice. The renderer now scores templates by `rhetorical_mask` × `stance_mask` × `(certainty/aggression/theatricality)` thresholds, *not* by intent alone. |
| **Emotional model too singular** — everything compressed to mood + drives | Four new layered affect variables (`baseline_temperament`, `acute_spike`, `suppression_mask`, `obsession_pressure`) that coexist independently. `mood` is now a *display surface*; the truer state is the stack. |
| **Lacks embodiment** | New embodiment block: `intoxication`, `exhaustion`, `irritation_carry`, `physical_fragility`, `fixation_topic/strength/remaining`, `recovery_curve`. Gin keywords push intoxication; obsession topic exceeding 800 momentum locks a fixation for 6 turns. Exhaustion + paranoia trigger theatrical-collapse ellipses. |
| **Memory decay doesn't match spec (64 vs 1000 turns)** | `pe_decay_episodic` now uses saturating uint8 counter + 4-saturation throttle → ~1020-turn cadence per spec §5.4. Associative recall hits reset the counter. |
| **Pattern path leaks modern CPU assumptions** | Input lowered + 256-bit char bitmap computed once per turn (`pe_prep_input`). Pattern sweep skips any pattern whose `first_char` isn't in the bitmap. `kw_len` precomputed at compile time. No `strlen` inside the sweep. |
| **Semantic interpretation too shallow — "not angry" ≈ "angry"** | `pe_classify_input` now scans a 24-byte look-back window before each matched keyword for ~20 negation cues (`not`, `n't`, `never`, `cannot`, etc.) and flips valence/dominance, halves arousal, demotes class. Verified: "you are a monster" → mood -171, "you are not a monster" → mood +92. |
| **Needs instrumentation** | `TraceEntry` ring buffer (64 turns) recording mood/affect/intox/exhaust/irritation/obsession/goal/intent/mode/topic/fixation/class/negation each turn. New API: `persona_trace_dump()` (ASCII sparklines + intent-transition timeline) and `persona_plan_dump()` (full last-built UtterancePlan). REPL: `:trace`, `:plan`. |

## Layout (deltas from v1)

```
Pretorius_c99-1/
  include/persona.h                 + UtterancePlan, embodiment + affect
                                      fields on NPCState, TraceEntry ring,
                                      Template.rhetorical_mask/stance_mask
                                      + thresholds, Pattern.kw_len/first_char
  include/persona_internal.h        + pe_build_plan, pe_update_embodiment,
                                      pe_update_layered_affect, pe_trace_push,
                                      pe_prep_input, pe_bm_*
  src/
    plan.c                          NEW — rhetorical planner
    engine.c                        + embodiment, affect, trace, init defaults,
                                      richer debug_dump, trace_dump, plan_dump
    dialogue.c                      candidate scoring + transforms now driven
                                      by UtterancePlan (rhet/stance mask +
                                      verbosity/hedging/aggression/theatricality
                                      thresholds)
    topic_goal.c                    pe_classify_input now uses cached lowered
                                      input + char bitmap + negation-aware
                                      lookback; pe_prep_input added
    memory.c                        spec-correct decay (~1020 turns); recall
                                      hit resets decay_counter
  tools/compile_pretorius.c         pattern table fills kw_len/first_char;
                                      new T_addv2 helper for rhet/stance-tagged
                                      templates; 3 new today-states
                                      (manic_fixation, drunk_brilliant,
                                      fragile_theatrical)
  test/repl.c                       + :trace, :plan
```

## Plan layer (most important new concept)

The pipeline is now:

```
input
  -> pe_prep_input   (lower, bitmap, negation)
  -> pe_classify_input + pe_associative_recall
  -> pe_update_drives_from_input -> pe_compute_mood
  -> pe_update_embodiment   (intox, exhaust, irrit, fixation)
  -> pe_update_layered_affect (temperament, acute_spike, suppress, obsession_pressure)
  -> pe_update_topic_momentum
  -> pe_select_goal -> pe_select_intent
  -> pe_build_plan          ******** v2 PLANNING STAGE ********
  -> pe_generate_response  (now scores templates by plan, not intent only)
  -> pe_trace_push
```

`UtterancePlan` carries:
`rhetorical_mode`, `stance`, `target_topic`, `callback_memory`,
`certainty`, `verbosity`, `aggression`, `theatricality`, `hedging`,
`negation_in_play`. Templates declare `rhetorical_mask` (bitmask of
compatible modes) and `stance_mask` plus min thresholds for the byte fields.

## REPL commands

```
:dump   full state (drives, embodiment, layered affect, plan summary)
:trace  ASCII sparklines for last 64 turns + intent timeline
:plan   the current UtterancePlan
:save   flush
:user X switch interlocutor
:delay  toggle response-delay sleep
:quit
```

## Determinism

Same `today_seed` + same input sequence → byte-identical state and output.
The trace ring is part of `NPCState` and therefore part of `state.bin`, so
`:trace` survives across sessions. The plan struct is per-turn scratch (not
serialized), but the *last* plan's salient fields are mirrored into
`NPCState` for inspection across saves.

## Semantic memory subsystem (`liblsh.a`)

Self-contained, separate from `libpersona.a` (no `persona.h` dependency).
A future memory-layer rewrite will route through it; for now it builds
and tests standalone.

- `include/lsh_memory.h` / `src/lsh_memory.c` — 64-bit **SimHash** signatures
  over byte 4-grams. Hamming distance is locality-sensitive (verified
  by paraphrase-vs-unrelated test). Integer-only, PIII-friendly.
  `lsh_find_nearest` does brute-force linear scan — 20k sigs = 160 KB,
  fits in L2. No `bsearch` (numerical sort destroys Hamming locality).
- `include/consolidate.h` / `src/consolidate.c` — nightly digestion pass:
  - `generate_rules`: co-occurrence rules from event persona-key pairs,
    sorted by support, threshold `PE_COOC_THRESHOLD=5`.
  - `build_gist_summaries`: k-means clustering over signatures with
    **vertical bitwise majority vote** centroid update (XOR is parity,
    NOT majority — that bug stays dead).
- `test/lsh_test.c` — 12 assertions. Build & run with `make lsh_test`.

## Plasticity subsystem (`libplasticity.a`)

Self-contained, built but not yet wired into `dialogue.c`. Future integration:
LM as reranker on top-K candidates, mutator applied to the chosen template.

### Alt A — character n-gram LM reranker

- `include/ngram_lm.h` / `src/ngram_lm.c` — character-level 5-gram LM with
  **stupid backoff** (Brants et al. 2007 — the post-period simplification
  we apply with hindsight; quality ~indistinguishable from Kneser-Ney for
  reranking, ~10× simpler code). All-integer scoring via integer-log table.
- `tools/build_lm.c` — offline LM builder. `make data/pretorius.lm` reads
  `data/corpus.txt` and emits a sorted-hash binary LM.
- Bootstrap corpus: ~6 KB Pretorian-register text (Shelley + Pretorius-style
  originals) → ~144 KB compiled LM. Swap in a richer corpus when authored.
- Score curve verified: Pretorian text −552, paraphrase −1719,
  modern slang −3090, gibberish −7160 milli-nats/char.

### Alt C — procedural template mutator

- `include/mutator.h` / `src/mutator.c` — splice-marker expansion engine.
- Templates may contain `[bank_name]` markers (e.g.
  `[adj_morbid] [noun_obsession]`).
- 10 hand-curated banks (~70 fragments total): adj_morbid, adj_grand,
  adj_unwholesome, adj_scientific, noun_obsession, verb_create,
  verb_destroy, exclamation, simile_anatomical, intensifier.
- Each bank has `min_theatricality` / `min_aggression` gates — below the
  gate, the bank pins to entry 0 (most neutral variant).
- Deterministic: caller supplies xorshift32 state seeded from
  `(today_seed, turn_count)`.

### Wiring (v2.1 — live in the pipeline)

The plasticity subsystem is wired into the engine:

- `persona_open` loads `pretorius.lm` (per-character override, then shared
  `data/pretorius.lm`). Missing LM is non-fatal — engine just skips rerank.
- `score_template` adds an LM "Pretorianness" bonus: per-char normalized
  log-prob centered at -1500, scaled /10. A Pretorian template (~-500/char)
  gains ~+100 score; an off-register one (~-3000) loses ~-150.
- `pe_generate_response` runs `mutator_expand` after style transforms, with
  RNG seeded from `(today_seed XOR turn_count*2654435761u)` so expansions
  are deterministic per replay. Templates without `[bank_name]` markers
  pass through unchanged.
- `persona_close` releases the LM buffer at session end.

### Hardware footprint (v2.1)

| Item                | Size    | % of 300 KB spec |
|---------------------|---------|------------------|
| character files     | 110 KB  | 37%              |
| pretorius.lm        | 144 KB  | 48%              |
| **total**           | **254 KB** | **85%**       |

Headroom: ~46 KB for v3 additions (Theory of Mind, surprise, etc.).
The `build_lm` tool accepts a `min_count` knob for future corpus expansion
— at min_count=2 the LM shrinks ~3×, trading rerank resolution for size.

### Tests

`test/plasticity_test.c` — 14 assertions covering LM scoring monotonicity,
paraphrase preservation, mutator determinism, gating, and unknown-marker
passthrough. Build & run with `make plasticity_run`.

## v3.0 — The Mind layer

Adds three cognitive primitives on top of v2.1's affect/embodiment model:

### Theory of Mind (UserModel)

Embedded in `Relation` (~14 bytes per interlocutor).  Pretorius now keeps
a running model of *each* speaker:

- `um_valence/arousal/dominance` — EMA of inferred speaker affect.
- `um_belief_about_me` — does this person think me a genius (+) or a fraud (-)?
  Updated by input class (praise +8, insult -12, threat -16, relent +4)
  with negation polarity-flip.
- `um_knowledge_level` — 0..255, lifted by long inputs with `;`/`:`/`shall`/
  `however`/`therefore`/`indeed`.
- `um_engagement` — rolling attentiveness proxy via input length.
- `um_last_intent`, `um_last_stance`, `um_interest_topic`, `um_update_count`.

Updated by `pe_update_user_model` after `pe_classify_input`.

### Predictive coding / Surprise

End-of-turn: `pe_predict_next_input` writes `predicted_input_class` and
`predicted_input_valence` to `NPCState`, based on Pretorius's just-emitted
`last_rhetorical_mode` and the UserModel (e.g. INDICT → expect class 2,
val -60; hostile interlocutor overrides toward class 2 regardless).

Start of next turn: `pe_compute_surprise` compares prediction to reality.
A class mismatch yields +500 surprise; valence delta adds `|Δval|*2`.
`surprise_last` and `prediction_error_accum` (slow EMA) are written.
Large surprise (>500) jolts `acute_spike` in the direction of the actual
input valence — Pretorius visibly recalibrates.

The planner reads both:
- `prediction_error_accum / 8` adds to `plan.hedging`.
- `surprise_last > 600` flips rhetorical_mode to HEDGE or CONFESS by valence.

### LSH wiring (semantic memory recall)

- `pe_prep_input` now computes `eng->input_sig` (SimHash of lowered input).
- `pe_commit_memory` writes `lsh_sig` on each new MemoryNode.
- `pe_associative_recall` fuses Hamming distance into the score:
  `match += (64 - hamming(input_sig, mem.sig)) * 4` (max +256, capped at 1000).
  Memories whose *text content* is semantically near the current input
  get recall preference, even across topic tags.

### Plan integration

`pe_build_plan` reads UserModel:
- `belief_about_me < -40` → stance = DEFENSIVE (unless fixation-locked).
- `knowledge_level > 180` → certainty +30 (Pretorius doesn't dumb down for peers).
- `engagement < 60 && update_count > 2` → LAMENT if mood low, else INTIMATE
  (re-engagement bid).

### Footprint

| Item              | Size      | % of 300 KB |
|-------------------|-----------|-------------|
| character files   | ~110 KB   | 37%         |
| LM                | 144 KB    | 48%         |
| **total**         | **~255 KB** | **85%**   |

v3.0 struct growth: +14 bytes Relation, +6 bytes NPCState, +8 bytes per
MemoryNode (×50 = 400 bytes).  Net character-file growth ≈ +500 bytes.

## Known intentional deviations from spec

- Pattern matcher is still a sorted keyword table — but now with first-char
  bitmap pre-filter and cached lengths, so it behaves close to a constant-
  factor-better trie for sub-100 patterns. Trie compilation is a future
  optimization; the file layout has room.
- Style transforms are still direct function chains, not bytecode. The
  *gating* is now plan-driven, which is the spec's behavioral intent.
- Crash-safety writes the (larger) state every turn — still <2 KB.

## Build & run

```
cd Pretorius_c99-1
make                              # builds libpersona.a, compile_pretorius, repl
make character                    # generates characters/pretorius/*.bin
./build/repl characters/pretorius # talk to Pretorius v2
```

`make test` runs an 8-turn deterministic sanity script that includes an
explicit negation case and ends with `:dump :plan :trace`.
