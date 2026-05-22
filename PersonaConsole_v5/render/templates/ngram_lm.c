/* ngram_lm.c — see ngram_lm.h.
 *
 * Binary file layout (little-endian):
 *   header:
 *     u32 magic = 'PELM'
 *     u16 version = 1
 *     u8  order              (max n-gram order, ≤ 5)
 *     u8  reserved
 *     u8  alphabet[256]      (byte → 0..vocab-1, 0xFF = OOV)
 *   per order n in 1..order:
 *     u32 entry_count
 *     entries[entry_count]:  sorted by hash ascending
 *       u64 hash
 *       u32 count
 *   trailing:
 *     u64 total_unigram_count
 */
#include "ngram_lm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LM_BACKOFF_LOG_MILLI  (-916)   /* round(log(0.4) * 1000) */
#define LM_OOV_LOG_MILLI      (-12000) /* very-rare-character floor */

typedef struct {
    uint64_t hash;
    uint32_t count;
} LMEntry;

struct NGramLM {
    int order;
    uint8_t alphabet[256];
    uint32_t counts[PE_LM_MAX_ORDER + 1];   /* index 0 unused */
    const LMEntry *table[PE_LM_MAX_ORDER + 1];
    uint64_t total_unigram;
    void *owned;                            /* malloc'd buffer, freed on close */
    size_t owned_size;
};

/* ----- FNV-1a 64 over alphabet-ID bytes ----- */
static uint64_t hash_ids(const uint8_t *ids, int n){
    uint64_t h = 0xCBF29CE484222325ULL;
    int i;
    for (i = 0; i < n; i++){
        h ^= (uint64_t)ids[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

/* ----- binary search in a sorted hash table ----- */
static uint32_t lookup(const LMEntry *t, uint32_t cnt, uint64_t h){
    uint32_t lo = 0, hi = cnt;
    while (lo < hi){
        uint32_t mid = lo + (hi - lo) / 2;
        if (t[mid].hash < h) lo = mid + 1;
        else                 hi = mid;
    }
    if (lo < cnt && t[lo].hash == h) return t[lo].count;
    return 0;
}

/* ----- integer log lookup (returns milli-nats × 1) -----
 * Domain: x in [1, 2^31). Returns round(log(x) * 1000).
 * Uses bit-length + small fractional table for the mantissa.
 * Pure integer, deterministic across platforms.
 */
static int32_t ilog_milli(uint32_t x){
    static const int32_t frac[16] = {
        /* log(1 + i/16) * 1000 for i = 0..15 */
        0,   60,  117,  173,  227,  278,  329,  377,
        423, 470, 513,  557,  598,  640,  679,  718
    };
    int bit;
    uint32_t mant;
    if (x == 0) return -2147483647;
    bit = 0;
    {
        uint32_t v = x;
        while (v > 1){ v >>= 1; bit++; }
    }
    /* x = 2^bit * (1 + f), f in [0,1).  log(x) = bit*ln2 + log(1+f). */
    mant = (x << (31 - bit)) & 0x7FFFFFFFu;       /* fractional part << 31 */
    return bit * 693 + frac[mant >> 27];          /* 693 ≈ ln(2)*1000 */
}

/* log(num/denom) * 1000 = log(num)*1000 - log(denom)*1000 */
static int32_t log_ratio_milli(uint32_t num, uint32_t denom){
    if (num == 0 || denom == 0) return LM_OOV_LOG_MILLI;
    return ilog_milli(num) - ilog_milli(denom);
}

/* ----- core stupid-backoff scoring for one position ----- */
static int32_t score_position(const NGramLM *lm,
                              const uint8_t *ids_window, int n){
    /* ids_window is [oldest .. newest]; predict ids_window[n-1] given prefix.
     * Try longest first, back off on miss. */
    int try_order;
    int32_t penalty = 0;
    for (try_order = n; try_order >= 2; try_order--){
        const uint8_t *start = ids_window + (n - try_order);
        uint64_t h_full = hash_ids(start, try_order);
        uint32_t c_full = lookup(lm->table[try_order], lm->counts[try_order], h_full);
        if (c_full > 0){
            uint64_t h_ctx = hash_ids(start, try_order - 1);
            uint32_t c_ctx;
            if (try_order - 1 == 0) c_ctx = (uint32_t)lm->total_unigram;
            else c_ctx = lookup(lm->table[try_order - 1],
                                lm->counts[try_order - 1], h_ctx);
            if (c_ctx == 0) c_ctx = c_full;     /* shouldn't happen but safe */
            return penalty + log_ratio_milli(c_full, c_ctx);
        }
        penalty += LM_BACKOFF_LOG_MILLI;
    }
    /* unigram fallback */
    {
        uint64_t h_uni = hash_ids(ids_window + (n - 1), 1);
        uint32_t c_uni = lookup(lm->table[1], lm->counts[1], h_uni);
        if (c_uni > 0 && lm->total_unigram > 0)
            return penalty + log_ratio_milli(c_uni, (uint32_t)lm->total_unigram);
    }
    return penalty + LM_OOV_LOG_MILLI;
}

/* ----- public API ----- */

int32_t ngram_lm_score_n(const NGramLM *lm, const char *text, size_t len){
    uint8_t window[PE_LM_MAX_ORDER];
    int filled = 0;
    int32_t total = 0;
    size_t i;
    int order;

    if (!lm || !text || len == 0) return 0;
    order = lm->order;

    for (i = 0; i < len; i++){
        unsigned char c = (unsigned char)text[i];
        uint8_t id = lm->alphabet[c];
        if (id == 0xFF){
            /* OOV byte (e.g., unusual punctuation). Skip but penalize. */
            total += LM_OOV_LOG_MILLI;
            filled = 0;       /* reset context across OOV */
            continue;
        }
        if (filled < order){
            window[filled++] = id;
        } else {
            memmove(window, window + 1, (size_t)(order - 1));
            window[order - 1] = id;
        }
        total += score_position(lm, window, filled);
    }
    return total;
}

int32_t ngram_lm_score(const NGramLM *lm, const char *text){
    if (!text) return 0;
    return ngram_lm_score_n(lm, text, strlen(text));
}

int32_t ngram_lm_score_normalized(const NGramLM *lm, const char *text){
    size_t n;
    int32_t total;
    if (!text) return 0;
    n = strlen(text);
    if (n == 0) return 0;
    total = ngram_lm_score_n(lm, text, n);
    return total / (int32_t)n;
}

int ngram_lm_order(const NGramLM *lm){ return lm ? lm->order : 0; }

uint32_t ngram_lm_count_at_order(const NGramLM *lm, int order){
    if (!lm || order < 1 || order > lm->order) return 0;
    return lm->counts[order];
}

/* ----- loading ----- */

NGramLM *ngram_lm_load_mem(const void *data, size_t size){
    const uint8_t *p = (const uint8_t*)data;
    const uint8_t *end = p + size;
    NGramLM *lm;
    uint32_t magic;
    uint16_t version;
    uint8_t order;
    int n;

    if (size < 4 + 2 + 1 + 1 + 256) return NULL;
    memcpy(&magic, p, 4); p += 4;
    if (magic != PE_LM_MAGIC) return NULL;
    memcpy(&version, p, 2); p += 2;
    if (version != PE_LM_VERSION) return NULL;
    order = *p++;
    p++;                                       /* reserved */
    if (order < 1 || order > PE_LM_MAX_ORDER) return NULL;

    lm = (NGramLM*)calloc(1, sizeof(*lm));
    if (!lm) return NULL;
    lm->order = order;
    memcpy(lm->alphabet, p, 256); p += 256;

    for (n = 1; n <= order; n++){
        uint32_t cnt;
        if (p + 4 > end){ free(lm); return NULL; }
        memcpy(&cnt, p, 4); p += 4;
        if (p + (size_t)cnt * sizeof(LMEntry) > end){ free(lm); return NULL; }
        lm->counts[n] = cnt;
        lm->table[n]  = (const LMEntry*)p;
        p += (size_t)cnt * sizeof(LMEntry);
    }
    if (p + 8 > end){ free(lm); return NULL; }
    memcpy(&lm->total_unigram, p, 8);
    return lm;
}

NGramLM *ngram_lm_load(const char *path){
    FILE *f = fopen(path, "rb");
    long sz;
    void *buf;
    NGramLM *lm;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0){ fclose(f); return NULL; }
    sz = ftell(f);
    if (sz <= 0){ fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)sz);
    if (!buf){ fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz){
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    lm = ngram_lm_load_mem(buf, (size_t)sz);
    if (!lm){ free(buf); return NULL; }
    lm->owned = buf;
    lm->owned_size = (size_t)sz;
    return lm;
}

void ngram_lm_free(NGramLM *lm){
    if (!lm) return;
    if (lm->owned) free(lm->owned);
    free(lm);
}
