/* open_loops.h -- V6 Phase 6: carried intentions.
 *
 * Open loops are small structured records for things the character meant
 * to return to: avoided questions, unfinished topics, delayed apologies,
 * and other future-facing speech intentions. They are engine-authored and
 * renderer-independent, so the character can be held accountable later
 * without treating rendered prose as memory.
 */
#ifndef PE_OPEN_LOOPS_H
#define PE_OPEN_LOOPS_H

#include <stdint.h>
#include "sidecar.h"

#define PE_OPEN_LOOPS_MAGIC     PE_SIDECAR_MAGIC('O','L','O','P')
#define PE_OPEN_LOOPS_VERSION   1
#define PE_OPEN_LOOPS_RING_SIZE ((uint32_t)PE_MAX_OPEN_LOOPS)

enum {
    PE_OL_ACTIVE = 0,
    PE_OL_RESOLVED = 1,
    PE_OL_EXPIRED = 2
};

typedef struct {
    uint32_t id;
    uint32_t target_actor_id;
    uint32_t created_turn;
    uint32_t expires_turn;
    uint16_t target_topic_id;
    uint16_t desired_speech_act;
    uint16_t urgency;
    uint16_t shame_cost;
    uint16_t avoidance_pressure;
    uint8_t  status;
    uint8_t  _reserved[7];
} pe_open_loop_t;

typedef struct {
    pe_sidecar_header_t header;
    uint32_t head;
    uint32_t next_id;
    uint32_t total_recorded;
    uint32_t _reserved[5];
    pe_open_loop_t loops[PE_OPEN_LOOPS_RING_SIZE];
} pe_open_loops_t;

#ifdef __cplusplus
extern "C" {
#endif

void pe_open_loops_init(pe_open_loops_t *ol);
int  pe_open_loops_load(pe_open_loops_t *ol, const char *char_dir);
int  pe_open_loops_save(const pe_open_loops_t *ol, const char *char_dir);

void pe_open_loops_record(pe_open_loops_t *ol,
                          uint32_t actor_id,
                          uint16_t topic_id,
                          uint16_t desired_speech_act,
                          uint16_t urgency,
                          uint16_t shame_cost,
                          uint16_t avoidance_pressure,
                          uint32_t created_turn,
                          uint32_t expires_turn);

uint32_t pe_open_loops_count(const pe_open_loops_t *ol);
uint32_t pe_open_loops_count_status(const pe_open_loops_t *ol, uint8_t status);
const pe_open_loop_t *pe_open_loops_latest_active(const pe_open_loops_t *ol);
const pe_open_loop_t *pe_open_loops_latest_for_actor(const pe_open_loops_t *ol,
                                                     uint32_t actor_id);
uint16_t pe_open_loop_pressure(const pe_open_loop_t *loop,
                               uint32_t turn_count);

uint32_t pe_open_loops_expire_to(pe_open_loops_t *ol, uint32_t turn_count);
uint32_t pe_open_loops_resolve_topic(pe_open_loops_t *ol,
                                     uint32_t actor_id,
                                     uint16_t topic_id,
                                     uint32_t turn_count);

#ifdef __cplusplus
}
#endif
#endif
