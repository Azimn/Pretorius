/* actor_index.c — implementation of the actor-tagging sidecar. */
#include "persona.h"            /* PE_EPISODIC_MAX — must precede actor_index.h */
#include "persona_internal.h"   /* pe_path_join / pe_read_file / pe_write_file_atomic */
#include "actor_index.h"

#include <string.h>

void pe_actor_index_init(pe_actor_index_t *idx){
    if (!idx) return;
    memset(idx, 0, sizeof(*idx));
    idx->header.magic       = PE_ACTOR_INDEX_MAGIC;
    idx->header.version     = PE_ACTOR_INDEX_VERSION;
    idx->header.flags       = PE_SIDECAR_F_OPTIONAL;
    idx->header.entry_count = 0;
    idx->header.capacity    = (uint32_t)PE_EPISODIC_MAX;
}

int pe_actor_index_load(pe_actor_index_t *idx, const char *char_dir){
    if (!idx || !char_dir) return -1;
    pe_actor_index_init(idx);

    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "actor_index.bin") != 0)
        return -1;

    pe_actor_index_t tmp;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0) {
        /* Optional sidecar — a missing file is the legacy state and is fine. */
        return 0;
    }

    int v = pe_sidecar_validate(&tmp.header, PE_ACTOR_INDEX_MAGIC,
                                1, PE_ACTOR_INDEX_VERSION);
    if (v != PE_SIDECAR_OK) {
        /* Either too-new (forward incompatible) or corrupt. Either way we
         * keep the safe init state rather than partially adopt the file. */
        return 0;
    }
    *idx = tmp;
    return 0;
}

int pe_actor_index_save(const pe_actor_index_t *idx, const char *char_dir){
    if (!idx || !char_dir) return -1;
    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "actor_index.bin") != 0)
        return -1;
    return pe_write_file_atomic(path, idx, sizeof(*idx));
}

void pe_actor_index_tag(pe_actor_index_t *idx, uint16_t slot, uint32_t actor_id){
    if (!idx || slot >= PE_EPISODIC_MAX) return;
    int was_unset = (idx->actor_id[slot] == 0);
    idx->actor_id[slot] = actor_id;
    if (was_unset && actor_id != 0 && idx->header.entry_count < (uint32_t)PE_EPISODIC_MAX)
        idx->header.entry_count++;
}

uint32_t pe_actor_index_get(const pe_actor_index_t *idx, uint16_t slot){
    if (!idx || slot >= PE_EPISODIC_MAX) return 0;
    return idx->actor_id[slot];
}

void pe_actor_index_clear(pe_actor_index_t *idx, uint16_t slot){
    if (!idx || slot >= PE_EPISODIC_MAX) return;
    if (idx->actor_id[slot] != 0 && idx->header.entry_count > 0)
        idx->header.entry_count--;
    idx->actor_id[slot] = 0;
}

uint32_t pe_actor_index_count(const pe_actor_index_t *idx){
    return idx ? idx->header.entry_count : 0u;
}
