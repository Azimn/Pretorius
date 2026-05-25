/* aether_consolidate.c — LSM-tree merge: WAL → zones + dirty buckets.
 *
 * Algorithm (per call):
 *   1. Drain the WAL into a stack-bounded scratch list of events.
 *   2. Append every drained event to the active zone file; collect new
 *      index_entry_t tuples, grouped per bucket.
 *   3. For each bucket marked dirty (incremental) — or all 256 (full pass):
 *      a. Read existing bucket entries.
 *      b. Append new entries for that bucket.
 *      c. Sort by signature (stable enough for our use).
 *      d. Atomic write (.new + rename).
 *   4. Clear dirty bitmap, truncate WAL.
 *
 * Allocations: one heap scratch of (events × 64 B) + per-bucket buffers
 * sized to existing-count + new-count.  Per call, dominated by the WAL
 * drain (max ~1 MB).  No per-event malloc.
 */
#include "aether.h"
#include "aether_internal.h"
#include "lsh_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* sort helpers */
static int cmp_index_entry_sig(const void *a, const void *b){
    const index_entry_t *ea = (const index_entry_t *)a;
    const index_entry_t *eb = (const index_entry_t *)b;
    if (ea->signature != eb->signature)
        return ea->signature < eb->signature ? -1 : 1;
    /* qsort is not stable: equal-signature entries (LSH collisions are real
     * for near-duplicate memories) must order identically every run, or the
     * on-disk bucket bytes — and the order a bucket scan returns matches —
     * become platform-dependent, breaking behavioral holography. Tie-break
     * on the unique (zone, offset) location of each event. */
    if (ea->zone_id != eb->zone_id)
        return ea->zone_id < eb->zone_id ? -1 : 1;
    if (ea->event_offset != eb->event_offset)
        return ea->event_offset < eb->event_offset ? -1 : 1;
    return 0;
}

/* Per-bucket scratch entry list. */
typedef struct {
    index_entry_t *entries;
    uint32_t       count;
    uint32_t       cap;
} bucket_scratch_t;

static int bs_push(bucket_scratch_t *bs, const index_entry_t *e){
    if (bs->count >= bs->cap){
        uint32_t newcap = bs->cap ? bs->cap * 2 : 64;
        index_entry_t *p = (index_entry_t *)realloc(bs->entries, newcap * sizeof(*p));
        if (!p) return -1;
        bs->entries = p;
        bs->cap     = newcap;
    }
    bs->entries[bs->count++] = *e;
    return 0;
}

/* Free all per-band scratch lists. */
static void free_scratch(bucket_scratch_t scratch[AETHER_BAND_COUNT][AETHER_BUCKETS_PER_BAND]){
    for (unsigned band = 0; band < AETHER_BAND_COUNT; ++band)
        for (unsigned b = 0; b < AETHER_BUCKETS_PER_BAND; ++b)
            free(scratch[band][b].entries);
}

int aether_consolidate(aether_handle_t *h, int full){
    if (!h) return -1;

    /* 1. Drain WAL. */
    aether_event_t *drained = (aether_event_t *)
        malloc(AETHER_WAL_MAX_EVENTS * sizeof(aether_event_t));
    if (!drained) return -1;
    uint32_t drained_n = 0;
    ae_wal_drain(h, drained, AETHER_WAL_MAX_EVENTS, &drained_n);

    /* 2. For each drained event: append once to zone, compute sig,
     * push the same (sig, offset, zone) tuple into AETHER_BAND_COUNT
     * different bucket scratches — one per band's bucket assignment.
     * Each event will be indexed in 4 bucket files at consolidation
     * finish, enabling multi-band recall. */
    bucket_scratch_t scratch[AETHER_BAND_COUNT][AETHER_BUCKETS_PER_BAND];
    memset(scratch, 0, sizeof(scratch));

    for (uint32_t i = 0; i < drained_n; ++i){
        const aether_event_t *ev = &drained[i];

        uint16_t z_id;
        uint32_t z_off;
        if (ae_zone_append(h, ev, &z_id, &z_off) != 0){
            free(drained);
            free_scratch(scratch);
            return -1;
        }

        uint64_t sig = lsh_simhash((const uint8_t *)ev->inline_text,
                                   strnlen(ev->inline_text, AETHER_INLINE_TEXT));

        index_entry_t e;
        e.signature    = sig;
        e.event_offset = z_off;
        e.zone_id      = z_id;
        e._pad         = 0;

        for (unsigned band = 0; band < AETHER_BAND_COUNT; ++band){
            uint8_t bucket = ae_band_bucket(sig, (int)band);
            if (bs_push(&scratch[band][bucket], &e) != 0){
                free(drained);
                free_scratch(scratch);
                return -1;
            }
            /* Defensive: mark dirty even on a full rebuild so the bookkeeping
             * stays consistent with the rebuild path's expectations. */
            if (!ae_dirty_get(h->dirty[band], bucket)){
                ae_dirty_set(h->dirty[band], bucket);
                h->dirty_count++;
            }
        }
    }
    free(drained);

    /* 3. Rebuild each touched (or all, if full) bucket — per band. */
    const uint32_t READ_CAP = 1000000u;
    index_entry_t *existing = (index_entry_t *)
        malloc(READ_CAP * sizeof(index_entry_t));
    if (!existing){ free_scratch(scratch); return -1; }

    for (unsigned band = 0; band < AETHER_BAND_COUNT; ++band){
        for (unsigned b = 0; b < AETHER_BUCKETS_PER_BAND; ++b){
            int touched = ae_dirty_get(h->dirty[band], (uint8_t)b);
            if (!full && !touched) continue;

            uint32_t existing_n = 0;
            ae_bucket_read_all(h, (int)band, (uint8_t)b,
                               existing, READ_CAP, &existing_n);

            uint32_t new_n   = scratch[band][b].count;
            uint32_t total_n = existing_n + new_n;
            if (total_n == 0) continue;

            index_entry_t *combined = (index_entry_t *)
                malloc(total_n * sizeof(index_entry_t));
            if (!combined){
                free(existing);
                free_scratch(scratch);
                return -1;
            }
            if (existing_n)
                memcpy(combined, existing,
                       existing_n * sizeof(index_entry_t));
            if (new_n)
                memcpy(combined + existing_n, scratch[band][b].entries,
                       new_n * sizeof(index_entry_t));

            qsort(combined, total_n, sizeof(index_entry_t),
                  cmp_index_entry_sig);

            if (ae_bucket_write_atomic(h, (int)band, (uint8_t)b,
                                       combined, total_n) != 0){
                free(combined);
                free(existing);
                free_scratch(scratch);
                return -1;
            }
            free(combined);
        }
    }

    free(existing);
    free_scratch(scratch);

    /* 4. Truncate WAL, clear all per-band dirty bitmaps, persist. */
    ae_wal_truncate(h);
    memset(h->dirty, 0, sizeof(h->dirty));
    h->dirty_count = 0;
    return 0;
}
