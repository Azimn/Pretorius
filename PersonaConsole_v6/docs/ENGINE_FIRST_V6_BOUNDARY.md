# V6 Engine-First Boundary

PersonaConsole V6 should put as much character life as possible in the
engine, not in one showcase cartridge. A new character should inherit
the same living-character machinery automatically when loaded through a
valid cartridge.

## Current Engine-Level V6 Layers

These are implemented as engine/runtime systems and are available to
Pretorius, Kiki, and future cartridges:

- `speech_events.bin`: engine-authored self-ledger of what the character
  did conversationally.
- `relation_dims.bin`: per-actor trust, threat, intimacy, resentment,
  dependency, obligation, envy, admiration, and embarrassment.
- `dissonance.bin`: ideal / ought / feared self-gap accumulators.
- `recall_mode`: selected by engine state; changes retrieval intent
  without changing the memory store.
- `open_loops.bin`: carried intentions and unfinished conversational
  business, with active / resolved / expired lifecycle.
- Baseline pattern and template inheritance: generic social coverage
  for greetings, praise, insults, apologies, acknowledgements, and
  simple questions.
- Memory firewall: renderer output, including SLM output, cannot become
  canonical memory.

These systems must remain character-agnostic. Do not hardcode
Pretorius, Kiki, Henry, gin, or any cartridge-specific topic in engine
code.

## Cartridge Responsibilities

The cartridge should define who the character is, not reimplement the
runtime machinery. Cartridge and Forge output should supply:

- Identity and voice.
- Topics, obsessions, taboos, wants, and preoccupations.
- Templates, fallback lines, and style banks.
- Core memory seeds.
- Relationship posture and milestones.
- Optional V6 authoring fields when they land: ideal self, ought self,
  feared self, attachment style, defense tendencies, attention bias,
  dominant recall mode, desired impression, and archetype.

If a cartridge omits optional V6 fields, the engine must still run with
safe defaults. Missing V6 sidecars should initialize empty and begin
accumulating naturally.

## Forge And New Character Generation

Forge-generated characters must not become V6-inert. Export/preflight
should check for:

- At least one useful want.
- At least one preoccupation.
- Resumption lines for gap buckets.
- Relationship milestones.
- Plain conversational coverage, not only ornate monologues.
- Assistant-smell warnings when the character posture is not subordinate.
- Starter internal life generated from topics, memories, and posture.

The Forge can make authoring easier, but the engine should provide
fallback behavior when authoring is sparse.

## Character Card Import

Importing existing character cards, such as SillyTavern / TavernAI /
Chub-style cards, is a future convenience layer. It should map card
fields into PersonaConsole authoring fields:

- Name and description to identity.
- Persona / scenario / first message to voice and starter templates.
- Example dialogue to templates and voice corpus.
- Lorebook or alternate greetings to topics, memories, and banks.
- Tags to archetype, tone, obsessions, and taboo hints.

Card import should happen in the Forge or a conversion tool, not in the
engine. The engine should only load normalized cartridges.

## SLM Boundary

V6 may use a small local language model to improve variety and handle
novel wording, but the SLM is not the character.

Rules:

- Template mode remains the default and must stay excellent.
- SLM mode is optional and local.
- No cloud dependency for the core product.
- No mandatory model download for first run.
- The engine updates state before rendering.
- The SLM receives a read-only prompt compiled from Layer 1.
- SLM output is audited and may fall back to templates.
- SLM output never writes raw facts into memory.
- The same cartridge and same canonical inputs should preserve Layer 1
  identity across template and SLM renderers.

Target hardware:

- Template mode: tiny footprint, roughly current V6 engine costs.
- Optional sub-1B SLM: acceptable only as an opt-in tier.
- No GPU requirement.

## Next Engine-First Priorities

1. Make open loops influence planning: urgency should bias initiate,
   disclose, repair, evade, or redirect.
2. Add habit rules as another engine sidecar, learned from repeated
   speech events.
3. Add contradiction ledger as engine memory of inconsistent speech.
4. Add long-arc counters for wounds, growth, shame load, trust injury,
   confidence, dependency, and repair history.
5. Add Forge scaffolds for V6 optional identity fields after the engine
   defaults exist.
6. Add character-card import as a Forge/converter feature, not an
   engine dependency.

