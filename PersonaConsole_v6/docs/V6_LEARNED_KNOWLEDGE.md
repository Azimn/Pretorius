# V6 Learned Knowledge

PersonaConsole separates four things that should not collapse into one blob:

- Episodic memory: what happened.
- Learned knowledge: what the character can use as a claim.
- Relationship state: how the character appraises an actor.
- World or lore state: what is true inside a setting or simulation.

The learned-knowledge subsystem is Component 1 of the long-running character architecture. It is a compact C-native graph memory layer for claims, corrections, scopes, sources, confidence, evidence, and offline retrieval. It is not a vector database, a memory palace, or an LLM transcript store.

## Sidecar

The canonical store is:

```text
learned_knowledge.bin
```

It uses the same sidecar header policy as the rest of V6:

- magic: `LKNW`
- version: `1`
- optional if missing
- bounded record capacity
- bounded edge capacity
- invalid or corrupt files fail closed into an empty learned store

A corrupt sidecar must not masquerade as valid knowledge.

## Record Shape

Each learned record stores:

- `record_id`
- `topic_key`
- `claim_text`
- `scope`
- `source_type`
- `source_actor_id`
- `source_actor_name`
- `source_tier`
- `authority_rank`
- `confidence`
- `status`
- `created_at`
- `updated_at`
- `last_used_at`
- `reinforcement_count`
- `contradiction_count`
- `correction_of_record_id`
- `evidence_ref`
- `domain_tag`
- reserved `locus_id` and `index_hint`

Strings are fixed-size. Runtime lookup does not require heap allocation, embeddings, a server, or vector search.

## Edges

The graph foundation is intentionally tiny. Edges support:

- `corrects`
- `contradicts`
- `supports`
- `derived_from`
- `taught_by`
- `belongs_to_scope`
- `evidenced_by`
- `used_in_response`
- `related_to_actor`
- `related_to_topic`

This is enough for correction chains, evidence links, actor relevance, and later behavior hooks without becoming a graph database.

## Authority

The resolver is deterministic.

- Cartridge-authored knowledge outranks learned claims in cartridge-canon scope.
- World-authored knowledge outranks rumors in simulation-world scope.
- User-confirmed corrections outrank model claims in real-world and user-taught scopes.
- Character corrections can outrank model claims when authored as character sources.
- Model claims are provisional unless confirmed.
- Actor-specific claims do not become global facts.
- Relationship-specific claims do not become world facts.
- Session-local claims do not survive as global knowledge unless promoted.
- Deprecated or corrected claims are not selected as final answers.
- Close scores are treated as ambiguous, so the character can express uncertainty.

## Write Policy

Learned knowledge is not written every turn.

Allowed write paths:

- provisional model claim
- user correction
- character correction
- cartridge-authored or world-authored claim
- reinforcement of an existing claim
- correction of an older claim
- dispute between claims
- evidence/support edge

Renderer prose cannot directly become confirmed knowledge. The renderer may trigger a provisional candidate through explicit engine policy, but user or world authority must promote or correct it.

## SLM-to-Candidate Write Path

The next write path should use the existing fields rather than adding schema:

1. The renderer produces a response.
2. Layer 1 extracts a possible factual claim only if the turn is in an allowed knowledge context, such as direct explanation, correction, or explicit teaching.
3. The hallucination firewall and render/lore audit inspect the claim before storage.
4. Passing model claims are written as learned records with `source_type=model`, `source_tier=slm` or `frontier`, `status=provisional`, modest `confidence`, and low `authority_rank`.
5. Failing claims are not written, except as rejected trace rows.
6. A later user, character, cartridge, or world authority can confirm, correct, dispute, or deprecate the candidate.
7. Promotion to confirmed knowledge requires a non-model authority or explicit project policy. The model alone does not confirm itself.

This follows the existing firewall pattern: generated text may become an inspectable candidate event, but it cannot directly rewrite identity or confirmed memory. The current schema already has the required status, confidence, authority, source, scope, correction, and evidence fields.

## Offline Retrieval

Template mode can resolve learned knowledge without an LLM. Retrieval uses:

- topic key
- scope
- actor relevance
- status
- confidence
- authority rank
- correction links
- reinforcement and contradiction counts

The first production probe is the electricity bridge:

1. Kiki asks a technical question in SLM mode.
2. The model gives a provisional wrong claim.
3. Kiki corrects it.
4. The correction is stored as learned knowledge.
5. The profile closes and reopens in template mode.
6. Offline Pretorius answers from the corrected claim, not the model claim.

Run:

```bash
make v6_knowledge_bridge_run
```

## Prompt Packet

Situation packet mode includes relevant learned records as grounded constraints:

- topic
- status
- confidence
- source
- scope
- claim

Corrected and deprecated records are not shown as live truth. Provisional records are labeled provisional. The renderer is told not to revive corrected claims.

## Audit

The audit path can reject model output that repeats a corrected high-authority claim. In the electricity probe, once Kiki corrects the model's positive-charge simplification, later renderer output repeating that corrected claim is treated as lore drift.

## Trace

Learned-knowledge operations write JSONL traces to:

```text
tmp/learned_knowledge/trace.jsonl
```

Trace rows include operation, topic, claim, scope, source, tier, status, authority, confidence, relation edge, winner, and reason. The directory is ignored by git.

## Future Behavior Hooks

This module is designed so the future believable-agent rhythm layer can consume claims and edges for:

- needs and drives
- routines
- cooldowns
- daily or scene ticks
- environmental affordances
- idle behavior
- motivated resurfacing
- open-loop relevance
- actor-specific recall
- relationship pressure

Those systems should consume learned records. They should not parse renderer prose.

## Memory Palace

Memory palace is deferred. The record reserves tiny `locus_id` and `index_hint` fields so spatial indexing can be A/B tested later. It should not become canonical unless it beats simpler tag and graph retrieval in tests.

## Known Limits

- The first engine integration recognizes only a narrow electricity bridge.
- Learned technical answer surfaces are still plain and should become cartridge-authored.
- The sidecar is compact, not exhaustive.
- There is no consolidation or forgetting policy yet beyond bounded capacity replacement.
- No external importer writes learned knowledge yet.
- More scopes and authority rules need scenario coverage.
