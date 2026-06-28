#include "import_runtime.h"

#include "persona_internal.h"
#include "engine_clock.h"
#include "speech_ledger.h"
#include "relation_dims.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clampi(int v, int lo, int hi){
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void normalize_key(const char *src, char *dst, size_t cap){
    size_t pos = 0;
    int last_us = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (*src && pos + 1 < cap){
        unsigned char c = (unsigned char)*src++;
        if (isalnum(c)){
            dst[pos++] = (char)tolower(c);
            last_us = 0;
        } else if (!last_us && pos > 0){
            dst[pos++] = '_';
            last_us = 1;
        }
    }
    while (pos > 0 && dst[pos - 1] == '_') --pos;
    dst[pos] = 0;
}

int pe_import_text_safe(const char *text){
    char low[256];
    size_t i = 0;
    if (!text || !text[0]) return 0;
    for (; text[i] && i + 1 < sizeof(low); ++i)
        low[i] = (char)tolower((unsigned char)text[i]);
    low[i] = 0;
    if (strstr(low, "[learned:")) return 0;
    if (strstr(low, "[memory:")) return 0;
    if (strstr(low, "[system")) return 0;
    if (strstr(low, "[identity")) return 0;
    if (strstr(low, "<start_of_turn>")) return 0;
    if (strstr(low, "</")) return 0;
    if (strchr(low, '{') || strchr(low, '}')) return 0;
    return 1;
}

int pe_import_topic_id_for_key(const Engine *eng,
                               const char *topic_key,
                               uint16_t *topic_id_out){
    char want[PE_LK_TOPIC_LEN];
    if (topic_id_out) *topic_id_out = 0xFFFFu;
    if (!eng || !topic_key || !topic_key[0]) return 0;
    normalize_key(topic_key, want, sizeof(want));
    if (!want[0]) return 0;

    for (uint32_t i = 0; i < eng->topics.count; ++i){
        char have[PE_LK_TOPIC_LEN];
        const TopicDef *t = &eng->topics.topics[i];
        if (!t->name[0]) continue;
        normalize_key(t->name, have, sizeof(have));
        if (!strcmp(have, want)){
            if (topic_id_out) *topic_id_out = t->id;
            return 1;
        }
    }
    for (uint32_t i = 0; i < eng->patterns.count; ++i){
        char have[PE_LK_TOPIC_LEN];
        const Pattern *p = &eng->patterns.entries[i];
        if (!p->keyword[0] || p->topic_id == 0xFFFFu) continue;
        normalize_key(p->keyword, have, sizeof(have));
        if (!strcmp(have, want)){
            if (topic_id_out) *topic_id_out = p->topic_id;
            return 1;
        }
    }
    return 0;
}

static EmotionVector ev_from_impact(int emotional_impact){
    EmotionVector ev;
    int mag = emotional_impact < 0 ? -emotional_impact : emotional_impact;
    memset(&ev, 0, sizeof(ev));
    ev.valence = (int8_t)clampi(emotional_impact / 10, -100, 100);
    ev.arousal = (int8_t)clampi(mag / 10, 0, 100);
    ev.dominance = (int8_t)clampi(ev.valence / 2, -100, 100);
    return ev;
}

static int same_imported_memory(const Engine *eng,
                                uint32_t actor_hash,
                                uint16_t topic_id,
                                int is_core,
                                const char *summary,
                                uint16_t *slot_out){
    if (!eng || !summary || !summary[0]) return 0;
    for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
        const MemoryNode *m = &eng->memory.episodic[i];
        uint32_t tagged_actor = pe_actor_index_get(&eng->actor_index, i);
        if (!!(m->core_memory || m->memory_type == MEM_CORE) != !!is_core) continue;
        if (m->topic_id != topic_id) continue;
        if (tagged_actor != actor_hash) continue;
        if (strncmp(m->summary, summary, sizeof(m->summary)) != 0) continue;
        if (slot_out) *slot_out = i;
        return 1;
    }
    return 0;
}

int pe_import_memory_record(Engine *eng,
                            const pe_import_memory_record_t *rec,
                            uint32_t *memory_id_out){
    EmotionVector ev;
    uint16_t topic_id = 0xFFFFu;
    uint16_t slot = 0xFFFFu;
    uint32_t actor_hash = 0;
    uint8_t flags = PE_MEM_FLAG_IMPORTED;
    if (memory_id_out) *memory_id_out = 0;
    if (!eng || !rec || !rec->summary || !rec->summary[0]) return -1;
    if (!pe_import_text_safe(rec->summary)) return -2;
    if (rec->actor_name && rec->actor_name[0])
        actor_hash = persona_hash(rec->actor_name);
    pe_import_topic_id_for_key(eng, rec->topic_key, &topic_id);
    if (rec->is_pinned) flags |= PE_MEM_FLAG_USER_PINNED;

    if (same_imported_memory(eng, actor_hash, topic_id, rec->is_core, rec->summary, &slot)){
        MemoryNode *m = &eng->memory.episodic[slot];
        if ((uint8_t)rec->salience > m->salience) m->salience = (uint8_t)rec->salience;
        m->flags |= flags;
        m->retrieval_prob = 255u;
        if (memory_id_out) *memory_id_out = m->id;
        return 0;
    }

    ev = ev_from_impact(rec->emotional_impact);
    pe_commit_memory_ex(eng, rec->summary, &ev, topic_id,
                        (uint8_t)clampi(rec->salience, 0, 255), 0, flags);
    if (eng->memory.next_memory_id == 0) return -3;
    for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
        if (eng->memory.episodic[i].id == eng->memory.next_memory_id - 1u){
            slot = i;
            break;
        }
    }
    if (slot == 0xFFFFu) return -3;
    {
        MemoryNode *m = &eng->memory.episodic[slot];
        if (rec->is_core){
            m->core_memory = 1u;
            m->memory_type = MEM_CORE;
            m->retrieval_prob = 255u;
        }
        if (actor_hash)
            pe_actor_index_tag(&eng->actor_index, slot, actor_hash);
        if (memory_id_out) *memory_id_out = m->id;
    }
    return 0;
}

uint32_t pe_import_learned_record(Engine *eng,
                                  const pe_lk_write_t *w){
    if (!eng || !w || !w->topic_key || !w->claim_text) return 0;
    if (!pe_import_text_safe(w->claim_text)) return 0;
    return pe_lk_upsert(&eng->learned_knowledge, w, pe_clock_now_s());
}

uint32_t pe_import_learned_edge(Engine *eng,
                                uint32_t source_record_id,
                                uint8_t relation_type,
                                uint32_t target_record_id,
                                uint16_t weight,
                                uint8_t confidence){
    if (!eng) return 0;
    if (relation_type == PE_LK_EDGE_CORRECTS ||
        relation_type == PE_LK_EDGE_CONTRADICTS){
        for (uint32_t i = 0; i < eng->learned_knowledge.header.entry_count
                            && i < PE_LK_RECORD_CAP; ++i){
            pe_lk_record_t *r = &eng->learned_knowledge.records[i];
            if (!r->record_id) continue;
            if (r->record_id == target_record_id){
                r->status = (relation_type == PE_LK_EDGE_CORRECTS)
                          ? PE_LK_STATUS_CORRECTED
                          : PE_LK_STATUS_DISPUTED;
                r->contradiction_count++;
                r->updated_at = pe_clock_now_s();
            } else if (r->record_id == source_record_id &&
                       relation_type == PE_LK_EDGE_CORRECTS) {
                r->correction_of_record_id = target_record_id;
                r->updated_at = pe_clock_now_s();
            }
        }
    }
    return pe_lk_record_edge(&eng->learned_knowledge,
                             source_record_id,
                             relation_type,
                             target_record_id,
                             weight,
                             confidence,
                             pe_clock_now_s());
}

int pe_import_relationship_record(Engine *eng,
                                  const pe_import_relationship_record_t *rec){
    char saved_user[PE_NAME_LEN];
    int had_user = 0;
    if (!eng || !rec || !rec->actor_name || !rec->actor_name[0]) return -1;
    if (!pe_import_text_safe(rec->actor_name)) return -2;

    if (eng->relation.known_as[0]){
        snprintf(saved_user, sizeof(saved_user), "%s", eng->relation.known_as);
        had_user = 1;
    }

    if (pe_load_relation(eng, rec->actor_name) != 0) return -3;
    eng->relation.disposition = rec->trust;
    eng->state.trust_user = eng->relation.disposition;
    snprintf(eng->relation.known_as, sizeof(eng->relation.known_as), "%s", rec->actor_name);
    eng->relation_dims.trust = rec->trust;
    eng->relation_dims.threat = rec->threat;
    eng->relation_dims.intimacy = rec->intimacy;
    eng->relation_dims.resentment = rec->resentment;
    eng->relation_dims.dependency = rec->dependency;
    eng->relation_dims.obligation = rec->obligation;
    eng->relation_dims.envy = rec->envy;
    eng->relation_dims.admiration = rec->admiration;
    eng->relation_dims.embarrassment = rec->embarrassment;
    if (pe_save_relation(eng) != 0) return -4;

    if (had_user) pe_load_relation(eng, saved_user);
    return 0;
}

static uint16_t speech_act_from_name(const char *name){
    char key[32];
    if (!name || !name[0]) return PE_SA_ASSERTION;
    normalize_key(name, key, sizeof(key));
    if (!strcmp(key, "return_to")) return PE_SA_QUESTION;
    if (!strcmp(key, "question")) return PE_SA_QUESTION;
    if (!strcmp(key, "clarify")) return PE_SA_QUESTION;
    if (!strcmp(key, "promise")) return PE_SA_PROMISE;
    if (!strcmp(key, "apology")) return PE_SA_APOLOGY;
    if (!strcmp(key, "correction")) return PE_SA_CORRECTION;
    if (!strcmp(key, "disclosure")) return PE_SA_DISCLOSURE;
    if (!strcmp(key, "withdraw")) return PE_SA_WITHDRAWAL;
    return PE_SA_ASSERTION;
}

int pe_import_open_loop_record(Engine *eng,
                               const pe_import_open_loop_record_t *rec,
                               uint32_t *loop_id_out){
    uint32_t actor_id = 0;
    uint16_t topic_id = 0xFFFFu;
    uint16_t desired = 0;
    if (loop_id_out) *loop_id_out = 0;
    if (!eng || !rec) return -1;
    if (rec->actor_name && rec->actor_name[0]){
        if (!pe_import_text_safe(rec->actor_name)) return -2;
        actor_id = persona_hash(rec->actor_name);
    } else {
        actor_id = eng->relation.user_hash;
    }
    pe_import_topic_id_for_key(eng, rec->topic_key, &topic_id);
    if (topic_id == 0xFFFFu) return 0;
    desired = speech_act_from_name(rec->desired_speech_act);

    for (uint32_t i = 0; i < PE_OPEN_LOOPS_RING_SIZE; ++i){
        const pe_open_loop_t *loop = &eng->open_loops.loops[i];
        if (loop->status != PE_OL_ACTIVE) continue;
        if (loop->target_actor_id == actor_id &&
            loop->target_topic_id == topic_id &&
            loop->desired_speech_act == desired){
            if (loop_id_out) *loop_id_out = loop->id;
            return 0;
        }
    }

    pe_open_loops_record(&eng->open_loops,
                         actor_id,
                         topic_id,
                         desired,
                         rec->urgency,
                         rec->shame_cost,
                         rec->avoidance_pressure,
                         eng->state.turn_count,
                         eng->state.turn_count + 160u);
    if (loop_id_out){
        const pe_open_loop_t *loop = pe_open_loops_latest_for_actor(&eng->open_loops, actor_id);
        if (loop) *loop_id_out = loop->id;
    }
    return 0;
}
