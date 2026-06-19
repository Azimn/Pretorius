/* learned_knowledge.h -- V6 compact learned-knowledge graph sidecar.
 *
 * Episodic memory records what happened. Learned knowledge records what the
 * character can currently use as a claim, with provenance, scope, authority,
 * confidence, correction links, and tiny graph edges. It is deliberately not
 * a vector DB or prose memory palace.
 */
#ifndef PE_LEARNED_KNOWLEDGE_H
#define PE_LEARNED_KNOWLEDGE_H

#include <stdint.h>
#include "sidecar.h"

#define PE_LEARNED_KNOWLEDGE_MAGIC      PE_SIDECAR_MAGIC('L','K','N','W')
#define PE_LEARNED_KNOWLEDGE_VERSION    1
#define PE_LK_RECORD_CAP                64u
#define PE_LK_EDGE_CAP                  96u
#define PE_LK_TOPIC_LEN                 32u
#define PE_LK_CLAIM_LEN                 192u
#define PE_LK_SOURCE_NAME_LEN           48u
#define PE_LK_TRACE_DIR                 "tmp/learned_knowledge"

enum {
    PE_LK_SCOPE_REAL_WORLD = 1,
    PE_LK_SCOPE_CARTRIDGE_CANON,
    PE_LK_SCOPE_SIMULATION_WORLD,
    PE_LK_SCOPE_ACTOR_SPECIFIC,
    PE_LK_SCOPE_RELATIONSHIP_SPECIFIC,
    PE_LK_SCOPE_SESSION_LOCAL,
    PE_LK_SCOPE_PRIVATE_CHARACTER_BELIEF
};

enum {
    PE_LK_SRC_CARTRIDGE = 1,
    PE_LK_SRC_WORLD,
    PE_LK_SRC_USER,
    PE_LK_SRC_CHARACTER,
    PE_LK_SRC_MODEL,
    PE_LK_SRC_SYSTEM,
    PE_LK_SRC_IMPORTED,
    PE_LK_SRC_UNKNOWN
};

enum {
    PE_LK_TIER_TEMPLATE = 1,
    PE_LK_TIER_LOCAL_MODEL,
    PE_LK_TIER_SLM,
    PE_LK_TIER_FRONTIER,
    PE_LK_TIER_OFFLINE,
    PE_LK_TIER_SYSTEM
};

enum {
    PE_LK_STATUS_PROVISIONAL = 1,
    PE_LK_STATUS_CONFIRMED,
    PE_LK_STATUS_CORRECTED,
    PE_LK_STATUS_DISPUTED,
    PE_LK_STATUS_DEPRECATED,
    PE_LK_STATUS_CARTRIDGE_AUTHORED,
    PE_LK_STATUS_WORLD_AUTHORED
};

enum {
    PE_LK_EDGE_CORRECTS = 1,
    PE_LK_EDGE_CONTRADICTS,
    PE_LK_EDGE_SUPPORTS,
    PE_LK_EDGE_DERIVED_FROM,
    PE_LK_EDGE_TAUGHT_BY,
    PE_LK_EDGE_BELONGS_TO_SCOPE,
    PE_LK_EDGE_EVIDENCED_BY,
    PE_LK_EDGE_USED_IN_RESPONSE,
    PE_LK_EDGE_RELATED_TO_ACTOR,
    PE_LK_EDGE_RELATED_TO_TOPIC
};

typedef struct {
    uint32_t record_id;
    char     topic_key[PE_LK_TOPIC_LEN];
    char     claim_text[PE_LK_CLAIM_LEN];
    uint8_t  scope;
    uint8_t  source_type;
    uint8_t  source_tier;
    uint8_t  authority_rank;
    uint32_t source_actor_id;
    char     source_actor_name[PE_LK_SOURCE_NAME_LEN];
    uint16_t confidence;
    uint8_t  status;
    uint8_t  domain_tag;
    uint32_t created_at;
    uint32_t updated_at;
    uint32_t last_used_at;
    uint16_t reinforcement_count;
    uint16_t contradiction_count;
    uint32_t correction_of_record_id;
    uint32_t evidence_ref;
    uint16_t locus_id;
    uint16_t index_hint;
    uint32_t _reserved[4];
} pe_lk_record_t;

typedef struct {
    uint32_t edge_id;
    uint32_t source_record_id;
    uint8_t  relation_type;
    uint8_t  confidence;
    uint16_t weight;
    uint32_t target_record_id;
    uint32_t created_at;
    uint32_t updated_at;
    uint32_t _reserved[2];
} pe_lk_edge_t;

typedef struct {
    pe_sidecar_header_t header;
    uint32_t edge_count;
    uint32_t next_record_id;
    uint32_t next_edge_id;
    uint32_t last_load_status;
    uint32_t _reserved[4];
    pe_lk_record_t records[PE_LK_RECORD_CAP];
    pe_lk_edge_t   edges[PE_LK_EDGE_CAP];
} pe_learned_knowledge_t;

typedef struct {
    const char *topic_key;
    const char *claim_text;
    uint8_t scope;
    uint8_t source_type;
    uint8_t source_tier;
    uint8_t status;
    uint8_t authority_rank;
    uint16_t confidence;
    uint32_t source_actor_id;
    const char *source_actor_name;
    uint32_t correction_of_record_id;
    uint32_t evidence_ref;
    uint8_t domain_tag;
} pe_lk_write_t;

#ifdef __cplusplus
extern "C" {
#endif

void pe_lk_init(pe_learned_knowledge_t *lk);
int  pe_lk_load(pe_learned_knowledge_t *lk, const char *char_dir);
int  pe_lk_save(const pe_learned_knowledge_t *lk, const char *char_dir);

uint32_t pe_lk_upsert(pe_learned_knowledge_t *lk,
                      const pe_lk_write_t *w,
                      uint32_t now_s);
uint32_t pe_lk_record_edge(pe_learned_knowledge_t *lk,
                           uint32_t source_record_id,
                           uint8_t relation_type,
                           uint32_t target_record_id,
                           uint16_t weight,
                           uint8_t confidence,
                           uint32_t now_s);
const pe_lk_record_t *pe_lk_resolve(const pe_learned_knowledge_t *lk,
                                    const char *topic_key,
                                    uint8_t scope,
                                    uint32_t actor_id,
                                    int *ambiguous);
int pe_lk_mark_used(pe_learned_knowledge_t *lk,
                    uint32_t record_id,
                    uint32_t now_s);
int pe_lk_output_repeats_corrected_claim(const pe_learned_knowledge_t *lk,
                                         const char *out,
                                         const char *topic_key);

const char *pe_lk_scope_name(uint8_t v);
const char *pe_lk_source_name(uint8_t v);
const char *pe_lk_tier_name(uint8_t v);
const char *pe_lk_status_name(uint8_t v);
const char *pe_lk_edge_name(uint8_t v);

void pe_lk_trace(const char *op,
                 const pe_lk_record_t *rec,
                 const pe_lk_edge_t *edge,
                 const char *reason,
                 uint32_t winner_id);

#ifdef __cplusplus
}
#endif
#endif
