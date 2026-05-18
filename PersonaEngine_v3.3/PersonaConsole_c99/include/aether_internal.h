/* aether_internal.h — internal types and constants for libaether.
 * Not part of the public API.  Visible only to aether*.c.
 */
#ifndef AETHER_INTERNAL_H
#define AETHER_INTERNAL_H

#include "aether.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ---------- file-layout constants ---------- */

/* Multi-band LSH (v2 layout):
 *
 * Every event is indexed in AETHER_BAND_COUNT separate bucket files —
 * one per band.  Each band partitions the 64-bit SimHash by an 8-bit
 * slice taken from a different offset (bits 56-63, 40-47, 24-31, 8-15),
 * giving 4 statistically-independent bucket assignments per event.
 *
 * Querying probes the query's bucket in each band and merges Hamming-
 * ranked results.  An event is reachable as long as *any* of its 4
 * bucket slices matches the query's — so paraphrases that differ in
 * the top-8-bit slice no longer disappear (the v1 single-band miss
 * documented in the standalone aether_test).
 *
 * Cost vs v1: 4× index size on disk (480 MB at 10M events), same
 * RAM (mmap one bucket file per band per query), ~4× query CPU
 * (still trivial at ~150 KB per bucket × 4 mmaps). */
#define AETHER_BAND_COUNT          4u
#define AETHER_BAND_BITS           8
#define AETHER_BUCKETS_PER_BAND    (1u << AETHER_BAND_BITS)   /* 256 */
#define AETHER_DIRTY_BYTES_PER_BAND (AETHER_BUCKETS_PER_BAND / 8u)  /* 32 */
/* Legacy alias for code that doesn't care about band partitioning. */
#define AETHER_BUCKET_COUNT        AETHER_BUCKETS_PER_BAND
#define AETHER_BUCKET_BITS         AETHER_BAND_BITS
#define AETHER_WAL_SOFT_LIMIT      (1u << 20)   /* ~1 MB */
#define AETHER_WAL_RECORD_MAGIC    0xAE57AE57u
#define AETHER_BUCKET_MAGIC        0x4B424541u  /* 'AEBK' little-endian */
#define AETHER_BUCKET_VERSION      2u           /* v2 = multi-band layout */
#define AETHER_ZONE_MAX_BYTES      (256u * 1024u * 1024u)  /* 256 MB per zone */

/* Total dirty bitmap bytes (4 bands × 32 each). */
#define AETHER_DIRTY_BYTES         (AETHER_BAND_COUNT * AETHER_DIRTY_BYTES_PER_BAND)

/* Extract the 8-bit bucket ID for a given band from a 64-bit signature.
 * Bands sample non-adjacent 8-bit slices: 56-63, 40-47, 24-31, 8-15. */
static inline uint8_t ae_band_bucket(uint64_t sig, int band){
    return (uint8_t)(sig >> (56 - band * 16));
}

/* Cap the number of events drained from the WAL in a single consolidation
 * pass — keeps the in-memory scratch buffer bounded.  At 64 B per event,
 * 16k events = 1 MB scratch. */
#define AETHER_WAL_MAX_EVENTS    16384u

/* Maximum K supported for top-K queries (bounded so we can stack-alloc). */
#define AETHER_MAX_K             16

/* ---------- on-disk structures ---------- */

/* Bucket file header (16 bytes). */
typedef struct {
    uint32_t magic;        /* AETHER_BUCKET_MAGIC */
    uint32_t version;      /* AETHER_BUCKET_VERSION */
    uint32_t entry_count;  /* number of index_entry_t records following header */
    uint32_t last_rebuild; /* unix timestamp of last consolidation */
} bucket_header_t;

/* Index entry inside a bucket (16 bytes, naturally aligned). */
typedef struct {
    uint64_t signature;     /* full 64-bit SimHash */
    uint32_t event_offset;  /* byte offset in zone file */
    uint16_t zone_id;       /* which zone_NNN.dat */
    uint16_t _pad;          /* keep struct at 16 bytes */
} index_entry_t;

/* WAL record: magic + event (68 bytes total).  Single pwrite is atomic
 * because 68 < PIPE_BUF on every POSIX platform we target. */
typedef struct {
    uint32_t       magic;   /* AETHER_WAL_RECORD_MAGIC */
    aether_event_t event;
} wal_record_t;

/* ---------- handle ---------- */

struct aether_handle {
    char     storage_dir[256];

    /* WAL */
    int      wal_fd;            /* O_RDWR | O_APPEND */
    uint32_t wal_size_bytes;
    uint32_t wal_event_count;   /* cached, updated on put + reload */

    /* Active zone (events appended here until 256 MB, then rotated). */
    int      zone_fd;
    uint16_t zone_id;
    uint32_t zone_size_bytes;

    /* Per-band dirty bitmap — one 256-bit map per band, persisted
     * between sessions as a single 128-byte dirty.dat file. */
    uint8_t  dirty[AETHER_BAND_COUNT][AETHER_DIRTY_BYTES_PER_BAND];
    uint32_t dirty_count;       /* cached total popcount across all bands */
};

/* ---------- internal helpers (implemented in aether.c) ---------- */

int  ae_path_join(char *out, size_t n, const char *a, const char *b);
int  ae_mkdir_p(const char *path);
int  ae_open_create_rw(const char *path);       /* O_CREAT|O_RDWR, 0644 */
int  ae_write_file_atomic(const char *path, const void *buf, size_t n);

/* dirty bitmap ops — operate on a single band's 32-byte map */
static inline void ae_dirty_set(uint8_t *bm, uint8_t bucket){
    bm[bucket >> 3] |= (uint8_t)(1u << (bucket & 7));
}
static inline int  ae_dirty_get(const uint8_t *bm, uint8_t bucket){
    return (bm[bucket >> 3] >> (bucket & 7)) & 1;
}
/* Total popcount across all bands' bitmaps. */
uint32_t ae_dirty_popcount_all(const aether_handle_t *h);

/* WAL helpers (aether_wal.c) */
int  ae_wal_open(aether_handle_t *h);
int  ae_wal_append(aether_handle_t *h, const aether_event_t *ev);
int  ae_wal_scan_for_query(aether_handle_t *h,
                           uint64_t qsig, uint32_t now, uint32_t max_age,
                           int max_k,
                           int       *dists_inout,    /* size max_k */
                           aether_event_t *evs_inout); /* size max_k */
int  ae_wal_drain(aether_handle_t *h,
                  aether_event_t *out, uint32_t cap, uint32_t *out_count);
int  ae_wal_truncate(aether_handle_t *h);

/* Bucket file helpers (aether_bucket.c) — all take a band index 0..3 */
int  ae_bucket_scan(aether_handle_t *h, int band, uint8_t bucket_id,
                    uint64_t qsig, uint32_t now, uint32_t max_age,
                    int max_k,
                    int       *dists_inout,
                    index_entry_t *entries_inout);
int  ae_bucket_write_atomic(aether_handle_t *h, int band, uint8_t bucket_id,
                            const index_entry_t *entries, uint32_t count);
int  ae_bucket_read_all(aether_handle_t *h, int band, uint8_t bucket_id,
                        index_entry_t *out, uint32_t cap, uint32_t *out_count);
/* Ensure the per-band subdirectories exist (called from aether_open). */
int  ae_ensure_band_dirs(const aether_handle_t *h);

/* Zone file helpers (aether_bucket.c) */
int  ae_zone_append(aether_handle_t *h, const aether_event_t *ev,
                    uint16_t *out_zone_id, uint32_t *out_offset);
int  ae_zone_read_event(aether_handle_t *h, uint16_t zone_id, uint32_t offset,
                        aether_event_t *out);
int  ae_zone_init_active(aether_handle_t *h);

/* Top-K helpers (aether.c) — insertion sort into a sorted (dist, payload) array */
typedef struct { int dist; int valid; } topk_slot_t;
void ae_topk_insert_event(int *dists, aether_event_t *evs, int k,
                          int dist, const aether_event_t *ev);

/* Time helpers */
uint32_t ae_now_unix(void);

#endif /* AETHER_INTERNAL_H */
