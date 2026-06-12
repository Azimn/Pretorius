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
#include "../providers/ollama_provider.h"
#include "../providers/api_provider.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PE_SLM_WIDE_TEXT 2048

typedef struct {
    char model_name[64];
    char provider[32];
    int  profile;
    int  chat_format;
    int  available;
    OllamaConfig ollama;
    ApiConfig api;
} SlmPriv;

static SlmPriv g_slm_priv;

static int contains_ci(const char *hay, const char *needle){
    if (!hay || !needle || !needle[0]) return 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; ++p){
        size_t i = 0;
        while (i < nl && p[i]){
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            ++i;
        }
        if (i == nl) return 1;
    }
    return 0;
}

static int profile_from_env_or_model(const char *provider, const char *model){
    const char *env = getenv("PE_SLM_PROFILE");
    if (env && env[0]){
        if (!strcmp(env, "tiny") || !strcmp(env, "micro") || !strcmp(env, "edge"))
            return PE_SLM_PROFILE_TINY;
        if (!strcmp(env, "expressive") || !strcmp(env, "frontier") || !strcmp(env, "large"))
            return PE_SLM_PROFILE_EXPRESSIVE;
        return PE_SLM_PROFILE_BALANCED;
    }
    if (provider && !strcmp(provider, "api"))
        return PE_SLM_PROFILE_EXPRESSIVE;
    if (contains_ci(model, "gemma3:1b") || contains_ci(model, "1b") ||
        contains_ci(model, "1.2b") || contains_ci(model, "lfm2.5"))
        return PE_SLM_PROFILE_TINY;
    if (contains_ci(model, "mistral"))
        return PE_SLM_PROFILE_EXPRESSIVE;
    return PE_SLM_PROFILE_BALANCED;
}

static int chat_format_from_env_or_model(const char *model){
    const char *env = getenv("PE_SLM_CHAT_FORMAT");
    if (env && env[0]){
        if (!strcmp(env, "gemma") || !strcmp(env, "gemma3"))
            return PE_SLM_CHAT_GEMMA;
        return PE_SLM_CHAT_FLAT;
    }
    if (contains_ci(model, "gemma"))
        return PE_SLM_CHAT_GEMMA;
    return PE_SLM_CHAT_FLAT;
}

static int slm_init(RenderBackend *self){
    SlmPriv *p = (SlmPriv*)self->priv;
    if (!p) return -1;
    const char *prov  = getenv("PE_SLM_PROVIDER");
    const char *model = getenv("PE_SLM_MODEL");
    snprintf(p->provider,   sizeof(p->provider),   "%s", prov  ? prov  : "none");
    snprintf(p->model_name, sizeof(p->model_name), "%s", model ? model : "unset");
    p->profile = profile_from_env_or_model(p->provider, p->model_name);
    p->chat_format = chat_format_from_env_or_model(p->model_name);
    /* For Ollama: pull host/port/model/timeout/temp from env so the
     * runtime can be pointed at any local instance without rebuilds. */
    ollama_load_config(&p->ollama);
    if (model) snprintf(p->ollama.model, sizeof(p->ollama.model), "%s", model);
    if (p->chat_format == PE_SLM_CHAT_GEMMA && !getenv("PE_OLLAMA_RAW"))
        p->ollama.raw = 1;
    /* For frontier/OpenAI-compatible APIs: runtime-optional curl transport.
     * This remains renderer-only and never becomes a Layer 1 dependency. */
    api_load_config(&p->api);
    if (model) snprintf(p->api.model, sizeof(p->api.model), "%s", model);
    /* availability check is delegated to the provider at first render */
    p->available = (prov && (!strcmp(prov, "ollama") || !strcmp(prov, "api")));
    return 0;
}

static int slm_shutdown(RenderBackend *self){
    (void)self;
    return 0;
}

static int compress_to_render_result(const char *wide, char *out, int out_cap){
    if (!wide || !out || out_cap <= 1) return 0;

    int len = (int)strlen(wide);
    if (len < out_cap){
        memcpy(out, wide, (size_t)len + 1u);
        return len;
    }

    int limit = out_cap - 1;
    int cut = -1;
    for (int i = limit - 1; i >= 0; --i){
        char c = wide[i];
        if (c == '.' || c == '!' || c == '?'){
            cut = i + 1;
            break;
        }
    }
    if (cut < 0){
        for (int i = limit - 1; i >= 0; --i){
            if (wide[i] == ' ' || wide[i] == '\n' || wide[i] == '\t'){
                cut = i;
                break;
            }
        }
    }
    if (cut < 0 || cut > limit) cut = limit;
    while (cut > 0 && (wide[cut-1] == ' ' || wide[cut-1] == '\n' ||
                       wide[cut-1] == '\r' || wide[cut-1] == '\t'))
        --cut;
    memcpy(out, wide, (size_t)cut);
    out[cut] = 0;
    return cut;
}

static void apply_frame_budget(SlmPriv *p, const RenderContext *ctx){
    if (!p || !ctx || !ctx->frame) return;
    int mw = ctx->frame->max_words ? (int)ctx->frame->max_words : 40;
    int want_tokens = mw * 2 + 32;
    if (ctx->frame->speech_act == PE_SA_CONFESSION ||
        ctx->frame->speech_act == PE_SA_CONCESSION ||
        ctx->frame->speech_act == PE_SA_PROMISE)
        want_tokens += 32;
    if (want_tokens < 96) want_tokens = 96;
    if (want_tokens > 512) want_tokens = 512;

    if (!getenv("PE_OLLAMA_NUM_PRED"))
        p->ollama.num_predict = want_tokens;
    if (!getenv("PE_API_MAX_TOKENS"))
        p->api.max_tokens = want_tokens;
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
        out->flags = 2u;   /* bit 1 = unavailable, please fall back */
        return 0;
    }

    /* 1. Compile a structured constraint block from Layer 1 state.
     *    This is what the project calls a "behavioral topology
     *    projection" — not a lore dump, not roleplay prompting. */
    char prompt[PE_PROMPT_MAX_BYTES];
    PromptCompilerConfig pcfg;
    prompt_compiler_default_config(&pcfg);
    pcfg.render_profile = p->profile;
    pcfg.chat_format = p->chat_format;
    int pn = prompt_compile_with_input(ctx, &pcfg, ctx->user_input,
                                       prompt, (int)sizeof(prompt));
    if (pn <= 0){
        out->flags = 2u;
        return 0;
    }
    apply_frame_budget(p, ctx);

    /* 2. Ship to the configured provider.  Currently: Ollama.
     *    More providers slot in here behind the same dispatch. */
    if (!strcmp(p->provider, "ollama")){
        char wide[PE_SLM_WIDE_TEXT];
        int n = ollama_generate(&p->ollama, prompt, ctx->seed,
                                wide, (int)sizeof(wide));
        if (n > 0){
            out->output_len = compress_to_render_result(
                wide, out->output, (int)sizeof(out->output));
            out->confidence = 750;   /* placeholder until logprob-based */
            out->flags = 0;
            return 0;
        }
        /* network / protocol / timeout failure → fall back to template */
        out->flags = 2u;
        return 0;
    }

    if (!strcmp(p->provider, "api")){
        char wide[PE_SLM_WIDE_TEXT];
        int n = api_generate(&p->api, prompt, ctx->seed,
                             wide, (int)sizeof(wide));
        if (n > 0){
            out->output_len = compress_to_render_result(
                wide, out->output, (int)sizeof(out->output));
            out->confidence = 800;
            out->flags = 0;
            return 0;
        }
        out->flags = 2u;
        return 0;
    }

    /* Unknown provider — fall back. */
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
