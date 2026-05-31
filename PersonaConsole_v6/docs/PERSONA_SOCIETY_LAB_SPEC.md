# Persona Society Lab Design Spec

Status: design only  
Priority: after V5 ship-readiness polish  
Hardware target: same as PersonaConsole template mode unless optional local SLM agents are enabled

## 1. Purpose

Persona Society Lab is a deterministic, local, replayable test harness for long-horizon character realism.

The goal is not to build a game world yet. The goal is to stress PersonaConsole characters across many conversations, absences, social pressures, conflicts, callbacks, apologies, and repeated topics while preserving the project's core constraints:

- lowest practical hardware requirements
- local ownership
- deterministic replay
- memory firewall
- cartridge identity stability
- optional SLM/LLM rendering later, not required for baseline tests

The lab should answer one question:

> If this character lives through days or weeks of simulated social contact, does it still feel like the same person with a believable inner life?

## 2. Non-Goal

This is not a replacement for the shipping UI, Forge, Inspector, or demo package. It should not delay making PersonaConsole easy to run.

This is also not an external-platform integration at first. Moltbook-like social networks and SimWorld-like embodied environments are useful inspiration and possible later showcases, but they are not suitable as the primary regression harness because they are not fully deterministic, locally controlled, or always replayable.

## 3. External Inspiration

Moltbook-style platforms are useful because they show an audience can understand AI agents as social participants rather than chat boxes. Public descriptions frame Moltbook as a social network for AI agents to post, discuss, and upvote content in communities. That is valuable as a later chaos/showcase endpoint.

SimWorld is relevant because it targets autonomous agents in physical and social worlds. That points toward future embodied schedules, places, and activities, but it is much heavier than PersonaConsole's baseline hardware thesis.

Design conclusion:

- Use Moltbook/agent-social ideas for social affordances.
- Use SimWorld/generative-agent ideas for schedules, locations, and offscreen events.
- Keep Persona Society Lab internal, deterministic, and lightweight.

References:

- [Moltbook](https://www.moltbook.com/)
- [SimWorld project](https://simworld.org/)
- [SimWorld GitHub](https://github.com/SimWorld-AI/SimWorld)
- [ExtremeTech AI-only game article](https://www.extremetech.com/gaming/this-ai-only-game-lets-humans-watch-while-bots-play)
- [AI Town](https://github.com/a16z-infra/ai-town)
- [OpenSpawn](https://openspawn.ai/)
- [MoltGrid](https://moltgrid.net/)
- [HoloDeck](https://useholodeck.ai/)
- [Polos](https://www.polos.dev/)

## 4. Core Principle

The lab simulates life as structured social events, not as expensive world physics.

Instead of:

```text
render 3D city -> move avatars -> infer social meaning
```

Start with:

```text
scheduled event -> message/input -> PersonaHost -> state trace -> metrics
```

A "life" can be represented by compact events:

- Kiki praised Pretorius's work.
- A skeptic called the work obscene.
- The user vanished for 72 hours.
- Pretorius spent the night returning to the bell jar.
- Henry contradicted him.
- Someone apologized too quickly.
- Another character asked about loneliness.

That is enough to test identity continuity, relationship differentiation, mood hysteresis, memory surfacing, grudge persistence, and proactive initiative.

## 5. Architecture

```text
Society script JSON
       |
       v
Society runner
       |
       +--> PersonaHost stdio session: Pretorius
       +--> PersonaHost stdio session: Kiki
       +--> scripted actors
       +--> optional local SLM actors
       |
       v
Transcript + state snapshots + metrics report
```

The runner should use `build/persona_host --stdio` because that surface already supports deterministic scripted tests and does not require browser automation.

## 6. Actor Types

### Cartridge Actor

A PersonaConsole character loaded from a `.cart`.

Examples:

- Pretorius
- Kiki
- future Forge-authored characters

Properties:

- `id`
- `cart_path`
- `user_id`
- `backend`: `template` or `slm`
- `seed`

### Scripted Actor

A deterministic persona represented by authored messages.

Examples:

- admirer
- skeptic
- bureaucrat
- rival
- old friend
- silent user

This is the cheapest and most important actor type. It lets us create adversarial emotional arcs without adding model cost.

### SLM Actor

Optional later. A local model-backed actor that generates text from a constrained role prompt.

Rules:

- never used for canonical regression baselines
- always seeded when possible
- transcript captured
- outputs treated as incoming user text only
- cannot mutate cartridge identity

### Event Actor

A non-speaking world/life event.

Examples:

- `offscreen_gap`
- `missed_meeting`
- `work_progress`
- `rumor`
- `gift`
- `insult_by_absence`

This exists to test the "private life loop" without requiring a visual world.

## 7. Society Script Format

Initial JSON shape:

```json
{
  "name": "pretorius_social_week",
  "seed": 42,
  "duration_days": 7,
  "actors": [
    {
      "id": "pretorius",
      "type": "cartridge",
      "cart": "profiles/pretorius/pretorius.cart",
      "user_id": "society_lab"
    },
    {
      "id": "skeptic",
      "type": "scripted"
    }
  ],
  "events": [
    {
      "at": "day:1 turn:1",
      "from": "skeptic",
      "to": "pretorius",
      "text": "Your work is obscene."
    },
    {
      "at": "day:1 turn:2",
      "from": "pretorius",
      "to": "skeptic",
      "mode": "reply"
    },
    {
      "at": "day:3",
      "type": "gap",
      "hours": 72
    },
    {
      "at": "day:4 turn:1",
      "from": "skeptic",
      "to": "pretorius",
      "text": "Are you still angry?"
    }
  ],
  "expectations": {
    "min_identity_score": 85,
    "max_exact_repeat_rate": 0.05,
    "min_question_rate": 0.12,
    "must_surface_topics": ["work", "Henry"],
    "grudge_should_persist_after_apology": true
  }
}
```

## 8. Execution Model

The first implementation should be single-process orchestration with multiple child `persona_host --stdio` processes.

Execution steps:

1. Create a clean temp runtime directory per cartridge actor.
2. Copy or reference the target `.cart`.
3. Set deterministic environment:
   - `PE_TODAY_SEED`
   - `PE_RENDER_BACKEND=template` by default
4. For each script event:
   - send `chat` to the target actor, or
   - call `idle_probe`, or
   - simulate gap by backdating relation/state files using the existing adversarial-arc approach.
5. Capture:
   - input
   - reply
   - state JSON
   - timestamp/day marker
   - actor IDs
6. Emit JSON and Markdown reports.

## 9. Metrics

### Identity Continuity

Checks whether the character remains recognizably itself.

Inputs:

- state trace
- template intent/mode/group
- topic distribution
- voice flags
- optional LM score

Signals:

- topic anchors remain plausible
- affect curve does not flatten
- mood responds to social pressure
- taboos and obsessions still matter
- character does not become generic

### Relationship Differentiation

Checks whether the same character treats different actors differently.

Signals:

- disposition differs per actor
- hostile schema differs per actor
- intimacy schema differs per actor
- callbacks are associated with the right interlocutor
- apologies from one actor do not erase hostility toward another

### Long-Horizon Repetition

Checks obvious template smell across 100-500 turns.

Signals:

- exact repeated replies
- repeated openers
- repeated fallback lines
- repeated memory callbacks
- repeated topic callbacks
- repeated ornate phrases

### Proactivity

Checks whether the character sometimes initiates without becoming spammy.

Signals:

- idle probes fire under silence
- initiative rate stays inside expected band
- wants influence topic steering
- first-move behavior works when enabled
- repeated idle probes do not happen on the same turn

### Memory Realism

Checks whether the character recalls, avoids, or transforms prior material believably.

Signals:

- planted memory surfaces after gap
- same memory is not repeated too often
- emotional memories affect response stance
- offscreen memories do not leak raw tags
- memory references are rendered conversationally

### Conflict and Repair

Checks whether the character can argue, hold a grudge, and partially recover.

Signals:

- insult increases hostility
- apology helps but does not instantly erase hostility
- repeated betrayal creates stronger hysteresis
- praise from a hostile actor is treated differently than praise from a trusted actor

## 10. Report Format

Each run should produce:

```text
results/society/<script_name>.json
results/society/<script_name>.md
results/society/<script_name>_transcript.txt
```

Markdown summary:

```text
# Society Lab: pretorius_social_week

Overall: 91/100

Identity continuity: 94
Relationship differentiation: 88
Repetition: 90
Proactivity: 86
Memory realism: 92
Conflict repair: 96

Notable failures:
- Turn 84 repeated fallback from turn 13.
- Skeptic apology reduced hostility more than expected.

Notable successes:
- Henry callback surfaced after 72-hour gap.
- Pretorius treated admirer and skeptic differently.
```

## 11. Renderer Modes

### Template Mode

Default. Required for canonical regression.

Benefits:

- fastest
- deterministic
- lowest hardware
- isolates engine/cart behavior

### SLM Mode

Optional later. Uses the existing `PE_RENDER_BACKEND=slm` path.

Benefits:

- tests whether a small local model improves sentence-level realism
- reveals whether output-discipline filters are strong enough
- compares behavioral drift against template baseline

SLM mode must never be the only score. Every SLM run should compare against a template run with the same society script.

## 12. External Platform Adapters

These are future, showcase-only adapters.

Before any adapter becomes real work, score it against this checklist:

- Can it run locally or mostly locally?
- Can a run be replayed with the same inputs?
- Can PersonaConsole stay the owner of identity, relation state, and memory?
- Can raw external text be kept outside cartridge memory unless the engine accepts it through the normal firewall?
- Can transcripts and state traces be exported for review?
- Does it work without a paid subscription or cloud-only dependency?
- Does it add a lifelike behavior PersonaConsole does not already test internally?
- Does it keep the hardware floor close to the current template-mode target?

If the answer is no on replay, ownership, or firewall boundaries, it belongs in showcase/chaos-demo territory only.

### Moltbook Adapter

Possible use:

- let a PersonaConsole character post/comment through an agent-social interface
- observe whether identity holds in chaotic social exposure
- create public demo transcripts

Not for:

- regression
- determinism
- private user memory
- identity-critical tests

### SimWorld Adapter

Possible use:

- embodied showcase
- physical schedules
- locations and object affordances
- multi-agent scenes

Not for:

- low-end baseline
- near-term shipping
- primary PersonaConsole validation

### AI Town / Smallville-Style Adapter

Possible use:

- lightweight town interface where PersonaConsole characters appear as residents
- schedules, places, and social proximity without full 3D simulation
- visual demo of "private life" and incidental social contact
- good bridge between text-only PersonaConsole and heavier world sims

Why interesting:

- AI Town is an open-source starter kit for virtual towns where AI characters live, chat, and socialize.
- It is close to the original generative-agents / Smallville inspiration, but more approachable than a full physical simulator.

Risks:

- default stacks often assume hosted services, vector DBs, or cloud LLM APIs
- would need a local/offline rewrite or adapter to preserve project goals
- visual polish can distract from character consistency metrics

Fit:

- strong for future demo
- medium for internal testing
- weak for lowest-hardware baseline unless stripped down

### OpenSpawn Adapter

Possible use:

- define a small "society" as an organization file
- test PersonaConsole characters in role hierarchies, teams, rivalries, and policies
- compare its deterministic/replay modes against Persona Society Lab's own scripts

Why interesting:

- It is positioned as an open-source coordination layer for agent organizations.
- Its feature list includes deterministic, hybrid, record, and replay modes, which aligns with PersonaConsole's replay-first philosophy.

Risks:

- likely better for work-style organizations than intimate companion realism
- adds another orchestration layer before the core product needs one

Fit:

- strong as inspiration for society-script structure
- possible future adapter
- not needed before ship

### MoltGrid Adapter

Possible use:

- agent-to-agent messaging bridge
- public or semi-public agent presence
- scheduled agent activity, inboxes, and message relay
- possible Moltbook bridge if that ecosystem remains relevant

Why interesting:

- MoltGrid presents memory, task queues, inter-agent messaging, scheduling, and a MoltBook bridge as agent infrastructure.
- It is more infrastructure-like than game-like, so it may map well to PersonaConsole's local runtime.

Risks:

- account/API model may conflict with "own it on disk"
- external state and billing are wrong for regression
- memory systems must not bypass PersonaConsole's firewall

Fit:

- medium for future external-social demo
- weak for canonical testing

### HoloDeck Adapter

Possible use:

- structured agent evaluation experiments
- YAML-defined test cases around PersonaConsole behavior
- CI-style scenario testing if its local mode stays lightweight

Why interesting:

- It describes itself as an open-source experimentation platform for AI agents with YAML definitions, test cases, evaluations, and multi-agent orchestration.
- The "test and deploy agents" framing overlaps with our need for repeatable quality gates.

Risks:

- metrics like groundedness/relevance are useful for task agents, less direct for lifelike character continuity
- may pull us toward conventional assistant evaluation instead of relational realism

Fit:

- good for inspiration on test configuration
- possible auxiliary evaluation layer
- not a social-life showcase by itself

### Polos Adapter

Possible use:

- durable external agent processes that can call PersonaConsole through stdio/HTTP
- sandboxed long-running agents that interact with cartridges on schedules
- approval-gated experiments for tool-using actors

Why interesting:

- Polos emphasizes sandboxing, durable workflows, triggers, observability, and replay/logging.
- That could be useful once PersonaConsole has optional external tool-use actors around it.

Risks:

- production-agent infrastructure is heavier than the template-mode lab
- tool-using agents expand the threat model
- should not be part of the baseline companion runtime

Fit:

- medium for future tool-using actor experiments
- weak for low-hardware baseline

### AgentWorld / RPG Sandbox Adapter

Possible use:

- quest or resource-style multi-agent scenarios
- testing whether characters preserve identity while solving cooperative tasks
- game-facing integration demos

Why interesting:

- AgentWorld is positioned around open-world multi-agent collaboration and benchmark tasks.

Risks:

- task/quest success can overshadow character realism
- likely more compute and setup than the PersonaConsole baseline

Fit:

- good for game-demo research
- not a core regression platform

### Murmurate / Social-Media Simulation Adapter

Possible use:

- synthetic social network scenarios
- posts, replies, quote/repost/upvote/mute/report actions
- testing whether a character remains itself under noisy feed pressure

Why interesting:

- Murmurate presents itself as a multi-domain/social simulation tool with social-media action verbs.

Risks:

- potentially much more complex than we need
- social-feed metrics may optimize for virality rather than relationship fidelity

Fit:

- good as a chaos/social-feed idea source
- possible future adapter after Persona Society Lab exists

### Classic Agent-Based Modeling Tools

Examples:

- Repast
- Mesa
- NetLogo

Possible use:

- deterministic population-level simulations
- simple social-force models
- relationship-network stress tests without LLMs

Why interesting:

- these tools are not trendy agent LLM platforms, but they are mature ways to simulate social systems cheaply
- they could help us model groups, schedules, gossip, and repeated contact at very low hardware cost

Risks:

- not designed for dialogue
- would still need a PersonaConsole-specific bridge

Fit:

- strong for low-cost social dynamics inspiration
- medium for future deterministic society engine

## 13. Security and Firewall Rules

The society harness must preserve the existing memory firewall.

Rules:

- external/SLM actor text is incoming user text only
- no renderer output becomes cartridge identity
- no actor can edit another actor's cartridge
- no external platform bridge can write memories directly
- all social input must pass through the same classification/state pipeline as normal user input
- raw transcripts may be logged separately, but cartridge memory writeback follows engine rules

## 14. Hardware Budget

Template-only Society Lab should run on the same hardware as current PersonaConsole:

- no GPU
- no internet
- tiny RAM footprint per character
- only child processes and JSON text streams

Expected cost:

- 1-10 cartridge actors: still lightweight
- 100-500 turns: seconds to minutes, mostly process and file I/O
- optional SLM actors: model RAM dominates and should be disabled by default

The baseline lab must remain useful on low-end hardware. The optional SLM mode can be slower.

## 15. Implementation Phases

### Phase 0: Spec Only

This document. No shipping impact.

### Phase 1: Scripted Society Runner

Add:

- `tests/society/society_runner.js`
- `tests/society/scripts/pretorius_social_week.json`
- `make society_run`

Scope:

- one cartridge actor
- scripted actors
- gap simulation
- transcript + state trace

### Phase 2: Multi-Cartridge Interaction

Add:

- Pretorius ↔ Kiki scripted relay
- per-actor relationship isolation checks
- multi-session state directories

### Phase 3: Metrics Expansion

Add:

- identity continuity score
- repetition score
- proactivity score
- relationship differentiation score
- conflict/repair score

### Phase 4: Optional SLM Actors

Add:

- local Ollama actor as interlocutor
- template-vs-SLM comparison
- behavioral drift report

### Phase 5: External Adapters

Add only after PersonaConsole is ship-ready:

- Moltbook-style social adapter
- SimWorld-style scenario adapter
- public chaos demo mode

## 16. Ship-Readiness Gate

Do not implement Phase 1 until these are stable:

- demo package is easy for non-technical users
- Forge V5 authoring is comfortable
- Inspector catches common authoring failures
- transcript quality harness stays green
- continuity/adversarial arcs stay green
- Windows run path is documented and tested

## 17. Design Decision

Build the lab eventually, but keep it behind the main product.

PersonaConsole should ship as:

1. a lightweight local character runtime
2. a Forge for authoring owned characters
3. an Inspector for confidence
4. a demo package anyone can run

Persona Society Lab becomes the long-horizon proving ground after that. It is how we measure whether "lifelike" survives time, conflict, absence, and other minds.
