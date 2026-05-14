#include "persona.h"
#include "persona_internal.h"
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

uint32_t persona_now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}

uint32_t persona_rng_u32(NPCState *s){
    uint32_t x = s->rng_state;
    if (x == 0) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng_state = x;
    return x;
}

int32_t persona_rng_range(NPCState *s, int32_t lo, int32_t hi){
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int32_t)(persona_rng_u32(s) % span);
}

uint32_t persona_hash(const char *s){
    uint32_t h = 2166136261u;
    if (!s) return h;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h ? h : 1u;
}

void pe_strlower(char *s){ for (; *s; ++s) *s = (char)tolower((unsigned char)*s); }

int pe_strieq(const char *a, const char *b){
    for (;;) {
        unsigned char ca = (unsigned char)tolower((unsigned char)*a++);
        unsigned char cb = (unsigned char)tolower((unsigned char)*b++);
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

const char *pe_find_lower(const char *hay, const char *needle_lower){
    if (!*needle_lower) return hay;
    size_t nl = 0;
    while (needle_lower[nl]) nl++;
    for (; *hay; ++hay) {
        size_t i;
        for (i = 0; i < nl; ++i) {
            unsigned char c = (unsigned char)tolower((unsigned char)hay[i]);
            if (!hay[i] || c != (unsigned char)needle_lower[i]) break;
        }
        if (i == nl) return hay;
    }
    return NULL;
}

size_t pe_append(char *dst, size_t cap, size_t pos, const char *s){
    while (*s && pos + 1 < cap) dst[pos++] = *s++;
    if (pos < cap) dst[pos] = 0;
    return pos;
}
