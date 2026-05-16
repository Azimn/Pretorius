# Cartridge Forge

A character-authoring tool for the **PersonaConsole** engine.

This is a completely separate application from the engine itself. Where
the engine sticks to Pentium-3-class hardware constraints, the Forge is
free to be a modern browser app — it just needs to **run on anything
with a browser**.

## How to use

1. Download `forge.html`.
2. Double-click it. It opens in your default browser.

That's it. No install, no account, no upload — the file you author stays
in your browser until you explicitly download it as a `character.json`.

Works in any modern browser on Chromebook, iPad, iPhone, Android tablet,
or any PC from the last decade.

## What you can do

### Start from an archetype

Pick one of the six starting templates — Warm Science Nerd, Theatrical
Recluse, Bubbly Pop Polymath, Stoic Detective, Caring Mentor, Sardonic
Wit. The archetype prefills the Big Five sliders, voice flags, address
slots, core memories, and a few stylistic phrases ("flourishes" and
"expansions") matching its register.

Tune any field. The live-preview sidebar regenerates a sample reply as
you tweak.

### Import chat history

Drop in a chat log file. The Forge parses it client-side (nothing is
uploaded anywhere) and extracts:

- Top words (likely obsessions)
- Average sentence length (verbosity)
- Punctuation patterns (em-dashes → metaphor flag, exclamations → mood
  bleed, etc.)

Click **Use this as a starting point** and the editor opens prefilled
with those signals. Refine, then export.

#### Supported formats

| Format | Status |
| --- | --- |
| ChatGPT data export (`conversations.json`) | ✓ |
| Generic JSONL (`{"role":...,"content":...}` per line) | ✓ |
| SillyTavern chat JSON / character card V2 | ✓ |
| Gemini Takeout | coming soon |
| Claude conversation export | coming soon |
| character.ai logs | coming soon |

### Export

Click **Download character.json** in the Export tab. You get a
self-contained JSON file describing the character:

```json
{
  "magic": "PERSONA_CHARACTER",
  "version": 1,
  "name": "Eleanor Vance",
  "identity": {
    "O": 90, "C": 50, "E": 70, "A": 80, "N": 25,
    "verbosity": 5,
    "voice_flags": ["METAPHOR","ALLOW_BLEED","ALLOW_CALLBACK","SELF_INTERRUPT"],
    "address_user_as": ["friend","you","darling","dear one"],
    "obsessions": ["physics","entropy","black holes","curiosity"],
    "taboos": ["cruelty"],
    "flourishes":  [" — fascinating, isn't it", " — like a quantum tunnel", " — beautiful, really", " — Sagan would have loved that"],
    "expansions":  [", which is wild", ", honestly", ", and I love that", ", it's a whole thing"],
    "core_memories": [
      { "text": "Watched Cosmos and fell for the stars",
        "v": 80, "a": 55, "d": 30 }
    ]
  },
  "drives":  [ ... 8 entries ... ],
  "banks":   [ ... 10 entries ... ]
}
```

## Wiring into the engine

The Forge produces **two** files from the Export tab:

1. `<name>.cart` — a runnable cartridge ready for the engine.
   Drop it next to your PersonaConsole engine binary and run:
   ```
   ./build/persona_host my_character.cart
   ```
   Open `http://127.0.0.1:7777/` in a browser and start talking.

2. `<name>.character.json` — the editable source-of-truth.  Save it,
   come back later, re-import to keep authoring.

The `.cart` is assembled entirely in your browser via byte-exact
serialization of the engine's struct layouts plus a generic dialogue
pack (topics, patterns, templates, fallbacks, goals).  Your authored
identity, drives, banks, flourishes, and core memories ride on top of
the generic dialogue layer.

## What's intentionally **not** in v1

- No login, no cloud sync, no account.
- No telemetry. Nothing leaves your browser.
- No WASM preview of the real engine yet — the live preview is a fast
  rule-based heuristic so the author can hear the register evolve. A
  WASM preview that runs the actual engine is on the roadmap.
- No image embedding for character portraits in the JSON — those live
  beside the cartridge as `portrait.png` per the engine's convention.

## Architecture

Single self-contained HTML file:
- HTML structure
- CSS (dark theme, mobile-responsive)
- Vanilla JavaScript (no frameworks, no build step, no transpiler)

If you can read HTML/CSS/JS, you can change anything in the tool.
