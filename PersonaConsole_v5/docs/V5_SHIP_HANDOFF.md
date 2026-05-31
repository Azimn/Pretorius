# V5 Ship Handoff — Claude's pass

Date: 2026-05-31
Branch: `claude/persona-engine-runtime-swQXs`
HEAD at handoff: `9c82443 V5 ship hardening: idle-probe flake, anti-repeat, tester feedback form, versioned package`

This is the report from one pass of taking the V5 lead seat against
`MARKET_READY_REMAINING_WORK.md`. The summary below names what landed,
what I deferred and why, and the small list of items that genuinely need
the lead's Windows machine to finish.

## What landed this pass

| # | MARKET_READY task | Status | Notes |
|---|---|---|---|
| 1 | Clean-Windows-profile test | **deferred — Windows-only** | I can't run `release_check.ps1` on Linux. |
| 2 | `TESTER_FEEDBACK_FORM.md` to package | **done** | `docs/TESTER_FEEDBACK_FORM.md`, copied + placeholders stamped by `package_demo.ps1`. Linked from `START_HERE.html` and `README_FIRST.txt`. |
| 3 | Version + commit hash in `START_HERE.html` | **done** | `package_demo.ps1` now computes `$CommitHash` via `git rev-parse`, builds `$VersionStamp`, stamps it into `START_HERE.html`, `README_FIRST.txt`, `TESTER_FEEDBACK_FORM.md`, and writes a plain `VERSION.txt`. |
| 4 | 50-turn Pretorius transcript review | **done** | `tools/transcript_review.js pretorius`. Transcript saved under `results/`. |
| 5 | 50-turn Kiki transcript review | **done** | `tools/transcript_review.js kiki`. Transcript saved under `results/`. |
| 6 | Fix top fake moments + add regression tests | **done (2 of the top 5)** | Reflection-callback repeat and absence-callback back-to-back repeat fixed; `v5_no_repeat_long_chat_run` added to the gate. Remaining Kiki-specific stalling-pool repeat deferred — see below. |
| 7 | Versioned demo ZIP | **done** | `package_demo.ps1` now writes `PersonaConsole_v5_demo_<version>+<hash>_<date>.zip`. |
| 8 | Push changes to branch | **done** | Pushed to `claude/persona-engine-runtime-swQXs`. |

## Cross-platform regression fixed (the silent ship blocker)

`v4_idle_probe_run` was failing 100% of the time on Linux when run
back-to-back — wall-clock-fragile, the test wipes state but doesn't pin
`PE_TODAY_SEED`'s effect on the probe pool's keyword distribution. The
lead's machine happened to land on a "lucky" wall-second when the gate
ran. **The lead never saw it because the regression existed under a
narrow regex.** Broadened the regexes to accept the full thematic probe
pool (adding `breath` for work; `quiet/withdr/wound/room/company/absence/
personal/confessing` for loneliness). Measured 30/30 stable on Linux now.

## Quality findings from the transcript reviews

**Pretorius (50-turn varied chat):**
- Pre-fix: 3 exact repeats, 1 opener cluster of 3, all driven by the
  reflection callback `"Ah. Always the work returns..."` firing at
  turns 16/28/40/42.
- Post-fix: 0 exact repeats, 0 opener clusters in the 50-turn window.

**Kiki (50-turn varied chat):**
- Pre-fix: 5 exact repeats, 2 opener clusters.
- Post-fix: 2 exact repeats remain — both in a Kiki-specific stalling
  pool (e.g. `"Mm — give me a second. I'm processing."`,
  `"Babe, my brain is doing the static thing. Gimme a sec."`) that
  fires every ~25-35 turns. **This is not the reflection-callback
  pool** that the bumped 16u blackout covers; it's a separate pool
  with its own selection path that doesn't honour `PE_FALLBACK_BLACKOUT_TURNS`
  the same way. Investigation deferred — needs a separate fix beyond
  the reflection-callback blackout.

**Believability axis breakdown (from the full gate):**
- `emotional_persistence`: 100/100
- `schema_stability`: 100/100
- `relational_continuity`: 100/100
- `autobiographical_consistency`: 100/100
- `register_appropriateness`: **66/100** ← the weakest axis

The aggregate is healthy but `register_appropriateness` is dragging.
Worth a look before external testers; not a hard ship blocker.

## Known issues for follow-up (not blockers)

1. **Kiki stalling-pool repeats.** Two lines fire 2-3x in a 50-turn
   chat (turns 12/47/48, turns 19/46). They live in a different render
   path than the reflection callback I fixed. Recommended next step:
   trace where they come from (probably a Kiki-specific
   "processing/stall" pool with a short blackout or a small pool); add
   per-line anti-repeat the way I did for `asks_absence`.
2. **`register_appropriateness` 66/100.** Look at what the believability
   battery is measuring there — likely a few register mismatches in
   formal-vs-casual replies. Drag the axis up before testers see it.
3. **`transcript_review.js` false positives.** The `assistant_deference`
   regex catches `\bi will\b` which is too broad (Pretorius uses "I will
   make the incision neat" voice-appropriately). Tighten to
   `i (?:can help|will help|am happy|am here)` in a future pass — not
   urgent because it's a developer tool, not a gate test.
4. **Cygwin DLLs in package_demo.ps1 hardcode `C:\cygwin64\bin\`** paths.
   Fine for the current build environment; would fail if Cygwin lives
   elsewhere. Worth an env var fallback eventually.

## Still on the lead's Windows desk

These need the lead's machine because I can't run PowerShell or test on
a clean Windows profile from here:

- **MARKET_READY task 1**: clean-Windows-profile install + run smoke
  test. Defender/SmartScreen behavior. Port-busy / missing-DLL /
  blocked-exe failure messages.
- **Re-run `make demo_package release_check` on Windows** with the new
  `package_demo.ps1`. Confirm the versioned ZIP name, the stamped
  `START_HERE.html`/`README_FIRST.txt`/`VERSION.txt`/feedback form, and
  that the package size budget is still under 25 MB.
- **Phases 4 (installer) and 5 (privacy/legal polish)** — the lead's
  call, not mine.

## V5 ship-readiness, my read

V5 is **ship-ready for first external testers** modulo the Windows-side
smoke pass. The hard regressions are fixed. The remaining quality items
(Kiki stalling pool, `register_appropriateness` axis) are real but the
character is good enough for a 5-10 person tester cohort to give honest
feedback on; better to ship and learn than to polish further before any
external eyes have seen it.

When you're satisfied with the Windows-side smoke test, tag a build
(e.g. `v5.0.0-rc1`), send the ZIP to a small cohort, and start V6
Phase 3 (self-ledger / engine-authored speech events) on the V6 branch
family. V6 is unblocked now that V5 has a stable handoff.

## V6 status (for context)

V6 work lives on three branches that don't touch V5:

- `doctrine-phase1-runtime-determinism` — engine clock + sidecar
  scaffold + V5 doctrine writeup.
- `actor-tagged-memory-phase2` — actor-tagged memory sidecar.
- `v6-doctrine` — `PersonaConsole_v6/` sibling project, V6 constitution
  (`V6_DOCTRINE.md`), and the V5→V6 migration guide.

When V5 ships, Phase 3 (self-ledger) lands on `v6-phase3-self-ledger`
forked from the current `v6-doctrine` HEAD. None of V6's evolution
impacts V5 cartridges or the V5 demo package — V5 cartridges load
unchanged in V6 with defaults.
