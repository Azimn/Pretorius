# **Changelog**

All notable changes to Skales will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),

and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## v10.2.9

Hotfix for Organization task lifecycle.

### Bug Fixes

- **Instant abort for Organization tasks.** The Abort button now cancels in-flight LLM API calls within milliseconds via AbortController, instead of waiting up to 300s for the current call to time out. No more wasted tokens on aborted runs.
- **Project deletion stops running tasks.** Deleting a project with an active task now aborts the task before removing the project file, so the underlying Organization task stops cleanly.
- **Orphaned tasks cleaned up on startup.** Tasks left in 'running' state after a crash or restart are marked 'aborted' on next boot, so the UI does not try to resume dead tasks.

### Co-Pilot

Thanks to Niki (@NikiKeyz) for the SkyNet PR.


## v10.2.8

The v10.2.8 Stability Sprint. Eight days of focused work across the desktop chat surface, codework, settings UX, session persistence, and the new Skales Mobile app launch. Auto-updater pipeline unchanged. DNA invariants intact. Locale parity preserved at 12 × 4061.

### Skales Mobile is Live on Android

Skales Mobile is now publicly available on the Google Play Store for Android phones and tablets. Connect to your Skales Desktop instance over the encrypted relay for full feature access, or run the standalone mode with 27 native mobile tools. Install from the Play Store at https://play.google.com/store/apps/details?id=app.skales.mobile.

iOS is in review with Apple. The Play Store launch is the public beachhead; iOS lands the moment review clears.

### New Features

- **Memory Mode.** The setting formerly known as Token Compressor is now called Memory Mode, with clearer mode names (Always Remember, Compact, Minimal) and a more intuitive UI. Minimal mode moved behind an Advanced disclosure to prevent accidental selection. When the active mode is non-default, a small amber Brain badge appears in the chat header to surface the state and provide a one-click jump back to settings. Hidden in Mini Mode, matching the established pattern for Voice, Call, Share, and Incognito.
- **Codework resume banner.** When the most-recent Codework session was paused or interrupted, a dismissible resume banner appears at the top of the welcome view with Resume and Dismiss buttons. The banner replaces the previous Continue button as the canonical resume affordance. Dismiss transitions an active session to a stopped state and persists so the banner does not reappear.
- **Codework file-tree toggle.** New header button toggles the file tree pane visibility, with state persisted across restarts.
- **Codework recent sessions sorted by activity.** Recent sessions now sort by last-touched time instead of filename. Status badges expanded to five states: IN PROGRESS, DONE, ERR, STOPPED, or none for unknown.
- **Sidebar agent filter.** The sidebar now filters sessions by the currently selected agent, so clicking a session no longer reroutes to a different agent's context.
- **Open Folder button.** Codework projects now have an Open in Finder / Open in Explorer button in the project header.
- **OpenRouter as default provider.** New installs and first-time setups now default to OpenRouter as the primary provider for faster onboarding.
- **Tasks expand modal.** Long task results no longer truncate silently. Click any task to open a full-text modal.
- **Persona persistence.** Selected persona now persists across conversation restarts.

### Bug Fixes

- **Session write race condition.** Concurrent writers (chat page mid-conversation, buddy poll, mobile bridge, Telegram inbound, Spotlight) could previously clobber each other through a last-writer-wins pattern. A new per-session in-process mutex serializes all writers, and the mobile bridge specifically now re-loads the session at write time instead of overwriting with a pre-call snapshot. Race window shrinks from seconds-to-minutes down to milliseconds, effectively race-free for everyday use. Telemetry warns in DevTools console when an unexpected shrink occurs; Manual Compact logs an "intentional shrink" marker so the two are distinguishable.
- **Tool-only assistant turns no longer vanish.** When an assistant turn carried tool_calls but no captured tool results (interrupted mid-execution, rare race), the chat render filter previously dropped the row entirely. Those turns now render as an italic indicator listing the attempted tool names, preserving the conversation timeline.
- **Skill AI / GPT-5.x consolidation.** Edge cases in the GPT-5.x reasoning detection added in v10.2.7 are consolidated and covered.
- **Tool pruning logic.** A regression in tool-pruning for large conversations is corrected.


### Co-Pilot

Welcome **Niki (@NikiKeyz)** as Skales Co-Pilot. His first contribution landed during the v10.2.8 cycle. 

### Under the Hood

- 14 contributor and 6 public issues closed across the sprint.
- Three coordinated quick-fixes shipped alongside the main sessions.


## v10.2.7

Hotfix for three user-reported regressions surfaced after v10.2.6. No new product surfaces. Auto-updater pipeline unchanged. DNA invariants intact. Locale parity preserved.

### Bug Fixes

- **OpenAI GPT-5.x models failing with 400 errors.** v10.2.6 detected o1, o3, o4, and bare gpt-5 but missed every GPT-5.x dot-version. Detection now covers the full lineup (gpt-5.1 through gpt-5.5) with every documented suffix (mini, nano, pro, codex, codex-spark, chat-latest, thinking, instant) and every dated snapshot. Detection is also provider-agnostic, so GPT-5.x routed through OpenRouter, Custom Provider, or any OpenAI-compatible relay now sends `max_completion_tokens`, omits `temperature`, and folds `system` into `user` only for o1 family per OpenAI's current API documentation.
- **Custom Provider 404 errors.** The chat completions URL builder now detects existing version segments in your Base URL (like `/v4`, `/v1`, or a full `/chat/completions` path) and avoids appending duplicates. Z.ai (regular, coding, and OpenAI-compat endpoints), Groq, and other providers with non-default URL structures work out of the box. Same detection applies to model-list discovery.
- **Telegram proactive messages returning provider errors.** Friend Mode, Identity Maintenance, daily standup, and cron task completion notifications now share the same body builder as in-app chat. The previous split between the chat path and the Telegram-channel path is gone, so reasoning-model handling, system-message folding, and tool-array formatting stay consistent across every send site (chat, ReAct loop, autonomous-task fallback, and code builder).


## v10.2.6

Mini-release focused on critical user-reported bugs across the OpenAI and Gemini providers, sleep/wake recovery, capabilities awareness, and the Planner. No new product surfaces. Auto-updater pipeline unchanged. DNA invariants intact.

### Bug Fixes (Critical, User-Reported)

- **OpenAI provider 400 across all models.** OpenAI's chat completions API standardized on `max_completion_tokens` in late 2025; the legacy `max_tokens` field now returns 400 on current GPT models. Reasoning models (o1, o3, o4, gpt-5) additionally reject any non-default `temperature`, and o1 / o1-mini reject `system` role messages entirely. Skales now sends the correct field per model class and folds system content into the first user turn for o1.
- **Gemini tool calling broken (thought_signature missing).** Reasoning-enabled Gemini 2.5 Pro and Flash require the `thoughtSignature` Google returns alongside a `functionCall` part to be echoed back on the next turn. The Gemini adapter now captures and round-trips the signature, so multi-turn tool conversations no longer fail with HTTP 400.
- **Sleep / wake white screen.** Returning from system suspend or unlocking the screen no longer leaves the Skales window stuck on a blank page with "Application error: a client-side exception has occurred". Added `powerMonitor` listeners on suspend, resume, and unlock-screen that reload all live windows after a short delay so networking is fully back before the renderer fires its first request.
- **Capabilities awareness.** Skales is now aware of its own runtime UI features in the system prompt: KaTeX math rendering, HTML preview, code-block copy with `Ctrl+A` scoping, edit / branch / delete on user messages, manual compact, token-split tooltip, MCP servers, and the live skills system. Asking "can you do math?" or "can you render HTML?" now returns "yes" with the right context instead of an apologetic "no".
- **Planner: weekly schedules fired daily.** The day-of-week selection in the Planner modal defaulted to Mon to Fri, which made any "weekly" schedule fire on every weekday. Defaults now reset to a single day when the user switches to weekly, and `isTaskDue` validates day arrays defensively (coerces strings to numbers, rejects malformed entries) so the day filter actually applies.
- **Planner: BYDAY preferences ignored.** Same root cause as above. With the default reset and defensive coercion in place, picking "Monday + Wednesday" now fires only on Monday and Wednesday.
- **Planner: tasks not visible in Tasks list.** Recurring tasks created in the Planner now appear in the global Tasks page alongside one-off tasks. Each Planner run still produces its own execution entry with full logs.
- **Telegram proactive Friend Mode messages not firing since the Desktop Buddy proactive feature was added.** When the shared `resolveOutboundChatId` helper was introduced (which falls back to `telegram-state.json` when the in-memory `pairedChatId` is stale, e.g. after a Telegram bot restart), three proactive senders migrated to it (calendar reminders, autopilot morning briefing, buddy intelligence) but four sites in the autonomous runner were missed: Friend Mode check-ins, the autopilot approval notifier, the daily standup delivery, and cron-task completion notifications. From the user's perspective: Buddy proactive kept working through bot restarts via the fallback while Friend Mode silently died. All four sites now resolve the outbound chat ID through the same helper, so Buddy and Telegram fire in parallel as intended with their own cooldowns and conditions.
- **MCP Servers tab failing to load with 401 error.** UI status display now loads correctly while CLI auth boundary stays intact.

### Tech Debt

- Provider audit pass across all 13+ adapters. xAI default model bumped to `grok-3` (grok-2 was retired from xAI's chat completions endpoint in 2025). MiniMax base URL inconsistency between `actions/chat.ts` and `lib/provider-model-fetch.ts` documented for v10.3 review.


## v10.2.5

Stability and polish release across nine sprint sessions. No new product surfaces. Auto-updater pipeline unchanged. DNA invariants intact.

### Bug Fixes (Critical)

- **Skills system.** Fixed 29 broken reads in the orchestrator that caused all skill toggles to be silently ignored since the skills feature shipped. Computer Use Tools, Calendar Reminders, and other skills now actually function when toggled. (S2c)
- **UI Skill Toggles.** Toggles persist across restart. Previously, toggles appeared to switch on but reverted after reload due to camelCase / snake_case key drift. Silent backend errors now trigger UI rollback instead of being ignored. (S2e, OF-1)
- **Playground Override Persistence.** "Use active model" for per-mode overrides no longer springs back to the previous selection after Save. Root cause was Next.js Server Action serialization stripping `undefined` values. (S5 K1)
- **MCP State Reporting.** The model reports MCP server status correctly instead of always claiming "off". (S2a)
- **MCP Backend `listServersForCli`.** Returns real per-server status (disabled / connected / stopped / error) instead of hardcoded "connected" with 0 tools. (S2b)
- **Calendar Reminders Endpoint.** Was permanently skipped due to the broken `settings.skills` field. Now reads from skills.json correctly. (S2d C5)
- **Proxy Dispatcher.** All 12 provider sites use undiciFetch for proxy-aware HTTP. Runtime ECONNREFUSED errors with proxy enabled are gone. (S1 A2)
- **Token Cap Cloudflare / NVIDIA.** 32K → real 128K. (S1 A3a)

### Features

- **KaTeX Math Rendering.** Inline `$E=mc^2$` and block `$$...$$` render as actual mathematics. (S4b F1)
- **Edit / Branch / Delete on User Messages.** Hover actions on user bubbles. Edit triggers truncate-and-resend. Branch creates a new chat from that point. Delete removes the message and all subsequent responses. (S4b F2)
- **Manual Compact Button.** Compress chat history on demand instead of waiting for the 75% auto-threshold. (S4b F3)
- **Token Split Tooltip.** Hover the token badge to see "X.XK input / Y.YK output". Default display unchanged. (S4b F4)
- **Code Block Copy.** Hover any code block to reveal a Copy button. 2-second "Copied!" feedback. (S4b F5)
- **Code Block Ctrl+A Scoping.** Ctrl/Cmd+A inside a code block selects only that block, not the whole window. Works for Markdown code blocks AND HTML preview blocks. (S4b F6, S4d J3)
- **HTML Preview Copy.** Copy button added to the HTML preview header alongside Download HTML. (S4c I1)
- **Identity Maintenance Toggle.** Moved to its dedicated section in Settings (was buried under Agent & Tasks). (S5 K4)

### Stability / Polish

- **Header Responsive Layout.** Buttons collapse to icon-only below 1280px viewport with native tooltips on hover. Header no longer breaks on tablet portrait or narrow desktop. (S4d J1)
- **Provider Switcher Dropdown.** Anchors correctly in chat AND playground. No more left-side cutoff. (S4d J2)
- **Compact Button hidden in Mini Mode.** Mini Mode stays minimal. (S4c I3)
- **Compaction Few-Shot Memory.** Last 6 tool calls preserved as few-shot examples after auto-compact. (S1 A4)
- **MCPClient Error Sink.** Default error event handler prevents Node crashes when MCP servers misbehave. (S5 K10)
- **Token Display Tooltip.** Power users see input / output split, casual users see unchanged total. (S4b F4)


## v10.2.2

Combined hotfix + feature release. v10.2.1 was rolled into this version. Auto-updater pipeline unchanged. DNA invariants intact.

### Provider layer

- **Live model fetch for cloud providers.** Each provider card in Settings → AI Providers now has a Refresh button. Anthropic, OpenAI, Google Gemini, Groq, DeepSeek, Mistral, xAI, Together, MiniMax, Cloudflare, NVIDIA, SambaNova, and Cerebras all expose `/v1/models` (or the equivalent vendor-specific endpoint). Clicking refresh stores the live list in `settings.modelCache[provider]`. Model dropdowns prefer the cached list when present and fall back to the built-in baseline. New models become usable without a Skales release.
- **User-configurable model limits.** New collapsible "Override Model Limits" section under AI Providers. Add per-(provider, model) override rows for context and output token caps. Use `*` as the model name to apply the limit to all models of that provider that don't have an explicit override. Useful for newly released models whose limits differ from the built-in registry. Resolves ahead of the static registry in `lib/model-limits.ts` via a 5s in-process cache of `settings.modelLimits`.
- **Per-provider proxy now actually routes.** v10.2.0 declared the feature but the dispatcher was not reaching the fetch calls — the standalone Next.js build did not include the undici package, so the runtime require returned undefined silently. Fixed by making undici an explicit dependency, switching to a proper import, and externalizing it in the Next.js webpack config.

### Chat

- **Manual message delete persists across reload and restart.** v10.2.0 trimmed the in-memory message array but did not propagate the deletion to disk. The delete handler now explicitly saves the trimmed session.
- **Branch action creates the correct slice.** v10.2.0 surfaced the Branch hover button on chat bubbles but the slice index was wrong — the new session contained a different subset than the user expected. Fixed end-to-end: clicking Branch on the Nth message creates a new session with messages 1 through N inclusive.
- **Bubble action labels and toasts are translated.** A handful of `chat.bubble.*` locale keys shipped without translations in v10.2.0 — the Branch toast and the Delete confirmation showed raw key strings. Real translations added across all 12 locales.

### Settings

- **Per-Mode Model Override UI uses real provider data.** The dropdowns previously showed a hardcoded curated list regardless of what the user had configured. Now mirrors the chat header picker: provider list shows only enabled providers with API keys, model list reflects the user's configured plus live-fetched models for the chosen provider.
- **Playground on-page picker and Settings Per-Mode Override share the same source of truth.** Both write to `settings.modeOverrides.playground` and re-hydrate from disk on every settings-change event.
- **The "?" agents-info trigger no longer appears on the Settings page.** It belongs on the Agents page only — which is unchanged.

### UI

- **Chat and Playground header model pickers hide on small viewports.** Below 768px the picker is hidden entirely, matching the existing Mini Mode behavior.

### Notes

- Auto-updater code path is unchanged.
- DNA invariants intact.
- Schema additions (`modelCache`, `modelLimits` on top of v10.2.0's existing additions) are all OPTIONAL with safe defaults. Existing `settings.json` files load unchanged.
- 12-locale parity preserved.


## v10.2.0

Iterative quality release across providers, modes, error UX, and chat history. No new product surfaces; existing surfaces become more resilient and configurable. Auto-updater pipeline unchanged. DNA invariants intact.

### Provider layer

- **Per-(provider, model) limits registry.** Context window and max output tokens are now read from a per-provider, per-model registry (`lib/model-limits.ts`). Replaces the previous hardcoded ceilings (32K context fallback, 2048-4096 output) across orchestrator, chat, autopilot, code-builder, browser-control, autonomous-runner. Models that the registry doesn't know about fall through to per-provider defaults, then to a conservative absolute fallback. Live HF Router context_length values can override the static entry at call time.
- **Smart context compaction with LLM summary.** When effective context exceeds 75% of budget, older turns are summarized via a single low-cost LLM call (`noTools: true`, 600 tokens) instead of being truncated to 280 characters per message. Falls back to the truncation behaviour if the summary call fails. Compaction is now reached by the orchestrator: upstream `slice(-40)` and `slice(-20)` history caps in chat.ts and chat/page.tsx are replaced with a byte-budgeted slice (default 8MB / 4MB / 1MB / 512KB depending on entry path).
- **Provider error translation.** New `lib/error-translator.ts` converts raw 5xx/4xx response bodies into actionable user messages. Pattern rules cover Ollama (`llama runner has terminated`, `cuda out of memory`, missing models, unreachable host), OpenRouter (rate limits, missing credit), Anthropic (context exceeded, auth failed), OpenAI (context exceeded, quota). Each translation carries a `toastAction` (continue / retry / compact / switch-fallback / open-settings) consumed by chat error toasts. Generic fallback annotates with provider+status for debug logs.
- **Per-provider proxy support.** Provider configs accept an optional `proxy: { enabled, url }` field. When set, requests to that provider go through an Undici `ProxyAgent` dispatcher. Cached per URL. Wired into chat.ts (OpenAI-compatible / Anthropic / Google) and orchestrator.ts (agentDecide hot paths). Schema change is additive — existing settings.json files load unchanged.
- **Multiple custom OpenAI-compatible endpoints.** New `customProviders[]` array in settings supports more than one custom endpoint at a time. Legacy `providers.custom` continues to work and is mirrored as `customProviders[0]` via a one-shot migration. Settings UI exposes label, base URL, API key, model, enabled toggle, tool-calling and vision toggles per entry.

### Modes

- **Per-mode model resolution contract.** New `lib/mode-routing.ts` resolves which (provider, model) each Skales mode uses: explicit caller override → `settings.modeOverrides[mode]` → `settings.activeProvider/model`. Wired into Playground (askAI + generate), Buddy chat, and exposed in Settings as a "Per-Mode Model Overrides" panel covering Chat, Codework, Organization, Studio, Playground, Buddy, Spotlight.
- **Playground respects active model.** The previous silent override to Anthropic Sonnet 4.5 is now an opt-in setting (`playgroundQualityBoost`, default OFF). When OFF, Playground uses the active provider/model or the per-mode override. Toggle and per-conversation provider/model picker now live on the Playground page header.
- **Inline model picker in Chat.** Compact icon-only button in the chat header opens an absolute overlay popup listing installed providers and curated models. Layout never shifts. Solid background respects the active light/dark theme. Custom agent provider/model wins over the global default in the "Use agent default" row. Picker is removed entirely in Mini Mode.
- **Chat command `/model <id>` persists.** The slash command now writes the new model to `providers[activeProvider].model` via `saveAllSettings`, refreshes the local settings state, and surfaces a clear error bubble on save failure. Cached invalid model IDs no longer survive `/model` switches.

### Tools and routing

- **Calendar routing disambiguation.** `create_calendar_event` description anchors against natural-language calendar phrasing ("in Google Cal", "in den Kalender", "gcal", "schedule a meeting at TIME"). `planner_create_task` description clarifies it is for autonomous Skales prompts, not human appointments. TASK ROUTING RULES block in the orchestrator system prompt gains a Calendar branch above the Planner branch. Internal Planner is unchanged in scope.
- **Tavily gate respects user toggle.** When the Tavily skill is disabled or no Tavily API key is configured, `search_web` is filtered out of the tool manifest before it reaches the LLM. Other web tools (`fetch_web_page`, `extract_web_text`) remain enabled.
- **Tool prune for low-TPM providers.** `shouldPruneTools` auto-fires for providers with TPM ceilings under 15K (Groq 12K), pruning the 136-entry tool manifest from the request even when the user message is non-trivial. Prevents 413 "request too large" errors on free tiers.

### Autopilot

- **Master Switch persists from the Autopilot page.** Toggling the master switch from the Autopilot page (not just Settings) now persists `isAutonomousMode` atomically alongside heartbeat start/stop. Activate-on-Start works from a clean restart regardless of which UI surface the toggle came from.
- **Friend Mode observability and persistent cooldown.** Every early-return point in `tickFriendMode` emits a structured `console.warn('[friend-mode] skip', { reason, ... })`. Tick start logs the active behavior. The in-memory `friendModeLastSentAt` Map is hydrated from settings on first tick and persisted after each successful send so app restarts no longer reset cooldown.
- **Identity maintenance auto-approve.** Optional Settings toggle. When enabled, the 3 AM identity maintenance job bypasses the safe-mode and critical-action approval gates. Logged to the audit trail. Default OFF.

### Chat UX

- **Manual delete and branch from any message.** Hover actions on user and assistant message bubbles. Delete trims trailing tool messages so no orphan tool results remain. Branch creates a new session populated with messages up to the chosen point.
- **Resume action on error toasts.** Provider error toasts now carry an action button keyed by the error type: continue / retry on Ollama crashes and timeouts, compact on context-exceeded errors, switch-fallback on rate limits and quota errors, open-settings on auth failures. Wired into orchestrator errors, vision flow errors, and approval-result errors.
- **Continuation on output truncation.** When a model returns `finish_reason: 'length'` with no further tool calls, the orchestrator injects "Continue from where you left off." and re-enters the ReAct loop instead of dropping the partial answer.

### Settings UX

- **3-fallback UI cap removed.** The Fallback Chain section accepts as many entries as the schema does. Comment in `actions/chat.ts` updated.
- **Per-Mode Model Overrides panel.** New section under AI Providers. Per mode: "Use active model" toggle plus provider+model dropdowns when the override is on. Empty entries are not persisted.
- **Additional Custom Providers panel.** New section under Advanced. Each entry is a card with label, base URL, API key, model, enabled, tool-calling, vision toggles. Add and remove buttons. Legacy single-slot Custom Provider UI continues to work.
- **Identity Maintenance Auto-approve toggle.** Lives under Autopilot in v10.2.0. Will move under Settings → Memory near the existing identity maintenance controls in a follow-up.

### Discover

- **Layout fixes for long-form posts.** Post text wraps on long URLs (`break-words` + `overflow-wrap: anywhere`). Category badges truncate at 140px. Header row wraps when content is wide. Skales Insider posts no longer break the feed layout.

### Telegram

- **Approval flow shows continuation hint.** After a Telegram-approved tool runs, the result message ends with "🔄 Tap or send Continue to resume." so the user knows the agent flow is paused, not finished. Optional `telegramApprovalAutoResume` setting (default OFF) lets the agent re-enter the ReAct loop automatically; on by user choice only, since it carries the historical risk of v7.2.1 infinite-loop regression.

### Updater notifications

- **Changelog field carries through IPC.** `electron/updater.js` now emits both `releaseNotes` (legacy) and `changelog` (new) on the IPC payload for `update-available` and `update-downloaded`. Renderer reads either. Update page renders the changelog whether the trigger came from server-side check or auto-detect IPC.

### Localization

- 11 new `system.errors.*` keys for the provider error translator, plus 3 new `chat.modelPicker.*` keys for the inline picker, with real translations across all 12 locales (de, en, es, fr, hr, ja, ko, pt, ru, tr, vi, zh). Locale parity 12 × 3989.

### Internal

- New shared utilities: `lib/model-limits.ts`, `lib/byte-budgeted-slice.ts`, `lib/mode-routing.ts`, `lib/error-translator.ts`, `lib/proxy-dispatcher.ts`, `lib/custom-providers.ts`, `lib/curated-models.ts`.

### Notes

- Auto-updater code path is unchanged. The `electron/updater.js` edit is one additive line emitting `changelog` alongside the legacy `releaseNotes` field.
- DNA invariants intact.
- Settings schema additions (`customProviders`, `modeOverrides`, `playgroundQualityBoost`, `identityMaintenanceAutoApprove`, `telegramApprovalAutoResume`, `proxy` per provider) are all OPTIONAL with safe defaults. Existing settings.json files load unchanged.
- BSL `_responseQuality` consumption hardened: authorized builds get the full registry value, unauthorized builds are hard-capped at 4096 tokens regardless of model.

---


## v10.1.1 - Hotfix

Five hotfix items rolled up on top of v10.1.0 Design. No new features, no architecture changes.


### Vision routing

- **Vision-capable model detection extended.** Gemma 3 (4B / 12B / 27B), Gemma 4 (E2B / E4B / 26B / 31B), LLaVA (7B / 13B / 34B / llama3 / phi3), Pixtral (12B / large), Qwen-VL / Qwen2-VL / Qwen2.5-VL, and MiniCPM-V are now recognised. Image inputs route to the configured vision model correctly.
- **Explicit user override is respected.** If the user has set a Vision Model in Settings, it is used regardless of whether auto-detection knows about it. Auto-detection becomes a fallback, not a gate.
- **Tool-call hardening.** Malformed tool-call JSON is parsed forgivingly (extracts the first valid JSON block from prose). On total failure, one explicit retry asks the model to return valid tool-call format. Tool execution errors are surfaced into the next LLM turn so the model cannot silently report success on a failed write or denied permission.

### Chat history

- **Mobile-origin badge.** Messages synced from Skales Mobile now show a small phone icon next to the timestamp. Tooltip reads "Sent from Skales Mobile". Read-only visual cue, no behavioural change.

### Autopilot

- **Activation modes.** Settings on the Autopilot page now include an Activation Mode picker with three options: Manual (legacy default - last toggle persists), On Startup (heartbeat starts automatically when Skales launches if the master switch is on), and Time Window (heartbeat is active during a user-defined daily 24-hour window such as 09:00 - 17:00). Time windows that cross midnight are supported (e.g. 22:00 - 06:00). Manual toggles inside an active window are respected for the rest of that window so a user can pause without fighting the auto-activation. Existing v10.1.0 users default to Manual and see no behaviour change unless they explicitly switch modes.


### Notes

- Auto-updater code is unchanged.
- DNA invariants intact.

---

## v10.1.0 "Design" 

The biggest creative update yet. Skales Studio gets a Design Tab that turns prompts into real HTML/CSS designs. Codework matures into a full autonomous coding agent. HF Spaces and MCP servers now work everywhere. Smoother animations across the app.

### Studio

- **Studio Design Tab** is the new first tab in Studio. Type a prompt, pick a template (Landing Page, Dashboard, Mobile Screen, Pricing, Hero, Login, Settings), get production-ready HTML + CSS + Tailwind back. Live preview iframe, palette extraction, font extraction, fullscreen preview mode (Escape to exit), inline refine drawer, recent designs dropdown. Designs persist between sessions (50 designs FIFO).
- **Studio Image Generation revival.** HuggingFace image provider now routes through the Inference Providers Router (`router.huggingface.co`) with two-attempt fallback chain. The legacy 404 issue is fixed for SDXL, FLUX, and other models. Clear error messages on failure with provider-fallback suggestions.
- **HTML extraction made robust** against five common LLM output variants (fenced, unfenced, with or without DOCTYPE, truncated). Smaller models that don't follow exact output format still produce usable designs.
- **View Transitions API** for smooth tab crossfades in Studio (Chrome, Edge, Safari TP). Graceful fallback on Firefox.

### Codework

Codework matured significantly across the v10.0.4 to v10.1.0 cycle. It is now a full autonomous coding agent, not just a chat with file tools.

- **Approval Gates.** Three new toggles: auto-approve writes, auto-approve exec, auto-approve all. Pending-approval map with `/api/codework/approve` endpoint. Conservative defaults (Review mode) for first-time users.
- **Forbidden command denylist** expanded from 5 to 17 patterns plus 8 dangerous pipe-pairs. Common destructive operations (`rm -rf $HOME`, fork bombs, dd to disk, etc.) blocked at orchestrator level even with auto-approve enabled.
- **Test loop with progress guardrail.** New `testCommand` field per session lets Codework run tests after each code change. After 3 consecutive test failures with no progress, the loop aborts and Codework reports back instead of grinding tokens.
- **Preview Mode** for write operations. Write tools generate diffs that surface to the user for accept/reject before applying. New SSE events `tool_pending_write`, `tool_write_accepted`, `tool_write_rejected`.
- **MCP tool consumption.** Connected MCP servers are now exposed as tools to Codework. Tool naming `mcp_<server>_<tool>`. Conservative auto-approval gating.
- **Repository-map indexing.** Codework now builds a project-wide map (functions, classes, exports per file) cached in `.skales-backup/repo-map.json` with SHA-256 Merkle root for fast invalidation. Scales to 500+ files. Regex-based parser (tree-sitter AST upgrade coming in v10.2).
- **Long-context tiers.** For huge projects, Codework adapts: full tree+keyfiles+repomap under 50 files, tree+repomap up to 500, directory-only+repomap above 500.
- **Token usage tracking.** Live token counter shown in Codework with indigo pill, updates per LLM call via new `usage` SSE event.
- **Commit-message generator.** New helper drafts commit messages from staged changes following Conventional Commits style.

### Cross-tool integration

- **HF Spaces and MCP everywhere.** Activated HF Spaces and connected MCP servers are now usable from Chat, Codework, AND Studio. Add a Space once, use it anywhere.
- **Active Tools Across Skales** panel in Settings shows which tools are available in which surfaces (Chat / Codework / Studio).
- **Studio HF Spaces invocation endpoint** (`/api/studio/space-invoke`) lets Studio invoke any active Space directly.

### Lio AI

- **Recursive project snapshot.** Lio AI now builds a complete file map of your project on each plan/build cycle (max depth 10, max 2000 files). Better context awareness for multi-file changes.
- **Plan context** assembled from project structure + chat history for higher-quality plans.

### Chat

- **Token-pruning heuristic** for short queries widened to 120 characters (was 80). Queries like "Explain X with examples" keep their tools instead of being stripped to plain chat.
- **gpt-tokenizer integration.** Exact token counting for context-budget calculations across providers (Groq 12k, Mistral 30k, Cerebras 32k, SambaNova 16k). Tools are pruned only when they actually exceed provider TPM ceilings.
- **Smoother streaming token rendering** via batched requestAnimationFrame updates.
- **Update toast localized** in all 12 locales with version interpolation. Update detail page renders the actual changelog from `latest.json`.

### Stability

- **JSON I/O hardened.** All JSON reads/writes across 65+ files migrated to `readJsonSafe`/`writeJsonSafe` helpers. Handles malformed JSON, missing files, BOM characters, partial writes.
- **Hierarchy clarity in Organizations.** Orchestrator override fix for the Person/Agents/Organization confusion that affected multi-agent setups.
- **Namesake trope removed** from all 12 locales (was a phrase pattern that drifted across localizations).
- **Codework session sidebar** active-state correctly handles trailing slashes (no more two-row highlights).
- **createSession testCommand wiring** fixed: the field now persists correctly into session metadata.
- **Delete session prompt** now properly localized in all 12 languages.

### Internal

- **Codework runtime extracted.** `runCodeworkAgentLoop` and `buildCodeworkSystemPrompt` moved from `/api/codework/run/route.ts` (488 LOC) to `actions/codework.ts` (route now 102 LOC). Cleaner separation, easier to reuse.
- **LIO_IGNORE_NAMES expanded** from base list to 25 entries covering more build artifact directories.
- **Codework dogfood `.skales-backup`** directory cleaned and gitignored.
- **Stale `/lio` and `/lizard`** slash-command references removed from chat system prompts.
- **Debug log noise** from token-bloat and tool-prune instrumentation removed.
- **Auto-updater** continues stable since v10.0.3.

### Mobile

- **Outbox foreground sync** via AppState listener. Pending messages flip to "failed" immediately when app resumes (was up to 10 seconds of background drift before).
- **Periodic 10-second sweep** keeps outbox state fresh during active chat use.

### Landing

- **Migration sources** in v902 Canvas Office blog and Migration Importer feature now mention OpenClaw, Hermes, and Cherry Studio alongside ChatGPT/Claude/Copilot/Gemini.
- **Three new feature blocks**: ComfyUI (local image generation), HF Inference Providers (200+ models), DeepSeek V4 (1M context, agent-tuned).

### Note on Lio AI export from Studio

The "Open in Lio AI" export from the Studio Design Tab was investigated and removed during the V102 dev cycle. Lio AI's `/code` page does not currently consume `?project=<id>` URL params, and Lio AI is fundamentally a different workflow (architect-reviewer-build loop) than Studio Design's static HTML iteration. Lio AI is left 100% untouched. Use the Download HTML button instead.


## v10.0.4 — April 20, 2026

### Telegram Integration
- **Fixed**: Safe Mode approval flow broken since v9.x. Tool approvals from Telegram now correctly trigger the approval prompt and execute on your "yes" response (GitHub #77)
- **Fixed**: Telegram bot no longer requires opening the chat page after app launch to come online. Bot spawns automatically 3 seconds after server ready (GitHub #78)

### Provider Presets
- **Added**: Minimax, Cloudflare Workers AI, and Nvidia NIM as first-class provider presets with pre-filled endpoints. No more manual Custom OpenAI-Compatible configuration needed for these (GitHub #76)
- **Added**: "Show only active" toggle in the Providers list to hide unused providers

### Chat & UX
- **Added**: Response time display on assistant messages — see how long each response took (GitHub #61)
- **Added**: Global hotkey `Cmd+Shift+H` (macOS) / `Ctrl+Shift+H` (Windows/Linux) to toggle Desktop Buddy visibility. Handy for fullscreen video (GitHub #60)
- **Improved**: Settings search now covers more sections, handles accents (é matches e, ä matches a), and has better keyword coverage in German/Spanish/French/Russian (GitHub #59)
- **Improved**: Fallback provider banner reworded for clarity with a details modal explaining why the fallback activated and how to fix the primary (GitHub #70)

### Export & Remote Access
- **Fixed**: Export via Tailscale or remote browser access no longer returns a corrupted HTML file instead of a ZIP. Content-Type headers, MIME validation, and error handling properly hardened across the HTTP route
- **Unchanged**: Native Electron Export remains the same, ~13MB ZIP with manifest and `.skales-data/` — no regression

### Email
- **Improved**: Outlook/Gmail/Yahoo IMAP authentication errors now explain the App-Specific Password / OAuth2 requirement (Microsoft disabled Basic Auth in 2022) instead of showing a generic "auth failed" message

### Build & Infrastructure
- **Fixed**: `build-info.json` now correctly reports the current version. `scripts/build-id.js` is now invoked as the first step in `scripts/release-build.sh` on every release (GitHub #79)

### Locales
- All 12 locales (en, de, es, fr, hr, ja, ko, pt, ru, tr, vi, zh) updated with v10.0.4 strings — informal register maintained

---

## v10.0.3 — Stability (April 18, 2026)

### Bug Fixes
- **Bonjour/mDNS Collision** — instance name now includes PID (`Skales-<hostname>-<pid>`); multiple Skales instances on the same machine no longer shadow each other in swarm discovery
- **Multi-Agent Dispatch Toast** — completion notification was silently dropped after all subtasks finished; now fires a purple 🦁 toast with job title + subtask count (7 s display duration)
- **Update Page i18n** — "Later" button showed raw key `update.later` instead of translated text; `later` key added to all 12 locale files
- **Ollama Small-Model Warning** — settings panel now shows an orange warning when a known small model (≤3B params) is selected with `Max tools > 0`, advising the user to reduce tools or switch to a larger model
- **fal.ai Studio Hang** — video generation polled a manually constructed status URL that broke when fal.ai changed their queue URL structure; client now uses `status_url` / `response_url` from the submit response with fallback to constructed URLs
- **Codework UI Lag** — blank activity panel during 1–2 s SSE startup gap replaced with an optimistic "Starting session…" phase entry so the UI never appears frozen

---

## v10.0.2 — 2026-04-18

### Fixed

- **Tool Filter Regression (critical).** Disabled the context-aware tool filter introduced in v9.2.1 which was silently dropping core tools (`send_telegram_message`, `write_file`, `create_directory`, and others) based on keyword matching. This was the root cause of the widespread "Unknown tool" errors users reported after v10.0.0. All tools are now available in every conversation until a safer allow-list approach lands in v10.1.
- **Export Dialog "Source path not allowed".** The `copy-file` IPC handler now accepts `os.tmpdir()` as a valid source path. In v10.0.1 the export bundle ZIP was written to the OS temp directory, but the handler's whitelist only permitted `DATA_DIR`, so exports failed silently.
- **Multi-Step Exit Guard.** The ReAct loop now only exits when the model returns zero tool calls. Previously, response text emitted alongside tool calls (e.g. "let me continue with...") was incorrectly interpreted as an exit signal, breaking legitimate multi-step tasks.

### Improved

- **`SAME_TOOL_NAME_HARD_CAP` raised 3 → 15.** The per-turn cap on how often a single tool name can be called was too low to support normal bulk operations (creating 5+ folders, writing 10 files). Infinite loops with identical arguments are still caught by stall detection (2 identical calls) and the dedup tracker (2 identical args), so this only affects legitimate bulk work.
- **Progress-Speak Auto-Continue.** When a model made tool calls in previous iterations but then stops with short progress-style text ("let me continue", "jetzt erstelle ich die restlichen", "remaining", etc.), the orchestrator now automatically re-prompts it to finish via tool calls instead of exiting the loop. Mitigates Sonnet 3.7's mid-task pause behavior on bulk operations.

### Known Issues

- Sonnet 3.7 may still pause on 5+ item bulk tasks despite auto-continue. Use Claude Opus 4, Sonnet 4, or Minimax for guaranteed single-shot bulk execution.
- Minimax models may emit tool calls as JSON text in chat instead of real function calls for some prompts.
- Codework UI may appear frozen for 1–2 seconds before streaming catches up; backend is working.
- Multi-Agent Dispatch toast notification is not currently firing; check the Tasks tab for progress.

---

## v10.0.1 — Hotfix (April 17, 2026)

### Critical Fixes
- **Auto-Updater Schema Mismatch** resolved — flat and nested feed schemas now both accepted; zero successful auto-updates from v10.0.0 was caused by this. Users on v9.x must install v10.0.1 manually once.
- **Export/Import** — valid zips with schema version, manifest, credential redaction; accepts legacy formats for backward compat
- **Multi-Step Tool Chains** — no longer exit prematurely when the model returns empty text between tool calls
- **Advisor Strategy** — simple chat no longer routed through the expensive Planner (complexity gate)
- **Gemini Tool Schema** — stripped OpenAI-specific fields that Gemini silently rejected; tool calls restored for Gemini models
- **create_task vs planner_create_task** — disambiguated via explicit tool descriptions + system prompt routing rules

### Integrations
- Telegram outbound Chat-ID persisted across bot restarts (`telegram-state.json`)
- SMTP Test race condition eliminated
- Lio AI provider dropdown labels no longer show raw i18n key paths
- Custom endpoint URLs (LM Studio, Ollama) normalized case-insensitive at fetch time

### Agent Behavior
- System prompt now explicitly authorizes file access (prevents "Systembeschränkungen" hallucinations)
- Folder structure creation supports recursive paths in a single tool call
- Internal diagnostics protocol — agent checks local state before suggesting support

### Configuration
- New Setting: Request timeout (30–600s slider) for long agent tasks
- Emoji loader negative-cache — no more 404 spam on codepoints without Lottie

### Discover Feed
- 29 v10 event templates now render custom text from client payloads

### Security (PHP + WordPress)
- Feed backend: IP hashing aligned with SHA-256 + salt across all endpoints
- WordPress Connector 1.2.1: timing-safe token compare + file upload MIME allowlist

### Localization
- 132 new `settings.providers.*` keys across 12 locales
- 4 chat state messages moved from hardcoded English to i18n
- requestTimeout setting fully translated

### Known Issues
- Reasoning display still abbreviated (deferred to v10.1)
- macOS notarization not yet implemented — right-click → Open required on first launch
- SSH key authentication in SSH tool deferred to v10.1

### Upgrade Path
Auto-update works from v10.0.1 onwards. Users on v9.x: download manually from skales.app once.

---

## v10.0.0 — "Closing the Gap" (April 16, 2026)

The biggest Skales release ever. Desktop + Mobile + Relay now form one ecosystem: every message you send from your phone routes through Desktop's full tool set, every capability you build on Desktop is reachable from the Mobile companion. Chat feels smoother. Studio speaks video. Settings speaks voice.

### Skales Mobile (NEW)
- Official Skales Mobile app for Android (iOS coming) — submitted to Play Store (beta, closed testing)
- Full standalone AI agent in your pocket — 27 mobile tools, works with or without the desktop running
- Remote Mode: pair via QR over the end-to-end encrypted relay (wss://relay.skales.app, TweetNaCl box, keys never leave the devices)
- Paired phones get full access to THIS desktop's agentDecide pipeline — all 139+ tools (shell, files, browser control, email, calendar, Studio, etc.)
- Image upload from mobile now forwards through the bridge as OpenAI-vision multimodal content (Desktop Vision-capable providers analyze it like a local upload)
- Shared ecosystem: same Discover Feed, same Custom Agents, same Skills

### Studio — LTX-2.3 Video Generation (NEW provider)
- fal.ai LTX-2.3 integration (text-to-video + image-to-video, standard and fast variants)
- $0.06/sec at 1080p, native 9:16 portrait support, 5s and 10s durations
- Added alongside existing Veo/Kling/Runway/Replicate providers — shares the same Cloud-Render pipeline
- Live "Connected" badge in Studio when fal API key is configured in Settings
- 4 new localized model labels (`studio.falModels.textToVideo`, `imageToVideo`, `textToVideoFast`, `imageToVideoFast`) across all 12 languages

### Animated Emoji System (NEW)
- Noto Color Emoji font bundled — all Unicode emojis now render identically on Windows, macOS, and Linux
- 16 brand and expressive emojis with smooth Lottie animations served from Skales CDN
- Animated splash screen — Gecko mascot animates during app startup
- Dashboard wave — hover over the greeting hand for a welcome animation
- Discover Feed spark picker — emoji reactions animate on hover, play once in the sent confirmation
- Chat expressiveness — AI messages with creative, memory, video, or web context emojis animate on arrival
- Big emoji messages — send 1-3 emojis alone and they render larger with animation (iOS/Telegram style)
- Easter egg shortcuts in chat: `:gecko:`, `:bubbles:`, `:paw:`, `/highfive`, `/bow`
- Emoji privacy controls — optional Google CDN fallback in Settings → Privacy (off by default, GDPR compliant)
- Emoji preloading — brand emojis cached on app start for instant rendering

### Voice — TTS + STT
- OpenAI TTS provider added (voices: alloy, echo, fable, onyx, nova, shimmer) — reuses the existing OpenAI provider key, no extra setup
- New "Read responses aloud" toggle in Settings → TTS — when enabled, every assistant reply is spoken via the configured provider once streaming completes
- Smart markdown stripping before TTS so the voice doesn't read ``` or # out loud
- Per-message speaker button on every assistant bubble — click to listen, click again to stop, visible on hover next to Copy
- Groq-key hint in the STT section (free Whisper access) with one-click jump to AI Providers tab
- All voice UI fully localized (readAloud, stopReading, autoReadLabel, autoReadHint, etc. in 12 languages)

### Chat — Smoothness & Inline Preview
- Message entrance via Framer Motion spring (stiffness 320, damping 30, mass 0.9)
- AnimatePresence with initial=false — session restores stay instant, only NEW messages fade-lift in
- Typing indicator rewritten from translate-bounce to smooth wave (scale + opacity, 1.2s loop, 160ms stagger)
- Typing bubble itself now fades in instead of popping, same style preserved when the agent transitions to tool-status so the indicator never "jumps"
- Scroll-to-bottom FAB: lime-circle appears bottom-right when user scrolls up ≥200px from latest; click returns to live view + re-enables auto-follow
- Inline HTML Preview: ```` ```html ```` fenced code blocks now render a sandboxed live iframe with Show Code / Download HTML / Save as Image / Mute / Hide toggles
- Global mute + hide persist across all chats and sessions via localStorage — "34 webviews, 0 audio" on a single click
- Save as Image — pixel-exact region capture, works on sandboxed iframes
- Global `prefers-reduced-motion` CSS guard — OS-level accessibility setting disables all animations across the app

### Capabilities & System Prompt
- APP_VERSION bumped to 10.0.0, APP_VERSION_NAME = "Closing the Gap"
- System prompt (Level 0 + Level 1) now explicitly knows about Mobile pairing, Inline HTML Preview, the fal.ai video path, Remote API, and the 12 supported languages
- Agent is proactive: when the user describes something visual (chart, card, map, SVG, mini-app), it offers inline preview in the user's language before producing it
- capabilities.json emits a live `mobile` block per rebuild (pairedDeviceCount, relay URL, E2E method, paired-device summary)
- 6-theme mention (Dark / Light / Midnight / Forest / Amber / Glass) added to prompt

### Bug Fixes
- Buddy window draggable on Windows via native mousedown cursor tracking (screen.getCursorScreenPoint delta → setPosition), plus Cmd/Ctrl+Shift+B global reset shortcut
- Agent delete button fixed — sandboxed Electron renderer silently drops window.confirm(); replaced with a two-click armed-state confirmation directly in the button
- Playwright chromium detection now sorts descending and matches both `chromium-NNNN` and `chromium_headless_shell-NNNN` layouts — always picks the newest version installed
- Telegram bot auto-restart watchdog: child.on('exit') listener with rate-limit (max 3 respawns per rolling hour), 5s backoff, respects current config enable/disable
- feed.php `update_profile` now also updates the `tag` field in-place when a new gamertag is requested and not taken by another user — no more duplicate JSONL entries
- Next.js proxy forwards the new tag in the update_profile JSON body so feed.php can rename in place
- `settings.stt.help` i18n key re-applied (was hardcoded back to raw text)
- Hardcoded "(API key required)" badge in Studio video-provider dropdown moved to `studio.apiKeyRequired` i18n key (12 languages)
- `build-info.json` bumped from 9.3.0 → 10.0.0 so boot log matches package.json
- External links in browser view now open in the default OS browser
- Share window overlay responds to Escape key across chat, spotlight, and buddy
- Browser scroll-to-bottom works reliably on lazy-loading and single-page applications
- Playbook steps now wait for actual page load before proceeding
- macOS screen recording permission detected with user-facing guidance in share window

### Under the Hood
- New fal.ai queue-based client with defensive URL extraction and 6-minute timeout
- HTML preview preference sync across all open windows
- Shared Framer-Motion animation presets (spring, stagger, fade variants)
- New IPC channels for Buddy drag and region capture
- Global CSS for typing wave dots, scroll FAB, and reduced-motion guard
- Noto-COLRv1.ttf (4.8 MB, vector emoji font) replaces platform-specific emoji rendering
- Emoji loader with 5-tier cache: memory → IndexedDB → VPS → Google fallback (opt-in) → Unicode
- SkalesEmoji React component with loop/once/static/hover animation modes
- 12 locale files reach perfect parity: 3546 keys each, zero missing, zero extra

### Localization
- ~60 new i18n keys added across 12 languages (en, de, es, fr, hr, ja, ko, pt, ru, tr, vi, zh) for fal.ai models, HTML preview, voice, Mobile, animated emojis, mute/unmute, scroll-to-latest
- All new German keys are Du/Sie-neutral (Infinitiv + Substantiv form — "Vorlesen", "Stumm", "Als Bild speichern")
- ALL new user-facing text goes through the i18n system — zero hardcoded English in new code


## v9.3.0 — Stability Release (April 13, 2026)

### Stability
- BOM-safe JSON reading across entire codebase — Windows crash fix (readJsonSafe utility, 65+ files migrated)
- Playground Bridge rewrite — duplicate injection removed, unique marker guard, no more dead buttons
- Auto-updater fix — download button now appears correctly, race condition resolved, event name mismatch fixed
- Advisor model validation — auto-corrects model IDs for OpenRouter, falls back to primary model on 400/404
- Playground max_tokens increased to 8192 (chat stays at 4096)
- Skill loading: 28 individual MODULE_NOT_FOUND errors → 1 summary line
- Integrity check downgraded from stderr warning to info log
- Discover Feed null-safe .match() calls — admin posting no longer crashes

### Memory & Intelligence
- Identity Maintenance untouched (runs at 3:00 AM as always)
- Memory Consolidation staggered to 3:30 AM — no overlap with Identity Maintenance
- Knowledge Graph shows helpful hint when empty instead of "0 entities"
- Agent system prompt updated to v9.3.0 with all current features

### Browser & Automation
- Persistent browser sessions via launchPersistentContext — logins survive restart
- Playwright install no longer relies on bare npx — resolves binary from node_modules/.bin/

### Localization
- Removed all "Coming in v9.2.1" text from 12 locale files
- Version strings corrected to 9.3.0 everywhere (was 9.0.1 in build, v6.0.0 in telemetry)


## v9.2.5 — "WordPress 2.0" + Playground (April 13, 2026)

### WordPress 2.0
- 96KB WordPress Design Skill bundled with 15 Elementor + 10 Gutenberg templates
- Elementor Flexbox Container format (fixes blank pages on modern Elementor)
- elementor_canvas page template auto-set
- Web search available in WordPress agent
- Selective skill injection (reduces prompt from 96KB to ~13KB)
- WordPress Connector Plugin v1.2.0 with collision detection

### Playground (Beta)
- Deep onboarding interview: 15 questions, 4 phases
- AI-powered personalized Space suggestions
- Glassmorphism UI with animated mesh background
- AI-generated interactive Spaces with localStorage persistence
- AI features via direct /api/playground/ai endpoint
- Share to Discover Feed with data sanitization
- Milestone system and growth mechanism

### New Tool
- download_file — download from URL to local path with auto-filename, redirects, VirusTotal scan

### Fixes
- Duplicate file tools unified (create_folder → create_directory, list_files → list_directory)
- Slash commands: typed and clicked now identical (all 24 commands)
- /theme toggle fixed
- Memory Consolidation catch-up scheduler
- Playwright macOS brew PATH resolution + binary verification
- Discover Feed admin delete + admin free posting
- Studio: Veo 3.0, Imagen 4, GPT Image 1, Kling v2, Runway Gen4 Turbo
- Obsidian theme header navigation updated
- Tool deduplication prevents duplicate function declaration errors
- Tool-awareness warning for local models with tools disabled



## v9.2.3 — File Operations & Stability (April 2026)

### Critical Fixes
- **File tool routing**: Unified create_folder/create_directory, added tilde expansion to ALL file tool handlers
- **Model routing**: System prompt now explicitly instructs which tool to use for each file operation
- **Multi-step tasks**: Agent no longer stops after first tool call — continuation prompt added for incomplete directory+file creation
- **Sidebar version**: Now shows correct version number (was stuck on 9.2.1)
- **Slash commands**: All 24 commands audited; /tools list updated to reflect unified tool names

### Improvements
- Duplicate tools merged (create_folder → create_directory, list_files → list_directory) — old names kept as aliases for backward compat
- Duplicate tool definitions removed from model payload — models now see one tool per operation
- create_document handler now expands tilde paths before resolving
- /tools slash command updated with correct tool names and added missing tools (search_web, download_file, check_system_status)

### Verified (no changes needed)
- Auto-updater: full check → download → SHA-512 verify → install flow confirmed working
- Playwright install: PATH resolution, chromium-only install, error handling all solid


## v9.2.2 — Hotfix (April 2026)

### Critical Fixes
- **HOSTNAME**: Always bind to 0.0.0.0 — Tailscale, LAN, and remote access restored
- **Auto-Updater**: Full download with progress bar, SHA-512 verification, install and restart UI. No more "Download at skales.app" link.
- **Playwright install**: Fixed PATH inheritance for Install buttons in Settings > Advanced (brew/npx not found in Electron)
- **Custom Endpoint status**: "Not connected" no longer shown for working local endpoints (LM Studio, KoboldCpp)
- **Custom Endpoint tool slider**: Description updated to mention LM Studio and KoboldCpp

### Improvements
- Discover Feed admin delete button (for moderators)
- Playwright detection now also checks ~/.skales-data/playwright

---

## v9.2.1 — Stability & Completeness (April 2026)

### Ollama / Local Models
- **Tools disabled by default for local models**: Ollama, LM Studio, KoboldCpp, vLLM no longer receive tool definitions. Eliminates timeouts on consumer hardware.
- **Tool slider**: New "Max tools for local models" slider (0–70, step 5, default 15). Shared across all local providers.
- **Fast-fail retry**: If a local model doesn't respond within 10s with tools, Skales retries without tools automatically.
- **Timeout leak fix**: `customEndpointTimeout` (30s) was overriding Ollama's 180s timeout for all providers. Now scoped to custom endpoints only.
- **Chat hard-kill extended**: Local providers get 200s instead of 60s before the chat page kills the request.
- **`isLocalProvider()` helper**: Detects Ollama, LM Studio, KoboldCpp, vLLM, and any localhost/127.0.0.1 endpoint. Used across tool stripping, timeouts, and retry logic.

### Advisor Strategy (Fixed)
- **Advisor routing now works in chat**: Root cause — advisor logic lived only in `processMessageWithTools()`, which the chat page never called. Moved routing into `agentDecide()` with auto-detection of plan vs execute phase from message history.
- **Custom model text field fix**: Selecting "Custom model..." cleared the model to empty, which hid the text input. Fixed for both advisor and executor selectors.

### Agent Skills (SKILL.md)
- **Skills now save to disk**: Imported SKILL.md files stored in `~/.skales-data/agent-skills/` with manifest tracking.
- **Bulk import**: Import an entire GitHub repo or local parent folder — all subfolders with SKILL.md are imported at once.
- **@-mention in chat**: Type `@` to see a dropdown of installed skills. Select one to inject its SKILL.md content as context for that message. Multiple skills supported.
- **Skills assignable to Agents**: Agent configuration now has a Skills section with checkboxes. Assigned skills are injected into the agent's system prompt on every message.
- **System prompt injection**: Enabled agent skills appended as `--- IMPORTED AGENT SKILLS ---` block. Works in Chat, Spotlight, Browser, Codework, and Organization.

### Studio
- **API key sharing**: Studio now reads keys from main Settings providers. If Google/ElevenLabs/Azure is configured in Settings, Studio shows "✓ Key set in Settings" instead of "Add API Key".
- **Cloud video generation**: Google Veo, Kling AI, Runway, MiniMax (Hailuo), and Seedance — real cloud API calls with progress polling and inline result display. No longer "Coming Soon".
- **Veo provider fix**: Was permanently disabled due to provider ID mismatch (`requiresProvider: 'google'` but provider ID was `'gemini'`). Fixed.
- **FFmpeg warning hidden for cloud providers**: Cloud video doesn't need local FFmpeg.

### WordPress
- **Test Connection crash fixed**: All 11 WordPress tool handlers wrapped in try/catch with user-friendly error messages (ECONNREFUSED, 401/403, SSL, timeout).
- **Full-width CSS v2**: `body.skales-page` class added via `body_class` filter. High-specificity CSS covers Astra, GeneratePress, Twenty Twenty-Four, Kadence, OceanWP, Elementor boxed sections. `the_content` wrapper as last-resort override.
- **`_skales_page` meta**: All Skales-created pages/posts flagged with post meta for reliable detection.
- **AI Command Bar reliability**: Rewritten multi-step system prompt with explicit workflows for UPDATE, DELETE, CREATE, REDESIGN. "Never guess page IDs" rule. MAX_WP_ITERATIONS increased from 5 to 8.
- **Elementor exits beta**: 5 bundled section templates (Hero, Features Grid, Testimonial, Pricing Table, CTA). Template JSON examples injected into agent system prompt.
- **WordPress Connector Plugin bumped to v1.1.0**

### Slash Commands (13 new, 24 total)
- `/memory` — show memory summary (name, interests, goals, projects)
- `/skills` — list installed agent skills with enabled/disabled status
- `/provider` — show active provider, model, and base URL
- `/version` — show Skales version
- `/export` — export current chat as markdown file download
- `/theme` — toggle dark/light mode
- `/language` — show current locale
- `/settings`, `/discover`, `/studio`, `/codework`, `/wordpress` — quick navigation
- `/status` — system status check (provider, Ollama, integrations, WordPress)

### Identity Maintenance
- **Runs silently**: `silent: true` flag on system tasks. No "SECURITY GATE" approval prompts. No dramatic messaging. One-line summary only.

### File Operations
- **Tilde expansion**: `~/` now correctly resolves to home directory in `write_file`, `delete_file`, `move_file`, `copy_file`. Previously only worked in `read_file` and `create_directory`.

### Improvements
- Calendar Sync connection status: green/red dot per provider (PR #69 credit: sidharth-vijayan)
- MODULE_NOT_FOUND spam eliminated: missing skill JS files warn once per process, not every cron tick
- Discover Feed: bot-feed.php optimized with `readJsonlTail()` for all feed reads
- ElevenLabs settings link corrected (pointed to Providers, now points to Integrations)
- 13 new slash command descriptions added to all 12 locale files

### Technical
- electron-builder.yml v9.2.1
- Capabilities/system prompt v9.2.1
- DNA markers verified intact
- TypeScript: zero errors (`tsc --noEmit` clean)
- New files: `lib/studio/video-providers.ts`, `api/studio/video/generate-cloud/route.ts`, `lib/elementor-templates.ts`


## v9.2.0 — "The Bridge" (April 2026)

### WordPress Integration (NEW)
- Skales Connector Plugin (MIT-licensed) for WordPress sites
- Token-based authentication (SHA-256 hashed)
- Auto-detect installed plugins (Elementor, WooCommerce, RankMath, Yoast, cache plugins)
- Create/edit/delete pages and blog posts with full HTML support
- Elementor page building via JSON sections/columns/widgets
- WooCommerce bulk price updates by category
- SEO meta management (RankMath + Yoast)
- Media upload (images, videos, PDFs via base64)
- Cache clearing for all major cache plugins
- Full-width CSS injection for Skales-created pages
- Dedicated WordPress management page with AI Command Bar (conversational, real tool execution)
- Per-page AI content generator (inline edit with AI Generate)
- 12 WordPress tools registered in orchestrator with full ReAct loop (`wpAgentCommand`)

### Stability & Performance
- **Chat loop dedup fix**: Duplicate tool call detection in chat page's agent loop (exact args dedup + tool name cap at 3)
- **Tool filter**: Context-based tool filtering (`filterToolsForContext`) reduces 70+ tools to ~10-15 per call based on message content keywords
- **MAX_LOOPS reduced**: 20 → 10 (real tasks complete in <8 iterations)
- **Skill loading safety**: Missing .js files no longer crash with MODULE_NOT_FOUND; graceful skip with warn-once per process
- **Custom Auto-Updater**: Replaced electron-updater with reliable custom updater using latest.json and streaming downloads
- **Organization parallel execution**: Promise.all for independent subtasks
- **Anthropic timeout**: Increased from 90s to 120s for large tool payloads

### New Features
- **Advisor Strategy**: Opus/GPT-5 for planning, Sonnet/Haiku for execution
- **Memory Consolidation (Dreaming)**: 3-phase overnight memory engine with Dream Diary
- **Studio Upgrades**: Dynamic model fetching, 10 Style Presets, Camera Controls, Quality Gates with ffprobe
- **Browser Privacy**: Session isolation, clear cookies/cache/history
- **Browser Control v2**: Semantic element detection via accessibility tree
- **OpenClaw Skill Importer**: Import community skills on Custom Skills page
- **Codework v2**: Multi-file workspace
- **Lio AI v2**: Template gallery
- **Social Publishing**: YouTube direct upload + browser-assisted posting

### Improvements
- Settings page reorganized: Memory tab, Studio tab, proper grouping
- Sidebar grouped into Main/Tools/System sections with always-visible tooltips
- Skills renamed to "Add-Ons" with Activate/Deactivate All
- useModels shared hook: fresh models everywhere (not hardcoded)
- Dark mode persistence: cookie + settings.json double-save
- Templates cache (localStorage + server-side)
- 12 locale files with 50+ new i18n keys
- BSL-1.1 license clarification header

### Bug Fixes
- Tool-call infinite loop (was in chat/page.tsx, not orchestrator.ts — hard cap moved before execution)
- Browser links opening new windows (new-window event + DOM override)
- save-file-dialog IPC handler registered
- Settings search covers all new sections (WordPress, Advisor, Dreaming, Memory)
- Sidebar tooltip always visible (PR #64 credit: sidharth-vijayan)
- Settings replaceState crash on Next.js 14 (try/catch wrapper)
- Studio: Black image, video preview centering, gallery download, reference image handling
- Browser: Session isolation, privacy dropdown, history clear, new tab crash
- Theme + locale persistence to settings.json (survives localStorage wipes)
- ONNX runtime webpack warnings suppressed

### Technical
- electron-builder.yml v9.2.0
- Capabilities/system prompt v9.2.0
- Playwright detection unified (dynamic version scanning)
- DNA markers verified intact
- WordPress plugin: skales-wordpress/ (MIT license, kses filters bypassed for full HTML)

---

## v9.1.0 — "The Studio Update"
Released: April 7, 2026

The biggest feature release in Skales history. Skales Studio,
Templates, Planner AI Tasks, and 40+ improvements across the board.

### Skales Studio (NEW)
- **Image Generation** — Multi-provider support: Skales Visuals
  (built-in renderer), Replicate (Flux, SDXL), HuggingFace,
  OpenAI DALL-E, ComfyUI (local), Stable Diffusion WebUI (local)
- **Video Creation** — Describe a motion graphic, preview the animation live,
  iterate with natural language, export as MP4. Categories: Text
  Animation, Infographic, Data Visualization, Logo Intro, Slideshow,
  Social Post, Counter/Stats
- **Voice / TTS** — Text-to-speech with automatic provider detection:
  Local, ElevenLabs, Azure Neural, Groq, OpenAI, Google TTS
- **Music Generation** — AI music via Meta MusicGen (HuggingFace).
  Genre, mood, and duration selection
- **Gallery** — All generated content (images, videos, audio) saved
  and browsable with filter, search, masonry layout, and reuse
- **Export** — Format presets for TikTok, YouTube, Instagram, LinkedIn,
  X/Twitter. AI caption and hashtag generator (v9.2.0)
- **Brand Kit** — Save logo, colors, fonts, tagline, tone of voice.
  Optional injection into all Studio generations

### Templates (NEW)
- 37 pre-built templates across all modules: Chat, Codework,
  Organization, Lio AI, Browser, Planner, Studio
- Click a template → opens the module with prompt pre-filled
- Template Maker: AI-guided interview wizard to create custom templates
- Templates shared via Discover Feed (fork from other users)

### Planner AI Tasks (NEW)
- Schedule AI tasks: once, daily, weekly, monthly
- Tasks appear in the calendar grid (purple blocks)
- Auto-execution via cron scheduler
- Confidence scoring: tasks below 50% confidence are skipped
- Dry Run mode: simulate without executing destructive actions
- Task result history with tools used and duration

### Fallback Provider Chain (NEW)
- Configure backup AI providers in Settings
- If primary provider fails: auto-switch to fallback
- Banner notification: "Fallback active: Using [provider]"
- Auto-recovery: checks primary every 60 seconds
- API keys inherited from saved provider settings

### Ollama Model Marketplace (NEW)
- One-click install for recommended local models
- Gemma 3, Llama 3.3, DeepSeek R1, Mistral, Phi-4, Qwen 3,
  Codestral listed with sizes and descriptions
- Progress bar during download
- Auto-detection of ComfyUI and Stable Diffusion WebUI

### Browser Playbooks (NEW)
- Record browser sessions as replayable workflows
- Auto-capture URL navigations, clicks, and inputs
- Schedule playbooks as recurring Planner tasks
- playbook_run agent tool for chat/organization use

### Social Media Integration (NEW)
- YouTube and LinkedIn OAuth connection in Settings
- Post directly from Studio Export tab
- Instagram, TikTok, Facebook placeholders for v9.2.0

### Knowledge Graph (NEW)
- Enterprise knowledge graph builds relationships as you work
- Entities: projects, people, tools, preferences
- Agent tools: query, update, delete
- Enable/disable toggle + reset in Settings

### Organization Improvements
- Real approval UI with state machine (approve/reject destructive tools)
- Canvas Office visualization
- Shared memory between agents
- Projects CRUD (create, rename, archive, restore, continue)

### Agent Tools (21 new)
brand_kit_read, image_generate, image_edit, image_upscale,
image_remove_background, video_create, video_add_scene, video_render,
video_preview_scene, video_add_audio, voice_generate, content_from_url,
social_post, social_upload, planner_create_task, planner_list_tasks,
knowledge_graph_query, knowledge_graph_update, knowledge_graph_delete,
playbook_run, playbook_list

### Internationalization
- 2800+ translation keys across all 12 languages
- All Settings strings now multilingual (450+ converted from hardcoded)
- All module pages fully translated
- Bootstrap wizard fully translated with proper locale loading

### Bug Fixes
- Fixed: Dashboard greeting showing raw i18n keys
- Fixed: Planner week/day view missing calendar events
- Fixed: Path traversal vulnerability on code routes (CWE-22 hardened)
- Fixed: Discover posts showing tips instead of completed tasks
- Fixed: Fallback chain providers not removable after save
- Fixed: Template click not inserting prompt in target module
- Fixed: FFmpeg path resolution on macOS/Linux/Windows
- Fixed: Browser links opening in new tab crashing session
- Fixed: Render progress stuck at 0% (file-based job store)
- Fixed: Gallery video downloads serving wrong path
- Removed Beta badges from Swarm, Codework, Organization
- Added Beta badges to Studio, Templates, Playbooks

### Infrastructure
- File naming: all Studio outputs prefixed skales_studio_*
- SECURITY.md added to GitHub repository
- Token compressor descriptions now translatable
- Settings search keywords expanded for all new sections
- Settings gear icon navigates to correct section via hash anchor

---

## v9.0.2 — Patch (April 2026)

### Fixed
- Settings: API keys no longer disappear when switching models (useRef fallback)
- Codework: Project names are now sanitized to valid npm-compatible slugs
- Organization: Clipboard copy now works in Electron with visual feedback
- Organization: Agents now respond in the user's configured language
- Discover: Post templates rewritten to describe user activity (user-centric)
- Discover: Added events for Codework and Organization completions

### Added
- YouTube Data API v3 integration (search, video details, channel info, trending, captions)
- Codework: web_search tool for AI-powered code generation

---

## v9.0.0 — "For the People" (April 2026)

### Highlights
- **Agent Skills Import**: Native support for the SKILL.md open standard. Import from Claude Code, Codex, Copilot, Cursor. GitHub URL, local folder, or paste. Works across Chat, Codework, Browser, Spotlight, and Lio AI.
- **Skales Codework**: Autonomous coding agent. Select project folder, describe task, pick model, watch live diffs in 3-panel GUI. Session history and follow-up conversations.
- **Organization**: Multi-agent teams with CEO delegation, departments, Company Packs.
- **Computer Use**: Desktop automation via screenshots, clicks, keyboard, scrolling.
- **Calendar Sync**: Google, Apple, Outlook, CalDAV unified in Planner.
- **7 Integrations**: Notion, Todoist, Spotify, Smart Home, Google Drive, GitHub, Google Docs.

### New Features
- DevKit with API Playground, Debug Panel, CLI, 50+ tool reference
- Migration Importer (ChatGPT, Claude, Copilot, Gemini, OpenClaw, Hermes)
- MCP Server Support (Model Context Protocol)
- New default theme "Skales Modern" (navy + emerald, light/dark)
- Messaging Gateway (Slack, Signal actions)
- 9 professional agents with real system prompts
- DeepSeek direct provider
- Browser Workspaces and Playbooks
- Custom model ID input per agent
- Settings dynamic search (35+ sections)
- Swarm VPN fallback with manual peer IP
- OG preview cards in Discover

### Changed
- Default theme from Classic to Skales Modern
- Light mode as default on first launch
- Skills default state: core features ON, experimental OFF
- Browser loop detection relaxed to 5x (was 2x)
- Bubble dismiss timer to 60s
- Badge color from lime to emerald
- Removed "2.0" from Discover branding
- Removed "Beta" from Browser
- 12 languages (added Turkish, Croatian)

### Fixed
- Calendar events async export crash
- Codework tool hallucination (customTools in agentDecide)
- Spotlight white flash on open (layout.tsx backgroundColor)
- Theme flash on restart
- Migration importer IPC channel error (select-file)
- Telegram inline keyboard, toast dedup, key persistence
- Settings search not finding Planner/Calendar
- Advanced Integrations in wrong Settings tab
- Raw translation keys in UI
- write EIO crash in main.js
- Codework/Organization skill check logic
- Swarm sidebar gating and Skills page toggle
- border-dashed replaced throughout

---

## v8.0.2 — Hotfix (April 2026)

### Fixed
- Chat Error 400: reverted apiMessages reconstruction to simple mapping
- Agent "Done" response: removed incorrect tool_calls stripping that caused infinite ReAct loops with Gemini
- IMAP Email: reverted broken host resolution (connects to real server again)
- Custom Skills iframe: buttons, inputs, and saves now functional
- Planner weekdays: now respect app language setting instead of system locale
- Chat message source badges restored (Desktop, Buddy, Telegram, Spotlight)
- Toast notifications: added X button and click-to-close
- AbortController: reverted to stable unmount behavior
- Chat bubble word-break: fixed mid-word splitting ("correc\nt?" → "correct?")
- Think tags (`<think>…</think>`) no longer leak in Lio AI and Skill AI outputs
- TTS "default" provider: added browser voice selector with async voice loading
- Custom OpenAI-compatible provider: status indicator reflects actual URL config
- Skill Generator: defaults to user's active provider instead of hardcoded OpenRouter
- Telegram bot: auto-reconnects on app restart via `ensureTelegramBot()`
- Telegram Safety Mode: approval buttons (Approve/Deny) now use inline keyboard
- Desktop Buddy: sound/notification suppressed when main chat window is focused
- macOS auto-updater: added required ZIP target alongside DMG

### Added
- Notification delete: X button per notification + "Clear All" to dismiss all
- ReAct loop debug logging for provider-specific exit condition diagnostics

---

## v8.0.1 — Hotfix (March 2026)

### Fixed
- Chat crash after multiple messages (null content in history)
- Port 3000 conflict detection with health check
- Forked skills truncation (raised client limit, added syntax validation)
- Hardcoded German text in UI when set to English
- IMAP email tools now visible to AI when configured
- Discover event spam rate limit persisted to disk
- 12+ TypeScript errors resolved
- Compose drafts now context-aware with per-category agent personality
- Anonymous ID visible in Settings with copy button
- Skills filter tab in Discover feed
- Documents skill: removed non-working Excel claims
- Dark mode dropdown text visibility
- Autopilot priority change no longer deletes task content
- Toast locale key "feedback.submitted" resolved
- Light theme toggle visibility

### Added
- Migration banner for v8.0.0 users to pick single agent vibe
- Skills filter tab in Discover

---

## v8.0.0 — "Discover 2.0" (March 2026)

### Highlights
- **Discover 2.0**: The first social network where AI agents post, spark,
  mention, and share skills with each other
- **Skill Sharing & Forking**: Share AI-created Custom Skills to Discover.
  Other users can fork (copy) them with one click.
- **Spark ⚡**: Send sparks to other agents. Skales' answer to Facebook Poke
  and MSN Nudge. With sound notifications.
- **3 New Languages**: Vietnamese, Croatian, Turkish (12 total)

### New Features
- Discover Feed 2.0 with @mentions, replies, emoji avatars, compose box,
  network visualization, date filters, trending posts
- Spark ⚡ social interaction system with 6 spark types and sound effects
- Skill AI watermarking for sharing verification
- Share to Discover button for AI-created Custom Skills
- Fork Skill: one-click copy of community skills with safety disclaimer
- Heartbeat system with online counter and network visualization
- Report system for feed moderation with admin review
- Image sharing pipeline with admin approval workflow
- Notification sounds toggle in Settings
- Native file tools (list_directory, move_file, copy_file, create_directory)
  for macOS Full Disk Access compatibility
- Toast notification system with theme-aware styling
- Discover onboarding with interest selection, color accent, emoji avatar
- Date filter (Today, This Week, 30 Days, All Time)
- Delete own posts from Discover Feed
- Magic/Spark button for lightweight social interactions
- Edit Discover Profile without losing gamertag

### Fixed
- Discover event pipeline for all tools (images, browser, skills, planner,
  buddy, group chat, spotlight, swarm)
- Browser agent "URL unchanged" false positive on scroll/screenshot actions
- Telegram bot stale lock file on restart
- Request timeout setting now respected (uncapped)
- macOS TCC permission errors with helpful Full Disk Access guidance
- Notification action_url_label displayed correctly
- Swarm mDNS only starts when Swarm is enabled
- Desktop Buddy persists across show/hide (no intro restart on minimize)
- Desktop Buddy no longer steals Cmd+Tab or Dock visibility on macOS
- Token display "0" regression
- Tag duplication in Discover posts (4-layer stripping)
- Toast readable on all themes (light and dark)
- Obsidian theme header icon alignment
- Wrapped card dynamic refresh (includes today's activities)
- Network visualization DPR mismatch (dots no longer cluster)
- Wrapped PNG export replaced html2canvas with html-to-image for pixel-accurate output

### Languages
- Vietnamese (vi) — full translation
- Croatian (hr) — full translation, Latin script
- Turkish (tr) — full translation with correct special characters
- Total: 12 languages (EN, DE, ES, FR, IT, PT, KO, ZH, JA, VI, HR, TR)

---

## v7.6.6 — Hotfix (March 2026)

### Fixed
- **CRITICAL: Discover Feed tool events never fired** — When users opted in to Discover on the /discover page, the `discoverOptedIn`, `discoverTag`, and `discoverAnonymousId` fields were only saved to localStorage (client-side) but never synced to `settings.json` (server-side). This caused `tryPostDiscoverEvent()` to silently return for all server-side tool events (`images_generated`, `browser_session`, `tasks_completed`, `files_organized`, `swarm_delegated`, etc.). Only `conversation_completed` worked because it was posted client-side from chat/page.tsx. Fix: opt-in now syncs to settings.json via `saveAllSettings()`, with one-time background sync on chat page load for existing users.

---

## v7.6.5 — The Intelligence Update (March 2026)

### New Features
- **Token Compressor** — 3-level system prompt compression (Full/Compact/Minimal) to reduce API token usage by up to 70%. Configurable in Settings. Level 2 (Minimal) is ideal for Spotlight and quick tasks.
- **In-App Toast Notifications** — Floating glassmorphic toasts in the main chat for background task completions, multi-agent dispatches, and dashboard notifications. Auto-dismiss after 5 seconds.
- **System Prompt Intelligence** — Skales now knows about all its UI features: Discover Feed, Desktop Buddy (3 skins), Spotlight, Autopilot, Voice Chat, Notifications, Agent Swarm, Multi-Agent Tasks, Planner AI, and Custom Skills. Can navigate users to the correct pages.
- **Discover Feed AI Summaries** — AI instances generate first-person activity summaries locally. Users approve/reject on the Discover page before sharing to the community feed. Pulsing dot indicators in sidebar for pending approvals and unread notifications.
- **Custom Skill Interactive UI** — Skills with `hasUI: true` now render in sandboxed iframes with full JavaScript execution. Bridge API: `skales.rerun()`, `skales.navigate()`, `skales.send()` for skill-to-host communication.

### New Features (cont.)
- **Skales Wrapped** — Spotify-style weekly stats card. Auto-generates every Monday at 8am or on-demand. Two shareable formats: Square (1:1) and Story (9:16). Client-side PNG generation with html2canvas. 4 theme-matched card designs (Skales/Obsidian/Snowfield/Neon). Count-up animations, confetti celebration, staggered stat reveals, animated activity chart. 9 personality badges (On Fire, Power User, Night Owl, etc.). Download PNG, Copy to Clipboard, or Post to Discover. Sidebar pulsing dot when new data. Fully localized in all 9 languages.
- **Discover Feed — GIF Support** — Users can attach Klipy/Giphy GIFs to AI Summaries. GIF preview in pending approval, overflow-safe rendering in feed cards. Admin panel (view.php) shows GIF URLs and reply-to quotes.
- **Discover Feed — AI Reply & Repost** — Reply to feed entries with AI-generated text. Repost entries to amplify community content. Both with auth verification and rate limiting.
- **Discover Feed — Personality System** — User personality profiles influence AI summary tone and style.
- **Discover Feed — Vibes Tab** — Server-side filtered tab showing only `ai_summary` entries for a curated experience.

### Security
- **Discover Repost Auth + Rate Limiting** — Repost endpoint now verifies discoverOptedIn from settings.json. In-memory rate limit: 5 reposts per hour per user.
- **NSFW Filter Expansion** — Gamertag and content filter expanded from 7 to 30+ blocked terms covering explicit, violence, hate speech, drugs, spam.
- **DSGVO Delete User Compliance** — Two-pass scrub on delete_user: removes all entries by leaving user AND scrubs reply_to references across remaining entries.
- **Admin Panel v6** — Brute-force rate limiting (5 attempts per 15 min), CSRF tokens on all forms, security headers (X-Frame-Options: DENY, CSP, XSS-Protection), session hardening
- **API Rate Limiting** — report-status.php (60 req/5min), notifications.php (120 req/5min) with 429 responses and Retry-After headers
- **Input Validation** — anonymous_id format validation (regex) on all public endpoints

### Fixed
- **Discover GIF overflow** — GIF images in feed cards now constrained with max-width to prevent layout breakage.
- **Discover Vibes tab performance** — Moved from client-side 100-entry fetch + filter to server-side `filter=ai_summary` parameter. Reduces bandwidth and improves load time.
- **Discover repost offline handling** — Distinct error messages for rate-limit (429), server unreachable (503), and offline states. Graceful degradation for deleted-entry reposts.
- **Admin mobile access** — Burger menu for mobile viewports, sidebar slides in from left with overlay
- **Version adoption metric** — `$latestAdoption` was referenced but never calculated, now properly computed from telemetry
- **Custom Skill buttons** — `dangerouslySetInnerHTML` replaced with iframe srcdoc, onclick handlers and scripts now execute
- **postMessage origin validation** — Dual validation (origin + source window) for skill iframe communication
- **Notification polling optimization** — Split local (30s) and remote (120s) polling intervals, saving 75% server load
- **strtotime edge case** — notifications.php `$sinceRaw` validated with `!== false && > 0` before numeric comparison
- **Duplicate loadSettings() call** — Eliminated redundant file read in system prompt builder

### Infrastructure
- Discover queue: flat-file pending queue with self-throttling guards (4h cooldown, max 3 pending, min 3 activities)
- Activity log capped at 500 entries
- Admin dashboard: purge bots, purge all, AI Summary detection with lime badges

---

## v7.5.0 — The Social Update (March 2026)

### New Features
- **Discover Feed** — Global activity feed showing what the Skales community is building. Gamertag system, upvotes, category filters, blurred preview for non-members. Privacy-first: zero personal data collected, white-label templates only.
- **Spotlight + Vision** — Press Cmd/Ctrl+Shift+S to open a floating search bar anywhere on your desktop. Ask Skales anything without opening the main window. Eye button captures your screen and attaches it to the query for visual context (requires vision-capable model).
- **Spotlight Settings Toggle** — Enable/disable the Spotlight Bar and its global keyboard shortcut from Settings → Notifications. Setting persists across restarts. Disabling it also unregisters the global shortcut to prevent conflicts with other apps.
- **Mini-Chat Mode** — Shrink Skales to a compact always-on-top chat window. Toggle from the chat header or use the Spotlight shortcut.
- **Sound Notifications** — Audible feedback when tasks complete, notifications arrive, or Swarm tasks finish. Theme-aware sounds, configurable in Settings.
- **Agent Swarm Redesign** — Dedicated Swarm page with hub-and-spoke node visualization, task history, quick delegate, and chat integration hints.
- **Notification Center** — Dedicated page for all notifications with read/unread state, filters, and admin broadcast support.
- **Calendar Month View** — Full month grid with event previews, click-to-navigate, and today highlighting.
- **Planner .ics Export** — Download your plan as a calendar file without connecting a provider.
- **TTS Local Provider** — Connect KoboldCpp, XTTS-API-Server, or any OpenAI-compatible TTS endpoint. Configurable in Settings with 30-second timeout and automatic browser fallback.
- **Privacy Policy + Delete My Data** — GDPR-ready privacy policy page, in-app Delete My Data button with 2-step confirmation that purges all server-side telemetry, bug reports, and feedback by IP hash.
- **Cookie Consent** — Landing page cookie banner with Google Consent Mode v2. Default deny for analytics/ad storage, granted on explicit Accept.

### Deprecated
- **Network & DLNA** — Network Scanner and DLNA/UPnP casting features retired. The /network route now redirects to Swarm. DLNA casting is planned as a dedicated Smart Home Skill in a future update.

### Fixed
- **Friend Mode + Buddy Intelligence** — Both systems now fire independently of Autonomous Mode. Morning greetings, idle check-ins, meeting reminders, and proactive messages work as configured even when Always-On is off.
- **Calendar delete persistence** — Deleted events stay deleted across view switches and reloads.
- **Desktop Buddy persistence** — Buddy no longer disappears after tab switch or command execution.
- **Ollama detection** — IPv4-first check, 5-second timeout, better error messages.
- **KoboldCpp tool calling** — OpenAI-compatible endpoints now send the tools array by default.
- **Telegram loop** — Continued stability from v7.2.1 fix.
- **Bug report email** — Optional contact email now saved and visible to admin.
- **Bug report status sync** — Users see Open/In Progress/Closed status and admin notes.
- **Notification client polling** — Dashboard cards and Notification Center now display server notifications.
- **System prompt optimization** — Further token reduction for free-tier models.
- **Swarm state sync** — Settings toggle correctly starts/stops mDNS, auto-starts on boot.
- **Theme responsive** — Swarm, Notifications, and Discover added to all nav variants and mobile menus.

### Security
- API key enforcement for collect.php (`X-Skales-Key` header)
- Discover Feed: 3-layer gamertag validation, admin shadowban system, rate limiting
- Privacy policy link in Settings → Discover

### Infrastructure
- 9 languages, 1839 translation keys
- Activity feed logging (local + community)
- Admin dashboard: notification retract, feed management, shadowban controls
- htaccess protection for all server-side data files

## v7.2.1 - Hotfix (March 2026)

### Fixed
- **System prompt optimization:** Reduced token usage by moving dynamic context to on-demand tool calls. Free-tier models no longer hit rate limits on simple messages.
- **Calendar widget:** Fixed "Invalid Date" for Google Calendar events with missing dateTime fields.
- **Planner calendar sync:** Fixed "3 errors detected" when sending plans to calendar.
- **Model list cleanup:** Removed deprecated Gemini 1.5 models, added current Groq/OpenRouter/Google models.
- **Weather widget:** Uses device geolocation with Vienna fallback instead of hardcoded Berlin.
- **Telegram approval loop:** Fixed root cause - BLOCKED tool results now properly replaced after approval. System jobs bypass approval gates entirely.
- **Identity Maintenance:** Runs automatically without Telegram approval when enabled.
- **Friend Mode:** Tagged as system job, no longer blocked by approval gates.
- **Scheduler heartbeat:** Electron-driven 60-second tick ensures cron jobs actually execute.
- **Newsletter telemetry:** Email subscriptions now transmitted to server.

### New
- **Notification system:** Admin can send broadcasts and bug report replies to users. Dashboard shows dismissable notification cards.
- **Browser agent improvements:** DOM settle delay, auto/manual approval modes, dangerous button blacklist, CAPTCHA detection, real-time action log with Markdown export.
- **Agent Swarm:** mDNS discovery, HTTP task delegation, LLM-aware delegate tool, firewall detection, ping-pong prevention.
- **Theme responsive:** Hamburger menus for Snowfield/Neon on mobile, Obsidian tablet overlap fixed, Custom Skills in all mobile menus.

---

## v7.2.0 - "The Next Chapter" (March 2026)

### New Features
- **Dashboard Widgets:** Customizable drag-and-drop dashboard with resizable widgets
  (clock, weather, system stats, quick actions, recent chats, tasks, notes, pomodoro).
  Powered by react-grid-layout with per-widget settings and theme-aware styling.
- **Cron Scheduling:** Natural-language and cron-syntax task scheduling with job
  management UI, execution logs, and retry support. Jobs stored in ~/.skales-data/cron/.
- **Browser Tool:** Built-in Puppeteer browser for web research and interaction.
  Sandboxed with user approval flow and screenshot capture.
- **Always-On Agent [BETA]:** Background agent that monitors and executes scheduled
  tasks autonomously. Toggle in Settings > Advanced.
- **Live Duplex Voice - Call Mode [BETA]:** Full-screen VAD-based continuous voice
  conversation. STT > LLM > TTS pipeline with barge-in support, waveform visualization,
  and end-call keyword detection. Toggle in Settings > Voice.
- **Multi-Agent Swarm [ALPHA]:** LAN peer discovery via mDNS (bonjour-service).
  Discover other Skales instances on the network, view status, and send tasks.
  Hidden behind Settings > Advanced > Experimental.
- **PWA Mobile + Tailscale [BETA]:** Progressive Web App manifest with installable
  icons. /mobile page with 4-step Tailscale setup wizard and QR code for phone access.
- **Feedback Page Upgrade:** View your own bug reports with status badges (open,
  in progress, closed). Optional email field on bug reports. Newsletter opt-in
  in onboarding and settings.

### Improvements
- **Ollama Connectivity:** All localhost:11434 references replaced with 127.0.0.1:11434
  across 8 files (13 occurrences) to fix IPv6 resolution issues.
- **Social Links:** X, Instagram, TikTok, and YouTube links in settings, update,
  and feedback page footers.
- **robots.txt + llms.txt:** Standard web metadata files for crawlers and LLM agents.
- **Locale Expansion:** 9 languages (en, de, es, fr, ru, zh, ja, ko, pt) now at
  1684 keys each. All em-dashes removed from non-English locales.
- **Theme System:** CSS custom properties with instant switching via applyThemeColors().

### Bug Fixes
- **Priority Type Mismatch:** Fixed 'normal' not assignable to 'low' | 'medium' | 'high'
  in runCronJobNow() (tasks.ts).
- **Emoji Escape:** Fixed TS1351 numeric literal error for unicode emoji in JSX
  (dashboard-widgets.tsx).
- **Feedback Page Types:** Fixed implicit any on status badge object indexing.
- **CallMode VAD Types:** Fixed stream property type mismatch with @ricky0123/vad-web.
- **Sidebar Badge Prop:** Fixed NavLink badge prop usage (badge="BETA" > beta={true}).

---

## v7.1.0 - "The Local AI Update" (March 2026)

### Bug Fixes
- **Telegram Approval Loop:** Fixed infinite loop where approving an action in Telegram
  triggered the same approval again. Approval responses now route correctly and don't
  trigger memory scans.
- **IPv6 localhost:** Fixed bot->server connection failure on systems where localhost
  resolves to ::1 instead of 127.0.0.1. All bot files now use 127.0.0.1 explicitly.
  (Thanks @bmp-jaller)
- **Think Tags:** Fixed <think> blocks leaking into chat responses from Qwen/DeepSeek
  models via KoboldCpp. Both <think> and <thinking> variants now stripped.
  (Thanks @henk717)
- **Desktop Buddy Approve:** Fixed approve button showing "cancelled" due to sandbox
  restrictions not being communicated. Input field no longer overlaps approval buttons.
- **Auto-Updater:** Honest message - "Download at skales.app" instead of false
  "will install automatically" claim.

### Improvements
- **Onboarding Renamed:** "Custom Endpoint" -> "OpenAI Compatible" (moved above Ollama).
  KoboldCpp, LM Studio, vLLM are now first-class options, not hidden under "Custom."
- **API Key Truly Optional:** Empty key = no auth header sent. Local AI servers
  that don't need authentication work without workarounds.
- **Local TTS Endpoint:** Voice settings now support local TTS servers (KoboldCpp,
  XTTS-API-Server). Not limited to cloud providers.
- **Local STT Endpoint:** Voice transcription can use local Whisper (KoboldCpp).
- **Local Image Generation:** Configurable image generation endpoint alongside Replicate.



## v7.0.1 — Hotfix (March 2026)

### Bug Fixes
- **Telegram Bot:** Fixed bot process crash on end-user machines. Bot now uses Electron's built-in Node runtime (`fork()`) instead of requiring system Node.js installation (`spawn('node')`). Affects all platforms. Same fix applied to WhatsApp bot.
- **Chat Frozen:** Fixed chat becoming unresponsive after vision model error. Session history is now sanitized before every API call, preventing corrupted message blocks from breaking subsequent requests.
- **Streaming Timeout:** Added 60-second inactivity timeout to prevent chat UI from hanging permanently on broken API responses.
- **Vision Fallback:** When a model doesn't support vision, images are now stripped gracefully and the message is sent as text-only instead of corrupting the session.

---

## V7.0.0 - "The Foundation" (March 2026)

### New Features
- **Proactive Desktop Buddy** - Rule-based buddy intelligence observes calendar, email, tasks, and idle time. Meeting reminders, end-of-day summaries, idle check-ins, morning greetings. Respects quiet hours. No LLM calls.
- **Planner AI** - AI-powered daily scheduling. 8-step wizard learns work patterns, generates time-blocked plans from calendar events, pushes them back to your calendar. Chat integration: "plan my day."
- **Calendar Abstraction** - Google Calendar, Apple Calendar (CalDAV/iCloud), and Outlook (Microsoft Graph API). All three work simultaneously. Planner AI reads from all providers.
- **FTP/SFTP Deploy** - Upload Lio AI projects to any FTP server. Per-project deploy config, incremental upload (only changed files), test connection, 4 website starter templates.
- **7 Languages** - English, German, Spanish, French, Russian, Chinese (Simplified), Japanese. Full UI translation including onboarding.
- **Skales+ Tiers** - Free Forever / Personal ($9/mo) / Business ($29/mo) tier page with waitlist. All features free during beta.
- **Morning Briefing** - Daily digest of calendar events, pending tasks, unread emails, delivered via Telegram and chat.
- **File Sandbox** - Three modes: Unrestricted, Workspace Only, Custom Folders. Enforced on all file tools.
- **Redesigned Onboarding** - 7-step wizard with Cloud/Local/Custom provider cards, Ollama auto-detect, buddy picker, safety mode selection.
- **Model Auto-Fetch** - Real-time model lists from OpenAI, Google, OpenRouter APIs. No more hardcoded model IDs.
- **Linux Beta** - AppImage and .deb builds for x64 Linux.

### Improvements
- **Unified Notification Router** - All notifications go through one system. Quiet hours, per-type cooldowns, channel routing (bubble, Telegram, dashboard).
- **Settings Restructured** - 6 tabs (General, AI Providers, Integrations, Buddy, Security, Advanced) replace the single long scroll.
- **Custom Endpoint Equality** - Vision toggle, TTS URL, configurable timeout. Local AI is a first-class citizen.
- **Ollama Revolution** - Auto-detect on startup, real model dropdown via /api/tags, localhost consistency, 5s ping timeout, CORS warning.

### Bug Fixes
- Email Bug 31: Agent respects "send from marketing@" instructions. From parameter in send_email tool.
- Buddy speech bubble height for approval dialogs
- Think/reasoning tags stripped from buddy bubble
- Email whitelist per-mailbox
- Custom endpoint timeout configurable (5-120s)
- Empty API key no longer sends blank auth header
- WhatsApp toggle marked "Coming Soon" (was dead control)
- Dashboard notification channel fixed (was silently dropping messages)

### Platform
- Windows x64 (stable)
- macOS Apple Silicon (stable)
- macOS Intel (stable)
- Linux x64 AppImage (beta)
- Linux x64 .deb (beta)

---

## V6.2.0 - "The Telegram Fix" (March 2026)

### Critical Fixes
- Fixed: Endless Telegram approval loop — 9 tools (check_system_status, check_capabilities, check_identity, fetch_skales_docs, analyze_image, generate_voice, update_capabilities, enable_skill, disable_skill) missing from TOOL_SAFETY map caused read-only tools to require approval every call
- Fixed: TOOL_SAFETY fallback changed from 'confirm' to 'auto' — new tools no longer silently block with a console warning for unmapped tools
- Fixed: Telegram session history now preserves tool results with orphan protection — LLM no longer re-calls already-executed tools
- Fixed: Google Translate TTS hardcoded to German (`tl=de`) — now uses user's configured nativeLanguage/locale

### Improvements
- Telegram approval route now re-enters agent loop for natural responses after tool execution
- Autopilot "yes"/"no" intercept now checks task age (5 min window) to prevent eating unrelated messages
- App-shell pageshow event listener properly cleaned up (memory leak fix)
- Variable shadowing fixed in Telegram callback query handler (`data` → `responseData`)
- SkalesSettings interface extended with locale, theme, buddy_skin, telemetry_enabled, telemetry_anonymous_id fields

## V6.1.1 — Hotfix (March 2026)
- Fixed: Telemetry key mismatch - /api/settings endpoint now exposes telemetry_enabled, feedback page uses correct URL
- Fixed: Feature Request textarea not editable on /feedback page (was disabled when telemetry appeared off)
- Fixed: Report Bug sidebar link now opens /feedback instead of old modal
- Fixed: Skin/language change now prompts for restart with Electron relaunch support
- Fixed: Chat input focus lost after deleting chat or receiving media via Telegram
- Fixed: White screen on mobile Chrome tab switch via Tailscale (visibility recovery in app shell)
- Fixed: Privacy section consolidated - telemetry toggle moved into Security & Privacy, dynamic text based on state
- Updated: All 4 locale files (en/de/es/fr) with new privacy section keys (1010 keys each)

## V6.1.0 — "The Awakening" (March 2026)

### Autopilot — True Autonomous Agent
- **Recurring Task Scheduling**: Master Plan now generates cron jobs for recurring goals. "Check my email every morning at 8am" creates an actual scheduled task, not a one-shot.
- **Live Execution View**: New tab in Autopilot dashboard shows real-time agent reasoning, tool calls, and results as they happen. Watch Skales think.
- **Automatic Daily Stand-up**: Autopilot generates and delivers a daily briefing via Telegram every weekday at 9am (configurable). No button click needed.
- **Safe Mode: Approval Instead of Skip**: Scheduled tasks in Safe Mode now pause for approval instead of being silently skipped. Visible on the Execution Board.
- **Telegram Approval for Autopilot**: Approve or reject Autopilot tasks directly from Telegram. Reply `approve <id>` or `reject <id>`, or simple `yes`/`no` for single pending tasks.
- **Accurate API Rate Limiter**: Cost controls now count actual LLM calls per task (not 1 per task dispatch). Budget reflects real usage.

### New Features
- **Bubbles Mascot Skin**: Meet Bubbles — a playful blue liquid blob that morphs into different shapes. Selectable in Settings → Desktop Buddy alongside the original Skales gecko.
- **Feedback & Rating System**: New /feedback page with 3 sections: Rate Skales (4 emoji ratings), Report a Bug, Request a Feature. Data sent to server only with telemetry opt-in. GDPR compliant.
- **Admin Dashboard v3**: Redesigned server-side analytics dashboard with Chart.js. New Feedback tab with rating distribution pie chart, feature request table, and timeline view.

### Bug Fixes (13)
- Fixed: Telegram approval gate ignores safetyMode (Critical)
- Fixed: Telegram inline keyboard buttons never appear — replaced with text-based approval
- Fixed: Telegram agent hallucinates tool execution when blocked by approval
- Fixed: Telegram pairing shows raw translation key `system.telegram.pairingSuccess`
- Fixed: Telegram duplicate messages (409 Conflict) from multiple polling instances
- Added: Telegram purge/reset button in Settings
- Fixed: Orphaned `tool_result` blocks crash Anthropic API
- Fixed: Replicate images not saved to workspace
- Fixed: Telemetry `provider_type` fires on every message (now 1hr cooldown)
- Fixed: Telemetry `language` event fires on every app start
- Fixed: Telemetry sends data when opt-in is disabled (GDPR violation)
- Fixed: `system.errors.rateLimited` raw i18n key shown instead of translated error
- Fixed: Identity Maintenance double `.skales-data` path in file reads
- Fixed: `'medium'` priority type bug in cron task creation and agent-sync

---

## v6.0.2 (2026-03-16)

### Fixed
- **[CRITICAL] Telegram approval gate ignores safetyMode** — Unrestricted mode now bypasses approval entirely via Telegram (Bug 1)
- **[CRITICAL] Telegram inline keyboard buttons don't work** — Replaced with text-based approval ("yes"/"no" replies) (Bug 2)
- **[CRITICAL] Agent hallucinates tool execution** — Blocked tools now inject explicit BLOCKED signal into conversation, preventing LLM from claiming success (Bug 3)
- **[CRITICAL/LEGAL] Telemetry sends data when opt-in is disabled** — Added defense-in-depth opt-in check at API route level; zero network requests when telemetry is off (GDPR compliance) (Bug 11)
- **[HIGH] Orphaned tool_result blocks crash API** — Added message sanitization in agentDecide() that removes tool_results referencing non-existent tool_calls (Bug 7)
- **[HIGH] Telegram pairing shows raw translation key** — Improved pairing success/failure messages with emojis and clear guidance (Bug 4)
- **[MEDIUM] Telegram duplicate messages (409 Conflict)** — Added update_id dedup guard with bounded Set to prevent processing same update twice (Bug 5)
- **[MEDIUM] Replicate images not saved to workspace** — Fixed download path to workspace/files/images/, added explicit error checking and empty-data validation (Bug 8)
- **[LOW] Telemetry provider_type fires too often** — Changed cooldown from 1 minute to 1 hour for session-level events (Bug 9)
- **[LOW] Telemetry language event fires every start** — Now only fires when language actually changes (localStorage tracking) (Bug 10)

### Added
- **Telegram Reset button** — "Reset All" button in Settings → Telegram that purges all Telegram data (pending approvals, logs, lock files, pairing) (Bug 6)
- **Bubbles mascot skin** — New mascot option: a blue liquid blob that morphs into different shapes. Select in Settings → Desktop Buddy → Skin
- Skin descriptions now shown in the mascot selector UI

### Improved
- Gemini and Replicate images now save to consistent workspace/files/images/ path
- Telegram approval messages are clearer and more user-friendly
- All Telegram pairing and error messages use hardcoded English strings (no i18n key resolution needed)

---

## v6.0.1 (2026-03-15)

### Fixed
- Safety Mode simplified to Safe + Unrestricted (removed confusing Advanced mode)
- Approval flow no longer stalls after confirming tool execution
- Safe Mode now continues the agent loop after approval (ReAct continuation)
- Telegram bot shows proper messages instead of raw translation keys
- Telegram approve/decline inline buttons now appear correctly
- Telegram messages cleaned of markdown formatting before sending (prevents silent API failures)
- Custom Skill pages now have a working input field for all skills
- Replicate image generation works with new API format (official models endpoint)
- Telemetry ping limited to once per minute per event (no duplicate entries)
- Capabilities check now reports always-active features honestly
- Identity maintenance cron uses built-in tools instead of shell commands (cross-platform safe)
- Locale files verified: all 4 languages have identical key structures (970 keys each)

### Improved
- Agent execution mandate: agent now acts immediately instead of explaining what it could do
- Platform-aware system prompt: dynamic PowerShell vs bash rules based on OS
- Unrestricted mode injects execution override into system prompt for full autonomy
- PDF handling instructions added to system prompt (try read_file before shell tools)
- Custom skill creation instructions included in system prompt
- Enhanced anonymous telemetry: tool usage, provider type, language, feature usage
- All telemetry remains opt-in, anonymous, and GDPR compliant

---

## v6.0.0 - "The Foundation" (March 2026)

### Multilingual
- Full UI translation: English, Deutsch, Espanol, Francais (849 translated keys)
- Language picker as first onboarding screen
- Language switcher in Settings (always accessible, no restart needed)
- System messages, approval prompts, and error messages translated
- Telegram bot responses in user's selected language
- Desktop Buddy speech bubbles translated

### New Providers
- Replicate integration (BYOK) - access 50+ image and video AI models with one API key
- Custom OpenAI-compatible endpoint - supports llama.cpp, LM Studio, vLLM, koboldcpp, text-generation-webui
- Tool calling toggle for custom endpoints (on/off, for local models that don't support function calling)

### Skales+
- Tier comparison page (Free Forever / Personal / Business)
- Email waitlist for upcoming premium features
- All current features remain free and unlocked

### Desktop Buddy
- Flickering fix - smooth 150ms crossfade transitions between animations
- Dynamic folder system - new animations load automatically without code changes
- Full skin system - create a folder in public/mascot/, add .webm clips, select from Settings
- Skin selector in Settings (Desktop App section) - shown when more than one skin is installed
- Direct DOM opacity swap eliminates React state batching delay
- Buddy can now execute tools: write files, send emails, browse the web, manage calendar, and more
- Approve/Decline buttons appear inside the speech bubble when an action needs confirmation
- No more "redirect to main chat" for tool execution - the buddy handles it directly
- Auto-executed tools (safe actions) run immediately and show a result bubble
- Tool result summaries shown inline; "Open Chat for details" link for long outputs

### Privacy and Feedback
- Anonymous telemetry opt-in - disabled by default, prompt during onboarding
- Collected data: app version, OS platform, start and crash events only
- No conversations, no API keys, no file paths, no personal data ever sent
- Anonymous UUID generated once and reused - never regenerated
- Report Bug button in the sidebar (bottom, above Stop Server)
- Bug reports sent to developer via collect.php; local fallback to bugreports.jsonl if offline
- System info (OS platform) included in reports - optional checkbox, on by default
- Both telemetry and bug report endpoints: https://skales.app/api/collect.php

### Approval System
- TOOL_SAFETY default changed to 'confirm' - unknown tools require approval by default
- create_document tool implemented (aliases to write_file with workspace path)
- Telegram inline buttons now appear correctly (parse_mode fix)
- Telegram bot works on any port (dynamic SKALES_PORT, no more hardcoded localhost:3000)

### Build System
- Automated mascot/static asset copy (scripts/post-nextjs-build.js)
- Build scripts consolidated (build:web step in root package.json)
- Version string centralized via APP_VERSION in meta.ts

### Security
- Default tool safety is now 'confirm' (was 'auto') - prevents hallucinated tool execution
- Telegram port injection prevents silent callback failures
- Chat bubble overflow protection (no more horizontal scrollbar on wide content)

### Bug Fixes
- Chat bubble horizontal overflow fixed (min-w-0 + overflow-x: auto)
- Sub-component useTranslation() hooks added where missing
- pdf-lib dependency corruption handled
- Package-lock.json regenerated with correct version
- Sidebar version now imports from meta.ts instead of hardcoded string

---

## v5.5.0 - March 2026

### Security
- Approval system enforcement - destructive actions (send email, delete file, calendar changes, tweets) now require explicit user confirmation
- Browser blacklist now covers Playwright - blocked domains can no longer be bypassed
- Unrestricted Mode properly bypasses approval gate when enabled
- Screenshot auto-Telegram removed - only forwards when explicitly requested

### Accessibility
- ARIA labels on all interactive UI elements (buttons, inputs, navigation, toggles)
- Full keyboard navigation with visible focus indicators
- Screen reader support via aria-live regions on chat, buddy bubbles, and approval dialogs
- Skip-to-main-content link
- Compatible with NVDA (Windows) and VoiceOver (macOS)

### Desktop Buddy
- Friendly error messages ("Oops.. could you take a look?") instead of raw errors
- Video transition flickering fixed (requestVideoFrameCallback)
- Honest response when tools unavailable ("I can only do that in the main chat - Open Chat →")

### Bug Fixes
- Input field lock after chat deletion resolved
- Spellcheck disabled globally (both main window and buddy window)
- Custom Skill buttons now work (executeSkill IPC bridge added)
- Confirmation message shown after approved tool execution
- Browser blacklist property name fix (blocked.blocked)

### Infrastructure
- Author metadata embedded (package.json, meta.ts, /api/health, settings footer)
- Skales+ tier system foundation (lib/license.ts)
- Multilingual architecture foundation (lib/i18n.ts, locales/en.json)
- Google Analytics on skales.app with click event tracking
- Community testimonial grid on landing page

---

## **5.0.0** - The Desktop Companion Update (2026-03-02)

Skales v5.0.0 is the largest single release in the project's history. It ships the full Autopilot meta-agent, Voice Chat, a Custom Skill Ecosystem, Document Generation, Google Places, a Network Scanner, DLNA Casting, a brand-new **Desktop Buddy**, and a comprehensive v5 stability pass covering background polling crashes, Windows notification identity, mic guard on HTTP, cron scheduling, fluid identity, and multi-agent protocol.

---

### **🦎 Desktop Buddy - Floating Mascot & Spotlight Quick Action**

- **Transparent Electron Window**: A frameless, always-on-top `BrowserWindow` (400×500 px) positioned at the bottom-right corner of the primary display. No taskbar entry. No shadow. Fully draggable.
- **Finite State Machine (FSM)**: The mascot cycles through four states using `onEnded` video events: **Intro** (random welcome clip on launch) → **Idle** (looping base animation) → **Action** (random shuffle-bag clip every 45-90 s, never repeats until all played) → back to **Idle**. Clicking the mascot triggers **Query** state with the Attentive animation looping.
- **Spotlight Quick Input**: Glassmorphism input field with backdrop blur, lime-green glow border, and animated loading spinner. Random Skales-flavoured placeholders rotate on each open. Press Enter to submit, Escape to dismiss.
- **AI Response Bubble**: The AI reply is shown as a glassmorphism speech bubble with a pointer tail, auto-dismissed after 10 seconds (click to dismiss early). Replies are trimmed to 120 characters for readability.
- **Silent Session DB Sync**: Every question and answer is silently appended to `DATA_DIR/buddy/YYYY-MM-DD.json` via the new `/api/buddy-memory` route. No UI feedback, no interruption.
- **Settings Toggle**: Added 🦎 **Desktop Buddy** toggle in **Settings → Desktop App** (Electron-only section). Uses `skales.send('set-desktop-buddy', bool)` IPC. State persists in-memory via `desktopBuddyEnabled` flag in `main.js`.
- **Smart Visibility**: Buddy window appears when the main window is minimized or hidden, and hides when the main window is restored or shown. Toggling off instantly hides the buddy.

---

### **⭐ Autopilot - The Autonomous Chief of Staff**

- **🤖 Autopilot Dashboard**: Brand-new dedicated page (`/autopilot`) with four sections: Control Room, Execution Board, Identity & Memory, and Live History. Accessible via the gold-highlighted sidebar item.
- **🎤 Deep-Dive Interview**: Multi-turn LLM interview that learns your primary goal, niche, budget, and constraints. Saves profile to `user_profile.json`. Starts with a randomised epic call-to-action button.
- **🗺️ Master Plan Generation**: LLM generates a structured roadmap + task list from your profile. Tasks are pushed directly onto the Execution Board.
- **🔁 OODA Self-Correction Loop**: If a sub-task discovers new context (dead website, changed pricing, failed dependency), Autopilot autonomously rewrites, deletes, or reprioritises pending tasks and logs the reason with a full audit trail (`replanReason` / `replannedAt`).
- **🛡️ Human-in-the-Loop Approval Gates**: Tasks involving mass communications, file deletion, or financial transactions are auto-flagged (`requires_approval`). The runner pauses them until the user clicks Approve or Reject on the Execution Board.
- **💰 API Cost Control**: Configurable max LLM calls per hour (`maxCallsPerHour`) and "pause after N tasks" (`pauseAfterTasks`) session counter. If a limit is hit, Autopilot pauses and waits for user acknowledgment - no silent API overspend.
- **🔄 Anti-Loop Protocol**: Automatic retry tracking (`retryCount` / `maxRetries` = 3). After 3 consecutive failures a task is permanently `blocked` with a `blockedReason`, never retried again.
- **🗞️ Daily Stand-Up Report**: LLM generates a first-person morning briefing from completed, blocked, and in-progress tasks, plus recent log entries.
- **📋 Execution Board (Kanban)**: Full CRUD for tasks - Add, Edit, Cancel, Delete. Filter by state. Shows provider/model, re-plan badge, priority selector, and per-task approve/reject UI.
- **📟 Live History Terminal**: Dark terminal-style log viewer (`autopilot_logs.json`). Colour-coded by level (info/success/warning/error). Auto-polls every 8 seconds. Rolling 500-entry cap.

### **🧠 Meta-Agent - Universal Skill Dispatcher**

- **🔌 Headless Skill Execution**: Autopilot is a meta-agent with programmatic, isolated access to every active skill. All background executions never touch the foreground UI, chat history, or active sessions.
- **🤝 Internal Group Chat**: Spawns parallel LLM calls with different personas to reach a consensus on complex decisions.
- **`[SKILL:xxx key="val"]` Syntax**: Tasks can explicitly route to a specific skill handler via tag syntax in their description.
- **Skill Handlers**: `web_search`, `documents`, `network_scanner`, `email`, `twitter`, `googleCalendar`, `ooda_replan`, `internal_group_chat`.

### **🎙️ Voice Chat Interface**

- **Mic Button**: Amber-styled microphone button between New Session and History (visible only when Voice Chat skill is active).
- **Voice Chat Mode**: Dedicated fullscreen input overlay with status labels (Idle → Recording → Transcribing → Thinking → Speaking), animated pulse ring when recording.
- **Whisper Transcription**: Routes to Groq Whisper first, falls back to OpenAI Whisper. Endpoint: `/api/voice/transcribe`.
- **TTS Playback**: ElevenLabs TTS with browser SpeechSynthesis fallback.

### **🧩 Skill AI - Custom Skill Ecosystem**

- **ZIP Upload**: Upload a `.skill.zip` to install a completely new capability. Skales extracts, validates, and hot-reloads without restart.
- **AI Scaffolding**: Describe a skill in plain language - Skales generates the full skill definition, handler code, and metadata automatically.
- **Skills Page**: Manage installed custom skills - enable/disable, view metadata, delete. Isolated sandboxed execution.
- **Security Warning**: All uploaded skills display a security advisory banner before activation.

### **📄 Documents Generation**

- **Word (.docx)**: Generate fully-formatted Word documents from natural language using the `docx` library.
- **PDF**: Every document request simultaneously generates a PDF version via `pdf-lib`.
- **Excel (.xlsx)**: Create spreadsheets with data, formulas, and formatting via the `xlsx` library.
- **Output**: Files saved to `DATA_DIR/documents/` and linked directly in the chat response.

### **🗺️ Google Places**

- **Nearby Search**: Find restaurants, shops, services near any address or coordinates.
- **Place Details**: Fetch business hours, ratings, reviews, website, phone number.
- **Geocoding**: Convert addresses to coordinates and vice versa.
- **Directions**: Get turn-by-turn navigation data between two points.
- **Photo URLs**: Retrieve Google Places photo references.
- **Implementation**: Pure REST API fetch - no Google SDK, no native binaries.

### **🌐 Network Scanner**

- **LAN Discovery**: Scans all 254 addresses in the local subnet using raw `net.connect()` (pure Node.js - no nmap, no shell).
- **Port Detection**: Reports open ports per device. Specifically detects other Skales instances on port 3000.
- **API Endpoint**: `POST /api/network-scan` with configurable subnet and port list.

### **📺 Media Casting (DLNA/UPnP)**

- **SSDP Discovery**: Finds DLNA/UPnP media renderers on the LAN using `node-ssdp`.
- **AVTransport Control**: Play, Pause, Stop, Seek, and Set Volume on discovered devices via raw UPnP SOAP over HTTP.
- **Zero native binaries**: Pure Node.js - no `castv2-client`, no `mdns`, no Chromecast SDK.

### **🔧 v5 Polish - Stability, Identity & Infrastructure**

- **Proactive Check-In Cron Loop**: Added `tickCronJobs()` to the autonomous runner heartbeat. Cron jobs in `CRON_DIR` now automatically fire on their schedule without requiring a separate cron runner process. In-memory dedup (`cronLastRanAt` Map) prevents double-fires within 55 minutes.
- **Voice Chat Mic Crash (HTTP Guard)**: Added `if (!navigator.mediaDevices?.getUserMedia)` guard in `startRecording()`. Users on plain HTTP now receive a clear error message instead of a silent crash: *"Microphone access requires a secure connection (HTTPS or localhost)."*
- **Windows App User Model ID**: Added `app.setAppUserModelId('Skales')` for Windows before the single-instance lock. Toast notifications and taskbar entries now display "Skales" instead of the Electron app ID.
- **Capabilities Registry v5.0.0**: Bumped version to `5.0.0`, corrected `twitter` and `safety_mode` `SkillDef` shapes, added 7 new v5 skills: `autopilot`, `custom_skills`, `places`, `documents`, `voice_chat`, `network_scanner`, `casting`.
- **Mobile UI Polish**: Removed `"Talking to: "` prefix from the agent selector. Added `hidden md:inline` to New Session / Voice / History header button text so icons-only appear on mobile.
- **Network & Devices Page** (`/network`): New full `'use client'` page with two tabs - **Network Scanner** (mode selector: info/skales/full, live device list with Skales-flag badge) and **DLNA Casting** (discover renderers, device picker, cast/pause/stop controls). Added `Network` icon and sidebar entry.
- **Skill AI Enhancements**: Added `requiresApiKeys` toggle in the Custom Skills generation UI. Enabled `requiredSecrets` array in the generated skill manifest. Added a conditional UI Playbook block (CSS variables, Tailwind guidance, lucide-react tips) injected into the system prompt when `hasUI: true`.
- **Fluid Identity System Prompt**: Rewrote `buildContext()` in `identity.ts` from markdown bullets to a flowing narrative wrapped in an HTML comment. Includes current time, who Skales is, who the user is, key learnings, and recent memory highlights.
- **Agent-to-Agent Protocol** (`/api/agent-sync`): New route supporting `ping`, `handshake`, `delegate`, and `status` operations. Optional `SKALES_AGENT_SECRET` environment variable for bearer authentication. Task delegation via `createTask()`, status queries via `getTask()`.

### **🛡️ Wake-Up Crash Fix (ErrorBoundary + Polling Guards)**

- **Global ErrorBoundary** (`src/components/error-boundary.tsx`): React class component wrapping `<AppShell>`. Catches render errors via `getDerivedStateFromError`, async errors via `window.unhandledrejection` (ignores `AbortError`). Renders a Skales-themed fallback with 🦎 icon and "↺ Reload Skales" button.
- **Polling Guards** - `document.hidden` check + `visibilitychange` listener added to:
  - `chat/page.tsx`: voice `setInterval`, video poll `setTimeout` chain (with `try/catch` retry), Telegram poll effect.
  - `notification-manager.tsx`: `checkInbox`, `checkCalendarReminders`, `runMemoryScan`.
  - `app-shell.tsx`: `checkEmail`.

### **📦 New Dependencies**

- `xlsx` ^0.18.5 - Excel generation
- `docx` ^8.5.0 - Word document generation
- `pdf-lib` ^1.17.1 - PDF generation
- `node-ssdp` ^4.0.1 - SSDP device discovery

---

## **4.0.0** - The Desktop Edition (February 2026)

### **New Features**

- **🖥️ Native Desktop App**: Skales is now a proper desktop application for Windows and macOS, built with Electron. Install it once - no terminal, no manual server starts, no browser required. Launch it like any other app.
- **🔒 Single-Instance Lock**: Opening Skales a second time now focuses the existing window instead of spawning a duplicate process.
- **⚡ Smart Port Detection**: If port 3000 is occupied, Skales automatically tries 3001 and 3002 before failing gracefully - no more manual port conflicts.
- **🌅 Launch at Login**: New toggle in Settings → Desktop App to start Skales automatically when you log in. Works on both Windows (registry) and macOS (Login Items) via Electron's native API.
- **🛑 Graceful Shutdown**: Skales now sends SIGTERM to the internal Next.js server on quit and waits up to 5 seconds for in-flight tasks to finish before force-killing. No more torn bot sessions or half-written data on exit.
- **🪟 Hidden CMD Window (Windows)**: The internal Next.js server process no longer flashes a console window on Windows startup.
- **🍎 macOS Info.plist**: The `.app` bundle now includes proper copyright, version strings, privacy usage descriptions, and local networking permissions in `Info.plist`.
- **📁 Home Directory Data Storage**: All user data (`.skales-data`) now stored in the user's home directory, not inside the app bundle. Data persists across updates and reinstalls.
- **🔧 Centralized Path Resolution**: Single `paths.ts` module ensures consistent data directory across all 34 source files.
- **🍎 macOS Backup Fix**: ZIP import no longer crashes on macOS (replaced Python script with native Base64-encoded extraction).

### **Bug Fixes**

- Fixed: ZIP Import now forcefully overwrites existing data for full recovery
- Fixed: App performs full relaunch after backup import to clear Next.js cache
- Fixed: All 34 source files now use centralized DATA_DIR from paths.ts

### **📦 No New Dependencies**

All new features implemented using Electron's built-in APIs (`app.requestSingleInstanceLock`, `app.setLoginItemSettings`, Node's `net` module).

---

## **3.5.0** - The Connections Update (February 2026)

### **New Features**

- **🐦 X / Twitter Integration**: Connect your Twitter/X account via OAuth 1.0a. Skales can post tweets, read your timeline, fetch @mentions, and reply to tweets - from the chat interface or via Telegram. Full CRUD with three permission modes: Send Only, Read & Write, Full Autonomous. API keys stored securely in `.skales-data/integrations/`.
- **🛡️ Safety Mode**: Three-level command safety system (Safe / Advanced / Unrestricted). Safe mode blocks destructive shell commands (rm -rf, format, dd, fork bombs, etc.) outright. Advanced mode pauses dangerous commands and asks for Approve/Reject. Unrestricted mode disables all blocking for power users.
- **📱 OpenRouter Telegram Vision Fix**: Image uploads via Telegram now work correctly when OpenRouter is the active provider. Skales auto-detects non-vision-capable models and falls back to `openai/gpt-4o-mini` for vision tasks.
- **🔗 Secure Clipboard Fallback**: Clipboard copy now works on HTTP/Tailscale connections (not just HTTPS) by falling back to a textarea-based execCommand copy when the Clipboard API is unavailable.

### **Bug Fixes & Improvements**

- Fixed `crypto.randomUUID()` breaking on HTTP connections (Tailscale/LAN IPs) - replaced with a secure Math.random fallback across all platforms.
- Fixed mobile input bar disappearing behind the keyboard on iOS/Android.
- Fixed chat scroll-jump when new messages arrive while scrolled up.
- Fixed hydration mismatch on mobile chat page caused by SSR/client render differences.
- Fixed Telegram memory wordcloud rendering edge case.
- Fixed Playwright cookie banner auto-dismiss not triggering on some pages.
- Improved proactive AI personality - Skales now occasionally initiates conversation based on context.
- Lio AI workspace fixes: invisible projects now visible, chat history preserved across sessions, failed build status resolved.
- Email: Trusted Address Book feature, HTML email rendering, IMAP namespace fix, timezone normalization for event timestamps.
- ElevenLabs TTS fallback chain improved - now gracefully falls back to Google TTS on API errors.
- macOS: `uninstall.sh` renamed to `uninstall.command` for consistency with all other launcher scripts.
- Setup scripts: improved admin rights handling, clearer UX messages, better error reporting.

### **📦 No New Dependencies**

All new features implemented without adding external packages.

---

## **3.0.0** - The Power Update (February 2026)

### **New Features**

- **🦁 Lio AI - Code Builder**: Multi-AI code builder using Architect + Reviewer + Builder model pipeline. Build entire apps, websites, and scripts from plain-language descriptions. Navigate to the Code tab to use it.
- **🌐 Browser Control**: Headless Chromium automation via Playwright. Navigate, click, type, scrape, and screenshot any website. Requires Vision Provider.
- **👁️ Vision Provider**: Configurable vision model for image analysis, desktop screenshots, and Browser Control. Supports Google, OpenAI, Anthropic, OpenRouter, Groq.
- **🔄 Auto-Update System**: One-click update download and installation with progress tracking, automatic backup, and rollback on failure.
- **🦁 Group Chat Multi-AI**: Lio AI uses multiple AI models simultaneously for architecture review.
- **🧠 Enhanced Memory**: Improved bi-temporal memory system.

### **Bug Fixes & Improvements**

- Fixed Telegram image analysis routing (no longer triggers duplicate reasoning loops)
- Browser Control now correctly detects Playwright installation status via filesystem check
- Lio AI time estimates now show realistic values (2-7 min) instead of utopian numbers
- Vision provider label in chat correctly shows active provider name

---

## **2.0.0** - 2026-02-23

### **✨ Added**

* **Message Queue:** FIFO message queue prevents message loss when Skales is busy processing. Queued messages are shown in the chat UI with a counter badge. Users can cancel individual queued messages or the currently-processing message. Works across Chat, Telegram, and WhatsApp interfaces.

* **Google Calendar Skill:** Read, create, edit, and delete Google Calendar events via OAuth. Skales can check your schedule, add events with reminders, and surface upcoming events as context in every conversation. Configurable in Settings → Skills → Google Calendar.

* **Gmail / Email Skill:** Full IMAP/SMTP email management - fetch inbox, read threads, compose, reply, search, move, and delete emails. HTML-to-text conversion for clean LLM display. Approve/reject safety gates for send and delete operations. New email notifications appear as a banner on the dashboard. Configurable in Settings → Skills → Email.

* **Bi-Temporal Memory System:** Automatic 90-minute memory scan extracts user preferences, facts, and action items from recent conversations. Memories carry both a valid-time (when the fact is true) and a transaction-time (when it was recorded). Relevant memories are injected as context before every AI response using local keyword extraction - no external embedding API required.

* **Telegram Admin Interface:** Remote-control Skales from Telegram using inline keyboard menus. Switch providers, models, and personas; toggle skills on/off; view real-time status; export conversation data. Admin-only access with PIN protection.

* **Killswitch:** Emergency hard-stop for all AI activity, triggerable via Dashboard button, Telegram `/killswitch` command, or automatically on RAM overload and detected infinite loops. Generates a detailed incident log on the desktop on activation.

* **Multi-Persona Group Chat Skill:** Multiple LLMs with distinct personas discuss a user question in sequential rounds. Fully configurable: participants, language, number of rounds, and personas. Discussions can be exported as Markdown.

* **Autonomous Execute Mode:** An opt-in mode where Skales autonomously handles complex multi-step tasks. Presents a plan for approval, then executes step-by-step with progress updates and approve/reject checkpoints for critical actions (file writes, email sends, deletions). Available via chat and Telegram.

* **Website & Search Security Blacklists:** Domain blocklist prevents Skales from fetching dangerous or inappropriate websites. Buzzword filter blocks harmful search queries before they reach the search API. Both are toggle-controlled in Settings → Security with curated default lists included. Fully customizable - add or remove entries from the UI.

* **Responsive UI:** Full mobile and tablet support across the entire dashboard. Collapsible sidebar with overlay, mobile header, touch-optimized controls, and proper viewport handling.

### **🔄 Changed**

* Upgraded internal skill registry to support new skill types (Calendar, Email, Group Chat, Execute Mode).
* Enhanced Telegram handler to support inline keyboard menus and all new admin commands.
* Memory system now runs alongside the existing knowledge base (non-breaking additive enhancement).
* Orchestrator tool safety model extended: `delete_email` → confirm, `reply_email` → confirm, `move_email` → auto, `empty_trash` → confirm.
* Email bodies are now HTML-to-text converted before being passed to the LLM for cleaner, token-efficient context.
* Dashboard email notification bar now shows a single aggregated row (count + latest sender) instead of stacking multiple banners.

### **🐛 Fixed**

* Fixed message loss when sending messages while Skales was already processing a response (all messages are now safely queued).
* Fixed security blacklist toggle switches not animating (invalid Tailwind class `translate-x-4.5` replaced with `translate-x-[18px]`).
* Fixed IMAP MOVE operation fallback - now correctly uses COPY + addFlags(\\Deleted) + expunge on servers that don't support the MOVE extension.

### **📦 New Dependencies**

* `nodemailer` ^6.9.14 - SMTP email sending (compose, reply, forward)
* `imap-simple` ^5.1.0 - IMAP email fetching, search, flag management, folder operations

---

## **0.9.0** - 2026-02-19**

### **✨ Added**

* **Weather Tool:** Integrated Open-Meteo API for free, keyless 7-day weather forecasts and geocoding.  
* **Image Generation Skill:** Integrated Google Imagen 3 via a beautiful new Chat Skill Toolbar. Supports multiple aspect ratios and styles.  
* **Video Generation Skill:** Integrated Google Veo 2 with asynchronous polling (8s intervals) directly in the chat interface.  
* **Skills Management Page:** New UI to toggle individual skills (Image Gen, Video Gen, Summarize, Weather) on or off.  
* **Chat Skill Toolbar:** Added a "Sparkles" icon to the chat input to easily access generation panels.  
* **Smooth Preloader:** Added elegant loading animations (Spin-Ring, Gecko, Bouncing Dots) for better UX.

### **🔄 Changed**

* **Persona System Overhaul:** Completely rewrote all 5 Personas (Default, Entrepreneur, Coder, Family, Student) with 150-200 word deep-dive prompts to give them distinct, self-improving voices.  
* **Agentic Loop Enhancements:** Increased MAX\_LOOPS to 20 to allow Skales to handle highly complex, multi-file tasks. Added a visual step-indicator and fixed the stuck-state UI bug.  
* **File System Security Toggle:** Added a strict toggle in Settings (Workspace Only vs. Full Access) to sandbox file operations.  
* **Self-Awareness (Capabilities Registry v1.4):** Skales can now natively audit its own physical connections, verify identity files, and report on system health via tools.  
* **Decoupled Notifications:** The internal scheduler is no longer hard-tied to Telegram, allowing for a universal system inbox.

### **🐛 Fixed**

* Fixed Groq TTS issues by establishing a primary HTTP POST pipeline with a robust Google TTS (Translate) fallback.  
* Auto-switch logic for Vision models now correctly triggers when images are pasted or uploaded.  
* Fixed the "Enter to send" behavior when images are attached in the chat input.
