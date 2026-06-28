# V6 Web Client Integration

PersonaConsole's web chat is the reference client for using one engine binary
with many cartridges. The client is intentionally plain HTML, CSS, and
JavaScript so the same pattern can be copied into a game, toy, app, museum
kiosk, or local desktop shell without adding a framework.

## Core Rule

The client never assumes which character is loaded. It asks the host.

Required flow:

1. Start `persona_host` with any valid cartridge.
2. Call `GET /state`.
3. Read `name` and `profile_slug`.
4. If the user chooses another character, call `POST /load`.
5. Call `GET /state` again.
6. Do not accept chat input until the reported character matches the requested one.

This prevents a stale Pretorius session from being mistaken for Kiki, or any
future cartridge from inheriting another cartridge's live state.

## Endpoints

`GET /state`

Returns the active session state. Client code should treat `name` and
`profile_slug` as the authoritative loaded-character identity.

`POST /load`

Body:

```json
{"path":"profiles/kiki/kiki.cart"}
```

The host saves the current session, loads the requested cartridge, preserves
the active user id, and returns `{"ok":true}` on success. The client must still
verify with `GET /state` afterward.

`POST /chat`

Body:

```json
{"text":"Good evening."}
```

Returns the character reply and a fresh `state` object.

`POST /idle_probe`

Optional character-initiated turn. This is read-only: it does not process a
user input or commit memory. It should be disabled while `/load` is in flight.

`POST /discard_changes`

Reloads the current cartridge from disk without saving in-memory mutations.
This exists for safe import/editor flows. If a browser-side history import
fails halfway through, the client should call this method before letting the
session continue.

`POST /reset_runtime`

Clears the active cartridge's mutable runtime sidecars, reloads the same
cartridge, and preserves the active `user_id`. Use this when a tester wants a
fresh local session without manually deleting `memory.bin`, `state.bin`,
relations, open loops, or learned knowledge.

Canonical import endpoints:

- `POST /import_memory`
- `POST /import_relationship`
- `POST /import_open_loop`
- `POST /import_learned_knowledge`
- `POST /import_learned_edge`

These endpoints are flat on purpose. A client or tool can read a V6 bundle,
then apply each record through the host without touching `memory.bin`,
`learned_knowledge.bin`, relation sidecars, or `open_loops.bin` directly.
That keeps the C runtime in charge of authority, duplicate handling, topic
mapping, actor tagging, and correction semantics.

## Character Switching

The reference UI now has a character selector. On switch it:

- disables input,
- clears the visible transcript,
- calls `/load`,
- verifies `/state`,
- refreshes portrait and state cues,
- resets idle timers,
- re-enables input only after the active character is confirmed.

This is the pattern other clients should copy.

## Refresh Versus Fresh Session

Refreshing the page does not mean "start over." It reconnects to the current
local runtime.

That is intentional for continuity, but it can confuse testing if the browser
looks empty while the character is still carrying old state. The reference UI
now makes that explicit:

- if `turn_count > 0` on load, the transcript hint says the browser resumed an
  existing local session,
- `start fresh local session` calls `POST /reset_runtime`,
- the active cartridge remains loaded after reset,
- the local web actor identity is preserved.

## Memory Editing And Import

Runtime memory files are binary sidecars and should not be hand-edited:

- `memory.bin`
- `learned_knowledge.bin`
- `open_loops.bin`
- `speech_events.bin`
- `relation_dims` sidecars

Use cartridge authoring for starting memories. Forge can already edit/export
core memory seeds inside the cartridge. Learned knowledge is intentionally a
runtime sidecar governed by the learned-knowledge API and firewall rules, so a
future memory editor should write through a small validated tool rather than
editing bytes directly.

Safe current options:

- Edit core memory seeds in Forge or cartridge build code.
- Rebuild the cartridge.
- Let runtime conversations create episodic memory and learned knowledge.
- Use the V6 bundle importer or the web client's `import history bundle` action
  for transcript-derived history.
- Use tests or future tools that call the learned-knowledge API for corrections.

Recommended user workflow:

1. Give the long transcript to ChatGPT or another strong formatter.
2. Ask it for a PersonaConsole V6 bundle JSON.
3. In the web UI, choose `import history bundle`.
4. Ask a memory probe or resume an old topic in offline mode to verify the
   imported history changed behavior.

Do not let renderer prose, model output, or arbitrary text imports become
confirmed memory without the firewall and authority checks.

## Low-Hardware Constraint

The reference web UI is only a client. The Tier 0 runtime remains:

- local,
- template-first,
- no model required,
- no cloud required,
- no database,
- no vector store,
- no framework dependency.

Optional local/API models may improve phrasing, but the C runtime and cartridge
remain the identity and memory authority.
