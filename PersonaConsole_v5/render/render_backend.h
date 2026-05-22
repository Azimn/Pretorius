/* render_backend.h — V4 renderer abstraction contract.
 *
 * Layer 2 of the V4 architecture.  Renderers are non-authoritative;
 * outputs are disposable.  No renderer may write to NPCState, memory,
 * AETHER, schemas, or any other Layer 1 state.  Enforcement of that
 * invariant lives in memory_firewall.{h,c}.
 *
 * The runtime composes a RenderContext from the canonical identity
 * state (Layer 1) and hands it to a RenderBackend.  The backend returns
 * a RenderResult.  That's the entire contract.
 *
 * Mandatory v4 backends:
 *   - TemplateBackend (deterministic, default, zero deps)
 *   - TinySLMBackend  (optional, llama.cpp/Ollama target)
 *
 * Future backends (cloud, larger local models) plug in without altering
 * Layer 1.  Behavioral holography: identical RenderContext must produce
 * identical *identity* (mood, drives, grudges, memory selection) across
 * backends — only linguistic fidelity may vary.
 */
#ifndef PERSONA_V4_RENDER_BACKEND_H
#define PERSONA_V4_RENDER_BACKEND_H

#include <stdint.h>
#include <stddef.h>
#include "../core/persona.h"
#include "../schema/schema_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PE_RENDER_MAX_TEXT      512
#define PE_RENDER_MAX_BACKENDS  8

/* Selected memories handed to the renderer.  All values are READ-ONLY
 * from the renderer's perspective.  Indices index into the engine's
 * working memory ring or AETHER cold scratch. */
typedef struct {
    int   episodic_idx[4];      /* up to 4 episodic memories */
    int   episodic_count;
    int   cold_aether_idx[4];   /* up to 4 cold AETHER promotions */
    int   cold_aether_count;
    int   core_idx[4];          /* up to 4 core memories (never decay) */
    int   core_count;
} RetrievedMemorySet;

/* RenderContext = the immutable snapshot of Layer 1 state that a
 * renderer is allowed to observe.  Pointer fields are non-owning.
 * Layer 1 types come from core/persona.h. */
typedef struct {
    const Engine             *npc;        /* canonical state, read-only */
    const RetrievedMemorySet *memories;
    const UtterancePlan      *plan;       /* rhetorical + stance + thresholds */
    const Relation           *relation;   /* current interlocutor */
    const SchemaState        *schema;     /* compressed beliefs */
    uint32_t                  seed;       /* deterministic RNG seed for this turn */
} RenderContext;

/* RenderResult = a disposable rendering of the above.  The runtime
 * displays `output` to the user but never writes it back into Layer 1
 * state directly — the next turn's user input is what re-enters the
 * pipeline. */
typedef struct {
    char     output[PE_RENDER_MAX_TEXT];
    int      output_len;
    int      confidence;        /* 0..1000, backend-defined */
    int      latency_ms;
    int      token_count;       /* for SLM backends; 0 for templates */
    uint32_t flags;             /* reserved for hedge/refusal markers */
} RenderResult;

typedef struct RenderBackend {
    const char *name;

    /* Initialization: load model files, allocate scratch, etc.
     * Returns 0 on success.  Called once per process. */
    int (*initialize)(struct RenderBackend *self);

    /* Cleanup.  Idempotent. */
    int (*shutdown)(struct RenderBackend *self);

    /* The render call.  MUST NOT mutate any field reachable through
     * ctx — that is a Layer 1 invariant enforced by memory_firewall. */
    int (*render)(struct RenderBackend *self,
                  const RenderContext  *ctx,
                  RenderResult         *out);

    /* Backend-private state (model handle, scratch buffer, etc.). */
    void *priv;
} RenderBackend;

/* ----- Registry -----
 * Backends register themselves at startup.  The runtime selects one by
 * name (config-driven) and falls back to "template" if the requested
 * backend is unavailable. */
int            render_backend_register(RenderBackend *backend);
RenderBackend *render_backend_find(const char *name);
int            render_backend_count(void);
RenderBackend *render_backend_at(int idx);

/* Default backend selection: returns "template" unless overridden via
 * PE_RENDER_BACKEND env var or runtime config. */
RenderBackend *render_backend_default(void);

/* Built-in backend factories.  Each lives in its own .c file under
 * render/<name>/ and registers itself when its initialize() is called
 * the first time.  Calling these is optional — bringing a backend's
 * symbols into the link automatically registers it via __attribute__
 * ((constructor)) on platforms that support it; the explicit getters
 * are for hosts that need static control. */
RenderBackend *render_template_backend(void);
RenderBackend *render_slm_backend(void);

/* Explicit init for hosts that link the engine as a static archive
 * (constructor attribute does not fire for unreferenced .o in .a).
 * Idempotent: safe to call multiple times. */
void render_backends_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_V4_RENDER_BACKEND_H */
