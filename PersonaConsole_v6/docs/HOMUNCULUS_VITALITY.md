# Homunculus Vitality V7.2

## Purpose

Homunculus Vitality is the V7.2 layer that turns existing continuity pressure into a compact prompt-facing behavioral packet. It is not a new personality engine and it is not a hidden monologue.

The rule is:

- The engine owns vitality scaffolding.
- The cartridge owns the soul.
- The renderer phrases the already bounded turn.

Pretorius is the flagship regression specimen, not the default style. A good vitality pass makes Pretorius more Pretorius, Kiki more Kiki, and future cartridges more themselves.

## Files

The optional cartridge section is:

```text
vitality.bin
```

It is loaded after the required cartridge sections. If absent, the engine zero-fills the profile and synthesizes neutral vitality behavior. Old cartridges remain valid.

Runtime code:

- `core/vitality.h`
- `core/vitality.c`
- `render/prompt_compiler.c`

Tests:

- `tests/replay/v7_2_homunculus_vitality_test.c`
- `tests/fixtures/vitality/*.fixture`

## VitalityProfile

`VitalityProfile` is cartridge-authored data. It contains bounded text fields:

- `address_terms`
- `social_stances`
- `rhetorical_moves`
- `recurring_images`
- `forbidden_generic_phrases`
- `emotional_palette`
- `intimacy_gradient`
- `authority_style`
- `vulnerability_style`
- `conflict_style`
- `humor_style`
- `metaphoric_domains`
- `ritual_phrases`
- `taboo_tones`
- `memory_coloring_preferences`

Authors should write these as short pressure cues, not paragraphs. They should describe how the character tends to move, not exact lines to recite.

Good:

```text
authority_style = "precise, amused, technically useful, never helpdesk"
```

Bad:

```text
authority_style = "Always say: I have returned from the laboratory..."
```

## VitalityFrame

`VitalityFrame` is runtime synthesis. It is the per-turn weather:

- `emotional_posture`
- `social_stance`
- `active_desire`
- `conversational_tactic`
- `unresolved_thread`
- `style_anchor`
- `memory_boundary`

It compresses existing engine state into a small shape that a template renderer, small local model, or frontier model can obey.

It is synthesized from:

- affect
- intent
- relation dimensions
- open loop pressure
- selected memories
- wants and preoccupations
- cartridge-authored vitality fields
- durable schema climate

It must not invent biography, relationships, or past events.

## Weather And Climate

The user-facing vitality packet is not the whole memory system.

The distinction is:

- `VitalityFrame` is weather. It describes what is tugging on this turn.
- `SchemaState` and `learned_knowledge.bin` are climate. They store slow accumulated expectations, durable claims, corrections, and confidence.

V7.2 does not add a separate `BeliefLedger` because the engine already has compact fixed-slot schema state with evidence, decay, and persistence. The vitality synthesizer now reads that schema climate so repeated experience can shape posture.

Examples:

- high `SCHEMA_USER_TRUSTWORTHY` can make the social stance expect reliability
- high `SCHEMA_USER_HOSTILE` can make the social stance expect hostility
- high `SCHEMA_USER_DECEPTIVE` can make the character test framing

This keeps the design low-hardware and avoids a second ledger that would compete with existing recall modes, salience, relation dimensions, dissonance, and open loops.

Future work may add named schema slots such as `flattery_precedes_request` or `ethics_challenge_pattern`, but only if tests show the existing fixed slots are too coarse.

## Prompt Packet

The prompt compiler emits a compact section:

```text
[VITALITY]
emotional_posture=...
social_stance=...
active_desire=...
tactic=...
unresolved_thread=...
style_anchor=...
memory_boundary=memory facts are durable only when explicitly stored; expressive imagery is present-performance only
avoid_generic=...
```

This section is intentionally short. It gives the model a live pressure packet without turning the prompt into a roleplay essay.

## Memory Boundary

Memory facts and expressive performance are different.

Facts:

- user name
- past commitments
- corrections
- relationship history
- cartridge canon
- learned knowledge records

These must come from Layer 1 memory and sidecars.

Performance:

- sensory color
- present mood texture
- atmosphere
- metaphor
- private-feeling expression

These may be supplied by the renderer as present performance. They must not become durable memory unless the memory system explicitly commits them.

This is why the V7.1 prompt allows sensory detail while still forbidding invented named people, named places, and specific past events.

## GGUF Template Hygiene

PersonaConsole does not blindly trust model-supplied GGUF chat templates.

Ollama requests default to raw prompt mode. Setting `PE_OLLAMA_RAW=0` is ignored unless `PE_ALLOW_MODEL_TEMPLATE=1` is also set.

This keeps a model template from injecting hidden system behavior that could override the cartridge, memory authority, or character voice.

The small rule is:

```text
Use PersonaConsole's prompt as authority unless the operator explicitly opts into model templates.
```

## Assistant Leakage Hygiene

Shared vitality hygiene detects generic assistant phrases such as:

- `Sure, I can help with that.`
- `Here are some suggestions.`
- `Let me know if you want`
- `As an AI language model`
- `I'd be happy to help`
- `I can help you with that`
- `Certainly!`
- `Of course!`

The shared audit detects these phrases. It does not replace them with Pretorius phrasing. Repair and fallback must stay cartridge-neutral or cartridge-authored.

Neutral fallback rule:

```text
Answer directly in character. Do not use helpdesk phrasing.
```

## Writing A Parity Fixture

A parity fixture only needs enough data to prove character distinction.

Each fixture should define:

- a character name
- social stance
- rhetorical move
- recurring image
- emotional palette
- authority style
- conflict style
- humor style
- metaphoric domain

V7.2 includes fixtures for:

- Devil NPC
- Queen
- Archbishop
- Priest
- Ballerina

The test requires each fixture to produce a distinct style anchor without inheriting Pretorius strings or inventing user biography.

## Authoring Advice

Use vitality fields for pressure and style tendencies.

Do:

- give the character specific social moves
- define how intimacy changes the voice
- define what kind of humor belongs to the character
- define forbidden generic tones
- define recurring image domains
- keep entries short

Do not:

- put universal roleplay prose in the engine
- teach every character the same examples
- rely on Pretorius as the hidden default
- store exact rendered replies as memory
- let model output become identity

## Testing A New Homunculus

Run:

```text
make v7_2_homunculus_vitality_run
```

The scorecard is:

- Homunculus Vitality: pass
- Character Distinction: pass
- Generic Assistant Leakage: 0 hard failures
- Memory Invention: 0 hard failures
- Repetition Collapse: no shared opener across fixtures
- Backward Compatibility: pass

For live testing, run the same prompts through multiple cartridges. If Kiki, Pretorius, and a neutral mentor all share the same opener, same cadence, or same fallback posture, the engine has leaked style. Fix the cartridge packet or fallback path before adding more prose.

## Pretorius Parity

Pretorius vitality data belongs in `profiles/pretorius/vitality.bin`, generated by `tools/compile_pretorius.c`.

Pretorius should preserve:

- interlocutor-style address
- scientific, occult, and alchemical metaphor domains
- sardonic but useful technical guidance
- theatrical precision
- disdain without useless cruelty
- concrete engineering advice beneath persona voice

None of those traits belong in shared engine code.

## Known Limits

The vitality frame is compact by design. It does not replace:

- episodic memory
- learned knowledge
- relation dimensions
- open loops
- typed dissonance
- recall modes
- render audit

The current schema climate is fixed-slot and intentionally coarse. That is acceptable for V7.2. If future tests show repeated patterns need finer tracking, add named schema slots or a tiny pattern ledger only after proving the existing climate layer cannot carry the behavior.
