# PersonaConsole V6 — Runtime Doctrine

> **V6 thesis.** The cartridge defines who the character is; the V6 engine
> keeps the character *being that person* under contradiction, time, absence,
> and other minds. It maintains a structured autobiographical ledger, a
> multi-dimensional relational frame per actor, bounded attention,
> dissonance with typed sources, recall modes, and a self-image it actively
> defends.
>
> **The renderer remains a renderer.** Every realism gain lives in
> deterministic, inspectable, replay-tested Layer 1. The model — if used at
> all — only phrases the result.

This document is V6's constitution. It supersedes V5's `DEV_WORKFLOW.md`
doctrine half and extends it with the V6-specific commitments. V5 keeps
its leaner doctrine for the demo-team scope. V6 is the deeper engine that
all future cartridges, sidecars, and renderers must conform to.

The doctrine is normative. A change that violates it is wrong even if it
compiles, tests green, and reads well. Practice (Git workflow, build,
cartridge artifacts, line endings) lives in `DEV_WORKFLOW.md`.

---

## Part 0 — The Low-Hardware Soul Rule

PersonaConsole is an ownable, low-hardware deterministic character core.
This is not a side constraint; it is the product.

The deployable persona core is:

- engine,
- cartridge,
- compact sidecar state and memory,
- optional portrait/assets.

It must not require a GPU, cloud service, account, network connection,
model download, JavaScript runtime, database server, vector store, or
LLM. Forge, import tools, inspectors, evaluators, package builders, and
optional renderers may be heavier because they are authoring and testing
tools. They are not the soul.

Every engine feature should be judged by the old-game test: could a
bounded version of this idea run on laughably weak hardware and still
make the character feel more alive? Prefer symbolic records, fixed
capacity, integer scoring, small scans, and deterministic callbacks over
large dynamic systems. Do not add a large subsystem when a meaningful
salience flag, commitment record, open loop, or diary callback will do.

The goal is not to remember everything. The goal is to remember what
makes a persona feel alive: names, promises, private jokes, explicit
remember requests, conflicts, apologies, preferences, emotional turning
points, unfinished business, and relationship milestones.

Optional SLM/frontier renderers can improve phrasing, but they are never
the source of identity. The renderer decorates. Layer 1 decides.

---

## Part I — The V6 thesis

### 1. Bounded continuity under pressure

"Human-likeness" is not richer dialogue. It is **bounded continuity under
pressure**. Humans are convincing because they carry grudges, forget
inconvenient details, repair misunderstandings, avoid shame, remember
what they promised, react differently to different people, and slowly
become the sort of person their experiences have trained them to be.

Polished sentences are not the goal. Maintained selves are.

V6 design rule: a feature improves human-likeness if and only if it
improves the engine's ability to **carry**, **commit**, **remember**,
**defend**, or **be challenged on** a stable self over time. Features
that only improve isolated sentence quality without changing continuity,
memory, relation, repair, or self-consistency are secondary.

### 2. The seven non-negotiables

1. **Layer 1 owns identity.** The cartridge plus the engine are the
   character. The renderer is a disposable surface.
2. **Memory firewall.** No model-authored facts ever become canonical
   memory. Engine-authored facts about the character's own communicative
   acts may, and must.
3. **Behavioral holography.** Layer 1 state is a pure function of the
   five-tuple `(cartridge, seed, WAL, canonical inputs, canonical clock)`.
4. **Canonical time flows through one clock.** No behavior-relevant code
   reads the wall clock directly.
5. **Renderer non-authority.** Renderer output is disposable until audit
   passes; it never directly mutates Layer 1.
6. **Continuity governs sophistication.** Sophistication is welcome when
   it is deterministic, inspectable, testable, character-agnostic in
   engine code, and authored through cartridge or sidecar data.
7. **Selfhood is engineered, not generated.** The character's
   accountability, attention, dissonance, defenses, and self-image are
   structured engine state — never paraphrases, never LLM monologues,
   never emergent properties of prompt length.

---

## Part II — Foundational invariants (V5 carry-over, re-anchored)

### 3. Memory firewall, refined

The firewall blocks **model-authored truth** about the world, not
**engine-authored memory** about the character's own behavior. A
character that cannot remember it accused, refused, conceded, or
apologized cannot be conversationally accountable.

**Forbidden in canonical memory:**
- Raw renderer prose stored as fact.
- Renderer output treated as new world knowledge.
- Memories whose `MemorySource` is `PE_SRC_RENDERER_OUTPUT`.

**Permitted in canonical memory** (engine-authored, structured):
- Speech events — what the character said, refused, withheld, promised
  (see §14).
- Decisions — selected intent, stance, defense mode, repair mode.
- Activations — selected memory id, template id, render id.
- Outcomes — audit result, output hash.
- Reflections, schema deltas, dissonance increments, contradiction
  records.

### 4. Determinism — the five-tuple

Layer 1 state is a pure function of:

```
(cartridge, seed, WAL, canonical inputs, canonical clock) → Layer 1 state
```

All five must be pinned for an exact replay. Inputs are exogenous: a
different rendered surface can elicit a different next user input, which
legitimately changes future state. The renderer is allowed to influence
future state **only** through the user's next canonical input, never by
mutating Layer 1 directly.

Concretely:

- Randomness is only the seeded engine PRNG (`persona_rng_u32`).
- All sorts that affect behavior or persisted bytes are **total orders**.
  Returning `0` on a tie is a bug.
- Engine hot-path arithmetic is **fixed-point integer**.
- Endianness is little-endian-native for the current target.

### 5. Canonical time

All canonical time enters Layer 1 through `pe_clock_now_ms`,
`pe_clock_now_s`, `pe_clock_today_key`, `pe_clock_local_hour`, and
`pe_clock_today_dt`. Test fixtures pin via `PE_CLOCK_OVERRIDE_MS` or
`pe_clock_set_override_ms`/`pe_clock_advance_ms`. Direct `time(NULL)` is
permitted only in the clock implementation itself, non-canonical tooling,
logging that never feeds back into Layer 1, and RNG seed-entropy
fallback. Any time-derived behavior must be replayable from the WAL or
an explicit fixture.

### 6. Renderer non-authority and audit

The renderer receives a read-only `RenderContext` and returns disposable
text. It may not mutate Layer 1 under any circumstance. Inferences from
the renderer ("the user probably meant Henry") must reenter as user
input through the normal classification pipeline, never as direct
memory writes.

Renderer output passes audit before display. Audit is split:

**Deterministic** (these *guarantee* a property):
- Length within bounds.
- No illegal bracket tags or sentinel leakage.
- No exact repetition of the last N rendered openings.
- **Realizes the selected speech act** (V6 sharpening — see §14).
- If `repair_mode` is set, the output performs the indicated repair.
- If `defense_mode` is set, the output is consistent with it (does not
  apologize while suppressing, does not deny while confessing).

**Heuristic** (these *signal*; they do not prove):
- Invention detection — references to actors, places, events, memories
  not in Layer 1.
- Voice flag alignment.
- Excessive or insufficient disfluency relative to fatigue/arousal.
- Tonal alignment with current discrete emotion.

On audit failure: one deterministic repair pass, then template fallback.
Audit results are written into the speech event for the turn so a failed
audit is itself canonical memory.

### 7. Layer ownership

State lives in exactly one layer.

| Layer | Owns | Examples |
| --- | --- | --- |
| **Identity** | Authored, stable, read-only at runtime. | Big Five, voice flags, obsessions, taboos, wants, formative memory seeds, attachment style, defense tendencies, ideal self, ought self, feared self, archetype defaults. |
| **NPCState** | Session and long-running internal state. | Mood, drives, fatigue, suppression, fixation, surprise, prediction error, active goal, current intent, repair pressure, current discrete emotion, defense mode, attention slots (V6), dissonance accumulators (V6, typed). |
| **Relation** (multi-dim, V6) | Actor-specific state across many dimensions. | Trust, threat, intimacy, resentment, dependency, obligation, envy, admiration, embarrassment, theory-of-mind, attachment response, conflict history, repair history, actor id, desired impression for this actor. |
| **MemoryStore** | Canonical remembered events. | Episodic memories, core memories, actor-tagged events, **structured speech events (V6)**, contradiction records (V6), privacy thresholds, salience, emotion, topic. |
| **SchemaState** | Compressed beliefs about an interlocutor. | 8-slot per-relation belief state. |
| **OpenLoops** (V6) | Carried intentions and unresolved commitments. | Target actor, target topic, desired future speech act, urgency, shame cost, avoidance pressure, expiration. |
| **HabitRules** (V6) | Procedural bias learned from repeated experience. | Trigger class, actor class, relation state, preferred response act, confidence, decay, last-reinforced turn. |
| **UtterancePlan** | Per-turn, non-serialized speech plan. | Rhetorical mode, stance, target topic, callback memory, speech act, response act, defense mode, repair mode, attention slots, withhold reason, disfluency level. |
| **Renderer** | Wording only. | Read-only RenderContext; disposable output; passes audit before display. |

Cross-layer writes are forbidden. The renderer cannot write any layer.
The planner reads all of them but writes only `UtterancePlan`.

### 8. Psychological subsystem policy

Every new psychological module must satisfy all of:

1. **Engine-agnostic.** Zero character names or hardcoded character data
   in `core/`, `memory/`, `schema/`, `render/`.
2. **Data-driven** via cartridge fields or sidecars.
3. **Deterministic.** Fixed-point. Total-order. Seeded RNG.
4. **Inspectable.** Surfaced in the inner-loop display or state trace.
5. **Replay-tested.** A clock-pinned replay confirms the new state
   transition.
6. **Behavior-connected.** "It computes a number that nothing reads" is
   not a feature.
7. **Firewall-respecting.** Never reads raw renderer output as truth.

This policy is non-negotiable. Every V6 module below lands under it.

### 9. Sidecar and ABI policy

New persistent state lands as a versioned sidecar, never a casual
expansion of cartridge or core layout. Each sidecar carries
`pe_sidecar_header_t` (magic, version, flags, entry count, capacity,
reserved padding) and is validated through `pe_sidecar_validate`.
Optional sidecars soft-fail when missing; required cartridge sections
hard-fail when corrupted. Migrations are deterministic and have replay
tests. The legacy-identity migration test is the ratchet.

### 10. Inspectability is an invariant

Every state the engine maintains must be visible to the operator. If
you cannot see it, you cannot tune it and you cannot trust it. Every
new V6 module must expose its state in at least one of:

- the inner-loop display (web UI),
- the state trace (instrumentation),
- the per-turn debug output,
- the `state` method's JSON.

A new emotion, plan field, repair signal, or drift counter that is
invisible to the operator does not exist for product purposes.

### 11. Cross-platform code rules

C99 + POSIX. Supported toolchains: Linux native `gcc` and Windows
**Cygwin** `gcc`. No `_WIN32`/`__linux__` `#ifdef`s. No Linux-only
APIs. Forward-slash paths. Future native platforms enter through a
portability layer, not scattered forks.

---

## Part III — V6 new doctrine

### 12. The self-ledger (engine-authored speech events)

The self-ledger is V6's autobiographical spine and the most important
new layer. Without it, the character can remember user facts but cannot
remember **itself**. That is the difference between a clever puppet and
a character with continuity.

Every rendered turn produces a structured speech event. Records are
small, fixed-size, packed structs — never prose. Stored in a ring buffer
sidecar `speech_events.bin`.

**Per-event record (canonical, packed):**

| Field | Meaning |
| --- | --- |
| `turn_count` | The turn this happened on. |
| `clock_ms` | Pinned engine-clock time of utterance. |
| `target_actor_id` | The actor addressed (or zero for soliloquy / dream). |
| `target_topic_id` | The topic addressed. |
| `speech_act` | What the character did with words. |
| `response_act` | What the character was responding to. |
| `intent_id`, `stance`, `rhetorical_mode` | Planner choices. |
| `defense_mode`, `repair_mode` | Active strategies. |
| `selected_memory_id` | Memory surfaced (or zero). |
| `template_id`, `render_id` | Surface attribution. |
| `audit_result` | Passed / repaired / fallback. |
| `output_hash` | Hash of the rendered prose (the prose itself is NOT stored). |
| `withheld_intent` | Structured form of what the character *almost* said. |
| `withhold_reason` | Why it wasn't said: shame, privacy, distrust, taboo, confusion, fatigue, strategy. |
| `regret_marker` | Set later if dissonance grows around this turn. |

**Canonical speech-act enum (initial set):** assertion, question,
request, command, apology, praise, insult, threat, disclosure, refusal,
correction, greeting, farewell, evasion, confession, concession, promise,
deflection, pause, withdrawal, meta-conversation.

**Why withheld_intent on day one:** withholdings are accountability
events. "You avoided this before" must include things the character
almost said. Adding the slot later means a version bump. The slot may be
zero for ordinary turns.

The renderer audit's deterministic checks include **realizing the
selected speech act**. If the engine logs *"I accused Henry"* the
rendered prose must structurally realize an accusation; otherwise the
audit demands a repair pass or template fallback. Canonical memory and
the user's experience never split.

### 13. Multi-dimensional Relation

Disposition is too blunt. Humans can love and resent simultaneously,
trust and envy, fear and need. Each actor needs a relational profile
across many dimensions, persisted as a per-actor sidecar
`relation_dims.bin` (parallel to the existing `Relation` file).

**Dimensions (all 0..1000 fixed-point):**

- **trust** — credence in the actor's statements.
- **threat** — perceived danger to identity or safety.
- **intimacy** — closeness, vulnerability shared.
- **resentment** — accumulated unrepaid grievance.
- **dependency** — degree to which the character needs the actor.
- **obligation** — degree to which the character owes the actor.
- **envy** — desire for what the actor has or is.
- **admiration** — esteem for the actor's qualities.
- **embarrassment** — shame load that the actor has witnessed.

Each dimension has its own decay curve, hysteresis, and trait
amplifier. They are not orthogonal — the planner reads combinations.
Praise from `(trust=high, admiration=high)` soothes; praise from
`(trust=low, threat=high)` triggers suspicion; praise from
`(dependency=high)` is felt as obligation. Same input, different
meaning, deterministically derived.

The existing single `disposition` field remains as a derived summary
for V5 compatibility but is no longer canonical; the dimensions are.

### 14. Attention budget

Humans do not process everything equally. The V6 engine has bounded
attention: a per-turn, non-persisted scratch `attention_slots[4]`, each
slot tagged with a source:

- **user_text** — explicit content of the input.
- **active_memory** — surfaced episodic / reflection.
- **current_want** — engaged want.
- **relation_threat** — perceived danger from this actor.
- **bodily_state** — exhaustion, intoxication, fragility.
- **unresolved_thread** — open loop pressing for attention.
- **dissonance** — internal conflict pressing for resolution.

Slot selection is deterministic and depends on cartridge traits (a
vain character notices flattery first; an anxious one notices threat
first), current state, and relation. Planning operates **only on
attended items**. Things the character did not notice do not influence
the response.

This produces **partial understanding without random stupidity**.
Pretorius can miss obvious emotional needs when vanity is activated, in
a way that is reproducible and explainable.

Inspectability requirement: attention slot contents are exposed in
state output every turn.

### 15. Typed dissonance (after Higgins, 1987)

Self-discrepancy theory distinguishes three internal references that
each produce a different emotional family:

| Discrepancy | Source | Emotion family |
| --- | --- | --- |
| Actual ↔ **ideal self** | What I want to be vs. what I am | Disappointment, shame, envy, aspiration. |
| Actual ↔ **ought self** | What I should be vs. what I am | Guilt, anxiety, defensiveness. |
| Actual ↔ **feared self** | What I dread becoming vs. what I am | Panic, denial, overcorrection. |

V6 maintains three dissonance accumulators, one per type. The cartridge
authors define `ideal_self`, `ought_self`, and `feared_self` as fields.
When an engine-authored speech event or external input pushes the
character's behavior toward the feared self or away from the ideal,
the matching accumulator increments.

The planner has typed resolution paths:
- Ideal gap → confession, apology, aspirational reframe.
- Ought gap → rationalization, deflection, guilt-driven repair.
- Feared-self proximity → denial, overcorrection, defensive attack.

Dissonance is canonical state, surfaced in inspectability, persisted
across sessions.

### 16. Recall modes

Memory retrieval is not neutral. The same memory store supports many
retrieval intents, and the planner selects the mode for the turn:

- **accurate** — best semantic + recency match.
- **defensive** — boost memories that justify the current stance.
- **nostalgic** — boost high-positive-valence memories with the current
  actor.
- **accusatory** — boost memories of the current actor's prior bad acts.
- **shame-avoidant** — penalize memories near the feared-self gap.
- **intimacy-seeking** — boost shared-vulnerability memories with the
  current actor.
- **obsession-driven** — boost memories anchored to current fixation.
- **mood-congruent** — additive bonus where memory emotion matches
  current PAD (Bower, 1981).

Mode selection itself is canonical and recorded in the speech event.
Recall mode changes scoring, never the memories themselves.

### 17. Impression management

Pretorius alone, Pretorius with a rival, Pretorius with a student,
Pretorius with a confidant, and Pretorius in front of a group must not
feel identical. Each `Relation` carries a `desired_impression`:

- **superior**, **harmless**, **brilliant**, **wounded**, **seductive**,
  **paternal**, **dangerous**, **reasonable**, **misunderstood**, or
  **(none)**.

The desired impression influences stance, disclosure threshold, and
defense mode. When the user sees through the mask (e.g. challenges a
"harmless" stance with evidence of cruelty), dissonance increments
along the ideal-self axis.

This subsumes the V4/V5 `um_belief_about_me` field, which is
re-expressed as part of the multi-dim Relation. There is one
"what they think of me" system, not two.

### 18. Refusal and withholding as accountability

A character that always answers is not a person. V6 makes refusal a
**first-class speech act** with structured reason. Refusal records:

- `speech_act = REFUSAL` (or `PAUSE`, `EVASION`, `WITHDRAWAL`,
  `DEFLECTION`).
- `withhold_reason ∈ {shame, privacy, distrust, taboo, confusion,
  fatigue, strategic_concealment}`.

A later actor that says *"You avoided this before"* must be answerable
from the speech-event ring, not from template flavor. Refusal is
remembered and can be revisited.

### 19. Contradiction tracking

When a new speech event conflicts with a prior speech event, schema
belief, or stored memory, the engine appends to a `contradictions.bin`
sidecar:

- `prior_event_id`, `new_event_id`, `actor_id`, `topic_id`, `severity`.
- `awareness` — did the character notice? (often false on commit;
  set true later if challenged)
- `resolution_mode` — none / noticed / rationalized / defensive /
  confessed / repaired.

This is a major realism feature because people **are not perfectly
consistent**. They are inconsistent in **patterned ways**, and the
patterns are what define them. Contradiction handling becomes a
planner mode, not an error condition.

### 20. Open loops and carried intentions

Humans carry intentions across time. They plan to apologize, avoid
someone, bring up a topic later, test whether someone remembers, or
return to an old argument. The V6 sidecar `open_loops.bin`:

Per entry: `target_actor_id`, `target_topic_id`,
`desired_future_speech_act`, `urgency`, `shame_cost`,
`avoidance_pressure`, `created_turn`, `expiration`.

The planner reads open loops every turn and may surface them when the
matching actor returns. *"I did not answer your question about the
experiment last time. I noticed that I avoided it."* — that line is
authored from an open-loop record, not from template flavor.

### 21. Procedural habits (ACT-R-light)

The cognitive distinction between declarative and procedural memory
(ACT-R, Anderson 1996) is useful: declarative is "what I know",
procedural is "how I tend to respond." V6 adds a `habit_rules.bin`
sidecar:

Per rule: `trigger_class`, `actor_class`, `relation_state`,
`preferred_response_act`, `confidence`, `decay`, `last_reinforced_turn`.

Habits **emerge from repeated speech events**: when the character
responds the same way to the same trigger from the same actor class
across several turns, a habit rule is created or its confidence
increments. The planner has a habit-application mode (fatigue, low
attention) where habits weigh more than fresh appraisal. This gives
characters **habits, not just memories**.

### 22. Embodiment shapes cognition, not just style

Existing fields — `exhaustion`, `intoxication`, `physical_fragility` —
must do more than insert disfluency. V6 wires them to cognition itself:

- **Fatigue** reduces effective `attention_slot_count`, increases habit
  reliance, increases repetition, lowers repair patience, raises
  accessibility of old wounds.
- **Intoxication** lowers suppression mask effectiveness, increases
  associative-jump rate in recall, lowers audit confidence, raises
  inappropriate intimacy.
- **Fragility** raises threat appraisal weight in attention selection.

Embodiment changes **what the character can mentally do**, not only
how the character sounds.

### 23. Long-horizon drift

The cartridge defines who the character is. The engine does not mutate
authored identity. But the engine **does** maintain mutable
long-horizon state in a `long_arc.bin` sidecar:

Tracked counters (all 0..1000 fixed-point, slow decay): `wounds`,
`growth`, `shame_load`, `trust_injury`, `confidence`, `dependency`,
`attachment_insecurity`, `repaired_conflicts`, `betrayal_count`,
`formative_bond_count`.

Updates only on high-salience triggers (major confession, betrayal,
long absence, repeated repair, identity threat). The planner reads
long-arc counters as biases on appraisal and resolution. After three
days of argument the character reacts differently — without breaking
who they are.

### 24. The self-image guardian (capstone)

The deepest design rule of V6.

A character is human-like when **something is at stake internally**. Not
goals. Not emotion. A self-image the character is actively defending.

Pretorius does not merely *want* recognition. He needs to believe he is
above ordinary morality, more honest than cowards, more aesthetically
courageous than doctors, and immune to sentimental weakness. *Praise,
insult, intimacy, correction, boredom, and absence then all become
psychologically meaningful.* They threaten or support the self-image.
Without the self-image, those inputs are just numbers.

V6 implements this as a single canonical loop on every turn:

1. **Appraise** the input against the self-image (ideal / ought / feared).
2. **Compute** the dissonance increment(s) by type.
3. **Select** a resolution path consistent with current state and
   relation: confess, apologize, rationalize, deny, deflect, attack,
   withdraw, repair, reframe.
4. **Emit** a speech event whose `speech_act` and `defense_mode`
   reflect the chosen path.
5. **Render** under audit.

The self-image guardian is therefore not a new module but the **shape
of the V6 turn**. Every prior V6 layer feeds it; every prior V6 layer
is needed for it to be coherent.

---

## Part IV — Measurement and authoring

### 25. Society Lab as personhood regression

Society Lab is not a future demo. It is V6's **regression test for
personhood**. Once actor-tagged memory and the self-ledger exist,
Society Lab pairs agents in scripted multi-turn trials and measures:

- Are grudges held against the right actor?
- Do apologies from one actor change disposition toward another?
- Do rumors propagate through the speech-event ledger?
- Are alliances asymmetric (A trusts B more than B trusts A)?
- Are old events recalled by the correct actors?
- Does refusal persist? Does the prior refusal get challenged?

Society Lab runs daily on every V6 branch. Regressions are gate failures.

### 26. Believability battery — not one score

A single believability score becomes dangerous because developers tune
toward it. V6 measures along a battery of axes, each reported separately:

- Memory accountability (did the character remember what it did?).
- Actor attribution (was the right speaker recalled?).
- Repair quality (does conflict actually repair?).
- Contradiction handling (notice / rationalize / confess rate).
- Relationship differentiation (different reactions to different actors).
- Long-horizon drift (do counters move in coherent directions?).
- Speech-act realization (rendered prose matches logged act).
- Renderer conformance (audit pass rate, repair rate, fallback rate).

Plus adversarial trials: sarcasm, apology-after-insult, false-memory
claim, *"you said X"* fabrication, topic drift, long absence, repeated
praise, repeated threat, intimacy-from-stranger, contradiction-by-
trusted-actor.

The aggregate is computed but is not the truth. Per-axis scores are.

### 27. Forge archetype scaffolds

Without authoring scaffolds the V6 cartridge becomes unauthorable — too
many fields, too much shape. The Forge takes a short personality brief
plus an archetype selection and pre-fills 80% of:

attachment style, defense tendencies, ideal self, ought self, feared
self, formative memories, speech style banks, disfluency tendencies,
taboos, values, long-arc defaults, dominant recall mode, default
desired-impression, attention bias.

The author overrides what they need. This is how V6 becomes an engine
for any character, not a Pretorius-specific runtime.

---

## Part V — Examples of acceptable and unacceptable changes

**Acceptable under V6 doctrine:**

- Adding `relation_dims.bin` as a sidecar with `pe_sidecar_validate`,
  exposing the dimensions in `state` JSON, with a clock-pinned replay.
- Tightening a `qsort` comparator's tie-break to a unique secondary key.
- Adding a typed dissonance accumulator with author-defined cartridge
  fields and a planner read.
- Adding a recall mode whose selection is logged in the speech event.

**Unacceptable under V6 doctrine** (regardless of how compelling):

- A `time(NULL)` call inside engine, memory, schema, or render code.
- A renderer that writes to `MemoryStore` directly.
- Storing renderer prose as a structured fact in canonical memory.
- A new emotion module that produces a number nothing reads.
- A `#ifdef _WIN32` block in `core/` or `memory/`.
- A serialized struct change without a version bump.
- A `qsort` comparator that returns `0` on a tie.
- A psychological feature that lives in C strings hardcoded for one
  character.
- A new field with no debug visibility.
- An LLM call that *decides* identity rather than *phrases* it.
- A speech event whose rendered prose does not realize its logged
  `speech_act`.
- A "human-likeness improvement" that only changes wording, not
  continuity.

---

## Part VI — Pointer to practice

## Gate Chunk Timing Baseline

Measured on 2026-06-20 after the V6 chunked gate cleanup:

| Target | Wall time | Result |
| --- | ---: | --- |
| `make v6_gate_chunk_1` | 14.3s | Pass |
| `make v6_gate_chunk_2` | 13.0s | Pass |
| `make v6_gate_chunk_3` | 17.9s | Pass |
| `make v6_gate_chunk_4` | 42.2s | Pass |

The slow-gate failure was not primarily target-count imbalance. The main
cause was Node watchdog timers that kept successful harnesses alive until
their 15s to 30s timeout expired. Long-running Node harness watchdogs
should call `.unref()` after `setTimeout(...)` so the process can exit as
soon as the test has passed. Tests that copy profile state should use the
user temp directory on Windows/Cygwin, not `C:\cygwin64\tmp`, because that
path can reject cleanup during elevated Cygwin runs.

All chunks are now below the 200s budget, so no target rebalance is needed
at this baseline. Future rebalancing should use measured wall time, not
target count.

## Phase 5d Implementation Status

Current status against the Phase 5d planner-layer plan:

| Item | Status | Evidence / next work |
| --- | --- | --- |
| Attention budget | Missing | Doctrine defines `attention_slots[4]`, but no canonical engine struct, state JSON exposure, or tests exist yet. Current situation packet has lightweight `user_act`, `pressure`, and `attend_before_open_loops`, but this is not the full bounded attention budget. |
| Typed dissonance | Implemented | `dissonance.bin`, `pe_dissonance_t`, state JSON exposure, decay, persistence, replay coverage, and identity-test overlay reads exist. The sidecar now carries compact signed `ideal_self_model`, `ought_self_model`, and `feared_self_model` scalars seeded deterministically from cartridge traits until full cartridge-authored symbolic self ids land. |
| Impression management | Stubbed | Relation dimensions and prompt/stance pressure can express social posture, but no canonical `desired_impression` field or tested impression-management planner read exists yet. |
| Refusal / withhold logging | Implemented | Speech ledger records both `withhold_reason` and `withheld_intent`; state JSON exposes `last_withhold_reason` and `last_withheld_intent`; `withhold_logging_test.js` covers neutral, hostile, fatigue, withheld-intent persistence, and replay cases. |
| Private-thought frame | Implemented | Engine tracks a compact private-thought diagnostic frame (`private_thought_kind`, topic, pressure, withhold flag, internal hash) distinct from the expressed speech act. State JSON exposes it for tests; renderer output does not receive or leak the private diagnostic frame. |

Practice (Git workflow, build, test gates, cartridge artifacts, line
endings, pre-push checklist) lives in `DEV_WORKFLOW.md` and is shared
with V5. The doctrine binds; the practice supports it.

When in doubt, prefer the change that keeps the character **stable and
identical across machines** and **defended against its own
contradictions** over the change that is cleverer. Sophistication is
welcome but it serves continuity — never the other way around.
