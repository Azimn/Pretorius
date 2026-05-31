/* recall_plasticity.c — V5 Recall-Coupled Plasticity (RCP) impl.
 *
 * See recall_plasticity.h for the architectural reasoning + citations.
 * No malloc, no floats, deterministic, ~70 lines of body code.
 */
#include "recall_plasticity.h"
#include "persona_internal.h"
#include "../schema/schema_state.h"
#include <string.h>

/* Helpers ------------------------------------------------------------- */

static int clamp_i(int v, int lo, int hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

/* The dominant live schema slot informs which recalled memories are
 * "congruent" with the current view of the user.  Returns a sign:
 *   +1 if positive memories (high valence) match current schema
 *   -1 if negative memories (low valence) match current schema
 *    0 if the schema is too uncommitted to bias retrieval. */
static int schema_valence_polarity(const Engine *eng){
    int hostile  = eng->schema.slot[SCHEMA_USER_HOSTILE];
    int trust    = eng->schema.slot[SCHEMA_USER_TRUSTWORTHY];
    int intimate = eng->schema.slot[SCHEMA_USER_INTIMATE];
    int deceptive= eng->schema.slot[SCHEMA_USER_DECEPTIVE];
    int positive = trust + intimate;
    int negative = hostile + deceptive;
    int delta    = positive - negative;
    if (delta >  300) return +1;
    if (delta < -300) return -1;
    return 0;
}

/* Blend one LSH bit between two signatures.  Finds the lowest-index bit
 * on which they differ and toggles it in `b` toward `a`.  This is the
 * offline-replay coupling step — biologically modelled on sharp-wave
 * ripple co-firing strengthening (Pfeiffer 2024 review). */
static void ripple_couple(uint64_t a, uint64_t *b){
    uint64_t diff = a ^ *b;
    if (diff == 0) return;
    /* Lowest set bit of diff = first differing position. */
    uint64_t lsb = diff & (~diff + 1ULL);
    *b ^= lsb;  /* flip that bit in b — now it matches a there */
}

/* Main entrypoint ----------------------------------------------------- */

void pe_recall_plasticity_tick(Engine *eng, const EmotionVector *current_event){
    if (!eng || !current_event) return;
    if (eng->active_count == 0) return;

    int sch_polarity = schema_valence_polarity(eng);

    uint16_t prev_episodic_idx = 0xFFFFu;

    for (uint16_t i = 0; i < eng->active_count; ++i){
        uint16_t idx = eng->active_memories[i];
        /* active_memories[] uses sentinels (>= PE_EPISODIC_MAX) to refer
         * to cold_scratch entries from AETHER.  Skip those — cold-scratch
         * is ephemeral, owned by next-turn reset; mutating it would not
         * survive anyway. */
        if (idx >= PE_EPISODIC_MAX) continue;

        MemoryNode *m = &eng->memory.episodic[idx];
        if (m->core_memory) {
            /* Core memories are foundational autobiography — immovable.
             * Park et al. reflections can summarise them but never edit
             * them.  Same rule applies here. */
            prev_episodic_idx = idx;
            continue;
        }

        /* (1) Reconsolidation (Nader & Hardt 2009; Schiller/Phelps follow-ups).
         * Blend 1/20 of current event affect into the stored emotion.
         * α = 0.05 keeps memories largely stable but drifts them toward
         * the affective context of repeated retrieval — exactly what
         * "extinction-via-reconsolidation" shows in human trauma work. */
        int v_blend = ((int)m->emotion.valence * 19 + current_event->valence) / 20;
        int a_blend = ((int)m->emotion.arousal * 19 + current_event->arousal) / 20;
        int d_blend = ((int)m->emotion.dominance * 19 + current_event->dominance) / 20;
        m->emotion.valence   = (int8_t)clamp_i(v_blend, -100, 100);
        m->emotion.arousal   = (int8_t)clamp_i(a_blend,    0, 100);
        m->emotion.dominance = (int8_t)clamp_i(d_blend, -100, 100);

        /* (2) Schema-congruent survival bump (Bartlett 1932; Gilboa &
         * Marlatte reviews 2024-2026).  Memories whose valence agrees
         * with the dominant live schema get a +1 salience nudge.  Over
         * many recalls this is what produces the well-attested
         * "schema-consistent memory advantage" effect. */
        if (sch_polarity != 0){
            int m_polarity = m->emotion.valence > 20 ? +1
                           : m->emotion.valence < -20 ? -1 : 0;
            if (m_polarity != 0 && m_polarity == sch_polarity && m->salience < 250){
                m->salience += 1;
            }
        }

        /* (3) Replay coupling (Buzsáki SWR; Pfeiffer 2024 review).
         * For each adjacent pair of recalled episodic memories, nudge
         * their LSH signatures one bit toward each other.  After many
         * co-recalls, they become more easily co-recalled.  This is the
         * offline mechanism that builds associative chains — what humans
         * call "one memory leads to another." */
        if (prev_episodic_idx != 0xFFFFu){
            MemoryNode *prev = &eng->memory.episodic[prev_episodic_idx];
            ripple_couple(prev->lsh_sig, &m->lsh_sig);
            ripple_couple(m->lsh_sig,    &prev->lsh_sig);
        }
        prev_episodic_idx = idx;
    }
}
