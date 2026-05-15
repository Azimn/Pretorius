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
    uint64_t sa = ((const index_entry_t *)a)->signature;
    uint64_t sb = ((const index_entry_t *)b)->signature;
    if (sa < sb) return -1;
    if (sa > sb) return  1;
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

int aether_consolidate(aether_handle_t *h, int full){
    if (!h) return -1;

    /* 1. Drain WAL. */
    aether_event_t *drained = (aether_event_t *)
        malloc(AETHER_WAL_MAX_EVENTS * sizeof(aether_event_t));
    if (!drained) return -1;
    uint32_t drained_n = 0;
    ae_wal_drain(h, drained, AETHER_WAL_MAX_EVENTS, &drained_n);

    /* 2. For each drained event: append to zone, compute sig, bucket-tag. */
    bucket_scratch_t per_bucket[AETHER_BUCKET_COUNT];
    memset(per_bucket, 0, sizeof(per_bucket));

    for (uint32_t i = 0; i < drained_n; ++i){
        const aether_event_t *ev = &drained[i];

        uint16_t z_id;
        uint32_t z_off;
        if (ae_zone_append(h, ev, &z_id, &z_off) != 0){
            free(drained);
            for (uint32_t b = 0; b < AETHER_BUCKET_COUNT; ++b) free(per_bucket[b].entries);
            return -1;
        }

        uint64_t sig = lsh_simhash((const uint8_t *)ev->inline_text,
                                   strnlen(ev->inline_text, AETHER_INLINE_TEXT));
        uint8_t bucket = (uint8_t)(sig >> 56);

        index_entry_t e;
        e.signature    = sig;
        e.event_offset = z_off;
        e.zone_id      = z_id;
        e._pad         = 0;

        if (bs_push(&per_bucket[bucket], &e) != 0){
            free(drained);
            for (uint32_t b = 0; b < AETHER_BUCKET_COUNT; ++b) free(per_bucket[b].entries);
            return -1;
        }
        /* Defensive: ensure dirty bitmap reflects this even after a full
         * rebuild request — otherwise the per-bucket counters mismatch. */
        if (!ae_dirty_get(h->dirty, bucket)){
            ae_dirty_set(h->dirty, bucket);
            h->dirty_count++;
        }
    }
    free(drained);

    /* 3. Rebuild each touched (or all, if full) bucket. */
    for (uint32_t b = 0; b < AETHER_BUCKET_COUNT; ++b){
        int touched = ae_dirty_get(h->dirty, (uint8_t)b);
        if (!full && !touched) continue;

        /* Read existing entries.  Cap at 1M — far above 39k expected. */
        const uint32_t READ_CAP = 1000000u;
        index_entry_t *existing = (index_entry_t *)
            malloc(READ_CAP * sizeof(index_entry_t));
        if (!existing){
            for (uint32_t bb = 0; bb < AETHER_BUCKET_COUNT; ++bb)
                free(per_bucket[bb].entries);
            return -1;
        }
        uint32_t existing_n = 0;
        ae_bucket_read_all(h, (uint8_t)b, existing, READ_CAP, &existing_n);

        /* Combine. */
        uint32_t new_n   = per_bucket[b].count;
        uint32_t total_n = existing_n + new_n;
        if (total_n == 0){
            free(existing);
            free(per_bucket[b].entries);
            per_bucket[b].entries = NULL;
            continue;
        }

        index_entry_t *combined = (index_entry_t *)
            malloc(total_n * sizeof(index_entry_t));
        if (!combined){
            free(existing);
            for (uint32_t bb = 0; bb < AETHER_BUCKET_COUNT; ++bb)
                free(per_bucket[bb].entries);
            return -1;
        }
        if (existing_n) memcpy(combined, existing, existing_n * sizeof(index_entry_t));
        if (new_n)
            memcpy(combined + existing_n, per_bucket[b].entries,
                   new_n * sizeof(index_entry_t));
        free(existing);

        qsort(combined, total_n, sizeof(index_entry_t), cmp_index_entry_sig);

        if (ae_bucket_write_atomic(h, (uint8_t)b, combined, total_n) != 0){
            free(combined);
            for (uint32_t bb = 0; bb < AETHER_BUCKET_COUNT; ++bb)
                free(per_bucket[bb].entries);
            return -1;
        }
        free(combined);
    }

    /* 4. Free per-bucket scratch. */
    for (uint32_t b = 0; b < AETHER_BUCKET_COUNT; ++b)
        free(per_bucket[b].entries);

    /* 5. Truncate WAL, clear dirty bitmap, persist. */
    ae_wal_truncate(h);
    memset(h->dirty, 0, AETHER_DIRTY_BYTES);
    h->dirty_count = 0;
    return 0;
}
