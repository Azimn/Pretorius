/* belief_ledger.c -- V7 compact behavioral scars from memory decay. */
#include "belief_ledger.h"
#include "persona.h"
#include "persona_internal.h"

#include <stdio.h>
#include <string.h>

static int clamp_i(int v, int lo, int hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

static int bl_path(const char *char_dir, uint32_t user_hash,
                   char *out, size_t n){
    char rel_dir[256];
    char base[256];
    snprintf(base, sizeof(base), "%.220s", char_dir ? char_dir : "");
    if (pe_path_join(rel_dir, sizeof(rel_dir), base, "relations") != 0) return -1;
    pe_mkdir_p(rel_dir);
    int w = snprintf(out, n, "%s/%08x.blfl", rel_dir, user_hash);
    return (w > 0 && (size_t)w < n) ? 0 : -1;
}

void pe_belief_ledger_init(pe_belief_ledger_t *bl, uint32_t user_hash){
    if (!bl) return;
    memset(bl, 0, sizeof(*bl));
    bl->header.magic       = PE_BELIEF_LEDGER_MAGIC;
    bl->header.version     = PE_BELIEF_LEDGER_VERSION;
    bl->header.flags       = PE_SIDECAR_F_OPTIONAL;
    bl->header.entry_count = PE_BELIEF_COUNT;
    bl->header.capacity    = PE_BELIEF_COUNT;
    bl->user_hash          = user_hash;
}

int pe_belief_ledger_load(pe_belief_ledger_t *bl, const char *char_dir,
                          uint32_t user_hash){
    if (!bl || !char_dir) return -1;
    pe_belief_ledger_init(bl, user_hash);

    char path[512];
    if (bl_path(char_dir, user_hash, path, sizeof(path)) != 0) return -1;

    pe_belief_ledger_t tmp;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0) return 0;
    if (pe_sidecar_validate(&tmp.header, PE_BELIEF_LEDGER_MAGIC,
                            1, PE_BELIEF_LEDGER_VERSION) != PE_SIDECAR_OK)
        return 0;
    if (tmp.user_hash != user_hash) return 0;
    *bl = tmp;
    return 0;
}

int pe_belief_ledger_save(const pe_belief_ledger_t *bl,
                          const char *char_dir){
    if (!bl || !char_dir || bl->user_hash == 0) return 0;
    char path[512];
    if (bl_path(char_dir, bl->user_hash, path, sizeof(path)) != 0) return -1;
    return pe_write_file_atomic(path, bl, sizeof(*bl));
}

void pe_belief_ledger_update(pe_belief_ledger_t *bl,
                             pe_belief_slot_t slot,
                             int delta,
                             uint32_t turn_count){
    if (!bl || slot < 0 || slot >= PE_BELIEF_COUNT) return;
    pe_belief_record_t *r = &bl->slots[slot];
    r->pressure = (int16_t)clamp_i((int)r->pressure + delta, -1000, 1000);
    if (r->evidence_count < 65535u) r->evidence_count++;
    r->last_updated = turn_count;
}

void pe_belief_ledger_decay(pe_belief_ledger_t *bl,
                            uint32_t turns_elapsed){
    if (!bl || turns_elapsed == 0) return;
    uint32_t steps = turns_elapsed / 24u; /* roughly day-scale if caller passes hours */
    if (steps == 0) steps = 1;
    if (steps > 3650u) steps = 3650u;

    for (int i = 0; i < PE_BELIEF_COUNT; ++i){
        pe_belief_record_t *r = &bl->slots[i];
        int p = r->pressure;
        if (p == 0) continue;
        int mag = p < 0 ? -p : p;
        int divisor = (mag >= 500) ? 180 : (mag >= 200 ? 90 : 45);
        int drop = (int)(steps / (uint32_t)divisor);
        if (drop < 1) drop = 1;
        if (r->evidence_count >= 3 && mag > 200 && drop > mag / 8)
            drop = mag / 8;
        if (drop < 1) drop = 1;
        if (p > 0) p = (p > drop) ? p - drop : 0;
        else       p = (-p > drop) ? p + drop : 0;
        r->pressure = (int16_t)p;
    }
}

void pe_belief_absorb_memory_trace(pe_belief_ledger_t *bl,
                                   const MemoryNode *m,
                                   uint32_t turn_count){
    if (!bl || !m) return;
    if (m->core_memory || m->memory_type == MEM_CORE) return;
    if (m->salience < 30u && m->emotion.arousal < 30) return;

    /* The runtime stores compact VAD emotion, not named affect channels.
     * Derive named pressure on the same 0..1000 scale used by the rest of
     * Layer 1. This keeps MemoryNode ABI stable while still letting a few
     * strong repeated events confirm a pattern. */
    int anger = (m->emotion.valence < -40 && m->emotion.dominance >= 0)
              ? -m->emotion.valence * 10 : 0;
    int fear = (m->emotion.valence < -40 && m->emotion.dominance < 0)
             ? (-m->emotion.valence + m->emotion.arousal) * 5 : 0;
    int joy = (m->emotion.valence > 40) ? m->emotion.valence * 10 : 0;
    int sadness = (m->emotion.valence < -50 && m->emotion.arousal < 60)
                ? -m->emotion.valence * 10 : 0;

    if (anger > 40 || fear > 40){
        if (anger > 40)
            pe_belief_ledger_update(bl, PE_BELIEF_DISRESPECT,
                                    anger / 10, turn_count);
        if (fear > 40)
            pe_belief_ledger_update(bl, PE_BELIEF_THREAT_PATTERN,
                                    fear / 15, turn_count);
        return;
    }
    if (joy > 40 && m->salience > 80u){
        pe_belief_ledger_update(bl, PE_BELIEF_INTIMACY_EARNED,
                                m->salience / 20, turn_count);
        pe_belief_ledger_update(bl, PE_BELIEF_SHARED_PROJECT,
                                joy / 20, turn_count);
        return;
    }
    if (m->type == 1){
        pe_belief_ledger_update(bl, PE_BELIEF_DISRESPECT, 15, turn_count);
        pe_belief_ledger_update(bl, PE_BELIEF_SELF_FAILURE, 10, turn_count);
        return;
    }
    if (sadness > 50 && m->salience > 60u){
        pe_belief_ledger_update(bl, PE_BELIEF_ABANDONMENT,
                                sadness / 12, turn_count);
        return;
    }
}

const char *pe_belief_slot_name(uint8_t slot){
    static const char *names[PE_BELIEF_COUNT] = {
        "disrespect",
        "trust",
        "manipulation_guard",
        "abandonment",
        "shared_pull",
        "self_doubt",
        "threat",
        "intimacy"
    };
    return slot < PE_BELIEF_COUNT ? names[slot] : "unknown";
}
