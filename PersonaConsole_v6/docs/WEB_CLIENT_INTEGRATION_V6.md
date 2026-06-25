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

## Memory Editing

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
- Use tests or future tools that call the learned-knowledge API for corrections.

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
