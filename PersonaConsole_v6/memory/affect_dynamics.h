/* affect_dynamics.h -- V6 contagion and topic forecast helpers. */
#ifndef PE_AFFECT_DYNAMICS_H
#define PE_AFFECT_DYNAMICS_H

#include <stdint.h>
#include "persona.h"

#ifdef __cplusplus
extern "C" {
#endif

int16_t affect_contagion_pull(int16_t current_valence,
                              uint8_t input_class,
                              int8_t arousal_pct,
                              int16_t observed_valence,
                              const pe_relation_dims_t *rel,
                              uint16_t susceptibility);

int16_t affect_forecast_topic(uint16_t topic_id,
                              const MemoryStore *mem,
                              uint16_t horizon_weight);

uint8_t expression_policy_decide(int16_t internal_valence,
                                 const pe_relation_dims_t *rel,
                                 uint16_t mask_threshold,
                                 uint8_t withhold_reason);

#ifdef __cplusplus
}
#endif
#endif
