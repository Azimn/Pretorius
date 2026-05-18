# PersonaConsole V4 — Architecture

> Version 4 is the first architecture hardening release.
>
> V4 is not about making the character smarter.
> V4 is about making the character harder to break.

## Thesis

The target product is **not** "AI assistant."

The target product is:

> A deterministic, low-resource, persistent synthetic identity runtime
> with optional semantic rendering augmentation.

That distinction radically changes implementation priorities. The
deterministic engine remains canonical. A language model, when present,
is treated as a *renderer* — never as the character.

```
The LLM is not the character.
The cartridge is the character.
The runtime is the nervous system.
The model is only a renderer.
```

Every engineering decision in V4 preserves that hierarchy.

The corollary: **never allow semantic sophistication to compromise
continuity determinism.** A dumber but stable character is preferable
to a smarter but unstable one.

## Three strictly separated layers

V4 separates concerns so external complexity cannot collapse the system.

### Layer 1 — Canonical Identity Runtime

**Authoritative.** Nothing external may mutate this state directly.

| File | Responsibility |
| --- | --- |
| `core/engine.c` | top-level pipeline, drive update, mood, plan |
| `core/plan.c` | UtterancePlan construction |
| `core/intent.c` | intent selection |
| `core/today.c` | today-state selection |
| `core/identity.c` | identity loading |
| `core/cartridge.c` | .cart container |
| `core/environment.c` | embodiment / arousal |
| `core/rng.c` | deterministic xorshift |
| `memory/memory.c` | working memory ring + associative recall |
| `memory/aether*.c` | long-term episodic store |
| `memory/lsh_memory.c` | SimHash |
| `memory/consolidate.c` | nightly k-means consolidation |
| `memory/story.c` | autobiographical chapters + dreams |
| `memory/relations.c` + `user_model.c` | theory of mind |
| `memory/serialize.c` | persistence |
| **`memory/memory_firewall.c`** (V4 NEW) | renderer contamination barrier |
| **`memory/affect_curve.c`** (V4 NEW) | nonlinear emotional persistence |
| **`schema/schema_state.c`** (V4 NEW) | compressed identity interpretations |

### Layer 2 — Rendering Abstraction

**Non-authoritative.** Outputs are disposable. **No memory writes.**

| File | Responsibility |
| --- | --- |
| `render/render_backend.h` | strict interface contract |
| `render/render_backend.c` | registry + default selection |
| `render/prompt_compiler.c` | state → structured constraints |
| `render/templates/template_backend.c` | deterministic v3.2 path as a RenderBackend |
| `render/slm/slm_backend.c` | tiny SLM target (stub; provider TBD) |
| `render/providers/` | future: ollama, llama.cpp, openai-compat |
| `render/templates/dialogue.c`, `voice.c`, `mutator.c`, `ngram_lm.c` | legacy template renderer (still authoritative for `template` backend) |

### Layer 3 — Interface

| File | Responsibility |
| --- | --- |
| `bridges/persona_host.c` | HTTP + stdio harness |
| `bridges/persona_ffi.c` | C ABI for Unity/Unreal/Godot |
| `bridges/http.c`, `json.c` | transport |
| `bridges/web/` | minimal chat UI |

Future bridges: terminal, mobile, voice IO, game-engine plugins.

## The four V4 contracts

### 1. RenderBackend interface (`render/render_backend.h`)

```c
typedef struct RenderBackend {
    const char *name;
    int (*initialize)(struct RenderBackend *self);
    int (*shutdown)  (struct RenderBackend *self);
    int (*render)    (struct RenderBackend *self,
                      const RenderContext  *ctx,    /* CONST */
                      RenderResult         *out);
    void *priv;
} RenderBackend;
```

The runtime composes a `RenderContext` from Layer 1 state and hands it
to the selected backend. The context is `const`. Backends produce a
`RenderResult` (disposable text). Hot-swappable: `PE_RENDER_BACKEND=slm`
selects the SLM path; falls back to template when unavailable.

Built-in backends:

- `template` — wraps the v3.2 template renderer. Deterministic, default.
- `slm` — tiny SLM target. Stub in this commit; provider wiring lives in `render/providers/` (ollama / llama.cpp / openai-compat).

### 2. Memory firewall (`memory/memory_firewall.h`)

> No generated text may directly become memory. Ever.

Every memory write declares a `MemorySource`:

```
PE_SRC_USER_INPUT       allowed
PE_SRC_SYSTEM_TICK      allowed (decay, not creation)
PE_SRC_CARTRIDGE_LOAD   allowed (seed only)
PE_SRC_RENDERER_OUTPUT  ALWAYS DENIED
```

If a future cloud LLM hallucinates that the user said something they
didn't, the hallucination is structurally barred from re-entering
memory as truth. This is the single most important architectural
protection in V4.

### 3. Schema layer (`schema/schema_state.h`)

Schemas are **not** memories. They are **beliefs derived from
memories.** Eight built-in slots per relation:

```
USER_TRUSTWORTHY  USER_HOSTILE       USER_INTIMATE      USER_COMPETENT
USER_DECEPTIVE    RELATIONSHIP_OWED  RELATIONSHIP_OWES  SELF_DIGNITY
```

Each slot carries strength (−1000…+1000) and an evidence count.
Schemas are updated by *symbolic events* (`SCHEMA_EVT_INSULTED_US`,
`SCHEMA_EVT_BROKE_PROMISE`, etc.), never by text. The renderer reads
schemas first, raw episodic memory second — that's what makes the
character's stance toward the user coherent across long timescales
even when the AETHER cold scratch fails to surface the original
offending memory.

### 4. Affect curves (`memory/affect_curve.h`)

Nonlinear emotional persistence:

1. **Salience-weighted decay** — high-salience memories / schemas
   decay slower (`decay × (1 - sal/1000)²`).
2. **Hysteresis** — state resists rapid reversal. Same-sign deltas
   damp at extremes; opposite-sign deltas attenuate by current
   magnitude.
3. **Habituation** — repeated identical stimuli halve effective
   magnitude per consecutive hit. Gap resets the counter.
4. **Trait-amplified persistence** — high-N characters retain
   hostility longer; high-A retain trust longer; etc.

All saturating fixed-point. No floats. No allocations. Deterministic.

## Prompt compiler

`render/prompt_compiler.c` projects deterministic state into structured
rendering constraints. **Not a lore dump.** **Not roleplay prompting.**

Bad (what we never produce):

> You are Maker, a sarcastic blacksmith who hates the user…

Good (what we produce):

```
IDENTITY:
  name: Maker
  big5: O=40 C=80 E=30 A=20 N=60
CURRENT_AFFECT:
  mood:        -180
  arousal_acute_spike: 820
  obsession_pressure:  650
  exhaustion:  410
RELATIONAL_STANCE:
  schema: hostile=540/3 self_dignity=-180/2
ACTIVE_MEMORY_HOOKS:
  - user insulted craftsmanship
  - unresolved dispute regarding sword repair
VOICE_MASK:
  - terse
  - metaphorical
  - sardonic
  - flourish:  — and the lightning will witness
INTENT:
  primary: accuse
  rhetorical: indict
  stance: condescending
  certainty=160 aggression=180 theatricality=120 hedging=20
USER_INPUT:
  why are you being so difficult about this
```

Identical RenderContext → byte-identical prompt. That's the
**behavioral holography** invariant operationalized.

## Behavioral holography

Given identical seed, WAL, drives, relation state, elapsed time, and
retrieved memories, the character must:

- hold the same grudges
- reference the same memories
- maintain the same emotional stance
- preserve the same interpersonal framing

…whether rendered through templates, a 1B SLM, an 8B model, or a
cloud model. Only **linguistic fidelity** varies. **Identity does
not.**

Test: `tests/replay/replay_determinism_test.js` drives `persona_host`
twice with PE_RENDER_BACKEND unset and once with `=slm`, asserts every
turn's state JSON is byte-identical across all three runs.

## Tiny SLM target

V4 assumes model interchangeability. Architecture targets:

- 1B–4B parameter range
- Q4 quantization minimum
- CPU-compatible inference first
- GPU optional, never required

Initial compatible families (never hardcoded): Gemma small, Qwen small
instruct, SmolLM, TinyLlama-class, Phi-mini-class. Providers live in
`render/providers/` and ship after the abstraction layer is stable.

## Performance budgets

| Mode | RAM | Latency | Dependencies |
| --- | --- | --- | --- |
| Offline deterministic | <100 MB | instant (<10 ms/turn) | zero |
| Tiny SLM | <4 GB | <3 s/turn | local model + provider lib |
| High-end | (optional) | (optional) | never required for continuity |

The deterministic engine is the canonical implementation. SLM and
cloud paths are *augmentations.*

## What's explicitly **not** in scope

- Autonomous agents
- Internet-connected assistants
- Tool-using copilots
- Generalized AGI

Those domains are overcrowded and structurally unstable. V4's niche is
**persistent synthetic identity under constrained compute** — unique,
technically defensible, philosophically coherent.

## V4 test suite

| Test | Assertions | What it covers |
| --- | --- | --- |
| `make test` | qualitative | Pretorius 8-turn sanity |
| `make kiki_test` | qualitative | Kiki 8-turn sanity |
| `make lsh_test` | 12 | SimHash + consolidation |
| `make plasticity_run` | 14 | LM scoring + mutator banks |
| `make aether_run` | 19 | AETHER standalone |
| `make aether_pe_run` | 12 | AETHER ↔ engine integration |
| `make voice_run` | 12 | counterfactual rerank |
| `make host_run` | 12 | HTTP + FFI surface |
| `make v4_modules_run` | 28 | firewall + schema + affect + registry + prompt compiler |
| `make v4_replay_run` | 12 | determinism + cross-backend holography |
| `make forge_cart_test` | 1 | Forge → .cart → host roundtrip |
| `make forge_bundle_test` | 1 | Forge bundle.zip + AETHER WAL |
| `make forge_dialogue_test` | 2 | dialogue-pack overrides + today-states |
| `make forge_archetypes_test` | 12 | all gallery archetypes build + reply |
| `make forge_parsers_test` | 21 | ChatGPT/Claude/Gemini/character.ai/SillyTavern parsers |

## Future commits

- Wire `engine.c` to call `RenderBackend->render()` directly (currently the legacy template path runs inside `pe_generate_response`).
- Implement `render/providers/ollama_provider.c` against `localhost:11434/api/generate`.
- Implement `render/providers/llama_cpp_provider.c` against `libllama.so`.
- `instrumentation/state_trace.c` + `replay_debugger.c` + `emotional_graph.c` + `memory_inspector.c`.
- Schema integration: wire `pe_classify_input` to emit symbolic events into the per-relation `SchemaState`.
- Affect-curve integration: replace linear decay in `engine.c` with `affect_decay`.
