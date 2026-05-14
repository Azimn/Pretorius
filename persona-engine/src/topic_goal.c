/* topic_goal.c — topic momentum, pattern classification, goal arbitration, intent. */
#include "persona.h"
#include "persona_internal.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ---------- pattern lookup ---------- */

static void boost_topic(NPCState *s, uint16_t topic_id, int amount){
    if (topic_id == 0xFFFF) return;
    int slot_free = -1;
    int slot_lowest = 0;
    int lowest = 0x7FFFFFFF;
    for (int i = 0; i < PE_TOPIC_SLOTS; ++i){
        if (s->topic_momentum[i].momentum == 0 && s->topic_momentum[i].topic_id == 0) {
            if (slot_free == -1) slot_free = i;
            continue;
        }
        if (s->topic_momentum[i].topic_id == topic_id) {
            int v = s->topic_momentum[i].momentum + amount;
            if (v > 1000) v = 1000;
            s->topic_momentum[i].momentum = (uint16_t)v;
            return;
        }
        if (s->topic_momentum[i].momentum < lowest) {
            lowest = s->topic_momentum[i].momentum;
            slot_lowest = i;
        }
    }
    int slot = slot_free >= 0 ? slot_free : slot_lowest;
    s->topic_momentum[slot].topic_id = topic_id;
    s->topic_momentum[slot].momentum = (uint16_t)(amount > 1000 ? 1000 : amount);
}

void pe_classify_input(Engine *eng, const char *input, EmotionVector *out_ev){
    char lower[512];
    size_t L = strlen(input);
    if (L >= sizeof(lower)) L = sizeof(lower) - 1;
    memcpy(lower, input, L); lower[L] = 0; pe_strlower(lower);

    eng->input_class = 0;
    eng->matched_group = 0xFFFF;
    eng->primary_topic = 0xFFFF;
    int v = 0, a = 30, d = 0;

    /* sweep all matching patterns; sum emotion deltas, last class wins */
    for (uint32_t i = 0; i < eng->patterns.count; ++i){
        const Pattern *p = &eng->patterns.entries[i];
        if (!p->keyword[0]) continue;
        if (pe_find_lower(lower, p->keyword)){
            v += p->delta_valence;
            a += p->delta_arousal;
            d += p->delta_dominance;
            if (p->input_class) eng->input_class = p->input_class;
            if (p->template_group != 0xFFFF) eng->matched_group = p->template_group;
            if (p->topic_id != 0xFFFF){
                boost_topic(&eng->state, p->topic_id, 400);
                if (eng->primary_topic == 0xFFFF) eng->primary_topic = p->topic_id;
            }
        }
    }
    /* punctuation heuristics */
    if (strchr(lower, '?')) {
        if (eng->input_class == 0) eng->input_class = 3;
        a += 10;
    }
    if (strchr(lower, '!')) { a += 15; }
    if (v >  100) v =  100;
    if (v < -100) v = -100;
    if (a >  100) a =  100;
    if (a <    0) a =    0;
    if (d >  100) d =  100;
    if (d < -100) d = -100;
    out_ev->valence = (int8_t)v;
    out_ev->arousal = (int8_t)a;
    out_ev->dominance = (int8_t)d;
}

void pe_update_topic_momentum(Engine *eng){
    /* decay all by 245/256 */
    for (int i = 0; i < PE_TOPIC_SLOTS; ++i){
        uint32_t m = eng->state.topic_momentum[i].momentum;
        m = (m * 245u) >> 8;
        eng->state.topic_momentum[i].momentum = (uint16_t)m;
        if (m == 0) eng->state.topic_momentum[i].topic_id = 0;
    }
    /* propagate to adjacents of primary topic */
    if (eng->primary_topic != 0xFFFF){
        for (uint32_t i = 0; i < eng->topics.count; ++i){
            if (eng->topics.topics[i].id == eng->primary_topic){
                for (int k = 0; k < 6; ++k){
                    uint16_t adj = eng->topics.topics[i].adjacents[k];
                    if (adj == 0xFFFF) break;
                    boost_topic(&eng->state, adj, 100);
                }
                break;
            }
        }
    }
    /* obsessions get a slow trickle so they resurface */
    for (int i = 0; i < PE_OBSESSION_COUNT; ++i){
        uint16_t o = eng->identity.obsessions[i];
        if (!o) break;
        boost_topic(&eng->state, o, 40);
    }
}

/* ---------- goal arbitration ---------- */

static int topic_momentum_for(const NPCState *s, uint16_t tid){
    if (tid == 0xFFFF) return 0;
    for (int i = 0; i < PE_TOPIC_SLOTS; ++i)
        if (s->topic_momentum[i].topic_id == tid) return s->topic_momentum[i].momentum;
    return 0;
}

uint16_t pe_select_goal(Engine *eng){
    int32_t best_score = -0x7FFFFFFF;
    uint16_t best_idx = 0;
    for (uint16_t i = 0; i < eng->goals.count; ++i){
        const GoalDef *g = &eng->goals.entries[i];
        int32_t score = g->base_priority * 10;
        for (int d = 0; d < PE_DRIVE_COUNT; ++d)
            score += (eng->state.drive_values[d] * g->drive_weight[d]) / 128;
        /* topic bonus */
        score += topic_momentum_for(&eng->state, g->bias_topic) / 4;
        /* memory bonus: any active negative memory boosts vindication-like goals */
        for (uint16_t k = 0; k < eng->active_count && k < 6; ++k){
            const MemoryNode *m = &eng->memory.episodic[eng->active_memories[k]];
            if (m->emotion.valence < -20 && g->intent_id == PE_INTENT_ACCUSE) score += 50;
            if (m->emotion.valence >  20 && g->intent_id == PE_INTENT_REMINISCE) score += 40;
        }
        /* personality bonus */
        if (g->intent_id == PE_INTENT_MONOLOGUE) score += eng->identity.extraversion / 256;
        if (g->intent_id == PE_INTENT_WITHDRAW)  score += eng->identity.neuroticism / 256;
        if (g->intent_id == PE_INTENT_FLATTER)   score += eng->identity.agreeableness / 256;
        score += (int32_t)(persona_rng_u32(&eng->state) & 0x0F);
        if (score > best_score) { best_score = score; best_idx = i; }
    }
    return best_idx;
}

uint16_t pe_select_intent(Engine *eng, uint16_t goal_idx, const EmotionVector *ev){
    uint16_t base = (goal_idx < eng->goals.count) ? eng->goals.entries[goal_idx].intent_id : PE_INTENT_MONOLOGUE;
    /* high arousal + negative valence → accuse or withdraw */
    if (ev->arousal > 60 && ev->valence < -30){
        return (eng->identity.neuroticism > 0x8000) ? PE_INTENT_WITHDRAW : PE_INTENT_ACCUSE;
    }
    /* direct question + high trust → answer */
    if (eng->input_class == 3 && eng->relation.disposition > 500) return PE_INTENT_ANSWER;
    /* vivid memory active → reminisce occasionally */
    if (eng->active_count > 0 && eng->active_match[0] > 700
        && (persona_rng_u32(&eng->state) & 3) == 0) return PE_INTENT_REMINISCE;
    /* praise stokes boasting */
    if (eng->input_class == 1) return PE_INTENT_BOAST;
    /* threat → threaten back if dominance high */
    if (eng->input_class == 4 && eng->state.drive_values[PE_DRIVE_VINDICATION] > 700)
        return PE_INTENT_THREATEN;
    return base;
}
