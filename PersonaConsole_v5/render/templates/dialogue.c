/* dialogue.c — candidate generation, scoring, slot filling, style transforms,
 *               repetition suppression, fallback ladder.
 */
#include "persona.h"
#include "persona_internal.h"
#include "ngram_lm.h"        /* v2.1: optional reranker */
#include "mutator.h"         /* v2.1: optional template expansion */
#include "environment.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>

/* ---------- repetition suppression ---------- */

#define PE_USAGE_TEMPLATE   0x10000000u
#define PE_USAGE_FALLBACK   0x20000000u
#define PE_USAGE_FLOURISH   0x30000000u
#define PE_USAGE_EXPANSION  0x40000000u
#define PE_USAGE_CALLBACK   0x50000000u
#define PE_USAGE_MEMORY     0x60000000u

static uint32_t usage_id(uint32_t ns, uint32_t value){
    uint32_t h = value ^ ns;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h ? h : ns;
}

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

static int repetition_penalty_pct(const Engine *eng, uint32_t pid){
    MemoryStore *m = (MemoryStore*)&eng->memory;
    for (int i = 0; i < PE_PHRASE_USAGE; ++i){
        if (m->phrase_usage[i].phrase_id == pid) {
            uint32_t age = (eng->state.turn_count > m->phrase_usage[i].last_turn)
                         ? eng->state.turn_count - m->phrase_usage[i].last_turn : 0u;
            int penalty = 100 + m->phrase_usage[i].count * 80;
            if (age < 12) penalty += 5000;
            else if (age < 24) penalty += (int)(24u - age) * 60;
            return penalty;
        }
    }
    return 100;
}

static int phrase_recently_used(const Engine *eng, uint32_t pid, uint32_t cooldown){
    const MemoryStore *m = &eng->memory;
    for (int i = 0; i < PE_PHRASE_USAGE; ++i){
        if (m->phrase_usage[i].phrase_id == pid && m->phrase_usage[i].count > 0) {
            uint32_t age = (eng->state.turn_count > m->phrase_usage[i].last_turn)
                         ? eng->state.turn_count - m->phrase_usage[i].last_turn : 0u;
            return age < cooldown;
        }
    }
    return 0;
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

static void fill_text_slots(Engine *eng, const char *src, uint32_t slot_seed, char *out, size_t n){
    size_t pos = 0;
    const char *user_name = eng->relation.known_as[0] ? eng->relation.known_as : "my dear";
    if (n > 0) out[0] = 0;

    /* Address pick: intimate slots [2..PE_ADDRESS_COUNT-1] are gated by
     * relation schema, not merely elapsed time. */
    uint32_t modulus = PE_ADDRESS_COUNT;
    if (!pe_allow_intimate_address(eng))
        modulus = 2;
    uint32_t r = (slot_seed ^ eng->state.today_seed) % modulus;
    const char *address = eng->identity.address_user_as[r];
    if (!address[0]) address = "my dear";

    /* v2: prefer plan.callback_memory; fall back to top-recall.
     * v3.2: indices may resolve into working memory OR cold_scratch[]
     * (sentinel range >= PE_EPISODIC_MAX) — use pe_active_node helper. */
    const char *mem_summary = "";
    const MemoryNode *cb = NULL;
    if (eng->plan.callback_memory != 0xFFFF)
        cb = pe_active_node(eng, eng->plan.callback_memory);
    if (cb && phrase_recently_used(eng, usage_id(PE_USAGE_MEMORY, cb->id), 6u))
        cb = NULL;
    if (cb) {
        mem_summary = cb->summary;
    } else if (eng->active_count > 0) {
        for (uint16_t i = 0; i < eng->active_count; ++i){
            const MemoryNode *a = pe_active_node(eng, eng->active_memories[i]);
            if (a && !phrase_recently_used(eng, usage_id(PE_USAGE_MEMORY, a->id), 6u)){
                cb = a;
                mem_summary = a->summary;
                break;
            }
        }
    }

    /* v2: prefer plan.target_topic (covers fixation / obsession-pressure) */
    const char *topic_name = "the matter";
    uint16_t topic_for_name = eng->plan.target_topic != 0xFFFF
                            ? eng->plan.target_topic : eng->primary_topic;
    if (topic_for_name != 0xFFFF){
        for (uint32_t i = 0; i < eng->topics.count; ++i)
            if (eng->topics.topics[i].id == topic_for_name){
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
                else if (!strcmp(key, "session_count")) {
                    char tmp[16]; snprintf(tmp, sizeof(tmp), "%u", eng->environment.session_count);
                    pos = append_str(out, n, pos, tmp);
                }
                else if (!strcmp(key, "total_turns")) {
                    char tmp[16]; snprintf(tmp, sizeof(tmp), "%u", eng->environment.total_turns);
                    pos = append_str(out, n, pos, tmp);
                }
                else if (!strcmp(key, "repeated")) {
                    char tmp[16]; snprintf(tmp, sizeof(tmp), "%u", eng->environment.repeated_question_count);
                    pos = append_str(out, n, pos, tmp);
                }
                else if (!strcmp(key, "hour")) {
                    char tmp[8]; environment_hour_string(tmp, sizeof(tmp));
                    pos = append_str(out, n, pos, tmp);
                }
                src = end + 1;
                continue;
            }
        }
        out[pos++] = *src++;
    }
    if (pos < n) out[pos] = 0;
    if (cb) record_use(&eng->memory, usage_id(PE_USAGE_MEMORY, cb->id), eng->state.turn_count);
}

static void fill_slots(Engine *eng, const Template *t, char *out, size_t n){
    fill_text_slots(eng, t->text, t->id, out, n);
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

/* v2: style transforms now consume the UtterancePlan in addition to voice_flags.
 *
 * The plan's verbosity/aggression/theatricality/hedging fields gate the
 * probabilities. Identity voice_flags still serve as the umbrella permission. */
static void apply_style(Engine *eng, const Template *t, char *buf, size_t cap){
    uint32_t seed = t->id ^ eng->state.today_seed;
    uint32_t flags = eng->identity.voice_flags;
    uint32_t today_or = eng->todays.entries[eng->state.today_index].voice_flag_or;
    flags |= today_or;
    const UtterancePlan *p = &eng->plan;
    int grounded_turn = (eng->matched_group != 0xFFFF
                      && t->group == eng->matched_group
                      && eng->input_class != 2
                      && eng->input_class != 4);
    int hostile_turn = (eng->input_class == 2 || eng->input_class == 4);
    int appended_flourish = 0;
    if (grounded_turn || hostile_turn) {
        flags &= ~(uint32_t)(PE_VF_METAPHOR | PE_VF_SELF_INTERRUPT);
    }

    /* metaphor injection — gated by theatricality.  Strings live in the
     * cartridge (identity.flourishes); engine never embeds character-flavored
     * text.  Empty slots are skipped, so cartridges can opt out by leaving
     * the bank zero-filled. */
    if ((flags & PE_VF_METAPHOR) && (style_rng(&seed) % 255u) < p->theatricality){
        uint32_t start = style_rng(&seed) % PE_FLOURISH_COUNT;
        const char *f = "";
        uint32_t fid = 0;
        for (uint32_t i = 0; i < PE_FLOURISH_COUNT; ++i){
            uint32_t idx = (start + i) % PE_FLOURISH_COUNT;
            const char *candidate = eng->identity.flourishes[idx];
            uint32_t candidate_id = usage_id(PE_USAGE_FLOURISH, persona_hash(candidate));
            if (candidate[0] && !phrase_recently_used(eng, candidate_id, 4u)){
                f = candidate;
                fid = candidate_id;
                break;
            }
        }
        if (f[0]){
            size_t L = strlen(buf);
            size_t fl = strlen(f);
            if (L + fl + 1 < cap){
                memcpy(buf + L, f, fl);
                buf[L + fl] = 0;
                appended_flourish = 1;
                record_use(&eng->memory, fid, eng->state.turn_count);
            }
        }
    }
    /* avoid direct affirmation */
    if ((flags & PE_VF_NO_DIRECT_AFFIRM) && starts_with_lower(buf, "i agree")){
        const char *rep = "Your conclusion is not without merit";
        char tmp[PE_TEMPLATE_TEXT];
        snprintf(tmp, sizeof(tmp), "%s%s", rep, buf + 7);
        snprintf(buf, cap, "%s", tmp);
    }
    /* hedging — gated by plan.hedging */
    if (!grounded_turn && (style_rng(&seed) % 255u) < (uint32_t)(p->hedging / 2)){
        char tmp[PE_TEMPLATE_TEXT];
        snprintf(tmp, sizeof(tmp), "Perhaps. %s", buf);
        snprintf(buf, cap, "%s", tmp);
    }
    /* verbosity expansion — gated by plan.verbosity.  Strings come from the
     * cartridge (identity.expansions); empty slots are skipped. */
    if (!grounded_turn && !hostile_turn && !appended_flourish
        && (style_rng(&seed) % 255u) < (uint32_t)(p->verbosity / 2)){
        uint32_t start = style_rng(&seed) % PE_EXPANSION_COUNT;
        const char *ex = "";
        uint32_t exid = 0;
        for (uint32_t i = 0; i < PE_EXPANSION_COUNT; ++i){
            uint32_t idx = (start + i) % PE_EXPANSION_COUNT;
            const char *candidate = eng->identity.expansions[idx];
            uint32_t candidate_id = usage_id(PE_USAGE_EXPANSION, persona_hash(candidate));
            if (candidate[0] && !phrase_recently_used(eng, candidate_id, 4u)){
                ex = candidate;
                exid = candidate_id;
                break;
            }
        }
        if (ex[0]){
            char tmp[PE_TEMPLATE_TEXT];
            size_t L = strlen(buf);
            if (L > 0 && buf[L-1] == '.') buf[L-1] = 0;
            snprintf(tmp, sizeof(tmp), "%s%s.", buf, ex);
            snprintf(buf, cap, "%s", tmp);
            record_use(&eng->memory, exid, eng->state.turn_count);
        }
    }
    /* sardonic prefix — gated by plan.aggression on praise */
    if ((flags & PE_VF_SARDONIC) && eng->input_class == 1
        && (style_rng(&seed) % 255u) < (uint32_t)p->aggression){
        char tmp[PE_TEMPLATE_TEXT];
        snprintf(tmp, sizeof(tmp), "How kind. %s", buf);
        snprintf(buf, cap, "%s", tmp);
    }
    /* self-interruption — gated by exhaustion + intoxication.
     * Confidant boost: speakers stumble more around close friends than
     * strangers (relaxed register). */
    if (!hostile_turn && (flags & PE_VF_SELF_INTERRUPT)){
        uint32_t prob = (uint32_t)(eng->state.intoxication / 12 + eng->state.exhaustion / 16);
        if (eng->relation.tags & PE_TAG_CONFIDANT) prob += 30;
        if ((style_rng(&seed) % 255u) < prob){
            char tmp[PE_TEMPLATE_TEXT];
            snprintf(tmp, sizeof(tmp), "Yes. No. %s", buf);
            snprintf(buf, cap, "%s", tmp);
        }
    }
    /* v2: intoxicated slur — chops a period, doubles a vowel, low certainty */
    if (eng->state.intoxication > 600 && (style_rng(&seed) % 100) < 20){
        size_t L = strlen(buf);
        if (L > 4){
            for (size_t i = 1; i < L; ++i){
                char c = (char)tolower((unsigned char)buf[i]);
                if (c == 'o' || c == 'a'){
                    if (L + 1 < cap){
                        memmove(buf + i + 1, buf + i, L - i + 1);
                        L++;
                    }
                    break;
                }
            }
        }
    }
    /* v2: theatrical collapse — exhaustion + paranoia produce trailing ellipsis */
    if (eng->state.exhaustion > 700 && eng->state.paranoia > 500){
        size_t L = strlen(buf);
        if (L > 4 && L + 5 < cap){
            if (buf[L-1] == '.') buf[L-1] = 0;
            snprintf(buf + strlen(buf), cap - strlen(buf), "...");
        }
    }
    /* v2: confess mode — prepend hedge for explicit uncertainty */
    {
        const char *force_unresolved = getenv("PE_FORCE_UNRESOLVED_THREAD");
        int force = (force_unresolved && force_unresolved[0]) ? 1 : 0;
    if ((flags & PE_VF_ALLOW_CONTRADICT)
        && !grounded_turn && !hostile_turn
        && eng->state.current_intent != PE_INTENT_PAUSE
        && (force || (style_rng(&seed) % 100u) < 4u)){
        size_t L = strlen(buf);
        size_t cut = 0;
        for (size_t i = 8; i + 8 < L && i < cap; ++i){
            if (buf[i] == ',' || buf[i] == '.'){ cut = i; break; }
        }
        if (force && cut == 0 && L > 8 && L + 20 < cap) cut = L;
        if (cut > 8){
            const char *trail = "... no, never mind.";
            size_t trail_len = strlen(trail);
            if (cut + trail_len + 1 < cap)
                memcpy(buf + cut, trail, trail_len + 1);
            uint16_t thread_topic = eng->plan.target_topic != 0xFFFF
                                  ? eng->plan.target_topic
                                  : eng->identity.obsessions[0];
            if (thread_topic != 0xFFFF && thread_topic != 0){
                uint8_t h = eng->state.unresolved_head;
                eng->state.unresolved_threads[h] = thread_topic;
                eng->state.unresolved_head = (uint8_t)((h + 1u) % 8u);
                if (eng->state.unresolved_count < 8)
                    eng->state.unresolved_count++;
            }
        }
    }
    }
    if (p->rhetorical_mode == PE_RHET_CONFESS && p->certainty < 100){
        char tmp[PE_TEMPLATE_TEXT];
        snprintf(tmp, sizeof(tmp), "I shall confess: %s", buf);
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
    /* v2: plan threshold gates */
    const UtterancePlan *p = &eng->plan;
    if (t->min_certainty     > p->certainty)     return 0;
    if (t->min_aggression    > p->aggression)    return 0;
    if (t->min_theatricality > p->theatricality) return 0;
    /* v2: mode/stance compat (0 mask = compatible with all) */
    if (t->rhetorical_mask &&
        !(t->rhetorical_mask & (1u << p->rhetorical_mode))) return 0;
    if (t->stance_mask &&
        !(t->stance_mask & (1u << p->stance))) return 0;
    return 1;
}

static int32_t score_template(Engine *eng, const Template *t){
    int32_t s = t->base_score;
    uint32_t tid = usage_id(PE_USAGE_TEMPLATE, t->id);
    const UtterancePlan *p = &eng->plan;
    /* legacy intent + matched_group still contribute (kept for back-compat with corpus) */
    if (t->intent == eng->state.current_intent) s += 80;
    if (t->group   == eng->matched_group)       s += 60;
    if (t->source == PE_TEMPLATE_SRC_CARTRIDGE) s += 20;
    /* v2: rhetorical-mode + stance fit */
    if (t->rhetorical_mask & (1u << p->rhetorical_mode)) s += 120;
    if (t->stance_mask     & (1u << p->stance))          s += 60;
    /* drive bias */
    if (t->drive_bias_id >= 0 && t->drive_bias_id < PE_DRIVE_COUNT)
        s += eng->state.drive_values[t->drive_bias_id] / 20;
    /* personality fit */
    if (t->intent == PE_INTENT_MONOLOGUE) s += eng->identity.extraversion / 1024;
    if (t->intent == PE_INTENT_REMINISCE) s += eng->identity.openness     / 1024;
    /* v2: plan-shape fit — closer min_* to plan values = better */
    s += (int32_t)((p->certainty     - t->min_certainty)     / 8);
    s += (int32_t)((p->aggression    - t->min_aggression)    / 8);
    s += (int32_t)((p->theatricality - t->min_theatricality) / 8);
    /* obsession pressure favors monologue/reminisce */
    if (eng->state.obsession_pressure > 500
        && (t->intent == PE_INTENT_MONOLOGUE || t->intent == PE_INTENT_REMINISCE))
        s += eng->state.obsession_pressure / 8;
    /* repetition penalty */
    int pen = repetition_penalty_pct(eng, tid);
    s = (s * 100) / pen;
    /* v2.1: LM "Pretorianness" bonus on the raw template text.
     * Normalized score is per-char milli-nats; center at -1500, scale /10.
     *   Pretorian ≈ -500  → +100
     *   neutral   ≈ -1500 →    0
     *   off-register ≈ -3000 → -150
     * Skipped if LM not loaded (eng->lm == NULL). */
    if (eng->lm && t->text[0]){
        int32_t lm = ngram_lm_score_normalized(eng->lm, t->text);
        s += (lm + 1500) / 10;
    }
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
    eng->last_template_group = 0xFFFF;
    eng->last_template_intent = 0xFFFF;

    if (eng->state.current_intent == PE_INTENT_PAUSE){
        if (n > 0) out[0] = 0;
        eng->last_template_intent = PE_INTENT_PAUSE;
        return 0;
    }

    {
        uint32_t fatigue = (eng->environment.turns_this_session * 256u) / 1000u;
        if (fatigue > 255u) fatigue = 255u;
        if (eng->matched_group == 0xFFFF
            && (persona_rng_u32(&eng->state) & 255u) < fatigue){
            static const char *flaws[] = {
                "...",
                "I don't want to talk about that.",
                "Let's change the subject.",
                "Okay.",
                "No.",
                "Maybe."
            };
            uint32_t r = persona_rng_u32(&eng->state) % (uint32_t)(sizeof(flaws)/sizeof(flaws[0]));
            snprintf(out, n, "%s", flaws[r]);
            eng->last_template_intent = eng->state.current_intent;
            return 0;
        }
    }

    /* collect candidates.  If the classifier found a concrete dialogue
     * group, try that group first so "hello" and "how are you" are not
     * drowned out by generic mood/intent monologues.  If the group has no
     * admissible lines, fall back to the old intent-driven search. */
    for (int pass = 0; pass < 2 && eng->candidate_count == 0; ++pass){
        int prefer_group = (eng->matched_group != 0xFFFF && pass == 0);
        for (uint32_t i = 0; i < eng->templates.count && eng->candidate_count < 64; ++i){
            const Template *t = &eng->templates.entries[i];
            if (!t->text[0]) continue;
            if (!template_admissible(eng, t)) continue;
            int relevant = 0;
            if (prefer_group) {
                relevant = (t->group == eng->matched_group);
            } else {
                if (eng->matched_group != 0xFFFF) {
                    if (t->group == eng->matched_group) relevant = 1;
                    if (t->intent == eng->state.current_intent) relevant = 1;
                } else {
                    if (t->group == 0xFFFF && t->intent == eng->state.current_intent) relevant = 1;
                }
            }
            if (!relevant) continue;
            if (phrase_recently_used(eng, usage_id(PE_USAGE_TEMPLATE, t->id), 12u)) continue;
            eng->candidate_ids[eng->candidate_count] = (uint16_t)i;
            eng->candidate_scores[eng->candidate_count] = score_template(eng, t);
            if (prefer_group) {
                eng->candidate_scores[eng->candidate_count] += 140;
                if (eng->matched_group >= PE_BL_GROUP_BASE)
                    eng->candidate_scores[eng->candidate_count] += 180;
            }
            eng->candidate_count++;
        }
        if (eng->matched_group == 0xFFFF) break;
    }

    /* fallback if nothing matched */
    if (eng->candidate_count == 0){
        const char *line = fallback_line(eng);
        uint32_t line_id = usage_id(PE_USAGE_FALLBACK, persona_hash(line));
        fill_text_slots(eng, line, line_id, out, n);
        eng->last_template_intent = eng->state.current_intent;
        /* still record repetition usage so fallbacks vary */
        record_use(&eng->memory, line_id, eng->state.turn_count);
        return 0;
    }

    /* v3.2: The Voice — counterfactual rerank before top-3 selection.
     * Adjusts candidate_scores[] by predicted-goal-alignment, so the
     * top-3 pick reflects "what's the smart thing to say" not just
     * "what fits my current mood." */
    pe_voice_rerank(eng);

    if (eng->matched_group != 0xFFFF && eng->input_class != 2 && eng->input_class != 4){
        for (uint16_t i = 0; i < eng->candidate_count; ++i){
            const Template *t = &eng->templates.entries[eng->candidate_ids[i]];
            if (t->group != eng->matched_group) continue;
            if (t->intent == PE_INTENT_ANSWER) {
                eng->candidate_scores[i] += 260;
            } else if (t->intent == PE_INTENT_MONOLOGUE
                    || t->intent == PE_INTENT_REMINISCE
                    || t->intent == PE_INTENT_BOAST) {
                eng->candidate_scores[i] -= 180;
            }
        }
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
    eng->last_template_group = chosen->group;
    eng->last_template_intent = chosen->intent;

    /* fill slots, then style transforms */
    char buf[PE_TEMPLATE_TEXT];
    int grounded_turn = (eng->matched_group != 0xFFFF
                      && chosen->group == eng->matched_group
                      && eng->input_class != 2
                      && eng->input_class != 4);
    fill_slots(eng, chosen, buf, sizeof(buf));
    apply_style(eng, chosen, buf, sizeof(buf));

    /* v2.1: procedural splice-marker expansion. Templates may carry
     * [bank_name] markers (e.g. [adj_morbid]); the mutator replaces them
     * with plan-gated synonyms. RNG seeded from (today_seed XOR turn) so
     * the expansion is deterministic per replay.
     * Templates without markers pass through unchanged. */
    {
        char mut_out[PE_TEMPLATE_TEXT];
        uint32_t mrng = eng->state.today_seed
                      ^ (eng->state.turn_count * 2654435761u);
        if (mrng == 0) mrng = 0xA5A5A5A5u;
        /* v3.2: route through the cartridge-borne BankRegistry so each
         * character's synonym set lives in its own banks.bin, not the
         * engine binary. */
        size_t mw = mutator_expand_banks(buf, mut_out, sizeof(mut_out),
                                         eng->plan.theatricality,
                                         eng->plan.aggression, &mrng,
                                         &eng->banks);
        if (mw > 0){
            memcpy(buf, mut_out, mw + 1);
        }
    }

    /* topic callback (illusion layer): inject "by the way..." once in a while */
    if ((eng->identity.voice_flags & PE_VF_ALLOW_CALLBACK)
        && !grounded_turn
        && eng->input_class != 2
        && eng->input_class != 4
        && (persona_rng_u32(&eng->state) % 100) < 10){
        for (int i = 0; i < PE_TOPIC_SLOTS; ++i){
            if (eng->state.topic_momentum[i].momentum > 600
                && eng->state.topic_momentum[i].topic_id != eng->primary_topic){
                const char *tn = "the matter";
                for (uint32_t k = 0; k < eng->topics.count; ++k)
                    if (eng->topics.topics[k].id == eng->state.topic_momentum[i].topic_id){
                        tn = eng->topics.topics[k].name; break;
                    }
                if (!strcmp(tn, "gin")) continue;
                uint32_t cbid = usage_id(PE_USAGE_CALLBACK, eng->state.topic_momentum[i].topic_id);
                if (phrase_recently_used(eng, cbid, 8u)) continue;
                size_t L = strlen(buf);
                if (L + 64 < sizeof(buf)){
                    snprintf(buf + L, sizeof(buf) - L,
                             " But come, let us return to %s.", tn);
                    record_use(&eng->memory, cbid, eng->state.turn_count);
                }
                break;
            }
        }
    }

    /* tiny contradiction (2% with flag) — flagged in memory by stamping type=2 */
    if ((eng->identity.voice_flags & PE_VF_ALLOW_CONTRADICT)
        && !grounded_turn
        && (persona_rng_u32(&eng->state) % 100) < 2
        && eng->memory.episodic_count > 0){
        size_t L = strlen(buf);
        if (L + 16 < sizeof(buf))
            snprintf(buf + L, sizeof(buf) - L, " (Or perhaps the opposite.)");
    }

    snprintf(out, n, "%s", buf);
    record_use(&eng->memory, usage_id(PE_USAGE_TEMPLATE, chosen->id), eng->state.turn_count);
    return 0;
}
