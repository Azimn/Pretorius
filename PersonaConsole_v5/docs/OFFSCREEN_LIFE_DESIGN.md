# Offscreen Life And Internal Agency

PersonaConsole V5 treats the user as one relationship in the character's life, not the character's whole world. The implementation is intentionally small and deterministic: no external tools, no background service, no network calls, and no hidden LLM work while the user is away.

## Current Contract

The offscreen tick runs on the first chat turn of a session when the saved relation has a real elapsed gap.

- Gaps under 6 hours do nothing.
- Gaps over 6 hours age authored wants by elapsed hours.
- The most neglected high-intensity want wins the tick.
- A current preoccupation is chosen deterministically from the cartridge.
- A compact `[offscreen]` memory is committed through the system-ticket path.
- The selected want's age resets to zero.
- The selected topic receives momentum so the next reply can naturally steer toward it.
- A resumption line is queued so the user can hear that the character existed between visits.

This creates the illusion of private life without running any process while the app is closed.

## What Is Persisted

The runtime state lives in `state.bin`; relation-specific timing lives in `relations/*.bin`; per-user schemas live in sibling `.schema` files.

Important V5 fields:

- `want_turns_since_engaged[]`
- `resumption_pending`
- `unresolved_threads[]`
- `unresolved_count`
- `turns_since_question`
- `last_reply_had_question`
- `milestones_seen`

The regression target is `make v5_save_state_run`. It verifies that offscreen want ages, unresolved-thread counters, and conversation-rhythm fields survive host restart.

## Why It Is Not External Agency

This is internal agency only. The character does not browse, post, email, call tools, read files, spend money, or affect the user's machine beyond its own local cartridge state. It only updates symbolic self-state and speaks.

That distinction matters for the product:

- keeps hardware requirements tiny
- preserves deterministic replay
- avoids a broader security model
- protects the user's ownership of the character
- keeps the "old-school cartridge" claim honest

## Future Extension Points

Good next steps should stay in the same pattern:

- more authored offscreen event families per want
- per-character cadence controls for how often offscreen life surfaces
- relationship-specific offscreen reflections without leaking one user into another user's memory
- multi-character simulation harnesses that run as explicit tests, not hidden runtime behavior
- optional local SLM rendering after the deterministic tick has already updated Layer 1 state

Avoid adding always-on daemons or tool-use loops until there is a separate safety and UX design for them. For now, the character's private life is simulated at the moment of return, which is cheap, understandable, and replayable.
