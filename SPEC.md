# PersonaProject — Specification (FIN snapshot)

A deterministic synthetic-personality system shipped as three independent
components, designed around the metaphor of a retro game console.

```
┌────────────────────────────────────────────────────────────────────┐
│  Component 1 — PersonaConsole (the engine, the "logic chip")       │
│  C99, no malloc in process_input, < 10 ms/turn, < 300 KB/character │
│                                                                    │
│  Component 2 — PersonaHost (the wrapper, the "console hardware")   │
│  HTTP server + stdio JSON + FFI shared lib + minimal web chat UI   │
│                                                                    │
│  Component 3 — CartridgeForge (the authoring tool, the "dev kit")  │
│  Single-file browser app, no install, runs on anything modern      │
└────────────────────────────────────────────────────────────────────┘
```

The three components are independently deployable. The engine is the only
piece bound by the Pentium-III-class hardware budget; the harness and the
forge are free to use modern facilities.

---

## 1. PersonaConsole engine

### 1.1 What it is

Deterministic single-threaded C99 runtime that loads a *cartridge* (a
character data file) and runs a turn-based cognitive pipeline. No heap
allocation in `process_input`. All math saturating fixed-point. Reproducible
byte-for-byte under the same input sequence + the same `today_seed`.

### 1.2 Subsystems (four static libraries)

| Library | Purpose | Approx. LOC |
| --- | --- | --- |
| `libpersona.a`  | Cognitive runtime — pipeline, plan layer, dialogue, memory, story, voice | ~3500 |
| `liblsh.a`      | 64-bit SimHash + k-means consolidation primitives | ~250 |
| `libplasticity.a` | Stupid-backoff n-gram LM + cartridge-borne template mutator | ~600 |
| `libaether.a`   | Long-term episodic store — multi-band LSH + LSM WAL | ~750 |

### 1.3 Per-turn pipeline

```
input
  → pe_prep_input           lowercase + char bitmap + LSH input_sig
  → pe_classify_input       pattern sweep w/ negation window; sets
                             state.last_matched_flags (intoxicant etc.)
  → pe_compute_surprise     compare actual vs. last turn's prediction
  → pe_update_user_model    EMA of speaker affect, belief_about_me,
                             knowledge level, engagement
  → pe_associative_recall   working memory (VAD+LSH+salience+recency
                             +disclosure-gate) + AETHER cold fallback
                             → eng->cold_scratch[]
  → pe_update_drives_from_input
  → pe_compute_mood         drives × data-driven mood_weights
  → pe_update_embodiment    intox (flag-driven), exhaustion, irritation,
                             fixation lock
  → pe_update_layered_affect baseline, acute_spike, suppression, obsession
  → pe_update_topic_momentum
  → pe_select_goal / pe_select_intent
                            (weighted intent arbitration; identity gravity)
  → pe_build_plan           UtterancePlan: mode / stance / cert /
                             verb / aggr / theat / hedge
  → pe_generate_response    score templates → pe_voice_rerank
                             (1-ply counterfactual lookahead) →
                             top-3 weighted sample → style transforms
                             (cartridge flourishes + expansions) →
                             mutator_expand_banks (cartridge banks) →
                             session-fatigue evasion check
  → dream prefix            first turn after ≥8 h gap
  → opportunistic AETHER consolidate (every 16 turns when WAL dirty)
  → persona_save            state.bin + memory.bin + chapters.bin +
                             relation + AETHER WAL/bucket files
```

### 1.4 Cartridge layout

Each character ships as a directory or a single `.cart` container file.

| File | Type | Size | Contents |
| --- | --- | --- | --- |
| `identity.bin` | `Identity` | ~1.8 KB | Big Five, voice flags, obsessions, taboos, address slots, name, core memory seeds, flourishes, expansions |
| `drives.bin` | `DriveTable` | 272 B | 8 drives × { baseline, decay, mood_weight, personality_weight[5], name } |
| `today.bin` | `TodayTable` | 964 B | Up to 16 daily mood-modifier states |
| `banks.bin` | `BankRegistry` | 6 KB | Up to 12 synonym banks × 10 entries × 48 bytes |
| `dialogue/patterns.bin` | `PatternTable` | 4.4 KB | Keyword → topic, emotion delta, input_class, side-effect flags |
| `dialogue/templates.bin` | `TemplateTable` | 56 KB | Text + intent + rhetorical mask + stance mask + thresholds |
| `dialogue/fallback.bin` | `FallbackTable` | 3.5 KB | 3-tier fallback lines |
| `dialogue/goals.bin` | `GoalTable` | 748 B | Drive-weight vectors per goal |
| `dialogue/topics.bin` | `TopicTable` | 2.2 KB | Topic graph w/ adjacency |
| `voice.lm` | n-gram LM | ~150 KB | Stupid-backoff character LM for register reranking |
| `portrait.{png,jpg,...,svg}` | image | varies | Optional, served by PersonaHost |
| **typical total** | | **~265 KB** | **< 300 KB hardware spec budget** |

### 1.5 .cart container format

```
PECartridgeHeader (~1.5 KB)
  magic            'CART'
  version          1
  entry_count      N
  header_size      sizeof(header)
  payload_size     sum of all section sizes
  payload_checksum FNV-1a of full payload
  entries[N]       { char name[48], offset, size, checksum }

[payload]          concatenated section bytes
```

Validation: `pe_cart_validate_file` re-computes the payload checksum and
refuses to open a corrupt cartridge. The `compile_cartridge` tool consumes a
small JSON manifest and packs the named `.bin` files plus an `LM ` section.

### 1.6 Per-turn runtime files (gitignored)

| File | Purpose |
| --- | --- |
| `state.bin` | `NPCState` — drives, mood, embodiment, affect, plan mirror, trace ring, prediction/surprise, voice instrumentation, environment counters |
| `memory.bin` | `MemoryStore` — episodic ring (50 nodes, MEM_CORE never decays), semantic blocks, short-term buffer, phrase usage |
| `chapters.bin` | `ChapterBook` — autobiographical chapters + dream state |
| `relations/<hash>.bin` | Per-interlocutor `Relation` w/ embedded UserModel |
| `aether/` | Long-term episodic store (4 band dirs × 256 bucket files + zone files + WAL + dirty bitmap) |

### 1.7 Cognitive features implemented (v3.x)

| Feature | Where |
| --- | --- |
| Plan layer (rhetorical mode × stance × cert/aggr/theat/hedge) | `src/plan.c` |
| Embodiment (intoxication, exhaustion, fixation) | `src/engine.c::pe_update_embodiment` |
| Layered affect (baseline, acute_spike, suppression, obsession_pressure) | `src/engine.c::pe_update_layered_affect` |
| Trace ring (64-turn ASCII sparkline buffer) | `src/engine.c::pe_trace_push` |
| LM register reranker | `src/dialogue.c::score_template` + `libplasticity` |
| Cartridge-borne synonym banks | `src/mutator.c::mutator_expand_banks` |
| Theory of Mind (UserModel in Relation) | `src/user_model.c` |
| Predictive coding + surprise | `src/user_model.c::pe_predict_next_input` / `pe_compute_surprise` |
| SimHash-fused associative recall | `src/memory.c::pe_associative_recall` |
| Autobiographical chapters + dream recall | `src/story.c` |
| The Voice — 1-ply counterfactual rerank | `src/voice.c::pe_voice_rerank` |
| AETHER long-term episodic (multi-band LSH) | `src/aether*.c` |
| Probabilistic memory persistence (retrieval_prob, MEM_CORE/EPISODIC/CACHE) | `src/memory.c::pe_decay_episodic` |
| Weighted intent arbitration | `src/intent.c::intent_recompute` |
| Identity gravity (rolling drift detection) | `src/identity.c::identity_update` |
| Environmental cognition (session count, repeated questions, {placeholders}) | `src/environment.c` |
| Controlled imperfection (long-session evasive shorts) | `src/dialogue.c` |
| Arousal-modulated decay | `src/memory.c::pe_decay_episodic` |

### 1.8 Determinism

Same `today_seed` + same input sequence + same cartridge ⇒ byte-identical
output across replays. Every RNG path is `xorshift32` seeded from
`(today_seed XOR turn_count * 2654435761u)`. The voice rerank, mutator
expansion, plan building, and AETHER promotion are pure functions of state.

### 1.9 Test coverage

| Suite | Assertions | What it covers |
| --- | --- | --- |
| `make test`           | qualitative | 8-turn Pretorius sanity + dumps (runs through `.cart`) |
| `make kiki_test`      | qualitative | 8-turn Kiki sanity + dumps |
| `make lsh_test`       | 12 | SimHash, Hamming, k-means, co-occurrence rules |
| `make plasticity_run` | 14 | LM scoring monotonicity, mutator gating |
| `make aether_run`     | 19 | AETHER standalone — open / put / query / consolidate / scale |
| `make aether_pe_run`  | 12 | End-to-end: demote on eviction, cold scratch, sentinel indices |
| `make voice_run`      | 12 | Counterfactual rerank: goal alignment, hostile damping, determinism |
| `make host_run`       | 12 | FFI surface: ps_open / ps_reply / ps_state / ps_load (hot-swap) |

All 93 hard assertions pass plus two qualitative sanity scripts at the FIN
snapshot.

---

## 2. PersonaHost — the harness

### 2.1 Roles

One binary, three transports, all sharing the same dispatch table:

| Transport | When to use |
| --- | --- |
| **HTTP/JSON on :7777** | Default. Browser-based chat UI, any HTTP client, curl |
| **JSON-over-stdio** (`--stdio`) | Game engines or scripts that prefer subprocess control over FFI |
| **C ABI via `libpersona_host.so`** | Unity P/Invoke, Unreal third-party module, Godot GDExtension |

### 2.2 Public C ABI (the FFI surface)

```c
typedef struct PersonaSession PersonaSession;  /* opaque handle */

PersonaSession* ps_open(const char *cartridge_path);   /* .cart or dir */
void            ps_close(PersonaSession *s);
int             ps_set_user(PersonaSession *s, const char *user_id);
int             ps_reply(PersonaSession *s,
                         const char *input,
                         char *out_buf, int out_buf_size);
int             ps_save(PersonaSession *s);
int             ps_load(PersonaSession *s, const char *new_path);   /* hot-swap */
int             ps_state(PersonaSession *s, char *out_buf, int out_buf_size);
int             ps_name (PersonaSession *s, char *out_buf, int out_buf_size);
int             ps_char_dir(PersonaSession *s, char *out_buf, int out_buf_size);
```

Caller-allocated output buffers throughout; no cross-boundary frees. Maps
cleanly to Unity P/Invoke, Unreal's `ModuleType.External`, and Godot's
GDExtension as documented in the engine integration research.

### 2.3 HTTP routes

| Route | Body | Returns |
| --- | --- | --- |
| `GET  /` | — | `index.html` |
| `GET  /web/<file>` | — | static asset (css, js, …) |
| `GET  /state` | — | full session JSON snapshot |
| `GET  /portrait` | — | first hit among `portrait.{png,jpg,jpeg,webp,svg,gif}` in `<char_dir>/` |
| `POST /chat` | `{"text":...}` | `{"reply":"…","state":{…}}` |
| `POST /save` | — | `{"ok":true}` |
| `POST /load` | `{"path":...}` | hot-swap cartridge |
| `POST /set_user` | `{"user_id":...}` | bind interlocutor |

Stdio mode accepts one JSON line per request with a `"method"` field; same
contract, different transport.

### 2.4 Web UI

`host/web/` ships a placeholder chat interface — character portrait box
(falls back to first-letter initial when no portrait file is present), name
+ today label, sidebar showing mood/intent/mode/turn/intox/exhaust/voice_delta/
disposition, chat bubble history, mobile-responsive at < 700 px. Vanilla
HTML/CSS/JS, no framework, no build step. Override with `--web-root DIR`.

### 2.5 CLI

```
persona_host [--port N] [--stdio] [--web-root DIR] <cartridge_path>

  cartridge_path: directory or .cart file
  default port:   7777
  default web:    host/web
```

### 2.6 Game-engine FFI patterns (documented; samples deferred)

| Engine | Pattern | Boilerplate |
| --- | --- | --- |
| Unity 2022+ | P/Invoke against `libpersona_host.{dll,so,dylib}` | ~20 lines of C# `[DllImport]` declarations + 1 MonoBehaviour wrapper |
| Unreal Engine 5 | Third-party plugin (`ModuleType.External`) | `.Build.cs` + thin actor wrapper |
| Godot 4 | GDExtension | `.gdextension` config + thin GDScript wrapper |

Caller-allocated output buffer pattern is uniform across all three; that's
what the public ABI was designed for.

---

## 3. CartridgeForge — the authoring tool

### 3.1 What it is

A character-authoring app that runs in any modern browser, on any device.
**Single self-contained HTML file** (`forge.html`, ~54 KB). No install, no
account, no upload. Authoring is entirely local — the only data that leaves
the device is the `character.json` you choose to download.

### 3.2 Authoring paths

| Path | UX |
| --- | --- |
| **Start from an archetype** | Pick one of six presets (Warm Science Nerd, Theatrical Recluse, Bubbly Pop Polymath, Stoic Detective, Caring Mentor, Sardonic Wit) or a blank slate |
| **Import chat history** | Drag-and-drop a chat log file; the forge parses client-side and extracts vocabulary, sentence rhythm, and punctuation patterns to seed the editor |

### 3.3 Import format support

| Format | Status |
| --- | --- |
| ChatGPT data export (`conversations.json`, mapping-tree walker) | ✓ shipped |
| Generic JSONL (`{"role":...,"content":...}` per line) | ✓ shipped |
| SillyTavern chat JSON / character card V2 | ✓ shipped |
| Gemini Takeout | coming soon |
| Claude conversation export | coming soon |
| character.ai logs | coming soon |

### 3.4 Editor tabs

| Tab | Fields |
| --- | --- |
| Identity | name; Big Five sliders; verbosity; 4 address slots (warmer slots labelled as gated by disposition ≥ 700 + 30-day relationship) |
| Voice | 9 voice-flag checkboxes (each with one-line description); 4 flourish + 4 expansion phrases |
| Drives | baseline + mood-weight sliders for all 8 drives |
| Memories | up to 8 core memories with VAD sliders; obsessions + taboos as tag inputs |
| Banks | 10 synonym banks (one textarea each, one entry per line, max 10) |
| Export | download `character.json` or copy to clipboard |

A live-preview sidebar regenerates a heuristic sample reply each time a
slider moves — fast feedback while authoring.

### 3.5 Export schema (`character.json`)

```json
{
  "magic":   "PERSONA_CHARACTER",
  "version": 1,
  "name":    "Eleanor Vance",
  "identity": {
    "O": 90, "C": 50, "E": 70, "A": 80, "N": 25,
    "verbosity": 5,
    "voice_flags": ["METAPHOR","ALLOW_BLEED","ALLOW_CALLBACK","SELF_INTERRUPT"],
    "address_user_as": ["friend","you","darling","dear one"],
    "obsessions": ["physics","entropy","black holes","curiosity"],
    "taboos":     ["cruelty"],
    "flourishes": [" — fascinating, isn't it", ...],
    "expansions": [", which is wild", ...],
    "core_memories": [{ "text": "...", "v": 80, "a": 55, "d": 30 }]
  },
  "drives":  [ /* 8 entries */ ],
  "banks":   [ /* 10 entries */ ]
}
```

This is the authoring source of truth. It's *not* yet runnable by the
engine on its own — a small `json_to_cart` bridge tool is required to
convert it into the engine's per-section `.bin` files (which
`compile_cartridge` then packs into a `.cart`). That bridge is the
biggest known gap in the FIN snapshot — see §5.

---

## 4. Build pipeline

```
data/corpus_<name>.txt           ─┐
                                  │  build_lm
                                  ▼
                  characters/<name>/voice.lm  ─┐
                                                │
  tools/compile_<name>.c          ─┐            │
                                  │  cc          │
                                  ▼              │
                  build/compile_<name>           │
                                  │              │
                                  ▼              │
  characters/<name>/identity.bin                 │
  characters/<name>/drives.bin                   │
  characters/<name>/today.bin                    │  ─┐
  characters/<name>/banks.bin                    │   │
  characters/<name>/dialogue/*.bin               │   │ compile_cartridge
                                                  │   │
  characters/<name>/manifest.json   ─────────────┘   │
                                                      ▼
                  characters/<name>/<name>.cart
                                  │
                                  ▼
            ./build/persona_host  →  HTTP / stdio / FFI
```

`make` builds the four engine libraries, both character compilers, the LM
builder, the cartridge packer, the REPL, the PersonaHost binary, and the
shared `libpersona_host.so`. Then it generates both `.lm`s and both
`.cart`s.

`make host` and `make host_run` are the harness-specific shortcuts.

---

## 5. Known gaps at FIN

1. **`character.json` → `.cart` bridge** is not yet implemented. The Forge
   produces a runnable-by-no-one JSON until a small `tools/json_to_cart`
   utility lands in the engine repo. This is the highest-leverage next step
   — without it the Forge is decoupled from the engine.
2. **Per-character compile tools** still exist as C source (one per
   character). Adding a new character requires writing `tools/compile_<name>.c`
   alongside a manifest. Closing this is the same work as gap #1: a
   data-driven authoring step that consumes `character.json`.
3. **Real portrait images** are placeholder SVGs. The `/portrait` route
   probes PNG/JPG before SVG; drop a real image into the character
   directory and it just appears.
4. **Game-engine FFI samples** (Unity / Unreal / Godot) are documented but
   not yet shipped. `libpersona_host.so` is built and ready for them.
5. **WASM preview** in the Forge. The current live preview is heuristic; a
   real WASM build of the engine for in-Forge previews is on the roadmap.
6. **Additional log-import parsers** for the Forge (Gemini Takeout, Claude
   export, character.ai). Format research is done; implementation is
   straightforward.
7. **Portability layer** (`platform.h`) abstracting POSIX calls. Currently
   POSIX-only by design; a Windows port would need an mmap/socket shim.
   Documented in `PORTABILITY.md`.

---

## 6. Files at a glance

```
PersonaProject/
├── PersonaConsole_c99/         ← Component 1 + 2 (engine + harness)
│   ├── CLAUDE.md                 project orientation
│   ├── PORTABILITY.md            POSIX dependencies + planned shim
│   ├── Makefile                  4 libs + cartridge packer + host + REPL
│   ├── include/                  public + internal headers
│   ├── src/                      engine + LSH + plasticity + AETHER (24 .c)
│   ├── host/                     PersonaHost — FFI + HTTP + JSON + web UI
│   ├── tools/                    compile_pretorius/kiki + compile_cartridge + build_lm
│   ├── test/                     8 test suites
│   ├── data/                     bootstrap corpora
│   └── characters/{pretorius,kiki}/
│       ├── identity.bin, drives.bin, today.bin, banks.bin
│       ├── dialogue/{patterns,templates,fallback,topics,goals}.bin
│       ├── voice.lm
│       ├── manifest.json
│       ├── portrait.svg          placeholder; drop a PNG to override
│       └── <name>.cart           packed cartridge
└── CartridgeForge/             ← Component 3 (authoring tool)
    ├── forge.html                single-file SPA, double-click to open
    └── README.md
```

---

## 7. Versioning convention

Snapshots are labelled `FIN<N>` (FIN1 through FIN4 in the engine repo,
`PersonaProject_FIN` for the combined bundle). The engine itself goes
through internal version numbers (v3.2 was the last engine-only label).

Determinism is preserved across snapshots only for code paths that don't
change struct layouts. The `.cart` container has its own `version` field
(currently 1) and refuses to load mismatched versions.
