/* slm_backend.c — V4 Tiny SLM renderer stub.
 *
 * Target: 1B–4B parameter models, Q4 quantization, CPU-first.
 * Compatible model families (interchangeable, never hardcoded):
 *   - Gemma small variants
 *   - Qwen small instruct
 *   - SmolLM family
 *   - TinyLlama-class
 *   - Phi-mini class
 *
 * Transports (each lives in render/providers/):
 *   - ollama provider     (HTTP to localhost:11434/api/generate)
 *   - llama.cpp provider  (libllama linkage)
 *   - openai-compatible   (local llamafile / vLLM / etc.)
 *
 * This file is the dispatcher.  The provider is configured at init
 * time via PE_SLM_PROVIDER + PE_SLM_MODEL env vars.  When no provider
 * is available, this backend's render() returns confidence=0, output_len=0,
 * and flags bit 1 set — caller MUST fall back to template.
 *
 * Critical: this backend NEVER writes to Layer 1.  It receives a
 * compiled prompt from prompt_compiler, ships it to the provider,
 * captures the generation, applies output post-processing (style
 * enforcement, token budget, voice mask), and returns the result.
 * Output is disposable.  The next turn's USER input is what reaches
 * memory — never the SLM's generation.
 */
#include "../render_backend.h"
#include "../prompt_compiler.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char model_name[64];
    char provider[32];
    int  available;
} SlmPriv;

static SlmPriv g_slm_priv;

static int slm_init(RenderBackend *self){
    SlmPriv *p = (SlmPriv*)self->priv;
    if (!p) return -1;
    const char *prov = getenv("PE_SLM_PROVIDER");
    const char *model = getenv("PE_SLM_MODEL");
    if (prov)  snprintf(p->provider,   sizeof(p->provider),   "%s", prov);
    else       snprintf(p->provider,   sizeof(p->provider),   "none");
    if (model) snprintf(p->model_name, sizeof(p->model_name), "%s", model);
    else       snprintf(p->model_name, sizeof(p->model_name), "unset");
    /* availability check is delegated to the provider at first render */
    p->available = (prov != NULL);
    return 0;
}

static int slm_shutdown(RenderBackend *self){
    (void)self;
    return 0;
}

static int slm_render(RenderBackend *self,
                      const RenderContext *ctx,
                      RenderResult *out){
    SlmPriv *p = (SlmPriv*)self->priv;
    if (!ctx || !out) return -1;
    memset(out, 0, sizeof(*out));

    if (!p || !p->available){
        /* No provider configured.  Signal "unavailable" so the host can
         * fall back to the template backend without policy drift. */
        out->confidence = 0;
        out->output_len = 0;
        out->flags = 2u;   /* bit 1 = unavailable, please fall back */
        return 0;
    }

    /* Provider dispatch happens here in a future commit.  For now this
     * backend is a registered stub: visible to the host, selectable
     * via PE_RENDER_BACKEND=slm, but indicating unavailability so the
     * host falls back to templates.  This is intentional — V4 ships
     * the renderer ABSTRACTION first; actual SLM provider wiring is a
     * separate, optional integration. */
    out->confidence = 0;
    out->output_len = 0;
    out->flags = 2u;
    return 0;
}

static RenderBackend g_slm_backend = {
    .name        = "slm",
    .initialize  = slm_init,
    .shutdown    = slm_shutdown,
    .render      = slm_render,
    .priv        = &g_slm_priv,
};

RenderBackend *render_slm_backend(void){
    return &g_slm_backend;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void slm_backend_autoregister(void){
    render_backend_register(&g_slm_backend);
}
#endif
