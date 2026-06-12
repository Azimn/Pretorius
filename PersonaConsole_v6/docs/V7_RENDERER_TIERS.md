# PersonaConsole V7 Renderer Tiers

V7 formalizes the renderer strategy without changing the core doctrine:
PersonaConsole is a deterministic character operating system. Layer 1 owns
identity, mood, memory, relationships, refusals, open loops, and decisions.
Renderers phrase a turn. They do not become the character.

## Tier 0: Template Runtime

Default mode.

- No LLM
- No internet
- No account
- No GPU
- No model download
- Lowest hardware target
- Deterministic and replayable
- Works in Micro Mode

This is the soul-cartridge baseline. If every cloud service disappears and no
model is installed, the character still exists.

## Tier 1: Local SLM Harness

Optional private model mode.

Use:

```sh
PE_RENDER_BACKEND=slm PE_SLM_PROVIDER=ollama PE_SLM_MODEL=gemma3:1b ./build/persona_host profiles/pretorius/pretorius.cart --stdio
```

The local SLM receives a structured packet compiled from `CanonicalTurnFrame`,
selected memories, relationship state, speech act, topic, stance, and voice
constraints. The model may improve language variety and sentence-level life,
but it cannot write canonical memory.

Recommended purpose:

- private richer conversation
- small-model roleplay experiments
- offline or LAN-only deployments with Ollama/llama.cpp-class models

Candidate small models to evaluate:

- `qwen3:8b` is the current best local-quality reference from early V7 tests.
- `LFM2.5-1.2B-Instruct-GGUF` is a promising tiny/edge candidate to test as a
  portable "small lightning" tier between `gemma3:1b` and larger 4B/8B models.
- `LFM2.5-Audio-1.5B-GGUF` is a future voice/harness candidate, not a core
  runtime dependency. Audio belongs outside the deterministic identity core.

Evaluate models by PersonaConsole behavior, not generic benchmark scores:
lore drift, repetition, assistant tone, speech-act obedience, character voice,
and whether the model improves template mode without taking identity authority
away from Layer 1.

## Tier 2: Frontier API Renderer

Optional API mode for users who want frontier-model expressiveness while using
PersonaConsole as the continuity harness.

Use:

```sh
PE_RENDER_BACKEND=slm \
PE_SLM_PROVIDER=api \
PE_API_URL=https://api.openai.com/v1/chat/completions \
PE_API_KEY=... \
PE_API_MODEL=gpt-4.1-mini \
./build/persona_host profiles/pretorius/pretorius.cart --stdio
```

This mode depends on `curl` at runtime. It is not linked into the engine as a
cloud SDK, and it is never required for the default experience.

The API model receives the same read-only grounding packet as a local SLM, just
with a larger language model behind it. The same post-render audits apply:

- speech-act audit
- lore/proper-noun drift audit for renderer output
- template fallback if the renderer violates the turn frame

## The Non-Negotiable Contract

All renderer tiers consume the same kind of packet:

```text
Layer 1 Persona Core
  -> CanonicalTurnFrame
  -> retrieved symbolic memories
  -> relationship/open-loop/recall context
  -> renderer
  -> audited disposable prose
```

No renderer may directly mutate:

- episodic memory
- core memory
- relation dimensions
- open loops
- speech ledger
- dissonance state
- cartridge identity

The next user input re-enters Layer 1. The renderer's own prose does not become
truth merely because it was generated.

## Hardware Principle

V7 may raise requirements only when the user explicitly chooses a model-backed
renderer. The core runtime remains low-hardware by design.

- Template/Micro: tiny, deterministic, offline
- Local SLM: requirements dominated by chosen model
- Frontier API: requirements dominated by network/account/API access, not local compute

The renderer tier is replaceable. The character's continuity is not.
