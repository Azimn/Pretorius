/* memory.c — episodic store, salience, associative recall, short-term buffer. */
#include "persona.h"
#include "persona_internal.h"
#include "lsh_memory.h"            /* v3.0: fuzzy semantic recall */
#ifndef PE_DISABLE_AETHER
#include "aether.h"                /* v3.2: long-term episodic storage */
#endif
#include "reflection.h"            /* v5: high-level pattern recall */
#include "engine_clock.h"          /* canonical Layer 1 clock */
#include <string.h>
#include <stdlib.h>

#define ACTIVATION_THRESHOLD_PER_MIL 200  /* 0.2 in 0..1000 scale */

/* v3.2: threshold above which working memory is "good enough" to skip the
 * AETHER query.  Set to 900 — only a near-perfect hit (Hamming ≈ 0 + VAD
 * ≈ 0 + high salience) suppresses the cold path.  Anything weaker still
 * queries AETHER and merges results by score, so cold hits can outrank
 * mediocre working-memory hits.  Augment, not replace. */
#define PE_COLD_FALLBACK_MATCH 900

/* v3.2: convert MemoryNode → aether_event_t.  Used when a working-memory
 * node is about to be evicted by pe_commit_memory. */
#ifndef PE_DISABLE_AETHER
static void pe_node_to_aether(const MemoryNode *n, aether_event_t *ev){
    memset(ev, 0, sizeof(*ev));
    ev->timestamp     = pe_clock_now_s();
    ev->last_accessed = ev->timestamp;
    /* PE valence/dominance: int8 -100..100  →  uint16 0..65400 */
    ev->emotion_valence   = (uint16_t)(((int32_t)n->emotion.valence   + 100) * 327);
    ev->emotion_dominance = (uint16_t)(((int32_t)n->emotion.dominance + 100) * 327);
    /* PE arousal: int8 0..100  →  uint16 0..65500 */
    ev->emotion_arousal   = (uint16_t)((int32_t)n->emotion.arousal * 655);
    ev->retrievability_score = (uint16_t)((uint32_t)n->salience * 257u); /* 0..65535 */
    ev->keys[0] = n->topic_id;
    size_t L = strnlen(n->summary, sizeof(n->summary));
    if (L >= AETHER_INLINE_TEXT) L = AETHER_INLINE_TEXT - 1;
    memcpy(ev->inline_text, n->summary, L);
    ev->inline_text[L] = 0;
}

/* v3.2: convert aether_event_t → MemoryNode (cold-promotion path). */
static void pe_aether_to_node(const aether_event_t *ev, MemoryNode *n){
    memset(n, 0, sizeof(*n));
    n->id = 0xFFFFFFFFu;  /* sentinel: not a working-memory node */
    n->salience = (uint8_t)(ev->retrievability_score / 257u);
    n->emotion.valence   = (int8_t)((int32_t)ev->emotion_valence   / 327 - 100);
    n->emotion.dominance = (int8_t)((int32_t)ev->emotion_dominance / 327 - 100);
    n->emotion.arousal   = (int8_t)((int32_t)ev->emotion_arousal   / 655);
    n->timestamp = ev->timestamp * 1000u;
    n->core_memory = 0;
    n->memory_type = MEM_EPISODIC;
    n->retrieval_prob = 255;
    n->decay_counter = 0;
    n->topic_id = ev->keys[0];
    size_t L = strnlen(ev->inline_text, AETHER_INLINE_TEXT);
    if (L >= sizeof(n->summary)) L = sizeof(n->summary) - 1;
    memcpy(n->summary, ev->inline_text, L);
    n->summary[L] = 0;
    n->lsh_sig = lsh_compute_n(n->summary, L);
}
#endif

uint8_t pe_compute_salience(Engine *eng,
                            const EmotionVector *prev,
                            const EmotionVector *cur,
                            int32_t drive_impact_abs,
                            int identity_threat,
                            int repeat_count,
                            int relationship_impact)
{
    (void)eng;
    int32_t emo_delta =
        (prev->valence   > cur->valence   ? prev->valence   - cur->valence   : cur->valence   - prev->valence) +
        (prev->arousal   > cur->arousal   ? prev->arousal   - cur->arousal   : cur->arousal   - prev->arousal) +
        (prev->dominance > cur->dominance ? prev->dominance - cur->dominance : cur->dominance - prev->dominance);

    int32_t s = (emo_delta * 35) / 100
              + (drive_impact_abs * 30) / 800   /* drive_impact normalized: max ~8000 */
              + (identity_threat ? 20 : 0)
              + (repeat_count * 10)
              + (relationship_impact * 5) / 100;
    if (s < 0) s = 0;
    if (s > 255) s = 255;
    return (uint8_t)s;
}

static uint16_t evict_lowest_non_core(MemoryStore *m){
    uint16_t worst = 0;
    int32_t worst_score = 0x7fffffff;
    for (uint16_t i = 0; i < m->episodic_count; ++i){
        if (m->episodic[i].core_memory || m->episodic[i].memory_type == MEM_CORE) continue;
        int32_t score = (int32_t)m->episodic[i].salience - (int32_t)m->episodic[i].decay_counter;
        if (score < worst_score) { worst_score = score; worst = i; }
    }
    return worst;
}

void pe_commit_memory(Engine *eng, const char *summary,
                      const EmotionVector *ev, uint16_t topic_id,
                      uint8_t salience, int identity_threat)
{
    MemoryStore *m = &eng->memory;
    uint16_t slot;
    if (m->episodic_count < PE_EPISODIC_MAX) {
        slot = m->episodic_count++;
    } else {
        slot = evict_lowest_non_core(m);
        if (m->episodic[slot].core_memory || m->episodic[slot].memory_type == MEM_CORE) return; /* nothing to evict */
        /* v3.2: demote the evicted memory into AETHER long-term storage
         * before overwriting.  Working memory becomes hot tier; AETHER
         * is the cold ledger.  Soft-fails if AETHER isn't available. */
#ifndef PE_DISABLE_AETHER
        if (eng->aether){
            aether_event_t ev_out;
            pe_node_to_aether(&m->episodic[slot], &ev_out);
            aether_put(eng->aether, &ev_out);
        }
#endif
    }
    MemoryNode *n = &m->episodic[slot];
    memset(n, 0, sizeof(*n));
    n->id = m->next_memory_id++;
    n->type = identity_threat ? 1 : 0;
    n->salience = salience;
    n->emotion = *ev;
    n->timestamp = persona_now_ms();
    n->topic_id = topic_id;
    n->core_memory = 0;
    n->memory_type = MEM_EPISODIC;
    n->retrieval_prob = 255;
    n->decay_counter = 0;
    if (summary) {
        size_t L = strlen(summary);
        if (L >= sizeof(n->summary)) L = sizeof(n->summary) - 1;
        memcpy(n->summary, summary, L);
        n->summary[L] = 0;
        /* v3.0: SimHash of the summary for fuzzy semantic recall. */
        n->lsh_sig = lsh_compute_n(n->summary, L);
    } else {
        n->lsh_sig = 0;
    }
    /* v3.1: self-disclosure gate — emotionally charged memories require
     * trust before they surface in associative recall.
     * salience 0..120 → threshold 0 (always public)
     * salience 121..255 → threshold (salience-120)*2  (0..270, capped at 255)
     * A threshold of N means disposition + engagement/4 >= N*4 is needed. */
    if (salience > 120) {
        int32_t t = ((int32_t)salience - 120) * 2;
        n->private_threshold = (uint8_t)(t > 255 ? 255 : t);
    } else {
        n->private_threshold = 0;
    }

    /* V5 Phase 2: tag this slot with the actor under whom it was committed.
     * Stored in the parallel sidecar (actor_index.bin), never in the summary
     * text. A zero hash means "no actor known" and is treated as legacy. */
    pe_actor_index_tag(&eng->actor_index, slot, eng->relation.user_hash);
}

void pe_decay_episodic(Engine *eng){
    /* Spec §5.4: non-core salience -1 every 1000 turns unless re-accessed.
     * decay_counter is uint8 (0..255). Saturate, reset, then only decrement
     * salience every 4th saturation — yielding a ~1020-turn cadence.
     * Re-access (associative recall hit) resets the counter; see recall path. */
    MemoryStore *m = &eng->memory;
    for (uint16_t i = 0; i < m->episodic_count; ++i){
        MemoryNode *n = &m->episodic[i];
        if (n->core_memory || n->memory_type == MEM_CORE) {
            n->retrieval_prob = 255;
            continue;
        }
        if (n->retrieval_prob > 0){
            uint8_t drop = (uint8_t)(255u / (uint8_t)(n->emotion.arousal + 1u));
            if (drop == 0) drop = 1;
            n->retrieval_prob = (n->retrieval_prob > drop)
                              ? (uint8_t)(n->retrieval_prob - drop) : 0;
        }
        if (n->decay_counter < 255) {
            n->decay_counter++;
        } else {
            n->decay_counter = 0;
            /* v3.2: arousal-modulated decay.  Spec §5.4 baseline is "drop
             * salience every 4th saturation".  High-arousal memories (the
             * shocks and ecstasies) saturate twice as slowly — every 8th
             * cycle — so vivid events outlive routine ones for ~2× longer.
             * Cheap integer branch; no FPU. */
            uint8_t arousal_abs = (n->emotion.arousal > 0)
                                ? (uint8_t)n->emotion.arousal : 0u;
            uint32_t throttle_mask = (arousal_abs >= 60) ? 7u : 3u;
            if (((n->id ^ eng->state.turn_count) & throttle_mask) == 0){
                if (n->salience > 0) n->salience--;
            }
        }
    }
    /* compact: drop salience-0 non-core */
    uint16_t w = 0;
    for (uint16_t i = 0; i < m->episodic_count; ++i){
        if (m->episodic[i].core_memory || m->episodic[i].memory_type == MEM_CORE || m->episodic[i].salience > 0)
            m->episodic[w++] = m->episodic[i];
    }
    m->episodic_count = w;
}

/* V6 Phase 5d: name lookup for state-JSON exposure. */
static const char *RECALL_MODE_NAMES[PE_RECALL_COUNT] = {
    "accurate","defensive","nostalgic","accusatory",
    "shame_avoidant","intimacy_seeking","obsession_driven","mood_congruent"
};
const char *pe_recall_mode_name(uint8_t m){
    return (m < PE_RECALL_COUNT) ? RECALL_MODE_NAMES[m] : "unknown";
}

/* V6 Phase 5d: pick the recall mode for this turn from canonical state.
 * Per V6_DOCTRINE §16, mode selection itself is canonical and is read by
 * the recall scoring below; it is exposed in state JSON for operator
 * inspection. Selection is a first-match cascade — same shape as
 * Phase 5c's withhold-reason cascade, deterministic over the five-tuple. */
static uint8_t pick_recall_mode(const Engine *eng){
    if (eng->dissonance.feared_gap > 400)      return PE_RECALL_SHAME_AVOIDANT;
    if (eng->relation_dims.threat > 700)       return PE_RECALL_DEFENSIVE;
    if (eng->relation_dims.resentment > 400)   return PE_RECALL_ACCUSATORY;
    if (eng->relation_dims.intimacy > 600)     return PE_RECALL_INTIMACY_SEEKING;
    if (eng->state.obsession_pressure > 600)   return PE_RECALL_OBSESSION_DRIVEN;
    if (eng->state.drive_values[PE_DRIVE_CONTINUITY] > 800
        && eng->state.mood > -150)             return PE_RECALL_NOSTALGIC;
    if (eng->state.mood < -200 || eng->state.mood > 200)
                                                return PE_RECALL_MOOD_CONGRUENT;
    return PE_RECALL_ACCURATE;
}

static int topic_is_obsession(const Engine *eng, uint16_t topic_id){
    if (topic_id == 0xFFFFu || topic_id == 0) return 0;
    for (int i = 0; i < PE_OBSESSION_COUNT; ++i){
        if (!eng->identity.obsessions[i]) break;
        if (eng->identity.obsessions[i] == topic_id) return 1;
    }
    return 0;
}

static void lower_text(char *dst, size_t cap, const char *src){
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    for (; src[i] && i + 1 < cap; ++i){
        unsigned char c = (unsigned char)src[i];
        dst[i] = (char)((c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c);
    }
    dst[i] = 0;
}

static const char *topic_name_for_id(const Engine *eng, uint16_t topic_id){
    if (!eng || topic_id == 0xFFFFu) return "";
    for (uint32_t i = 0; i < eng->topics.count; ++i)
        if (eng->topics.topics[i].id == topic_id)
            return eng->topics.topics[i].name;
    return "";
}

static int learned_knowledge_recall_boost(const Engine *eng, const MemoryNode *m){
    char summary[PE_MEM_SUMMARY_LEN];
    char topic_name[PE_TOPIC_NAME];
    if (!eng || !m) return 0;
    lower_text(summary, sizeof(summary), m->summary);
    lower_text(topic_name, sizeof(topic_name), topic_name_for_id(eng, m->topic_id));
    for (uint32_t i = 0; i < eng->learned_knowledge.header.entry_count
                        && i < PE_LK_RECORD_CAP; ++i){
        const pe_lk_record_t *r = &eng->learned_knowledge.records[i];
        if (!r->record_id || !r->topic_key[0]) continue;
        if (r->status != PE_LK_STATUS_CONFIRMED &&
            r->status != PE_LK_STATUS_CARTRIDGE_AUTHORED &&
            r->status != PE_LK_STATUS_WORLD_AUTHORED)
            continue;
        if ((topic_name[0] && strstr(topic_name, r->topic_key)) ||
            (summary[0] && strstr(summary, r->topic_key)))
            return 70;
    }
    return 0;
}

void pe_associative_recall(Engine *eng, const EmotionVector *ev){
    eng->active_count = 0;
    eng->cold_scratch_count = 0;          /* v3.2: reset per-turn scratch */
    uint32_t now = persona_now_ms();
    uint64_t qsig = eng->input_sig;
    int have_qsig = (qsig != 0);

    /* V6 Phase 5d: pick + cache the recall mode for this turn. */
    eng->current_recall_mode = pick_recall_mode(eng);
    uint8_t mode = eng->current_recall_mode;

    for (uint16_t i = 0; i < eng->memory.episodic_count && eng->active_count < PE_ACTIVE_MAX; ++i){
        MemoryNode *m = &eng->memory.episodic[i];
        if (m->memory_type != MEM_CORE
            && (persona_rng_u32(&eng->state) & 255u) >= m->retrieval_prob)
            continue;
        int32_t dv = ev->valence   - m->emotion.valence;   if (dv < 0) dv = -dv;
        int32_t da = ev->arousal   - m->emotion.arousal;   if (da < 0) da = -da;
        int32_t dd = ev->dominance - m->emotion.dominance; if (dd < 0) dd = -dd;
        /* weights: valence 1.0, arousal 0.7, dominance 0.5 — in tenths to keep int math */
        int32_t dist = dv * 10 + da * 7 + dd * 5;     /* max ≈ 400 * 10 = 4000 */
        int32_t match = 1000 - (dist / 4);            /* 0..1000 */
        if (match < 0) match = 0;

        /* v3.0: SimHash semantic similarity bonus.  Hamming distance 0..64
         * (lower = more similar).  Bonus contributes up to ~+250 to the
         * raw match score, which is then capped at 1000.  Memories with no
         * sig (sig==0) get no bonus. */
        if (have_qsig && m->lsh_sig != 0){
            int hd = lsh_hamming_distance(qsig, m->lsh_sig);
            int32_t lsh_bonus = (64 - hd) * 4;        /* 0..256 */
            match += lsh_bonus;
            if (match > 1000) match = 1000;
        }

        /* v3.1: self-disclosure gate — private memories only surface to
         * interlocutors who have earned enough trust.  Trust combines
         * disposition (0..1000) and engagement (0..255 → contributes 0..63).
         * A threshold of N requires trust >= N*4.  Core memories (seeded
         * from identity) always have threshold=0 and pass freely. */
        if (m->private_threshold > 0){
            int32_t trust  = (int32_t)eng->relation.disposition
                           + (int32_t)eng->relation.um_engagement / 4;
            int32_t needed = (int32_t)m->private_threshold * 4;
            if (trust < needed) continue;
        }

        match = (match * m->salience) / 255;
        /* recency: linear decay over 24h */
        uint32_t age_ms = (now > m->timestamp) ? (now - m->timestamp) : 0;
        int32_t recency = 1000 - (int32_t)(age_ms / 86400u);  /* 1 ms ≈ 1/86400 of a day, scaled wrong intentionally to favor recent within ~17min, then floor */
        if (age_ms > 24u*3600u*1000u) recency = 200;          /* old memories still trickle in */
        if (recency < 100) recency = 100;
        match = (match * recency) / 1000;

        /* V5 Phase 2: same-actor bonus. A modest additive lift (not a filter)
         * so memories with the current interlocutor surface more readily than
         * unrelated memories with similar semantic match — without hiding
         * cross-actor memories entirely. Untagged (legacy) memories get no
         * bonus and no penalty. */
        {
            uint32_t slot_actor    = pe_actor_index_get(&eng->actor_index, i);
            uint32_t current_actor = eng->relation.user_hash;
            if (slot_actor != 0 && current_actor != 0 && slot_actor == current_actor){
                match += 30;
                if (match > 1000) match = 1000;
            }
        }

        {
            int lk_boost = learned_knowledge_recall_boost(eng, m);
            if (lk_boost){
                match += lk_boost;
                if (match > 1000) match = 1000;
            }
        }

        /* V6 Phase 5d: recall-mode scoring shift. Same memory store,
         * different retrieval intent. Modifiers are deliberately modest,
         * integer-only, and inspectable: they change ranking at the
         * margins without inventing, deleting, or rewriting memories. */
        switch (mode){
        case PE_RECALL_DEFENSIVE:
            if (m->emotion.dominance < 0 || m->emotion.valence < 0) match += 24;
            if (m->emotion.valence > 40) match -= 12;
            break;
        case PE_RECALL_NOSTALGIC:
            if (m->core_memory || m->memory_type == MEM_CORE) match += 26;
            if (m->emotion.valence > 0 && m->emotion.arousal < 65) match += 18;
            break;
        case PE_RECALL_ACCUSATORY:
            if (m->emotion.valence < 0) match += 28;
            if (m->emotion.dominance < 0) match += 12;
            break;
        case PE_RECALL_INTIMACY_SEEKING:
            if (m->private_threshold > 0) match += 30;
            if (m->emotion.valence > -20 && m->emotion.arousal < 70) match += 14;
            break;
        case PE_RECALL_OBSESSION_DRIVEN:
            if (topic_is_obsession(eng, m->topic_id)) match += 42;
            if (eng->primary_topic != 0xFFFFu && m->topic_id == eng->primary_topic) match += 18;
            break;
        case PE_RECALL_MOOD_CONGRUENT: {
            int mood_pos = eng->state.mood > 0;
            int mem_pos  = m->emotion.valence > 0;
            if (mood_pos == mem_pos) match += 20;
            break;
        }
        case PE_RECALL_SHAME_AVOIDANT:
            if (m->emotion.valence < -20) match -= 30;
            break;
        default:
            break;
        }
        if (match < 0)    match = 0;
        if (match > 1000) match = 1000;

        if (match >= ACTIVATION_THRESHOLD_PER_MIL) {
            eng->active_memories[eng->active_count] = i;
            eng->active_match[eng->active_count] = (uint16_t)(match > 1000 ? 1000 : match);
            eng->active_count++;
            /* re-access resets decay counter — keeps revisited memories alive */
            eng->memory.episodic[i].decay_counter = 0;
            eng->memory.episodic[i].retrieval_prob =
                (uint8_t)(m->retrieval_prob + ((255u - m->retrieval_prob) >> 4));
        }
    }
    /* V5: reflections are abstract pattern memories, so they join the
     * same active-memory list as AETHER cold hits.  Query them before
     * AETHER so the small cold_scratch buffer cannot crowd them out. */
    if (eng->reflections.count > 0
        && eng->cold_scratch_count < PE_COLD_SCRATCH_MAX
        && eng->active_count < PE_ACTIVE_MAX){
        MemoryNode refl[2];
        int n_refl = pe_query_reflections(eng, qsig, eng->primary_topic, refl, 2);
        for (int i = 0; i < n_refl
                       && eng->cold_scratch_count < PE_COLD_SCRATCH_MAX
                       && eng->active_count < PE_ACTIVE_MAX; ++i){
            MemoryNode *rn = &eng->cold_scratch[eng->cold_scratch_count];
            *rn = refl[i];
            int32_t score = 640 + rn->salience / 2;
            if (eng->primary_topic != 0xFFFF && rn->topic_id == eng->primary_topic)
                score += 120;
            if (score > 900) score = 900;
            eng->active_memories[eng->active_count] =
                (uint16_t)(PE_EPISODIC_MAX + eng->cold_scratch_count);
            eng->active_match[eng->active_count] = (uint16_t)score;
            eng->active_count++;
            eng->cold_scratch_count++;
        }
    }

    /* v3.2: AETHER fallback.  If working memory had nothing strong (either
     * empty or top match below PE_COLD_FALLBACK_MATCH), query the cold
     * store with the current input.  Results are materialised into
     * cold_scratch[] and surfaced via active_memories[] using sentinel
     * indices (>= PE_EPISODIC_MAX).  Match scores are derived from
     * Hamming distance to the input SimHash so the existing sort below
     * orders cold hits relative to working-memory hits correctly. */
#ifndef PE_DISABLE_AETHER
    if (eng->aether && eng->lowered[0]
        && (eng->active_count == 0
            || eng->active_match[0] < PE_COLD_FALLBACK_MATCH)){
        aether_event_t cold[PE_COLD_SCRATCH_MAX];
        int n_cold = aether_query_by_text(eng->aether, eng->lowered,
                                          cold, PE_COLD_SCRATCH_MAX, 0);
        for (int i = 0; i < n_cold
                       && eng->cold_scratch_count < PE_COLD_SCRATCH_MAX
                       && eng->active_count < PE_ACTIVE_MAX; ++i){
            MemoryNode *cn = &eng->cold_scratch[eng->cold_scratch_count];
            pe_aether_to_node(&cold[i], cn);
            /* Match score from Hamming similarity, gated by cold-source
             * discount (cold hits cap at 900 so working-memory ties win). */
            int32_t cold_match = 500;
            if (have_qsig && cn->lsh_sig != 0){
                int hd = lsh_hamming_distance(qsig, cn->lsh_sig);
                cold_match = (64 - hd) * 14;     /* 0..896 */
            }
            if (cold_match < ACTIVATION_THRESHOLD_PER_MIL) continue;
            eng->active_memories[eng->active_count] =
                (uint16_t)(PE_EPISODIC_MAX + eng->cold_scratch_count);
            eng->active_match[eng->active_count]    = (uint16_t)cold_match;
            eng->active_count++;
            eng->cold_scratch_count++;
        }
    }
#endif

    /* simple insertion sort: highest match first */
    for (uint16_t i = 1; i < eng->active_count; ++i){
        uint16_t mi = eng->active_memories[i], ms = eng->active_match[i];
        int j = i - 1;
        while (j >= 0 && eng->active_match[j] < ms){
            eng->active_memories[j+1] = eng->active_memories[j];
            eng->active_match[j+1] = eng->active_match[j];
            j--;
        }
        eng->active_memories[j+1] = mi;
        eng->active_match[j+1] = ms;
    }
}

void pe_push_short_term(Engine *eng, uint8_t type, const char *text){
    MemoryStore *m = &eng->memory;
    ShortTermEntry *e = &m->short_term[m->short_term_pos];
    e->type = type;
    size_t L = strlen(text);
    if (L >= sizeof(e->text)) L = sizeof(e->text) - 1;
    memcpy(e->text, text, L);
    e->text[L] = 0;
    m->short_term_pos = (uint8_t)((m->short_term_pos + 1) % PE_SHORT_TERM_LEN);
}
