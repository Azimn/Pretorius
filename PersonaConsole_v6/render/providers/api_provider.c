/* api_provider.c - optional OpenAI-compatible API provider via curl.
 *
 * No SDK, no TLS library linkage, no required cloud dependency. The
 * standard runtime can be built and used without curl or an API key. This
 * provider activates only when PE_SLM_PROVIDER=api is selected.
 */
#include "api_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void copy_env(char *dst, size_t cap, const char *name, const char *fallback){
    const char *v = getenv(name);
    snprintf(dst, cap, "%s", (v && v[0]) ? v : fallback);
}

void api_load_config(ApiConfig *cfg){
    if (!cfg) return;
    const char *to   = getenv("PE_API_TIMEOUT_MS");
    const char *temp = getenv("PE_API_TEMP");
    const char *tok  = getenv("PE_API_MAX_TOKENS");

    copy_env(cfg->url,      sizeof(cfg->url),      "PE_API_URL",
             "https://api.openai.com/v1/chat/completions");
    copy_env(cfg->model,    sizeof(cfg->model),    "PE_API_MODEL", "gpt-4.1-mini");
    copy_env(cfg->api_key,  sizeof(cfg->api_key),  "PE_API_KEY", "");
    copy_env(cfg->curl_bin, sizeof(cfg->curl_bin), "PE_API_CURL", "curl");

    cfg->timeout_ms            = to   ? atoi(to)   : 30000;
    cfg->temperature_per_mille = temp ? atoi(temp) : 200;
    cfg->max_tokens            = tok  ? atoi(tok)  : 512;

    if (cfg->timeout_ms < 1000) cfg->timeout_ms = 1000;
    if (cfg->timeout_ms > 300000) cfg->timeout_ms = 300000;
    if (cfg->temperature_per_mille < 0) cfg->temperature_per_mille = 0;
    if (cfg->temperature_per_mille > 2000) cfg->temperature_per_mille = 2000;
    if (cfg->max_tokens < 32) cfg->max_tokens = 32;
    if (cfg->max_tokens > 4096) cfg->max_tokens = 4096;
}

static int safe_cmd_token(const char *s){
    if (!s || !s[0]) return 0;
    for (const unsigned char *p = (const unsigned char*)s; *p; ++p){
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
            *p == '/' || *p == '.' || *p == ':' || *p == '\\')
            continue;
        return 0;
    }
    return 1;
}

static int json_escape_append(char *buf, int cap, int *pos, const char *s){
    if (!s) return 1;
    while (*s){
        if (*pos >= cap - 2) return 0;
        unsigned char c = (unsigned char)*s++;
        if (c == '"' || c == '\\'){
            buf[(*pos)++] = '\\';
            if (*pos >= cap - 1) return 0;
            buf[(*pos)++] = (char)c;
        } else if (c == '\n'){
            buf[(*pos)++] = '\\'; buf[(*pos)++] = 'n';
        } else if (c == '\r'){
            buf[(*pos)++] = '\\'; buf[(*pos)++] = 'r';
        } else if (c == '\t'){
            buf[(*pos)++] = '\\'; buf[(*pos)++] = 't';
        } else if (c < 0x20){
            int n = snprintf(buf + *pos, cap - *pos, "\\u%04x", c);
            if (n < 0 || n >= cap - *pos) return 0;
            *pos += n;
        } else {
            buf[(*pos)++] = (char)c;
        }
    }
    return 1;
}

static int json_extract_string(const char *json, const char *key,
                               char *out, int out_cap){
    char pat[64];
    int pn = snprintf(pat, sizeof(pat), "\"%s\":", key);
    if (pn < 0) return -1;
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += pn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (*p != '"') return -1;
    ++p;
    int w = 0;
    while (*p && w < out_cap - 1){
        if (*p == '\\' && p[1]){
            char c = p[1];
            ++p;
            switch (c){
            case 'n': out[w++] = '\n'; break;
            case 'r': out[w++] = '\r'; break;
            case 't': out[w++] = '\t'; break;
            case '"': out[w++] = '"'; break;
            case '\\': out[w++] = '\\'; break;
            default: out[w++] = c; break;
            }
            ++p;
        } else if (*p == '"'){
            break;
        } else {
            out[w++] = *p++;
        }
    }
    out[w] = 0;
    return w;
}

static void trim_ws(char *s, int *len){
    int w = *len;
    while (w > 0 && (s[w-1] == '\n' || s[w-1] == '\r' ||
                     s[w-1] == ' ' || s[w-1] == '\t'))
        s[--w] = 0;
    *len = w;
}

static int write_text_file(const char *path, const char *text){
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n;
    fclose(f);
    return ok;
}

static int read_pipe(FILE *pipe, char *buf, int cap){
    int got = 0;
    while (got < cap - 1){
        size_t n = fread(buf + got, 1, (size_t)(cap - 1 - got), pipe);
        if (n == 0) break;
        got += (int)n;
    }
    buf[got] = 0;
    return got;
}

int api_generate(const ApiConfig *cfg,
                 const char *prompt,
                 unsigned int seed,
                 char *out, int out_cap){
    if (!cfg || !prompt || !out || out_cap < 64) return -2;
    if (!safe_cmd_token(cfg->curl_bin)) return -2;

    char body[24576];
    int p = snprintf(body, sizeof(body),
                     "{\"model\":\"%s\",\"messages\":["
                     "{\"role\":\"system\",\"content\":\"PersonaConsole renderer. Return only the character reply. Layer 1 state is authoritative.\"},"
                     "{\"role\":\"user\",\"content\":\"",
                     cfg->model);
    if (p < 0 || p >= (int)sizeof(body)) return -2;
    if (!json_escape_append(body, (int)sizeof(body), &p, prompt)) return -2;
    int n = snprintf(body + p, sizeof(body) - (size_t)p,
                     "\"}],\"temperature\":%.3f,\"max_tokens\":%d,\"seed\":%u}",
                     (double)cfg->temperature_per_mille / 1000.0,
                     cfg->max_tokens,
                     seed);
    if (n < 0 || n >= (int)sizeof(body) - p) return -2;

    /* Relative temp names are deliberate: Windows curl may not understand
     * Cygwin /tmp paths when persona_host is built under Cygwin. */
    char body_path[] = ".persona_api_body_XXXXXX";
    char cfg_path[]  = ".persona_api_curl_XXXXXX";
    int bfd = mkstemp(body_path);
    int cfd = mkstemp(cfg_path);
    if (bfd < 0 || cfd < 0){
        if (bfd >= 0) close(bfd);
        if (cfd >= 0) close(cfd);
        return -1;
    }
    close(bfd);
    close(cfd);
    if (!write_text_file(body_path, body)){
        unlink(body_path); unlink(cfg_path); return -1;
    }

    char curl_cfg[4096];
    int q = snprintf(curl_cfg, sizeof(curl_cfg),
                     "silent\nshow-error\nrequest = \"POST\"\nurl = \"%s\"\n"
                     "max-time = \"%d\"\nheader = \"Content-Type: application/json\"\n",
                     cfg->url, cfg->timeout_ms / 1000);
    if (q < 0 || q >= (int)sizeof(curl_cfg)){
        unlink(body_path); unlink(cfg_path); return -2;
    }
    if (cfg->api_key[0]){
        q += snprintf(curl_cfg + q, sizeof(curl_cfg) - (size_t)q,
                      "header = \"Authorization: Bearer %s\"\n", cfg->api_key);
        if (q < 0 || q >= (int)sizeof(curl_cfg)){
            unlink(body_path); unlink(cfg_path); return -2;
        }
    }
    q += snprintf(curl_cfg + q, sizeof(curl_cfg) - (size_t)q,
                  "data-binary = \"@%s\"\n", body_path);
    if (q < 0 || q >= (int)sizeof(curl_cfg)){
        unlink(body_path); unlink(cfg_path); return -2;
    }
    if (!write_text_file(cfg_path, curl_cfg)){
        unlink(body_path); unlink(cfg_path); return -1;
    }

    char cmd[256];
    n = snprintf(cmd, sizeof(cmd), "%s --config %s", cfg->curl_bin, cfg_path);
    if (n < 0 || n >= (int)sizeof(cmd)){
        unlink(body_path); unlink(cfg_path); return -2;
    }
    FILE *pipe = popen(cmd, "r");
    if (!pipe){
        unlink(body_path); unlink(cfg_path); return -1;
    }
    char response[65536];
    int got = read_pipe(pipe, response, (int)sizeof(response));
    int rc = pclose(pipe);
    unlink(body_path);
    unlink(cfg_path);
    if (rc != 0 || got <= 0) return -1;

    int w = json_extract_string(response, "content", out, out_cap);
    if (w < 0) w = json_extract_string(response, "response", out, out_cap);
    if (w < 0) return -2;
    trim_ws(out, &w);
    return w;
}
