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

## Latest Local Result

Run:

```bash
make v6_packet_experiment_run
```

Model:

```text
qwen3:8b
```

The first run exposed useful heuristic bugs:

- "What do you actually want..." was misread as correction because of the word "actually".
- A reflective sentence with an internal "but" was misread as challenge.
- "I get nervous..." was not recognized as emotional disclosure.
- "What would you ask me..." was treated as a generic direct question instead of an invitation.

Those issues are now covered by `v4_modules_test`.

After the fixes, the situation packet showed partial support for the hypothesis:

- It attended better to the direct jar question than the current packet.
- It gave a more relevant response to Kiki's nervous disclosure.
- It produced a stronger character-led question for the open-ended invitation.
- It still failed the memory probe once and fell back to "Say it another way."
- It still sometimes repaired into template lines when audit rejected the model output.

Interpretation:

The result supports the narrow version of the hypothesis: richer situation packets can help the local model attend to the live turn instead of merely decorating an engine cue. It does not prove the packet is finished. The next improvement should focus on memory-probe wording and reducing audit-triggered fallback in situation mode without weakening the firewall.
