# Optional Renderers V6

Template mode is canonical. It is the default, test baseline, Micro Mode renderer, and lowest-hardware path.

Local Ollama is optional:

```powershell
$env:PE_RENDER_BACKEND="slm"
$env:PE_SLM_PROVIDER="ollama"
$env:PE_SLM_MODEL="gemma2:2b"
$env:PE_OLLAMA_MODEL="gemma2:2b"
```

The V6 test package includes `Run_Pretorius_Ollama_Optional.cmd`. Ollama must already be installed and running locally. If the provider is unavailable, PersonaConsole falls back to deterministic templates.

Cloud/API rendering is not required and is not enabled in the release-candidate binary. It should be added only as a renderer-only provider behind the same Layer 1 boundary:

- Renderer receives `CanonicalTurnFrame` read-only.
- Renderer output never writes directly into memory.
- Render audit still applies.
- Template fallback remains mandatory.
- No account, network, model download, or cloud service may be required for the default build.

This keeps PersonaConsole a deterministic character operating system, not a hosted chatbot wrapper.
