/* ngram_lm.h — character n-gram language model with stupid-backoff scoring.
 *
 * Used as a "Pretorianness" reranker: given a candidate utterance, returns a
 * log-probability under a small character-level n-gram model trained offline
 * on Victorian/scientific/gothic text. Higher score = more period-authentic.
 *
 * Format: binary file written by tools/build_lm. Loaded once at engine init.
 * Runtime is stateless and deterministic — pure integer math after the
 * compile-time logarithm table.
 *
 * Period authenticity: n-gram LMs were SOTA in 1999–2002. Stupid backoff
 * (Brants et al. 2007) is the post-period simplification we'd apply with
 * 2025 hindsight; quality is statistically indistinguishable from Kneser-Ney
 * for reranking tasks but the code is ~10× simpler.
 */
#ifndef PE_NGRAM_LM_H
#define PE_NGRAM_LM_H

#include <stdint.h>
#include <stddef.h>

#define PE_LM_MAX_ORDER  5
#define PE_LM_MAGIC      0x4D4C4550u   /* "PELM" little-endian */
#define PE_LM_VERSION    1

typedef struct NGramLM NGramLM;

/* Load from disk. Returns NULL on failure. */
NGramLM *ngram_lm_load(const char *path);

/* Load from an in-memory buffer (e.g. mmap'd). LM borrows the buffer —
 * caller must keep it alive for the LM's lifetime. */
NGramLM *ngram_lm_load_mem(const void *data, size_t size);

void ngram_lm_free(NGramLM *lm);

/* Score a NUL-terminated string. Returns the sum of log-probabilities
 * across all character positions, scaled by 1000 (milli-nats).
 *
 * Higher (less negative) = more probable under the model = more Pretorian.
 * A typical Victorian sentence scores ≈ -2500 per character. */
int32_t ngram_lm_score(const NGramLM *lm, const char *text);
int32_t ngram_lm_score_n(const NGramLM *lm, const char *text, size_t len);

/* Length-normalized score: returns per-character milli-nat log-prob.
 * Comparable across utterances of different lengths. */
int32_t ngram_lm_score_normalized(const NGramLM *lm, const char *text);

/* Introspection: highest order present in the LM (≤ PE_LM_MAX_ORDER). */
int ngram_lm_order(const NGramLM *lm);

/* Introspection: total number of n-grams stored at order n (1..order). */
uint32_t ngram_lm_count_at_order(const NGramLM *lm, int order);

#endif /* PE_NGRAM_LM_H */
