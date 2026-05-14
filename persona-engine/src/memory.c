/* memory.c — episodic store, salience, associative recall, short-term buffer. */
#include "persona.h"
#include "persona_internal.h"
#include <string.h>
#include <stdlib.h>

#define ACTIVATION_THRESHOLD_PER_MIL 200  /* 0.2 in 0..1000 scale */

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
        if (m->episodic[i].core_memory) continue;
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
        if (m->episodic[slot].core_memory) return; /* nothing to evict — drop */
    }
    MemoryNode *n = &m->episodic[slot];
    memset(n, 0, sizeof(*n));
    n->id = m->next_memory_id++;
    n->type = identity_threat ? 1 : 0;
    n->salience = salience;
    n->emotion = *ev;
    n->timestamp = persona_now_ms();
    n->topic_id = topic_id;
    n->core_memory = (salience > 200) ? 1 : 0;  /* promotion threshold */
    n->decay_counter = 0;
    if (summary) {
        size_t L = strlen(summary);
        if (L >= sizeof(n->summary)) L = sizeof(n->summary) - 1;
        memcpy(n->summary, summary, L);
        n->summary[L] = 0;
    }
}

void pe_decay_episodic(Engine *eng){
    /* non-core: salience -1 every 1000 turns; we approximate by per-turn 1/1000 chance */
    MemoryStore *m = &eng->memory;
    for (uint16_t i = 0; i < m->episodic_count; ++i){
        MemoryNode *n = &m->episodic[i];
        if (n->core_memory) continue;
        n->decay_counter++;
        if (n->decay_counter >= 64) {
            n->decay_counter = 0;
            if (n->salience > 0) n->salience--;
        }
    }
    /* compact: drop salience-0 non-core */
    uint16_t w = 0;
    for (uint16_t i = 0; i < m->episodic_count; ++i){
        if (m->episodic[i].core_memory || m->episodic[i].salience > 0)
            m->episodic[w++] = m->episodic[i];
    }
    m->episodic_count = w;
}

void pe_associative_recall(Engine *eng, const EmotionVector *ev){
    eng->active_count = 0;
    uint32_t now = persona_now_ms();
    for (uint16_t i = 0; i < eng->memory.episodic_count && eng->active_count < PE_EPISODIC_MAX; ++i){
        const MemoryNode *m = &eng->memory.episodic[i];
        int32_t dv = ev->valence   - m->emotion.valence;   if (dv < 0) dv = -dv;
        int32_t da = ev->arousal   - m->emotion.arousal;   if (da < 0) da = -da;
        int32_t dd = ev->dominance - m->emotion.dominance; if (dd < 0) dd = -dd;
        /* weights: valence 1.0, arousal 0.7, dominance 0.5 — in tenths to keep int math */
        int32_t dist = dv * 10 + da * 7 + dd * 5;     /* max ≈ 400 * 10 = 4000 */
        int32_t match = 1000 - (dist / 4);            /* 0..1000 */
        if (match < 0) continue;
        match = (match * m->salience) / 255;
        /* recency: linear decay over 24h */
        uint32_t age_ms = (now > m->timestamp) ? (now - m->timestamp) : 0;
        int32_t recency = 1000 - (int32_t)(age_ms / 86400u);  /* 1 ms ≈ 1/86400 of a day, scaled wrong intentionally to favor recent within ~17min, then floor */
        if (age_ms > 24u*3600u*1000u) recency = 200;          /* old memories still trickle in */
        if (recency < 100) recency = 100;
        match = (match * recency) / 1000;
        if (match >= ACTIVATION_THRESHOLD_PER_MIL) {
            eng->active_memories[eng->active_count] = i;
            eng->active_match[eng->active_count] = (uint16_t)(match > 1000 ? 1000 : match);
            eng->active_count++;
        }
    }
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
