# Persona Console — CLAUDE.md (v3.2 FIN)

Deterministic synthetic-personality runtime per PE-SPEC-001.
Single-threaded C99, no heap allocation in `process_input`, all math
saturating fixed-point. Target: < 10 ms / turn, < 300 KB / character
on P3-class hardware.

**This folder is the character-agnostic engine** ("the console"). Pretorius
and Kiki are two cartridges that prove the console runs any character
whose data conforms to the cartridge layout. The engine code in `src/` and
`include/` contains zero character-specific strings or behaviors.

## Architecture (cartridge / console split)

```
┌────────────────────────────────────────────────────────────────┐
│  PersonaConsole_c99/                                           │
│  ───────────────────                                           │
│                                                                │
│  src/        engine — character-agnostic C99 code              │
│  include/    persona.h + persona_internal.h + …                │
│  tools/      compile_pretorius.c, compile_kiki.c (one per      │
│              cartridge), build_lm.c                            │
│                                                                │
│  ┌──────────────────────────────────────┐                      │
│  │ characters/pretorius/  ← cartridge   │                      │
│  │ characters/kiki/       ← cartridge   │                      │
│  │   identity.bin                       │                      │
│  │   drives.bin                         │                      │
│  │   today.bin                          │                      │
│  │   banks.bin       (v3.2 NEW)         │                      │
│  │   dialogue/{patterns,templates,      │                      │
│  │            fallback,goals,topics}    │                      │
│  │   voice.lm                           │                      │
│  └──────────────────────────────────────┘                      │
└────────────────────────────────────────────────────────────────┘
```

The console knows: how to load arbitrary `*.bin` files in a character
directory, how to run the cognitive pipeline, how to score templates
against an utterance plan, how to query long-term episodic memory.

The console does **not** know what gin is, who Henry is, what 90210 means,
or how Kiki addresses people. All character flavor lives in the cartridge.

## The cognitive pipeline

```
input
  → pe_prep_input              lowercase + char bitmap + LSH input_sig
  → pe_classify_input          pattern sweep w/ negation window; sets
                                last_matched_flags (intoxicant etc.)
  → pe_compute_surprise        compare actual vs. last turn's prediction
  → pe_update_user_model       EMA of speaker affect; belief_about_me; …
  → pe_associative_recall      working memory (VAD + LSH + salience +
                                recency + disclosure gate)
                              + AETHER cold fallback when working hits
                                below threshold → cold_scratch[]
  → pe_update_drives_from_input
  → pe_compute_mood            data-driven (drives[i].mood_weight)
  → pe_update_embodiment       intox triggered by PE_PATTERN_FLAG_INTOXICANT
  → pe_update_layered_affect   baseline / acute_spike / suppression /
                                obsession_pressure
  → pe_update_topic_momentum
  → pe_select_goal / pe_select_intent
  → pe_build_plan              UtterancePlan: mode / stance / cert /
                                verb / aggr / theat / hedge
  → pe_generate_response       template score → pe_voice_rerank (v3.2
                                counterfactual lookahead) → top-3
                                weighted sample → style transforms
                                (flourishes/expansions from cartridge) →
                                mutator_expand_banks (cartridge banks)
  → dream prefix               first turn after ≥8 h gap
  → pe_trace_push
  → pe_predict_next_input
  → opportunistic AETHER consolidation (every 16 turns when WAL dirty)
  → persona_save               state.bin / memory.bin / chapters.bin
                                + relations + AETHER
```

## Subsystems (four static libraries)

| Library | Purpose |
| --- | --- |
| `libpersona.a` | The cognitive runtime. Depends on the next three. |
| `liblsh.a` | 64-bit SimHash signatures + k-means consolidation (vertical majority vote). Used by libpersona AND libaether. |
| `libplasticity.a` | Character n-gram LM (stupid-backoff, integer-log) + template mutator. Banks are cartridge-borne; the lib only ships a default for back-compat. |
| `libaether.a` | Long-term episodic storage. Multi-band LSH (4 bands × 256 buckets = 1024 files), Write-Ahead Log, atomic bucket rewrite via `.new` + `rename()`. Mmap one bucket file per band per query; RAM under 1 MB. |

## Cartridge-borne data

Every character file is loaded from disk; the engine binary contains no
per-character strings.

| File | Type | Size | Contents |
| --- | --- | --- | --- |
| `identity.bin` | `Identity` | 1786 B | Big Five, voice flags, obsessions, taboos, address slots, name, core memory seeds, **flourishes[4][48]** + **expansions[4][48]** (v3.2 style banks) |
| `drives.bin` | `DriveTable` | 272 B | 8 drives × {baseline, decay, mood_weight, personality_weight[5], name} |
| `today.bin` | `TodayTable` | 964 B | Up to 16 daily mood-modifier states |
| `banks.bin` | `BankRegistry` | 6012 B | **v3.2 NEW** — Up to 12 synonym banks × {name, 10 entries × 48 B, thresholds} |
| `dialogue/patterns.bin` | `PatternTable` | 4484 B | Keyword → topic, emotion delta, input_class, **flags** (intoxicant etc.) |
| `dialogue/templates.bin` | `TemplateTable` | 55812 B | Text + intent + rhetorical_mask + stance_mask + thresholds |
| `dialogue/fallback.bin` | `FallbackTable` | 3460 B | 3-tier fallback lines |
| `dialogue/goals.bin` | `GoalTable` | 748 B | Drive-weight vectors per goal |
| `dialogue/topics.bin` | `TopicTable` | 2180 B | Topic graph w/ adjacency |
| `voice.lm` | n-gram LM | ~150 KB | Stupid-backoff character LM for register reranking |

## Per-turn runtime files (gitignored)

| File | Purpose |
| --- | --- |
| `state.bin` | `NPCState` — drives, mood, embodiment, affect, plan mirror, trace ring, prediction/surprise, voice instrumentation |
| `memory.bin` | `MemoryStore` — episodic ring (50 nodes), semantic blocks, short-term buffer, phrase usage |
| `chapters.bin` | `ChapterBook` — autobiographical chapters + dream state |
| `relations/<hash>.bin` | Per-interlocutor `Relation` w/ embedded UserModel |
| `aether/` | AETHER long-term episodic store (zone files + bucket files per band + WAL + dirty bitmap) |

## Determinism

Same `today_seed` + same input sequence + same cartridge → byte-identical
state, response, AETHER WAL contents, and instrumentation across replays.
All RNG is xorshift32 seeded from `(today_seed XOR turn_count*2654435761u)`.
The voice rerank, mutator expansion, plan building, and AETHER promotion
are pure functions of state.

## Engine agnosticism — what's NOT in the binary

The engine code in `src/` and `include/` contains **zero** character-flavored
strings or hardcoded keyword checks. Everything that used to be:

| v1 → v2 (engine) | v3.2 (cartridge) |
| --- | --- |
| `strstr(eng->lowered, "gin")` | `state.last_matched_flags & PE_PATTERN_FLAG_INTOXICANT` |
| `static drive_mood_w[]` | `eng->drives.drives[i].mood_weight` |
| Hardcoded `"pretorius.lm"` filename | `<character_dir>/voice.lm` |
| `"cathedrals of bone"` flourishes in apply_style | `eng->identity.flourishes[]` |
| `"as I told the priests"` expansions | `eng->identity.expansions[]` |
| `"Pretorius:"` REPL prefix | `eng->identity.character_name` |
| Synonym banks in `mutator.c` | `eng->banks` from `<character_dir>/banks.bin` |

`grep` the engine sources for the character names — they appear nowhere.

## v3.2 cognitive layers (top to bottom)

1. **The Voice** (`src/voice.c`) — 1-ply counterfactual rerank. For each
   candidate template, predict the speaker's likely response if that
   utterance were chosen, score how well the prediction advances the
   current goal, add the alignment delta to `candidate_scores[]`. Hostile
   speakers (UserModel `belief_about_me < -40`) damp expected praise.
2. **AETHER long-term episodic** (`src/aether*.c`) — multi-band LSH
   (4 bands × 256 buckets), LSM-tree WAL, atomic bucket rewrite. Working
   memory demotes evicted nodes; cold recall promotes back into per-turn
   scratch via sentinel indices.
3. **Story** (`src/story.c`) — autobiographical chapters crystallized
   from memory clusters at session start; dream-recall phrase prepended
   to the first response after ≥8 h gap.
4. **Theory of Mind** (`src/user_model.c`) — UserModel embedded in
   `Relation`: speaker affect EMA, belief-about-me, knowledge level,
   engagement.
5. **Predictive coding** — end-of-turn prediction → start-of-next-turn
   surprise; surprise > 600 flips planner toward HEDGE/CONFESS; running
   `prediction_error_accum` adds to plan.hedging.
6. **Layered affect** — baseline_temperament, acute_spike,
   suppression_mask, obsession_pressure.
7. **Embodiment** — intoxication (flag-driven), exhaustion,
   irritation_carry, fixation lock.
8. **Plan layer** — rhetorical_mode × stance × cert/aggr/theat/hedge
   drives template selection AND style transforms.
9. **Plasticity** — LM-based register reranker; cartridge-borne synonym
   banks for procedural template expansion.
10. **Arousal-modulated decay** — high-arousal episodic memories
    saturate twice as slowly in `pe_decay_episodic`.

## REPL commands

```
:dump   full state — drives, embodiment, affect, plan summary,
        user_model, prediction/surprise, voice delta + choice,
        chapters, AETHER stats
:trace  ASCII sparklines for last 64 turns + intent timeline
:plan   the current UtterancePlan
:save   flush
:user X switch interlocutor
:delay  toggle response-delay sleep
:quit
```

## Hardware budget (per character, on disk)

| Item | Bytes | % of 300 KB |
| --- | --- | --- |
| Cartridge bins (identity + drives + today + dialogue/* + **banks**) | ~115 KB | 38 % |
| `voice.lm` | 144–211 KB | 48–70 % |
| `chapters.bin` (when populated) | 1124 B | 0.4 % |
| `state.bin` runtime | ~7 KB | 2 % |
| `memory.bin` runtime | ~9 KB | 3 % |
| **typical total** | **~265 KB** | **88 %** |

AETHER cold storage scales separately on disk (4 × 256 bucket files +
zone files); RAM cost is one bucket-file mmap per band per query
(~600 KB total) which is paged on demand.

## Tests

| Test | Assertions | What it covers |
| --- | --- | --- |
| `make test` | qualitative | 8-turn Pretorius sanity w/ negation, gin, dump |
| `make kiki_test` | qualitative | 8-turn Kiki sanity w/ identity-threat probe |
| `make lsh_test` | 12 | SimHash signatures, hamming distance, consolidation, co-occurrence rules |
| `make plasticity_run` | 14 | LM scoring monotonicity, paraphrase preservation, mutator banks/gating |
| `make aether_run` | 19 | AETHER standalone: open, put, query, consolidate, multi-band paraphrase, 1k-scale, max_age |
| `make aether_pe_run` | 12 | End-to-end: demote on eviction, cold scratch, sentinel index resolution |
| `make voice_run` | 12 | Counterfactual rerank: goal alignment, hostile damping, determinism, instrumentation |

All seven green at FIN.

## Build & run

```
cd PersonaConsole_c99
make                              # builds all libs, tools, both cartridges
make test kiki_test               # 8-turn sanity for each character
make aether_run aether_pe_run     # AETHER standalone + integration
make voice_run                    # The Voice
make lsh_test plasticity_run      # subsystem tests
./build/repl characters/pretorius # talk to Pretorius
./build/repl characters/kiki      # talk to Kiki
```

## Adding a new character

1. Copy `tools/compile_pretorius.c` → `tools/compile_<name>.c`.
2. Replace identity/drives/topics/patterns/templates/fallbacks/goals/today and banks with the new character's data.
3. Author `data/corpus_<name>.txt` in the character's register (~6 KB).
4. Add Makefile targets:
   - `build/compile_<name>` linking the same four libraries
   - `<name>_character` target invoking it
   - `characters/<name>/voice.lm` target driven by `build_lm`
5. `make <name>_character && ./build/repl characters/<name> <user_id>`

No engine code changes required.
