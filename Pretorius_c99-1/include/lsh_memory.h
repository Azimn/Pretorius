/* lsh_memory.h — Locality-Sensitive Hashing semantic signatures.
 *
 * 64-bit SimHash signatures over byte 4-grams.
 * SimHash IS locality-sensitive: Hamming distance ≈ cosine distance of the
 * underlying feature vectors (here, the bag of 4-grams).
 *
 * Brute-force linear scan: 20k signatures = 160 KB, fits in PIII L2.
 * NO bsearch — integer sort destroys Hamming locality.
 */
#ifndef PE_LSH_MEMORY_H
#define PE_LSH_MEMORY_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t lsh_sig_t;

/* Number of equal-width slices in a signature. Used for table partitioning
 * (AND-of-OR LSH retrieval): two signatures within Hamming radius r are
 * extremely likely to collide in at least one slice. */
#define LSH_N_BANDS   4
#define LSH_BAND_BITS 16
#define LSH_BAND_MASK 0xFFFFu

/* Sentinel returned by lsh_find_nearest when database is empty. */
#define LSH_DIST_MAX  65

/* Compute a 64-bit LSH signature over a NUL-terminated string.
 * Empty string → 0. Strings <4 bytes hash their partial window. */
lsh_sig_t lsh_compute(const char *text);

/* Compute over an explicit length (no NUL required). */
lsh_sig_t lsh_compute_n(const char *text, size_t len);

/* Hamming distance, 0..64. */
int lsh_hamming_distance(lsh_sig_t a, lsh_sig_t b);

/* 16-bit bucket index for one of the LSH_N_BANDS slices (0..3). */
static inline uint16_t lsh_bucket(lsh_sig_t sig, int band){
    return (uint16_t)((sig >> (band * LSH_BAND_BITS)) & LSH_BAND_MASK);
}

/* Linear brute-force nearest-neighbor search.
 * Returns the signature with smallest Hamming distance to query_sig.
 * If out_distance != NULL, writes the best distance there.
 * If out_index != NULL, writes the index of the best match (-1 if empty). */
lsh_sig_t lsh_find_nearest(lsh_sig_t query_sig,
                           const lsh_sig_t *database, int count,
                           int *out_distance, int *out_index);

#endif /* PE_LSH_MEMORY_H */
