#include "persona.h"
#include "identity.h"
#include "persona_internal.h"
#include "engine_clock.h"
#include <stdlib.h>

int pe_allow_intimate_address(const Engine *eng){
    if (!eng) return 0;
    if (eng->relation.disposition < 600) return 0;
    if (schema_get(&eng->schema, SCHEMA_USER_INTIMATE) < 200) return 0;
    if (schema_get(&eng->schema, SCHEMA_USER_HOSTILE) > 400) return 0;

    uint32_t now = pe_clock_now_s();
    if (eng->relation.first_contact == 0) return 0;
    if (now <= eng->relation.first_contact) return 0;
    if (now - eng->relation.first_contact < (7u * 86400u)) return 0;
    return 1;
}

void identity_update_rolling(Engine *eng, int16_t valence, int16_t arousal){
    eng->recent_valence_sum += valence;
    eng->recent_arousal_sum += arousal;
    eng->recent_count++;
    if (eng->identity_boost_remaining > 0)
        eng->identity_boost_remaining--;

    if (eng->recent_count >= 50){
        int16_t avg_valence = (int16_t)(eng->recent_valence_sum / 50);
        int16_t avg_arousal = (int16_t)(eng->recent_arousal_sum / 50);
        if (abs(avg_valence - eng->baseline_valence) > 15
            || abs(avg_arousal - eng->baseline_arousal) > 15){
            eng->identity_boost_remaining = 5;
        }
        eng->recent_valence_sum = 0;
        eng->recent_arousal_sum = 0;
        eng->recent_count = 0;
    }
}
