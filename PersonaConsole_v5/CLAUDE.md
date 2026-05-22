# Persona Console V4 — CLAUDE.md

V4 is the first **architecture hardening** release.  Target product:

> A deterministic, low-resource, persistent synthetic identity runtime
> with optional semantic rendering augmentation.

Not an AI assistant.  See `docs/V4_ARCHITECTURE.md` for the full spec.

## The four V4 invariants

1. **The LLM is not the character.**  The cartridge is the character.  The runtime is the nervous system.  The model is a renderer.
2. **Memory firewall.**  No generated text ever becomes memory.  All Layer 1 writes carry a `MemorySource`; `PE_SRC_RENDERER_OUTPUT` is always denied.
3. **Behavioral holography.**  Same seed + WAL + inputs → identical identity state across all renderers.  Only linguistic fidelity varies.
4. **Continuity beats sophistication.**  A dumber but stable character is preferable to a smarter but unstable one.

## V4 directory layout

```
PersonaConsole_v4/
  core/                 Layer 1 — canonical identity runtime (authoritative)
  memory/               Layer 1 — episodic + AETHER + firewall + affect curves
  schema/               Layer 1 — compressed identity interpretations
  render/               Layer 2 — non-authoritative renderers (disposable output)
    templates/            template renderer (deterministic default)
    slm/                  tiny SLM renderer (stub)
    providers/            future cloud / llama.cpp / ollama adapters
  instrumentation/      observability (state trace, replay debugger)
  bridges/              Layer 3 — HTTP, FFI, stdio, web UI
  tests/                unit + integration
    replay/                V4 determinism + holography tests
  tools/                cartridge compilers, build_lm
  profiles/             character cartridges (was characters/)
  data/                 LM corpus sources
  docs/                 V4_ARCHITECTURE.md + PORTABILITY.md
```

## V4 new files

| File | Role |
| --- | --- |
| `render/render_backend.h` | RenderBackend interface contract + registry |
| `render/render_backend.c` | registry, `render_backend_default()`, `render_backends_init()` |
| `render/prompt_compiler.{h,c}` | deterministic state → structured constraints (not lore) |
| `render/templates/template_backend.c` | wraps v3.2 template path as a RenderBackend |
| `render/slm/slm_backend.c` | tiny SLM target (stub; provider TBD) |
| `memory/memory_firewall.{h,c}` | renderer contamination barrier |
| `memory/affect_curve.{h,c}` | salience-weighted decay + hysteresis + habituation + trait amp |
| `schema/schema_state.{h,c}` | 8-slot per-relation belief state |
| `tests/replay/v4_modules_test.c` | 28 assertions on the new subsystems |
| `tests/replay/replay_determinism_test.js` | 12 assertions on determinism + cross-backend holography |
| `docs/V4_ARCHITECTURE.md` | the long version |

## Build & run

```
cd PersonaConsole_v4
make                            # everything: libs, tools, profiles, cartridges
make host                       # persona_host binary
make v4_modules_run             # V4 subsystem unit tests
make v4_replay_run              # determinism + holography
make test kiki_test             # legacy v3.x sanity scripts
./build/repl profiles/pretorius/pretorius.cart user
./build/persona_host profiles/pretorius/pretorius.cart
```

Set `PE_RENDER_BACKEND=template` (default) or `slm` (stub → falls back
to template).  Identity state is invariant across both.

## Test summary

All v3.3 tests preserved.  V4 adds:

- `v4_modules_run` — 28 checks across firewall / schema / affect /
  registry / prompt compiler
- `v4_replay_run` — 12 checks: 6 cross-run + 6 cross-backend

`make` produces identical cartridge bytes to v3.3 — V4 is augmentation,
not rewrite.

## Engine agnosticism preserved

The engine binary still contains zero character-flavored strings or
hardcoded keyword checks.  V4 adds renderer + schema + firewall +
affect-curve + prompt-compiler subsystems on top of the existing
agnostic core — nothing in V4 references Pretorius, Kiki, or any
specific character data.

## What V4 is **not**

V4 is not a rewrite.  V4 is a continuity-preserving augmentation layer.
The existing deterministic engine remains canonical.  V4 adds:

- low-resource semantic rendering (optional)
- hardened state authority (firewall)
- schema-layer identity consolidation
- nonlinear affect persistence
- renderer abstraction
- replay validation

The system still functions offline, without an LLM, on low-end
hardware, with deterministic replay intact.

## v3.x notes preserved below for reference

(For the cognitive pipeline, cartridge file table, hardware budget,
REPL commands, and "Adding a new character" guide, see the v3.2 FIN
documentation in `docs/` and the inline comments in `core/engine.c`.
Those facts are unchanged — only the directory layout and the V4
augmentation layers are new.)
