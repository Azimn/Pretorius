# V6 Renderer Packet Experiment

## Purpose

This experiment tests whether V6 sometimes feels like an old NPC because the renderer sees too narrow a turn packet.

The baseline packet tells the model the selected identity, affect, memory, intent, voice, user input, and task. That is safe, but it can behave like a cue-card assignment. The situation packet keeps Layer 1 fully authoritative while giving the renderer a compact description of the conversational situation.

## Hypothesis

If a local model receives the live conversational situation, including user act, conversational pressure, memory-as-motive, and a response-move recommendation, it should attend to the user's actual turn more naturally while staying inside the cartridge and audit rules.

This does not make the model the character. The C runtime remains the identity, memory, state, and audit authority.

## What Changed

Files modified for the experiment:

- `render/prompt_compiler.h`
- `render/prompt_compiler.c`
- `core/engine.c`
- `tests/replay/v4_modules_test.c`
- `tests/continuity/packet_experiment_ab.js`
- `Makefile`
- `.gitignore`
- `docs/V6_PACKET_EXPERIMENT.md`

The new mode is enabled with:

```bash
V6_PACKET_MODE=situation
```

Default behavior is unchanged when that variable is absent.

## Situation Packet Contents

The situation packet adds:

- Character authority and identity guardrails.
- Human-readable affect and relation state.
- Canonical frame details such as intent, speech act, stance, rhetorical mode, topic, and open-loop pressure.
- Selected memory as motive, not as database text.
- A heuristic user-turn interpretation.
- A response-move recommendation.
- Output rules that forbid system explanation, invented facts, and assistant tone.

The user-turn interpreter is heuristic and deterministic. It currently recognizes practical acts such as greeting, direct question, correction, disagreement, challenge, emotional disclosure, memory probe, identity test, topic shift, open-ended invitation, rich neutral input, small talk, and unclear input.

## Trace Logging

Set:

```bash
V6_PACKET_TRACE=1
```

Trace output is written to:

```text
tmp/packet_experiment/packet_trace.jsonl
```

The A/B harness writes:

```text
tmp/packet_experiment/current.jsonl
tmp/packet_experiment/situation.jsonl
```

Each JSONL record includes:

- timestamp
- profile name
- input text
- packet mode
- detected user act
- detected conversational pressure
- selected goal and intent
- open-loop pressure
- retrieved memory ids and summaries
- response-move recommendation
- model name
- raw model output
- audit result
- final output
- whether Layer 1 memory changed
- whether symbolic sidecars changed

Generated traces are ignored by git.

## A/B Demo

Build first:

```bash
make host cartridges
```

Run the demo:

```bash
make v6_packet_experiment_run
```

The harness uses Pretorius as the character and Kiki as the user voice. Kiki is intentionally high contrast, so it is easier to see whether the renderer attends to the actual user turn instead of falling back to generic gothic motion.

By default the harness looks for:

```text
qwen3:8b
```

To use another local Ollama model:

```bash
PE_PACKET_MODEL=mistral:latest make v6_packet_experiment_run
```

If Ollama or the requested model is unavailable, the harness prints a clear skip and exits without pretending the quality comparison ran.

## Human Review Rubric

For each paired turn, ask:

1. Did the response attend to Kiki's actual message?
2. Did it avoid cue-card feeling?
3. Did it avoid over-performing Pretorius voice?
4. Did it answer directly when appropriate?
5. Did it preserve identity?
6. Did it avoid invented facts?
7. Did memory remain uncontaminated?
8. Did open loops help rather than hijack the turn?

Do not collapse this into one vague quality score. The point is to see which failure mode moved.

## Safety Rules Preserved

- No required LLM.
- No cloud dependency.
- No renderer authority over identity.
- No raw generated prose as canonical memory.
- No direct renderer-to-memory write path.
- Render audit and lore audit remain active.
- Template mode remains the canonical low-hardware default.

## Current Weak Spots To Watch

- The user-turn interpreter is intentionally simple. It should prevent obvious hijacks, not perfectly infer psychology.
- The situation packet is not yet model-template-specialized for Gemma-style two-role chat. Use it first with qwen or other models that handle flat structured prompts well.
- A lively raw model output may still be repaired or replaced by audit. Check both `raw_model_output` and `final_output` in the trace.
- The experiment evaluates the renderer packet, not memory quality by itself. Memory realism still needs the cold-open and cross-session tests.

## Audit Categories And Repair

The audit now records both result and violation category:

- `none`
- `speech_act_mismatch`
- `empty_output`
- `self_repeat`
- `fatigue_terms`
- `meta_or_assistant_tone`
- `lore_drift`
- `copied_user_text`
- `memory_label`
- `wrong_addressee`

Hard violations are treated conservatively:

- meta or assistant tone
- lore drift
- copied user text
- memory/system labels

Soft violations receive one constrained renderer retry:

- speech-act mismatch
- empty output
- self-repeat
- fatigue-term overuse

Wrong addressee is a special deterministic repair case. If the model uses
the wrong vocative name but the conversational move is otherwise valid, the
engine replaces only that vocative with the active actor's canonical name and
re-runs audit. This preserves the live response instead of throwing it away.

Lore drift receives one constrained retry with a narrow instruction to keep
the same conversational move while removing unsupported facts, names, places,
dates, and relationships. If that retry still fails, the fallback is chosen by
both violation type and practical user act, so an emotional disclosure receives
an acknowledgement fallback and a memory probe receives grounded uncertainty.

The repair prompt includes a `[REPAIR]` block with the previous draft, violation type, and instruction to preserve the same conversational move. The renderer still cannot write memory.

## Latest Local Result After Repair Pass

Run:

```bash
make v6_packet_experiment_run
```

Model:

```text
qwen3:8b
```

Baseline before this pass:

- Current packet: 8 pass, 1 repaired, 1 fallback.
- Situation packet: 6 pass, 3 repaired, 1 fallback.
- Situation memory probe fell back to generic deflection.

After audit categorization, constrained rewrite, memory-probe overlay, and memory-probe recall boost:

- Current packet: 8 pass, 1 repaired, 1 fallback, 1 constrained rewrite attempted.
- Situation packet: 7 pass, 3 repaired, 0 fallback.
- Situation fallback rate dropped from 10 percent to 0 percent.
- Situation memory probe stopped deflecting and answered from the planted code/spellwork memory.

Representative improved memory-probe output:

```text
Kiki, you said code feels like spellwork, like you're stitching together something that should not exist, but somehow does. Did you mean that in the sense of creation, or in the sense of... well, the work?
```

Important trace finding:

The earlier memory-probe failure was not only prompt wording. Layer 1 selected Henry memories instead of the user's code/spellwork memory. A small engine-level memory-probe recall boost now prepends recent episodic memories that share content words with explicit memory-probe input.

The first packet experiment exposed useful heuristic bugs:

- "What do you actually want..." was misread as correction because of the word "actually".
- A reflective sentence with an internal "but" was misread as challenge.
- "I get nervous..." was not recognized as emotional disclosure.
- "What would you ask me..." was treated as a generic direct question instead of an invitation.

Those issues are now covered by `v4_modules_test`.

Those issues are covered by `v4_modules_test`.

New tests added:

- `v6_audit_rewrite_run`
- `v6_memory_probe_overlay_run`
- `v6_wrong_addressee_repair_run`
- `v6_lore_act_fallback_run`

Interpretation:

The result supports the hypothesis more strongly than the first pass. Richer situation packets help, but the bigger lesson is that renderer quality depends on Layer 1 selecting the right symbolic memory and exposing audit failures precisely. When Layer 1 selected the correct memory, qwen produced a much more alive response without weakening the firewall.

Latest narrow hard-repair pass:

- Wrong addressee no longer falls into the hard fallback pool. In the focused test, `Henry, ...` became `Kiki, ...` with no renderer retry and no template fallback.
- Lore drift now gets one constrained rewrite before fallback. If the rewrite also drifts, the fallback is selected from the user act. In the focused test, an emotional disclosure received: `I hear the weight of it. Stay with that a moment.`
- Re-running the A/B harness kept situation mode at 0 percent fallback rate: 8 pass, 2 repaired, 0 fallback.
- The current packet still had 1 fallback out of 10. That remaining fallback is useful evidence for moving the richer situation packet toward the production SLM path.

Remaining weakness:

Lore-drift repair is still only as good as the model's second attempt. If the constrained rewrite also invents facts, the act-aware fallback preserves conversational relevance but may still feel less character-specific than a successful renderer response. The next polish should be better allowed-name and cast anchoring, not broader renderer authority.
