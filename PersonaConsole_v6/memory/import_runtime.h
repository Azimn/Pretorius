#ifndef PE_IMPORT_RUNTIME_H
#define PE_IMPORT_RUNTIME_H

#include "persona.h"
#include "learned_knowledge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *summary;
    const char *topic_key;
    const char *actor_name;
    int salience;
    int emotional_impact;
    int is_core;
    int is_pinned;
} pe_import_memory_record_t;

typedef struct {
    const char *actor_name;
    uint16_t trust;
    uint16_t threat;
    uint16_t intimacy;
    uint16_t resentment;
    uint16_t dependency;
    uint16_t obligation;
    uint16_t envy;
    uint16_t admiration;
    uint16_t embarrassment;
} pe_import_relationship_record_t;

typedef struct {
    const char *actor_name;
    const char *topic_key;
    const char *desired_speech_act;
    uint16_t urgency;
    uint16_t shame_cost;
    uint16_t avoidance_pressure;
} pe_import_open_loop_record_t;

int pe_import_memory_record(Engine *eng,
                            const pe_import_memory_record_t *rec,
                            uint32_t *memory_id_out);
uint32_t pe_import_learned_record(Engine *eng,
                                  const pe_lk_write_t *w);
uint32_t pe_import_learned_edge(Engine *eng,
                                uint32_t source_record_id,
                                uint8_t relation_type,
                                uint32_t target_record_id,
                                uint16_t weight,
                                uint8_t confidence);
int pe_import_relationship_record(Engine *eng,
                                  const pe_import_relationship_record_t *rec);
int pe_import_open_loop_record(Engine *eng,
                               const pe_import_open_loop_record_t *rec,
                               uint32_t *loop_id_out);
int pe_import_topic_id_for_key(const Engine *eng,
                               const char *topic_key,
                               uint16_t *topic_id_out);
int pe_import_text_safe(const char *text);

#ifdef __cplusplus
}
#endif

#endif
