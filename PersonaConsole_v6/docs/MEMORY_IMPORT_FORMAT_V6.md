# Memory Import Format V6

PersonaConsole can accept structured history and knowledge bundles created outside the low-resource runtime. This is for bootstrap imports, user-guided history reconstruction, world/NPC backstory seeding, and test planting. It is not the normal memory path for a running character.

Living characters create future and continuous memories through the C runtime. The import tool exists for the moment before play begins, or for controlled tests where a developer wants to plant years of background, relationships, lore, and learned facts without making the low-hardware engine digest raw chat logs.

The intended workflow is simple:

1. A user gives a long chat history to a frontier model or another offline analysis tool.
2. That tool returns a compact JSON bundle in this format.
3. `tools/import_memory_bundle.js` validates the bundle and can either stage it for review or apply it into canonical runtime sidecars.
4. The web client can also import the same JSON bundle directly through the host's canonical import endpoints.

The engine remains the authority. Even when a bundle is applied directly, the JSON is not turned into binary sidecars by the JavaScript tool or the browser. Instead, the importer calls the C host's import methods, which write through the same runtime memory and learned-knowledge paths the engine already uses.

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

Batch CLI apply:

```powershell
node tools/import_memory_bundle.js profiles/kiki/kiki.cart path/to/bundle.json --apply
```

Stage only:

```powershell
node tools/import_memory_bundle.js profiles/kiki/kiki.cart path/to/bundle.json
```

Stage/apply writes review files under the target character directory:

```text
profiles/kiki/import_pending/import_summary.json
profiles/kiki/import_pending/pending_memory_import.json
profiles/kiki/import_pending/pending_lk_import.json
profiles/kiki/import_pending/pending_relationship_import.json
profiles/kiki/import_pending/pending_open_loops_import.json
profiles/kiki/import_pending/pending_attachment_import.json
```

When `--apply` is used, the tool also writes:

```text
profiles/kiki/import_pending/import_apply_summary.json
```

and the runtime sidecars are updated through the host:

```text
profiles/kiki/memory.bin
profiles/kiki/learned_knowledge.bin
profiles/kiki/open_loops.bin
profiles/kiki/relations/*.bin
profiles/kiki/relations/*.dims
```

Use `--dry-run` to validate without writing anything:

```powershell
node tools/import_memory_bundle.js profiles/kiki/kiki.cart path/to/bundle.json --dry-run
```

The importer also writes `tmp/import_report.jsonl` during real imports. That file records rejections and warnings.

Direct browser workflow:

1. Open the local web chat.
2. Expand `Advanced`.
3. Click `import history bundle`.
4. Choose the JSON bundle produced by ChatGPT, Claude, or another formatter.
5. The browser reads the file locally and calls the host's canonical import methods.

If the import fails, the browser issues `discard_changes` so partial in-memory writes are reloaded from disk rather than lingering in session state.

## Authority Rules

Imported learned knowledge is staged as `source_type: "imported"` and its `authority_rank` is capped at `60`. Imported records cannot outrank cartridge-authored knowledge or later user-confirmed corrections.

The importer rejects text that looks like renderer or prompt residue, including bracketed learned-memory tags and unresolved template placeholders. This prevents raw generated prose from sneaking in as canonical memory.

Imported episodic/core memories may also carry:

```json
"pinned": true
```

Use this sparingly for milestone-grade memories you explicitly want the engine to favor in offline recall and consequence coupling.

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
  "emotional_impact": 350,
  "pinned": true
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

## Actor And Topic Matching

Two fields matter for making imported history feel alive rather than merely present:

- `actor_name`
  Use the same interlocutor id the engine will actually see at runtime. In the stock web UI, that is `You` unless you deliberately change the local user id in a custom client.
- `topic_key`
  Use cartridge topic vocabulary where possible. Imported episodic memory and learned knowledge can still load without a mapped topic id, but imported open loops only become active runtime open loops when their `topic_key` matches a cartridge topic/pattern vocabulary entry.

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
Mark only milestone-grade memories as pinned.
Use actor_name "You" for the main user unless I tell you a different runtime user id.
Prefer cartridge-relevant topic_key labels over raw prose categories.
Do not invent facts. Mark uncertain items as candidate or disputed.
Use short summaries. Remove raw dialogue unless needed as evidence_ref labels.
Keep relationship values conservative unless the history strongly supports them.
Never include prompt tags, template placeholders, or system messages as memory.
```

## Current Limits

This pass supports direct canonical import for:

- core memories
- episodic memories
- learned knowledge records
- learned knowledge correction/support edges
- relationship dimensions
- open loops when the topic maps to cartridge vocabulary

Attachment bonds remain staged/documentary only. They are not yet a committed runtime sidecar.

The import format is intentionally broad enough for NPCs that have already lived beyond their starting loops. A bundle can describe learned knowledge, changed relationships, unresolved business, and major life events. The runtime still decides what becomes active canonical memory.
