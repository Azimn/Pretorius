# Cartridge Forge

A character-authoring tool for the PersonaConsole engine.

This is a completely separate application from the engine itself. Where
the engine sticks to low-resource runtime constraints, the Forge is free
to be a modern browser app. It still runs as a single local HTML file:
no install, no account, no telemetry, and no upload.

## How To Use

1. Download `forge.html`.
2. Double-click it. It opens in your default browser.
3. Author the character.
4. Export a runnable `.cart`, an optional bundle, or the editable
   `character.json` source.

Everything stays in your browser until you explicitly download it.

## What You Can Do

### Start From An Archetype

Pick one of the starting templates. The archetype prefills the Big Five
sliders, voice flags, address slots, core memories, V5 proactive
presence fields, and a few stylistic phrases matching its register.

Tune any field. The live-preview sidebar regenerates a sample reply as
you tweak.

### Author V5 Internal Life

The Forge now writes the V5 proactivity surface into the cartridge:

- Wants: internal goals that age when neglected and steer initiative.
- Current preoccupations: topics the character can return to when idle
  or offscreen.
- Resumption lines: gap-specific greetings after absences.
- Relationship milestones: lines that fire as the relationship ages.

These fields are part of what prevents Forge-built characters from being
"V5-inert." Exported carts are validated by the engine-side
`cartridge_lint` test.

### Import Chat History

Drop in a chat log file. The Forge parses it client-side and extracts:

- Top words as likely obsessions
- Average sentence length as verbosity
- Punctuation patterns as voice-style hints

Click "Use this as a starting point" and the editor opens prefilled with
those signals. Refine, then export.

### Supported Formats

| Format | Status |
| --- | --- |
| ChatGPT data export (`conversations.json`) | supported |
| Generic JSONL (`{"role":...,"content":...}` per line) | supported |
| SillyTavern chat JSON / character card V2 | supported |
| Claude conversation export | supported |
| Gemini API history | supported |
| Gemini Takeout activity | supported |
| character.ai logs | supported |

## Export

The Export tab can produce:

1. `<name>.cart`: a runnable cartridge ready for the engine.
2. `<name>.zip`: a bundle containing the cart plus prepopulated AETHER
   memory data when deep memories exist.
3. `<name>.character.json`: the editable source-of-truth for later
   re-import.

The `.cart` is assembled entirely in your browser via byte-exact
serialization of the engine's V5 struct layouts plus a generic dialogue
pack. Your authored identity, drives, banks, flourishes, core memories,
wants, preoccupations, resumption lines, and milestones ride on top of
that generic dialogue layer.

## Wiring Into The Engine

Drop the exported cart next to your PersonaConsole engine binary and run:

```sh
./build/persona_host my_character.cart
```

Then open `http://127.0.0.1:7777/` in a browser and start talking.

## What's Intentionally Not In The Forge

- No login, cloud sync, account, telemetry, or subscription.
- No WASM preview of the real engine yet. The live preview is a fast
  rule-based heuristic so the author can hear the register evolve.
- No image embedding for character portraits in the JSON. Portraits
  should live beside the cartridge as `portrait.png` per convention.

## Architecture

Single self-contained HTML file:

- HTML structure
- CSS
- Vanilla JavaScript

If you can read HTML, CSS, and JavaScript, you can change anything in
the tool.
