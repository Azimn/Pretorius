/* today.c — day hash → today entry; applies its modifiers to runtime state. */
#include "persona.h"
#include "persona_internal.h"
#include <time.h>
#include <string.h>

static uint32_t day_hash(uint32_t seed){
    time_t now = time(NULL);
    struct tm t;
    /* localtime is fine; we just need a daily-stable bucket */
    localtime_r(&now, &t);
    uint32_t key = (uint32_t)((t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday);
    uint32_t h = seed ^ key;
    h ^= h << 13; h ^= h >> 17; h ^= h << 5;
    return h;
}

void pe_pick_today(Engine *eng){
    if (eng->todays.count == 0) { eng->state.today_index = 0; return; }
    uint32_t h = day_hash(eng->state.today_seed);
    uint16_t idx = (uint16_t)(h % eng->todays.count);
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
