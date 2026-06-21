/* long_arc_drift.c -- slow earned baseline drift. */
#include "long_arc_drift.h"
#include "persona_internal.h"
#include <string.h>
#include <stdlib.h>

static int clamp_i(int v, int lo, int hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

static int sign_i(int v){
    return (v > 0) - (v < 0);
}

void pe_long_arc_drift_init(pe_long_arc_drift_t *d){
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->header.magic       = PE_LONG_ARC_DRIFT_MAGIC;
    d->header.version     = PE_LONG_ARC_DRIFT_VERSION;
    d->header.flags       = PE_SIDECAR_F_OPTIONAL;
    d->header.entry_count = 1;
    d->header.capacity    = 1;
}

int pe_long_arc_drift_load(pe_long_arc_drift_t *d, const char *char_dir){
    char path[512];
    pe_long_arc_drift_t tmp;
    int v;
    if (!d || !char_dir) return -1;
    pe_long_arc_drift_init(d);
    if (pe_path_join(path, sizeof(path), char_dir, "long_arc_drift.bin") != 0)
        return -1;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0) return 0;
    v = pe_sidecar_validate(&tmp.header, PE_LONG_ARC_DRIFT_MAGIC,
                            1, PE_LONG_ARC_DRIFT_VERSION);
    if (v != PE_SIDECAR_OK) return 0;
    *d = tmp;
    return 0;
}

int pe_long_arc_drift_save(const pe_long_arc_drift_t *d, const char *char_dir){
    char path[512];
    if (!d || !char_dir) return -1;
    if (pe_path_join(path, sizeof(path), char_dir, "long_arc_drift.bin") != 0)
        return -1;
    return pe_write_file_atomic(path, d, sizeof(*d));
}

void pe_long_arc_drift_apply_history(pe_long_arc_drift_t *d,
                                     const pe_dissonance_t *self,
                                     const pe_relation_dims_t *rel,
                                     uint16_t drift_malleability,
                                     uint32_t evidence_days){
    int positive, negative, target, cap, months, new_months, step, delta;
    uint32_t *applied_months;
    if (!d || !self || !rel || evidence_days == 0) return;
    if (drift_malleability == 0) return;

    positive = (rel->trust >= 700 && rel->resentment <= 180 &&
                rel->threat <= 560);
    negative = ((rel->trust <= 330 || rel->threat >= 720) &&
                rel->resentment >= 420);
    if (!positive && !negative) return;

    if (positive){
        d->positive_evidence_days += evidence_days;
        if (d->negative_evidence_days > evidence_days)
            d->negative_evidence_days -= evidence_days;
        else
            d->negative_evidence_days = 0;
        d->last_direction = 1;
    } else {
        d->negative_evidence_days += evidence_days;
        if (d->positive_evidence_days > evidence_days)
            d->positive_evidence_days -= evidence_days;
        else
            d->positive_evidence_days = 0;
        d->last_direction = -1;
    }

    months = (int)((positive ? d->positive_evidence_days
                             : d->negative_evidence_days) / 30u);
    applied_months = positive ? &d->positive_applied_months
                              : &d->negative_applied_months;
    new_months = months - (int)*applied_months;
    if (new_months <= 0) return;
    *applied_months = (uint32_t)months;

    cap = 20 + (int)drift_malleability / 4; /* 0..270-ish */
    if (cap > 320) cap = 320;
    target = positive ? self->ideal_self_model : self->feared_self_model;
    if (target == 0) target = positive ? 160 : -160;
    target = clamp_i(target, -1000, 1000);
    target = (target * cap) / 1000;

    /* One point per earned month at low malleability, up to several points
     * per month for highly malleable characters. This is intentionally much
     * slower than mood/schema decay. */
    step = (int)(((uint32_t)drift_malleability * (uint32_t)new_months) / 220u);
    if (step < 1) step = 1;
    if (step > 12) step = 12;
    delta = target - d->baseline_offset;
    if (delta != 0){
        int move = sign_i(delta) * (step < abs(delta) ? step : abs(delta));
        d->baseline_offset = (int16_t)clamp_i((int)d->baseline_offset + move,
                                             -cap, cap);
    }
}
