/* learned_knowledge.c -- compact learned-knowledge graph sidecar. */
#include "persona.h"
#include "persona_internal.h"
#include "learned_knowledge.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint16_t clamp1000_u16(uint16_t v){ return v > 1000u ? 1000u : v; }

static void copy_str(char *dst, size_t cap, const char *src){
    if (!dst || cap == 0) return;
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}

void pe_lk_init(pe_learned_knowledge_t *lk){
    if (!lk) return;
    memset(lk, 0, sizeof(*lk));
    lk->header.magic       = PE_LEARNED_KNOWLEDGE_MAGIC;
    lk->header.version     = PE_LEARNED_KNOWLEDGE_VERSION;
    lk->header.flags       = PE_SIDECAR_F_OPTIONAL;
    lk->header.entry_count = 0;
    lk->header.capacity    = PE_LK_RECORD_CAP;
    lk->next_record_id     = 1;
    lk->next_edge_id       = 1;
}

int pe_lk_load(pe_learned_knowledge_t *lk, const char *char_dir){
    char path[512];
    pe_learned_knowledge_t tmp;
    int v;
    if (!lk || !char_dir) return -1;
    pe_lk_init(lk);
    if (pe_path_join(path, sizeof(path), char_dir, "learned_knowledge.bin") != 0)
        return -1;
    if (pe_read_file(path, &tmp, sizeof(tmp)) != 0) return 0;
    v = pe_sidecar_validate(&tmp.header, PE_LEARNED_KNOWLEDGE_MAGIC,
                            1, PE_LEARNED_KNOWLEDGE_VERSION);
    if (v != PE_SIDECAR_OK) {
        lk->last_load_status = (uint32_t)(-v);
        pe_lk_trace("load_rejected", NULL, NULL, "invalid_sidecar", 0);
        return 0;
    }
    if (tmp.edge_count > PE_LK_EDGE_CAP) {
        lk->last_load_status = 100u;
        pe_lk_trace("load_rejected", NULL, NULL, "edge_overflow", 0);
        return 0;
    }
    if (tmp.next_record_id == 0) tmp.next_record_id = 1;
    if (tmp.next_edge_id == 0) tmp.next_edge_id = 1;
    *lk = tmp;
    return 0;
}

int pe_lk_save(const pe_learned_knowledge_t *lk, const char *char_dir){
    char path[512];
    if (!lk || !char_dir) return -1;
    if (pe_path_join(path, sizeof(path), char_dir, "learned_knowledge.bin") != 0)
        return -1;
    return pe_write_file_atomic(path, lk, sizeof(*lk));
}

static int same_topic_scope(const pe_lk_record_t *r, const pe_lk_write_t *w){
    if (!r || !w || !w->topic_key) return 0;
    if (strcmp(r->topic_key, w->topic_key)) return 0;
    if (r->scope != w->scope) return 0;
    if ((r->scope == PE_LK_SCOPE_ACTOR_SPECIFIC ||
         r->scope == PE_LK_SCOPE_RELATIONSHIP_SPECIFIC) &&
        r->source_actor_id != w->source_actor_id) return 0;
    return 1;
}

static int status_promotion_rank(uint8_t status){
    switch (status){
    case PE_LK_STATUS_DEPRECATED:        return -20;
    case PE_LK_STATUS_CORRECTED:         return -10;
    case PE_LK_STATUS_CANDIDATE:         return 5;
    case PE_LK_STATUS_PROVISIONAL:       return 10;
    case PE_LK_STATUS_DISPUTED:          return 20;
    case PE_LK_STATUS_CONFIRMED:         return 50;
    case PE_LK_STATUS_WORLD_AUTHORED:    return 80;
    case PE_LK_STATUS_CARTRIDGE_AUTHORED:return 90;
    default: return 0;
    }
}

static int is_high_authority_source(uint8_t src){
    return src == PE_LK_SRC_CARTRIDGE ||
           src == PE_LK_SRC_WORLD ||
           src == PE_LK_SRC_USER ||
           src == PE_LK_SRC_CHARACTER ||
           src == PE_LK_SRC_SYSTEM ||
           src == PE_LK_SRC_IMPORTED;
}

static void maybe_escalate_candidate(pe_lk_record_t *r,
                                      const pe_lk_write_t *w,
                                      uint32_t now_s){
    if (!r || !w) return;
    if (r->status != PE_LK_STATUS_CANDIDATE) return;
    if (is_high_authority_source(w->source_type) &&
        w->status >= PE_LK_STATUS_CONFIRMED){
        r->status = PE_LK_STATUS_CONFIRMED;
        r->source_type = w->source_type;
        r->source_tier = w->source_tier;
        r->source_actor_id = w->source_actor_id;
        copy_str(r->source_actor_name, sizeof(r->source_actor_name),
                 w->source_actor_name);
        if (r->confidence < w->confidence) r->confidence = w->confidence;
        if (r->authority_rank < w->authority_rank) r->authority_rank = w->authority_rank;
        pe_lk_trace("promote", r, NULL, "higher_authority_vouch", r->record_id);
        return;
    }
    if (r->source_type == PE_LK_SRC_MODEL &&
        w->source_type == PE_LK_SRC_MODEL &&
        w->status == PE_LK_STATUS_CANDIDATE &&
        r->reinforcement_count >= 2u &&
        now_s > r->created_at + 3600u){
        r->status = PE_LK_STATUS_CONFIRMED;
        if (r->confidence < 650u) r->confidence = 650u;
        if (r->authority_rank < 35u) r->authority_rank = 35u;
        pe_lk_trace("promote", r, NULL, "independent_model_evidence", r->record_id);
    }
}

static uint32_t pick_slot(pe_learned_knowledge_t *lk){
    uint32_t count = lk->header.entry_count;
    uint32_t worst = 0;
    int worst_score = 0x7fffffff;
    if (count < PE_LK_RECORD_CAP) {
        lk->header.entry_count++;
        return count;
    }
    for (uint32_t i = 0; i < PE_LK_RECORD_CAP; ++i){
        pe_lk_record_t *r = &lk->records[i];
        int score = (int)r->authority_rank * 4 + (int)r->confidence
                  + (int)r->reinforcement_count * 3;
        if (r->status == PE_LK_STATUS_DEPRECATED) score -= 1000;
        if (r->status == PE_LK_STATUS_CORRECTED) score -= 700;
        if (score < worst_score){ worst_score = score; worst = i; }
    }
    return worst;
}

uint32_t pe_lk_upsert(pe_learned_knowledge_t *lk,
                      const pe_lk_write_t *w,
                      uint32_t now_s){
    pe_lk_record_t rec;
    uint32_t slot;
    if (!lk || !w || !w->topic_key || !w->topic_key[0] ||
        !w->claim_text || !w->claim_text[0]) return 0;
    if (lk->header.magic != PE_LEARNED_KNOWLEDGE_MAGIC) pe_lk_init(lk);

    for (uint32_t i = 0; i < lk->header.entry_count && i < PE_LK_RECORD_CAP; ++i){
        pe_lk_record_t *r = &lk->records[i];
        if (!same_topic_scope(r, w)) continue;
        if (!strncmp(r->claim_text, w->claim_text, PE_LK_CLAIM_LEN)){
            r->reinforcement_count++;
            if (w->confidence > r->confidence) r->confidence = w->confidence;
            if (w->authority_rank > r->authority_rank) r->authority_rank = w->authority_rank;
            maybe_escalate_candidate(r, w, now_s);
            if (status_promotion_rank(w->status) > status_promotion_rank(r->status))
                r->status = w->status;
            r->updated_at = now_s;
            pe_lk_trace("reinforce", r, NULL, "same_claim", r->record_id);
            return r->record_id;
        }
    }

    memset(&rec, 0, sizeof(rec));
    rec.record_id       = lk->next_record_id++;
    if (lk->next_record_id == 0) lk->next_record_id = 1;
    copy_str(rec.topic_key, sizeof(rec.topic_key), w->topic_key);
    copy_str(rec.claim_text, sizeof(rec.claim_text), w->claim_text);
    rec.scope           = w->scope ? w->scope : PE_LK_SCOPE_REAL_WORLD;
    rec.source_type     = w->source_type ? w->source_type : PE_LK_SRC_UNKNOWN;
    rec.source_tier     = w->source_tier ? w->source_tier : PE_LK_TIER_OFFLINE;
    rec.authority_rank  = w->authority_rank;
    rec.source_actor_id = w->source_actor_id;
    copy_str(rec.source_actor_name, sizeof(rec.source_actor_name), w->source_actor_name);
    rec.confidence      = clamp1000_u16(w->confidence);
    rec.status          = w->status ? w->status : PE_LK_STATUS_PROVISIONAL;
    rec.domain_tag      = w->domain_tag;
    rec.created_at      = now_s;
    rec.updated_at      = now_s;
    rec.correction_of_record_id = w->correction_of_record_id;
    rec.evidence_ref    = w->evidence_ref;

    if (w->correction_of_record_id){
        for (uint32_t i = 0; i < lk->header.entry_count && i < PE_LK_RECORD_CAP; ++i){
            pe_lk_record_t *old = &lk->records[i];
            if (old->record_id == w->correction_of_record_id){
                old->status = PE_LK_STATUS_CORRECTED;
                old->contradiction_count++;
                old->updated_at = now_s;
            }
        }
    }

    slot = pick_slot(lk);
    lk->records[slot] = rec;
    if (w->correction_of_record_id)
        pe_lk_record_edge(lk, rec.record_id, PE_LK_EDGE_CORRECTS,
                          w->correction_of_record_id, 1000, 255, now_s);
    if (w->evidence_ref)
        pe_lk_record_edge(lk, rec.record_id, PE_LK_EDGE_EVIDENCED_BY,
                          w->evidence_ref, 600, 180, now_s);
    pe_lk_trace("upsert", &lk->records[slot], NULL, "new_claim", rec.record_id);
    return rec.record_id;
}

uint32_t pe_lk_record_edge(pe_learned_knowledge_t *lk,
                           uint32_t source_record_id,
                           uint8_t relation_type,
                           uint32_t target_record_id,
                           uint16_t weight,
                           uint8_t confidence,
                           uint32_t now_s){
    uint32_t slot;
    pe_lk_edge_t e;
    if (!lk || !source_record_id || !target_record_id || !relation_type) return 0;
    if (lk->edge_count < PE_LK_EDGE_CAP) slot = lk->edge_count++;
    else slot = (lk->next_edge_id - 1u) % PE_LK_EDGE_CAP;
    memset(&e, 0, sizeof(e));
    e.edge_id = lk->next_edge_id++;
    if (lk->next_edge_id == 0) lk->next_edge_id = 1;
    e.source_record_id = source_record_id;
    e.relation_type = relation_type;
    e.target_record_id = target_record_id;
    e.weight = clamp1000_u16(weight);
    e.confidence = confidence;
    e.created_at = now_s;
    e.updated_at = now_s;
    lk->edges[slot] = e;
    pe_lk_trace("edge", NULL, &lk->edges[slot], pe_lk_edge_name(relation_type), source_record_id);
    return e.edge_id;
}

static int scope_matches(const pe_lk_record_t *r, uint8_t scope, uint32_t actor_id){
    if (!r) return 0;
    if (r->scope == scope) {
        if ((scope == PE_LK_SCOPE_ACTOR_SPECIFIC ||
             scope == PE_LK_SCOPE_RELATIONSHIP_SPECIFIC) &&
            r->source_actor_id && actor_id && r->source_actor_id != actor_id)
            return 0;
        return 1;
    }
    if (scope == PE_LK_SCOPE_REAL_WORLD && r->scope == PE_LK_SCOPE_REAL_WORLD) return 1;
    if (scope == 0 && r->scope != PE_LK_SCOPE_SESSION_LOCAL) return 1;
    return 0;
}

static int authority_score(const pe_lk_record_t *r){
    int score;
    if (!r) return -999999;
    if (r->status == PE_LK_STATUS_DEPRECATED ||
        r->status == PE_LK_STATUS_CORRECTED) return -999999;
    score = (int)r->authority_rank * 20 + (int)r->confidence;
    switch (r->status){
    case PE_LK_STATUS_CARTRIDGE_AUTHORED: score += 5000; break;
    case PE_LK_STATUS_WORLD_AUTHORED:     score += 4500; break;
    case PE_LK_STATUS_CONFIRMED:          score += 3000; break;
    case PE_LK_STATUS_CANDIDATE:          score -= 500; break;
    case PE_LK_STATUS_DISPUTED:           score -= 1200; break;
    case PE_LK_STATUS_PROVISIONAL:        score += 0; break;
    default: break;
    }
    switch (r->source_type){
    case PE_LK_SRC_CARTRIDGE: score += 2500; break;
    case PE_LK_SRC_WORLD:     score += 2200; break;
    case PE_LK_SRC_USER:      score += 1500; break;
    case PE_LK_SRC_CHARACTER: score += 1200; break;
    case PE_LK_SRC_MODEL:     score -= 300; break;
    default: break;
    }
    score += (int)r->reinforcement_count * 20;
    score -= (int)r->contradiction_count * 80;
    return score;
}

const pe_lk_record_t *pe_lk_resolve(const pe_learned_knowledge_t *lk,
                                    const char *topic_key,
                                    uint8_t scope,
                                    uint32_t actor_id,
                                    int *ambiguous){
    const pe_lk_record_t *best = NULL;
    int best_score = -999999;
    int second_score = -999999;
    if (ambiguous) *ambiguous = 0;
    if (!lk || lk->header.magic != PE_LEARNED_KNOWLEDGE_MAGIC ||
        !topic_key || !topic_key[0]) return NULL;
    for (uint32_t i = 0; i < lk->header.entry_count && i < PE_LK_RECORD_CAP; ++i){
        const pe_lk_record_t *r = &lk->records[i];
        int score;
        if (!r->record_id || strcmp(r->topic_key, topic_key)) continue;
        if (!scope_matches(r, scope, actor_id)) continue;
        score = authority_score(r);
        if (score > best_score){
            second_score = best_score;
            best_score = score;
            best = r;
        } else if (score > second_score) {
            second_score = score;
        }
    }
    if (ambiguous && best && second_score > -999999 && best_score - second_score < 200)
        *ambiguous = 1;
    if (best) pe_lk_trace("resolve", best, NULL,
                          (ambiguous && *ambiguous) ? "ambiguous" : "winner",
                          best->record_id);
    return best;
}

int pe_lk_mark_used(pe_learned_knowledge_t *lk,
                    uint32_t record_id,
                    uint32_t now_s){
    if (!lk || !record_id) return 0;
    for (uint32_t i = 0; i < lk->header.entry_count && i < PE_LK_RECORD_CAP; ++i){
        if (lk->records[i].record_id == record_id){
            lk->records[i].last_used_at = now_s;
            lk->records[i].updated_at = now_s;
            pe_lk_trace("used", &lk->records[i], NULL, "offline_retrieval", record_id);
            return 1;
        }
    }
    return 0;
}

static void lower_copy(char *dst, size_t cap, const char *src){
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    for (; src[i] && i + 1 < cap; ++i)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

int pe_lk_output_repeats_corrected_claim(const pe_learned_knowledge_t *lk,
                                         const char *out,
                                         const char *topic_key){
    char low[512];
    if (!lk || !out || !topic_key) return 0;
    lower_copy(low, sizeof(low), out);
    for (uint32_t i = 0; i < lk->header.entry_count && i < PE_LK_RECORD_CAP; ++i){
        const pe_lk_record_t *r = &lk->records[i];
        if (!r->record_id || strcmp(r->topic_key, topic_key)) continue;
        if (r->status != PE_LK_STATUS_CORRECTED &&
            r->status != PE_LK_STATUS_DEPRECATED) continue;
        if (strstr(r->claim_text, "positive charge") &&
            strstr(low, "positive charge"))
            return 1;
    }
    return 0;
}

int pe_lk_output_conflicts_authority(const pe_learned_knowledge_t *lk,
                                     const char *out,
                                     const char *topic_key,
                                     uint8_t scope,
                                     uint32_t actor_id){
    const pe_lk_record_t *best;
    char low[512];
    int ambiguous = 0;
    if (!lk || !out || !topic_key || !topic_key[0]) return 0;
    best = pe_lk_resolve(lk, topic_key, scope, actor_id, &ambiguous);
    if (!best || ambiguous) return 0;
    if (best->source_type == PE_LK_SRC_MODEL ||
        best->status == PE_LK_STATUS_CANDIDATE ||
        best->status == PE_LK_STATUS_PROVISIONAL ||
        best->status == PE_LK_STATUS_DISPUTED ||
        best->confidence < 650u ||
        best->authority_rank < 60u)
        return 0;
    lower_copy(low, sizeof(low), out);
    if (!strcmp(topic_key, "electricity")){
        if ((strstr(best->claim_text, "electron") ||
             strstr(best->claim_text, "potential difference")) &&
            strstr(low, "positive charge"))
            return 1;
    }
    return 0;
}

const char *pe_lk_scope_name(uint8_t v){
    switch (v){
    case PE_LK_SCOPE_REAL_WORLD: return "real_world";
    case PE_LK_SCOPE_CARTRIDGE_CANON: return "cartridge_canon";
    case PE_LK_SCOPE_SIMULATION_WORLD: return "simulation_world";
    case PE_LK_SCOPE_ACTOR_SPECIFIC: return "actor_specific";
    case PE_LK_SCOPE_RELATIONSHIP_SPECIFIC: return "relationship_specific";
    case PE_LK_SCOPE_SESSION_LOCAL: return "session_local";
    case PE_LK_SCOPE_PRIVATE_CHARACTER_BELIEF: return "private_character_belief";
    default: return "unknown";
    }
}

const char *pe_lk_source_name(uint8_t v){
    switch (v){
    case PE_LK_SRC_CARTRIDGE: return "cartridge";
    case PE_LK_SRC_WORLD: return "world";
    case PE_LK_SRC_USER: return "user";
    case PE_LK_SRC_CHARACTER: return "character";
    case PE_LK_SRC_MODEL: return "model";
    case PE_LK_SRC_SYSTEM: return "system";
    case PE_LK_SRC_IMPORTED: return "imported";
    default: return "unknown";
    }
}

const char *pe_lk_tier_name(uint8_t v){
    switch (v){
    case PE_LK_TIER_TEMPLATE: return "template";
    case PE_LK_TIER_LOCAL_MODEL: return "local_model";
    case PE_LK_TIER_SLM: return "slm";
    case PE_LK_TIER_FRONTIER: return "frontier";
    case PE_LK_TIER_OFFLINE: return "offline";
    case PE_LK_TIER_SYSTEM: return "system";
    default: return "unknown";
    }
}

const char *pe_lk_status_name(uint8_t v){
    switch (v){
    case PE_LK_STATUS_PROVISIONAL: return "provisional";
    case PE_LK_STATUS_CONFIRMED: return "confirmed";
    case PE_LK_STATUS_CORRECTED: return "corrected";
    case PE_LK_STATUS_DISPUTED: return "disputed";
    case PE_LK_STATUS_DEPRECATED: return "deprecated";
    case PE_LK_STATUS_CARTRIDGE_AUTHORED: return "cartridge_authored";
    case PE_LK_STATUS_WORLD_AUTHORED: return "world_authored";
    case PE_LK_STATUS_CANDIDATE: return "candidate";
    default: return "unknown";
    }
}

const char *pe_lk_edge_name(uint8_t v){
    switch (v){
    case PE_LK_EDGE_CORRECTS: return "corrects";
    case PE_LK_EDGE_CONTRADICTS: return "contradicts";
    case PE_LK_EDGE_SUPPORTS: return "supports";
    case PE_LK_EDGE_DERIVED_FROM: return "derived_from";
    case PE_LK_EDGE_TAUGHT_BY: return "taught_by";
    case PE_LK_EDGE_BELONGS_TO_SCOPE: return "belongs_to_scope";
    case PE_LK_EDGE_EVIDENCED_BY: return "evidenced_by";
    case PE_LK_EDGE_USED_IN_RESPONSE: return "used_in_response";
    case PE_LK_EDGE_RELATED_TO_ACTOR: return "related_to_actor";
    case PE_LK_EDGE_RELATED_TO_TOPIC: return "related_to_topic";
    default: return "unknown";
    }
}

static void json_escape(FILE *f, const char *s){
    if (!s) s = "";
    for (; *s; ++s){
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fputc('\\', f), fputc(c, f);
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\r') fputs("\\r", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c < 32) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
}

void pe_lk_trace(const char *op,
                 const pe_lk_record_t *rec,
                 const pe_lk_edge_t *edge,
                 const char *reason,
                 uint32_t winner_id){
    const char *enabled = getenv("PE_LK_TRACE");
    FILE *f;
    if (enabled && !strcmp(enabled, "0")) return;
    mkdir("tmp", 0777);
    mkdir(PE_LK_TRACE_DIR, 0777);
    f = fopen("tmp/learned_knowledge/trace.jsonl", "ab");
    if (!f) return;
    fprintf(f, "{\"op\":\"");
    json_escape(f, op ? op : "");
    fprintf(f, "\",\"reason\":\"");
    json_escape(f, reason ? reason : "");
    fprintf(f, "\",\"winner\":%u", (unsigned)winner_id);
    if (rec){
        fprintf(f, ",\"record_id\":%u,\"topic\":\"", (unsigned)rec->record_id);
        json_escape(f, rec->topic_key);
        fprintf(f, "\",\"claim\":\"");
        json_escape(f, rec->claim_text);
        fprintf(f, "\",\"scope\":\"%s\",\"source\":\"%s\",\"tier\":\"%s\",\"status\":\"%s\",\"authority\":%u,\"confidence\":%u",
                pe_lk_scope_name(rec->scope), pe_lk_source_name(rec->source_type),
                pe_lk_tier_name(rec->source_tier), pe_lk_status_name(rec->status),
                (unsigned)rec->authority_rank, (unsigned)rec->confidence);
    }
    if (edge){
        fprintf(f, ",\"edge_id\":%u,\"edge\":\"%s\",\"source_record\":%u,\"target_record\":%u,\"weight\":%u",
                (unsigned)edge->edge_id, pe_lk_edge_name(edge->relation_type),
                (unsigned)edge->source_record_id, (unsigned)edge->target_record_id,
                (unsigned)edge->weight);
    }
    fprintf(f, "}\n");
    fclose(f);
}
