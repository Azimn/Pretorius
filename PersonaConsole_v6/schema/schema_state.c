/* schema_state.c — V4 compressed identity interpretations (impl).
 *
 * Schemas are derived from symbolic events.  External text never
 * reaches this code directly — only enum-coded events from the
 * canonical update path.
 */
#include "schema_state.h"
#include "../memory/affect_curve.h"
#include <stdio.h>
#include <string.h>

#define SCHEMA_VERSION 1

void schema_state_init(SchemaState *s){
    memset(s, 0, sizeof(*s));
    s->version = SCHEMA_VERSION;
}

/* Map a SchemaEvent onto (slot, sign, base_magnitude).  This is the
 * canonical interpretation of "what does this event MEAN" — separate
 * from the raw text that triggered it, separate from any rendering. */
typedef struct {
    SchemaSlot slot;
    int8_t     sign;       /* +1 or -1 */
    int16_t    base;       /* base magnitude before habituation */
} SchemaEvtEffect;

static const SchemaEvtEffect EVT_TABLE[SCHEMA_EVT_COUNT] = {
    [SCHEMA_EVT_PRAISED_US]     = { SCHEMA_USER_TRUSTWORTHY,  +1, 60 },
    [SCHEMA_EVT_INSULTED_US]    = { SCHEMA_USER_HOSTILE,      +1, 90 },
    [SCHEMA_EVT_THREATENED_US]  = { SCHEMA_USER_HOSTILE,      +1, 180 },
    [SCHEMA_EVT_CONFIDED_IN_US] = { SCHEMA_USER_INTIMATE,     +1, 80 },
    [SCHEMA_EVT_LIED]           = { SCHEMA_USER_DECEPTIVE,    +1, 140 },
    [SCHEMA_EVT_KEPT_PROMISE]   = { SCHEMA_USER_TRUSTWORTHY,  +1, 120 },
    [SCHEMA_EVT_BROKE_PROMISE]  = { SCHEMA_USER_TRUSTWORTHY,  -1, 160 },
    [SCHEMA_EVT_HELPED_US]      = { SCHEMA_RELATIONSHIP_OWED, +1, 90 },
    [SCHEMA_EVT_HURT_US]        = { SCHEMA_SELF_DIGNITY,      -1, 110 },
    [SCHEMA_EVT_APOLOGIZED]     = { SCHEMA_USER_HOSTILE,      -1, 80 },
};

void schema_apply_event(SchemaState *s, SchemaEvent evt, int magnitude,
                        const int16_t *trait_amplifiers){
    if (!s) return;
    if (evt < 0 || evt >= SCHEMA_EVT_COUNT) return;
    const SchemaEvtEffect *e = &EVT_TABLE[evt];

    /* habituation: count consecutive hits of this event vs last */
    int gap = (int)s->turns_since_event[evt];
    int hits = (gap == 0) ? 1 : 0;  /* coarse — refined when integrated with engine */
    int eff_mag = affect_habituate(magnitude, hits, gap);

    /* trait amplifier */
    int amp = 1000;
    if (trait_amplifiers){
        TraitVec t = {
            (int8_t)trait_amplifiers[0], (int8_t)trait_amplifiers[1],
            (int8_t)trait_amplifiers[2], (int8_t)trait_amplifiers[3],
            (int8_t)trait_amplifiers[4],
        };
        amp = affect_trait_amplifier(e->slot, &t);
    }

    /* delta in −1000..+1000 domain.
     * eff_mag is 0..255; scale up: ×8 → 0..2040; clamp to 1000. */
    int delta = (e->sign * eff_mag * 8 * amp) / 1000;
    int16_t prev = s->slot[e->slot];
    s->slot[e->slot] = affect_hysteresis_apply(prev, delta);
    if (s->evidence[e->slot] < 32000) s->evidence[e->slot] += 1;
    s->turns_since_event[evt] = 0;
    s->last_update_turn += 1;
}

void schema_tick(SchemaState *s){
    if (!s) return;
    for (int i = 0; i < SCHEMA_SLOT_COUNT; ++i){
        /* salience proxy: evidence count, capped at 1000. */
        int sal = s->evidence[i] > 1000 ? 1000 : s->evidence[i];
        int base_rate = (i == SCHEMA_SELF_DIGNITY) ? 25 : 8;  /* per-mille per tick */
        s->slot[i] = affect_decay(s->slot[i], (int16_t)sal, base_rate);
    }
    for (int i = 0; i < SCHEMA_EVT_COUNT; ++i){
        if (s->turns_since_event[i] < 0xFFFFFFFFu) s->turns_since_event[i] += 1;
    }
    s->last_update_turn += 1;
}

void schema_tick_many(SchemaState *s, uint32_t ticks){
    if (!s || ticks == 0) return;
    for (int i = 0; i < SCHEMA_SLOT_COUNT; ++i){
        int sal = s->evidence[i] > 1000 ? 1000 : s->evidence[i];
        int base_rate = (i == SCHEMA_SELF_DIGNITY) ? 25 : 8;
        s->slot[i] = affect_decay_steps(s->slot[i], (int16_t)sal,
                                        base_rate, ticks);
    }
    for (int i = 0; i < SCHEMA_EVT_COUNT; ++i){
        uint64_t next = (uint64_t)s->turns_since_event[i] + ticks;
        s->turns_since_event[i] = next > 0xFFFFFFFFu
                                ? 0xFFFFFFFFu : (uint32_t)next;
    }
    {
        uint64_t next = (uint64_t)s->last_update_turn + ticks;
        s->last_update_turn = next > 0xFFFFFFFFu
                            ? 0xFFFFFFFFu : (uint32_t)next;
    }
}

int schema_get(const SchemaState *s, SchemaSlot slot){
    if (!s) return 0;
    if (slot < 0 || slot >= SCHEMA_SLOT_COUNT) return 0;
    return s->slot[slot];
}

int schema_evidence(const SchemaState *s, SchemaSlot slot){
    if (!s) return 0;
    if (slot < 0 || slot >= SCHEMA_SLOT_COUNT) return 0;
    return s->evidence[slot];
}

static const char *slot_name(SchemaSlot slot){
    switch (slot){
    case SCHEMA_USER_TRUSTWORTHY:    return "trustworthy";
    case SCHEMA_USER_HOSTILE:        return "hostile";
    case SCHEMA_USER_INTIMATE:       return "intimate";
    case SCHEMA_USER_COMPETENT:      return "competent";
    case SCHEMA_USER_DECEPTIVE:      return "deceptive";
    case SCHEMA_RELATIONSHIP_OWED:   return "owed_to_us";
    case SCHEMA_RELATIONSHIP_OWES:   return "owes_to_them";
    case SCHEMA_SELF_DIGNITY:        return "self_dignity";
    default:                          return "?";
    }
}

int schema_format(const SchemaState *s, char *buf, int cap){
    if (!s || !buf || cap < 32) return 0;
    int n = 0;
    n += snprintf(buf + n, cap - n, "schema:");
    for (int i = 0; i < SCHEMA_SLOT_COUNT && n < cap - 24; ++i){
        if (s->slot[i] == 0 && s->evidence[i] == 0) continue;
        n += snprintf(buf + n, cap - n, " %s=%d/%d",
                      slot_name((SchemaSlot)i),
                      (int)s->slot[i], (int)s->evidence[i]);
    }
    return n;
}
