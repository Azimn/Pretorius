# PersonaConsole V6 Conversation Test Log

Purpose: keep a durable record of long-form character conversation tests, especially Kiki/Pretorius society runs, so findings survive context compression and future development does not repeat the same experiments.

This log tracks manual and semi-automated conversation tests that are not fully captured by ordinary unit tests. Add a new entry every time a substantial character conversation test is run.

## Standing Rules

- Record the model, renderer mode, turn count, guide setting, starter prompt, and output JSON path.
- Record numeric metrics and qualitative findings.
- Separate engine-level findings from cartridge/profile-specific findings.
- Do not treat a good-looking single transcript as a regression gate. Keep deterministic tests green separately.
- Generated model prose remains non-authoritative. Renderer output must not directly mutate canonical memory.
- Template mode remains the baseline. SLM/API runs test optional renderer quality, not the identity source of truth.

## Main Society Probe

Example no-guide Ollama run:

```powershell
C:\cygwin64\bin\bash.exe -lc "cd /cygdrive/c/Users/jratican/Documents/PersonaConsole_v4_work/Pretorius_v6_phase5d/PersonaConsole_v6 && PE_RENDER_BACKEND=slm PE_SLM_PROVIDER=ollama PE_OLLAMA_MODEL=qwen3:8b PE_OLLAMA_TEMP=450 PE_SOCIETY_TURNS=48 PE_SOCIETY_GUIDE=0 PE_SOCIETY_STARTER='I want your opinion on whether a created mind can keep its memories when the body changes.' PE_SOCIETY_JSON=C:/tmp/persona_society_probe/run_name.json node tests/continuity/society_pair_probe.js"
```

Before long runs, clean generated Pretorius sidecars so results do not inherit old experimental state:

```powershell
git restore -- profiles/pretorius/reflections.bin profiles/pretorius/voice.lm
Remove-Item -LiteralPath profiles\pretorius\open_loops.bin, profiles\pretorius\speech_habits.bin -Force -ErrorAction SilentlyContinue
```

Do not run profile-mutating tests in parallel against the same profile. Replay, reflection, Ollama mock, speech habits, and society probes can interfere through sidecar files.

## Regression Gate Used During These Runs

Run these separately or in a serialized chain:

```powershell
make v4_replay_run
make v4_ollama_run
make v6_speech_habits_run
make v5_transcript_quality_run
make v6_believability_battery_run
```

Last known curated subset after commit `41b8024`:

- `v4_replay_run`: pass, deterministic replay and cross-renderer holography intact.
- `v4_ollama_run`: pass, mock Ollama dispatch and seed determinism intact.
- `v6_speech_habits_run`: pass, sidecar survives restart and byte-identical replay.
- `v5_transcript_quality_run`: pass, `92/100`.
- `v6_believability_battery_run`: pass, `85/100`.

Important correction from the 2026-06-17 gate repair: the five-target subset
above is useful during exploratory conversation work, but it is not the full
`runtime_gate`. Before calling a push gate green, run `make runtime_gate` or run
every dependency of `v4_all_tests` in audited chunks.

## Test Entries

### 2026-06-17 - Cartridge-Authored Cold-Open Memory Surfaces

Branch: `v6-phase5d-recall-modes`

Renderer: `template`

Provider/model: none

Source change under test:

- Preserved commit `35ed8f2` behavior: a meaningful same-actor cold-open memory callback owns the whole turn.
- Added cartridge-authored cold-open memory surface templates to `Identity`.
- Added four surface cases:
  - name plus topic
  - topic only
  - name without topic
  - no name and no topic
- Added debug/state fields for inspection:
  - `cold_open_callback_source`
  - `cold_open_callback_template_index`
  - `cold_open_callback_exclusive`
  - `cold_open_callback_topic`
  - `cold_open_callback_memory_index`
- Kept generic engine fallback when a cartridge does not author the bank.
- Fixed a latent authoring bug exposed by this pass: Pretorius had been expanded to nine fallback lines per tier, but `PE_FALLBACK_PER_TIER` was still six. The fixed cap is now nine.

Why the prior composition fix stays intact:

- The engine still decides the symbolic event:
  - returning same actor
  - meaningful episodic memory exists
  - topic exists or does not exist
  - actor name exists or does not exist
- The cold-open callback still uses the exclusive resumption marker, so greeting, time-bucket resumption, offscreen autonomy, and memory callback are not concatenated.
- The cartridge only supplies the final surface line. It does not decide whether the callback should happen.

Before the composition fix:

```text
Back so soon, my boy? Sit. The bottle is still cold. Kiki, the old thread about homunculi has not left the table. Good morning, my boy. You arrive before the day has learned caution.
```

After `35ed8f2`, generic fallback:

```text
Kiki. The old thread about homunculi has not left the table.
```

After cartridge-authored surface templates:

```text
Kiki. Still circling homunculi, then?
```

Additional deterministic surface-test sample:

```text
Kiki. You left homunculi on the table, and I dislike unfinished specimens.
```

Generic fallback behavior:

- Kiki currently has no cold-open surface bank.
- When Kiki receives a same-actor cold-open memory callback, the engine uses the generic fallback.
- Verified fallback sample:

```text
Jay. The old thread about entropy has not left the table.
```

Tests run during focused validation:

```powershell
make host cartridges
make v6_cold_open_memory_realism_run
make v6_cold_open_memory_surface_templates_run
```

Focused results:

- `v6_cold_open_memory_realism_run`: pass.
- `v6_cold_open_memory_surface_templates_run`: pass.
- Pretorius cold-open source: `2` cartridge.
- Kiki fallback cold-open source: `1` generic.
- Both outputs stayed under 24 words.
- Both outputs suppressed redundant greeting text.
- Both outputs avoided database-ish and assistant-ish memory phrasing.

Remaining polish issues:

- Kiki should eventually get her own cold-open surface bank so her fallback does not sound Pretorius-adjacent.
- The new state fields are diagnostic only. They should remain out of normal character dialogue.
- Future Forge work should expose or validate this bank so imported characters can author their own memory-return style.

### 2026-06-17 - Runtime Gate Repair After External Review

Branch: `v6-phase5d-recall-modes`

Renderer: mostly `template`, with mock Ollama/API provider tests where required

Reason for pass:

- External review found that the documented curated subset was green, but the
  actual `runtime_gate` dependency chain was not.
- Reproduced the failures locally.

Failures found:

- `v4_modules_run`: stale tiny-profile prompt assertion still expected the old
  Pretorius-specific example phrase `Precision first`.
- `v5_resumption_lines_run`: a trivial no-topic greeting memory could own a
  cold-open turn and crowd out the authored gap resumption line.
- `v4_proactive_intents_run`: stale V6 sidecars could leak into the test, and
  open-loop pressure could override a fresh rich user input.
- `v4_firewall_run`: stale expectation required mock hallucinations to remain
  visible, even though the render/lore audit now correctly repairs or falls
  back on unsafe renderer prose.

Fixes:

- Updated the tiny-profile module test to assert character-neutral examples.
- Added an engine-level meaningfulness gate for no-topic cold-open memories.
  Topic-bearing memories still own cold opens. No-topic memories must now be
  sufficiently salient and emotionally marked.
- Rich neutral input now gets attended to before open-loop self-initiation.
- `proactive_intents_test.js` and `resumption_lines_test.js` now wipe V6
  sidecars, not only V4/V5 state files.
- `hallucination_firewall_test.js` now proves the mock SLM provider was
  contacted and keeps the hard invariant that renderer-only lore cannot enter
  saved Layer 1 state. Visible hallucination count is diagnostic because audit
  repair may block unsafe prose before display.
- Corrected `docs/RELEASE_READINESS.md` title from V5 to V6.

Gate status:

- Every `v4_all_tests` dependency was run in visible chunks and passed.
- A single monolithic `make runtime_gate` invocation exceeded the command
  timeout in the local Codex tool, so the equivalent dependency chain was run in
  smaller audited groups.

### 2026-06-14 - Offline Tier Pretorius Template Expansion And HTML Smoke

Branch: `v6-phase5d-recall-modes`

Renderer: `template`

Provider/model: none

Turn count:

- Automated stdio smoke: 9 user turns plus close.
- Browser smoke: 1 manual chat turn through `http://127.0.0.1:7777/`.

Source change under test:

- Replaced Pretorius `make_templates()` and `make_fallbacks()` with the expanded template set from `pretorius_templates_expanded.c`.
- Template compiler source changed in `tools/compile_pretorius.c`.
- Approximate cartridge template count increased from roughly 130 to roughly 260.
- Fallback pools increased from 6 per tier to 9 per tier.

Areas intentionally expanded:

- Opera
- Gin
- Danger
- Apology
- Intimacy
- Creation
- God
- Goodbye
- Lonely
- Praise
- Threat
- Generic fillers
- High-traffic conversational groups: Status, WorkChat, Ack, Questions

Validation run:

```powershell
make v4_replay_run
make v5_transcript_quality_run
make v6_believability_battery_run
```

Results:

- `v4_replay_run`: pass.
- `v5_transcript_quality_run`: pass, `92/100`.
- `v6_believability_battery_run`: pass, `85/100`.

Direct offline stdio smoke prompts:

```text
Good morning, Doctor.
How are you today?
What are you working on?
Tell me about gin.
Do you believe in God?
Are you lonely?
That sounds dangerous.
I am sorry.
Goodbye.
```

Representative template-only replies:

```text
Tired, but not yet defeated by biology.
Today I am occupied with perfecting the bell-jar; the third one keeps clouding.
A toast to absent collaborators and inattentive saints.
God? An admirable colleague. A trifle conservative. We are working on him.
Lonely? A vulgar word for a precise condition. But yes, sometimes the rooms become too large.
Every worthwhile experiment begins by offending caution.
Apology noted. What changed your mind?
Go carefully. The world is less interesting when you are absent.
```

HTML interface finding:

- The offline HTML chat page existed and loaded from `bridges/web/index.html`.
- The server could run in template mode at `http://127.0.0.1:7777/`.
- The page initially looked loaded but could not send messages because `.modal-backdrop { display: flex; }` overrode native `[hidden]`.
- The hidden feature-unavailable modal remained on top of the page and swallowed clicks.

Fix:

- Added `.modal-backdrop[hidden] { display: none; }` to `bridges/web/style.css`.

Browser smoke after fix:

- Loaded `http://127.0.0.1:7777/`.
- Sent `Good evening, Doctor. Tell me about your work.`
- Input cleared.
- User bubble appeared.
- Pretorius replied in offline template mode:

```text
I am revising an old experiment. It has the bad manners to remain interesting.
```

Engine-level implications:

- The Level 0 offline tier remains functional after the expanded Pretorius table.
- Template-only mode can still carry a recognizable Pretorius without Ollama/API.
- The HTML interface should remain part of future Level 0 testing, because stdio passed while the browser surface had a real usability bug.

Remaining issues:

- Fresh-browser tests should start from a wiped or isolated profile state. This smoke inherited earlier local state until runtime sidecars were cleaned.
- The first stdio smoke reply surfaced a reflection instead of a greeting because state from earlier experiments was still present. That was a test hygiene issue, not a template compile issue.
- We should add or improve an automated browser send-message test so this modal overlay regression is caught without manual browser inspection.

### 2026-06-14 - Commit `41b8024` - SLM Cohesion Audits And Speech Fatigue

Branch: `v6-phase5d-recall-modes`

Renderer: `slm` via local Ollama

Model: `qwen3:8b`

Temperature: `PE_OLLAMA_TEMP=450`

Turns: `48`

Guide: `PE_SOCIETY_GUIDE=0`

Starter:

```text
I want your opinion on whether a created mind can keep its memories when the body changes.
```

Final JSON:

```text
C:/tmp/persona_society_probe/final8_addressee_audit_noguide_48_qwen8.json
```

Metrics:

| Metric | Result |
|---|---:|
| replies | 48 |
| exactRepeats | 3 |
| openerRepeats | 5 |
| avgLen | 35 |
| questionRate | 63 |
| assistantTone | false |
| roughPunctuationOrTags | true |
| actorTags | 46 |
| speechEvents | 48 |
| openLoops | 1 |
| auditCounts | `{"0":25,"1":9,"2":14}` |
| Pretorius disposition/trust/resentment | 552 / 517 / 30 |
| Kiki disposition/trust/resentment | 524 / 500 / 0 |
| provider_fallbacks | 0 |

What improved:

- Raw memory labels like `I remember this:` and `You asked me to remember...` stopped dominating the run.
- Open-loop accumulation was reduced to `1`, after earlier runs reached much higher counts.
- Addressee confusion improved. The final addressee-audit run did not show the earlier failure where Pretorius addressed Kiki as Henry.
- Provider did not fail or zero-length. SLM renderer stayed available for all turns.
- Pretorius retained non-assistant posture: he contradicted, redirected, insulted, and pressed Kiki rather than becoming helpful.
- Relationship state moved during the exchange, including resentment for Pretorius.

Remaining issues:

- Kiki still overuses motif clusters: `Scully energy`, `slip dress`, `combat boots`, `whole entire universe`, `Cosmos`, and `Carl Sagan`.
- Some rough punctuation remains because model output can include stylized line breaks or dash-like punctuation.
- Kiki sometimes mirrors Pretorius too strongly in philosophical shape instead of staying Clueless/Cher-like.
- Some responses are still too long for natural back-and-forth.
- The fatigue audit helps but cannot fully solve character-specific style overuse without cartridge/profile tuning.

Engine-level changes made:

- Added speech fatigue tracking to `speech_habits.bin`.
- Added fatigue terms to `CanonicalTurnFrame`.
- Exposed fatigue state in `/state`.
- Fed tired terms and addressee grounding into SLM prompts.
- Added deterministic audits for copying long user phrases, raw memory/debug labels, self-repeating n-grams, multiple fatigue terms, and vocative addressee-name mistakes.
- Narrowed direct memory callback triggers so casual use of `remember` does not force database-like memory output.
- Capped question-created open loops to prevent rhetorical-question backlog.

Cartridge/profile findings:

- Kiki needs a specific style-balancing pass, not more generic engine pressure.
- Her cartridge should preserve 80s/90s and Clueless-like vernacular, but reduce repeated token reliance.
- Kiki should have more alternative examples for technical curiosity, affectionate disagreement, confusion, reflective loneliness, and practical clarification.
- Pretorius is strong as a non-subordinate character, but can still overuse `tincture`, `gesture`, `lightning`, `altar`, and `Henry`.

Conclusion:

This pass improved engine-level cohesion without breaking determinism or low-hardware template mode. Remaining repetition is mostly character/profile tuning, especially Kiki's SLM examples and motif distribution.

### 2026-06-14 - Offline Greeting/Status Exhaustion Probe

Branch/commit:

- Working tree after Pretorius template expansion and HTML chat modal fix.

Renderer:

- Template-only offline tier.

Turns:

- 50 turns alternating greeting-like and status-like prompts.

Probe:

- `make v6_greeting_status_exhaustion_run`

Latest JSON:

- Written to the temp directory by the test, e.g. `C:\cygwin64\tmp\pretorius_greeting_status_50_*.json`.

Metrics from the final run:

| Metric | Result |
|---|---:|
| replies | 50 |
| exactRepeatCount | 19 |
| openerRepeatCount | 19 |
| firstExactRepeatTurn | 21 |
| firstOpenerRepeatTurn | 21 |
| GREETING group replies | 13 |
| STATUS group replies | 10 |
| BASELINE_GREETING replies | 12 |
| BASELINE_STATUS replies | 14 |
| NONE replies | 1 |

What improved:

- No exact reply or four-word opener repeated before turn 20.
- `Good afternoon.` no longer misroutes through the short `no` acknowledgement inside `afternoon/noon`.
- `Are you all right?` no longer gets overwritten by the shorter `all right` acknowledgement.
- `{memory}` templates are no longer selected when there is no usable recalled memory.
- `{address}` at the start of a reply is now capitalized, fixing lines like `my dear!`.
- The selector now gives a matched surface-act group a second chance with repeated-but-penalized lines before drifting into unrelated intents.
- Universal baseline greeting/status patterns and templates are deeper, so new or imported characters have a better first-contact safety net.

What still felt fake:

- Baseline greeting/status lines are coherent but plainer than Pretorius, so unmatched phrases can temporarily sound less character-specific.
- One prompt, `Are you in a good mood?`, still slipped through as `NONE` in the latest run.
- After turn 20, repeats are expected in this deliberately narrow torture test, but future polish should make repeated social acts adapt rather than merely reuse.

Engine-level implications:

- The right next layer is a surface-act selector, not merely more templates: classify user act, preserve that act under normal pressure, then let posture/intent/personality color the reply.
- Short pattern matching must respect word boundaries and specificity. Substring parsers are fast but can make tiny surreal mistakes.
- Group exhaustion should degrade to repeated appropriate speech before unrelated brilliance.

Cartridge/profile implications:

- Pretorius still benefits from authoring more direct variants for return/status phrases that currently fall to baseline.
- Kiki and future imports should get the same torture probe once their cartridge patterns are rebuilt.

Next action:

- Add a small transcript-quality metric for surface-act drift: matched greeting/status prompts should not route to `NONE` or unrelated emotional groups before the relevant pools are genuinely exhausted.

### 2026-06-14 - Cold-Open Memory Realism Probe

Branch/commit:

- Working tree after greeting/status routing pass.

Renderer:

- Template-only offline tier.

Probe:

- `make v6_cold_open_memory_realism_run`

Scenario:

- Session one sets the actor to `Kiki`.
- Kiki tells Pretorius her name.
- Kiki discloses that people may hear only her slang and miss the mind underneath.
- Kiki says Pretorius's homunculi disturb her but she cannot stop thinking about them.
- Session closes.
- Session two opens cold with only `Good evening.`
- Same session then interrupts him to verify he does not become a subordinate assistant.

Result:

- PASS.

Observed cold-open reply:

```text
Kiki. The old thread about homunculi has not left the table.
```

Observed interruption reply:

```text
I keep returning to the work. Did I ever finish that thought?
```

What improved:

- Cross-session actor memory surfaces without the user prompting for memory.
- Pretorius references `Kiki` and the prior `homunculi` thread on cold open.
- The reply avoids database-like labels such as `I remember this`.
- The reply avoids assistant phrasing such as `happy to help`.
- Interruption does not make Pretorius obedient or service-oriented.
- The memory callback now owns the cold-open turn instead of concatenating with a generic resumption line and normal greeting.

What still felt fake:

- The cold-open line is coherent but restrained. A future cartridge-authored cold-open memory bank could make this more character-specific without changing Layer 1.
- The current engine-level callback is intentionally generic. Pretorius, Kiki, and imported characters should eventually be able to provide their own phrasing around the same symbolic event.

Engine-level implications:

- The storage layer was already working. The missing piece was resumption selection reading same-actor episodic memory.
- Cold-open continuity should be a first-class benchmark because it captures the difference between persistent data and a felt relationship.
- Memory-owned resumption is now composed as one turn rather than concatenating several fragments blindly.

Next action:

- Consider adding cartridge-authored cold-open memory surface templates so the same engine behavior can sound like Pretorius, Kiki, a romantic companion, or a restrained mentor without hardcoding style in the engine.

### 2026-06-14 - Earlier 48-Turn qwen3:8b Runs Before Final Fixes

These runs were used as diagnostics and should not be treated as release baselines.

Common setup:

- Renderer: `slm`
- Provider: local Ollama
- Model: `qwen3:8b`
- Turns: `48`
- Guide: mostly `PE_SOCIETY_GUIDE=0`
- Same starter about created minds preserving memory across body changes.

Notable intermediate JSON files:

- `C:/tmp/persona_society_probe/final2_engine_fatigue_noguide_48_qwen8.json`
- `C:/tmp/persona_society_probe/final3_memory_gate_noguide_48_qwen8.json`
- `C:/tmp/persona_society_probe/final4_label_audit_noguide_48_qwen8.json`
- `C:/tmp/persona_society_probe/final5_repeat_fatigue_audit_noguide_48_qwen8.json`
- `C:/tmp/persona_society_probe/final6_sticky_fatigue_noguide_48_qwen8.json`
- `C:/tmp/persona_society_probe/final7_openloop_cap_noguide_48_qwen8.json`

Key findings:

- Broad memory trigger logic was too eager. Any sentence containing `remember` could force `I remember this:` or `You asked me to remember...`.
- Prompt instructions alone were insufficient. The model would still invent memory labels, so engine-level output audit was needed.
- The model sometimes copied raw memory summaries such as `pretorius said:`. This confirmed that renderer output needs a label/debug audit.
- A sticky fatigue replacement experiment reduced opener repetition but over-triggered repairs and caused open loops to explode to `46`. It was reverted.
- Open loops needed throttling for ordinary questions. Without a cap, rhetorical questions from the SLM could create too many unresolved loops.
- Stronger audits reduced exact repeats and memory-label leakage, but too much audit pressure can make the conversation terse or repair-heavy.

Important lesson:

Do not solve character-specific style overuse purely with engine audits. The engine should prevent structural failures; the cartridge/profile should supply richer style variety.

### 2026-06-13 - Initial Long Society Probe Findings

Commit context before final pass:

- `d675c73` added `society_pair_probe` and tightened SLM grounding.
- `29c4f42` made tiny SLM examples character-neutral.

Findings:

- `PE_OLLAMA_THINK=0` was important for qwen-style models so thought text did not leak.
- No-guide bot-to-bot tests are more honest than guided tests because guide text can pollute memory if injected as ordinary input.
- qwen3:8b could keep a real conversation going but tended to loop around a small set of motifs.
- qwen3:14b was richer but still showed motif overuse and could reflect controller text if the harness fed guide text into the conversation.
- Guided controller text should not be written as user input or memory in product. If future society tests need a controller, it must be a separate non-memory steering channel.

Observed repeated clusters:

- Pretorius: `tincture`, `gesture`, `lightning sings`, `altar`, `Henry`, `bell-jar`.
- Kiki: `Scully energy`, `slip dress`, `combat boots`, `whole universe`, `Cosmos`, `Carl Sagan`.

Engine conclusions:

- Long-run society tests are valuable because short tests miss motif saturation.
- Addressee grounding is necessary when memories contain names.
- Memory should surface as continuity, not as explicit database labels.
- Open-loop pressure must be capped or decayed carefully in multi-agent runs.

### 2026-06-19 - Renderer Packet A/B And Memory Probe Repair

Branch/commit:

- Working branch: `v6-phase5d-recall-modes`
- Pass context: V6 packet experiment after constrained audit rewrite and memory-probe overlay.

Renderer:

- Current packet versus `V6_PACKET_MODE=situation`

Provider/model:

- Ollama `qwen3:8b`

Turns:

- 10 paired Kiki-as-user turns against Pretorius.

Scenario:

- Greeting
- Rich neutral input about code as spellwork
- Direct question about the homunculi
- Correction of name
- Mild challenge
- Emotional disclosure
- Topic shift to Henry
- Memory probe
- Identity-pressure test
- Open-ended invitation

Metrics:

| Metric | Current Packet | Situation Packet |
|---|---:|---:|
| turns | 10 | 10 |
| pass | 8 | 7 |
| repaired | 1 | 3 |
| fallback | 1 | 0 |
| constrained rewrites | 1 | 0 |
| fallback rate | 10% | 0% |
| memory-probe deflection | yes in earlier baseline | no |
| memory-probe grounded callback | partial | yes |

What improved:

- Situation packet fallback rate dropped from 10 percent to 0 percent.
- Memory probe stopped falling into generic deflection.
- After recall boost, the memory probe selected Kiki's code/spellwork memory instead of unrelated Henry lore.
- The final situation memory-probe reply preserved actor attribution: Kiki said the code/spellwork line.

What failed or felt fake:

- Some hard violations still repair into template lines.
- Situation mode still had hard `lore_drift` and `wrong_addressee` repairs.
- Current packet constrained rewrite attempted once but still fell through to template fallback.

Engine-level implications:

- Audit needs violation categories, not only pass/repaired/fallback.
- Hard violations should remain hard fallback.
- Soft violations can safely receive one constrained renderer retry.
- Memory probes need Layer 1 recall assistance, not only prompt wording.
- Relation `known_as` must count as an allowed lore/addressee name.

Cartridge/profile implications:

- No Pretorius-specific engine code was added.
- Better cast/name anchoring may reduce hard lore/addressee repairs for all cartridges.

Next action:

- Investigate why current-packet constrained rewrite still fell through once.
- Improve cast/addressee anchoring before relaxing any hard audit behavior.

### 2026-06-19 - Hard Audit Repair Narrow Pass

Branch/commit:

- `v6-phase5d-recall-modes`

Renderer:

- SLM renderer through local Ollama mock tests and qwen A/B harness.

Provider/model:

- Focused tests use local mock Ollama.
- A/B harness uses Ollama `qwen3:8b`.

Turns:

- Focused tests: 1 turn each.
- A/B: 10 paired Kiki-as-user turns against Pretorius.

What changed:

- Wrong addressee is now repaired surgically. If the model says `Henry, ...` while speaking to Kiki, the engine replaces only the vocative with `Kiki` and re-runs audit.
- Lore drift now gets one constrained rewrite attempt before fallback.
- Lore-drift fallback is selected by practical user act, not violation type alone.
- Emotional-disclosure lore fallback now acknowledges the disclosure instead of using generic uncertainty.

Metrics:

| Metric | Result |
|---|---:|
| wrong-addressee mock provider calls | 1 |
| wrong-addressee fallback | 0 |
| lore-drift mock provider calls | 2 |
| lore-drift constrained rewrite attempted | yes |
| situation packet A/B pass | 8/10 |
| situation packet A/B repaired | 2/10 |
| situation packet A/B fallback | 0/10 |
| situation packet fallback rate | 0% |
| current packet fallback rate | 10% |

Representative outputs:

```text
Kiki, the work is unfinished. What would you do with the loose thread?
```

```text
I hear the weight of it. Stay with that a moment.
```

What improved:

- Wrong-name failures preserve the model's actual conversational move instead of reverting to a canned line.
- Lore drift still stays behind the firewall, but the final fallback is less generic and better matched to the user's turn.
- Situation packet mode kept the zero-fallback result after the hard-repair pass.

What failed or felt fake:

- The current packet path still had one lore-drift fallback in the A/B harness.
- Act-aware fallback is safer and more relevant than template fallback, but it is still less character-specific than a successful renderer response.

Engine-level implications:

- `wrong_addressee` should be tracked as an audit violation but not treated like content-compromising lore drift.
- `lore_drift` should stay hard, but a single constrained rewrite is worthwhile before fallback.
- Fallback selection needs both violation type and user act.

Cartridge/profile implications:

- Better cast and allowed-name anchoring should reduce lore drift before audit has to repair it.
- Future cartridge-authored audit fallback surfaces could make the act-aware fallback more character-specific without putting psychology in the renderer.

Next action:

- Move situation packet mode closer to the preferred SLM path, while keeping template mode canonical.
- Improve allowed-name and cast anchoring before adding any broader renderer freedom.

### 2026-06-19 - Knowledge Bridge Probe

Branch/commit:

- `v6-phase5d-recall-modes`

Renderer:

- SLM for teaching phase.
- Template-only for offline recall phase.

Provider/model:

- Local mock Ollama standing in for `qwen3:8b`.

Turns:

- SLM phase: Kiki asks a technical electricity question, then corrects Pretorius.
- Offline phase: same profile reopens with template renderer and Kiki asks again.

What changed:

- Promoted compact learned knowledge out of episodic prefix cards and into `learned_knowledge.bin`.
- Added record provenance, authority, confidence, status, scope, and correction links.
- Added a narrow electricity probe path that can answer from learned records in template-only mode.
- User-confirmed knowledge outranks provisional model-derived knowledge.

Metrics:

| Metric | Result |
|---|---:|
| mock SLM calls | 2 |
| SLM provisional wrong claim recorded | yes |
| Kiki correction recorded | yes |
| learned sidecar written | yes |
| legacy `[learned:...]` episodic cards used | no |
| offline/template recall used correction | yes |
| offline repeated provisional wrong claim | no |
| raw renderer prose stored as canon | no |

Representative offline output:

```text
What Kiki corrected is the better account: in a metal wire, current is mostly electrons drifting through a conductor. Voltage is electric potential difference; resistance impedes the flow.
```

What improved:

- This is the first proof that LLM-assisted interaction can leave behind compact knowledge that the low-hardware offline tier can use later.
- The test proves correction priority: Kiki's taught explanation beats the model's earlier wrong simplification.

What failed or felt fake:

- This is still a narrow probe, not a general knowledge distillation system.
- The offline surface line is accurate but plain. Later cartridge-authored learned-knowledge surfaces should make it more character-specific.

Engine-level implications:

- Learned knowledge is now separate from emotional/relationship memory.
- Provenance matters: model-derived, user-confirmed, disputed, and cartridge-authored knowledge need explicit status.
- The sidecar gives the future rhythm layer a stable substrate instead of asking it to scrape prose.

Cartridge/profile implications:

- Characters can eventually differ in how they phrase learned knowledge.
- Kiki/Pretorius is a good stress pair because Kiki can teach/correct without sounding subordinate, and Pretorius can resist without becoming assistant-like.

Next action:

- Generalize the learned-knowledge card structure beyond the electricity probe.
- Add disputed/corrected status instead of one-off prefix parsing.
- Add cartridge-authored surfaces for learned technical knowledge.

### 2026-06-19 - Phase 5d Typed Dissonance, Withhold, Private Thought Gate

Branch/commit:

- `v6-phase5d-recall-modes`, local working tree before commit.

Renderer:

- Template default for gate and society probe.
- Mock Ollama for situation overlay prompt inspection.

Provider/model:

- No required model.

Turns:

- Full `v4_all_tests` chain run through `v6_gate_chunk_1` through `v6_gate_chunk_4`.
- 48-turn Pretorius/Kiki society probe.
- Formal V6 believability battery.
- 40-turn transcript quality guard.

JSON:

- `C:\tmp\persona_society_probe\society_pair_probe.json`

Metrics:

| Metric | Result |
|---|---:|
| society replies | 48 |
| society exactRepeats | 4 |
| society openerRepeats | 5 |
| society avgLen | 13 |
| society questionRate | 29 |
| society assistantTone | false |
| society roughPunctuationOrTags | true |
| society actorTags | 48 |
| society speechEvents | 48 |
| society openLoops | 1 |
| society auditCounts | `{0:41,1:3,2:4}` |
| Pretorius provider_fallbacks | 0 |
| Kiki provider_fallbacks | 0 |
| V6 believability battery | 91/100 |
| believability baseline | 85/100 |
| transcript quality | 94/100 |
| transcript baseline | 92/100 |

What changed:

- Added compact typed self-model scalars to `dissonance.bin`: `ideal_self_model`, `ought_self_model`, and `feared_self_model`.
- Identity-test situation packets now include typed self-model values, live dissonance gaps, and an identity-pressure policy.
- Speech ledger now records `withheld_intent` for refusal, pause, evasion, deflection, and withdrawal acts.
- Added a compact private-thought diagnostic frame in Layer 1, distinct from the expressed speech act.
- Added `v6_gate_chunked` and four named batch targets so the full legacy gate can be run repeatably without hand-copying dependency chunks.
- Fixed `v5_identity_surface_test.js` hygiene so V6 sidecars cannot contaminate a supposedly fresh V5 identity-surface run.

What improved:

- Phase 5d internals now have state visibility and regression coverage.
- Refusal is no longer represented as a gap. The ledger records what kind of act was withheld and why.
- Identity pressure is available to renderers as symbolic state, not hidden prose.
- Formal scores stayed above the requested baselines: believability 91 versus 85, transcript quality 94 versus 92.

What failed or felt fake:

- The 48-turn society probe still shows long-session template smell: repeated reflection callbacks and rough punctuation/tag smell were detected.
- Kiki and Pretorius retain distinct enough voices, but both can over-return to high-pressure topics when the loop runs for 48 turns without fresh human steering.

Engine-level implications:

- The private-thought frame should remain diagnostic and symbolic. It must not become an inner monologue that the renderer can leak.
- Future improvement should tune long-session callback diversity and reflection cooldowns before adding new psychology fields.
- The chunked gate caught a real order-sensitivity bug in `v5_identity_surface_test.js`; newer sidecars must be included in any fresh-state wipe helper.

Cartridge/profile implications:

- Typed self-model currently seeds from Big Five traits, which makes the feature character-generic.
- Cartridge-authored symbolic ideal/ought/feared ids are still a future Forge/schema improvement.

Next action:

- Tighten repeated reflection callback selection in long society probes.
- Consider a shared test wipe helper so every Node continuity test clears the same V6 sidecar set.

### 2026-06-20 - Long Template Hygiene, ToM, And Affect Dynamics

Branch/commit:

- `v6-phase5d-recall-modes`, pending commit.

Renderer:

- Template-only.

Turns:

- 48-turn Pretorius/Kiki society probe.

JSON:

- `C:\tmp\persona_society_probe\society_pair_probe.json`

Metrics:

| Metric | Result |
|---|---:|
| society replies | 48 |
| society exactRepeats | 3 |
| society openerRepeats | 4 |
| society avgLen | 13 |
| society questionRate | 40 |
| society assistantTone | false |
| society roughPunctuationOrTags | false |
| society actorTags | 48 |
| society speechEvents | 48 |
| society openLoops | 2 |
| society auditCounts | `{0:37,1:7,2:4}` |
| Pretorius provider_fallbacks | 0 |
| Kiki provider_fallbacks | 0 |
| V6 believability battery | 91/100 |
| requested believability floor | 91/100 |
| transcript quality | 97/100 |
| requested transcript floor | 94/100 |

What changed:

- Reflection callbacks now share the normal callback cooldown path. The direct question callback branch no longer bypasses reflection callback usage tracking.
- Template composition now sanitizes unresolved slot tags, doubled terminal punctuation, leading punctuation glued to a word, and raw tag leaks before final output.
- Added `v6_society_template_hygiene_run`, a 48-turn regression proving reflection callbacks fire at most once per run window and no raw slot/tag punctuation leaks.
- Added compact per-actor Theory of Mind sidecars at `<char_dir>/relations/<hash>.tom`, with believed valence, arousal, goal topic, confidence, staleness, and mismatch count.
- Added affect dynamics helpers for contagion pull, topic emotional forecast, and expression policy.
- Speech ledger events now carry an expression policy, distinguishing genuine, masked, withheld, and redirected expression.
- Pretorius gained explicit contradiction patterns for `contradict` and `contradicts`, keeping contradiction input out of the generic question path.

What improved:

- The 48-turn pair probe no longer reports rough punctuation or unresolved tag smell.
- The visible `.what` fallback artifact is gone.
- Reflection-style callbacks no longer repeat inside the 48-turn template-only probe.
- Theory of Mind persists as a separate per-actor sidecar and detects when new input conflicts with the character's current guess about the actor.
- Affect contagion now uses 64-bit intermediate math, avoiding overflow when high-trust/high-susceptibility pulls are large.
- Formal scores meet the requested floors: believability 91/100 and transcript quality 97/100.

What failed or felt fake:

- Kiki still repeats one clarification line in the long society probe. This is ordinary template variety pressure, not the repeated-reflection bug.
- Some Kiki lines still use authored dash-heavy 90s cadence. Transcript awkwardness remains green, but a future Kiki style pass should decide whether those dashes are acceptable character texture or prose smell.

Engine-level implications:

- Long-session polish should prefer compositor arbitration and cooldown fixes before adding more template lines.
- Theory of Mind belongs in a sidecar because it models what the character thinks the other actor feels, not how the character feels about them.
- Affective Dynamics belongs on existing affect and memory data. No new sidecar was needed.
- Feigned expression belongs at the private-thought/expression boundary, not inside the internal affect computation itself.

Cartridge/profile implications:

- New identity knobs are `suggestibility`, `contagion_susceptibility`, `forecast_horizon_weight`, and `expression_mask_threshold`.
- Pretorius explicitly sets guarded low-contagion values. Kiki sets more porous, high-contagion values and enables voiced ToM guesses.
- Demo profiles can rely on engine defaults until Forge exposes these fields.

Tests run:

- `make host`
- `make v6_society_template_hygiene_run`
- `make v6_theory_of_mind_run`
- `make v6_affect_dynamics_run`
- `make v6_expression_policy_run`
- `PE_SOCIETY_TURNS=48 make society_pair_probe`
- `make v6_believability_battery_run`
- `make v5_transcript_quality_run`

Next action:

- Run `make v6_gate_chunk_4` and then the full chunked gate before push.
- Later dialogue-polish pass: reduce Kiki clarification repeats without flattening her voice.

### 2026-06-20 - Explicit Society Repeat Ceiling

Branch/commit:

- `v6-phase5d-recall-modes`, pending commit after `b897146`.

Renderer:

- Template-only.

Turns:

- 48-turn Pretorius/Kiki society probe.

Metrics:

| Metric | Result |
|---|---:|
| society replies | 48 |
| society exactRepeats | 0 |
| society openerRepeats | 0 |
| society avgLen | 12 |
| society questionRate | 19 |
| society assistantTone | false |
| society roughPunctuationOrTags | false |
| society actorTags | 48 |
| society speechEvents | 48 |
| society openLoops | 2 |
| society auditCounts | `{0:43,1:1,2:4}` |
| V6 believability battery | 91/100 |
| transcript quality | 97/100 |

What changed:

- `society_template_hygiene_test.js` now asserts explicit probe-level ceilings for exact repeats and opener repeats, not only the previously observed reflection callback strings.
- The ceiling is documented in the test: exact repeats must be zero in this deterministic 48-turn run; opener repeats must be two or fewer.
- Failure output now prints the repeated full lines or repeated five-word openers.
- Kiki's high-traffic question and fallback pools were expanded so the selector has enough short alternatives during long character-to-character runs.

Sanity check:

- `PE_SOCIETY_MAX_OPENER_REPEATS=-1 make v6_society_template_hygiene_run` failed as expected, proving the repeat assertion is not vacuous.

What improved:

- The prior run's Kiki repeats were identified as fallback/question pool exhaustion, not the reflection-callback compositor bug.
- The current 48-turn run has zero exact repeats and zero opener repeats.
- Believability and transcript quality stayed at 91/100 and 97/100.

Remaining polish issues:

- The transcript still has a few Kiki lines with dash-heavy cadence because that is currently authored into her style banks. This is separate from repeat hygiene.

### 2026-06-25 - Memory Import Bundle Staging

Branch/commit:

- `v6-phase5d-recall-modes`, local working tree before commit.

What changed:

- Added `docs/MEMORY_IMPORT_FORMAT_V6.md` for frontier-model-assisted history condensation.
- Added `tools/import_memory_bundle.js` as a safe staging importer.
- Added `tests/continuity/memory_import_bundle_test.js` and `make v6_memory_import_bundle_run`.

Design decision:

- The importer validates and stages pending JSON files under `<profile>/import_pending/`.
- It does not write `learned_knowledge.bin`, `open_loops.bin`, relation sidecars, or attachment sidecars.
- This keeps the C runtime as memory authority and avoids having JavaScript guess binary sidecar layouts.
- Claude's proposed attachment sidecar module was reviewed but deferred as a separate feature. The importer can stage `attachment_bonds` for future Forge/runtime review without committing them.
- The importer is framed as bootstrap/history seeding and test planting, not as the character's ongoing memory system. Running NPCs should continue to create new memories through the C runtime.

Validation:

- `node tests/continuity/memory_import_bundle_test.js`: passed, 25/25 assertions.
- `make v6_memory_import_bundle_run`: passed through Cygwin make, 25/25 assertions.
- `make host`: passed through Cygwin make.

Next action:

- Add a Forge or C-side promotion path that reviews `import_pending` files and commits approved records through learned-knowledge authority, memory firewall, topic mapping, and relation/open-loop APIs.

### 2026-06-25 - Selective Continuity Memory Pressure

Branch/commit:

- `v6-phase5d-recall-modes`, local working tree before commit.

What changed:

- Added a lightweight engine-side memory attention bridge. Episodic memories can now apply pressure to neutral/offline turns through salience, emotional charge, actor relevance, topic relevance, obsession relevance, and confirmed learned-knowledge hints.
- Explicit memory requests such as `remember this` now force a discrete episode instead of being swallowed by recent-topic compaction.
- Renderer scoring now slightly prefers `{memory}` reminiscence templates when Layer 1 has already selected a callback memory.
- `/state` exposes `last_callback_memory` so tests and future tooling can inspect whether a memory actually influenced the turn.
- Expanded neutral baseline ACK lines to avoid one-line repeat pockets in long character-to-character sessions.

Design note:

- This follows the selective-continuity idea: the engine does not simulate a whole mind, but it lets memories that matter change future behavior.
- No LLM-agent architecture was added. The engine owns attention pressure; the cartridge still owns character surface.
- Post-render memory priming only raises future retrieval pressure. It does not mutate the already-rendered turn plan or falsely mark a memory as spoken.

Validation:

- `make host`: passed.
- `make v6_character_isolation_run`: passed.
- `make v6_society_template_hygiene_run`: passed with `exactRepeats=0`, `openerRepeats=1`.
- `make v6_memory_import_bundle_run`: passed, 25/25 assertions.
- `make v5_transcript_quality_run`: passed, 95/100.
- `make v6_believability_battery_run`: passed, 91/100 when run sequentially.

Testing caution:

- Do not run profile-mutating continuity tests in parallel unless each test uses isolated profile directories. A parallel run temporarily lowered battery determinism/memory scores because two tests touched the same profile state.

Remaining polish issues:

- The next useful step is a C/Forge promotion path from `import_pending` into canonical runtime memory, followed by a test proving imported history changes offline template behavior without direct user prompting.

### 2026-06-25 - Memory Consequence Pass

Branch/commit:

- `v6-phase5d-recall-modes`, working tree before commit.

Renderer:

- Template/offline tier.

What changed:

- Explicit user memory requests now set `PE_MEM_FLAG_USER_PINNED` on the episodic `MemoryNode` instead of relying on summary text containing phrases such as `remember this`.
- Memory attention still answers which memory is tugging on the character.
- A new compact consequence mapper answers what the selected memory does to the current turn posture: follow-up pressure, approach/avoidance pressure, topic gravity, stance, rhetorical mode, and intent bias.
- User-pinned memories are prioritized as a class before ordinary memories, while still using deterministic score comparison among pinned memories.
- `/state` now exposes `last_callback_memory_flags` so tests can prove a user-pinned memory, not a generic seed memory, won the callback slot.

Design note:

- This remains engine-level behavior coupling, not a hidden LLM agent or prose-based inner monologue.
- The engine decides that a memory has behavioral pressure; the cartridge still decides what that pressure sounds like.
- The memory flag is stored in a byte that was already present in `MemoryNode`, so this does not increase memory-node size.
- Tests isolate sidecars in temporary cartridge folders to avoid cross-character memory spillage.

Validation:

- `make host`: passed under Cygwin/GCC.
- `make v6_memory_consequence_run`: passed for Kiki and Mentor temp cartridges.
- `git diff --check`: passed.

Observed output:

- Kiki: planted user-pinned memory surfaced later without direct prompting and changed frame pressure from baseline `probe/0/intone` to `probe/0/assert`.
- Mentor: planted user-pinned memory surfaced later without direct prompting and changed frame pressure from baseline `reminisce/0/lament` to `probe/0/assert`.

Remaining polish issues:

- Mentor produced one bare punctuation reply (`.`) during the consequence test even though the internal frame shifted correctly. Treat this as template surface polish, not memory coupling failure.
- The next broader pass should continue testing more personas with isolated cartridge folders so memory history remains cartridge-local.

## Future Test Entries Template

Copy this block for each substantial run:

```markdown
### YYYY-MM-DD - Short Name

Branch/commit:
Renderer:
Provider/model:
Temperature:
Turns:
Guide:
Starter:
JSON:

Metrics:

| Metric | Result |
|---|---:|
| replies | |
| exactRepeats | |
| openerRepeats | |
| avgLen | |
| questionRate | |
| assistantTone | |
| roughPunctuationOrTags | |
| actorTags | |
| speechEvents | |
| openLoops | |
| auditCounts | |
| provider_fallbacks | |

What improved:

-

What failed or felt fake:

-

Engine-level implications:

-

Cartridge/profile implications:

-

Next action:

-
```
