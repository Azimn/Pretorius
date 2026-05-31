# Persona Console V6 — CLAUDE.md

> **V6 thesis.** The cartridge defines who the character is; the V6 engine
> keeps the character *being that person* under contradiction, time, absence,
> and other minds.

V6 is the **bounded continuity** release. It builds on V5's deterministic
local character runtime and the V4 invariants beneath it, and adds the
machinery of selfhood: a structured autobiographical ledger, a multi-
dimensional relational frame per actor, bounded attention, typed
dissonance, recall modes, impression management, and a self-image the
engine actively defends.

The full constitution is in `docs/V6_DOCTRINE.md`. The practice (git
workflow, build, line endings, test gates) is in `docs/DEV_WORKFLOW.md`,
shared with V5. The V5→V6 migration story is in `docs/MIGRATION_V5_TO_V6.md`.

## The seven non-negotiables

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
   passes; never directly mutates Layer 1.
6. **Continuity governs sophistication.** Sophistication is welcome when
   it is deterministic, inspectable, testable, character-agnostic in
   engine code, and authored through cartridge or sidecar data.
7. **Selfhood is engineered, not generated.** Accountability, attention,
   dissonance, defenses, and self-image are structured engine state —
   never paraphrases, never LLM monologues.

## V6 directory layout

```
PersonaConsole_v6/
  core/                 Layer 1 — canonical identity runtime
  memory/               Layer 1 — episodic + AETHER + firewall + affect +
                          sidecars (actor index, speech events,
                          contradictions, open loops, habits, long arc)
  schema/               Layer 1 — compressed identity interpretations
  render/               Layer 2 — non-authoritative renderers
    templates/            template renderer (deterministic default)
    slm/                  optional small local model renderer (stub)
    providers/            llama.cpp / ollama adapters
  instrumentation/      state trace, replay debugger
  bridges/              HTTP, FFI, stdio, web UI
  tests/                unit + integration
    replay/                determinism + sidecar replay tests
  tools/                cartridge compilers, build_lm
  profiles/             character cartridges
  data/                 LM corpus sources
  docs/                 V6_DOCTRINE.md, DEV_WORKFLOW.md,
                          MIGRATION_V5_TO_V6.md, PORTABILITY.md
```

## Build & run

```
cd PersonaConsole_v6
make                     # everything: libs, tools, profiles, cartridges
make host                # persona_host binary
make runtime_gate        # the required pre-push gate (alias for v5_gate)
make v5_clock_replay_run        # canonical clock determinism
make v5_actor_tagged_memory_run # actor-index sidecar replay
```

The `runtime_gate` target is the green gate every push must clear. It
includes V4 deterministic tests + V5 demo-quality tests + V6 sidecar
replay tests. New V6 modules add their replay test to the gate as they
land.

## V6 phase plan (active development)

V6 builds out beyond V5's Phase 1 (engine clock + sidecar scaffold) and
Phase 2 (actor-tagged memory) — both of which V6 inherits intact:

- **Phase 3: self-ledger / engine-authored speech events.** Sidecar
  `speech_events.bin`, with `withheld_intent` on day one. Audit gains the
  speech-act-realization check.
- **Phase 4: multi-dimensional Relation.** Sidecar `relation_dims.bin`
  per actor: trust, threat, intimacy, resentment, dependency, obligation,
  envy, admiration, embarrassment.
- **Phase 5: planner layers.** Attention budget, repair loop (first-
  class), typed dissonance (ideal/ought/feared), recall modes,
  impression management, refusal/withhold logging, private-thought frame.
- **Phase 6: persistent layers.** `open_loops.bin`, `habit_rules.bin`,
  contradiction ledger, long-arc drift (`long_arc.bin`).
- **Phase 7: capstone.** Self-image guardian — the canonical V6 turn
  shape that ties appraisal → dissonance → resolution → speech event →
  render under audit.
- **Continuous:** Society Lab as personhood regression, the believability
  battery (per-axis, not single score), Forge archetype scaffolds.

## What V6 is **not**

V6 is not a generative-agent framework. The LLM, if used at all, is a
renderer of words; identity lives in deterministic, inspectable,
replay-tested Layer 1. V6 is not a rewrite of V5: it inherits everything
V5 had and extends it. V5 cartridges load in V6 with defaults. The V5
demo package remains the lead's release vehicle while V6 evolves in
parallel.

## v3.x and v4.x notes preserved for reference

(For the cognitive pipeline, cartridge file table, hardware budget,
REPL commands, the V4 firewall design, and "Adding a new character"
guides, see `docs/V4_ARCHITECTURE.md` plus the v3.2 FIN documentation
referenced there, and the inline comments in `core/engine.c`. Those
facts remain accurate.)
