# PersonaConsole V6 — Developer Practice

This document is the **practice** half — Git workflow, environment, build
and test gates, cartridge artifact handling, and the pre-push checklist.
The **doctrine** half (non-negotiables, layer ownership, sidecar policy,
self-ledger contract, multi-dim relation, attention budget, typed
dissonance, recall modes, impression management, self-image guardian,
Society Lab as personhood regression) is in `V6_DOCTRINE.md`.

A change is acceptable only if it satisfies **both** the doctrine and
the practice below.

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
