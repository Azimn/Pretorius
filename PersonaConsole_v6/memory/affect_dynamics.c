/* affect_dynamics.c -- no-sidecar affect trajectory helpers. */
#include "affect_dynamics.h"
#include "affect_curve.h"
#include "speech_ledger.h"

static int clamp_i(int v, int lo, int hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

static int16_t class_valence(uint8_t input_class, int16_t observed){
    if (observed != 0) return observed;
    switch (input_class){
    case 1: return 45;
    case 2: return -60;
    case 4: return -80;
    case 5: return 25;
    default: return 0;
    }
}

int16_t affect_contagion_pull(int16_t current_valence,
                              uint8_t input_class,
                              int8_t arousal_pct,
                              int16_t observed_valence,
                              const pe_relation_dims_t *rel,
                              uint16_t susceptibility){
    int target = class_valence(input_class, observed_valence) * 10; /* -1000..1000 */
    int arousal = clamp_i(arousal_pct, 0, 100);
    int trust = rel ? rel->trust : 500;
    int intimacy = rel ? rel->intimacy : 0;
    int threat = rel ? rel->threat : 500;
    int sus = susceptibility ? susceptibility : 350;
    if (sus > 1000) sus = 1000;

    int scale = 40 + (trust / 20) + (intimacy / 25) - (threat > 600 ? (threat - 600) / 10 : 0);
    scale = clamp_i(scale, 0, 160);
    int delta = (int)(((int64_t)(target - current_valence)
                     * scale * sus * (40 + arousal))
                    / (1000 * 100 * 140));
    return affect_hysteresis_apply(current_valence, delta);
}

int16_t affect_forecast_topic(uint16_t topic_id,
                              const MemoryStore *mem,
                              uint16_t horizon_weight){
    if (!mem || topic_id == 0xFFFFu) return 0;
    int32_t weighted = 0;
    int32_t total = 0;
    int weight_scale = horizon_weight ? horizon_weight : 350;
    if (weight_scale > 1000) weight_scale = 1000;
    for (uint16_t i = 0; i < mem->episodic_count && i < PE_EPISODIC_MAX; ++i){
        const MemoryNode *m = &mem->episodic[i];
        if (!m->summary[0] || m->topic_id != topic_id) continue;
        int w = 20 + m->salience;
        weighted += (int32_t)m->emotion.valence * 10 * w;
        total += w;
    }
    if (!total) return 0;
    return (int16_t)clamp_i((int)(weighted / total) * weight_scale / 1000, -1000, 1000);
}

uint8_t expression_policy_decide(int16_t internal_valence,
                                 const pe_relation_dims_t *rel,
                                 uint16_t mask_threshold,
                                 uint8_t withhold_reason){
    if (withhold_reason != PE_WR_NONE) return PE_EXPR_WITHHELD;
    int trust = rel ? rel->trust : 500;
    int threat = rel ? rel->threat : 500;
    int threshold = mask_threshold ? mask_threshold : 420;
    if (trust < threshold && internal_valence < -120) return PE_EXPR_MASKED;
    if (threat > 720 && internal_valence < -80) return PE_EXPR_REDIRECTED;
    return PE_EXPR_GENUINE;
}
