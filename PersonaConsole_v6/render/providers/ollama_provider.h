/* ollama_provider.h — V4 SLM provider for Ollama.
 *
 * POST http://<host>:<port>/api/generate
 *   {"model":"<name>","prompt":"<text>","stream":false,
 *    "options":{"temperature":0,"num_predict":<max_tokens>,"seed":<seed>}}
 *
 * Returns {"response":"<text>","done":true,...} which we extract into
 * RenderResult.output.  Determinism: temperature=0 + per-turn seed
 * propagates the engine's xorshift state into the model's sampler so
 * replay tests get repeatable token sequences.
 *
 * Config (env vars, all optional):
 *   PE_OLLAMA_HOST       default "127.0.0.1"
 *   PE_OLLAMA_PORT       default 11434
 *   PE_OLLAMA_MODEL      default "gemma2:2b"   (override per character)
 *   PE_OLLAMA_TIMEOUT_MS default 8000
 *   PE_OLLAMA_TEMP       default 0     (integer per-mille, 0..2000)
 *   PE_OLLAMA_NUM_PRED   default 160   (max tokens)
 *
 * No SSL.  Local-only by design — providers reaching the internet are
 * a separate concern and a separate file.
 */
#ifndef PERSONA_V4_OLLAMA_PROVIDER_H
#define PERSONA_V4_OLLAMA_PROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char host[64];
    int  port;
    char model[64];
    int  timeout_ms;
    int  temperature_per_mille;   /* 0 = greedy / deterministic */
    int  num_predict;
    int  raw;                     /* bypass Ollama template when nonzero */
} OllamaConfig;

/* Populate defaults + env overrides.  Always succeeds. */
void ollama_load_config(OllamaConfig *cfg);

/* Single-shot generate.  Sends `prompt` to the configured endpoint,
 * blocks up to timeout_ms, writes the model's text reply into out (up
 * to out_cap-1 bytes, NUL-terminated).  Returns bytes written on
 * success, -1 on connect/network error, -2 on protocol error (bad
 * response shape), -3 on timeout, -4 on truncation. */
int ollama_generate(const OllamaConfig *cfg,
                    const char *prompt,
                    unsigned int seed,
                    char *out, int out_cap);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_V4_OLLAMA_PROVIDER_H */
