/* json.c — minimal JSON encode/decode for PersonaHost. */
#include "json.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

int json_emit_quoted(char *dst, int cap, const char *src){
    if (!dst || cap <= 0) return -1;
    int w = 0;
    if (w + 1 >= cap) return -1;
    dst[w++] = '"';
    for (; *src; ++src){
        char esc = 0;
        switch (*src){
            case '"':  esc = '"';  break;
            case '\\': esc = '\\'; break;
            case '\n': esc = 'n';  break;
            case '\r': esc = 'r';  break;
            case '\t': esc = 't';  break;
        }
        if (esc){
            if (w + 2 >= cap) return -1;
            dst[w++] = '\\';
            dst[w++] = esc;
        } else if ((unsigned char)*src < 0x20){
            /* drop control chars below 0x20 — keeps output compact */
            continue;
        } else {
            if (w + 1 >= cap) return -1;
            dst[w++] = *src;
        }
    }
    if (w + 1 >= cap) return -1;
    dst[w++] = '"';
    dst[w] = 0;
    return w;
}

/* Locate the value following "key": in body, returning a pointer to the
 * first non-whitespace char after the colon (the value itself).  Returns
 * NULL if key not found. */
static const char *find_value(const char *body, const char *key){
    if (!body || !key) return NULL;
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL){
        /* require the match to be preceded by a quote */
        if (p > body && *(p - 1) == '"'
            && *(p + klen) == '"'){
            const char *q = p + klen + 1;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == ':'){
                q++;
                while (*q == ' ' || *q == '\t') q++;
                return q;
            }
        }
        p += klen;
    }
    return NULL;
}

int json_get_string(const char *body, const char *key,
                    char *out, int out_cap){
    if (!body || !key || !out || out_cap <= 0) return -1;
    const char *v = find_value(body, key);
    if (!v || *v != '"') return -1;
    v++;
    int w = 0;
    while (*v && *v != '"'){
        char c = *v++;
        if (c == '\\' && *v){
            char esc = *v++;
            switch (esc){
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                default:   c = esc;  break;
            }
        }
        if (w + 1 >= out_cap) return -1;
        out[w++] = c;
    }
    out[w] = 0;
    return 0;
}

int json_get_int(const char *body, const char *key, int *out){
    if (!body || !key || !out) return -1;
    const char *v = find_value(body, key);
    if (!v) return -1;
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (end == v) return -1;
    *out = (int)n;
    return 0;
}
