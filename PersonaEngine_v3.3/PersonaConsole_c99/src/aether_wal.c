/* aether_wal.c — Write-Ahead Log for libaether.
 *
 * Format: sequence of (uint32_t magic, aether_event_t event) records — 68
 * bytes each.  Single pwrite per record is atomic on POSIX (68 << PIPE_BUF).
 *
 * On open we sequentially validate the magic of each record from offset 0;
 * the first invalid magic marks the safe-truncation point (recovery from
 * a power loss between writes).
 *
 * On query we linearly scan the WAL for matches (it's bounded at ~1 MB,
 * so ~16k events, ~16k Hamming-distance calls — fast on PIII).
 */
#include "aether.h"
#include "aether_internal.h"
#include "lsh_memory.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

int ae_wal_open(aether_handle_t *h){
    char path[512];
    if (ae_path_join(path, sizeof(path), h->storage_dir, "wal.dat") != 0) return -1;

    h->wal_fd = open(path, O_RDWR | O_CREAT, 0644);
    if (h->wal_fd < 0) return -1;

    /* Scan from offset 0, validate magics, find last good record. */
    off_t pos = 0;
    uint32_t count = 0;
    wal_record_t rec;
    while (1){
        ssize_t r = pread(h->wal_fd, &rec, sizeof(rec), pos);
        if (r == 0) break;                          /* clean EOF */
        if (r != (ssize_t)sizeof(rec)) break;       /* short read = partial */
        if (rec.magic != AETHER_WAL_RECORD_MAGIC) break;
        pos += sizeof(rec);
        count++;
    }
    /* Truncate any trailing junk. */
    if (ftruncate(h->wal_fd, pos) != 0){
        close(h->wal_fd);
        h->wal_fd = -1;
        return -1;
    }
    h->wal_size_bytes  = (uint32_t)pos;
    h->wal_event_count = count;
    return 0;
}

int ae_wal_append(aether_handle_t *h, const aether_event_t *ev){
    if (h->wal_fd < 0) return -1;
    wal_record_t rec;
    rec.magic = AETHER_WAL_RECORD_MAGIC;
    rec.event = *ev;
    ssize_t w = pwrite(h->wal_fd, &rec, sizeof(rec), (off_t)h->wal_size_bytes);
    if (w != (ssize_t)sizeof(rec)) return -1;
    h->wal_size_bytes  += (uint32_t)sizeof(rec);
    h->wal_event_count += 1;
    return 0;
}

int ae_wal_scan_for_query(aether_handle_t *h,
                          uint64_t qsig, uint32_t now, uint32_t max_age,
                          int max_k,
                          int *dists_inout,
                          aether_event_t *evs_inout)
{
    if (h->wal_fd < 0 || h->wal_size_bytes == 0) return 0;
    off_t pos = 0;
    wal_record_t rec;
    while (pos + (off_t)sizeof(rec) <= (off_t)h->wal_size_bytes){
        ssize_t r = pread(h->wal_fd, &rec, sizeof(rec), pos);
        if (r != (ssize_t)sizeof(rec)) break;
        pos += sizeof(rec);
        if (rec.magic != AETHER_WAL_RECORD_MAGIC) break;
        if (max_age > 0 && rec.event.timestamp + max_age < now) continue;

        uint64_t sig = lsh_simhash((const uint8_t *)rec.event.inline_text,
                                   strnlen(rec.event.inline_text, AETHER_INLINE_TEXT));
        int d = lsh_hamming_distance(sig, qsig);
        ae_topk_insert_event(dists_inout, evs_inout, max_k, d, &rec.event);
    }
    return 0;
}

int ae_wal_drain(aether_handle_t *h,
                 aether_event_t *out, uint32_t cap, uint32_t *out_count)
{
    *out_count = 0;
    if (h->wal_fd < 0 || h->wal_size_bytes == 0) return 0;
    off_t pos = 0;
    wal_record_t rec;
    while (*out_count < cap
           && pos + (off_t)sizeof(rec) <= (off_t)h->wal_size_bytes){
        ssize_t r = pread(h->wal_fd, &rec, sizeof(rec), pos);
        if (r != (ssize_t)sizeof(rec)) break;
        pos += sizeof(rec);
        if (rec.magic != AETHER_WAL_RECORD_MAGIC) break;
        out[(*out_count)++] = rec.event;
    }
    return 0;
}

int ae_wal_truncate(aether_handle_t *h){
    if (h->wal_fd < 0) return -1;
    if (ftruncate(h->wal_fd, 0) != 0) return -1;
    h->wal_size_bytes  = 0;
    h->wal_event_count = 0;
    return 0;
}
