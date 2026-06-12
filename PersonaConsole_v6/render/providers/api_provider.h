/* api_provider.h - optional OpenAI-compatible frontier/API provider.
 *
 * This provider is intentionally runtime-optional. It shells out to curl
 * when PE_SLM_PROVIDER=api is selected, so the normal template/Micro build
 * does not link TLS or cloud SDK dependencies. Renderer prose remains
 * disposable; Layer 1 state is still authoritative.
 */
#ifndef PERSONA_API_PROVIDER_H
#define PERSONA_API_PROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char url[192];
    char model[96];
    char api_key[160];
    char curl_bin[96];
    int  timeout_ms;
    int  temperature_per_mille;
    int  max_tokens;
} ApiConfig;

void api_load_config(ApiConfig *cfg);

int api_generate(const ApiConfig *cfg,
                 const char *prompt,
                 unsigned int seed,
                 char *out, int out_cap);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_API_PROVIDER_H */
