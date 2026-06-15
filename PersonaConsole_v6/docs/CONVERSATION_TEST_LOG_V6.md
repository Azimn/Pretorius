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

Last known good gate after commit `41b8024`:

- `v4_replay_run`: pass, deterministic replay and cross-renderer holography intact.
- `v4_ollama_run`: pass, mock Ollama dispatch and seed determinism intact.
- `v6_speech_habits_run`: pass, sidecar survives restart and byte-identical replay.
- `v5_transcript_quality_run`: pass, `92/100`.
- `v6_believability_battery_run`: pass, `85/100`.

## Test Entries

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
