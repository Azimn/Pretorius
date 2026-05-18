#include "persona.h"
#include "identity.h"
#include <stdlib.h>

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
