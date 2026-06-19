# PersonaConsole V6 Release Readiness

Status: pre-alpha external tester gate  
Goal: make the current deterministic system safe and understandable for real users before SLM work becomes the focus

## 1. Release Shape

The first public test should be a small Windows demo package, not a developer repo.

Required contents:

- `START_HERE.html`
- `README_FIRST.txt`
- `Run_Pretorius.cmd`
- `Run_Kiki.cmd`
- `Stop_Server.cmd`
- `persona_host.exe`
- required runtime DLLs
- `characters/pretorius/pretorius.cart`
- `characters/kiki/kiki.cart`
- `host/web/`
- `Forge/forge.html`
- `Inspector/cartridge_inspector.html`

The tester should not need Cygwin, WSL, Git, Node, Make, a compiler, an account, a GPU, or an internet connection after receiving the package.

## 2. Non-Negotiable Ship Gates

### Local First

- Runs on `127.0.0.1`.
- No external service required.
- No telemetry.
- No subscription or account.
- Character state and memory stay beside the cartridge.

### Low Hardware

The default package is template-mode only.

Budgets:

- demo zip under 25 MB
- host working set under 96 MB during a smoke run
- no bundled SLM model
- no GPU requirement
- no model download during first run

The expected real footprint should be far below these budgets. The limits exist to catch accidental bloat, not to define success.

### First-Run Usability

- One obvious start path.
- One obvious stop path.
- Browser opens automatically from the run script.
- Failure messages are plain enough for a non-technical tester to report.
- The README says what to run and what hardware is not required.

### Character Quality

- Pretorius and Kiki both load from separate cartridge folders.
- No obvious repeated lines in a short conversation.
- No em dash-heavy template smell.
- Proactive presence can be disabled.
- Idle initiative works without incrementing normal turn count unexpectedly.
- The character can acknowledge, ask questions, disagree, recover from silence, and hold state after conflict.

### Authoring Safety

- Forge exports V5 cartridges.
- Forge preflight gives suggestions, not only errors.
- Inspector can identify V5 fields and obvious missing internal-life data.
- `cartridge_lint` passes for included cartridges.

### Regression Safety

Run before sharing:

```powershell
$env:PATH='C:\cygwin64\bin;' + $env:PATH
& 'C:\cygwin64\bin\make.exe' v4_all_tests
& 'C:\cygwin64\bin\make.exe' demo_package
& 'C:\cygwin64\bin\make.exe' release_check
```

## 3. External Tester Instructions

The first tester build should ask for narrow feedback:

- Did it start without help?
- Did the browser open?
- Could you stop it?
- Did the character repeat itself?
- Did the character ever feel like it was ignoring you?
- Did proactive speech feel alive or annoying?
- Did the Forge make sense without explanation?
- Did anything look like an AI-written canned response?
- What was the oldest or weakest computer you tried?

Avoid asking broad questions like "is it good?" until startup and basic comprehension are solid.

## 4. What Is Not Required Yet

- SLM rendering.
- External platform adapters.
- Installer.
- Signed binaries.
- Auto-update.
- Cloud sync.
- Mobile packaging.

Those are later. The first goal is proof that a normal person can run an owned, local character and feel a spark of life without technical setup.

## 5. Current Highest-Priority Work

1. Keep the release package reproducible.
2. Keep the automated release check green.
3. Improve first-run text and failure recovery.
4. Continue transcript quality passes on short replies, questions, acknowledgements, and anti-repeat.
5. Do a real zero-context tester run from the zip on a clean Windows user profile.
