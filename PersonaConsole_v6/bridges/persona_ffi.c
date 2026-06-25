/* persona_ffi.c — opaque-handle wrappers around the PersonaConsole engine.
 *
 * Hides the Engine struct from external callers.  Allocates the engine on
 * the heap so the public handle is a true void*-style opaque pointer.
 * Heap allocation is fine here: it happens at session open/close, never
 * inside ps_reply.  The engine's "no malloc in process_input" discipline
 * is preserved.
 */
#include "persona_ffi.h"
#include "persona.h"
#include "persona_internal.h"
#include "reflection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

struct PersonaSession {
    Engine eng;
    char   user_id[64];
};

static const char *intent_name(uint16_t i){
    static const char *NAMES[] = {
        "answer","evade","accuse","flatter","threaten","probe",
        "redirect","monologue","reminisce","withdraw","joke","boast",
        "initiate","attend","clarify","pause"
    };
    if (i < (sizeof(NAMES)/sizeof(NAMES[0]))) return NAMES[i];
    return "unknown";
}

static const char *rhet_name(uint16_t r){
    static const char *NAMES[] = {
        "assert","hedge","deflect","escalate","lament","gloat",
        "indict","romanticize","intone","confess"
    };
    if (r < (sizeof(NAMES)/sizeof(NAMES[0]))) return NAMES[r];
    return "unknown";
}

static const char *private_thought_name(uint8_t k){
    static const char *NAMES[] = {
        "none","attend_user","open_loop","dissonance",
        "obsession","fatigue","withhold"
    };
    if (k < (sizeof(NAMES)/sizeof(NAMES[0]))) return NAMES[k];
    return "unknown";
}

PersonaSession* ps_open(const char *cartridge_path){
    if (!cartridge_path) return NULL;
    PersonaSession *s = (PersonaSession*)calloc(1, sizeof(PersonaSession));
    if (!s) return NULL;
    if (persona_open(&s->eng, cartridge_path) != 0){
        free(s);
        return NULL;
    }
    snprintf(s->user_id, sizeof(s->user_id), "%s", "Someone");
    persona_set_user(&s->eng, s->user_id);
    return s;
}

void ps_close(PersonaSession *s){
    if (!s) return;
    persona_save(&s->eng);
    persona_close(&s->eng);
    free(s);
}

int ps_set_user(PersonaSession *s, const char *user_id){
    if (!s || !user_id) return -1;
    snprintf(s->user_id, sizeof(s->user_id), "%s", user_id);
    return persona_set_user(&s->eng, s->user_id);
}

int ps_reply(PersonaSession *s, const char *input,
             char *out_buf, int out_buf_size){
    if (!s || !input || !out_buf || out_buf_size <= 0) return -1;
    return persona_process_input(&s->eng, s->user_id, input,
                                 out_buf, (size_t)out_buf_size);
}

static const char *topic_name_by_id(const Engine *eng, uint16_t topic_id){
    if (topic_id == 0xFFFF) return "";
    for (uint32_t i = 0; i < eng->topics.count; ++i){
        if (eng->topics.topics[i].id == topic_id)
            return eng->topics.topics[i].name;
    }
    return "";
}

static const char *idle_topic_name(const Engine *eng){
    const char *planned = topic_name_by_id(eng, eng->state.last_target_topic);
    if (planned[0]) return planned;

    uint16_t best_topic = 0xFFFF;
    uint16_t best_momentum = 0;
    for (int i = 0; i < PE_TOPIC_SLOTS; ++i){
        const TopicState *ts = &eng->state.topic_momentum[i];
        if (ts->topic_id != 0xFFFF && ts->momentum > best_momentum){
            best_topic = ts->topic_id;
            best_momentum = ts->momentum;
        }
    }
    if (best_topic == 0xFFFF || best_momentum < 180) return "";
    return topic_name_by_id(eng, best_topic);
}

static const char *unresolved_resurface(const Engine *eng, char *buf, size_t cap){
    if (!eng || !buf || cap == 0) return NULL;
    const pe_open_loop_t *loop = pe_open_loops_latest_active(&eng->open_loops);
    uint16_t topic_id = loop ? loop->target_topic_id : 0xFFFFu;
    if (topic_id == 0xFFFFu && eng->state.unresolved_count > 0){
        uint8_t idx = (uint8_t)((eng->state.unresolved_head + 7u) % 8u);
        topic_id = eng->state.unresolved_threads[idx];
    }
    if (topic_id == 0xFFFF) return NULL;
    const char *topic = topic_name_by_id(eng, topic_id);
    if (!topic[0]) topic = "what I almost said";
    snprintf(buf, cap, "I still owe you the rest of what I almost said about %s.", topic);
    return buf;
}

static uint32_t idle_pool_pick(const Engine *eng, uint32_t seed,
                               const char *topic, uint32_t count){
    if (!count) return 0;
    uint32_t h = seed ^ (seed >> 16);
    h ^= (eng->state.turn_count + 1u) * 2246822519u;
    h ^= ((uint32_t)(int32_t)eng->state.mood + 1009u) * 3266489917u;
    h ^= (uint32_t)(eng->state.turns_since_question + 3u) * 668265263u;
    if (topic){
        for (const unsigned char *p = (const unsigned char*)topic; *p; ++p)
            h = (h ^ *p) * 16777619u;
    }
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h % count;
}

int ps_idle_probe(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    const Engine *eng = &s->eng;
    if (eng->state.turn_count == 0) {
        out_buf[0] = 0;
        return 0;
    }

    const char *topic = idle_topic_name(eng);
    uint32_t address_modulus = pe_allow_intimate_address(eng) ? PE_ADDRESS_COUNT : 2u;
    const char *address = eng->identity.address_user_as[
        (eng->state.today_seed ^ eng->state.turn_count) % address_modulus
    ];
    if (!address[0]) address = "you";

    uint32_t seed = eng->state.today_seed
                  ^ (eng->state.turn_count * 2654435761u)
                  ^ ((uint32_t)eng->state.mood << 3);

    char unresolved[PE_TEMPLATE_TEXT];
    const char *force_unresolved = getenv("PE_FORCE_UNRESOLVED_THREAD");
    if ((force_unresolved && force_unresolved[0]) || ((seed >> 8) & 1u)){
        if (unresolved_resurface(eng, unresolved, sizeof(unresolved)))
            return snprintf(out_buf, (size_t)out_buf_size, "%s", unresolved);
    }

    const Template *candidates[32];
    uint32_t count = 0;
    uint16_t topic_group = 0xFFFFu;
    if (topic && topic[0]){
        for (uint32_t i = 0; i < eng->topics.count; ++i){
            if (!strcmp(eng->topics.topics[i].name, topic)){
                topic_group = eng->topics.topics[i].id;
                break;
            }
        }
    }
    for (uint32_t pass = 0; pass < 2 && count == 0; ++pass){
        for (uint32_t i = 0; i < eng->templates.count && count < 32; ++i){
            const Template *t = &eng->templates.entries[i];
            if (!t->text[0]) continue;
            if (t->intent != PE_INTENT_INITIATE &&
                t->intent != PE_INTENT_PROBE &&
                t->intent != PE_INTENT_CLARIFY)
                continue;
            if (pass == 0 && topic_group != 0xFFFFu &&
                t->group != topic_group && t->group != 0xFFFFu)
                continue;
            candidates[count++] = t;
        }
    }

    const char *line = NULL;
    if (count)
        line = candidates[idle_pool_pick(eng, seed, topic, count)]->text;
    if (!line || !line[0])
        line = "I have a thought forming. Tell me where you want to begin.";

    char tmp[PE_TEMPLATE_TEXT];
    const char *slot = strstr(line, "{address}");
    if (slot) {
        size_t head = (size_t)(slot - line);
        snprintf(tmp, sizeof(tmp), "%.*s%s%s",
                 (int)head, line, address, slot + 9);
        line = tmp;
    }
    return snprintf(out_buf, (size_t)out_buf_size, "%s", line);
}

int ps_save(PersonaSession *s){
    if (!s) return -1;
    return persona_save(&s->eng);
}

int ps_load(PersonaSession *s, const char *new_cart){
    if (!s || !new_cart) return -1;
    char saved_user[64];
    snprintf(saved_user, sizeof(saved_user), "%s", s->user_id);
    persona_save(&s->eng);
    persona_close(&s->eng);
    memset(&s->eng, 0, sizeof(s->eng));
    if (persona_open(&s->eng, new_cart) != 0) return -1;
    snprintf(s->user_id, sizeof(s->user_id), "%s", saved_user);
    persona_set_user(&s->eng, s->user_id);
    return 0;
}

int ps_name(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    return snprintf(out_buf, (size_t)out_buf_size, "%s",
                    s->eng.identity.character_name);
}

int ps_char_dir(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    return snprintf(out_buf, (size_t)out_buf_size, "%s", s->eng.char_dir);
}

static const char *profile_slug_from_dir(const char *path){
    const char *end;
    const char *start;
    static char slug[64];
    if (!path || !path[0]) return "";
    end = path + strlen(path);
    while (end > path && (end[-1] == '/' || end[-1] == '\\')) --end;
    start = end;
    while (start > path && start[-1] != '/' && start[-1] != '\\') --start;
    {
        size_t n = (size_t)(end - start);
        if (n >= sizeof(slug)) n = sizeof(slug) - 1;
        memcpy(slug, start, n);
        slug[n] = 0;
    }
    return slug;
}

int ps_state(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    const Engine *eng = &s->eng;
    const char *today_label = "";
    uint32_t lk_count = eng->learned_knowledge.header.entry_count;
    uint16_t lk_max_confidence = 0;
    uint8_t lk_best_status = 0;
    if (eng->state.today_index < eng->todays.count)
        today_label = eng->todays.entries[eng->state.today_index].label;
    for (uint32_t i = 0; i < lk_count && i < PE_LK_RECORD_CAP; ++i){
        const pe_lk_record_t *r = &eng->learned_knowledge.records[i];
        if (!r->record_id) continue;
        if (r->confidence >= lk_max_confidence){
            lk_max_confidence = r->confidence;
            lk_best_status = r->status;
        }
    }

    int n = snprintf(out_buf, (size_t)out_buf_size,
        "{\"name\":\"%s\","
        "\"profile_slug\":\"%s\","
        "\"mood\":%d,"
        "\"intent\":\"%s\","
        "\"rhetorical_mode\":\"%s\","
        "\"today\":\"%s\","
        "\"turn_count\":%u,"
        "\"intoxication\":%d,"
        "\"exhaustion\":%d,"
        "\"acute_spike\":%d,"
        "\"obsession_pressure\":%d,"
        "\"voice_delta\":%d,"
        "\"voice_choice\":%u,"
        "\"last_template_group\":%u,"
        "\"last_template_intent\":\"%s\","
        "\"target_topic\":%u,"
        "\"last_callback_memory\":%u,"
        "\"unresolved_count\":%u,"
        "\"open_loop_count\":%u,"
        "\"open_loop_resolved_count\":%u,"
        "\"open_loop_expired_count\":%u,"
        "\"turns_since_question\":%u,"
        "\"last_reply_had_question\":%u,"
        "\"habit_question_bias\":%u,"
        "\"habit_brevity_bias\":%u,"
        "\"habit_initiative_bias\":%u,"
        "\"habit_fatigue_bias\":%u,"
        "\"habit_fatigue_count\":%u,"
        "\"habit_avg_reply_words\":%u,"
        "\"habit_turns_observed\":%u,"
        "\"habit_no_question_streak\":%u,"
        "\"want_ages\":[%u,%u,%u],"
        "\"drive_values\":[%d,%d,%d,%d,%d,%d,%d,%d],"
        "\"disposition\":%d,"
        "\"user_id\":\"%s\","
        "\"actor_tagged_memories\":%u,"
        "\"speech_event_count\":%u,"
        "\"last_speech_act\":\"%s\","
        "\"last_withhold_reason\":\"%s\","
        "\"last_withheld_intent\":\"%s\","
        "\"last_expression_policy\":\"%s\","
        "\"last_audit_result\":%u,"
        "\"last_audit_violation\":%u,"
        "\"last_audit_hardness\":%u,"
        "\"last_audit_rewrite\":%u,"
        "\"cold_open_callback_source\":%u,"
        "\"cold_open_callback_template_index\":%u,"
        "\"cold_open_callback_exclusive\":%u,"
        "\"cold_open_callback_topic\":%u,"
        "\"cold_open_callback_memory_index\":%u,"
        "\"private_thought\":{\"kind\":\"%s\",\"expressed\":\"%s\",\"expression\":\"%s\","
                  "\"topic\":%u,\"pressure\":%u,\"withheld\":%u,"
                  "\"internal_hash\":%u,\"expressed_hash\":%u},"
        "\"frame\":{\"actor_id\":%u,\"input_class\":%u,\"primary_topic\":%u,"
                  "\"selected_goal\":%u,\"selected_intent\":\"%s\","
                  "\"speech_act\":\"%s\",\"stance\":%u,\"rhetorical_mode\":\"%s\","
                  "\"recall_mode\":\"%s\",\"open_loop_pressure\":%u,"
                  "\"ideal_gap\":%u,\"ought_gap\":%u,\"feared_gap\":%u,"
                  "\"max_words\":%u,\"require_question\":%u,\"allow_empty\":%u,"
                  "\"fatigue_term_count\":%u},"
        "\"relation_dims\":{\"trust\":%u,\"threat\":%u,\"intimacy\":%u,"
                          "\"resentment\":%u,\"dependency\":%u,\"obligation\":%u,"
                          "\"envy\":%u,\"admiration\":%u,\"embarrassment\":%u},"
        "\"learned_knowledge\":{\"count\":%u,\"max_confidence\":%u,\"best_status\":%u},"
        "\"theory_of_mind\":{\"believed_valence\":%d,\"believed_arousal\":%d,"
                          "\"believed_goal_topic\":%u,\"confidence\":%u,"
                          "\"stale_turns\":%u,\"mismatch_count\":%u},"
        "\"dissonance\":{\"ideal_gap\":%u,\"ought_gap\":%u,\"feared_gap\":%u,"
                      "\"ideal_self_model\":%d,\"ought_self_model\":%d,"
                      "\"feared_self_model\":%d},"
        "\"recall_mode\":\"%s\","
        "\"schema\":{\"trustworthy\":%d,\"hostile\":%d,\"intimate\":%d,"
                    "\"competent\":%d,\"deceptive\":%d,\"owed\":%d,"
                    "\"owes\":%d,\"dignity\":%d}}",
        eng->identity.character_name,
        profile_slug_from_dir(eng->char_dir),
        eng->state.mood,
        intent_name(eng->state.current_intent),
        rhet_name(eng->state.last_rhetorical_mode),
        today_label,
        eng->state.turn_count,
        eng->state.intoxication,
        eng->state.exhaustion,
        eng->state.acute_spike,
        eng->state.obsession_pressure,
        eng->state.last_voice_delta,
        eng->state.last_voice_choice,
        eng->last_template_group,
        intent_name(eng->last_template_intent),
        (unsigned)eng->state.last_target_topic,
        (unsigned)eng->state.last_callback_memory,
        (unsigned)eng->state.unresolved_count,
        (unsigned)pe_open_loops_count(&eng->open_loops),
        (unsigned)pe_open_loops_count_status(&eng->open_loops, PE_OL_RESOLVED),
        (unsigned)pe_open_loops_count_status(&eng->open_loops, PE_OL_EXPIRED),
        (unsigned)eng->state.turns_since_question,
        (unsigned)eng->state.last_reply_had_question,
        (unsigned)eng->speech_habits.question_bias,
        (unsigned)eng->speech_habits.brevity_bias,
        (unsigned)eng->speech_habits.initiative_bias,
        (unsigned)eng->speech_habits.fatigue_bias,
        (unsigned)eng->speech_habits.fatigue_count,
        (unsigned)(eng->speech_habits.avg_reply_words_q8 >> 8),
        (unsigned)eng->speech_habits.turns_observed,
        (unsigned)eng->speech_habits.no_question_streak,
        (unsigned)eng->state.want_turns_since_engaged[0],
        (unsigned)eng->state.want_turns_since_engaged[1],
        (unsigned)eng->state.want_turns_since_engaged[2],
        (int)eng->state.drive_values[0],
        (int)eng->state.drive_values[1],
        (int)eng->state.drive_values[2],
        (int)eng->state.drive_values[3],
        (int)eng->state.drive_values[4],
        (int)eng->state.drive_values[5],
        (int)eng->state.drive_values[6],
        (int)eng->state.drive_values[7],
        eng->relation.disposition,
        s->user_id,
        (unsigned)pe_actor_index_count(&eng->actor_index),
        (unsigned)pe_speech_ledger_count(&eng->speech_ledger),
        pe_speech_act_name(pe_speech_ledger_last(&eng->speech_ledger)
                           ? pe_speech_ledger_last(&eng->speech_ledger)->speech_act
                           : PE_SA_NONE),
        pe_withhold_reason_name(pe_speech_ledger_last(&eng->speech_ledger)
                                ? pe_speech_ledger_last(&eng->speech_ledger)->withhold_reason
                                : PE_WR_NONE),
        pe_speech_act_name(pe_speech_ledger_last(&eng->speech_ledger)
                           ? pe_speech_ledger_last(&eng->speech_ledger)->withheld_intent
                           : PE_SA_NONE),
        pe_expression_policy_name(pe_speech_ledger_last(&eng->speech_ledger)
                                  ? pe_speech_ledger_last(&eng->speech_ledger)->expression_policy
                                  : PE_EXPR_GENUINE),
        (unsigned)eng->last_audit_result,
        (unsigned)eng->last_audit_violation,
        (unsigned)eng->last_audit_hardness,
        (unsigned)eng->last_audit_rewrite,
        (unsigned)eng->cold_open_callback_source,
        (unsigned)eng->cold_open_callback_template_index,
        (unsigned)eng->cold_open_callback_exclusive,
        (unsigned)eng->cold_open_callback_topic,
        (unsigned)eng->cold_open_callback_memory_index,
        private_thought_name(eng->private_thought_kind),
        pe_speech_act_name(eng->expressed_thought_kind),
        pe_expression_policy_name(eng->expression_policy),
        (unsigned)eng->private_thought_topic,
        (unsigned)eng->private_thought_pressure,
        (unsigned)eng->private_thought_withheld,
        (unsigned)eng->private_thought_hash,
        (unsigned)eng->expressed_thought_hash,
        (unsigned)eng->frame.actor_id,
        (unsigned)eng->frame.input_class,
        (unsigned)eng->frame.primary_topic,
        (unsigned)eng->frame.selected_goal,
        intent_name(eng->frame.selected_intent),
        pe_speech_act_name(eng->frame.speech_act),
        (unsigned)eng->frame.stance,
        rhet_name(eng->frame.rhetorical_mode),
        pe_recall_mode_name((uint8_t)eng->frame.recall_mode),
        (unsigned)eng->frame.open_loop_pressure,
        (unsigned)eng->frame.ideal_gap,
        (unsigned)eng->frame.ought_gap,
        (unsigned)eng->frame.feared_gap,
        (unsigned)eng->frame.max_words,
        (unsigned)eng->frame.require_question,
        (unsigned)eng->frame.allow_empty,
        (unsigned)eng->frame.fatigue_term_count,
        (unsigned)eng->relation_dims.trust,
        (unsigned)eng->relation_dims.threat,
        (unsigned)eng->relation_dims.intimacy,
        (unsigned)eng->relation_dims.resentment,
        (unsigned)eng->relation_dims.dependency,
        (unsigned)eng->relation_dims.obligation,
        (unsigned)eng->relation_dims.envy,
        (unsigned)eng->relation_dims.admiration,
        (unsigned)eng->relation_dims.embarrassment,
        (unsigned)lk_count,
        (unsigned)lk_max_confidence,
        (unsigned)lk_best_status,
        (int)eng->theory_of_mind.believed_valence,
        (int)eng->theory_of_mind.believed_arousal,
        (unsigned)eng->theory_of_mind.believed_goal_topic,
        (unsigned)eng->theory_of_mind.confidence,
        (unsigned)eng->theory_of_mind.stale_turns,
        (unsigned)eng->theory_of_mind.mismatch_count,
        (unsigned)eng->dissonance.ideal_gap,
        (unsigned)eng->dissonance.ought_gap,
        (unsigned)eng->dissonance.feared_gap,
        (int)eng->dissonance.ideal_self_model,
        (int)eng->dissonance.ought_self_model,
        (int)eng->dissonance.feared_self_model,
        pe_recall_mode_name(eng->current_recall_mode),
        (int)eng->schema.slot[SCHEMA_USER_TRUSTWORTHY],
        (int)eng->schema.slot[SCHEMA_USER_HOSTILE],
        (int)eng->schema.slot[SCHEMA_USER_INTIMATE],
        (int)eng->schema.slot[SCHEMA_USER_COMPETENT],
        (int)eng->schema.slot[SCHEMA_USER_DECEPTIVE],
        (int)eng->schema.slot[SCHEMA_RELATIONSHIP_OWED],
        (int)eng->schema.slot[SCHEMA_RELATIONSHIP_OWES],
        (int)eng->schema.slot[SCHEMA_SELF_DIGNITY]);
    return n;
}

static int json_escape_into(char *dst, int cap, const char *src){
    int p = 0;
    for (const char *c = src; *c && p + 8 < cap; ++c){
        unsigned char ch = (unsigned char)*c;
        switch (ch){
        case '"':  dst[p++]='\\'; dst[p++]='"';  break;
        case '\\': dst[p++]='\\'; dst[p++]='\\'; break;
        case '\n': dst[p++]='\\'; dst[p++]='n';  break;
        case '\r': dst[p++]='\\'; dst[p++]='r';  break;
        case '\t': dst[p++]='\\'; dst[p++]='t';  break;
        default:
            if (ch < 0x20) p += snprintf(dst + p, (size_t)(cap - p), "\\u%04x", ch);
            else dst[p++] = (char)ch;
        }
    }
    if (p < cap) dst[p] = 0;
    return p;
}

int ps_reflections(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    const Engine *eng = &s->eng;
    int pos = 0;
    pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                    "{\"count\":%u,\"reflections\":[",
                    (unsigned)eng->reflections.count);
    for (unsigned i = 0; i < eng->reflections.count && pos < out_buf_size - 64; ++i){
        const MemoryNode *m = &eng->reflections.memories[i];
        char text[256] = "";
        char esc[512] = "";
        pe_reflection_render(eng, m, text, (int)sizeof(text));
        json_escape_into(esc, (int)sizeof(esc), text);
        pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                        "%s{\"topic\":%u,\"sources\":%u,\"salience\":%u,"
                        "\"timestamp\":%u,\"text\":\"%s\"}",
                        i == 0 ? "" : ",",
                        (unsigned)m->topic_id,
                        (unsigned)eng->reflections.source_count[i],
                        (unsigned)m->salience,
                        (unsigned)m->timestamp,
                        esc);
    }
    pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos), "]}");
    return pos;
}

int ps_relationships(PersonaSession *s, char *out_buf, int out_buf_size){
    if (!s || !out_buf || out_buf_size <= 0) return -1;
    Engine *eng = &s->eng;
    char rel_dir[512];
    if (pe_path_join(rel_dir, sizeof(rel_dir), eng->char_dir, "relations") != 0)
        return -1;

    int pos = 0;
    int count = 0;
    pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                    "{\"relationships\":[");

    DIR *d = opendir(rel_dir);
    if (d){
        struct dirent *de;
        while ((de = readdir(d)) != NULL && pos < out_buf_size - 128){
            size_t len = strlen(de->d_name);
            if (len < 5 || strcmp(de->d_name + len - 4, ".bin")) continue;

            char path[512];
            if (pe_path_join(path, sizeof(path), rel_dir, de->d_name) != 0) continue;
            Relation r;
            if (pe_read_file(path, &r, sizeof(r)) != 0) continue;

            char name[128];
            char esc[256];
            snprintf(name, sizeof(name), "%s", r.known_as[0] ? r.known_as : "Someone");
            json_escape_into(esc, (int)sizeof(esc), name);
            pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                            "%s{\"user_hash\":%u,\"known_as\":\"%s\","
                            "\"disposition\":%d,\"tags\":%u,"
                            "\"first_contact\":%u,\"last_contact\":%u}",
                            count == 0 ? "" : ",",
                            (unsigned)r.user_hash,
                            esc,
                            (int)r.disposition,
                            (unsigned)r.tags,
                            (unsigned)r.first_contact,
                            (unsigned)r.last_contact);
            count++;
        }
        closedir(d);
    }
    pos += snprintf(out_buf + pos, (size_t)(out_buf_size - pos),
                    "],\"count\":%d}", count);
    return pos;
}
