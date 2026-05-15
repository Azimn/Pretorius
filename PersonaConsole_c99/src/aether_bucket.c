/* aether_bucket.c — bucket file + zone file I/O.
 *
 * Bucket file layout (on disk):
 *   [bucket_header_t (16 B)] [index_entry_t × entry_count]
 *
 * Each entry is 16 bytes; the file is sorted by signature so we *could* do
 * binary search, but for similarity (Hamming-rank) we must scan all entries
 * in the bucket.  At 10M events across 256 buckets, each bucket holds ~39k
 * entries = ~624 KB — comfortably mmap'd one at a time.
 *
 * Zone file layout (on disk):
 *   [aether_event_t][aether_event_t]...  (raw 64-byte events appended)
 * Rotated when size exceeds AETHER_ZONE_MAX_BYTES.
 */
#include "aether.h"
#include "aether_internal.h"
#include "lsh_memory.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---------- bucket path helper ----------
 * Multi-band layout: bucket files live in band-specific subdirectories
 * (band0/, band1/, ...) so each band has its own 256-file namespace. */
static int bucket_path(const aether_handle_t *h, int band, uint8_t bid,
                       char *out, size_t n){
    char name[40];
    snprintf(name, sizeof(name), "band%d/bucket_%03u.dat",
             band, (unsigned)bid);
    return ae_path_join(out, n, h->storage_dir, name);
}

int ae_ensure_band_dirs(const aether_handle_t *h){
    char path[512];
    for (unsigned b = 0; b < AETHER_BAND_COUNT; ++b){
        char sub[32];
        snprintf(sub, sizeof(sub), "band%u", b);
        if (ae_path_join(path, sizeof(path), h->storage_dir, sub) != 0) return -1;
        if (ae_mkdir_p(path) != 0) return -1;
    }
    return 0;
}

static int zone_path(const aether_handle_t *h, uint16_t zid, char *out, size_t n){
    char name[32];
    snprintf(name, sizeof(name), "zone_%05u.dat", (unsigned)zid);
    return ae_path_join(out, n, h->storage_dir, name);
}

/* ---------- bucket scan (read-only, mmap'd) ---------- */

int ae_bucket_scan(aether_handle_t *h, int band, uint8_t bucket_id,
                   uint64_t qsig, uint32_t now, uint32_t max_age,
                   int max_k,
                   int *dists_inout,
                   index_entry_t *entries_inout)
{
    (void)now; (void)max_age;  /* age filter happens at zone read time */

    char path[512];
    if (bucket_path(h, band, bucket_id, path, sizeof(path)) != 0) return 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;  /* bucket doesn't exist yet */

    struct stat st;
    if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(bucket_header_t)){
        close(fd);
        return 0;
    }

    void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return 0;

    const bucket_header_t *hdr = (const bucket_header_t *)m;
    if (hdr->magic != AETHER_BUCKET_MAGIC || hdr->version != AETHER_BUCKET_VERSION){
        munmap(m, (size_t)st.st_size);
        return 0;
    }

    size_t expected = sizeof(bucket_header_t)
                    + (size_t)hdr->entry_count * sizeof(index_entry_t);
    if (expected > (size_t)st.st_size){
        /* corrupt — entry_count claims more data than the file holds */
        munmap(m, (size_t)st.st_size);
        return 0;
    }

    const index_entry_t *entries =
        (const index_entry_t *)((const char *)m + sizeof(bucket_header_t));

    for (uint32_t i = 0; i < hdr->entry_count; ++i){
        int d = lsh_hamming_distance(entries[i].signature, qsig);
        if (d >= dists_inout[max_k - 1]) continue;
        /* Multi-band dedup: the same event is indexed in AETHER_BAND_COUNT
         * buckets and could surface multiple times in successive ae_bucket_scan
         * calls.  Skip if its signature is already in the top-K. */
        int dup = 0;
        for (int k = 0; k < max_k; ++k){
            if (dists_inout[k] >= 65) break;
            if (entries_inout[k].signature == entries[i].signature){ dup = 1; break; }
        }
        if (dup) continue;
        /* Insertion sort into top-K parallel arrays. */
        int pos = max_k - 1;
        while (pos > 0 && dists_inout[pos - 1] > d) pos--;
        for (int j = max_k - 1; j > pos; --j){
            dists_inout[j]   = dists_inout[j - 1];
            entries_inout[j] = entries_inout[j - 1];
        }
        dists_inout[pos]   = d;
        entries_inout[pos] = entries[i];
    }

    munmap(m, (size_t)st.st_size);
    return 0;
}

/* ---------- bucket read-all (for consolidation merge) ---------- */

int ae_bucket_read_all(aether_handle_t *h, int band, uint8_t bucket_id,
                       index_entry_t *out, uint32_t cap, uint32_t *out_count)
{
    *out_count = 0;
    char path[512];
    if (bucket_path(h, band, bucket_id, path, sizeof(path)) != 0) return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;  /* OK — fresh bucket */

    bucket_header_t hdr;
    ssize_t r = pread(fd, &hdr, sizeof(hdr), 0);
    if (r != (ssize_t)sizeof(hdr)
        || hdr.magic != AETHER_BUCKET_MAGIC
        || hdr.version != AETHER_BUCKET_VERSION){
        close(fd);
        return 0;
    }
    uint32_t n = hdr.entry_count;
    if (n > cap) n = cap;

    if (n > 0){
        ssize_t want = (ssize_t)(n * sizeof(index_entry_t));
        ssize_t got  = pread(fd, out, (size_t)want, (off_t)sizeof(hdr));
        if (got != want){
            close(fd);
            return -1;
        }
    }
    close(fd);
    *out_count = n;
    return 0;
}

/* ---------- bucket write (atomic) ---------- */

int ae_bucket_write_atomic(aether_handle_t *h, int band, uint8_t bucket_id,
                           const index_entry_t *entries, uint32_t count)
{
    char path[512];
    if (bucket_path(h, band, bucket_id, path, sizeof(path)) != 0) return -1;

    /* Build a single buffer: header + entries.  Cap at 1M entries per
     * bucket (16 MB) — well above our 10M / 256 ≈ 39k target. */
    if (count > 1000000u) return -1;

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    int fd = open(tmp, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    bucket_header_t hdr;
    hdr.magic        = AETHER_BUCKET_MAGIC;
    hdr.version      = AETHER_BUCKET_VERSION;
    hdr.entry_count  = count;
    hdr.last_rebuild = ae_now_unix();

    if (write(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)){
        close(fd); unlink(tmp); return -1;
    }
    if (count > 0){
        size_t bytes = count * sizeof(index_entry_t);
        const char *p = (const char *)entries;
        size_t written = 0;
        while (written < bytes){
            ssize_t w = write(fd, p + written, bytes - written);
            if (w <= 0){ close(fd); unlink(tmp); return -1; }
            written += (size_t)w;
        }
    }
    fdatasync(fd);
    close(fd);
    if (rename(tmp, path) != 0){ unlink(tmp); return -1; }
    return 0;
}

/* ---------- zone init / append / read ---------- */

int ae_zone_init_active(aether_handle_t *h){
    /* Find the highest-numbered zone file in the storage dir; if its
     * size is below the cap, append to it.  Otherwise create the next. */
    uint16_t best = 0;
    int      found = 0;
    char path[512];
    struct stat st;

    /* Linear probe — at this scale we have <100 zone files even for 10M
     * events (256 MB × ~3 = 800 MB).  A directory listing would also work. */
    for (uint16_t z = 0; z < 200; ++z){
        if (zone_path(h, z, path, sizeof(path)) != 0) continue;
        if (stat(path, &st) == 0){
            best  = z;
            found = 1;
        } else {
            break;
        }
    }

    if (!found){
        h->zone_id = 0;
        if (zone_path(h, 0, path, sizeof(path)) != 0) return -1;
    } else {
        h->zone_id = best;
        if (zone_path(h, best, path, sizeof(path)) != 0) return -1;
        if (st.st_size >= (off_t)AETHER_ZONE_MAX_BYTES){
            h->zone_id = (uint16_t)(best + 1);
            if (zone_path(h, h->zone_id, path, sizeof(path)) != 0) return -1;
        }
    }
    h->zone_fd = open(path, O_RDWR | O_CREAT, 0644);
    if (h->zone_fd < 0) return -1;
    if (fstat(h->zone_fd, &st) != 0) return -1;
    h->zone_size_bytes = (uint32_t)st.st_size;
    return 0;
}

int ae_zone_append(aether_handle_t *h, const aether_event_t *ev,
                   uint16_t *out_zone_id, uint32_t *out_offset)
{
    if (h->zone_fd < 0) return -1;
    /* Rotate if active zone full. */
    if (h->zone_size_bytes + sizeof(*ev) > AETHER_ZONE_MAX_BYTES){
        close(h->zone_fd);
        h->zone_id += 1;
        char path[512];
        if (zone_path(h, h->zone_id, path, sizeof(path)) != 0) return -1;
        h->zone_fd = open(path, O_RDWR | O_CREAT, 0644);
        if (h->zone_fd < 0) return -1;
        h->zone_size_bytes = 0;
    }
    uint32_t off = h->zone_size_bytes;
    ssize_t w = pwrite(h->zone_fd, ev, sizeof(*ev), (off_t)off);
    if (w != (ssize_t)sizeof(*ev)) return -1;
    h->zone_size_bytes += (uint32_t)sizeof(*ev);
    *out_zone_id = h->zone_id;
    *out_offset  = off;
    return 0;
}

int ae_zone_read_event(aether_handle_t *h, uint16_t zone_id, uint32_t offset,
                       aether_event_t *out)
{
    /* If reading from the active zone, use pread on the open fd. */
    if (zone_id == h->zone_id && h->zone_fd >= 0){
        ssize_t r = pread(h->zone_fd, out, sizeof(*out), (off_t)offset);
        return (r == (ssize_t)sizeof(*out)) ? 0 : -1;
    }
    /* Otherwise: mmap an 8 KB aligned window — kernel page-fetch pattern
     * from the spec.  Cheap because the kernel caches pages.  */
    char path[512];
    if (zone_path(h, zone_id, path, sizeof(path)) != 0) return -1;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    off_t aligned = (off_t)offset & ~((off_t)4095);
    size_t map_len = 8192;
    /* Don't over-map past EOF. */
    struct stat st;
    if (fstat(fd, &st) == 0 && aligned + (off_t)map_len > st.st_size){
        map_len = (size_t)(st.st_size - aligned);
    }
    void *m = mmap(NULL, map_len, PROT_READ, MAP_PRIVATE, fd, aligned);
    close(fd);
    if (m == MAP_FAILED) return -1;

    size_t inside = (size_t)offset - (size_t)aligned;
    if (inside + sizeof(*out) > map_len){
        munmap(m, map_len);
        return -1;
    }
    memcpy(out, (const char *)m + inside, sizeof(*out));
    munmap(m, map_len);
    return 0;
}
