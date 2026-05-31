/* aether.h — libaether public API.
 *
 * AETHER is the long-term episodic storage layer for PersonaConsole.
 * Targets the Pentium III hardware budget: 128 MB RAM, slow disk, no malloc
 * in hot paths.  Holds 10M+ events on disk; queries via SimHash bucket scan
 * with mmap-on-demand bucket files (~600 KB each) and a Write-Ahead Log.
 *
 * Design summary:
 *   - 256 bucket files keyed by top 8 bits of 64-bit SimHash.
 *   - One zone file (rotated at 256 MB) holds the raw event payloads.
 *   - WAL absorbs all writes; nightly consolidation merges into buckets.
 *   - Query scans the relevant bucket AND the WAL, merges, returns top-K.
 *
 * NOTE: aether_event_t is exactly 64 bytes, naturally aligned — no packed.
 */
#ifndef AETHER_H
#define AETHER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- public types ---------- */

#define AETHER_INLINE_TEXT 40
#define AETHER_KEY_COUNT   4

/* The fundamental episodic memory unit.  Exactly 64 bytes.  Natural
 * alignment — every field is at a multiple of its own size, so loads on
 * Pentium III are aligned and single-cycle. */
typedef struct {
    uint32_t timestamp;             /* 4B: unix seconds when event occurred */
    uint32_t last_accessed;         /* 4B: unix seconds for LRU / decay */
    uint16_t emotion_arousal;       /* 2B: 0..65535 */
    uint16_t emotion_valence;       /* 2B: 0..65535 */
    uint16_t emotion_dominance;     /* 2B: 0..65535 */
    uint16_t retrievability_score;  /* 2B: 0..65535, decays */
    uint16_t keys[AETHER_KEY_COUNT];/* 8B: caller-defined keyword IDs */
    char     inline_text[AETHER_INLINE_TEXT]; /* 40B: short event text */
} aether_event_t;  /* 4+4+2+2+2+2+8+40 = 64 */

/* Opaque handle. */
typedef struct aether_handle aether_handle_t;

/* ---------- public API ---------- */

/* Install a canonical-clock callback. The engine calls this once with
 * pe_clock_now_s before aether_open so AETHER timestamps flow through the
 * same clock as the rest of Layer 1 — required for deterministic replay
 * under PE_CLOCK_OVERRIDE_MS. If never set, ae_now_unix() falls back to
 * time(NULL) (acceptable for libaether-only test programs). */
typedef uint32_t (*aether_clock_now_s_fn)(void);
void aether_set_clock(aether_clock_now_s_fn fn);

/* Open (or create) a store rooted at storage_dir.
 * Returns NULL on failure (mkdir/open error, corrupt critical file). */
aether_handle_t *aether_open(const char *storage_dir);

/* Append an event to the WAL.  O(1) amortised — no index update.
 * Returns 0 on success, -1 on I/O error. */
int aether_put(aether_handle_t *h, const aether_event_t *ev);

/* Query top-K most-similar events to the given SimHash.
 * - Scans the matching bucket file (mmap, Hamming-rank).
 * - Linear-scans the WAL (Hamming-rank pending events).
 * - Merges, returns up to max_results events sorted by Hamming distance
 *   (closest first) into out_results.
 * - max_age_seconds: if non-zero, skip events older than now-max_age.
 * Returns the number of results written. */
int aether_query_by_hash(aether_handle_t *h,
                         uint64_t query_simhash,
                         aether_event_t *out_results,
                         int max_results,
                         uint32_t max_age_seconds);

/* Convenience: query by raw text.  Computes SimHash internally. */
int aether_query_by_text(aether_handle_t *h,
                         const char *text,
                         aether_event_t *out_results,
                         int max_results,
                         uint32_t max_age_seconds);

/* Force consolidation: drain WAL into zone file(s), rebuild dirty bucket
 * files atomically (write to .new + POSIX rename), clear WAL.
 * Incremental — only rebuilds buckets marked dirty since the last merge.
 * If full=1, rebuilds every bucket (use weekly). */
int aether_consolidate(aether_handle_t *h, int full);

/* Returns 1 if the WAL has exceeded the soft threshold and a
 * consolidation pass is recommended. */
int aether_should_consolidate(const aether_handle_t *h);

/* Close store: persist dirty bitmap, close fds, free handle. */
void aether_close(aether_handle_t *h);

/* ---------- stats (read-only inspection) ---------- */

typedef struct {
    uint32_t wal_event_count;
    uint32_t wal_bytes;
    uint32_t dirty_bucket_count;
    uint32_t current_zone_id;
    uint64_t current_zone_size;
} aether_stats_t;

void aether_stats(const aether_handle_t *h, aether_stats_t *out);

#ifdef __cplusplus
}
#endif
#endif /* AETHER_H */
