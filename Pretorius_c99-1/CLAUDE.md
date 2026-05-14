# Persona Engine — CLAUDE.md (v2)

Deterministic synthetic-personality runtime per PE-SPEC-001, second pass.
Single-threaded C99, no heap allocation in `process_input`, all math
saturating fixed-point. Target: < 10 ms / turn, < 300 KB / character on
P3-class hardware.

This is **v2 of the Pretorius_C99 reference implementation**, addressing
external-review feedback on top of the original. The base architecture from
v1 is preserved; v2 adds layers on top rather than replacing primitives.

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
