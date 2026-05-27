# PersonaConsole V5 Market-Ready Handoff

Date: 2026-05-27

This is the remaining work list for bringing PersonaConsole V5 to a real external tester / early market-ready state. The goal is not to chase a cloud chatbot. The goal is a low-requirement, local-first character console where the characters feel alive, distinctive, and owned by the user.

## Current Baseline

Work from the V5 branch is in:

`C:\Users\jratican\Documents\PersonaConsole_v4_work\Pretorius_github\PersonaConsole_v5`

Git branch:

`claude/persona-engine-runtime-swQXs`

Current project health from the latest local gate:

- `make v4_all_tests release_check` passes.
- Clean unzip release health check passes.
- Demo host working set is roughly 10 to 15 MB.
- Believability trial is around 98/100.
- Adversarial continuity arcs are around 100/100.
- Transcript quality is around 93/100.
- Pretorius and Kiki demo carts run from the packaged demo folder.
- The Forge can author V5 living-character fields and has assistant-smell warnings.
- Offscreen autonomy, wants, preoccupations, relationship hooks, and multi-user scaffolding exist and are tested.

There is an untracked `PersonaConsole_v5.zip` in the repo root. Treat it as a local artifact unless the user explicitly asks to include it.

## Non-Negotiables

- Keep the first market test local-first, offline-capable, and subscription-free.
- Do not make an SLM or LLM mandatory for the first release.
- Template mode is the primary shipping baseline.
- Preserve deterministic replay and the memory firewall.
- No telemetry by default. No hidden network dependency.
- Keep hardware requirements low: no GPU, no WSL requirement for the packaged demo, no internet after download, and a host memory target under 64 MB for template mode.
- Characters must not feel like generic assistants unless their authored personality says they should. Kiki can be helpful and service-oriented. Pretorius should not feel subordinate.
- Do not increase hardware requirements without a clear product reason and a documented tradeoff.

## Working Rules

Use the V5 folder only for this work.

Before any code or cartridge change, run from PowerShell:

```powershell
cd "C:\Users\jratican\Documents\PersonaConsole_v4_work\Pretorius_github"
```

Full validation gate:

```powershell
& 'C:\cygwin64\bin\bash.exe' -lc 'cd /cygdrive/c/Users/jratican/Documents/PersonaConsole_v4_work/Pretorius_github/PersonaConsole_v5 && make v4_all_tests release_check'
```

Package gate:

```powershell
& 'C:\cygwin64\bin\bash.exe' -lc 'cd /cygdrive/c/Users/jratican/Documents/PersonaConsole_v4_work/Pretorius_github/PersonaConsole_v5 && make demo_package release_check'
```

Plain build:

```powershell
& 'C:\cygwin64\bin\bash.exe' -lc 'cd /cygdrive/c/Users/jratican/Documents/PersonaConsole_v4_work/Pretorius_github/PersonaConsole_v5 && make'
```

When changing cartridge authoring source, commit the source and generated `.bin` / cartridge output together. Do not commit runtime state created by tests unless it is an intentional seed asset.

Do not commit transient relation or memory state such as local `state.bin`, `relations`, `aether`, `memory.bin`, or test-modified `reflections.bin` unless the file is explicitly part of a designed fixture.

## Phase 1: External Tester Build

Purpose: prove a nontechnical user can unzip, start, chat, stop, reset, and send diagnostics without developer tools.

Tasks:

- Test the demo ZIP on a clean Windows machine or a separate Windows user profile.
- Verify `START_HERE.html` explains exactly what to do.
- Verify `Run_Pretorius.cmd`, `Run_Kiki.cmd`, `Stop_Server.cmd`, reset scripts, health check, and diagnostics work from the extracted folder.
- Confirm there is no WSL, Cygwin, Git, Node, or compiler requirement for testers.
- Check Defender / SmartScreen behavior and document what the tester will see.
- Improve failure messages for common startup problems: port busy, missing DLL, blocked executable, corrupted unzip, browser did not open.
- Add a versioned release ZIP name with date and commit hash.
- Add a short known-issues file to the package.
- Add a tester feedback form or markdown file inside the package.

Acceptance criteria:

- A tester with no dev setup can start Pretorius or Kiki in under 3 minutes.
- Stop, reset, health check, and diagnostics are discoverable and work.
- Diagnostics are privacy-clear and do not silently collect chat text.
- Package size stays inside the low-hardware release budget.

## Phase 2: Conversation Quality Pass

Purpose: make the characters feel alive enough that users wonder how old-school machinery is producing the effect.

Tasks:

- Run at least one 50-turn manual transcript with Pretorius.
- Run at least one 50-turn manual transcript with Kiki.
- Mark fake moments: repeated openings, assistant-like deference, ornate fallback language, bad callbacks, em dash-heavy wording, unnatural topic pivots, and bland acknowledgements.
- Fix the top five fake moments before adding new features.
- Add targeted transcript tests for each repeated failure pattern.
- Increase short/plain reply pools where needed.
- Increase question-asking and disagreement where personality-appropriate.
- Strengthen anti-repeat behavior across openers, metaphors, topic callbacks, and memory callbacks.
- Keep Pretorius assertive, argumentative, self-directed, and morally non-assistant-like.
- Keep Kiki helpful and warmer without making her generic.

Acceptance criteria:

- No obvious repeated line in a normal 20-turn chat.
- Transcript quality remains at or above 90/100.
- Believability remains at or above 95/100.
- Assistant-smell guard passes.
- Replies show length variance: some short, some expansive, not all polished monologues.

## Phase 3: Forge Usability

Purpose: make V5 authoring usable by real people, not just engine developers.

Tasks:

- Polish Simple Mode for wants, preoccupations, resumption lines, milestones, and relationship posture.
- Show concrete examples beside each field.
- Make "Generate starter internal life" explain what it generated and why.
- Add actionable preflight suggestions, not only warnings.
- Add guidance that every character should ask the user their name early if relationship memory matters.
- Add default wording rules that reduce assistant-coded dialogue unless the character posture calls for it.
- Test a Forge-created character from export through `cartridge_lint`, engine load, and a short transcript.
- Add one Forge-created sample cart to the demo package once it is good enough.

Acceptance criteria:

- A nontechnical user can create a passable character in under 10 minutes.
- Forge-created carts pass `cartridge_lint`.
- Forge-created carts do not feel inert: they have wants, preoccupations, resumption lines, milestones, and a relationship posture.
- Export preflight explains what to improve in plain language.

## Phase 4: Installer And Distribution

Purpose: remove setup friction for real users.

Tasks:

- Decide between self-extracting ZIP, installer, or polished portable folder for first public test.
- Add visible version number and build hash to `START_HERE.html` or package README.
- Add a clear uninstall / delete-local-state instruction.
- Add a desktop shortcut option if using an installer.
- Consider code signing or at least document unsigned executable warnings.
- Keep a portable-folder option even if an installer is added.

Acceptance criteria:

- Install or unzip path is tested on clean Windows.
- User can find logs, diagnostics, reset, and uninstall instructions.
- The package remains usable without administrator privileges where possible.

## Phase 5: Privacy, Safety, And Legal Polish

Purpose: make the product trustworthy before strangers use it.

Tasks:

- Add a privacy statement: local files, no telemetry, no subscription, no cloud dependency in template mode.
- Explain exactly what diagnostics collect.
- Explain transcript export, if present.
- Add a concise fictional-character disclaimer.
- Add clear boundaries: not medical, legal, financial, emergency, or therapy software.
- Review bundled DLL, Cygwin, and third-party license obligations.
- Add a license summary to the package.

Acceptance criteria:

- The demo package contains privacy, diagnostics, reset, and license information.
- No hidden telemetry exists.
- Any optional network or SLM behavior is opt-in and documented.

## Phase 6: Regression And Release Gates

Purpose: keep quality stable while polishing.

Required gates before every release candidate:

```powershell
& 'C:\cygwin64\bin\bash.exe' -lc 'cd /cygdrive/c/Users/jratican/Documents/PersonaConsole_v4_work/Pretorius_github/PersonaConsole_v5 && make v4_all_tests release_check'
```

Also run:

- Clean unzip package test.
- Health check from inside the extracted folder.
- Transcript quality harness.
- Assistant-smell guard.
- Adversarial arcs.
- Cartridge lint on all bundled carts.
- Manual smoke test of Pretorius and Kiki.

Acceptance criteria:

- All gates are green.
- No test is weakened just to pass.
- Any known failure is documented as a release blocker or accepted known issue.

## Phase 7: Future Society / Multi-Agent Testing

Purpose: stress-test long-horizon consistency later, after first market readiness.

This is not a blocker for the first release.

Future options:

- Internal deterministic society harness: characters talk to each other and scripted agents over simulated days or weeks.
- SimWorld-style local world simulation.
- External AI-only social spaces as chaos demos, not canonical regression rigs.
- Moltbook / SpaceMolt-style environments as future showcase targets if they are available and compatible.

Principle:

Use external platforms for demos and chaos testing only. Use internal deterministic harnesses for regression testing because failures must be replayable.

## Phase 8: Optional Local SLM Later

Purpose: improve sentence-level novelty after the deterministic product is already useful.

Do not make this part of the first market-ready release.

Rules:

- SLM must be opt-in.
- Template fallback must remain available.
- No cloud model dependency.
- No model download during basic first run.
- Keep hardware tiers documented.
- Preserve memory firewall and behavioral holography.
- Prefer sub-1B local models first.

Target baseline:

- Template-only mode: roughly 10 to 15 MB working set.
- Optional 360M-class local SLM: roughly 300 to 500 MB model/runtime footprint.
- No GPU required.

## Market-Ready Exit Criteria

PersonaConsole V5 is ready for real external testing when:

- Five to ten nontechnical testers can run it without developer support.
- Startup, stop, reset, health check, and diagnostics work from the package.
- `make v4_all_tests release_check` is green.
- Clean unzip release test is green.
- Package remains inside the low-hardware budget.
- Pretorius and Kiki both produce plausible 20-turn conversations.
- Forge can create a new V5 character that passes lint and can chat.
- Privacy, diagnostics, reset, and license docs are included.
- There are no known critical startup bugs.

## Immediate Next Tasks For Claude

1. Do a real clean-machine or clean-profile Windows package test.
2. Add `TESTER_FEEDBACK_FORM.md` or an HTML equivalent to the demo package.
3. Add package version and commit hash display to `START_HERE.html` or package README.
4. Run 50-turn Pretorius and Kiki transcript reviews.
5. Fix the top five fake moments found in those transcripts.
6. Add regression tests for those fake moments.
7. Produce a versioned demo ZIP.
8. Push all changes to `claude/persona-engine-runtime-swQXs`.

## Do Not Do Yet

- Do not pivot to cloud hosting.
- Do not add telemetry.
- Do not make the SLM mandatory.
- Do not add external tool-use agency to characters for the first release.
- Do not over-engineer a society simulator before the package is usable by testers.
- Do not raise the hardware floor without explicit user approval.
- Do not make Pretorius sound like a helpful assistant.
