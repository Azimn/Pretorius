# Optional Renderers V6/V7

Template mode is canonical. It is the default, test baseline, Micro Mode renderer, and lowest-hardware path.

Local Ollama is optional:

```powershell
$env:PE_RENDER_BACKEND="slm"
$env:PE_SLM_PROVIDER="ollama"
$env:PE_SLM_MODEL="gemma2:2b"
$env:PE_OLLAMA_MODEL="gemma2:2b"
```

The V6 test package includes `Run_Pretorius_Ollama_Optional.cmd`. Ollama must already be installed and running locally. If the provider is unavailable, PersonaConsole falls back to deterministic templates.

V7 adds an optional OpenAI-compatible API provider for frontier models:

```powershell
$env:PE_RENDER_BACKEND="slm"
$env:PE_SLM_PROVIDER="api"
$env:PE_API_URL="https://api.openai.com/v1/chat/completions"
$env:PE_API_KEY="..."
$env:PE_API_MODEL="gpt-4.1-mini"
```

The API path uses `curl` at runtime and is never required for template mode,
Micro Mode, or local Ollama mode. See `docs/V7_RENDERER_TIERS.md`.

Cloud/API rendering must remain renderer-only behind the same Layer 1 boundary:

- Renderer receives `CanonicalTurnFrame` read-only.
- Renderer output never writes directly into memory.
- Render audit still applies.
- Template fallback remains mandatory.
- No account, network, model download, or cloud service may be required for the default build.

This keeps PersonaConsole a deterministic character operating system, not a hosted chatbot wrapper.
