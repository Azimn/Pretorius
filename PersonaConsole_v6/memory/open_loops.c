/* open_loops.c -- V6 Phase 6 carried-intention sidecar. */
#include "persona_internal.h"
#include "open_loops.h"

#include <string.h>

void pe_open_loops_init(pe_open_loops_t *ol){
    if (!ol) return;
    memset(ol, 0, sizeof(*ol));
    ol->header.magic       = PE_OPEN_LOOPS_MAGIC;
    ol->header.version     = PE_OPEN_LOOPS_VERSION;
    ol->header.flags       = PE_SIDECAR_F_OPTIONAL;
    ol->header.entry_count = 0;
    ol->header.capacity    = PE_OPEN_LOOPS_RING_SIZE;
    ol->next_id            = 1;
}

int pe_open_loops_load(pe_open_loops_t *ol, const char *char_dir){
    if (!ol || !char_dir) return -1;
    pe_open_loops_init(ol);

    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "open_loops.bin") != 0)
        return -1;

    pe_open_loops_t tmp;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0)
        return 0;

    int v = pe_sidecar_validate(&tmp.header, PE_OPEN_LOOPS_MAGIC,
                                1, PE_OPEN_LOOPS_VERSION);
    if (v != PE_SIDECAR_OK) return 0;
    if (tmp.head >= PE_OPEN_LOOPS_RING_SIZE) tmp.head = 0;
    if (tmp.next_id == 0) tmp.next_id = 1;
    *ol = tmp;
    return 0;
}

int pe_open_loops_save(const pe_open_loops_t *ol, const char *char_dir){
    if (!ol || !char_dir) return -1;
    char path[512];
    if (pe_path_join(path, sizeof(path), char_dir, "open_loops.bin") != 0)
        return -1;
    return pe_write_file_atomic(path, ol, sizeof(*ol));
}

static uint16_t clamp1000(uint16_t v){
    return v > 1000u ? 1000u : v;
}

void pe_open_loops_record(pe_open_loops_t *ol,
                          uint32_t actor_id,
                          uint16_t topic_id,
                          uint16_t desired_speech_act,
                          uint16_t urgency,
                          uint16_t shame_cost,
                          uint16_t avoidance_pressure,
                          uint32_t created_turn,
                          uint32_t expires_turn){
    if (!ol || topic_id == 0xFFFFu) return;
    if (ol->header.magic != PE_OPEN_LOOPS_MAGIC) pe_open_loops_init(ol);

    uint32_t slot = ol->head % PE_OPEN_LOOPS_RING_SIZE;
    pe_open_loop_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.id                 = ol->next_id++;
    if (ol->next_id == 0) ol->next_id = 1;
    rec.target_actor_id    = actor_id;
    rec.created_turn       = created_turn;
    rec.expires_turn       = expires_turn;
    rec.target_topic_id    = topic_id;
    rec.desired_speech_act = desired_speech_act;
    rec.urgency            = clamp1000(urgency);
    rec.shame_cost         = clamp1000(shame_cost);
    rec.avoidance_pressure = clamp1000(avoidance_pressure);
    rec.status             = PE_OL_ACTIVE;

    ol->loops[slot] = rec;
    ol->head = (ol->head + 1u) % PE_OPEN_LOOPS_RING_SIZE;
    if (ol->total_recorded < 0xFFFFFFFFu) ol->total_recorded++;
    if (ol->header.entry_count < PE_OPEN_LOOPS_RING_SIZE)
        ol->header.entry_count++;
}

uint32_t pe_open_loops_count(const pe_open_loops_t *ol){
    return pe_open_loops_count_status(ol, PE_OL_ACTIVE);
}

uint32_t pe_open_loops_count_status(const pe_open_loops_t *ol, uint8_t status){
    if (!ol || ol->header.magic != PE_OPEN_LOOPS_MAGIC) return 0;
    uint32_t n = 0;
    uint32_t count = ol->header.entry_count;
    if (count > PE_OPEN_LOOPS_RING_SIZE) count = PE_OPEN_LOOPS_RING_SIZE;
    for (uint32_t i = 0; i < count; ++i)
        if (ol->loops[i].status == status) n++;
    return n;
}

const pe_open_loop_t *pe_open_loops_latest_active(const pe_open_loops_t *ol){
    if (!ol || ol->header.magic != PE_OPEN_LOOPS_MAGIC
        || ol->header.entry_count == 0) return NULL;
    uint32_t count = ol->header.entry_count;
    if (count > PE_OPEN_LOOPS_RING_SIZE) count = PE_OPEN_LOOPS_RING_SIZE;
    for (uint32_t step = 0; step < count; ++step){
        uint32_t idx = (ol->head + PE_OPEN_LOOPS_RING_SIZE - 1u - step)
                     % PE_OPEN_LOOPS_RING_SIZE;
        if (ol->loops[idx].status == PE_OL_ACTIVE)
            return &ol->loops[idx];
    }
    return NULL;
}

uint32_t pe_open_loops_expire_to(pe_open_loops_t *ol, uint32_t turn_count){
    if (!ol || ol->header.magic != PE_OPEN_LOOPS_MAGIC) return 0;
    uint32_t changed = 0;
    uint32_t count = ol->header.entry_count;
    if (count > PE_OPEN_LOOPS_RING_SIZE) count = PE_OPEN_LOOPS_RING_SIZE;
    for (uint32_t i = 0; i < count; ++i){
        pe_open_loop_t *loop = &ol->loops[i];
        if (loop->status != PE_OL_ACTIVE) continue;
        if (loop->expires_turn != 0 && turn_count >= loop->expires_turn){
            loop->status = PE_OL_EXPIRED;
            changed++;
        }
    }
    return changed;
}

uint32_t pe_open_loops_resolve_topic(pe_open_loops_t *ol,
                                     uint32_t actor_id,
                                     uint16_t topic_id,
                                     uint32_t turn_count){
    if (!ol || ol->header.magic != PE_OPEN_LOOPS_MAGIC
        || topic_id == 0xFFFFu) return 0;
    uint32_t changed = 0;
    uint32_t count = ol->header.entry_count;
    if (count > PE_OPEN_LOOPS_RING_SIZE) count = PE_OPEN_LOOPS_RING_SIZE;
    for (uint32_t i = 0; i < count; ++i){
        pe_open_loop_t *loop = &ol->loops[i];
        if (loop->status != PE_OL_ACTIVE) continue;
        if (loop->created_turn >= turn_count) continue;
        if (loop->target_topic_id != topic_id) continue;
        if (loop->target_actor_id != 0 && actor_id != 0
            && loop->target_actor_id != actor_id) continue;
        loop->status = PE_OL_RESOLVED;
        changed++;
    }
    return changed;
}
