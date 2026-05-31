# PersonaConsole V5 — Runtime Doctrine and Developer Workflow

This document is two things at once: the **doctrine** that governs what may
and may not change in the persona runtime, and the **practice** that two
developers across two operating systems use to actually ship the work. The
doctrine sits on top because it is the harder part to get right; the practice
follows because it is the part you do every day.

A change is acceptable only if it satisfies both halves.

---

## Part I — Doctrine

### 1. Purpose and scope

PersonaConsole is a **deterministic, local-first synthetic-identity runtime**.
The cartridge and engine own the character; the renderer expresses the
character but never authors it. The goal is a portable, testable
social-emotional engine whose renderer can be swapped — template today,
small local model tomorrow, larger model later — without changing the
character's canonical self.

The doctrine below protects that goal. Anything below the doctrine, in
"Practice", is mechanical: how we run the build, share work between Linux
and Windows, and avoid Git drift.

### 2. The doctrine, summarized

1. **Layer 1 owns identity.** The cartridge plus the engine are the
   character. The renderer is a disposable surface.
2. **Memory firewall.** No model-authored facts ever become canonical
   memory. Engine-authored facts about the character's own communicative
   acts may.
3. **Behavioral holography.** Layer 1 state is a pure function of the
   five-tuple `(cartridge, seed, WAL, canonical inputs, canonical clock)`.
4. **Canonical time flows through one clock.** No behavior-relevant code
   reads the wall clock directly.
5. **Renderer non-authority.** Renderer output is disposable until audit
   passes; it never directly mutates Layer 1.
6. **Continuity governs sophistication.** Sophistication is welcome when
   it is deterministic, inspectable, testable, character-agnostic in
   engine code, and authored through cartridge or sidecar data.
7. **Realism must be stateful.** Improvements to isolated wording without
   a corresponding improvement to continuity, memory, relation, repair,
   or self-consistency are secondary.

A change that violates any of these is wrong even if it compiles, tests
green, and reads well.

### 3. Memory firewall — refined

The firewall blocks **model-authored truth**, not **engine-authored
communicative memory**. A character must be able to remember what *it
itself just did* in conversation, or it cannot be conversationally
accountable.

**Forbidden in canonical memory:**

- Raw renderer prose stored as fact.
- Renderer output treated as new world knowledge ("the model said X
  happened, so X happened").
- Memories whose `MemorySource` is `PE_SRC_RENDERER_OUTPUT`.

**Permitted in canonical memory** (engine-authored speech events):

- The selected speech act and response act for the turn.
- The target actor and target topic.
- The selected intent, rhetorical mode, stance, defense mode, repair mode.
- The template id or render id chosen.
- A hash of the rendered output (not the prose).
- The audit result and any audit repair actions.

These are structured engine facts about the engine's own behavior. They
let the character recall *I accused him*, *I refused to answer that*,
*I conceded the point*, *I changed the subject* — without inviting the
renderer to invent new world facts.

### 4. Determinism — the five-tuple

Layer 1 state is a pure function of:

```
(cartridge, seed, WAL, canonical inputs, canonical clock) -> Layer 1 state
```

All five must be pinned for a replay to be exact. **Inputs** are
exogenous: a different rendered surface can elicit a different next user
input, which legitimately changes future state. The renderer is allowed
to influence future state *only* through the user's next canonical input,
never by mutating Layer 1 directly.

Concretely:

- Randomness comes only from the seeded engine PRNG (`persona_rng_u32`).
  No `rand()`, no addresses, no uninitialized memory.
- All sorts that affect behavior or persisted bytes are **total orders**.
  Returning `0` on a tie is a bug — qsort is not stable.
- Engine hot-path arithmetic is **fixed-point integer**. Floats round
  differently across compilers and platforms.
- Endianness is little-endian-native for the current target. All
  serialized layouts assume that until a portability layer says otherwise.

### 5. Canonical time

No engine, memory, schema, or planner code reads the wall clock directly.
All canonical time enters Layer 1 through one interface:

```
pe_clock_now_ms()      // millisecond clock
pe_clock_now_s()       // second clock
pe_clock_today_key()   // YYYYMMDD packed key for day-bucketing
pe_clock_local_hour()  // 0..23 for time-of-day bands
```

A test or replay harness may pin the clock via `PE_CLOCK_OVERRIDE_MS`
(environment) or `pe_clock_set_override_ms()` / `pe_clock_advance_ms()`
(programmatic). When the clock is overridden, the day/hour helpers use
UTC so the result is independent of the host time zone.

Direct `time(NULL)` / `localtime_r` / `clock_gettime` is allowed only in:

- the engine clock implementation itself,
- non-canonical tooling (cartridge compilers, packagers, release scripts),
- logging that never feeds back into Layer 1,
- the RNG's fallback seed source when no cartridge seed is supplied.

Any behavior derived from time must be replayable from the WAL or an
explicit fixture. New time-derived behavior added without a clock-pinned
replay test does not ship.

### 6. Renderer non-authority

The renderer receives a read-only `RenderContext` and returns disposable
text. It may not mutate Layer 1 state under any circumstance. Even helpful
inferences from the renderer ("the user probably meant Henry") must go
through the engine's normal classification pipeline as if they were new
user input, not into canonical memory directly.

### 7. Renderer audit

Renderer output must pass audit before display. Audit is split into two
classes; the document is honest about which is which.

**Deterministic audits** (these *guarantee* a property):

- Output length within configured bounds.
- No illegal bracket tags or sentinel sequences leaked from constraints.
- No exact repetition of the last N rendered openings (configurable).
- The rendered output realizes the selected speech act class.
- If `repair_mode` is set, the output performs the indicated repair.

**Heuristic audits** (these *signal*; they do not prove):

- Invention detection — references to actors, places, events, or
  memories that do not appear in Layer 1.
- Voice flag alignment — abstract vs concrete, sardonic vs sincere, etc.
- Excessive or insufficient disfluency relative to fatigue/arousal.
- Tonal alignment with current discrete emotion.

On audit failure: one deterministic repair pass, then template fallback.
The renderer never gets to retry indefinitely. Audit results are written
into the engine-authored speech event for the turn, so a failed audit is
itself part of canonical memory.

### 8. Layer ownership

State lives in exactly one layer. The boundaries below are normative.

| Layer | Owns | Examples |
| --- | --- | --- |
| **Identity** | Authored, stable traits. Read-only at runtime. | Big Five, voice flags, obsessions, taboos, wants, formative memory seeds, attachment style, defense tendencies, ideal self, ought self, feared self, author-defined limits. |
| **NPCState** | Session and long-running internal state. | Mood, drives, fatigue, acute spike, suppression, fixation, surprise, prediction error, active goal, current intent, repair pressure, current discrete emotion, defense mode, slow drift counters. |
| **Relation** | Actor-specific state. | Disposition, trust, resentment, attachment response, theory-of-mind fields, conflict history, repair history, intimacy threshold, known name, actor id. |
| **MemoryStore** | Canonical remembered events. | Episodic memories, core memories, **actor-tagged events**, **structured speech events**, privacy thresholds, salience, emotion, topic, recall metadata. |
| **SchemaState** | Compressed beliefs about an interlocutor or relationship. | 8-slot per-relation belief state. |
| **UtterancePlan** | Per-turn, non-serialized speech plan. | Rhetorical mode, stance, target topic, callback memory, speech act, response act, defense mode, repair mode, disfluency level, verbosity, certainty, aggression, theatricality, hedging. |
| **Renderer** | Wording only. | Receives read-only RenderContext; produces disposable output; passes through audit before display. |

Cross-layer writes are not allowed. The renderer cannot write any layer.
The planner reads all of them but writes only `UtterancePlan`.

### 9. Psychological subsystem policy

Every new psychological module — appraisal, discrete emotion derivation,
mood-congruent recall, attachment, defense mechanisms, self-discrepancy,
long-horizon drift, conversational repair, disfluency — must satisfy
**all** of:

1. Engine-agnostic. Zero character names or hardcoded character data in
   `core/`, `memory/`, `schema/`, or `render/`.
2. Data-driven via cartridge fields or sidecars.
3. Deterministic. Fixed-point. Total-order. Seeded RNG.
4. Inspectable. Surfaced in the inner-loop display or state trace.
5. Replay-tested. A clock-pinned replay confirms the new state
   transition is deterministic.
6. Connected to behavior, memory, relation, or rendering constraints.
   "It computes a number that nothing reads" is not a feature.
7. Forbidden from reading raw renderer output as truth.

This is the doctrine slot for OCC appraisal, derived discrete emotions,
mood-congruent recall, formative-memory floors, spreading activation,
repair behavior, disfluency, attachment style, defense mechanisms,
self-discrepancy, and long-horizon drift. They land under this policy or
they do not land.

### 10. Sidecar and ABI policy

Cartridge struct capacities and on-disk layouts are part of the **binary
ABI**. Carelessly extending them is forbidden.

Rules:

1. Every serialized struct change requires a version bump.
2. New persistent state should land as a **sidecar** rather than a core
   layout change wherever possible.
3. Every sidecar has: magic, version, flags (optional/required), entry
   count, capacity, reserved zero-padding for future expansion.
4. Optional sidecars **soft-fail** when missing (engine continues with
   sensible defaults). Required cartridge sections **hard-fail** when
   corrupted.
5. The `pe_sidecar_validate()` helper performs the standard checks.
6. Migrations are deterministic and have replay tests.
7. The legacy-identity migration test is the ratchet; every new sidecar
   adds its own migration test.

The sidecar layer is how Phase 2+ realism modules persist new state
(speech events, actor index, long-arc drift, conflict history) without
touching the cartridge ABI.

### 11. Inspectability is an invariant

If you cannot see the state, you cannot tune it and you cannot trust it.

Every module added under the psychological policy must expose its state
in at least one of:

- the inner-loop display (web UI),
- the state trace (instrumentation),
- the per-turn debug output.

A new emotion, plan field, repair signal, or drift counter that is
invisible to the operator does not exist for product purposes. This is
not optional or "nice-to-have" — it is the difference between a system
the team can iterate on and a black box.

### 12. Society Lab readiness rules

The Society Lab is not just a future test harness — it is the quality
multiplier that turns "feels alive" into measurable progress. Modules
land on Society-Lab-ready terms:

- Actor-tagged memory is the prerequisite. Nothing relationship-specific
  ships until `MemoryNode` carries an actor id.
- Scripted believability trials run per actor pair, daily.
- Transcript diffs and relation-state deltas are produced as artifacts.
- Regressions in believability, repetition, repair, or relationship
  differentiation are surfaced as test failures.
- Template mode is the baseline. SLM mode is compared *against* template
  mode, never used as the sole score.

### 13. Cross-platform code rules

The runtime is C99 + POSIX. The supported toolchains are **Linux native
gcc** and **Windows Cygwin gcc** — both POSIX. Native MSVC is currently
not supported because it lacks the POSIX layer the engine needs
(`mmap`, `fcntl` locks, `usleep`).

Rules:

1. No `_WIN32` / `__linux__` / `__APPLE__` `#ifdef`s in current sources.
   Use only the POSIX interfaces listed in `PORTABILITY.md`.
2. No Linux-only APIs (`epoll`, `eventfd`, `<linux/*>`, `SO_REUSEPORT`,
   `accept4`). Use portable `select`/`poll`, plain BSD sockets.
3. Forward-slash paths, relative to the cartridge directory. No drive
   letters or absolute paths.
4. Future native-platform support is introduced behind a portability
   layer, not scattered `#ifdef`s, and is tested by the same
   determinism gate.

This is not a permanent ban on native Windows or macOS — it is the
current contract. When the portability layer is written, this section
gets a clean amendment, not a thousand ad-hoc forks.

---

## Part II — Practice

### 14. Environment

Same toolchain on both platforms — `gcc` + POSIX:

- **Linux** — native `gcc`/`cc`, `make`, `node` for the JS test harnesses.
- **Windows** — **Cygwin** with `gcc`, `make`, and `node`. Run all `make`
  commands from a Cygwin shell. The PowerShell release scripts prepend
  `C:\cygwin64\bin` to `PATH`.

### 15. Git workflow (this is what prevents "version mismatch")

**Use the shared remote, not zip handoffs.** The single biggest source
of pain has been exchanging zipped trees. Don't.

```
git fetch origin
git checkout <branch>
git pull --rebase origin <branch>

# ... work, build, gate ...

git add <specific files>
git commit -m "clear message"
git push origin <branch>
```

- One branch per task. Keep `main` clean; open a PR to merge.
- Pull before you start *and* before you push.
- Small, focused commits whose messages say *why*.
- Never force-push a shared branch without agreeing first.

**Line endings.** The repo's `.gitattributes` normalizes text to LF on
both platforms; only `*.ps1/*.cmd/*.bat` are CRLF. Cartridge binaries
are marked `binary` so EOL conversion can never corrupt them. After
pulling a new `.gitattributes`, run **once**:

```
git add --renormalize .
git commit -m "Normalize line endings"   # only if it lists changes
```

If you ever see a diff that says "every file changed", a `.gitattributes`
rule is missing — fix the rule, not the diff.

### 16. Build and test gates

**Every push must leave the suite green.** Run the gate before pushing:

```
make host             # build the runtime binary
make                  # libs, tools, profiles, cartridges
make runtime_gate     # preferred V5 alias; same dep list as v4_all_tests
```

`runtime_gate` (and its synonym `v5_gate`) is the required pre-push gate.
It includes all V4 deterministic tests plus V5 believability, transcript
quality, milestone catch-up, offscreen memory rendering, web presence,
and the clock-pinned replay. The legacy `v4_all_tests` target is kept as
an alias for backward compatibility.

Useful subsets while iterating:

```
make v4_modules_run             # engine subsystem unit tests
make v4_replay_run              # cross-run + cross-renderer holography
make v5_clock_replay_run        # clock-pinned determinism
make v5_believability_run       # "does it feel alive" trial
make v5_transcript_quality_run  # short-reply / anti-repeat quality
```

The Windows-only release gates (`make release_check`, `make demo_package`)
run PowerShell and must run on Windows; they are not part of
`runtime_gate`.

If a test fails: fix the root cause. Don't weaken the assertion to make
it pass. If an assertion is genuinely wrong, change it in its own commit
with an explanation.

### 17. Cartridges and binary artifacts

- Edit the source (`tools/compile_pretorius.c` etc.) — `make` regenerates
  `profiles/<char>/dialogue/*.bin`, `identity.bin`, and so on.
- **Commit the source change and the regenerated `*.bin` together** so
  source and artifact never disagree.
- **Never hand-edit a `.bin`/`.lm`/`.cart`** and never run text tools on
  them. They are packed-struct binary ABI.
- `*.cart` is a build output and is gitignored. Section `*.bin` files
  *are* tracked.
- Runtime state files (`state.bin`, `memory.bin`, `chapters.bin`,
  `reflections.bin`, `aether/*`) are written by the running host. Don't
  commit changes that came from a test run — `git checkout --` them.
- Cartridge struct capacities are part of the ABI. If you change a
  capacity, **all cartridges must be regenerated** (`make`).
- Future human-readable cartridge sources are allowed if they compile
  deterministically to the same binary ABI. Until then, the C compilers
  are the canonical authoring route.

### 18. Acceptable vs unacceptable changes (concrete examples)

**Acceptable:**

- Adding a new fixed-point counter to `NPCState` and a replay test that
  pins it.
- Adding a sidecar `speech_events.bin` with magic, version, validation,
  and a soft-fail load path.
- Routing a new behavior through `pe_clock_now_s()` instead of reading
  the wall clock.
- Tightening a `qsort` comparator's tie-break to a unique secondary key.
- Surfacing a new plan field in the inner-loop display.

**Unacceptable** (no matter how compelling the rationale):

- A `time(NULL)` call inside engine, memory, schema, or render code.
- A renderer that writes to `MemoryStore` directly.
- A new emotion module that produces a number nothing reads.
- A `#ifdef _WIN32` block in `core/` or `memory/`.
- A serialized struct change without a version bump.
- A `qsort` comparator that returns `0` on a tie.
- A psychological feature that lives in C strings hardcoded for one
  character.
- A new field with no debug visibility.

### 19. Pre-push checklist

Run down this list every time:

- [ ] Pulled and rebased on the latest of my branch.
- [ ] `make runtime_gate` ends in `--- V4 tests: green ---`.
- [ ] Touched a cartridge source? Regenerated `*.bin` and committed both.
- [ ] No stray runtime-state files or build outputs staged.
- [ ] Did this change alter canonical state? If yes — version bump or
      sidecar? Replay test?
- [ ] Did this change add a serialized field or sidecar? Validation
      added?
- [ ] Did this change use the canonical clock only? No new `time(NULL)`
      in canonical paths?
- [ ] Did this change preserve renderer non-authority?
- [ ] Did this change add debug visibility for any new state?
- [ ] Did this change keep character-specific content out of engine code?
- [ ] Did this change avoid raw renderer output as memory?
- [ ] Did this change preserve total-order scoring for any `qsort`?
- [ ] Commit message explains *why*.

When in doubt, prefer the change that keeps the character **stable and
identical across machines** over the one that's cleverer. Sophistication
is welcome, but it serves continuity — never the other way around.
