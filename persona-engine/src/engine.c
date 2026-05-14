/* engine.c — process_input main loop, drives, mood, lifecycle. */
#include "persona.h"
#include "persona_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ---------- load helpers ---------- */
static int load_or_zero(const char *dir, const char *name, void *buf, size_t n){
    char path[512];
    if (pe_path_join(path, sizeof(path), dir, name) != 0) return -1;
    if (pe_read_file(path, buf, n) == 0) return 1;
    memset(buf, 0, n);
    return 0;
}

static int load_required(const char *dir, const char *name, void *buf, size_t n){
    char path[512];
    if (pe_path_join(path, sizeof(path), dir, name) != 0) return -1;
    return pe_read_file(path, buf, n);
}

static void seed_drives(Engine *eng){
    /* drives start near their baseline */
    for (int i = 0; i < PE_DRIVE_COUNT; ++i)
        eng->state.drive_values[i] = eng->drives.drives[i].baseline;
}

/* ---------- pe_decay_drives ---------- */
void pe_decay_drives(Engine *eng, uint32_t delta_ms){
    /* decay = delta_ms * decay_per_minute / 60000, tugged toward baseline */
    for (int i = 0; i < PE_DRIVE_COUNT; ++i){
        const DriveDef *d = &eng->drives.drives[i];
        int32_t step = ((int32_t)delta_ms * d->decay_per_minute) / 60000;
        /* trait acceleration: neuroticism speeds repose, extraversion stimulation, etc. */
        int32_t traits[5] = {
            eng->identity.openness,        eng->identity.conscientiousness,
            eng->identity.extraversion,    eng->identity.agreeableness,
            eng->identity.neuroticism
        };
        int32_t accel = 0;
        for (int k = 0; k < 5; ++k)
            accel += (traits[k] * d->personality_weight[k]) >> 16; /* 0.16 fixed */
        step += (step * accel) / 0x1000;

        int32_t v = eng->state.drive_values[i];
        if (v > d->baseline) v -= step > 0 ? step : -step;
        else if (v < d->baseline) v += step > 0 ? step : -step;
        eng->state.drive_values[i] = pe_clamp16(v, 0, 1000);
    }
    /* fatigue creeps up over time, repose drains it */
    int32_t f = eng->state.fatigue + (int32_t)(delta_ms / 30000);
    f -= eng->state.drive_values[PE_DRIVE_REPOSE] / 100;
    eng->state.fatigue = pe_clamp16(f, 0, 1000);
}

/* ---------- mood (saturating fixed-point) ---------- */
void pe_compute_mood(Engine *eng){
    /* mood is a weighted sum of drive deltas-from-baseline plus last-emotion bleed */
    int32_t m = 0;
    static const int8_t drive_mood_w[PE_DRIVE_COUNT] = {
        +6, +5, +3, +4, +2, +3, -7, +1
    };
    for (int i = 0; i < PE_DRIVE_COUNT; ++i){
        int32_t delta = eng->state.drive_values[i] - eng->drives.drives[i].baseline;
        m += delta * drive_mood_w[i];
    }
    m /= 8;
    /* neuroticism amplifies negative excursions */
    if (m < 0) m = m - (m * eng->identity.neuroticism) / (int32_t)0x20000;

    /* emotional bleed from last input — gated by voice_flags */
    if (eng->identity.voice_flags & PE_VF_ALLOW_BLEED){
        m += (int32_t)eng->state.last_input_emotion.valence * 2;
        m -= (int32_t)eng->state.last_input_emotion.arousal / 4;
    }
    /* fatigue depresses mood */
    m -= eng->state.fatigue / 4;
    eng->state.mood = pe_clamp16(m, -1000, 1000);
}

/* ---------- drives respond to classified input ---------- */
void pe_update_drives_from_input(Engine *eng){
    int16_t *v = eng->state.drive_values;
    switch (eng->input_class) {
    case 1: /* praise */
        v[PE_DRIVE_RECOGNITION] = pe_clamp16(v[PE_DRIVE_RECOGNITION] - 150, 0, 1000);
        v[PE_DRIVE_COMMUNION]   = pe_clamp16(v[PE_DRIVE_COMMUNION]   - 60,  0, 1000);
        v[PE_DRIVE_VINDICATION] = pe_clamp16(v[PE_DRIVE_VINDICATION] - 40,  0, 1000);
        break;
    case 2: /* insult */
        v[PE_DRIVE_RECOGNITION] = pe_clamp16(v[PE_DRIVE_RECOGNITION] + 250, 0, 1000);
        v[PE_DRIVE_VINDICATION] = pe_clamp16(v[PE_DRIVE_VINDICATION] + 300, 0, 1000);
        v[PE_DRIVE_PROVOCATION] = pe_clamp16(v[PE_DRIVE_PROVOCATION] + 120, 0, 1000);
        v[PE_DRIVE_AUTONOMY]    = pe_clamp16(v[PE_DRIVE_AUTONOMY]    + 80,  0, 1000);
        eng->state.paranoia     = pe_clamp16(eng->state.paranoia     + 60,  0, 1000);
        break;
    case 3: /* question */
        v[PE_DRIVE_STIMULATION] = pe_clamp16(v[PE_DRIVE_STIMULATION] - 60,  0, 1000);
        v[PE_DRIVE_RECOGNITION] = pe_clamp16(v[PE_DRIVE_RECOGNITION] - 25,  0, 1000);
        break;
    case 4: /* threat */
        v[PE_DRIVE_VINDICATION] = pe_clamp16(v[PE_DRIVE_VINDICATION] + 200, 0, 1000);
        v[PE_DRIVE_AUTONOMY]    = pe_clamp16(v[PE_DRIVE_AUTONOMY]    + 200, 0, 1000);
        v[PE_DRIVE_CONTINUITY]  = pe_clamp16(v[PE_DRIVE_CONTINUITY]  + 100, 0, 1000);
        eng->state.paranoia     = pe_clamp16(eng->state.paranoia     + 200, 0, 1000);
        break;
    case 5: /* intimacy */
        v[PE_DRIVE_COMMUNION]   = pe_clamp16(v[PE_DRIVE_COMMUNION]   - 200, 0, 1000);
        v[PE_DRIVE_PROVOCATION] = pe_clamp16(v[PE_DRIVE_PROVOCATION] - 40,  0, 1000);
        break;
    default: /* neutral — drives crawl back per decay only */
        break;
    }
    /* every turn nudges stimulation upward (boredom) */
    v[PE_DRIVE_STIMULATION] = pe_clamp16(v[PE_DRIVE_STIMULATION] + 15, 0, 1000);
}

/* ---------- response delay (illusion layer) ---------- */
static int compute_delay(Engine *eng){
    if (!(eng->identity.voice_flags & PE_VF_DELAY_TIMING)) return 0;
    int arousal = eng->state.last_input_emotion.arousal;
    int volatility = (eng->state.mood < 0 ? -eng->state.mood : eng->state.mood) / 200;
    int delay = 300 + arousal * 5 + volatility * 3;
    int jitter = (int)(persona_rng_u32(&eng->state) % 120) - 60;
    delay += jitter;
    if (delay < 80) delay = 80;
    if (delay > 2000) delay = 2000;
    return delay;
}

/* ---------- public API ---------- */
int persona_open(Engine *eng, const char *character_dir){
    memset(eng, 0, sizeof(*eng));
    snprintf(eng->char_dir, sizeof(eng->char_dir), "%s", character_dir);

    char sub[512];

    if (load_required(character_dir, "identity.bin", &eng->identity, sizeof(Identity)) != 0) {
        fprintf(stderr, "persona: failed to load identity.bin from %s\n", character_dir);
        return -1;
    }
    if (load_required(character_dir, "drives.bin",   &eng->drives,   sizeof(DriveTable)) != 0)  return -2;
    if (load_required(character_dir, "today.bin",    &eng->todays,   sizeof(TodayTable)) != 0)  return -3;

    pe_path_join(sub, sizeof(sub), character_dir, "dialogue/patterns.bin");
    if (pe_read_file(sub, &eng->patterns,  sizeof(PatternTable)) != 0) return -4;
    pe_path_join(sub, sizeof(sub), character_dir, "dialogue/templates.bin");
    if (pe_read_file(sub, &eng->templates, sizeof(TemplateTable)) != 0) return -5;
    pe_path_join(sub, sizeof(sub), character_dir, "dialogue/fallback.bin");
    if (pe_read_file(sub, &eng->fallbacks, sizeof(FallbackTable)) != 0) return -6;
    pe_path_join(sub, sizeof(sub), character_dir, "dialogue/topics.bin");
    if (pe_read_file(sub, &eng->topics,    sizeof(TopicTable)) != 0)    return -7;
    pe_path_join(sub, sizeof(sub), character_dir, "dialogue/goals.bin");
    if (pe_read_file(sub, &eng->goals,     sizeof(GoalTable)) != 0)     return -8;

    /* mutable state */
    int had_state = load_or_zero(character_dir, "state.bin",  &eng->state,  sizeof(NPCState));
    int had_mem   = load_or_zero(character_dir, "memory.bin", &eng->memory, sizeof(MemoryStore));

    if (!had_state) {
        seed_drives(eng);
        eng->state.mood = 0;
        eng->state.trust_user = 400;
        eng->state.paranoia = 200;
        eng->state.today_seed = (uint32_t)time(NULL);
        eng->state.session_start_time = persona_now_ms();
        eng->state.last_update_time   = eng->state.session_start_time;
        eng->state.rng_state = eng->state.today_seed ? eng->state.today_seed : 0xC0FFEEu;
    }
    if (!had_mem) {
        /* seed core memories from identity */
        for (uint8_t i = 0; i < eng->identity.core_memory_count && i < PE_EPISODIC_MAX; ++i){
            eng->memory.episodic[i] = eng->identity.core_memories_seed[i];
            eng->memory.episodic[i].core_memory = 1;
            eng->memory.episodic[i].id = i + 1;
            eng->memory.episodic_count = i + 1;
        }
        eng->memory.next_memory_id = eng->memory.episodic_count + 1;
    }

    pe_pick_today(eng);
    return 0;
}

int persona_set_user(Engine *eng, const char *user_id){
    return pe_load_relation(eng, user_id);
}

int persona_save(Engine *eng){
    char p[512];
    pe_path_join(p, sizeof(p), eng->char_dir, "state.bin");
    if (pe_write_file_atomic(p, &eng->state, sizeof(NPCState)) != 0) return -1;
    pe_path_join(p, sizeof(p), eng->char_dir, "memory.bin");
    if (pe_write_file_atomic(p, &eng->memory, sizeof(MemoryStore)) != 0) return -1;
    pe_save_relation(eng);
    return 0;
}

int persona_process_input(Engine *eng,
                          const char *user_id,
                          const char *input_text,
                          char *out, size_t n)
{
    if (!eng || !input_text || !out || n == 0) return -1;

    /* 1. relation */
    pe_load_relation(eng, user_id);

    /* 2. time delta + decay */
    uint32_t now = persona_now_ms();
    uint32_t delta = now - eng->state.last_update_time;
    if (delta > 3600u*1000u) delta = 3600u*1000u; /* cap one hour */
    pe_decay_drives(eng, delta);
    pe_decay_episodic(eng);
    pe_repetition_decay(eng);

    /* fold turn count + today seed into rng — guarantees deterministic replay */
    eng->state.turn_count++;
    eng->state.rng_state ^= eng->state.today_seed + eng->state.turn_count * 2654435761u;

    /* 3. emotional fingerprint */
    EmotionVector ev = {0};
    pe_classify_input(eng, input_text, &ev);
    eng->state.prev_input_emotion = eng->state.last_input_emotion;
    eng->state.last_input_emotion = ev;

    /* 4. associative recall */
    pe_associative_recall(eng, &ev);

    /* 5. drive update */
    pe_update_drives_from_input(eng);

    /* 6. mood */
    pe_compute_mood(eng);

    /* 7. topic momentum */
    pe_update_topic_momentum(eng);

    /* 8. goal arbitration (with 2-turn hysteresis except on interrupt) */
    uint16_t prev_goal = eng->state.current_goal;
    int interrupt = (eng->input_class == 2 || eng->input_class == 4); /* insult/threat */
    if (eng->state.goal_hysteresis == 0 || interrupt) {
        eng->state.current_goal = pe_select_goal(eng);
        if (eng->state.current_goal != prev_goal)
            eng->state.goal_hysteresis = 2;
    } else {
        eng->state.goal_hysteresis--;
    }

    /* 9. intent */
    eng->state.current_intent = pe_select_intent(eng, eng->state.current_goal, &ev);

    /* 10–11. candidates, repetition, transforms, select */
    pe_generate_response(eng, input_text, out, n);

    /* 12. illusion: schedule delay (caller may sleep if desired) */
    eng->scheduled_delay_ms = compute_delay(eng);

    /* 13. memory commit */
    {
        EmotionVector prev = eng->state.prev_input_emotion;
        int32_t drive_impact = 0;
        for (int i = 0; i < PE_DRIVE_COUNT; ++i)
            drive_impact += (eng->state.drive_values[i] > eng->drives.drives[i].baseline
                             ? eng->state.drive_values[i] - eng->drives.drives[i].baseline
                             : eng->drives.drives[i].baseline - eng->state.drive_values[i]);
        int identity_threat = (eng->input_class == 2 || eng->input_class == 4);
        uint8_t s = pe_compute_salience(eng, &prev, &ev, drive_impact,
                                        identity_threat, 0,
                                        identity_threat ? 100 : 30);
        char summary[PE_MEM_SUMMARY_LEN];
        snprintf(summary, sizeof(summary), "%s said: %.60s",
                 eng->relation.known_as[0] ? eng->relation.known_as : "stranger",
                 input_text);
        pe_commit_memory(eng, summary, &ev, eng->primary_topic, s, identity_threat);
        pe_push_short_term(eng, (uint8_t)eng->input_class, input_text);
    }

    /* 14. relation update + save */
    {
        int delta_disp = 0;
        switch (eng->input_class){
        case 1: delta_disp = +12; break;
        case 2: delta_disp = -40; break;
        case 4: delta_disp = -70; break;
        case 5: delta_disp = +30; break;
        default: delta_disp = +1; break;
        }
        eng->relation.disposition = pe_clamp16(eng->relation.disposition + delta_disp, 0, 1000);
        eng->relation.last_contact = (uint32_t)time(NULL);
        if (eng->relation.disposition > 700) {
            eng->relation.tags |= PE_TAG_CONFIDANT;
            eng->relation.tags &= ~PE_TAG_STRANGER;
        }
        if (eng->relation.disposition < 200) {
            eng->relation.tags |= PE_TAG_BENEATH_CONTEMPT;
        }
    }

    eng->state.last_update_time = now;
    persona_save(eng);
    return (int)strlen(out);
}

void persona_debug_dump(const Engine *eng){
    static const char *drive_names[PE_DRIVE_COUNT] = {
        "Recognition","Stimulation","Provocation","Communion",
        "Autonomy","Continuity","Vindication","Repose"
    };
    static const char *intent_names[PE_INTENT_COUNT] = {
        "answer","evade","accuse","flatter","threaten","probe",
        "redirect","monologue","reminisce","withdraw","joke","boast"
    };
    fprintf(stderr, "\n--- persona dump (turn=%u) ---\n", eng->state.turn_count);
    fprintf(stderr, "today=%u(%s) seed=0x%08x mood=%d fatigue=%d paranoia=%d trust=%d\n",
            eng->state.today_index,
            eng->todays.entries[eng->state.today_index].label,
            eng->state.today_seed,
            eng->state.mood, eng->state.fatigue, eng->state.paranoia,
            eng->state.trust_user);
    for (int i = 0; i < PE_DRIVE_COUNT; ++i)
        fprintf(stderr, "  %-12s %4d / baseline %4d\n",
                drive_names[i], eng->state.drive_values[i], eng->drives.drives[i].baseline);
    uint16_t gi = eng->state.current_goal;
    fprintf(stderr, "goal=%u(%s) intent=%u(%s) hysteresis=%u input_class=%d primary_topic=%u\n",
            gi, gi < eng->goals.count ? eng->goals.entries[gi].name : "?",
            eng->state.current_intent,
            eng->state.current_intent < PE_INTENT_COUNT ? intent_names[eng->state.current_intent] : "?",
            eng->state.goal_hysteresis, eng->input_class, eng->primary_topic);
    fprintf(stderr, "topic_momentum: ");
    for (int i = 0; i < PE_TOPIC_SLOTS; ++i)
        if (eng->state.topic_momentum[i].momentum > 0)
            fprintf(stderr, "%u=%u ",
                    eng->state.topic_momentum[i].topic_id,
                    eng->state.topic_momentum[i].momentum);
    fprintf(stderr, "\n");
    fprintf(stderr, "active_memories=%u relation(user=%08x disp=%d tags=0x%02x)\n",
            eng->active_count, eng->relation.user_hash, eng->relation.disposition,
            eng->relation.tags);
    fprintf(stderr, "episodic_count=%u next_id=%u short_term_pos=%u\n",
            eng->memory.episodic_count, eng->memory.next_memory_id,
            eng->memory.short_term_pos);
    fprintf(stderr, "--- end dump ---\n\n");
}
