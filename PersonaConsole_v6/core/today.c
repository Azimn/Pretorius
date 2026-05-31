/* today.c — day hash → today entry; applies its modifiers to runtime state. */
#include "persona.h"
#include "persona_internal.h"
#include "engine_clock.h"
#include <string.h>
#include <stdlib.h>

static uint32_t day_hash(uint32_t seed){
    /* Canonical day bucket — packed YYYYMMDD from the engine clock. */
    uint32_t key = pe_clock_today_key();
    uint32_t h = seed ^ key;
    h ^= h << 13; h ^= h >> 17; h ^= h << 5;
    return h;
}

static int hour_now(void){
    /* Legacy fixture override (predates the engine clock); kept so existing
     * time-of-day tests keep working unchanged. New fixtures should pin
     * the clock via PE_CLOCK_OVERRIDE_MS, which is honored by the fall-
     * through to pe_clock_local_hour(). */
    const char *env = getenv("PE_TEST_HOUR");
    if (env && env[0]){
        char *end = NULL;
        long h = strtol(env, &end, 10);
        if (end && *end == '\0' && h >= 0 && h <= 23)
            return (int)h;
    }
    return (int)pe_clock_local_hour();
}

static int today_band_weight(const char *label, int hour){
    if (!label || !label[0]) return 1;
    int match = 0;
    if (hour >= 6 && hour < 10){
        match = (strstr(label, "composed") || strstr(label, "restless")
              || strstr(label, "convalescent"));
    } else if (hour >= 10 && hour < 14){
        match = (strstr(label, "lectur") || strstr(label, "work")
              || strstr(label, "expansive"));
    } else if (hour >= 14 && hour < 18){
        match = (strstr(label, "expansive") || strstr(label, "bright")
              || strstr(label, "grandiose"));
    } else if (hour >= 18 && hour < 22){
        match = (strstr(label, "tipsy") || strstr(label, "intimate")
              || strstr(label, "evening"));
    } else {
        match = (strstr(label, "theatrical") || strstr(label, "manic")
              || strstr(label, "drunk") || strstr(label, "paranoid"));
    }
    return match ? 4 : 1;
}

void pe_pick_today(Engine *eng){
    if (eng->todays.count == 0) { eng->state.today_index = 0; return; }
    uint32_t h = day_hash(eng->state.today_seed);
    int hour = hour_now();
    uint32_t weights[PE_TODAY_MAX];
    uint32_t total = 0;
    for (uint32_t i = 0; i < eng->todays.count && i < PE_TODAY_MAX; ++i){
        weights[i] = (uint32_t)today_band_weight(eng->todays.entries[i].label, hour);
        total += weights[i];
    }
    uint32_t r = total ? (h % total) : 0;
    uint16_t idx = 0;
    for (uint32_t i = 0; i < eng->todays.count && i < PE_TODAY_MAX; ++i){
        if (r < weights[i]){
            idx = (uint16_t)i;
            break;
        }
        r -= weights[i];
    }
    if (idx == eng->state.today_index && eng->state.session_start_time != 0) {
        /* already applied today — modifiers persist via state.bin */
        return;
    }
    eng->state.today_index = idx;
    const TodayEntry *td = &eng->todays.entries[idx];
    /* apply modifiers */
    eng->state.mood = pe_clamp16(eng->state.mood + td->mood_modifier, -1000, 1000);
    for (int i = 0; i < PE_DRIVE_COUNT; ++i)
        eng->state.drive_values[i] = pe_clamp16(
            eng->state.drive_values[i] + td->drive_modifiers[i], 0, 1000);
    if (td->goal_override != 0xFFFF) eng->state.current_goal = td->goal_override;
}
