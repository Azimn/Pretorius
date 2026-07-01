/* engine.c — process_input main loop, drives, mood, lifecycle. */
#include "persona.h"
#include "persona_internal.h"
#include "cartridge.h"
#include "ngram_lm.h"            /* v2.1: optional plasticity */
#include "mutator.h"             /* v3.2: cartridge banks */
#ifndef PE_DISABLE_AETHER
#include "aether.h"              /* v3.2: long-term episodic storage */
#endif
#include "identity.h"
#include "environment.h"
#include "engine_clock.h"        /* canonical Layer 1 clock */
#include "vitality.h"            /* V7.2 cartridge vitality profile */
#include "turn_drama.h"          /* V7 turn drama synthesis */
#include "../render/render_backend.h"   /* v4: renderer dispatch */
#include "../render/prompt_compiler.h"  /* v6 packet experiment */
#include "../memory/affect_curve.h"     /* v4: nonlinear affect */
#include "../memory/reflection.h"       /* v5: reflective consolidation */
#include "../memory/affect_dynamics.h"
#include "../memory/recall_plasticity.h" /* v5: recall-coupled plasticity */
#include "../instrumentation/state_trace.h"  /* v4: observability */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>
#include <ctype.h>
#include <sys/stat.h>

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

static int load_identity_section(const char *root, int is_cart, Identity *out){
    void *buf = NULL;
    size_t sz = 0;
    size_t v4_size = offsetof(Identity, current_preoccupations);
    size_t v6_pre_cold_open_size = offsetof(Identity, cold_open_memory_templates);
    size_t v6_pre_mind_size = offsetof(Identity, suggestibility);
    size_t v6_pre_drift_size = offsetof(Identity, drift_malleability);
    int rc;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (is_cart) {
        rc = pe_cart_load_section_alloc(root, "identity.bin", &buf, &sz);
        if (rc != 0) return rc;
    } else {
        char path[512];
        FILE *f;
        long n;
        if (pe_path_join(path, sizeof(path), root, "identity.bin") != 0) return -1;
        f = fopen(path, "rb");
        if (!f) return -2;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n < 0){ fclose(f); return -3; }
        buf = malloc((size_t)n ? (size_t)n : 1u);
        if (!buf){ fclose(f); return -4; }
        if (fread(buf, 1, (size_t)n, f) != (size_t)n){
            free(buf);
            fclose(f);
            return -5;
        }
        fclose(f);
        sz = (size_t)n;
    }
    if (sz == sizeof(Identity) || sz == v6_pre_drift_size || sz == v6_pre_mind_size
        || sz == v6_pre_cold_open_size || sz == v4_size){
        memcpy(out, buf, sz);
        if (sz <= v6_pre_drift_size)
            out->drift_malleability = 80;
        free(buf);
        return 0;
    }
    free(buf);
    return -10;
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

static int pe_packet_trace_enabled(void){
    const char *e = getenv("V6_PACKET_TRACE");
    if (e && e[0] && strcmp(e, "0") != 0) return 1;
    return v6_packet_mode_is_situation();
}

static void pe_json_escape(FILE *f, const char *s){
    if (!f) return;
    if (!s) s = "";
    for (; *s; ++s){
        unsigned char c = (unsigned char)*s;
        switch (c){
        case '\\': fputs("\\\\", f); break;
        case '"':  fputs("\\\"", f); break;
        case '\n': fputs("\\n", f);  break;
        case '\r': fputs("\\r", f);  break;
        case '\t': fputs("\\t", f);  break;
        default:
            if (c < 32) fprintf(f, "\\u%04x", (unsigned)c);
            else fputc((int)c, f);
            break;
        }
    }
}

static const char *pe_packet_model_name(void){
    const char *m = getenv("PE_OLLAMA_MODEL");
    if (m && m[0]) return m;
    m = getenv("PE_SLM_MODEL");
    if (m && m[0]) return m;
    m = getenv("PE_API_MODEL");
    if (m && m[0]) return m;
    return "";
}

static const char *pe_audit_result_name(uint8_t r){
    switch (r){
    case PE_AUDIT_PASS: return "pass";
    case PE_AUDIT_REPAIRED: return "repaired";
    case PE_AUDIT_FALLBACK: return "fallback";
    default: return "unknown";
    }
}

static const char *pe_audit_violation_name(uint8_t v){
    switch (v){
    case PE_AUDIT_V_SPEECH_ACT:   return "speech_act_mismatch";
    case PE_AUDIT_V_EMPTY:        return "empty_output";
    case PE_AUDIT_V_SELF_REPEAT:  return "self_repeat";
    case PE_AUDIT_V_FATIGUE:      return "fatigue_terms";
    case PE_AUDIT_V_META:         return "meta_or_assistant_tone";
    case PE_AUDIT_V_LORE:         return "lore_drift";
    case PE_AUDIT_V_COPY:         return "copied_user_text";
    case PE_AUDIT_V_OUTPUT_LABEL: return "memory_label";
    case PE_AUDIT_V_ADDRESSEE:    return "wrong_addressee";
    case PE_AUDIT_V_PRIVATE_LEAK: return "private_state_leak";
    case PE_AUDIT_V_FLAT:         return "flat";
    case PE_AUDIT_V_ECHO:         return "echo";
    default:                      return "none";
    }
}

static int pe_audit_violation_is_hard(uint8_t v){
    switch (v){
    case PE_AUDIT_V_META:
    case PE_AUDIT_V_LORE:
    case PE_AUDIT_V_COPY:
    case PE_AUDIT_V_OUTPUT_LABEL:
        return 1;
    default:
        return 0;
    }
}

static void pe_packet_trace_write(const Engine *eng,
                                  const char *input_text,
                                  const V6UserTurnInterpretation *it,
                                  const char *backend_name,
                                  const char *raw_model_output,
                                  int renderer_output,
                                  const char *final_output,
                                  uint16_t pre_epi_count,
                                  uint32_t pre_next_memory_id,
                                  uint32_t pre_speech_count,
                                  uint32_t pre_open_total,
                                  uint32_t pre_habit_turns){
    const char *path;
    FILE *f;
    int memory_changed;
    int sidecars_changed;
    if (!eng || !pe_packet_trace_enabled()) return;
    mkdir("tmp", 0777);
    mkdir("tmp/packet_experiment", 0777);
    path = getenv("V6_PACKET_TRACE_PATH");
    if (!path || !path[0]) path = "tmp/packet_experiment/packet_trace.jsonl";
    f = fopen(path, "ab");
    if (!f) return;

    memory_changed = (pre_epi_count != eng->memory.episodic_count ||
                      pre_next_memory_id != eng->memory.next_memory_id);
    sidecars_changed = (pre_speech_count != pe_speech_ledger_count(&eng->speech_ledger) ||
                        pre_open_total != eng->open_loops.total_recorded ||
                        pre_habit_turns != eng->speech_habits.turns_observed);

    fprintf(f, "{\"timestamp_ms\":%llu",
            (unsigned long long)pe_clock_now_ms());
    fprintf(f, ",\"profile\":\"");
    pe_json_escape(f, eng->identity.character_name);
    fprintf(f, "\",\"input_text\":\"");
    pe_json_escape(f, input_text);
    fprintf(f, "\",\"packet_mode\":\"%s\"",
            it && it->packet_mode ? it->packet_mode : "current");
    fprintf(f, ",\"detected_user_act\":\"%s\"",
            it && it->user_act ? it->user_act : "unclear");
    fprintf(f, ",\"detected_conversational_pressure\":\"%s\"",
            it && it->pressure ? it->pressure : "");
    fprintf(f, ",\"open_loop_pressure\":%u",
            (unsigned)eng->frame.open_loop_pressure);
    fprintf(f, ",\"selected_goal\":%u,\"selected_intent\":%u",
            (unsigned)eng->state.current_goal,
            (unsigned)eng->state.current_intent);
    fprintf(f, ",\"retrieved_memories\":[");
    for (uint16_t i = 0, shown = 0; i < eng->active_count && shown < 4; ++i){
        uint16_t idx = eng->active_memories[i];
        const MemoryNode *m;
        if (idx >= PE_EPISODIC_MAX || idx >= eng->memory.episodic_count) continue;
        m = &eng->memory.episodic[idx];
        fprintf(f, "%s{\"id\":%u,\"topic\":%u,\"summary\":\"",
                shown ? "," : "", (unsigned)m->id, (unsigned)m->topic_id);
        pe_json_escape(f, m->summary);
        fprintf(f, "\"}");
        ++shown;
    }
    fprintf(f, "]");
    fprintf(f, ",\"response_move\":\"");
    pe_json_escape(f, it && it->response_move ? it->response_move : "");
    fprintf(f, "\",\"model_name\":\"");
    pe_json_escape(f, pe_packet_model_name());
    fprintf(f, "\",\"backend\":\"");
    pe_json_escape(f, backend_name ? backend_name : "");
    fprintf(f, "\",\"renderer_output\":%s", renderer_output ? "true" : "false");
    fprintf(f, ",\"raw_model_output\":\"");
    pe_json_escape(f, raw_model_output);
    fprintf(f, "\",\"audit_result\":\"%s\"",
            pe_audit_result_name(eng->last_audit_result));
    fprintf(f, ",\"audit_violation\":\"%s\"",
            pe_audit_violation_name(eng->last_audit_violation));
    fprintf(f, ",\"audit_hardness\":\"%s\"",
            eng->last_audit_hardness == PE_AUDIT_HARD ? "hard" : "soft");
    fprintf(f, ",\"constrained_rewrite\":%s",
            eng->last_audit_rewrite ? "true" : "false");
    fprintf(f, ",\"final_output\":\"");
    pe_json_escape(f, final_output);
    fprintf(f, "\",\"layer1_memory_changed\":%s",
            memory_changed ? "true" : "false");
    fprintf(f, ",\"sidecars_changed\":%s",
            sidecars_changed ? "true" : "false");
    fprintf(f, "}\n");
    fclose(f);
}

static uint16_t pe_neglected_want_index(const Engine *eng){
    uint16_t best = 0xFFFF;
    uint32_t best_score = 0;
    for (uint16_t i = 0; i < PE_WANT_COUNT; ++i){
        const CharacterWant *w = &eng->identity.wants[i];
        if (!w->name[0]) continue;
        if (w->target_topic_id == 0xFFFF || w->target_topic_id == 0) continue;
        uint32_t age = eng->state.want_turns_since_engaged[i];
        uint32_t intensity = w->intensity ? w->intensity : 100u;
        uint32_t score = age * intensity;
        if (score > best_score){
            best_score = score;
            best = i;
        }
    }
    return best_score >= 600u ? best : 0xFFFF;
}

static const char *pe_cold_open_address(const Engine *eng){
    uint32_t modulus = pe_allow_intimate_address(eng) ? PE_ADDRESS_COUNT : 2u;
    const char *address = eng->identity.address_user_as[
        (eng->state.today_seed ^ eng->state.turn_count) % modulus
    ];
    return address[0] ? address : "you";
}

static int pe_is_generic_actor_name(const char *name){
    if (!name || !name[0]) return 1;
    return pe_strieq(name, "Someone")
        || pe_strieq(name, "You")
        || pe_strieq(name, "User")
        || pe_strieq(name, "Visitor")
        || pe_strieq(name, "the visitor");
}

static size_t pe_append_token(char *dst, size_t cap, size_t pos, const char *s){
    while (s && *s && pos + 1 < cap) dst[pos++] = *s++;
    if (pos < cap) dst[pos] = 0;
    return pos;
}

static void pe_fill_cold_open_surface(Engine *eng, const char *src,
                                      const char *name, const char *topic,
                                      const char *memory_summary,
                                      char *out, size_t n){
    size_t pos = 0;
    const char *address = pe_cold_open_address(eng);
    if (n > 0) out[0] = 0;
    while (src && *src && pos + 1 < n){
        if (!strncmp(src, "{name}", 6)){
            pos = pe_append_token(out, n, pos, name ? name : "");
            src += 6;
        } else if (!strncmp(src, "{topic}", 7)){
            pos = pe_append_token(out, n, pos, topic ? topic : "the matter");
            src += 7;
        } else if (!strncmp(src, "{address}", 9)){
            pos = pe_append_token(out, n, pos, address);
            src += 9;
        } else if (!strncmp(src, "{memory_summary}", 16)){
            pos = pe_append_token(out, n, pos,
                                  memory_summary ? memory_summary : "");
            src += 16;
        } else {
            out[pos++] = *src++;
        }
    }
    if (pos < n) out[pos] = 0;
}

static int pe_pick_cold_open_surface(Engine *eng, int case_id,
                                     const char *name, const char *topic,
                                     const char *memory_summary,
                                     uint16_t mem_index,
                                     char *out, size_t n){
    if (!eng || !out || n == 0 || case_id < 0 || case_id >= PE_COLD_OPEN_CASES)
        return -1;
    uint8_t available[PE_COLD_OPEN_VARIANTS];
    uint8_t count = 0;
    for (uint8_t i = 0; i < PE_COLD_OPEN_VARIANTS; ++i){
        if (eng->identity.cold_open_memory_templates[case_id][i][0])
            available[count++] = i;
    }
    if (count == 0) return -1;
    uint32_t seed = eng->state.today_seed
                  ^ eng->relation.user_hash
                  ^ ((uint32_t)mem_index * 2654435761u)
                  ^ (eng->state.turn_count * 2246822519u)
                  ^ (uint32_t)case_id;
    uint8_t pick_pos = (uint8_t)(seed % count);
    uint8_t idx = available[pick_pos];
    pe_fill_cold_open_surface(eng,
        eng->identity.cold_open_memory_templates[case_id][idx],
        name, topic, memory_summary, out, n);
    eng->cold_open_callback_source = 2;
    eng->cold_open_callback_template_index = idx;
    return idx;
}

static int pe_memory_can_own_cold_open(const MemoryNode *m){
    if (!m) return 0;
    if (m->topic_id != 0xFFFFu) return 1;
    size_t summary_len = strnlen(m->summary, sizeof(m->summary));
    if (summary_len >= 48u
        && m->salience >= 180u
        && (m->emotion.arousal >= 45
            || m->emotion.valence >= 40
            || m->emotion.valence <= -40))
        return 1;
    return 0;
}

static void pe_update_character_wants(Engine *eng){
    if (!eng) return;
    for (uint16_t i = 0; i < PE_WANT_COUNT; ++i){
        const CharacterWant *w = &eng->identity.wants[i];
        if (!w->name[0]){
            eng->state.want_turns_since_engaged[i] = 0;
            continue;
        }
        int engaged = 0;
        if (w->target_topic_id != 0xFFFF && w->target_topic_id != 0
            && eng->primary_topic == w->target_topic_id)
            engaged = 1;
        if (w->target_pattern_class != 0
            && eng->input_class == w->target_pattern_class)
            engaged = 1;
        if (engaged) {
            eng->state.want_turns_since_engaged[i] = 0;
        } else if (eng->state.want_turns_since_engaged[i] < 0xFFFFu) {
            eng->state.want_turns_since_engaged[i]++;
        }
    }

    if (eng->input_class == 0 && eng->matched_group == 0xFFFF){
        uint16_t idx = pe_neglected_want_index(eng);
        if (idx != 0xFFFF){
            const CharacterWant *w = &eng->identity.wants[idx];
            pe_boost_topic(&eng->state, w->target_topic_id,
                           80 + (int)(w->intensity ? w->intensity : 100u));
        }
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

static void lowercase_copy(char *dst, size_t n, const char *src){
    size_t i = 0;
    if (!dst || n == 0) return;
    if (!src) src = "";
    for (; src[i] && i + 1 < n; ++i){
        unsigned char c = (unsigned char)src[i];
        dst[i] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    dst[i] = 0;
}

static int has_any_token(const char *s, const char *const *tokens, size_t count){
    for (size_t i = 0; i < count; ++i)
        if (tokens[i] && strstr(s, tokens[i])) return 1;
    return 0;
}

static uint16_t pe_highest_want_pressure(const Engine *eng, uint8_t *reason_out){
    uint32_t best_score = 0;
    if (reason_out) *reason_out = PE_SOV_NONE;
    if (!eng) return 0;
    for (uint16_t i = 0; i < PE_WANT_COUNT; ++i){
        const CharacterWant *w = &eng->identity.wants[i];
        uint32_t intensity, score;
        if (!w->name[0]) continue;
        intensity = w->intensity ? w->intensity : 100u;
        score = (uint32_t)eng->state.want_turns_since_engaged[i] * intensity;
        if (score > best_score) best_score = score;
    }
    if (best_score > 1000u) best_score = 1000u;
    if (best_score && reason_out) *reason_out = PE_SOV_WANT;
    return (uint16_t)best_score;
}

static uint16_t pe_self_image_threat_pressure(const Engine *eng){
    uint32_t p;
    if (!eng) return 0;
    p = (uint32_t)eng->dissonance.ideal_gap + (uint32_t)eng->dissonance.feared_gap;
    return (uint16_t)(p > 1000u ? 1000u : p);
}

static uint16_t pe_normalized_sovereignty_threshold(const Engine *eng){
    uint16_t t;
    if (!eng) return 500u;
    t = eng->identity.sovereignty_threshold;
    if (t == 0u) return 500u;
    if (t > 1000u) return 1000u;
    return t;
}

static void pe_apply_sovereign_override(Engine *eng){
    const pe_open_loop_t *loop;
    uint16_t ol_pressure = 0, self_pressure, want_pressure, threshold;
    uint32_t total;
    uint8_t reason = PE_SOV_NONE;
    uint8_t want_reason = PE_SOV_NONE;
    if (!eng) return;
    eng->state.sovereign_override = 0;
    eng->state.sovereign_reason = PE_SOV_NONE;
    if (eng->state.current_intent == PE_INTENT_PAUSE ||
        eng->state.current_intent == PE_INTENT_WITHDRAW)
        return;
    threshold = pe_normalized_sovereignty_threshold(eng);
    loop = pe_open_loops_latest_for_actor(&eng->open_loops, eng->relation.user_hash);
    if (loop){
        ol_pressure = pe_open_loop_pressure(loop, eng->state.turn_count);
        if (ol_pressure) reason = PE_SOV_OPEN_LOOP;
    }
    self_pressure = pe_self_image_threat_pressure(eng);
    if (self_pressure > ol_pressure) reason = PE_SOV_SELF_IMAGE;
    want_pressure = pe_highest_want_pressure(eng, &want_reason);
    if (want_pressure > ol_pressure && want_pressure > self_pressure) reason = want_reason;
    total = (uint32_t)ol_pressure + (uint32_t)self_pressure + (uint32_t)want_pressure;
    if (total > 3000u) total = 3000u;
    if (total > threshold){
        eng->state.current_intent = PE_INTENT_INITIATE;
        eng->state.sovereign_override = 1;
        eng->state.sovereign_reason = reason ? reason : PE_SOV_OPEN_LOOP;
        if (loop && loop->target_topic_id != 0xFFFFu)
            eng->primary_topic = loop->target_topic_id;
    }
}
static void pe_build_turn_frame(Engine *eng){
    CanonicalTurnFrame *f = &eng->frame;
    memset(f, 0, sizeof(*f));
    f->actor_id            = eng->relation.user_hash;
    f->input_class         = (uint8_t)eng->input_class;
    f->primary_topic       = eng->primary_topic;
    f->selected_goal       = eng->state.current_goal;
    f->selected_intent     = (uint8_t)eng->state.current_intent;
    f->speech_act          = pe_speech_act_from_intent(eng->state.current_intent);
    f->stance              = (uint8_t)eng->plan.stance;
    f->rhetorical_mode     = eng->plan.rhetorical_mode;
    f->recall_mode         = eng->current_recall_mode;
    for (int i = 0; i < PE_SPEECH_FATIGUE_TERMS; ++i){
        if (eng->speech_habits.fatigue_terms[i][0]
            && eng->speech_habits.fatigue_hits[i] >= 2u
            && f->fatigue_term_count < PE_SPEECH_FATIGUE_TERMS){
            snprintf(f->fatigue_terms[f->fatigue_term_count],
                     PE_SPEECH_FATIGUE_LEN, "%s",
                     eng->speech_habits.fatigue_terms[i]);
            f->fatigue_term_count++;
        }
    }
    f->relation_trust      = eng->relation_dims.trust;
    f->relation_threat     = eng->relation_dims.threat;
    f->relation_intimacy   = eng->relation_dims.intimacy;
    f->relation_resentment = eng->relation_dims.resentment;
    f->relation_obligation = eng->relation_dims.obligation;
    f->relation_dependency = eng->relation_dims.dependency;
    f->relation_envy       = eng->relation_dims.envy;
    f->relation_admiration = eng->relation_dims.admiration;
    f->relation_embarrassment = eng->relation_dims.embarrassment;
    f->ideal_gap           = eng->dissonance.ideal_gap;
    f->ought_gap           = eng->dissonance.ought_gap;
    f->feared_gap          = eng->dissonance.feared_gap;
    f->withhold_reason     = PE_WR_NONE;
    f->max_words           = (eng->state.current_intent == PE_INTENT_MONOLOGUE ||
                              eng->state.current_intent == PE_INTENT_REMINISCE) ? 48u : 32u;
    f->require_question    = (f->speech_act == PE_SA_QUESTION) ? 1u : 0u;
    f->allow_empty         = (f->speech_act == PE_SA_PAUSE) ? 1u : 0u;
    f->forbid_meta         = 1u;
    {
        const pe_open_loop_t *loop =
            pe_open_loops_latest_for_actor(&eng->open_loops, eng->relation.user_hash);
        f->open_loop_pressure = pe_open_loop_pressure(loop, eng->state.turn_count);
    }
    for (uint16_t i = 0; i < eng->active_count && f->selected_memory_count < 4; ++i)
        f->selected_memories[f->selected_memory_count++] = eng->active_memories[i];
}

enum {
    PE_PRIVATE_THOUGHT_NONE = 0,
    PE_PRIVATE_THOUGHT_ATTEND_USER = 1,
    PE_PRIVATE_THOUGHT_OPEN_LOOP = 2,
    PE_PRIVATE_THOUGHT_DISSONANCE = 3,
    PE_PRIVATE_THOUGHT_OBSESSION = 4,
    PE_PRIVATE_THOUGHT_FATIGUE = 5,
    PE_PRIVATE_THOUGHT_WITHHOLD = 6
};

static const char *pe_private_thought_kind_name(uint8_t kind){
    static const char *names[] = {
        "none", "attend_user", "open_loop", "dissonance",
        "obsession", "fatigue", "withhold"
    };
    if (kind < (sizeof(names) / sizeof(names[0]))) return names[kind];
    return "unknown";
}

static int16_t pe_trait_to_self_axis(uint16_t q16){
    int32_t v = ((int32_t)q16 * 2000) / 65535 - 1000;
    if (v < -1000) v = -1000;
    if (v >  1000) v =  1000;
    return (int16_t)v;
}

static void pe_seed_typed_self_model(Engine *eng){
    if (!eng) return;
    if (eng->dissonance.ideal_self_model != 0
        || eng->dissonance.ought_self_model != 0
        || eng->dissonance.feared_self_model != 0)
        return;

    /* Compact, cartridge-neutral defaults derived from authored Big Five.
     * Cartridge-authored symbolic self fields can replace this later, but
     * every profile gets a deterministic self-model now. */
    eng->dissonance.ideal_self_model =
        (int16_t)((pe_trait_to_self_axis(eng->identity.openness) +
                  pe_trait_to_self_axis(eng->identity.extraversion)) / 2);
    eng->dissonance.ought_self_model =
        (int16_t)((pe_trait_to_self_axis(eng->identity.conscientiousness) +
                  pe_trait_to_self_axis(eng->identity.agreeableness)) / 2);
    eng->dissonance.feared_self_model =
        (int16_t)((pe_trait_to_self_axis(eng->identity.neuroticism) -
                  pe_trait_to_self_axis(eng->identity.agreeableness)) / 2);
}

static void pe_default_mind_affect_knobs(Engine *eng){
    if (!eng) return;
    if (eng->identity.suggestibility == 0)
        eng->identity.suggestibility =
            (uint16_t)(250u + ((uint32_t)eng->identity.agreeableness * 350u / 65535u));
    if (eng->identity.contagion_susceptibility == 0)
        eng->identity.contagion_susceptibility =
            (uint16_t)(180u + ((uint32_t)eng->identity.agreeableness * 360u / 65535u));
    if (eng->identity.forecast_horizon_weight == 0)
        eng->identity.forecast_horizon_weight =
            (uint16_t)(260u + ((uint32_t)eng->identity.neuroticism * 420u / 65535u));
    if (eng->identity.expression_mask_threshold == 0)
        eng->identity.expression_mask_threshold =
            (uint16_t)(360u + ((uint32_t)(65535u - eng->identity.agreeableness) * 220u / 65535u));
}

static void pe_update_private_thought_frame(Engine *eng){
    if (!eng) return;
    uint16_t pressure = 100u;
    uint8_t kind = PE_PRIVATE_THOUGHT_ATTEND_USER;

    if (eng->frame.open_loop_pressure > pressure){
        pressure = eng->frame.open_loop_pressure;
        kind = PE_PRIVATE_THOUGHT_OPEN_LOOP;
    }
    if (eng->frame.ideal_gap > pressure){
        pressure = eng->frame.ideal_gap;
        kind = PE_PRIVATE_THOUGHT_DISSONANCE;
    }
    if (eng->frame.ought_gap > pressure){
        pressure = eng->frame.ought_gap;
        kind = PE_PRIVATE_THOUGHT_DISSONANCE;
    }
    if (eng->frame.feared_gap > pressure){
        pressure = eng->frame.feared_gap;
        kind = PE_PRIVATE_THOUGHT_DISSONANCE;
    }
    if (eng->state.obsession_pressure > pressure){
        pressure = eng->state.obsession_pressure;
        kind = PE_PRIVATE_THOUGHT_OBSESSION;
    }
    if (eng->state.exhaustion > pressure){
        pressure = eng->state.exhaustion;
        kind = PE_PRIVATE_THOUGHT_FATIGUE;
    }
    if (pe_speech_act_is_withhold(eng->frame.speech_act)){
        kind = PE_PRIVATE_THOUGHT_WITHHOLD;
        if (pressure < 520u) pressure = 520u;
    }

    eng->private_thought_topic = eng->frame.primary_topic;
    eng->private_thought_pressure = pressure;
    eng->private_thought_kind = kind;
    eng->expressed_thought_kind = eng->frame.speech_act;
    eng->private_thought_withheld = pe_speech_act_is_withhold(eng->frame.speech_act) ? 1u : 0u;
    eng->expression_policy =
        expression_policy_decide(eng->state.mood, &eng->relation_dims,
                                 eng->identity.expression_mask_threshold,
                                 eng->private_thought_withheld ? PE_WR_PRIVACY : PE_WR_NONE);
    eng->private_thought_hash =
        persona_hash(pe_private_thought_kind_name(kind)) ^ ((uint32_t)pressure << 7) ^
        ((uint32_t)eng->frame.primary_topic << 17);
    eng->expressed_thought_hash =
        persona_hash(pe_speech_act_name(eng->frame.speech_act)) ^
        ((uint32_t)eng->frame.rhetorical_mode << 9) ^
        ((uint32_t)eng->frame.selected_intent << 19);
    if (eng->private_thought_hash == eng->expressed_thought_hash)
        eng->expressed_thought_hash ^= 0x51A7u;
}

static uint8_t pe_withhold_reason_from_frame(const Engine *eng,
                                             const CanonicalTurnFrame *f){
    if      (eng->state.exhaustion   > 600) return PE_WR_FATIGUE;
    else if (f->feared_gap > 400)           return PE_WR_SHAME;
    else if (eng->state.surprise_last > 500) return PE_WR_CONFUSION;
    else if (f->relation_trust < 300)       return PE_WR_DISTRUST;
    else if (f->rhetorical_mode == PE_RHET_DEFLECT) return PE_WR_STRATEGY;
    else                                    return PE_WR_PRIVACY;
}

static int word_in_text_ci(const char *text, const char *word, size_t wlen){
    if (!text || !word || wlen == 0) return 0;
    for (const char *p = text; *p; ++p){
        size_t i = 0;
        while (i < wlen && p[i]
            && tolower((unsigned char)p[i]) == tolower((unsigned char)word[i]))
            ++i;
        if (i == wlen) {
            unsigned char before = (p == text) ? 0 : (unsigned char)p[-1];
            unsigned char after = (unsigned char)p[i];
            if (!isalnum(before) && before != '_' && !isalnum(after) && after != '_')
                return 1;
        }
    }
    return 0;
}

static int starts_with_word_ci(const char *text, const char *word, size_t wlen){
    size_t i = 0;
    if (!text || !word || wlen == 0) return 0;
    while (*text && isspace((unsigned char)*text)) text++;
    while (i < wlen && text[i]
        && tolower((unsigned char)text[i]) == tolower((unsigned char)word[i]))
        ++i;
    if (i != wlen) return 0;
    return !isalnum((unsigned char)text[i]) && text[i] != '_';
}

static int lore_word_allowed(const Engine *eng, const char *user_input,
                             const char *word, size_t wlen){
    static const char *common[] = {
        "I","A","An","The","This","That","These","Those","He","She",
        "We","You","It","They","Yes","No","Oh","Ah","But","And","Or","If",
        "In","On","Of","To","For","From","With","Without","When","Why",
        "What","How","Where","Who","Do","Does","Did","Is","Are","Was",
        "Were","Will","Would","Could","Should","Perhaps","Indeed","Well",
        "So","Now","Then","Still","Listen","Tell","Try","Go","Come","Look",
        "Wait","Good","Evening","Morning","Doctor","Dr","My","Your","Our",
        NULL
    };
    if (!eng || !word || wlen == 0) return 0;
    for (int i = 0; common[i]; ++i){
        if (strlen(common[i]) == wlen && !strncmp(common[i], word, wlen))
            return 1;
    }
    if (word_in_text_ci(eng->identity.character_name, word, wlen))
        return 1;
    if (word_in_text_ci(eng->relation.known_as, word, wlen))
        return 1;
    for (int i = 0; i < PE_ADDRESS_COUNT; ++i){
        if (word_in_text_ci(eng->identity.address_user_as[i], word, wlen))
            return 1;
    }
    for (uint32_t i = 0; i < eng->topics.count; ++i){
        if (word_in_text_ci(eng->topics.topics[i].name, word, wlen))
            return 1;
    }
    for (uint8_t i = 0; i < eng->identity.core_memory_count; ++i){
        if (word_in_text_ci(eng->identity.core_memories_seed[i].summary, word, wlen))
            return 1;
    }
    for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
        if (word_in_text_ci(eng->memory.episodic[i].summary, word, wlen))
            return 1;
    }
    if (word_in_text_ci(user_input, word, wlen))
        return 1;
    return 0;
}

static int pe_lore_audit_pass(const Engine *eng, const char *user_input,
                              const char *out){
    if (!eng || !out || !out[0]) return 1;
    const char *p = out;
    while (*p){
        if (isupper((unsigned char)*p)){
            size_t len = 0;
            while (isalpha((unsigned char)p[len])) ++len;
            if (len >= 3){
                char word[32];
                size_t cl = len < sizeof(word) - 1u ? len : sizeof(word) - 1u;
                memcpy(word, p, cl);
                word[cl] = 0;
                int all_caps = 1;
                for (size_t j = 0; j < cl; ++j){
                    if (isalpha((unsigned char)word[j])
                        && !isupper((unsigned char)word[j])){
                        all_caps = 0;
                        break;
                    }
                }
                if (all_caps) {
                    p += len;
                    continue;
                }
                if (!lore_word_allowed(eng, user_input, word, cl))
                    return 0;
            }
            p += len ? len : 1u;
        } else {
            ++p;
        }
    }
    return 1;
}

static int pe_input_allows_general_model_knowledge(const char *input){
    char low[384];
    if (!input || !input[0]) return 0;
    lowercase_copy(low, sizeof(low), input);

    /* Keep identity, relationship, and remembered-continuity probes strict.
     * Those are character-soul claims, not general knowledge questions. */
    if (strstr(low, "remember") || strstr(low, "recall") ||
        strstr(low, "last time") || strstr(low, "earlier") ||
        strstr(low, "our past") || strstr(low, "your past") ||
        strstr(low, "your memory") || strstr(low, "who am i") ||
        strstr(low, "who are you") || strstr(low, "are you real"))
        return 0;

    if (!strchr(input, '?')) return 0;
    return strstr(low, "teach me") || strstr(low, "explain") ||
           strstr(low, "how do") || strstr(low, "how does") ||
           strstr(low, "how to") || strstr(low, "what is") ||
           strstr(low, "what are") || strstr(low, "tell me about") ||
           strstr(low, "can you teach") || strstr(low, "can you explain");
}

static int pe_input_is_memory_commit_request(const char *input){
    char low[384];
    if (!input || !input[0]) return 0;
    lowercase_copy(low, sizeof(low), input);
    return strstr(low, "remember this") || strstr(low, "remember that") ||
           strstr(low, "please remember") || strstr(low, "can you remember") ||
           strstr(low, "do not forget") || strstr(low, "don't forget") ||
           starts_with_word_ci(input, "remember", 8);
}

static int pe_copy_audit_pass(const char *input, const char *out);
static int pe_echo_audit_pass(const char *input, const char *out);
static int pe_output_label_audit_pass(const char *out);
static int pe_self_repeat_audit_pass(const char *out);
static int pe_fatigue_audit_pass(const CanonicalTurnFrame *f, const char *out);
static int pe_flat_audit_pass(const Engine *eng, const char *out, int renderer_output);
static int pe_addressee_audit_pass(const Engine *eng, const char *out);
static int pe_private_state_audit_pass(const Engine *eng, const char *out);
static int pe_lk_topic_key_for_input(const Engine *eng, const char *text,
                                     char *topic_key, size_t topic_cap,
                                     uint16_t *topic_id);

static uint8_t pe_render_audit_violation(const CanonicalTurnFrame *f, const char *out,
                                         const char *input){
    char low[PE_RENDER_MAX_TEXT];
    const char *apology[]    = {"sorry","apolog","forgive","regret"};
    const char *refusal[]    = {"no","not","refuse","will not","won't","cannot","shall not"};
    const char *accusation[] = {"you ","your ","wrong","lie","deceiv","coward","fool","insult"};
    const char *threat[]     = {"careful","beware","threat","danger","destroy","hurt","ruin","stop you"};
    const char *praise[]     = {"good","excellent","brilliant","admire","impressive","credit","rare"};
    const char *confess[]    = {"confess","admit","truth","i have","i did","i fear"};
    const char *concede[]    = {"concede","granted","perhaps","you may be right","fair"};
    const char *promise[]    = {"promise","i will","i shall","count on"};
    const char *evasion[]    = {"perhaps","another time","not tonight","delicate","oblique"};
    const char *deflect[]    = {"instead","leave that","another matter","turn to","not the point"};
    const char *withdraw[]   = {"enough","leave","not now","silence","go"};
    if (!f || !out) return PE_AUDIT_V_EMPTY;
    lowercase_copy(low, sizeof(low), out);
    if (!f->allow_empty && low[0] == 0) return PE_AUDIT_V_EMPTY;
    if (f->forbid_meta && (has_any_token(low, (const char*[]){"as an ai","language model","how can i help","let me know"}, 4)
        || pe_vitality_text_has_assistant_leak(out)))
        return PE_AUDIT_V_META;
    if (input && !pe_echo_audit_pass(input, out)) return PE_AUDIT_V_ECHO;
    switch (f->speech_act){
    case PE_SA_APOLOGY:    return has_any_token(low, apology, 4) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_REFUSAL:    return has_any_token(low, refusal, 7) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_QUESTION:   return (strchr(out, '?') ||
                                   has_any_token(low, (const char*[]){"what ","why ","how ","tell me","would you","do you"}, 6) ||
                                   has_any_token(low, (const char*[]){"you asked me to remember","i remember","i recall"}, 3))
                                  ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_INSULT:     return has_any_token(low, accusation, 8) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_THREAT:     return has_any_token(low, threat, 8) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_PRAISE:     return has_any_token(low, praise, 7) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_CONFESSION: return has_any_token(low, confess, 6) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_CONCESSION: return has_any_token(low, concede, 5) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_PROMISE:    return has_any_token(low, promise, 4) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_EVASION:    return has_any_token(low, evasion, 5) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_DEFLECTION: return has_any_token(low, deflect, 5) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_PAUSE:      return (low[0] == 0 || strlen(low) < 12) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    case PE_SA_WITHDRAWAL: return (has_any_token(low, withdraw, 5) || strlen(low) < 80) ? PE_AUDIT_V_NONE : PE_AUDIT_V_SPEECH_ACT;
    default:
        return PE_AUDIT_V_NONE;
    }
}

static int pe_render_audit_pass(const CanonicalTurnFrame *f, const char *out){
    return pe_render_audit_violation(f, out, NULL) == PE_AUDIT_V_NONE;
}

static uint8_t pe_audit_evaluate(const Engine *eng,
                                 const char *input,
                                 const char *out,
                                 int renderer_output){
    uint8_t v = pe_render_audit_violation(&eng->frame, out, input);
    int allow_general_knowledge = pe_input_allows_general_model_knowledge(input);
    int memory_commit = pe_input_is_memory_commit_request(input);
    if (v == PE_AUDIT_V_META) return v;
    if (renderer_output && !allow_general_knowledge && !memory_commit &&
        !pe_lore_audit_pass(eng, input, out)) return PE_AUDIT_V_LORE;
    {
        char topic_key[PE_LK_TOPIC_LEN];
        uint16_t topic_id = 0xFFFFu;
        int topic_hit = pe_lk_topic_key_for_input(eng, input,
                                                  topic_key, sizeof(topic_key),
                                                  &topic_id);
        (void)topic_id;
        if (renderer_output && topic_hit
        && pe_lk_output_repeats_corrected_claim(&eng->learned_knowledge,
                                                out, topic_key))
            return PE_AUDIT_V_LORE;
        if (renderer_output && topic_hit
        && pe_lk_output_conflicts_authority(&eng->learned_knowledge,
                                            out, topic_key,
                                            PE_LK_SCOPE_REAL_WORLD,
                                            eng->relation.user_hash))
            return PE_AUDIT_V_LORE;
    }
    if (renderer_output && !memory_commit &&
        !pe_copy_audit_pass(input, out)) return PE_AUDIT_V_COPY;
    if (renderer_output && !pe_output_label_audit_pass(out)) return PE_AUDIT_V_OUTPUT_LABEL;
    if (renderer_output && !pe_addressee_audit_pass(eng, out)) return PE_AUDIT_V_ADDRESSEE;
    if (renderer_output && !pe_private_state_audit_pass(eng, out)) return PE_AUDIT_V_PRIVATE_LEAK;
    if (v != PE_AUDIT_V_NONE) return v;
    if (renderer_output && !pe_self_repeat_audit_pass(out)) return PE_AUDIT_V_SELF_REPEAT;
    if (renderer_output && !pe_fatigue_audit_pass(&eng->frame, out)) return PE_AUDIT_V_FATIGUE;
    if (renderer_output && !pe_flat_audit_pass(eng, out, renderer_output)) return PE_AUDIT_V_FLAT;
    return PE_AUDIT_V_NONE;
}

static const char *pe_repair_instruction_for(uint8_t violation,
                                             const V6UserTurnInterpretation *it){
    switch (violation){
    case PE_AUDIT_V_SPEECH_ACT:
        return "Realize the selected speech act while preserving the same answer, topic, and conversational move.";
    case PE_AUDIT_V_EMPTY:
        return "Write a complete spoken reply. Preserve the intended conversational move.";
    case PE_AUDIT_V_SELF_REPEAT:
        return "Remove the repeated phrase or sentence shape. Preserve the same conversational move.";
    case PE_AUDIT_V_FATIGUE:
        return "Replace overused terms with fresher wording. Preserve the same conversational move.";
    case PE_AUDIT_V_LORE:
        return "Make the same conversational move, but remove any fact, name, place, date, or relationship not present in identity, world, user input, or selected memory.";
    case PE_AUDIT_V_PRIVATE_LEAK:
        return "Make the same conversational move, but do not reveal private thought, internal valence, hidden mood, masking, or withholding mechanics.";
    case PE_AUDIT_V_ECHO:
        return "Restate the same conversational move without mirroring the user's phrasing. Respond from the character's own frame, not the user's words.";
    case PE_AUDIT_V_FLAT:
        return "The response is too thin for the pressure present. Surface one of: a memory, an emotional edge, an image from the character's world, or a genuine reaction. Do not add length without adding weight.";
    default:
        break;
    }
    if (it && it->response_move) return it->response_move;
    return "Repair the draft without changing the conversational move.";
}

static int pe_try_constrained_rewrite(RenderBackend *be,
                                      RenderContext *ctx,
                                      uint8_t violation,
                                      const V6UserTurnInterpretation *it,
                                      const char *draft,
                                      char *out,
                                      size_t n,
                                      char *repair_out,
                                      size_t repair_cap){
    RenderResult res;
    size_t copy;
    if (!be || !be->render || !ctx || !draft || !out || n == 0) return 0;
    memset(&res, 0, sizeof(res));
    ctx->repair_mode = 1u;
    ctx->repair_violation = violation;
    ctx->repair_source_text = draft;
    ctx->repair_instruction = pe_repair_instruction_for(violation, it);
    ctx->seed ^= 0xA17D1A1Du;
    be->render(be, ctx, &res);
    ctx->repair_mode = 0u;
    if (res.output_len <= 0 || (res.flags & 0x3u)) return 0;
    copy = (size_t)res.output_len;
    if (repair_out && repair_cap > 0){
        size_t rcopy = copy;
        if (rcopy >= repair_cap) rcopy = repair_cap - 1u;
        memcpy(repair_out, res.output, rcopy);
        repair_out[rcopy] = 0;
    }
    if (copy >= n) copy = n - 1u;
    memcpy(out, res.output, copy);
    out[copy] = 0;
    return 1;
}

static int collect_audit_words(const char *text,
                               char words[][24],
                               int max_words,
                               int min_len,
                               int char_limit){
    int n = 0;
    char word[24];
    int len = 0;
    int seen = 0;
    const unsigned char *p;
    if (!text || !words || max_words <= 0) return 0;
    for (p = (const unsigned char *)text;; ++p){
        if (char_limit > 0 && seen >= char_limit && len == 0) break;
        if (*p) seen++;
        int is_word = *p && (isalnum(*p) || *p == '\'');
        if (is_word){
            if (len < (int)sizeof(word) - 1){
                unsigned char c = (unsigned char)tolower(*p);
                if (c != '\'') word[len++] = (char)c;
            }
            continue;
        }
        if (len >= min_len && n < max_words){
            word[len] = 0;
            snprintf(words[n++], sizeof(words[0]), "%s", word);
        }
        len = 0;
        if (!*p) break;
    }
    return n;
}

static int pe_echo_audit_pass(const char *input, const char *out){
    char in_words[64][24];
    char out_words[32][24];
    int in_n, out_n;
    int matches = 0;
    if (!input || !out || !input[0] || !out[0]) return 1;
    in_n = collect_audit_words(input, in_words, 64, 4, 0);
    out_n = collect_audit_words(out, out_words, 32, 4, 60);
    for (int i = 0; i < in_n; ++i){
        for (int j = 0; j < out_n; ++j){
            if (!strcmp(in_words[i], out_words[j])){
                matches++;
                break;
            }
        }
        if (matches >= 4) return 0;
    }
    return 1;
}

static int pe_copy_audit_pass(const char *input, const char *out){
    char in_words[64][24];
    char out_words[96][24];
    int in_n, out_n;
    if (!input || !out || !input[0] || !out[0]) return 1;
    in_n = collect_audit_words(input, in_words, 64, 3, 0);
    out_n = collect_audit_words(out, out_words, 96, 3, 0);
    for (int i = 0; i + 4 < in_n; ++i){
        for (int j = 0; j + 4 < out_n; ++j){
            int match = 1;
            for (int k = 0; k < 5; ++k){
                if (strcmp(in_words[i + k], out_words[j + k])){ match = 0; break; }
            }
            if (match) return 0;
        }
    }
    return 1;
}

static int audit_word_overlap(const char *a, const char *b,
                              int min_len, int a_limit, int b_limit){
    char a_words[96][24];
    char b_words[96][24];
    int a_n = collect_audit_words(a, a_words, 96, min_len, a_limit);
    int b_n = collect_audit_words(b, b_words, 96, min_len, b_limit);
    for (int i = 0; i < a_n; ++i){
        for (int j = 0; j < b_n; ++j){
            if (!strcmp(a_words[i], b_words[j])) return 1;
        }
    }
    return 0;
}

static int pe_output_has_active_memory_word(const Engine *eng, const char *out){
    if (!eng || !out || !out[0]) return 0;
    for (uint16_t i = 0; i < eng->active_count && i < PE_ACTIVE_MAX; ++i){
        const MemoryNode *m = pe_active_node(eng, eng->active_memories[i]);
        if (!m || !m->summary[0]) continue;
        if (audit_word_overlap(m->summary, out, 4, 0, 0)) return 1;
    }
    return 0;
}

static int pe_output_has_vitality_word(const Engine *eng, const char *out){
    if (!eng || !out || !out[0]) return 0;
    for (int i = 0; i < PE_VITALITY_SLOT_COUNT; ++i){
        if (eng->vitality_profile.recurring_images[i][0] &&
            audit_word_overlap(eng->vitality_profile.recurring_images[i], out, 4, 0, 0))
            return 1;
    }
    if (eng->vitality_profile.metaphoric_domains[0] &&
        audit_word_overlap(eng->vitality_profile.metaphoric_domains, out, 4, 0, 0))
        return 1;
    return 0;
}

static int pe_flat_audit_pass(const Engine *eng, const char *out, int renderer_output){
    if (!renderer_output || !eng || !out || !v6_packet_mode_is_situation()) return 1;
    if (strlen(out) >= 120u) return 1;
    if (strchr(out, '?')) return 1;
    if (!eng->state.turn_drama.hidden_pressure[0] ||
        !strcmp(eng->state.turn_drama.hidden_pressure, "none"))
        return 1;
    if (pe_output_has_active_memory_word(eng, out)) return 1;
    if (pe_output_has_vitality_word(eng, out)) return 1;
    return 0;
}

uint8_t pe_audit_evaluate_for_test(const Engine *eng,
                                   const char *input,
                                   const char *out,
                                   int renderer_output){
    return pe_audit_evaluate(eng, input, out, renderer_output);
}

int pe_audit_violation_is_hard_for_test(uint8_t violation){
    return pe_audit_violation_is_hard(violation);
}

const char *pe_repair_instruction_for_test(uint8_t violation){
    return pe_repair_instruction_for(violation, NULL);
}

static int pe_output_label_audit_pass(const char *out){
    char low[PE_RENDER_MAX_TEXT];
    if (!out || !out[0]) return 1;
    lowercase_copy(low, sizeof(low), out);
    if (strstr(low, "i remember this")) return 0;
    if (strstr(low, "you asked me to remember")) return 0;
    if (strstr(low, "i remember the thread")) return 0;
    if (strstr(low, "you remember the thread")) return 0;
    if (strstr(low, " said:")) return 0;
    if (strstr(low, " asked:")) return 0;
    return 1;
}

static int pe_self_repeat_audit_pass(const char *out){
    char words[96][24];
    int n = 0;
    char word[24];
    int len = 0;
    const unsigned char *p;
    if (!out || !out[0]) return 1;
    for (p = (const unsigned char *)out;; ++p){
        int is_word = *p && (isalnum(*p) || *p == '\'');
        if (is_word){
            if (len < (int)sizeof(word) - 1){
                unsigned char c = (unsigned char)tolower(*p);
                if (c != '\'') word[len++] = (char)c;
            }
            continue;
        }
        if (len >= 3 && n < 96){
            word[len] = 0;
            snprintf(words[n++], sizeof(words[0]), "%s", word);
        }
        len = 0;
        if (!*p) break;
    }
    for (int i = 0; i + 2 < n; ++i){
        for (int j = i + 3; j + 2 < n; ++j){
            if (!strcmp(words[i], words[j])
                && !strcmp(words[i + 1], words[j + 1])
                && !strcmp(words[i + 2], words[j + 2]))
                return 0;
        }
    }
    return 1;
}

static int pe_fatigue_audit_pass(const CanonicalTurnFrame *f, const char *out){
    char low[PE_RENDER_MAX_TEXT];
    int hits = 0;
    if (!f || !out || !out[0] || f->fatigue_term_count == 0) return 1;
    lowercase_copy(low, sizeof(low), out);
    for (uint8_t i = 0; i < f->fatigue_term_count; ++i){
        if (f->fatigue_terms[i][0] && strstr(low, f->fatigue_terms[i])){
            if (++hits >= 2) return 0;
        }
    }
    return 1;
}

static void first_name_lower(const char *src, char *dst, size_t cap){
    size_t pos = 0;
    if (!dst || cap == 0) return;
    dst[0] = 0;
    if (!src) return;
    while (*src && isspace((unsigned char)*src)) src++;
    while (*src && !isspace((unsigned char)*src) && pos + 1 < cap){
        if (isalpha((unsigned char)*src))
            dst[pos++] = (char)tolower((unsigned char)*src);
        else if (pos > 0)
            break;
        src++;
    }
    dst[pos] = 0;
}

static void first_name_copy(const char *src, char *dst, size_t cap){
    size_t pos = 0;
    if (!dst || cap == 0) return;
    dst[0] = 0;
    if (!src) return;
    while (*src && isspace((unsigned char)*src)) src++;
    while (*src && !isspace((unsigned char)*src) && pos + 1 < cap){
        if (isalpha((unsigned char)*src))
            dst[pos++] = *src;
        else if (pos > 0)
            break;
        src++;
    }
    dst[pos] = 0;
}

static int output_has_vocative_name(const char *low, const char *name){
    char needle[40];
    char first[32];
    if (!low || !name || !name[0]) return 0;
    first_name_lower(name, first, sizeof(first));
    if (!first[0]) return 0;
    snprintf(needle, sizeof(needle), "%s,", first);
    if (!strncmp(low, needle, strlen(needle))) return 1;
    snprintf(needle, sizeof(needle), "%s:", first);
    if (!strncmp(low, needle, strlen(needle))) return 1;
    snprintf(needle, sizeof(needle), " %s,", first);
    if (strstr(low, needle)) return 1;
    snprintf(needle, sizeof(needle), " %s:", first);
    if (strstr(low, needle)) return 1;
    snprintf(needle, sizeof(needle), " %s-", first);
    if (strstr(low, needle)) return 1;
    snprintf(needle, sizeof(needle), " %s\xe2\x80\x94", first);
    if (strstr(low, needle)) return 1;
    return 0;
}

static int name_matches_at_ci(const char *p, const char *name){
    size_t i = 0;
    if (!p || !name || !name[0]) return 0;
    while (name[i]){
        if (!p[i]) return 0;
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)name[i]))
            return 0;
        ++i;
    }
    if (isalpha((unsigned char)p[i])) return 0;
    return 1;
}

static int replace_vocative_name(char *out, size_t n,
                                 const char *wrong,
                                 const char *canonical){
    char tmp[PE_RENDER_MAX_TEXT];
    const char *p;
    size_t pos = 0;
    size_t wrong_len;
    size_t canon_len;
    int replaced = 0;
    if (!out || !wrong || !wrong[0] || !canonical || !canonical[0] || n == 0)
        return 0;
    wrong_len = strlen(wrong);
    canon_len = strlen(canonical);
    for (p = out; *p && pos + 1 < sizeof(tmp); ){
        int boundary = (p == out) || isspace((unsigned char)p[-1]);
        int punct = 0;
        if (boundary && name_matches_at_ci(p, wrong)){
            unsigned char next = (unsigned char)p[wrong_len];
            punct = (next == ',' || next == ':' || next == '-'
                     || next == 0xE2u || next == 0);
        }
        if (punct && pos + canon_len + 1 < sizeof(tmp)){
            memcpy(tmp + pos, canonical, canon_len);
            pos += canon_len;
            p += wrong_len;
            replaced = 1;
            continue;
        }
        tmp[pos++] = *p++;
    }
    tmp[pos] = 0;
    if (!replaced) return 0;
    snprintf(out, n, "%s", tmp);
    return 1;
}

static int pe_surgical_addressee_repair(const Engine *eng, char *out, size_t n){
    char low[PE_RENDER_MAX_TEXT];
    char canonical[32];
    char canonical_low[32];
    char wrong[32];
    if (!eng || !out || !out[0] || n == 0) return 0;
    first_name_copy(eng->relation.known_as, canonical, sizeof(canonical));
    if (!canonical[0]){
        for (int i = 0; i < PE_ADDRESS_COUNT; ++i){
            first_name_copy(eng->identity.address_user_as[i],
                            canonical, sizeof(canonical));
            if (canonical[0]) break;
        }
    }
    first_name_lower(canonical, canonical_low, sizeof(canonical_low));
    if (!canonical_low[0]) return 0;
    lowercase_copy(low, sizeof(low), out);

    for (uint8_t i = 0; i < eng->identity.core_memory_count; ++i){
        first_name_lower(eng->identity.core_memories_seed[i].summary,
                         wrong, sizeof(wrong));
        if (!wrong[0] || !strcmp(wrong, canonical_low)) continue;
        if (output_has_vocative_name(low, wrong)
            && replace_vocative_name(out, n, wrong, canonical))
            return 1;
    }
    first_name_lower(eng->identity.character_name, wrong, sizeof(wrong));
    if (wrong[0] && strcmp(wrong, canonical_low)
        && output_has_vocative_name(low, wrong)
        && replace_vocative_name(out, n, wrong, canonical))
        return 1;
    return 0;
}

static int pe_addressee_audit_pass(const Engine *eng, const char *out){
    char low[PE_RENDER_MAX_TEXT];
    char addressee[32];
    if (!eng || !out || !out[0]) return 1;
    lowercase_copy(low, sizeof(low), out);
    first_name_lower(eng->relation.known_as, addressee, sizeof(addressee));
    if (!addressee[0]){
        for (int i = 0; i < PE_ADDRESS_COUNT; ++i){
            first_name_lower(eng->identity.address_user_as[i], addressee, sizeof(addressee));
            if (addressee[0]) break;
        }
    }
    for (uint8_t i = 0; i < eng->identity.core_memory_count; ++i){
        char first[32];
        first_name_lower(eng->identity.core_memories_seed[i].summary, first, sizeof(first));
        if (!first[0] || !strcmp(first, addressee)) continue;
        if (output_has_vocative_name(low, first)) return 0;
    }
    {
        char self[32];
        first_name_lower(eng->identity.character_name, self, sizeof(self));
        if (self[0] && strcmp(self, addressee) && output_has_vocative_name(low, self))
            return 0;
    }
    return 1;
}

static int pe_private_state_audit_pass(const Engine *eng, const char *out){
    char low[PE_RENDER_MAX_TEXT];
    static const char *leaks[] = {
        "internal valence", "private thought", "expression policy",
        "my raw mood", "my real mood", "hidden mood", "internal mood",
        "i am masking", "i'm masking", "i am withholding", "i'm withholding",
        "actually feel", "really feel", "valence:", "mood:",
        NULL
    };
    if (!eng || !out || !out[0]) return 1;
    if (eng->expression_policy == PE_EXPR_GENUINE) return 1;
    lowercase_copy(low, sizeof(low), out);
    for (int i = 0; leaks[i]; ++i)
        if (strstr(low, leaks[i])) return 0;
    return 1;
}

static void pe_act_aware_audit_fallback(const V6UserTurnInterpretation *it,
                                        uint8_t violation,
                                        char *out,
                                        size_t n){
    const char *act = it && it->user_act ? it->user_act : "";
    const char *line = "I am not sure yet.";
    if (!out || n == 0) return;
    if (violation == PE_AUDIT_V_PRIVATE_LEAK){
        if (!strcmp(act, "emotional_disclosure"))
            line = "I hear that.";
        else if (!strcmp(act, "identity_test"))
            line = "Ask me plainly.";
        else
            line = "Not all of that is ready to say.";
    } else if (violation == PE_AUDIT_V_LORE){
        if (!strcmp(act, "memory_probe")){
            line = "I am not certain about that memory.";
        } else if (!strcmp(act, "emotional_disclosure")){
            line = "I hear that.";
        } else if (!strcmp(act, "correction")){
            line = "I will hold to the correction.";
        } else if (!strcmp(act, "direct_question")){
            line = "I am not sure enough to answer that.";
        } else if (!strcmp(act, "challenge") || !strcmp(act, "disagreement")){
            line = "I will not invent an answer.";
        } else if (!strcmp(act, "identity_test")){
            line = "I will not invent proof.";
        } else if (!strcmp(act, "open_ended_invitation")){
            line = "What part matters most?";
        } else if (!strcmp(act, "rich_neutral_input")){
            line = "There is enough there to stay with.";
        }
    }
    snprintf(out, n, "%s", line);
}

static void pe_replace_generic_uncertainty(const char *input, char *out, size_t n){
    char low_input[256];
    if (!input || !out || n == 0) return;
    if (strcmp(out, "I am not sure yet.")) return;
    lowercase_copy(low_input, sizeof(low_input), input);
    if (strstr(low_input, "henry") || strstr(low_input, "frankenstein"))
        snprintf(out, n, "Henry remains in the record, but not cleanly enough for a neat little answer.");
    else if (strstr(low_input, "remember") || strstr(low_input, "earlier") || strstr(low_input, "discussed"))
        snprintf(out, n, "I remember the shape of it, not enough to swear by every edge.");
}
static int pe_lk_has_confirmed_topic_hint(const Engine *eng, uint16_t topic_id,
                                          const char *summary){
    char low_summary[PE_MEM_SUMMARY_LEN];
    char low_topic[PE_TOPIC_NAME];
    if (!eng || topic_id == 0xFFFFu) return 0;
    lowercase_copy(low_summary, sizeof(low_summary), summary ? summary : "");
    low_topic[0] = 0;
    for (uint32_t i = 0; i < eng->topics.count; ++i){
        if (eng->topics.topics[i].id == topic_id){
            lowercase_copy(low_topic, sizeof(low_topic), eng->topics.topics[i].name);
            break;
        }
    }
    for (uint32_t i = 0; i < eng->learned_knowledge.header.entry_count
                        && i < PE_LK_RECORD_CAP; ++i){
        const pe_lk_record_t *r = &eng->learned_knowledge.records[i];
        if (!r->record_id || !r->topic_key[0]) continue;
        if (r->status != PE_LK_STATUS_CONFIRMED &&
            r->status != PE_LK_STATUS_CARTRIDGE_AUTHORED &&
            r->status != PE_LK_STATUS_WORLD_AUTHORED)
            continue;
        if ((low_topic[0] && strstr(low_topic, r->topic_key)) ||
            (low_summary[0] && strstr(low_summary, r->topic_key)))
            return 1;
    }
    return 0;
}

static int pe_engine_topic_is_obsession(const Engine *eng, uint16_t topic_id){
    if (!eng || topic_id == 0xFFFFu || topic_id == 0) return 0;
    for (int i = 0; i < PE_OBSESSION_COUNT; ++i){
        if (!eng->identity.obsessions[i]) break;
        if (eng->identity.obsessions[i] == topic_id) return 1;
    }
    return 0;
}

static int pe_memory_attention_score(Engine *eng, uint16_t idx,
                                     const MemoryNode *m,
                                     int allow_cross_actor){
    int score;
    uint32_t actor;
    if (!eng || !m || !m->summary[0]) return 0;
    if (m->memory_type == MEM_CORE || m->core_memory) return 0;
    actor = pe_actor_index_get(&eng->actor_index, idx);
    if (!allow_cross_actor && actor != 0 && eng->relation.user_hash != 0
        && actor != eng->relation.user_hash)
        return 0;

    score = (int)m->salience * 3;
    score += 255 - (int)m->retrieval_prob;
    if (m->emotion.valence < 0)
        score += (int)(-m->emotion.valence) * 2;
    else
        score += (int)m->emotion.valence;
    score += (int)m->emotion.arousal;
    if (m->flags & PE_MEM_FLAG_USER_PINNED)
        score += 900;
    if (eng->primary_topic != 0xFFFFu && m->topic_id == eng->primary_topic)
        score += 180;
    if (actor != 0 && actor == eng->relation.user_hash)
        score += 520;
    if (pe_lk_has_confirmed_topic_hint(eng, m->topic_id, m->summary))
        score += 90;
    if (pe_engine_topic_is_obsession(eng, m->topic_id))
        score += 70;
    return score;
}

typedef struct {
    int16_t  emotional_residue;
    uint16_t topic_gravity;
    uint16_t approach_pressure;
    uint16_t avoidance_pressure;
    uint16_t followup_pressure;
    uint16_t intent_bias;
    uint16_t rhet_bias;
    uint16_t stance_bias;
} pe_memory_consequence_t;

static pe_memory_consequence_t pe_memory_consequence_from(const Engine *eng,
                                                          const MemoryNode *m,
                                                          uint16_t idx){
    pe_memory_consequence_t c;
    uint32_t actor = 0;
    int positive;
    int negative;
    int high_arousal;
    memset(&c, 0, sizeof(c));
    c.intent_bias = PE_INTENT_REMINISCE;
    c.rhet_bias = PE_RHET_ASSERT;
    c.stance_bias = PE_STANCE_NEUTRAL;
    if (!eng || !m) return c;

    actor = pe_actor_index_get(&eng->actor_index, idx);
    positive = (m->emotion.valence > 22);
    negative = (m->emotion.valence < -22);
    high_arousal = (m->emotion.arousal > 52);

    c.emotional_residue = (int16_t)((int)m->emotion.valence *
                          (int)(64u + m->salience) / 192);
    c.topic_gravity = (uint16_t)(160u + m->salience);
    if (m->topic_id != 0xFFFFu && eng->primary_topic == m->topic_id)
        c.topic_gravity = (uint16_t)(c.topic_gravity + 120u);
    if (pe_engine_topic_is_obsession(eng, m->topic_id))
        c.topic_gravity = (uint16_t)(c.topic_gravity + 90u);

    if (actor != 0 && actor == eng->relation.user_hash)
        c.approach_pressure = (uint16_t)(c.approach_pressure + 120u);
    if (m->flags & PE_MEM_FLAG_USER_PINNED){
        c.followup_pressure = (uint16_t)(c.followup_pressure + 420u);
        c.topic_gravity = (uint16_t)(c.topic_gravity + 180u);
    }

    if (positive){
        c.approach_pressure = (uint16_t)(c.approach_pressure + 180u);
        c.stance_bias = PE_STANCE_INTIMATE;
        c.rhet_bias = PE_RHET_CONFESS;
    } else if (negative && high_arousal){
        c.avoidance_pressure = (uint16_t)(c.avoidance_pressure + 260u);
        c.stance_bias = PE_STANCE_DEFENSIVE;
        c.rhet_bias = PE_RHET_DEFLECT;
        c.intent_bias = PE_INTENT_REDIRECT;
    } else if (negative){
        c.avoidance_pressure = (uint16_t)(c.avoidance_pressure + 160u);
        c.stance_bias = PE_STANCE_DEFENSIVE;
        c.intent_bias = PE_INTENT_ATTEND;
    }

    if (pe_lk_has_confirmed_topic_hint(eng, m->topic_id, m->summary)){
        c.followup_pressure = (uint16_t)(c.followup_pressure + 140u);
        c.intent_bias = PE_INTENT_PROBE;
    }
    if (c.followup_pressure >= 380u && !negative)
        c.intent_bias = PE_INTENT_PROBE;
    if (c.approach_pressure >= 260u && c.intent_bias == PE_INTENT_REMINISCE)
        c.intent_bias = PE_INTENT_PROBE;
    return c;
}

static void pe_prime_unprompted_memory(Engine *eng){
    if (!eng) return;
    if (eng->state.turns_since_unprompted_recall < 0xFFFFu)
        eng->state.turns_since_unprompted_recall++;
    if (eng->state.turns_since_unprompted_recall < 12) return;

    MemoryNode *best = NULL;
    uint16_t best_idx = 0xFFFFu;
    int best_score = 0;
    int best_pinned = 0;
    for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
        MemoryNode *m = &eng->memory.episodic[i];
        int score = pe_memory_attention_score(eng, i, m, 0);
        int pinned = (m->flags & PE_MEM_FLAG_USER_PINNED) ? 1 : 0;
        if ((pinned && !best_pinned) || (pinned == best_pinned && score > best_score)){
            best_score = score;
            best = m;
            best_idx = i;
            best_pinned = pinned;
        }
    }
    if (!best || best_score < 640) return;
    if ((int)(persona_rng_u32(&eng->state) & 0x3FFu) >= best_score) return;
    best->retrieval_prob = 240;
    (void)best_idx;
    if (!best_pinned)
        eng->state.turns_since_unprompted_recall = 0;
}

static void pe_apply_memory_attention(Engine *eng, const EmotionVector *ev,
                                      size_t input_len){
    MemoryNode *best = NULL;
    uint16_t best_idx = 0xFFFFu;
    int best_score = 0;
    int best_pinned = 0;
    int neutral_space;
    int rich_or_direct;
    if (!eng || !ev) return;
    if (eng->state.turns_since_unprompted_recall < 12u) return;
    rich_or_direct = (eng->input_class == 3 || eng->input_class == 4 ||
                      input_len > 80 || ev->arousal > 55);
    if (rich_or_direct) return;
    neutral_space = (eng->input_class == 0 && eng->matched_group == 0xFFFF);
    if (!neutral_space
        && eng->state.current_intent != PE_INTENT_INITIATE
        && eng->state.current_intent != PE_INTENT_REMINISCE
        && eng->state.current_intent != PE_INTENT_MONOLOGUE)
        return;
    for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
        MemoryNode *m = &eng->memory.episodic[i];
        int score = pe_memory_attention_score(eng, i, m, 1);
        int pinned = (m->flags & PE_MEM_FLAG_USER_PINNED) ? 1 : 0;
        if ((pinned && !best_pinned) || (pinned == best_pinned && score > best_score)){
            best_score = score;
            best = m;
            best_idx = i;
            best_pinned = pinned;
        }
    }
    if (!best || best_score < 620) return;
    if ((int)(persona_rng_u32(&eng->state) & 0x3FFu) >= best_score) return;
    pe_memory_consequence_t c = pe_memory_consequence_from(eng, best, best_idx);
    eng->plan.callback_memory = best_idx;
    eng->plan.target_topic = best->topic_id;
    if (eng->state.current_intent == PE_INTENT_INITIATE ||
        eng->state.current_intent == PE_INTENT_REMINISCE ||
        eng->state.current_intent == PE_INTENT_MONOLOGUE ||
        neutral_space ||
        c.followup_pressure >= 380u ||
        c.avoidance_pressure >= 240u)
        eng->state.current_intent = c.intent_bias;
    eng->plan.rhetorical_mode = c.rhet_bias;
    eng->plan.stance = c.stance_bias;
    best->retrieval_prob = (best->retrieval_prob < 220u) ? 220u : best->retrieval_prob;
    eng->state.last_callback_memory = eng->plan.callback_memory;
    eng->state.last_target_topic = eng->plan.target_topic;
    eng->state.last_rhetorical_mode = eng->plan.rhetorical_mode;
    eng->state.last_stance = eng->plan.stance;
    eng->state.turns_since_unprompted_recall = 0;
}

static int pe_probe_skip_word(const char *w){
    static const char *skip[] = {
        "remember","what","about","earlier","before","said","told","tell",
        "that","this","with","from","your","youre","were","was","feeling",
        NULL
    };
    if (!w || !w[0]) return 1;
    for (int i = 0; skip[i]; ++i)
        if (!strcmp(w, skip[i])) return 1;
    return 0;
}

static int pe_memory_probe_overlap(const char *input, const char *summary){
    char word[32];
    int len = 0;
    int score = 0;
    const unsigned char *p;
    if (!input || !summary) return 0;
    for (p = (const unsigned char*)input;; ++p){
        int is_word = *p && isalnum(*p);
        if (is_word){
            if (len < (int)sizeof(word) - 1)
                word[len++] = (char)tolower(*p);
            continue;
        }
        if (len >= 4){
            word[len] = 0;
            if (!pe_probe_skip_word(word) &&
                word_in_text_ci(summary, word, (size_t)len))
                score += len >= 7 ? 3 : 1;
        }
        len = 0;
        if (!*p) break;
    }
    return score;
}

static void pe_memory_probe_recall_boost(Engine *eng, const char *input){
    uint16_t best = 0xFFFFu;
    int best_score = 0;
    if (!eng || !input) return;
    if (!word_in_text_ci(input, "remember", 8) &&
        !word_in_text_ci(input, "recall", 6) &&
        !word_in_text_ci(input, "last time", 9))
        return;
    for (uint16_t i = 0; i < eng->memory.episodic_count; ++i){
        const MemoryNode *m = &eng->memory.episodic[i];
        if (!m->summary[0] || m->memory_type == MEM_CORE || m->core_memory) continue;
        int overlap = pe_memory_probe_overlap(input, m->summary);
        if (overlap <= 0) continue;
        int recency = (int)i;
        int score = overlap * 100 + recency;
        if (score > best_score){
            best_score = score;
            best = i;
        }
    }
    if (best == 0xFFFFu) return;
    for (uint16_t i = 0; i < eng->active_count; ++i)
        if (eng->active_memories[i] == best) return;
    uint16_t limit = eng->active_count < PE_ACTIVE_MAX ? eng->active_count : (PE_ACTIVE_MAX - 1u);
    for (uint16_t i = limit; i > 0; --i){
        eng->active_memories[i] = eng->active_memories[i - 1u];
        eng->active_match[i] = eng->active_match[i - 1u];
    }
    eng->active_memories[0] = best;
    eng->active_match[0] = 1000;
    if (eng->active_count < PE_ACTIVE_MAX) eng->active_count++;
}

static int pe_lk_word_char(unsigned char c){
    return isalnum(c) || c == '_';
}

static int pe_lk_pattern_boundary_ok(const char *lower, const char *m,
                                     const Pattern *p){
    unsigned char first, last;
    if (!lower || !m || !p || !p->kw_len) return 0;
    first = (unsigned char)p->keyword[0];
    last = (unsigned char)p->keyword[p->kw_len - 1u];
    if (pe_lk_word_char(first) && m > lower
        && pe_lk_word_char((unsigned char)m[-1]))
        return 0;
    if (pe_lk_word_char(last)
        && pe_lk_word_char((unsigned char)m[p->kw_len]))
        return 0;
    return 1;
}

static int pe_lk_pattern_matches(const char *lower, const Pattern *p){
    const char *m;
    if (!lower || !p || !p->kw_len || p->topic_id == 0xFFFFu) return 0;
    m = lower;
    while ((m = strstr(m, p->keyword)) != NULL){
        if (pe_lk_pattern_boundary_ok(lower, m, p)) return 1;
        ++m;
    }
    return 0;
}

static int pe_lk_topic_key_for_id(const Engine *eng, uint16_t topic_id,
                                  char *topic_key, size_t topic_cap){
    const char *name;
    if (!eng || topic_id == 0xFFFFu || !topic_key || topic_cap == 0) return 0;
    name = topic_name_by_id(eng, topic_id);
    if (!name || !name[0]) return 0;
    snprintf(topic_key, topic_cap, "%s", name);
    return topic_key[0] != 0;
}

static int pe_lk_topic_key_for_input(const Engine *eng, const char *text,
                                     char *topic_key, size_t topic_cap,
                                     uint16_t *topic_id){
    char low[512];
    uint16_t best_topic = 0xFFFFu;
    uint8_t best_len = 0;
    if (topic_key && topic_cap) topic_key[0] = 0;
    if (topic_id) *topic_id = 0xFFFFu;
    if (!eng || !text || !text[0]) return 0;
    lowercase_copy(low, sizeof(low), text);
    for (uint32_t i = 0; i < eng->patterns.count; ++i){
        const Pattern *p = &eng->patterns.entries[i];
        if (!p->kw_len || p->topic_id == 0xFFFFu) continue;
        if (p->kw_len < best_len) continue;
        if (!pe_lk_pattern_matches(low, p)) continue;
        best_topic = p->topic_id;
        best_len = p->kw_len;
    }
    if (best_topic == 0xFFFFu && eng->primary_topic != 0xFFFFu)
        best_topic = eng->primary_topic;
    if (!pe_lk_topic_key_for_id(eng, best_topic, topic_key, topic_cap))
        return 0;
    if (topic_id) *topic_id = best_topic;
    return 1;
}

static int pe_text_teaches_topic(const Engine *eng, const char *text,
                                 char *topic_key, size_t topic_cap,
                                 uint16_t *topic_id){
    char low[512];
    if (!text || !text[0]) return 0;
    lowercase_copy(low, sizeof(low), text);
    if (!pe_lk_topic_key_for_input(eng, text, topic_key, topic_cap, topic_id))
        return 0;
    return strstr(low, "actually")
        || strstr(low, "correction")
        || strstr(low, "not quite")
        || strstr(low, "that's wrong")
        || strstr(low, "that is wrong")
        || strstr(low, "you missed")
        || strstr(low, "not really")
        || strstr(low, "more precisely")
        || strstr(low, "the better account")
        || strstr(low, "what i mean is")
        || strstr(low, "to be precise");
}

static int pe_ascii_ncasecmp(const char *a, const char *b, size_t n){
    for (size_t i = 0; i < n; ++i){
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (!ca || !cb) return (int)tolower(ca) - (int)tolower(cb);
        ca = (unsigned char)tolower(ca);
        cb = (unsigned char)tolower(cb);
        if (ca != cb) return (int)ca - (int)cb;
    }
    return 0;
}

static void pe_clean_learned_claim(const char *src, char *dst, size_t cap){
    const char *p = src ? src : "";
    if (!dst || cap == 0) return;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (!pe_ascii_ncasecmp(p, "actually,", 9)) p += 9;
    else if (!pe_ascii_ncasecmp(p, "actually", 8)) p += 8;
    else if (!pe_ascii_ncasecmp(p, "correction:", 11)) p += 11;
    else if (!pe_ascii_ncasecmp(p, "more precisely,", 15)) p += 15;
    while (*p && isspace((unsigned char)*p)) ++p;
    snprintf(dst, cap, "%s", p);
}

static uint32_t pe_lk_latest_record_for_topic(const Engine *eng,
                                              const char *topic,
                                              uint8_t status){
    if (!eng || !topic || !topic[0]) return 0;
    for (uint32_t i = 0; i < eng->learned_knowledge.header.entry_count
                        && i < PE_LK_RECORD_CAP; ++i){
        const pe_lk_record_t *r = &eng->learned_knowledge.records[i];
        if (!r->record_id || strcmp(r->topic_key, topic)) continue;
        if (status && r->status != status) continue;
        return r->record_id;
    }
    return 0;
}

static void pe_maybe_commit_learned_knowledge(Engine *eng,
                                              const char *input,
                                              const char *final_output,
                                              int renderer_output,
                                              const EmotionVector *ev){
    EmotionVector kev = { +8, 32, +12, 0 };
    const char *speaker;
    pe_lk_write_t w;
    uint32_t old_id = 0;
    char topic_key[PE_LK_TOPIC_LEN];
    char claim[PE_LK_CLAIM_LEN];
    uint16_t topic_id = 0xFFFFu;
    if (!eng || !input) return;
    speaker = eng->relation.known_as[0] ? eng->relation.known_as : "the user";
    if (pe_text_teaches_topic(eng, input, topic_key, sizeof(topic_key), &topic_id)){
        old_id = pe_lk_latest_record_for_topic(eng, topic_key,
                                               PE_LK_STATUS_CANDIDATE);
        if (!old_id)
            old_id = pe_lk_latest_record_for_topic(eng, topic_key,
                                               PE_LK_STATUS_PROVISIONAL);
        pe_clean_learned_claim(input, claim, sizeof(claim));
        memset(&w, 0, sizeof(w));
        w.topic_key = topic_key;
        w.claim_text = claim;
        w.scope = PE_LK_SCOPE_REAL_WORLD;
        w.source_type = PE_LK_SRC_USER;
        w.source_tier = PE_LK_TIER_OFFLINE;
        w.status = PE_LK_STATUS_CONFIRMED;
        w.authority_rank = 80;
        w.confidence = 880;
        w.source_actor_id = eng->relation.user_hash;
        w.source_actor_name = speaker;
        w.correction_of_record_id = old_id;
        pe_lk_upsert(&eng->learned_knowledge, &w, pe_clock_now_s());
        pe_lk_record_edge(&eng->learned_knowledge,
                          pe_lk_latest_record_for_topic(eng, topic_key,
                                                        PE_LK_STATUS_CONFIRMED),
                          PE_LK_EDGE_TAUGHT_BY,
                          eng->relation.user_hash ? eng->relation.user_hash : 1u,
                          700, 200, pe_clock_now_s());
        snprintf(claim, sizeof(claim), "%s corrected %s.",
                 speaker, topic_key);
        pe_commit_memory(eng, claim, ev ? ev : &kev, topic_id, 90, 0);
        return;
    }
    if (!renderer_output || !final_output || !pe_lk_topic_key_for_input(eng, input, topic_key, sizeof(topic_key), &topic_id))
        return;
    if (eng->input_class != 3 && !strchr(input, '?')) return;
    if (pe_lk_latest_record_for_topic(eng, topic_key, 0)) return;
    pe_clean_learned_claim(final_output, claim, sizeof(claim));
    memset(&w, 0, sizeof(w));
    w.topic_key = topic_key;
    w.scope = PE_LK_SCOPE_REAL_WORLD;
    w.source_type = PE_LK_SRC_MODEL;
    w.source_tier = PE_LK_TIER_SLM;
    w.status = PE_LK_STATUS_CANDIDATE;
    w.authority_rank = 20;
    w.confidence = 420;
    w.source_actor_id = 0;
    w.source_actor_name = "renderer";
    w.claim_text = claim;
    pe_lk_upsert(&eng->learned_knowledge, &w, pe_clock_now_s());
}

static int pe_try_learned_knowledge_answer(Engine *eng,
                                           const char *input,
                                           char *out,
                                           size_t n){
    const pe_lk_record_t *r;
    int ambiguous = 0;
    char topic_key[PE_LK_TOPIC_LEN];
    uint16_t topic_id = 0xFFFFu;
    if (!eng || !input || !out || n == 0) return 0;
    if (pe_text_teaches_topic(eng, input, topic_key, sizeof(topic_key), &topic_id))
        return 0;
    if (!pe_lk_topic_key_for_input(eng, input, topic_key, sizeof(topic_key), &topic_id))
        return 0;
    if (eng->input_class != 3 && !strchr(input, '?')) return 0;
    r = pe_lk_resolve(&eng->learned_knowledge, topic_key,
                      PE_LK_SCOPE_REAL_WORLD, eng->relation.user_hash,
                      &ambiguous);
    if (!r) return 0;
    pe_lk_mark_used(&eng->learned_knowledge, r->record_id, pe_clock_now_s());
    if (ambiguous || r->status == PE_LK_STATUS_DISPUTED || r->confidence < 350u){
        snprintf(out, n,
                 "I have a disputed note on %s, not certainty. Ask the narrower version.", topic_key);
    } else if (r->source_type == PE_LK_SRC_USER ||
               r->status == PE_LK_STATUS_CONFIRMED ||
               r->status == PE_LK_STATUS_WORLD_AUTHORED ||
               r->status == PE_LK_STATUS_CARTRIDGE_AUTHORED){
        const char *who = r->source_actor_name[0] ? r->source_actor_name : "you";
        uint32_t speech_count = pe_speech_ledger_count(&eng->speech_ledger);
        uint32_t pick = (persona_hash(topic_key) + r->record_id +
                         persona_hash(r->claim_text) +
                         (uint32_t)eng->state.turn_count +
                         speech_count * 2u +
                         persona_hash(who)) % 5u;
        if (pick == 0)
            snprintf(out, n, "What %s corrected is the firmer account: %s",
                     who, r->claim_text);
        else if (pick == 1)
            snprintf(out, n, "The better-grounded version is %s's correction: %s",
                     who, r->claim_text);
        else if (pick == 2)
            snprintf(out, n, "I would use %s's correction here: %s",
                     who, r->claim_text);
        else if (pick == 3)
            snprintf(out, n, "The corrected account holds: %s",
                     r->claim_text);
        else
            snprintf(out, n, "I am staying with the correction from %s: %s",
                     who, r->claim_text);
    } else {
        snprintf(out, n,
                 "The provisional note on %s says: %s I would not call that settled.",
                 topic_key, r->claim_text);
    }
    return 1;
}

static void pe_queue_resumption(Engine *eng, uint32_t real_gap_seconds){
    if (!eng || eng->relation.last_contact == 0) return;
    eng->cold_open_callback_source = 0;
    eng->cold_open_callback_template_index = 0xFFu;
    eng->cold_open_callback_exclusive = 0;
    eng->cold_open_callback_topic = 0xFFFFu;
    eng->cold_open_callback_memory_index = 0xFFFFu;
    int bucket = -1;
    if (real_gap_seconds < 7200u) bucket = -1;
    else if (real_gap_seconds < 86400u) bucket = 0;
    else if (real_gap_seconds < 7u * 86400u) bucket = 1;
    else if (real_gap_seconds < 30u * 86400u) bucket = 2;
    else bucket = 3;
    if (bucket >= 0 && bucket < PE_RESUMPTION_BUCKETS
        && eng->identity.resumption_lines[bucket][0]){
        snprintf(eng->state.resumption_pending,
                 sizeof(eng->state.resumption_pending),
                 "%s", eng->identity.resumption_lines[bucket]);
    }
    if (eng->relation.first_contact > 0){
        uint32_t now_s = pe_clock_now_s();
        uint32_t age_days = now_s > eng->relation.first_contact
                          ? (now_s - eng->relation.first_contact) / 86400u : 0u;
        int milestone_idx = -1;
        for (int i = 0; i < PE_MILESTONE_COUNT && i < 8; ++i){
            if (eng->identity.milestone_days[i] == 0) continue;
            if (age_days >= eng->identity.milestone_days[i]
                && !(eng->state.milestones_seen & (1u << i))
                && eng->identity.milestone_lines[i][0]){
                /* Mark every crossed milestone seen so a long absence cannot
                 * later surface a lesser milestone out of order; only the most
                 * recent crossed milestone is spoken on this return. */
                eng->state.milestones_seen |= (uint8_t)(1u << i);
                milestone_idx = i;
            }
        }
        if (milestone_idx >= 0){
            snprintf(eng->state.resumption_pending,
                     sizeof(eng->state.resumption_pending),
                     "%s", eng->identity.milestone_lines[milestone_idx]);
        }
    }
    {
        const MemoryNode *best = NULL;
        uint16_t best_index = 0xFFFFu;
        int best_score = -1;
        for (uint16_t pos = eng->memory.episodic_count; pos > 0; --pos){
            uint16_t i = (uint16_t)(pos - 1u);
            const MemoryNode *m = &eng->memory.episodic[i];
            if (m->core_memory || m->memory_type == MEM_CORE) continue;
            if (eng->relation.user_hash != 0){
                uint32_t actor = pe_actor_index_get(&eng->actor_index, i);
                if (actor != 0 && actor != eng->relation.user_hash) continue;
            }
            int score = 1000 - (int)(eng->memory.episodic_count - i) * 10
                      + (int)m->salience
                      + (m->topic_id != 0xFFFF ? 80 : 0);
            if (score > best_score){
                best_score = score;
                best = m;
                best_index = i;
            }
        }
        if (best){
            char callback[PE_RESUMPTION_LEN];
            const char *name = pe_is_generic_actor_name(eng->relation.known_as)
                             ? NULL : eng->relation.known_as;
            const char *topic = topic_name_by_id(eng, best->topic_id);
            if (!pe_memory_can_own_cold_open(best)) return;
            int case_id = (topic && topic[0])
                        ? (name ? 0 : 1)
                        : (name ? 2 : 3);
            if (pe_pick_cold_open_surface(eng, case_id, name, topic,
                                          best->summary, best_index,
                                          callback, sizeof(callback)) < 0){
                eng->cold_open_callback_source = 1;
                eng->cold_open_callback_template_index = 0xFFu;
                if (topic && topic[0] && name)
                    snprintf(callback, sizeof(callback),
                             "\x1F%s. The old thread about %s has not left the table.",
                             name, topic);
                else if (topic && topic[0])
                    snprintf(callback, sizeof(callback),
                             "\x1FThe old thread about %s has not left the table.",
                             topic);
                else if (name)
                    snprintf(callback, sizeof(callback),
                             "\x1F%s, you left a thread unfinished. I noticed.", name);
                else
                    snprintf(callback, sizeof(callback),
                             "\x1FYou left a thread unfinished. I noticed.");
            } else {
                char tmp[PE_RESUMPTION_LEN];
                snprintf(tmp, sizeof(tmp), "\x1F%s", callback);
                snprintf(callback, sizeof(callback), "%s", tmp);
            }

            /* A meaningful same-actor memory owns the cold-open turn.  Do not
             * concatenate time-bucket resumption plus generic greeting plus
             * memory callback; that reads like stitched systems. */
            snprintf(eng->state.resumption_pending,
                     sizeof(eng->state.resumption_pending), "%s", callback);
            eng->cold_open_callback_exclusive = 1;
            eng->cold_open_callback_topic = best->topic_id;
            eng->cold_open_callback_memory_index = best_index;
        }
    }
}

static void pe_fill_pending_line(Engine *eng, const char *src, char *out, size_t n){
    size_t pos = 0;
    uint32_t modulus = pe_allow_intimate_address(eng) ? PE_ADDRESS_COUNT : 2u;
    const char *address = eng->identity.address_user_as[
        (eng->state.today_seed ^ eng->state.turn_count) % modulus
    ];
    if (!address[0]) address = "you";
    if (n > 0) out[0] = 0;
    while (src && *src && pos + 1 < n){
        if (!strncmp(src, "{address}", 9)){
            const char *a = address;
            while (*a && pos + 1 < n) out[pos++] = *a++;
            src += 9;
        } else {
            out[pos++] = *src++;
        }
    }
    if (pos < n) out[pos] = 0;
}

static void pe_prepend_resumption_if_pending(Engine *eng, char *out, size_t n){
    if (!eng || !out || n == 0 || !eng->state.resumption_pending[0]) return;
    char line[PE_RESUMPTION_LEN];
    char reply[PE_TEMPLATE_TEXT];
    int exclusive = ((unsigned char)eng->state.resumption_pending[0] == 0x1Fu);
    const char *pending = exclusive ? eng->state.resumption_pending + 1
                                    : eng->state.resumption_pending;
    pe_fill_pending_line(eng, pending, line, sizeof(line));
    if (exclusive){
        snprintf(out, n, "%s", line);
        eng->state.resumption_pending[0] = 0;
        return;
    }
    snprintf(reply, sizeof(reply), "%s", out);
    if (reply[0])
        snprintf(out, n, "%s %s", line, reply);
    else
        snprintf(out, n, "%s", line);
    eng->state.resumption_pending[0] = 0;
}

static void pe_append_offscreen_resumption(Engine *eng, const char *line){
    if (!eng || !line || !line[0]) return;
    char tmp[PE_RESUMPTION_LEN];
    if (eng->state.resumption_pending[0])
        snprintf(tmp, sizeof(tmp), "%s %s", eng->state.resumption_pending, line);
    else
        snprintf(tmp, sizeof(tmp), "%s", line);
    snprintf(eng->state.resumption_pending,
             sizeof(eng->state.resumption_pending),
             "%s", tmp);
}

static void pe_offscreen_autonomy_tick(Engine *eng, uint32_t real_gap_seconds){
    if (!eng || real_gap_seconds < 6u * 3600u) return;
    uint32_t gap_hours = real_gap_seconds / 3600u;

    const CharacterWant *best = NULL;
    uint16_t best_idx = 0;
    uint32_t best_score = 0;
    for (uint16_t i = 0; i < PE_WANT_COUNT; ++i){
        const CharacterWant *w = &eng->identity.wants[i];
        if (!w->name[0]) continue;
        uint32_t age = eng->state.want_turns_since_engaged[i] + gap_hours;
        eng->state.want_turns_since_engaged[i] =
            age > 0xFFFFu ? 0xFFFFu : (uint16_t)age;
        uint32_t intensity = w->intensity ? w->intensity : 100u;
        uint32_t score = (uint32_t)eng->state.want_turns_since_engaged[i] * intensity;
        if (score > best_score){
            best_score = score;
            best = w;
            best_idx = i;
        }
    }

    const char *pre = "the work";
    int n_pre = 0;
    for (int i = 0; i < PE_PREOCCUPATION_COUNT; ++i)
        if (eng->identity.current_preoccupations[i][0]) n_pre++;
    if (n_pre > 0){
        uint32_t pick = (eng->state.today_seed ^ real_gap_seconds ^ eng->state.turn_count) % (uint32_t)n_pre;
        int seen = 0;
        for (int i = 0; i < PE_PREOCCUPATION_COUNT; ++i){
            if (!eng->identity.current_preoccupations[i][0]) continue;
            if (seen == (int)pick){ pre = eng->identity.current_preoccupations[i]; break; }
            seen++;
        }
    }

    uint16_t topic = best ? best->target_topic_id : eng->identity.obsessions[0];
    if (topic == 0 || topic == 0xFFFF) topic = eng->identity.obsessions[0];
    if (topic == 0) topic = 0xFFFF;

    char summary[PE_MEM_SUMMARY_LEN];
    if (best)
        snprintf(summary, sizeof(summary), "[offscreen] I pursued %s by %s.", best->name, pre);
    else
        snprintf(summary, sizeof(summary), "[offscreen] I occupied myself with %s.", pre);

    EmotionVector ev = { +12, 28, +18, 0 };
    pe_commit_memory(eng, summary, &ev, topic, 90, 0);
    if (best && best_idx < PE_WANT_COUNT)
        eng->state.want_turns_since_engaged[best_idx] = 0;
    if (topic != 0xFFFF)
        pe_boost_topic(&eng->state, topic, 260);

    char line[PE_RESUMPTION_LEN];
    snprintf(line, sizeof(line), "In your absence, I occupied myself with %s.", pre);
    pe_append_offscreen_resumption(eng, line);
}

static int reply_has_question(const char *s){
    return s && strchr(s, '?') != NULL;
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
        int64_t step64 = ((int64_t)delta_ms * d->decay_per_minute) / 60000;
        int32_t step = step64 > 1000000 ? 1000000
                     : step64 < -1000000 ? -1000000
                     : (int32_t)step64;
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
            int base_rate = step < 0 ? -step : step;
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

static void pe_decay_drives_for_gap(Engine *eng, uint32_t real_gap_seconds){
    if (!eng || real_gap_seconds == 0) return;
    uint32_t days = real_gap_seconds / 86400u;
    if (days == 0) days = 1;
    for (int i = 0; i < PE_DRIVE_COUNT; ++i){
        const DriveDef *d = &eng->drives.drives[i];
        int32_t traits[5] = {
            eng->identity.openness,        eng->identity.conscientiousness,
            eng->identity.extraversion,    eng->identity.agreeableness,
            eng->identity.neuroticism
        };
        int32_t accel = 0;
        int base_rate = d->decay_per_minute < 0
                      ? -d->decay_per_minute : d->decay_per_minute;
        for (int k = 0; k < 5; ++k)
            accel += (traits[k] * d->personality_weight[k]) >> 16;
        base_rate += (base_rate * accel) / 0x1000;
        if (base_rate < 1) base_rate = 1;
        if (base_rate > 1000) base_rate = 1000;

        int32_t delta = (int32_t)eng->state.drive_values[i] - d->baseline;
        if (delta != 0){
            int salience = delta < 0 ? -delta : delta;
            int16_t new_delta = affect_decay_steps((int16_t)delta,
                                                   (int16_t)salience,
                                                   base_rate, days);
            eng->state.drive_values[i] =
                pe_clamp16((int32_t)d->baseline + new_delta, 0, 1000);
        }
    }
    eng->state.fatigue =
        (uint16_t)affect_decay_steps((int16_t)eng->state.fatigue,
                                     0, 12, days);
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
      - ((int32_t)eng->identity.agreeableness / 256)
      + eng->long_arc_drift.baseline_offset,
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

void pe_synthesize_imprint(Engine *eng){
    if (!eng) return;
    ExperienceImprint *im = &eng->state.imprint;
    const pe_belief_ledger_t *bl = &eng->belief_ledger;
    memset(im, 0, sizeof(*im));
    im->disrespect_pressure = bl->slots[PE_BELIEF_DISRESPECT].pressure;
    im->trust_pressure = bl->slots[PE_BELIEF_TRUST_EARNED].pressure;
    im->manipulation_guard = bl->slots[PE_BELIEF_MANIPULATION].pressure;
    im->abandonment_ache = bl->slots[PE_BELIEF_ABANDONMENT].pressure;
    im->shared_pull = bl->slots[PE_BELIEF_SHARED_PROJECT].pressure;
    im->self_doubt_weight = bl->slots[PE_BELIEF_SELF_FAILURE].pressure;
    im->threat_vigilance = bl->slots[PE_BELIEF_THREAT_PATTERN].pressure;
    im->intimacy_readiness = bl->slots[PE_BELIEF_INTIMACY_EARNED].pressure;

    int16_t best = 0;
    uint8_t best_slot = 0;
    for (int i = 0; i < PE_BELIEF_COUNT; ++i){
        int16_t p = bl->slots[i].pressure;
        int16_t mag = p < 0 ? (int16_t)-p : p;
        if (mag > best){
            best = mag;
            best_slot = (uint8_t)i;
        }
    }
    im->dominant_slot = best_slot;
    im->pattern_confirmed =
        (best > 200 && bl->slots[best_slot].evidence_count >= 3u) ? 1u : 0u;
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

    if (load_identity_section(character_dir, is_cart, &eng->identity) != 0) {
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
    pe_vitality_load_optional(eng, character_dir, is_cart);
    pe_merge_baseline_patterns(&eng->patterns);
    pe_merge_baseline_templates(&eng->templates);
    pe_default_mind_affect_knobs(eng);

    /* mutable state */
    int had_state = load_or_zero(eng->char_dir, "state.bin",  &eng->state,  sizeof(NPCState));
    int had_mem   = load_or_zero(eng->char_dir, "memory.bin", &eng->memory, sizeof(MemoryStore));
    /* V5 Phase 2: actor-tagging sidecar is parallel to memory.bin. Only load
     * an existing sidecar when memory.bin existed too — otherwise the sidecar
     * would describe slots that no longer exist (e.g. after a test wipes
     * memory.bin without knowing about the new sidecar). Initialize fresh in
     * the wiped-memory case so tags stay in sync with the episodic array. */
    if (had_mem) pe_actor_index_load(&eng->actor_index, eng->char_dir);
    else         pe_actor_index_init(&eng->actor_index);
    /* V6 Phase 3: self-ledger sidecar — load if present, init fresh otherwise. */
    if (had_mem) pe_speech_ledger_load(&eng->speech_ledger, eng->char_dir);
    else         pe_speech_ledger_init(&eng->speech_ledger);
    /* V6 Phase 5b: typed dissonance accumulators sidecar. */
    pe_dissonance_load(&eng->dissonance, eng->char_dir);
    pe_seed_typed_self_model(eng);
    pe_long_arc_drift_load(&eng->long_arc_drift, eng->char_dir);
    /* V6 Phase 6: carried intentions / open loops sidecar. */
    pe_open_loops_load(&eng->open_loops, eng->char_dir);
    /* V6 Phase 6: lightweight rhythm habits sidecar. */
    pe_speech_habits_load(&eng->speech_habits, eng->char_dir);
    /* V6: learned-knowledge graph sidecar. Missing/corrupt loads empty. */
    pe_lk_load(&eng->learned_knowledge, eng->char_dir);

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
                                  : pe_clock_now_s();
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
#ifndef PE_DISABLE_AETHER
        /* Route AETHER's internal timestamp helper through the canonical
         * Layer 1 clock so AETHER state replays deterministically under
         * PE_CLOCK_OVERRIDE_MS. Set once before aether_open is called. */
        aether_set_clock(pe_clock_now_s);

        char aether_dir[512];
        pe_path_join(aether_dir, sizeof(aether_dir), eng->char_dir, "aether");
        eng->aether = aether_open(aether_dir);
#else
        eng->aether = NULL;
#endif
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

    /* V5: reflective consolidation state — load persisted ring (soft-fail). */
    pe_reflection_load(eng, eng->char_dir);

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
    /* V5 Phase 2: persist the actor-tagging sidecar alongside the memory it
     * indexes. Non-fatal — a missing sidecar is the legacy state and the
     * runtime tolerates it. */
    pe_actor_index_save(&eng->actor_index, eng->char_dir);
    pe_speech_ledger_save(&eng->speech_ledger, eng->char_dir);
    pe_dissonance_save(&eng->dissonance, eng->char_dir);
    pe_long_arc_drift_save(&eng->long_arc_drift, eng->char_dir);
    pe_open_loops_save(&eng->open_loops, eng->char_dir);
    pe_speech_habits_save(&eng->speech_habits, eng->char_dir);
    pe_lk_save(&eng->learned_knowledge, eng->char_dir);
    pe_save_relation(eng);
    /* v3.1: chapters — non-fatal if write fails (re-crystallised on next load) */
    pe_path_join(p, sizeof(p), eng->char_dir, "chapters.bin");
    pe_write_file_atomic(p, &eng->chapters, sizeof(ChapterBook));
    /* V5: reflections — non-fatal if write fails (re-synthesised on next gate) */
    pe_reflection_save(eng, eng->char_dir);
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
#ifndef PE_DISABLE_AETHER
    if (eng->aether){
        /* Drain any pending writes before close.  Cheap if WAL is empty. */
        if (aether_should_consolidate(eng->aether))
            aether_consolidate(eng->aether, 0 /*incremental*/);
        aether_close(eng->aether);
        eng->aether = NULL;
    }
#endif
}

int persona_process_input(Engine *eng,
                          const char *user_id,
                          const char *input_text,
                          char *out, size_t n)
{
    if (!eng || !input_text || !out || n == 0) return -1;
    uint16_t pre_epi_count = 0;
    uint32_t pre_next_memory_id = 0;
    uint32_t pre_speech_count = 0;
    uint32_t pre_open_total = 0;
    uint32_t pre_habit_turns = 0;
    char raw_model_output[PE_RENDER_MAX_TEXT];
    char repair_model_output[PE_RENDER_MAX_TEXT];
    char render_backend_name[32];
    V6UserTurnInterpretation packet_it;

    raw_model_output[0] = 0;
    repair_model_output[0] = 0;
    snprintf(render_backend_name, sizeof(render_backend_name), "%s", "template");
    memset(&packet_it, 0, sizeof(packet_it));

    /* 1. relation */
    pe_load_relation(eng, user_id);
    int first_turn_of_session = (eng->environment.turns_this_session == 0);
    uint32_t real_gap_seconds = 0;
    {
        uint32_t now_s = pe_clock_now_s();
        if (eng->relation.last_contact > 0 && now_s > eng->relation.last_contact)
            real_gap_seconds = now_s - eng->relation.last_contact;
    }
    if (first_turn_of_session && eng->relation.last_contact > 0)
        pe_queue_resumption(eng, real_gap_seconds);
    if (first_turn_of_session)
        pe_offscreen_autonomy_tick(eng, real_gap_seconds);
    environment_update_turn(eng, input_text);
    pre_epi_count = eng->memory.episodic_count;
    pre_next_memory_id = eng->memory.next_memory_id;
    pre_speech_count = pe_speech_ledger_count(&eng->speech_ledger);
    pre_open_total = eng->open_loops.total_recorded;
    pre_habit_turns = eng->speech_habits.turns_observed;

    /* 2. time delta + decay */
    uint32_t now = persona_now_ms();
    uint32_t delta = now - eng->state.last_update_time;
    uint32_t cap_ms = (uint32_t)(3600u * 1000u);
    if (delta > cap_ms) delta = cap_ms;
    if (first_turn_of_session && real_gap_seconds > 3600u)
        pe_decay_drives_for_gap(eng, real_gap_seconds);
    else
        pe_decay_drives(eng, delta);
    pe_decay_episodic(eng);
    pe_repetition_decay(eng);
    if (first_turn_of_session && real_gap_seconds > 3600u){
        schema_tick_many(&eng->schema, real_gap_seconds / 3600u);
    }

    /* fold turn count + today seed into rng — guarantees deterministic replay */
    eng->state.turn_count++;
    eng->state.rng_state ^= eng->state.today_seed + eng->state.turn_count * 2654435761u;

    /* 3a. v2: cache lowered input + char bitmap + negation flag */
    pe_prep_input(eng, input_text);

    /* 3. emotional fingerprint (now uses cached lower + bitmap + negation) */
    EmotionVector ev = {0};
    pe_classify_input(eng, input_text, &ev);
    pe_speech_habits_note_input(&eng->speech_habits, input_text);
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

        /* V6 Phase 5a: drive the multi-dim relation profile from the
         * same classified input. Praise from a high-threat actor lifts
         * threat (suspicion); praise from a trusted one lifts trust and
         * admiration — same input class, different appraisal because
         * the dims condition on the current relational state. */
        pe_relation_dims_update_from_input(&eng->relation_dims,
                                           (uint8_t)eng->input_class,
                                           ev.arousal);
        pe_tom_update_from_input(&eng->theory_of_mind,
                                 (uint8_t)eng->input_class,
                                 ev.arousal, ev.valence,
                                 eng->primary_topic,
                                 (int8_t)(eng->identity.suggestibility / 10u));
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
    pe_memory_probe_recall_boost(eng, input_text);

    /* 4a. V5: retrieval is a write event.  Recalled memories are gently
     * reconsolidated through the current affect/schema context. */
    pe_recall_plasticity_tick(eng, &ev);

    /* 5. drive update */
    pe_update_drives_from_input(eng);

    /* 6. mood */
    pe_compute_mood(eng);
    eng->state.mood = affect_contagion_pull(eng->state.mood,
                                            (uint8_t)eng->input_class,
                                            ev.arousal, ev.valence,
                                            &eng->relation_dims,
                                            eng->identity.contagion_susceptibility);

    /* 6a. v2: embodiment + layered affect (modulates mood, sets acute spike, etc.) */
    pe_update_embodiment(eng, delta);
    pe_update_layered_affect(eng);

    /* 7. topic momentum */
    pe_update_topic_momentum(eng);
    pe_update_character_wants(eng);

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
    {
        const pe_speech_event_t *last_refusal =
            pe_speech_ledger_last_refusal(&eng->speech_ledger);
        const pe_speech_event_t *last_contra =
            pe_speech_ledger_last_contradiction_candidate(&eng->speech_ledger,
                                                          eng->relation.user_hash,
                                                          eng->primary_topic);
        if (last_refusal
            && eng->input_class == 3
            && eng->primary_topic != 0xFFFFu
            && last_refusal->target_topic_id == eng->primary_topic
            && eng->state.current_intent != PE_INTENT_PAUSE){
            eng->state.current_intent = PE_INTENT_ANSWER;
        }
        if (last_contra
            && eng->negation_active
            && eng->input_class == 3
            && eng->state.current_intent != PE_INTENT_PAUSE){
            eng->state.current_intent = PE_INTENT_CLARIFY;
        }
        if (pe_speech_ledger_recent_repeated_act(&eng->speech_ledger,
                                                 PE_SA_QUESTION, 4)
            && eng->state.current_intent == PE_INTENT_PROBE){
            eng->state.current_intent = PE_INTENT_ATTEND;
        }
    }
    if (eng->state.fixation_topic != 0xFFFF && eng->state.fixation_strength > 500){
        /* fixation forces monologue/reminisce alternation */
        eng->state.current_intent = (eng->state.turn_count & 1)
            ? PE_INTENT_MONOLOGUE : PE_INTENT_REMINISCE;
    }
    /* exhaustion > 800 + paranoia > 600 → theatrical collapse (withdraw) */
    if (eng->state.exhaustion > 800 && eng->state.paranoia > 600)
        eng->state.current_intent = PE_INTENT_WITHDRAW;
    size_t input_len = strlen(input_text);
    int rich_input = (ev.arousal > 50) || (input_len > 80);
    if (rich_input
        && eng->input_class == 0
        && eng->matched_group == 0xFFFF
        && eng->state.current_intent != PE_INTENT_PAUSE
        && eng->state.current_intent != PE_INTENT_WITHDRAW
        && eng->state.current_intent != PE_INTENT_REMINISCE){
        if (input_len > 100 || (persona_rng_u32(&eng->state) & 0xFFu) < 60u)
            eng->state.current_intent = PE_INTENT_ATTEND;
    }
    if (eng->state.turns_since_question >= 4
        && eng->input_class != 2
        && eng->input_class != 3
        && eng->input_class != 4
        && eng->state.current_intent != PE_INTENT_REMINISCE
        && eng->state.current_intent != PE_INTENT_ATTEND
        && eng->state.current_intent != PE_INTENT_PAUSE){
        eng->state.current_intent = (eng->matched_group != 0xFFFF)
            ? PE_INTENT_PROBE : PE_INTENT_INITIATE;
    }
    if (eng->state.neutral_streak >= 3
        && eng->matched_group == 0xFFFF
        && eng->state.current_intent != PE_INTENT_ATTEND
        && eng->state.current_intent != PE_INTENT_REMINISCE){
        if ((persona_rng_u32(&eng->state) & 0xFFu) < 80u){
            eng->state.current_intent = PE_INTENT_INITIATE;
            eng->state.neutral_streak = 0;
        }
    }
    if (eng->input_class == 0 && eng->matched_group == 0xFFFF
        && eng->state.neutral_streak >= 2
        && pe_neglected_want_index(eng) != 0xFFFF
        && eng->state.current_intent != PE_INTENT_ATTEND
        && eng->state.current_intent != PE_INTENT_REMINISCE){
        eng->state.current_intent = PE_INTENT_INITIATE;
    }
    {
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
    eng->state.sovereign_override = 0;
    eng->state.sovereign_reason = PE_SOV_NONE;

    if (eng->state.exhaustion > 850){
        if ((persona_rng_u32(&eng->state) & 0xFFu) < 100u)
            eng->state.current_intent = PE_INTENT_PAUSE;
    }
    if (eng->speech_habits.question_bias >= 180u
        && eng->input_class != 2
        && eng->input_class != 3
        && eng->input_class != 4
        && eng->state.current_intent != PE_INTENT_PAUSE
        && eng->state.current_intent != PE_INTENT_WITHDRAW
        && eng->state.current_intent != PE_INTENT_ATTEND
        && eng->state.current_intent != PE_INTENT_REMINISCE){
        eng->state.current_intent =
            (eng->speech_habits.initiative_bias > eng->speech_habits.question_bias + 60u)
              ? PE_INTENT_INITIATE : PE_INTENT_PROBE;
    }
    if (eng->speech_habits.fatigue_bias >= 360u
        && eng->input_class != 2
        && eng->input_class != 4
        && eng->state.current_intent != PE_INTENT_PAUSE
        && eng->state.current_intent != PE_INTENT_WITHDRAW){
        eng->state.current_intent =
            (eng->speech_habits.fatigue_bias >= 720u)
            ? PE_INTENT_REDIRECT : PE_INTENT_PROBE;
    }
    {
        const pe_open_loop_t *loop =
            pe_open_loops_latest_for_actor(&eng->open_loops, eng->relation.user_hash);
        uint16_t pressure = pe_open_loop_pressure(loop, eng->state.turn_count);
        if (loop && pressure >= 700
            && eng->input_class == 0
            && eng->matched_group == 0xFFFF
            && eng->state.current_intent != PE_INTENT_PAUSE
            && eng->state.current_intent != PE_INTENT_ATTEND
            && eng->state.current_intent != PE_INTENT_WITHDRAW){
            if (loop->avoidance_pressure > loop->urgency + 180u)
                eng->state.current_intent = PE_INTENT_REDIRECT;
            else
                eng->state.current_intent = PE_INTENT_INITIATE;
        }
    }

    pe_apply_sovereign_override(eng);

    /* 9a. v2: build rhetorical plan before realization */
    pe_build_plan(eng);
    pe_apply_memory_attention(eng, &ev, input_len);
    pe_build_turn_frame(eng);
    pe_update_private_thought_frame(eng);
    pe_synthesize_imprint(eng);
    pe_vitality_synthesize(eng);
    eng->last_audit_result = PE_AUDIT_PASS;
    eng->last_audit_violation = PE_AUDIT_V_NONE;
    eng->last_audit_hardness = PE_AUDIT_SOFT;
    eng->last_audit_rewrite = 0;
    int renderer_output = 0;
    int learned_knowledge_output = 0;
    RetrievedMemorySet render_mem;
    RenderContext render_ctx;
    RenderBackend *render_be = NULL;
    memset(&render_mem, 0, sizeof(render_mem));
    memset(&render_ctx, 0, sizeof(render_ctx));

    /* V4: renderer dispatch.  Compose a RenderContext from Layer 1 state
     * and offer the selected backend the chance to produce the reply.
     * The template backend defers to the legacy pe_generate_response()
     * call below (signaled by flags bit 0).  An SLM backend that's
     * actually available will fill out->output and we use that instead. */
    {
        int em_n = 0;
        for (int i = 0; i < PE_ACTIVE_MAX && em_n < 6; ++i){
            uint16_t idx = eng->active_memories[i];
            if (idx >= PE_EPISODIC_MAX) break;  /* sentinel-tagged cold entries */
            render_mem.episodic_idx[em_n++] = idx;
        }
        render_mem.episodic_count = em_n;

        CanonicalTurnFrame packet_frame;
        render_ctx.npc       = eng;
        render_ctx.memories  = &render_mem;
        render_ctx.plan      = &eng->plan;
        render_ctx.frame     = &eng->frame;
        render_ctx.relation  = &eng->relation;
        render_ctx.schema    = &eng->schema;
        render_ctx.user_input = input_text;
        render_ctx.seed      = eng->state.rng_state;
        v6_interpret_user_turn(&render_ctx, input_text, &packet_it);
        pe_synthesize_turn_drama(eng, &packet_it, &eng->state.turn_drama);
        if (v6_packet_mode_is_situation()){
            uint16_t adjusted_intent = eng->state.current_intent;
            if (!strcmp(packet_it.user_act, "correction")){
                adjusted_intent = eng->negation_active
                    ? PE_INTENT_CLARIFY : PE_INTENT_ANSWER;
            } else if (!strcmp(packet_it.user_act, "continuation") ||
                       !strcmp(packet_it.user_act, "rich_neutral_input") ||
                       !strcmp(packet_it.user_act, "emotional_disclosure") ||
                       !strcmp(packet_it.user_act, "memory_commit")){
                adjusted_intent = PE_INTENT_ATTEND;
            } else if (!strcmp(packet_it.user_act, "memory_probe") ||
                       !strcmp(packet_it.user_act, "clarification_probe") ||
                       !strcmp(packet_it.user_act, "direct_question") ||
                       !strcmp(packet_it.user_act, "identity_test")){
                adjusted_intent = PE_INTENT_ANSWER;
            }
            if (adjusted_intent != eng->state.current_intent){
                packet_frame = eng->frame;
                packet_frame.selected_intent = (uint8_t)adjusted_intent;
                packet_frame.speech_act = pe_speech_act_from_intent(adjusted_intent);
                packet_frame.require_question = (packet_frame.speech_act == PE_SA_QUESTION) ? 1u : 0u;
                packet_frame.allow_empty = (packet_frame.speech_act == PE_SA_PAUSE) ? 1u : 0u;
                packet_frame.max_words = (adjusted_intent == PE_INTENT_MONOLOGUE ||
                                          adjusted_intent == PE_INTENT_REMINISCE) ? 48u : 32u;
                render_ctx.frame = &packet_frame;
            }
        }

        if (pe_try_learned_knowledge_answer(eng, input_text, out, n)){
            learned_knowledge_output = 1;
            goto post_render;
        }

        render_be = render_backend_default();
        if (render_be && render_be->render){
            RenderResult res;
            memset(&res, 0, sizeof(res));
            snprintf(render_backend_name, sizeof(render_backend_name), "%s",
                     render_be->name ? render_be->name : "unknown");
            render_be->render(render_be, &render_ctx, &res);
            trace_render_dispatch(eng->state.turn_count, render_be->name,
                                  res.latency_ms, res.output_len, res.flags);
            /* bit 0: backend deferred to legacy path.  bit 1: backend
             * unavailable, fall back.  Either way drop to legacy. */
            if (res.output_len > 0 && !(res.flags & 0x3u)){
                size_t copy = (size_t)res.output_len;
                size_t raw_copy = (size_t)res.output_len;
                if (raw_copy >= sizeof(raw_model_output))
                    raw_copy = sizeof(raw_model_output) - 1u;
                memcpy(raw_model_output, res.output, raw_copy);
                raw_model_output[raw_copy] = 0;
                if (copy >= n) copy = n - 1;
                memcpy(out, res.output, copy);
                out[copy] = 0;
                renderer_output = 1;
                goto post_render;
            }
            if (res.flags & 0x2u){
                trace_emit(eng->state.turn_count, PE_TRACE_RENDER_FALLBACK,
                           0, 0, render_be->name, "unavailable->template");
            }
        }
    }

    /* 10–11. candidates, repetition, transforms, select (plan-driven) */
    pe_generate_response(eng, input_text, out, n);
post_render:;

    {
        uint8_t violation = learned_knowledge_output
                           ? PE_AUDIT_V_NONE
                           : pe_audit_evaluate(eng, input_text, out, renderer_output);
        if (violation != PE_AUDIT_V_NONE){
            eng->last_audit_violation = violation;
            eng->last_audit_hardness =
                pe_audit_violation_is_hard(violation) ? PE_AUDIT_HARD : PE_AUDIT_SOFT;
            if (renderer_output
                && violation == PE_AUDIT_V_ADDRESSEE
                && pe_surgical_addressee_repair(eng, out, n)){
                eng->last_audit_rewrite = 1u;
                violation = pe_audit_evaluate(eng, input_text, out, 1);
            }
            if (renderer_output
                && eng->last_audit_hardness == PE_AUDIT_SOFT
                && violation != PE_AUDIT_V_NONE
                && pe_try_constrained_rewrite(render_be, &render_ctx, violation,
                                              &packet_it, out, out, n,
                                              repair_model_output,
                                              sizeof(repair_model_output))){
                eng->last_audit_rewrite = 1u;
                violation = pe_audit_evaluate(eng, input_text, out, 1);
            }
            if (renderer_output
                && violation == PE_AUDIT_V_LORE
                && pe_try_constrained_rewrite(render_be, &render_ctx, violation,
                                              &packet_it, out, out, n,
                                              repair_model_output,
                                              sizeof(repair_model_output))){
                eng->last_audit_rewrite = 1u;
                violation = pe_audit_evaluate(eng, input_text, out, 1);
            }
        }
        if (violation != PE_AUDIT_V_NONE){
            if (renderer_output && violation == PE_AUDIT_V_LORE)
                pe_act_aware_audit_fallback(&packet_it, violation, out, n);
            else
                pe_generate_response(eng, input_text, out, n);
            if (!pe_copy_audit_pass(input_text, out)
                || !pe_output_label_audit_pass(out)
                || !pe_self_repeat_audit_pass(out)){
                pe_act_aware_audit_fallback(&packet_it, violation, out, n);
                pe_replace_generic_uncertainty(input_text, out, n);
                if (!pe_copy_audit_pass(input_text, out)
                    || !pe_output_label_audit_pass(out)
                    || !pe_self_repeat_audit_pass(out)){
                    {
                        char low_input[256];
                        lowercase_copy(low_input, sizeof(low_input), input_text);
                        if (strstr(low_input, "henry") || strstr(low_input, "frankenstein"))
                            snprintf(out, n, "Henry remains in the record, but not cleanly enough for a neat little answer.");
                        else if (strstr(low_input, "remember") || strstr(low_input, "earlier"))
                            snprintf(out, n, "I remember the shape of it, not enough to swear by every edge.");
                        else
                            snprintf(out, n, "I will answer only the part I can ground.");
                    }
                }
                eng->state.current_intent = PE_INTENT_CLARIFY;
            }
            eng->last_audit_result =
                pe_render_audit_pass(&eng->frame, out)
                    ? PE_AUDIT_REPAIRED
                    : PE_AUDIT_FALLBACK;
        } else if (eng->last_audit_rewrite) {
            eng->last_audit_result = PE_AUDIT_REPAIRED;
        }
    }

    /* 11b. v3.1: dream recall — prepend dream_phrase to first response after
     * a long absence.  Only fires once (dream_pending is cleared here). */
    if (!learned_knowledge_output)
        pe_maybe_commit_learned_knowledge(eng, input_text, out,
                                          renderer_output, &ev);

    pe_prepend_resumption_if_pending(eng, out, n);

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

    {
        int asked = reply_has_question(out);
        eng->state.last_reply_had_question = asked ? 1u : 0u;
        if (asked) eng->state.turns_since_question = 0;
        else if (eng->state.turns_since_question < 255)
            eng->state.turns_since_question++;
        pe_speech_habits_update(&eng->speech_habits, out, asked, eng->input_class);
    }

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
        int explicit_memory_request = pe_input_is_memory_commit_request(input_text);
        uint8_t memory_flags = explicit_memory_request ? PE_MEM_FLAG_USER_PINNED : 0u;
        if (explicit_memory_request && s < 110u) s = 110u;
        if (!explicit_memory_request
            && !identity_threat && eng->input_class != 1 && eng->input_class != 5)
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
            snprintf(summary, sizeof(summary), "%s%s %s: %.96s",
                     prefix, speaker,
                     eng->input_class == 3 ? "asked" : "said",
                     input_text);
            pe_commit_memory_ex(eng, summary, &ev, eng->primary_topic,
                                s, identity_threat, memory_flags);
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
        eng->relation.last_contact = pe_clock_now_s();
        if (eng->relation.disposition > 700) {
            eng->relation.tags |= PE_TAG_CONFIDANT;
            eng->relation.tags &= ~PE_TAG_STRANGER;
        }
        if (eng->relation.disposition < 200) {
            eng->relation.tags |= PE_TAG_BENEATH_CONTEMPT;
        }
    }
    if ((eng->state.turn_count & 15u) == 0u){
        pe_long_arc_drift_apply_history(&eng->long_arc_drift,
                                        &eng->dissonance,
                                        &eng->relation_dims,
                                        eng->identity.drift_malleability,
                                        1u);
    }

    /* v3.2: opportunistic AETHER consolidation.  Every 16 turns, if the
     * WAL has crossed its soft threshold, run an incremental rebuild —
     * folding pending demoted events into their bucket files so subsequent
     * cold recalls see them via bucket scan (cheaper) rather than WAL
     * linear scan.  Cheap when no consolidation is needed
     * (aether_should_consolidate returns 0 quickly). */
#ifndef PE_DISABLE_AETHER
    if (eng->aether
        && (eng->state.turn_count & 15u) == 0
        && aether_should_consolidate(eng->aether)){
        aether_consolidate(eng->aether, 0 /* incremental */);
    }
#endif

    pe_prime_unprompted_memory(eng);

    /* V5: reflective consolidation.  Cooldown-gated inside the function;
     * cheap on ordinary turns, and persisted with persona_save below. */
    pe_consolidate_reflections(eng);

    eng->state.last_update_time = now;
    identity_update_rolling(eng, ev.valence, ev.arousal);

    /* V6 Phase 3: record an engine-authored speech event for the turn.
     * Captures the structured facts about what the character just did
     * (selected speech act, response act, intent, stance, target actor,
     * target topic, surfaced memory, audit result, output hash) — never
     * the rendered prose itself, so the memory firewall holds. */
    {
        pe_speech_event_t sev;
        memset(&sev, 0, sizeof(sev));
        sev.turn_count          = eng->state.turn_count;
        sev.clock_ms            = pe_clock_now_ms();
        sev.target_actor_id     = eng->relation.user_hash;
        sev.selected_memory_id  = (eng->plan.callback_memory != 0xFFFF)
                                ? eng->plan.callback_memory : 0u;
        sev.output_hash         = persona_hash(out);
        sev.target_topic_id     = (eng->plan.target_topic != 0xFFFF)
                                ? eng->plan.target_topic
                                : eng->primary_topic;
        sev.template_id         = (uint16_t)eng->last_template_group;
        sev.render_id           = (uint16_t)(sev.output_hash & 0xFFFFu);
        sev.speech_act          = eng->state.sovereign_override ? PE_SA_REDIRECT : pe_speech_act_from_intent(eng->state.current_intent);
        sev.response_act        = PE_SA_NONE;          /* Phase 5 will derive */
        sev.intent_id           = (uint8_t)eng->state.current_intent;
        sev.stance              = (uint8_t)eng->plan.stance;
        sev.rhetorical_mode     = (uint8_t)eng->plan.rhetorical_mode;
        sev.defense_mode        = 0;                   /* Phase 5 */
        sev.repair_mode         = 0;                   /* Phase 5 */
        sev.audit_result        = eng->last_audit_result;
        sev.withheld_intent     = PE_SA_NONE;
        sev.regret_marker       = 0;                   /* set later */

        /* V6 Phase 5c: refusal/withhold reason. When the character's own
         * speech act is a withhold (refusal/pause/evasion/withdrawal/
         * deflection), tag WHY using the current canonical state. This
         * makes "you avoided this before" a true engine assertion — a
         * later actor can read the ledger and see the reason.
         *
         * Selection is deterministic and reads only engine-canonical
         * fields (no renderer output). First-match wins:
         *   FATIGUE  — exhaustion is the highest pressure on speech;
         *   SHAME    — feared_gap suggests withdrawal-as-protection;
         *   CONFUSION — surprise_last says we did not understand;
         *   DISTRUST — relation_dims.trust is low;
         *   STRATEGY — rhetorical_mode is DEFLECT (chosen, not forced);
         *   PRIVACY  — default catchall for refusals without a stronger
         *              triggering signal.  */
        if (pe_speech_act_is_withhold(sev.speech_act)){
            sev.withheld_intent =
                (eng->input_class == 2) ? PE_SA_QUESTION :
                (eng->input_class == 3) ? PE_SA_ASSERTION :
                (eng->input_class == 4) ? PE_SA_DISCLOSURE :
                                          PE_SA_ASSERTION;
            sev.withhold_reason = pe_withhold_reason_from_frame(eng, &eng->frame);
        } else {
            sev.withheld_intent = PE_SA_NONE;
            sev.withhold_reason = PE_WR_NONE;
        }
        sev.expression_policy =
            expression_policy_decide(eng->state.mood, &eng->relation_dims,
                                     eng->identity.expression_mask_threshold,
                                     sev.withhold_reason);
        eng->expression_policy = sev.expression_policy;
        eng->frame.withhold_reason = sev.withhold_reason;

        pe_speech_ledger_record(&eng->speech_ledger, &sev);
        pe_open_loops_expire_to(&eng->open_loops, eng->state.turn_count);
        if (sev.target_topic_id != 0xFFFFu){
            if (eng->input_class == 3 && pe_speech_act_is_withhold(sev.speech_act)){
                pe_open_loops_record(&eng->open_loops, sev.target_actor_id,
                                     sev.target_topic_id, PE_SA_ASSERTION,
                                     720, 360, 420,
                                     eng->state.turn_count,
                                     eng->state.turn_count + 96u);
            }
            if (sev.speech_act == PE_SA_EVASION
                || sev.speech_act == PE_SA_DEFLECTION
                || sev.speech_act == PE_SA_WITHDRAWAL
                || sev.speech_act == PE_SA_PAUSE){
                pe_open_loops_record(&eng->open_loops, sev.target_actor_id,
                                     sev.target_topic_id,
                                     eng->input_class == 3 ? PE_SA_ASSERTION : PE_SA_DISCLOSURE,
                                     520, 420, 520,
                                     eng->state.turn_count,
                                     eng->state.turn_count + 96u);
            }
            if (sev.speech_act == PE_SA_PROMISE){
                pe_open_loops_record(&eng->open_loops, sev.target_actor_id,
                                     sev.target_topic_id, PE_SA_PROMISE,
                                     780, 300, 120,
                                     eng->state.turn_count,
                                     eng->state.turn_count + 160u);
            }
            if (eng->input_class == 2){
                pe_open_loops_record(&eng->open_loops, sev.target_actor_id,
                                     sev.target_topic_id, PE_SA_CORRECTION,
                                     640, 160, 260,
                                     eng->state.turn_count,
                                     eng->state.turn_count + 80u);
            }
            if (reply_has_question(out)){
                const pe_open_loop_t *recent_question =
                    pe_open_loops_latest_for_actor(&eng->open_loops, sev.target_actor_id);
                uint16_t recent_pressure =
                    pe_open_loop_pressure(recent_question, eng->state.turn_count);
                if (recent_pressure > 220u)
                    goto skip_question_open_loop;
                pe_open_loops_record(&eng->open_loops, sev.target_actor_id,
                                     sev.target_topic_id, PE_SA_QUESTION,
                                     460, 80, 80,
                                     eng->state.turn_count,
                                     eng->state.turn_count + 48u);
            }
skip_question_open_loop:;
        }
        if (eng->input_class == 3 && !pe_speech_act_is_withhold(sev.speech_act)){
            pe_open_loops_resolve_topic(&eng->open_loops,
                                        sev.target_actor_id,
                                        sev.target_topic_id,
                                        eng->state.turn_count);
        }

        /* V6 Phase 5b: typed dissonance ticks per turn (slow decay
         * toward 0) and shifts by speech-act class — evasion/deflection/
         * withdrawal raise ideal_gap; apology/concession close ought_gap;
         * insult/threat raise feared_gap. The arousal of the input event
         * carries the magnitude. Phase 5c will read these gaps to choose
         * resolution paths (confess, rationalize, deny, repair). */
        pe_dissonance_decay_to(&eng->dissonance, eng->state.turn_count);
        pe_dissonance_update_from_speech(&eng->dissonance,
                                         sev.speech_act,
                                         (uint8_t)(ev.arousal > 0 ? ev.arousal : 0));
    }

    pe_packet_trace_write(eng, input_text, &packet_it,
                          render_backend_name, raw_model_output,
                          renderer_output, out,
                          pre_epi_count, pre_next_memory_id,
                          pre_speech_count, pre_open_total,
                          pre_habit_turns);

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
