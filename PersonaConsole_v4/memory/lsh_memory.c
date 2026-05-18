/* lsh_memory.c — SimHash over byte 4-grams.
 *
 * SimHash IS locality-sensitive: Hamming distance between two signatures
 * approximates the cosine similarity of the underlying feature vectors
 * (here, the bag of 4-grams). Near-paraphrases share most 4-grams, so
 * their accumulators agree on most of the 64 bit positions.
 *
 * For each 4-gram:
 *   1. compute a 64-bit hash (splitmix64-style mixer, integer-only).
 *   2. for each of 64 bit positions, accumulator[b] += (bit_b set ? +1 : -1).
 * Final signature: bit b = 1 iff accumulator[b] > 0.
 *
 * Pentium-III-friendly:
 *   - one 64-bit multiply per 4-gram (≈10 cycles)
 *   - 64 integer add/sub per 4-gram
 *   - no FPU, no SSE required
 */
#include "lsh_memory.h"

static int popcount64(uint64_t x){
    x = (x & 0x5555555555555555ULL) + ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x & 0x0F0F0F0F0F0F0F0FULL) + ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL);
    x = (x & 0x00FF00FF00FF00FFULL) + ((x >> 8) & 0x00FF00FF00FF00FFULL);
    x = (x & 0x0000FFFF0000FFFFULL) + ((x >> 16) & 0x0000FFFF0000FFFFULL);
    x = (x & 0x00000000FFFFFFFFULL) + ((x >> 32) & 0x00000000FFFFFFFFULL);
    return (int)x;
}

int lsh_hamming_distance(lsh_sig_t a, lsh_sig_t b){
    return popcount64(a ^ b);
}

/* splitmix64-style 32→64 mixer.  Excellent avalanche, all-integer. */
static uint64_t mix64(uint32_t w){
    uint64_t h = (uint64_t)w * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 33;
    h *= 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 29;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return h;
}

static void accumulate(int32_t acc[64], uint64_t h){
    int b;
    for (b = 0; b < 64; b++){
        if (h & ((uint64_t)1 << b)) acc[b]++;
        else                         acc[b]--;
    }
}

static lsh_sig_t finalize(const int32_t acc[64]){
    lsh_sig_t sig = 0;
    int b;
    for (b = 0; b < 64; b++){
        if (acc[b] > 0) sig |= ((uint64_t)1 << b);
    }
    return sig;
}

lsh_sig_t lsh_compute_n(const char *text, size_t len){
    int32_t acc[64];
    uint32_t window = 0;
    const unsigned char *p = (const unsigned char*)text;
    size_t i;
    int b;

    if (!text || len == 0) return 0;

    for (b = 0; b < 64; b++) acc[b] = 0;

    if (len < 4){
        /* pad short strings to 4 bytes with a sentinel */
        for (i = 0; i < len; i++) window = (window << 8) | p[i];
        for (; i < 4; i++)        window = (window << 8) | 0x80u;
        accumulate(acc, mix64(window));
        return finalize(acc);
    }

    for (i = 0; i < 4; i++) window = (window << 8) | p[i];

    while (1){
        accumulate(acc, mix64(window));
        if (i >= len) break;
        window = (window << 8) | p[i];
        i++;
    }

    return finalize(acc);
}

lsh_sig_t lsh_compute(const char *text){
    size_t n = 0;
    if (!text) return 0;
    while (text[n]) n++;
    return lsh_compute_n(text, n);
}

lsh_sig_t lsh_find_nearest(lsh_sig_t query_sig,
                           const lsh_sig_t *database, int count,
                           int *out_distance, int *out_index){
    int best_dist = LSH_DIST_MAX;
    int best_idx  = -1;
    lsh_sig_t best_match = 0;
    int i;

    for (i = 0; i < count; i++){
        int d = lsh_hamming_distance(query_sig, database[i]);
        if (d < best_dist){
            best_dist = d;
            best_match = database[i];
            best_idx = i;
            if (d == 0) break;
        }
    }

    if (out_distance) *out_distance = best_dist;
    if (out_index)    *out_index    = best_idx;
    return best_match;
}
