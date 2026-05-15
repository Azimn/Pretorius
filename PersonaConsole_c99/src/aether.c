/* aether.c — libaether main entry points.
 *
 * Responsibilities:
 *   - storage directory bootstrap (mkdir, paths)
 *   - handle lifecycle (open, close)
 *   - put / query / stats top-level coordination
 *   - shared path + atomic-write helpers used by WAL / bucket / consolidate
 *
 * No malloc inside aether_put or aether_query_by_hash — both work with
 * stack scratch only.  Consolidation allocates one scratch buffer.
 */
#include "aether.h"
#include "aether_internal.h"
#include "lsh_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

/* ---------- small utilities ---------- */

uint32_t ae_now_unix(void){
    return (uint32_t)time(NULL);
}

int ae_path_join(char *out, size_t n, const char *a, const char *b){
    if (snprintf(out, n, "%s/%s", a, b) >= (int)n) return -1;
    return 0;
}

int ae_mkdir_p(const char *path){
    /* Single-level: caller passes an already-flat directory. */
    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

int ae_open_create_rw(const char *path){
    return open(path, O_RDWR | O_CREAT, 0644);
}

int ae_write_file_atomic(const char *path, const void *buf, size_t n){
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s.new", path) >= (int)sizeof(tmp)) return -1;
    int fd = open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    size_t written = 0;
    const char *p = (const char *)buf;
    while (written < n){
        ssize_t w = write(fd, p + written, n - written);
        if (w <= 0){ close(fd); unlink(tmp); return -1; }
        written += (size_t)w;
    }
    fdatasync(fd);
    close(fd);
    if (rename(tmp, path) != 0){ unlink(tmp); return -1; }
    return 0;
}

uint32_t ae_dirty_popcount(const uint8_t *bm){
    uint32_t n = 0;
    for (unsigned i = 0; i < AETHER_DIRTY_BYTES; ++i)
        n += (uint32_t)__builtin_popcount(bm[i]);
    return n;
}

/* ---------- top-K insertion ---------- */

void ae_topk_insert_event(int *dists, aether_event_t *evs, int k,
                          int dist, const aether_event_t *ev){
    if (k <= 0) return;
    /* If new dist not better than worst, skip. */
    if (dist >= dists[k - 1]) return;
    /* Find insertion point. */
    int pos = k - 1;
    while (pos > 0 && dists[pos - 1] > dist) pos--;
    /* Shift down. */
    for (int i = k - 1; i > pos; --i){
        dists[i] = dists[i - 1];
        evs[i]   = evs[i - 1];
    }
    dists[pos] = dist;
    evs[pos]   = *ev;
}

/* ---------- dirty-bitmap persistence ---------- */

static int load_dirty(aether_handle_t *h){
    char path[512];
    if (ae_path_join(path, sizeof(path), h->storage_dir, "dirty.dat") != 0) return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0){
        memset(h->dirty, 0, AETHER_DIRTY_BYTES);
        h->dirty_count = 0;
        return 0; /* fresh store */
    }
    ssize_t r = read(fd, h->dirty, AETHER_DIRTY_BYTES);
    close(fd);
    if (r != AETHER_DIRTY_BYTES){
        memset(h->dirty, 0, AETHER_DIRTY_BYTES);
    }
    h->dirty_count = ae_dirty_popcount(h->dirty);
    return 0;
}

static int save_dirty(aether_handle_t *h){
    char path[512];
    if (ae_path_join(path, sizeof(path), h->storage_dir, "dirty.dat") != 0) return -1;
    return ae_write_file_atomic(path, h->dirty, AETHER_DIRTY_BYTES);
}

/* ---------- public API ---------- */

aether_handle_t *aether_open(const char *storage_dir){
    if (!storage_dir) return NULL;
    if (ae_mkdir_p(storage_dir) != 0) return NULL;

    aether_handle_t *h = (aether_handle_t *)calloc(1, sizeof(*h));
    if (!h) return NULL;
    snprintf(h->storage_dir, sizeof(h->storage_dir), "%s", storage_dir);

    h->wal_fd  = -1;
    h->zone_fd = -1;

    /* Open and validate WAL — truncates trailing partial records. */
    if (ae_wal_open(h) != 0){
        free(h);
        return NULL;
    }

    /* Resolve / create the active zone file. */
    if (ae_zone_init_active(h) != 0){
        if (h->wal_fd >= 0) close(h->wal_fd);
        free(h);
        return NULL;
    }

    load_dirty(h);
    return h;
}

void aether_close(aether_handle_t *h){
    if (!h) return;
    save_dirty(h);
    if (h->wal_fd  >= 0) close(h->wal_fd);
    if (h->zone_fd >= 0) close(h->zone_fd);
    free(h);
}

int aether_put(aether_handle_t *h, const aether_event_t *ev){
    if (!h || !ev) return -1;
    if (ae_wal_append(h, ev) != 0) return -1;

    /* Mark the bucket dirty for incremental consolidation. */
    uint64_t sig = lsh_simhash((const uint8_t *)ev->inline_text,
                               strnlen(ev->inline_text, AETHER_INLINE_TEXT));
    uint8_t bucket = (uint8_t)(sig >> 56);
    if (!ae_dirty_get(h->dirty, bucket)){
        ae_dirty_set(h->dirty, bucket);
        h->dirty_count++;
    }
    return 0;
}

int aether_should_consolidate(const aether_handle_t *h){
    if (!h) return 0;
    return (h->wal_size_bytes > AETHER_WAL_SOFT_LIMIT) ? 1 : 0;
}

void aether_stats(const aether_handle_t *h, aether_stats_t *out){
    if (!h || !out) return;
    out->wal_event_count    = h->wal_event_count;
    out->wal_bytes          = h->wal_size_bytes;
    out->dirty_bucket_count = h->dirty_count;
    out->current_zone_id    = h->zone_id;
    out->current_zone_size  = h->zone_size_bytes;
}

/* ---------- query ----------
 *
 * Two passes over candidates:
 *   1. Bucket file scan (mmap, all entries in matching bucket)
 *   2. WAL linear scan (all pending events)
 * Both feed the same top-K sorted-by-distance array.  Finally we read
 * the K event payloads from their zone files (or already have them from
 * the WAL pass) and return them in order.
 */
int aether_query_by_hash(aether_handle_t *h,
                         uint64_t query_simhash,
                         aether_event_t *out_results,
                         int max_results,
                         uint32_t max_age_seconds)
{
    if (!h || !out_results || max_results <= 0) return 0;
    if (max_results > AETHER_MAX_K) max_results = AETHER_MAX_K;

    /* Two top-K lists: one of (dist, index_entry) for bucket hits (payload
     * lives on disk), one of (dist, event) for WAL hits (payload in hand).
     * After scanning, we materialise bucket entries to events and merge. */
    int           b_dists[AETHER_MAX_K];
    index_entry_t b_ents[AETHER_MAX_K];
    int           w_dists[AETHER_MAX_K];
    aether_event_t w_evs[AETHER_MAX_K];
    for (int i = 0; i < AETHER_MAX_K; ++i){
        b_dists[i] = 65;  /* worse than max Hamming distance (64) */
        w_dists[i] = 65;
        memset(&b_ents[i], 0, sizeof(b_ents[i]));
        memset(&w_evs[i],  0, sizeof(w_evs[i]));
    }

    uint8_t bucket = (uint8_t)(query_simhash >> 56);
    uint32_t now   = ae_now_unix();

    /* Multi-probe: scan the exact bucket plus all 8 single-bit-flip
     * neighbors of the top-8-bit pattern.  This catches the case where the
     * target's signature is Hamming-near the query's but happens to differ
     * in the top 8 bits — a fundamental limitation of single-band LSH
     * bucketing.  9 mmap/scan calls per query; each bucket is ~600 KB at
     * 10M-event scale, so total cost stays within budget. */
    ae_bucket_scan(h, bucket, query_simhash, now, max_age_seconds,
                   max_results, b_dists, b_ents);
    for (int bit = 0; bit < 8; ++bit){
        uint8_t neighbor = (uint8_t)(bucket ^ (1u << bit));
        ae_bucket_scan(h, neighbor, query_simhash, now, max_age_seconds,
                       max_results, b_dists, b_ents);
    }
    ae_wal_scan_for_query(h, query_simhash, now, max_age_seconds,
                          max_results, w_dists, w_evs);

    /* Merge: keep top max_results across both lists, tracking source. */
    int      m_dists[AETHER_MAX_K];
    int      m_src[AETHER_MAX_K];   /* 0=bucket, 1=wal */
    int      m_src_idx[AETHER_MAX_K];
    for (int i = 0; i < AETHER_MAX_K; ++i){ m_dists[i] = 65; m_src[i] = -1; m_src_idx[i] = -1; }

    /* Insert helper. */
    int K = max_results;
    for (int i = 0; i < K; ++i){
        int d = b_dists[i];
        if (d >= 65) break;
        if (d < m_dists[K - 1]){
            int pos = K - 1;
            while (pos > 0 && m_dists[pos - 1] > d) pos--;
            for (int j = K - 1; j > pos; --j){
                m_dists[j]   = m_dists[j - 1];
                m_src[j]     = m_src[j - 1];
                m_src_idx[j] = m_src_idx[j - 1];
            }
            m_dists[pos]   = d;
            m_src[pos]     = 0;
            m_src_idx[pos] = i;
        }
    }
    for (int i = 0; i < K; ++i){
        int d = w_dists[i];
        if (d >= 65) break;
        if (d < m_dists[K - 1]){
            int pos = K - 1;
            while (pos > 0 && m_dists[pos - 1] > d) pos--;
            for (int j = K - 1; j > pos; --j){
                m_dists[j]   = m_dists[j - 1];
                m_src[j]     = m_src[j - 1];
                m_src_idx[j] = m_src_idx[j - 1];
            }
            m_dists[pos]   = d;
            m_src[pos]     = 1;
            m_src_idx[pos] = i;
        }
    }

    /* Materialise. */
    int written = 0;
    for (int i = 0; i < K; ++i){
        if (m_dists[i] >= 65) break;
        if (m_src[i] == 0){
            const index_entry_t *e = &b_ents[m_src_idx[i]];
            if (ae_zone_read_event(h, e->zone_id, e->event_offset,
                                   &out_results[written]) != 0){
                continue;  /* skip on read failure */
            }
        } else {
            out_results[written] = w_evs[m_src_idx[i]];
        }
        /* Optional age filter — already enforced inside scans, but
         * re-check here in case a zone read returned a stale event. */
        if (max_age_seconds > 0
            && out_results[written].timestamp + max_age_seconds < now){
            continue;
        }
        written++;
    }
    return written;
}

int aether_query_by_text(aether_handle_t *h, const char *text,
                         aether_event_t *out_results, int max_results,
                         uint32_t max_age_seconds)
{
    if (!h || !text) return 0;
    uint64_t sig = lsh_simhash((const uint8_t *)text, strlen(text));
    return aether_query_by_hash(h, sig, out_results, max_results, max_age_seconds);
}
