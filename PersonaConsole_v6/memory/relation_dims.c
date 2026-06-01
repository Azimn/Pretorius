/* relation_dims.c — V6 Phase 4 implementation. See relation_dims.h. */
#include "persona.h"
#include "persona_internal.h"
#include "relation_dims.h"

#include <stdio.h>
#include <string.h>

/* Build the per-actor .dims path next to <hash>.bin and <hash>.schema. */
static int dims_path(const char *char_dir, uint32_t user_hash,
                     char *out, size_t n){
    char rel_dir[256];
    char base[256];
    snprintf(base, sizeof(base), "%.220s", char_dir);
    if (pe_path_join(rel_dir, sizeof(rel_dir), base, "relations") != 0) return -1;
    pe_mkdir_p(rel_dir);
    int w = snprintf(out, n, "%s/%08x.dims", rel_dir, user_hash);
    return (w > 0 && (size_t)w < n) ? 0 : -1;
}

void pe_relation_dims_init_from_disposition(pe_relation_dims_t *dims,
                                            uint32_t user_hash,
                                            uint16_t disposition){
    if (!dims) return;
    memset(dims, 0, sizeof(*dims));

    dims->header.magic       = PE_RELATION_DIMS_MAGIC;
    dims->header.version     = PE_RELATION_DIMS_VERSION;
    dims->header.flags       = PE_SIDECAR_F_OPTIONAL;
    dims->header.entry_count = 1;  /* single record per file */
    dims->header.capacity    = 1;
    dims->user_hash          = user_hash;

    /* Default derivation from V5 disposition (0..1000):
     *   trust       ≈ disposition           (positive correlation)
     *   threat      ≈ 1000 - disposition    (inverse)
     *   intimacy    ≈ max(0, disp - 600)*2  (only past confidant threshold)
     *   admiration  ≈ disposition * 0.7     (admiration weaker than trust by default)
     *   others      ≈ 0                     (require event-driven accumulation)
     *
     * Phase 5's planner-level event handlers will move these from
     * here based on speech events, appraisal, and repair. */
    if (disposition > 1000) disposition = 1000;

    dims->trust         = disposition;
    dims->threat        = (uint16_t)(1000u - disposition);
    dims->intimacy      = (disposition > 600u)
                        ? (uint16_t)((disposition - 600u) * 2u)  /* 0..800 */
                        : 0u;
    dims->admiration    = (uint16_t)((uint32_t)disposition * 7u / 10u);
    dims->resentment    = 0;
    dims->dependency    = 0;
    dims->obligation    = 0;
    dims->envy          = 0;
    dims->embarrassment = 0;
}

int pe_relation_dims_load(pe_relation_dims_t *dims,
                          const char *char_dir,
                          uint32_t user_hash,
                          uint16_t disposition){
    if (!dims || !char_dir) return -1;
    /* Default-init in case the file is missing/invalid; we'll overwrite
     * with the persisted values if the load succeeds. */
    pe_relation_dims_init_from_disposition(dims, user_hash, disposition);

    char path[512];
    if (dims_path(char_dir, user_hash, path, sizeof(path)) != 0) return -1;

    pe_relation_dims_t tmp;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0) {
        /* legitimate first-encounter case */
        return 0;
    }
    int v = pe_sidecar_validate(&tmp.header, PE_RELATION_DIMS_MAGIC,
                                1, PE_RELATION_DIMS_VERSION);
    if (v != PE_SIDECAR_OK) {
        /* Corrupt or forward-incompatible: keep the disposition-derived
         * defaults rather than half-adopt a malformed file. */
        return 0;
    }
    /* Defensive: the user_hash in the file MUST match the requested
     * actor; if not, the file is for a different actor — refuse it. */
    if (tmp.user_hash != user_hash) {
        return 0;
    }
    *dims = tmp;
    return 0;
}

int pe_relation_dims_save(const pe_relation_dims_t *dims,
                          const char *char_dir){
    if (!dims || !char_dir) return -1;
    if (dims->user_hash == 0) return 0;  /* no actor known yet */
    char path[512];
    if (dims_path(char_dir, dims->user_hash, path, sizeof(path)) != 0) return -1;
    return pe_write_file_atomic(path, dims, sizeof(*dims));
}
