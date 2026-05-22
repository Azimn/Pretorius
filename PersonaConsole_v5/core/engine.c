/* engine.c — process_input main loop, drives, mood, lifecycle. */
#include "persona.h"
#include "persona_internal.h"
#include "cartridge.h"
#include "ngram_lm.h"            /* v2.1: optional plasticity */
#include "mutator.h"             /* v3.2: cartridge banks */
#include "aether.h"              /* v3.2: long-term episodic storage */
#include "identity.h"
#include "environment.h"
#include "../render/render_backend.h"   /* v4: renderer dispatch */
#include "../memory/affect_curve.h"     /* v4: nonlinear affect */
#include "../instrumentation/state_trace.h"  /* v4: observability */
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

static int load_static_section(const char *root, int is_cart,
                               const char *name, void *buf, size_t n){
    if (is_cart) return pe_cart_load_section(root, name, buf, n);
    return load_required(root, name, buf, n);
}

static const char *topic_name_by_id(const Engine *eng, uint16_t topic_id){
    if (topic_id == 0xFFFF) return NULL;
    for (uint32_t i = 0; i < eng->topics.count; ++i)
        if (eng->topics.topics[i].id == topic_id)
            return eng->topics.topics[i].name;
    return NULL;
}

static const char *input_class_label(int input_class){
    switch (input_class){
    case 1: return "praise";
    case 2: return "insult";
    case 3: return "question";
    case 4: return "threat";
    case 5: return "intimacy";
    default: return NULL;
    }
}

static MemoryNode *recent_topic_memory(Engine *eng, uint16_t topic_id, uint16_t window){
    if (topic_id == 0xFFFF) return NULL;
    MemoryStore *m = &eng->memory;
    uint16_t n = m->episodic_count;
    uint16_t start = n > window ? (uint16_t)(n - window) : 0;
    for (uint16_t i = n; i > start; --i){
        MemoryNode *node = &m->episodic[i - 1];
        if (node->memory_type != MEM_CORE && node->topic_id == topic_id)
            return node;
    }
    return NULL;
}

static void pe_prime_unprompted_memory(Engine *eng){
    if (!eng) return;
    if (eng->state.turns_since_unprompted_recall < 0xFFFFu)
        eng->state.turns_since_unprompted_recall++;
    if (eng->state.turns_since_unprompted_recall < 12) return;
    if ((persona_rng_u32(&eng->state) & 0xFFu) >= 40u) return;

    MemoryNode *best = NULL;
    int best_score = 0;
    for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
        MemoryNode *m = &eng->memory.episodic[i];
        if (m->core_memory || m->memory_type == MEM_CORE) continue;
        if (m->retrieval_prob > 100) continue;
        int stale = 200 - (int)m->retrieval_prob;
        int score = (int)m->salience * 2 + stale;
        if (score > best_score){
            best_score = score;
            best = m;
        }
    }
    if (best){
        best->retrieval_prob = 240;
        eng->state.turns_since_unprompted_recall = 0;
    }
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

        /* V4 priority 4: salience-weighted decay.  Drives far from
         * baseline carry "emotional inertia" — they decay slower than
         * mild perturbations, matching how affect actually persists.
         * The shape comes from affect_decay (squared inverse salience),
         * applied to the signed delta-from-baseline. */
        int32_t v = eng->state.drive_values[i];
        int32_t delta = v - d->baseline;
        if (step != 0 && delta != 0){
            int abs_delta = delta < 0 ? -delta : delta;
            int salience = abs_delta;            /* 0..1000 */
            /* rate is the linear step expressed as per-mille of full
             * range — affect_decay's domain expects 0..1000 base rate */
            int base_rate = (int)((step * 1000) / 1000);
            if (base_rate < 1) base_rate = 1;
            if (base_rate > 1000) base_rate = 1000;
            int16_t new_delta = affect_decay((int16_t)delta,
                                             (int16_t)salience,
                                             base_rate);
            v = (int32_t)d->baseline + new_delta;
        }
        eng->state.drive_values[i] = pe_clamp16(v, 0, 1000);
    }
    /* fatigue creeps up over time, repose drains it */
    int32_t f = eng->state.fatigue + (int32_t)(delta_ms / 30000);
    f -= eng->state.drive_values[PE_DRIVE_REPOSE] / 100;
    eng->state.fatigue = pe_clamp16(f, 0, 1000);
}

/* ---------- mood (saturating fixed-point) ---------- */
void pe_compute_mood(Engine *eng){
    /* mood is a weighted sum of drive deltas-from-baseline plus last-emotion bleed.
     * Weights are now data-driven (DriveDef.mood_weight), supplied by each
     * character's drives.bin — no hardcoded per-character coefficients here. */
    int32_t m = 0;
    for (int i = 0; i < PE_DRIVE_COUNT; ++i){
        int32_t delta = eng->state.drive_values[i] - eng->drives.drives[i].baseline;
        m += delta * eng->drives.drives[i].mood_weight;
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

    /* V4 priority 4: mood hysteresis.  Compute the target mood from
     * drives + bleed + fatigue as before, then move toward it via
     * affect_hysteresis_apply rather than snapping.  This prevents
     * the "emotional pinball" failure mode where mood flips with
     * every input — replaces it with emotional inertia. */
    int target = (int)pe_clamp16(m, -1000, 1000);
    int delta  = target - (int)eng->state.mood;
    eng->state.mood = affect_hysteresis_apply(eng->state.mood, delta);
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

/* ---------- v2: embodiment ----------
 *
 *   intoxication        rises on PE_PATTERN_FLAG_INTOXICANT match, slow decay
 *   exhaustion          rises per turn (boosted by aggression/spike), drained by repose
 *   irritation_carry    rises on insult/threat, slow decay (5/min); bleeds into mood
 *   physical_fragility  monotone slow climb, capped 1000; resets only on save
 *   fixation_*          locks current_intent toward MONOLOGUE/REMINISCE for N turns
 */
void pe_update_embodiment(Engine *eng, uint32_t delta_ms){
    NPCState *s = &eng->state;
    int32_t mins = (int32_t)(delta_ms / 60000u);

    /* intoxication decay */
    int32_t v = s->intoxication - mins * 8;
    s->intoxication = pe_clamp16(v, 0, 1000);

    /* data-driven intoxicant trigger: any matched Pattern with
     * PE_PATTERN_FLAG_INTOXICANT raises intoxication.  Character-agnostic. */
    if (s->last_matched_flags & PE_PATTERN_FLAG_INTOXICANT){
        s->intoxication = pe_clamp16(s->intoxication + 180, 0, 1000);
    }

    /* exhaustion: +5/turn baseline, +arousal effect, drained by repose drive */
    int32_t e = s->exhaustion + 5
              + (s->last_input_emotion.arousal / 8)
              - (s->drive_values[PE_DRIVE_REPOSE] / 80);
    s->exhaustion = pe_clamp16(e, 0, 1000);

    /* irritation_carry: spike on insult/threat, slow decay */
    int32_t ic = s->irritation_carry - mins * 5;
    if (eng->input_class == 2) ic += 220;
    if (eng->input_class == 4) ic += 320;
    s->irritation_carry = pe_clamp16(ic, 0, 1000);

    /* physical_fragility: +1/turn (capped) — slow theatrical collapse over long sessions */
    if (s->physical_fragility < 1000) s->physical_fragility++;

    /* fixation lock decay */
    if (s->fixation_remaining > 0) s->fixation_remaining--;
    if (s->fixation_remaining == 0) {
        s->fixation_topic = 0xFFFF;
        s->fixation_strength = 0;
    } else {
        /* fixation strength fades toward 0 over its lifetime */
        s->fixation_strength = pe_clamp16(s->fixation_strength - 8, 0, 1000);
    }

    /* trigger a new fixation: obsession topic momentum exceeds 800 → lock for 6 turns */
    if (s->fixation_topic == 0xFFFF){
        for (int i = 0; i < PE_TOPIC_SLOTS; ++i){
            uint16_t tid = s->topic_momentum[i].topic_id;
            uint16_t mom = s->topic_momentum[i].momentum;
            if (mom < 800) continue;
            for (int k = 0; k < PE_OBSESSION_COUNT; ++k){
                if (eng->identity.obsessions[k] == tid){
                    s->fixation_topic     = tid;
                    s->fixation_strength  = 900;
                    s->fixation_remaining = 6;
                    break;
                }
            }
            if (s->fixation_topic != 0xFFFF) break;
        }
    }

    /* recovery_curve: counts down whenever no acute event occurred this turn */
    if (eng->input_class == 2 || eng->input_class == 4) s->recovery_curve = 6;
    else if (s->recovery_curve > 0) s->recovery_curve--;
}

/* ---------- v2: layered affect ----------
 *
 *   baseline_temperament  identity-derived, set once per session
 *   acute_spike           sharp short-lived shock from input emotion
 *   suppression_mask      how much external mood differs from internal (agreeableness)
 *   obsession_pressure    rises when obsession topics get no recent momentum
 */
void pe_update_layered_affect(Engine *eng){
    NPCState *s = &eng->state;

    /* baseline_temperament: ext - neu - agree*0.5 → range roughly -500..+500 */
    s->baseline_temperament = pe_clamp16(
        ((int32_t)eng->identity.extraversion  / 128)
      - ((int32_t)eng->identity.neuroticism   / 128)
      - ((int32_t)eng->identity.agreeableness / 256),
        -1000, 1000);

    /* acute_spike: respond to current input emotion (valence * arousal),
     * decay toward 0 at ~25%/turn */
    int32_t spike = s->acute_spike;
    spike = (spike * 3) / 4;
    spike += ((int32_t)s->last_input_emotion.valence
            * (int32_t)s->last_input_emotion.arousal) / 10;
    /* irritation carry sustains it negative */
    spike -= s->irritation_carry / 4;
    s->acute_spike = pe_clamp16(spike, -1000, 1000);

    /* suppression_mask: high agreeableness → high suppression of negative spikes */
    int32_t mask = ((int32_t)eng->identity.agreeableness / 80);
    /* defensive stance amplifies suppression — but only if not already exploded */
    if (s->irritation_carry < 700) mask += s->paranoia / 5;
    /* intoxication lowers suppression */
    mask -= s->intoxication / 4;
    s->suppression_mask = pe_clamp16(mask, 0, 1000);

    /* obsession_pressure is weighted by per-cartridge obsession strength.
     * A zero strength means legacy/default 50, preserving older carts. */
    int32_t pressure = s->obsession_pressure;
    for (int i = 0; i < PE_OBSESSION_COUNT; ++i){
        uint16_t o = eng->identity.obsessions[i];
        if (!o) break;
        uint8_t strength = eng->identity.obsession_strength[i]
                         ? eng->identity.obsession_strength[i] : 50;
        int mom = 0;
        for (int j = 0; j < PE_TOPIC_SLOTS; ++j)
            if (s->topic_momentum[j].topic_id == o){ mom = s->topic_momentum[j].momentum; break; }
        if (mom < 200) pressure += 10 + (int)((uint32_t)strength * 40u / 100u);
        if (mom > 700) pressure -= 15 + (int)((uint32_t)strength * 70u / 100u);
    }
    pressure -= 4;  /* slow self-decay */
    s->obsession_pressure = pe_clamp16(pressure, 0, 1000);

    /* mood is then re-tweaked by acute_spike & suppression (acute leaks past suppression) */
    int32_t bleed = (s->acute_spike * (1000 - s->suppression_mask)) / 4000;
    s->mood = pe_clamp16(s->mood + bleed + s->baseline_temperament / 8
                       - s->irritation_carry / 8
                       + s->intoxication / 20, -1000, 1000);
}

/* ---------- v2: trace ring ---------- */
void pe_trace_push(Engine *eng){
    NPCState *s = &eng->state;
    TraceEntry *t = &s->trace[s->trace_pos];
    t->turn             = s->turn_count;
    t->mood             = s->mood;
    t->acute_spike      = s->acute_spike;
    t->intoxication     = s->intoxication;
    t->exhaustion       = s->exhaustion;
    t->irritation_carry = s->irritation_carry;
    t->obsession_pressure = s->obsession_pressure;
    t->goal             = s->current_goal;
    t->intent           = s->current_intent;
    t->rhetorical_mode  = s->last_rhetorical_mode;
    t->primary_topic    = eng->primary_topic;
    t->fixation_topic   = s->fixation_topic;
    t->input_class      = (uint8_t)eng->input_class;
    t->negation_flag    = eng->negation_active;
    s->trace_pos        = (uint8_t)((s->trace_pos + 1) % PE_TRACE_LEN);
}

/* ---------- response delay (illusion layer) ---------- */
static int compute_delay(Engine *eng){
    if (!(eng->identity.voice_flags & PE_VF_DELAY_TIMING)) return 0;
    int arousal = eng->state.last_input_emotion.arousal;
    int volatility = (eng->state.mood < 0 ? -eng->state.mood : eng->state.mood) / 200;
    int delay = 300 + arousal * 5 + volatility * 3;
    /* v2: exhaustion slows, intoxication adds jitter, fixation hurries */
    delay += eng->state.exhaustion / 3;
    delay -= eng->state.fixation_strength / 8;
    int jitter = (int)(persona_rng_u32(&eng->state) % 120) - 60;
    jitter   += (eng->state.intoxication > 500) ? ((int)(persona_rng_u32(&eng->state) % 400) - 200) : 0;
    delay += jitter;
    if (delay < 80) delay = 80;
    if (delay > 2000) delay = 2000;
    return delay;
}

/* ---------- public API ---------- */
int persona_open(Engine *eng, const char *character_dir){
    int is_cart = pe_is_cart_path(character_dir);
    char state_dir[256];
    memset(eng, 0, sizeof(*eng));
    eng->last_template_group = 0xFFFF;
    eng->last_template_intent = 0xFFFF;
    render_backends_init();   /* v4: idempotent renderer registry init */
    if (is_cart){
        if (pe_cart_validate_file(character_dir) != 0){
            fprintf(stderr, "persona: cartridge validation failed: %s\n", character_dir);
            return -20;
        }
        if (pe_cart_state_dir(state_dir, sizeof(state_dir), character_dir) != 0)
            return -21;
        snprintf(eng->char_dir, sizeof(eng->char_dir), "%s", state_dir);
    } else {
        snprintf(eng->char_dir, sizeof(eng->char_dir), "%s", character_dir);
    }

    if (load_static_section(character_dir, is_cart, "identity.bin", &eng->identity, sizeof(Identity)) != 0) {
        fprintf(stderr, "persona: failed to load identity.bin from %s\n", character_dir);
        return -1;
    }
    if (load_static_section(character_dir, is_cart, "drives.bin",   &eng->drives,   sizeof(DriveTable)) != 0)  return -2;
    if (load_static_section(character_dir, is_cart, "today.bin",    &eng->todays,   sizeof(TodayTable)) != 0)  return -3;
    if (load_static_section(character_dir, is_cart, "dialogue/patterns.bin",  &eng->patterns,  sizeof(PatternTable)) != 0) return -4;
    if (load_static_section(character_dir, is_cart, "dialogue/templates.bin", &eng->templates, sizeof(TemplateTable)) != 0) return -5;
    if (load_static_section(character_dir, is_cart, "dialogue/fallback.bin",  &eng->fallbacks, sizeof(FallbackTable)) != 0) return -6;
    if (load_static_section(character_dir, is_cart, "dialogue/topics.bin",    &eng->topics,    sizeof(TopicTable)) != 0)    return -7;
    if (load_static_section(character_dir, is_cart, "dialogue/goals.bin",     &eng->goals,     sizeof(GoalTable)) != 0)     return -8;
    pe_merge_baseline_patterns(&eng->patterns);
    pe_merge_baseline_templates(&eng->templates);

    /* mutable state */
    int had_state = load_or_zero(eng->char_dir, "state.bin",  &eng->state,  sizeof(NPCState));
    int had_mem   = load_or_zero(eng->char_dir, "memory.bin", &eng->memory, sizeof(MemoryStore));

    if (!had_state) {
        seed_drives(eng);
        eng->state.mood = 0;
        eng->state.trust_user = 400;
        eng->state.paranoia = 200;
        {
            const char *seed_env = getenv("PE_TODAY_SEED");
            char *end = NULL;
            unsigned long fixed_seed = seed_env && seed_env[0]
                                     ? strtoul(seed_env, &end, 0)
                                     : 0ul;
            eng->state.today_seed = (seed_env && seed_env[0] && end && *end == '\0')
                                  ? (uint32_t)fixed_seed
                                  : (uint32_t)time(NULL);
        }
        eng->state.session_start_time = persona_now_ms();
        eng->state.last_update_time   = eng->state.session_start_time;
        eng->state.rng_state = eng->state.today_seed ? eng->state.today_seed : 0xC0FFEEu;
        /* v2: embodiment + layered affect defaults */
        eng->state.intoxication       = 0;
        eng->state.exhaustion         = 0;
        eng->state.irritation_carry   = 0;
        eng->state.physical_fragility = 0;
        eng->state.fixation_topic     = 0xFFFF;
        eng->state.fixation_strength  = 0;
        eng->state.fixation_remaining = 0;
        eng->state.recovery_curve     = 0;
        eng->state.baseline_temperament = 0;
        eng->state.acute_spike          = 0;
        eng->state.suppression_mask     = 200;
        eng->state.obsession_pressure   = 0;
        eng->state.trace_pos            = 0;
    }
    if (!had_mem) {
        /* seed core memories from identity */
        for (uint8_t i = 0; i < eng->identity.core_memory_count && i < PE_EPISODIC_MAX; ++i){
            eng->memory.episodic[i] = eng->identity.core_memories_seed[i];
            eng->memory.episodic[i].core_memory = 1;
            eng->memory.episodic[i].memory_type = MEM_CORE;
            eng->memory.episodic[i].retrieval_prob = 255;
            eng->memory.episodic[i].id = i + 1;
            eng->memory.episodic_count = i + 1;
        }
        eng->memory.next_memory_id = eng->memory.episodic_count + 1;
    }
    {
        for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
            MemoryNode *m = &eng->memory.episodic[i];
            if (m->core_memory) m->memory_type = MEM_CORE;
            else if (m->memory_type == 0) m->memory_type = MEM_EPISODIC;
            if (m->memory_type == MEM_CORE) m->retrieval_prob = 255;
            else if (m->retrieval_prob == 0) m->retrieval_prob = m->salience ? m->salience : 128;
        }
    }

    pe_pick_today(eng);

    /* v2.1: plasticity — load <character_dir>/voice.lm if present.
     * The LM is part of the cartridge; the engine never references a
     * character-specific filename.  Missing LM is non-fatal (no rerank). */
    if (is_cart
        && pe_cart_load_section_alloc(character_dir, "LM ",
                                      &eng->lm_data, &eng->lm_size) == 0){
        eng->lm = ngram_lm_load_mem(eng->lm_data, eng->lm_size);
    }

    /* v3.2: AETHER long-term storage — opens (or creates) a per-character
     * subdirectory.  Soft-fails: if open fails, eng->aether stays NULL and
     * memory.c gracefully skips demotion / cold recall. */
    {
        char aether_dir[512];
        pe_path_join(aether_dir, sizeof(aether_dir), eng->char_dir, "aether");
        eng->aether = aether_open(aether_dir);
    }
    eng->cold_scratch_count = 0;

    /* v3.2: cartridge-borne synonym banks.  Try <character_dir>/banks.bin
     * first.  If the file is missing or its magic/version don't match,
     * fall back to the engine's built-in default Pretorian banks so a
     * partially-authored cartridge still produces valid output. */
    {
        int loaded_banks;
        if (is_cart)
            loaded_banks = (pe_cart_load_section(character_dir, "banks.bin",
                                                 &eng->banks, sizeof(BankRegistry)) == 0);
        else
            loaded_banks = load_or_zero(eng->char_dir, "banks.bin",
                                        &eng->banks, sizeof(BankRegistry));
        if (!loaded_banks
            || eng->banks.magic   != PE_BANK_REGISTRY_MAGIC
            || eng->banks.version != PE_BANK_REGISTRY_VERSION){
            mutator_load_default_banks(&eng->banks);
        }
    }

    /* v3.1: autobiographical chapters — load persisted book (soft-fail). */
    load_or_zero(eng->char_dir, "chapters.bin", &eng->chapters, sizeof(ChapterBook));
    eng->baseline_valence = 0;
    eng->baseline_arousal = 30;
    environment_session_start(eng);

    /* v3.1: dream detection — if this session starts after a long absence,
     * crystallise memories into chapters and flag a dream for first turn. */
    if (had_state && eng->state.last_update_time > 0) {
        uint32_t now_ms = persona_now_ms();
        uint32_t gap_ms = (now_ms >= eng->state.last_update_time)
                        ? (now_ms - eng->state.last_update_time) : 0u;
        pe_check_dream(eng, gap_ms);
    }

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
    /* v3.1: chapters — non-fatal if write fails (re-crystallised on next load) */
    pe_path_join(p, sizeof(p), eng->char_dir, "chapters.bin");
    pe_write_file_atomic(p, &eng->chapters, sizeof(ChapterBook));
    return 0;
}

void persona_close(Engine *eng){
    if (!eng) return;
    if (eng->lm){
        ngram_lm_free(eng->lm);
        eng->lm = NULL;
    }
    if (eng->lm_data){
        free(eng->lm_data);
        eng->lm_data = NULL;
        eng->lm_size = 0;
    }
    if (eng->aether){
        /* Drain any pending writes before close.  Cheap if WAL is empty. */
        if (aether_should_consolidate(eng->aether))
            aether_consolidate(eng->aether, 0 /*incremental*/);
        aether_close(eng->aether);
        eng->aether = NULL;
    }
}

int persona_process_input(Engine *eng,
                          const char *user_id,
                          const char *input_text,
                          char *out, size_t n)
{
    if (!eng || !input_text || !out || n == 0) return -1;

    /* 1. relation */
    pe_load_relation(eng, user_id);
    int first_turn_of_session = (eng->environment.turns_this_session == 0);
    uint32_t real_gap_seconds = 0;
    {
        uint32_t now_s = (uint32_t)time(NULL);
        if (eng->relation.last_contact > 0 && now_s > eng->relation.last_contact)
            real_gap_seconds = now_s - eng->relation.last_contact;
    }
    environment_update_turn(eng, input_text);

    /* 2. time delta + decay */
    uint32_t now = persona_now_ms();
    uint32_t delta = now - eng->state.last_update_time;
    uint32_t cap_ms = first_turn_of_session
                    ? (uint32_t)(30u * 86400u * 1000u)
                    : (uint32_t)(3600u * 1000u);
    if (delta > cap_ms) delta = cap_ms;
    if (first_turn_of_session && real_gap_seconds > 3600u){
        uint64_t gap_ms_64 = (uint64_t)real_gap_seconds * 1000u;
        uint32_t gap_ms = (gap_ms_64 < cap_ms) ? (uint32_t)gap_ms_64 : cap_ms;
        if (gap_ms > delta) delta = gap_ms;
    }
    pe_decay_drives(eng, delta);
    pe_decay_episodic(eng);
    pe_repetition_decay(eng);
    if (first_turn_of_session && real_gap_seconds > 3600u){
        int extra_ticks = (int)(real_gap_seconds / 3600u);
        if (extra_ticks > 240) extra_ticks = 240;
        for (int i = 0; i < extra_ticks; ++i) schema_tick(&eng->schema);
    }

    /* fold turn count + today seed into rng — guarantees deterministic replay */
    eng->state.turn_count++;
    eng->state.rng_state ^= eng->state.today_seed + eng->state.turn_count * 2654435761u;

    /* 3a. v2: cache lowered input + char bitmap + negation flag */
    pe_prep_input(eng, input_text);

    /* 3. emotional fingerprint (now uses cached lower + bitmap + negation) */
    EmotionVector ev = {0};
    pe_classify_input(eng, input_text, &ev);
    if (eng->input_class == 0) {
        if (eng->state.neutral_streak < 255) eng->state.neutral_streak++;
    } else {
        eng->state.neutral_streak = 0;
    }
    eng->state.prev_input_emotion = eng->state.last_input_emotion;
    eng->state.last_input_emotion = ev;

    /* V4 priority 3: translate the canonical input class into a symbolic
     * schema event and apply it to the active relation's belief state.
     * input_class is set by pe_classify_input — character-agnostic,
     * pattern-table-driven, no text reaches schema_apply_event. */
    {
        int evt = -1;
        switch (eng->input_class){
        case 1: evt = SCHEMA_EVT_PRAISED_US;     break;
        case 2: evt = SCHEMA_EVT_INSULTED_US;    break;
        case 4: evt = SCHEMA_EVT_THREATENED_US;  break;
        case 5: evt = SCHEMA_EVT_CONFIDED_IN_US; break;
        default: break;
        }
        if (evt >= 0){
            /* Magnitude derived from arousal (0..100 → 0..200) so loud
             * insults register harder than mild ones, deterministically. */
            int magnitude = (int)ev.arousal * 2;
            if (magnitude < 0)   magnitude = 0;
            if (magnitude > 255) magnitude = 255;
            int16_t traits[5] = {
                (int16_t)eng->identity.openness,
                (int16_t)eng->identity.conscientiousness,
                (int16_t)eng->identity.extraversion,
                (int16_t)eng->identity.agreeableness,
                (int16_t)eng->identity.neuroticism,
            };
            int prev = eng->schema.slot[SCHEMA_USER_HOSTILE +
                       (evt == SCHEMA_EVT_INSULTED_US ? 0 :
                        evt == SCHEMA_EVT_THREATENED_US ? 0 : -1)];
            (void)prev;
            schema_apply_event(&eng->schema, (SchemaEvent)evt, magnitude, traits);
            trace_emit(eng->state.turn_count, PE_TRACE_SCHEMA_EVENT,
                       evt, magnitude, "schema_event", NULL);
        }
        /* Every turn ticks the schema decay, even when no event fires. */
        schema_tick(&eng->schema);
    }

    /* 3b. v3.0: predictive coding — compare last turn's prediction to
     * actual; write surprise_last + update prediction_error_accum; jolt
     * acute_spike on large mismatches.  Skipped on turn 1 (no prediction). */
    pe_compute_surprise(eng, &ev);

    /* 3c. v3.0: Theory of Mind — update the UserModel from this turn's
     * input.  Must run after classify (needs input_class + matched_group)
     * and after pe_prep_input (needs eng->lowered, negation_active). */
    pe_update_user_model(eng, &ev);

    /* 4. associative recall */
    pe_associative_recall(eng, &ev);

    /* 5. drive update */
    pe_update_drives_from_input(eng);

    /* 6. mood */
    pe_compute_mood(eng);

    /* 6a. v2: embodiment + layered affect (modulates mood, sets acute spike, etc.) */
    pe_update_embodiment(eng, delta);
    pe_update_layered_affect(eng);

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

    /* 9. intent — overridden by fixation lock if active */
    eng->state.current_intent = pe_select_intent(eng, eng->state.current_goal, &ev);
    if (eng->state.fixation_topic != 0xFFFF && eng->state.fixation_strength > 500){
        /* fixation forces monologue/reminisce alternation */
        eng->state.current_intent = (eng->state.turn_count & 1)
            ? PE_INTENT_MONOLOGUE : PE_INTENT_REMINISCE;
    }
    /* exhaustion > 800 + paranoia > 600 → theatrical collapse (withdraw) */
    if (eng->state.exhaustion > 800 && eng->state.paranoia > 600)
        eng->state.current_intent = PE_INTENT_WITHDRAW;
    if (eng->state.neutral_streak >= 3 && eng->matched_group == 0xFFFF){
        if ((persona_rng_u32(&eng->state) & 0xFFu) < 80u){
            eng->state.current_intent = PE_INTENT_INITIATE;
            eng->state.neutral_streak = 0;
        }
    }
    {
        size_t input_len = strlen(input_text);
        int rich_input = (ev.arousal > 50) || (input_len > 80);
        if (rich_input && eng->input_class == 0
            && eng->state.current_intent != PE_INTENT_INITIATE){
            if (input_len > 100 || (persona_rng_u32(&eng->state) & 0xFFu) < 60u)
                eng->state.current_intent = PE_INTENT_ATTEND;
        }

        if (eng->input_class == 0
            && eng->matched_group == 0xFFFF
            && input_len > 20 && input_len < 80
            && eng->state.neutral_streak < 3
            && eng->state.current_intent != PE_INTENT_INITIATE
            && eng->state.current_intent != PE_INTENT_ATTEND){
            if ((persona_rng_u32(&eng->state) & 0xFFu) < 30u)
                eng->state.current_intent = PE_INTENT_CLARIFY;
        }
    }
    if (eng->state.acute_spike < -500 && eng->input_class == 2){
        if ((persona_rng_u32(&eng->state) & 0xFFu) < 80u)
            eng->state.current_intent = PE_INTENT_PAUSE;
    }
    if (eng->state.exhaustion > 850){
        if ((persona_rng_u32(&eng->state) & 0xFFu) < 100u)
            eng->state.current_intent = PE_INTENT_PAUSE;
    }

    /* 9a. v2: build rhetorical plan before realization */
    pe_build_plan(eng);

    /* V4: renderer dispatch.  Compose a RenderContext from Layer 1 state
     * and offer the selected backend the chance to produce the reply.
     * The template backend defers to the legacy pe_generate_response()
     * call below (signaled by flags bit 0).  An SLM backend that's
     * actually available will fill out->output and we use that instead. */
    {
        RetrievedMemorySet mem;
        memset(&mem, 0, sizeof(mem));
        int em_n = 0;
        for (int i = 0; i < PE_ACTIVE_MAX && em_n < 4; ++i){
            uint16_t idx = eng->active_memories[i];
            if (idx >= PE_EPISODIC_MAX) break;  /* sentinel-tagged cold entries */
            mem.episodic_idx[em_n++] = idx;
        }
        mem.episodic_count = em_n;

        RenderContext ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.npc       = eng;
        ctx.memories  = &mem;
        ctx.plan      = &eng->plan;
        ctx.relation  = &eng->relation;
        ctx.schema    = &eng->schema;
        ctx.seed      = eng->state.rng_state;

        RenderBackend *be = render_backend_default();
        if (be && be->render){
            RenderResult res;
            memset(&res, 0, sizeof(res));
            be->render(be, &ctx, &res);
            trace_render_dispatch(eng->state.turn_count, be->name,
                                  res.latency_ms, res.output_len, res.flags);
            /* bit 0: backend deferred to legacy path.  bit 1: backend
             * unavailable, fall back.  Either way drop to legacy. */
            if (res.output_len > 0 && !(res.flags & 0x3u)){
                size_t copy = (size_t)res.output_len;
                if (copy >= n) copy = n - 1;
                memcpy(out, res.output, copy);
                out[copy] = 0;
                goto post_render;
            }
            if (res.flags & 0x2u){
                trace_emit(eng->state.turn_count, PE_TRACE_RENDER_FALLBACK,
                           0, 0, be->name, "unavailable→template");
            }
        }
    }

    /* 10–11. candidates, repetition, transforms, select (plan-driven) */
    pe_generate_response(eng, input_text, out, n);
post_render:;

    /* 11b. v3.1: dream recall — prepend dream_phrase to first response after
     * a long absence.  Only fires once (dream_pending is cleared here). */
    if (eng->chapters.dream_pending){
        size_t dl = strlen(eng->chapters.dream_phrase);
        size_t rl = strlen(out);
        if (dl + 2u + rl + 1u <= n){
            memmove(out + dl + 2, out, rl + 1);
            memcpy(out, eng->chapters.dream_phrase, dl);
            out[dl]     = '\n';
            out[dl + 1] = '\n';
        }
        eng->chapters.dream_pending = 0;
    }

    /* 11a. v2: record trace */
    pe_trace_push(eng);

    /* 11b. v3.0: at end-of-turn, predict next input given what we just
     * said + current UserModel.  Surprise check at start of next turn
     * will compare against this prediction. */
    pe_predict_next_input(eng);

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
        const char *speaker = eng->relation.known_as[0]
                            ? eng->relation.known_as : "the visitor";
        if (!strcmp(speaker, "anon")) speaker = "Someone";
        MemoryNode *recent = NULL;
        if (!identity_threat && eng->input_class != 1 && eng->input_class != 5)
            recent = recent_topic_memory(eng, eng->primary_topic, 5);
        if (recent){
            recent->retrieval_prob = (recent->retrieval_prob > 223)
                                   ? 255 : (uint8_t)(recent->retrieval_prob + 32);
        } else {
            const char *label = input_class_label(eng->input_class);
            const char *topic = topic_name_by_id(eng, eng->primary_topic);
            char prefix[64];
            prefix[0] = 0;
            if (label) snprintf(prefix + strlen(prefix), sizeof(prefix) - strlen(prefix), "[%s] ", label);
            if (topic) snprintf(prefix + strlen(prefix), sizeof(prefix) - strlen(prefix), "[%s] ", topic);
            snprintf(summary, sizeof(summary), "%s%s %s: %.48s",
                     prefix, speaker,
                     eng->input_class == 3 ? "asked" : "said",
                     input_text);
            pe_commit_memory(eng, summary, &ev, eng->primary_topic, s, identity_threat);
        }
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

    /* v3.2: opportunistic AETHER consolidation.  Every 16 turns, if the
     * WAL has crossed its soft threshold, run an incremental rebuild —
     * folding pending demoted events into their bucket files so subsequent
     * cold recalls see them via bucket scan (cheaper) rather than WAL
     * linear scan.  Cheap when no consolidation is needed
     * (aether_should_consolidate returns 0 quickly). */
    if (eng->aether
        && (eng->state.turn_count & 15u) == 0
        && aether_should_consolidate(eng->aether)){
        aether_consolidate(eng->aether, 0 /* incremental */);
    }

    pe_prime_unprompted_memory(eng);

    eng->state.last_update_time = now;
    identity_update_rolling(eng, ev.valence, ev.arousal);
    persona_save(eng);
    return (int)strlen(out);
}

static const char *RHET_NAMES[PE_RHET_COUNT] = {
    "assert","hedge","deflect","escalate","lament","gloat",
    "indict","romanticize","intone","confess"
};
static const char *STANCE_NAMES[PE_STANCE_COUNT] = {
    "neutral","dominant","intimate","defensive","condescending","conspiratorial"
};
static const char *INTENT_NAMES[PE_INTENT_COUNT] = {
    "answer","evade","accuse","flatter","threaten","probe",
    "redirect","monologue","reminisce","withdraw","joke","boast",
    "initiate","attend","clarify","pause"
};

/* Render a 9-step ASCII sparkline for an int16 series scaled into [lo, hi]. */
static void sparkline(const int16_t *vals, int n, int lo, int hi, char *out, size_t cap){
    static const char glyphs[] = "._-=+*#%@";   /* 9 steps, 0..8 */
    size_t pos = 0;
    int span = hi - lo;
    if (span <= 0) span = 1;
    for (int i = 0; i < n && pos + 1 < cap; ++i){
        int v = vals[i];
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        int idx = ((v - lo) * 8) / span;
        if (idx < 0) idx = 0;
        if (idx > 8) idx = 8;
        out[pos++] = glyphs[idx];
    }
    if (pos < cap) out[pos] = 0;
}

void persona_debug_dump(const Engine *eng){
    fprintf(stderr, "\n--- persona dump (turn=%u) ---\n", eng->state.turn_count);
    fprintf(stderr, "today=%u(%s) seed=0x%08x mood=%d fatigue=%d paranoia=%d trust=%d\n",
            eng->state.today_index,
            eng->todays.entries[eng->state.today_index].label,
            eng->state.today_seed,
            eng->state.mood, eng->state.fatigue, eng->state.paranoia,
            eng->state.trust_user);
    static const char *drive_names[PE_DRIVE_COUNT] = {
        "Recognition","Stimulation","Provocation","Communion",
        "Autonomy","Continuity","Vindication","Repose"
    };
    for (int i = 0; i < PE_DRIVE_COUNT; ++i)
        fprintf(stderr, "  %-12s %4d / baseline %4d\n",
                drive_names[i], eng->state.drive_values[i], eng->drives.drives[i].baseline);

    /* v2: embodiment */
    fprintf(stderr, "embodiment: intox=%d exhaust=%d irrit=%d fragil=%d  fix=topic%u/str=%d/turns=%d  rec=%d\n",
            eng->state.intoxication, eng->state.exhaustion,
            eng->state.irritation_carry, eng->state.physical_fragility,
            eng->state.fixation_topic, eng->state.fixation_strength,
            eng->state.fixation_remaining, eng->state.recovery_curve);
    /* v2: layered affect */
    fprintf(stderr, "affect: temperament=%d acute=%d suppress=%d obsession_pressure=%d\n",
            eng->state.baseline_temperament, eng->state.acute_spike,
            eng->state.suppression_mask, eng->state.obsession_pressure);

    uint16_t gi = eng->state.current_goal;
    fprintf(stderr, "goal=%u(%s) intent=%u(%s) hysteresis=%u input_class=%d primary_topic=%u negation=%d\n",
            gi, gi < eng->goals.count ? eng->goals.entries[gi].name : "?",
            eng->state.current_intent,
            eng->state.current_intent < PE_INTENT_COUNT ? INTENT_NAMES[eng->state.current_intent] : "?",
            eng->state.goal_hysteresis, eng->input_class, eng->primary_topic,
            eng->negation_active);
    /* v2: plan */
    fprintf(stderr, "plan: mode=%s stance=%s target_topic=%u callback_mem=%u\n"
                    "      cert=%u verb=%u aggr=%u theat=%u hedge=%u\n",
            eng->state.last_rhetorical_mode < PE_RHET_COUNT
                ? RHET_NAMES[eng->state.last_rhetorical_mode] : "?",
            eng->state.last_stance < PE_STANCE_COUNT
                ? STANCE_NAMES[eng->state.last_stance] : "?",
            eng->state.last_target_topic, eng->state.last_callback_memory,
            eng->state.last_certainty, eng->state.last_verbosity,
            eng->state.last_aggression, eng->state.last_theatricality,
            eng->state.last_hedging);

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
    /* v3.0: Theory of Mind + predictive coding */
    fprintf(stderr, "user_model: val=%d ar=%d dom=%d belief=%d know=%u eng=%u upd=%u\n",
            eng->relation.um_valence, eng->relation.um_arousal,
            eng->relation.um_dominance, eng->relation.um_belief_about_me,
            eng->relation.um_knowledge_level, eng->relation.um_engagement,
            eng->relation.um_update_count);
    fprintf(stderr, "predicted: class=%u val=%d   surprise_last=%u err_accum=%u\n",
            eng->state.predicted_input_class, eng->state.predicted_input_valence,
            eng->state.surprise_last, eng->state.prediction_error_accum);
    fprintf(stderr, "input_sig=0x%016llx\n", (unsigned long long)eng->input_sig);
    /* v3.2: The Voice — counterfactual rerank instrumentation */
    fprintf(stderr, "voice: last_delta=%d last_choice_tid=%u\n",
            eng->state.last_voice_delta, eng->state.last_voice_choice);
    /* v3.1: story layer */
    fprintf(stderr, "chapters=%u dream_pending=%u", eng->chapters.chapter_count,
            eng->chapters.dream_pending);
    if (eng->chapters.chapter_count > 0){
        /* show top chapter by salience */
        int top = 0;
        for (int i = 1; i < eng->chapters.chapter_count; ++i)
            if (eng->chapters.chapters[i].salience_peak >
                eng->chapters.chapters[top].salience_peak) top = i;
        const Chapter *ch = &eng->chapters.chapters[top];
        fprintf(stderr, "  top_chapter: mood=%d mems=%u sal=%u phrase=\"%.32s\"",
                ch->dominant_mood, ch->memory_count, ch->salience_peak, ch->phrase);
    }
    fprintf(stderr, "\n--- end dump ---\n\n");
}

/* v2: trace dump — sparklines + intent transition timeline. */
void persona_trace_dump(const Engine *eng){
    int n = PE_TRACE_LEN;
    /* unroll ring into chronological order */
    static int16_t mood[PE_TRACE_LEN];
    static int16_t intox[PE_TRACE_LEN], exhaust[PE_TRACE_LEN], irrit[PE_TRACE_LEN];
    static int16_t acute[PE_TRACE_LEN], obsess[PE_TRACE_LEN];
    int valid = 0;
    for (int i = 0; i < n; ++i){
        int idx = (eng->state.trace_pos + i) % n;
        const TraceEntry *t = &eng->state.trace[idx];
        if (t->turn == 0 && i == 0) continue;  /* skip empty leading slots */
        mood[valid]    = t->mood;
        intox[valid]   = t->intoxication;
        exhaust[valid] = t->exhaustion;
        irrit[valid]   = t->irritation_carry;
        acute[valid]   = t->acute_spike;
        obsess[valid]  = t->obsession_pressure;
        valid++;
    }
    if (valid == 0){ fprintf(stderr, "(no trace yet)\n"); return; }
    char buf[256];
    fprintf(stderr, "\n--- trace (last %d turns) ---\n", valid);
    sparkline(mood,    valid, -1000, 1000, buf, sizeof(buf)); fprintf(stderr, "mood     |%s|\n", buf);
    sparkline(acute,   valid, -1000, 1000, buf, sizeof(buf)); fprintf(stderr, "acute    |%s|\n", buf);
    sparkline(intox,   valid,     0, 1000, buf, sizeof(buf)); fprintf(stderr, "intox    |%s|\n", buf);
    sparkline(exhaust, valid,     0, 1000, buf, sizeof(buf)); fprintf(stderr, "exhaust  |%s|\n", buf);
    sparkline(irrit,   valid,     0, 1000, buf, sizeof(buf)); fprintf(stderr, "irrit    |%s|\n", buf);
    sparkline(obsess,  valid,     0, 1000, buf, sizeof(buf)); fprintf(stderr, "obsess.p |%s|\n", buf);

    fprintf(stderr, "intent transitions: ");
    uint16_t prev_intent = 0xFFFF;
    for (int i = 0; i < n; ++i){
        int idx = (eng->state.trace_pos + i) % n;
        const TraceEntry *t = &eng->state.trace[idx];
        if (t->turn == 0 && i == 0) continue;
        if (t->intent != prev_intent){
            fprintf(stderr, "t%u:%s%s ",
                    t->turn,
                    t->intent < PE_INTENT_COUNT ? INTENT_NAMES[t->intent] : "?",
                    t->negation_flag ? "(¬)" : "");
            prev_intent = t->intent;
        }
    }
    fprintf(stderr, "\n--- end trace ---\n\n");
}

/* v2: plan dump — the current UtterancePlan in full. */
void persona_plan_dump(const Engine *eng){
    const UtterancePlan *p = &eng->plan;
    fprintf(stderr, "\n--- utterance plan ---\n");
    fprintf(stderr, "  mode:           %s\n", p->rhetorical_mode < PE_RHET_COUNT ? RHET_NAMES[p->rhetorical_mode] : "?");
    fprintf(stderr, "  stance:         %s\n", p->stance < PE_STANCE_COUNT ? STANCE_NAMES[p->stance] : "?");
    fprintf(stderr, "  target_topic:   %u\n", p->target_topic);
    fprintf(stderr, "  callback_mem:   %u\n", p->callback_memory);
    fprintf(stderr, "  certainty:      %u\n", p->certainty);
    fprintf(stderr, "  verbosity:      %u\n", p->verbosity);
    fprintf(stderr, "  aggression:     %u\n", p->aggression);
    fprintf(stderr, "  theatricality:  %u\n", p->theatricality);
    fprintf(stderr, "  hedging:        %u\n", p->hedging);
    fprintf(stderr, "  negation_in_play: %u\n", p->negation_in_play);
    fprintf(stderr, "--- end plan ---\n\n");
}
