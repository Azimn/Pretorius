# Memory Import Format V6

PersonaConsole can accept structured history and knowledge bundles created outside the low-resource runtime. This is for bootstrap imports, user-guided history reconstruction, world/NPC backstory seeding, and test planting. It is not the normal memory path for a running character.

Living characters create future and continuous memories through the C runtime. The import tool exists for the moment before play begins, or for controlled tests where a developer wants to plant years of background, relationships, lore, and learned facts without making the low-hardware engine digest raw chat logs.

The intended workflow is simple:

1. A user gives a long chat history to a frontier model or another offline analysis tool.
2. That tool returns a compact JSON bundle in this format.
3. `tools/import_memory_bundle.js` validates and stages the bundle for review.
4. A later Forge/runtime promotion step can commit approved records through the normal C authority and firewall paths.

The importer is deliberately a staging tool. It does not write `learned_knowledge.bin`, `open_loops.bin`, relation sidecars, or any other binary runtime file. That keeps the C engine as the memory authority and avoids letting imported prose bypass validation.

This distinction matters for long-running NPCs. A character may begin with years of history, a marriage, rivalries, old grudges, world lore, or knowledge learned before the first user conversation. Those can be imported as structured background. Once the character is alive, new memories should be created by the runtime from actual interactions, corrections, environmental events, and learned-knowledge promotions.

## Bundle Shape

```json
{
  "bundle_version": 1,
  "character_id": "alice",
  "generated_by": "claude-or-chatgpt",
  "generated_at": "2026-06-25T00:00:00Z",
  "source_description": "three years of chat exports",
  "records": {
    "core_memories": [],
    "episodic_memories": [],
    "learned_knowledge": [],
    "learned_knowledge_edges": [],
    "relationships": [],
    "open_loops": [],
    "attachment_bonds": []
  }
}
```

Only `bundle_version` and `records` are required. `bundle_version` must be `1`.

## Output Files

Run:

```powershell
node tools/import_memory_bundle.js profiles/kiki/kiki.cart path/to/bundle.json
```

The tool writes review files under the target character directory:

```text
profiles/kiki/import_pending/import_summary.json
profiles/kiki/import_pending/pending_memory_import.json
profiles/kiki/import_pending/pending_lk_import.json
profiles/kiki/import_pending/pending_relationship_import.json
profiles/kiki/import_pending/pending_open_loops_import.json
profiles/kiki/import_pending/pending_attachment_import.json
```

Use `--dry-run` to validate without writing anything:

```powershell
node tools/import_memory_bundle.js profiles/kiki/kiki.cart path/to/bundle.json --dry-run
```

The importer also writes `tmp/import_report.jsonl` during real imports. That file records rejections and warnings.

## Authority Rules

Imported learned knowledge is staged as `source_type: "imported"` and its `authority_rank` is capped at `60`. Imported records cannot outrank cartridge-authored knowledge or later user-confirmed corrections.

The importer rejects text that looks like renderer or prompt residue, including bracketed learned-memory tags and unresolved template placeholders. This prevents raw generated prose from sneaking in as canonical memory.

## Record Types

### Core Memories

Use for compact identity-shaping facts.

```json
{
  "topic_key": "origin",
  "summary": "Alice was built for a museum installation.",
  "salience": 90,
  "emotional_impact": 250
}
```

### Episodic Memories

Use for things that happened.

```json
{
  "topic_key": "rainy_roof",
  "actor_name": "Mira",
  "summary": "Mira and Alice watched rain from the roof and argued about stars.",
  "happened_at": "2025-11-03",
  "salience": 80,
  "emotional_impact": 350
}
```

### Learned Knowledge

Use for claims the character may later know or believe after review.

```json
{
  "topic_key": "plasma",
  "claim_text": "Plasma is ionized gas that conducts electricity.",
  "scope": "real_world",
  "status": "confirmed",
  "confidence": 740,
  "authority_rank": 50,
  "source_actor_name": "Mira",
  "evidence_ref": "chat_export_part_03"
}
```

Supported scopes mirror the C learned-knowledge module:

```text
real_world
cartridge_canon
simulation_world
actor_specific
relationship_specific
session_local
private_character_belief
```

Supported statuses:

```text
candidate
provisional
confirmed
corrected
disputed
deprecated
cartridge_authored
world_authored
```

### Learned Knowledge Edges

Use for correction, support, and evidence relationships between staged records.

```json
{
  "source_import_id": "lk_2",
  "relation_type": "corrects",
  "target_import_id": "lk_1",
  "confidence": 900,
  "weight": 1000
}
```

Supported relation types:

```text
corrects
contradicts
supports
derived_from
taught_by
belongs_to_scope
evidenced_by
used_in_response
related_to_actor
related_to_topic
```

### Relationships

Use for actor-specific relation dimensions. These are staged for review and are not written to `.dims` sidecars by the importer.

```json
{
  "actor_name": "Mira",
  "trust": 850,
  "threat": 100,
  "intimacy": 700,
  "resentment": 20,
  "admiration": 800
}
```

Missing dimensions default to neutral or zero depending on the dimension.

### Open Loops

Use for unresolved conversational business. These records keep `topic_key` as text so the C runtime can map it to cartridge topic IDs later.

```json
{
  "actor_name": "Mira",
  "topic_key": "stars",
  "desired_speech_act": "return_to",
  "urgency": 700,
  "note": "Mira asked whether Alice still believes the stars are signs."
}
```

### Attachment Bonds

Attachment bonds are staged only. V6 does not yet treat this as a committed runtime sidecar.

```json
{
  "actor_name": "Mira",
  "attachment_type": "friendship",
  "bond_strength": 760,
  "security": 650,
  "anxiety": 120,
  "avoidance": 80
}
```

This leaves room for future relationship import UX without smuggling a new attachment subsystem into the runtime.

## Prompt For Frontier Model Preformatting

Use a prompt like this with ChatGPT, Claude, Grok, or another strong model:

```text
Convert the attached chat history into a PersonaConsole V6 memory import bundle.
Return only valid JSON matching bundle_version 1.
Separate events that happened from claims the character learned.
Do not invent facts. Mark uncertain items as candidate or disputed.
Use short summaries. Remove raw dialogue unless needed as evidence_ref labels.
Keep relationship values conservative unless the history strongly supports them.
Never include prompt tags, template placeholders, or system messages as memory.
```

## Current Limits

This first pass stages imports for review. It does not commit data into binary runtime sidecars. That promotion step should be implemented in C or Forge so imported records pass through the same authority resolver, learned-knowledge graph, memory firewall, and topic mapping as normal runtime writes.

The import format is intentionally broad enough for NPCs that have already lived beyond their starting loops. A bundle can describe learned knowledge, changed relationships, unresolved business, and major life events. The runtime still decides what becomes active canonical memory.
