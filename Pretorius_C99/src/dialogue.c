/* dialogue.c — candidate generation, scoring, slot filling, style transforms,
 *               repetition suppression, fallback ladder.
 */
#include "persona.h"
#include "persona_internal.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ---------- repetition suppression ---------- */

static PhraseUsage *find_phrase(MemoryStore *m, uint32_t pid){
    PhraseUsage *empty = NULL;
    PhraseUsage *lru = &m->phrase_usage[0];
    for (int i = 0; i < PE_PHRASE_USAGE; ++i){
        if (m->phrase_usage[i].phrase_id == pid) return &m->phrase_usage[i];
        if (!empty && m->phrase_usage[i].count == 0) empty = &m->phrase_usage[i];
        if (m->phrase_usage[i].last_turn < lru->last_turn) lru = &m->phrase_usage[i];
    }
    PhraseUsage *slot = empty ? empty : lru;
    slot->phrase_id = pid;
    slot->count = 0;
    return slot;
}

static int repetition_penalty_pct(MemoryStore *m, uint32_t pid){
    for (int i = 0; i < PE_PHRASE_USAGE; ++i){
        if (m->phrase_usage[i].phrase_id == pid)
            return 100 + m->phrase_usage[i].count * 40;
    }
    return 100;
}

void pe_repetition_decay(Engine *eng){
    /* every 20 turns of staleness, count -= 1 (lazy) */
    uint32_t turn = eng->state.turn_count;
    for (int i = 0; i < PE_PHRASE_USAGE; ++i){
        PhraseUsage *p = &eng->memory.phrase_usage[i];
        if (p->count == 0) continue;
        uint32_t age = (turn > p->last_turn) ? turn - p->last_turn : 0;
        uint32_t drop = age / 20u;
        if (drop > 0) {
            if (drop > p->count) p->count = 0;
            else p->count -= (uint16_t)drop;
            p->last_turn = turn;
        }
    }
}

static void record_use(MemoryStore *m, uint32_t pid, uint32_t turn){
    PhraseUsage *p = find_phrase(m, pid);
    if (p->count < 0xFFFF) p->count++;
    p->last_turn = turn;
}

/* ---------- slot filling ---------- */

static size_t append_str(char *dst, size_t cap, size_t pos, const char *s){
    while (*s && pos + 1 < cap) dst[pos++] = *s++;
    if (pos < cap) dst[pos] = 0;
    return pos;
}

static void fill_slots(Engine *eng, const Template *t, char *out, size_t n){
    const char *src = t->text;
    size_t pos = 0;
    const char *user_name = eng->relation.known_as[0] ? eng->relation.known_as : "my dear";

    /* pick an address line deterministically per template */
    uint32_t r = (t->id ^ eng->state.today_seed) % PE_ADDRESS_COUNT;
    const char *address = eng->identity.address_user_as[r];
    if (!address[0]) address = "my dear";

    /* pick a recent memory summary if active */
    const char *mem_summary = "";
    if (eng->active_count > 0)
        mem_summary = eng->memory.episodic[eng->active_memories[0]].summary;

    /* primary topic name */
    const char *topic_name = "the matter";
    if (eng->primary_topic != 0xFFFF){
        for (uint32_t i = 0; i < eng->topics.count; ++i)
            if (eng->topics.topics[i].id == eng->primary_topic){
                topic_name = eng->topics.topics[i].name; break;
            }
    }

    while (*src && pos + 1 < n){
        if (src[0] == '{'){
            const char *end = strchr(src, '}');
            if (end){
                size_t klen = (size_t)(end - src - 1);
                char key[24]; if (klen >= sizeof(key)) klen = sizeof(key)-1;
                memcpy(key, src + 1, klen); key[klen] = 0;
                if      (!strcmp(key, "user"))    pos = append_str(out, n, pos, user_name);
                else if (!strcmp(key, "address")) pos = append_str(out, n, pos, address);
                else if (!strcmp(key, "topic"))   pos = append_str(out, n, pos, topic_name);
                else if (!strcmp(key, "memory"))  pos = append_str(out, n, pos, mem_summary);
                else if (!strcmp(key, "name"))    pos = append_str(out, n, pos, eng->identity.character_name);
                src = end + 1;
                continue;
            }
        }
        out[pos++] = *src++;
    }
    if (pos < n) out[pos] = 0;
}

/* ---------- style transforms (deterministic; seeded per template+today) ---------- */

static uint32_t style_rng(uint32_t *state){
    uint32_t x = *state ? *state : 0xCAFEBABEu;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (*state = x);
}

static int starts_with_lower(const char *s, const char *w){
    while (*s && isspace((unsigned char)*s)) s++;
    for (; *w; ++w, ++s){
        if (tolower((unsigned char)*s) != (unsigned char)*w) return 0;
    }
    return 1;
}

static void apply_style(Engine *eng, const Template *t, char *buf, size_t cap){
    uint32_t seed = t->id ^ eng->state.today_seed;
    uint32_t flags = eng->identity.voice_flags;
    uint32_t today_or = eng->todays.entries[eng->state.today_index].voice_flag_or;
    flags |= today_or;

    /* metaphor injection: append a flourish at higher arousal */
    if ((flags & PE_VF_METAPHOR) && (style_rng(&seed) % 100) < 35){
        static const char *flourishes[] = {
            " — like cathedrals of bone, do you see?",
            " — and the lightning sings, oh how it sings.",
            " — what an exquisite arrangement of clay we are.",
            " — a tincture, a gesture, an entire eternity."
        };
        size_t L = strlen(buf);
        const char *f = flourishes[style_rng(&seed) % 4];
        size_t fl = strlen(f);
        if (L + fl + 1 < cap){ memcpy(buf + L, f, fl); buf[L + fl] = 0; }
    }
    /* avoid direct affirmation */
    if ((flags & PE_VF_NO_DIRECT_AFFIRM) && starts_with_lower(buf, "i agree")){
        const char *rep = "Your conclusion is not without merit";
        char tmp[PE_TEMPLATE_TEXT];
        snprintf(tmp, sizeof(tmp), "%s%s", rep, buf + 7);
        snprintf(buf, cap, "%s", tmp);
    }
    /* hedging at high openness */
    if (eng->identity.openness > 0xB000 && (style_rng(&seed) % 100) < 20){
        char tmp[PE_TEMPLATE_TEXT];
        snprintf(tmp, sizeof(tmp), "Perhaps — %c%s",
                 (char)tolower((unsigned char)buf[0]), buf + 1);
        snprintf(buf, cap, "%s", tmp);
    }
    /* verbosity scaling (high) */
    if (PE_VF_GET_VERBOSITY(flags) >= 5 && (style_rng(&seed) % 100) < 40){
        char tmp[PE_TEMPLATE_TEXT];
        size_t L = strlen(buf);
        if (L > 0 && buf[L-1] == '.') buf[L-1] = 0;
        snprintf(tmp, sizeof(tmp), "%s (though one might also argue the inverse).", buf);
        snprintf(buf, cap, "%s", tmp);
    }
    /* sarcasm flag on praise replies if low agreeableness */
    if ((flags & PE_VF_SARDONIC) && eng->input_class == 1
        && eng->identity.agreeableness < 0x6000
        && (style_rng(&seed) % 100) < 30){
        char tmp[PE_TEMPLATE_TEXT];
        snprintf(tmp, sizeof(tmp), "How kind. %s", buf);
        snprintf(buf, cap, "%s", tmp);
    }
    /* self-interruption */
    if ((flags & PE_VF_SELF_INTERRUPT) && (style_rng(&seed) % 100) < 12){
        char tmp[PE_TEMPLATE_TEXT];
        snprintf(tmp, sizeof(tmp), "Yes — no — %s", buf);
        snprintf(buf, cap, "%s", tmp);
    }
}

/* ---------- candidate scoring ---------- */

static int template_admissible(const Engine *eng, const Template *t){
    const TodayEntry *td = &eng->todays.entries[eng->state.today_index];
    if (t->dialogue_mask_bit && !(td->dialogue_mask & t->dialogue_mask_bit)) return 0;
    if (eng->state.mood < t->mood_min || eng->state.mood > t->mood_max) return 0;
    if (t->required_voice_flags &&
        (eng->identity.voice_flags & t->required_voice_flags) != t->required_voice_flags) return 0;
    return 1;
}

static int32_t score_template(Engine *eng, const Template *t){
    int32_t s = t->base_score;
    if (t->intent == eng->state.current_intent) s += 80;
    if (t->group   == eng->matched_group)       s += 60;
    if (t->drive_bias_id >= 0 && t->drive_bias_id < PE_DRIVE_COUNT)
        s += eng->state.drive_values[t->drive_bias_id] / 20;
    /* personality fit */
    if (t->intent == PE_INTENT_MONOLOGUE) s += eng->identity.extraversion / 1024;
    if (t->intent == PE_INTENT_REMINISCE) s += eng->identity.openness     / 1024;
    /* repetition penalty */
    int pen = repetition_penalty_pct(&eng->memory, t->id);
    s = (s * 100) / pen;
    /* small chaos */
    s += (int32_t)(persona_rng_u32(&eng->state) & 0x1F);
    return s;
}

/* ---------- fallback ladder ---------- */

static const char *fallback_line(Engine *eng){
    int stim = eng->state.drive_values[PE_DRIVE_STIMULATION];
    int mood = eng->state.mood;
    const FallbackTable *fb = &eng->fallbacks;
    if (stim > 700 && fb->tier1_count > 0) {
        uint32_t r = persona_rng_u32(&eng->state) % fb->tier1_count;
        return fb->tier1[r];
    } else if (mood >= 300 && fb->tier2_count > 0) {
        uint32_t r = persona_rng_u32(&eng->state) % fb->tier2_count;
        return fb->tier2[r];
    } else if (fb->tier3_count > 0) {
        uint32_t r = persona_rng_u32(&eng->state) % fb->tier3_count;
        return fb->tier3[r];
    }
    return "...";
}

/* ---------- main generator ---------- */

int pe_generate_response(Engine *eng, const char *input, char *out, size_t n){
    (void)input;
    eng->candidate_count = 0;

    /* collect candidates: matching group, current intent, active-memory injection */
    for (uint32_t i = 0; i < eng->templates.count && eng->candidate_count < 64; ++i){
        const Template *t = &eng->templates.entries[i];
        if (!t->text[0]) continue;
        if (!template_admissible(eng, t)) continue;
        int relevant = 0;
        if (eng->matched_group != 0xFFFF && t->group == eng->matched_group) relevant = 1;
        if (t->intent == eng->state.current_intent) relevant = 1;
        if (!relevant) continue;
        eng->candidate_ids[eng->candidate_count] = (uint16_t)i;
        eng->candidate_scores[eng->candidate_count] = score_template(eng, t);
        eng->candidate_count++;
    }

    /* fallback if nothing matched */
    if (eng->candidate_count == 0){
        const char *line = fallback_line(eng);
        snprintf(out, n, "%s", line);
        /* still record repetition usage so fallbacks vary */
        record_use(&eng->memory, persona_hash(line), eng->state.turn_count);
        return 0;
    }

    /* select top-3 by score, weighted random among them */
    uint16_t top_idx[3] = {0,0,0};
    int32_t  top_sc [3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    for (uint16_t i = 0; i < eng->candidate_count; ++i){
        int32_t s = eng->candidate_scores[i];
        if (s > top_sc[0]){ top_sc[2]=top_sc[1]; top_idx[2]=top_idx[1];
                             top_sc[1]=top_sc[0]; top_idx[1]=top_idx[0];
                             top_sc[0]=s; top_idx[0]=eng->candidate_ids[i]; }
        else if (s > top_sc[1]){ top_sc[2]=top_sc[1]; top_idx[2]=top_idx[1];
                                  top_sc[1]=s; top_idx[1]=eng->candidate_ids[i]; }
        else if (s > top_sc[2]){ top_sc[2]=s; top_idx[2]=eng->candidate_ids[i]; }
    }
    int slots = (eng->candidate_count >= 3) ? 3 :
                (eng->candidate_count >= 2) ? 2 : 1;
    /* shift to positive, weighted sample */
    int32_t lo = top_sc[0];
    for (int k = 0; k < slots; ++k) if (top_sc[k] < lo) lo = top_sc[k];
    int32_t total = 0;
    int32_t w[3] = {0,0,0};
    for (int k = 0; k < slots; ++k){ w[k] = top_sc[k] - lo + 1; total += w[k]; }
    int32_t r = (int32_t)(persona_rng_u32(&eng->state) % (uint32_t)total);
    int pick = 0;
    for (int k = 0; k < slots; ++k){ if (r < w[k]){ pick = k; break; } r -= w[k]; }

    const Template *chosen = &eng->templates.entries[top_idx[pick]];

    /* fill slots, then style transforms */
    char buf[PE_TEMPLATE_TEXT];
    fill_slots(eng, chosen, buf, sizeof(buf));
    apply_style(eng, chosen, buf, sizeof(buf));

    /* topic callback (illusion layer): inject "by the way..." once in a while */
    if ((eng->identity.voice_flags & PE_VF_ALLOW_CALLBACK)
        && (persona_rng_u32(&eng->state) % 100) < 10){
        for (int i = 0; i < PE_TOPIC_SLOTS; ++i){
            if (eng->state.topic_momentum[i].momentum > 600
                && eng->state.topic_momentum[i].topic_id != eng->primary_topic){
                const char *tn = "the matter";
                for (uint32_t k = 0; k < eng->topics.count; ++k)
                    if (eng->topics.topics[k].id == eng->state.topic_momentum[i].topic_id){
                        tn = eng->topics.topics[k].name; break;
                    }
                size_t L = strlen(buf);
                if (L + 64 < sizeof(buf))
                    snprintf(buf + L, sizeof(buf) - L,
                             " But come — let us return to %s.", tn);
                break;
            }
        }
    }

    /* tiny contradiction (2% with flag) — flagged in memory by stamping type=2 */
    if ((eng->identity.voice_flags & PE_VF_ALLOW_CONTRADICT)
        && (persona_rng_u32(&eng->state) % 100) < 2
        && eng->memory.episodic_count > 0){
        size_t L = strlen(buf);
        if (L + 16 < sizeof(buf))
            snprintf(buf + L, sizeof(buf) - L, " (Or perhaps the opposite.)");
    }

    snprintf(out, n, "%s", buf);
    record_use(&eng->memory, chosen->id, eng->state.turn_count);
    return 0;
}
