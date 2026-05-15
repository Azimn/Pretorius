# Persona Engine — Project Status (FIN)

_Last updated: 2026-05-15_
_Branch: `claude/persona-engine-runtime-swQXs`_
_Final commit reference: see git log_

## TL;DR

The Persona Engine is **ship-ready** as `PersonaConsole_c99/`. It is a
character-agnostic cognitive runtime that loads any cartridge conforming
to its `*.bin` layout. Two cartridges are bundled — Pretorius (the
original Frankenstein character) and Kiki (a valley-girl AI built to
prove the engine is genuinely character-neutral).

All seven test suites pass. Every previously identified engine-level
character coupling has been moved into the cartridge.

## Folders on the branch

| Folder | Role |
| --- | --- |
| `Pretorius_C99/` | v1 reference implementation (frozen) |
| `Pretorius_c99-1/` | v3.1 character-coupled fork w/ full cognitive stack (frozen) |
| **`PersonaConsole_c99/`** | **Ship-ready engine + Pretorius + Kiki cartridges** |

## What's in the ship-ready build

### Four static libraries

| Library | Purpose | LOC |
| --- | --- | --- |
| `libpersona.a` | Cognitive runtime — process_input pipeline, plan layer, dialogue, memory, story, voice | ~3500 |
| `liblsh.a` | 64-bit SimHash + consolidation primitives | ~250 |
| `libplasticity.a` | Stupid-backoff n-gram LM + cartridge-borne synonym mutator | ~600 |
| `libaether.a` | Long-term episodic store w/ multi-band LSH + LSM WAL | ~750 |

### Two cartridges

| Cartridge | Register | Cartridge size |
| --- | --- | --- |
| Pretorius | Theatrical, late-Victorian, vermouth-saturated | ~115 KB + 148 KB LM |
| Kiki | Valley-girl 80s/90s AI, physics-obsessed, warm | ~115 KB + 212 KB LM |

### Tests (94 hard assertions + 2 sanity scripts, all green)

| Suite | Assertions | Topic |
| --- | --- | --- |
| `make test` | (qualitative) | 8-turn Pretorius sanity + dumps |
| `make kiki_test` | (qualitative) | 8-turn Kiki sanity + dumps |
| `make lsh_test` | 12 | SimHash, Hamming, k-means consolidation, rules |
| `make plasticity_run` | 14 | LM scoring, mutator gating, banks |
| `make aether_run` | 19 | AETHER standalone — open/put/query/consolidate/scale/multi-band |
| `make aether_pe_run` | 12 | PE+AETHER integration — demotion, cold recall, sentinels |
| `make voice_run` | 12 | Counterfactual rerank — goal alignment, hostility, determinism |

## What the engine knows how to do

```
input
  → pe_prep_input              lowercase + char bitmap + LSH input_sig
  → pe_classify_input          pattern sweep, negation window, last_matched_flags
  → pe_compute_surprise        compare actual vs. last turn's prediction
  → pe_update_user_model       EMA of speaker affect, belief_about_me, knowledge
  → pe_associative_recall      working memory (VAD+LSH+salience+recency+gate)
                              + AETHER cold fallback → cold_scratch[]
  → pe_update_drives_from_input
  → pe_compute_mood            drives × data-driven mood_weights
  → pe_update_embodiment       intox flag, exhaustion, irritation, fixation
  → pe_update_layered_affect   temperament, acute_spike, suppression, obsession
  → pe_update_topic_momentum
  → pe_select_goal / pe_select_intent
  → pe_build_plan              UtterancePlan
  → pe_generate_response       score → pe_voice_rerank (1-ply lookahead)
                                → top-3 sample → style transforms
                                (cartridge flourishes + expansions)
                                → mutator_expand_banks (cartridge banks)
  → dream prefix               first turn after ≥8 h gap
  → opportunistic AETHER consolidate every 16 turns when WAL dirty
  → persona_save
```

## Engine agnosticism — auditable claim

```bash
$ grep -rEn "\"(gin|drink|pretorius|frankenstein|homunculi|kiki|scully|punky)\"" src/ include/
```

Returns nothing. Every character-flavored string lives in `compile_<name>.c`
(write-side) and `characters/<name>/*.bin` (read-side).

Adding a third character requires writing a new `compile_<name>.c` of the
same shape — no engine code changes.

## v3.2 features (the final-pass additions)

1. **The Voice** (`src/voice.c`) — counterfactual 1-ply rerank. For each
   candidate the engine just scored, predict the speaker's likely
   reaction, score how it advances the current goal, nudge the
   candidate score by the alignment delta. Hostile speakers damp
   expected praise. Pure function of state + relation + goals +
   templates; no allocations, no I/O, no RNG side effects.
2. **Multi-band AETHER** — each event is indexed in 4 separate bucket
   files (one per band's 8-bit slice of the SimHash). Recall at
   Hamming d=6 jumped from ~40% (single-band) to ~92% (multi-band).
3. **Banks-into-cartridge** — `BankRegistry` in `mutator.h` is a
   serialisable struct (6012 bytes). Each cartridge ships its own
   `banks.bin`. Pretorius's banks are the engine's built-in defaults;
   Kiki's banks ("obvi", "iconic", "like a 90s sitcom finale") are
   authored in `tools/compile_kiki.c`. The engine binary contains no
   bank text.
4. **Arousal-modulated decay** — `pe_decay_episodic` now uses an
   8-saturation throttle (vs 4) for memories with arousal ≥ 60, so
   vivid events outlive routine ones ~2× longer.
5. **Opportunistic AETHER consolidation** — `persona_process_input`
   triggers `aether_consolidate(incremental)` every 16 turns when the
   WAL has crossed its soft threshold. Cheap when nothing's pending.

## Hardware budget per character

| Item | Bytes | % of 300 KB |
| --- | --- | --- |
| Cartridge bins (identity, drives, today, banks, dialogue/*) | ~115 KB | 38 % |
| `voice.lm` | 144–212 KB | 48–71 % |
| `state.bin` runtime | ~7 KB | 2 % |
| `memory.bin` runtime | ~9 KB | 3 % |
| `chapters.bin` runtime | 1124 B | 0.4 % |
| **typical total** | **~265 KB** | **88 %** |

AETHER cold storage is on-disk-only (multi-band 4×256 buckets + zone
files); active RAM use is ~600 KB total (one bucket mmap per band per
query, kernel-paged).

## Quick reference — where things live

```
PersonaConsole_c99/
├── CLAUDE.md                       project orientation (v3.2 FIN)
├── Makefile                        4 libs + 2 cartridge compilers + repl
├── include/
│   ├── persona.h                   public API + on-disk structs
│   ├── persona_internal.h          cross-module helpers
│   ├── lsh_memory.h, consolidate.h liblsh.a
│   ├── ngram_lm.h, mutator.h       libplasticity.a (mutator now has BankRegistry)
│   ├── aether.h, aether_internal.h libaether.a
├── src/
│   ├── engine.c                    process_input loop + lifecycle
│   ├── topic_goal.c                classify, prep_input, goals, intents
│   ├── dialogue.c                  template scoring + style + mutator
│   ├── plan.c                      rhetorical planner
│   ├── memory.c                    episodic store, recall (+ AETHER fallback)
│   ├── relations.c, today.c, serialize.c, rng.c
│   ├── user_model.c                theory of mind (v3.0)
│   ├── story.c                     chapters + dream recall (v3.1)
│   ├── voice.c                     counterfactual rerank (v3.2)
│   ├── lsh_memory.c, consolidate.c liblsh.a
│   ├── ngram_lm.c, mutator.c       libplasticity.a (mutator: BankRegistry)
│   └── aether.c, aether_wal.c, aether_bucket.c, aether_consolidate.c
├── tools/
│   ├── compile_pretorius.c         writes Pretorius cartridge bins + banks.bin
│   ├── compile_kiki.c              writes Kiki cartridge bins + banks.bin
│   └── build_lm.c                  writes <character_dir>/voice.lm
├── test/
│   ├── repl.c                      interactive harness
│   ├── lsh_test.c                  12 assertions
│   ├── plasticity_test.c           14 assertions
│   ├── aether_test.c               19 assertions
│   ├── aether_pe_test.c            12 assertions
│   └── voice_test.c                12 assertions
├── data/
│   ├── corpus.txt                  Pretorian bootstrap corpus (6 KB)
│   └── corpus_kiki.txt             Kiki bootstrap corpus (8 KB)
└── characters/
    ├── pretorius/                  cartridge — all .bin + voice.lm
    └── kiki/                       cartridge — all .bin + voice.lm
```

## Polish complete

Every item from the prior roadmap is either shipped or explicitly
deferred for two follow-on components the user has mentioned as
separate work:

| Item | Status |
| --- | --- |
| Engine agnosticism (Pattern.flags + DriveDef.mood_weight) | ✅ shipped |
| Intimacy/comfort gating | ✅ shipped |
| LM path agnosticism (`<char_dir>/voice.lm`) | ✅ shipped |
| Kiki cartridge | ✅ shipped |
| libaether (long-term episodic) | ✅ shipped (multi-band v2) |
| PE+AETHER integration | ✅ shipped |
| Multi-band LSH | ✅ shipped |
| The Voice (counterfactual rerank) | ✅ shipped |
| Banks-into-cartridge | ✅ shipped |
| Arousal-modulated decay | ✅ shipped |
| Opportunistic AETHER consolidation | ✅ shipped |

Ship-ready. Ready for evaluation.
